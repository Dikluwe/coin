#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include "rendering/coinrender/CoinRenderClipCore.h"

#include <Inventor/SbMatrix.h>
#include "rendering/coinrender/CoinRenderComposition.h"

#include <cmath>
#include <cstddef>
#include <cstring>

static_assert(sizeof(CoinWgpuFrameView) == 176, "Frame view ABI size changed");
static_assert(offsetof(CoinWgpuFrameView, sorted_layers_passes) == 160,
              "Layer count ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, transparency_reserved) == 164,
              "Transparency reserved ABI offset changed");
static_assert(offsetof(CoinWgpuFrameView, transparency_budget_bytes) == 168,
              "Transparency budget ABI offset changed");
static_assert(sizeof(CoinWgpuTextureUnit) == 96, "Texture unit ABI size changed");
static_assert(offsetof(CoinWgpuVertex, extra_texcoords) == 44, "Extra UV ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, extra_textures) == 1096, "Extra textures ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, texture_combines) == 1768, "Combine ABI offset changed");
static_assert(sizeof(CoinWgpuRenderState) == 2280, "CoinWgpuRenderState ABI size changed");
static_assert(offsetof(CoinWgpuRenderState, polygon_offset_max_depth_bits) == 1092, "Maximum depth ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, polygon_offset_slope_bias) == 1088, "Slope bias ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, polygon_offset_enabled) == 936, "Polygon offset ABI tail changed");
static_assert(offsetof(CoinWgpuRenderState, depth_test) == 916, "depth_test ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, depth_write) == 920, "depth_write ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, depth_function) == 924, "depth_function ABI offset changed");
static_assert(offsetof(CoinWgpuRenderState, depth_range) == 928, "depth_range ABI offset changed");

CoinWgpuFfiFrame::CoinWgpuFfiFrame()
  : packedRevision(0), reused(false),
    prepareKind(CoinRenderFrameReuseKind::UNKNOWN), view{}
{
}

bool
CoinWgpuFfiFrame::prepare(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height,
                        std::string & outDiagnostic)
{
  return this->prepare(frame, width, height,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0),
    outDiagnostic);
}

