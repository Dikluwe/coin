#ifndef COIN_RENDER_COMPOSITION_H
#define COIN_RENDER_COMPOSITION_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderTextureCombineCore.h"
#include <Inventor/actions/SoGLRenderAction.h>
#include <algorithm>
#include <cmath>

struct CoinRenderCompositionItem {
  size_t drawIndex = 0;
  uint32_t firstIndex = 0, indexCount = 0;
  bool blend = false;
  bool deferred = false;
  bool additive = false;
  bool sortTriangles = false;
  bool sortObject = false;
  float eyeDepth = 0.0f;
  bool screenDoor = false;
  uint32_t screenDoorLevel = 0;
  bool depthTest = true;
  bool depthWrite = true;
  CoinRenderDepthFunction depthFunction = CoinRenderDepthFunction::LESS;
  float depthRange[2] = {0.0f, 1.0f};
  enum TransparencyStrategy { OBJECT, WEIGHTED_OIT, SORTED_LAYERS } transparencyStrategy = OBJECT;
};

// Names describe the Coin operation; GPU algorithms are selected by Infra.
inline bool coin_render_transparency_strategy(
    int32_t type, CoinRenderCompositionItem::TransparencyStrategy& strategy, const char*& name) {
  strategy = CoinRenderCompositionItem::OBJECT;
  switch (type) {
#define COIN_RENDER_MODE(mode)                                                                     \
  case SoGLRenderAction::mode:                                                                     \
    name = #mode " -> object";                                                                     \
    return true
    COIN_RENDER_MODE(NONE);
    COIN_RENDER_MODE(SCREEN_DOOR);
    COIN_RENDER_MODE(ADD);
    COIN_RENDER_MODE(BLEND);
    COIN_RENDER_MODE(DELAYED_ADD);
    COIN_RENDER_MODE(DELAYED_BLEND);
    COIN_RENDER_MODE(SORTED_OBJECT_ADD);
    COIN_RENDER_MODE(SORTED_OBJECT_BLEND);
    COIN_RENDER_MODE(SORTED_OBJECT_SORTED_TRIANGLE_ADD);
    COIN_RENDER_MODE(SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
#undef COIN_RENDER_MODE
  case SoGLRenderAction::SORTED_LAYERS_BLEND:
    strategy = CoinRenderCompositionItem::SORTED_LAYERS;
    name = "SORTED_LAYERS_BLEND -> sorted_layers";
    return true;
  default:
    name = "unsupported";
    return false;
  }
}

// Common Coin policy: classify alpha, resolve immediate/delayed/sorted draws,
// preserve annotation traversal, and apply transparent-pass depth defaults.
// This function consumes captured data only; it never traverses or submits.
inline bool coin_render_composition_order(const CoinRenderFramePlan& frame,
                                          std::vector<CoinRenderCompositionItem>& order,
                                          std::string& diagnostic) {
  order.clear();
  order.reserve(frame.draws.size());
  for (const auto& material : frame.materials) {
    const float alpha = material.diffuse[3], transparency = material.transparency;
    if (!std::isfinite(alpha) || !std::isfinite(transparency) || alpha < 0 || alpha > 1 ||
        transparency < 0 || transparency > 1 || std::abs(alpha + transparency - 1.0f) > 1.0e-5f) {
      diagnostic = "Invalid or inconsistent material alpha/transparency";
      return false;
    }
  }
  std::vector<int8_t> textureHasAlpha(frame.textures.size(), -1);
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const CoinRenderDrawPacket& draw = frame.draws[i];
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "Invalid render state in composition order";
      return false;
    }
    const CoinRenderRenderStateSnapshot& rs = frame.renderStates[draw.renderStateSlot];
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
    float minDepth = 0, maxDepth = 0;
    for (size_t j = first; j < first + count; ++j) {
      const uint32_t vertexIndex = frame.indices[j];
      if (vertexIndex >= frame.vertices.size()) {
        diagnostic = "Invalid vertex in composition order";
        return false;
      }
      const CoinRenderVertexSnapshot& vertex = frame.vertices[vertexIndex];
      if (vertex.materialSlot >= frame.materials.size()) {
        diagnostic = "Invalid vertex material in composition order";
        return false;
      }
      materialAlpha = materialAlpha || frame.materials[vertex.materialSlot].diffuse[3] < 1.0f;
      SbVec3f viewPosition;
      modelView.multVecMatrix(SbVec3f(vertex.position[0], vertex.position[1], vertex.position[2]),
                              viewPosition);
      if (!std::isfinite(viewPosition[2])) {
        diagnostic = "Invalid non-finite eye depth in composition order";
        return false;
      }
      float eyeDepth = -viewPosition[2];
      if (rs.polygonOffsetPrimitiveStyle != 1) {
        // Expanded strokes are in NDC. Recover their original eye-space depth
        // from the source camera rather than sorting on normalized depth.
        if (rs.cameraSlot >= frame.cameras.size()) {
          diagnostic = "Invalid stroke camera in composition order";
          return false;
        }
        const auto& projection = frame.cameras[rs.cameraSlot].projectionMatrixCoin;
        SbVec3f eye;
        projection.inverse().multVecMatrix(SbVec3f(vertex.position), eye);
        eyeDepth = -eye[2];
      }
      if (j == first)
        minDepth = maxDepth = eyeDepth;
      else {
        minDepth = std::min(minDepth, eyeDepth);
        maxDepth = std::max(maxDepth, eyeDepth);
      }
    }
    const bool primaryAlpha = materialAlpha;
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      const CoinRenderTextureUnitSnapshot tex = coin_render_texture_unit(rs, unit);
      if (!tex.enabled)
        continue;
      if (tex.imageSlot >= frame.textures.size()) {
        diagnostic = "Invalid texture in composition order";
        return false;
      }
      int8_t& cached = textureHasAlpha[tex.imageSlot];
      if (cached < 0) {
        const CoinRenderTextureImageSnapshot& texture = frame.textures[tex.imageSlot];
        cached = texture.gpuToken != 0 && !texture.gpuOpaque ? 1 : 0;
        for (size_t byte = 3; byte < texture.pixelsRgba.size(); byte += 4)
          if (texture.pixelsRgba[byte] != 255) {
            cached = 1;
            break;
          }
      }
      if (rs.textureCombines[unit].instructions[0][0] > .5f)
        materialAlpha = coin_render_combine_may_have_alpha(rs.textureCombines[unit], primaryAlpha,
                                                           cached != 0, materialAlpha);
      else if (tex.model == CoinRenderTextureModel::REPLACE)
        materialAlpha = cached != 0;
      else if (tex.model != CoinRenderTextureModel::DECAL)
        materialAlpha = materialAlpha || cached != 0;
    }

    CoinRenderCompositionItem item;
    item.drawIndex = i;
    item.firstIndex = draw.geometry.firstIndex;
    item.indexCount = draw.geometry.indexCount;
    item.blend = materialAlpha;
    item.eyeDepth = count ? (minDepth + maxDepth) * 0.5f : 0.0f;
    if (draw.hasSortingCenter) {
      if (rs.cameraSlot >= frame.cameras.size()) {
        diagnostic = "Invalid sorting camera in composition order";
        return false;
      }
      SbVec3f center;
      frame.cameras[rs.cameraSlot].viewMatrix.multVecMatrix(SbVec3f(draw.sortingCenterWorld),
                                                            center);
      item.eyeDepth = -center[2];
    }
    if (!std::isfinite(item.eyeDepth)) {
      diagnostic = "Invalid non-finite average eye depth in composition order";
      return false;
    }
    {
      switch (rs.transparencyType) {
      case SoGLRenderAction::NONE:
      case SoGLRenderAction::SCREEN_DOOR:
        item.blend = false;
        item.screenDoor = rs.transparencyType == SoGLRenderAction::SCREEN_DOOR;
        if (item.screenDoor && rs.polygonOffsetPrimitiveStyle == 1) {
          const float transparency = rs.screenDoorTransparency >= 0
                                         ? rs.screenDoorTransparency
                                         : frame.materials[rs.materialSlot].transparency;
          if (!std::isfinite(transparency)) {
            diagnostic = "Invalid non-finite screen-door transparency";
            return false;
          }
          item.screenDoorLevel =
              static_cast<uint32_t>(std::min(64, std::max(0, int(transparency * 64.0f))));
        }
        break;
      case SoGLRenderAction::ADD:
        item.additive = true;
        break;
      case SoGLRenderAction::BLEND:
        break;
      case SoGLRenderAction::DELAYED_ADD:
        item.additive = true;
        item.deferred = item.blend;
        break;
      case SoGLRenderAction::DELAYED_BLEND:
        item.deferred = item.blend;
        break;
      case SoGLRenderAction::SORTED_OBJECT_ADD:
        item.additive = true;
        item.sortObject = true;
        item.deferred = item.blend;
        break;
      case SoGLRenderAction::SORTED_OBJECT_BLEND:
        item.sortObject = true;
        item.deferred = item.blend;
        break;
      case SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD:
        item.additive = true;
        item.sortTriangles = true;
        item.sortObject = true;
        item.deferred = item.blend;
        break;
      case SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND:
        item.sortTriangles = true;
        item.sortObject = true;
        item.deferred = item.blend;
        break;
      case SoGLRenderAction::SORTED_LAYERS_BLEND:
        item.deferred = item.blend;
        item.transparencyStrategy = CoinRenderCompositionItem::SORTED_LAYERS;
        break;
      default:
        diagnostic = "Unknown Coin transparency mode";
        return false;
      }
    }
    // Annotation paths execute immediately, including translucent depth writers.
    item.deferred = item.deferred && draw.renderLayer == 0;
    item.sortTriangles = item.sortTriangles && rs.polygonOffsetPrimitiveStyle == 1;
    item.depthTest = rs.depthTest;
    item.depthWrite = rs.depthWrite;
    item.depthFunction = rs.depthFunction;
    item.depthRange[0] = rs.depthRange[0];
    item.depthRange[1] = rs.depthRange[1];
    // SoGLRenderAction::renderSingle changes these defaults for the transparent
    // traversal. Explicit SoDepthBuffer fields are reapplied by that traversal.
    if (item.deferred) {
      if (!(rs.explicitDepthMask & 1))
        item.depthTest = true;
      if (!(rs.explicitDepthMask & 2))
        item.depthWrite = false;
      if (!(rs.explicitDepthMask & 4))
        item.depthFunction = CoinRenderDepthFunction::LEQUAL;
      if (!(rs.explicitDepthMask & 8)) {
        item.depthRange[0] = 0;
        item.depthRange[1] = 1;
      }
    }
    order.push_back(item);
  }
  std::stable_sort(
      order.begin(), order.end(),
      [&frame](const CoinRenderCompositionItem& a, const CoinRenderCompositionItem& b) {
        const uint32_t layerA = frame.draws[a.drawIndex].renderLayer;
        const uint32_t layerB = frame.draws[b.drawIndex].renderLayer;
        if (layerA != layerB)
          return layerA < layerB;
        // Overlay layers follow immediate traversal order: their opaque depth
        // writers and translucent labels can depend on the exact submission order.
        if (layerA != 0)
          return false;
        if (a.deferred != b.deferred)
          return !a.deferred;
        // Coin renders its sorted path list before its unsorted delayed list.
        if (!a.deferred)
          return false;
        if (a.sortObject != b.sortObject)
          return a.sortObject;
        return a.sortObject && a.eyeDepth > b.eyeDepth;
      });
  diagnostic.clear();
  return true;
}

