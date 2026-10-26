// CoinRender BGFX Multipass responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"
#include "coin_bgfx_vs_glsl.h"
#include "coin_bgfx_vs_spirv.h"
#include "coin_bgfx_fs_stereo_glsl.h"
#include "coin_bgfx_fs_stereo_spirv.h"
#include "coin_bgfx_fs_stereo_msaa_glsl.h"
#include "coin_bgfx_fs_stereo_msaa_spirv.h"
#include "coin_bgfx_fs_multipass_sum_glsl.h"
#include "coin_bgfx_fs_multipass_sum_spirv.h"
#include "coin_bgfx_fs_multipass_mean_glsl.h"
#include "coin_bgfx_fs_multipass_mean_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_vs_dx11.h"
#include "coin_bgfx_fs_stereo_dx11.h"
#include "coin_bgfx_fs_stereo_msaa_dx11.h"
#include "coin_bgfx_fs_multipass_sum_dx11.h"
#include "coin_bgfx_fs_multipass_mean_dx11.h"
#endif

using namespace coin_bgfx_internal;

void CoinBgfxBackend::configureMultipassTarget(const CoinRenderTargetP & target) {
  this->sampleCount = target.options.sampleCount;
}

bool CoinBgfxBackend::multipassOutputBytes(const CoinRenderTargetP & target, bool asynchronous,
    uint64_t & bytes) const {
  if (!CoinRenderBackend::multipassOutputBytes(target, asynchronous, bytes)) return false;
  if (target.size[0] < 0 || target.size[1] < 0) return false;
  const uint64_t width = uint64_t(target.size[0]), height = uint64_t(target.size[1]);
  if (target.kind == CoinRenderTargetP::KIND_WINDOW) {
    // Private scene attachments are in requiredBytes; the acquired surface is
    // an independent final color image. Window APIs expose no depth readback.
    if (!CoinRenderMultipassCore::addAttachmentBytes(width,height,4,1,bytes)) return false;
    if (!target.windowReadbackRequested) return true;
  } else if (target.directTextureOutput) return true;
  const uint64_t channels = target.kind == CoinRenderTargetP::KIND_WINDOW ? 4 :
    target.depthReadbackEnabled ? 8 : 4;
  const uint64_t copies = target.kind == CoinRenderTargetP::KIND_WINDOW ? 2 :
    uint64_t(this->readbackPipelineDepth) * 2 + (asynchronous ? 2 : 0);
  return CoinRenderMultipassCore::addAttachmentBytes(width,height,channels,copies,bytes) &&
    (!target.depthReadbackEnabled || target.kind == CoinRenderTargetP::KIND_WINDOW ||
     CoinRenderMultipassCore::addAttachmentBytes(width,height,4,1,bytes));
}

CoinRenderSubmitResult CoinBgfxBackend::preflightMultipassGraph(const CoinRenderRttPlan & graph,
    const CoinRenderFramePlan & root, const SbVec2i32 & extent) const {
  if (root.numPasses == 1) return {};
  uint64_t retained = 0;
  if (!CoinRenderMultipassCore::graphBytes(graph, root, extent, this->sampleCount,
        CoinRenderMultipassCore::Executor::BGFX, retained) || retained > root.multipassBufferBudget)
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "Multipass simultaneous RTT resources exceed the buffer budget"};
  CoinRenderFramePlan admitted = root;
  admitted.multipassBufferBudget -= retained;
  return this->preflightMultipass(admitted, extent);
}