bool
CoinWgpuFfiFrame::prepare(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height,
                        const CoinRenderFrameReuseDecision & reuse,
                        std::string & outDiagnostic)
{
  outDiagnostic.clear();
  for (const auto& texture : frame.textures) {
    if (texture.producerId) {
      outDiagnostic = "Unresolved scene texture producer at wgpu execution boundary";
      return false;
    }
  }
  if (frame.revision != 0 && frame.revision == this->packedRevision) {
    uint64_t requiredBytes = 0;
    const bool needsPeeling =
        std::any_of(this->draws.begin(), this->draws.end(), [](const CoinWgpuDraw& draw) {
          return (draw.composition_flags & (1u << 3)) != 0;
        });
    if (!coin_render_transparency_budget(width, height, frame.transparency, needsPeeling,
                                         requiredBytes, outDiagnostic))
      return false;
    this->reused = true;
    this->prepareKind = CoinRenderFrameReuseKind::REUSE;
    this->view.camera_base_revision = 0;
    this->view.width = width;
    this->view.height = height;
    this->view.sorted_layers_passes = frame.transparency.layers;
    this->view.transparency_budget_bytes = frame.transparency.bufferBudget;
    return true;
  }
  this->reused = false;

  if (reuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
      frame.revision != 0 && frame.revision != reuse.baseRevision &&
      reuse.baseRevision != 0 && reuse.baseRevision == this->packedRevision) {
    const auto previousDraws = this->draws;
    this->packedRevision = 0;
    if (!this->packStates(frame, width, height, outDiagnostic)) return false;
    const bool sameOrder = previousDraws.size() == this->draws.size() &&
      (previousDraws.empty() || std::memcmp(previousDraws.data(), this->draws.data(),
        previousDraws.size() * sizeof(CoinWgpuDraw)) == 0);
    this->bindView(frame, width, height);
    this->view.camera_base_revision = sameOrder ? reuse.baseRevision : 0;
    this->packedRevision = frame.revision;
    this->prepareKind = CoinRenderFrameReuseKind::CAMERA_PATCH;
    return true;
  }

  this->packedRevision = 0;
  this->vertices.resize(frame.vertices.size());
  for (size_t i = 0; i < frame.vertices.size(); ++i) {
    const CoinRenderVertexSnapshot & src = frame.vertices[i];
    CoinWgpuVertex & dst = this->vertices[i];
    std::memcpy(dst.position, src.position, sizeof(src.position));
    std::memcpy(dst.normal, src.normal, sizeof(src.normal));
    std::memcpy(dst.texcoord, src.texcoord, sizeof(src.texcoord));
    dst.material_slot = src.materialSlot;
    std::memcpy(dst.extra_texcoords, src.extraTexcoords, sizeof(dst.extra_texcoords));
    dst.screen_space_w = src.screenSpaceW;
    dst.fog_eye_depth_plus_one = src.fogEyeDepth >= 0 ? src.fogEyeDepth + 1.0f : 0.0f;
  }

  this->indices = frame.indices;
  this->materials.resize(frame.materials.size());
  for (size_t i = 0; i < frame.materials.size(); ++i) {
    const CoinRenderMaterialSnapshot & src = frame.materials[i];
    CoinWgpuMaterial & dst = this->materials[i];
    std::memcpy(dst.ambient, src.ambient, sizeof(src.ambient));
    std::memcpy(dst.diffuse, src.diffuse, sizeof(src.diffuse));
    std::memcpy(dst.specular, src.specular, sizeof(src.specular));
    std::memcpy(dst.emission, src.emission, sizeof(src.emission));
    dst.shininess = src.shininess;
    dst.transparency = src.transparency;
  }

  if (!this->packStates(frame, width, height, outDiagnostic)) return false;

  this->texturePixels.resize(frame.textures.size());
  this->textures.assign(frame.textures.size(), CoinWgpuTexture{});
  for (size_t i = 0; i < frame.textures.size(); ++i) {
    const CoinRenderTextureImageSnapshot & src = frame.textures[i];
    CoinWgpuTexture & dst = this->textures[i];
    this->texturePixels[i] = src.pixelsRgba;
    dst.width = src.width;
    dst.height = src.height;
    dst.format = src.gpuToken ? 1 : 0;
    dst.reserved = src.gpuToken && src.gpuOpaque ? 1 : 0;
    dst.content_digest = src.gpuToken ? src.gpuToken : src.contentDigest;
    dst.pixels = src.gpuToken ? NULL : this->texturePixels[i].data();
    dst.pixel_bytes_len = static_cast<uint64_t>(this->texturePixels[i].size());
  }

  this->samplers.assign(frame.samplers.size(), CoinWgpuSampler{});
  for (size_t i = 0; i < frame.samplers.size(); ++i) {
    this->samplers[i].wrap_s = static_cast<uint32_t>(frame.samplers[i].wrapS);
    this->samplers[i].wrap_t = static_cast<uint32_t>(frame.samplers[i].wrapT);
    this->samplers[i].filter = static_cast<uint32_t>(frame.samplers[i].filter);
  }

  this->bindView(frame, width, height);
  this->packedRevision = frame.revision;
  this->prepareKind = reuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD
    ? CoinRenderFrameReuseKind::RESOURCE_REBUILD
    : CoinRenderFrameReuseKind::FULL_REBUILD;
  return true;
}

