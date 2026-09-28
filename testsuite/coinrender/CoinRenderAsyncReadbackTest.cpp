#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfi.h"

#include <cmath>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

static_assert(sizeof(CoinWgpuReadbackTicket) == 72, "Private readback ticket layout");

namespace {

bool check(bool ok, const char * message, const char * diagnostic = "") {
  if (!ok) std::cerr << "CoinRenderAsyncReadbackTest: " << message << " " << diagnostic << "\n";
  return ok;
}

CoinWgpuFrameView clearFrame(uint32_t width, uint32_t height,
                             const CoinWgpuMaterial * material,
                             float red, float green, float blue) {
  CoinWgpuFrameView frame{};
  frame.abi_version = COIN_WGPU_ABI_VERSION;
  frame.struct_size = sizeof(frame);
  frame.width = width;
  frame.height = height;
  frame.materials = material;
  frame.material_count = 1;
  frame.clear_color[0] = red;
  frame.clear_color[1] = green;
  frame.clear_color[2] = blue;
  frame.clear_color[3] = 1.0f;
  return frame;
}

bool request(const CoinWgpuFrameView & frame, bool withDepth,
             CoinWgpuReadbackTicket & ticket) {
  CoinWgpuTarget target{};
  target.width = frame.width;
  target.height = frame.height;
  target.depth_buffer_len = withDepth ? uint64_t(frame.width) * frame.height : 0;
  ticket.abi_version = COIN_WGPU_ABI_VERSION;
  ticket.struct_size = sizeof(ticket);
  char error[512] = {};
  const CoinWgpuStatus status = coin_wgpu_submit_async(&target, &frame, &ticket,
                                                        error, sizeof(error));
  return check(status == COIN_WGPU_OK && ticket.token != 0 &&
               ticket.submission_serial == target.submission_serial,
               "async submit", error);
}

CoinWgpuStatus pollUntilReady(const CoinWgpuReadbackTicket & ticket,
                              std::vector<uint8_t> & color,
                              std::vector<float> & depth,
                              char * error, size_t errorLength) {
  for (int attempt = 0; attempt < 5000; ++attempt) {
    const CoinWgpuStatus status = coin_wgpu_readback_poll(ticket.token,
      color.data(), color.size(), depth.empty() ? nullptr : depth.data(),
      depth.size(), error, errorLength);
    if (status != COIN_WGPU_NOT_READY) return status;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return COIN_WGPU_NOT_READY;
}

bool colorIs(const std::vector<uint8_t> & color, uint32_t width, uint32_t height,
             int red, int green, int blue) {
  const size_t pixel = (size_t(height / 2) * width + width / 2) * 4;
  return color.size() == size_t(width) * height * 4 &&
    std::abs(int(color[pixel]) - red) <= 2 &&
    std::abs(int(color[pixel + 1]) - green) <= 2 &&
    std::abs(int(color[pixel + 2]) - blue) <= 2 &&
    color[pixel + 3] == 255;
}

} // namespace

int main() {
  if (!coin_wgpu_is_available()) {
    std::cout << "No WebGPU adapter; async readback test skipped\n";
    return 0;
  }
  char adapter[256] = {};
  coin_wgpu_get_adapter_info(adapter, sizeof(adapter));
  const auto runStart = std::chrono::steady_clock::now();
  CoinWgpuMaterial material{};
  material.diffuse[3] = 1.0f;
  CoinWgpuFrameView red = clearFrame(64, 64, &material, 1, 0, 0);
  CoinWgpuFrameView green = clearFrame(64, 64, &material, 0, 1, 0);
  CoinWgpuReadbackTicket first{}, second{};
  if (!request(red, true, first) || !request(green, true, second)) return 1;
  if (!check(first.token != second.token && first.submission_serial < second.submission_serial,
             "unique ordered tokens and serials") ||
      !check(second.width == 64 && second.height == 64 &&
             second.color_format == 0 && second.depth_format == 1 &&
             second.color_row_pitch == 256 && second.depth_row_pitch == 256 &&
             second.color_bytes == 64u * 64u * 4u && second.depth_bytes == 64u * 64u * 4u,
             "ticket metadata")) return 1;

  char error[512] = {};
  const CoinWgpuStatus initialQuery =
    coin_wgpu_readback_query(second.token, error, sizeof(error));
  if (!check(initialQuery == COIN_WGPU_NOT_READY || initialQuery == COIN_WGPU_OK,
             "query must be nonblocking and preserve pending ticket", error)) return 1;
  std::vector<uint8_t> tooSmall(4, 17);
  std::vector<float> tooShallow(4, -1.0f);
  const CoinWgpuStatus small = pollUntilReady(second, tooSmall, tooShallow,
                                               error, sizeof(error));
  if (!check(small == COIN_WGPU_INVALID_ARGUMENT && tooSmall[0] == 17 &&
             tooShallow[0] == -1.0f, "small buffers cannot publish partial output", error) ||
      !check(coin_wgpu_readback_query(second.token, error, sizeof(error)) == COIN_WGPU_OK,
             "query reports ready without consuming ticket", error)) return 1;

  std::vector<float> overlapping(64u * 64u * 2u, -1.0f);
  const CoinWgpuStatus overlap = coin_wgpu_readback_poll(second.token,
      reinterpret_cast<uint8_t *>(overlapping.data()), 64u * 64u * 4u,
      overlapping.data(), 64u * 64u, error, sizeof(error));
  if (!check(overlap == COIN_WGPU_INVALID_ARGUMENT && overlapping[0] == -1.0f,
             "overlapping output buffers must be rejected without consumption", error)) return 1;

  std::vector<uint8_t> greenColor(64u * 64u * 4u, 17);
  std::vector<float> greenDepth(64u * 64u, -1.0f);
  const CoinWgpuStatus secondResult = pollUntilReady(second, greenColor, greenDepth,
                                                     error, sizeof(error));
  if (!check(secondResult == COIN_WGPU_OK && colorIs(greenColor, 64, 64, 0, 255, 0) &&
             std::abs(greenDepth[32u * 64u + 32u] - 1.0f) < 0.001f,
             "second request completed first", error) ||
      !check(coin_wgpu_readback_query(second.token, error, sizeof(error)) ==
               COIN_WGPU_INVALID_ARGUMENT,
             "query rejects consumed ticket", error)) return 1;

  std::vector<uint8_t> redColor(64u * 64u * 4u, 17);
  std::vector<float> redDepth(64u * 64u, -1.0f);
  const CoinWgpuStatus firstResult = pollUntilReady(first, redColor, redDepth,
                                                    error, sizeof(error));
  if (!check(firstResult == COIN_WGPU_OK && colorIs(redColor, 64, 64, 255, 0, 0) &&
             std::abs(redDepth[32u * 64u + 32u] - 1.0f) < 0.001f,
             "first request retained its own attachments", error)) return 1;

  CoinWgpuTarget synchronous{};
  synchronous.width = red.width;
  synchronous.height = red.height;
  std::vector<uint8_t> synchronousColor(redColor.size(), 0);
  std::vector<float> synchronousDepth(redDepth.size(), 0.0f);
  synchronous.color_buffer = synchronousColor.data();
  synchronous.color_buffer_len = synchronousColor.size();
  synchronous.depth_buffer = synchronousDepth.data();
  synchronous.depth_buffer_len = synchronousDepth.size();
  if (!check(coin_wgpu_submit(&synchronous, &red, error, sizeof(error)) == COIN_WGPU_OK &&
             synchronousColor == redColor && synchronousDepth == redDepth,
             "async and synchronous readback must match exactly", error)) return 1;

  // Exercise completed staging reuse across synchronous and asynchronous
  // requests: a recycled mapped buffer must never leak the previous frame.
  synchronous.depth_buffer = nullptr;
  synchronous.depth_buffer_len = 0;
  for (int i = 0; i < 12; ++i) {
    const CoinWgpuFrameView & frame = (i % 2 == 0) ? red : green;
    if (!check(coin_wgpu_submit(&synchronous, &frame, error, sizeof(error)) == COIN_WGPU_OK &&
               colorIs(synchronousColor, 64, 64,
                       i % 2 == 0 ? 255 : 0, i % 2 == 0 ? 0 : 255, 0),
               "repeated synchronous color-only readback", error)) return 1;
  }
  std::vector<float> noDepth;
  for (int i = 0; i < 4; ++i) {
    CoinWgpuReadbackTicket reused{};
    const CoinWgpuFrameView & frame = (i % 2 == 0) ? red : green;
    if (!request(frame, false, reused)) return 1;
    std::vector<uint8_t> pixels(64u * 64u * 4u, 17);
    if (!check(pollUntilReady(reused, pixels, noDepth, error, sizeof(error)) == COIN_WGPU_OK &&
               colorIs(pixels, 64, 64,
                       i % 2 == 0 ? 255 : 0, i % 2 == 0 ? 0 : 255, 0),
               "repeated asynchronous color-only readback", error)) return 1;
  }

  CoinWgpuReadbackTicket cancelled{};
  if (!request(red, false, cancelled) ||
      !check(coin_wgpu_readback_cancel(cancelled.token) == COIN_WGPU_OK,
             "cancel pending readback") ||
      !check(coin_wgpu_readback_poll(cancelled.token, nullptr, 0, nullptr, 0,
                                    error, sizeof(error)) == COIN_WGPU_INVALID_ARGUMENT,
             "cancelled token rejected")) return 1;

  CoinWgpuFrameView yellow = clearFrame(32, 32, &material, 1, 1, 0);
  CoinWgpuReadbackTicket oldSize{}, resized{};
  if (!request(red, false, oldSize) || !request(yellow, false, resized)) return 1;
  std::vector<uint8_t> yellowColor(32u * 32u * 4u, 17);
  const CoinWgpuStatus resizeResult = pollUntilReady(resized, yellowColor, noDepth,
                                                     error, sizeof(error));
  if (!check(resizeResult == COIN_WGPU_OK && resized.width == 32 &&
             resized.depth_bytes == 0 && colorIs(yellowColor, 32, 32, 255, 255, 0),
             "resized request has independent dimensions", error)) return 1;
  std::vector<uint8_t> oldColor(64u * 64u * 4u, 17);
  if (!check(pollUntilReady(oldSize, oldColor, noDepth, error, sizeof(error)) == COIN_WGPU_OK &&
             colorIs(oldColor, 64, 64, 255, 0, 0),
             "pending old-size request survives newer resized submission", error)) return 1;

  CoinWgpuReadbackTicket mapFailure{};
  if (!request(red, true, mapFailure)) return 1;
  std::vector<uint8_t> untouchedColor(64u * 64u * 4u, 17);
  std::vector<float> untouchedDepth(64u * 64u, -1.0f);
  coin_wgpu_inject_async_fault(COIN_WGPU_BACKEND_ERROR);
  if (!check(coin_wgpu_readback_poll(mapFailure.token, untouchedColor.data(),
                                    untouchedColor.size(), untouchedDepth.data(),
                                    untouchedDepth.size(), error, sizeof(error)) == COIN_WGPU_BACKEND_ERROR &&
             untouchedColor[0] == 17 && untouchedDepth[0] == -1.0f,
             "map failure must not publish partial attachments", error)) return 1;

  CoinWgpuReadbackTicket lost{}, lostPeer{};
  if (!request(red, false, lost) || !request(green, false, lostPeer)) return 1;
  coin_wgpu_inject_async_fault(COIN_WGPU_DEVICE_LOST);
  if (!check(coin_wgpu_readback_query(lost.token, error, sizeof(error)) ==
               COIN_WGPU_DEVICE_LOST,
             "device loss invalidates pending readback") ||
      !check(coin_wgpu_readback_poll(lostPeer.token, nullptr, 0, nullptr, 0,
                                    error, sizeof(error)) == COIN_WGPU_INVALID_ARGUMENT,
             "device loss invalidates all peer tickets")) return 1;
  CoinWgpuReadbackTicket recovered{};
  if (!request(yellow, false, recovered)) return 1;
  std::vector<uint8_t> recoveredColor(32u * 32u * 4u, 17);
  if (!check(pollUntilReady(recovered, recoveredColor, noDepth, error, sizeof(error)) == COIN_WGPU_OK &&
             recovered.generation > lost.generation &&
             colorIs(recoveredColor, 32, 32, 255, 255, 0),
             "new generation recovers after device loss", error)) return 1;
  coin_wgpu_reset_context();
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now() - runStart).count();
  std::cout << "CoinRenderAsyncReadbackTest passed; adapter=" << adapter
            << "; two 64x64 RGBA8+Depth32 staging requests="
            << 2u * (first.color_row_pitch + first.depth_row_pitch) * first.height
            << " bytes; test roundtrip=" << elapsed << " us (not an SLA)\n";
  return 0;
}
