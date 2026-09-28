#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/rendering/CoinRenderCapabilities.h>

#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/coinrender/SoWgpuRustBackend.h"
#endif

#if defined(HAVE_WGPU_BGFX)
#include "rendering/coinrender/SoWgpuBgfxBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <bgfx/bgfx.h>
#endif

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
const size_t capabilitiesV1Size =
  offsetof(CoinRenderCapabilities, probe_status);

void
setDiagnostic(CoinRenderCapabilities & result, const std::string & text)
{
  std::snprintf(result.diagnostic, sizeof(result.diagnostic), "%s", text.c_str());
}

#if defined(HAVE_WGPU_BGFX)
const char *
rendererName(bgfx::RendererType::Enum renderer)
{
  switch (renderer) {
  case bgfx::RendererType::Vulkan: return "Vulkan";
  case bgfx::RendererType::OpenGL: return "OpenGL";
  default: return bgfx::getRendererName(renderer);
  }
}

uint32_t
rendererId(bgfx::RendererType::Enum renderer)
{
  switch (renderer) {
  case bgfx::RendererType::Vulkan: return COIN_RENDER_RENDERER_VULKAN;
  case bgfx::RendererType::OpenGL: return COIN_RENDER_RENDERER_OPENGL;
  default: return COIN_RENDER_RENDERER_OTHER;
  }
}

