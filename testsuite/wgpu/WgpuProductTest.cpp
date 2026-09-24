#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include <Inventor/rendering/SoWgpuSceneManager.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {
bool check(bool condition, const char * message) {
  if (!condition) std::cerr << "WgpuProductTest: " << message << '\n';
  return condition;
}
}

int main() {
  SoDB::init();
  SoWgpuRenderAction::initClass();

  CoinWgpuExperimentalCapabilities caps{};
  unsigned char untouched[sizeof(caps)];
  std::memset(untouched, 0x5a, sizeof(untouched));
  if (!check(coin_wgpu_experimental_query_capabilities(
               COIN_WGPU_EXPERIMENTAL_OFFSCREEN, nullptr, sizeof(caps)) == 2,
             "null capability output was accepted") ||
      !check(coin_wgpu_experimental_query_capabilities(
               COIN_WGPU_EXPERIMENTAL_OFFSCREEN, untouched, sizeof(untouched) - 1) == 2 &&
             untouched[0] == 0x5a,
             "short capability output was accepted or overwritten") ||
      !check(coin_wgpu_experimental_query_capabilities(999, untouched, sizeof(untouched)) == 1 &&
             untouched[0] == 0x5a,
             "unknown target was accepted or overwrote output")) return 1;

  if (!check(coin_wgpu_experimental_query_capabilities(
               COIN_WGPU_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) == 0 &&
             caps.struct_size == sizeof(caps) &&
             caps.version == COIN_WGPU_CAPABILITIES_VERSION &&
             caps.target == COIN_WGPU_EXPERIMENTAL_OFFSCREEN &&
             (caps.features & COIN_WGPU_FEATURE_COLOR_DEPTH) != 0 &&
             caps.max_lights_per_draw == 8 &&
             caps.max_texture_units == 1 &&
             caps.max_scene_texture_depth == 8 &&
             caps.max_scene_texture_bytes_per_apply == UINT64_C(64) * 1024 * 1024,
             "offscreen capability profile is incomplete")) return 1;

  CoinWgpuExperimentalCapabilities windowCaps{};
#if defined(HAVE_WGPU_RUST_BRIDGE) && defined(__linux__)
  if (!check(caps.backend == COIN_WGPU_EXPERIMENTAL_RUST &&
             (caps.features & COIN_WGPU_FEATURE_ASYNC_READBACK) != 0 &&
             (caps.features & COIN_WGPU_FEATURE_DIRECT_RTT) != 0 &&
             coin_wgpu_experimental_query_capabilities(
               COIN_WGPU_EXPERIMENTAL_XLIB_WINDOW, &windowCaps, sizeof(windowCaps)) == 0 &&
             windowCaps.target == COIN_WGPU_EXPERIMENTAL_XLIB_WINDOW &&
             (windowCaps.features & COIN_WGPU_FEATURE_ASYNC_READBACK) == 0 &&
             (windowCaps.features & COIN_WGPU_FEATURE_DIRECT_RTT) == 0,
             "Rust target capability split is wrong")) return 1;
#else
  if (!check(coin_wgpu_experimental_query_capabilities(
               COIN_WGPU_EXPERIMENTAL_XLIB_WINDOW, &windowCaps, sizeof(windowCaps)) == 1,
             "unsupported window target was advertised")) return 1;
#endif

#if defined(HAVE_WGPU_RUST_BRIDGE)
  if (!caps.gpu_available) {
    std::cout << "No WebGPU adapter; manager rendering skipped\n";
    return 0;
  }
#else
  if (!check(caps.backend == COIN_WGPU_EXPERIMENTAL_RECORDING &&
             caps.gpu_available == 0 &&
             (caps.features & COIN_WGPU_FEATURE_ASYNC_READBACK) == 0 &&
             (caps.features & COIN_WGPU_FEATURE_DIRECT_RTT) == 0,
             "Recording backend advertised GPU-only features")) return 1;
#endif

  SoWgpuSceneManager * manager = new SoWgpuSceneManager(SbVec2i32(8, 8));
  if (!check(manager->getRenderTarget() &&
             manager->render() == SoWgpuRenderAction::INVALID_SCENE &&
             manager->getLastError().getLength() > 0,
             "missing scene was not rejected")) return 1;
  SoSeparator * root = new SoSeparator;
  root->ref();
  manager->setSceneGraph(root);
  root->unref(); // Manager retains its own scene reference.
  manager->setBackgroundColor(SbColor4f(0.2f, 0.4f, 0.6f, 1.0f));
  if (!check(manager->getSceneGraph() == root &&
             manager->render() == SoWgpuRenderAction::SUCCESS,
             "manager failed to render retained scene")) return 1;
  std::vector<uint8_t> color;
  manager->getRenderTarget()->readbackRGBA(color);
  if (!check(color.size() == 8u * 8u * 4u &&
             color[0] >= 50 && color[0] <= 52 &&
             color[1] >= 101 && color[1] <= 103 &&
             color[2] >= 152 && color[2] <= 154,
             "manager did not publish its clear color")) return 1;
  if (!check(manager->resize(SbVec2i32(0, 0)) &&
             manager->render() == SoWgpuRenderAction::NOT_READY,
             "zero-size suspension was not propagated")) return 1;
  if (!check(manager->resize(SbVec2i32(16, 16)) &&
             manager->render() == SoWgpuRenderAction::SUCCESS,
             "manager did not recover after resize")) return 1;

#if defined(HAVE_WGPU_RUST_BRIDGE)
  SoWgpuReadbackTicket ticket{};
  if (!check(manager->renderAsync(ticket) == SoWgpuRenderAction::SUCCESS &&
             ticket.token != 0 && ticket.width == 16 && ticket.height == 16,
             "manager did not return an offscreen async ticket")) return 1;
  delete manager;
  std::vector<uint8_t> asyncColor;
  std::vector<float> asyncDepth;
  SbString diagnostic;
  SoWgpuRenderTarget::ReadbackStatus status = SoWgpuRenderTarget::READBACK_NOT_READY;
  for (int i = 0; i < 5000 && status == SoWgpuRenderTarget::READBACK_NOT_READY; ++i) {
    status = SoWgpuRenderTarget::pollReadback(ticket, asyncColor, asyncDepth, &diagnostic);
    if (status == SoWgpuRenderTarget::READBACK_NOT_READY)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!check(status == SoWgpuRenderTarget::READBACK_READY &&
             asyncColor.size() == 16u * 16u * 4u &&
             asyncDepth.size() == 16u * 16u,
             "async ticket did not survive manager/target destruction")) return 1;
#else
  SoWgpuReadbackTicket ticket{};
  if (!check(manager->renderAsync(ticket) == SoWgpuRenderAction::UNSUPPORTED &&
             ticket.token == 0,
             "Recording manager did not reject async readback")) return 1;
  delete manager;
#endif
  std::cout << "WgpuProductTest passed\n";
  return 0;
}
