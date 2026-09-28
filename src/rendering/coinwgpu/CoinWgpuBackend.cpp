#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuBackend.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"

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

static_assert(sizeof(CoinWgpuDraw) == 56, "CoinWgpuDraw size mismatch");
static_assert(alignof(CoinWgpuDraw) == 8, "CoinWgpuDraw alignment mismatch");
static_assert(offsetof(CoinWgpuDraw, stable_node_id) == 24, "CoinWgpuDraw stable_node_id offset mismatch");
static_assert(offsetof(CoinWgpuDraw, draw_ordinal) == 32, "CoinWgpuDraw draw_ordinal offset mismatch");
static_assert(offsetof(CoinWgpuDraw, reserved) == 36, "CoinWgpuDraw reserved offset mismatch");
static_assert(offsetof(CoinWgpuDraw, source_revision) == 40, "CoinWgpuDraw source_revision offset mismatch");

static_assert(offsetof(CoinWgpuDraw, render_layer) == 48, "CoinWgpuDraw layer offset mismatch");
static_assert(offsetof(CoinWgpuDraw, clear_depth_before) == 52, "CoinWgpuDraw depth clear offset mismatch");

static_assert(sizeof(CoinWgpuCacheStats) == 88, "CoinWgpuCacheStats size mismatch");
static_assert(alignof(CoinWgpuCacheStats) == 8, "CoinWgpuCacheStats alignment mismatch");

static_assert(sizeof(CoinWgpuMaterial) == 72, "CoinWgpuMaterial size mismatch");
static_assert(alignof(CoinWgpuMaterial) == 4, "CoinWgpuMaterial alignment mismatch");

static_assert(sizeof(CoinWgpuTexture) == 40, "CoinWgpuTexture size mismatch");
static_assert(alignof(CoinWgpuTexture) == 8, "CoinWgpuTexture alignment mismatch");
static_assert(sizeof(CoinWgpuSampler) == 16, "CoinWgpuSampler size mismatch");
static_assert(alignof(CoinWgpuSampler) == 4, "CoinWgpuSampler alignment mismatch");

static_assert(sizeof(CoinWgpuRenderState) == 956, "CoinWgpuRenderState size mismatch");
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
static_assert(offsetof(CoinWgpuRenderState, texture_blend_color) == 328, "CoinWgpuRenderState texture_blend_color offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, viewport) == 344, "CoinWgpuRenderState viewport offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, light_count) == 360, "CoinWgpuRenderState light_count offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, ambient_light) == 364, "CoinWgpuRenderState ambient_light offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, lights) == 380, "CoinWgpuRenderState lights offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, fog_mode) == 892, "CoinWgpuRenderState fog_mode offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, fog_color) == 896, "CoinWgpuRenderState fog_color offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, fog_start) == 908, "CoinWgpuRenderState fog_start offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, fog_end) == 912, "CoinWgpuRenderState fog_end offset mismatch");

static_assert(sizeof(CoinWgpuTarget) == 56, "CoinWgpuTarget size mismatch");
static_assert(sizeof(CoinWgpuFrameView) == 160, "CoinWgpuFrameView size mismatch");
static_assert(sizeof(CoinWgpuNativeSurfaceDescriptor) == 32, "CoinWgpuNativeSurfaceDescriptor size mismatch");
static_assert(sizeof(CoinWgpuSurfaceCreateInfo) == 48, "CoinWgpuSurfaceCreateInfo size mismatch");

CoinWgpuBackend::CoinWgpuBackend()
  : status(CoinRenderBackendStatus::SUCCESS),
    lastError(""),
    ffiFrame(new CoinWgpuFfiFrame)
{
}

CoinRenderBackendStatus
CoinWgpuBackend::getStatus() const
{
  return this->status;
}

CoinWgpuBackend::~CoinWgpuBackend()
{
}

bool
CoinWgpuBackend::isAvailable()
{
  return coin_wgpu_is_available() != 0;
}

std::string
CoinWgpuBackend::getAdapterInfo()
{
  char buf[256] = {0};
  coin_wgpu_get_adapter_info(buf, sizeof(buf));
  return std::string(buf);
}

