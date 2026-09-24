#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/SoWgpuRustBackend.h"
#include "rendering/wgpu/SoWgpuFfiFrame.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include "rendering/wgpu/coin_wgpu_ffi.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>

// Static assertions ensuring ABI compatibility with Rust bridge
static_assert(sizeof(CoinWgpuVertex) == 36, "CoinWgpuVertex size mismatch");
static_assert(alignof(CoinWgpuVertex) == 4, "CoinWgpuVertex alignment mismatch");
static_assert(offsetof(CoinWgpuVertex, position) == 0, "CoinWgpuVertex position offset mismatch");
static_assert(offsetof(CoinWgpuVertex, normal) == 12, "CoinWgpuVertex normal offset mismatch");
static_assert(offsetof(CoinWgpuVertex, texcoord) == 24, "CoinWgpuVertex texcoord offset mismatch");
static_assert(offsetof(CoinWgpuVertex, material_slot) == 32, "CoinWgpuVertex material_slot offset mismatch");

static_assert(sizeof(CoinWgpuDraw) == 48, "CoinWgpuDraw size mismatch");
static_assert(alignof(CoinWgpuDraw) == 8, "CoinWgpuDraw alignment mismatch");
static_assert(offsetof(CoinWgpuDraw, stable_node_id) == 24, "CoinWgpuDraw stable_node_id offset mismatch");
static_assert(offsetof(CoinWgpuDraw, draw_ordinal) == 32, "CoinWgpuDraw draw_ordinal offset mismatch");
static_assert(offsetof(CoinWgpuDraw, reserved) == 36, "CoinWgpuDraw reserved offset mismatch");
static_assert(offsetof(CoinWgpuDraw, source_revision) == 40, "CoinWgpuDraw source_revision offset mismatch");

static_assert(sizeof(CoinWgpuCacheStats) == 88, "CoinWgpuCacheStats size mismatch");
static_assert(alignof(CoinWgpuCacheStats) == 8, "CoinWgpuCacheStats alignment mismatch");

static_assert(sizeof(CoinWgpuMaterial) == 72, "CoinWgpuMaterial size mismatch");
static_assert(alignof(CoinWgpuMaterial) == 4, "CoinWgpuMaterial alignment mismatch");

static_assert(sizeof(CoinWgpuTexture) == 40, "CoinWgpuTexture size mismatch");
static_assert(alignof(CoinWgpuTexture) == 8, "CoinWgpuTexture alignment mismatch");
static_assert(sizeof(CoinWgpuSampler) == 16, "CoinWgpuSampler size mismatch");
static_assert(alignof(CoinWgpuSampler) == 4, "CoinWgpuSampler alignment mismatch");

static_assert(sizeof(CoinWgpuRenderState) == 884, "CoinWgpuRenderState size mismatch");
static_assert(alignof(CoinWgpuRenderState) == 4, "CoinWgpuRenderState alignment mismatch");
static_assert(offsetof(CoinWgpuRenderState, cull_mode) == 236, "CoinWgpuRenderState cull_mode offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, front_face) == 240, "CoinWgpuRenderState front_face offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, light_model) == 244, "CoinWgpuRenderState light_model offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, texture_matrix) == 248, "CoinWgpuRenderState texture_matrix offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, has_texture) == 312, "CoinWgpuRenderState has_texture offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, texture_slot) == 316, "CoinWgpuRenderState texture_slot offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, sampler_slot) == 320, "CoinWgpuRenderState sampler_slot offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, texture_model) == 324, "CoinWgpuRenderState texture_model offset mismatch");
static_assert(sizeof(CoinWgpuLight) == 64, "CoinWgpuLight size mismatch");
static_assert(offsetof(CoinWgpuRenderState, light_count) == 328, "CoinWgpuRenderState light_count offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, ambient_light) == 332, "CoinWgpuRenderState ambient_light offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, lights) == 348, "CoinWgpuRenderState lights offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, fog_mode) == 860, "CoinWgpuRenderState fog_mode offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, fog_color) == 864, "CoinWgpuRenderState fog_color offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, fog_start) == 876, "CoinWgpuRenderState fog_start offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, fog_end) == 880, "CoinWgpuRenderState fog_end offset mismatch");

