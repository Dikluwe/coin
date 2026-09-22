#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/SoWgpuRustBackend.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include "rendering/wgpu/coin_wgpu_ffi.h"

#include <Inventor/SbMatrix.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbColor.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

// Static assertions ensuring ABI compatibility with Rust bridge
static_assert(sizeof(CoinWgpuVertex) == 36, "CoinWgpuVertex size mismatch");
static_assert(alignof(CoinWgpuVertex) == 4, "CoinWgpuVertex alignment mismatch");
static_assert(offsetof(CoinWgpuVertex, position) == 0, "CoinWgpuVertex position offset mismatch");
static_assert(offsetof(CoinWgpuVertex, normal) == 12, "CoinWgpuVertex normal offset mismatch");
static_assert(offsetof(CoinWgpuVertex, texcoord) == 24, "CoinWgpuVertex texcoord offset mismatch");
static_assert(offsetof(CoinWgpuVertex, material_slot) == 32, "CoinWgpuVertex material_slot offset mismatch");

static_assert(sizeof(CoinWgpuDraw) == 24, "CoinWgpuDraw size mismatch");
static_assert(alignof(CoinWgpuDraw) == 4, "CoinWgpuDraw alignment mismatch");

static_assert(sizeof(CoinWgpuMaterial) == 72, "CoinWgpuMaterial size mismatch");
static_assert(alignof(CoinWgpuMaterial) == 4, "CoinWgpuMaterial alignment mismatch");

static_assert(sizeof(CoinWgpuRenderState) == 244, "CoinWgpuRenderState size mismatch");
static_assert(alignof(CoinWgpuRenderState) == 4, "CoinWgpuRenderState alignment mismatch");
static_assert(offsetof(CoinWgpuRenderState, cull_mode) == 236, "CoinWgpuRenderState cull_mode offset mismatch");
static_assert(offsetof(CoinWgpuRenderState, front_face) == 240, "CoinWgpuRenderState front_face offset mismatch");

static_assert(sizeof(CoinWgpuTarget) == 48, "CoinWgpuTarget size mismatch");
static_assert(sizeof(CoinWgpuFrameView) == 112, "CoinWgpuFrameView size mismatch");
static_assert(sizeof(CoinWgpuNativeSurfaceDescriptor) == 32, "CoinWgpuNativeSurfaceDescriptor size mismatch");
static_assert(sizeof(CoinWgpuSurfaceCreateInfo) == 48, "CoinWgpuSurfaceCreateInfo size mismatch");