uint32_t
formatFeatures(uint32_t nativeFlags)
{
  uint32_t result = 0;
  if ((nativeFlags & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0)
    result |= COIN_RENDER_FORMAT_TEXTURE_2D;
  if ((nativeFlags & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0)
    result |= COIN_RENDER_FORMAT_FRAMEBUFFER;
  if ((nativeFlags & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA) != 0)
    result |= COIN_RENDER_FORMAT_FRAMEBUFFER_MSAA;
  if ((nativeFlags & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ) != 0)
    result |= COIN_RENDER_FORMAT_IMAGE_READ;
  if ((nativeFlags & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE) != 0)
    result |= COIN_RENDER_FORMAT_IMAGE_WRITE;
  return result;
}

const char *
vendorName(uint16_t vendor)
{
  switch (vendor) {
  case 0x1002: return "AMD";
  case 0x10de: return "NVIDIA";
  case 0x8086: return "Intel";
  case 0x106b: return "Apple";
  case 0x1414: return "Microsoft";
  default: return "unknown vendor";
  }
}

void
probeBgfx(uint32_t target, CoinRenderCapabilities & result)
{
  CoinRenderTargetP probeTarget(SbVec2i32(1, 1));
  probeTarget.depthReadbackEnabled = false;
  SoWgpuBgfxBackend probeBackend;
  const CoinRenderBackendStatus status = probeBackend.prepare(probeTarget);
  if (status != CoinRenderBackendStatus::SUCCESS) {
    const std::string error = probeBackend.getLastError().empty() ?
      "BGFX runtime probe failed without a diagnostic" : probeBackend.getLastError();
    result.probe_status =
      status == CoinRenderBackendStatus::NOT_READY ? COIN_RENDER_PROBE_UNAVAILABLE :
      (status == CoinRenderBackendStatus::UNSUPPORTED &&
       (error.find("API thread") != std::string::npos ||
        error.find("view budget") != std::string::npos)) ?
        COIN_RENDER_PROBE_BUSY : COIN_RENDER_PROBE_ERROR;
    setDiagnostic(result, error);
    return;
  }

  bgfx::frame();
  const bgfx::Caps * caps = bgfx::getCaps();
  const bgfx::Stats * stats = bgfx::getStats();
  if (caps == nullptr) {
    result.probe_status = COIN_RENDER_PROBE_ERROR;
    setDiagnostic(result, "BGFX initialized but returned no capability record");
    return;
  }

  result.gpu_available = 1;
  result.probe_status = COIN_RENDER_PROBE_AVAILABLE;
  result.renderer = rendererId(caps->rendererType);
  result.vendor_id = caps->vendorId;
  result.device_id = caps->deviceId;
  result.max_framebuffer_attachments = caps->limits.maxFBAttachments;
  result.format_rgba8 = formatFeatures(caps->formats[bgfx::TextureFormat::RGBA8]);
  result.format_d24s8 = formatFeatures(caps->formats[bgfx::TextureFormat::D24S8]);
  result.format_d32f = formatFeatures(caps->formats[bgfx::TextureFormat::D32F]);
  result.format_rgba16f = formatFeatures(caps->formats[bgfx::TextureFormat::RGBA16F]);
  result.format_r16f = formatFeatures(caps->formats[bgfx::TextureFormat::R16F]);
  if (caps->limits.maxFBAttachments > 1)
    result.runtime_features |= COIN_RENDER_RUNTIME_MRT;
  if ((caps->supported & BGFX_CAPS_BLEND_INDEPENDENT) != 0)
    result.runtime_features |= COIN_RENDER_RUNTIME_INDEPENDENT_BLEND;
  if ((caps->supported & BGFX_CAPS_COMPUTE) != 0)
    result.runtime_features |= COIN_RENDER_RUNTIME_COMPUTE;
  if (stats != nullptr && stats->gpuTimerFreq > 0)
    result.runtime_features |= COIN_RENDER_RUNTIME_TIMESTAMPS;

  std::snprintf(result.adapter_name, sizeof(result.adapter_name),
                "BGFX %s %s (vendor 0x%04x, device 0x%04x)",
                rendererName(caps->rendererType), vendorName(caps->vendorId),
                static_cast<unsigned int>(caps->vendorId),
                static_cast<unsigned int>(caps->deviceId));
  std::snprintf(result.diagnostic, sizeof(result.diagnostic),
                "BGFX %s probe succeeded; MRT=%u (max attachments=%u), independent blend=%u, compute=%u, timestamps=%u; framebuffer formats RGBA8=0x%x D24S8=0x%x D32F=0x%x RGBA16F=0x%x R16F=0x%x%s",
                rendererName(caps->rendererType),
                (result.runtime_features & COIN_RENDER_RUNTIME_MRT) != 0,
                result.max_framebuffer_attachments,
                (result.runtime_features & COIN_RENDER_RUNTIME_INDEPENDENT_BLEND) != 0,
                (result.runtime_features & COIN_RENDER_RUNTIME_COMPUTE) != 0,
                (result.runtime_features & COIN_RENDER_RUNTIME_TIMESTAMPS) != 0,
                result.format_rgba8, result.format_d24s8, result.format_d32f,
                result.format_rgba16f, result.format_r16f,
                target == COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW ?
                  "; Xlib presentation requires preparing a real surface" : "");
}
#endif
}

extern "C" int32_t
coin_render_query_capabilities(uint32_t target,
                                            void * output,
                                            size_t output_size)
{
  const bool legacyV1 = output_size == capabilitiesV1Size;
  if (!output || (!legacyV1 &&
                  output_size < sizeof(CoinRenderCapabilities))) return 2;
  if (target != COIN_RENDER_EXPERIMENTAL_OFFSCREEN &&
      target != COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW) return 1;
#if !defined(__linux__)
  if (target == COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW) return 1;
#endif

  CoinRenderCapabilities result{};
  result.struct_size = static_cast<uint32_t>(legacyV1 ? capabilitiesV1Size : sizeof(result));
  result.version = legacyV1 ? 1u : COIN_RENDER_CAPABILITIES_VERSION;
  result.target = target;
  result.probe_status = COIN_RENDER_PROBE_NOT_RUN;

#if defined(HAVE_WGPU_RUST_BRIDGE)
  result.backend = COIN_RENDER_EXPERIMENTAL_RUST;
  result.gpu_available = SoWgpuRustBackend::isAvailable() ? 1u : 0u;
  result.probe_status = result.gpu_available ?
    COIN_RENDER_PROBE_AVAILABLE : COIN_RENDER_PROBE_UNAVAILABLE;
  if (result.gpu_available) {
    const std::string name = SoWgpuRustBackend::getAdapterInfo();
    std::strncpy(result.adapter_name, name.c_str(), sizeof(result.adapter_name) - 1);
    setDiagnostic(result, "Rust/wgpu adapter probe succeeded");
  } else {
    setDiagnostic(result, "Rust/wgpu found no compatible runtime adapter");
  }
  result.features = COIN_RENDER_FEATURE_TRIANGLES |
                    COIN_RENDER_FEATURE_INDEXED_GEOMETRY |
                    COIN_RENDER_FEATURE_LINES_POINTS |
                    COIN_RENDER_FEATURE_TEXTURE_2D |
                    COIN_RENDER_FEATURE_LIGHTS |
                    COIN_RENDER_FEATURE_FOG |
                    COIN_RENDER_FEATURE_SORTED_ALPHA |
                    COIN_RENDER_FEATURE_COLOR_DEPTH;
  result.max_lights_per_draw = 8;
  result.max_texture_units = 1;
  if (target == COIN_RENDER_EXPERIMENTAL_OFFSCREEN) {
    result.features |= COIN_RENDER_FEATURE_ASYNC_READBACK |
                       COIN_RENDER_FEATURE_DIRECT_RTT;
    result.max_scene_texture_depth = 8;
    result.max_scene_texture_bytes_per_apply = UINT64_C(64) * 1024 * 1024;
  }
#elif defined(HAVE_WGPU_DAWN) || defined(HAVE_WGPU_NATIVE)
  result.backend = COIN_RENDER_EXPERIMENTAL_NATIVE_SPIKE;
  result.probe_status = COIN_RENDER_PROBE_UNAVAILABLE;
  setDiagnostic(result, "Native WebGPU spike has no established runtime rendering profile");
  if (target == COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW) return 1;
#elif defined(HAVE_WGPU_BGFX)
  result.backend = COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION;
  result.features = COIN_RENDER_FEATURE_TRIANGLES |
                    COIN_RENDER_FEATURE_INDEXED_GEOMETRY |
                    COIN_RENDER_FEATURE_LINES_POINTS |
                    COIN_RENDER_FEATURE_TEXTURE_2D |
                    COIN_RENDER_FEATURE_LIGHTS |
                    COIN_RENDER_FEATURE_FOG |
                    COIN_RENDER_FEATURE_SORTED_ALPHA;
  result.max_lights_per_draw = 8;
  result.max_texture_units = COIN_RENDER_MAX_TEXTURE_UNITS;
  if (target == COIN_RENDER_EXPERIMENTAL_OFFSCREEN)
    result.features |= COIN_RENDER_FEATURE_COLOR_DEPTH | COIN_RENDER_FEATURE_ASYNC_READBACK;
  probeBgfx(target, result);
#else
  result.backend = COIN_RENDER_EXPERIMENTAL_RECORDING;
  if (target == COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW) return 1;
  result.features = COIN_RENDER_FEATURE_TRIANGLES |
                    COIN_RENDER_FEATURE_INDEXED_GEOMETRY |
                    COIN_RENDER_FEATURE_LINES_POINTS |
                    COIN_RENDER_FEATURE_TEXTURE_2D |
                    COIN_RENDER_FEATURE_LIGHTS |
                    COIN_RENDER_FEATURE_FOG |
                    COIN_RENDER_FEATURE_SORTED_ALPHA |
                    COIN_RENDER_FEATURE_COLOR_DEPTH;
  result.max_lights_per_draw = 8;
  result.max_texture_units = 1;
  result.max_scene_texture_depth = 8;
  result.max_scene_texture_bytes_per_apply = UINT64_C(64) * 1024 * 1024;
  setDiagnostic(result, "CPU recording backend does not run a GPU probe");
#endif

  const size_t copySize = legacyV1 ? capabilitiesV1Size : sizeof(result);
  std::memcpy(output, &result, copySize);
  return 0;
}
