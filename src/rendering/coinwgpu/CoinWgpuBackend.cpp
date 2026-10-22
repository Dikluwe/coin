#include <set>
#include "rendering/coinrender/CoinRenderBumpMapCore.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuBackend.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include "rendering/coinwgpu/CoinWgpuPortableCore.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include "rendering/coinrender/CoinRenderMultipassCore.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>

// Static assertions ensuring ABI compatibility with Rust bridge
static_assert(sizeof(CoinWgpuVertex) == 164, "CoinWgpuVertex size mismatch");
static_assert(alignof(CoinWgpuVertex) == 4, "CoinWgpuVertex alignment mismatch");
static_assert(offsetof(CoinWgpuVertex, position) == 0, "CoinWgpuVertex position offset mismatch");
static_assert(offsetof(CoinWgpuVertex, normal) == 12, "CoinWgpuVertex normal offset mismatch");
static_assert(offsetof(CoinWgpuVertex, texcoord) == 24, "CoinWgpuVertex texcoord offset mismatch");
static_assert(offsetof(CoinWgpuVertex, material_slot) == 40, "CoinWgpuVertex material_slot offset mismatch");

static_assert(sizeof(CoinWgpuDraw) == 56, "CoinWgpuDraw size mismatch");
static_assert(alignof(CoinWgpuDraw) == 8, "CoinWgpuDraw alignment mismatch");
static_assert(offsetof(CoinWgpuDraw, stable_node_id) == 24, "CoinWgpuDraw stable_node_id offset mismatch");
static_assert(offsetof(CoinWgpuDraw, draw_ordinal) == 32, "CoinWgpuDraw draw_ordinal offset mismatch");
static_assert(offsetof(CoinWgpuDraw, composition_flags) == 36, "CoinWgpuDraw composition flags offset mismatch");
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

static_assert(sizeof(CoinWgpuRenderState) == 2436, "CoinWgpuRenderState size mismatch");
static_assert(offsetof(CoinWgpuRenderState, raster_texture_source_slot) == 2300, "Raster source ABI tail mismatch");
static_assert(offsetof(CoinWgpuRenderState, raster_texture_coordinates) == 2304, "Raster coordinate ABI tail mismatch");
static_assert(offsetof(CoinWgpuRenderState, alpha_test_function) == 2280, "Alpha function ABI tail mismatch");
static_assert(offsetof(CoinWgpuRenderState, alpha_test_reference) == 2284, "Alpha reference ABI tail mismatch");
static_assert(offsetof(CoinWgpuRenderState, texture_projection) == 2288, "Texture projection ABI tail changed");
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

