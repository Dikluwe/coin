#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuBgfxBackend.h"
#include "rendering/wgpu/SoWgpuBgfxCore.h"
#include "rendering/wgpu/SoWgpuDiagnosticShell.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"

#include "coin_bgfx_vs_spirv.h"
#include "coin_bgfx_fs_spirv.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace {
std::atomic<bool> bgfxInUse(false);

uint64_t drawState(const SoWgpuBgfxDraw & draw)
{
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
  // Coin's frontFace denotes the visible winding; BGFX state denotes the
  // winding to discard. The Vulkan viewport origin remains top-left.
  if (draw.cullMode == CullMode::BACK) {
    state |= draw.frontFace == FrontFace::CCW ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
  } else if (draw.cullMode == CullMode::FRONT) {
    state |= draw.frontFace == FrontFace::CCW ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
  }
  return state;
}
}

SoWgpuBgfxBackend::SoWgpuBgfxBackend()
  : status(BackendStatus::NOT_READY), initialized(false), serial(0),
    width(0), height(0), program(BGFX_INVALID_HANDLE),
    frameBuffer(BGFX_INVALID_HANDLE), readbackTexture(BGFX_INVALID_HANDLE),
    cachedRevision(0), cachedWidth(0), cachedHeight(0),
    cachedHomogeneousDepth(false), cachedVertexBuffer(BGFX_INVALID_HANDLE),
    cachedIndexBuffer(BGFX_INVALID_HANDLE)
{
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
  if (bgfx::isValid(this->cachedVertexBuffer)) bgfx::destroy(this->cachedVertexBuffer);
  if (bgfx::isValid(this->cachedIndexBuffer)) bgfx::destroy(this->cachedIndexBuffer);
  if (bgfx::isValid(this->program)) bgfx::destroy(this->program);
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
  if (target.kind != SoWgpuRenderTargetP::KIND_OFFSCREEN) {
    this->lastError = "BGFX evaluation supports offscreen targets only";
    return BackendStatus::UNSUPPORTED;
  }
  bool expected = false;
  if (!bgfxInUse.compare_exchange_strong(expected, true)) {
    this->lastError = "BGFX evaluation permits one active target per process";
    return BackendStatus::UNSUPPORTED;
  }
  bgfx::Init init;
  init.type = bgfx::RendererType::Vulkan;
  init.swapChain.width = 0;
  init.swapChain.height = 0;
  if (!bgfx::init(init)) {
    bgfxInUse.store(false);
    this->lastError = "BGFX could not initialize its headless Vulkan renderer";
    return BackendStatus::NOT_READY;
  }
  this->initialized = true;
  this->apiThread = std::this_thread::get_id();
  const bgfx::Caps * caps = bgfx::getCaps();
  if (caps->rendererType != bgfx::RendererType::Vulkan ||
      !(caps->supported & BGFX_CAPS_INDEX32) ||
      !(caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA8,
                            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK)) {
    this->lastError = "BGFX Vulkan lacks RGBA8 framebuffer/readback or 32-bit indices";
    this->status = BackendStatus::UNSUPPORTED;
    return this->status;
  }
  this->layout.begin()
    .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
    .end();
  bgfx::ShaderHandle vs = bgfx::createShader(bgfx::copy(coin_bgfx_vs_spirv,
                                                      sizeof(coin_bgfx_vs_spirv)));
  bgfx::ShaderHandle fs = bgfx::createShader(bgfx::copy(coin_bgfx_fs_spirv,
                                                      sizeof(coin_bgfx_fs_spirv)));
  if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
    if (bgfx::isValid(vs)) bgfx::destroy(vs);
    if (bgfx::isValid(fs)) bgfx::destroy(fs);
    this->lastError = "BGFX failed to create its SPIR-V shaders";
    this->status = BackendStatus::BACKEND_ERROR;
    return this->status;
  }
  this->program = bgfx::createProgram(vs, fs, true);
  if (!bgfx::isValid(this->program)) {
    this->lastError = "BGFX failed to link its base-color shader program";
    this->status = BackendStatus::BACKEND_ERROR;
    return this->status;
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
    this->lastError = "Invalid BGFX offscreen dimensions";
    return false;
  }
  this->destroyFrameBuffers();
  this->frameBuffer = bgfx::createFrameBuffer(static_cast<uint16_t>(newWidth),
    static_cast<uint16_t>(newHeight), bgfx::TextureFormat::RGBA8);
  this->readbackTexture = bgfx::createTexture2D(static_cast<uint16_t>(newWidth),
    static_cast<uint16_t>(newHeight), false, 1, bgfx::TextureFormat::RGBA8,
    BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
  if (!bgfx::isValid(this->frameBuffer) || !bgfx::isValid(this->readbackTexture)) {
    this->lastError = "BGFX could not allocate offscreen/readback textures";
    this->destroyFrameBuffers();
    return false;
  }
  this->width = newWidth;
  this->height = newHeight;
  return true;
}

