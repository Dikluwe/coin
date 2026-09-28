#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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
                       cacheIsolation() && validatedCameraSceneOwnership() &&
                       concurrentSubmissions());
  if (passed) std::cout << "CoinWgpuMultiDeviceTest passed"
                        << (runStress ? " (stress)" : "") << '\n';
  return passed ? 0 : 1;
}