static_assert(sizeof(CoinWgpuTarget) == 64, "CoinWgpuTarget size mismatch");
static_assert(sizeof(CoinWgpuFrameView) == 544, "CoinWgpuFrameView size mismatch");
static_assert(sizeof(CoinWgpuInstance) == 144, "CoinWgpuInstance size mismatch");
static_assert(sizeof(CoinWgpuInstanceRange) == 16, "CoinWgpuInstanceRange size mismatch");
static_assert(offsetof(CoinWgpuFrameView, instances) == 416, "CoinWgpuFrameView instance offset mismatch");
static_assert(offsetof(CoinWgpuFrameView, instance_ranges) == 432, "CoinWgpuFrameView instance range offset mismatch");
static_assert(sizeof(CoinWgpuShadowDraw) == 160, "CoinWgpuShadowDraw size mismatch");
static_assert(sizeof(CoinWgpuShadowReceiver) == 144, "CoinWgpuShadowReceiver size mismatch");
static_assert(sizeof(CoinWgpuNativeSurfaceDescriptor) == 32, "CoinWgpuNativeSurfaceDescriptor size mismatch");
static_assert(sizeof(CoinWgpuSurfaceCreateInfo) == 56, "CoinWgpuSurfaceCreateInfo size mismatch");

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
  if (coin_wgpu_install_cpp_portable_core() != COIN_WGPU_OK) {
    this->lastError = "Portable shader Core callback registration failed";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  this->sampleCount = target.options.sampleCount;
  if (target.kind != CoinRenderTargetP::KIND_WINDOW &&
      target.options.renderer != COIN_RENDER_RENDERER_UNKNOWN) {
    CoinWgpuRuntimeCapabilities caps{};
    const auto status = coin_wgpu_query_runtime_capabilities(&caps, sizeof(caps));
    if (status != COIN_WGPU_OK || caps.renderer != uint32_t(target.options.renderer)) {
      this->lastError =
          "Requested renderer does not match the active wgpu adapter; no fallback was applied";
      return CoinRenderBackendStatus::UNSUPPORTED;
    }
  }
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
#if defined(__linux__)
      } else if (target.nativeDesc.type == COIN_RENDER_SURFACE_WAYLAND) {
        info.native.handle_a = reinterpret_cast<uintptr_t>(target.nativeDesc.native.wayland.display);
        info.native.handle_b = reinterpret_cast<uintptr_t>(target.nativeDesc.native.wayland.surface);
#endif
#if defined(__APPLE__)
      } else if (target.nativeDesc.type == COIN_RENDER_SURFACE_APPKIT_LAYER) {
        info.native.handle_a = reinterpret_cast<uintptr_t>(target.nativeDesc.native.appkit.metalLayer);
#endif
#if defined(__ANDROID__)
      } else if (target.nativeDesc.type == COIN_RENDER_SURFACE_ANDROID_NDK) {
        info.native.handle_a = reinterpret_cast<uintptr_t>(target.nativeDesc.native.android.nativeWindow);
#endif
#if defined(_WIN32)
      } else if (target.nativeDesc.type == COIN_RENDER_SURFACE_WIN32) {
        info.native.handle_a = reinterpret_cast<uintptr_t>(target.nativeDesc.native.win32.hinstance);
        info.native.handle_b = reinterpret_cast<uintptr_t>(target.nativeDesc.native.win32.hwnd);
#endif
      } else {
        this->lastError = "Native surface platform not supported by this backend";
        return CoinRenderBackendStatus::UNSUPPORTED;
      }

      info.width = target.size[0] > 0 ? static_cast<uint32_t>(target.size[0]) : 0;
      info.height = target.size[1] > 0 ? static_cast<uint32_t>(target.size[1]) : 0;
      info.renderer = uint32_t(target.options.renderer);
      info.sample_count = target.options.sampleCount;

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
    this->preparedSurfaceId = target.surfaceId;
    this->multipassDevicePrepared = true;
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
  this->preparedSurfaceId = 0;
  this->multipassDevicePrepared = true;
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
  const bool hasActiveBump = std::any_of(frame.bumpCoords.begin(), frame.bumpCoords.end(),
    [](const CoinRenderBumpCoordOverride & coord) { return coord.imageSlot != 0; });
  const bool packed = hasActiveBump
    ? this->ffiFrame->prepareBumpFrame(frame,
        static_cast<uint32_t>(target.size[0]),
        static_cast<uint32_t>(target.size[1]), packDiagnostic)
    : this->ffiFrame->prepare(frame,
                               static_cast<uint32_t>(target.size[0]),
                               static_cast<uint32_t>(target.size[1]),
                               reuse,
                               packDiagnostic, target.submissionPreflight(frame),
                               !target.directTextureOutput);
  if (!packed) {
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
    st = target.windowReadbackRequested
      ? coin_wgpu_surface_submit_readback(target.surfaceId, &fView,
          target.colorBuffer.data(), target.colorBuffer.size(), errBuf, sizeof(errBuf))
      : coin_wgpu_surface_submit(target.surfaceId, &fView, errBuf, sizeof(errBuf));
    if (st == COIN_WGPU_OK) serial = coin_wgpu_surface_submission_serial(target.surfaceId);
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
    tPod.sample_count = target.options.sampleCount;
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

CoinRenderDeviceDomain CoinWgpuBackend::resourceDomain() const {
  CoinRenderDeviceDomain result;
  result.generation = coin_wgpu_default_device_generation();
  result.device = result.generation ? 1 : 0; // Existing C++ executor uses the default device.
  return result;
}

CoinRenderSubmitResult CoinWgpuBackend::submitRtt(const CoinRenderFramePlan& frame,
                                                  const SbVec2i32& size, uint64_t,
                                                  CoinRenderTargetP& parent, uint64_t& token) {
  CoinRenderTargetP child;
  child.options = parent.options;
  child.optionsDiagnostic.clear();
  child.directTextureOutput = true;
  child.depthReadbackEnabled = false;
  child.resize(size);
  token = 0;
  CoinRenderSubmitResult result = this->submit(frame, child);
  token = child.directTextureToken;
  return result;
}

CoinRenderSubmitResult CoinWgpuBackend::assembleCubeRtt(const std::array<uint64_t,6> & faces,
    uint32_t n,bool mips,CoinRenderTargetP &,uint64_t & token) {
  token=0;char error[1024]={0};
  const auto status=coin_wgpu_assemble_cube_rtt(faces.data(),n,mips?1u:0u,
    coin_wgpu_default_device_generation(),&token,error,sizeof(error));
  if(status==COIN_WGPU_OK)return {};
  return {status==COIN_WGPU_DEVICE_LOST?CoinRenderBackendStatus::DEVICE_LOST:
    status==COIN_WGPU_OUT_OF_MEMORY?CoinRenderBackendStatus::OUT_OF_MEMORY:
    status==COIN_WGPU_NOT_READY?CoinRenderBackendStatus::NOT_READY:CoinRenderBackendStatus::UNSUPPORTED,error};
}

void CoinWgpuBackend::finishRtt(const std::vector<uint64_t>& tokens) {
  for (uint64_t token : tokens)
    coin_wgpu_release_texture(token);
}

void CoinWgpuBackend::retireRtt(const std::vector<uint64_t>& tokens) {
  // Release active registrations now; the bridge retains their texture/views
  // until the last local submission fence, including cube assembly, completes.
  for (uint64_t token : tokens) coin_wgpu_release_texture(token);
}

void CoinWgpuBackend::configureMultipassTarget(const CoinRenderTargetP & target) {
  this->sampleCount = target.options.sampleCount;
}
bool CoinWgpuBackend::multipassOutputBytes(const CoinRenderTargetP & target, bool asynchronous,
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
  const uint64_t pitch = ((width * 4 + 255) / 256) * 256;
  return CoinRenderMultipassCore::addAttachmentBytes(pitch,height,1,
    target.kind == CoinRenderTargetP::KIND_WINDOW ? 1 : target.depthReadbackEnabled ? 2 : 1,bytes);
}

CoinRenderSubmitResult CoinWgpuBackend::preflightMultipassGraph(const CoinRenderRttPlan & graph,
    const CoinRenderFramePlan & root, const SbVec2i32 & extent) const {
  if (root.numPasses == 1) return {};
  uint64_t retained = 0;
  if (!CoinRenderMultipassCore::graphBytes(graph, root, extent, this->sampleCount,
        CoinRenderMultipassCore::Executor::WGPU, retained) || retained > root.multipassBufferBudget)
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "Multipass simultaneous RTT resources exceed the buffer budget"};
  CoinRenderFramePlan admitted = root;
  admitted.multipassBufferBudget -= retained;
  return this->preflightMultipass(admitted, extent);
}

