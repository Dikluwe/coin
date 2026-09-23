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
};

// Private Coin 4 profile: each draw has uniform material alpha. Textured draws
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
      if (vertex.materialSlot >= frame.materials.size() ||
          std::abs(frame.materials[vertex.materialSlot].diffuse[3] - alpha) > 1.0e-6f) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Draw " << i << " has mixed per-vertex material alpha";
        diagnostic = ss.str();
        return false;
      }
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
        const std::vector<uint8_t> & pixels = frame.textures[rs.textureImageSlot].pixelsRgba;
        cached = 0;
        for (size_t byte = 3; byte < pixels.size(); byte += 4) {
          if (pixels[byte] != 255) { cached = 1; break; }
        }
      }
      textureAlpha = cached != 0;
    }
    SoWgpuCompositionItem item;
    item.drawIndex = i;
    item.blend = alpha < 1.0f || textureAlpha;
    item.eyeDepth = count ? static_cast<float>(depthSum / static_cast<double>(count)) : 0.0f;
    if (!std::isfinite(item.eyeDepth)) {
      diagnostic = "Invalid non-finite average eye depth in composition order";
      return false;
    }
    if (item.blend) {
      if (draw.topology != PrimitiveTopology::TRIANGLE_LIST) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Draw " << i << " is a transparent line or point";
        diagnostic = ss.str();
        return false;
      }
      if (rs.transparencyType != SoGLRenderAction::SORTED_OBJECT_BLEND) {
        std::ostringstream ss;
        ss << "UNSUPPORTED: Draw " << i << " requires SoTransparencyType::SORTED_OBJECT_BLEND";
        diagnostic = ss.str();
        return false;
      }
    }
    order.push_back(item);
  }
  std::stable_sort(order.begin(), order.end(), [](const SoWgpuCompositionItem & a,
                                                   const SoWgpuCompositionItem & b) {
    if (a.blend != b.blend) return !a.blend;
    return a.blend && a.eyeDepth > b.eyeDepth;
  });
  diagnostic.clear();
  return true;
}

#endif // COIN_SOWGPUCOMPOSITION_H
