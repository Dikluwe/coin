#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuBgfxBackend.h"
#include "rendering/wgpu/SoWgpuBgfxCore.h"
#include "rendering/wgpu/SoWgpuImageCore.h"
#include "rendering/wgpu/SoWgpuDiagnosticShell.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"

#include "coin_bgfx_vs_glsl.h"
#include "coin_bgfx_fs_glsl.h"
#include "coin_bgfx_vs_spirv.h"
#include "coin_bgfx_fs_spirv.h"
#include "coin_bgfx_fs_peel_next_glsl.h"
#include "coin_bgfx_fs_peel_next_spirv.h"
#include "coin_bgfx_fs_composite_glsl.h"
#include "coin_bgfx_fs_composite_spirv.h"
#include "coin_bgfx_fs_weighted_oit_glsl.h"
#include "coin_bgfx_fs_weighted_oit_spirv.h"
#include "coin_bgfx_fs_weighted_composite_glsl.h"
#include "coin_bgfx_fs_weighted_composite_spirv.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace {
std::atomic<bool> bgfxInUse(false);

uint64_t drawState(const SoWgpuBgfxDraw & draw)
{
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_DEPTH_TEST_LESS;
  if (draw.blend) {
    state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
      BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
      BGFX_STATE_BLEND_INV_SRC_ALPHA);
  } else {
    state |= BGFX_STATE_WRITE_Z;
  }
  // Coin's frontFace denotes the visible winding; BGFX state denotes the
  // winding to discard. BGFX accounts for each renderer's target origin.
  if (draw.cullMode == CullMode::BACK) {
    state |= draw.frontFace == FrontFace::CCW ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
  } else if (draw.cullMode == CullMode::FRONT) {
    state |= draw.frontFace == FrontFace::CCW ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
  }
  return state;
}
const uint8_t peelPasses = 4;
const uint64_t peelTextureFlags = BGFX_TEXTURE_RT |
  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
  BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;

uint64_t peelDrawState(const SoWgpuBgfxDraw & draw, bool depthOnly)
{
  uint64_t state = drawState(draw);
  state &= ~(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
             BGFX_STATE_WRITE_Z | BGFX_STATE_BLEND_MASK);
  state |= BGFX_STATE_WRITE_Z;
  if (!depthOnly) state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
  return state;
}

bgfx::ProgramHandle createLayerProgram(const uint8_t * vertexData,
                                       uint32_t vertexBytes,
                                       const uint8_t * fragmentData,
                                       uint32_t fragmentBytes)
{
  bgfx::ShaderHandle vs = bgfx::createShader(bgfx::copy(vertexData, vertexBytes));
  bgfx::ShaderHandle fs = bgfx::createShader(bgfx::copy(fragmentData, fragmentBytes));
  if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
    if (bgfx::isValid(vs)) bgfx::destroy(vs);
    if (bgfx::isValid(fs)) bgfx::destroy(fs);
    return BGFX_INVALID_HANDLE;
  }
  return bgfx::createProgram(vs, fs, true);
}
}

SoWgpuBgfxBackend::SoWgpuBgfxBackend()
  : status(BackendStatus::NOT_READY), initialized(false), presentToWindow(false),
    cameraPatchEnabled(true), transparencyMode(TransparencyMode::OBJECT), serial(0),
    width(0), height(0), program(BGFX_INVALID_HANDLE),
    peelNextProgram(BGFX_INVALID_HANDLE), compositeProgram(BGFX_INVALID_HANDLE),
    weightedOitProgram(BGFX_INVALID_HANDLE),
    weightedCompositeProgram(BGFX_INVALID_HANDLE),
    previousDepthSampler(BGFX_INVALID_HANDLE),
    previousColorSampler(BGFX_INVALID_HANDLE),
    layerSampler(BGFX_INVALID_HANDLE), oitAccumSampler(BGFX_INVALID_HANDLE),
    oitRevealSampler(BGFX_INVALID_HANDLE), depthInfoUniform(BGFX_INVALID_HANDLE),
    fullscreenVertexBuffer(BGFX_INVALID_HANDLE),
    fullscreenIndexBuffer(BGFX_INVALID_HANDLE),
    frameBuffer(BGFX_INVALID_HANDLE), oitFrameBuffer(BGFX_INVALID_HANDLE),
    readbackTexture(BGFX_INVALID_HANDLE),
    cachedRevision(0), cachedWidth(0), cachedHeight(0),
    cachedHomogeneousDepth(false), cachedVertexBuffer(BGFX_INVALID_HANDLE),
    cachedIndexBuffer(BGFX_INVALID_HANDLE)
{
  for (uint8_t i = 0; i < peelPasses; ++i) this->peelFrameBuffers[i] = BGFX_INVALID_HANDLE;
  const char * disabled = std::getenv("COIN_BGFX_DISABLE_CAMERA_PATCH");
  this->cameraPatchEnabled = disabled == nullptr || std::strcmp(disabled, "1") != 0;
}