CoinRenderSubmitResult CoinWgpuBackend::preflightMultipass(const CoinRenderFramePlan & frame,
                                                         const SbVec2i32 & extent) const {
  if (frame.numPasses == 1) return {};
  uint64_t required = 0;
  const uint64_t ticketBytes = coin_wgpu_readback_staging_bytes();

  if (!CoinRenderMultipassCore::requiredBytes(frame, extent, this->sampleCount, required) ||
      required > frame.multipassBufferBudget)
    return {CoinRenderBackendStatus::OUT_OF_MEMORY,"Multipass attachments exceed the buffer budget"};
  // BGFX has both CPU staging and a readback texture; wgpu reports the
  // physical, row-aligned ticket buffers through its concrete resource owner.
  if (!CoinRenderMultipassCore::addAttachmentBytes(ticketBytes, 1, 1, 1, required) ||
      required > frame.multipassBufferBudget)
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "Multipass attachments and retained tickets exceed the buffer budget"};
  uint64_t rasterBytes = 0;
  if (!CoinRenderMultipassCore::rasterPayloadBytes(frame, sizeof(CoinWgpuVertex), sizeof(CoinWgpuDraw), true, rasterBytes) ||
      rasterBytes > frame.multipassBufferBudget - required)
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "Multipass raster payload exceeds the buffer budget"};
  required += rasterBytes;
  CoinWgpuFfiFrame packed;
  std::string diagnostic;
  if (!packed.preparePortablePreflight(frame, uint32_t(extent[0]), uint32_t(extent[1]), diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  const uint64_t floats = packed.getView().multipass_matrix_count;
  if (floats > (frame.multipassBufferBudget - required) / sizeof(float))
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "Multipass projection payload exceeds the buffer budget"};
  if (this->multipassDevicePrepared && coin_wgpu_default_device_generation() &&
      coin_wgpu_preflight_multipass(uint32_t(extent[0]),uint32_t(extent[1]),
        uint32_t(frame.outputColorFormat), this->preparedSurfaceId) != COIN_WGPU_OK)
    return {CoinRenderBackendStatus::UNSUPPORTED,"Multipass RGBA32F storage/extent unavailable on this device"};
  return {};
}

