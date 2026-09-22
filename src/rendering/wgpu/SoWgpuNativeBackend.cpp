#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuNativeBackend.h"
#include "rendering/wgpu/SoWgpuRenderTargetP.h"

#include <cstring>
#include <iostream>
#include <vector>

SoWgpuNativeBackend::SoWgpuNativeBackend()
  : lastError("")
#if defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
  , instance(nullptr)
  , adapter(nullptr)
  , device(nullptr)
  , queue(nullptr)
  , isReady(false)
#endif
{
}

SoWgpuNativeBackend::~SoWgpuNativeBackend()
{
#if defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
  if (this->queue) { wgpuQueueRelease(this->queue); this->queue = nullptr; }
  if (this->device) { wgpuDeviceRelease(this->device); this->device = nullptr; }
  if (this->adapter) { wgpuAdapterRelease(this->adapter); this->adapter = nullptr; }
  if (this->instance) { wgpuInstanceRelease(this->instance); this->instance = nullptr; }
#endif
}

#if defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
static void onAdapterRequestEnded(WGPURequestAdapterStatus status,
                                 WGPUAdapter adapter,
                                 const char * message,
                                 void * userdata) {
  if (status == WGPURequestAdapterStatus_Success) {
    *static_cast<WGPUAdapter*>(userdata) = adapter;
  }
}

static void onDeviceRequestEnded(WGPURequestDeviceStatus status,
                                WGPUDevice device,
                                const char * message,
                                void * userdata) {
  if (status == WGPURequestDeviceStatus_Success) {
    *static_cast<WGPUDevice*>(userdata) = device;
  }
}
#endif

BackendStatus
SoWgpuNativeBackend::prepare(SoWgpuRenderTargetP & /*target*/)
{
#if defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
  if (this->isReady) return BackendStatus::SUCCESS;

  WGPUInstanceDescriptor instDesc = {};
  instDesc.nextInChain = nullptr;
  this->instance = wgpuCreateInstance(&instDesc);
  if (!this->instance) {
    this->lastError = "Failed to create WebGPU instance";
    return BackendStatus::BACKEND_ERROR;
  }

  WGPURequestAdapterOptions opt = {};
  opt.nextInChain = nullptr;
  opt.powerPreference = WGPUPowerPreference_HighPerformance;
  wgpuInstanceRequestAdapter(this->instance, &opt, onAdapterRequestEnded, &this->adapter);
  if (!this->adapter) {
    this->lastError = "No compatible WebGPU adapter found";
    return BackendStatus::NOT_READY;
  }

  WGPUDeviceDescriptor devDesc = {};
  devDesc.nextInChain = nullptr;
  wgpuAdapterRequestDevice(this->adapter, &devDesc, onDeviceRequestEnded, &this->device);
  if (!this->device) {
    this->lastError = "Failed to create WebGPU device";
    return BackendStatus::NOT_READY;
  }

  this->queue = wgpuDeviceGetQueue(this->device);
  if (!this->queue) {
    this->lastError = "Failed to get WebGPU queue";
    return BackendStatus::BACKEND_ERROR;
  }

  this->isReady = true;
  return BackendStatus::SUCCESS;
#else
  this->lastError = "Native WebGPU backend not enabled at compile time";
  return BackendStatus::UNSUPPORTED;
#endif
}