SoWgpuBgfxBackend::~SoWgpuBgfxBackend()
{
  if (!this->initialized) return;
  // bgfx is a process singleton and requires shutdown on its API thread.
  if (!this->onApiThread()) {
    std::fprintf(stderr, "BGFX evaluation: destroy the target on its API thread; global renderer left active\n");
    return;
  }
  this->destroyFrameBuffers();
  if (bgfx::isValid(this->fullscreenVertexBuffer)) bgfx::destroy(this->fullscreenVertexBuffer);
  if (bgfx::isValid(this->fullscreenIndexBuffer)) bgfx::destroy(this->fullscreenIndexBuffer);
  if (bgfx::isValid(this->previousDepthSampler)) bgfx::destroy(this->previousDepthSampler);
  if (bgfx::isValid(this->previousColorSampler)) bgfx::destroy(this->previousColorSampler);
  if (bgfx::isValid(this->layerSampler)) bgfx::destroy(this->layerSampler);
  if (bgfx::isValid(this->depthInfoUniform)) bgfx::destroy(this->depthInfoUniform);
  if (bgfx::isValid(this->oitAccumSampler)) bgfx::destroy(this->oitAccumSampler);
  if (bgfx::isValid(this->oitRevealSampler)) bgfx::destroy(this->oitRevealSampler);
  if (bgfx::isValid(this->cachedVertexBuffer)) bgfx::destroy(this->cachedVertexBuffer);
  if (bgfx::isValid(this->cachedIndexBuffer)) bgfx::destroy(this->cachedIndexBuffer);
  if (bgfx::isValid(this->peelNextProgram)) bgfx::destroy(this->peelNextProgram);
  if (bgfx::isValid(this->compositeProgram)) bgfx::destroy(this->compositeProgram);
  if (bgfx::isValid(this->program)) bgfx::destroy(this->program);
  if (bgfx::isValid(this->weightedOitProgram)) bgfx::destroy(this->weightedOitProgram);
  if (bgfx::isValid(this->weightedCompositeProgram)) bgfx::destroy(this->weightedCompositeProgram);
  bgfx::shutdown();
  bgfxInUse.store(false);
}

bool
SoWgpuBgfxBackend::onApiThread() const
{
  return this->apiThread == std::this_thread::get_id();
}