static_assert(sizeof(CoinWgpuTarget) == 56, "CoinWgpuTarget size mismatch");
static_assert(sizeof(CoinWgpuFrameView) == 152, "CoinWgpuFrameView size mismatch");
static_assert(sizeof(CoinWgpuNativeSurfaceDescriptor) == 32, "CoinWgpuNativeSurfaceDescriptor size mismatch");
static_assert(sizeof(CoinWgpuSurfaceCreateInfo) == 48, "CoinWgpuSurfaceCreateInfo size mismatch");

SoWgpuRustBackend::SoWgpuRustBackend()
  : status(BackendStatus::SUCCESS),
    lastError(""),
    ffiFrame(new SoWgpuFfiFrame)
{
}

BackendStatus
SoWgpuRustBackend::getStatus() const
{
  return this->status;
}

SoWgpuRustBackend::~SoWgpuRustBackend()
{
}

bool
SoWgpuRustBackend::isAvailable()
{
  return coin_wgpu_is_available() != 0;
}

std::string
SoWgpuRustBackend::getAdapterInfo()
{
  char buf[256] = {0};
  coin_wgpu_get_adapter_info(buf, sizeof(buf));
  return std::string(buf);
}

BackendStatus
SoWgpuRustBackend::prepare(SoWgpuRenderTargetP & target)
{
  if (target.kind == SoWgpuRenderTargetP::KIND_WINDOW) {
    if (target.surfaceId == 0) {
      CoinWgpuSurfaceCreateInfo info{};
      info.abi_version = COIN_WGPU_ABI_VERSION;
      info.struct_size = sizeof(CoinWgpuSurfaceCreateInfo);
      info.native.abi_version = COIN_WGPU_ABI_VERSION;
      info.native.struct_size = sizeof(CoinWgpuNativeSurfaceDescriptor);
      info.native.type = target.nativeDesc.type;
      info.native.reserved = 0;

      if (target.nativeDesc.type == COIN_WGPU_SURFACE_XLIB) {
        info.native.handle_a = reinterpret_cast<uintptr_t>(target.nativeDesc.native.xlib.display);
        info.native.handle_b = target.nativeDesc.native.xlib.window;
      } else {
        this->lastError = "Native surface platform not supported in Onda 1B";
        return BackendStatus::UNSUPPORTED;
      }

      info.width = target.size[0] > 0 ? static_cast<uint32_t>(target.size[0]) : 0;
      info.height = target.size[1] > 0 ? static_cast<uint32_t>(target.size[1]) : 0;

      CoinWgpuSurfaceId sId = COIN_WGPU_INVALID_SURFACE_ID;
      char errBuf[512] = {0};
      CoinWgpuStatus st = coin_wgpu_surface_create(&info, &sId, errBuf, sizeof(errBuf));
      if (st != COIN_WGPU_OK) {
        this->lastError = errBuf[0] ? errBuf : "Failed to create native window surface";
        if (st == COIN_WGPU_UNSUPPORTED) return BackendStatus::UNSUPPORTED;
        if (st == COIN_WGPU_NOT_READY) return BackendStatus::NOT_READY;
        if (st == COIN_WGPU_OUT_OF_MEMORY) return BackendStatus::OUT_OF_MEMORY;
        if (st == COIN_WGPU_DEVICE_LOST) return BackendStatus::DEVICE_LOST;
        return BackendStatus::BACKEND_ERROR;
      }
      target.surfaceId = sId;
    } else if (target.needsReconfigure) {
      char errBuf[512] = {0};
      uint32_t w = target.size[0] > 0 ? static_cast<uint32_t>(target.size[0]) : 0;
      uint32_t h = target.size[1] > 0 ? static_cast<uint32_t>(target.size[1]) : 0;
      CoinWgpuStatus st = coin_wgpu_surface_resize(target.surfaceId, w, h, errBuf, sizeof(errBuf));
      if (st != COIN_WGPU_OK) {
        this->lastError = errBuf[0] ? errBuf : "Failed to resize native window surface";
        return BackendStatus::BACKEND_ERROR;
      }
      target.needsReconfigure = false;
    }
    return BackendStatus::SUCCESS;
  }

  // Offscreen target
  if (!this->isAvailable()) {
    this->lastError = "No compatible WebGPU adapter or device available";
    return BackendStatus::NOT_READY;
  }
  if (target.size[0] <= 0 || target.size[1] <= 0) {
    this->lastError = "Target size must be positive";
    return BackendStatus::NOT_READY;
  }
  return BackendStatus::SUCCESS;
}