BackendStatus
SoWgpuNativeBackend::submit(const FramePlan & frame, SoWgpuRenderTargetP & target)
{
#if defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
  if (!this->isReady) {
    BackendStatus st = this->prepare(target);
    if (st != BackendStatus::SUCCESS) return st;
  }

  int width = target.size[0];
  int height = target.size[1];
  if (width <= 0 || height <= 0) {
    this->lastError = "Target size is invalid";
    return BackendStatus::BACKEND_ERROR;
  }

  // Create color texture
  WGPUTextureDescriptor colorDesc = {};
  colorDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
  colorDesc.dimension = WGPUTextureDimension_2D;
  colorDesc.size = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
  colorDesc.format = WGPUTextureFormat_RGBA8Unorm;
  colorDesc.mipLevelCount = 1;
  colorDesc.sampleCount = 1;

  WGPUTexture colorTex = wgpuDeviceCreateTexture(this->device, &colorDesc);
  if (!colorTex) {
    this->lastError = "Failed to create color texture";
    return BackendStatus::OUT_OF_MEMORY;
  }
  WGPUTextureView colorView = wgpuTextureCreateView(colorTex, nullptr);

  // Create depth texture
  WGPUTextureDescriptor depthDesc = {};
  depthDesc.usage = WGPUTextureUsage_RenderAttachment;
  depthDesc.dimension = WGPUTextureDimension_2D;
  depthDesc.size = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
  depthDesc.format = WGPUTextureFormat_Depth24Plus;
  depthDesc.mipLevelCount = 1;
  depthDesc.sampleCount = 1;

  WGPUTexture depthTex = wgpuDeviceCreateTexture(this->device, &depthDesc);
  WGPUTextureView depthView = depthTex ? wgpuTextureCreateView(depthTex, nullptr) : nullptr;

  // Command encoder
  WGPUCommandEncoderDescriptor encDesc = {};
  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(this->device, &encDesc);

  // Render pass
  WGPURenderPassColorAttachment colorAttachment = {};
  colorAttachment.view = colorView;
  colorAttachment.loadOp = WGPULoadOp_Clear;
  colorAttachment.storeOp = WGPUStoreOp_Store;
  colorAttachment.clearValue = {
    static_cast<double>(frame.clearColor[0]),
    static_cast<double>(frame.clearColor[1]),
    static_cast<double>(frame.clearColor[2]),
    static_cast<double>(frame.clearColor[3])
  };

  WGPURenderPassDepthStencilAttachment depthAttachment = {};
  if (depthView) {
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Discard;
    depthAttachment.depthClearValue = 1.0f;
  }

  WGPURenderPassDescriptor passDesc = {};
  passDesc.colorAttachmentCount = 1;
  passDesc.colorAttachments = &colorAttachment;
  passDesc.depthStencilAttachment = depthView ? &depthAttachment : nullptr;

  WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);

  // Staging buffer for readback
  uint32_t bytesPerRow = (static_cast<uint32_t>(width) * 4 + 255) & ~255;
  uint32_t bufferSize = bytesPerRow * static_cast<uint32_t>(height);

  WGPUBufferDescriptor bufDesc = {};
  bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
  bufDesc.size = bufferSize;
  WGPUBuffer readbackBuf = wgpuDeviceCreateBuffer(this->device, &bufDesc);

  WGPUImageCopyTexture copySrc = {};
  copySrc.texture = colorTex;
  WGPUImageCopyBuffer copyDst = {};
  copyDst.buffer = readbackBuf;
  copyDst.layout.bytesPerRow = bytesPerRow;
  copyDst.layout.rowsPerImage = static_cast<uint32_t>(height);
  WGPUExtent3D copySize = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };

  wgpuCommandEncoderCopyTextureToBuffer(encoder, &copySrc, &copyDst, &copySize);

  WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
  wgpuQueueSubmit(this->queue, 1, &commands);

  wgpuCommandBufferRelease(commands);
  wgpuCommandEncoderRelease(encoder);

  // Map and read pixels
  struct MapContext {
    bool done = false;
  } ctx;
  auto onMap = [](WGPUBufferMapAsyncStatus /*status*/, void * userdata) {
    auto * c = static_cast<MapContext*>(userdata);
    c->done = true;
  };
  wgpuBufferMapAsync(readbackBuf, WGPUMapMode_Read, 0, bufferSize, onMap, &ctx);
  this->poll();

  const uint8_t * mapped = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(readbackBuf, 0, bufferSize));
  if (mapped) {
    target.colorBuffer.resize(width * height * 4);
    for (int y = 0; y < height; ++y) {
      std::memcpy(&target.colorBuffer[y * width * 4], &mapped[y * bytesPerRow], width * 4);
    }
    wgpuBufferUnmap(readbackBuf);
  }

  wgpuBufferRelease(readbackBuf);
  wgpuTextureViewRelease(colorView);
  wgpuTextureRelease(colorTex);
  if (depthView) wgpuTextureViewRelease(depthView);
  if (depthTex) wgpuTextureRelease(depthTex);

  return BackendStatus::SUCCESS;
#else
  this->lastError = "Native WebGPU backend not enabled at compile time";
  return BackendStatus::UNSUPPORTED;
#endif
}

void
SoWgpuNativeBackend::poll()
{
#if defined(HAVE_WGPU_DAWN)
  if (this->device) {
    wgpuDeviceTick(this->device);
  }
#elif defined(HAVE_WGPU_NATIVE)
  if (this->device) {
    wgpuDevicePoll(this->device, true, nullptr);
  }
#endif
}