CoinRenderBackendStatus
CoinWgpuBackend::prepare(CoinRenderTargetP & target)
{
  if (target.kind == CoinRenderTargetP::KIND_WINDOW) {
    if (target.surfaceId == 0) {
      CoinWgpuSurfaceCreateInfo info{};
      info.abi_version = COIN_WGPU_ABI_VERSION;
      info.struct_size = sizeof(CoinWgpuSurfaceCreateInfo);
      info.native.abi_version = COIN_WGPU_ABI_VERSION;
      info.native.struct_size = sizeof(CoinWgpuNativeSurfaceDescriptor);
      info.native.type = target.nativeDesc.type;
      info.native.reserved = 0;

      if (target.nativeDesc.type == COIN_RENDER_SURFACE_XLIB) {
        info.native.handle_a = reinterpret_cast<uintptr_t>(target.nativeDesc.native.xlib.display);
        info.native.handle_b = target.nativeDesc.native.xlib.window;
      } else {
        this->lastError = "Native surface platform not supported in Onda 1B";
        return CoinRenderBackendStatus::UNSUPPORTED;
      }

      info.width = target.size[0] > 0 ? static_cast<uint32_t>(target.size[0]) : 0;
      info.height = target.size[1] > 0 ? static_cast<uint32_t>(target.size[1]) : 0;

      CoinWgpuSurfaceId sId = COIN_WGPU_INVALID_SURFACE_ID;
      char errBuf[512] = {0};
      CoinWgpuStatus st = coin_wgpu_surface_create(&info, &sId, errBuf, sizeof(errBuf));
      if (st != COIN_WGPU_OK) {
        this->lastError = errBuf[0] ? errBuf : "Failed to create native window surface";
        if (st == COIN_WGPU_UNSUPPORTED) return CoinRenderBackendStatus::UNSUPPORTED;
        if (st == COIN_WGPU_NOT_READY) return CoinRenderBackendStatus::NOT_READY;
        if (st == COIN_WGPU_OUT_OF_MEMORY) return CoinRenderBackendStatus::OUT_OF_MEMORY;
        if (st == COIN_WGPU_DEVICE_LOST) return CoinRenderBackendStatus::DEVICE_LOST;
        return CoinRenderBackendStatus::BACKEND_ERROR;
      }
      target.surfaceId = sId;
    } else if (target.needsReconfigure) {
      char errBuf[512] = {0};
      uint32_t w = target.size[0] > 0 ? static_cast<uint32_t>(target.size[0]) : 0;
      uint32_t h = target.size[1] > 0 ? static_cast<uint32_t>(target.size[1]) : 0;
      CoinWgpuStatus st = coin_wgpu_surface_resize(target.surfaceId, w, h, errBuf, sizeof(errBuf));
      if (st != COIN_WGPU_OK) {
        this->lastError = errBuf[0] ? errBuf : "Failed to resize native window surface";
        return CoinRenderBackendStatus::BACKEND_ERROR;
      }
      target.needsReconfigure = false;
    }
    return CoinRenderBackendStatus::SUCCESS;
  }

  // Offscreen target
  if (!this->isAvailable()) {
    this->lastError = "No compatible WebGPU adapter or device available";
    return CoinRenderBackendStatus::NOT_READY;
  }
  if (target.size[0] <= 0 || target.size[1] <= 0) {
    this->lastError = "Target size must be positive";
    return CoinRenderBackendStatus::NOT_READY;
  }
  return CoinRenderBackendStatus::SUCCESS;
}

CoinRenderSubmitResult
CoinWgpuBackend::submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target)
{
  return this->submitInternal(frame, target, NULL,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0));
}

CoinRenderSubmitResult
CoinWgpuBackend::submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                          const CoinRenderFrameReuseDecision & reuse)
{
  return this->submitInternal(frame, target, NULL, reuse);
}

CoinRenderSubmitResult
CoinWgpuBackend::submitAsync(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                               CoinRenderReadbackTicket & outTicket)
{
  outTicket = CoinRenderReadbackTicket{};
  return this->submitInternal(frame, target, &outTicket,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::FULL_REBUILD, 0));
}

CoinRenderSubmitResult
CoinWgpuBackend::submitAsync(const CoinRenderFramePlan & frame,
                               CoinRenderTargetP & target,
                               CoinRenderReadbackTicket & outTicket,
                               const CoinRenderFrameReuseDecision & reuse)
{
  outTicket = CoinRenderReadbackTicket{};
  return this->submitInternal(frame, target, &outTicket, reuse);
}