bool
CoinWgpuFfiFrame::packStates(const CoinRenderFramePlan & frame, uint32_t targetWidth, uint32_t targetHeight,
                           std::string & outDiagnostic)
{
  this->states.assign(frame.renderStates.size(), CoinWgpuRenderState{});
  for (size_t i = 0; i < frame.renderStates.size(); ++i) {
    const CoinRenderRenderStateSnapshot & src = frame.renderStates[i];
    CoinWgpuRenderState & dst = this->states[i];
    dst.clip_plane_count = static_cast<uint32_t>(src.clipPlanesWorld.size());
    if (!coin_render_clip_equations(src, dst.clip_planes, outDiagnostic)) return false;
    const SbMatrix modelView = src.model * src.view;
    const float determinant = modelView.det4();
    const SbMatrix normalMatrix = std::abs(determinant) > 1.0e-12f
      ? modelView.inverse().transpose() : SbMatrix::identity();
    const SbMatrix clipConversion(
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f);
    const SbMatrix projectionWgpu = src.projectionCoin * clipConversion;
    const SbMatrix mvpWgpu = modelView * projectionWgpu;

    std::memcpy(dst.model_view, modelView.getValue(), sizeof(float) * 16);
    std::memcpy(dst.model_view_projection, mvpWgpu.getValue(), sizeof(float) * 16);
    std::memcpy(dst.normal_matrix, normalMatrix.getValue(), sizeof(float) * 16);

    bool hasLight = false;
    if (src.lightModel == CoinRenderLightModel::PHONG &&
        src.lightingSlot < frame.lightingStates.size() &&
        !frame.lightingStates[src.lightingSlot].lights.empty()) {
      const CoinRenderLightSourceSnapshot & light = frame.lightingStates[src.lightingSlot].lights[0];
      hasLight = true;
      for (int c = 0; c < 3; ++c) {
        dst.light_direction[c] = light.direction[c];
        dst.light_color[c] = light.color[c];
      }
      dst.light_direction[3] = 0.0f;
      dst.light_color[3] = 1.0f;
      dst.light_intensity = light.intensity;
    } else {
      dst.light_direction[2] = 1.0f;
      dst.light_color[0] = 1.0f;
      dst.light_color[1] = 1.0f;
      dst.light_color[2] = 1.0f;
      dst.light_color[3] = 1.0f;
      dst.light_intensity = 1.0f;
    }
    dst.has_light = hasLight ? 1 : 0;
    dst.material_slot = src.materialSlot;
    dst.cull_mode = static_cast<uint32_t>(src.cullMode);
    dst.front_face = static_cast<uint32_t>(src.frontFace);
    dst.light_model = static_cast<uint32_t>(src.lightModel);
    std::memcpy(dst.texture_matrix, src.textureMatrix.getValue(), sizeof(float) * 16);
    std::memcpy(dst.texture_combines, src.textureCombines, sizeof(dst.texture_combines));
    for (size_t unit = 1; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      const auto layer = coin_render_texture_unit(src, unit);
      auto& target = dst.extra_textures[unit - 1];
      std::memcpy(target.matrix, layer.matrix.getValue(), sizeof(target.matrix));
      target.enabled = layer.enabled ? 1 : 0;
      target.texture_slot = layer.imageSlot; target.sampler_slot = layer.samplerSlot;
      target.model = static_cast<uint32_t>(layer.model);
      std::memcpy(target.blend_color, layer.blendColor, sizeof(target.blend_color));
    }
    dst.has_texture = src.hasTexture ? 1 : 0;
    dst.texture_slot = src.textureImageSlot;
    dst.sampler_slot = src.samplerSlot;
    dst.texture_model = static_cast<uint32_t>(src.textureModel);
    std::memcpy(dst.texture_blend_color, src.textureBlendColor,
                sizeof(src.textureBlendColor));
    if (src.viewportSlot < frame.viewports.size()) {
      const CoinRenderViewportSnapshot & viewport = frame.viewports[src.viewportSlot];
      dst.viewport[0] = viewport.x;
      dst.viewport[1] = static_cast<int32_t>(targetHeight) - viewport.y - viewport.height;
      dst.viewport[2] = viewport.width;
      dst.viewport[3] = viewport.height;
    } else {
      dst.viewport[0] = dst.viewport[1] = 0;
      dst.viewport[2] = static_cast<int32_t>(targetWidth);
      dst.viewport[3] = static_cast<int32_t>(targetHeight);
    }
    dst.fog_mode = static_cast<uint32_t>(src.fogMode);
    std::memcpy(dst.fog_color, src.fogColor, sizeof(src.fogColor));
    dst.fog_start = src.fogStart;
    dst.fog_end = src.fogEnd;
    dst.depth_test = src.depthTest ? 1u : 0u;
    dst.depth_write = src.depthWrite ? 1u : 0u;
    dst.depth_function = static_cast<uint32_t>(src.depthFunction);
    dst.depth_range[0] = src.depthRange[0];
    dst.depth_range[1] = src.depthRange[1];
    dst.polygon_offset_enabled = src.polygonOffsetEnabled ? 1u : 0u;
    dst.polygon_offset_factor = src.polygonOffsetFactor;
    dst.polygon_offset_units = src.polygonOffsetUnits;
    dst.polygon_offset_slope_bias = src.polygonOffsetSlopeBias;
    dst.polygon_offset_max_depth_bits = 0;
    if (src.polygonOffsetMaxDepth >= 0) {
      std::memcpy(&dst.polygon_offset_max_depth_bits, &src.polygonOffsetMaxDepth, sizeof(float));
      ++dst.polygon_offset_max_depth_bits;
    }
    dst.polygon_offset_styles = src.polygonOffsetStyles;
    dst.polygon_offset_primitive_style = src.polygonOffsetPrimitiveStyle;
    dst.ambient_light[3] = 1.0f;

    if (src.lightingSlot < frame.lightingStates.size()) {
      const CoinRenderLightingSnapshot & lighting = frame.lightingStates[src.lightingSlot];
      for (int c = 0; c < 3; ++c) {
        dst.ambient_light[c] = lighting.ambientColor[c] * lighting.ambientIntensity;
      }
      if (src.lightModel == CoinRenderLightModel::PHONG) {
        if (lighting.lights.size() > COIN_WGPU_FFI_MAX_LIGHTS) {
          outDiagnostic = "More than eight active lights in CoinRenderFramePlan";
          this->packedRevision = 0;
          this->prepareKind = CoinRenderFrameReuseKind::UNKNOWN;
          return false;
        }
        dst.light_count = static_cast<uint32_t>(lighting.lights.size());
        for (size_t j = 0; j < lighting.lights.size(); ++j) {
          const CoinRenderLightSourceSnapshot & light = lighting.lights[j];
          CoinWgpuLight & packedLight = dst.lights[j];
          for (int c = 0; c < 3; ++c) {
            packedLight.position_type[c] = light.position[c];
            packedLight.direction_cutoff[c] = light.direction[c];
            packedLight.color_intensity[c] = light.color[c];
          }
          packedLight.position_type[3] = static_cast<float>(light.type);
          packedLight.direction_cutoff[3] = std::cos(light.cutOffAngle);
          packedLight.color_intensity[3] = light.intensity;
          packedLight.attenuation_exponent[0] = light.attenuation[0];
          packedLight.attenuation_exponent[1] = light.attenuation[1];
          packedLight.attenuation_exponent[2] = light.attenuation[2];
          packedLight.attenuation_exponent[3] = light.dropOffRate * 128.0f;
        }
      }
    }
  }
  std::vector<CoinRenderCompositionItem> order;
  if (!coin_render_composition_schedule(frame, order, outDiagnostic))
    return false;
  for (const auto& item : order) {
    if (item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT) {
      outDiagnostic = coin_render_selection_diagnostic(COIN_RENDER_SELECTION_NOT_IMPLEMENTED);
      return false;
    }
  }
  uint64_t requiredBytes = 0;
  const bool needsPeeling =
      std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem& item) {
        return item.blend && item.deferred &&
               item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS;
      });
  if (!coin_render_transparency_budget(targetWidth, targetHeight, frame.transparency, needsPeeling,
                                       requiredBytes, outDiagnostic))
    return false;
  std::vector<CoinWgpuDraw> resolvedDraws;
  resolvedDraws.reserve(order.size());
  for (const auto & item : order) {
    const auto & src = frame.draws[item.drawIndex];
    const auto & state = frame.renderStates[src.renderStateSlot];
    CoinWgpuDraw dst{};
    dst.topology = static_cast<uint32_t>(src.topology);
    dst.first_vertex = src.geometry.firstVertex;
    dst.vertex_count = src.geometry.vertexCount;
    dst.first_index = item.firstIndex;
    dst.index_count = item.indexCount;
    dst.render_state_slot = src.renderStateSlot;
    dst.stable_node_id = item.blend && item.sortTriangles ? 0 : src.stableNodeId;
    dst.draw_ordinal = src.drawOrdinal;
    dst.composition_flags =
        (item.blend ? 1u : 0u) | (item.additive && item.blend ? 2u : 0u) |
        (item.screenDoor ? 4u : 0u) |
        (item.blend && item.deferred &&
                 item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS
             ? 8u
             : 0u) |
        (item.screenDoorLevel << 8);
    dst.source_revision = src.sourceRevision;
    dst.render_layer = src.renderLayer;
    dst.clear_depth_before = src.clearDepthBefore ? 1u : 0u;
    if (item.depthTest != state.depthTest || item.depthWrite != state.depthWrite ||
        item.depthFunction != state.depthFunction ||
        item.depthRange[0] != state.depthRange[0] || item.depthRange[1] != state.depthRange[1]) {
      CoinWgpuRenderState resolved = this->states[src.renderStateSlot];
      resolved.depth_test = item.depthTest ? 1u : 0u;
      resolved.depth_write = item.depthWrite ? 1u : 0u;
      resolved.depth_function = static_cast<uint32_t>(item.depthFunction);
      resolved.depth_range[0] = item.depthRange[0];
      resolved.depth_range[1] = item.depthRange[1];
      dst.render_state_slot = static_cast<uint32_t>(this->states.size());
      this->states.push_back(resolved);
    }
    resolvedDraws.push_back(dst);
  }
  this->draws.swap(resolvedDraws);
  return true;
}

