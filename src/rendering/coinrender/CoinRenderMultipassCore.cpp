#include "rendering/coinrender/CoinRenderMultipassCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#include "rendering/coinrender/CoinRenderTextureFormatCore.h"

namespace CoinRenderMultipassCore {
namespace {
bool add(uint64_t value, uint64_t & total) {
  return addAttachmentBytes(value, 1, 1, 1, total);
}
bool compositionBytes(const CoinRenderFramePlan & frame, const SbVec2i32 & extent,
                      uint32_t samples, Executor executor, uint64_t & total) {
  std::vector<CoinRenderCompositionItem> order;
  std::string diagnostic;
  if (!coin_render_composition_order(frame, order, diagnostic, true)) return false;
  bool peel = false, weighted = false;
  for (const auto & item : order) {
    peel = peel || (item.blend && item.deferred &&
      item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS);
    weighted = weighted || (item.blend && item.deferred &&
      item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT);
  }
  if (executor == Executor::CPU) {
    // CPU stores a vector per pixel and depth/color/write records per retained
    // peeling layer. Software OIT is not a supported legacy profile.
    return !peel || (addAttachmentBytes(extent[0], extent[1],
      sizeof(std::vector<float>), 1, total) &&
      addAttachmentBytes(extent[0], extent[1], 24, frame.transparency.layers, total));
  }
  // wgpu peeling owns an opaque depth snapshot and RGBA16F/D32F/R8 layers.
  // BGFX owns RGBA16F/D32F/R16 layers, and resolves every sample texture.
  if (peel && executor == Executor::WGPU &&
      !addAttachmentBytes(extent[0], extent[1], 4, samples, total)) return false;
  const uint64_t layerBytes = executor == Executor::BGFX ? 14 : 13;
  if (peel && !addAttachmentBytes(extent[0], extent[1], layerBytes,
        uint64_t(frame.transparency.layers) * samples, total)) return false;
  if (weighted && !addAttachmentBytes(extent[0], extent[1],
        executor == Executor::BGFX ? 14 : 10, samples, total)) return false;
  if (executor == Executor::BGFX && samples > 1) {
    if (peel && !addAttachmentBytes(extent[0], extent[1], 14,
          frame.transparency.layers, total)) return false;
    if (weighted && !addAttachmentBytes(extent[0], extent[1], 14, 1, total)) return false;
  }
  return true;
}
}
bool requiredBytes(const CoinRenderFramePlan & frame, const SbVec2i32 & extent,
                   uint32_t samples, uint64_t & total, Executor executor,
                   bool shadowArray) {
  total = 0;
  if (frame.numPasses == 1) return true;
  if (extent[0] < 0 || extent[1] < 0 || !samples) return false;
  if (!extent[0] || !extent[1]) return true;
  const uint64_t colorBytes = frame.outputColorFormat == CoinRenderTextureFormat::RGBA16_FLOAT ? 8 : 4;
  if (executor == Executor::CPU) {
    if (!addAttachmentBytes(extent[0], extent[1], 24, 1, total)) return false;
  } else {
    // Two alternating RGBA32F sums; color/depth scene and its single-sample
    // resolved color are reused by all N samples.
    if (!addAttachmentBytes(extent[0], extent[1], 32, 1, total) ||
        !addAttachmentBytes(extent[0], extent[1], colorBytes + 4, samples, total) ||
        (samples > 1 && !addAttachmentBytes(extent[0], extent[1], colorBytes + 4, 1, total))) return false;
  }
  if (!compositionBytes(frame, extent, samples, executor, total)) return false;
  if (executor != Executor::CPU) {
    CoinRenderShadowPlan shadows;
    std::string diagnostic;
    if (!coin_render_plan_shadows(frame, shadows, diagnostic)) return false;
    uint64_t largest = 0;
    for (const auto & pass : shadows.passes) {
      largest = std::max(largest, uint64_t(pass.mapSize));
      if (!addAttachmentBytes(pass.mapSize, pass.mapSize, 20, 1, total)) return false;
    }
    if (shadowArray && !shadows.passes.empty()) {
      uint64_t layers = std::max<uint64_t>(2, shadows.passes.size());
      if (executor == Executor::WGPU && layers % 6 == 0) ++layers;
      if (!addAttachmentBytes(largest, largest, 16, layers, total)) return false;
    }
    // Native inherited raster shaders materialize an independent RGBA32F
    // image per consumed state. These images coexist with the AA sums.
    for (const auto & state : frame.renderStates) {
      if (state.rasterTextureSourceSlot == UINT32_MAX) continue;
      if (state.textureImageSlot >= frame.textures.size()) return false;
      const auto & image = frame.textures[state.textureImageSlot];
      if (!addAttachmentBytes(image.width, image.height, 16, 1, total)) return false;
    }
  }
  return true;
}
bool graphBytes(const CoinRenderRttPlan & graph, const CoinRenderFramePlan & root,
                const SbVec2i32 & extent, uint32_t samples, Executor executor,
                uint64_t & total) {
  total = 0;
  if (root.numPasses == 1) return true;
  const bool direct = graph.mode == COIN_RENDER_SCENE_TEXTURE_DIRECT;
  uint64_t retained = 0, transient = 0;
  SbVec2i32 previousMsaaExtent = extent;
  for (size_t i = 0; i < graph.producers.size(); ++i) {
    const auto & producer = graph.producers[i];
    if (producer.size[0] <= 0 || producer.size[1] <= 0) return false;
    const uint32_t width = uint32_t(producer.size[0]), height = uint32_t(producer.size[1]);
    const bool mips = graph.requestsMips(i + 1, root);
    const uint64_t bpp = producer.format == CoinRenderTextureFormat::RGBA16_FLOAT ? 8 : 4;
    uint64_t color = 0;
    if (!addAttachmentBytes(width, height, bpp, 1, color)) return false;
    uint64_t result = color;
    if (producer.cube) {
      CoinRenderTextureImageSnapshot atlas;
      if (!CoinRenderCubeTextureCore::describe(width, mips, atlas)) return false;
      result = 0;
      if (!addAttachmentBytes(atlas.width, atlas.height, 4, 1, result)) return false;
    } else if (mips && !add(CoinRenderTextureFormatCore::mipBytes(width, height, producer.format), result)) return false;
    if (!add(result, retained)) return false;
    uint64_t local = 0;
    if (direct) {
      // 2D outputs retain their own depth. A cube atlas additionally overlaps
      // six source face tokens until its assembly command completes.
      const uint64_t faces = producer.cube ? 6 : 1;
      if (producer.cube && !addAttachmentBytes(color, 1, 1, faces, retained)) return false;
      if (!addAttachmentBytes(width, height, 4, faces, retained)) return false;
      if (samples > 1) {
        if (executor == Executor::WGPU) {
          // Every submitted face owns its own sample attachments until its
          // fence; token retirement does not make their storage disappear.
          if (!addAttachmentBytes(width,height,bpp+4,uint64_t(samples)*faces,retained)) return false;
        } else if (producer.size != previousMsaaExtent) {
          // BGFX reuses its concrete MSAA pair at an unchanged extent. A size
          // transition retires that pair through the ordered destruction queue.
          if (!addAttachmentBytes(width,height,bpp+4,samples,retained)) return false;
          previousMsaaExtent = producer.size;
        }
      }
      if (mips && !add(result, local)) return false; // private mip source during generation
    } else {
      // A staged producer retains CPU pixels and uploads their GPU image into
      // the consumer. Child attachments and aligned readback exist together.
      if (executor != Executor::CPU && !add(result, retained)) return false;
      if (!addAttachmentBytes(width, height, bpp + 4, samples, local) ||
          (samples > 1 && !add(color, local))) return false;
      const uint64_t pitch = ((uint64_t(width) * bpp + 255) / 256) * 256;
      if (!addAttachmentBytes(pitch, height, 1, 1, local)) return false;
      if (producer.cube && !addAttachmentBytes(color, 1, 1, 6, local)) return false;
    }
    // Inspect producer feature scratch as well, even though its independent
    // camera correctly renders once rather than inheriting consumer AA.
    for (size_t face = 0; face < producer.faceCount(); ++face) {
      CoinRenderFramePlan feature = producer.face(face);
      feature.numPasses = 2;
      uint64_t footprint = 0, bare = 0;
      CoinRenderFramePlan empty;
      empty.numPasses = 2; empty.outputColorFormat = feature.outputColorFormat;
      if (!requiredBytes(feature, producer.size, samples, footprint, executor) ||
          !requiredBytes(empty, producer.size, samples, bare, executor) || footprint < bare ||
          !add(footprint - bare, local)) return false;
    }
    if (direct) {
      // Producer-specific VSM/composition/mip work can remain queue-pending
      // while the consumer executes. Staged children complete their readback
      // before the next child and therefore share a peak temporary envelope.
      if (!add(local,retained)) return false;
    } else transient = std::max(transient, local);
  }
  if (direct && samples > 1 && executor == Executor::BGFX && previousMsaaExtent != extent &&
      !addAttachmentBytes(extent[0],extent[1],8,samples,retained)) return false;
  return add(retained, total) && add(transient, total);
}
}