SoWgpuRustBackend::SoWgpuRustBackend()
  : status(BackendStatus::SUCCESS),
    lastError("")
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
  if (target.kind == SoWgpuRenderTargetP::KIND_OFFSCREEN) {
    if (target.colorBuffer.empty() || target.size[0] <= 0 || target.size[1] <= 0) {
      this->lastError = "Render target buffer is not allocated";
      return BackendStatus::NOT_READY;
    }
  } else if (target.kind == SoWgpuRenderTargetP::KIND_WINDOW) {
    if (target.surfaceId == 0 || target.suspended || target.size[0] <= 0 || target.size[1] <= 0) {
      this->lastError = "Window surface target is not ready or suspended";
      return BackendStatus::NOT_READY;
    }
  }

  // 1. Pack vertices
  std::vector<CoinWgpuVertex> verticesPod(frame.vertices.size());
  for (size_t i = 0; i < frame.vertices.size(); ++i) {
    const auto & v = frame.vertices[i];
    verticesPod[i].position[0] = v.position[0];
    verticesPod[i].position[1] = v.position[1];
    verticesPod[i].position[2] = v.position[2];
    verticesPod[i].normal[0] = v.normal[0];
    verticesPod[i].normal[1] = v.normal[1];
    verticesPod[i].normal[2] = v.normal[2];
    verticesPod[i].texcoord[0] = v.texcoord[0];
    verticesPod[i].texcoord[1] = v.texcoord[1];
    verticesPod[i].material_slot = v.materialSlot;
  }

  // 2. Pack draws
  std::vector<CoinWgpuDraw> drawsPod(frame.draws.size());
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const auto & d = frame.draws[i];
    drawsPod[i].topology = static_cast<uint32_t>(d.topology);
    drawsPod[i].first_vertex = d.geometry.firstVertex;
    drawsPod[i].vertex_count = d.geometry.vertexCount;
    drawsPod[i].first_index = d.geometry.firstIndex;
    drawsPod[i].index_count = d.geometry.indexCount;
    drawsPod[i].render_state_slot = d.renderStateSlot;
  }

  // 3. Pack materials
  std::vector<CoinWgpuMaterial> materialsPod(frame.materials.size());
  for (size_t i = 0; i < frame.materials.size(); ++i) {
    const auto & m = frame.materials[i];
    std::memcpy(materialsPod[i].ambient, m.ambient, sizeof(m.ambient));
    std::memcpy(materialsPod[i].diffuse, m.diffuse, sizeof(m.diffuse));
    std::memcpy(materialsPod[i].specular, m.specular, sizeof(m.specular));
    std::memcpy(materialsPod[i].emission, m.emission, sizeof(m.emission));
    materialsPod[i].shininess = m.shininess;
    materialsPod[i].transparency = m.transparency;
  }

  // 4. Pack render states
  std::vector<CoinWgpuRenderState> statesPod(frame.renderStates.size());
  for (size_t i = 0; i < frame.renderStates.size(); ++i) {
    const auto & rs = frame.renderStates[i];
    SbMatrix modelView = rs.model * rs.view;

    SbMatrix normalMatrix;
    float det = modelView.det4();
    if (std::abs(det) > 1e-9f) {
      normalMatrix = modelView.inverse().transpose();
    } else {
      normalMatrix = SbMatrix::identity();
    }

    // Clip space conversion from Coin [-1, 1] to WebGPU [0, 1]
    SbMatrix C(
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f
    );
    SbMatrix projWgpu = rs.projectionCoin * C;
    SbMatrix mvpWgpu = modelView * projWgpu;

    std::memcpy(statesPod[i].model_view, modelView.getValue(), sizeof(float) * 16);
    std::memcpy(statesPod[i].model_view_projection, mvpWgpu.getValue(), sizeof(float) * 16);
    std::memcpy(statesPod[i].normal_matrix, normalMatrix.getValue(), sizeof(float) * 16);

    bool hasLight = false;
    if (rs.lightingSlot < frame.lightingStates.size() && !frame.lightingStates[rs.lightingSlot].lights.empty()) {
      const auto & l = frame.lightingStates[rs.lightingSlot].lights[0];
      hasLight = true;
      statesPod[i].light_direction[0] = l.direction[0];
      statesPod[i].light_direction[1] = l.direction[1];
      statesPod[i].light_direction[2] = l.direction[2];
      statesPod[i].light_direction[3] = 0.0f;
      statesPod[i].light_color[0] = l.color[0];
      statesPod[i].light_color[1] = l.color[1];
      statesPod[i].light_color[2] = l.color[2];
      statesPod[i].light_color[3] = 1.0f;
      statesPod[i].light_intensity = l.intensity;
    } else {
      statesPod[i].light_direction[0] = 0.0f;
      statesPod[i].light_direction[1] = 0.0f;
      statesPod[i].light_direction[2] = 1.0f;
      statesPod[i].light_direction[3] = 0.0f;
      statesPod[i].light_color[0] = 1.0f;
      statesPod[i].light_color[1] = 1.0f;
      statesPod[i].light_color[2] = 1.0f;
      statesPod[i].light_color[3] = 1.0f;
      statesPod[i].light_intensity = 1.0f;
    }
    statesPod[i].has_light = hasLight ? 1 : 0;
    statesPod[i].material_slot = rs.materialSlot;
    statesPod[i].cull_mode = static_cast<uint32_t>(rs.cullMode);
    statesPod[i].front_face = static_cast<uint32_t>(rs.frontFace);
  }

  // 5. Build frame view
  CoinWgpuFrameView fView{};
  fView.abi_version = COIN_WGPU_ABI_VERSION;
  fView.struct_size = sizeof(CoinWgpuFrameView);
  fView.vertices = verticesPod.empty() ? nullptr : verticesPod.data();
  fView.vertex_count = static_cast<uint64_t>(verticesPod.size());
  fView.indices = frame.indices.empty() ? nullptr : frame.indices.data();
  fView.index_count = static_cast<uint64_t>(frame.indices.size());
  fView.draws = drawsPod.empty() ? nullptr : drawsPod.data();
  fView.draw_count = static_cast<uint64_t>(drawsPod.size());
  fView.materials = materialsPod.empty() ? nullptr : materialsPod.data();
  fView.material_count = static_cast<uint64_t>(materialsPod.size());
  fView.states = statesPod.empty() ? nullptr : statesPod.data();
  fView.state_count = static_cast<uint64_t>(statesPod.size());

  fView.clear_color[0] = frame.clearColor[0];
  fView.clear_color[1] = frame.clearColor[1];
  fView.clear_color[2] = frame.clearColor[2];
  fView.clear_color[3] = frame.clearColor[3];
  fView.width = static_cast<uint32_t>(target.size[0]);
  fView.height = static_cast<uint32_t>(target.size[1]);

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

    // 7. Submit to WebGPU via FFI
    st = coin_wgpu_submit(&tPod, &fView, errBuf, sizeof(errBuf));
    serial = tPod.submission_serial;
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
BackendStatus SoWgpuRustBackend::submit(const FramePlan &, SoWgpuRenderTargetP &) { return BackendStatus::UNSUPPORTED; }
void SoWgpuRustBackend::poll() {}
const std::string & SoWgpuRustBackend::getLastError() const { return lastError; }
bool SoWgpuRustBackend::isAvailable() { return false; }
std::string SoWgpuRustBackend::getAdapterInfo() { return "None"; }
#endif