BackendStatus
SoWgpuBgfxBackend::prepare(SoWgpuRenderTargetP & target)
{
  if (this->initialized) return this->status;
  this->presentToWindow = target.kind == SoWgpuRenderTargetP::KIND_WINDOW;
  if (this->presentToWindow &&
      (target.nativeDesc.type != COIN_WGPU_SURFACE_XLIB ||
       target.nativeDesc.native.xlib.display == nullptr ||
       target.nativeDesc.native.xlib.window == 0)) {
    this->lastError = "BGFX window presentation requires a valid Xlib surface";
    return BackendStatus::UNSUPPORTED;
  }
  const char * transparencyMode = std::getenv("COIN_BGFX_TRANSPARENCY");
  if (transparencyMode == nullptr || std::strcmp(transparencyMode, "object") == 0) {
    this->transparencyMode = TransparencyMode::OBJECT;
  } else if (std::strcmp(transparencyMode, "sorted_layers") == 0) {
    this->transparencyMode = TransparencyMode::SORTED_LAYERS;
  } else if (std::strcmp(transparencyMode, "weighted_oit") == 0) {
    this->transparencyMode = TransparencyMode::WEIGHTED_OIT;
  } else {
    this->lastError = "COIN_BGFX_TRANSPARENCY must be object, sorted_layers, or weighted_oit";
    return BackendStatus::UNSUPPORTED;
  }
  bool expected = false;
  if (!bgfxInUse.compare_exchange_strong(expected, true)) {
    this->lastError = "BGFX evaluation permits one active target per process";
    return BackendStatus::UNSUPPORTED;
  }
  const char * rendererFlag = std::getenv("COIN_BGFX_RENDERER");
  const bool useOpenGl = rendererFlag != nullptr && std::strcmp(rendererFlag, "opengl") == 0;
  if (rendererFlag != nullptr && !useOpenGl && std::strcmp(rendererFlag, "vulkan") != 0) {
    bgfxInUse.store(false);
    this->lastError = "COIN_BGFX_RENDERER must be opengl or vulkan";
    return BackendStatus::UNSUPPORTED;
  }
  const bgfx::RendererType::Enum renderer =
    useOpenGl ? bgfx::RendererType::OpenGL : bgfx::RendererType::Vulkan;
  bgfx::Init init;
  init.type = renderer;
  if (this->presentToWindow) {
    init.platformData.type = bgfx::NativeWindowHandleType::Default;
    init.swapChain.ndt = target.nativeDesc.native.xlib.display;
    init.swapChain.nwh = reinterpret_cast<void *>(
      static_cast<uintptr_t>(target.nativeDesc.native.xlib.window));
    init.swapChain.width = static_cast<uint32_t>(target.size[0]);
    init.swapChain.height = static_cast<uint32_t>(target.size[1]);
  } else {
    init.swapChain.width = 0;
    init.swapChain.height = 0;
  }
  if (!bgfx::init(init)) {
    bgfxInUse.store(false);
    this->lastError = this->presentToWindow ?
      "BGFX could not initialize Xlib window presentation" :
      (useOpenGl ? "BGFX could not initialize its headless OpenGL renderer" :
                   "BGFX could not initialize its headless Vulkan renderer");
    return BackendStatus::NOT_READY;
  }
  this->initialized = true;
  this->apiThread = std::this_thread::get_id();
  const bgfx::Caps * caps = bgfx::getCaps();
  if (SoWgpuDiagnosticShell::phaseTracingEnabled()) {
    std::fprintf(stderr,
      "COIN_WGPU_PHASE bgfx_device renderer=%s vendor_id=0x%04x device_id=0x%04x homogeneous_depth=%d\n",
      useOpenGl ? "opengl" : "vulkan",
      static_cast<unsigned int>(caps->vendorId),
      static_cast<unsigned int>(caps->deviceId),
      caps->homogeneousDepth ? 1 : 0);
    std::fprintf(stderr,
      "COIN_WGPU_PHASE bgfx_formats rgba8=0x%x d24s8=0x%x d32f=0x%x readback=%d\n",
      static_cast<unsigned int>(caps->formats[bgfx::TextureFormat::RGBA8]),
      static_cast<unsigned int>(caps->formats[bgfx::TextureFormat::D24S8]),
      static_cast<unsigned int>(caps->formats[bgfx::TextureFormat::D32F]),
      bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK) ? 1 : 0);
  }
  if (caps->rendererType != renderer ||
      !(caps->supported & BGFX_CAPS_INDEX32) ||
      (!this->presentToWindow &&
       (!(caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
        (!(caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
         !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)) ||
        !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA8,
                              BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK)))) {
    this->lastError = "BGFX renderer lacks the required window/offscreen capabilities";
    this->status = BackendStatus::UNSUPPORTED;
    return this->status;
  }
  if (this->transparencyMode == TransparencyMode::SORTED_LAYERS &&
      (!(caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
       !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
       !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::D32F,
                             peelTextureFlags))) {
    this->lastError = "BGFX sorted layers requires sampleable D32F and RGBA8 render targets";
    this->status = BackendStatus::UNSUPPORTED;
    return this->status;
  }
  if (this->transparencyMode == TransparencyMode::WEIGHTED_OIT &&
      (!(caps->supported & BGFX_CAPS_BLEND_INDEPENDENT) ||
       caps->limits.maxFBAttachments < 2 ||
       !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA16F,
                             peelTextureFlags) ||
       !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R16F,
                             peelTextureFlags))) {
    this->lastError = "BGFX weighted OIT requires independent blending and sampleable RGBA16F/R16F render targets";
    this->status = BackendStatus::UNSUPPORTED;
    return this->status;
  }
  this->layout.begin()
    .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
    .end();
  bgfx::ShaderHandle vs = bgfx::createShader(useOpenGl ?
    bgfx::copy(coin_bgfx_vs_glsl, sizeof(coin_bgfx_vs_glsl)) :
    bgfx::copy(coin_bgfx_vs_spirv, sizeof(coin_bgfx_vs_spirv)));
  bgfx::ShaderHandle fs = bgfx::createShader(useOpenGl ?
    bgfx::copy(coin_bgfx_fs_glsl, sizeof(coin_bgfx_fs_glsl)) :
    bgfx::copy(coin_bgfx_fs_spirv, sizeof(coin_bgfx_fs_spirv)));
  if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
    if (bgfx::isValid(vs)) bgfx::destroy(vs);
    if (bgfx::isValid(fs)) bgfx::destroy(fs);
    this->lastError = "BGFX failed to create its renderer-specific shaders";
    this->status = BackendStatus::BACKEND_ERROR;
    return this->status;
  }
  this->program = bgfx::createProgram(vs, fs, true);
  if (!bgfx::isValid(this->program)) {
    this->lastError = "BGFX failed to link its base-color shader program";
    this->status = BackendStatus::BACKEND_ERROR;
    return this->status;
  }
  if (this->transparencyMode == TransparencyMode::SORTED_LAYERS) {
    const uint8_t * vertexShader = useOpenGl ? coin_bgfx_vs_glsl : coin_bgfx_vs_spirv;
    const uint32_t vertexBytes = useOpenGl ? sizeof(coin_bgfx_vs_glsl) : sizeof(coin_bgfx_vs_spirv);
    this->peelNextProgram = createLayerProgram(vertexShader, vertexBytes,
      useOpenGl ? coin_bgfx_fs_peel_next_glsl : coin_bgfx_fs_peel_next_spirv,
      useOpenGl ? sizeof(coin_bgfx_fs_peel_next_glsl) : sizeof(coin_bgfx_fs_peel_next_spirv));
    this->compositeProgram = createLayerProgram(vertexShader, vertexBytes,
      useOpenGl ? coin_bgfx_fs_composite_glsl : coin_bgfx_fs_composite_spirv,
      useOpenGl ? sizeof(coin_bgfx_fs_composite_glsl) : sizeof(coin_bgfx_fs_composite_spirv));
    this->previousDepthSampler = bgfx::createUniform("s_prevDepth", bgfx::UniformType::Sampler);
    this->previousColorSampler = bgfx::createUniform("s_prevColor", bgfx::UniformType::Sampler);
    this->layerSampler = bgfx::createUniform("s_layer", bgfx::UniformType::Sampler);
    this->depthInfoUniform = bgfx::createUniform("u_depthInfo", bgfx::UniformType::Vec4);
    SoWgpuBgfxVertex fullscreen[3] = {};
    fullscreen[0].position[0] = -1.0f; fullscreen[0].position[1] = -1.0f;
    fullscreen[1].position[0] =  3.0f; fullscreen[1].position[1] = -1.0f;
    fullscreen[2].position[0] = -1.0f; fullscreen[2].position[1] =  3.0f;
    const uint16_t indices[3] = {0, 1, 2};
    this->fullscreenVertexBuffer = bgfx::createVertexBuffer(
      bgfx::copy(fullscreen, sizeof(fullscreen)), this->layout);
    this->fullscreenIndexBuffer = bgfx::createIndexBuffer(
      bgfx::copy(indices, sizeof(indices)));
    if (!bgfx::isValid(this->peelNextProgram) ||
        !bgfx::isValid(this->compositeProgram) ||
        !bgfx::isValid(this->previousDepthSampler) ||
        !bgfx::isValid(this->previousColorSampler) ||
        !bgfx::isValid(this->layerSampler) ||
        !bgfx::isValid(this->depthInfoUniform) ||
        !bgfx::isValid(this->fullscreenVertexBuffer) ||
        !bgfx::isValid(this->fullscreenIndexBuffer)) {
      this->lastError = "BGFX could not allocate sorted-layers programs and uniforms";
      this->status = BackendStatus::BACKEND_ERROR;
      return this->status;
    }
  }
  if (this->transparencyMode == TransparencyMode::WEIGHTED_OIT) {
    const uint8_t * vertexShader = useOpenGl ? coin_bgfx_vs_glsl : coin_bgfx_vs_spirv;
    const uint32_t vertexBytes = useOpenGl ? sizeof(coin_bgfx_vs_glsl) : sizeof(coin_bgfx_vs_spirv);
    this->weightedOitProgram = createLayerProgram(vertexShader, vertexBytes,
      useOpenGl ? coin_bgfx_fs_weighted_oit_glsl : coin_bgfx_fs_weighted_oit_spirv,
      useOpenGl ? sizeof(coin_bgfx_fs_weighted_oit_glsl) : sizeof(coin_bgfx_fs_weighted_oit_spirv));
    this->weightedCompositeProgram = createLayerProgram(vertexShader, vertexBytes,
      useOpenGl ? coin_bgfx_fs_weighted_composite_glsl : coin_bgfx_fs_weighted_composite_spirv,
      useOpenGl ? sizeof(coin_bgfx_fs_weighted_composite_glsl) : sizeof(coin_bgfx_fs_weighted_composite_spirv));
    this->oitAccumSampler = bgfx::createUniform("s_oitAccum", bgfx::UniformType::Sampler);
    this->oitRevealSampler = bgfx::createUniform("s_oitReveal", bgfx::UniformType::Sampler);
    this->depthInfoUniform = bgfx::createUniform("u_depthInfo", bgfx::UniformType::Vec4);
    SoWgpuBgfxVertex fullscreen[3] = {};
    fullscreen[0].position[0] = -1.0f; fullscreen[0].position[1] = -1.0f;
    fullscreen[1].position[0] =  3.0f; fullscreen[1].position[1] = -1.0f;
    fullscreen[2].position[0] = -1.0f; fullscreen[2].position[1] =  3.0f;
    const uint16_t indices[3] = {0, 1, 2};
    this->fullscreenVertexBuffer = bgfx::createVertexBuffer(
      bgfx::copy(fullscreen, sizeof(fullscreen)), this->layout);
    this->fullscreenIndexBuffer = bgfx::createIndexBuffer(bgfx::copy(indices, sizeof(indices)));
    if (!bgfx::isValid(this->weightedOitProgram) ||
        !bgfx::isValid(this->weightedCompositeProgram) ||
        !bgfx::isValid(this->oitAccumSampler) ||
        !bgfx::isValid(this->oitRevealSampler) ||
        !bgfx::isValid(this->depthInfoUniform) ||
        !bgfx::isValid(this->fullscreenVertexBuffer) ||
        !bgfx::isValid(this->fullscreenIndexBuffer)) {
      char detail[192];
      std::snprintf(detail, sizeof(detail),
        "BGFX weighted-OIT resources failed: draw=%d composite=%d accum=%d reveal=%d depthInfo=%d vb=%d ib=%d",
        bgfx::isValid(this->weightedOitProgram) ? 1 : 0,
        bgfx::isValid(this->weightedCompositeProgram) ? 1 : 0,
        bgfx::isValid(this->oitAccumSampler) ? 1 : 0,
        bgfx::isValid(this->oitRevealSampler) ? 1 : 0,
        bgfx::isValid(this->depthInfoUniform) ? 1 : 0,
        bgfx::isValid(this->fullscreenVertexBuffer) ? 1 : 0,
        bgfx::isValid(this->fullscreenIndexBuffer) ? 1 : 0);
      this->lastError = detail;
      this->status = BackendStatus::BACKEND_ERROR;
      return this->status;
    }
  }
  if (!this->resize(target.size[0], target.size[1])) {
    this->status = BackendStatus::BACKEND_ERROR;
    return this->status;
  }
  this->status = BackendStatus::SUCCESS;
  this->lastError.clear();
  return this->status;
}

