#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuBgfxCore.h"
#include "rendering/wgpu/SoWgpuComposition.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace {
bool finiteMatrix(const SbMatrix & matrix)
{
  const float (*value)[4] = matrix.getValue();
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      if (!std::isfinite(value[row][col])) return false;
    }
  }
  return true;
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
    diagnostic = "BGFX evaluation requires a nonzero target up to 16384 pixels per side";
    return false;
  }
  if (!frame.isValid(&diagnostic)) return false;
  std::vector<SoWgpuCompositionItem> order;
  if (!coin_wgpu_composition_order(frame, order, diagnostic)) return false;

  for (const SoWgpuCompositionItem & item : order) {
    const DrawPacket & draw = frame.draws[item.drawIndex];
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
    float maxAlpha = 0.0f;
    for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
      const uint32_t index = frame.indices[draw.geometry.firstIndex + j];
      if (index < draw.geometry.firstVertex ||
          index >= draw.geometry.firstVertex + draw.geometry.vertexCount) {
        diagnostic = "BGFX draw index escapes its vertex range";
        return false;
      }
      const MaterialSnapshot & material = frame.materials[frame.vertices[index].materialSlot];
      if (!std::isfinite(material.diffuse[0]) || !std::isfinite(material.diffuse[1]) ||
          !std::isfinite(material.diffuse[2]) ||
          !std::isfinite(material.diffuse[3])) {
        diagnostic = "BGFX evaluation received non-finite material color";
        return false;
      }
      if (material.diffuse[3] > maxAlpha) maxAlpha = material.diffuse[3];
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
    lowered.blend = item.blend;
    lowered.alpha = maxAlpha;
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
  for (int channel = 0; channel < 4; ++channel) {
    candidate.clearColor[channel] = frame.clearColor[channel];
  }
  output = std::move(candidate);
  return true;
}

bool
SoWgpuBgfxCore::patchCamera(const FramePlan & frame, bool homogeneousDepth,
                            const SoWgpuBgfxPlan & base,
                            std::vector<SoWgpuBgfxDraw> & output,
                            std::string & diagnostic)
{
  diagnostic.clear();
  if (frame.draws.size() != base.draws.size()) {
    diagnostic = "BGFX camera patch changed the draw count";
    return false;
  }
  for (const SoWgpuBgfxDraw & draw : base.draws) {
    if (draw.blend) {
      diagnostic = "BGFX camera patch rebuilds transparent object order";
      return false;
    }
  }
  for (int channel = 0; channel < 4; ++channel) {
    if (!std::isfinite(frame.clearColor[channel])) {
      diagnostic = "BGFX camera patch has a non-finite clear color";
      return false;
    }
  }
  for (int channel = 0; channel < 4; ++channel) {
    if (frame.clearColor[channel] != base.clearColor[channel]) {
      diagnostic = "BGFX camera patch changed the clear color";
      return false;
    }
  }
  std::vector<SoWgpuBgfxDraw> candidate = base.draws;
  const SbMatrix clipConversion(
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 0.5f, 1.0f);
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const DrawPacket & draw = frame.draws[i];
    const SoWgpuBgfxDraw & previous = base.draws[i];
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "BGFX camera patch has an invalid render-state slot";
      return false;
    }
    const RenderStateSnapshot & state = frame.renderStates[draw.renderStateSlot];
    if (draw.topology != PrimitiveTopology::TRIANGLE_LIST ||
        draw.geometry.firstVertex != previous.firstVertex ||
        draw.geometry.vertexCount != previous.vertexCount ||
        draw.geometry.firstIndex != previous.firstIndex ||
        draw.geometry.indexCount != previous.indexCount ||
        state.cullMode != previous.cullMode ||
        state.frontFace != previous.frontFace ||
        state.lightModel != LightModel::BASE_COLOR ||
        state.hasTexture || state.fogMode != FogMode::NONE) {
      diagnostic = "BGFX camera patch changed non-camera draw state";
      return false;
    }
    if (!finiteMatrix(state.model) || !finiteMatrix(state.view) ||
        !finiteMatrix(state.projectionCoin)) {
      diagnostic = "BGFX camera patch has a non-finite matrix";
      return false;
    }

    const SbMatrix projection = homogeneousDepth
      ? state.projectionCoin : state.projectionCoin * clipConversion;
    const SbMatrix mvp = state.model * state.view * projection;
    std::memcpy(candidate[i].mvp, mvp.getValue(), sizeof(candidate[i].mvp));
  }
  output.swap(candidate);
  return true;
}