CoinRenderSubmitResult CoinWgpuBackend::preflightRtt(const CoinRenderRttPlan& graph,
                                                     const CoinRenderFramePlan& root,
                                                     const SbVec2i32& size) const {
  return this->preflightRttImpl(graph,root,size,true);
}

CoinRenderSubmitResult CoinWgpuBackend::preflightRttImpl(const CoinRenderRttPlan& graph,
                                                     const CoinRenderFramePlan& root,
                                                     const SbVec2i32& size,
                                                     bool checkTokenCapacity) const {
  uint64_t shaderBytes = 0;
  std::string shaderDiagnostic;
  if (!graph.shaderResourceBytes(root, shaderBytes, shaderDiagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, shaderDiagnostic};
  if(std::any_of(graph.producers.begin(),graph.producers.end(),[](const CoinRenderRttProducer & p){return p.cube;})) {
    std::string diagnostic;uint64_t nominal=0;
    if(!graph.cubeNominalBytes(root,this->sampleCount,nominal,diagnostic))return {CoinRenderBackendStatus::UNSUPPORTED,diagnostic};
    if(checkTokenCapacity && graph.mode==COIN_RENDER_SCENE_TEXTURE_DIRECT && graph.tokenCount()>coin_wgpu_default_rtt_capacity())
      return {CoinRenderBackendStatus::UNSUPPORTED,"Cube RTT candidates exceed retained GPU texture capacity"};
    if(coin_wgpu_default_device_generation())for(size_t i=0;i<graph.producers.size();++i)if(graph.producers[i].cube &&
      coin_wgpu_preflight_cube_rtt(uint32_t(graph.producers[i].size[0]),graph.requestsMips(i+1,root)?1:0,this->sampleCount)!=COIN_WGPU_OK)
      return {CoinRenderBackendStatus::UNSUPPORTED,"Cube RTT atlas/face extent, copy or samples unavailable"};
    // Expanded descriptors inspect every face's capabilities and resources;
    // capacity was already checked against the logical graph's actual peak.
    const auto expanded=graph.inspection(root);return this->preflightRttImpl(expanded,root,size,false);
  }
  if (checkTokenCapacity && graph.mode == COIN_RENDER_SCENE_TEXTURE_DIRECT) {
    if (graph.tokenCount() > coin_wgpu_default_rtt_capacity())
      return {CoinRenderBackendStatus::UNSUPPORTED,
              "wgpu direct RTT exceeds the capacity of 64 retained textures"};
  }
  // Inspect every producer before the first GPU submission, including STAGED.
  bool needsWeighted = false;
  bool needsPeeling = false;
  bool needsSampleDepth = false;
  bool needsShadowMultitexture = false;
  bool needsShadowMultitextureDepth = false;
  uint32_t shadowAlphaModes=1, shadowArrayExtent=1, shadowArrayLayers=1;
  uint32_t rasterOutputs=0,rasterWidth=0,rasterHeight=0;
  uint64_t rasterBytes=0;
  const auto budget = [&](const CoinRenderFramePlan &frame, const SbVec2i32 &extent,
                          std::string &diagnostic) {
    if (!coin_render_shader_execution_profile(frame, diagnostic))
      return false;
    if (!coin_render_validate_extended_bump(frame, diagnostic))
      return false;
    const bool extendedBump = coin_render_uses_extended_bump(frame);
    if (extendedBump) {
      for (const auto &draw : frame.draws) {
        if (draw.shadowLightSlot)
          continue;
        const auto &viewport =
            frame.viewports[frame.renderStates[draw.renderStateSlot].viewportSlot];
        if (viewport.x || viewport.y || viewport.width != extent[0] ||
            viewport.height != extent[1]) {
          diagnostic = "Extended bump requires the full target viewport";
          return false;
        }
      }
    }
    if (extendedBump && coin_wgpu_default_device_generation()) {
      uint32_t imageAxis = 0;
      for (const auto &image : frame.bumpImages)
        imageAxis = std::max(imageAxis, std::max(image.width, image.height));
      const uint32_t effectivePolicy =
          std::any_of(frame.samplers.begin(), frame.samplers.end(),
                      [](const CoinRenderSamplerSnapshot &sampler) {
                        return sampler.filter == CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR;
                      })
              ? uint32_t(frame.textureSamplingPolicy)
              : 0;
      size_t largestDraw = 0;
      for (const auto &draw : frame.draws) {
        if (draw.shadowLightSlot)
          continue;
        const auto first = frame.indices.begin() + draw.geometry.firstIndex;
        const std::set<uint32_t> vertices(first, first + draw.geometry.indexCount);
        largestDraw = std::max(largestDraw, vertices.size());
      }
      if (coin_wgpu_preflight_bump_receiver(this->sampleCount, largestDraw, 1, imageAxis,
                                            effectivePolicy) != COIN_WGPU_OK) {
        diagnostic =
            "Complete directional bump receiver pipeline or device resource limits unavailable";
        return false;
      }
    }
    uint32_t outputs = 0;
    uint64_t bytes = 0;
    for (const auto & state:frame.renderStates) {
      if (state.rasterTextureSourceSlot == UINT32_MAX)
        continue;
      if (++outputs>4096 || state.textureImageSlot>=frame.textures.size()) {
        diagnostic = "Inherited images exceed output count or reference invalid payload";
        return false;
      }
      const auto & image=frame.textures[state.textureImageSlot];
      const uint64_t pixels=uint64_t(image.width)*image.height;
      if (!image.width || !image.height || pixels>(128ull*1024*1024-bytes)/16) {
        diagnostic = "Inherited image outputs exceed 128 MiB or have zero extent";
        return false;
      }
      bytes+=pixels*16;
      rasterWidth = std::max(rasterWidth, image.width);
      rasterHeight = std::max(rasterHeight, image.height);
    }
    // Producers execute sequentially: retain the largest per-frame footprint,
    // not a sum of temporaries that cannot coexist.
    rasterOutputs = std::max(rasterOutputs, outputs);
    rasterBytes = std::max(rasterBytes, bytes);
    std::vector<CoinRenderCompositionItem> order;
    if (!coin_render_composition_order(frame, order, diagnostic))
      return false;
    const bool weighted = std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
      return item.blend && item.deferred && !item.additive &&
        item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT;
    });
    const bool peeling = std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
      return item.blend && item.deferred && !item.additive &&
        item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS;
    });
    needsSampleDepth = needsSampleDepth || frame.rasterizationPolicy == COIN_RENDER_RASTER_CANONICAL_8BIT ||
      std::any_of(frame.renderStates.begin(), frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot & state) {
      return state.polygonOffsetEnabled || state.depthRange[0] > state.depthRange[1];
    });
    const bool shadowMultitexture = !frame.shadowGroups.empty() && std::any_of(frame.draws.begin(), frame.draws.end(), [&frame](const CoinRenderDrawPacket & draw) {
      if (draw.renderStateSlot >= frame.renderStates.size()) return false;
      const auto & state = frame.renderStates[draw.renderStateSlot];
      return (state.hasTexture && state.textureImageSlot < frame.textures.size() &&
        (frame.textures[state.textureImageSlot].volumeDepth || frame.textures[state.textureImageSlot].cubeFaceSize)) ||
        std::any_of(std::begin(state.extraTextures), std::end(state.extraTextures), [](const CoinRenderTextureUnitSnapshot & unit) { return unit.enabled; });
    });
    if (shadowMultitexture && this->sampleCount > 1 && frame.rasterizationPolicy != COIN_RENDER_RASTER_NATIVE) {
      diagnostic = "Shadow multitexture MSAA requires native rasterization";
      return false;
    }
    needsShadowMultitextureDepth = needsShadowMultitextureDepth || (shadowMultitexture && this->sampleCount > 1 &&
      std::any_of(frame.renderStates.begin(), frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot & state) {
        return state.depthRange[0] != 0.0f || state.depthRange[1] != 1.0f ||
          (state.polygonOffsetEnabled && (state.polygonOffsetStyles & state.polygonOffsetPrimitiveStyle) &&
           (state.polygonOffsetFactor != 0.0f || state.polygonOffsetUnits != 0.0f || state.polygonOffsetSlopeBias != 0.0f));
      }));
    if (shadowMultitexture) {
      CoinRenderShadowPlan shadows;
      if (!coin_render_plan_shadows(frame, shadows, diagnostic) ||
          !coin_render_shadow_object_profile(frame, shadows, shadows.passes.size(), diagnostic))
        return false;
      // Match the private D2Array allocation, including HAL-compatible padding
      // for singleton arrays and square six-layer cube-compatible descriptors.
      const uint32_t arrayLayers=uint32_t(shadows.passes.size());
      const uint32_t minimumArrayLayers=arrayLayers%6==0?arrayLayers+1:2;
      if (!coin_render_shadow_array_budget(shadows, minimumArrayLayers, diagnostic))
        return false;
      shadowArrayLayers=std::max(shadowArrayLayers,uint32_t(shadows.passes.size()));
      for (const auto &pass : shadows.passes)
        shadowArrayExtent = std::max(shadowArrayExtent, pass.mapSize);
      shadowAlphaModes |= (weighted?2u:0u)|(peeling?4u:0u);
    }
    needsShadowMultitexture = needsShadowMultitexture || shadowMultitexture;
    needsWeighted = needsWeighted || weighted;
    needsPeeling = needsPeeling || peeling;
    if (!weighted && !peeling)
      return true;
    auto options = frame.transparency;
    if (!peeling)
      options.layers = 1;
    uint64_t required = 0;
    return coin_render_transparency_sample_budget(extent[0], extent[1], options, true,
      this->sampleCount, required, diagnostic);
  };
  std::string diagnostic;
  if (!budget(root, size, diagnostic)) return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  for (const auto & producer : graph.producers)
    if (!budget(producer.plan, producer.size, diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  if(!coin_wgpu_default_device_generation()) return {}; // Hardware checks follow prepare.
  if (rasterOutputs && coin_wgpu_preflight_raster_images(rasterOutputs,rasterWidth,rasterHeight,rasterBytes)!=COIN_WGPU_OK)
    return {CoinRenderBackendStatus::UNSUPPORTED,"wgpu inherited raster image pipeline, output extent or storage capability unavailable"};
  if (needsShadowMultitexture && coin_wgpu_preflight_shadow_multitexture_alpha(this->sampleCount,shadowAlphaModes,shadowArrayExtent,shadowArrayLayers) != COIN_WGPU_OK)
    return {CoinRenderBackendStatus::UNSUPPORTED, "wgpu complete shadow multitexture composition or moment-array capability unavailable"};
  if (needsShadowMultitextureDepth && coin_wgpu_preflight_shadow_multitexture_depth(this->sampleCount) != COIN_WGPU_OK)
    return {CoinRenderBackendStatus::UNSUPPORTED, "wgpu extended custom MSAA depth profile unavailable"};
  if (needsSampleDepth && coin_wgpu_preflight_sample_depth(this->sampleCount) != COIN_WGPU_OK)
    return {CoinRenderBackendStatus::UNSUPPORTED, "wgpu custom MSAA depth requires sample-frequency shading"};
  if (needsWeighted && coin_wgpu_preflight_weighted(this->sampleCount) != COIN_WGPU_OK)
    return {CoinRenderBackendStatus::UNSUPPORTED, "wgpu weighted MSAA formats/blend/sample count unavailable"};
  if (needsPeeling && coin_wgpu_preflight_peeling(this->sampleCount) != COIN_WGPU_OK)
    return {CoinRenderBackendStatus::UNSUPPORTED, "wgpu peeling formats/depth/mask sample count unavailable"};
  uint32_t formats=0, outputs=0, anisotropy=1;
  const auto scan=[&](const CoinRenderFramePlan & frame) {
    for(const auto & image:frame.textures) formats|=1u<<uint32_t(image.format);
    for(const auto & sampler:frame.samplers) anisotropy=std::max(anisotropy,sampler.maxAnisotropy);
  };
  scan(root);
  for(size_t i=0;i<graph.producers.size();++i) {
    const auto & producer=graph.producers[i]; scan(producer.plan);
    if(graph.mode==COIN_RENDER_SCENE_TEXTURE_DIRECT) {
      const bool hdr=producer.format==CoinRenderTextureFormat::RGBA16_FLOAT;
      outputs|=hdr?2u:1u;
      if(graph.requestsMips(i+1,root)) outputs|=hdr?8u:4u;
    }
  }
  const auto status=coin_wgpu_preflight_texture_profile(formats,outputs,anisotropy);
  if(status!=COIN_WGPU_OK) {
    const auto mapped=status==COIN_WGPU_NOT_READY?CoinRenderBackendStatus::NOT_READY:
      status==COIN_WGPU_DEVICE_LOST?CoinRenderBackendStatus::DEVICE_LOST:
      status==COIN_WGPU_OUT_OF_MEMORY?CoinRenderBackendStatus::OUT_OF_MEMORY:
      status==COIN_WGPU_BACKEND_ERROR?CoinRenderBackendStatus::BACKEND_ERROR:CoinRenderBackendStatus::UNSUPPORTED;
    return {mapped,"wgpu advanced texture profile unavailable on the prepared adapter"};
  }
  if (coin_wgpu_default_device_generation()) {
    const auto prepareModules = [&](const CoinRenderFramePlan &frame, const SbVec2i32 &extent) -> CoinRenderSubmitResult {
      if (!frame.hasPortableShaderDraws()) return CoinRenderSubmitResult{};
      CoinWgpuFfiFrame packed;
      std::string diagnostic;
      if (!packed.preparePortablePreflight(frame, uint32_t(extent[0]), uint32_t(extent[1]), diagnostic))
        return CoinRenderSubmitResult{CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
      char error[1024] = {};
      if (coin_wgpu_install_cpp_portable_core() != COIN_WGPU_OK)
        return CoinRenderSubmitResult{CoinRenderBackendStatus::BACKEND_ERROR, "Portable shader Core callback installation failed"};
      const auto status = coin_wgpu_prepare_portable_frame(&packed.getView(), this->sampleCount, error, sizeof(error));
      return status == COIN_WGPU_OK ? CoinRenderSubmitResult{} :
        CoinRenderSubmitResult{CoinRenderBackendStatus::UNSUPPORTED, error};
    };
    auto prepared = prepareModules(root, size);
    if (prepared.status != CoinRenderBackendStatus::SUCCESS) return prepared;
    for (const auto &producer : graph.producers) {
      prepared = prepareModules(producer.plan, producer.size);
      if (prepared.status != CoinRenderBackendStatus::SUCCESS) return prepared;
    }
  }
  (void)size;
  // Viewport projection/intersection is resolved by the shared Core during
  // packing for each producer's own extent, including empty intersections.
  return CoinRenderSubmitResult();
}

bool CoinWgpuBackend::readbackLoad(uint64_t& jobs, uint64_t& bytes) const {
  return coin_wgpu_readback_resource_load(&jobs, &bytes) != 0;
}