void
SoWgpuBgfxBackend::destroyFrameBuffers()
{
  for (uint8_t pass = 0; pass < peelPasses; ++pass) {
    if (bgfx::isValid(this->peelFrameBuffers[pass]))
      bgfx::destroy(this->peelFrameBuffers[pass]);
    this->peelFrameBuffers[pass] = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(this->oitFrameBuffer)) bgfx::destroy(this->oitFrameBuffer);
  this->oitFrameBuffer = BGFX_INVALID_HANDLE;
  if (bgfx::isValid(this->readbackTexture)) bgfx::destroy(this->readbackTexture);
  if (bgfx::isValid(this->frameBuffer)) bgfx::destroy(this->frameBuffer);
  this->readbackTexture = BGFX_INVALID_HANDLE;
  this->frameBuffer = BGFX_INVALID_HANDLE;
  this->width = this->height = 0;
}

bool
SoWgpuBgfxBackend::resize(int newWidth, int newHeight)
{
  if (newWidth == this->width && newHeight == this->height) return true;
  if (newWidth <= 0 || newHeight <= 0 || newWidth > 16384 || newHeight > 16384) {
    this->lastError = "Invalid BGFX target dimensions";
    return false;
  }
  this->destroyFrameBuffers();
  const bgfx::Caps * caps = bgfx::getCaps();
  const bgfx::TextureFormat::Enum depthFormat =
    (caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
      ? bgfx::TextureFormat::D24S8 : bgfx::TextureFormat::D32F;
  if (this->presentToWindow) {
    bgfx::SwapChain swapChain;
    swapChain.width = static_cast<uint32_t>(newWidth);
    swapChain.height = static_cast<uint32_t>(newHeight);
    bgfx::reset(BGFX_RESET_NONE, &swapChain);
  } else {
    bgfx::TextureHandle color = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, bgfx::TextureFormat::RGBA8, peelTextureFlags);
    bgfx::TextureHandle depth = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, depthFormat, BGFX_TEXTURE_RT_WRITE_ONLY);
    if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
      if (bgfx::isValid(color)) bgfx::destroy(color);
      if (bgfx::isValid(depth)) bgfx::destroy(depth);
      this->lastError = "BGFX could not allocate offscreen color/depth textures";
      return false;
    }
    const bgfx::TextureHandle attachments[2] = {color, depth};
    this->frameBuffer = bgfx::createFrameBuffer(2, attachments, true);
    if (!bgfx::isValid(this->frameBuffer)) {
      bgfx::destroy(color);
      bgfx::destroy(depth);
      this->lastError = "BGFX could not create an offscreen color/depth framebuffer";
      return false;
    }
    this->readbackTexture = bgfx::createTexture2D(static_cast<uint16_t>(newWidth),
      static_cast<uint16_t>(newHeight), false, 1, bgfx::TextureFormat::RGBA8,
      BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    if (!bgfx::isValid(this->readbackTexture)) {
      this->lastError = "BGFX could not allocate the offscreen readback texture";
      this->destroyFrameBuffers();
      return false;
    }
  }
  if (this->transparencyMode == TransparencyMode::SORTED_LAYERS) {
    for (uint8_t pass = 0; pass < peelPasses; ++pass) {
      bgfx::TextureHandle color = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, bgfx::TextureFormat::RGBA8, peelTextureFlags);
      bgfx::TextureHandle depth = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, bgfx::TextureFormat::D32F, peelTextureFlags);
      if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
        if (bgfx::isValid(color)) bgfx::destroy(color);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
        this->lastError = "BGFX could not allocate sorted-layer textures";
        this->destroyFrameBuffers();
        return false;
      }
      const bgfx::TextureHandle attachments[2] = {color, depth};
      this->peelFrameBuffers[pass] = bgfx::createFrameBuffer(2, attachments, true);
      if (!bgfx::isValid(this->peelFrameBuffers[pass])) {
        bgfx::destroy(color);
        bgfx::destroy(depth);
        this->lastError = "BGFX could not create sorted-layer framebuffer";
        this->destroyFrameBuffers();
        return false;
      }
    }
  }
  if (this->transparencyMode == TransparencyMode::WEIGHTED_OIT) {
    bgfx::TextureHandle accum = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, bgfx::TextureFormat::RGBA16F, peelTextureFlags);
    bgfx::TextureHandle reveal = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, bgfx::TextureFormat::R16F, peelTextureFlags);
    bgfx::TextureHandle depth = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, depthFormat, BGFX_TEXTURE_RT_WRITE_ONLY);
    if (!bgfx::isValid(accum) || !bgfx::isValid(reveal) || !bgfx::isValid(depth)) {
      if (bgfx::isValid(accum)) bgfx::destroy(accum);
      if (bgfx::isValid(reveal)) bgfx::destroy(reveal);
      if (bgfx::isValid(depth)) bgfx::destroy(depth);
      this->lastError = "BGFX could not allocate weighted-OIT textures";
      this->destroyFrameBuffers();
      return false;
    }
    const bgfx::TextureHandle attachments[3] = {accum, reveal, depth};
    this->oitFrameBuffer = bgfx::createFrameBuffer(3, attachments, true);
    if (!bgfx::isValid(this->oitFrameBuffer)) {
      bgfx::destroy(accum);
      bgfx::destroy(reveal);
      bgfx::destroy(depth);
      this->lastError = "BGFX could not create weighted-OIT framebuffer";
      this->destroyFrameBuffers();
      return false;
    }
  }
  this->width = newWidth;
  this->height = newHeight;
  return true;
}

