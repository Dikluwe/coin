#ifndef COIN_RENDER_COMPOSITION_H
#define COIN_RENDER_COMPOSITION_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <Inventor/actions/SoGLRenderAction.h>
#include <algorithm>
#include <cmath>
#include <sstream>

struct CoinRenderCompositionItem {
  size_t drawIndex = 0;
  bool blend = false;
  bool deferred = false;
  bool additive = false;
  bool sortTriangles = false;
  bool sortObject = false;
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
coin_render_transparency_strategy(int32_t type,
  CoinRenderCompositionItem::TransparencyStrategy & strategy,
  const char * & name)
{
  switch (type) {
  case SoGLRenderAction::SCREEN_DOOR:
    strategy = CoinRenderCompositionItem::OBJECT; name = "SCREEN_DOOR -> object"; return true;
  case SoGLRenderAction::BLEND:
    strategy = CoinRenderCompositionItem::OBJECT; name = "BLEND -> object"; return true;
  case SoGLRenderAction::DELAYED_BLEND:
    strategy = CoinRenderCompositionItem::OBJECT; name = "DELAYED_BLEND -> object"; return true;
  case SoGLRenderAction::SORTED_OBJECT_BLEND:
    strategy = CoinRenderCompositionItem::OBJECT; name = "SORTED_OBJECT_BLEND -> object"; return true;
  case SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND:
    strategy = CoinRenderCompositionItem::WEIGHTED_OIT; name = "SORTED_OBJECT_SORTED_TRIANGLE_BLEND -> weighted_oit"; return true;
  case SoGLRenderAction::SORTED_LAYERS_BLEND:
    strategy = CoinRenderCompositionItem::SORTED_LAYERS; name = "SORTED_LAYERS_BLEND -> sorted_layers"; return true;
  default:
    name = "unsupported";
    return false;
  }
}

// Private Coin 4 profile: per-vertex material alpha is preserved. Textured draws
// with any non-opaque texel enter the blended pass. Object-level depth sorting
// is stable, but neither triangle-level sorting nor intersections are promised.
inline bool
coin_render_composition_order(const CoinRenderFramePlan & frame,
                            std::vector<CoinRenderCompositionItem> & order,
                            std::string & diagnostic,
                            bool exactCoin = false)
{
  order.clear();
  order.reserve(frame.draws.size());
  std::vector<int8_t> textureHasAlpha(frame.textures.size(), -1);
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const CoinRenderDrawPacket & draw = frame.draws[i];
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "Invalid render state in composition order";
      return false;
    }
    const CoinRenderRenderStateSnapshot & rs = frame.renderStates[draw.renderStateSlot];
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
    float minDepth = 0, maxDepth = 0;
    for (size_t j = first; j < first + count; ++j) {
      const uint32_t vertexIndex = frame.indices[j];
      if (vertexIndex >= frame.vertices.size()) {
        diagnostic = "Invalid vertex in composition order";
        return false;
      }
      const CoinRenderVertexSnapshot & vertex = frame.vertices[vertexIndex];
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
      float eyeDepth = -viewPosition[2];
      if (exactCoin && rs.polygonOffsetPrimitiveStyle != 1) {
        // Expanded strokes are in NDC. Recover their original eye-space depth
        // from the source camera rather than sorting on normalized depth.
        const auto & projection = frame.cameras[rs.cameraSlot].projectionMatrixCoin;
        SbVec3f eye;
        projection.inverse().multVecMatrix(SbVec3f(vertex.position), eye);
        eyeDepth = -eye[2];
      }
      if (j == first) minDepth = maxDepth = eyeDepth;
      else { minDepth = std::min(minDepth, eyeDepth); maxDepth = std::max(maxDepth, eyeDepth); }
      depthSum += eyeDepth;
    }
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      const CoinRenderTextureUnitSnapshot tex = coin_render_texture_unit(rs, unit);
      if (!tex.enabled) continue;
      if (tex.imageSlot >= frame.textures.size()) {
        diagnostic = "Invalid texture in composition order"; return false;
      }
      int8_t & cached = textureHasAlpha[tex.imageSlot];
      if (cached < 0) {
        const CoinRenderTextureImageSnapshot & texture = frame.textures[tex.imageSlot];
        cached = texture.gpuToken != 0 && !texture.gpuOpaque ? 1 : 0;
        for (size_t byte = 3; byte < texture.pixelsRgba.size(); byte += 4)
          if (texture.pixelsRgba[byte] != 255) { cached = 1; break; }
      }
      if (tex.model == CoinRenderTextureModel::REPLACE) materialAlpha = cached != 0;
      else if (tex.model != CoinRenderTextureModel::DECAL) materialAlpha = materialAlpha || cached != 0;
    }

    CoinRenderCompositionItem item;
    item.drawIndex = i;
    item.blend = materialAlpha;
    item.eyeDepth = count ? (exactCoin ? (minDepth + maxDepth) * 0.5f : static_cast<float>(depthSum / static_cast<double>(count))) : 0.0f;
    if (exactCoin && draw.hasSortingCenter) {
      SbVec3f center;
      frame.cameras[rs.cameraSlot].viewMatrix.multVecMatrix(SbVec3f(draw.sortingCenterWorld), center);
      item.eyeDepth = -center[2];
    }
    if (!std::isfinite(item.eyeDepth)) {
      diagnostic = "Invalid non-finite average eye depth in composition order";
      return false;
    }
    if (exactCoin) {
      switch (rs.transparencyType) {
      case SoGLRenderAction::NONE:
      case SoGLRenderAction::SCREEN_DOOR: item.blend = false; break;
      case SoGLRenderAction::ADD: item.additive = true; break;
      case SoGLRenderAction::BLEND: break;
      case SoGLRenderAction::DELAYED_ADD: item.additive = true; item.deferred = item.blend; break;
      case SoGLRenderAction::DELAYED_BLEND: item.deferred = item.blend; break;
      case SoGLRenderAction::SORTED_OBJECT_ADD: item.additive = true; item.sortObject = true; item.deferred = item.blend; break;
      case SoGLRenderAction::SORTED_OBJECT_BLEND: item.sortObject = true; item.deferred = item.blend; break;
      case SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD:
        item.additive = true; item.sortTriangles = true; item.sortObject = true; item.deferred = item.blend; break;
      case SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND:
        item.sortTriangles = true; item.sortObject = true; item.deferred = item.blend; break;
      case SoGLRenderAction::SORTED_LAYERS_BLEND:
        item.deferred = item.blend; item.transparencyStrategy = CoinRenderCompositionItem::SORTED_LAYERS; break;
      default: diagnostic = "Unknown Coin transparency mode"; return false;
      }
    } else if (item.blend) {
      const char * mapping = NULL;
      if (!coin_render_transparency_strategy(rs.transparencyType,
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
  std::stable_sort(order.begin(), order.end(), [&frame, exactCoin](const CoinRenderCompositionItem & a,
                                                   const CoinRenderCompositionItem & b) {
    const uint32_t layerA = frame.draws[a.drawIndex].renderLayer;
    const uint32_t layerB = frame.draws[b.drawIndex].renderLayer;
    if (layerA != layerB) return layerA < layerB;
    // Overlay layers follow immediate traversal order: their opaque depth
    // writers and translucent labels can depend on the exact submission order.
    if (layerA != 0) return false;
    if (exactCoin) {
      if (a.deferred != b.deferred) return !a.deferred;
      // Coin renders its sorted path list before its unsorted delayed list.
      if (!a.deferred) return false;
      if (a.sortObject != b.sortObject) return a.sortObject;
      return a.sortObject && a.eyeDepth > b.eyeDepth;
    }
    if (a.blend != b.blend) return !a.blend;
    return a.blend && a.eyeDepth > b.eyeDepth;
  });
  diagnostic.clear();
  return true;
}

#endif // COIN_RENDER_COMPOSITION_H