CoinRenderSubmitResult CoinBgfxBackend::preflightMultipass(const CoinRenderFramePlan & frame,
                                                           const SbVec2i32 & extent) const {
  if (frame.numPasses == 1) return {};
  uint64_t required = 0;
  uint64_t jobs = 0, ticketBytes = 0;
  this->readbackLoad(jobs, ticketBytes);

  if (!CoinRenderMultipassCore::requiredBytes(frame,extent,this->sampleCount,required,
        CoinRenderMultipassCore::Executor::BGFX,bgfxShadowMultitexture(frame)) ||
      required > frame.multipassBufferBudget)
    return {CoinRenderBackendStatus::OUT_OF_MEMORY,"Multipass attachments exceed the buffer budget"};
  // BGFX has both CPU staging and a readback texture; wgpu reports the
  // physical, row-aligned ticket buffers through its concrete resource owner.
  if (!CoinRenderMultipassCore::addAttachmentBytes(ticketBytes, 1, 2, 1, required) ||
      required > frame.multipassBufferBudget)
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "Multipass attachments and retained tickets exceed the buffer budget"};
  uint64_t rasterBytes = 0;
  if (!CoinRenderMultipassCore::rasterPayloadBytes(frame, sizeof(CoinBgfxVertex), sizeof(CoinBgfxDraw), false, rasterBytes) ||
      rasterBytes > frame.multipassBufferBudget - required)
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "Multipass raster payload exceeds the buffer budget"};
  const auto * caps = sharedRuntime().references ? bgfx::getCaps() : nullptr;
  if (caps && (extent[0] > caps->limits.maxTextureSize || extent[1] > caps->limits.maxTextureSize ||
      !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT)))
    return {CoinRenderBackendStatus::UNSUPPORTED,"Multipass RGBA32F accumulation format or extent unavailable"};
  if (this->initialized && !const_cast<CoinBgfxBackend *>(this)->prepareMultipassPrograms())
    return {CoinRenderBackendStatus::BACKEND_ERROR,this->lastError};
  return {};
}

bool CoinBgfxBackend::prepareStereoPrograms()
{
  if (!this->prepareFullscreenResources()) return false;
  if (!bgfx::isValid(this->stereoProgram))
    this->stereoProgram = createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_stereo), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_stereo));
  if (this->sampleCount > 1 && !bgfx::isValid(this->stereoMsaaProgram))
    this->stereoMsaaProgram = createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_stereo_msaa), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_stereo_msaa));
  const char * names[] = {"s_stereoLeft", "s_stereoRight", "s_stereoLeftDepth", "s_stereoRightDepth"};
  for (int i=0; i<4; ++i) if (!bgfx::isValid(this->stereoSamplers[i]))
    this->stereoSamplers[i] = bgfx::createUniform(names[i], bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->stereoInfo)) this->stereoInfo = bgfx::createUniform("u_stereoInfo", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->stereoViewport)) this->stereoViewport = bgfx::createUniform("u_stereoViewport", bgfx::UniformType::Vec4);
  bool valid = bgfx::isValid(this->stereoProgram) && (this->sampleCount == 1 || bgfx::isValid(this->stereoMsaaProgram)) &&
    bgfx::isValid(this->stereoInfo) && bgfx::isValid(this->stereoViewport);
  for (auto sampler : this->stereoSamplers) valid = valid && bgfx::isValid(sampler);
  if (!valid) this->lastError = "BGFX completed-view composition resources unavailable";
  return valid;
}

bool CoinBgfxBackend::encodeStereoViews(const CoinRenderFramePlan & frame, bgfx::FrameBufferHandle output,
    bgfx::ViewId view, int width, int height)
{
  if (frame.stereoMode < 1 || frame.stereoMode > 4 || !frame.stereoTokens[0] ||
      (frame.stereoMode != 4 && (frame.stereoTokens[0] == frame.stereoTokens[1] || frame.stereoExtent[0] <= 0 || frame.stereoExtent[1] <= 0))) {
    this->lastError = "Invalid stereo completed-view composition";
    return false;
  }
  bgfx::TextureHandle textures[4] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
  for (int eye=0; eye<2; ++eye) for (const auto & resource : this->directTextures)
    if (resource.token == frame.stereoTokens[eye] && resource.inUse && resource.width == width && resource.height == height) {
      const auto source = this->sampleCount > 1 ? resource.viewSamples : resource.frameBuffer;
      if (!bgfx::isValid(source)) break;
      textures[eye] = bgfx::getTexture(source, 0);
      textures[eye+2] = bgfx::getTexture(source, 1);
    }
  for (auto texture : textures) if (!bgfx::isValid(texture)) {
    this->lastError = "Stereo view token is stale, mismatched or belongs to another owner";
    return false;
  }
  bgfx::setViewName(view, "stereo_completed_views");
  bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
  bgfx::setViewFrameBuffer(view, output);
  bgfx::setViewRect(view, 0, 0, uint16_t(width), uint16_t(height));
  bgfx::setViewTransform(view, nullptr, nullptr);
  const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  const float info[4] = {1.0f/width, 1.0f/height, float(frame.stereoMode), bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f};
  const float viewport[4] = {float(frame.stereoOrigin[0]), float(frame.stereoOrigin[1]),
    float(frame.stereoExtent[0]), float(frame.stereoExtent[1])};
  for (uint32_t sample=0; sample<this->sampleCount; ++sample) {
    const float sampleInfo[4] = {frame.clearColor[0],frame.clearColor[1],frame.clearColor[2],float(sample)};
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->stereoInfo, info);
    bgfx::setUniform(this->stereoViewport, viewport);
    bgfx::setUniform(this->depthInfoUniform, sampleInfo);
    for (int i=0; i<4; ++i) bgfx::setTexture(uint8_t(i), this->stereoSamplers[i], textures[i],
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    bgfx::setSampleMask(1u << sample);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
      BGFX_STATE_DEPTH_TEST_ALWAYS | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
    bgfx::submit(view, this->sampleCount > 1 ? this->stereoMsaaProgram : this->stereoProgram);
  }
  return true;
}