void
SoWgpuBgfxBackend::encodeSortedLayers(const std::vector<SoWgpuBgfxDraw> & draws,
                                      bgfx::VertexBufferHandle vertices,
                                      bgfx::IndexBufferHandle indices,
                                      bgfx::FrameBufferHandle output)
{
  const float transparentBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float depthInfo[4] = {1.0f / float(this->width),
                              1.0f / float(this->height), 0.0f, 0.0f};
  bgfx::setPaletteColor(1, transparentBlack);
  for (uint8_t pass = 0; pass < peelPasses; ++pass) {
    const uint8_t view = uint8_t(pass + 1);
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(view, 0, 0, static_cast<uint16_t>(this->width),
                      static_cast<uint16_t>(this->height));
    bgfx::setViewFrameBuffer(view, this->peelFrameBuffers[pass]);
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 1);
    bgfx::setViewTransform(view, nullptr, nullptr);
    bgfx::touch(view);
    // Opaque geometry supplies the occlusion depth in every peel pass.
    for (const SoWgpuBgfxDraw & draw : draws) {
      if (draw.blend) continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vertices);
      bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
      bgfx::setState(peelDrawState(draw, true));
      bgfx::submit(view, this->program);
    }
    for (const SoWgpuBgfxDraw & draw : draws) {
      if (!draw.blend || draw.alpha <= 0.0f) continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vertices);
      bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
      bgfx::setState(peelDrawState(draw, false));
      if (pass != 0) {
        bgfx::setUniform(this->depthInfoUniform, depthInfo);
        bgfx::setTexture(0, this->previousDepthSampler,
                         bgfx::getTexture(this->peelFrameBuffers[pass - 1], 1));
        bgfx::setTexture(1, this->previousColorSampler,
                         bgfx::getTexture(this->peelFrameBuffers[pass - 1], 0));
      }
      bgfx::submit(view, pass == 0 ? this->program : this->peelNextProgram);
    }
  }

  const uint8_t compositeView = uint8_t(peelPasses + 1);
  bgfx::setViewMode(compositeView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(compositeView, 0, 0, static_cast<uint16_t>(this->width),
                    static_cast<uint16_t>(this->height));
  bgfx::setViewFrameBuffer(compositeView, output);
  bgfx::setViewClear(compositeView, BGFX_CLEAR_NONE);
  bgfx::setViewTransform(compositeView, nullptr, nullptr);
  bgfx::touch(compositeView);
  const float identity[16] = {1, 0, 0, 0,
                              0, 1, 0, 0,
                              0, 0, 1, 0,
                              0, 0, 0, 1};
  const uint64_t blendState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
      BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
      BGFX_STATE_BLEND_INV_SRC_ALPHA);
  for (int pass = int(peelPasses) - 1; pass >= 0; --pass) {
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, depthInfo);
    bgfx::setTexture(0, this->layerSampler,
                     bgfx::getTexture(this->peelFrameBuffers[pass], 0));
    bgfx::setState(blendState);
    bgfx::submit(compositeView, this->compositeProgram);
  }
}