SubmitResult
SoWgpuBgfxBackend::submit(const FramePlan & frame, SoWgpuRenderTargetP & target)
{
  if (!this->initialized || this->status != BackendStatus::SUCCESS || !this->onApiThread()) {
    this->lastError = "BGFX submission requires a prepared backend on its API thread";
    return SubmitResult(BackendStatus::NOT_READY, this->lastError);
  }
  if (target.kind != SoWgpuRenderTargetP::KIND_OFFSCREEN ||
      target.directTextureOutput || target.depthReadbackEnabled) {
    this->lastError = "BGFX evaluation supports offscreen synchronous RGBA readback only; disable depth readback";
    return SubmitResult(BackendStatus::UNSUPPORTED, this->lastError);
  }
  typedef std::chrono::steady_clock Clock;
  const Clock::time_point begin = Clock::now();
  const bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;
  const bool cacheHit = frame.revision != 0 &&
    frame.revision == this->cachedRevision &&
    target.size[0] == this->cachedWidth &&
    target.size[1] == this->cachedHeight &&
    homogeneousDepth == this->cachedHomogeneousDepth;
  SoWgpuBgfxPlan freshPlan;
  const SoWgpuBgfxPlan * plan = &this->cachedPlan;
  if (!cacheHit) {
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
  if (cacheHit) {
    vb = this->cachedVertexBuffer;
    ib = this->cachedIndexBuffer;
  }
  bool retained = cacheHit;
  if (!cacheHit) {
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
  bgfx::setViewFrameBuffer(0, this->frameBuffer);
  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, plan->clearRgba, 1.0f);
  bgfx::setViewTransform(0, nullptr, nullptr);
  bgfx::touch(0);
  for (const SoWgpuBgfxDraw & draw : plan->draws) {
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    bgfx::setState(drawState(draw));
    bgfx::submit(0, this->program);
  }
  bgfx::TextureRegion destination;
  destination.handle = this->readbackTexture;
  bgfx::TextureRegion source;
  source.handle = bgfx::getTexture(this->frameBuffer);
  bgfx::blit(1, destination, source);
  const Clock::time_point encoded = Clock::now();
  const size_t bytes = static_cast<size_t>(this->width) * static_cast<size_t>(this->height) * 4;
  target.colorBuffer.resize(bytes);
  bgfx::TextureRegion readRegion;
  readRegion.handle = this->readbackTexture;
  const uint32_t readyFrame = bgfx::read(readRegion, target.colorBuffer.data());
  const Clock::time_point readRequested = Clock::now();
  uint32_t completedFrame = bgfx::frame();
  const Clock::time_point submitted = Clock::now();
  // BGFX readback is delayed by multiple frames even for a synchronous API.
  for (int attempts = 0; completedFrame < readyFrame && attempts < 16; ++attempts)
    completedFrame = bgfx::frame();
  if (completedFrame < readyFrame) {
    if (!retained) {
      if (bgfx::isValid(vb)) bgfx::destroy(vb);
      if (bgfx::isValid(ib)) bgfx::destroy(ib);
    }
    this->lastError = "BGFX readback did not complete within sixteen frames";
    return SubmitResult(BackendStatus::BACKEND_ERROR, this->lastError);
  }
  const Clock::time_point readComplete = Clock::now();
  if (!retained) {
    if (bgfx::isValid(vb)) bgfx::destroy(vb);
    if (bgfx::isValid(ib)) bgfx::destroy(ib);
  }
  target.depthBuffer.clear();
  if (SoWgpuDiagnosticShell::phaseTracingEnabled()) {
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    SoWgpuBgfxPhaseSample sample;
    sample.lowerMs = ms(begin, lowered);
    sample.uploadMs = ms(lowered, uploaded);
    sample.encodeMs = ms(uploaded, encoded);
    sample.readRequestMs = ms(encoded, readRequested);
    sample.submitFrameMs = ms(readRequested, submitted);
    sample.readWaitMs = ms(submitted, readComplete);
    sample.vertices = plan->vertices.size();
    sample.draws = plan->draws.size();
    sample.resourceCacheHit = cacheHit;
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
