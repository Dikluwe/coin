#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/rendering/CoinRenderCapabilities.h>
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuBackend.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#endif

#if defined(HAVE_COIN_BGFX)
#include "rendering/coinbgfx/CoinBgfxBackend.h"
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
const size_t capabilitiesV2Size = offsetof(CoinRenderCapabilities, known_hardware_facts);

void
setDiagnostic(CoinRenderCapabilities & result, const std::string & text)
{
  std::snprintf(result.diagnostic, sizeof(result.diagnostic), "%s", text.c_str());
}

#if defined(HAVE_COIN_BGFX)
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

void probeBgfx(uint32_t target, CoinRenderRenderer renderer, CoinRenderCapabilities& result) {
  CoinRenderTargetP probeTarget(SbVec2i32(1, 1));
  probeTarget.capabilityProbeOnly = true;
  probeTarget.options = CoinRenderOptions{};
  probeTarget.options.renderer = renderer;
  probeTarget.optionsDiagnostic.clear();
  probeTarget.depthReadbackEnabled = false;
  CoinBgfxBackend probeBackend;
  const CoinRenderBackendStatus status = probeBackend.prepare(probeTarget);
  if (status != CoinRenderBackendStatus::SUCCESS) {
    const std::string error = probeBackend.getLastError().empty() ?
      "BGFX runtime probe failed without a diagnostic" : probeBackend.getLastError();
    result.probe_status = status == CoinRenderBackendStatus::NOT_READY
                              ? COIN_RENDER_PROBE_UNAVAILABLE
                          : (status == CoinRenderBackendStatus::UNSUPPORTED &&
                             (error.find("API thread") != std::string::npos ||
                              error.find("view budget") != std::string::npos ||
                              error.find("share the active renderer") != std::string::npos))
                              ? COIN_RENDER_PROBE_BUSY
                              : COIN_RENDER_PROBE_ERROR;
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

  result.known_hardware_facts = COIN_RENDER_HARDWARE_RENDERER | COIN_RENDER_HARDWARE_ADAPTER_IDS |
                                COIN_RENDER_HARDWARE_FORMATS | COIN_RENDER_HARDWARE_LIMITS |
                                COIN_RENDER_HARDWARE_RUNTIME_FEATURES;
  result.known_formats = COIN_RENDER_KNOWN_RGBA8 | COIN_RENDER_KNOWN_D24S8 |
                         COIN_RENDER_KNOWN_D32F | COIN_RENDER_KNOWN_RGBA16F |
                         COIN_RENDER_KNOWN_R16F;
  result.available_mechanisms = probeBackend.getAvailableTransparencyMechanisms(
      target == COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW);
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

extern "C" int32_t coin_render_query_capabilities_for_renderer(uint32_t target,
                                                               CoinRenderRenderer renderer,
                                                               void* output, size_t output_size) {
  const bool legacyV1 = output_size == capabilitiesV1Size;
  const bool legacyV2 = output_size == capabilitiesV2Size;
  if (!output || (!legacyV1 && !legacyV2 && output_size < sizeof(CoinRenderCapabilities)))
    return 2;
  if (renderer != COIN_RENDER_RENDERER_UNKNOWN && renderer != COIN_RENDER_RENDERER_VULKAN &&
      renderer != COIN_RENDER_RENDERER_OPENGL && renderer != COIN_RENDER_RENDERER_D3D12 &&
      renderer != COIN_RENDER_RENDERER_METAL)
    return 2;
  const size_t copySize = legacyV1   ? capabilitiesV1Size
                          : legacyV2 ? capabilitiesV2Size
                                     : sizeof(CoinRenderCapabilities);
  if (target != COIN_RENDER_EXPERIMENTAL_OFFSCREEN &&
      target != COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW &&
      target != COIN_RENDER_EXPERIMENTAL_WAYLAND_WINDOW &&
      target != COIN_RENDER_EXPERIMENTAL_WIN32_WINDOW &&
      target != COIN_RENDER_EXPERIMENTAL_APPKIT_LAYER &&
      target != COIN_RENDER_EXPERIMENTAL_ANDROID_WINDOW) return 1;
#if !defined(__linux__)
  if (target == COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW ||
      target == COIN_RENDER_EXPERIMENTAL_WAYLAND_WINDOW) return 1;
#endif
#if !defined(_WIN32)
  if (target == COIN_RENDER_EXPERIMENTAL_WIN32_WINDOW) return 1;
#endif
#if !defined(__APPLE__)
  if (target == COIN_RENDER_EXPERIMENTAL_APPKIT_LAYER) return 1;
#endif
#if !defined(__ANDROID__)
  if (target == COIN_RENDER_EXPERIMENTAL_ANDROID_WINDOW) return 1;
#endif
#if !defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  if (target == COIN_RENDER_EXPERIMENTAL_ANDROID_WINDOW ||
      target == COIN_RENDER_EXPERIMENTAL_WAYLAND_WINDOW ||
      target == COIN_RENDER_EXPERIMENTAL_WIN32_WINDOW ||
      target == COIN_RENDER_EXPERIMENTAL_APPKIT_LAYER) return 1;
#endif

  CoinRenderCapabilities result{};
  result.struct_size = static_cast<uint32_t>(copySize);
  result.version = legacyV1 ? 1u : legacyV2 ? 2u : COIN_RENDER_CAPABILITIES_VERSION;
  result.target = target;
  result.probe_status = COIN_RENDER_PROBE_NOT_RUN;

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  result.backend = COIN_RENDER_EXPERIMENTAL_RUST;
  result.gpu_available = CoinWgpuBackend::isAvailable() ? 1u : 0u;
  result.probe_status = result.gpu_available ?
    COIN_RENDER_PROBE_AVAILABLE : COIN_RENDER_PROBE_UNAVAILABLE;
  if (result.gpu_available) {
    const std::string name = CoinWgpuBackend::getAdapterInfo();
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
                    COIN_RENDER_FEATURE_CLIP_PLANES |
                    COIN_RENDER_FEATURE_SORTED_ALPHA |
                    COIN_RENDER_FEATURE_COLOR_DEPTH;
  result.max_lights_per_draw = 8;
  result.max_texture_units = 8;
  if (target == COIN_RENDER_EXPERIMENTAL_OFFSCREEN) {
    result.features |= COIN_RENDER_FEATURE_ASYNC_READBACK |
                       COIN_RENDER_FEATURE_DIRECT_RTT;
    result.max_scene_texture_depth = 8;
    result.max_scene_texture_bytes_per_apply = UINT64_C(64) * 1024 * 1024;
  }
#elif defined(HAVE_COIN_DAWN) || defined(HAVE_COIN_WGPU_NATIVE)
  result.backend = COIN_RENDER_EXPERIMENTAL_NATIVE_SPIKE;
  result.probe_status = COIN_RENDER_PROBE_UNAVAILABLE;
  setDiagnostic(result, "Native WebGPU spike has no established runtime rendering profile");
  if (target == COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW) return 1;
#elif defined(HAVE_COIN_BGFX)
  result.backend = COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION;
  result.features = COIN_RENDER_FEATURE_TRIANGLES |
                    COIN_RENDER_FEATURE_INDEXED_GEOMETRY |
                    COIN_RENDER_FEATURE_LINES_POINTS |
                    COIN_RENDER_FEATURE_TEXTURE_2D |
                    COIN_RENDER_FEATURE_LIGHTS |
                    COIN_RENDER_FEATURE_FOG |
                    COIN_RENDER_FEATURE_CLIP_PLANES |
                    COIN_RENDER_FEATURE_SORTED_ALPHA;
  result.max_lights_per_draw = 8;
  result.max_texture_units = COIN_RENDER_MAX_TEXTURE_UNITS;
  if (target == COIN_RENDER_EXPERIMENTAL_OFFSCREEN)
    result.features |= COIN_RENDER_FEATURE_COLOR_DEPTH | COIN_RENDER_FEATURE_ASYNC_READBACK;
  probeBgfx(target, renderer, result);
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
  result.max_texture_units = 8;
  result.max_scene_texture_depth = 8;
  result.max_scene_texture_bytes_per_apply = UINT64_C(64) * 1024 * 1024;
  setDiagnostic(result, "CPU recording backend does not run a GPU probe");
#endif

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  result.implemented_mechanisms = COIN_RENDER_MECHANISM_OBJECT | COIN_RENDER_MECHANISM_PEELING | COIN_RENDER_MECHANISM_WEIGHTED_OIT;
  if (result.gpu_available) {
    CoinWgpuRuntimeCapabilities runtime{};
    const auto status = coin_wgpu_query_runtime_capabilities(&runtime, sizeof(runtime));
    if (status == COIN_WGPU_OK) {
      result.renderer = runtime.renderer;
      result.vendor_id = runtime.vendor_id;
      result.device_id = runtime.device_id;
      result.max_framebuffer_attachments = runtime.max_framebuffer_attachments;
      result.format_rgba8 = runtime.format_rgba8;
      result.format_d24s8 = runtime.format_d24s8;
      result.format_d32f = runtime.format_d32f;
      result.format_rgba16f = runtime.format_rgba16f;
      result.format_r16f = runtime.format_r16f;
      result.runtime_features = runtime.runtime_features;
      result.available_mechanisms = runtime.available_mechanisms;
      // Abstract Depth24PlusStencil8 does not establish a concrete D24S8 fact.
      result.known_hardware_facts = COIN_RENDER_HARDWARE_RENDERER |
                                    COIN_RENDER_HARDWARE_ADAPTER_IDS | COIN_RENDER_HARDWARE_LIMITS |
                                    COIN_RENDER_HARDWARE_RUNTIME_FEATURES;
      // FORMATS covers the reported concrete formats; D24S8 stays unknown (zero).
      result.known_hardware_facts |= COIN_RENDER_HARDWARE_FORMATS;
      result.known_formats = COIN_RENDER_KNOWN_RGBA8 | COIN_RENDER_KNOWN_D32F |
                             COIN_RENDER_KNOWN_RGBA16F | COIN_RENDER_KNOWN_R16F;
      if (renderer != COIN_RENDER_RENDERER_UNKNOWN && runtime.renderer != uint32_t(renderer)) {
        result.gpu_available = 0;
        result.probe_status = COIN_RENDER_PROBE_UNAVAILABLE;
        result.available_mechanisms = 0;
        setDiagnostic(
            result,
            "Requested renderer does not match the active wgpu adapter; no fallback was applied");
      }
    } else {
      result.gpu_available = 0;
      result.probe_status = COIN_RENDER_PROBE_ERROR;
      setDiagnostic(result, "Rust/wgpu runtime capability probe failed");
    }
  }
#elif defined(HAVE_COIN_BGFX)
  result.implemented_mechanisms = COIN_RENDER_MECHANISM_OBJECT | COIN_RENDER_MECHANISM_PEELING |
                                  COIN_RENDER_MECHANISM_WEIGHTED_OIT;
#elif !defined(HAVE_COIN_DAWN) && !defined(HAVE_COIN_WGPU_NATIVE)
  result.implemented_mechanisms = COIN_RENDER_MECHANISM_OBJECT | COIN_RENDER_MECHANISM_PEELING;
  result.available_mechanisms =
      renderer == COIN_RENDER_RENDERER_UNKNOWN ? result.implemented_mechanisms : 0;
#endif
  if (result.implemented_mechanisms & COIN_RENDER_MECHANISM_PEELING)
    result.max_peel_layers = 8;
  // Qualification is evidence for these bounded profiles and target paths.
  // It does not certify a new adapter/driver or a window without a real surface.
  if (target == COIN_RENDER_EXPERIMENTAL_OFFSCREEN &&
      (result.backend == COIN_RENDER_EXPERIMENTAL_RECORDING ||
       result.renderer == COIN_RENDER_RENDERER_VULKAN ||
       result.renderer == COIN_RENDER_RENDERER_OPENGL)) {
    result.qualified_profile_mechanisms = result.implemented_mechanisms;
    result.qualified_profiles = COIN_RENDER_PROFILE_P08_MULTITEXTURE |
                                COIN_RENDER_PROFILE_P09_TRANSPARENCY |
                                COIN_RENDER_PROFILE_P10_PEELING;
    if (result.backend == COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION)
      result.qualified_profiles |= COIN_RENDER_PROFILE_P10_BGFX_WEIGHTED_OIT;
  }
  std::memcpy(output, &result, copySize);
  return 0;
}

extern "C" int32_t coin_render_query_capabilities(uint32_t target, void* output,
                                                  size_t output_size) {
  CoinRenderRenderer renderer = COIN_RENDER_RENDERER_UNKNOWN;
#if defined(HAVE_COIN_BGFX)
  std::string diagnostic;
  renderer = CoinRenderDiagnosticShell::rendererOption(diagnostic);
  if (!diagnostic.empty())
    return 2;
#endif
  return coin_render_query_capabilities_for_renderer(target, renderer, output, output_size);
}

extern "C" CoinRenderSelection coin_render_select_mechanism(const CoinRenderCapabilities* caps,
                                                            uint64_t mechanism,
                                                            uint32_t require_qualified_profile) {
  if (!caps || caps->version < 3 || caps->struct_size < sizeof(CoinRenderCapabilities) ||
      require_qualified_profile > 1) {
    CoinRenderSelection result{};
    result.mechanism = mechanism;
    result.reason = COIN_RENDER_SELECTION_INVALID_REQUEST;
    return result;
  }
  const auto selection =
      coin_render_selection(mechanism, caps->implemented_mechanisms, caps->available_mechanisms,
                            caps->qualified_profile_mechanisms, require_qualified_profile != 0);
  if (selection.reason == COIN_RENDER_SELECTION_INVALID_REQUEST ||
      selection.reason == COIN_RENDER_SELECTION_NOT_IMPLEMENTED)
    return selection;
  if ((caps->implemented_mechanisms & mechanism) &&
      caps->backend != COIN_RENDER_EXPERIMENTAL_RECORDING &&
      (caps->probe_status == COIN_RENDER_PROBE_BUSY ||
       caps->probe_status == COIN_RENDER_PROBE_ERROR ||
       caps->probe_status == COIN_RENDER_PROBE_NOT_RUN)) {
    CoinRenderSelection result{};
    result.mechanism = mechanism;
    result.reason = COIN_RENDER_SELECTION_RUNTIME_NOT_READY;
    return result;
  }
  return selection;
}
