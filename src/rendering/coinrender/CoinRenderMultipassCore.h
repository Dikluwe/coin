#ifndef COIN_RENDER_MULTIPASS_CORE_H
#define COIN_RENDER_MULTIPASS_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <Inventor/C/tidbits.h>
#include <limits>
#include <cmath>
#include <algorithm>
#include <utility>
#include <vector>

// Pure multipass facts. Device owners still account for their simultaneous
// format/sample attachments, existing feature scratch and concrete limits.
namespace CoinRenderMultipassCore {
inline bool jitter(int count, int pass, const CoinRenderViewportSnapshot & viewport,
                   SbVec3f & displacement)
{
  displacement.setValue(0, 0, 0);
  if (count < 1 || count > 16 || pass < 0 || pass >= count ||
      viewport.width < 0 || viewport.height < 0) return false;
  // The Coin helper forms its table pointer before its count<2 guard.
  if (count == 1 || viewport.width == 0 || viewport.height == 0) return true;
  const int extent[2] = {viewport.width, viewport.height};
  float value[3];
  coin_viewvolume_jitter(count, pass, extent, value);
  displacement.setValue(value);
  return true;
}
inline bool projection(const SbMatrix & original, int count, int pass,
                       const CoinRenderViewportSnapshot & viewport, SbMatrix & output)
{
  SbVec3f displacement;
  if (!jitter(count, pass, viewport, displacement)) return false;
  output = original;
  if (count > 1) {
    SbMatrix translation;
    translation.setTranslate(displacement);
    output.multRight(translation); // SoCamera::GLRender's exact operation order.
  }
  return true;
}
inline bool projectionsFinite(const CoinRenderFramePlan & frame)
{
  if (frame.numPasses == 1) return true;
  for (const auto & state : frame.renderStates) {
    if (state.viewportSlot >= frame.viewports.size()) return false;
    for (int pass = 0; pass < frame.numPasses; ++pass) {
      SbMatrix shifted;
      if (!projection(state.projectionCoin, frame.numPasses, pass,
            frame.viewports[state.viewportSlot], shifted)) return false;
      const SbMatrix mvp = state.model * state.view * shifted;
      for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
          if (!std::isfinite(shifted[row][column]) ||
              !std::isfinite(mvp[row][column])) return false;
    }
  }
  return true;
}
inline bool addAttachmentBytes(uint64_t width, uint64_t height,
                               uint64_t bytesPerPixel, uint64_t samples,
                               uint64_t & total)
{
  const uint64_t limit = std::numeric_limits<uint64_t>::max();
  if (width && height > limit / width) return false;
  uint64_t bytes = width * height;
  if (bytesPerPixel && bytes > limit / bytesPerPixel) return false;
  bytes *= bytesPerPixel;
  if (samples && bytes > limit / samples) return false;
  bytes *= samples;
  if (bytes > limit - total) return false;
  total += bytes;
  return true;
}
inline bool rasterPayloadBytes(const CoinRenderFramePlan & frame, uint64_t gpuVertexStride,
                               uint64_t gpuDrawStride, bool indexedGpu, uint64_t & total)
{
  total = 0;
  if (frame.numPasses == 1) return true;
  std::vector<std::pair<uint64_t,uint64_t>> vertices, indices;
  uint64_t drawCount = 0;
  for (const auto & draw : frame.draws) if (draw.multipassMask) {
    vertices.emplace_back(draw.geometry.firstVertex,
      uint64_t(draw.geometry.firstVertex) + draw.geometry.vertexCount);
    indices.emplace_back(draw.geometry.firstIndex,
      uint64_t(draw.geometry.firstIndex) + draw.geometry.indexCount);
    ++drawCount;
  }
  const auto covered = [](std::vector<std::pair<uint64_t,uint64_t>> & ranges) {
    std::sort(ranges.begin(), ranges.end());
    uint64_t result = 0, end = 0;
    for (const auto & range : ranges) {
      if (range.second > end) result += range.second - std::max(end, range.first);
      end = std::max(end, range.second);
    }
    return result;
  };
  const uint64_t vertexCount = covered(vertices), indexCount = covered(indices);
  return addAttachmentBytes(vertexCount, 1, sizeof(CoinRenderVertexSnapshot), 1, total) &&
    addAttachmentBytes(indexCount, 1, sizeof(uint32_t), 1, total) &&
    addAttachmentBytes(indexedGpu ? vertexCount : indexCount, 1, gpuVertexStride, 1, total) &&
    addAttachmentBytes(indexCount, 1, gpuVertexStride ? sizeof(uint32_t) : 0, 1, total) &&
    addAttachmentBytes(drawCount, 1, sizeof(CoinRenderDrawPacket) + gpuDrawStride, 1, total);
}
enum class Executor { CPU, BGFX, WGPU };
// Nominal live attachments for the selected executor. This is separate from
// geometry and resident texture cache budgets; only per-frame scratch is added.
COIN_RENDER_DLL_API bool requiredBytes(const CoinRenderFramePlan & frame,
  const SbVec2i32 & extent, uint32_t samples, uint64_t & total,
  Executor executor = Executor::WGPU, bool shadowArray = true);
}
class CoinRenderRttPlan;
namespace CoinRenderMultipassCore {
COIN_RENDER_DLL_API bool graphBytes(const CoinRenderRttPlan & graph,
  const CoinRenderFramePlan & root, const SbVec2i32 & extent, uint32_t samples,
  Executor executor, uint64_t & total);
}
#endif
