#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuFfiFrame.h"

#include <Inventor/SbMatrix.h>

#include <cmath>
#include <cstring>

SoWgpuFfiFrame::SoWgpuFfiFrame()
  : packedRevision(0), reused(false), view{}
{
}

bool
SoWgpuFfiFrame::prepare(const FramePlan & frame, uint32_t width, uint32_t height,
                        std::string & outDiagnostic)
{
  outDiagnostic.clear();
  if (frame.revision != 0 && frame.revision == this->packedRevision) {
    this->reused = true;
    this->view.width = width;
    this->view.height = height;
    return true;
  }
  this->reused = false;

  this->vertices.resize(frame.vertices.size());
  for (size_t i = 0; i < frame.vertices.size(); ++i) {
    const VertexSnapshot & src = frame.vertices[i];
    CoinWgpuVertex & dst = this->vertices[i];
    std::memcpy(dst.position, src.position, sizeof(src.position));
    std::memcpy(dst.normal, src.normal, sizeof(src.normal));
    std::memcpy(dst.texcoord, src.texcoord, sizeof(src.texcoord));
    dst.material_slot = src.materialSlot;
  }

  this->indices = frame.indices;
  this->draws.resize(frame.draws.size());
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const DrawPacket & src = frame.draws[i];
    CoinWgpuDraw & dst = this->draws[i];
    dst.topology = static_cast<uint32_t>(src.topology);
    dst.first_vertex = src.geometry.firstVertex;
    dst.vertex_count = src.geometry.vertexCount;
    dst.first_index = src.geometry.firstIndex;
    dst.index_count = src.geometry.indexCount;
    dst.render_state_slot = src.renderStateSlot;
    dst.stable_node_id = src.stableNodeId;
    dst.draw_ordinal = src.drawOrdinal;
    dst.reserved = 0;
    dst.source_revision = src.sourceRevision;
  }

  this->materials.resize(frame.materials.size());
  for (size_t i = 0; i < frame.materials.size(); ++i) {
    const MaterialSnapshot & src = frame.materials[i];
    CoinWgpuMaterial & dst = this->materials[i];
    std::memcpy(dst.ambient, src.ambient, sizeof(src.ambient));
    std::memcpy(dst.diffuse, src.diffuse, sizeof(src.diffuse));
    std::memcpy(dst.specular, src.specular, sizeof(src.specular));
    std::memcpy(dst.emission, src.emission, sizeof(src.emission));
    dst.shininess = src.shininess;
    dst.transparency = src.transparency;
  }

  this->states.assign(frame.renderStates.size(), CoinWgpuRenderState{});
  for (size_t i = 0; i < frame.renderStates.size(); ++i) {
    const RenderStateSnapshot & src = frame.renderStates[i];
    CoinWgpuRenderState & dst = this->states[i];
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
    if (src.lightModel == LightModel::PHONG &&
        src.lightingSlot < frame.lightingStates.size() &&
        !frame.lightingStates[src.lightingSlot].lights.empty()) {
      const LightSourceSnapshot & light = frame.lightingStates[src.lightingSlot].lights[0];
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
    dst.has_texture = src.hasTexture ? 1 : 0;
    dst.texture_slot = src.textureImageSlot;
    dst.sampler_slot = src.samplerSlot;
    dst.texture_model = static_cast<uint32_t>(src.textureModel);
    dst.fog_mode = static_cast<uint32_t>(src.fogMode);
    std::memcpy(dst.fog_color, src.fogColor, sizeof(src.fogColor));
    dst.fog_start = src.fogStart;
    dst.fog_end = src.fogEnd;
    dst.ambient_light[3] = 1.0f;

    if (src.lightingSlot < frame.lightingStates.size()) {
      const LightingSnapshot & lighting = frame.lightingStates[src.lightingSlot];
      for (int c = 0; c < 3; ++c) {
        dst.ambient_light[c] = lighting.ambientColor[c] * lighting.ambientIntensity;
      }
      if (src.lightModel == LightModel::PHONG) {
        if (lighting.lights.size() > COIN_WGPU_FFI_MAX_LIGHTS) {
          outDiagnostic = "More than eight active lights in FramePlan";
          this->packedRevision = 0;
          return false;
        }
        dst.light_count = static_cast<uint32_t>(lighting.lights.size());
        for (size_t j = 0; j < lighting.lights.size(); ++j) {
          const LightSourceSnapshot & light = lighting.lights[j];
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

  this->texturePixels.resize(frame.textures.size());
  this->textures.assign(frame.textures.size(), CoinWgpuTexture{});
  for (size_t i = 0; i < frame.textures.size(); ++i) {
    const TextureImageSnapshot & src = frame.textures[i];
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
  return true;
}

void
SoWgpuFfiFrame::bindView(const FramePlan & frame, uint32_t width, uint32_t height)
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
}

const CoinWgpuFrameView &
SoWgpuFfiFrame::getView() const
{
  return this->view;
}

bool
SoWgpuFfiFrame::reusedLastPrepare() const
{
  return this->reused;
}