bool CoinBgfxBackend::prepareMultipassPrograms() {
  if (!this->prepareFullscreenResources()) return false;
  if (!bgfx::isValid(this->multipassSumProgram))
    this->multipassSumProgram=createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs),COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_multipass_sum),COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_multipass_sum));
  if (!bgfx::isValid(this->multipassMeanProgram))
    this->multipassMeanProgram=createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs),COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_multipass_mean),COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_multipass_mean));
  if (!bgfx::isValid(this->multipassSceneSampler))
    this->multipassSceneSampler=bgfx::createUniform("s_multipassScene",bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->multipassPreviousSampler))
    this->multipassPreviousSampler=bgfx::createUniform("s_multipassPrevious",bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->multipassInfoUniform))
    this->multipassInfoUniform=bgfx::createUniform("u_multipassInfo",bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->multipassSumProgram) || !bgfx::isValid(this->multipassMeanProgram) ||
      !bgfx::isValid(this->multipassSceneSampler) || !bgfx::isValid(this->multipassPreviousSampler) ||
      !bgfx::isValid(this->multipassInfoUniform)) {
    this->lastError="Multipass accumulation program/resource creation failed";
    return false;
  }
  return true;
}

bool
CoinBgfxBackend::prepareFullscreenResources()
{
  if (!bgfx::isValid(this->depthInfoUniform))
    this->depthInfoUniform = bgfx::createUniform("u_depthInfo", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->fullscreenVertexBuffer)) {
    CoinBgfxVertex fullscreen[3] = {};
    fullscreen[0].position[0] = -1.0f; fullscreen[0].position[1] = -1.0f;
    fullscreen[1].position[0] = 3.0f; fullscreen[1].position[1] = -1.0f;
    fullscreen[2].position[0] = -1.0f; fullscreen[2].position[1] = 3.0f;
    this->fullscreenVertexBuffer = bgfx::createVertexBuffer(bgfx::copy(fullscreen, sizeof(fullscreen)), this->layout);
  }
  if (!bgfx::isValid(this->fullscreenIndexBuffer)) {
    const uint16_t indices[3] = {0, 1, 2};
    this->fullscreenIndexBuffer = bgfx::createIndexBuffer(bgfx::copy(indices, sizeof(indices)));
  }
  if (!bgfx::isValid(this->depthInfoUniform) || !bgfx::isValid(this->fullscreenVertexBuffer) ||
      !bgfx::isValid(this->fullscreenIndexBuffer)) {
    this->lastError = "BGFX could not allocate shared transparency resources";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return false;
  }
  return true;
}

CoinRenderSubmitResult CoinBgfxBackend::preflightStereo(const CoinRenderFramePlan & frame,
    const CoinRenderTargetP & target, size_t) const {
  if (!this->initialized) return {};
  const auto * caps = bgfx::getCaps();
  if (!caps || target.size[0] > caps->limits.maxTextureSize || target.size[1] > caps->limits.maxTextureSize ||
      !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::D32F, peelTextureFlags))
    return {CoinRenderBackendStatus::UNSUPPORTED, "Completed stereo view color/depth attachments exceed adapter capabilities"};
  if (!const_cast<CoinBgfxBackend *>(this)->prepareStereoPrograms())
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  return this->preflightMultipass(frame, target.size);
}