void
SoWgpuBgfxBackend::encodeWeightedOit(const std::vector<SoWgpuBgfxDraw> & draws,
                                     bgfx::VertexBufferHandle vertices,
                                     bgfx::IndexBufferHandle indices,
                                     bgfx::FrameBufferHandle output)
{
  const uint8_t oitView = 1;
  const float transparentBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float opaqueWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  const float depthInfo[4] = {1.0f / float(this->width),
                              1.0f / float(this->height), 0.0f, 0.0f};
  bgfx::setPaletteColor(1, transparentBlack);
  bgfx::setPaletteColor(2, opaqueWhite);
  bgfx::setViewMode(oitView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(oitView, 0, 0, static_cast<uint16_t>(this->width),
                    static_cast<uint16_t>(this->height));
  bgfx::setViewFrameBuffer(oitView, this->oitFrameBuffer);
  bgfx::setViewClear(oitView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                     1.0f, 0, 1, 2);
  bgfx::setViewTransform(oitView, nullptr, nullptr);
  bgfx::touch(oitView);

  // Rebuild only opaque depth so transparent fragments behind opaque Coin
  // geometry cannot contribute to either accumulation attachment.
  for (const SoWgpuBgfxDraw & draw : draws) {
    if (draw.blend) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vertices);
    bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
    bgfx::setState(peelDrawState(draw, true));
    bgfx::submit(oitView, this->program);
  }

  for (const SoWgpuBgfxDraw & draw : draws) {
    if (!draw.blend || draw.alpha <= 0.0f) continue;
    uint64_t state = drawState(draw);
    state &= ~(BGFX_STATE_WRITE_Z | BGFX_STATE_BLEND_MASK);
    state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
             BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE) |
             BGFX_STATE_BLEND_INDEPENDENT;
    const uint32_t independentBlend = BGFX_STATE_BLEND_FUNC_RT_1(
      BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_COLOR);
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vertices);
    bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
    bgfx::setState(state, independentBlend);
    bgfx::submit(oitView, this->weightedOitProgram);
  }

  const uint8_t compositeView = 2;
  bgfx::setViewMode(compositeView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(compositeView, 0, 0, static_cast<uint16_t>(this->width),
                    static_cast<uint16_t>(this->height));
  bgfx::setViewFrameBuffer(compositeView, output);
  bgfx::setViewClear(compositeView, BGFX_CLEAR_NONE);
  bgfx::setViewTransform(compositeView, nullptr, nullptr);
  bgfx::touch(compositeView);
  const float identity[16] = {1, 0, 0, 0,
                              0, 1, 0, 0,
                              0, 0, 1, 0,
                              0, 0, 0, 1};
  const uint64_t blendState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
      BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
      BGFX_STATE_BLEND_INV_SRC_ALPHA);
  bgfx::setTransform(identity);
  bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
  bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
  bgfx::setUniform(this->depthInfoUniform, depthInfo);
  bgfx::setTexture(0, this->oitAccumSampler,
                   bgfx::getTexture(this->oitFrameBuffer, 0));
  bgfx::setTexture(1, this->oitRevealSampler,
                   bgfx::getTexture(this->oitFrameBuffer, 1));
  bgfx::setState(blendState);
  bgfx::submit(compositeView, this->weightedCompositeProgram);
}

SubmitResult
SoWgpuBgfxBackend::submit(const FramePlan & frame, SoWgpuRenderTargetP & target)
{
  return this->submit(frame, target, SoWgpuFrameReuseDecision());
}