SubmitResult
SoWgpuRustBackend::submit(const FramePlan & frame, SoWgpuRenderTargetP & target)
{
  return this->submitInternal(frame, target, NULL);
}

SubmitResult
SoWgpuRustBackend::submitAsync(const FramePlan & frame, SoWgpuRenderTargetP & target,
                               SoWgpuReadbackTicket & outTicket)
{
  outTicket = SoWgpuReadbackTicket{};
  return this->submitInternal(frame, target, &outTicket);
}

SubmitResult
SoWgpuRustBackend::submitInternal(const FramePlan & frame, SoWgpuRenderTargetP & target,
                                  SoWgpuReadbackTicket * outTicket)
{
  typedef std::chrono::steady_clock ProfileClock;
  const ProfileClock::time_point profileBegin = ProfileClock::now();
  const bool tracePhases = std::getenv("COIN_WGPU_TRACE_PHASES") != NULL;
  if (outTicket && target.kind != SoWgpuRenderTargetP::KIND_OFFSCREEN) {
    this->lastError = "Asynchronous readback requires an offscreen target";
    return SubmitResult(BackendStatus::UNSUPPORTED, this->lastError);
  }
  if (target.kind == SoWgpuRenderTargetP::KIND_OFFSCREEN) {
    if ((!target.directTextureOutput && target.colorBuffer.empty()) ||
        target.size[0] <= 0 || target.size[1] <= 0) {
      this->lastError = "Render target buffer is not allocated";
      return BackendStatus::NOT_READY;
    }
  } else if (target.kind == SoWgpuRenderTargetP::KIND_WINDOW) {
    if (target.surfaceId == 0 || target.suspended || target.size[0] <= 0 || target.size[1] <= 0) {
      this->lastError = "Window surface target is not ready or suspended";
      return BackendStatus::NOT_READY;
    }
  }

  std::string packDiagnostic;
  if (!this->ffiFrame->prepare(frame,
                               static_cast<uint32_t>(target.size[0]),
                               static_cast<uint32_t>(target.size[1]),
                               packDiagnostic)) {
    this->lastError = packDiagnostic;
    this->status = BackendStatus::UNSUPPORTED;
    return SubmitResult(BackendStatus::UNSUPPORTED, this->lastError);
  }
  const CoinWgpuFrameView & fView = this->ffiFrame->getView();
  const bool packCacheHit = this->ffiFrame->reusedLastPrepare();
  const ProfileClock::time_point profilePacked = ProfileClock::now();

  char errBuf[512] = {0};
  CoinWgpuStatus st = COIN_WGPU_OK;
  uint64_t serial = 0;

  if (target.kind == SoWgpuRenderTargetP::KIND_WINDOW) {
    st = coin_wgpu_surface_submit(target.surfaceId, &fView, errBuf, sizeof(errBuf));
  } else {
    // 6. Build target pod for offscreen
    CoinWgpuTarget tPod{};
    tPod.width = static_cast<uint32_t>(target.size[0]);
    tPod.height = static_cast<uint32_t>(target.size[1]);
    tPod.color_buffer = target.colorBuffer.data();
    tPod.color_buffer_len = static_cast<uint64_t>(target.colorBuffer.size());
    tPod.depth_buffer = target.depthBuffer.data();
    tPod.depth_buffer_len = static_cast<uint64_t>(target.depthBuffer.size());
    tPod.submission_serial = 0;

    tPod.device_id = 0;
    // 7. Submit to WebGPU via FFI
    target.directTextureToken = 0;
    if (target.directTextureOutput) {
      if (outTicket) return SubmitResult(BackendStatus::UNSUPPORTED, "GPU-only RTT cannot request a readback ticket");
      st = coin_wgpu_submit_texture(&tPod, &fView, &target.directTextureToken, errBuf, sizeof(errBuf));
    } else if (outTicket) {
      CoinWgpuReadbackTicket bridgeTicket{};
      bridgeTicket.abi_version = COIN_WGPU_ABI_VERSION;
      bridgeTicket.struct_size = sizeof(bridgeTicket);
      st = coin_wgpu_submit_async(&tPod, &fView, &bridgeTicket, errBuf, sizeof(errBuf));
      if (st == COIN_WGPU_OK) {
        outTicket->token = bridgeTicket.token;
        outTicket->generation = bridgeTicket.generation;
        outTicket->submissionSerial = bridgeTicket.submission_serial;
        outTicket->width = bridgeTicket.width;
        outTicket->height = bridgeTicket.height;
        outTicket->colorFormat = bridgeTicket.color_format;
        outTicket->depthFormat = bridgeTicket.depth_format;
        outTicket->colorRowPitch = bridgeTicket.color_row_pitch;
        outTicket->depthRowPitch = bridgeTicket.depth_row_pitch;
        outTicket->colorBytes = bridgeTicket.color_bytes;
        outTicket->depthBytes = bridgeTicket.depth_bytes;
      }
    } else {
      st = coin_wgpu_submit(&tPod, &fView, errBuf, sizeof(errBuf));
    }
    const ProfileClock::time_point profileBridgeDone = ProfileClock::now();
    serial = tPod.submission_serial;
    if (tracePhases) {
      std::cerr << "COIN_WGPU_PHASE bridge pack_ms="
                << std::chrono::duration<double, std::milli>(profilePacked - profileBegin).count()
                << " pack_cache_hit=" << (packCacheHit ? 1 : 0)
                << " ffi_ms="
                << std::chrono::duration<double, std::milli>(profileBridgeDone - profilePacked).count()
                << '\n';
    }
  }

  if (st != COIN_WGPU_OK) {
    this->lastError = errBuf[0] ? errBuf : "WebGPU bridge execution failed";
    switch (st) {
      case COIN_WGPU_NOT_READY:
        return SubmitResult(BackendStatus::NOT_READY, this->lastError, serial);
      case COIN_WGPU_UNSUPPORTED:
        return SubmitResult(BackendStatus::UNSUPPORTED, this->lastError, serial);
      case COIN_WGPU_OUT_OF_MEMORY:
        return SubmitResult(BackendStatus::OUT_OF_MEMORY, this->lastError, serial);
      case COIN_WGPU_DEVICE_LOST:
        return SubmitResult(BackendStatus::DEVICE_LOST, this->lastError, serial);
      case COIN_WGPU_SURFACE_LOST:
        return SubmitResult(BackendStatus::SURFACE_LOST, this->lastError, serial);
      case COIN_WGPU_INVALID_ARGUMENT:
      case COIN_WGPU_BACKEND_ERROR:
      default:
        return SubmitResult(BackendStatus::BACKEND_ERROR, this->lastError, serial);
    }
  }

  this->lastError.clear();
  return SubmitResult(BackendStatus::SUCCESS, "", serial);
}