// Coin's 32x32 Bayer matrix, built from {0,2,3,1}; framebuffer origin is bottom-left.
inline uint32_t coin_render_screen_door_rank(uint32_t x, uint32_t y) {
  uint32_t rank = 0, weight = 256;
  for (int bit = 0; bit < 5; ++bit) {
    const uint32_t bx = x & 1, by = y & 1;
    rank += (by ? 3 - 2 * bx : 2 * bx) * weight;
    x >>= 1;
    y >>= 1;
    weight >>= 2;
  }
  return rank;
}

// Expand sorted requests into triangle ranges, including material runs of one
// shape. Executors consume this sequence; no backend sorts Coin triangles.
inline bool coin_render_composition_schedule(const CoinRenderFramePlan& frame,
                                             std::vector<CoinRenderCompositionItem>& schedule,
                                             std::string& diagnostic) {
  std::vector<CoinRenderCompositionItem> order;
  if (!coin_render_composition_order(frame, order, diagnostic))
    return false;
  schedule.clear();
  for (size_t begin = 0; begin < order.size();) {
    const auto& first = order[begin];
    const auto& source = frame.draws[first.drawIndex];
    if (!first.blend || !first.sortTriangles) {
      schedule.push_back(first);
      ++begin;
      continue;
    }
    size_t end = begin + 1;
    if (source.sourceNodeId && source.renderLayer == 0)
      while (end < order.size()) {
        const auto& next = order[end];
        const auto& draw = frame.draws[next.drawIndex];
        const auto& a = frame.renderStates[source.renderStateSlot];
        const auto& b = frame.renderStates[draw.renderStateSlot];
        if (!next.blend || !next.sortTriangles || draw.sourceNodeId != source.sourceNodeId ||
            draw.renderLayer != source.renderLayer || a.model != b.model || a.view != b.view)
          break;
        ++end;
      }
    std::vector<CoinRenderCompositionItem> triangles;
    for (size_t i = begin; i < end; ++i) {
      const auto& item = order[i];
      const auto& draw = frame.draws[item.drawIndex];
      const auto& state = frame.renderStates[draw.renderStateSlot];
      if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST || item.indexCount % 3) {
        diagnostic = "Triangle sorting requires complete triangle lists";
        return false;
      }
      const SbMatrix mv = state.model * state.view;
      for (uint32_t index = item.firstIndex; index < item.firstIndex + item.indexCount;
           index += 3) {
        auto triangle = item;
        triangle.firstIndex = index;
        triangle.indexCount = 3;
        triangle.eyeDepth = 0;
        for (uint32_t v = 0; v < 3; ++v) {
          SbVec3f eye;
          mv.multVecMatrix(SbVec3f(frame.vertices[frame.indices[index + v]].position), eye);
          triangle.eyeDepth -= eye[2] / 3;
        }
        if (!std::isfinite(triangle.eyeDepth)) {
          diagnostic = "Non-finite triangle sorting depth";
          return false;
        }
        triangles.push_back(triangle);
      }
    }
    std::stable_sort(triangles.begin(), triangles.end(),
                     [](const CoinRenderCompositionItem& a, const CoinRenderCompositionItem& b) {
                       return a.eyeDepth > b.eyeDepth;
                     });
    schedule.insert(schedule.end(), triangles.begin(), triangles.end());
    begin = end;
  }
  diagnostic.clear();
  return true;
}

#endif // COIN_RENDER_COMPOSITION_H