CoinRenderSubmitResult
CoinWgpuBackend::submitInternal(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                                  CoinRenderReadbackTicket * outTicket,
                                  const CoinRenderFrameReuseDecision & reuse)
{
  typedef std::chrono::steady_clock ProfileClock;
  const ProfileClock::time_point profileBegin = ProfileClock::now();
  const bool tracePhases = CoinRenderDiagnosticShell::phaseTracingEnabled();
  if (outTicket && target.kind != CoinRenderTargetP::KIND_OFFSCREEN) {
    this->lastError = "Asynchronous readback requires an offscreen target";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  if (target.kind == CoinRenderTargetP::KIND_OFFSCREEN) {
    if ((!target.directTextureOutput && target.colorBuffer.empty()) ||
        target.size[0] <= 0 || target.size[1] <= 0) {
      this->lastError = "Render target buffer is not allocated";
      return CoinRenderBackendStatus::NOT_READY;
    }
  } else if (target.kind == CoinRenderTargetP::KIND_WINDOW) {
    if (target.surfaceId == 0 || target.suspended || target.size[0] <= 0 || target.size[1] <= 0) {
      this->lastError = "Window surface target is not ready or suspended";
      return CoinRenderBackendStatus::NOT_READY;
    }
  }

  std::string packDiagnostic;
  if (!this->ffiFrame->prepare(frame,
                               static_cast<uint32_t>(target.size[0]),
                               static_cast<uint32_t>(target.size[1]),
                               reuse,
                               packDiagnostic)) {
    this->lastError = packDiagnostic;
    this->status = CoinRenderBackendStatus::UNSUPPORTED;
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  const CoinWgpuFrameView & fView = this->ffiFrame->getView();
  const bool packCacheHit = this->ffiFrame->reusedLastPrepare();
  const ProfileClock::time_point profilePacked = ProfileClock::now();

  char errBuf[512] = {0};
  CoinWgpuStatus st = COIN_WGPU_OK;
  uint64_t serial = 0;

  if (target.kind == CoinRenderTargetP::KIND_WINDOW) {
    st = coin_wgpu_surface_submit(target.surfaceId, &fView, errBuf, sizeof(errBuf));
  } else {
    // 6. Build target pod for offscreen
    CoinWgpuTarget tPod{};
    tPod.width = static_cast<uint32_t>(target.size[0]);
    tPod.height = static_cast<uint32_t>(target.size[1]);
    tPod.color_buffer = target.colorBuffer.data();
    tPod.color_buffer_len = static_cast<uint64_t>(target.colorBuffer.size());
    tPod.depth_buffer = target.depthReadbackEnabled ? target.depthBuffer.data() : NULL;
    tPod.depth_buffer_len = target.depthReadbackEnabled
      ? static_cast<uint64_t>(target.depthBuffer.size()) : 0;
    tPod.submission_serial = 0;

    tPod.device_id = 0;
    // 7. Submit to WebGPU via FFI
    target.directTextureToken = 0;
    if (target.directTextureOutput) {
      if (outTicket) return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, "GPU-only RTT cannot request a readback ticket");
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
      CoinWgpuBridgePhaseSample sample;
      sample.packMs = std::chrono::duration<double, std::milli>(
        profilePacked - profileBegin).count();
      sample.ffiMs = std::chrono::duration<double, std::milli>(
        profileBridgeDone - profilePacked).count();
      sample.packCacheHit = packCacheHit;
      sample.packKind = this->ffiFrame->lastPrepareKind();
      std::cerr << CoinRenderDiagnosticShell::formatBridgePhase(sample) << '\n';
    }
  }

  if (st != COIN_WGPU_OK) {
    this->lastError = errBuf[0] ? errBuf : "WebGPU bridge execution failed";
    switch (st) {
      case COIN_WGPU_NOT_READY:
        return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY, this->lastError, serial);
      case COIN_WGPU_UNSUPPORTED:
        return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError, serial);
      case COIN_WGPU_OUT_OF_MEMORY:
        return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError, serial);
      case COIN_WGPU_DEVICE_LOST:
        return CoinRenderSubmitResult(CoinRenderBackendStatus::DEVICE_LOST, this->lastError, serial);
      case COIN_WGPU_SURFACE_LOST:
        return CoinRenderSubmitResult(CoinRenderBackendStatus::SURFACE_LOST, this->lastError, serial);
      case COIN_WGPU_INVALID_ARGUMENT:
      case COIN_WGPU_BACKEND_ERROR:
      default:
        return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError, serial);
    }
  }

  this->lastError.clear();
  return CoinRenderSubmitResult(CoinRenderBackendStatus::SUCCESS, "", serial);
}

void
CoinWgpuBackend::poll()
{
}

const std::string &
CoinWgpuBackend::getLastError() const
{
  return this->lastError;
}

#else
// Stub implementation when Rust bridge is disabled
#include "rendering/coinwgpu/CoinWgpuBackend.h"

CoinWgpuBackend::CoinWgpuBackend() : status(CoinRenderBackendStatus::UNSUPPORTED), lastError("Rust bridge not compiled in") {}
CoinWgpuBackend::~CoinWgpuBackend() {}
CoinRenderBackendStatus CoinWgpuBackend::getStatus() const { return status; }
CoinRenderBackendStatus CoinWgpuBackend::prepare(CoinRenderTargetP &) { return CoinRenderBackendStatus::UNSUPPORTED; }
CoinRenderSubmitResult CoinWgpuBackend::submit(const CoinRenderFramePlan &, CoinRenderTargetP &) { return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, "Rust bridge not compiled in"); }
CoinRenderSubmitResult CoinWgpuBackend::submit(const CoinRenderFramePlan &, CoinRenderTargetP &, const CoinRenderFrameReuseDecision &) { return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, "Rust bridge not compiled in"); }
CoinRenderSubmitResult CoinWgpuBackend::submitAsync(const CoinRenderFramePlan &, CoinRenderTargetP &, CoinRenderReadbackTicket &) { return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, "Rust bridge not compiled in"); }
CoinRenderSubmitResult CoinWgpuBackend::submitAsync(const CoinRenderFramePlan &, CoinRenderTargetP &, CoinRenderReadbackTicket &, const CoinRenderFrameReuseDecision &) { return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, "Rust bridge not compiled in"); }
void CoinWgpuBackend::poll() {}
const std::string & CoinWgpuBackend::getLastError() const { return lastError; }
bool CoinWgpuBackend::isAvailable() { return false; }
std::string CoinWgpuBackend::getAdapterInfo() { return "None"; }
#endif
