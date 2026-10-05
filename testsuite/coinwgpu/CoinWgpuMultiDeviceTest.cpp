#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kSide = 16;
constexpr uint8_t kSentinel = 37;

bool check(bool condition, const char * description, const char * error = "") {
  if (!condition) {
    std::cerr << "CoinWgpuMultiDeviceTest: " << description;
    if (error && error[0]) std::cerr << ": " << error;
    std::cerr << '\n';
  }
  return condition;
}

CoinWgpuFrameView clearFrame(uint8_t red, uint8_t green, uint8_t blue) {
  CoinWgpuFrameView frame{};
  frame.abi_version = COIN_WGPU_ABI_VERSION;
  frame.struct_size = sizeof(frame);
  frame.width = kSide;
  frame.height = kSide;
  frame.clear_color[0] = red / 255.0f;
  frame.clear_color[1] = green / 255.0f;
  frame.clear_color[2] = blue / 255.0f;
  frame.clear_color[3] = 1.0f;
  return frame;
}

CoinWgpuTarget targetFor(CoinWgpuDeviceId id, std::vector<uint8_t> * color = nullptr) {
  CoinWgpuTarget target{};
  target.width = kSide;
  target.height = kSide;
  target.device_id = id;
  if (color) {
    target.color_buffer = color->data();
    target.color_buffer_len = color->size();
  }
  return target;
}

bool colorIs(const std::vector<uint8_t> & pixels,
             uint8_t red, uint8_t green, uint8_t blue) {
  if (pixels.size() != kSide * kSide * 4u) return false;
  for (size_t i = 0; i < pixels.size(); i += 4) {
    if (std::abs(int(pixels[i]) - int(red)) > 2 ||
        std::abs(int(pixels[i + 1]) - int(green)) > 2 ||
        std::abs(int(pixels[i + 2]) - int(blue)) > 2 ||
        pixels[i + 3] != 255) return false;
  }
  return true;
}

bool unchanged(const std::vector<uint8_t> & pixels) {
  return std::all_of(pixels.begin(), pixels.end(),
                     [](uint8_t value) { return value == kSentinel; });
}

bool submitClear(CoinWgpuDeviceId id, uint8_t red, uint8_t green, uint8_t blue) {
  std::vector<uint8_t> color(kSide * kSide * 4u, kSentinel);
  CoinWgpuTarget target = targetFor(id, &color);
  const CoinWgpuFrameView frame = clearFrame(red, green, blue);
  char error[512] = {};
  const CoinWgpuStatus status = coin_wgpu_submit(&target, &frame, error, sizeof(error));
  return check(status == COIN_WGPU_OK && target.submission_serial != 0 &&
               colorIs(color, red, green, blue), "independent synchronous render", error);
}

bool createDevice(CoinWgpuDeviceId & id) {
  char error[512] = {};
  id = 0;
  const CoinWgpuStatus status = coin_wgpu_device_create(&id, error, sizeof(error));
  return check(status == COIN_WGPU_OK && id != 0, "create extra device", error);
}

bool submitAsync(CoinWgpuDeviceId id, const CoinWgpuFrameView & frame,
                 CoinWgpuReadbackTicket & ticket) {
  // Neither the target nor the frame lives until readback. Tickets must own
  // their staging resources independently of both callers' stack frames.
  CoinWgpuTarget target = targetFor(id);
  ticket = CoinWgpuReadbackTicket{};
  ticket.abi_version = COIN_WGPU_ABI_VERSION;
  ticket.struct_size = sizeof(ticket);
  char error[512] = {};
  const CoinWgpuStatus status = coin_wgpu_submit_async(
      &target, &frame, &ticket, error, sizeof(error));
  return check(status == COIN_WGPU_OK && ticket.token != 0 &&
               ticket.submission_serial == target.submission_serial &&
               ticket.color_bytes == kSide * kSide * 4u,
               "submit independent asynchronous readback", error);
}

