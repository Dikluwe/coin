#ifndef COIN_WGPU_SHADOW_FRAME_H
#define COIN_WGPU_SHADOW_FRAME_H

#include "rendering/coinrender/CoinRenderShadowCore.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// Mechanical conversion of Core-resolved view-space lighting to the wgpu ABI.
inline CoinWgpuLight
coin_wgpu_pack_light(const CoinRenderLightSourceSnapshot & light)
{
  CoinWgpuLight packed{};
  for (int c = 0; c < 3; ++c) {
    packed.position_type[c] = light.position[c];
    packed.direction_cutoff[c] = light.direction[c];
    packed.color_intensity[c] = light.color[c];
  }
  packed.position_type[3] = static_cast<float>(light.type);
  packed.direction_cutoff[3] = std::cos(light.cutOffAngle);
  packed.color_intensity[3] = light.intensity;
  packed.attenuation_exponent[0] = light.attenuation[0];
  packed.attenuation_exponent[1] = light.attenuation[1];
  packed.attenuation_exponent[2] = light.attenuation[2];
  packed.attenuation_exponent[3] = light.dropOffRate * 128.0f;
  return packed;
}

// Infra transport for the Core-selected casters. These draws refer to the
// original frame vertex/index arrays, before composition reorders main draws.
struct CoinWgpuShadowPass {
  uint32_t mapSize = 0;
  uint32_t kind = 0; // 0=directional axial distance, 1=spot radial distance
  float nearDistance = 0.0f;
  float farDistance = 0.0f;
  float epsilon = 0.0f;
  float threshold = 0.0f;
  std::vector<CoinWgpuShadowDraw> casters;
  std::vector<CoinWgpuShadowReceiver> receivers;
};

struct CoinWgpuShadowFrame : CoinWgpuShadowPass {
  CoinWgpuShadowPass second;
  CoinWgpuShadowPass third;
  CoinWgpuShadowPass fourth;
  std::vector<CoinWgpuShadowPass> extra;
  bool hasSecond = false;
  bool hasThird = false;
  bool hasFourth = false;

  // Composition may clone a packed state to change depth policy. Its shadow
  // receiver stays identical; transport the same slot in every concrete pass.
  void appendReceiverState(uint32_t sourceSlot)
  {
    const auto append = [sourceSlot](CoinWgpuShadowPass & pass) {
      if (pass.receivers.empty()) return;
      const CoinWgpuShadowReceiver receiver = pass.receivers[sourceSlot];
      pass.receivers.push_back(receiver);
    };
    append(*this);
    if (this->hasSecond) append(this->second);
    if (this->hasThird) append(this->third);
    if (this->hasFourth) append(this->fourth);
    for (auto & pass : this->extra) append(pass);
  }

