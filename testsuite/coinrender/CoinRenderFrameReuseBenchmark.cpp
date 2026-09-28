// Non-gating Release benchmark for the private FFI packing update paths.
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/SoWgpuFfiFrame.h"

#include <Inventor/SbVec3f.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace {
typedef std::chrono::steady_clock Clock;

CoinRenderFramePlan
makePlan(uint64_t revision)
{
  CoinRenderFramePlan plan;
  plan.revision = revision;
  // Matches the captured payload sizes of the documented Assembly scene.
  plan.vertices.resize(22005);
  plan.indices.resize(131169, 0);
  plan.materials.push_back(CoinRenderMaterialSnapshot{});
  plan.lightingStates.push_back(CoinRenderLightingSnapshot{});
  plan.cameras.push_back(CoinRenderCameraSnapshot{});
  plan.viewports.push_back(CoinRenderViewportSnapshot{});
  CoinRenderRenderStateSnapshot state;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  plan.renderStates.push_back(state);
  CoinRenderDrawPacket draw;
  draw.topology = CoinRenderPrimitiveTopology::TRIANGLE_LIST;
  draw.geometry.vertexCount = static_cast<uint32_t>(plan.vertices.size());
  draw.geometry.indexCount = static_cast<uint32_t>(plan.indices.size());
  plan.draws.push_back(draw);
  return plan;
}

double
percentile(std::vector<double> values, size_t percentileValue)
{
  std::sort(values.begin(), values.end());
  return values[(values.size() - 1) * percentileValue / 100];
}
}

int
main()
{
  CoinRenderFramePlan fullFrame = makePlan(1);
  CoinRenderFramePlan patchFrame = fullFrame;
  SoWgpuFfiFrame fullPacker;
  SoWgpuFfiFrame patchPacker;
  std::string diagnostic;
  if (!fullPacker.prepare(fullFrame, 512, 512, diagnostic) ||
      !patchPacker.prepare(patchFrame, 512, 512, diagnostic)) {
    std::cerr << "Initial FFI packing failed: " << diagnostic << '\n';
    return 1;
  }

  std::vector<double> fullMs;
  std::vector<double> patchMs;
  for (uint64_t i = 0; i < 250; ++i) {
    const uint64_t revision = i + 2;
    SbMatrix view = SbMatrix::identity();
    view.setTranslate(SbVec3f(float(i + 1) * 0.0001f, 0.0f, 0.0f));

    fullFrame.revision = revision;
    fullFrame.cameras[0].viewMatrix = view;
    fullFrame.renderStates[0].view = view;
    Clock::time_point begin = Clock::now();
    if (!fullPacker.prepare(fullFrame, 512, 512,
          CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0),
          diagnostic)) return 1;
    fullMs.push_back(std::chrono::duration<double, std::milli>(
      Clock::now() - begin).count());

    const uint64_t base = patchFrame.revision;
    patchFrame.revision = revision;
    patchFrame.cameras[0].viewMatrix = view;
    patchFrame.renderStates[0].view = view;
    begin = Clock::now();
    if (!patchPacker.prepare(patchFrame, 512, 512,
          CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH, base),
          diagnostic)) return 1;
    patchMs.push_back(std::chrono::duration<double, std::milli>(
      Clock::now() - begin).count());
  }

  const double fullMedian = percentile(fullMs, 50);
  const double patchMedian = percentile(patchMs, 50);
  std::cout << "samples=250 vertices=22005 indices=131169"
            << " full_median_ms=" << fullMedian
            << " full_p95_ms=" << percentile(fullMs, 95)
            << " patch_median_ms=" << patchMedian
            << " patch_p95_ms=" << percentile(patchMs, 95)
            << " median_reduction_percent="
            << (1.0 - patchMedian / fullMedian) * 100.0 << '\n';
  return 0;
}