bool pollColor(const CoinWgpuReadbackTicket & ticket,
               uint8_t red, uint8_t green, uint8_t blue) {
  std::vector<uint8_t> color(kSide * kSide * 4u, kSentinel);
  char error[512] = {};
  for (int attempt = 0; attempt < 10000; ++attempt) {
    const CoinWgpuStatus status = coin_wgpu_readback_poll(
        ticket.token, color.data(), color.size(), nullptr, 0, error, sizeof(error));
    if (status == COIN_WGPU_NOT_READY) {
      if (!check(unchanged(color), "pending ticket published pixels")) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    return check(status == COIN_WGPU_OK && colorIs(color, red, green, blue),
                 "readback belongs to its submitting device", error);
  }
  return check(false, "readback did not complete within ten seconds");
}

bool ticketInvalidWithoutWrite(const CoinWgpuReadbackTicket & ticket) {
  std::vector<uint8_t> color(kSide * kSide * 4u, kSentinel);
  char error[512] = {};
  const CoinWgpuStatus status = coin_wgpu_readback_poll(
      ticket.token, color.data(), color.size(), nullptr, 0, error, sizeof(error));
  return check((status == COIN_WGPU_DEVICE_LOST ||
                status == COIN_WGPU_INVALID_ARGUMENT) && unchanged(color),
               "invalidated ticket wrote pixels or returned an untyped status", error);
}

bool rejectedWithoutWrite(CoinWgpuDeviceId id, const CoinWgpuFrameView & frame,
                          CoinWgpuStatus expected) {
  std::vector<uint8_t> color(kSide * kSide * 4u, kSentinel);
  CoinWgpuTarget target = targetFor(id, &color);
  char error[512] = {};
  const CoinWgpuStatus status = coin_wgpu_submit(&target, &frame, error, sizeof(error));
  return check(status == expected && unchanged(color) && target.submission_serial == 0,
               "rejected submit changed output or serial", error);
}

bool waitForNoRtt() {
  uint64_t active = 0, retired = 0;
  for (int attempt = 0; attempt < 10000; ++attempt) {
    coin_wgpu_rtt_resource_counts(&active, &retired);
    if (active == 0 && retired == 0) return true;
    coin_wgpu_poll_device();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::cerr << "CoinWgpuMultiDeviceTest: RTT resources did not retire; active="
            << active << " retired=" << retired << '\n';
  return false;
}

bool invalidHandles() {
  char error[512] = {};
  if (!check(coin_wgpu_device_create(nullptr, error, sizeof(error)) ==
                 COIN_WGPU_INVALID_ARGUMENT,
             "null device-create output was accepted", error)) return false;

  const CoinWgpuDeviceId unknown = UINT64_MAX;
  const CoinWgpuFrameView frame = clearFrame(90, 80, 70);
  if (!rejectedWithoutWrite(unknown, frame, COIN_WGPU_INVALID_ARGUMENT)) return false;

  CoinWgpuTarget target = targetFor(unknown);
  CoinWgpuReadbackTicket ticket{};
  ticket.abi_version = COIN_WGPU_ABI_VERSION;
  ticket.struct_size = sizeof(ticket);
  error[0] = '\0';
  const CoinWgpuStatus asyncStatus = coin_wgpu_submit_async(
      &target, &frame, &ticket, error, sizeof(error));
  if (!check(asyncStatus == COIN_WGPU_INVALID_ARGUMENT && ticket.token == 0 &&
             target.submission_serial == 0,
             "unknown-device async submit was accepted", error)) return false;

  uint64_t rttToken = 0;
  error[0] = '\0';
  const CoinWgpuStatus textureStatus = coin_wgpu_submit_texture(
      &target, &frame, &rttToken, error, sizeof(error));
  if (!check(textureStatus == COIN_WGPU_INVALID_ARGUMENT && rttToken == 0 &&
             target.submission_serial == 0,
             "unknown-device RTT submit was accepted", error)) return false;

  CoinWgpuReadbackTicket unaffected{};
  if (!submitAsync(0, frame, unaffected)) return false;
  coin_wgpu_device_destroy(unknown);
  coin_wgpu_inject_device_fault(unknown, COIN_WGPU_DEVICE_LOST);
  return pollColor(unaffected, 90, 80, 70) && submitClear(0, 90, 80, 70);
}

bool lifecycleAndIsolation() {
  CoinWgpuDeviceId first = 0, second = 0;
  if (!createDevice(first) || !createDevice(second)) return false;
  if (!check(first != second, "two created devices share an id") ||
      !submitClear(0, 220, 20, 20) ||
      !submitClear(first, 20, 220, 20) ||
      !submitClear(second, 20, 20, 220)) return false;

  const CoinWgpuFrameView red = clearFrame(230, 10, 10);
  const CoinWgpuFrameView green = clearFrame(10, 230, 10);
  const CoinWgpuFrameView blue = clearFrame(10, 10, 230);
  CoinWgpuReadbackTicket doomed{}, survivor{};
  if (!submitAsync(first, green, doomed) ||
      !submitAsync(second, blue, survivor)) return false;
  coin_wgpu_device_destroy(first);
  coin_wgpu_device_destroy(first); // Stale id must not affect a live peer.
  if (!ticketInvalidWithoutWrite(doomed) ||
      !pollColor(survivor, 10, 10, 230) ||
      !rejectedWithoutWrite(first, red, COIN_WGPU_INVALID_ARGUMENT) ||
      !submitClear(second, 20, 20, 220) ||
      !submitClear(0, 220, 20, 20)) return false;

  CoinWgpuDeviceId replacement = 0;
  if (!createDevice(replacement) ||
      !check(replacement != first && replacement != second,
             "destroyed device id was reused while stale handles exist")) return false;

  // A device-local OOM affects its own output only. A healthy peer and the
  // default context remain usable before any recovery of the faulty context.
  coin_wgpu_inject_device_fault(replacement, COIN_WGPU_OUT_OF_MEMORY);
  if (!rejectedWithoutWrite(replacement, red, COIN_WGPU_OUT_OF_MEMORY) ||
      !submitClear(second, 20, 20, 220) ||
      !submitClear(0, 220, 20, 20) ||
      !submitClear(replacement, 230, 10, 10)) return false;

  CoinWgpuReadbackTicket lost{}, unaffected{};
  if (!submitAsync(replacement, red, lost) ||
      !submitAsync(second, blue, unaffected)) return false;
  coin_wgpu_inject_device_fault(replacement, COIN_WGPU_DEVICE_LOST);
  if (!rejectedWithoutWrite(replacement, green, COIN_WGPU_DEVICE_LOST) ||
      !ticketInvalidWithoutWrite(lost) ||
      !pollColor(unaffected, 10, 10, 230) ||
      !submitClear(second, 20, 20, 220)) return false;
  CoinWgpuReadbackTicket recovered{};
  if (!submitAsync(replacement, green, recovered) ||
      !check(recovered.generation > lost.generation,
             "device recovery did not advance its ticket generation") ||
      !pollColor(recovered, 10, 230, 10)) return false;

  coin_wgpu_device_destroy(replacement);
  coin_wgpu_device_destroy(second);
  return submitClear(0, 220, 20, 20);
}

bool rttOwnership() {
  if (!waitForNoRtt()) return false;
  CoinWgpuDeviceId owner = 0, foreign = 0;
  if (!createDevice(owner) || !createDevice(foreign)) return false;
  CoinWgpuFrameView producer = clearFrame(70, 130, 210);
  CoinWgpuTarget source = targetFor(owner);
  uint64_t token = 0;
  char error[512] = {};
  if (!check(coin_wgpu_submit_texture(&source, &producer, &token,
                                      error, sizeof(error)) == COIN_WGPU_OK && token != 0,
             "create device-owned RTT token", error)) return false;
  uint64_t active = 0, retired = 0;
  coin_wgpu_rtt_resource_counts(&active, &retired);
  if (!check(active >= 1, "new RTT was absent from resource accounting")) return false;

  CoinWgpuTexture texture{};
  texture.width = kSide;
  texture.height = kSide;
  texture.format = 1;
  texture.reserved = 1; // Opaque clear in producer.
  texture.content_digest = token;
  CoinWgpuFrameView consumer = clearFrame(4, 5, 6);
  consumer.textures = &texture;
  consumer.texture_count = 1;
  if (!rejectedWithoutWrite(foreign, consumer, COIN_WGPU_INVALID_ARGUMENT) ||
      !rejectedWithoutWrite(0, consumer, COIN_WGPU_INVALID_ARGUMENT) ||
      !submitClear(foreign, 30, 40, 50)) return false;
  std::vector<uint8_t> accepted(kSide * kSide * 4u, kSentinel);
  CoinWgpuTarget ownerTarget = targetFor(owner, &accepted);
  if (!check(coin_wgpu_submit(&ownerTarget, &consumer, error, sizeof(error)) == COIN_WGPU_OK &&
             colorIs(accepted, 4, 5, 6),
             "owner rejected its own RTT token", error)) return false;

  // Releasing a used token can defer retirement until the owner's GPU work
  // completes. Completing work on the owner must eventually drain it.
  coin_wgpu_release_texture(token);
  coin_wgpu_rtt_resource_counts(&active, &retired);
  if (!check(active == 0, "released RTT remained active")) return false;
  if (!rejectedWithoutWrite(owner, consumer, COIN_WGPU_INVALID_ARGUMENT)) return false;
  const CoinWgpuFrameView plain = clearFrame(8, 9, 10);
  CoinWgpuReadbackTicket ownerFence{};
  if (!submitAsync(owner, plain, ownerFence) ||
      !pollColor(ownerFence, 8, 9, 10) || !waitForNoRtt()) return false;

  // Device recovery must not resurrect a token from the previous generation.
  source = targetFor(owner);
  token = 0;
  error[0] = '\0';
  if (!check(coin_wgpu_submit_texture(&source, &producer, &token,
                                      error, sizeof(error)) == COIN_WGPU_OK && token != 0,
             "create RTT token before device loss", error)) return false;
  texture.content_digest = token;
  coin_wgpu_rtt_resource_counts(&active, &retired);
  if (!check(active >= 1, "live RTT was absent before device loss")) return false;
  coin_wgpu_inject_device_fault(owner, COIN_WGPU_DEVICE_LOST);
  if (!rejectedWithoutWrite(owner, plain, COIN_WGPU_DEVICE_LOST) ||
      !submitClear(foreign, 30, 40, 50) ||
      !submitClear(owner, 8, 9, 10) ||
      !rejectedWithoutWrite(owner, consumer, COIN_WGPU_INVALID_ARGUMENT) ||
      !waitForNoRtt()) return false;
  coin_wgpu_release_texture(token);

  // Destroying a context with a live RTT must release its resources without
  // requiring a completion serial from either surviving context.
  source = targetFor(owner);
  token = 0;
  error[0] = '\0';
  if (!check(coin_wgpu_submit_texture(&source, &producer, &token,
                                      error, sizeof(error)) == COIN_WGPU_OK && token != 0,
             "create RTT token before device destruction", error)) return false;
  coin_wgpu_rtt_resource_counts(&active, &retired);
  if (!check(active >= 1, "live RTT was absent before device destruction")) return false;
  coin_wgpu_device_destroy(owner);
  if (!waitForNoRtt()) return false;
  coin_wgpu_release_texture(token);
  coin_wgpu_device_destroy(foreign);
  return true;
}

bool cacheIsolation() {
  CoinWgpuDeviceId first = 0, second = 0;
  if (!createDevice(first) || !createDevice(second)) return false;

  // The same cache keys on three devices must each produce valid resources
  // owned by that device. Repeating the frame exercises cache hits as well.
  CoinWgpuVertex vertices[3] = {};
  const float positions[3][2] = {{-0.8f, -0.8f}, {0.8f, -0.8f}, {0.0f, 0.8f}};
  for (int i = 0; i < 3; ++i) {
    vertices[i].position[0] = positions[i][0];
    vertices[i].position[1] = positions[i][1];
    vertices[i].normal[2] = 1.0f;
    vertices[i].texcoord[0] = 0.5f;
    vertices[i].texcoord[1] = 0.5f;
  }
  const uint32_t indices[3] = {0, 1, 2};
  CoinWgpuDraw draw{};
  draw.topology = 0;
  draw.vertex_count = 3;
  draw.index_count = 3;
  draw.stable_node_id = 0xC01u;
  draw.source_revision = 1;
  CoinWgpuMaterial material{};
  material.diffuse[0] = 1.0f;
  material.diffuse[1] = 1.0f;
  material.diffuse[2] = 1.0f;
  material.diffuse[3] = 1.0f;
  CoinWgpuRenderState state{};
  state.polygon_offset_primitive_style = 1; // Valid triangle style, with offset disabled.
  for (unsigned i = 0; i < 4; ++i) {
    state.model_view[i * 5] = 1.0f;
    state.model_view_projection[i * 5] = 1.0f;
    state.normal_matrix[i * 5] = 1.0f;
    state.texture_matrix[i * 5] = 1.0f;
  }
  state.has_texture = 1;
  state.texture_model = 1; // REPLACE
  const uint8_t texel[4] = {240, 120, 80, 255};
  CoinWgpuTexture texture{};
  texture.width = 1;
  texture.height = 1;
  texture.format = 0;
  texture.content_digest = UINT64_C(0xC01C0FFEE);
  texture.pixels = texel;
  texture.pixel_bytes_len = sizeof(texel);
  CoinWgpuSampler sampler{};
  sampler.filter = 0;
  CoinWgpuFrameView frame = clearFrame(0, 0, 0);
  frame.vertices = vertices;
  frame.vertex_count = 3;
  frame.indices = indices;
  frame.index_count = 3;
  frame.draws = &draw;
  frame.draw_count = 1;
  frame.materials = &material;
  frame.material_count = 1;
  frame.states = &state;
  frame.state_count = 1;
  frame.textures = &texture;
  frame.texture_count = 1;
  frame.samplers = &sampler;
  frame.sampler_count = 1;
  for (int repetition = 0; repetition < 2; ++repetition) {
    const CoinWgpuDeviceId devices[3] = {first, second, 0};
    for (CoinWgpuDeviceId id : devices) {
      std::vector<uint8_t> pixels(kSide * kSide * 4u, kSentinel);
      CoinWgpuTarget target = targetFor(id, &pixels);
      char error[512] = {};
      const CoinWgpuStatus status = coin_wgpu_submit(&target, &frame, error, sizeof(error));
      const size_t center = (kSide / 2u * kSide + kSide / 2u) * 4u;
      if (!check(status == COIN_WGPU_OK && pixels[center] > 200 &&
                 pixels[center + 1] > 80 && pixels[center + 2] > 40,
                 "same geometry/texture keys failed on separate devices", error)) return false;
    }
  }
  coin_wgpu_device_destroy(first);
  coin_wgpu_device_destroy(second);
  return true;
}

bool validatedCameraSceneOwnership() {
  CoinWgpuDeviceId fast = 0, reference = 0;
  if (!createDevice(fast) || !createDevice(reference)) return false;
  const CoinWgpuVertex vertices[3] = {
    {{-0.75f, -0.75f, 0.0f}, {0, 0, 1}, {0, 0}, 0},
    {{ 0.75f, -0.75f, 0.0f}, {0, 0, 1}, {0, 0}, 0},
    {{ 0.00f,  0.75f, 0.0f}, {0, 0, 1}, {0, 0}, 0}
  };
  const uint32_t indices[3] = {0, 1, 2};
  CoinWgpuDraw draw{};
  draw.vertex_count = 3;
  draw.index_count = 3;
  draw.stable_node_id = 0xCA4Eu;
  draw.source_revision = 1;
  CoinWgpuMaterial material{};
  material.diffuse[0] = 1.0f;
  material.diffuse[1] = 0.2f;
  material.diffuse[2] = 0.1f;
  material.diffuse[3] = 1.0f;
  CoinWgpuRenderState state{};
  state.polygon_offset_primitive_style = 1; // Valid triangle style, with offset disabled.
  for (unsigned i = 0; i < 4; ++i) {
    state.model_view[i * 5] = 1.0f;
    state.model_view_projection[i * 5] = 1.0f;
    state.normal_matrix[i * 5] = 1.0f;
    state.texture_matrix[i * 5] = 1.0f;
  }
  CoinWgpuFrameView frame = clearFrame(0, 0, 0);
  frame.frame_revision = 41001;
  frame.vertices = vertices;
  frame.vertex_count = 3;
  frame.indices = indices;
  frame.index_count = 3;
  frame.draws = &draw;
  frame.draw_count = 1;
  frame.materials = &material;
  frame.material_count = 1;
  frame.states = &state;
  frame.state_count = 1;
  std::vector<uint8_t> base(kSide * kSide * 4u, kSentinel);
  CoinWgpuTarget target = targetFor(fast, &base);
  char error[512] = {};
  if (!check(coin_wgpu_submit(&target, &frame, error, sizeof(error)) == COIN_WGPU_OK,
             "owned scene base submission", error)) return false;

  state.model_view[12] = 0.2f;
  state.model_view_projection[12] = 0.2f;
  frame.frame_revision = 41002;
  std::vector<uint8_t> expected(kSide * kSide * 4u, kSentinel);
  target = targetFor(reference, &expected);
  error[0] = 0;
  if (!check(coin_wgpu_submit(&target, &frame, error, sizeof(error)) == COIN_WGPU_OK,
             "full camera reference submission", error)) return false;

  frame.camera_base_revision = 41001;
  frame.vertices = nullptr;
  frame.indices = nullptr;
  frame.draws = nullptr;
  frame.materials = nullptr;
  std::vector<uint8_t> actual(kSide * kSide * 4u, kSentinel);
  target = targetFor(fast, &actual);
  error[0] = 0;
  if (!check(coin_wgpu_submit(&target, &frame, error, sizeof(error)) == COIN_WGPU_OK &&
             actual == expected && actual != base,
             "camera patch must use Rust-owned geometry and match full traversal", error)) return false;

  // The first patch creates persistent bindings; later patches must update
  // their uniforms without leaking a previous frame's camera transform.
  for (unsigned iteration = 0; iteration < 2; ++iteration) {
    state.model_view[12] = 0.35f + 0.15f * iteration;
    state.model_view_projection[12] = state.model_view[12];
    frame.frame_revision = 41003 + iteration;
    frame.camera_base_revision = 41002 + iteration;
    CoinWgpuFrameView full = frame;
    full.camera_base_revision = 0;
    full.vertices = vertices;
    full.indices = indices;
    full.draws = &draw;
    full.materials = &material;
    std::vector<uint8_t> fullPixels(kSide * kSide * 4u, kSentinel);
    target = targetFor(reference, &fullPixels);
    error[0] = 0;
    if (!check(coin_wgpu_submit(&target, &full, error, sizeof(error)) == COIN_WGPU_OK,
               "repeated full camera reference submission", error)) return false;
    std::vector<uint8_t> patchPixels(kSide * kSide * 4u, kSentinel);
    target = targetFor(fast, &patchPixels);
    error[0] = 0;
    if (!check(coin_wgpu_submit(&target, &frame, error, sizeof(error)) == COIN_WGPU_OK &&
               patchPixels == fullPixels && patchPixels != actual,
               "repeated camera patch must match full submission", error)) return false;
    actual.swap(patchPixels);
  }

  // Build both reference images first, then leave two camera patches in
  // flight together. Rewriting the same uniform buffer must not let the
  // second camera transform contaminate the first ticket.
  std::vector<uint8_t> asyncExpected[2] = {
    std::vector<uint8_t>(kSide * kSide * 4u, kSentinel),
    std::vector<uint8_t>(kSide * kSide * 4u, kSentinel)
  };
  for (unsigned iteration = 0; iteration < 2; ++iteration) {
    state.model_view[12] = 0.65f + 0.15f * iteration;
    state.model_view_projection[12] = state.model_view[12];
    CoinWgpuFrameView full = frame;
    full.camera_base_revision = 0;
    full.vertices = vertices;
    full.indices = indices;
    full.draws = &draw;
    full.materials = &material;
    full.frame_revision = 41005 + iteration;
    target = targetFor(reference, &asyncExpected[iteration]);
    error[0] = 0;
    if (!check(coin_wgpu_submit(&target, &full, error, sizeof(error)) == COIN_WGPU_OK,
               "async camera full reference", error)) return false;
  }
  CoinWgpuReadbackTicket asyncTickets[2] = {};
  for (unsigned iteration = 0; iteration < 2; ++iteration) {
    state.model_view[12] = 0.65f + 0.15f * iteration;
    state.model_view_projection[12] = state.model_view[12];
    frame.frame_revision = 41005 + iteration;
    frame.camera_base_revision = 41004 + iteration;
    asyncTickets[iteration].abi_version = COIN_WGPU_ABI_VERSION;
    asyncTickets[iteration].struct_size = sizeof(CoinWgpuReadbackTicket);
    target = targetFor(fast);
    error[0] = 0;
    if (!check(coin_wgpu_submit_async(&target, &frame, &asyncTickets[iteration],
                                      error, sizeof(error)) == COIN_WGPU_OK &&
               asyncTickets[iteration].token != 0,
               "async camera patch submission", error)) return false;
  }
  for (unsigned iteration = 0; iteration < 2; ++iteration) {
    std::vector<uint8_t> pixels(kSide * kSide * 4u, kSentinel);
    CoinWgpuStatus status = COIN_WGPU_NOT_READY;
    for (unsigned attempt = 0; attempt < 5000 && status == COIN_WGPU_NOT_READY; ++attempt) {
      status = coin_wgpu_readback_poll(asyncTickets[iteration].token,
                                      pixels.data(), pixels.size(), nullptr, 0,
                                      error, sizeof(error));
      if (status == COIN_WGPU_NOT_READY) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    if (!check(status == COIN_WGPU_OK && pixels == asyncExpected[iteration],
               "in-flight camera ticket must match its own full frame", error)) return false;
  }

  frame.frame_revision = 41007;
  frame.camera_base_revision = 41006;
  uint64_t rttToken = 0;
  target = targetFor(fast);
  error[0] = 0;
  if (!check(coin_wgpu_submit_texture(&target, &frame, &rttToken, error, sizeof(error)) ==
             COIN_WGPU_INVALID_ARGUMENT && rttToken == 0,
             "RTT must not use owned camera geometry", error)) return false;
  state.material_slot = 99;
  std::vector<uint8_t> rejected(kSide * kSide * 4u, kSentinel);
  target = targetFor(fast, &rejected);
  error[0] = 0;
  if (!check(coin_wgpu_submit(&target, &frame, error, sizeof(error)) == COIN_WGPU_INVALID_ARGUMENT &&
             unchanged(rejected), "state mutation must not reuse owned geometry", error)) return false;

  state.material_slot = 0;
  frame.frame_revision = 41008;
  frame.camera_base_revision = 41001;
  target = targetFor(fast, &rejected);
  error[0] = 0;
  if (!check(coin_wgpu_submit(&target, &frame, error, sizeof(error)) == COIN_WGPU_INVALID_ARGUMENT &&
             unchanged(rejected), "stale camera base must take full validation", error)) return false;

  coin_wgpu_device_destroy(fast);
  if (!createDevice(fast)) return false;
  frame.camera_base_revision = 41006;
  target = targetFor(fast, &rejected);
  error[0] = 0;
  const bool lostBaseRejected = check(
      coin_wgpu_submit(&target, &frame, error, sizeof(error)) == COIN_WGPU_INVALID_ARGUMENT &&
      unchanged(rejected), "new device must not inherit a validated scene", error);
  coin_wgpu_device_destroy(fast);
  coin_wgpu_device_destroy(reference);
  return lostBaseRejected;
}

bool ownedPhongCameraBuffers() {
  CoinWgpuDeviceId reference = 0;
  if (!createDevice(reference)) return false;
  CoinWgpuVertex vertices[3] = {};
  vertices[0].position[0] = -.75f; vertices[0].position[1] = -.75f;
  vertices[1].position[0] = .75f; vertices[1].position[1] = -.75f;
  vertices[2].position[1] = .75f;
  for (auto & vertex : vertices) { vertex.normal[2] = 1; vertex.screen_space_w = 1; }
  const uint32_t indices[3] = {0,1,2};
  CoinWgpuDraw draw{}; draw.vertex_count = draw.index_count = 3;
  CoinWgpuMaterial material{};
  material.diffuse[0] = .8f; material.diffuse[1] = .25f; material.diffuse[3] = 1;
  material.specular[0] = .2f; material.shininess = .25f;
  CoinWgpuRenderState state{};
  state.light_model = 1; state.light_count = 1; state.polygon_offset_primitive_style = 1;
  state.lights[0].position_type[2] = 3; state.lights[0].position_type[3] = 1;
  state.lights[0].color_intensity[0] = state.lights[0].color_intensity[1] = 1;
  state.lights[0].color_intensity[2] = state.lights[0].color_intensity[3] = 1;
  state.lights[0].attenuation_exponent[2] = 1;
  for (unsigned i = 0; i < 4; ++i) {
    state.model_view[i*5] = state.model_view_projection[i*5] = state.normal_matrix[i*5] = 1;
  }
  CoinWgpuFrameView frame = clearFrame(0,0,0);
  frame.frame_revision = 42001;
  frame.vertices = vertices; frame.vertex_count = 3;
  frame.indices = indices; frame.index_count = 3;
  frame.draws = &draw; frame.draw_count = 1;
  frame.materials = &material; frame.material_count = 1;
  frame.states = &state; frame.state_count = 1;
  std::vector<uint8_t> base(kSide*kSide*4u,kSentinel);
  CoinWgpuTarget target = targetFor(0,&base);
  char error[512] = {};
  if (!check(coin_wgpu_submit(&target,&frame,error,sizeof(error)) == COIN_WGPU_OK,
             "owned PHONG base",error)) return false;
  for (unsigned iteration = 0; iteration < 3; ++iteration) {
    frame.camera_base_revision = frame.frame_revision++;
    state.model_view[12] = .1f + .1f*iteration;
    state.model_view_projection[12] = state.model_view[12];
    state.lights[0].position_type[0] = .25f + iteration;
    CoinWgpuFrameView full = frame; full.camera_base_revision = 0;
    std::vector<uint8_t> expected(kSide*kSide*4u,kSentinel), actual(expected);
    target = targetFor(reference,&expected);
    if (!check(coin_wgpu_submit(&target,&full,error,sizeof(error)) == COIN_WGPU_OK,
               "full PHONG camera reference",error)) return false;
    CoinWgpuFrameView patch = frame;
    patch.vertices = nullptr; patch.indices = nullptr; patch.draws = nullptr; patch.materials = nullptr;
    target = targetFor(0,&actual);
    if (!check(coin_wgpu_submit(&target,&patch,error,sizeof(error)) == COIN_WGPU_OK &&
               actual == expected && actual != base,
               "owned PHONG camera must match full frame with new view-space light",error)) return false;
    CoinWgpuCacheStats stats{}; coin_wgpu_get_cache_stats(&stats);
    if (!check(stats.frame_uploads == 0 && stats.frame_uploaded_bytes == 0 && stats.frame_hits >= 1,
               "PHONG camera must retain GPU vertex/index/material payload")) return false;
  }
  const uint64_t oldCameraBase = frame.frame_revision;
  // Consecutive object revisions must keep the ordinary full path and retire
  // the former owned snapshot. A subsequent camera hint may readmit exactly
  // the current object payload after complete validation, then reuse its Arc.
  for (unsigned iteration = 0; iteration < 2; ++iteration) {
    frame.camera_base_revision = 0; ++frame.frame_revision;
    vertices[0].position[0] = -.6f + .1f*iteration;
    material.diffuse[1] = .4f + .1f*iteration;
    std::vector<uint8_t> actual(kSide*kSide*4u,kSentinel), expected(actual);
    target = targetFor(0,&actual);
    if (!check(coin_wgpu_submit(&target,&frame,error,sizeof(error)) == COIN_WGPU_OK,
               "PHONG full object revision",error)) return false;
    target = targetFor(reference,&expected);
    if (!check(coin_wgpu_submit(&target,&frame,error,sizeof(error)) == COIN_WGPU_OK && actual == expected,
               "object full revisions must match an independent device",error)) return false;
  }
  CoinWgpuFrameView stale = frame;
  stale.frame_revision += 100; stale.camera_base_revision = oldCameraBase;
  stale.vertices = nullptr; stale.indices = nullptr; stale.draws = nullptr; stale.materials = nullptr;
  std::vector<uint8_t> staleOutput(kSide*kSide*4u,kSentinel);
  target = targetFor(0,&staleOutput);
  if (!check(coin_wgpu_submit(&target,&stale,error,sizeof(error)) == COIN_WGPU_INVALID_ARGUMENT &&
             unchanged(staleOutput), "object rebuild must clear the old camera-owned base",error)) return false;
  for (unsigned iteration = 0; iteration < 3; ++iteration) {
    frame.camera_base_revision = frame.frame_revision++;
    state.model_view[12] = .4f + .1f*iteration;
    state.model_view_projection[12] = state.model_view[12];
    state.lights[0].position_type[0] = 2 + .25f*iteration;
    CoinWgpuFrameView full = frame; full.camera_base_revision = 0;
    std::vector<uint8_t> actual(kSide*kSide*4u,kSentinel), expected(actual);
    target = targetFor(reference,&expected);
    if (!check(coin_wgpu_submit(&target,&full,error,sizeof(error)) == COIN_WGPU_OK,
               "readmitted PHONG camera reference",error)) return false;
    CoinWgpuFrameView patch = frame;
    if (iteration) {
      patch.vertices = nullptr; patch.indices = nullptr; patch.draws = nullptr; patch.materials = nullptr;
    }
    target = targetFor(0,&actual);
    if (iteration == 0) {
      // This field is checked by encode_frame, after the full composition
      // preflight and speculative snapshot admission. Rejecting it must leave
      // this device's suspension/base unchanged, without publishing pixels.
      const float validNormal = state.normal_matrix[0];
      state.normal_matrix[0] = std::numeric_limits<float>::quiet_NaN();
      const CoinWgpuStatus invalidNormal = coin_wgpu_submit(&target,&patch,error,sizeof(error));
      state.normal_matrix[0] = validNormal;
      if (!check(invalidNormal == COIN_WGPU_INVALID_ARGUMENT && unchanged(actual) &&
                 std::string(error).find("normal matrix") != std::string::npos,
                 "failed encoder readmission must preserve the sentinel and uncommitted base",error)) return false;
      CoinWgpuFrameView withoutPayload = patch;
      withoutPayload.vertices = nullptr; withoutPayload.indices = nullptr;
      withoutPayload.draws = nullptr; withoutPayload.materials = nullptr;
      if (!check(coin_wgpu_submit(&target,&withoutPayload,error,sizeof(error)) == COIN_WGPU_INVALID_ARGUMENT &&
                 unchanged(actual), "failed encoder must not admit owned camera geometry",error)) return false;

      // Existing synchronous late fault: consumed after queue submission,
      // map requests and GPU wait, before snapshot/output publication. Device
      // loss also changes generation; the retry must validate every payload.
      coin_wgpu_inject_async_fault(COIN_WGPU_DEVICE_LOST);
      const CoinWgpuStatus lateFailure = coin_wgpu_submit(&target,&patch,error,sizeof(error));
      coin_wgpu_inject_async_fault(COIN_WGPU_OK);
      if (!check(lateFailure == COIN_WGPU_DEVICE_LOST && unchanged(actual) &&
                 std::string(error).find("Injected async DEVICE_LOST") != std::string::npos,
                 "late failed camera readmission must preserve the output sentinel",error)) return false;
      if (!check(coin_wgpu_submit(&target,&withoutPayload,error,sizeof(error)) == COIN_WGPU_INVALID_ARGUMENT &&
                 unchanged(actual), "late failure must not leave an owned base for a payload-free hint",error)) return false;
    }
    if (!check(coin_wgpu_submit(&target,&patch,error,sizeof(error)) == COIN_WGPU_OK && actual == expected,
               "validated hint must readmit the current object geometry and subsequent patches",error)) return false;
    CoinWgpuCacheStats stats{}; coin_wgpu_get_cache_stats(&stats);
    if (!check(iteration == 0 ? stats.frame_uploads == 1 :
                 (stats.frame_uploads == 0 && stats.frame_uploaded_bytes == 0 && stats.frame_hits >= 1),
               "hint must upload once, followed by owned geometry cache hits")) return false;
  }
  frame.camera_base_revision = frame.frame_revision++;
  frame.vertices = nullptr; frame.indices = nullptr; frame.draws = nullptr; frame.materials = nullptr;
  state.lights[0].color_intensity[0] = .25f;
  std::vector<uint8_t> rejected(kSide*kSide*4u,kSentinel);
  target = targetFor(0,&rejected);
  const bool guard = check(coin_wgpu_submit(&target,&frame,error,sizeof(error)) == COIN_WGPU_INVALID_ARGUMENT &&
               unchanged(rejected), "changed PHONG light color must decline immutable camera hint",error);
  coin_wgpu_device_destroy(reference);
  return guard;
}

// Compare the storage-instance path against the old baked vertex path on a
// second device. Both color and depth are public readback oracles; equal counts
// and repeated revisions deliberately cannot serve as proof of equal contents.
bool instancedOpaqueOwnership() {
  CoinWgpuDeviceId reference = 0, peer = 0;
  if (!createDevice(reference) || !createDevice(peer)) return false;
  CoinWgpuVertex vertices[3]{};
  vertices[0].position[0] = -.5f; vertices[0].position[1] = -.5f;
  vertices[1].position[0] = .5f; vertices[1].position[1] = -.5f;
  vertices[2].position[1] = .5f;
  for (auto & vertex : vertices) { vertex.normal[2] = 1; vertex.screen_space_w = 1; }
  const uint32_t indices[] = {0,1,2};
  CoinWgpuDraw draws[2]{};
  for (auto & draw : draws) { draw.vertex_count = 3; draw.index_count = 3; }
  CoinWgpuMaterial materials[2]{};
  for (auto & material : materials) {
    material.diffuse[3] = 1; material.ambient[0] = material.ambient[1] = .1f;
    material.specular[0] = material.specular[1] = material.specular[2] = .15f;
    material.shininess = .25f;
  }
  materials[0].diffuse[0] = .85f; materials[0].diffuse[1] = .2f;
  materials[1].diffuse[1] = .65f; materials[1].diffuse[2] = .3f;
  CoinWgpuRenderState state{}; state.polygon_offset_primitive_style = 1;
  state.light_model = 1; state.light_count = 1;
  state.depth_test = 1; state.depth_write = 1; state.depth_function = 2;
  state.depth_range[1] = 1;
  state.lights[0].position_type[2] = 2; state.lights[0].position_type[3] = 1;
  for (unsigned i = 0; i < 4; ++i) {
    state.model_view[i*5] = state.model_view_projection[i*5] = state.normal_matrix[i*5] = 1;
    state.lights[0].color_intensity[i] = 1;
  }
  state.lights[0].attenuation_exponent[0] = 1;
  CoinWgpuInstance instances[2]{};
  for (unsigned i = 0; i < 2; ++i) {
    for (unsigned c = 0; c < 4; ++c) instances[i].model_view[c*5] = instances[i].normal_matrix[c*5] = 1;
    instances[i].model_view[0] = .5f; instances[i].model_view[5] = .75f;
    instances[i].model_view[12] = i ? .5f : -.5f;
    instances[i].model_view[14] = i ? .5f : .25f;
    instances[i].material_slot = i;
  }
  instances[1].normal_matrix[8] = .25f;
  CoinWgpuInstanceRange ranges[] = {{0,0,1,0},{1,1,1,0}};
  CoinWgpuFrameView frame = clearFrame(0,0,0);
  frame.frame_revision = 53001;
  frame.vertices = vertices; frame.vertex_count = 3;
  frame.indices = indices; frame.index_count = 3;
  frame.draws = draws; frame.draw_count = 2;
  frame.materials = materials; frame.material_count = 2;
  frame.states = &state; frame.state_count = 1;
  frame.instances = instances; frame.instance_count = 2;
  frame.instance_ranges = ranges; frame.instance_range_count = 2;
  char error[512]{};
  uint64_t referenceRevision = 59000;
  auto samePixels = [&](const std::vector<uint8_t> & actual, const std::vector<uint8_t> & expected,
                        const std::vector<float> & depth, const std::vector<float> & expectedDepth) {
    if (actual.size() != expected.size() || depth.size() != expectedDepth.size()) return false;
    bool wroteDepth = false;
    for (size_t i = 0; i < actual.size(); ++i)
      if (std::abs(int(actual[i]) - int(expected[i])) > 2) return false;
    for (size_t i = 0; i < depth.size(); ++i) {
      if (!std::isfinite(depth[i]) || std::abs(depth[i] - expectedDepth[i]) > 2e-6f) return false;
      wroteDepth |= depth[i] < 1;
    }
    return wroteDepth;
  };
  auto target = [&](CoinWgpuDeviceId id, std::vector<uint8_t> & color, std::vector<float> & depth) {
    CoinWgpuTarget result = targetFor(id, &color);
    result.depth_buffer = depth.data(); result.depth_buffer_len = depth.size();
    return result;
  };
  auto referencePixels = [&](std::vector<uint8_t> & color, std::vector<float> & depth) {
    CoinWgpuVertex baked[6]{};
    const uint32_t bakedIndices[] = {0,1,2,3,4,5};
    for (unsigned i = 0; i < 2; ++i) for (unsigned v = 0; v < 3; ++v) {
      auto & output = baked[i*3+v]; output = vertices[v];
      const auto & instance = instances[i];
      float normal[3]{};
      for (unsigned r = 0; r < 3; ++r) {
        output.position[r] = instance.model_view[r]*vertices[v].position[0]
            + instance.model_view[4+r]*vertices[v].position[1]
            + instance.model_view[8+r]*vertices[v].position[2] + instance.model_view[12+r];
        normal[r] = instance.normal_matrix[r]*vertices[v].normal[0]
            + instance.normal_matrix[4+r]*vertices[v].normal[1]
            + instance.normal_matrix[8+r]*vertices[v].normal[2];
      }
      const float length = std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
      for (unsigned r = 0; r < 3; ++r) output.normal[r] = normal[r]/length;
      output.material_slot = instance.material_slot;
    }
    CoinWgpuDraw bakedDraw{}; bakedDraw.vertex_count = bakedDraw.index_count = 6;
    CoinWgpuFrameView bakedFrame = clearFrame(0,0,0);
    bakedFrame.frame_revision = ++referenceRevision;
    bakedFrame.vertices = baked; bakedFrame.vertex_count = 6;
    bakedFrame.indices = bakedIndices; bakedFrame.index_count = 6;
    bakedFrame.draws = &bakedDraw; bakedFrame.draw_count = 1;
    bakedFrame.materials = materials; bakedFrame.material_count = 2;
    bakedFrame.states = &state; bakedFrame.state_count = 1;
    CoinWgpuTarget output = target(reference,color,depth);
    return check(coin_wgpu_submit(&output,&bakedFrame,error,sizeof(error)) == COIN_WGPU_OK,
                 "baked independent RGB/depth reference",error);
  };
  auto compare = [&](CoinWgpuDeviceId id, const CoinWgpuFrameView & submission, const char * description) {
    std::vector<uint8_t> actual(kSide*kSide*4u,kSentinel), expected(actual);
    std::vector<float> depth(kSide*kSide,-17), expectedDepth(depth);
    if (!referencePixels(expected,expectedDepth)) return false;
    CoinWgpuTarget output = target(id,actual,depth);
    return check(coin_wgpu_submit(&output,&submission,error,sizeof(error)) == COIN_WGPU_OK &&
                 samePixels(actual,expected,depth,expectedDepth),description,error);
  };
  auto nullPayload = [](CoinWgpuFrameView f) {
    f.vertices = nullptr; f.indices = nullptr; f.draws = nullptr; f.materials = nullptr;
    f.instances = nullptr; f.instance_ranges = nullptr;
    return f;
  };
  auto reject = [&](CoinWgpuDeviceId id, const CoinWgpuFrameView & submission, CoinWgpuStatus status,
                    const char * description) {
    std::vector<uint8_t> color(kSide*kSide*4u,kSentinel);
    std::vector<float> depth(kSide*kSide,-17);
    CoinWgpuTarget output = target(id,color,depth);
    return check(coin_wgpu_submit(&output,&submission,error,sizeof(error)) == status &&
        unchanged(color) && std::all_of(depth.begin(),depth.end(),[](float v){ return v == -17; }) &&
        output.submission_serial == 0,description,error);
  };
  auto zeroUploads = [&] {
    CoinWgpuCacheStats stats{}; coin_wgpu_get_cache_stats(&stats);
    return check(stats.frame_uploads == 0 && stats.frame_uploaded_bytes == 0 && stats.frame_hits >= 1,
                 "instances static/camera must retain vertex/index/instance/material GPU buffers");
  };
  auto dynamicOnly = [&] {
    CoinWgpuCacheStats stats{}; coin_wgpu_get_cache_stats(&stats);
    // GpuMaterial: four color vec4 plus one params vec4, as in the shader.
    return check(stats.frame_uploads == 1 && stats.frame_hits >= 1 &&
                 stats.frame_uploaded_bytes == sizeof(instances) + 2u*5u*16u,
                 "object/material updates must upload only instances/materials, retaining canonical geometry");
  };
  if (!compare(0,frame,"initial instances RGB/depth") ||
      !compare(0,frame,"full static instance payload") || !zeroUploads()) return false;
  // Same revision, same counts, changed object and material: exact bytes are
  // required, even though a validated owned snapshot already exists.
  instances[0].model_view[12] += .125f; materials[1].diffuse[1] = .3f;
  if (!compare(0,frame,"instances changed under the same revision") || !dynamicOnly()) return false;
  ++frame.frame_revision; instances[1].model_view[12] -= .125f;
  instances[1].material_slot = 0; materials[0].diffuse[2] = .25f;
  if (!compare(0,frame,"new object/material instance revision") || !dynamicOnly()) return false;
  frame.camera_base_revision = frame.frame_revision++;
  state.model_view[12] = state.model_view_projection[12] = .125f;
  state.lights[0].position_type[0] = .125f;
  if (!compare(0,nullPayload(frame),"instance camera delta with null immutable payloads") || !zeroUploads()) return false;
  frame.camera_base_revision = frame.frame_revision++;
  state.model_view[12] = state.model_view_projection[12] = state.lights[0].position_type[0] = 0;
  if (!compare(0,nullPayload(frame),"instance camera returns to anchor") || !zeroUploads()) return false;
  CoinWgpuFrameView full = frame; full.camera_base_revision = 0; ++full.frame_revision;
  instances[0].reserved[0] = 1;
  if (!reject(0,full,COIN_WGPU_INVALID_ARGUMENT,"instance reserved field must reject without publishing")) return false;
  instances[0].reserved[0] = 0; ranges[1].first_instance = 0;
  if (!reject(0,full,COIN_WGPU_INVALID_ARGUMENT,"overlapping instance ranges must reject")) return false;
  ranges[1].first_instance = 1; instances[0].normal_matrix[0] = std::numeric_limits<float>::quiet_NaN();
  if (!reject(0,full,COIN_WGPU_INVALID_ARGUMENT,"nonfinite instance transform must reject")) return false;
  instances[0].normal_matrix[0] = 1;
  CoinWgpuFrameView invalid = full; invalid.instance_count = std::numeric_limits<uint64_t>::max();
  if (!reject(0,invalid,COIN_WGPU_UNSUPPORTED,"instance count overflow before pointer reads")) return false;
  invalid = full; invalid.instances = reinterpret_cast<const CoinWgpuInstance *>(
      reinterpret_cast<const char *>(instances)+1);
  if (!reject(0,invalid,COIN_WGPU_INVALID_ARGUMENT,"misaligned instance pointer must reject")) return false;
  uint64_t rtt = 0; CoinWgpuTarget rttTarget = targetFor(0);
  if (!check(coin_wgpu_submit_texture(&rttTarget,&full,&rtt,error,sizeof(error)) == COIN_WGPU_UNSUPPORTED &&
             rtt == 0 && std::string(error).find("render-to-texture") != std::string::npos,
             "instanced RTT must reject clearly",error)) return false;
  // A late failure must keep the former camera snapshot AND GPU buffers.
  const CoinWgpuInstance oldInstance = instances[0]; const CoinWgpuMaterial oldMaterial = materials[0];
  instances[0].model_view[12] += .25f; materials[0].diffuse[0] = .3f;
  coin_wgpu_inject_async_fault(COIN_WGPU_OUT_OF_MEMORY);
  const bool late = reject(0,full,COIN_WGPU_OUT_OF_MEMORY,"late instance OOM preserves outputs and previous base");
  coin_wgpu_inject_async_fault(COIN_WGPU_OK);
  if (!late) return false;
  const CoinWgpuInstance retryInstance = instances[0]; const CoinWgpuMaterial retryMaterial = materials[0];
  instances[0] = oldInstance; materials[0] = oldMaterial;
  frame.camera_base_revision = frame.frame_revision++; // the last successful camera revision
  state.model_view[12] = state.model_view_projection[12] = state.lights[0].position_type[0] = .125f;
  if (!compare(0,nullPayload(frame),"camera after failed instance revision uses old owned payload") || !zeroUploads()) return false;
  instances[0] = retryInstance; materials[0] = retryMaterial;
  state.model_view[12] = state.model_view_projection[12] = state.lights[0].position_type[0] = 0;
  frame.camera_base_revision = 0; ++frame.frame_revision;
  if (!compare(0,frame,"retry full instance revision after late OOM") || !dynamicOnly()) return false;
  auto submitDepthAsync = [&](const CoinWgpuFrameView & submission, CoinWgpuReadbackTicket & ticket) {
    CoinWgpuTarget output = targetFor(0); output.depth_buffer_len = kSide*kSide;
    ticket = CoinWgpuReadbackTicket{}; ticket.abi_version = COIN_WGPU_ABI_VERSION; ticket.struct_size = sizeof(ticket);
    return check(coin_wgpu_submit_async(&output,&submission,&ticket,error,sizeof(error)) == COIN_WGPU_OK &&
                 ticket.token != 0 && ticket.depth_bytes == kSide*kSide*sizeof(float),"async full instance submit with depth",error);
  };
  auto pollPixels = [&](const CoinWgpuReadbackTicket & ticket,
                        const std::vector<uint8_t> & expected, const std::vector<float> & expectedDepth) {
    std::vector<uint8_t> color(kSide*kSide*4u,kSentinel); std::vector<float> depth(kSide*kSide,-17);
    for (unsigned i = 0; i < 10000; ++i) {
      const CoinWgpuStatus status = coin_wgpu_readback_poll(ticket.token,color.data(),color.size(),
          depth.data(),depth.size(),error,sizeof(error));
      if (status == COIN_WGPU_NOT_READY) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
      return check(status == COIN_WGPU_OK && samePixels(color,expected,depth,expectedDepth),
                   "async instances RGB/depth match baked reference",error);
    }
    return check(false,"async instanced readback timeout");
  };
  // A readback failure follows a successful rendering submission. Its owned
  // snapshot stays current; generation loss invalidates it independently.
  ++frame.frame_revision; instances[1].model_view[14] = .375f;
  CoinWgpuReadbackTicket ticket{};
  if (!submitDepthAsync(frame,ticket)) return false;
  coin_wgpu_inject_async_fault(COIN_WGPU_BACKEND_ERROR);
  std::vector<uint8_t> failedColor(kSide*kSide*4u,kSentinel); std::vector<float> failedDepth(kSide*kSide,-17);
  if (!check(coin_wgpu_readback_poll(ticket.token,failedColor.data(),failedColor.size(),failedDepth.data(),
       failedDepth.size(),error,sizeof(error)) == COIN_WGPU_BACKEND_ERROR && unchanged(failedColor) &&
       std::all_of(failedDepth.begin(),failedDepth.end(),[](float v){ return v == -17; }),
       "failed instance poll preserves both outputs",error)) return false;
  coin_wgpu_inject_async_fault(COIN_WGPU_OK);
  frame.camera_base_revision = frame.frame_revision++;
  state.model_view[12] = state.model_view_projection[12] = state.lights[0].position_type[0] = .125f;
  if (!compare(0,nullPayload(frame),"camera after failed poll retains successfully submitted instance base") || !zeroUploads()) return false;
  frame.camera_base_revision = 0; ++frame.frame_revision;
  state.model_view[12] = state.model_view_projection[12] = state.lights[0].position_type[0] = 0;
  std::vector<uint8_t> expected(kSide*kSide*4u,kSentinel); std::vector<float> expectedDepth(kSide*kSide,-17);
  if (!referencePixels(expected,expectedDepth) || !submitDepthAsync(frame,ticket) || !pollPixels(ticket,expected,expectedDepth)) return false;
  ++frame.frame_revision;
  if (!submitDepthAsync(frame,ticket)) return false;
  coin_wgpu_inject_async_fault(COIN_WGPU_DEVICE_LOST);
  if (!check(coin_wgpu_readback_poll(ticket.token,failedColor.data(),failedColor.size(),failedDepth.data(),
       failedDepth.size(),error,sizeof(error)) == COIN_WGPU_DEVICE_LOST && unchanged(failedColor),
       "instance poll generation loss",error)) return false;
  coin_wgpu_inject_async_fault(COIN_WGPU_OK);
  CoinWgpuFrameView lostCamera = nullPayload(frame); lostCamera.camera_base_revision = frame.frame_revision; ++lostCamera.frame_revision;
  if (!reject(0,lostCamera,COIN_WGPU_INVALID_ARGUMENT,"lost generation cannot resolve null instance payloads") ||
      !compare(0,frame,"full instance retry after generation loss")) return false;
  CoinWgpuFrameView peerCamera = nullPayload(frame); peerCamera.camera_base_revision = frame.frame_revision; ++peerCamera.frame_revision;
  if (!reject(peer,peerCamera,COIN_WGPU_INVALID_ARGUMENT,"peer must not borrow default device instance snapshot") ||
      !compare(peer,frame,"same instance revision belongs independently to another device")) return false;
  state.model_view[12] = state.model_view_projection[12] = state.lights[0].position_type[0] = .125f;
  if (!compare(peer,peerCamera,"peer null camera owns its instance snapshot")) return false;
  coin_wgpu_inject_device_fault(peer,COIN_WGPU_DEVICE_LOST);
  if (!reject(peer,frame,COIN_WGPU_DEVICE_LOST,"instance device loss before submit") ||
      !reject(peer,peerCamera,COIN_WGPU_INVALID_ARGUMENT,"peer lost generation invalidates null instance payloads")) return false;
  state.model_view[12] = state.model_view_projection[12] = state.lights[0].position_type[0] = 0;
  const bool recovered = compare(peer,frame,"peer full instance retry after device loss");
  coin_wgpu_device_destroy(reference); coin_wgpu_device_destroy(peer);
  return recovered;
}

// Equal-depth overlapping occurrences require traversal order A/B/A, including
// the nonzero first_instance in later groups. Always and LessEqual are both
// admitted by the opaque profile and both must leave the last material visible.
bool instancedOpaqueDrawOrder() {
  CoinWgpuDeviceId reference = 0;
  if (!createDevice(reference)) return false;
  CoinWgpuVertex vertices[6]{};
  for (unsigned mesh = 0; mesh < 2; ++mesh) {
    vertices[mesh*3].position[0] = -.5f; vertices[mesh*3].position[1] = -.5f;
    vertices[mesh*3+1].position[0] = .5f; vertices[mesh*3+1].position[1] = -.5f;
    vertices[mesh*3+2].position[1] = .5f;
    for (unsigned v = mesh*3; v < mesh*3+3; ++v) {
      vertices[v].position[2] = .25f; vertices[v].normal[2] = 1;
      vertices[v].normal[0] = mesh ? .25f : 0; vertices[v].screen_space_w = 1;
    }
  }
  const uint32_t indices[] = {0,1,2,3,4,5};
  CoinWgpuDraw draws[3]{};
  for (unsigned i = 0; i < 3; ++i) {
    draws[i].vertex_count = draws[i].index_count = 3;
    draws[i].first_vertex = draws[i].first_index = i == 1 ? 3 : 0;
  }
  CoinWgpuMaterial materials[3]{};
  for (unsigned i = 0; i < 3; ++i) { materials[i].diffuse[i] = 1; materials[i].diffuse[3] = 1; }
  CoinWgpuRenderState state{}; state.polygon_offset_primitive_style = 1; state.depth_test = state.depth_write = 1; state.depth_range[1] = 1;
  CoinWgpuInstance instances[3]{};
  for (unsigned i = 0; i < 4; ++i) {
    state.model_view[i*5] = state.model_view_projection[i*5] = state.normal_matrix[i*5] = 1;
    for (auto & instance : instances) instance.model_view[i*5] = instance.normal_matrix[i*5] = 1;
  }
  for (unsigned i = 0; i < 3; ++i) instances[i].material_slot = i;
  const CoinWgpuInstanceRange ranges[] = {{0,0,1,0},{1,1,1,0},{2,2,1,0}};
  CoinWgpuVertex baked[9]{};
  uint32_t bakedIndices[9]{};
  for (unsigned i = 0; i < 3; ++i) for (unsigned v = 0; v < 3; ++v) {
    baked[i*3+v] = vertices[(i == 1 ? 3 : 0)+v]; baked[i*3+v].material_slot = i;
    bakedIndices[i*3+v] = i*3+v;
  }
  CoinWgpuDraw bakedDraw{}; bakedDraw.vertex_count = bakedDraw.index_count = 9;
  CoinWgpuFrameView frame = clearFrame(0,0,0);
  frame.vertices = vertices; frame.vertex_count = 6; frame.indices = indices; frame.index_count = 6;
  frame.draws = draws; frame.draw_count = 3; frame.materials = materials; frame.material_count = 3;
  frame.states = &state; frame.state_count = 1; frame.instances = instances; frame.instance_count = 3;
  frame.instance_ranges = ranges; frame.instance_range_count = 3;
  CoinWgpuFrameView full = frame; full.instances = nullptr; full.instance_count = 0;
  full.instance_ranges = nullptr; full.instance_range_count = 0;
  full.vertices = baked; full.vertex_count = 9; full.indices = bakedIndices; full.index_count = 9;
  full.draws = &bakedDraw; full.draw_count = 1;
  char error[512]{};
  for (unsigned test = 0; test < 2; ++test) {
    state.depth_function = test ? 3 : 1;
    frame.frame_revision = 54001+test; full.frame_revision = 54101+test;
    std::vector<uint8_t> actual(kSide*kSide*4u,kSentinel), expected(actual);
    std::vector<float> depth(kSide*kSide,-17), expectedDepth(depth);
    CoinWgpuTarget target = targetFor(0,&actual); target.depth_buffer = depth.data(); target.depth_buffer_len = depth.size();
    if (!check(coin_wgpu_submit(&target,&frame,error,sizeof(error)) == COIN_WGPU_OK,
               "overlapping A/B/A instanced groups",error)) return false;
    target = targetFor(reference,&expected); target.depth_buffer = expectedDepth.data(); target.depth_buffer_len = expectedDepth.size();
    const size_t center = (kSide/2*kSide+kSide/2);
    if (!check(coin_wgpu_submit(&target,&full,error,sizeof(error)) == COIN_WGPU_OK && actual == expected &&
               depth == expectedDepth && depth[center] == .25f && actual[center*4] == 0 &&
               actual[center*4+1] == 0 && actual[center*4+2] == 255,
               "equal-depth overlapping groups must preserve last occurrence color",error)) return false;
  }
  coin_wgpu_device_destroy(reference);
  return true;
}

bool concurrentSubmissions() {
  CoinWgpuDeviceId first = 0, second = 0;
  if (!createDevice(first) || !createDevice(second)) return false;
  std::atomic<unsigned> ready(0);
  std::atomic<bool> start(false);
  std::atomic<bool> passed(true);
  const auto worker = [&](CoinWgpuDeviceId id, unsigned channel) {
    ready.fetch_add(1, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    for (unsigned iteration = 0; iteration < 24; ++iteration) {
      const uint8_t intensity = static_cast<uint8_t>(80u + iteration * 7u);
      const bool ok = submitClear(id,
          channel == 0 ? intensity : 0,
          channel == 1 ? intensity : 0,
          channel == 2 ? intensity : 0);
      if (!ok) {
        passed.store(false, std::memory_order_release);
        break;
      }
    }
  };
  std::thread defaultThread(worker, CoinWgpuDeviceId(0), 0u);
  std::thread firstThread(worker, first, 1u);
  std::thread secondThread(worker, second, 2u);
  while (ready.load(std::memory_order_acquire) != 3u) std::this_thread::yield();
  start.store(true, std::memory_order_release);
  defaultThread.join();
  firstThread.join();
  secondThread.join();
  coin_wgpu_device_destroy(first);
  coin_wgpu_device_destroy(second);
  return check(passed.load(std::memory_order_acquire),
               "parallel device submissions failed or mixed pixels") &&
         submitClear(0, 20, 40, 60);
}

bool stress() {
  CoinWgpuDeviceId first = 0, second = 0;
  if (!createDevice(first) || !createDevice(second)) return false;
  for (unsigned iteration = 0; iteration < 512; ++iteration) {
    const uint8_t red = static_cast<uint8_t>((iteration * 13u) % 256u);
    const uint8_t green = static_cast<uint8_t>((iteration * 29u) % 256u);
    const uint8_t blue = static_cast<uint8_t>((iteration * 47u) % 256u);
    if (!submitClear(first, red, 0, 0) ||
        !submitClear(second, 0, green, 0) ||
        !submitClear(0, 0, 0, blue)) return false;
    if (iteration % 32u == 0) {
      CoinWgpuReadbackTicket oldTicket{}, peerTicket{};
      const CoinWgpuFrameView oldFrame = clearFrame(red, 0, 0);
      const CoinWgpuFrameView peerFrame = clearFrame(0, green, 0);
      if (!submitAsync(first, oldFrame, oldTicket) ||
          !submitAsync(second, peerFrame, peerTicket)) return false;
      coin_wgpu_device_destroy(first);
      if (!ticketInvalidWithoutWrite(oldTicket) ||
          !pollColor(peerTicket, 0, green, 0) ||
          !createDevice(first)) return false;
    }
  }
  coin_wgpu_device_destroy(first);
  coin_wgpu_device_destroy(second);
  return submitClear(0, 0, 0, 200);
}

} // namespace

int main(int argc, char ** argv) {
  if (!coin_wgpu_is_available()) {
    std::cout << "No WebGPU adapter; multi-device test skipped\n";
    return 0;
  }
  const bool runStress = argc > 1 && std::string(argv[1]) == "--stress";
  const bool passed = runStress ? stress() :
                      (invalidHandles() && lifecycleAndIsolation() && rttOwnership() &&
                       cacheIsolation() && validatedCameraSceneOwnership() && ownedPhongCameraBuffers() && instancedOpaqueOwnership() && instancedOpaqueDrawOrder() &&
                       concurrentSubmissions());
  if (passed) std::cout << "CoinWgpuMultiDeviceTest passed"
                        << (runStress ? " (stress)" : "") << '\n';
  return passed ? 0 : 1;
}