  bool prepare(const CoinRenderFramePlan & frame, std::string & diagnostic)
  {
    CoinWgpuShadowFrame candidate;
    if (frame.shadowGroups.empty()) {
      *this = std::move(candidate);
      diagnostic.clear();
      return true;
    }
    CoinRenderShadowPlan plan;
    if (!coin_render_plan_shadows(frame, plan, diagnostic)) return false;
    if (!coin_render_shadow_object_profile(
          frame, plan, plan.passes.size(), diagnostic)) return false;
    if (plan.passes.size() > 8) {
      diagnostic = "wgpu shadow encoder supports at most eight maps";
      return false;
    }
    candidate.hasSecond = plan.passes.size() > 1;
    candidate.hasThird = plan.passes.size() > 2;
    candidate.hasFourth = plan.passes.size() > 3;
    candidate.extra.resize(plan.passes.size() > 4 ? plan.passes.size() - 4 : 0);
    const SbMatrix clipConversion(
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f);
    for (size_t passSlot = 0; passSlot < plan.passes.size(); ++passSlot) {
      const auto & pass = plan.passes[passSlot];
      CoinWgpuShadowPass & packedPass = passSlot == 0
        ? static_cast<CoinWgpuShadowPass &>(candidate) :
          passSlot == 1 ? candidate.second :
          passSlot == 2 ? candidate.third :
          passSlot == 3 ? candidate.fourth : candidate.extra[passSlot - 4];
      packedPass.mapSize = pass.mapSize;
      packedPass.kind = frame.shadowLights[pass.lightSlot].type == CoinRenderLightType::SPOT ? 1u : 0u;
      packedPass.nearDistance = pass.nearDistance;
      packedPass.farDistance = pass.vsmFarDistance;
      packedPass.epsilon = pass.epsilon;
      packedPass.threshold = pass.threshold;
      const SbMatrix projectionWgpu = pass.projectionCoin * clipConversion;
      packedPass.receivers.resize(frame.renderStates.size());
      for (size_t stateSlot = 0; stateSlot < frame.renderStates.size(); ++stateSlot) {
        const auto & state = frame.renderStates[stateSlot];
        if (state.shadowGroupSlot != pass.groupSlot) continue;
        CoinWgpuShadowReceiver & receiver = packedPass.receivers[stateSlot];
        receiver.receives = coin_render_shadow_shading_flags(frame, state) |
          ((state.shadowStyle & 2u) != 0 && pass.lightingIndexByState[stateSlot] >= 0 ? 1u : 0u);
        receiver.lighting_index = pass.lightingIndexByState[stateSlot];
        receiver.max_shadow_distance = pass.maxShadowDistance;
        receiver.distance_falloff_coefficient = pass.distanceFalloffCoefficient;
        const SbMatrix modelView = passSlot < 2 ? state.model * pass.view :
          state.view.inverse() * pass.view;
        const SbMatrix mvp = modelView * projectionWgpu;
        std::memcpy(receiver.model_view, modelView.getValue(),
                    sizeof(receiver.model_view));
        std::memcpy(receiver.model_view_projection, mvp.getValue(),
                    sizeof(receiver.model_view_projection));
        for (const float value : receiver.model_view)
          if (!std::isfinite(value)) {
            diagnostic = "Shadow receiver has a non-finite model-view matrix";
            return false;
          }
        for (const float value : receiver.model_view_projection)
          if (!std::isfinite(value)) {
            diagnostic = "Shadow receiver has a non-finite projection";
            return false;
          }
      }
      for (const uint32_t drawSlot : pass.casterDraws) {
        if (drawSlot >= frame.draws.size()) {
          diagnostic = "Shadow caster references an invalid draw";
          return false;
        }
        const auto & draw = frame.draws[drawSlot];
        const uint64_t indexEnd =
          uint64_t(draw.geometry.firstIndex) + draw.geometry.indexCount;
        const uint64_t vertexEnd =
          uint64_t(draw.geometry.firstVertex) + draw.geometry.vertexCount;
        if (draw.renderStateSlot >= frame.renderStates.size() ||
            draw.geometry.indexCount == 0 ||
            draw.geometry.indexCount % 3 != 0 ||
            indexEnd > frame.indices.size() ||
            vertexEnd > frame.vertices.size()) {
          diagnostic = "Shadow caster has an invalid triangle range";
          return false;
        }
        for (uint64_t i = draw.geometry.firstIndex; i < indexEnd; ++i) {
          const uint32_t index = frame.indices[static_cast<size_t>(i)];
          if (index < draw.geometry.firstVertex || index >= vertexEnd) {
            diagnostic = "Shadow caster index is outside its vertex range";
            return false;
          }
        }
        CoinWgpuShadowDraw packed{};
        packed.first_index = draw.geometry.firstIndex;
        packed.index_count = draw.geometry.indexCount;
        packed.render_state_slot = draw.renderStateSlot;
        const SbMatrix modelView = frame.renderStates[draw.renderStateSlot].model *
                                   pass.view;
        const SbMatrix mvp = modelView * projectionWgpu;
        std::memcpy(packed.model_view, modelView.getValue(), sizeof(packed.model_view));
        std::memcpy(packed.model_view_projection, mvp.getValue(),
                    sizeof(packed.model_view_projection));
        for (const float value : packed.model_view)
          if (!std::isfinite(value)) {
            diagnostic = "Shadow caster has a non-finite model-view matrix";
            return false;
          }
        for (const float value : packed.model_view_projection)
          if (!std::isfinite(value)) {
            diagnostic = "Shadow caster has a non-finite projection";
            return false;
          }
        packedPass.casters.push_back(packed);
      }
    }
    *this = std::move(candidate);
    diagnostic.clear();
    return true;
  }
};

#endif // COIN_WGPU_SHADOW_FRAME_H