void
CoinWgpuFfiFrame::bindView(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height)
{
  this->view = CoinWgpuFrameView{};
  this->view.abi_version = COIN_WGPU_ABI_VERSION;
  this->view.struct_size = sizeof(CoinWgpuFrameView);
  this->view.frame_revision = frame.revision;
  this->view.vertices = this->vertices.empty() ? NULL : this->vertices.data();
  this->view.vertex_count = static_cast<uint64_t>(this->vertices.size());
  this->view.indices = this->indices.empty() ? NULL : this->indices.data();
  this->view.index_count = static_cast<uint64_t>(this->indices.size());
  this->view.draws = this->draws.empty() ? NULL : this->draws.data();
  this->view.draw_count = static_cast<uint64_t>(this->draws.size());
  this->view.materials = this->materials.empty() ? NULL : this->materials.data();
  this->view.material_count = static_cast<uint64_t>(this->materials.size());
  this->view.states = this->states.empty() ? NULL : this->states.data();
  this->view.state_count = static_cast<uint64_t>(this->states.size());
  this->view.textures = this->textures.empty() ? NULL : this->textures.data();
  this->view.texture_count = static_cast<uint64_t>(this->textures.size());
  this->view.samplers = this->samplers.empty() ? NULL : this->samplers.data();
  this->view.sampler_count = static_cast<uint64_t>(this->samplers.size());
  for (int c = 0; c < 4; ++c) this->view.clear_color[c] = frame.clearColor[c];
  this->view.width = width;
  this->view.height = height;
  this->view.sorted_layers_passes = frame.transparency.layers;
  this->view.transparency_budget_bytes = frame.transparency.bufferBudget;
}

const CoinWgpuFrameView &
CoinWgpuFfiFrame::getView() const
{
  return this->view;
}

bool
CoinWgpuFfiFrame::reusedLastPrepare() const
{
  return this->reused;
}

CoinRenderFrameReuseKind
CoinWgpuFfiFrame::lastPrepareKind() const
{
  return this->prepareKind;
}
