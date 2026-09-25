#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuBgfxCore.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace {
uint32_t byteColor(float value)
{
  return static_cast<uint32_t>(std::lround(std::max(0.0f, std::min(1.0f, value)) * 255.0f));
}

}

bool
SoWgpuBgfxCore::lower(const FramePlan & frame, int width, int height,
                      bool homogeneousDepth, SoWgpuBgfxPlan & output,
                      std::string & diagnostic)
{
  diagnostic.clear();
  SoWgpuBgfxPlan candidate;
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
    diagnostic = "BGFX evaluation requires a nonzero offscreen target up to 16384 pixels per side";
    return false;
  }
  if (!frame.isValid(&diagnostic)) return false;

  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const DrawPacket & draw = frame.draws[i];
    const RenderStateSnapshot & state = frame.renderStates[draw.renderStateSlot];
    const ViewportSnapshot & viewport = frame.viewports[state.viewportSlot];
    if ((state.cullMode != CullMode::NONE && state.cullMode != CullMode::BACK &&
         state.cullMode != CullMode::FRONT) ||
        (state.frontFace != FrontFace::CCW && state.frontFace != FrontFace::CW)) {
      diagnostic = "BGFX evaluation received an unknown face/cull state";
      return false;
    }
    if (draw.topology != PrimitiveTopology::TRIANGLE_LIST ||
        state.lightModel != LightModel::BASE_COLOR || state.hasTexture ||
        state.fogMode != FogMode::NONE ||
        viewport.x != 0 || viewport.y != 0 ||
        viewport.width != width || viewport.height != height) {
      diagnostic = "BGFX evaluation supports only untextured BASE_COLOR triangles in the full viewport";
      return false;
    }
    if (draw.geometry.indexCount == 0 || draw.geometry.vertexCount == 0) {
      diagnostic = "BGFX evaluation requires nonempty indexed triangles";
      return false;
    }
    for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
      const uint32_t index = frame.indices[draw.geometry.firstIndex + j];
      if (index < draw.geometry.firstVertex ||
          index >= draw.geometry.firstVertex + draw.geometry.vertexCount) {
        diagnostic = "BGFX draw index escapes its vertex range";
        return false;
      }
      const MaterialSnapshot & material = frame.materials[frame.vertices[index].materialSlot];
      if (!std::isfinite(material.diffuse[0]) || !std::isfinite(material.diffuse[1]) ||
          !std::isfinite(material.diffuse[2])) {
        diagnostic = "BGFX evaluation received non-finite material color";
        return false;
      }
      if (material.diffuse[3] < 1.0f) {
        diagnostic = "BGFX evaluation does not support transparent materials";
        return false;
      }
    }
    const SbMatrix clipConversion(
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f);
    const SbMatrix projection = homogeneousDepth
      ? state.projectionCoin : state.projectionCoin * clipConversion;
    const SbMatrix mvp = state.model * state.view * projection;
    SoWgpuBgfxDraw lowered{};
    std::memcpy(lowered.mvp, mvp.getValue(), sizeof(lowered.mvp));
    lowered.firstVertex = draw.geometry.firstVertex;
    lowered.vertexCount = draw.geometry.vertexCount;
    lowered.firstIndex = draw.geometry.firstIndex;
    lowered.indexCount = draw.geometry.indexCount;
    lowered.cullMode = state.cullMode;
    lowered.frontFace = state.frontFace;
    candidate.draws.push_back(lowered);
  }

  candidate.vertices.reserve(frame.vertices.size());
  for (const VertexSnapshot & src : frame.vertices) {
    SoWgpuBgfxVertex dst{};
    std::memcpy(dst.position, src.position, sizeof(dst.position));
    std::memcpy(dst.color, frame.materials[src.materialSlot].diffuse,
                sizeof(dst.color));
    candidate.vertices.push_back(dst);
  }
  candidate.indices = frame.indices;
  float clear[4] = {frame.clearColor[0], frame.clearColor[1],
                    frame.clearColor[2], frame.clearColor[3]};
  candidate.clearRgba = (byteColor(clear[0]) << 24) |
                        (byteColor(clear[1]) << 16) |
                        (byteColor(clear[2]) << 8) | byteColor(clear[3]);
  output = std::move(candidate);
  return true;
}