void
SoWgpuRustBackend::poll()
{
}

const std::string &
SoWgpuRustBackend::getLastError() const
{
  return this->lastError;
}

#else
// Stub implementation when Rust bridge is disabled
#include "rendering/wgpu/SoWgpuRustBackend.h"

SoWgpuRustBackend::SoWgpuRustBackend() : status(BackendStatus::UNSUPPORTED), lastError("Rust bridge not compiled in") {}
SoWgpuRustBackend::~SoWgpuRustBackend() {}
BackendStatus SoWgpuRustBackend::getStatus() const { return status; }
BackendStatus SoWgpuRustBackend::prepare(SoWgpuRenderTargetP &) { return BackendStatus::UNSUPPORTED; }
SubmitResult SoWgpuRustBackend::submit(const FramePlan &, SoWgpuRenderTargetP &) { return SubmitResult(BackendStatus::UNSUPPORTED, "Rust bridge not compiled in"); }
SubmitResult SoWgpuRustBackend::submitAsync(const FramePlan &, SoWgpuRenderTargetP &, SoWgpuReadbackTicket &) { return SubmitResult(BackendStatus::UNSUPPORTED, "Rust bridge not compiled in"); }
void SoWgpuRustBackend::poll() {}
const std::string & SoWgpuRustBackend::getLastError() const { return lastError; }
bool SoWgpuRustBackend::isAvailable() { return false; }
std::string SoWgpuRustBackend::getAdapterInfo() { return "None"; }
#endif