SubmitResult
SoWgpuBgfxBackend::submit(const FramePlan & frame, SoWgpuRenderTargetP & target,
                          const SoWgpuFrameReuseDecision & reuse)
{
  if (!this->initialized || this->status != BackendStatus::SUCCESS || !this->onApiThread()) {
    this->lastError = "BGFX submission requires a prepared backend on its API thread";
    return SubmitResult(BackendStatus::NOT_READY, this->lastError);
  }
  if ((target.kind == SoWgpuRenderTargetP::KIND_WINDOW) != this->presentToWindow ||
      target.directTextureOutput ||
      (!this->presentToWindow && target.depthReadbackEnabled)) {
    this->lastError = "BGFX supports Xlib presentation or offscreen color readback; disable offscreen depth readback";
    return SubmitResult(BackendStatus::UNSUPPORTED, this->lastError);
  }
  typedef std::chrono::steady_clock Clock;
  const Clock::time_point begin = Clock::now();
  const bool tracePhases = SoWgpuDiagnosticShell::phaseTracingEnabled();
  const char * gpuTimestampFlag = std::getenv("COIN_WGPU_GPU_TIMESTAMPS");
  const bool traceGpu = tracePhases && gpuTimestampFlag != nullptr &&
                        std::strcmp(gpuTimestampFlag, "1") == 0;
  const bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;
  const bool cacheDimensionsMatch =
    target.size[0] == this->cachedWidth &&
    target.size[1] == this->cachedHeight &&
    homogeneousDepth == this->cachedHomogeneousDepth;
  const bool cacheHit = frame.revision != 0 &&
    frame.revision == this->cachedRevision && cacheDimensionsMatch;
  const bool cameraPatchEligible =
    this->cameraPatchEnabled &&
    reuse.kind == SoWgpuFrameReuseKind::CAMERA_PATCH &&
    reuse.baseRevision != 0 && reuse.baseRevision == this->cachedRevision &&
    frame.revision != 0 && frame.revision != this->cachedRevision &&
    cacheDimensionsMatch &&
    (this->cachedPlan.draws.empty() ||
      (bgfx::isValid(this->cachedVertexBuffer) &&
       bgfx::isValid(this->cachedIndexBuffer)));
  std::vector<SoWgpuBgfxDraw> cameraDraws;
  const bool cameraPatchUsed = cameraPatchEligible &&
    SoWgpuBgfxCore::patchCamera(frame, homogeneousDepth, this->cachedPlan,
                                cameraDraws, this->lastError);
  SoWgpuBgfxPlan freshPlan;
  const SoWgpuBgfxPlan * plan = &this->cachedPlan;
  if (!cacheHit && !cameraPatchUsed) {
    if (!SoWgpuBgfxCore::lower(frame, target.size[0], target.size[1],
                              homogeneousDepth, freshPlan, this->lastError)) {
      return SubmitResult(BackendStatus::UNSUPPORTED, this->lastError);
    }
    plan = &freshPlan;
  }
  const Clock::time_point lowered = Clock::now();
  if (!this->resize(target.size[0], target.size[1])) {
    return SubmitResult(BackendStatus::OUT_OF_MEMORY, this->lastError);
  }
  if (plan->vertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(SoWgpuBgfxVertex) ||
      plan->indices.size() > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t)) {
    this->lastError = "BGFX geometry exceeds buffer size limits";
    return SubmitResult(BackendStatus::OUT_OF_MEMORY, this->lastError);
  }
  const size_t geometryBytes = plan->vertices.size() * sizeof(SoWgpuBgfxVertex) +
                               plan->indices.size() * sizeof(uint32_t) +
                               plan->draws.size() * sizeof(SoWgpuBgfxDraw);
  bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
  if (cacheHit || cameraPatchUsed) {
    vb = this->cachedVertexBuffer;
    ib = this->cachedIndexBuffer;
  }
  bool retained = cacheHit || cameraPatchUsed;
  if (!retained) {
    if (!plan->draws.empty()) {
      vb = bgfx::createVertexBuffer(bgfx::copy(plan->vertices.data(),
        static_cast<uint32_t>(plan->vertices.size() * sizeof(SoWgpuBgfxVertex))), this->layout);
      ib = bgfx::createIndexBuffer(bgfx::copy(plan->indices.data(),
        static_cast<uint32_t>(plan->indices.size() * sizeof(uint32_t))), BGFX_BUFFER_INDEX32);
      if (!bgfx::isValid(vb) || !bgfx::isValid(ib)) {
        if (bgfx::isValid(vb)) bgfx::destroy(vb);
        if (bgfx::isValid(ib)) bgfx::destroy(ib);
        this->lastError = "BGFX geometry upload failed";
        return SubmitResult(BackendStatus::OUT_OF_MEMORY, this->lastError);
      }
    }
    if (bgfx::isValid(this->cachedVertexBuffer)) bgfx::destroy(this->cachedVertexBuffer);
    if (bgfx::isValid(this->cachedIndexBuffer)) bgfx::destroy(this->cachedIndexBuffer);
    this->cachedVertexBuffer = BGFX_INVALID_HANDLE;
    this->cachedIndexBuffer = BGFX_INVALID_HANDLE;
    this->cachedRevision = 0;
    if (frame.revision != 0 && geometryBytes <= 32u * 1024u * 1024u) {
      this->cachedPlan = std::move(freshPlan);
      this->cachedVertexBuffer = vb;
      this->cachedIndexBuffer = ib;
      this->cachedRevision = frame.revision;
      this->cachedWidth = target.size[0];
      this->cachedHeight = target.size[1];
      this->cachedHomogeneousDepth = homogeneousDepth;
      plan = &this->cachedPlan;
      retained = true;
    }
  }

  const Clock::time_point uploaded = Clock::now();
  bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(this->width),
                    static_cast<uint16_t>(this->height));
  const bgfx::FrameBufferHandle windowFrameBuffer = BGFX_INVALID_HANDLE;
  bgfx::setViewFrameBuffer(0, this->presentToWindow ? windowFrameBuffer : this->frameBuffer);
  // The packed clear API quantizes Coin's float color before the GL clear.
  // Keep the float until the renderer converts it to its target format.
  bgfx::setPaletteColor(0, plan->clearColor);
  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 0);
  bgfx::setViewTransform(0, nullptr, nullptr);
  bgfx::touch(0);
  const std::vector<SoWgpuBgfxDraw> & draws =
    cameraPatchUsed ? cameraDraws : plan->draws;
  bool hasTransparentDraws = false;
  if (this->transparencyMode != TransparencyMode::OBJECT) {
    for (const SoWgpuBgfxDraw & draw : draws) {
      if (draw.blend) { hasTransparentDraws = true; break; }
    }
  }
  const bool useSortedLayers = hasTransparentDraws &&
    this->transparencyMode == TransparencyMode::SORTED_LAYERS;
  const bool useWeightedOit = hasTransparentDraws &&
    this->transparencyMode == TransparencyMode::WEIGHTED_OIT;
  for (const SoWgpuBgfxDraw & draw : draws) {
    if ((useSortedLayers || useWeightedOit) && draw.blend) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    bgfx::setState(drawState(draw));
    bgfx::submit(0, this->program);
  }
  if (useSortedLayers) {
    this->encodeSortedLayers(draws, vb, ib,
      this->presentToWindow ? windowFrameBuffer : this->frameBuffer);
  } else if (useWeightedOit) {
    this->encodeWeightedOit(draws, vb, ib,
      this->presentToWindow ? windowFrameBuffer : this->frameBuffer);
  }
  const Clock::time_point drawsEncoded = Clock::now();
  if (this->presentToWindow) {
    const uint32_t submittedFrame = bgfx::frame();
    const Clock::time_point submitted = Clock::now();
    if (!retained) {
      if (bgfx::isValid(vb)) bgfx::destroy(vb);
      if (bgfx::isValid(ib)) bgfx::destroy(ib);
    }
    if (cameraPatchUsed) {
      this->cachedPlan.draws.swap(cameraDraws);
      this->cachedRevision = frame.revision;
    }
    target.colorBuffer.clear();
    target.depthBuffer.clear();
    target.needsReconfigure = false;
    if (tracePhases) {
      SoWgpuBgfxPhaseSample sample;
      const auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
      };
      sample.lowerMs = ms(begin, lowered);
      sample.uploadMs = ms(lowered, uploaded);
      sample.encodeMs = ms(uploaded, drawsEncoded);
      sample.drawEncodeMs = sample.encodeMs;
      sample.submitFrameMs = ms(drawsEncoded, submitted);
      sample.vertices = plan->vertices.size();
      sample.draws = plan->draws.size();
      sample.resourceCacheHit = cacheHit || cameraPatchUsed;
      sample.cameraPatchUsed = cameraPatchUsed;
      if (traceGpu) {
        sample.gpuTimingRequested = true;
        const bgfx::Stats * stats = bgfx::getStats();
        if (stats && stats->gpuFrameNum == submittedFrame &&
            stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin) {
          sample.gpuFrameMs = double(stats->gpuTimeEnd - stats->gpuTimeBegin) *
                              1000.0 / double(stats->gpuTimerFreq);
        }
      }
      std::fprintf(stderr, "%s\n", SoWgpuDiagnosticShell::formatBgfxPhase(sample).c_str());
    }
    this->lastError.clear();
    return SubmitResult(BackendStatus::SUCCESS, "", ++this->serial);
  }
  bgfx::TextureRegion destination;
  destination.handle = this->readbackTexture;
  bgfx::TextureRegion source;
  source.handle = bgfx::getTexture(this->frameBuffer);
  const uint8_t blitView = useSortedLayers ? uint8_t(peelPasses + 2) :
    (useWeightedOit ? uint8_t(3) : uint8_t(1));
  bgfx::blit(blitView, destination, source);
  const Clock::time_point encoded = Clock::now();
  const size_t bytes = static_cast<size_t>(this->width) * static_cast<size_t>(this->height) * 4;
  target.colorBuffer.resize(bytes);
  bgfx::TextureRegion readRegion;
  readRegion.handle = this->readbackTexture;
  const uint32_t readyFrame = bgfx::read(readRegion, target.colorBuffer.data());
  const Clock::time_point readRequested = Clock::now();
  uint32_t completedFrame = bgfx::frame();
  uint32_t readWaitFrames = 1;
  const uint32_t submittedFrame = completedFrame;
  const Clock::time_point submitted = Clock::now();
  double gpuFrameMs = -1.0;
  const auto captureGpuFrame = [&]() {
    if (!traceGpu) return;
    const bgfx::Stats * stats = bgfx::getStats();
    if (stats && stats->gpuFrameNum == submittedFrame &&
        stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin) {
      gpuFrameMs = double(stats->gpuTimeEnd - stats->gpuTimeBegin) *
                   1000.0 / double(stats->gpuTimerFreq);
    }
  };
  captureGpuFrame();
  // BGFX readback is delayed by multiple frames even for a synchronous API.
  for (int attempts = 0; completedFrame < readyFrame && attempts < 16; ++attempts) {
    completedFrame = bgfx::frame();
    ++readWaitFrames;
    captureGpuFrame();
  }
  if (completedFrame < readyFrame) {
    if (!retained) {
      if (bgfx::isValid(vb)) bgfx::destroy(vb);
      if (bgfx::isValid(ib)) bgfx::destroy(ib);
    }
    this->lastError = "BGFX readback did not complete within sixteen frames";
    return SubmitResult(BackendStatus::BACKEND_ERROR, this->lastError);
  }
  const Clock::time_point framesCompleted = Clock::now();
  // GL readback follows the framebuffer's bottom-left origin; Vulkan's
  // readback in this profile already matches the target's top-left RGBA view.
  // The row-swap is symmetric, so the shared mechanical image helper applies.
  if (bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL &&
      !SoWgpuImageCore::flipRgba8Rows(target.colorBuffer, target.size)) {
    if (!retained) {
      if (bgfx::isValid(vb)) bgfx::destroy(vb);
      if (bgfx::isValid(ib)) bgfx::destroy(ib);
    }
    this->lastError = "BGFX OpenGL returned an invalid RGBA readback size";
    return SubmitResult(BackendStatus::BACKEND_ERROR, this->lastError);
  }
  const Clock::time_point readComplete = Clock::now();
  const Clock::time_point gpuDrainBegin = Clock::now();
  uint32_t gpuQueryFrames = 0;
  for (int attempts = 0; traceGpu && gpuFrameMs < 0.0 && attempts < 4; ++attempts) {
    bgfx::frame();
    ++gpuQueryFrames;
    captureGpuFrame();
  }
  const Clock::time_point gpuDrainComplete = Clock::now();
  if (!retained) {
    if (bgfx::isValid(vb)) bgfx::destroy(vb);
    if (bgfx::isValid(ib)) bgfx::destroy(ib);
  }
  if (cameraPatchUsed) {
    this->cachedPlan.draws.swap(cameraDraws);
    this->cachedRevision = frame.revision;
  }
  target.depthBuffer.clear();
  if (tracePhases) {
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    SoWgpuBgfxPhaseSample sample;
    sample.lowerMs = ms(begin, lowered);
    sample.uploadMs = ms(lowered, uploaded);
    sample.encodeMs = ms(uploaded, encoded);
    sample.drawEncodeMs = ms(uploaded, drawsEncoded);
    sample.blitEncodeMs = ms(drawsEncoded, encoded);
    sample.gpuTimingRequested = traceGpu;
    sample.gpuQueryDrainMs = ms(gpuDrainBegin, gpuDrainComplete);
    sample.gpuQueryFrames = gpuQueryFrames;
    sample.readRequestMs = ms(encoded, readRequested);
    sample.submitFrameMs = ms(readRequested, submitted);
    sample.gpuFrameMs = gpuFrameMs;
    sample.readWaitMs = ms(submitted, readComplete);
    sample.frameWaitMs = ms(submitted, framesCompleted);
    sample.rowFlipMs = ms(framesCompleted, readComplete);
    sample.vertices = plan->vertices.size();
    sample.draws = plan->draws.size();
    sample.readWaitFrames = readWaitFrames;
    sample.resourceCacheHit = cacheHit || cameraPatchUsed;
    sample.cameraPatchUsed = cameraPatchUsed;
    std::fprintf(stderr, "%s\n", SoWgpuDiagnosticShell::formatBgfxPhase(sample).c_str());
  }
  this->lastError.clear();
  return SubmitResult(BackendStatus::SUCCESS, "", ++this->serial);
}

void
SoWgpuBgfxBackend::poll()
{
  // Synchronous readback in submit() already advances BGFX frames.
}
