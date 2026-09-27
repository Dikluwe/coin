#ifndef COIN_SOWGPUCOMPOSITION_H
#define COIN_SOWGPUCOMPOSITION_H

#include "rendering/wgpu/SoWgpuFramePlan.h"
#include <Inventor/actions/SoGLRenderAction.h>
#include <algorithm>
#include <cmath>
#include <sstream>

struct SoWgpuCompositionItem {
  size_t drawIndex = 0;
  bool blend = false;
  float eyeDepth = 0.0f;
  enum TransparencyStrategy {
    OBJECT,
    WEIGHTED_OIT,
    SORTED_LAYERS
  } transparencyStrategy = OBJECT;
};

// Explicit compatibility mapping for the blend modes supported by the
// experimental renderer. Additive modes deliberately remain unsupported:
// mapping those to source-over blending would silently change their meaning.
inline bool
coin_wgpu_transparency_strategy(int32_t type,
  SoWgpuCompositionItem::TransparencyStrategy & strategy,
  const char * & name)
{
  switch (type) {
  case SoGLRenderAction::SCREEN_DOOR:
    strategy = SoWgpuCompositionItem::OBJECT; name = "SCREEN_DOOR -> object"; return true;
  case SoGLRenderAction::BLEND:
    strategy = SoWgpuCompositionItem::OBJECT; name = "BLEND -> object"; return true;
  case SoGLRenderAction::DELAYED_BLEND:
    strategy = SoWgpuCompositionItem::OBJECT; name = "DELAYED_BLEND -> object"; return true;
  case SoGLRenderAction::SORTED_OBJECT_BLEND:
    strategy = SoWgpuCompositionItem::OBJECT; name = "SORTED_OBJECT_BLEND -> object"; return true;
  case SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND:
    strategy = SoWgpuCompositionItem::WEIGHTED_OIT; name = "SORTED_OBJECT_SORTED_TRIANGLE_BLEND -> weighted_oit"; return true;
  case SoGLRenderAction::SORTED_LAYERS_BLEND:
    strategy = SoWgpuCompositionItem::SORTED_LAYERS; name = "SORTED_LAYERS_BLEND -> sorted_layers"; return true;
  default:
    name = "unsupported";
    return false;
  }
}

// Private Coin 4 profile: per-vertex material alpha is preserved. Textured draws
// with any non-opaque texel enter the blended pass. Object-level depth sorting
// is stable, but neither triangle-level sorting nor intersections are promised.
inline bool
coin_wgpu_composition_order(const FramePlan & frame,
                            std::vector<SoWgpuCompositionItem> & order,
                            std::string & diagnostic)
{
  order.clear();
  order.reserve(frame.draws.size());
  std::vector<int8_t> textureHasAlpha(frame.textures.size(), -1);
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const DrawPacket & draw = frame.draws[i];
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "Invalid render state in composition order";
      return false;
    }
    const RenderStateSnapshot & rs = frame.renderStates[draw.renderStateSlot];
    if (rs.materialSlot >= frame.materials.size()) {
      diagnostic = "Invalid material in composition order";
      return false;
    }
    const float alpha = frame.materials[rs.materialSlot].diffuse[3];
    bool materialAlpha = alpha < 1.0f;
    const size_t first = draw.geometry.firstIndex;
    const size_t count = draw.geometry.indexCount;
    if (first > frame.indices.size() || count > frame.indices.size() - first) {
      diagnostic = "Invalid index range in composition order";
      return false;
    }
    SbMatrix modelView = rs.model * rs.view;
    double depthSum = 0.0;
    for (size_t j = first; j < first + count; ++j) {
      const uint32_t vertexIndex = frame.indices[j];
      if (vertexIndex >= frame.vertices.size()) {
        diagnostic = "Invalid vertex in composition order";
        return false;
      }
      const VertexSnapshot & vertex = frame.vertices[vertexIndex];
      if (vertex.materialSlot >= frame.materials.size()) {
        diagnostic = "Invalid vertex material in composition order";
        return false;
      }
      materialAlpha = materialAlpha || frame.materials[vertex.materialSlot].diffuse[3] < 1.0f;
      SbVec3f viewPosition;
      modelView.multVecMatrix(SbVec3f(vertex.position[0], vertex.position[1], vertex.position[2]), viewPosition);
      if (!std::isfinite(viewPosition[2])) {
        diagnostic = "Invalid non-finite eye depth in composition order";
        return false;
      }
      depthSum += -static_cast<double>(viewPosition[2]);
    }
    bool textureAlpha = false;
    if (rs.hasTexture) {
      if (rs.textureImageSlot >= frame.textures.size()) {
        diagnostic = "Invalid texture in composition order";
        return false;
      }
      int8_t & cached = textureHasAlpha[rs.textureImageSlot];
      if (cached < 0) {
        const TextureImageSnapshot & texture = frame.textures[rs.textureImageSlot];
        const std::vector<uint8_t> & pixels = texture.pixelsRgba;
        cached = texture.gpuToken != 0 && !texture.gpuOpaque ? 1 : 0;
        for (size_t byte = 3; byte < pixels.size(); byte += 4) {
          if (pixels[byte] != 255) { cached = 1; break; }
        }
      }
      textureAlpha = cached != 0;
    }
    if (rs.hasTexture && rs.textureModel == TextureModel::REPLACE) materialAlpha = false;
    if (rs.hasTexture && rs.textureModel == TextureModel::DECAL) textureAlpha = false;

    SoWgpuCompositionItem item;
    item.drawIndex = i;
    item.blend = materialAlpha || textureAlpha;
    item.eyeDepth = count ? static_cast<float>(depthSum / static_cast<double>(count)) : 0.0f;
    if (!std::isfinite(item.eyeDepth)) {
      diagnostic = "Invalid non-finite average eye depth in composition order";
      return false;
    }
    if (item.blend) {
      const char * mapping = NULL;
      if (!coin_wgpu_transparency_strategy(rs.transparencyType,
                                            item.transparencyStrategy,
                                            mapping)) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Draw " << i << " uses Coin transparency mode "
           << rs.transparencyType
           << "; supported mappings are SCREEN_DOOR/BLEND/DELAYED_BLEND/SORTED_OBJECT_BLEND -> object, "
              "SORTED_OBJECT_SORTED_TRIANGLE_BLEND -> weighted_oit, and SORTED_LAYERS_BLEND -> sorted_layers";
        diagnostic = ss.str();
        return false;
      }
    }
    order.push_back(item);
  }
  std::stable_sort(order.begin(), order.end(), [&frame](const SoWgpuCompositionItem & a,
                                                   const SoWgpuCompositionItem & b) {
    const uint32_t layerA = frame.draws[a.drawIndex].renderLayer;
    const uint32_t layerB = frame.draws[b.drawIndex].renderLayer;
    if (layerA != layerB) return layerA < layerB;
    // Overlay layers follow immediate traversal order: their opaque depth
    // writers and translucent labels can depend on the exact submission order.
    if (layerA != 0) return false;
    if (a.blend != b.blend) return !a.blend;
    return a.blend && a.eyeDepth > b.eyeDepth;
  });
  diagnostic.clear();
  return true;
}

#endif // COIN_SOWGPUCOMPOSITION_H
