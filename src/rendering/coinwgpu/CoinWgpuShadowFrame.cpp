#include "rendering/coinwgpu/CoinWgpuShadowFrame.h"
#include <Inventor/SbVec4f.h>
#include <cmath>
#include <cstring>
#include <utility>

CoinWgpuLight
coin_wgpu_pack_light(const CoinRenderLightSourceSnapshot &light)
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

void
CoinWgpuShadowFrame::appendReceiverState(uint32_t sourceSlot)
{
  const auto append = [sourceSlot](CoinWgpuShadowPass &pass) {
    if (pass.receivers.empty())
      return;
    const CoinWgpuShadowReceiver receiver = pass.receivers[sourceSlot];
    pass.receivers.push_back(receiver);
  };
  append(*this);
  if (this->hasSecond)
    append(this->second);
  if (this->hasThird)
    append(this->third);
  if (this->hasFourth)
    append(this->fourth);
  for (auto &pass : this->extra)
    append(pass);
}

bool
CoinWgpuShadowFrame::prepare(const CoinRenderFramePlan &frame, std::string &diagnostic)
{
  CoinWgpuShadowFrame candidate;
  if (frame.shadowGroups.empty()) {
    *this = std::move(candidate);
    diagnostic.clear();
    return true;
  }
  CoinRenderShadowPlan plan;
  if (!coin_render_plan_shadows(frame, plan, diagnostic))
    return false;
  if (!coin_render_shadow_object_profile(frame, plan, plan.passes.size(), diagnostic))
    return false;
  if (plan.passes.size() > 8) {
    diagnostic = "wgpu shadow encoder supports at most eight maps";
    return false;
  }
  candidate.hasSecond = plan.passes.size() > 1;
  candidate.hasThird = plan.passes.size() > 2;
  candidate.hasFourth = plan.passes.size() > 3;
  candidate.extra.resize(plan.passes.size() > 4 ? plan.passes.size() - 4 : 0);
  const SbMatrix clipConversion(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f,
                                0.0f, 0.0f, 0.0f, 0.5f, 1.0f);
  for (size_t passSlot = 0; passSlot < plan.passes.size(); ++passSlot) {
    const auto &pass = plan.passes[passSlot];
    CoinWgpuShadowPass &packedPass = passSlot == 0   ? static_cast<CoinWgpuShadowPass &>(candidate)
                                     : passSlot == 1 ? candidate.second
                                     : passSlot == 2 ? candidate.third
                                     : passSlot == 3 ? candidate.fourth
                                                     : candidate.extra[passSlot - 4];
    packedPass.mapSize = pass.mapSize;
    packedPass.kind =
        frame.shadowLights[pass.lightSlot].type == CoinRenderLightType::SPOT ? 1u : 0u;
    packedPass.nearDistance = pass.nearDistance;
    packedPass.farDistance = pass.vsmFarDistance;
    packedPass.epsilon = pass.epsilon;
    packedPass.threshold = pass.threshold;
    const SbMatrix projectionWgpu = pass.projectionCoin * clipConversion;
    packedPass.receivers.resize(frame.renderStates.size());
    for (size_t stateSlot = 0; stateSlot < frame.renderStates.size(); ++stateSlot) {
      const auto &state = frame.renderStates[stateSlot];
      if (state.shadowGroupSlot != pass.groupSlot)
        continue;
      CoinWgpuShadowReceiver &receiver = packedPass.receivers[stateSlot];
      // BASE_COLOR raster states may retain the authored lighting snapshot,
      // but packState publishes no lights for them. Only PHONG consumes a map
      // light; do not advertise an index outside the transported light list.
      const int32_t lightingIndex = state.lightModel == CoinRenderLightModel::PHONG
        ? pass.lightingIndexByState[stateSlot] : -1;
      receiver.receives =
          coin_render_shadow_shading_flags(frame, state) |
          ((state.shadowStyle & 2u) != 0 && lightingIndex >= 0 ? 1u : 0u);
      receiver.lighting_index = lightingIndex;
      receiver.max_shadow_distance = pass.maxShadowDistance;
      receiver.distance_falloff_coefficient = pass.distanceFalloffCoefficient;
      SbMatrix modelView =
          passSlot < 2 ? state.model * pass.view : state.view.inverse() * pass.view;
      SbMatrix mvp = modelView * projectionWgpu;
      if (state.rasterShadowAnchorValid) {
        SbVec3f lightAnchor;
        SbVec4f clipAnchor;
        pass.view.multVecMatrix(state.rasterShadowAnchorWorld, lightAnchor);
        (pass.view * projectionWgpu).multVecMatrix(
          SbVec4f(state.rasterShadowAnchorWorld[0], state.rasterShadowAnchorWorld[1],
                  state.rasterShadowAnchorWorld[2], 1.0f), clipAnchor);
        if (!coin_render_shadow_finite(lightAnchor) ||
            !std::isfinite(clipAnchor[0]) || !std::isfinite(clipAnchor[1]) ||
            !std::isfinite(clipAnchor[2]) || !std::isfinite(clipAnchor[3]) ||
            clipAnchor[3] <= 0.0f) {
          diagnostic = "Raster shadow receiver has a non-finite light anchor";
          return false;
        }
        modelView = SbMatrix(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             lightAnchor[0], lightAnchor[1], lightAnchor[2], 1);
        // Retain homogeneous W for perspective maps. The receiver shader
        // performs the same perspective division as ordinary geometry.
        mvp = SbMatrix(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                       clipAnchor[0], clipAnchor[1], clipAnchor[2], clipAnchor[3]);
      }
      std::memcpy(receiver.model_view, modelView.getValue(), sizeof(receiver.model_view));
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
      const auto &draw = frame.draws[drawSlot];
      const uint64_t indexEnd = uint64_t(draw.geometry.firstIndex) + draw.geometry.indexCount;
      const uint64_t vertexEnd = uint64_t(draw.geometry.firstVertex) + draw.geometry.vertexCount;
      if (draw.renderStateSlot >= frame.renderStates.size() || draw.geometry.indexCount == 0 ||
          draw.geometry.indexCount % 3 != 0 || indexEnd > frame.indices.size() ||
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
      const auto &state = frame.renderStates[draw.renderStateSlot];
      const SbMatrix modelView = state.model *
        (state.shadowMapCameraOverride ? state.view : pass.view);
      const SbMatrix casterProjection = state.shadowMapCameraOverride ?
        state.projectionCoin * clipConversion : projectionWgpu;
      SbMatrix viewportTransform;
      int32_t scissor[4];
      if (!coin_render_shadow_caster_viewport(frame, state, pass.mapSize, viewportTransform,
                                              scissor, diagnostic))
        return false;
      packed.viewport[0] = scissor[0];
      packed.viewport[1] = int32_t(pass.mapSize) - scissor[1] - scissor[3];
      packed.viewport[2] = scissor[2];
      packed.viewport[3] = scissor[3];
      const SbMatrix mvp = modelView * casterProjection * viewportTransform;
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
