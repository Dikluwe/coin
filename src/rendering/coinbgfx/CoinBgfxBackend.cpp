#include "rendering/coinrender/CoinRenderTransformCore.h"
#include "rendering/coinrender/CoinRenderBumpMapCore.h"
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinbgfx/CoinBgfxBackend.h"
#include "rendering/coinrender/CoinRenderResourceCore.h"
#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include "rendering/coinbgfx/CoinBgfxProgramCache.h"
#include "rendering/coinbgfx/CoinBgfxShaderCompiler.h"
#include "rendering/coinbgfx/CoinBgfxProgramSelection.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderAlphaTestCore.h"

#include "coin_bgfx_fs_msaa_resolve_glsl.h"
#include "coin_bgfx_fs_msaa_resolve_spirv.h"
#include "coin_bgfx_fs_weighted_composite_msaa_glsl.h"
#include "coin_bgfx_fs_weighted_composite_msaa_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_msaa_resolve_dx11.h"
#include "coin_bgfx_fs_weighted_composite_msaa_dx11.h"
#endif
#include "coin_bgfx_fs_depth_readback_glsl.h"
#include "coin_bgfx_fs_depth_readback_spirv.h"
#include "coin_bgfx_fs_mip_area_glsl.h"
#include "coin_bgfx_fs_mip_area_spirv.h"
#include "coin_bgfx_cs_raster_image_glsl.h"
#include "coin_bgfx_cs_raster_image_spirv.h"
#include "coin_bgfx_fs_bump_receiver_portable_glsl.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_portable_glsl.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_bump_receiver_portable_spirv.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_portable_spirv.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_bump_receiver_portable_dx11.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_portable_dx11.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_portable_dx11.h"
#endif
#include "coin_bgfx_vs_bump_receiver_glsl.h"
#include "coin_bgfx_fs_bump_receiver_glsl.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_glsl.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_glsl.h"
#include "coin_bgfx_vs_bump_receiver_spirv.h"
#include "coin_bgfx_fs_bump_receiver_spirv.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_spirv.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_vs_bump_receiver_dx11.h"
#include "coin_bgfx_fs_bump_receiver_dx11.h"
#include "coin_bgfx_fs_bump_receiver_native_depth_dx11.h"
#include "coin_bgfx_fs_bump_receiver_sample_depth_dx11.h"
#endif
#include "coin_bgfx_vs_bump_glsl.h"
#include "coin_bgfx_fs_bump_glsl.h"
#include "coin_bgfx_fs_bump_native_depth_glsl.h"
#include "coin_bgfx_fs_bump_sample_depth_glsl.h"
#include "coin_bgfx_vs_bump_spirv.h"
#include "coin_bgfx_fs_bump_spirv.h"
#include "coin_bgfx_fs_bump_native_depth_spirv.h"
#include "coin_bgfx_fs_bump_sample_depth_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_vs_bump_dx11.h"
#include "coin_bgfx_fs_bump_dx11.h"
#include "coin_bgfx_fs_bump_native_depth_dx11.h"
#include "coin_bgfx_fs_bump_sample_depth_dx11.h"
#endif
#include "coin_bgfx_vs_glsl.h"
#include "coin_bgfx_fs_glsl.h"
#include "coin_bgfx_vs_spirv.h"
#include "coin_bgfx_fs_spirv.h"
#include "coin_bgfx_fs_solid_color_glsl.h"
#include "coin_bgfx_fs_texture2d_native_depth_glsl.h"
#include "coin_bgfx_fs_texture2d_native_depth_spirv.h"
#include "coin_bgfx_fs_texture2d_native_depth_portable_glsl.h"
#include "coin_bgfx_fs_texture2d_native_depth_portable_spirv.h"
#include "coin_bgfx_fs_texture2d_sample_depth_glsl.h"
#include "coin_bgfx_fs_texture2d_sample_depth_spirv.h"
#include "coin_bgfx_fs_texture2d_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_texture2d_sample_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_texture2d_native_depth_dx11.h"
#include "coin_bgfx_fs_texture2d_native_depth_portable_dx11.h"
#include "coin_bgfx_fs_texture2d_sample_depth_dx11.h"
#include "coin_bgfx_fs_texture2d_sample_depth_portable_dx11.h"
#endif
#include "coin_bgfx_fs_texture2d_glsl.h"
#include "coin_bgfx_fs_texture2d_portable_glsl.h"
#include "coin_bgfx_fs_solid_color_spirv.h"
#include "coin_bgfx_fs_texture2d_spirv.h"
#include "coin_bgfx_fs_texture2d_portable_spirv.h"
#include "coin_bgfx_vs_instanced_color_glsl.h"
#include "coin_bgfx_vs_instanced_color_spirv.h"
#include "coin_bgfx_fs_instanced_color_glsl.h"
#include "coin_bgfx_fs_instanced_color_spirv.h"
#include "coin_bgfx_fs_peel_next_msaa_glsl.h"
#include "coin_bgfx_fs_peel_next_msaa_spirv.h"
#include "coin_bgfx_fs_peel_msaa_composite_glsl.h"
#include "coin_bgfx_fs_peel_msaa_composite_spirv.h"
#include "coin_bgfx_fs_shadow_peel_msaa_glsl.h"
#include "coin_bgfx_fs_shadow_peel_msaa_spirv.h"
#include "coin_bgfx_fs_peel_next_msaa_portable_glsl.h"
#include "coin_bgfx_fs_peel_next_msaa_portable_spirv.h"
#include "coin_bgfx_fs_shadow_peel_msaa_portable_glsl.h"
#include "coin_bgfx_fs_shadow_peel_msaa_portable_spirv.h"
#include "coin_bgfx_fs_peel_next_glsl.h"
#include "coin_bgfx_fs_peel_next_spirv.h"
#include "coin_bgfx_fs_composite_glsl.h"
#include "coin_bgfx_fs_composite_spirv.h"
#include "coin_bgfx_fs_multipass_sum_glsl.h"
#include "coin_bgfx_fs_multipass_sum_spirv.h"
#include "coin_bgfx_fs_multipass_mean_glsl.h"
#include "coin_bgfx_fs_multipass_mean_spirv.h"
#include "rendering/coinrender/CoinRenderMultipassCore.h"
#include "coin_bgfx_fs_composite_depth_glsl.h"
#include "coin_bgfx_fs_composite_depth_spirv.h"
#include "coin_bgfx_fs_weighted_oit_glsl.h"
#include "coin_bgfx_fs_weighted_oit_spirv.h"
#include "coin_bgfx_fs_weighted_composite_glsl.h"
#include "coin_bgfx_fs_weighted_composite_spirv.h"
#include "coin_bgfx_vs_shadow_moments_glsl.h"
#include "coin_bgfx_vs_shadow_moments_spirv.h"
#include "coin_bgfx_fs_shadow_moments_glsl.h"
#include "coin_bgfx_fs_shadow_moments_spirv.h"
#include "coin_bgfx_vs_shadow_receiver_glsl.h"
#include "coin_bgfx_vs_shadow_receiver_spirv.h"
#include "coin_bgfx_fs_shadow_receiver_glsl.h"
#include "coin_bgfx_fs_shadow_receiver_spirv.h"
#include "coin_bgfx_fs_shadow_receiver4_glsl.h"
#include "coin_bgfx_fs_shadow_receiver4_spirv.h"
#include "coin_bgfx_fs_shadow_receiver8_glsl.h"
#include "coin_bgfx_fs_shadow_receiver8_spirv.h"
#include "coin_bgfx_fs_shadow_peel_glsl.h"
#include "coin_bgfx_fs_shadow_peel_spirv.h"
#include "coin_bgfx_fs_shadow_oit_glsl.h"
#include "coin_bgfx_fs_shadow_oit_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_multitexture_peel_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_portable_dx11.h"
#endif
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_peel_msaa_portable_dx11.h"
#endif
#include "coin_bgfx_fs_shadow_multitexture_oit_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_multitexture_oit_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_portable_dx11.h"
#endif
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_oit_sample_depth_portable_dx11.h"
#endif
#include "coin_bgfx_vs_shadow_multitexture_glsl.h"
#include "coin_bgfx_vs_shadow_multitexture_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_portable_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_portable_spirv.h"
#include "coin_bgfx_fs_shadow_multitexture_portable_glsl.h"
#include "coin_bgfx_fs_shadow_multitexture_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_vs_shadow_multitexture_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_native_depth_portable_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_sample_depth_portable_dx11.h"
#include "coin_bgfx_fs_shadow_multitexture_portable_dx11.h"
#endif
#include "coin_bgfx_fs_instanced_sample_depth_glsl.h"
#include "coin_bgfx_fs_instanced_sample_depth_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_instanced_sample_depth_dx11.h"
#endif
#include "coin_bgfx_fs_surface_native_depth_glsl.h"
#include "coin_bgfx_fs_surface_native_depth_portable_glsl.h"
#include "coin_bgfx_fs_surface_native_depth_spirv.h"
#include "coin_bgfx_fs_surface_native_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_surface_native_depth_dx11.h"
#include "coin_bgfx_fs_surface_native_depth_portable_dx11.h"
#endif
#include "coin_bgfx_fs_surface_sample_depth_glsl.h"
#include "coin_bgfx_fs_surface_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_surface_sample_depth_spirv.h"
#include "coin_bgfx_fs_surface_sample_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_surface_sample_depth_dx11.h"
#include "coin_bgfx_fs_surface_sample_depth_portable_dx11.h"
#endif
#include "coin_bgfx_fs_shadow_native_depth_glsl.h"
#include "coin_bgfx_fs_shadow_native_depth_portable_glsl.h"
#include "coin_bgfx_fs_shadow_native_depth_spirv.h"
#include "coin_bgfx_fs_shadow_native_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_native_depth_dx11.h"
#include "coin_bgfx_fs_shadow_native_depth_portable_dx11.h"
#endif
#include "coin_bgfx_fs_shadow_sample_depth_glsl.h"
#include "coin_bgfx_fs_shadow_sample_depth_portable_glsl.h"
#include "coin_bgfx_fs_shadow_sample_depth_spirv.h"
#include "coin_bgfx_fs_shadow_sample_depth_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_shadow_sample_depth_dx11.h"
#include "coin_bgfx_fs_shadow_sample_depth_portable_dx11.h"
#endif
#include "coin_bgfx_fs_instanced_native_depth_glsl.h"
#include "coin_bgfx_fs_instanced_native_depth_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_instanced_native_depth_dx11.h"
#endif
#include "rendering/coinrender/CoinRenderShadowCore.h"

#include "coin_bgfx_fs_portable_glsl.h"
#include "coin_bgfx_fs_peel_next_portable_glsl.h"
#include "coin_bgfx_fs_weighted_oit_portable_glsl.h"
#include "coin_bgfx_fs_shadow_receiver_portable_glsl.h"
#include "coin_bgfx_fs_shadow_receiver4_portable_glsl.h"
#include "coin_bgfx_fs_shadow_receiver8_portable_glsl.h"
#include "coin_bgfx_fs_shadow_peel_portable_glsl.h"
#include "coin_bgfx_fs_shadow_oit_portable_glsl.h"
#include "coin_bgfx_fs_portable_spirv.h"
#include "coin_bgfx_fs_peel_next_portable_spirv.h"
#include "coin_bgfx_fs_weighted_oit_portable_spirv.h"
#include "coin_bgfx_fs_shadow_receiver_portable_spirv.h"
#include "coin_bgfx_fs_shadow_receiver4_portable_spirv.h"
#include "coin_bgfx_fs_shadow_receiver8_portable_spirv.h"
#include "coin_bgfx_fs_shadow_peel_portable_spirv.h"
#include "coin_bgfx_fs_shadow_oit_portable_spirv.h"
#ifdef _WIN32
#include "coin_bgfx_fs_portable_dx11.h"
#include "coin_bgfx_fs_peel_next_portable_dx11.h"
#include "coin_bgfx_fs_weighted_oit_portable_dx11.h"
#include "coin_bgfx_fs_shadow_receiver_portable_dx11.h"
#include "coin_bgfx_fs_shadow_receiver4_portable_dx11.h"
#include "coin_bgfx_fs_shadow_receiver8_portable_dx11.h"
#include "coin_bgfx_fs_shadow_peel_portable_dx11.h"
#include "coin_bgfx_fs_shadow_oit_portable_dx11.h"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <map>
#include <new>
#ifndef _WIN32
#include <X11/Xlib.h>
#include "rendering/coinrender/CoinRenderX11Wsi.h"
#include <dlfcn.h>
#endif
#ifdef _WIN32
#include <d3d12.h>
#include "coin_bgfx_fs_peel_next_msaa_dx11.h"
#include "coin_bgfx_fs_peel_msaa_composite_dx11.h"
#include "coin_bgfx_fs_shadow_peel_msaa_dx11.h"
#include "coin_bgfx_fs_peel_next_msaa_portable_dx11.h"
#include "coin_bgfx_fs_shadow_peel_msaa_portable_dx11.h"
#include "coin_bgfx_fs_depth_readback_dx11.h"
#include "coin_bgfx_fs_mip_area_dx11.h"
#include "coin_bgfx_cs_raster_image_dx11.h"
#include "coin_bgfx_vs_dx11.h"
#include "coin_bgfx_fs_dx11.h"
#include "coin_bgfx_fs_solid_color_dx11.h"
#include "coin_bgfx_fs_texture2d_dx11.h"
#include "coin_bgfx_fs_texture2d_portable_dx11.h"
#include "coin_bgfx_vs_instanced_color_dx11.h"
#include "coin_bgfx_fs_instanced_color_dx11.h"
#include "coin_bgfx_fs_peel_next_dx11.h"
#include "coin_bgfx_fs_composite_dx11.h"
#include "coin_bgfx_fs_multipass_sum_dx11.h"
#include "coin_bgfx_fs_multipass_mean_dx11.h"
#include "coin_bgfx_fs_composite_depth_dx11.h"
#include "coin_bgfx_fs_weighted_oit_dx11.h"
#include "coin_bgfx_fs_weighted_composite_dx11.h"
#include "coin_bgfx_vs_shadow_moments_dx11.h"
#include "coin_bgfx_fs_shadow_moments_dx11.h"
#include "coin_bgfx_vs_shadow_receiver_dx11.h"
#include "coin_bgfx_fs_shadow_receiver_dx11.h"
#include "coin_bgfx_fs_shadow_receiver4_dx11.h"
#include "coin_bgfx_fs_shadow_receiver8_dx11.h"
#include "coin_bgfx_fs_shadow_peel_dx11.h"
#include "coin_bgfx_fs_shadow_oit_dx11.h"
#endif

#include <utility>

namespace {
// Observation only: cache counts are API-thread owned. Runtime reference counts
// are supplied only after its existing mutex has been acquired; -1 is unknown.
void bgfxTraceLifecycle(const void *backend, const char *stage, size_t programs,
                        unsigned uniforms, int64_t references = -1)
{
  if (!CoinRenderDiagnosticShell::phaseTracingEnabled()) return;
  const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  std::fprintf(stderr,
      "COIN_RENDER_PHASE bgfx_lifecycle backend=%p stage=%s time_ns=%lld portable_programs=%zu portable_uniforms=%u runtime_refs=%lld\n",
      backend, stage, static_cast<long long>(timestamp), programs, uniforms,
      static_cast<long long>(references));
  std::fflush(stderr);
}

bgfx::TextureFormat::Enum coinBgfxTextureFormat(CoinRenderTextureFormat f) {
  if(f==CoinRenderTextureFormat::RGBA16_FLOAT)return bgfx::TextureFormat::RGBA16F;
  if(CoinRenderTextureFormatCore::compressed(f))return bgfx::TextureFormat::BC3;
  return bgfx::TextureFormat::RGBA8;
}
uint64_t coinBgfxTextureFlags(CoinRenderTextureFormat f) {
  return CoinRenderTextureFormatCore::srgb(f)?BGFX_TEXTURE_SRGB:BGFX_TEXTURE_NONE;
}

// Isolated view blocks prevent per-window state from aliasing. API calls stay
// on one thread; BGFX owns its render worker.
constexpr bgfx::ViewId targetViewCount = 16;
struct SharedBgfxRuntime;
SharedBgfxRuntime & sharedRuntime();

template <typename Vertex>
void releaseUploadVertices(void *, void * owner)
{
  // BGFX may invoke this on its render worker, including during shutdown.
  // The owner is independent of the target and its reusable plan.
  delete static_cast<std::vector<Vertex> *>(owner);
}

template <typename Vertex>
const bgfx::Memory * uploadVertexVector(std::vector<Vertex> & vertices,
                                       size_t & uploadedCount, bool transfer)
{
  const uint32_t bytes = static_cast<uint32_t>(
    vertices.size() * sizeof(Vertex));
  // These vertices already exceed the CPU geometry retention budget. Transfer
  // their allocation instead of copying it only to discard the original.
  if (transfer || bytes > 32u * 1024u * 1024u) {
    auto * owner = new (std::nothrow) std::vector<Vertex>;
    if (owner) {
      uploadedCount = vertices.size();
      owner->swap(vertices);
      return bgfx::makeRef(owner->data(), bytes, releaseUploadVertices<Vertex>, owner);
    }
  }
  return bgfx::copy(vertices.data(), bytes);
}

const bgfx::Memory * uploadVertices(CoinBgfxPlan & plan)
{
  if (plan.usesInstancing)
    return uploadVertexVector(plan.instancedVertices, plan.uploadedVertexCount, false);
  if (plan.usesCompactVertices)
    return uploadVertexVector(plan.packedVertices, plan.uploadedVertexCount, true);
  return uploadVertexVector(plan.vertices, plan.uploadedVertexCount, false);
}

bool bgfxShadowBatchSupported(const CoinRenderFramePlan & frame,
                              const CoinRenderShadowPlan & plan,
                              std::string & diagnostic)
{
  if (plan.passes.size() > 8) {
    diagnostic = "BGFX shadow receiver supports at most eight maps";
    return false;
  }
  for (const auto & draw : frame.draws) {
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "BGFX shadow draw references an invalid render-state slot";
      return false;
    }
    const auto & state = frame.renderStates[draw.renderStateSlot];
    if (!coin_render_alpha_test_valid(state.alphaTestFunction, state.alphaTestReference)) {
      diagnostic = "BGFX shadow draw has invalid alpha comparison state";
      return false;
    }
  }
  return true;
}

class CoinBgfxCallback : public bgfx::CallbackI {
public:
  explicit CoinBgfxCallback(bool openGl) : fatalCode(-1) {
    this->fatalMessage[0] = 0;
    this->glAdapterDiagnostics = openGl && std::getenv("COIN_BGFX_TRACE_GL_ADAPTER");
    const char * disabled = std::getenv("COIN_BGFX_DISABLE_PROGRAM_CACHE");
    this->programCacheEnabled = openGl && !(disabled && std::strcmp(disabled, "1") == 0);
  }

  void fatal(const char * filePath, uint16_t line, bgfx::Fatal::Enum code,
             const char * message) override
  {
    {
      std::lock_guard<std::mutex> guard(this->messageMutex);
      std::snprintf(this->fatalMessage, sizeof(this->fatalMessage),
        "BGFX fatal %d at %s:%u: %s", static_cast<int>(code),
        filePath ? filePath : "unknown", static_cast<unsigned int>(line),
        message ? message : "no diagnostic");
    }
    this->fatalCode.store(static_cast<int>(code), std::memory_order_release);
  }

  void traceVargs(const char * filePath, uint16_t line, const char * format,
                  va_list args) override
  {
    std::fprintf(stderr, "BGFX %s:%u: ", filePath ? filePath : "unknown",
                 static_cast<unsigned int>(line));
    std::vfprintf(stderr, format, args);
  }

  void profilerBegin(const char *, uint32_t, const char *, uint16_t) override {}
  void profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t) override {}
  void profilerEnd() override {}
  void traceGlAdapterOnce() {
#ifndef _WIN32
    if (!this->glAdapterDiagnostics || this->glAdapterPrinted.exchange(true)) return;
    // Program-cache callbacks run inside renderer_gl program creation with its
    // context current, including surfaceless/offscreen EGL. Qt GLX inventory
    // may describe another provider. This diagnostic only reads driver strings.
    void* library = dlopen("libOpenGL.so.0", RTLD_LAZY | RTLD_LOCAL);
    bool printed = false;
    if (library) {
      using GetString = const unsigned char* (*)(unsigned int);
      const auto getString = reinterpret_cast<GetString>(dlsym(library, "glGetString"));
      if (getString) {
        const auto* vendor = getString(0x1f00);
        const auto* renderer = getString(0x1f01);
        const auto* version = getString(0x1f02);
        if (vendor && renderer && version) {
          std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_gl_adapter vendor=%s renderer=%s version=%s\n",
                       vendor, renderer, version);
          printed = true;
        }
      }
      dlclose(library);
    }
    if (!printed) this->glAdapterPrinted.store(false);
#endif
  }
  uint32_t cacheReadSize(uint64_t key) override {
    this->traceGlAdapterOnce();
    auto * cache = this->programCacheEnabled ? programCache() : nullptr;
    return cache ? cache->size(key) : 0;
  }
  bool cacheRead(uint64_t key, void * data, uint32_t bytes) override {
    auto * cache = this->programCacheEnabled ? programCache() : nullptr;
    if (!cache || !cache->read(key, data, bytes)) return false;
    ++this->programCacheHits;
    return true;
  }
  void cacheWrite(uint64_t key, const void * data, uint32_t bytes) override {
    this->traceGlAdapterOnce();
    auto * cache = this->programCacheEnabled ? programCache() : nullptr;
    if (cache && cache->write(key, data, bytes)) ++this->programCacheWrites;
  }
  void traceProgramCache() const {
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_program_cache enabled=%d hits=%u writes=%u\n",
      this->programCacheEnabled ? 1 : 0, this->programCacheHits.load(), this->programCacheWrites.load());
  }
  void screenShot(const char * name, uint32_t width, uint32_t height,
                  uint32_t pitch, bgfx::TextureFormat::Enum format,
                  const void * pixels, uint32_t bytes, bool bottomLeft) override
  {
    std::lock_guard<std::mutex> guard(this->screenshotMutex);
    if (!name || !this->screenshotPending || name != this->screenshotName) return;
    const bool validFormat = format == bgfx::TextureFormat::BGRA8 ||
      format == bgfx::TextureFormat::RGBA8;
    // D3D12 footprints may omit trailing padding after the final row.
    const bool validRows = height > 0 &&
      uint64_t(pitch) * (height - 1u) + uint64_t(width) * 4u <= bytes;
    if (validFormat && pixels && width == this->screenshotWidth &&
        height == this->screenshotHeight && pitch >= width * 4u &&
        validRows) {
      try {
        this->screenshotPixels.resize(size_t(width) * height * 4u);
        const uint8_t * source = static_cast<const uint8_t *>(pixels);
        for (uint32_t y = 0; y < height; ++y) {
          const uint8_t * row = source + size_t(bottomLeft ? height - 1 - y : y) * pitch;
          uint8_t * output = this->screenshotPixels.data() + size_t(y) * width * 4u;
          for (uint32_t x = 0; x < width; ++x) {
            const uint8_t * pixel = row + size_t(x) * 4u;
            output[x * 4u] = pixel[format == bgfx::TextureFormat::BGRA8 ? 2 : 0];
            output[x * 4u + 1] = pixel[1];
            output[x * 4u + 2] = pixel[format == bgfx::TextureFormat::BGRA8 ? 0 : 2];
            output[x * 4u + 3] = pixel[3];
          }
        }
      } catch (const std::bad_alloc &) { this->screenshotPixels.clear(); }
    }
    if (this->screenshotPixels.empty()) {
      this->screenshotError = "BGFX window screenshot incomplete: actual=" + std::to_string(width) + "x" + std::to_string(height) +
        " expected=" + std::to_string(this->screenshotWidth) + "x" + std::to_string(this->screenshotHeight) +
        " format=" + std::to_string(format) + " pitch=" + std::to_string(pitch) + " bytes=" + std::to_string(bytes);
    }
    this->screenshotPending = false;
    this->screenshotReady = true;
    this->screenshotCondition.notify_all();
  }

  std::string beginScreenshot(uint32_t width, uint32_t height)
  {
    std::lock_guard<std::mutex> guard(this->screenshotMutex);
    this->screenshotWidth = width; this->screenshotHeight = height;
    this->screenshotPixels.clear();this->screenshotError.clear();
    this->screenshotReady = false; this->screenshotPending = true;
    this->screenshotName = "coin-window-rgba-" + std::to_string(++this->screenshotSerial);
    return this->screenshotName;
  }

  void cancelScreenshot()
  {
    std::lock_guard<std::mutex> guard(this->screenshotMutex);
    this->screenshotPending = false;
  }

  bool takeScreenshot(std::vector<uint8_t> & output, std::string & error)
  {
    std::unique_lock<std::mutex> guard(this->screenshotMutex);
    if (!this->screenshotCondition.wait_for(guard, std::chrono::seconds(5),
          [this] { return this->screenshotReady; })) {
      this->screenshotPending = false;error = "BGFX window screenshot timed out";
      return false;
    }
    if (this->screenshotPixels.empty()) {error = this->screenshotError; return false;}
    output.swap(this->screenshotPixels);
    return true;
  }
  void captureBegin(uint32_t, uint32_t, uint32_t,
                    bgfx::TextureFormat::Enum, bool) override {}
  void captureEnd() override {}
  void captureFrame(const void *, uint32_t) override {}

  bool failed() const
  {
    return this->fatalCode.load(std::memory_order_acquire) >= 0;
  }

  bool deviceLost() const
  {
    return this->fatalCode.load(std::memory_order_acquire) ==
      static_cast<int>(bgfx::Fatal::DeviceLost);
  }

  std::string diagnostic() const
  {
    std::lock_guard<std::mutex> guard(this->messageMutex);
    return this->fatalMessage;
  }

  void inject(bgfx::Fatal::Enum code, const char * message)
  {
    this->fatal("Coin fault injection", 0, code, message);
  }

private:
  static CoinBgfxProgramCache * programCache() {
    // Like the shared BGFX runtime, this bounded cache lives for the process.
    // It never holds renderer handles, callbacks, targets or disk files.
    static CoinBgfxProgramCache * cache = new (std::nothrow) CoinBgfxProgramCache;
    return cache;
  }
  bool programCacheEnabled = false;
  std::atomic<unsigned int> programCacheHits{0}, programCacheWrites{0};
  std::atomic<int> fatalCode;
  mutable std::mutex messageMutex;
  char fatalMessage[512];
  std::mutex screenshotMutex;
  std::condition_variable screenshotCondition;
  std::vector<uint8_t> screenshotPixels;
  uint32_t screenshotWidth = 0;
  uint32_t screenshotHeight = 0;
  bool screenshotPending = false;
  bool glAdapterDiagnostics = false;
  std::atomic<bool> glAdapterPrinted{false};
  bool screenshotReady = false;
  uint64_t screenshotSerial = 0;
  std::string screenshotName;
  std::string screenshotError;
};

struct BgfxPortableProgram {
  size_t pins = 0;
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
  size_t bytes = 0;
  uint64_t used = 0;
};
struct BgfxPortableProgramCache {
  // API-thread owned, like all other BGFX resources. No target, scene or
  // runtime reference is retained by the cache. Counts alone are atomic for
  // opt-in lifecycle observations from an uninitialized probe's destructor.
  std::map<std::string, BgfxPortableProgram> programs;
  bgfx::UniformHandle uniforms[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
  bgfx::UniformHandle probeControl = BGFX_INVALID_HANDLE;
  size_t bytes = 0;
  uint64_t use = 0;
  std::atomic<size_t> programCount{0};
  std::atomic<unsigned> uniformCount{0};

  void clear(bool releaseHandles) {
    if (releaseHandles)
      for (const auto &entry : this->programs)
        if (bgfx::isValid(entry.second.program)) bgfx::destroy(entry.second.program);
    this->programs.clear();
    for (auto &uniform : this->uniforms) {
      if (releaseHandles && bgfx::isValid(uniform)) bgfx::destroy(uniform);
      uniform = BGFX_INVALID_HANDLE;
    }
    if (releaseHandles && bgfx::isValid(this->probeControl)) bgfx::destroy(this->probeControl);
    this->probeControl = BGFX_INVALID_HANDLE;
    this->bytes = 0;
    this->use = 0;
    this->programCount.store(0, std::memory_order_relaxed);
    this->uniformCount.store(0, std::memory_order_relaxed);
  }
};

struct SharedBgfxRuntime {
  std::mutex mutex;
  std::thread::id apiThread;
  std::shared_ptr<bgfx::CallbackI> callback;
  bgfx::RendererType::Enum renderer = bgfx::RendererType::Count;
  bgfx::TextureFormat::Enum backbufferColorFormat = bgfx::TextureFormat::Count;
  unsigned int references = 0;
  BgfxPortableProgramCache portable;
#if defined(__linux__) && !defined(__ANDROID__)
  // eglInitialize does not acquire a reference to an existing EGLDisplay.
  // Give the GL worker its own native connection so eglTerminate cannot
  // invalidate the application's contexts on its caller-owned connection.
  std::unique_ptr<Display, int (*)(Display *)> openGlDisplay{nullptr, XCloseDisplay};
#endif
  void releaseNativeDisplay() {
#if defined(__linux__) && !defined(__ANDROID__)
    // The render worker and all EGL objects must already be gone.
    this->openGlDisplay.reset();
#endif
  }
  uint64_t generation = 0;
  // Written only on the shared API thread. Program handles are allocated
  // immediately, while native GL compilation executes later on the worker.
  uint32_t weightedSamplesVerified = 0;
  uint32_t weightedSamplesRejected = 0;
  uint32_t peelingSamplesVerified = 0;
  uint32_t peelingSamplesRejected = 0;
  uint64_t programCommandsQueued = 0;
  uint64_t programCommandsMaterialized = 0;
  std::vector<bool> viewBlocks;
};

SharedBgfxRuntime & sharedRuntime()
{
  // Process lifetime: even a target destroyed on the wrong thread cannot leave
  // BGFX holding a dangling callback. Such targets diagnose the contract error.
  static SharedBgfxRuntime * runtime = new SharedBgfxRuntime;
  return *runtime;
}

bool consumeTestFault(const char * name)
{
  const char * value = std::getenv(name);
  if (value == nullptr || std::strcmp(value, "1") != 0) return false;
  // Consuming the flag makes recovery
  // deterministic: the replacement backend must not inherit the same fault.
#ifdef _WIN32
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
  return true;
}

// Process-local rather than backend-local: STAGED RTT creates a temporary
// backend for each face. Only successful logical scene submissions count.
struct SubmitCountdownFault {
  std::mutex mutex;
  bool armed=false;
  uint64_t remaining=0;
};
SubmitCountdownFault & submitCountdownFault() {
  static SubmitCountdownFault fault;return fault;
}
bool injectOutOfMemoryAfterSubmits() {
  auto & fault=submitCountdownFault();std::lock_guard<std::mutex> guard(fault.mutex);
  const char * name="COIN_BGFX_TEST_OUT_OF_MEMORY_AFTER_SUBMITS";
  const char * value=std::getenv(name);
  if(value) {
    // Consume even malformed/overflowed values. A new setting replaces the
    // previous arm; it cannot accidentally become a persistent fail-next flag.
    bool valid=*value!='\0';uint64_t count=0;
    for(const unsigned char * digit=reinterpret_cast<const unsigned char *>(value);*digit;++digit) {
      if(*digit<'0'||*digit>'9' || count>(std::numeric_limits<uint64_t>::max()-uint64_t(*digit-'0'))/10) {
        valid=false;break;
      }
      count=count*10+uint64_t(*digit-'0');
    }
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
    fault.armed=valid;fault.remaining=valid?count:0;
  }
  if(!fault.armed || fault.remaining)return false;
  fault.armed=false;return true;
}
void logicalSubmitSucceeded() {
  auto & fault=submitCountdownFault();std::lock_guard<std::mutex> guard(fault.mutex);
  if(fault.armed && fault.remaining)--fault.remaining;
}

uint64_t drawState(const CoinBgfxDraw & draw)
{
  const uint8_t mask = draw.colorWrite ? draw.colorWriteMask : 0;
  uint64_t state = (mask & 1 ? BGFX_STATE_WRITE_R : 0) |
                   (mask & 2 ? BGFX_STATE_WRITE_G : 0) |
                   (mask & 4 ? BGFX_STATE_WRITE_B : 0) |
                   (mask & 8 ? BGFX_STATE_WRITE_A : 0);
  if (draw.depthTest) {
    switch (draw.depthFunction) {
    case CoinRenderDepthFunction::NEVER: state |= BGFX_STATE_DEPTH_TEST_NEVER; break;
    case CoinRenderDepthFunction::ALWAYS: state |= BGFX_STATE_DEPTH_TEST_ALWAYS; break;
    case CoinRenderDepthFunction::LEQUAL: state |= BGFX_STATE_DEPTH_TEST_LEQUAL; break;
    case CoinRenderDepthFunction::EQUAL: state |= BGFX_STATE_DEPTH_TEST_EQUAL; break;
    case CoinRenderDepthFunction::GEQUAL: state |= BGFX_STATE_DEPTH_TEST_GEQUAL; break;
    case CoinRenderDepthFunction::GREATER: state |= BGFX_STATE_DEPTH_TEST_GREATER; break;
    case CoinRenderDepthFunction::NOTEQUAL: state |= BGFX_STATE_DEPTH_TEST_NOTEQUAL; break;
    case CoinRenderDepthFunction::LESS: default: state |= BGFX_STATE_DEPTH_TEST_LESS; break;
    }
  }
  if (draw.depthTest && draw.depthWrite) state |= BGFX_STATE_WRITE_Z;
  if (draw.blend && draw.legacyBlendAlpha && !draw.additive) {
    state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
  } else if (draw.blend && draw.additive) {
    state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
  } else if (draw.blend) {
    state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
      BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
      BGFX_STATE_BLEND_INV_SRC_ALPHA);
  }
  // Coin.s frontFace denotes the visible winding; BGFX state denotes the
  // winding to discard. BGFX accounts for each renderer's target origin.
  if (draw.cullMode == CoinRenderCullMode::BACK) {
    state |= draw.frontFace == CoinRenderFrontFace::CCW ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
  } else if (draw.cullMode == CoinRenderCullMode::FRONT) {
    state |= draw.frontFace == CoinRenderFrontFace::CCW ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
  }
  return state;
}

struct LogicalDrawStats {
  uint32_t opaqueDraws = 0;
  uint32_t transparentDraws = 0;
  uint32_t pipelineChanges = 0;
  uint32_t materialChanges = 0;
  uint32_t textureChanges = 0;
  uint32_t lightingChanges = 0;
  bool opaqueOrderChanged = false;
};

bool samePipelineState(const CoinBgfxDraw & lhs,
                       const CoinBgfxDraw & rhs)
{
  return drawState(lhs) == drawState(rhs) &&
    lhs.alphaTestFunction == rhs.alphaTestFunction &&
    std::memcmp(&lhs.alphaTestReference, &rhs.alphaTestReference, sizeof(lhs.alphaTestReference)) == 0 &&
    std::memcmp(lhs.viewport, rhs.viewport, sizeof(lhs.viewport)) == 0;
}

bool sameTextureState(const CoinBgfxDraw & lhs,
                      const CoinBgfxDraw & rhs)
{
  return std::memcmp(lhs.textureCombines, rhs.textureCombines, sizeof(lhs.textureCombines)) == 0 &&
    lhs.textureProjection == rhs.textureProjection &&
    std::memcmp(lhs.extraTextures, rhs.extraTextures, sizeof(lhs.extraTextures)) == 0 &&
    lhs.hasTexture == rhs.hasTexture &&
    lhs.textureSlot == rhs.textureSlot && lhs.textureModel == rhs.textureModel &&
    lhs.wrapS == rhs.wrapS && lhs.wrapT == rhs.wrapT && lhs.filter == rhs.filter && lhs.maxAnisotropy == rhs.maxAnisotropy &&
    std::memcmp(lhs.textureBlendColor, rhs.textureBlendColor,
                sizeof(lhs.textureBlendColor)) == 0;
}

bool sameLightingState(const CoinBgfxDraw & lhs,
                       const CoinBgfxDraw & rhs)
{
  return std::memcmp(lhs.fogColorMode, rhs.fogColorMode, sizeof(lhs.fogColorMode)) == 0 &&
    std::memcmp(lhs.fogRange, rhs.fogRange, sizeof(lhs.fogRange)) == 0 &&
    std::memcmp(lhs.ambientLight, rhs.ambientLight,
                     sizeof(lhs.ambientLight)) == 0 &&
    std::memcmp(lhs.lightCount, rhs.lightCount, sizeof(lhs.lightCount)) == 0 &&
    std::memcmp(lhs.lightPositionType, rhs.lightPositionType,
                sizeof(lhs.lightPositionType)) == 0 &&
    std::memcmp(lhs.lightDirectionCutoff, rhs.lightDirectionCutoff,
                sizeof(lhs.lightDirectionCutoff)) == 0 &&
    std::memcmp(lhs.lightColorIntensity, rhs.lightColorIntensity,
                sizeof(lhs.lightColorIntensity)) == 0 &&
    std::memcmp(lhs.lightAmbientColor, rhs.lightAmbientColor,
                sizeof(lhs.lightAmbientColor)) == 0 &&
    std::memcmp(lhs.lightAttenuationDrop, rhs.lightAttenuationDrop,
                sizeof(lhs.lightAttenuationDrop)) == 0;
}

LogicalDrawStats logicalDrawStats(
  const std::vector<CoinBgfxDraw> & original,
  const std::vector<CoinBgfxDraw> & encoded)
{
  LogicalDrawStats stats;
  std::vector<uint32_t> originalOpaque;
  std::vector<uint32_t> encodedOpaque;
  const CoinBgfxDraw * previous = nullptr;
  for (const CoinBgfxDraw & draw : original) {
    if (!draw.blend) originalOpaque.push_back(draw.firstIndex);
  }
  for (const CoinBgfxDraw & draw : encoded) {
    if (draw.blend) ++stats.transparentDraws;
    else {
      ++stats.opaqueDraws;
      encodedOpaque.push_back(draw.firstIndex);
    }
    if (!previous || !samePipelineState(*previous, draw)) ++stats.pipelineChanges;
    if (!previous || previous->materialSignature != draw.materialSignature)
      ++stats.materialChanges;
    if (!previous || !sameTextureState(*previous, draw)) ++stats.textureChanges;
    if (!previous || !sameLightingState(*previous, draw)) ++stats.lightingChanges;
    previous = &draw;
  }
  stats.opaqueOrderChanged = originalOpaque != encodedOpaque;
  return stats;
}

void copyLogicalDrawStats(const LogicalDrawStats & source,
                          bool groupingEnabled,
                          CoinBgfxPhaseSample & destination)
{
  destination.opaqueDraws = source.opaqueDraws;
  destination.transparentDraws = source.transparentDraws;
  destination.logicalPipelineChanges = source.pipelineChanges;
  destination.logicalMaterialChanges = source.materialChanges;
  destination.logicalTextureChanges = source.textureChanges;
  destination.logicalLightingChanges = source.lightingChanges;
  destination.opaqueGroupingEnabled = groupingEnabled;
  destination.opaqueOrderChanged = source.opaqueOrderChanged;
}

uint32_t pooledCapacity(size_t required)
{
  // Bound slack for large uploads instead of reserving the next power of two.
  if (required > 65536) {
    const uint64_t aligned = (uint64_t(required) + 65535u) & ~UINT64_C(65535);
    return aligned <= UINT32_MAX ? static_cast<uint32_t>(aligned) : static_cast<uint32_t>(required);
  }
  uint32_t capacity = 256;
  while (capacity < required && capacity <= UINT32_MAX / 2u)
    capacity *= 2u;
  return capacity < required ? static_cast<uint32_t>(required) : capacity;
}

bool setDrawScissor(const CoinBgfxDraw & draw, int targetWidth, int targetHeight)
{
  int32_t clipped[4];
  if (!CoinBgfxLowering::clipViewport(draw.viewport, targetWidth, targetHeight, clipped))
    return false;
  const int32_t top = targetHeight - clipped[1] - clipped[3];
  bgfx::setScissor(static_cast<uint16_t>(clipped[0]),
                   static_cast<uint16_t>(top),
                   static_cast<uint16_t>(clipped[2]),
                   static_cast<uint16_t>(clipped[3]));
  return true;
}
const uint64_t peelTextureFlags = BGFX_TEXTURE_RT |
  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
  BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
const uint64_t shadowTextureFlags = BGFX_TEXTURE_RT |
  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
  BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;

uint64_t peelDrawState(const CoinBgfxDraw & draw, bool depthOnly)
{
  uint64_t state = drawState(draw);
  state &= ~(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
             BGFX_STATE_WRITE_Z | BGFX_STATE_BLEND_MASK);
  if (!depthOnly || (draw.depthTest && draw.depthWrite)) state |= BGFX_STATE_WRITE_Z;
  if (!depthOnly) state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
  return state;
}

uint32_t peelFixedSampleMask(uint32_t samples, uint32_t sample)
{
  const auto renderer = bgfx::getRendererType();
  // GL's MRT clear is itself a draw. Shader sample coverage expresses the
  // exact same per-draw mask without leaving GL_SAMPLE_MASK enabled at clear.
  return samples == 1 || renderer == bgfx::RendererType::OpenGL || renderer == bgfx::RendererType::OpenGLES
    ? UINT32_MAX : 1u << sample;
}

uint64_t peelSelectionState(const CoinBgfxDraw &draw)
{
  // The attachment selects the next nearest distinct depth. The captured
  // depth function is evaluated against the real opaque depth in the shader.
  uint64_t state = drawState(draw);
  state &= ~(BGFX_STATE_DEPTH_TEST_MASK | BGFX_STATE_WRITE_RGB |
             BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_BLEND_MASK);
  return state | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_WRITE_Z |
         BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
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
  const auto program = bgfx::createProgram(vs, fs, true);
  if (bgfx::isValid(program)) ++sharedRuntime().programCommandsQueued;
  return program;
}

bool captureGpuPasses(uint32_t submittedFrame, CoinBgfxPhaseSample & sample)
{
  const bgfx::Stats * stats = bgfx::getStats();
  if (!stats) return false;
  sample.gpuResourceStatsAvailable = true;
  if (stats->numDraw > sample.gpuDrawSubmits)
    sample.gpuDrawSubmits = stats->numDraw;
  sample.gpuMemoryUsedBytes = stats->gpuMemoryUsed >= 0 ? stats->gpuMemoryUsed : -1;
  sample.textureMemoryUsedBytes = stats->textureMemoryUsed >= 0 ?
    stats->textureMemoryUsed : -1;
  sample.renderTargetMemoryUsedBytes = stats->rtMemoryUsed >= 0 ?
    stats->rtMemoryUsed : -1;
  sample.gpuVertexBuffers = stats->numVertexBuffers;
  sample.gpuIndexBuffers = stats->numIndexBuffers;
  sample.gpuTextures = stats->numTextures;
  sample.gpuFrameBuffers = stats->numFrameBuffers;
  sample.gpuPrograms = stats->numPrograms;
  if (stats->gpuTimerFreq <= 0) return false;
  const double gpuMilliseconds = 1000.0 / double(stats->gpuTimerFreq);
  const bool targetFrame = stats->gpuFrameNum == submittedFrame &&
    stats->gpuTimeEnd > stats->gpuTimeBegin;
  if (targetFrame) {
    sample.gpuFrameMs = double(stats->gpuTimeEnd - stats->gpuTimeBegin) *
                        gpuMilliseconds;
  }
  if (stats->cpuTimerFreq > 0) {
    sample.gpuWaitMs = double(stats->waitRender + stats->waitSubmit) *
                       1000.0 / double(stats->cpuTimerFreq);
  }
  const auto add = [](double & total, double value) {
    if (total < 0.0) total = 0.0;
    total += value;
  };
  const bool needPasses = sample.gpuOpaqueMs < 0.0 &&
    sample.gpuTransparentMs < 0.0 && sample.gpuCompositeMs < 0.0 &&
    sample.gpuBlitReadbackMs < 0.0;
  for (uint16_t index = 0; needPasses && index < stats->numViews; ++index) {
    const bgfx::ViewStats & view = stats->viewStats[index];
    if (view.gpuTimeEnd <= view.gpuTimeBegin) continue;
    if (sample.gpuPassFrame == 0) sample.gpuPassFrame = view.gpuFrameNum;
    if (view.gpuFrameNum != sample.gpuPassFrame) continue;
    const double elapsed = double(view.gpuTimeEnd - view.gpuTimeBegin) *
                           gpuMilliseconds;
    if (std::strcmp(view.name, "opaque") == 0) {
      add(sample.gpuOpaqueMs, elapsed);
    } else if (std::strncmp(view.name, "transparent_accumulation", 24) == 0) {
      add(sample.gpuTransparentMs, elapsed);
    } else if (std::strcmp(view.name, "fullscreen_composition") == 0) {
      add(sample.gpuCompositeMs, elapsed);
    } else if (std::strcmp(view.name, "blit_readback") == 0) {
      add(sample.gpuBlitReadbackMs, elapsed);
    }
  }
  return targetFrame;
}
}

struct CoinBgfxBackend::AsyncEntry {
  ReadbackSlot slot;
  CoinRenderReadbackTicket ticket;
  bool bottomLeft = false;
};
namespace {
std::map<uint64_t, std::shared_ptr<CoinBgfxBackend::AsyncEntry>> & asyncEntries()
{
  static auto * entries = new std::map<uint64_t, std::shared_ptr<CoinBgfxBackend::AsyncEntry>>;
  return *entries;
}
struct LostReadback {
  CoinRenderReadbackTicket ticket;
  std::thread::id thread;
};
std::map<uint64_t, LostReadback> lostReadbacks;
uint64_t nextTicketToken = 1;
std::vector<std::shared_ptr<CoinBgfxBackend::AsyncEntry>> failedReadbacks;
bool sameTicket(const CoinRenderReadbackTicket & a, const CoinRenderReadbackTicket & b)
{
  return a.token == b.token && a.generation == b.generation &&
    a.submissionSerial == b.submissionSerial && a.width == b.width && a.height == b.height &&
    a.colorFormat == b.colorFormat && a.depthFormat == b.depthFormat &&
    a.colorRowPitch == b.colorRowPitch && a.depthRowPitch == b.depthRowPitch &&
    a.colorBytes == b.colorBytes && a.depthBytes == b.depthBytes;
}
// Caller holds the runtime mutex and has retired the GPU writes.
void releaseAsync(uint64_t token, bool failed = false)
{
  auto & entries = asyncEntries();
  auto entry = entries.at(token); // Keep CPU buffers alive through shutdown.
  auto & runtime = sharedRuntime();
  if (!failed) {
    bgfx::destroy(entry->slot.texture);
    if (bgfx::isValid(entry->slot.depthTexture)) bgfx::destroy(entry->slot.depthTexture);
  }
  if (failed) failedReadbacks.push_back(entry);
  entries.erase(token);
  if (--runtime.references == 0) {
    const auto *callback = static_cast<CoinBgfxCallback *>(runtime.callback.get());
    runtime.portable.clear(!failed && callback && !callback->failed());
    bgfx::shutdown();
    runtime.releaseNativeDisplay();
    failedReadbacks.clear();
    runtime.callback.reset(); runtime.renderer = bgfx::RendererType::Count;
    runtime.apiThread = std::thread::id(); runtime.viewBlocks.clear();
  }
}
}

// Direct3D12 consumes the DXBC shader binaries generated by shaderc.
#ifdef _WIN32
#define COIN_BGFX_SHADER_DATA(name) (bgfx::getRendererType() == bgfx::RendererType::Direct3D12 ? name##_dx11 : bgfx::getRendererType() == bgfx::RendererType::OpenGL ? name##_glsl : name##_spirv)
#define COIN_BGFX_SHADER_SIZE(name) (bgfx::getRendererType() == bgfx::RendererType::Direct3D12 ? sizeof(name##_dx11) : bgfx::getRendererType() == bgfx::RendererType::OpenGL ? sizeof(name##_glsl) : sizeof(name##_spirv))
#else
#define COIN_BGFX_SHADER_DATA(name) (bgfx::getRendererType() == bgfx::RendererType::OpenGL ? name##_glsl : name##_spirv)
#define COIN_BGFX_SHADER_SIZE(name) (bgfx::getRendererType() == bgfx::RendererType::OpenGL ? sizeof(name##_glsl) : sizeof(name##_spirv))
#endif

#define COIN_BGFX_SURFACE_DATA(name) (this->portableSampling ? COIN_BGFX_SHADER_DATA(name##_portable) : COIN_BGFX_SHADER_DATA(name))
#define COIN_BGFX_SURFACE_SIZE(name) (this->portableSampling ? COIN_BGFX_SHADER_SIZE(name##_portable) : COIN_BGFX_SHADER_SIZE(name))

namespace {
bgfx::ProgramHandle createRasterImageProgram()
{
  const auto shader = bgfx::createShader(bgfx::copy(
    COIN_BGFX_SHADER_DATA(coin_bgfx_cs_raster_image), COIN_BGFX_SHADER_SIZE(coin_bgfx_cs_raster_image)));
  if (!bgfx::isValid(shader)) return BGFX_INVALID_HANDLE;
  const auto program = bgfx::createProgram(shader, true);
  if (bgfx::isValid(program)) ++sharedRuntime().programCommandsQueued;
  return program;
}

}

namespace {
uint64_t weightedTextureFlags(uint32_t samples)
{
  return samples == 1 ? peelTextureFlags : BGFX_TEXTURE_MSAA_SAMPLE |
    (samples == 2 ? BGFX_TEXTURE_RT_MSAA_X2 : BGFX_TEXTURE_RT_MSAA_X4);
}

// Validate the actual half-float sample counts before any graph producer or
// scene is encoded. SDKs may downgrade nominal MSAA flags on a given format.
bool bgfxWeightedSamples(uint32_t samples, const bgfx::VertexLayout & layout,
                        bgfx::ViewId view, std::string & diagnostic, bool verify = true)
{
  const auto * caps = bgfx::getCaps();
  const uint64_t flags = weightedTextureFlags(samples);
  if (!caps || !(caps->supported & BGFX_CAPS_BLEND_INDEPENDENT) || caps->limits.maxFBAttachments < 3) {
    diagnostic = "Weighted OIT requires independent blending and three attachments";
    return false;
  }
  for (auto format : {bgfx::TextureFormat::RGBA16F, bgfx::TextureFormat::R16F})
    if (!(caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
        (samples > 1 && !(caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA)) ||
        !bgfx::isTextureValid(1, false, 1, format, flags)) {
      diagnostic = "Weighted OIT color formats cannot be blended/sampled at the requested count";
      return false;
    }
  if (!verify || samples == 1 || (sharedRuntime().weightedSamplesVerified & (1u << samples))) return true;
  if (sharedRuntime().weightedSamplesRejected & (1u << samples)) {
    diagnostic = "BGFX did not allocate the exact weighted accumulation/revealage sample count";
    return false;
  }
  if (!(caps->formats[bgfx::TextureFormat::R32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R32F,
                            BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST)) {
    diagnostic = "Weighted MSAA exact sample-count probe unavailable";
    return false;
  }
  const auto accum = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA16F, flags);
  const auto reveal = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R16F, flags);
  const auto result = bgfx::createFrameBuffer(1, 1, bgfx::TextureFormat::R32F, peelTextureFlags);
  const auto staging = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R32F,
    BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
  const auto a = bgfx::createUniform("s_oitAccumMS", bgfx::UniformType::Sampler);
  const auto r = bgfx::createUniform("s_oitRevealMS", bgfx::UniformType::Sampler);
  const auto info = bgfx::createUniform("u_depthInfo", bgfx::UniformType::Vec4);
  const auto program = createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
    COIN_BGFX_SHADER_DATA(coin_bgfx_fs_weighted_composite_msaa), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_weighted_composite_msaa));
  CoinBgfxVertex vertices[3] = {};
  vertices[0].position[0] = -1; vertices[0].position[1] = -1;
  vertices[1].position[0] = 3; vertices[1].position[1] = -1;
  vertices[2].position[0] = -1; vertices[2].position[1] = 3;
  const auto vb = bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), layout);
  const auto release = [&]() {
    if (bgfx::isValid(vb)) bgfx::destroy(vb);
    if (bgfx::isValid(program)) bgfx::destroy(program);
    if (bgfx::isValid(a)) bgfx::destroy(a);
    if (bgfx::isValid(r)) bgfx::destroy(r);
    if (bgfx::isValid(info)) bgfx::destroy(info);
    if (bgfx::isValid(accum)) bgfx::destroy(accum);
    if (bgfx::isValid(reveal)) bgfx::destroy(reveal);
    if (bgfx::isValid(result)) bgfx::destroy(result);
    if (bgfx::isValid(staging)) bgfx::destroy(staging);
  };
  if (!bgfx::isValid(accum) || !bgfx::isValid(reveal) || !bgfx::isValid(result) ||
      !bgfx::isValid(staging) || !bgfx::isValid(a) || !bgfx::isValid(r) ||
      !bgfx::isValid(info) || !bgfx::isValid(program) || !bgfx::isValid(vb)) {
    release(); diagnostic = "Weighted MSAA sample-count probe allocation failed"; return false;
  }
  bgfx::frame(BGFX_FRAME_FLUSH);
  sharedRuntime().programCommandsMaterialized = sharedRuntime().programCommandsQueued;
  bgfx::resetView(view); bgfx::resetView(view + 1);
  bgfx::setViewName(view, "weighted_msaa_sample_count");
  bgfx::setViewRect(view, 0, 0, 1, 1);
  bgfx::setViewFrameBuffer(view, result);
  const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  const float params[4] = {1,1,0,float(samples)};
  bgfx::setTransform(identity);
  bgfx::setUniform(info, params);
  bgfx::setTexture(0, a, accum); bgfx::setTexture(1, r, reveal);
  bgfx::setVertexBuffer(0, vb);
  bgfx::setSampleMask();
  bgfx::setState(BGFX_STATE_WRITE_R);
  bgfx::submit(view, program);
  bgfx::TextureRegion dst, src;
  dst.handle = staging; src.handle = bgfx::getTexture(result);
  bgfx::blit(view + 1, dst, src);
  float effective = -1;
  bgfx::TextureRegion read; read.handle = staging;
  const uint32_t ready = bgfx::read(read, &effective);
  uint32_t completed = bgfx::frame();
  while (static_cast<int32_t>(completed - ready) < 0) completed = bgfx::frame();
  bgfx::resetView(view); bgfx::resetView(view + 1);
  release();
  if (effective != float(samples)) {
    sharedRuntime().weightedSamplesRejected |= 1u << samples;
    diagnostic = "BGFX did not allocate the exact weighted accumulation/revealage sample count";
    return false;
  }
  sharedRuntime().weightedSamplesVerified |= 1u << samples;
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE weighted_msaa requested=%u effective=%.0f scene_draws=0\n", samples, effective);
  return true;
}

bool bgfxSampleInterpolationSupported(uint32_t samples, std::string & diagnostic)
{
  const auto * caps = bgfx::getCaps();
  if (samples > 1 && caps && caps->rendererType == bgfx::RendererType::Vulkan) {
#if defined(COIN_BGFX_SAMPLE_INTERPOLATION_EXTENSION_VERSION) && COIN_BGFX_SAMPLE_INTERPOLATION_EXTENSION_VERSION == 1 && BGFX_API_VERSION == UINT32_C(161) && defined(COIN_BGFX_CAPS_SAMPLE_INTERPOLATION)
    if (!(caps->supported & COIN_BGFX_CAPS_SAMPLE_INTERPOLATION)) {
      diagnostic = "BGFX Vulkan sample interpolation is not enabled on this runtime/device";
      return false;
    }
#else
    diagnostic = "BGFX Vulkan SDK lacks the controlled sample interpolation extension";
    return false;
#endif
  }
  return true;
}

// GL hardware depth may differ at partially covered samples when switching
// fragment-depth programs. Use the explicitly evaluated covered sample in both
// modes. GLSL sample interpolation (4.00) is already covered by this profile's
// mandatory GLSL 4.30 MSAA resolve/probe before any scene is submitted.
bool bgfxExplicitSurfaceSamples()
{
  return bgfx::getRendererType() == bgfx::RendererType::OpenGL;
}

bgfx::ProgramHandle bgfxTracePreparedProgram(bgfx::ProgramHandle program, const char *role,
                                             uint32_t samples)
{
  if (bgfx::isValid(program) && CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_program_prepare role=%s samples=%u\n", role,
                 samples);
  return program;
}

// Shared composition resources are needed by modules too; fixed surface
// stages are needed only by ordinary occurrences which actually execute them.
bool bgfxNeedsFixedSurfacePrograms(const std::vector<CoinBgfxDraw> &draws)
{
  return std::any_of(draws.begin(), draws.end(), [](const CoinBgfxDraw &draw) {
    return draw.indexCount && !draw.portableShaderSlot && !draw.bumpSlot;
  });
}

struct BgfxFrameProgramNeeds {
  bool ordinary = false;
  bool bump = false;
};

BgfxFrameProgramNeeds bgfxFrameProgramNeeds(const CoinRenderFramePlan &frame)
{
  BgfxFrameProgramNeeds needs;
  for (const auto &draw : frame.draws) {
    if (draw.shadowLightSlot || !draw.geometry.indexCount)
      continue;
    if (draw.geometry.firstIndex >= frame.indices.size()) {
      needs.ordinary = true; // Invalid geometry remains the admission validator's responsibility.
      continue;
    }
    if (draw.renderStateSlot < frame.renderStates.size() &&
        frame.renderStates[draw.renderStateSlot].portableShaderSlot)
      continue;
    const uint32_t vertex = frame.indices[draw.geometry.firstIndex];
    const auto coord =
        std::lower_bound(frame.bumpCoords.begin(), frame.bumpCoords.end(), vertex,
                         [](const CoinRenderBumpCoordOverride &candidate, uint32_t index) {
                           return candidate.vertexIndex < index;
                         });
    if (coord != frame.bumpCoords.end() && coord->vertexIndex == vertex && coord->imageSlot)
      needs.bump = true;
    else
      needs.ordinary = true;
  }
  return needs;
}

bool bgfxShadowMultitexture(const CoinRenderFramePlan & frame)
{
  // Raster inherited source states feed a compute prepass, not the surface
  // shader. Only states actually referenced by geometry select its layout.
  return !frame.shadowGroups.empty() && std::any_of(frame.draws.begin(), frame.draws.end(), [&frame](const CoinRenderDrawPacket & draw) {
    if (draw.renderStateSlot >= frame.renderStates.size()) return false;
    const auto & state = frame.renderStates[draw.renderStateSlot];
    return state.portableShaderSlot || !frame.bumpImages.empty() ||
           (state.hasTexture && state.textureImageSlot < frame.textures.size() &&
            (frame.textures[state.textureImageSlot].volumeDepth ||
             frame.textures[state.textureImageSlot].cubeFaceSize)) ||
           std::any_of(std::begin(state.extraTextures), std::end(state.extraTextures),
                       [](const CoinRenderTextureUnitSnapshot &unit) { return unit.enabled; });
  });
}

bool bgfxCustomSampleDepth(const CoinBgfxDraw & draw)
{
  return draw.depthRange[0] != 0.0f || draw.depthRange[1] != 1.0f ||
    draw.polygonOffsetFactor != 0.0f || draw.polygonOffsetUnits != 0.0f ||
    draw.polygonOffsetSlopeBias != 0.0f;
}

bool bgfxPeelingSamples(uint32_t samples, const bgfx::VertexLayout & layout,
                        bgfx::ViewId view, std::string & diagnostic, bool verify = true)
{
  const auto * caps = bgfx::getCaps();
  if (!bgfxSampleInterpolationSupported(samples, diagnostic)) return false;
  const uint64_t flags = weightedTextureFlags(samples);
  if (!caps || caps->limits.maxFBAttachments < 3) {
    diagnostic = "Peeling requires three attachments";
    return false;
  }
  for (auto format : {bgfx::TextureFormat::RGBA16F, bgfx::TextureFormat::R16F, bgfx::TextureFormat::D32F})
    if (!(caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
        (samples > 1 && !(caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA)) ||
        !bgfx::isTextureValid(1, false, 1, format, flags)) {
      diagnostic = "Peeling color/depth/mask formats cannot render/sample at the requested count";
      return false;
    }
  if (!verify || samples == 1 || (sharedRuntime().peelingSamplesVerified & (1u << samples))) return true;
  if (sharedRuntime().peelingSamplesRejected & (1u << samples)) {
    diagnostic = "BGFX did not allocate the exact peeling color/depth/mask sample count";
    return false;
  }
  if (!(caps->formats[bgfx::TextureFormat::R32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R32F,
                            BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST)) {
    diagnostic = "Peeling MSAA exact sample-count probe unavailable";
    return false;
  }
  const auto accum = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA16F, flags);
  const auto reveal = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R16F, flags);
  const auto depth = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::D32F, flags);
  const auto result = bgfx::createFrameBuffer(1, 1, bgfx::TextureFormat::R32F, peelTextureFlags);
  const auto staging = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R32F,
    BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
  const auto a = bgfx::createUniform("s_peelColorMS", bgfx::UniformType::Sampler);
  const auto r = bgfx::createUniform("s_peelMaskMS", bgfx::UniformType::Sampler);
  const auto d = bgfx::createUniform("s_peelDepthMS", bgfx::UniformType::Sampler);
  const auto info = bgfx::createUniform("u_depthInfo", bgfx::UniformType::Vec4);
  const auto program = createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
    COIN_BGFX_SHADER_DATA(coin_bgfx_fs_peel_msaa_composite), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_peel_msaa_composite));
  CoinBgfxVertex vertices[3] = {};
  vertices[0].position[0] = -1; vertices[0].position[1] = -1;
  vertices[1].position[0] = 3; vertices[1].position[1] = -1;
  vertices[2].position[0] = -1; vertices[2].position[1] = 3;
  const auto vb = bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), layout);
  const auto release = [&]() {
    if (bgfx::isValid(vb)) bgfx::destroy(vb);
    if (bgfx::isValid(program)) bgfx::destroy(program);
    if (bgfx::isValid(a)) bgfx::destroy(a);
    if (bgfx::isValid(r)) bgfx::destroy(r);
    if (bgfx::isValid(d)) bgfx::destroy(d);
    if (bgfx::isValid(depth)) bgfx::destroy(depth);
    if (bgfx::isValid(info)) bgfx::destroy(info);
    if (bgfx::isValid(accum)) bgfx::destroy(accum);
    if (bgfx::isValid(reveal)) bgfx::destroy(reveal);
    if (bgfx::isValid(result)) bgfx::destroy(result);
    if (bgfx::isValid(staging)) bgfx::destroy(staging);
  };
  if (!bgfx::isValid(depth) || !bgfx::isValid(d) || !bgfx::isValid(accum) || !bgfx::isValid(reveal) || !bgfx::isValid(result) ||
      !bgfx::isValid(staging) || !bgfx::isValid(a) || !bgfx::isValid(r) ||
      !bgfx::isValid(info) || !bgfx::isValid(program) || !bgfx::isValid(vb)) {
    release(); diagnostic = "Peeling MSAA sample-count probe allocation failed"; return false;
  }
  bgfx::frame(BGFX_FRAME_FLUSH);
  sharedRuntime().programCommandsMaterialized = sharedRuntime().programCommandsQueued;
  bgfx::resetView(view); bgfx::resetView(view + 1);
  bgfx::setViewName(view, "peeling_msaa_sample_count");
  bgfx::setViewRect(view, 0, 0, 1, 1);
  bgfx::setViewFrameBuffer(view, result);
  const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  const float params[4] = {1,1,0,float(samples)};
  bgfx::setTransform(identity);
  bgfx::setUniform(info, params);
  bgfx::setTexture(0, a, accum); bgfx::setTexture(1, r, reveal); bgfx::setTexture(2, d, depth);
  bgfx::setVertexBuffer(0, vb);
  bgfx::setSampleMask();
  bgfx::setState(BGFX_STATE_WRITE_R);
  bgfx::submit(view, program);
  bgfx::TextureRegion dst, src;
  dst.handle = staging; src.handle = bgfx::getTexture(result);
  bgfx::blit(view + 1, dst, src);
  float effective = -1;
  bgfx::TextureRegion read; read.handle = staging;
  const uint32_t ready = bgfx::read(read, &effective);
  uint32_t completed = bgfx::frame();
  while (static_cast<int32_t>(completed - ready) < 0) completed = bgfx::frame();
  bgfx::resetView(view); bgfx::resetView(view + 1);
  release();
  if (effective != float(samples)) {
    sharedRuntime().peelingSamplesRejected |= 1u << samples;
    diagnostic = "BGFX did not allocate the exact peeling color/depth/mask sample count";
    return false;
  }
  sharedRuntime().peelingSamplesVerified |= 1u << samples;
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE peeling_msaa requested=%u effective=%.0f scene_draws=0\n", samples, effective);
  return true;
}
}

CoinBgfxBackend::CoinBgfxBackend()
  : status(CoinRenderBackendStatus::NOT_READY), viewBase(0), nativeDisplay(nullptr),
    nativeWindow(nullptr), initialized(false), presentToWindow(false),
    cameraPatchEnabled(true), drawGroupingEnabled(true), drawBatchingEnabled(true), readbackPipelineDepth(1),
    readbackCursor(0), readbackSequence(0),
    transparencyMode(CoinBgfxTransparencyMode::AUTO),
    activeTransparencyStrategy(CoinBgfxTransparencyStrategy::OBJECT),
    weightedOitSupported(false), sortedLayersSupported(false), serial(0),
    directTextureSerial(0),
    width(0), height(0), program(BGFX_INVALID_HANDLE),
    solidProgram(BGFX_INVALID_HANDLE), texture2dProgram(BGFX_INVALID_HANDLE),
    instancedProgram(BGFX_INVALID_HANDLE),
    activeProgram(BGFX_INVALID_HANDLE),
    shadowMomentsProgram(BGFX_INVALID_HANDLE),
    shadowReceiverProgram(BGFX_INVALID_HANDLE),
    shadowReceiverProgram4(BGFX_INVALID_HANDLE),
    shadowReceiverProgram8(BGFX_INVALID_HANDLE),
    shadowPeelProgram(BGFX_INVALID_HANDLE), shadowOitProgram(BGFX_INVALID_HANDLE),
    shadowModelViewUniform(BGFX_INVALID_HANDLE),
    shadowClipModelViewUniform(BGFX_INVALID_HANDLE),
    shadowDepthUniform(BGFX_INVALID_HANDLE),
    shadowQualityUniform(BGFX_INVALID_HANDLE), shadowFlatDiffuseUniform(BGFX_INVALID_HANDLE),
    shadowFlatSpecularUniform(BGFX_INVALID_HANDLE), shadowFlatParamsUniform(BGFX_INVALID_HANDLE),
    shadowLightIndicesUniform(BGFX_INVALID_HANDLE),
    shadowLightIndicesExtraUniform(BGFX_INVALID_HANDLE),
    depthReadProgram(BGFX_INVALID_HANDLE), readDepthSampler(BGFX_INVALID_HANDLE),
    depthReadFrameBuffer(BGFX_INVALID_HANDLE),
    peelNextProgram(BGFX_INVALID_HANDLE), compositeProgram(BGFX_INVALID_HANDLE),
    compositeDepthProgram(BGFX_INVALID_HANDLE),
    npotMipProgram(BGFX_INVALID_HANDLE),
    weightedOitProgram(BGFX_INVALID_HANDLE),
    weightedCompositeProgram(BGFX_INVALID_HANDLE),
    previousDepthSampler(BGFX_INVALID_HANDLE),
    opaqueDepthSampler(BGFX_INVALID_HANDLE),
    layerSampler(BGFX_INVALID_HANDLE), layerMaskSampler(BGFX_INVALID_HANDLE),
    layerDepthSampler(BGFX_INVALID_HANDLE), peelMetaUniform(BGFX_INVALID_HANDLE),
    npotMipSampler(BGFX_INVALID_HANDLE),
    npotMipExtentUniform(BGFX_INVALID_HANDLE), oitAccumSampler(BGFX_INVALID_HANDLE),
    oitRevealSampler(BGFX_INVALID_HANDLE), depthInfoUniform(BGFX_INVALID_HANDLE),
    coinDepthUniform(BGFX_INVALID_HANDLE), screenDoorUniform(BGFX_INVALID_HANDLE),
    alphaTestUniform(BGFX_INVALID_HANDLE), rasterViewportUniform(BGFX_INVALID_HANDLE),
    clipMetaUniform(BGFX_INVALID_HANDLE), clipPlanesUniform(BGFX_INVALID_HANDLE),
    textureSampler(BGFX_INVALID_HANDLE), fogColorModeUniform(BGFX_INVALID_HANDLE),
    fogRangeUniform(BGFX_INVALID_HANDLE), textureParamsUniform(BGFX_INVALID_HANDLE), samplingTextureSizesUniform(BGFX_INVALID_HANDLE),
    textureBlendUniform(BGFX_INVALID_HANDLE), textureCombineUniform(BGFX_INVALID_HANDLE),
    ambientLightUniform(BGFX_INVALID_HANDLE), lightCountUniform(BGFX_INVALID_HANDLE),
    lightPositionTypeUniform(BGFX_INVALID_HANDLE),
    lightDirectionCutoffUniform(BGFX_INVALID_HANDLE),
    lightColorIntensityUniform(BGFX_INVALID_HANDLE),
    lightAmbientColorUniform(BGFX_INVALID_HANDLE),
    instancedCameraUniform(BGFX_INVALID_HANDLE),
    lightAttenuationDropUniform(BGFX_INVALID_HANDLE),
    fullscreenVertexBuffer(BGFX_INVALID_HANDLE),
    fullscreenIndexBuffer(BGFX_INVALID_HANDLE),
    frameBuffer(BGFX_INVALID_HANDLE), windowSceneFrameBuffer(BGFX_INVALID_HANDLE),
    oitFrameBuffer(BGFX_INVALID_HANDLE),
    readbackTexture(BGFX_INVALID_HANDLE), lastPublishedSequence(0),
    defaultTexture(BGFX_INVALID_HANDLE),
    cachedRevision(0), cachedWidth(0), cachedHeight(0),
    cachedHomogeneousDepth(false), cachedVertexBuffer(BGFX_INVALID_HANDLE),
    cachedIndexBuffer(BGFX_INVALID_HANDLE), cachedVertexCapacity(0),
    cachedIndexCapacity(0)
{
  for (auto & handle : this->extraTextureSamplers) handle = BGFX_INVALID_HANDLE;
  for (int i = 0; i < 8; ++i) {
    this->shadowViewToClipUniform[i] = BGFX_INVALID_HANDLE;
    this->shadowViewToLightUniform[i] = BGFX_INVALID_HANDLE;
    this->shadowParamsUniform[i] = BGFX_INVALID_HANDLE;
    this->shadowMetaUniform[i] = BGFX_INVALID_HANDLE;
    this->shadowTexelUniform[i] = BGFX_INVALID_HANDLE;
    this->shadowSampler[i] = BGFX_INVALID_HANDLE;
  }
  for (uint8_t i = 0; i < COIN_RENDER_MAX_PEEL_LAYERS; ++i)
    this->peelFrameBuffers[i] = BGFX_INVALID_HANDLE;
  const char * disabled = std::getenv("COIN_BGFX_DISABLE_CAMERA_PATCH");
  this->cameraPatchEnabled = disabled == nullptr || std::strcmp(disabled, "1") != 0;
  const char * groupingDisabled = std::getenv("COIN_BGFX_DISABLE_DRAW_GROUPING");
  this->drawGroupingEnabled = groupingDisabled == nullptr ||
    std::strcmp(groupingDisabled, "1") != 0;
  const char * batchingDisabled = std::getenv("COIN_BGFX_DISABLE_DRAW_BATCHING");
  this->drawBatchingEnabled = batchingDisabled == nullptr ||
    std::strcmp(batchingDisabled, "1") != 0;
  const char * readbackDepth = std::getenv("COIN_BGFX_READBACK_PIPELINE_DEPTH");
  if (readbackDepth && std::strcmp(readbackDepth, "2") == 0)
    this->readbackPipelineDepth = 2;
  else if (readbackDepth && std::strcmp(readbackDepth, "3") == 0)
    this->readbackPipelineDepth = 3;
}

bool CoinBgfxBackend::prepareShadowResources()
{
  if (!bgfx::isValid(this->shadowModelViewUniform))
    this->shadowModelViewUniform =
        bgfx::createUniform("u_shadowModelView", bgfx::UniformType::Mat4);
  if (!bgfx::isValid(this->shadowClipModelViewUniform))
    this->shadowClipModelViewUniform =
        bgfx::createUniform("u_shadowClipModelView", bgfx::UniformType::Mat4);
  if (!bgfx::isValid(this->shadowDepthUniform))
    this->shadowDepthUniform = bgfx::createUniform("u_shadowDepth", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowQualityUniform))
    this->shadowQualityUniform = bgfx::createUniform("u_shadowQuality", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowFlatDiffuseUniform))
    this->shadowFlatDiffuseUniform =
        bgfx::createUniform("u_shadowFlatDiffuse", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowFlatSpecularUniform))
    this->shadowFlatSpecularUniform =
        bgfx::createUniform("u_shadowFlatSpecular", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowFlatParamsUniform))
    this->shadowFlatParamsUniform =
        bgfx::createUniform("u_shadowFlatParams", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowLightIndicesUniform))
    this->shadowLightIndicesUniform =
        bgfx::createUniform("u_shadowLightIndices", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->shadowLightIndicesExtraUniform))
    this->shadowLightIndicesExtraUniform =
        bgfx::createUniform("u_shadowLightIndicesExtra", bgfx::UniformType::Vec4);
  bool valid =
      bgfx::isValid(this->shadowLightIndicesExtraUniform) &&
      bgfx::isValid(this->shadowModelViewUniform) &&
      bgfx::isValid(this->shadowClipModelViewUniform) && bgfx::isValid(this->shadowDepthUniform) &&
      bgfx::isValid(this->shadowLightIndicesUniform) && bgfx::isValid(this->shadowQualityUniform) &&
      bgfx::isValid(this->shadowFlatDiffuseUniform) &&
      bgfx::isValid(this->shadowFlatSpecularUniform) &&
      bgfx::isValid(this->shadowFlatParamsUniform);
  for (int i = 0; i < 8; ++i) {
    const std::string suffix = std::to_string(i);
    if (!bgfx::isValid(this->shadowViewToClipUniform[i]))
      this->shadowViewToClipUniform[i] =
          bgfx::createUniform(("u_shadowViewToClip" + suffix).c_str(), bgfx::UniformType::Mat4);
    if (!bgfx::isValid(this->shadowViewToLightUniform[i]))
      this->shadowViewToLightUniform[i] =
          bgfx::createUniform(("u_shadowViewToLight" + suffix).c_str(), bgfx::UniformType::Mat4);
    if (!bgfx::isValid(this->shadowParamsUniform[i]))
      this->shadowParamsUniform[i] =
          bgfx::createUniform(("u_shadowParams" + suffix).c_str(), bgfx::UniformType::Vec4);
    if (!bgfx::isValid(this->shadowMetaUniform[i]))
      this->shadowMetaUniform[i] =
          bgfx::createUniform(("u_shadowMeta" + suffix).c_str(), bgfx::UniformType::Vec4);
    if (!bgfx::isValid(this->shadowTexelUniform[i]))
      this->shadowTexelUniform[i] =
          bgfx::createUniform(("u_shadowTexel" + suffix).c_str(), bgfx::UniformType::Vec4);
    if (!bgfx::isValid(this->shadowSampler[i]))
      this->shadowSampler[i] =
          bgfx::createUniform(("s_shadow" + suffix).c_str(), bgfx::UniformType::Sampler);
    valid = valid && bgfx::isValid(this->shadowViewToClipUniform[i]) &&
      bgfx::isValid(this->shadowViewToLightUniform[i]) &&
      bgfx::isValid(this->shadowParamsUniform[i]) &&
      bgfx::isValid(this->shadowMetaUniform[i]) &&
      bgfx::isValid(this->shadowTexelUniform[i]) &&
      bgfx::isValid(this->shadowSampler[i]);
  }
  if (!valid) this->lastError = "BGFX shadow shader or uniform allocation failed";
  return valid;
}

bool CoinBgfxBackend::prepareShadowPrograms(size_t mapCount, bool receivers)
{
  if (!this->prepareShadowResources())
    return false;
  const bool extended = mapCount > 2;
  const bool eight = mapCount > 4;
  const auto createExtended = [&]() {
    return bgfxTracePreparedProgram(
        createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
                           COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
                           COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_receiver4),
                           COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_receiver4)),
        "shadow_receiver", this->sampleCount);
  };
  const auto createEight = [&]() {
    return bgfxTracePreparedProgram(
        createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
                           COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
                           COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_receiver8),
                           COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_receiver8)),
        "shadow_receiver", this->sampleCount);
  };
  if (!bgfx::isValid(this->shadowMomentsProgram)) {
    this->shadowMomentsProgram =
        createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_moments),
                           COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_moments),
                           COIN_BGFX_SHADER_DATA(coin_bgfx_fs_shadow_moments),
                           COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_shadow_moments));
  }
  if (receivers) {
    if (!extended && !bgfx::isValid(this->shadowReceiverProgram)) {
      this->shadowReceiverProgram = bgfxTracePreparedProgram(
          createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
                             COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
                             COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_receiver),
                             COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_receiver)),
          "shadow_receiver", this->sampleCount);
    }
    if (extended && !eight && !bgfx::isValid(this->shadowReceiverProgram4))
      this->shadowReceiverProgram4 = createExtended();
    if (eight && !bgfx::isValid(this->shadowReceiverProgram8))
      this->shadowReceiverProgram8 = createEight();
  }
  const bool valid = bgfx::isValid(this->shadowMomentsProgram) &&
                     (!receivers || bgfx::isValid(eight      ? this->shadowReceiverProgram8
                                                  : extended ? this->shadowReceiverProgram4
                                                             : this->shadowReceiverProgram));
  if (!valid)
    this->lastError = "BGFX shadow shader allocation failed";
  return valid;
}

bool
CoinBgfxBackend::prepareShadowArray(const CoinRenderShadowPlan & plan, bool enabled)
{
  this->shadowArrayActive = false;
  if (!enabled || plan.passes.empty()) return true;
  uint32_t extent = 1;
  for (const auto & pass : plan.passes) extent = std::max(extent, pass.mapSize);
  // BGFX distinguishes a 2D array from an ordinary 2D image by layer count.
  const uint16_t layers = uint16_t(std::max<size_t>(2, plan.passes.size()));
  const uint64_t flags = BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_MIN_POINT |
    BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
  if (extent > bgfx::getCaps()->limits.maxTextureSize || layers > bgfx::getCaps()->limits.maxTextureLayers ||
      !bgfx::isTextureValid(1,false,layers,bgfx::TextureFormat::RGBA32F,flags)) {
    this->lastError = "Shadow moment array extent/layers/format unavailable"; return false;
  }
  const auto texture = bgfx::createTexture2D(uint16_t(extent),uint16_t(extent),false,layers,
    bgfx::TextureFormat::RGBA32F,flags);
  if (!bgfx::isValid(texture)) { this->lastError = "Shadow moment array allocation failed"; return false; }
  if (bgfx::isValid(this->shadowArrayTexture)) bgfx::destroy(this->shadowArrayTexture);
  this->shadowArrayTexture = texture; this->shadowArraySize = extent; this->shadowArrayActive = true;
  return true;
}

void
CoinBgfxBackend::encodeShadowArray(const CoinRenderShadowPlan & plan,
  const std::vector<bgfx::FrameBufferHandle> & maps, bgfx::ViewId view)
{
  if (!this->shadowArrayActive) return;
  bgfx::setViewName(view,"shadow_moment_array_copy");
  for (size_t slot=0;slot<plan.passes.size();++slot) {
    bgfx::TextureRegion destination, source;
    destination.handle=this->shadowArrayTexture; destination.z=uint16_t(slot);
    destination.width=destination.height=uint16_t(plan.passes[slot].mapSize); destination.depth=1;
    source.handle=bgfx::getTexture(maps[slot],0);
    source.width=source.height=destination.width; source.depth=1;
    bgfx::blit(view,destination,source);
  }
}

bool CoinBgfxBackend::prepareShadowMultitextureResources()
{
  if (bgfx::getCaps()->limits.maxTextureSamplers < 11 || bgfx::getCaps()->limits.maxTextureLayers < 2) {
    this->lastError = "Shadow multitexture requires eleven texture stages and 2D array sampling"; return false;
  }
  if (!this->prepareShadowResources() || !this->prepareFullscreenResources())
    return false;
  if (!bgfx::isValid(this->shadowArraySampler))
    this->shadowArraySampler = bgfx::createUniform("s_shadowArray", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->defaultShadowArrayTexture)) {
    const float white[8] = {1,1,1,1,1,1,1,1};
    this->defaultShadowArrayTexture = bgfx::createTexture2D(1,1,false,2,
      bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
      bgfx::copy(white,sizeof(white)));
  }
  if (!bgfx::isValid(this->shadowArraySampler) || !bgfx::isValid(this->defaultShadowArrayTexture)) {
    this->lastError = "Shadow array capability resource allocation failed"; return false;
  }
  return true;
}

bool CoinBgfxBackend::prepareShadowMultitextureProgram(bool customSampleDepth, unsigned composition)
{
  const bool separateSampleProgram =
      this->sampleCount > 1 && customSampleDepth && !bgfxExplicitSurfaceSamples();
  auto &program = composition == 1        ? this->shadowMultitexturePeelProgram
                  : composition == 2      ? this->shadowMultitextureOitProgram
                  : separateSampleProgram ? this->shadowMultitextureSampleDepthProgram
                                          : this->shadowMultitextureProgram;
  auto &verified = composition == 1        ? this->shadowMultitexturePeelVerified
                   : composition == 2      ? this->shadowMultitextureOitVerified
                   : separateSampleProgram ? this->shadowMultitextureSampleDepthVerified
                                           : this->shadowMultitextureVerified;
  if (verified)
    return true;
  if (!this->prepareShadowMultitextureResources())
    return false;
  // Preserve the established ordinary-surface MSAA contract: OpenGL evaluates
  // clip depth at the covered sample, Vulkan default keeps hardware depth.
  const bool explicitSamples = this->sampleCount > 1 && (composition || customSampleDepth || bgfxExplicitSurfaceSamples());
  if (explicitSamples && !bgfxSampleInterpolationSupported(this->sampleCount, this->lastError)) return false;
  if (!bgfx::isValid(program))
    program = bgfxTracePreparedProgram(
        createLayerProgram(
            COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_multitexture),
            COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_multitexture),
            composition == 1
                ? (this->sampleCount > 1
                       ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_peel_msaa)
                       : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_peel))
            : composition == 2
                ? (this->sampleCount > 1
                       ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_oit_sample_depth)
                       : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_oit))
            : explicitSamples
                ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_sample_depth)
            : this->sampleCount > 1
                ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture_native_depth)
                : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_multitexture),
            composition == 1
                ? (this->sampleCount > 1
                       ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_peel_msaa)
                       : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_peel))
            : composition == 2
                ? (this->sampleCount > 1
                       ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_oit_sample_depth)
                       : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_oit))
            : explicitSamples
                ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_sample_depth)
            : this->sampleCount > 1
                ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture_native_depth)
                : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_multitexture)),
        "shadow_multitexture", this->sampleCount);
  if (!bgfx::isValid(program)) { this->lastError = "Shadow multitexture program allocation failed"; return false; }
  return this->verifyShadowMultitextureProgram(program, verified, composition, false);
}

bool CoinBgfxBackend::verifyShadowMultitextureProgram(bgfx::ProgramHandle program, bool &verified,
                                                      unsigned composition, bool bump, uint64_t surfaceState,
                                                      bgfx::TextureFormat::Enum colorFormat,
                                                      bgfx::TextureFormat::Enum depthFormat, bool portable)
{
  if (verified)
    return true;
  // BGFX exposes no varying-component limit. A private draw/readback proves
  // the actual complete program/pipeline before any RTT producer is encoded.
  const bool resolveProbe = composition || (bump && this->sampleCount > 1);
  const uint64_t probeFlags = peelTextureFlags | (this->sampleCount == 2 ? BGFX_TEXTURE_RT_MSAA_X2 :
    this->sampleCount == 4 ? BGFX_TEXTURE_RT_MSAA_X4 : 0);
  bgfx::FrameBufferHandle output=BGFX_INVALID_HANDLE, sampledDepth=BGFX_INVALID_HANDLE;
  bgfx::TextureHandle probeColor=BGFX_INVALID_HANDLE, probeAuxiliary=BGFX_INVALID_HANDLE, probeDepth=BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle probeVertices=BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle resolvedOutput=BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle resolveProgram=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle resolveColorSampler=BGFX_INVALID_HANDLE,resolveDepthSampler=BGFX_INVALID_HANDLE;
  const auto releaseResolver=[&]() {
    if(bgfx::isValid(resolvedOutput))bgfx::destroy(resolvedOutput);
    if(bgfx::isValid(resolveProgram))bgfx::destroy(resolveProgram);
    if(bgfx::isValid(resolveColorSampler))bgfx::destroy(resolveColorSampler);
    if(bgfx::isValid(resolveDepthSampler))bgfx::destroy(resolveDepthSampler);
  };
  if (composition) {
    // Match the actual peeling/weighted MRT exactly. In particular these
    // attachments are sampleable MSAA images, without automatic resolve targets.
    // Mixed resolved RGBA8/R8 targets are a different framebuffer contract.
    const uint64_t mrtFlags=weightedTextureFlags(this->sampleCount);
    const auto color=probeColor=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA16F,mrtFlags);
    const auto auxiliary=probeAuxiliary=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::R16F,mrtFlags);
    const auto depth=probeDepth=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::D32F,mrtFlags);
    const bgfx::TextureHandle attachments[]={color,auxiliary,depth};
    if(bgfx::isValid(color)&&bgfx::isValid(auxiliary)&&bgfx::isValid(depth)) output=bgfx::createFrameBuffer(3,attachments,true);
    if(!bgfx::isValid(output)) for(const auto handle:attachments) if(bgfx::isValid(handle))bgfx::destroy(handle);
    sampledDepth=bgfx::createFrameBuffer(1,1,bgfx::TextureFormat::D32F,weightedTextureFlags(this->sampleCount));
    CoinBgfxVertex vertices[3]={};
    vertices[0].position[0]=-1;vertices[0].position[1]=-1;
    vertices[1].position[0]=3;vertices[1].position[1]=-1;
    vertices[2].position[0]=-1;vertices[2].position[1]=3;
    for(auto &vertex:vertices){vertex.color[3]=1;vertex.material[2]=1;vertex.material[3]=1;}
    probeVertices=bgfx::createVertexBuffer(bgfx::copy(vertices,sizeof(vertices)),this->layout);
    if(!bgfx::isValid(this->previousDepthSampler))this->previousDepthSampler=bgfx::createUniform("s_prevDepth",bgfx::UniformType::Sampler);
    if(!bgfx::isValid(this->opaqueDepthSampler))this->opaqueDepthSampler=bgfx::createUniform("s_opaqueDepth",bgfx::UniformType::Sampler);
    if(!bgfx::isValid(this->peelMetaUniform))this->peelMetaUniform=bgfx::createUniform("u_peelMeta",bgfx::UniformType::Vec4);
  } else if (bump) {
    // Match the consumed attachment formats and sampleable MSAA contract.
    // A color-only/autoresolved probe would compile a second heavyweight PSO.
    const uint64_t flags = this->sampleCount > 1 ? weightedTextureFlags(this->sampleCount) : peelTextureFlags;
    probeColor = bgfx::createTexture2D(1, 1, false, 1, colorFormat, flags);
    probeDepth = bgfx::createTexture2D(1, 1, false, 1, depthFormat, flags);
    const bgfx::TextureHandle attachments[] = {probeColor, probeDepth};
    if (bgfx::isValid(probeColor) && bgfx::isValid(probeDepth))
      output = bgfx::createFrameBuffer(2, attachments, true);
    if (!bgfx::isValid(output))
      for (auto handle : attachments) if (bgfx::isValid(handle)) bgfx::destroy(handle);
  } else output=bgfx::createFrameBuffer(1,1,bgfx::TextureFormat::RGBA8,probeFlags);
  if (resolveProbe) {
    // Resolve the private sampleable attachments (or convert composition MRT).
    // The existing readback stays single-sample and the blit stays same-format.
    resolvedOutput=bgfx::createFrameBuffer(1,1,bgfx::TextureFormat::RGBA8,peelTextureFlags);
    resolveColorSampler=bgfx::createUniform(this->sampleCount>1?"s_msaaColor":"s_layer",bgfx::UniformType::Sampler);
    if(this->sampleCount>1)resolveDepthSampler=bgfx::createUniform("s_msaaDepth",bgfx::UniformType::Sampler);
    resolveProgram=createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs),COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      this->sampleCount>1?COIN_BGFX_SHADER_DATA(coin_bgfx_fs_msaa_resolve):COIN_BGFX_SHADER_DATA(coin_bgfx_fs_composite),
      this->sampleCount>1?COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_msaa_resolve):COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_composite));
  }

  const auto staging = bgfx::createTexture2D(1, 1, false, 1, (bump && !resolveProbe) ? colorFormat : bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
  if (!bgfx::isValid(output) || !bgfx::isValid(staging) || (composition && (!bgfx::isValid(sampledDepth) || !bgfx::isValid(probeVertices))) ||
      (resolveProbe && (!bgfx::isValid(resolvedOutput) || !bgfx::isValid(resolveProgram) ||
       !bgfx::isValid(resolveColorSampler) || (this->sampleCount>1&&!bgfx::isValid(resolveDepthSampler))))) {
    if (bgfx::isValid(output)) bgfx::destroy(output); if (bgfx::isValid(staging)) bgfx::destroy(staging);
    if(bgfx::isValid(sampledDepth))bgfx::destroy(sampledDepth);
    if(bgfx::isValid(probeVertices))bgfx::destroy(probeVertices);
    releaseResolver();
    this->lastError = "Shadow multitexture probe allocation failed: output="+std::to_string(output.idx)+
      " sampledDepth="+std::to_string(sampledDepth.idx)+" probeVertices="+std::to_string(probeVertices.idx)+
      " staging="+std::to_string(staging.idx)+" color="+std::to_string(probeColor.idx)+
      " auxiliary="+std::to_string(probeAuxiliary.idx)+" depth="+std::to_string(probeDepth.idx)+
      " resolvedOutput="+std::to_string(resolvedOutput.idx)+" resolveProgram="+std::to_string(resolveProgram.idx)+
      " resolveColorSampler="+std::to_string(resolveColorSampler.idx)+" resolveDepthSampler="+std::to_string(resolveDepthSampler.idx)+
      " samples="+std::to_string(this->sampleCount)+" composition="+std::to_string(composition);
    return false;
  }
  if (bump) {
    CoinBgfxVertex vertices[3] = {};
    vertices[0].position[0] = -1;
    vertices[0].position[1] = -1;
    vertices[1].position[0] = 3;
    vertices[1].position[1] = -1;
    vertices[2].position[0] = -1;
    vertices[2].position[1] = 3;
    for (auto &vertex : vertices) {
      vertex.material[0] = 1; // Defined zero specular: pow(0, 64), never pow(0, 0).
      vertex.material[1] = 1;
    }
    probeVertices = bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), this->layout);
    if (!bgfx::isValid(probeVertices)) {
      bgfx::destroy(output);
      bgfx::destroy(staging);
      releaseResolver();
      this->lastError = "Bump receiver capability geometry allocation failed";
      return false;
    }
  }
  bgfx::frame(BGFX_FRAME_FLUSH);
  sharedRuntime().programCommandsMaterialized = sharedRuntime().programCommandsQueued;
  const bgfx::ViewId view = this->viewBase;
  bgfx::resetView(view); bgfx::resetView(view + 1); bgfx::resetView(view + 2);
  bgfx::setViewName(view, "shadow_multitexture_capability"); bgfx::setViewRect(view,0,0,1,1);
  // Every enabled channel differs between the bump probe's clear (white,
  // alpha zero) and shader result (black, alpha one). Disabled channels must
  // retain their clear value, exactly as on the consumed surface pipeline.
  const uint32_t clearColor = bump ? UINT32_C(0xffffff00) : UINT32_C(0xff00ffff);
  bgfx::setViewFrameBuffer(view, output);
  bgfx::setViewClear(view, BGFX_CLEAR_COLOR | ((composition || bump)?BGFX_CLEAR_DEPTH:0), clearColor);
  bgfx::setViewTransform(view,nullptr,nullptr); bgfx::setScissor();
  const float identity[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  const float zero[4] = {}, absent[4] = {-1,-1,-1,-1};
  CoinBgfxDraw draw{}; this->bindDrawLighting(draw,1,1); this->bindDrawTexture(draw,{},true,false,true);
  bgfx::setUniform(this->shadowQualityUniform,zero); bgfx::setUniform(this->shadowFlatParamsUniform,zero);
  bgfx::setUniform(this->shadowFlatDiffuseUniform,zero); bgfx::setUniform(this->shadowFlatSpecularUniform,zero);
  bgfx::setUniform(this->shadowLightIndicesUniform,absent); bgfx::setUniform(this->shadowLightIndicesExtraUniform,absent);
  for(unsigned slot=0;slot<8;++slot) {
    bgfx::setUniform(this->shadowParamsUniform[slot],zero); bgfx::setUniform(this->shadowMetaUniform[slot],zero); bgfx::setUniform(this->shadowTexelUniform[slot],zero);
    bgfx::setUniform(this->shadowViewToClipUniform[slot],identity); bgfx::setUniform(this->shadowViewToLightUniform[slot],identity);

  }
  bgfx::setTexture(10,this->shadowArraySampler,this->defaultShadowArrayTexture);
  if (bump)
    bgfx::setTexture(8, this->bumpImageSampler, this->defaultTexture);
  const float sampleInfo[4] = {1,1,0,0}; bgfx::setUniform(this->depthInfoUniform,sampleInfo);
  if(composition==1) {
    bgfx::setUniform(this->peelMetaUniform,zero);
    bgfx::setTexture(8,this->previousDepthSampler,bgfx::getTexture(sampledDepth));
    bgfx::setTexture(9,this->opaqueDepthSampler,bgfx::getTexture(sampledDepth));
  }
  bgfx::setTransform(identity);
  bgfx::setVertexBuffer(0, (composition || bump) ? probeVertices : this->fullscreenVertexBuffer);
  bgfx::setIndexBuffer(this->fullscreenIndexBuffer, 0, 3);
  bgfx::setSampleMask();
  bgfx::setState((bump ? surfaceState : BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A) |
                 (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
  const uint32_t draws=composition && this->sampleCount>1 && (composition==1 || !bgfxExplicitSurfaceSamples()) ? this->sampleCount : 1;
  for(uint32_t sample=0;sample<draws;++sample) {
    const float info[4]={1,1,float(sample),0}; bgfx::setUniform(this->depthInfoUniform,info);
    if(draws>1)bgfx::setSampleMask(composition==1?peelFixedSampleMask(this->sampleCount,sample):1u<<sample);
    if (portable) {
      // This is the actual linked module: the runtime branch only makes the
      // capability draw's expected output independent of authored parameters.
      const float control[4] = {1, composition ? 1.0f : 0.0f, 0, 0};
      bgfx::setUniform(sharedRuntime().portable.probeControl, control);
    }
    bgfx::submit(view,program,0,sample+1==draws?BGFX_DISCARD_ALL:BGFX_DISCARD_NONE);
  }
  bgfx::setSampleMask();
  if(resolveProbe) {
    bgfx::setViewName(view+1,"shadow_multitexture_capability_resolve");
    bgfx::setViewRect(view+1,0,0,1,1);bgfx::setViewFrameBuffer(view+1,resolvedOutput);
    bgfx::setViewTransform(view+1,nullptr,nullptr);bgfx::setViewClear(view+1,BGFX_CLEAR_NONE);
    const float info[4]={1,1,1,float(this->sampleCount)};
    bgfx::setUniform(this->depthInfoUniform,info);
    bgfx::setTexture(0,resolveColorSampler,probeColor);
    if(this->sampleCount>1)bgfx::setTexture(1,resolveDepthSampler,probeDepth);
    bgfx::setTransform(identity);bgfx::setVertexBuffer(0,this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer,0,3);
    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
    bgfx::submit(view+1,resolveProgram);
  }
  bgfx::TextureRegion dst,src; dst.handle=staging;
  src.handle=bgfx::getTexture(resolveProbe?resolvedOutput:output);
  bgfx::blit(view+(resolveProbe?2:1),dst,src);
  uint32_t pixel=UINT32_MAX;bgfx::TextureRegion read;read.handle=staging;const uint32_t ready=bgfx::read(read,&pixel);
  uint32_t complete=bgfx::frame();while(static_cast<int32_t>(complete-ready)<0)complete=bgfx::frame();
  bgfx::resetView(view);bgfx::resetView(view+1);bgfx::resetView(view+2);bgfx::destroy(output);bgfx::destroy(staging);
  releaseResolver();
  if(bgfx::isValid(sampledDepth))bgfx::destroy(sampledDepth);
  if(bgfx::isValid(probeVertices))bgfx::destroy(probeVertices);
  uint32_t expectedPixel = composition ? UINT32_C(0xff000000) : 0;
  if (bump) {
    uint8_t expected[4] = {
      uint8_t(surfaceState & BGFX_STATE_WRITE_R ? 0 : 255),
      uint8_t(surfaceState & BGFX_STATE_WRITE_G ? 0 : 255),
      uint8_t(surfaceState & BGFX_STATE_WRITE_B ? 0 : 255),
      uint8_t(surfaceState & BGFX_STATE_WRITE_A ? 255 : 0)
    };
    // Direct readback retains the image's storage order. The MSAA resolver
    // instead writes RGBA8, regardless of the source attachment format.
    if (!resolveProbe && colorFormat == bgfx::TextureFormat::BGRA8)
      std::swap(expected[0], expected[2]);
    std::memcpy(&expectedPixel, expected, sizeof(expectedPixel));
  }
  if (this->checkRuntimeFailure("Shadow multitexture capability probe failed") !=
          CoinRenderBackendStatus::SUCCESS || pixel != expectedPixel) {
    if(this->lastError.empty())this->lastError="Shadow multitexture program failed its complete pipeline draw";
    return false;
  }
  verified=true; return true;
}

bool
CoinBgfxBackend::prepareShadowTransparencyPrograms()
{
  if (bgfx::getCaps()->limits.maxTextureSamplers < 16) {
    this->lastError = "Shadow peeling/OIT requires sixteen texture stages";
    return false;
  }
  if (!bgfx::isValid(this->shadowPeelProgram))
    this->shadowPeelProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_peel),
      COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_peel));
  if (!bgfx::isValid(this->shadowOitProgram))
    this->shadowOitProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_oit),
      COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_oit));
  if (this->sampleCount > 1 && !bgfx::isValid(this->shadowPeelMsaaProgram))
    this->shadowPeelMsaaProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver),
      COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_peel_msaa), COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_peel_msaa));
  const bool valid = bgfx::isValid(this->shadowPeelProgram) && bgfx::isValid(this->shadowOitProgram) &&
    (this->sampleCount == 1 || bgfx::isValid(this->shadowPeelMsaaProgram));
  if (!valid) this->lastError = "Shadow transparency shader allocation failed";
  return valid;
}

CoinBgfxBackend::~CoinBgfxBackend()
{
  const auto trace = [this](const char *stage) {
    bgfxTraceLifecycle(this, stage, sharedRuntime().portable.programCount.load(std::memory_order_relaxed),
        sharedRuntime().portable.uniformCount.load(std::memory_order_relaxed));
  };
  trace("backend_destructor_begin");
  this->shutdownRuntime();
  trace("backend_destructor_end");
}

CoinRenderBackendStatus
CoinBgfxBackend::checkRuntimeFailure(const char * operation)
{
  CoinBgfxCallback * cb = static_cast<CoinBgfxCallback *>(this->callback.get());
#ifdef _WIN32
  if (this->initialized && this->onApiThread() && cb && !cb->failed() &&
      bgfx::getRendererType() == bgfx::RendererType::Direct3D12 &&
      consumeTestFault("COIN_BGFX_TEST_REMOVE_NATIVE_DEVICE_ONCE")) {
    // A controlled runtime removal of this process's real D3D12 device.
    // It does not disable/reset the display adapter or emulate a driver TDR.
    auto * device = static_cast<ID3D12Device *>(bgfx::getInternalData()->context);
    ID3D12Device5 * removable = nullptr;
    const HRESULT queried = device->QueryInterface(__uuidof(ID3D12Device5),
                                                   reinterpret_cast<void **>(&removable));
    if (FAILED(queried)) {
      this->lastError = "D3D12 runtime removal interface unavailable";
      return CoinRenderBackendStatus::UNSUPPORTED;
    }
    const HRESULT before = device->GetDeviceRemovedReason();
    removable->RemoveDevice();
    const HRESULT after = device->GetDeviceRemovedReason();
    removable->Release();
    std::fprintf(stderr, "COIN_NATIVE_DEVICE_REMOVAL api=d3d12 before=0x%08lx after=0x%08lx runtime_remove=1 adapter_reset=0\n",
                 static_cast<unsigned long>(before), static_cast<unsigned long>(after));
    if (SUCCEEDED(after)) {
      this->lastError = "D3D12 RemoveDevice did not remove the device";
      return CoinRenderBackendStatus::UNSUPPORTED;
    }
    // Let BGFX observe removal itself, including its native swapchains.
    // Mirroring only the callback state can leave a removed window device
    // alive in DXGI and prevent recreation of the explicitly selected API.
    for (unsigned observe = 0; observe < 3 && !cb->failed(); ++observe)
      bgfx::frame();
    if (cb->failed()) return this->checkRuntimeFailure(operation);
    char reason[96];
    std::snprintf(reason, sizeof(reason), "Native D3D12 device removed, HRESULT 0x%08lx",
                  static_cast<unsigned long>(after));
    cb->fatal("D3D12 runtime RemoveDevice", 0, bgfx::Fatal::DeviceLost, reason);
  }
#endif
  if (cb == nullptr || !cb->failed()) return CoinRenderBackendStatus::SUCCESS;
  this->status = cb->deviceLost() ? CoinRenderBackendStatus::DEVICE_LOST :
                                   CoinRenderBackendStatus::BACKEND_ERROR;
  this->lastError = std::string(operation) + ": " + cb->diagnostic();
  return this->status;
}

void
CoinBgfxBackend::destroyResources()
{
  const auto trace = [this](const char *stage) {
    bgfxTraceLifecycle(this, stage, sharedRuntime().portable.programCount.load(std::memory_order_relaxed),
        sharedRuntime().portable.uniformCount.load(std::memory_order_relaxed));
  };
  trace("destroy_resources_begin");
  this->bumpReuseFrame.reset();
  this->samplingBoundTextureSizes.clear();
  if (!this->initialized) { trace("destroy_resources_end_uninitialized"); return; }
  this->releasePortablePreflightPins();
  // Handles are no longer trustworthy after a fatal renderer/device error.
  // bgfx::shutdown() owns their final release in that case.
  CoinBgfxCallback * cb = static_cast<CoinBgfxCallback *>(this->callback.get());
  if (cb != nullptr && cb->failed()) {
    for (auto & slot : this->readbackSlots) {
      auto entry = std::make_shared<AsyncEntry>();
      entry->slot = std::move(slot);
      failedReadbacks.push_back(entry);
    }
    this->readbackSlots.clear();
    trace("destroy_resources_end_failed_runtime");
    return;
  }
  this->destroyFrameBuffers();
  for (DirectTextureResource & resource : this->directTextures)
    if (bgfx::isValid(resource.frameBuffer)) bgfx::destroy(resource.frameBuffer);
  if (bgfx::isValid(this->fullscreenVertexBuffer)) bgfx::destroy(this->fullscreenVertexBuffer);
  if (bgfx::isValid(this->fullscreenIndexBuffer)) bgfx::destroy(this->fullscreenIndexBuffer);
  if (bgfx::isValid(this->previousDepthSampler)) bgfx::destroy(this->previousDepthSampler);
  if (bgfx::isValid(this->opaqueDepthSampler)) bgfx::destroy(this->opaqueDepthSampler);
  if (bgfx::isValid(this->layerSampler)) bgfx::destroy(this->layerSampler);
  if (bgfx::isValid(this->layerMaskSampler)) bgfx::destroy(this->layerMaskSampler);
  if (bgfx::isValid(this->layerDepthSampler)) bgfx::destroy(this->layerDepthSampler);
  if (bgfx::isValid(this->peelMetaUniform)) bgfx::destroy(this->peelMetaUniform);
  if (bgfx::isValid(this->npotMipSampler)) bgfx::destroy(this->npotMipSampler);
  if (bgfx::isValid(this->npotMipExtentUniform)) bgfx::destroy(this->npotMipExtentUniform);
  if (bgfx::isValid(this->depthInfoUniform)) bgfx::destroy(this->depthInfoUniform);
  if (bgfx::isValid(this->screenDoorUniform)) bgfx::destroy(this->screenDoorUniform);
  if (bgfx::isValid(this->alphaTestUniform)) bgfx::destroy(this->alphaTestUniform);
  if (bgfx::isValid(this->rasterViewportUniform)) bgfx::destroy(this->rasterViewportUniform);
  if (bgfx::isValid(this->clipMetaUniform)) bgfx::destroy(this->clipMetaUniform);
  if (bgfx::isValid(this->clipPlanesUniform)) bgfx::destroy(this->clipPlanesUniform);
  if (bgfx::isValid(this->coinDepthUniform)) bgfx::destroy(this->coinDepthUniform);
  if (bgfx::isValid(this->oitAccumSampler)) bgfx::destroy(this->oitAccumSampler);
  if (bgfx::isValid(this->oitRevealSampler)) bgfx::destroy(this->oitRevealSampler);
  if (bgfx::isValid(this->textureSampler)) bgfx::destroy(this->textureSampler);
  for (auto handle : this->extraTextureSamplers)
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  if (bgfx::isValid(this->fogColorModeUniform)) bgfx::destroy(this->fogColorModeUniform);
  if (bgfx::isValid(this->fogRangeUniform)) bgfx::destroy(this->fogRangeUniform);
  if (bgfx::isValid(this->samplingTextureSizesUniform)) bgfx::destroy(this->samplingTextureSizesUniform);
  if (bgfx::isValid(this->textureParamsUniform)) bgfx::destroy(this->textureParamsUniform);
  if (bgfx::isValid(this->textureBlendUniform)) bgfx::destroy(this->textureBlendUniform);
  if (bgfx::isValid(this->textureCombineUniform)) bgfx::destroy(this->textureCombineUniform);
  if (bgfx::isValid(this->ambientLightUniform)) bgfx::destroy(this->ambientLightUniform);
  if (bgfx::isValid(this->lightCountUniform)) bgfx::destroy(this->lightCountUniform);
  if (bgfx::isValid(this->lightPositionTypeUniform)) bgfx::destroy(this->lightPositionTypeUniform);
  if (bgfx::isValid(this->lightDirectionCutoffUniform)) bgfx::destroy(this->lightDirectionCutoffUniform);
  if (bgfx::isValid(this->lightColorIntensityUniform)) bgfx::destroy(this->lightColorIntensityUniform);
  if (bgfx::isValid(this->lightAmbientColorUniform)) bgfx::destroy(this->lightAmbientColorUniform);
  if (bgfx::isValid(this->instancedCameraUniform)) bgfx::destroy(this->instancedCameraUniform);
  if (bgfx::isValid(this->lightAttenuationDropUniform)) bgfx::destroy(this->lightAttenuationDropUniform);
  if (bgfx::isValid(this->defaultTexture)) bgfx::destroy(this->defaultTexture);
  for (bgfx::TextureHandle texture : this->cachedTextures)
    if (bgfx::isValid(texture)) bgfx::destroy(texture);
  if (bgfx::isValid(this->cachedVertexBuffer)) bgfx::destroy(this->cachedVertexBuffer);
  if (bgfx::isValid(this->cachedIndexBuffer)) bgfx::destroy(this->cachedIndexBuffer);
  if (bgfx::isValid(this->cachedInstanceBuffer)) bgfx::destroy(this->cachedInstanceBuffer);
  if (bgfx::isValid(this->peelNextProgram)) bgfx::destroy(this->peelNextProgram);
  if (bgfx::isValid(this->peelMsaaProgram)) bgfx::destroy(this->peelMsaaProgram);
  if (bgfx::isValid(this->shadowPeelMsaaProgram)) bgfx::destroy(this->shadowPeelMsaaProgram);
  if (bgfx::isValid(this->peelMsaaCompositeProgram)) bgfx::destroy(this->peelMsaaCompositeProgram);
  if (bgfx::isValid(this->peelMsaaColorSampler)) bgfx::destroy(this->peelMsaaColorSampler);
  if (bgfx::isValid(this->peelMsaaMaskSampler)) bgfx::destroy(this->peelMsaaMaskSampler);
  if (bgfx::isValid(this->peelMsaaDepthSampler)) bgfx::destroy(this->peelMsaaDepthSampler);
  if (bgfx::isValid(this->compositeProgram)) bgfx::destroy(this->compositeProgram);
  if (bgfx::isValid(this->compositeDepthProgram)) bgfx::destroy(this->compositeDepthProgram);
  for (auto handle : {this->multipassSumProgram, this->multipassMeanProgram})
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  for (auto handle : {this->multipassSceneSampler, this->multipassPreviousSampler, this->multipassInfoUniform})
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  if (bgfx::isValid(this->npotMipProgram)) bgfx::destroy(this->npotMipProgram);
  if (bgfx::isValid(this->rasterImageProgram)) bgfx::destroy(this->rasterImageProgram);
  if (bgfx::isValid(this->rasterPrimarySampler)) bgfx::destroy(this->rasterPrimarySampler);
  if (bgfx::isValid(this->rasterCoordinatesUniform)) bgfx::destroy(this->rasterCoordinatesUniform);
  if (bgfx::isValid(this->rasterExtentUniform)) bgfx::destroy(this->rasterExtentUniform);
  if (bgfx::isValid(this->msaaProgram)) bgfx::destroy(this->msaaProgram);
  if (bgfx::isValid(this->msaaColorSampler)) bgfx::destroy(this->msaaColorSampler);
  if (bgfx::isValid(this->msaaDepthSampler)) bgfx::destroy(this->msaaDepthSampler);
  if (bgfx::isValid(this->depthReadProgram)) bgfx::destroy(this->depthReadProgram);
  if (bgfx::isValid(this->readDepthSampler)) bgfx::destroy(this->readDepthSampler);
  for (auto handle : {this->bumpProgram, this->bumpNativeDepthProgram, this->bumpSampleDepthProgram,
                      this->bumpReceiverProgram, this->bumpReceiverNativeProgram,
                      this->bumpReceiverSampleProgram})
    if (bgfx::isValid(handle))
      bgfx::destroy(handle);
  for(auto handle:{this->bumpImageSampler,this->bumpBaseSampler,this->bumpParamsUniform})if(bgfx::isValid(handle))bgfx::destroy(handle);
  for (auto handle : {this->bumpLightMetaUniform,this->bumpLightVectorsUniform,
                       this->bumpLightColorsUniform,this->bumpEyeUniform,this->bumpModelViewUniform})
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  this->bumpLightMetaUniform=this->bumpLightVectorsUniform=this->bumpLightColorsUniform=
    this->bumpEyeUniform=this->bumpModelViewUniform=BGFX_INVALID_HANDLE;
  this->bumpReceiverVerifiedPipelines.clear();
  this->bumpProgram=this->bumpNativeDepthProgram=this->bumpSampleDepthProgram=BGFX_INVALID_HANDLE;
  this->bumpReceiverProgram = this->bumpReceiverNativeProgram = this->bumpReceiverSampleProgram =
      BGFX_INVALID_HANDLE;
  this->bumpImageSampler=this->bumpBaseSampler=this->bumpParamsUniform=BGFX_INVALID_HANDLE;
  trace("destroy_portable_resources_begin");
  this->portableProgramRoutes.clear();
  trace("destroy_portable_resources_end");
  if (bgfx::isValid(this->program)) bgfx::destroy(this->program);
  if (bgfx::isValid(this->solidProgram)) bgfx::destroy(this->solidProgram);
  if (bgfx::isValid(this->texture2dProgram)) bgfx::destroy(this->texture2dProgram);
  for (auto handle : {this->texture2dNativeDepthProgram, this->texture2dSampleDepthProgram})
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  this->texture2dNativeDepthProgram = this->texture2dSampleDepthProgram = BGFX_INVALID_HANDLE;
  this->activeTexture2dDepth = false;
  if (bgfx::isValid(this->instancedProgram)) bgfx::destroy(this->instancedProgram);
  if (bgfx::isValid(this->shadowMomentsProgram)) bgfx::destroy(this->shadowMomentsProgram);
  if (bgfx::isValid(this->surfaceNativeDepthProgram)) bgfx::destroy(this->surfaceNativeDepthProgram);
  if (bgfx::isValid(this->surfaceSampleDepthProgram)) bgfx::destroy(this->surfaceSampleDepthProgram);
  if (bgfx::isValid(this->shadowNativeDepthProgram)) bgfx::destroy(this->shadowNativeDepthProgram);
  if (bgfx::isValid(this->shadowSampleDepthProgram)) bgfx::destroy(this->shadowSampleDepthProgram);
  if (bgfx::isValid(this->shadowMultitexturePeelProgram)) bgfx::destroy(this->shadowMultitexturePeelProgram);
  if (bgfx::isValid(this->shadowMultitextureOitProgram)) bgfx::destroy(this->shadowMultitextureOitProgram);
  if (bgfx::isValid(this->shadowArrayTexture)) bgfx::destroy(this->shadowArrayTexture);
  if (bgfx::isValid(this->defaultShadowArrayTexture)) bgfx::destroy(this->defaultShadowArrayTexture);
  if (bgfx::isValid(this->shadowArraySampler)) bgfx::destroy(this->shadowArraySampler);
  if (bgfx::isValid(this->shadowMultitextureProgram)) bgfx::destroy(this->shadowMultitextureProgram);
  if (bgfx::isValid(this->shadowMultitextureSampleDepthProgram)) bgfx::destroy(this->shadowMultitextureSampleDepthProgram);
  if (bgfx::isValid(this->instancedNativeDepthProgram)) bgfx::destroy(this->instancedNativeDepthProgram);
  if (bgfx::isValid(this->instancedSampleDepthProgram)) bgfx::destroy(this->instancedSampleDepthProgram);
  if (bgfx::isValid(this->shadowReceiverProgram)) bgfx::destroy(this->shadowReceiverProgram);
  if (bgfx::isValid(this->shadowReceiverProgram4)) bgfx::destroy(this->shadowReceiverProgram4);
  if (bgfx::isValid(this->shadowReceiverProgram8)) bgfx::destroy(this->shadowReceiverProgram8);
  if (bgfx::isValid(this->shadowPeelProgram)) bgfx::destroy(this->shadowPeelProgram);
  if (bgfx::isValid(this->shadowOitProgram)) bgfx::destroy(this->shadowOitProgram);
  if (bgfx::isValid(this->shadowLightIndicesExtraUniform)) bgfx::destroy(this->shadowLightIndicesExtraUniform);
  if (bgfx::isValid(this->shadowModelViewUniform)) bgfx::destroy(this->shadowModelViewUniform);
  if (bgfx::isValid(this->shadowClipModelViewUniform)) bgfx::destroy(this->shadowClipModelViewUniform);
  if (bgfx::isValid(this->shadowDepthUniform)) bgfx::destroy(this->shadowDepthUniform);
  if (bgfx::isValid(this->shadowQualityUniform)) bgfx::destroy(this->shadowQualityUniform);
  if (bgfx::isValid(this->shadowFlatDiffuseUniform)) bgfx::destroy(this->shadowFlatDiffuseUniform);
  if (bgfx::isValid(this->shadowFlatSpecularUniform)) bgfx::destroy(this->shadowFlatSpecularUniform);
  if (bgfx::isValid(this->shadowFlatParamsUniform)) bgfx::destroy(this->shadowFlatParamsUniform);
  if (bgfx::isValid(this->shadowLightIndicesUniform)) bgfx::destroy(this->shadowLightIndicesUniform);
  for (int i = 0; i < 8; ++i) {
    if (bgfx::isValid(this->shadowViewToClipUniform[i])) bgfx::destroy(this->shadowViewToClipUniform[i]);
    if (bgfx::isValid(this->shadowViewToLightUniform[i])) bgfx::destroy(this->shadowViewToLightUniform[i]);
    if (bgfx::isValid(this->shadowParamsUniform[i])) bgfx::destroy(this->shadowParamsUniform[i]);
    if (bgfx::isValid(this->shadowMetaUniform[i])) bgfx::destroy(this->shadowMetaUniform[i]);
    if (bgfx::isValid(this->shadowTexelUniform[i])) bgfx::destroy(this->shadowTexelUniform[i]);
    if (bgfx::isValid(this->shadowSampler[i])) bgfx::destroy(this->shadowSampler[i]);
  }
  if (bgfx::isValid(this->weightedOitProgram)) bgfx::destroy(this->weightedOitProgram);
  if (bgfx::isValid(this->weightedCompositeProgram)) bgfx::destroy(this->weightedCompositeProgram);
  if (bgfx::isValid(this->weightedMsaaCompositeProgram)) bgfx::destroy(this->weightedMsaaCompositeProgram);
  if (bgfx::isValid(this->weightedMsaaAccumSampler)) bgfx::destroy(this->weightedMsaaAccumSampler);
  if (bgfx::isValid(this->weightedMsaaRevealSampler)) bgfx::destroy(this->weightedMsaaRevealSampler);
  trace("destroy_resources_end");
}

void
CoinBgfxBackend::shutdownRuntime()
{
  const auto trace = [this](const char *stage) {
    bgfxTraceLifecycle(this, stage, sharedRuntime().portable.programCount.load(std::memory_order_relaxed),
        sharedRuntime().portable.uniformCount.load(std::memory_order_relaxed));
  };
  trace("shutdown_runtime_begin");
  if (!this->initialized) { trace("shutdown_runtime_end_uninitialized"); return; }
  // Release this target without interrupting other windows on the shared device.
  if (!this->onApiThread()) {
    std::fprintf(stderr, "BGFX evaluation: destroy the target on its API thread; global renderer left active\n");
    trace("shutdown_runtime_end_wrong_thread");
    return;
  }
  this->destroyResources();
  SharedBgfxRuntime & runtime = sharedRuntime();
  trace("runtime_mutex_wait");
  std::lock_guard<std::mutex> guard(runtime.mutex);
  bgfxTraceLifecycle(this, "runtime_mutex_acquired", runtime.portable.programCount.load(std::memory_order_relaxed),
      runtime.portable.uniformCount.load(std::memory_order_relaxed),
      runtime.references);
  if (!static_cast<CoinBgfxCallback *>(runtime.callback.get())->failed()) {
    for (bgfx::ViewId view = this->viewBase;
         view < this->viewBase + this->viewCount; ++view) bgfx::resetView(view);
  }
  for(size_t i=this->viewBase/targetViewCount;i<(uint32_t(this->viewBase)+this->viewCount)/targetViewCount;++i) runtime.viewBlocks[i]=false;
  if (--runtime.references == 0) {
    runtime.portable.clear(!static_cast<CoinBgfxCallback *>(runtime.callback.get())->failed());
    trace("bgfx_shutdown_begin");
    bgfx::shutdown();
    trace("bgfx_shutdown_end");
    runtime.releaseNativeDisplay();
    failedReadbacks.clear();
    runtime.callback.reset();
    runtime.renderer = bgfx::RendererType::Count;
    runtime.apiThread = std::thread::id();
    runtime.viewBlocks.clear();
  } else if (!static_cast<CoinBgfxCallback *>(runtime.callback.get())->failed()) {
    // Surface destruction is queued to the render worker. Retire it before
    // the caller destroys the native window, while retaining other targets.
    // The second frame waits for consumption of the first frame's commands.
    trace("peer_frame_1_begin");
    bgfx::frame();
    trace("peer_frame_1_end");
    trace("peer_frame_2_begin");
    bgfx::frame();
    trace("peer_frame_2_end");
  }
  this->callback.reset();
  this->initialized = false;
  trace("shutdown_runtime_end");
}

bool
CoinBgfxBackend::reserveViews(uint32_t required) {
  if (required <= this->viewCount) return true;
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  const size_t blocks = (required + targetViewCount - 1) / targetViewCount;
  const size_t oldFirst = this->viewBase / targetViewCount, oldCount = this->viewCount / targetViewCount;
  size_t selected = runtime.viewBlocks.size();
  const auto available = [&](size_t first) {
    if (first + blocks > runtime.viewBlocks.size()) return false;
    for (size_t i=first; i<first+blocks; ++i)
      if (runtime.viewBlocks[i] && !(i>=oldFirst && i<oldFirst+oldCount)) return false;
    return true;
  };
  if (available(oldFirst)) selected = oldFirst;
  else for(size_t first=0; first<runtime.viewBlocks.size(); ++first) if(available(first)){selected=first;break;}
  if (selected == runtime.viewBlocks.size()) { this->lastError = "BGFX ordered composition has no contiguous free view span"; return false; }
  for(uint32_t view=this->viewBase; view<uint32_t(this->viewBase)+this->viewCount; ++view) bgfx::resetView(bgfx::ViewId(view));
  for(size_t i=oldFirst;i<oldFirst+oldCount;++i) runtime.viewBlocks[i]=false;
  for(size_t i=selected;i<selected+blocks;++i) runtime.viewBlocks[i]=true;
  this->viewBase=bgfx::ViewId(selected*targetViewCount);this->viewCount=uint32_t(blocks*targetViewCount);
  return true;
}

bool
CoinBgfxBackend::canReserveGraphViews(uint32_t rootViews, uint32_t producerViews,
  bool staged, std::string & diagnostic) const
{
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  const size_t oldFirst = this->viewBase / targetViewCount;
  const size_t oldCount = this->viewCount / targetViewCount;
  const uint32_t parentViews = staged ? rootViews : std::max(rootViews,producerViews);
  const size_t parentCount = (std::max(parentViews,this->viewCount)+targetViewCount-1)/targetViewCount;
  const size_t childCount = staged ? (std::max(uint32_t(targetViewCount),producerViews)+targetViewCount-1)/targetViewCount : 0;
  const auto owns = [&](size_t block) { return block >= oldFirst && block < oldFirst+oldCount; };
  // A STAGED producer first prepares its own backend while the live parent's
  // existing span remains owned. Probes must not move or grow that parent.
  if (childCount) {
    bool childFitsCurrent = false;
    for(size_t first=0;first+childCount<=runtime.viewBlocks.size();++first) {
      bool free=true;
      for(size_t i=first;i<first+childCount;++i) if(runtime.viewBlocks[i]) { free=false;break; }
      if(free) { childFitsCurrent=true;break; }
    }
    if(!childFitsCurrent) { diagnostic="BGFX STAGED producer has no free contiguous view span beside its live parent";return false; }
  }
  // Predict a real contiguous parent allocation, and (for STAGED) one largest
  // sequential child beside it. Other live targets remain occupied. No block
  // is retained by this inspection, including when admission succeeds.
  for(size_t first=0;first+parentCount<=runtime.viewBlocks.size();++first) {
    bool parentFits=true;
    for(size_t i=first;i<first+parentCount;++i)
      if(runtime.viewBlocks[i] && !owns(i)) { parentFits=false;break; }
    if(!parentFits) continue;
    if(!childCount) return true;
    for(size_t child=0;child+childCount<=runtime.viewBlocks.size();++child) {
      bool childFits=true;
      for(size_t i=child;i<child+childCount;++i)
        if((i>=first && i<first+parentCount) || (runtime.viewBlocks[i]&&!owns(i))) { childFits=false;break; }
      if(childFits) return true;
    }
  }
  diagnostic=staged ? "BGFX STAGED parent and largest sequential producer cannot coexist in contiguous view spans"
                    : "BGFX DIRECT graph has no free contiguous shared view span";
  return false;
}

bool
CoinBgfxBackend::onApiThread() const
{
  return this->apiThread == std::this_thread::get_id();
}

CoinRenderBackendStatus
CoinBgfxBackend::prepare(CoinRenderTargetP & target)
{
  this->preparedDepthReadbackEnabled = target.depthReadbackEnabled;
  if (this->initialized) {
    if (!this->onApiThread()) {
      this->lastError = "BGFX prepare requires the runtime API thread";
      return CoinRenderBackendStatus::UNSUPPORTED;
    }
    this->checkRuntimeFailure("BGFX shared renderer failed");
    return this->status;
  }
  this->presentToWindow = target.kind == CoinRenderTargetP::KIND_WINDOW;
#ifdef _WIN32
  if (this->presentToWindow &&
      (target.nativeDesc.type != COIN_RENDER_SURFACE_WIN32 ||
       target.nativeDesc.native.win32.hwnd == nullptr)) {
    this->lastError = "BGFX window presentation requires a valid Win32 surface";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
#else
  if (this->presentToWindow &&
      (target.nativeDesc.type != COIN_RENDER_SURFACE_XLIB ||
       target.nativeDesc.native.xlib.display == nullptr ||
       target.nativeDesc.native.xlib.window == 0)) {
    this->lastError = "BGFX window presentation requires a valid Xlib surface";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
#endif
  if (!target.optionsDiagnostic.empty() ||
      !coin_render_valid_options(target.options, this->lastError)) {
    if (!target.optionsDiagnostic.empty())
      this->lastError = target.optionsDiagnostic;
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  this->sampleCount = target.options.sampleCount;
  this->portableSampling = target.options.textureSamplingPolicy == COIN_RENDER_SAMPLING_PORTABLE;
  this->canonicalCoverage = target.options.rasterizationPolicy == COIN_RENDER_RASTER_CANONICAL_8BIT;
  switch (target.options.transparency) {
  case COIN_RENDER_TRANSPARENCY_COIN:
    this->transparencyMode = CoinBgfxTransparencyMode::AUTO;
    break;
  case COIN_RENDER_TRANSPARENCY_OBJECT:
    this->transparencyMode = CoinBgfxTransparencyMode::OBJECT;
    break;
  case COIN_RENDER_TRANSPARENCY_PEELING:
    this->transparencyMode = CoinBgfxTransparencyMode::SORTED_LAYERS;
    break;
  case COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT:
    this->transparencyMode = CoinBgfxTransparencyMode::WEIGHTED_OIT;
    break;
  }
  this->activeTransparencyStrategy = CoinBgfxTransparencyStrategy::OBJECT;
  if (target.options.renderer == COIN_RENDER_RENDERER_METAL
#ifndef _WIN32
      || target.options.renderer == COIN_RENDER_RENDERER_D3D12
#endif
      ) {
    this->lastError = "Requested BGFX renderer is not supported on this platform";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  const bool useOpenGl = target.options.renderer == COIN_RENDER_RENDERER_OPENGL;
  bgfx::RendererType::Enum renderer =
      useOpenGl ? bgfx::RendererType::OpenGL : bgfx::RendererType::Vulkan;
#ifdef _WIN32
  if (target.options.renderer == COIN_RENDER_RENDERER_D3D12 ||
      target.options.renderer == COIN_RENDER_RENDERER_UNKNOWN)
    renderer = bgfx::RendererType::Direct3D12;
#endif
  SharedBgfxRuntime & runtime = sharedRuntime();
  std::unique_lock<std::mutex> runtimeGuard(runtime.mutex);
  if (runtime.references != 0 && runtime.apiThread != std::this_thread::get_id()) {
    this->lastError = "BGFX targets must share the runtime API thread";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  if (runtime.references != 0 && runtime.renderer != renderer) {
    this->lastError = "BGFX targets must share the active renderer";
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  if (runtime.references != 0 &&
      static_cast<CoinBgfxCallback *>(runtime.callback.get())->failed()) {
    this->callback = runtime.callback;
    this->checkRuntimeFailure("BGFX shared renderer failed before target creation");
    return this->status;
  }
#if defined(__linux__) && !defined(__ANDROID__)
  if (this->presentToWindow && renderer == bgfx::RendererType::Vulkan &&
      !coin_render_x11_wsi::prepare(static_cast<Display *>(target.nativeDesc.native.xlib.display),
                                   this->lastError)) {
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
#endif
#ifdef _WIN32
  this->nativeDisplay = nullptr;
  this->nativeWindow = this->presentToWindow ? target.nativeDesc.native.win32.hwnd : nullptr;
#else
  this->nativeDisplay = this->presentToWindow ? target.nativeDesc.native.xlib.display : nullptr;
  this->nativeWindow = this->presentToWindow ? reinterpret_cast<void *>(
    static_cast<uintptr_t>(target.nativeDesc.native.xlib.window)) : nullptr;
#if defined(__linux__) && !defined(__ANDROID__)
  if (this->presentToWindow && renderer == bgfx::RendererType::OpenGL) {
    // Window creation may still be buffered on the caller's connection. The
    // renderer's separate connection must see the server resource before EGL
    // creates a surface for its XID.
    XSync(static_cast<Display *>(this->nativeDisplay), False);
  }
#endif
  if (this->presentToWindow && CoinRenderDiagnosticShell::phaseTracingEnabled()) {
    XWindowAttributes attributes;
    if (XGetWindowAttributes(static_cast<Display *>(this->nativeDisplay),
                             static_cast<Window>(target.nativeDesc.native.xlib.window), &attributes)) {
      std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_xlib visual=0x%lx depth=%d display=%s\n",
        XVisualIDFromVisual(attributes.visual), attributes.depth,
        XDisplayString(static_cast<Display *>(this->nativeDisplay)));
    }
  }
#endif
  if (runtime.references == 0) {
    runtime.portable.clear(false);
    runtime.weightedSamplesVerified = 0;
    runtime.weightedSamplesRejected = 0;
    runtime.peelingSamplesVerified = 0;
    runtime.peelingSamplesRejected = 0;
    runtime.programCommandsQueued = 0;
    runtime.programCommandsMaterialized = 0;
    bgfx::Init init;
    init.type = renderer;
    init.reset |= BGFX_RESET_MAXANISOTROPY;
    runtime.callback = std::make_shared<CoinBgfxCallback>(renderer == bgfx::RendererType::OpenGL);
    init.callback = runtime.callback.get();
    // No visible window owns the primary swapchain. Closing the first target
    // therefore cannot invalidate any other target's renderer/context.
    init.swapChain.ndt = this->nativeDisplay;
#if defined(__linux__) && !defined(__ANDROID__)
    if (renderer == bgfx::RendererType::OpenGL) {
      const char * displayName = this->nativeDisplay
        ? XDisplayString(static_cast<Display *>(this->nativeDisplay)) : nullptr;
      runtime.openGlDisplay.reset(XOpenDisplay(displayName));
      if (!runtime.openGlDisplay) {
        runtime.callback.reset();
        this->lastError = "BGFX OpenGL could not open its private X11 display connection";
        return CoinRenderBackendStatus::NOT_READY;
      }
      init.swapChain.ndt = runtime.openGlDisplay.get();
    }
#endif
    init.swapChain.nwh = nullptr;
    init.swapChain.width = 0;
    init.swapChain.height = 0;
    // Coin submits on one API thread and uses persistent buffers for geometry,
    // instances and fullscreen passes. BGFX's debug text owns separate pools.
    // Keep small, nonzero transient pools for BGFX's internal frame lifecycle;
    // the private opt-out restores its original reservations for comparisons.
    const char * disableSmallReservations =
      std::getenv("COIN_BGFX_DISABLE_SMALL_RUNTIME_RESERVATIONS");
    if (!(disableSmallReservations && std::strcmp(disableSmallReservations, "1") == 0)) {
      init.limits.maxEncoders = 1;
      init.limits.maxTransientVbSize = 64u * 1024u;
      init.limits.maxTransientIbSize = 64u * 1024u;
    }
    if (!bgfx::init(init)) {
      runtime.releaseNativeDisplay();
      runtime.callback.reset();
      this->lastError = std::string("BGFX could not initialize its headless renderer: ") + bgfx::getRendererName(renderer);
      return CoinRenderBackendStatus::NOT_READY;
    }
    // Secondary swapchains resolve Count from this exact Init descriptor in
    // Context::readSwapChain. RendererVK consumes that format without fallback.
    runtime.backbufferColorFormat = init.swapChain.formatColor;
    runtime.apiThread = std::this_thread::get_id();
    ++runtime.generation;
    runtime.renderer = renderer;
    runtime.viewBlocks.assign(bgfx::getCaps()->limits.maxViews / targetViewCount, false);
  }
  const auto freeBlock = std::find(runtime.viewBlocks.begin(), runtime.viewBlocks.end(), false);
  if (freeBlock == runtime.viewBlocks.end()) {
    this->lastError = "BGFX target view budget exhausted";
    if (runtime.references == 0) {
      runtime.portable.clear(true);
      bgfx::shutdown();
      runtime.releaseNativeDisplay();
      runtime.callback.reset();
      runtime.viewBlocks.clear();
    }
    return CoinRenderBackendStatus::UNSUPPORTED;
  }
  this->viewBase = static_cast<bgfx::ViewId>(
    std::distance(runtime.viewBlocks.begin(), freeBlock) * targetViewCount);
  this->viewCount = targetViewCount;
  *freeBlock = true;
  ++runtime.references;
  this->callback = runtime.callback;
  runtimeGuard.unlock();
  this->initialized = true;
  this->apiThread = std::this_thread::get_id();
  if (consumeTestFault("COIN_BGFX_TEST_FAIL_PREPARE_ONCE")) {
    this->lastError = "Injected BGFX failure after runtime initialization";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return this->status;
  }
  const bgfx::Caps * caps = bgfx::getCaps();
  if (caps->rendererType != renderer) {
    this->lastError = "Requested BGFX renderer does not match the active renderer";
    this->status = CoinRenderBackendStatus::UNSUPPORTED;
    return this->status;
  }
  if (!target.capabilityProbeOnly &&
      caps->limits.maxTextureSamplers < COIN_RENDER_MAX_TEXTURE_UNITS + 2) {
    this->lastError = "BGFX renderer needs ten texture samplers for eight units and transparency";
    this->status = CoinRenderBackendStatus::UNSUPPORTED;
    return this->status;
  }
  if (CoinRenderDiagnosticShell::phaseTracingEnabled()) {
    std::fprintf(stderr,
      "COIN_RENDER_PHASE bgfx_device renderer=%s vendor_id=0x%04x device_id=0x%04x homogeneous_depth=%d\n",
      bgfx::getRendererName(renderer),
      static_cast<unsigned int>(caps->vendorId),
      static_cast<unsigned int>(caps->deviceId),
      caps->homogeneousDepth ? 1 : 0);
    std::fprintf(stderr,
      "COIN_RENDER_PHASE bgfx_formats rgba8=0x%x d24s8=0x%x d32f=0x%x readback=%d\n",
      static_cast<unsigned int>(caps->formats[bgfx::TextureFormat::RGBA8]),
      static_cast<unsigned int>(caps->formats[bgfx::TextureFormat::D24S8]),
      static_cast<unsigned int>(caps->formats[bgfx::TextureFormat::D32F]),
      bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK) ? 1 : 0);
  }
  this->sortedLayersSupported =
      caps->limits.maxFBAttachments >= 3 &&
      bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA16F, peelTextureFlags) &&
      (caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
      bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R16F, peelTextureFlags) &&
      (caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
      bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::D32F, peelTextureFlags);
  this->weightedOitSupported =
    (caps->supported & BGFX_CAPS_BLEND_INDEPENDENT) &&
    caps->limits.maxFBAttachments >= 3 &&
    bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA16F, peelTextureFlags) &&
    bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R16F, peelTextureFlags);

  const bool baseSupported =
      (caps->supported & BGFX_CAPS_INDEX32) &&
      caps->limits.maxTextureSamplers >= COIN_RENDER_MAX_TEXTURE_UNITS + 2 &&
      (caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
      ((caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
       (caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER));
  this->windowSupported = baseSupported && (caps->supported & BGFX_CAPS_SWAP_CHAIN);
  this->offscreenSupported =
      baseSupported && bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA8,
                                            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
  if (target.capabilityProbeOnly) {
    this->initialized = true;
    return this->status = CoinRenderBackendStatus::SUCCESS;
  }
  if (!(caps->supported & BGFX_CAPS_INDEX32) ||
      (this->presentToWindow && !(caps->supported & BGFX_CAPS_SWAP_CHAIN)) ||
      (!this->presentToWindow &&
       (!(caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
        (!(caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) &&
         !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)) ||
        !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA8,
                              BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK)))) {
    this->lastError = "BGFX renderer lacks the required window/offscreen capabilities";
    this->status = CoinRenderBackendStatus::UNSUPPORTED;
    return this->status;
  }
  this->layout.begin()
    .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Color1, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Color2, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Color3, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float);
  this->compactLayout = this->layout;
  this->compactLayout.end();
  this->layout.add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Bitangent, 4, bgfx::AttribType::Float)
    .end();
  this->instancedLayout.begin()
    .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
    .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
    .end();
  // BGFX's persistent instance-buffer API consumes the stride, not these
  // regular attributes. Skip the payload to avoid requiring unused bindings.
  this->instanceLayout.begin().skip(static_cast<uint8_t>(sizeof(CoinBgfxInstance))).end();
  // Select the required base fragment stage after lowering the first plan.
  // Preparing a target alone does not compile an unused general surface stage.
  this->textureSampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
  for (size_t unit = 1; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    const std::string name = "s_texColor" + std::to_string(unit);
    this->extraTextureSamplers[unit - 1] = bgfx::createUniform(name.c_str(), bgfx::UniformType::Sampler);
  }
  this->fogColorModeUniform = bgfx::createUniform("u_fogColorMode", bgfx::UniformType::Vec4);
  this->fogRangeUniform = bgfx::createUniform("u_fogRange", bgfx::UniformType::Vec4);
  this->samplingTextureSizesUniform = bgfx::createUniform("u_coinSamplingSizes", bgfx::UniformType::Vec4, COIN_RENDER_MAX_TEXTURE_UNITS);
  this->textureParamsUniform = bgfx::createUniform("u_texParams", bgfx::UniformType::Vec4, COIN_RENDER_MAX_TEXTURE_UNITS);
  this->screenDoorUniform = bgfx::createUniform("u_screenDoor", bgfx::UniformType::Vec4);
  this->alphaTestUniform = bgfx::createUniform("u_alphaTest", bgfx::UniformType::Vec4);
  this->rasterViewportUniform = bgfx::createUniform("u_rasterViewport", bgfx::UniformType::Vec4);
  this->clipMetaUniform = bgfx::createUniform("u_clipMeta", bgfx::UniformType::Vec4);
  this->clipPlanesUniform = bgfx::createUniform("u_clipPlanes", bgfx::UniformType::Vec4, COIN_RENDER_MAX_CLIP_PLANES);
  this->coinDepthUniform = bgfx::createUniform("u_coinDepth", bgfx::UniformType::Vec4);
  this->textureBlendUniform = bgfx::createUniform("u_texBlend", bgfx::UniformType::Vec4, COIN_RENDER_MAX_TEXTURE_UNITS);
  this->textureCombineUniform = bgfx::createUniform("u_texCombine", bgfx::UniformType::Vec4, COIN_RENDER_MAX_TEXTURE_UNITS * 4);
  this->ambientLightUniform = bgfx::createUniform("u_ambientLight", bgfx::UniformType::Vec4);
  this->lightCountUniform = bgfx::createUniform("u_lightCount", bgfx::UniformType::Vec4);
  this->lightPositionTypeUniform = bgfx::createUniform("u_lightPositionType", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  this->lightDirectionCutoffUniform = bgfx::createUniform("u_lightDirectionCutoff", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  this->lightColorIntensityUniform = bgfx::createUniform("u_lightColorIntensity", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  this->lightAmbientColorUniform = bgfx::createUniform("u_lightAmbientColor", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  this->instancedCameraUniform = bgfx::createUniform("u_instancedCamera", bgfx::UniformType::Vec4, 6);
  this->lightAttenuationDropUniform = bgfx::createUniform("u_lightAttenuationDrop", bgfx::UniformType::Vec4, COIN_RENDER_MAX_LIGHTS);
  const uint32_t whitePixel = UINT32_C(0xffffffff);
  this->defaultTexture = bgfx::createTexture2D(1, 1, false, 1,
    bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
    bgfx::copy(&whitePixel, sizeof(whitePixel)));
  bool textureUniformsValid = bgfx::isValid(this->fogColorModeUniform) && bgfx::isValid(this->fogRangeUniform);
  for (auto handle : this->extraTextureSamplers) textureUniformsValid = textureUniformsValid && bgfx::isValid(handle);
  if (!textureUniformsValid || !bgfx::isValid(this->textureSampler) ||
      !bgfx::isValid(this->coinDepthUniform) ||
      !bgfx::isValid(this->alphaTestUniform) || !bgfx::isValid(this->rasterViewportUniform) ||
      !bgfx::isValid(this->samplingTextureSizesUniform) || !bgfx::isValid(this->textureParamsUniform) ||
      !bgfx::isValid(this->textureBlendUniform) ||
      !bgfx::isValid(this->textureCombineUniform) ||
      !bgfx::isValid(this->ambientLightUniform) ||
      !bgfx::isValid(this->clipMetaUniform) ||
      !bgfx::isValid(this->clipPlanesUniform) ||
      !bgfx::isValid(this->lightCountUniform) ||
      !bgfx::isValid(this->lightPositionTypeUniform) ||
      !bgfx::isValid(this->lightDirectionCutoffUniform) ||
      !bgfx::isValid(this->lightColorIntensityUniform) ||
      !bgfx::isValid(this->lightAmbientColorUniform) ||
      !bgfx::isValid(this->instancedCameraUniform) ||
      !bgfx::isValid(this->lightAttenuationDropUniform) ||
      !bgfx::isValid(this->defaultTexture)) {
    this->lastError = "BGFX could not allocate texture uniforms or default texture";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return this->status;
  }
  if (!this->resize(target.size[0], target.size[1])) {
    if (this->status != CoinRenderBackendStatus::DEVICE_LOST)
      this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return this->status;
  }
  if (!this->presentToWindow && target.depthReadbackEnabled && !this->prepareDepthReadbackResources())
    return this->status;
  this->status = CoinRenderBackendStatus::SUCCESS;
  this->lastError.clear();
  return this->status;
}

bool CoinBgfxBackend::prepareBumpResources()
{
  const auto *caps = bgfx::getCaps();
  if (!caps || caps->limits.maxTextureSamplers < 2 || caps->limits.maxVertexAttributes < 8 ||
      !(caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_2D)) {
    this->lastError =
        "BGFX bump needs two fragment samplers, eight active vertex attributes and RGBA8 sampling";
    return false;
  }
  if(!bgfx::isValid(this->depthInfoUniform))this->depthInfoUniform=bgfx::createUniform("u_depthInfo",bgfx::UniformType::Vec4);
  if(!bgfx::isValid(this->bumpImageSampler))this->bumpImageSampler=bgfx::createUniform("s_bumpImage",bgfx::UniformType::Sampler);
  if(!bgfx::isValid(this->bumpBaseSampler))this->bumpBaseSampler=bgfx::createUniform("s_bumpBase",bgfx::UniformType::Sampler);
  if(!bgfx::isValid(this->bumpParamsUniform))this->bumpParamsUniform=bgfx::createUniform("u_bumpParams",bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->depthInfoUniform) || !bgfx::isValid(this->bumpImageSampler) ||
      !bgfx::isValid(this->bumpBaseSampler) || !bgfx::isValid(this->bumpParamsUniform)) {
    this->lastError = "BGFX bump uniform allocation failed";
    return false;
  }
  return true;
}

bool CoinBgfxBackend::prepareBumpPrograms()
{
  if (!this->prepareBumpResources())
    return false;
  auto &program = this->sampleCount == 1         ? this->bumpProgram
                  : bgfxExplicitSurfaceSamples() ? this->bumpSampleDepthProgram
                                                 : this->bumpNativeDepthProgram;
  if (!bgfx::isValid(program))
    program = bgfxTracePreparedProgram(
        createLayerProgram(
            COIN_BGFX_SHADER_DATA(coin_bgfx_vs_bump), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_bump),
            this->sampleCount == 1         ? COIN_BGFX_SHADER_DATA(coin_bgfx_fs_bump)
            : bgfxExplicitSurfaceSamples() ? COIN_BGFX_SHADER_DATA(coin_bgfx_fs_bump_sample_depth)
                                           : COIN_BGFX_SHADER_DATA(coin_bgfx_fs_bump_native_depth),
            this->sampleCount == 1         ? COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_bump)
            : bgfxExplicitSurfaceSamples() ? COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_bump_sample_depth)
                                           : COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_bump_native_depth)),
        "legacy_bump", this->sampleCount);
  if (!bgfx::isValid(program)) {
    this->lastError = "BGFX bump pipeline resources unavailable";
    return false;
  }
  return true;
}

bool CoinBgfxBackend::prepareBumpReceiverPrograms(const CoinBgfxDraw &draw, bool windowTarget,
                                                   bgfx::TextureFormat::Enum colorFormat)
{
  if (!bgfx::isValid(this->bumpLightMetaUniform))
    this->bumpLightMetaUniform=bgfx::createUniform("u_coinBumpLightMeta",bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->bumpLightVectorsUniform))
    this->bumpLightVectorsUniform=bgfx::createUniform("u_coinBumpLightVectors",bgfx::UniformType::Vec4,8);
  if (!bgfx::isValid(this->bumpLightColorsUniform))
    this->bumpLightColorsUniform=bgfx::createUniform("u_coinBumpLightColors",bgfx::UniformType::Vec4,8);
  if (!bgfx::isValid(this->bumpEyeUniform))
    this->bumpEyeUniform=bgfx::createUniform("u_coinBumpEye",bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->bumpModelViewUniform))
    this->bumpModelViewUniform=bgfx::createUniform("u_coinBumpModelView",bgfx::UniformType::Mat4);
  if (!bgfx::isValid(this->bumpLightMetaUniform) || !bgfx::isValid(this->bumpLightVectorsUniform) ||
      !bgfx::isValid(this->bumpLightColorsUniform) || !bgfx::isValid(this->bumpEyeUniform) ||
      !bgfx::isValid(this->bumpModelViewUniform)) {
    this->lastError="Bump directional uniform allocation failed";
    return false;
  }

  if (bgfx::getCaps()->limits.maxVertexAttributes < 15) {
    this->lastError = "Bump receiver requires fifteen vertex attributes";
    return false;
  }
  if (!this->prepareBumpResources() || !this->prepareShadowMultitextureResources())
    return false;
  if (this->sampleCount > 1 && bgfxExplicitSurfaceSamples() &&
      !bgfxSampleInterpolationSupported(this->sampleCount, this->lastError))
    return false;
  auto &program = this->sampleCount == 1         ? this->bumpReceiverProgram
                  : bgfxExplicitSurfaceSamples() ? this->bumpReceiverSampleProgram
                                                 : this->bumpReceiverNativeProgram;
  if (!bgfx::isValid(program))
    program = bgfxTracePreparedProgram(
        createLayerProgram(
            COIN_BGFX_SHADER_DATA(coin_bgfx_vs_bump_receiver),
            COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_bump_receiver),
            this->sampleCount == 1 ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_bump_receiver)
            : bgfxExplicitSurfaceSamples()
                ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_bump_receiver_sample_depth)
                : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_bump_receiver_native_depth),
            this->sampleCount == 1 ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_bump_receiver)
            : bgfxExplicitSurfaceSamples()
                ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_bump_receiver_sample_depth)
                : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_bump_receiver_native_depth)),
        "bump_receiver", this->sampleCount);
  if (!bgfx::isValid(program)) {
    this->lastError = "Bump receiver pipeline unavailable";
    return false;
  }
  const auto *caps = bgfx::getCaps();
  const auto depthFormat = this->sampleCount > 1 ? bgfx::TextureFormat::D32F :
    (caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
      ? bgfx::TextureFormat::D24S8 : bgfx::TextureFormat::D32F;
  if (this->sampleCount > 1)
    colorFormat = bgfx::TextureFormat::RGBA8;
  else if (windowTarget && this->presentToWindow &&
           caps->rendererType != bgfx::RendererType::OpenGL &&
           !bgfx::isValid(this->windowSceneFrameBuffer) && !this->multipassSceneRequired)
    colorFormat = sharedRuntime().backbufferColorFormat;
  const uint64_t state = drawState(draw);
  const uint32_t formats = uint32_t(colorFormat) | (uint32_t(depthFormat) << 16);
  bool &verified = this->bumpReceiverVerifiedPipelines[std::make_pair(state, formats)];
  return this->verifyShadowMultitextureProgram(program, verified, 0, true,
                                              state, colorFormat, depthFormat);
}

bool CoinBgfxBackend::prepareBaseProgram(const std::vector<CoinBgfxDraw> &inputDraws,
                                         bool hasShadows, CoinBgfxTransparencyStrategy strategy,
                                         bool instanced, bool windowTarget,
                                         bgfx::TextureFormat::Enum colorFormat)
{
  const bool legacyBump =
      std::any_of(inputDraws.begin(), inputDraws.end(),
                  [](const CoinBgfxDraw &draw) { return draw.bumpSlot && !draw.bumpExtended; });
  const bool receiverBump =
      std::any_of(inputDraws.begin(), inputDraws.end(),
                  [](const CoinBgfxDraw &draw) { return draw.bumpSlot && draw.bumpExtended; });
  if (legacyBump && !this->prepareBumpPrograms())
    return false;
  if (receiverBump)
    for (const auto &draw : inputDraws)
      if (draw.bumpSlot && draw.bumpExtended &&
          !this->prepareBumpReceiverPrograms(draw, windowTarget, colorFormat))
        return false;
  const bool shadowArray =
      hasShadows && std::any_of(inputDraws.begin(), inputDraws.end(), [](const CoinBgfxDraw &draw) {
        return draw.portableShaderSlot || draw.bumpExtended || (draw.hasTexture && (draw.volumeDepth || draw.cubeFaceSize)) ||
               std::any_of(std::begin(draw.extraTextures), std::end(draw.extraTextures),
                           [](const CoinBgfxDraw::TextureLayer &layer) { return layer.enabled; });
      });
  // Bump and adapted draws select their own executors in submitSurfaceDraw.
  // Only ordinary occurrences require the fixed surface programs.
  std::vector<CoinBgfxDraw> draws;
  for (const auto &draw : inputDraws)
    if (!draw.bumpSlot && !draw.portableShaderSlot)
      draws.push_back(draw);
  this->activeProgram = BGFX_INVALID_HANDLE;
  this->activeTexture2dDepth = false;
  this->activeTextureUnitCount = 0;
  this->shadowMultitextureSampleDepthActive = hasShadows && this->sampleCount > 1 &&
    strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT && shadowArray;
  if (draws.empty()) {
    if (hasShadows && !this->prepareShadowMultitextureResources()) return false;
    // Sample-depth uniforms are resources, not a reason to create a fixed
    // surface executor. The existing MSAA attachment/probe remains unchanged.
    if (this->sampleCount > 1 && !this->prepareFullscreenResources()) return false;
    return true;
  }
  if (shadowArray && (!this->prepareShadowMultitextureProgram() ||
                      (this->sampleCount > 1 &&
                       (this->shadowMultitextureSampleDepthActive ||
                        std::any_of(draws.begin(), draws.end(), bgfxCustomSampleDepth)) &&
                       !this->prepareShadowMultitextureProgram(true)) ||
                      (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
                       !this->prepareShadowMultitextureProgram(false, 1)) ||
                      (strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT &&
                       !this->prepareShadowMultitextureProgram(false, 2))))
    return false;
  if (hasShadows && strategy == CoinBgfxTransparencyStrategy::OBJECT) {
    if (!shadowArray && !this->prepareSurfaceDepthPrograms(draws, true, false))
      return false;
    this->activeTextureUnitCount = COIN_RENDER_MAX_TEXTURE_UNITS;
    return true;
  }
  const char * disableSolid = std::getenv("COIN_BGFX_DISABLE_SOLID_PROGRAM");
  const bool solid = !(disableSolid && std::strcmp(disableSolid, "1") == 0) &&
    coin_bgfx_solid_program(draws, hasShadows, strategy);
  const char * disableTexture2d = std::getenv("COIN_BGFX_DISABLE_TEXTURE2D_PROGRAM");
  const bool texture2d = !instanced &&
    !(disableTexture2d && std::strcmp(disableTexture2d, "1") == 0) &&
    coin_bgfx_texture2d_program(draws, hasShadows, strategy);
  if (texture2d && this->sampleCount > 1) {
    // These draws never submit the single-sample activeProgram. Prepare only
    // their eligible depth family; keep mixed native/custom depth per draw.
    if (!this->prepareSurfaceDepthPrograms(draws, false, false, true))
      return false;
    this->activeTexture2dDepth = true;
    this->activeTextureUnitCount = 1;
    if (CoinRenderDiagnosticShell::phaseTracingEnabled())
      std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_base_program solid=0 instancing=0 texture2d=1\n");
    return true;
  }
  bgfx::ProgramHandle & selected = instanced ? this->instancedProgram :
    solid ? this->solidProgram : texture2d ? this->texture2dProgram : this->program;
  if (!bgfx::isValid(selected)) {
    selected = bgfxTracePreparedProgram(
        createLayerProgram(instanced ? COIN_BGFX_SHADER_DATA(coin_bgfx_vs_instanced_color)
                                     : COIN_BGFX_SHADER_DATA(coin_bgfx_vs),
                           instanced ? COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_instanced_color)
                                     : COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
                           instanced   ? COIN_BGFX_SHADER_DATA(coin_bgfx_fs_instanced_color)
                           : solid     ? COIN_BGFX_SHADER_DATA(coin_bgfx_fs_solid_color)
                           : texture2d ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_texture2d)
                                       : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs),
                           instanced   ? COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_instanced_color)
                           : solid     ? COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_solid_color)
                           : texture2d ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_texture2d)
                                       : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs)),
        "general_surface", this->sampleCount);
    if (!bgfx::isValid(selected)) {
      this->lastError = instanced ? "BGFX failed to create its instanced shader program" :
        solid ? "BGFX failed to create its solid-color shader program" :
        texture2d ? "BGFX failed to create its base 2D texture shader program" :
                    "BGFX failed to create its general surface shader program";
      this->status = CoinRenderBackendStatus::BACKEND_ERROR;
      return false;
    }
  }
  if (!this->prepareSurfaceDepthPrograms(
          draws,
          hasShadows &&
              !std::any_of(draws.begin(), draws.end(),
                           [](const CoinBgfxDraw &draw) {
                             return draw.bumpExtended ||
                                    (draw.hasTexture && (draw.volumeDepth || draw.cubeFaceSize)) ||
                                    std::any_of(std::begin(draw.extraTextures),
                                                std::end(draw.extraTextures),
                                                [](const CoinBgfxDraw::TextureLayer &layer) {
                                                  return layer.enabled;
                                                });
                           }),
          instanced))
    return false;
  if (hasShadows && strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT &&
      !this->prepareSurfaceDepthPrograms(draws, false, false)) return false;
  this->activeProgram = selected;
  this->activeTextureUnitCount = instanced || solid ? 0 :
    texture2d ? 1 : COIN_RENDER_MAX_TEXTURE_UNITS;
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_base_program solid=%d instancing=%d texture2d=%d\n",
      solid ? 1 : 0, instanced ? 1 : 0, texture2d ? 1 : 0);
  return true;
}

// A module variant reuses the existing vertex program, attachments, depth and
// composition entry point. Only the fixed texture cascade is replaced.
bool CoinBgfxBackend::preparePortablePrograms(const CoinRenderFramePlan &frame,
  const std::vector<CoinBgfxDraw> &draws, bool hasShadows, CoinBgfxTransparencyStrategy strategy,
  bool windowTarget, bgfx::TextureFormat::Enum colorFormat, bool pinPreflight)
{
  if (std::none_of(draws.begin(), draws.end(), [](const CoinBgfxDraw &draw) { return draw.portableShaderSlot != 0; }))
    return true;
  auto &runtime = sharedRuntime();
  auto &cache = runtime.portable;
  const auto renderer = bgfx::getRendererType();
  if (renderer != bgfx::RendererType::Vulkan && renderer != bgfx::RendererType::OpenGL) {
    this->lastError = "Portable BGFX modules require an installed compiler profile for this renderer";
    return false;
  }
  for (auto &uniform : cache.uniforms)
    if (!bgfx::isValid(uniform)) {
      const unsigned bank = &uniform == &cache.uniforms[0] ? 0 : 1;
      uniform = bgfx::createUniform(bank ? "u_portable1" : "u_portable0", bgfx::UniformType::Vec4, 128);
      if (!bgfx::isValid(uniform)) { this->lastError = "Portable shader uniform bank allocation failed"; return false; }
      cache.uniformCount.fetch_add(1, std::memory_order_relaxed);
    }
  if (!bgfx::isValid(cache.probeControl)) {
    cache.probeControl = bgfx::createUniform("u_portableProbe", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(cache.probeControl)) {
      this->lastError = "Portable shader capability control allocation failed";
      return false;
    }
    cache.uniformCount.fetch_add(1, std::memory_order_relaxed);
  }
  ++cache.use;
  this->portableProgramRoutes.clear();
  const std::string profile = renderer == bgfx::RendererType::Vulkan ? "spirv" : "330";
  // The module replaces fixed texturing and its only sampling operation is
  // SAMPLE_2D, whose resource domain is checked by Core before preparation.
  // Retain the shared P07 implementation, omitting unused spatial lowering.
  const std::string defines = frame.textureSamplingPolicy == COIN_RENDER_SAMPLING_PORTABLE ?
    "COIN_TEXTURE2D_SURFACE=1;COIN_PORTABLE_SAMPLING_SHADER=1" : "COIN_TEXTURE2D_SURFACE=1";
  const auto prepare = [&](const CoinRenderShaderInstance &instance, PortableFamily family,
                            const uint8_t *vertexData, uint32_t vertexBytes, const char *fragment) {
    const std::string identity = coin_render_shader_key(instance.module, instance.ir);
    const std::string fragmentName = fragment;
    const bool sampleShader = fragmentName.find("sample_depth") != std::string::npos ||
      fragmentName.find("peel_msaa") != std::string::npos || fragmentName.find("peel_next_msaa") != std::string::npos;
    const std::string compilerProfile = renderer == bgfx::RendererType::OpenGL && sampleShader ? "430" : profile;
    const auto targetFormat = windowTarget ? runtime.backbufferColorFormat : colorFormat;
    const std::string key = identity + "/" + compilerProfile + "/" + defines + "/" + fragment + "/" +
      std::to_string(runtime.generation) + "/" + std::to_string(unsigned(targetFormat)) + "/" + std::to_string(this->sampleCount);
    auto cached = cache.programs.find(key);
    if (cached == cache.programs.end()) {
      std::vector<uint8_t> fragmentBytes;
      if (!coin_bgfx_compile_portable_fragment(instance.ir, fragment, compilerProfile, defines, fragmentBytes, this->lastError))
        return false;
      uint64_t id = UINT64_C(14695981039346656037);
      for (unsigned char byte : key) id = (id ^ byte) * UINT64_C(1099511628211);
      if (CoinRenderDiagnosticShell::phaseTracingEnabled()) {
        std::fprintf(stderr, "COIN_RENDER_PHASE portable_shader_compile module=%016llx profile=bgfx_%s_%s_%u\n",
          static_cast<unsigned long long>(id), compilerProfile.c_str(), fragment, this->sampleCount);
        std::fflush(stderr);
      }
      const auto program = createLayerProgram(vertexData, vertexBytes, fragmentBytes.data(), static_cast<uint32_t>(fragmentBytes.size()));
      if (!bgfx::isValid(program)) { this->lastError = "Portable BGFX shader program allocation failed"; return false; }
      if (hasShadows) {
        const unsigned composition = family == PortableFamily::SHADOW_PEEL ? 1 :
          family == PortableFamily::SHADOW_OIT ? 2 : 0;
        bool verified = false;
        const bool valid = this->verifyShadowMultitextureProgram(program, verified, composition, false,
          0, bgfx::TextureFormat::RGBA8, bgfx::TextureFormat::Count, true);
        if (!valid) { bgfx::destroy(program); return false; }
      }
      BgfxPortableProgram entry;
      entry.program = program;
      entry.bytes = fragmentBytes.size() + key.size() + sizeof(BgfxPortableProgram);
      entry.used = cache.use;
      cache.bytes += entry.bytes;
      cached = cache.programs.emplace(key, entry).first;
      cache.programCount.store(cache.programs.size(), std::memory_order_relaxed);
    }
    cached->second.used = cache.use;
    // A parent graph's prepared variants must survive its sequential STAGED
    // children and peers until the root has finished encoding its submission.
    if (pinPreflight && std::find(this->portablePreflightPins.begin(),
          this->portablePreflightPins.end(), key) == this->portablePreflightPins.end()) {
      this->portablePreflightPins.push_back(key);
      ++cached->second.pins;
    }
    this->portableProgramRoutes[std::make_pair(identity, family)] = key;
    return true;
  };
  for (const auto &draw : draws) {
    if (!draw.portableShaderSlot) continue;
    if (draw.portableShaderSlot > frame.portableShaders.size() || draw.bumpSlot) {
      this->lastError = "Portable shader module occurrence has incompatible bump attributes";
      return false;
    }
    const auto &instance = frame.portableShaders[draw.portableShaderSlot - 1];
    const auto surface = [&](PortableFamily family, const char *fragment) {
      return prepare(instance, family, COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs), fragment);
    };
    const auto shadow = [&](PortableFamily family, const char *fragment) {
      return prepare(instance, family, COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_multitexture),
        COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_multitexture), fragment);
    };
    const bool customDepth = this->sampleCount > 1 &&
      (this->shadowMultitextureSampleDepthActive || bgfxExplicitSurfaceSamples() || bgfxCustomSampleDepth(draw));
    const bool deferred = draw.blend && draw.deferred && !draw.additive;
    const bool peel = strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
      (deferred || this->sampleCount > 1);
    const bool oit = strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT && deferred;
    // MSAA peeling uses its depth selector for immediate writers too; other
    // immediate/additive writers retain the normal surface depth family.
    const bool base = !oit && !(peel && (deferred || this->sampleCount > 1));
    if (hasShadows) {
      if (base) {
        const char *fragment = this->sampleCount == 1 ? "fs_shadow_multitexture.sc" :
          bgfxExplicitSurfaceSamples() ? "fs_shadow_multitexture_sample_depth.sc" :
          "fs_shadow_multitexture_native_depth.sc";
        // Vulkan custom-depth draws use the separate sampled family.
        if ((!customDepth || bgfxExplicitSurfaceSamples()) &&
            !shadow(PortableFamily::SHADOW_BASE, fragment)) return false;
        if (customDepth && !bgfxExplicitSurfaceSamples() &&
            !shadow(PortableFamily::SHADOW_SAMPLE, "fs_shadow_multitexture_sample_depth.sc")) return false;
      }
      if (peel && !shadow(PortableFamily::SHADOW_PEEL, this->sampleCount > 1 ?
            "fs_shadow_multitexture_peel_msaa.sc" : "fs_shadow_multitexture_peel.sc")) return false;
      if (oit && !shadow(PortableFamily::SHADOW_OIT, this->sampleCount > 1 ?
            "fs_shadow_multitexture_oit_sample_depth.sc" : "fs_shadow_multitexture_oit.sc")) return false;
    } else {
      if (base) {
        if (this->sampleCount == 1) {
          if (!surface(PortableFamily::BASE, "fs_base_color.sc")) return false;
        } else if (customDepth) {
          if (!surface(PortableFamily::SAMPLE_DEPTH, "fs_surface_sample_depth.sc")) return false;
        } else if (!surface(PortableFamily::NATIVE_DEPTH, "fs_surface_native_depth.sc")) return false;
      }
      if (peel && !surface(PortableFamily::PEEL, this->sampleCount > 1 ?
            "fs_peel_next_msaa.sc" : "fs_peel_next.sc")) return false;
      if (oit && !surface(PortableFamily::OIT, "fs_weighted_oit.sc")) return false;
    }
  }
  // Soft cache budget: entries needed by this frame remain live. Destroying an
  // older BGFX handle queues retirement behind its already-submitted uses.
  const size_t budget = 32u * 1024u * 1024u;
  while (cache.bytes > budget) {
    auto oldest = cache.programs.end();
    for (auto it = cache.programs.begin(); it != cache.programs.end(); ++it)
      if (!it->second.pins && it->second.used != cache.use &&
          (oldest == cache.programs.end() || it->second.used < oldest->second.used)) oldest = it;
    if (oldest == cache.programs.end()) break;
    bgfx::destroy(oldest->second.program);
    cache.bytes -= oldest->second.bytes;
    cache.programs.erase(oldest);
    cache.programCount.store(cache.programs.size(), std::memory_order_relaxed);
  }
  return true;
}

void CoinBgfxBackend::releasePortablePreflightPins()
{
  auto &programs = sharedRuntime().portable.programs;
  for (const auto &key : this->portablePreflightPins) {
    const auto entry = programs.find(key);
    if (entry != programs.end() && entry->second.pins) --entry->second.pins;
  }
  this->portablePreflightPins.clear();
}

void CoinBgfxBackend::submitPortableDraw(bgfx::ViewId view, bgfx::ProgramHandle base,
  const CoinBgfxDraw &draw, const CoinRenderFramePlan &frame, PortableFamily family,
  uint32_t depth, uint8_t discard)
{
  if (!draw.portableShaderSlot) { bgfx::submit(view, base, depth, discard); return; }
  auto &cache = sharedRuntime().portable;
  const auto &instance = frame.portableShaders[draw.portableShaderSlot - 1];
  const auto route = this->portableProgramRoutes.find(std::make_pair(
    coin_render_shader_key(instance.module, instance.ir), family));
  if (route == this->portableProgramRoutes.end()) {
    this->lastError = "Portable shader variant was not prepared before submission";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return;
  }
  const auto program = cache.programs.find(route->second);
  if (program == cache.programs.end()) {
    this->lastError = "Portable shader prepared variant was retired prematurely";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return;
  }
  float parameters[1024] = {};
  std::copy(instance.parameters.begin(), instance.parameters.end(), parameters);
  const float authored[4] = {};
  bgfx::setUniform(cache.probeControl, authored);
  bgfx::setUniform(cache.uniforms[0], parameters, 128);
  bgfx::setUniform(cache.uniforms[1], parameters + 512, 128);
  if (CoinRenderDiagnosticShell::phaseTracingEnabled()) {
    const char *name = "unknown";
    switch (family) {
    case PortableFamily::BASE: name = "base"; break;
    case PortableFamily::NATIVE_DEPTH: name = "native_depth"; break;
    case PortableFamily::SAMPLE_DEPTH: name = "sample_depth"; break;
    case PortableFamily::SHADOW_BASE: name = "shadow_base"; break;
    case PortableFamily::SHADOW_SAMPLE: name = "shadow_sample"; break;
    case PortableFamily::PEEL: name = "peel"; break;
    case PortableFamily::SHADOW_PEEL: name = "shadow_peel"; break;
    case PortableFamily::OIT: name = "oit"; break;
    case PortableFamily::SHADOW_OIT: name = "shadow_oit"; break;
    }
    std::fprintf(stderr,
      "COIN_RENDER_PHASE bgfx_portable_program_submit family=%s program=%u samples=%u\n",
      name, unsigned(program->second.program.idx), this->sampleCount);
    std::fflush(stderr);
  }
  bgfx::submit(view, program->second.program, depth, discard);
}

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

// The inherited-image compute output is one texel per source pixel. It is
// sampled by the ordinary surface program into the requested MSAA attachment;
// blending, masking and depth still execute before that attachment resolves.
static CoinRenderSubmitResult bgfxRasterImageProfile(const CoinRenderFramePlan & frame)
{
  const auto * caps = sharedRuntime().references ? bgfx::getCaps() : nullptr;
  uint64_t bytes = 0;
  size_t outputs = 0;
  for (const auto & state : frame.renderStates) {
    if (state.rasterTextureSourceSlot == UINT32_MAX) continue;
    if (++outputs > 4096 || state.textureImageSlot >= frame.textures.size())
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited images exceed output count or reference invalid payload"};
    const auto & image = frame.textures[state.textureImageSlot];
    const uint64_t pixels = uint64_t(image.width) * image.height;
    if (!image.width || !image.height || pixels > (128ull * 1024 * 1024 - bytes) / 16)
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited image outputs exceed 128 MiB or have zero extent"};
    bytes += pixels * 16;
    if (caps && (image.width > caps->limits.maxTextureSize || image.height > caps->limits.maxTextureSize))
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited image output exceeds adapter extent"};
  }
  if (!outputs || !caps) return {};
  const uint64_t flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
  if (!(caps->supported & BGFX_CAPS_COMPUTE) || caps->limits.maxTextureSamplers < 10 ||
      !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, flags))
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX inherited images require RGBA32F compute storage and nine prepass textures"};
  return {};
}

bool
CoinBgfxBackend::prepareRasterImageResources()
{
  if (!bgfx::isValid(this->rasterImageProgram)) this->rasterImageProgram = createRasterImageProgram();
  if (!bgfx::isValid(this->rasterPrimarySampler))
    this->rasterPrimarySampler = bgfx::createUniform("s_rasterPrimary", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->rasterCoordinatesUniform))
    this->rasterCoordinatesUniform = bgfx::createUniform("u_rasterCoordinates", bgfx::UniformType::Vec4, 8);
  if (!bgfx::isValid(this->rasterExtentUniform))
    this->rasterExtentUniform = bgfx::createUniform("u_rasterExtent", bgfx::UniformType::Vec4);
  if (bgfx::isValid(this->rasterImageProgram) && bgfx::isValid(this->rasterPrimarySampler) &&
      bgfx::isValid(this->rasterCoordinatesUniform) && bgfx::isValid(this->rasterExtentUniform)) return true;
  this->lastError = "BGFX image prepass program allocation failed";
  return false;
}

CoinRenderSubmitResult
CoinBgfxBackend::encodeRasterImages(const CoinRenderFramePlan & frame,
  const std::vector<bgfx::TextureHandle> & textures, std::vector<bgfx::TextureHandle> & owned)
{
  this->rasterImageOutputs.clear();
  bool needed = false;
  size_t outputCount = 0;
  uint64_t bytes = 0;
  for (const auto & state : frame.renderStates) {
    if (state.rasterTextureSourceSlot == UINT32_MAX) continue;
    needed = true;
    if (++outputCount > 4096)
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited images exceed 4096 output resources"};
    if (!state.rasterPixels || state.rasterTextureSourceSlot >= frame.renderStates.size() ||
        !state.hasTexture || state.textureImageSlot >= frame.textures.size() ||
        state.textureImageSlot >= textures.size() ||
        frame.renderStates[state.rasterTextureSourceSlot].rasterTextureSourceSlot != UINT32_MAX ||
        frame.renderStates[state.rasterTextureSourceSlot].rasterPixels)
      return {CoinRenderBackendStatus::UNSUPPORTED, "Invalid inherited image source or payload"};
    const auto & payload = frame.textures[state.textureImageSlot];
    bytes += uint64_t(payload.width) * payload.height * 16;
    if (bytes > 128ull * 1024ull * 1024ull)
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited image outputs exceed 128 MiB"};
    const auto & source = frame.renderStates[state.rasterTextureSourceSlot];
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      const auto layer = coin_render_texture_unit(source, unit);
      const auto & raw = state.rasterTextureCoordinates[unit];
      for (int c = 0; c < 4; ++c) if (!std::isfinite(raw[c]))
        return {CoinRenderBackendStatus::UNSUPPORTED, "Non-finite raster texture coordinate"};
      if (!layer.enabled) continue;
      if (layer.imageSlot >= textures.size() || layer.samplerSlot >= frame.samplers.size() ||
          !bgfx::isValid(textures[layer.imageSlot]))
        return {CoinRenderBackendStatus::UNSUPPORTED, "Invalid inherited image GPU resource"};
      SbVec4f transformed;
      layer.matrix.multVecMatrix(raw, transformed);
      for (int c = 0; c < 4; ++c) if (!std::isfinite(transformed[c]))
        return {CoinRenderBackendStatus::UNSUPPORTED, "Non-finite raster texture transform"};
      if (!frame.textures[layer.imageSlot].cubeFaceSize && source.textureProjection == CoinRenderTextureProjection::PROJECTIVE &&
          (transformed[3] == 0 || !std::isfinite(transformed[0]/transformed[3]) ||
           !std::isfinite(transformed[1]/transformed[3]) || !std::isfinite(transformed[2]/transformed[3])))
        return {CoinRenderBackendStatus::UNSUPPORTED, "Invalid raster texture projection"};
    }
  }
  if (!needed) return {};
  const auto * caps = bgfx::getCaps();
  const uint64_t flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
  if (!caps || !(caps->supported & BGFX_CAPS_COMPUTE) || caps->limits.maxTextureSamplers < 10 ||
      !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, flags))
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX inherited images require RGBA32F compute storage and nine prepass textures"};
  if (!this->prepareRasterImageResources())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  // Allocate the complete bounded output set before encoding any dispatch.
  for (size_t slot = 0; slot < frame.renderStates.size(); ++slot) {
    const auto & state = frame.renderStates[slot];
    if (state.rasterTextureSourceSlot == UINT32_MAX) continue;
    const auto & payload = frame.textures[state.textureImageSlot];
    if (!payload.width || !payload.height || payload.width > caps->limits.maxTextureSize ||
        payload.height > caps->limits.maxTextureSize)
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited image output exceeds adapter extent"};
    auto texture = bgfx::createTexture2D(uint16_t(payload.width), uint16_t(payload.height),
      false, 1, bgfx::TextureFormat::RGBA32F, flags);
    if (!bgfx::isValid(texture))
      return {CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX image RGBA32F allocation failed"};
    owned.push_back(texture);
    this->rasterImageOutputs[uint32_t(slot)] = texture;
    this->samplingBoundTextureSizes[texture.idx] = {payload.width, payload.height};
  }
  // DIRECT producers reuse this backend's view block. A previous opaque
  // render leaves its framebuffer and clear flags on viewBase; OpenGL applies
  // that clear on view entry even for a compute dispatch. Start an isolated
  // compute view so sampling the producer cannot clear its attachment first.
  bgfx::resetView(this->viewBase);
  bgfx::setViewName(this->viewBase, "image_inherited_rgba32f");
  bgfx::setViewMode(this->viewBase, bgfx::ViewMode::Sequential);
  for (const auto & entry : this->rasterImageOutputs) {
    const auto & state = frame.renderStates[entry.first];
    const auto & source = frame.renderStates[state.rasterTextureSourceSlot];
    const auto & payload = frame.textures[state.textureImageSlot];
    float params[8][4] = {}, blends[8][4] = {}, sizes[8][4] = {}, coordinates[8][4] = {};
    for (size_t unit = 0; unit < 8; ++unit) {
      const auto layer = coin_render_texture_unit(source, unit);
      SbVec4f transformed;
      layer.matrix.multVecMatrix(state.rasterTextureCoordinates[unit], transformed);
      for (int c = 0; c < 4; ++c) coordinates[unit][c] = transformed[c];
      params[unit][0] = layer.enabled ? 1.0f : 0.0f;
      params[unit][1] = float(layer.model);
      params[unit][3] = float(source.textureProjection);
      std::copy(layer.blendColor, layer.blendColor + 4, blends[unit]);
      auto texture = this->defaultTexture;
      uint32_t samplerFlags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
      if (layer.enabled) {
        texture = textures[layer.imageSlot];
        const auto & image = frame.textures[layer.imageSlot];
        const auto & sampler = frame.samplers[layer.samplerSlot];
        samplerFlags = BGFX_SAMPLER_MIP_POINT;
        if (sampler.wrapS == CoinRenderTextureWrap::CLAMP) samplerFlags |= BGFX_SAMPLER_U_CLAMP;
        if (sampler.wrapT == CoinRenderTextureWrap::CLAMP) samplerFlags |= BGFX_SAMPLER_V_CLAMP;
        if (sampler.filter == CoinRenderTextureFilter::NEAREST)
          samplerFlags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
        params[unit][2] = image.gpuToken && !image.cubeFaceSize && !caps->originBottomLeft ? 1.0f : 0.0f;
        sizes[unit][0] = float(image.width); sizes[unit][1] = float(image.height);
        sizes[unit][2] = float(sampler.filter);
        if (image.cubeFaceSize) {
          sizes[unit][0] = float(image.cubeFaceSize); sizes[unit][1] = image.cubeMipmapped ? 1.0f : 0.0f;
          sizes[unit][2] += 8; sizes[unit][3] = 0;
          samplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        }
        if (image.volumeDepth) {
          sizes[unit][0] = float(image.volumeWidth); sizes[unit][1] = float(image.volumeHeight);
          sizes[unit][2] += 4;
          sizes[unit][3] = float(image.volumeDepth * 16 + unsigned(sampler.wrapS) +
            2 * unsigned(sampler.wrapT) + 4 * unsigned(sampler.wrapR) + (image.volumeMipmapped ? 8 : 0));
          samplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        }
      }
      bgfx::setTexture(uint8_t(unit + 2), unit == 0 ? this->textureSampler :
        this->extraTextureSamplers[unit - 1], texture, 0, 1, 0, 1, samplerFlags);
    }
    const float extent[4] = {float(payload.width), float(payload.height), 0, 0};
    bgfx::setUniform(this->rasterExtentUniform, extent);
    bgfx::setUniform(this->rasterCoordinatesUniform, coordinates, 8);
    bgfx::setUniform(this->textureParamsUniform, params, 8);
    bgfx::setUniform(this->samplingTextureSizesUniform, sizes, 8);
    bgfx::setUniform(this->textureBlendUniform, blends, 8);
    bgfx::setUniform(this->textureCombineUniform, source.textureCombines.data(), 32);
    bgfx::setTexture(1, this->rasterPrimarySampler, textures[state.textureImageSlot],
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
      BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT);
    bgfx::setImage(0, entry.second, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
    bgfx::dispatch(this->viewBase, this->rasterImageProgram, (payload.width + 7) / 8, (payload.height + 7) / 8);
  }
  return {};
}

bool
CoinBgfxBackend::prepareNpotMipProgram()
{
  if (!this->prepareFullscreenResources()) return false;
  if (!bgfx::isValid(this->npotMipProgram))
    this->npotMipProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_mip_area),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_mip_area));
  if (!bgfx::isValid(this->npotMipSampler))
    this->npotMipSampler = bgfx::createUniform("s_mipSource", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->npotMipExtentUniform))
    this->npotMipExtentUniform = bgfx::createUniform("u_mipExtent", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(this->npotMipProgram) ||
      !bgfx::isValid(this->npotMipSampler) ||
      !bgfx::isValid(this->npotMipExtentUniform)) {
    this->lastError = "BGFX could not prepare the NPOT area reduction program";
    return false;
  }
  return true;
}

CoinRenderSubmitResult
CoinBgfxBackend::msaaProfile(const CoinRenderFramePlan & frame) const
{
  if (this->sampleCount == 1) return {};
  if (this->sampleCount != 2 && this->sampleCount != 4)
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA requires exactly 1, 2 or 4 samples"};
  if (frame.outputColorFormat != CoinRenderTextureFormat::RGBA8_LINEAR ||
      frame.rasterizationPolicy != COIN_RENDER_RASTER_NATIVE)
    return {CoinRenderBackendStatus::UNSUPPORTED,
      "BGFX MSAA requires native RGBA8 linear geometry"};
  std::vector<CoinRenderCompositionItem> order;
  std::string diagnostic;
  const bool customSampleDepth = std::any_of(frame.renderStates.begin(), frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot & state) {
    return state.depthRange[0] != 0.0f || state.depthRange[1] != 1.0f ||
      (state.polygonOffsetEnabled && (state.polygonOffsetStyles & state.polygonOffsetPrimitiveStyle) &&
       (state.polygonOffsetFactor != 0.0f || state.polygonOffsetUnits != 0.0f || state.polygonOffsetSlopeBias != 0.0f));
  });
  if (customSampleDepth && this->initialized && !bgfxSampleInterpolationSupported(this->sampleCount, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  if (!coin_render_composition_order(frame, order, diagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};

  const bool weighted = std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
    return item.blend && item.deferred && !item.additive && !item.screenDoor &&
      item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT;
  });
  if (weighted && this->initialized &&
      !bgfxWeightedSamples(this->sampleCount, this->layout, this->viewBase, diagnostic, false))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  const bool peeling = std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
    return item.blend && item.deferred && !item.additive &&
      item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS;
  });
  if (peeling && this->initialized &&
      !bgfxPeelingSamples(this->sampleCount, this->layout, this->viewBase, diagnostic, false))
    return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  return {};
}

void
CoinBgfxBackend::encodeMsaaResolve(bgfx::ViewId view, bgfx::FrameBufferHandle output,
                                  int width, int height, float mode)
{
  // View ids are reused by RTT producers and the parent. Remove their prior
  // framebuffer/clear state before any sampling of those same attachments.
  bgfx::resetView(view);
  bgfx::setViewName(view, mode < .5f ? "msaa_sample_count" :
    mode < 1.5f ? "msaa_color_resolve" : "msaa_depth_min_resolve");
  bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(view, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
  bgfx::setViewFrameBuffer(view, output);
  bgfx::setViewClear(view, BGFX_CLEAR_NONE);
  bgfx::setViewTransform(view, nullptr, nullptr);
  const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  const float info[4] = {1.0f / width, 1.0f / height, mode, float(this->sampleCount)};
  bgfx::setTransform(identity);
  bgfx::setUniform(this->depthInfoUniform, info);
  bgfx::setTexture(0, this->msaaColorSampler, bgfx::getTexture(this->msaaFrameBuffer, 0));
  bgfx::setTexture(1, this->msaaDepthSampler, bgfx::getTexture(this->msaaFrameBuffer, 1));
  bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
  bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
  bgfx::submit(view, this->msaaProgram);
}

CoinRenderSubmitResult
CoinBgfxBackend::prepareMsaa(int width, int height)
{
  if (this->sampleCount == 1) return {};
  const auto * caps = bgfx::getCaps();
  if (!caps || (caps->rendererType != bgfx::RendererType::Vulkan &&
                caps->rendererType != bgfx::RendererType::OpenGL))
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA sample-count validation requires Vulkan or OpenGL"};
  const uint64_t flags = BGFX_TEXTURE_MSAA_SAMPLE |
    (this->sampleCount == 2 ? BGFX_TEXTURE_RT_MSAA_X2 : BGFX_TEXTURE_RT_MSAA_X4);
  for (auto format : {bgfx::TextureFormat::RGBA8, bgfx::TextureFormat::D32F})
    if (!(caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA) ||
        !bgfx::isTextureValid(1, false, 1, format, flags))
      return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA needs sampleable RGBA8 and D32F attachments"};
  if (!(caps->formats[bgfx::TextureFormat::R32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::R32F,
                            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK))
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA requires exact R32F probe/depth readback"};
  if (width <= 0 || height <= 0 || width > caps->limits.maxTextureSize ||
      height > caps->limits.maxTextureSize ||
      uint64_t(width) * uint64_t(height) * 8 * this->sampleCount > 128ull * 1024 * 1024)
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA attachments exceed dimensions or 128 MiB"};
  if (bgfx::isValid(this->msaaFrameBuffer) && this->msaaWidth == width && this->msaaHeight == height)
    return {};
  if (!this->prepareFullscreenResources())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  // Raw GLSL has no shaderc uniform table: ProgramGL discovers uniforms at
  // link time, so their registry entries must precede the program command.
  if (!bgfx::isValid(this->msaaColorSampler))
    this->msaaColorSampler = bgfx::createUniform("s_msaaColor", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->msaaDepthSampler))
    this->msaaDepthSampler = bgfx::createUniform("s_msaaDepth", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->msaaProgram))
    this->msaaProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_msaa_resolve), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_msaa_resolve));
  if (!bgfx::isValid(this->msaaProgram) || !bgfx::isValid(this->msaaColorSampler) ||
      !bgfx::isValid(this->msaaDepthSampler))
    return {CoinRenderBackendStatus::BACKEND_ERROR, "BGFX MSAA resolve program allocation failed"};
  if (bgfx::isValid(this->msaaFrameBuffer)) bgfx::destroy(this->msaaFrameBuffer);
  this->msaaFrameBuffer = BGFX_INVALID_HANDLE;
  this->msaaWidth = this->msaaHeight = 0;
  bgfx::TextureHandle color = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA8, flags);
  bgfx::TextureHandle depth = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::D32F, flags);
  if (bgfx::isValid(color) && bgfx::isValid(depth)) {
    const bgfx::TextureHandle attachments[2] = {color, depth};
    this->msaaFrameBuffer = bgfx::createFrameBuffer(2, attachments, true);
  }
  if (!bgfx::isValid(this->msaaFrameBuffer)) {
    if (bgfx::isValid(color)) bgfx::destroy(color);
    if (bgfx::isValid(depth)) bgfx::destroy(depth);
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX MSAA framebuffer allocation failed"};
  }
  const auto probe = bgfx::createFrameBuffer(1, 1, bgfx::TextureFormat::R32F, peelTextureFlags);
  const auto staging = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R32F,
    BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
  if (!bgfx::isValid(probe) || !bgfx::isValid(staging)) {
    if (bgfx::isValid(probe)) bgfx::destroy(probe);
    if (bgfx::isValid(staging)) bgfx::destroy(staging);
    bgfx::destroy(this->msaaFrameBuffer); this->msaaFrameBuffer = BGFX_INVALID_HANDLE;
    return {CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX MSAA sample-count probe allocation failed"};
  }
  // Materialize all programs before the probe, and before any scene command.
  // A rejected effective count has never encoded or published scene geometry.
  bgfx::frame(BGFX_FRAME_FLUSH);
  sharedRuntime().programCommandsMaterialized = sharedRuntime().programCommandsQueued;
  this->encodeMsaaResolve(this->viewBase, probe, 1, 1, 0.0f);
  bgfx::resetView(this->viewBase + 1);
  bgfx::TextureRegion destination, source;
  destination.handle = staging; source.handle = bgfx::getTexture(probe);
  bgfx::blit(this->viewBase + 1, destination, source);
  float effective = -1.0f;
  bgfx::TextureRegion read;
  read.handle = staging;
  const uint32_t ready = bgfx::read(read, &effective);
  uint32_t completed = bgfx::frame();
  while (static_cast<int32_t>(completed - ready) < 0) completed = bgfx::frame();
  bgfx::resetView(this->viewBase); bgfx::resetView(this->viewBase + 1);
  bgfx::destroy(probe); bgfx::destroy(staging);
  const auto runtimeStatus = this->checkRuntimeFailure("BGFX MSAA sample-count verification failed");
  if (runtimeStatus != CoinRenderBackendStatus::SUCCESS || effective != float(this->sampleCount)) {
    bgfx::destroy(this->msaaFrameBuffer); this->msaaFrameBuffer = BGFX_INVALID_HANDLE;
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS) return {runtimeStatus, this->lastError};
    return {CoinRenderBackendStatus::UNSUPPORTED,
      "BGFX did not allocate the exact requested RGBA8/D32F MSAA sample count"};
  }
  this->msaaWidth = width; this->msaaHeight = height;
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_msaa requested=%u effective=%.0f width=%d height=%d\n",
      this->sampleCount, effective, width, height);
  return {};
}

bool
CoinBgfxBackend::prepareDepthReadbackResources(bool staging)
{
  if (!this->prepareFullscreenResources()) return false;
  if (!bgfx::isValid(this->depthReadProgram))
    this->depthReadProgram = createLayerProgram(
      COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_depth_readback), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_depth_readback));
  if (!bgfx::isValid(this->readDepthSampler))
    this->readDepthSampler = bgfx::createUniform("s_readDepth", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->depthReadFrameBuffer))
    this->depthReadFrameBuffer = bgfx::createFrameBuffer(
      static_cast<uint16_t>(this->width), static_cast<uint16_t>(this->height),
      bgfx::TextureFormat::R32F, peelTextureFlags);
  if (!bgfx::isValid(this->depthReadProgram) || !bgfx::isValid(this->readDepthSampler) ||
      !bgfx::isValid(this->depthReadFrameBuffer)) {
    this->lastError = "BGFX depth readback resource allocation failed";
    this->status = CoinRenderBackendStatus::BACKEND_ERROR;
    return false;
  }
  for (ReadbackSlot & slot : this->readbackSlots) {
    // Async tickets own their staging. In particular they must not resize a
    // pending ring slot's CPU destination while BGFX still writes into it.
    if (!staging || slot.pending) continue;
    if (!bgfx::isValid(slot.depthTexture))
      slot.depthTexture = bgfx::createTexture2D(this->width, this->height, false, 1,
        bgfx::TextureFormat::R32F, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    if (!bgfx::isValid(slot.depthTexture)) {
      this->lastError = "BGFX depth readback staging allocation failed";
      this->status = CoinRenderBackendStatus::OUT_OF_MEMORY;
      return false;
    }
    slot.depth.resize(size_t(this->width) * this->height);
  }
  return true;
}

bool
CoinBgfxBackend::prepareTransparencyPrograms(CoinBgfxTransparencyStrategy strategy, bool surfaceExecutors)
{
  if (strategy != CoinBgfxTransparencyStrategy::OBJECT && !this->prepareFullscreenResources()) return false;
  if (this->presentToWindow &&
      (this->multipassSceneRequired || bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL ||
       strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS)) {
    if (!this->prepareFullscreenResources()) return false;
    if (!bgfx::isValid(this->compositeProgram))
      this->compositeProgram = createLayerProgram(
        COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
        COIN_BGFX_SHADER_DATA(coin_bgfx_fs_composite), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_composite));
    if (!bgfx::isValid(this->layerSampler))
      this->layerSampler = bgfx::createUniform("s_layer", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->compositeProgram) || !bgfx::isValid(this->layerSampler)) {
      this->lastError = "BGFX OpenGL could not prepare window presentation";
      return false;
    }
  }
  if (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS) {
    if (this->sampleCount > 1) {
      if (!bgfx::isValid(this->peelMsaaColorSampler)) this->peelMsaaColorSampler = bgfx::createUniform("s_peelColorMS", bgfx::UniformType::Sampler);
      if (!bgfx::isValid(this->peelMsaaMaskSampler)) this->peelMsaaMaskSampler = bgfx::createUniform("s_peelMaskMS", bgfx::UniformType::Sampler);
      if (!bgfx::isValid(this->peelMsaaDepthSampler)) this->peelMsaaDepthSampler = bgfx::createUniform("s_peelDepthMS", bgfx::UniformType::Sampler);
      if (surfaceExecutors && !bgfx::isValid(this->peelMsaaProgram)) this->peelMsaaProgram = createLayerProgram(
        COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
        COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_peel_next_msaa), COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_peel_next_msaa));
      if (!bgfx::isValid(this->peelMsaaCompositeProgram)) this->peelMsaaCompositeProgram = createLayerProgram(
        COIN_BGFX_SHADER_DATA(coin_bgfx_vs), COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
        COIN_BGFX_SHADER_DATA(coin_bgfx_fs_peel_msaa_composite), COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_peel_msaa_composite));
      if ((surfaceExecutors && !bgfx::isValid(this->peelMsaaProgram)) || !bgfx::isValid(this->peelMsaaCompositeProgram) ||
          !bgfx::isValid(this->peelMsaaColorSampler) || !bgfx::isValid(this->peelMsaaMaskSampler) || !bgfx::isValid(this->peelMsaaDepthSampler)) {
        this->lastError = "BGFX MSAA peeling program allocation failed";
        return false;
      }
    }
    const uint8_t * vertexShader = COIN_BGFX_SHADER_DATA(coin_bgfx_vs);
    const uint32_t vertexBytes = COIN_BGFX_SHADER_SIZE(coin_bgfx_vs);
    if (surfaceExecutors && !bgfx::isValid(this->peelNextProgram)) this->peelNextProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_peel_next),
      COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_peel_next));
    if (!bgfx::isValid(this->compositeProgram)) this->compositeProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_composite),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_composite));
    if (!bgfx::isValid(this->compositeDepthProgram)) this->compositeDepthProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_composite_depth),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_composite_depth));
    if (!bgfx::isValid(this->previousDepthSampler)) this->previousDepthSampler = bgfx::createUniform("s_prevDepth", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->opaqueDepthSampler)) this->opaqueDepthSampler = bgfx::createUniform("s_opaqueDepth", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->layerSampler)) this->layerSampler = bgfx::createUniform("s_layer", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->layerMaskSampler)) this->layerMaskSampler = bgfx::createUniform("s_layerMask", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->layerDepthSampler)) this->layerDepthSampler = bgfx::createUniform("s_layerDepth", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->peelMetaUniform)) this->peelMetaUniform = bgfx::createUniform("u_peelMeta", bgfx::UniformType::Vec4);
    if ((surfaceExecutors && !bgfx::isValid(this->peelNextProgram)) ||
        !bgfx::isValid(this->compositeProgram) ||
        !bgfx::isValid(this->compositeDepthProgram) ||
        !bgfx::isValid(this->previousDepthSampler) ||
        !bgfx::isValid(this->opaqueDepthSampler) ||
        !bgfx::isValid(this->layerSampler) ||
        !bgfx::isValid(this->layerMaskSampler) ||
        !bgfx::isValid(this->layerDepthSampler) ||
        !bgfx::isValid(this->peelMetaUniform) ||
        !bgfx::isValid(this->depthInfoUniform) ||
        !bgfx::isValid(this->fullscreenVertexBuffer) ||
        !bgfx::isValid(this->fullscreenIndexBuffer)) {
      this->lastError = "BGFX could not allocate sorted-layers programs and uniforms";
      this->status = CoinRenderBackendStatus::BACKEND_ERROR;
      return false;
    }
  }
  if (strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT) {
    const uint8_t * vertexShader = COIN_BGFX_SHADER_DATA(coin_bgfx_vs);
    const uint32_t vertexBytes = COIN_BGFX_SHADER_SIZE(coin_bgfx_vs);
    if (this->sampleCount > 1) {
      if (!bgfx::isValid(this->weightedMsaaAccumSampler))
        this->weightedMsaaAccumSampler = bgfx::createUniform("s_oitAccumMS", bgfx::UniformType::Sampler);
      if (!bgfx::isValid(this->weightedMsaaRevealSampler))
        this->weightedMsaaRevealSampler = bgfx::createUniform("s_oitRevealMS", bgfx::UniformType::Sampler);
      if (!bgfx::isValid(this->weightedMsaaCompositeProgram))
        this->weightedMsaaCompositeProgram = createLayerProgram(vertexShader, vertexBytes,
          COIN_BGFX_SHADER_DATA(coin_bgfx_fs_weighted_composite_msaa),
          COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_weighted_composite_msaa));
      if (!bgfx::isValid(this->weightedMsaaCompositeProgram) ||
          !bgfx::isValid(this->weightedMsaaAccumSampler) || !bgfx::isValid(this->weightedMsaaRevealSampler)) {
        this->lastError = "BGFX could not allocate per-sample weighted composition";
        return false;
      }
    }
    if (surfaceExecutors && !bgfx::isValid(this->weightedOitProgram)) this->weightedOitProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_weighted_oit),
      COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_weighted_oit));
    if (!bgfx::isValid(this->weightedCompositeProgram)) this->weightedCompositeProgram = createLayerProgram(vertexShader, vertexBytes,
      COIN_BGFX_SHADER_DATA(coin_bgfx_fs_weighted_composite),
      COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_weighted_composite));
    if (!bgfx::isValid(this->oitAccumSampler)) this->oitAccumSampler = bgfx::createUniform("s_oitAccum", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->oitRevealSampler)) this->oitRevealSampler = bgfx::createUniform("s_oitReveal", bgfx::UniformType::Sampler);
    if ((surfaceExecutors && !bgfx::isValid(this->weightedOitProgram)) ||
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
      this->status = CoinRenderBackendStatus::BACKEND_ERROR;
      return false;
    }
  }
  return true;
}

void
CoinBgfxBackend::destroyFrameBuffers()
{
  for (uint8_t pass = 0; pass < COIN_RENDER_MAX_PEEL_LAYERS; ++pass) {
    if (bgfx::isValid(this->peelFrameBuffers[pass]))
      bgfx::destroy(this->peelFrameBuffers[pass]);
    this->peelFrameBuffers[pass] = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(this->oitFrameBuffer)) bgfx::destroy(this->oitFrameBuffer);
  this->oitFrameBuffer = BGFX_INVALID_HANDLE;
  if (bgfx::isValid(this->readbackTexture)) bgfx::destroy(this->readbackTexture);
  for (ReadbackSlot & slot : this->readbackSlots) {
    // CPU destinations must survive queued GPU writes, including resize.
    if (slot.pending) {
      uint32_t completed = bgfx::frame();
      while (static_cast<int32_t>(completed - slot.readyFrame) < 0)
        completed = bgfx::frame();
    }
    if (bgfx::isValid(slot.texture)) bgfx::destroy(slot.texture);
    if (bgfx::isValid(slot.depthTexture)) bgfx::destroy(slot.depthTexture);
  }
  if (bgfx::isValid(this->msaaFrameBuffer)) bgfx::destroy(this->msaaFrameBuffer);
  this->msaaFrameBuffer = BGFX_INVALID_HANDLE;
  this->msaaWidth = this->msaaHeight = 0;
  if (bgfx::isValid(this->depthReadFrameBuffer)) bgfx::destroy(this->depthReadFrameBuffer);
  this->depthReadFrameBuffer = BGFX_INVALID_HANDLE;
  this->lastPublishedDepth.clear();
  this->readbackSlots.clear();
  this->lastPublishedReadback.clear();
  this->readbackCursor = 0;
  this->readbackSequence = 0;
  this->lastPublishedSequence = 0;
  if (bgfx::isValid(this->frameBuffer)) bgfx::destroy(this->frameBuffer);
  if (bgfx::isValid(this->windowSceneFrameBuffer)) bgfx::destroy(this->windowSceneFrameBuffer);
  this->readbackTexture = BGFX_INVALID_HANDLE;
  this->frameBuffer = BGFX_INVALID_HANDLE;
  this->windowSceneFrameBuffer = BGFX_INVALID_HANDLE;
  this->width = this->height = 0;
}

bool
CoinBgfxBackend::resize(int newWidth, int newHeight)
{
  if (newWidth == this->width && newHeight == this->height &&
      !(this->presentToWindow && this->multipassSceneRequired && !bgfx::isValid(this->windowSceneFrameBuffer))) return true;
  if (newWidth <= 0 || newHeight <= 0 || newWidth > 16384 || newHeight > 16384) {
    this->lastError = "Invalid BGFX target dimensions";
    return false;
  }
  if (consumeTestFault("COIN_BGFX_TEST_DEVICE_LOST_ON_RESIZE_ONCE")) {
    static_cast<CoinBgfxCallback *>(this->callback.get())->inject(
      bgfx::Fatal::DeviceLost, "injected device loss during resize");
    this->checkRuntimeFailure("BGFX resize failed");
    return false;
  }
  const bool existingWindow = this->presentToWindow && bgfx::isValid(this->frameBuffer);
  const bool glWindowMsaa = this->presentToWindow && this->sampleCount > 1 &&
    bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL;
  // Resolve multisample textures in the producing context. Only the resolved
  // single-sample color crosses into the secondary native-window context.
  const bool prepareWindowScene = this->presentToWindow &&
    (this->multipassSceneRequired || ((existingWindow || glWindowMsaa) &&
    (bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL ||
     this->activeTransparencyStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS)));
  if (glWindowMsaa && uint64_t(newWidth) * uint64_t(newHeight) *
        (8ull * this->sampleCount + 8ull) > 128ull * 1024 * 1024) {
    this->lastError = "BGFX MSAA and window resolve attachments exceed 128 MiB";
    return false;
  }
  // Preserve the native framebuffer handle across resize; only its surface
  // attachments are resized. OIT/peel attachments remain private to this target.
  const bgfx::FrameBufferHandle windowOutput = this->presentToWindow ?
    this->frameBuffer : bgfx::FrameBufferHandle BGFX_INVALID_HANDLE;
  if (this->presentToWindow) this->frameBuffer = BGFX_INVALID_HANDLE;
  this->destroyFrameBuffers();
  if (this->presentToWindow) this->frameBuffer = windowOutput;
  const bgfx::Caps * caps = bgfx::getCaps();
  const bgfx::TextureFormat::Enum depthFormat =
    this->activeTransparencyStrategy != CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
    (caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
      ? bgfx::TextureFormat::D24S8 : bgfx::TextureFormat::D32F;
  if (this->presentToWindow) {
    bgfx::SwapChain swapChain;
    swapChain.ndt = this->nativeDisplay;
    swapChain.nwh = this->nativeWindow;
    swapChain.width = static_cast<uint32_t>(newWidth);
    swapChain.height = static_cast<uint32_t>(newHeight);
    // Let BGFX select the native surface format. Vulkan Xlib commonly uses
    // BGRA8; forcing RGBA8 reverses channels on drivers with a BGRA-only WSI.
    swapChain.formatColor = bgfx::TextureFormat::Count;
    swapChain.formatDepthStencil = depthFormat;
    if (bgfx::isValid(this->frameBuffer)) {
      bgfx::updateSwapChain(this->frameBuffer, swapChain);
    } else {
      this->frameBuffer = bgfx::createFrameBuffer(swapChain);
      if (!bgfx::isValid(this->frameBuffer)) {
        this->lastError = "BGFX could not create the target window swapchain";
        return false;
      }
    }
    if (prepareWindowScene) {
      bgfx::TextureHandle color = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, bgfx::TextureFormat::RGBA8, peelTextureFlags);
      bgfx::TextureHandle depth = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, depthFormat, peelTextureFlags);
      if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
        if (bgfx::isValid(color)) bgfx::destroy(color);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
        this->lastError = "BGFX OpenGL could not allocate the window scene target";
        return false;
      }
      const bgfx::TextureHandle attachments[2] = {color, depth};
      this->windowSceneFrameBuffer = bgfx::createFrameBuffer(2, attachments, true);
      if (!bgfx::isValid(this->windowSceneFrameBuffer)) {
        bgfx::destroy(color); bgfx::destroy(depth);
        this->lastError = "BGFX OpenGL could not create the window scene target";
        return false;
      }
    }
  } else {
    bgfx::TextureHandle color = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, bgfx::TextureFormat::RGBA8, peelTextureFlags);
    bgfx::TextureHandle depth = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, depthFormat, peelTextureFlags);
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
    const size_t readbackBytes = static_cast<size_t>(newWidth) * newHeight * 4u;
    this->readbackSlots.resize(this->readbackPipelineDepth);
    for (ReadbackSlot & slot : this->readbackSlots) {
      slot.texture = bgfx::createTexture2D(static_cast<uint16_t>(newWidth),
        static_cast<uint16_t>(newHeight), false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
      slot.pixels.resize(readbackBytes);
      if (!bgfx::isValid(slot.texture)) {
        this->lastError = "BGFX could not allocate the offscreen readback staging ring";
        this->destroyFrameBuffers();
        return false;
      }
    }
  }
  if (this->activeTransparencyStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS) {
    for (uint8_t pass = 0; pass < this->peelPassCount; ++pass) {
      bgfx::TextureHandle color =
          bgfx::createTexture2D(static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
                                false, 1, bgfx::TextureFormat::RGBA16F, weightedTextureFlags(this->sampleCount));
      bgfx::TextureHandle mask = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, bgfx::TextureFormat::R16F, weightedTextureFlags(this->sampleCount));
      bgfx::TextureHandle depth = bgfx::createTexture2D(
        static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
        false, 1, bgfx::TextureFormat::D32F, weightedTextureFlags(this->sampleCount));
      if (!bgfx::isValid(color) || !bgfx::isValid(mask) || !bgfx::isValid(depth)) {
        if (bgfx::isValid(color)) bgfx::destroy(color);
        if (bgfx::isValid(mask)) bgfx::destroy(mask);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
        this->lastError = "BGFX could not allocate sorted-layer textures";
        this->destroyFrameBuffers();
        return false;
      }
      const bgfx::TextureHandle attachments[3] = {color, mask, depth};
      this->peelFrameBuffers[pass] = bgfx::createFrameBuffer(3, attachments, true);
      if (!bgfx::isValid(this->peelFrameBuffers[pass])) {
        bgfx::destroy(color);
        bgfx::destroy(mask);
        bgfx::destroy(depth);
        this->lastError = "BGFX could not create sorted-layer framebuffer";
        this->destroyFrameBuffers();
        return false;
      }
    }
  }
  if (this->activeTransparencyStrategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT) {
    bgfx::TextureHandle accum = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, bgfx::TextureFormat::RGBA16F, weightedTextureFlags(this->sampleCount));
    bgfx::TextureHandle reveal = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),
      false, 1, bgfx::TextureFormat::R16F, weightedTextureFlags(this->sampleCount));
    bgfx::TextureHandle depth = bgfx::createTexture2D(
      static_cast<uint16_t>(newWidth), static_cast<uint16_t>(newHeight),

      false, 1, this->sampleCount > 1 ? bgfx::TextureFormat::D32F : depthFormat,
      this->sampleCount > 1 ? weightedTextureFlags(this->sampleCount) : BGFX_TEXTURE_RT_WRITE_ONLY);
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
  if (prepareWindowScene && existingWindow) {
    // Commit native drawable reconfiguration before the scene is rendered.
    // A touch presents no scene geometry and leaves public submission serials
    // unchanged. In particular, it does not retry a lost scene submission.
    bgfx::resetView(this->viewBase);
    bgfx::setViewName(this->viewBase, "window_resize_prepare");
    bgfx::setViewRect(this->viewBase, 0, 0, newWidth, newHeight);
    bgfx::setViewFrameBuffer(this->viewBase, this->frameBuffer);
    bgfx::setViewClear(this->viewBase, BGFX_CLEAR_NONE);
    bgfx::touch(this->viewBase);
    bgfx::frame();
    bgfx::resetView(this->viewBase);
    if (CoinRenderDiagnosticShell::phaseTracingEnabled())
      std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_window_resize_prepare size=%dx%d scene_draws=0\n",newWidth,newHeight);
  }
  return this->checkRuntimeFailure("BGFX resize failed") == CoinRenderBackendStatus::SUCCESS;
}
void
CoinBgfxBackend::bindDrawTexture(
  const CoinBgfxDraw & draw, const std::vector<bgfx::TextureHandle> & textures,
  bool shadowSurface, bool generalSurface, bool shadowMultitexture)
{
  const uint32_t declaredUnits = draw.portableShaderSlot ? COIN_RENDER_MAX_TEXTURE_UNITS : shadowMultitexture ? COIN_RENDER_MAX_TEXTURE_UNITS : shadowSurface ? 1 : generalSurface ? COIN_RENDER_MAX_TEXTURE_UNITS : this->activeTextureUnitCount;
  float params[COIN_RENDER_MAX_TEXTURE_UNITS][4] = {};
  float blend[COIN_RENDER_MAX_TEXTURE_UNITS][4] = {};
  float sizes[COIN_RENDER_MAX_TEXTURE_UNITS][4] = {};
  const bool portableEnabled=this->portableSampling;
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    CoinBgfxDraw::TextureLayer layer;
    if (unit == 0) {
      layer.enabled = draw.hasTexture; layer.slot = draw.textureSlot;
      layer.model = draw.textureModel; layer.wrapS = draw.wrapS; layer.wrapT = draw.wrapT;
      layer.wrapR = draw.wrapR;
      layer.filter = draw.filter; layer.maxAnisotropy = draw.maxAnisotropy;
      layer.cubeFaceSize = draw.cubeFaceSize; layer.cubeMipmapped = draw.cubeMipmapped;
      layer.volumeWidth = draw.volumeWidth; layer.volumeHeight = draw.volumeHeight;
      layer.volumeDepth = draw.volumeDepth; layer.volumeMipmapped = draw.volumeMipmapped;
      std::memcpy(layer.blendColor, draw.textureBlendColor, sizeof(layer.blendColor));
    } else layer = draw.extraTextures[unit - 1];
    bgfx::TextureHandle texture = this->defaultTexture;
    if (layer.enabled && layer.slot < textures.size() && bgfx::isValid(textures[layer.slot]))
      texture = textures[layer.slot];
    if (unit == 0) {
      const auto output = this->rasterImageOutputs.find(draw.renderStateSlot);
      if (output != this->rasterImageOutputs.end()) texture = output->second;
    }
    if(portableEnabled && layer.enabled) {
      const auto extent=this->samplingBoundTextureSizes.find(texture.idx);
      if(extent!=this->samplingBoundTextureSizes.end()) {sizes[unit][0]=float(extent->second.first);sizes[unit][1]=float(extent->second.second);}
    }
    uint32_t flags = BGFX_SAMPLER_NONE;
    if (layer.wrapS == CoinRenderTextureWrap::CLAMP) flags |= BGFX_SAMPLER_U_CLAMP;
    if (layer.wrapT == CoinRenderTextureWrap::CLAMP) flags |= BGFX_SAMPLER_V_CLAMP;
    if (layer.filter == CoinRenderTextureFilter::NEAREST)
      flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    if(layer.filter == CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR) flags |= BGFX_SAMPLER_MIN_POINT;
    if(layer.maxAnisotropy>1)flags|=BGFX_SAMPLER_MIN_ANISOTROPIC|BGFX_SAMPLER_MAG_ANISOTROPIC;
    if(layer.volumeDepth || layer.cubeFaceSize) flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    bool directRenderTarget = false;
    if (layer.enabled) for (const auto & resource : this->directTextures) {
      const auto direct = bgfx::getTexture(resource.frameBuffer, 0);
      if (bgfx::isValid(direct) && direct.idx == texture.idx) {
        directRenderTarget = true; sizes[unit][0] = float(resource.width); sizes[unit][1] = float(resource.height);
      }
    }
    params[unit][0] = layer.enabled ? 1.0f : 0.0f;
    sizes[unit][2] = float(layer.filter);
    sizes[unit][3] = float(unsigned(layer.wrapS) + 2 * unsigned(layer.wrapT));
    if(layer.volumeDepth) {
      sizes[unit][0] = float(layer.volumeWidth);
      sizes[unit][1] = float(layer.volumeHeight);
      sizes[unit][2] = float(4 + unsigned(layer.filter));
      sizes[unit][3] = float(layer.volumeDepth * 16 + unsigned(layer.wrapS) +
        2 * unsigned(layer.wrapT) + 4 * unsigned(layer.wrapR) +
        (layer.volumeMipmapped ? 8 : 0));
    }

    if (layer.cubeFaceSize) {
      sizes[unit][0] = float(layer.cubeFaceSize); sizes[unit][1] = layer.cubeMipmapped ? 1.0f : 0.0f;
      sizes[unit][2] = float(8 + unsigned(layer.filter)); sizes[unit][3] = 0;
    }


    params[unit][1] = static_cast<float>(layer.model);
    params[unit][2] = directRenderTarget && !layer.cubeFaceSize && !bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f;
    params[unit][3] = static_cast<float>(draw.textureProjection);
    std::memcpy(blend[unit], layer.blendColor, sizeof(layer.blendColor));
    // Bind only resources declared by the stage that will execute this draw.
    // Recycled native program slots can retain metadata for absent samplers;
    // supplying those undeclared bindings can corrupt its descriptor writes.
    // The general stage still declares and receives all eight scene units.
    if (unit < declaredUnits)
      bgfx::setTexture(static_cast<uint8_t>(unit + (shadowMultitexture ? 0 : 2)),
        unit == 0 ? this->textureSampler : this->extraTextureSamplers[unit - 1], texture, 0, 1, 0,
        (layer.volumeDepth || layer.cubeFaceSize) ? 1 :
          (CoinRenderTextureSamplingCore::mipFilter(layer.filter) ? UINT8_MAX : 1), flags);
  }
  bgfx::setUniform(this->samplingTextureSizesUniform, sizes, COIN_RENDER_MAX_TEXTURE_UNITS);
  bgfx::setUniform(this->textureParamsUniform, params, COIN_RENDER_MAX_TEXTURE_UNITS);
  bgfx::setUniform(this->textureBlendUniform, blend, COIN_RENDER_MAX_TEXTURE_UNITS);
  bgfx::setUniform(this->textureCombineUniform, draw.textureCombines, COIN_RENDER_MAX_TEXTURE_UNITS * 4);
}


bool
CoinBgfxBackend::prepareSurfaceDepthPrograms(const std::vector<CoinBgfxDraw> & draws,
  bool hasShadows, bool instanced, bool texture2d)
{
  if (this->sampleCount == 1 || draws.empty()) return true;
  if (!bgfx::isValid(this->depthInfoUniform))
    this->depthInfoUniform = bgfx::createUniform("u_depthInfo", bgfx::UniformType::Vec4);
  const bool custom = this->shadowMultitextureSampleDepthActive || bgfxExplicitSurfaceSamples() || std::any_of(draws.begin(), draws.end(), bgfxCustomSampleDepth);
  if (custom && !bgfxSampleInterpolationSupported(this->sampleCount, this->lastError)) return false;
  if (instanced) {
    if (bgfxExplicitSurfaceSamples()) {
      if (!bgfx::isValid(this->instancedSampleDepthProgram))
        this->instancedSampleDepthProgram = bgfxTracePreparedProgram(
            createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_instanced_color),
                               COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_instanced_color),
                               COIN_BGFX_SHADER_DATA(coin_bgfx_fs_instanced_sample_depth),
                               COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_instanced_sample_depth)),
            hasShadows ? "shadow_receiver" : "general_surface", this->sampleCount);
      return bgfx::isValid(this->instancedSampleDepthProgram);
    }
    if (!bgfx::isValid(this->instancedNativeDepthProgram))
      this->instancedNativeDepthProgram = bgfxTracePreparedProgram(
          createLayerProgram(COIN_BGFX_SHADER_DATA(coin_bgfx_vs_instanced_color),
                             COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_instanced_color),
                             COIN_BGFX_SHADER_DATA(coin_bgfx_fs_instanced_native_depth),
                             COIN_BGFX_SHADER_SIZE(coin_bgfx_fs_instanced_native_depth)),
          hasShadows ? "shadow_receiver" : "general_surface", this->sampleCount);
    return bgfx::isValid(this->instancedNativeDepthProgram);
  }
  bgfx::ProgramHandle & native = hasShadows ? this->shadowNativeDepthProgram :
    texture2d ? this->texture2dNativeDepthProgram : this->surfaceNativeDepthProgram;
  const bool nativeNeeded =
      !this->shadowMultitextureSampleDepthActive && !bgfxExplicitSurfaceSamples() &&
      std::any_of(draws.begin(), draws.end(),
                  [](const CoinBgfxDraw &draw) { return !bgfxCustomSampleDepth(draw); });
  if (nativeNeeded && !bgfx::isValid(native))
    native = bgfxTracePreparedProgram(
        createLayerProgram(hasShadows ? COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver)
                                      : COIN_BGFX_SHADER_DATA(coin_bgfx_vs),
                           hasShadows ? COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver)
                                      : COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
                           hasShadows ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_native_depth)
                           : texture2d ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_texture2d_native_depth)
                                       : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_surface_native_depth),
                           hasShadows ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_native_depth)
                           : texture2d ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_texture2d_native_depth)
                                       : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_surface_native_depth)),
        hasShadows ? "shadow_receiver" : "general_surface", this->sampleCount);
  bgfx::ProgramHandle & sampled = hasShadows ? this->shadowSampleDepthProgram :
    texture2d ? this->texture2dSampleDepthProgram : this->surfaceSampleDepthProgram;
  if (custom && !bgfx::isValid(sampled))
    sampled = bgfxTracePreparedProgram(
        createLayerProgram(hasShadows ? COIN_BGFX_SHADER_DATA(coin_bgfx_vs_shadow_receiver)
                                      : COIN_BGFX_SHADER_DATA(coin_bgfx_vs),
                           hasShadows ? COIN_BGFX_SHADER_SIZE(coin_bgfx_vs_shadow_receiver)
                                      : COIN_BGFX_SHADER_SIZE(coin_bgfx_vs),
                           hasShadows ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_shadow_sample_depth)
                           : texture2d ? COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_texture2d_sample_depth)
                                       : COIN_BGFX_SURFACE_DATA(coin_bgfx_fs_surface_sample_depth),
                           hasShadows ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_shadow_sample_depth)
                           : texture2d ? COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_texture2d_sample_depth)
                                       : COIN_BGFX_SURFACE_SIZE(coin_bgfx_fs_surface_sample_depth)),
        hasShadows ? "shadow_receiver" : "general_surface", this->sampleCount);
  if ((nativeNeeded && !bgfx::isValid(native)) || (custom && !bgfx::isValid(sampled)) ||
      !bgfx::isValid(this->depthInfoUniform)) {
    this->lastError = "BGFX MSAA surface-depth program allocation failed";
    return false;
  }
  return true;
}

void
CoinBgfxBackend::submitSurfaceDraw(bgfx::ViewId view, const CoinBgfxDraw & draw,
  const std::vector<bgfx::TextureHandle> & textures, const CoinRenderFramePlan & frame,
  const CoinRenderShadowPlan & shadowPlan,
  const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
  int width, int height, bgfx::TextureHandle sampleDepth)
{
  if (draw.bumpSlot && draw.bumpExtended) {
    this->bindDrawTexture(draw, textures, true, this->sampleCount > 1, true);
    this->bindShadowReceiver(frame, shadowPlan, shadowMaps, draw, height, 8);
    const auto &state=frame.renderStates[draw.renderStateSlot];
    const auto &lights=frame.lightingStates[state.lightingSlot].lights;
    const SbMatrix modelView=state.model*state.view;
    const auto inverseDirection=CoinRenderTransformCore::normalMatrix(modelView).transpose();
    float directions[8][4]={},colors[8][4]={};
    for (size_t i=0;i<lights.size();++i) {
      SbVec3f direction;
      const bool positional = lights[i].type != CoinRenderLightType::DIRECTIONAL;
      inverseDirection.multDirMatrix(positional ? SbVec3f(lights[i].position) -
        SbVec3f(modelView[3][0],modelView[3][1],modelView[3][2]) : -SbVec3f(lights[i].direction),direction);
      if (!positional) direction.normalize();
      directions[i][3] = positional ? 1.0f : 0.0f;
      direction.getValue(directions[i][0],directions[i][1],directions[i][2]);
      std::copy_n(lights[i].color,3,colors[i]);
      colors[i][3]=lights[i].intensity;
    }
    SbVec3f eye;
    inverseDirection.multDirMatrix(-SbVec3f(modelView[3][0],modelView[3][1],modelView[3][2]),eye);
    const float eyeValues[4]={eye[0],eye[1],eye[2],0};
    const float bumpMeta[4]={float(lights.size()),0,0,0};
    bgfx::setUniform(this->bumpLightMetaUniform,bumpMeta);
    bgfx::setUniform(this->bumpLightVectorsUniform,directions,8);
    bgfx::setUniform(this->bumpLightColorsUniform,colors,8);
    bgfx::setUniform(this->bumpEyeUniform,eyeValues);
    bgfx::setUniform(this->bumpModelViewUniform,modelView.getValue());
    bgfx::setTexture(10, this->shadowArraySampler,
                     this->shadowArrayActive ? this->shadowArrayTexture
                                             : this->defaultShadowArrayTexture);
    const uint32_t flags = (draw.bumpClampS ? BGFX_SAMPLER_U_CLAMP : 0) |
                           (draw.bumpClampT ? BGFX_SAMPLER_V_CLAMP : 0) | BGFX_SAMPLER_MIP_POINT;
    bgfx::setTexture(8, this->bumpImageSampler, textures[draw.bumpTextureSlot], 0, 1, 0, 1, flags);
    const float info[4] = {1.0f / width, 1.0f / height, 0, 0};
    bgfx::setUniform(this->depthInfoUniform, info);
    const auto program = this->sampleCount == 1         ? this->bumpReceiverProgram
                         : bgfxExplicitSurfaceSamples() ? this->bumpReceiverSampleProgram
                                                        : this->bumpReceiverNativeProgram;
    this->submitPortableDraw(view, program, draw, frame, PortableFamily::BASE);
    return;
  }
  if(draw.bumpSlot) {
    const uint32_t normalFlags=(draw.bumpClampS?BGFX_SAMPLER_U_CLAMP:0) |
      (draw.bumpClampT?BGFX_SAMPLER_V_CLAMP:0) | BGFX_SAMPLER_MIP_POINT;
    bgfx::setTexture(0,this->bumpImageSampler,textures[draw.bumpTextureSlot],0,1,0,1,normalFlags);
    uint32_t baseFlags=BGFX_SAMPLER_MIP_POINT;
    if(draw.wrapS==CoinRenderTextureWrap::CLAMP)baseFlags|=BGFX_SAMPLER_U_CLAMP;
    if(draw.wrapT==CoinRenderTextureWrap::CLAMP)baseFlags|=BGFX_SAMPLER_V_CLAMP;
    if(draw.filter==CoinRenderTextureFilter::NEAREST)baseFlags|=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT;
    if(draw.filter==CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR)baseFlags|=BGFX_SAMPLER_MIN_POINT;
    if(CoinRenderTextureSamplingCore::mipFilter(draw.filter))baseFlags&=~BGFX_SAMPLER_MIP_POINT;
    if(draw.maxAnisotropy>1)baseFlags|=BGFX_SAMPLER_MIN_ANISOTROPIC|BGFX_SAMPLER_MAG_ANISOTROPIC;
    bgfx::setTexture(1,this->bumpBaseSampler,draw.hasTexture?textures[draw.textureSlot]:this->defaultTexture,
      0,1,0,CoinRenderTextureSamplingCore::mipFilter(draw.filter)?UINT8_MAX:1,baseFlags);
    this->bindDrawLighting(draw,height,width);
    const float info[4]={1.0f/width,1.0f/height,0,0};
    if(bgfx::isValid(this->depthInfoUniform))bgfx::setUniform(this->depthInfoUniform,info);
    float params[4]={draw.hasTexture?1.0f:0.0f,0,0,0};
    bgfx::setUniform(this->bumpParamsUniform,params);
    const auto program=this->sampleCount==1?this->bumpProgram:
      bgfxExplicitSurfaceSamples()?this->bumpSampleDepthProgram:this->bumpNativeDepthProgram;
    this->submitPortableDraw(view,program, draw, frame, PortableFamily::BASE,0,BGFX_DISCARD_NONE);
    params[1]=1;bgfx::setUniform(this->bumpParamsUniform,params);
    // The additive pass changes lighting, not the authored channel mask.
    // Keep its depth read-only: the first pass already wrote geometry depth.
    const uint64_t colorWrites = drawState(draw) & (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setState(colorWrites | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_MSAA |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ONE));
    this->submitPortableDraw(view,program, draw, frame, PortableFamily::BASE);
    return;
  }
  const bool hasShadows = !shadowPlan.passes.empty();
  const bool perSampleDepth = this->sampleCount > 1 && bgfx::isValid(sampleDepth);
  const bool customDepth = this->sampleCount > 1 && (this->shadowMultitextureSampleDepthActive || bgfxExplicitSurfaceSamples() || bgfxCustomSampleDepth(draw));
  const bool instanced = !hasShadows && bgfx::isValid(this->activeProgram) &&
    this->activeProgram.idx == this->instancedProgram.idx;
  const bool texture2dDepth = !draw.portableShaderSlot && this->activeTexture2dDepth && !hasShadows && !instanced && !perSampleDepth;
  if (!draw.portableShaderSlot && this->sampleCount > 1 && CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_surface_depth_program family=%s mode=%s samples=%u\n",
      texture2dDepth ? "texture2d" : "general", customDepth || perSampleDepth ? "sample" : "native", this->sampleCount);
  const bool multitexture = hasShadows && this->shadowArrayActive;
  this->bindDrawTexture(draw, textures, hasShadows, this->sampleCount > 1 && !instanced && !texture2dDepth, multitexture);
  if (hasShadows)
    this->bindShadowReceiver(frame, shadowPlan, shadowMaps, draw, height, multitexture || this->sampleCount > 1 ? 8 : 0);
  else this->bindDrawLighting(draw, height, width);
  if (multitexture && !perSampleDepth) {
    if (!customDepth) { this->submitPortableDraw(view, this->shadowMultitextureProgram, draw, frame, PortableFamily::SHADOW_BASE); return; }
    const bool sampleInvocation = bgfxExplicitSurfaceSamples();
    const auto program = sampleInvocation ? this->shadowMultitextureProgram : this->shadowMultitextureSampleDepthProgram;
    const uint32_t draws = sampleInvocation ? 1 : this->sampleCount;
    for (uint32_t sample = 0; sample < draws; ++sample) {
      const float info[4] = {1.0f / float(width), 1.0f / float(height), float(sample), 0};
      bgfx::setUniform(this->depthInfoUniform, info);
      bgfx::setSampleMask(sampleInvocation ? UINT32_MAX : (1u << sample));
      this->submitPortableDraw(view, program, draw, frame, sampleInvocation ? PortableFamily::SHADOW_BASE : PortableFamily::SHADOW_SAMPLE, 0, sample + 1 == draws ? BGFX_DISCARD_ALL : BGFX_DISCARD_NONE);
    }
    bgfx::setSampleMask();
    return;
  }
  if (!perSampleDepth) {
    if (this->sampleCount == 1) {
      this->submitPortableDraw(view, hasShadows ? (shadowPlan.passes.size() > 4 ? this->shadowReceiverProgram8 :
        shadowPlan.passes.size() > 2 ? this->shadowReceiverProgram4 : this->shadowReceiverProgram) : this->activeProgram, draw, frame, hasShadows ? PortableFamily::SHADOW_BASE : PortableFamily::BASE);
    } else if (!customDepth) {
      // No fragment-depth output: retain hardware depth at each covered sample.
      this->submitPortableDraw(view, hasShadows ? this->shadowNativeDepthProgram :
        instanced ? this->instancedNativeDepthProgram :
        texture2dDepth ? this->texture2dNativeDepthProgram : this->surfaceNativeDepthProgram, draw, frame, hasShadows ? PortableFamily::SHADOW_BASE : PortableFamily::NATIVE_DEPTH);
    } else {
      const auto program = hasShadows ? this->shadowSampleDepthProgram :
        instanced ? this->instancedSampleDepthProgram :
        texture2dDepth ? this->texture2dSampleDepthProgram : this->surfaceSampleDepthProgram;
      const bool sampleInvocation = bgfx::getRendererType() == bgfx::RendererType::OpenGL ||
        bgfx::getRendererType() == bgfx::RendererType::OpenGLES;
      const uint32_t draws = sampleInvocation ? 1 : this->sampleCount;
      for (uint32_t sample = 0; sample < draws; ++sample) {
        const float info[4] = {1.0f / float(width), 1.0f / float(height), float(sample), 0};
        bgfx::setUniform(this->depthInfoUniform, info);
        bgfx::setSampleMask(sampleInvocation ? UINT32_MAX : (1u << sample));
        this->submitPortableDraw(view, program, draw, frame, hasShadows ? PortableFamily::SHADOW_SAMPLE : PortableFamily::SAMPLE_DEPTH, 0, sample + 1 == draws ? BGFX_DISCARD_ALL : BGFX_DISCARD_NONE);
      }
      bgfx::setSampleMask();
    }
    return;
  }
  // Use the selector's exact depth expression also for every opaque/immediate
  // writer in this frame. Native opaque depth (or pixel-center shader depth)
  // can round differently and breaks coplanar EQUAL/LEQUAL at individual samples.
  // The scratch depth is a distinct attachment, never the output being written;
  // these disabled selection tests perform no reads from its contents.
  const float meta[4] = {0, 0, 0, 0};
  bgfx::setUniform(this->peelMetaUniform, meta);
  bgfx::setTexture(multitexture?8:0, this->previousDepthSampler, sampleDepth);
  bgfx::setTexture(multitexture?9:1, this->opaqueDepthSampler, sampleDepth);
  const auto program = multitexture ? this->shadowMultitexturePeelProgram : hasShadows ? this->shadowPeelMsaaProgram : this->peelMsaaProgram;
  for (uint32_t sample = 0; sample < this->sampleCount; ++sample) {
    const float info[4] = {1.0f / float(width), 1.0f / float(height), float(sample), 0};
    bgfx::setUniform(this->depthInfoUniform, info);
    bgfx::setSampleMask(peelFixedSampleMask(this->sampleCount, sample));
    this->submitPortableDraw(view, program, draw, frame, hasShadows ? PortableFamily::SHADOW_PEEL : PortableFamily::PEEL, 0, sample + 1 == this->sampleCount ? BGFX_DISCARD_ALL : BGFX_DISCARD_NONE);
  }
  bgfx::setSampleMask();
}

bgfx::ViewId
CoinBgfxBackend::encodeDeferredRuns(const std::vector<CoinBgfxDraw> & draws,
  bgfx::DynamicVertexBufferHandle vertices, bgfx::DynamicIndexBufferHandle indices,
  bgfx::FrameBufferHandle output, const std::vector<bgfx::TextureHandle> & textures,
  bgfx::ViewId next, int width, int height, const CoinRenderFramePlan & frame,
  const CoinRenderShadowPlan & shadows, const std::vector<bgfx::FrameBufferHandle> & maps,
  CoinBgfxTransparencyStrategy strategy, const std::vector<bgfx::FrameBufferHandle> & layers,
  bgfx::FrameBufferHandle oit, bgfx::TextureHandle sampleDepth)
{
  std::vector<CoinBgfxDraw> run, depthContext;
  const bool weighted = strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
  const auto encodeDirect = [&](const std::vector<CoinBgfxDraw> & ordered) {
      bgfx::setViewName(next,"ordered_transparency");bgfx::setViewMode(next,bgfx::ViewMode::Sequential);
      bgfx::setViewRect(next,0,0,uint16_t(width),uint16_t(height));bgfx::setViewFrameBuffer(next,output);
      bgfx::setViewClear(next,BGFX_CLEAR_NONE);bgfx::setViewTransform(next,nullptr,nullptr);
      for(const auto & draw:ordered) {
        if(!draw.isSceneLayer() || !draw.blend || !draw.deferred || !setDrawScissor(draw,width,height))continue;
        bgfx::setTransform(draw.mvp);bgfx::setVertexBuffer(0,vertices);bgfx::setIndexBuffer(indices,draw.firstIndex,draw.indexCount);
        bgfx::setState(drawState(draw)|(this->sampleCount>1?BGFX_STATE_MSAA:0));
        this->submitSurfaceDraw(next,draw,textures,frame,shadows,maps,width,height,sampleDepth);
        if (weighted && draw.depthTest && draw.depthWrite) depthContext.push_back(draw);
      }
      ++next;
  };
  // Ordinary object composition already has a final common order. Iterate
  // it directly, without copying draw payloads into a temporary run.
  if (strategy == CoinBgfxTransparencyStrategy::OBJECT) { encodeDirect(draws); return next; }
  // Weighted accumulation reconstructs depth in its private attachments. Keep
  // every preceding depth writer separate from the current run's color inputs.
  if (weighted) for (const auto & draw : draws)
    if (draw.isSceneLayer() && !(draw.blend && draw.deferred) && draw.depthTest && draw.depthWrite)
      depthContext.push_back(draw);
  const auto flush = [&]() {
    if (run.empty()) return;
    if (!run.front().additive && strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS) {
      this->encodeSortedLayers(run,vertices,indices,output,textures,next,width,height,frame,shadows,maps,layers,oit);
      next += bgfx::ViewId(layers.size()+1);
    } else if (!run.front().additive && strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT) {
      this->encodeWeightedOit(run,vertices,indices,output,textures,next,width,height,frame,shadows,maps,{},oit,depthContext);
      next += 2;
    } else {
      encodeDirect(run);
    }
    run.clear();
  };
  for(const auto & draw:draws) {
    if(!draw.isSceneLayer()||!draw.blend||!draw.deferred)continue;
    if(!run.empty() && strategy!=CoinBgfxTransparencyStrategy::OBJECT && run.back().additive!=draw.additive)flush();
    run.push_back(draw);
  }
  flush();return next;
}

void
CoinBgfxBackend::bindDrawLighting(const CoinBgfxDraw & draw, int targetHeight, int targetWidth)
{
  const float door[4] = {draw.screenDoor[0], float(targetHeight > 0 ? targetHeight : this->height),
    bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f, draw.screenDoor[3]};
  bgfx::setUniform(this->screenDoorUniform, door);
  const float raster[4] = {0.0f, 0.0f,
    this->canonicalCoverage ? float(targetWidth > 0 ? targetWidth : this->width) : 0.0f,
    this->canonicalCoverage ? float(targetHeight > 0 ? targetHeight : this->height) : 0.0f};
  bgfx::setUniform(this->rasterViewportUniform, raster);
  const float alphaTest[4] = {static_cast<float>(draw.alphaTestFunction), draw.alphaTestReference, 0.0f, 0.0f};
  bgfx::setUniform(this->alphaTestUniform, alphaTest);
  bgfx::setUniform(this->clipMetaUniform, draw.clipMeta);
  bgfx::setUniform(this->clipPlanesUniform, draw.clipPlanes, COIN_RENDER_MAX_CLIP_PLANES);
  // Use two D24 LSBs on GL to survive the gl_FragCoord-to-gl_FragDepth
  // floating-point round trip; other renderers retain the one-LSB contract.
  // Driver-specific native polygon offset resolution remains approximate.
  const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
  const float unitScale =
    (renderer == bgfx::RendererType::OpenGL ||
     renderer == bgfx::RendererType::OpenGLES)
      ? 1.0f / 8388608.0f : 1.0f / 16777216.0f;
  const float depth[4] = {draw.depthRange[0], draw.depthRange[1],
    draw.polygonOffsetFactor, draw.polygonOffsetUnits * unitScale + draw.polygonOffsetSlopeBias};
  bgfx::setUniform(this->coinDepthUniform, depth);
  bgfx::setUniform(this->fogColorModeUniform, draw.fogColorMode);
  bgfx::setUniform(this->fogRangeUniform, draw.fogRange);
  bgfx::setUniform(this->ambientLightUniform, draw.ambientLight);
  bgfx::setUniform(this->lightCountUniform, draw.lightCount);
  bgfx::setUniform(this->lightPositionTypeUniform, draw.lightPositionType, COIN_RENDER_MAX_LIGHTS);
  bgfx::setUniform(this->lightDirectionCutoffUniform, draw.lightDirectionCutoff, COIN_RENDER_MAX_LIGHTS);
  bgfx::setUniform(this->lightColorIntensityUniform, draw.lightColorIntensity, COIN_RENDER_MAX_LIGHTS);
  bgfx::setUniform(this->lightAmbientColorUniform, draw.lightAmbientColor, COIN_RENDER_MAX_LIGHTS);
  bgfx::setUniform(this->lightAttenuationDropUniform, draw.lightAttenuationDrop, COIN_RENDER_MAX_LIGHTS);
  if (draw.instanceCount)
    bgfx::setUniform(this->instancedCameraUniform, draw.instanceCamera, 6);
}

void
CoinBgfxBackend::encodeSortedLayers(const std::vector<CoinBgfxDraw> & draws,
                                      bgfx::DynamicVertexBufferHandle vertices,
                                      bgfx::DynamicIndexBufferHandle indices,
                                      bgfx::FrameBufferHandle output,
                                      const std::vector<bgfx::TextureHandle> & textures,
  bgfx::ViewId firstView, int width, int height, const CoinRenderFramePlan & frame,
  const CoinRenderShadowPlan & shadows, const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
  const std::vector<bgfx::FrameBufferHandle> & layers, bgfx::FrameBufferHandle oitBuffer)
{
  const bool hasShadows = !shadows.passes.empty();
  const float transparentBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float depthInfo[4] = {1.0f / float(width),
                              1.0f / float(height), 0.0f, 0.0f};
  const bgfx::TextureHandle opaqueDepth = bgfx::getTexture(output, 1);
  bgfx::setPaletteColor(1, transparentBlack);
  for (uint8_t pass = 0; pass < layers.size(); ++pass) {
    const bgfx::ViewId view = firstView + pass;
    bgfx::setViewName(view, "transparent_accumulation");
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(view, 0, 0, static_cast<uint16_t>(width),
                      static_cast<uint16_t>(height));
    bgfx::setViewFrameBuffer(view, layers[pass]);
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 1, 1);
    bgfx::setViewTransform(view, nullptr, nullptr);
    bgfx::touch(view);
    for (uint32_t sample = 0; sample < this->sampleCount; ++sample) {
      const float selectionInfo[4] = {depthInfo[0], depthInfo[1], float(sample), 0};
      for (const CoinBgfxDraw & draw : draws) {
        if (!draw.isSceneLayer() || !draw.blend || !draw.deferred || draw.additive) continue;
        bgfx::setTransform(draw.mvp);
        bgfx::setVertexBuffer(0, vertices);
        bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
        bgfx::setSampleMask(peelFixedSampleMask(this->sampleCount, sample));
        bgfx::setState(peelSelectionState(draw) | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
        if (!setDrawScissor(draw, width, height)) continue;
        const float peelMeta[4] = {float(pass), draw.depthTest ? 1.0f : 0.0f,
          float(draw.depthFunction), draw.depthTest && draw.depthWrite ? 1.0f : 0.0f};
        bgfx::setUniform(this->depthInfoUniform, selectionInfo);
        bgfx::setUniform(this->peelMetaUniform, peelMeta);
        bgfx::setTexture(this->shadowArrayActive?8:0, this->previousDepthSampler,
          pass == 0 ? opaqueDepth : bgfx::getTexture(layers[pass - 1], 2));
        bgfx::setTexture(this->shadowArrayActive?9:1, this->opaqueDepthSampler, opaqueDepth);
        this->bindDrawTexture(draw, textures, hasShadows,false,hasShadows&&this->shadowArrayActive);
        if (hasShadows) this->bindShadowReceiver(frame, shadows, shadowMaps, draw, height, 8);
        else this->bindDrawLighting(draw, height);
        this->submitPortableDraw(view, hasShadows&&this->shadowArrayActive ? this->shadowMultitexturePeelProgram : this->sampleCount > 1 ? (hasShadows ? this->shadowPeelMsaaProgram : this->peelMsaaProgram) :
          (hasShadows ? this->shadowPeelProgram : this->peelNextProgram), draw, frame, hasShadows ? PortableFamily::SHADOW_PEEL : PortableFamily::PEEL);
      }
    }
    bgfx::setSampleMask();
  }

  const bgfx::ViewId compositeView = firstView + layers.size();
  bgfx::setViewName(compositeView, "fullscreen_composition");
  bgfx::setViewMode(compositeView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(compositeView, 0, 0, static_cast<uint16_t>(width),
                    static_cast<uint16_t>(height));
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
  for (int pass = int(layers.size()) - 1; pass >= 0; --pass) {
    if (this->sampleCount > 1) {
      for (uint32_t sample = 0; sample < this->sampleCount; ++sample) {
        for (unsigned depthPass = 0; depthPass < 2; ++depthPass) {
          const float info[4] = {depthInfo[0], depthInfo[1], depthPass ? 2.0f : 1.0f, float(sample)};
          bgfx::setTransform(identity);
          bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
          bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
          bgfx::setUniform(this->depthInfoUniform, info);
          bgfx::setTexture(0, this->peelMsaaColorSampler, bgfx::getTexture(layers[pass], 0));
          bgfx::setTexture(1, this->peelMsaaMaskSampler, bgfx::getTexture(layers[pass], 1));
          bgfx::setTexture(2, this->peelMsaaDepthSampler, bgfx::getTexture(layers[pass], 2));
          bgfx::setSampleMask(peelFixedSampleMask(this->sampleCount, sample));
          bgfx::setState((depthPass ? BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS : blendState) | BGFX_STATE_MSAA);
          bgfx::submit(compositeView, this->peelMsaaCompositeProgram);
        }
      }
      bgfx::setSampleMask();
      continue;
    }
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, depthInfo);
    bgfx::setTexture(0, this->layerSampler,
                     bgfx::getTexture(layers[pass], 0));
    bgfx::setState(blendState);
    bgfx::submit(compositeView, this->compositeProgram);

    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, depthInfo);
    bgfx::setTexture(0, this->layerMaskSampler, bgfx::getTexture(layers[pass], 1));
    bgfx::setTexture(1, this->layerDepthSampler, bgfx::getTexture(layers[pass], 2));
    bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS);
    bgfx::submit(compositeView, this->compositeDepthProgram);
  }
}

void
CoinBgfxBackend::encodeWeightedOit(const std::vector<CoinBgfxDraw> & draws,
                                     bgfx::DynamicVertexBufferHandle vertices,
                                     bgfx::DynamicIndexBufferHandle indices,
                                     bgfx::FrameBufferHandle output,
                                     const std::vector<bgfx::TextureHandle> & textures,
  bgfx::ViewId firstView, int width, int height, const CoinRenderFramePlan & frame,
  const CoinRenderShadowPlan & shadows, const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
  const std::vector<bgfx::FrameBufferHandle> & layers, bgfx::FrameBufferHandle oitBuffer,
  const std::vector<CoinBgfxDraw> & depthContext)
{
  const bool hasShadows = !shadows.passes.empty();
  const bgfx::ViewId oitView = firstView;
  bgfx::setViewName(oitView, "transparent_accumulation");
  const float transparentBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float opaqueWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  const float depthInfo[4] = {1.0f / float(width),
                              1.0f / float(height), 0.0f, 0.0f};
  bgfx::setPaletteColor(1, transparentBlack);
  bgfx::setPaletteColor(2, opaqueWhite);
  bgfx::setViewMode(oitView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(oitView, 0, 0, static_cast<uint16_t>(width),
                    static_cast<uint16_t>(height));
  bgfx::setViewFrameBuffer(oitView, oitBuffer);
  bgfx::setViewClear(oitView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                     1.0f, 0, 1, 2);
  bgfx::setViewTransform(oitView, nullptr, nullptr);
  bgfx::touch(oitView);

  // Rebuild the already-published depth history: immediate geometry followed
  // by explicit depth writers in preceding additive runs. These draws never
  // enter this run's accumulation/color loops.
  for (const CoinBgfxDraw & draw : depthContext) {
    if (!draw.isSceneLayer() || !draw.depthTest || !draw.depthWrite)
      continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vertices);
    bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
    bgfx::setState(peelDrawState(draw, true) | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
    if (!setDrawScissor(draw, width, height)) continue;
    this->submitSurfaceDraw(oitView, draw, textures, frame, CoinRenderShadowPlan(), {},
      width, height, bgfx::TextureHandle BGFX_INVALID_HANDLE);

  }

  for (const CoinBgfxDraw & draw : draws) {
    if (!draw.isSceneLayer() || !draw.blend || !draw.deferred || draw.additive)
      continue;
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
    bgfx::setState(state | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0), independentBlend);
    if (!setDrawScissor(draw, width, height)) continue;
    this->bindDrawTexture(draw, textures, hasShadows,false,hasShadows&&this->shadowArrayActive);
    if (hasShadows) this->bindShadowReceiver(frame, shadows, shadowMaps, draw, height, 8);
    else this->bindDrawLighting(draw, height);
    if(hasShadows&&this->shadowArrayActive) {
      const bool sampleInvocation=bgfxExplicitSurfaceSamples();
      const uint32_t samples=this->sampleCount>1&&!sampleInvocation?this->sampleCount:1;
      for(uint32_t sample=0;sample<samples;++sample) {
        const float info[4]={1.0f/float(width),1.0f/float(height),float(sample),0};
        bgfx::setUniform(this->depthInfoUniform,info);
        bgfx::setSampleMask(samples>1?1u<<sample:UINT32_MAX);
        this->submitPortableDraw(oitView,this->shadowMultitextureOitProgram, draw, frame, PortableFamily::SHADOW_OIT,0,sample+1==samples?BGFX_DISCARD_ALL:BGFX_DISCARD_NONE);
      }
      bgfx::setSampleMask();
    } else this->submitPortableDraw(oitView, hasShadows ? this->shadowOitProgram : this->weightedOitProgram, draw, frame, hasShadows ? PortableFamily::SHADOW_OIT : PortableFamily::OIT);

  }

  const bgfx::ViewId compositeView = firstView + 1;
  bgfx::setViewName(compositeView, "fullscreen_composition");
  bgfx::setViewMode(compositeView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(compositeView, 0, 0, static_cast<uint16_t>(width),
                    static_cast<uint16_t>(height));
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
  for (uint32_t sample = 0; sample < this->sampleCount; ++sample) {
    const float info[4] = {depthInfo[0], depthInfo[1], 1.0f, float(sample)};
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, info);
    bgfx::setTexture(0, this->sampleCount > 1 ? this->weightedMsaaAccumSampler : this->oitAccumSampler,
                     bgfx::getTexture(oitBuffer, 0));
    bgfx::setTexture(1, this->sampleCount > 1 ? this->weightedMsaaRevealSampler : this->oitRevealSampler,
                     bgfx::getTexture(oitBuffer, 1));
    bgfx::setSampleMask(this->sampleCount > 1 ? 1u << sample : UINT32_MAX);
    bgfx::setState(blendState | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
    bgfx::submit(compositeView, this->sampleCount > 1 ? this->weightedMsaaCompositeProgram : this->weightedCompositeProgram);
  }
  bgfx::setSampleMask();
}

bool
CoinBgfxBackend::encodeOverlayLayers(
  const std::vector<CoinBgfxDraw> & draws,
  bgfx::DynamicVertexBufferHandle vertices,
  bgfx::DynamicIndexBufferHandle indices,
  bgfx::FrameBufferHandle output,
  const std::vector<bgfx::TextureHandle> & textures,
  bgfx::ViewId & nextView, int width, int height,
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadowPlan,
  const std::vector<bgfx::FrameBufferHandle> & shadowMaps, bool beforeScene,
  bgfx::TextureHandle sampleDepth)
{
  const bool hasShadows = !shadowPlan.passes.empty();
  uint32_t sceneLayer = 0;
  for (const auto & draw : draws) if (draw.authoredScene) { sceneLayer = draw.renderLayer; break; }
  uint32_t encodedLayer = 0;
  for (const CoinBgfxDraw & layerDraw : draws) {
    if (layerDraw.renderLayer == 0 || layerDraw.renderLayer == encodedLayer ||
        (beforeScene ? layerDraw.renderLayer > sceneLayer : layerDraw.renderLayer <= sceneLayer)) continue;
    encodedLayer = layerDraw.renderLayer;
    const CoinBgfxDraw * barrier = nullptr;
    for (const CoinBgfxDraw & draw : draws) {
      if (draw.renderLayer == encodedLayer && draw.clearDepthBefore) {
        barrier = &draw;
        break;
      }
    }
    int32_t clipped[4];
    if (barrier && CoinBgfxLowering::clipViewport(barrier->viewport, width,
                                               height, clipped)) {
      const bgfx::ViewId clearView = nextView++;
      const int32_t top = height - clipped[1] - clipped[3];
      bgfx::setViewName(clearView, "overlay_depth_clear");
      bgfx::setViewMode(clearView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(clearView, static_cast<uint16_t>(clipped[0]),
                        static_cast<uint16_t>(top),
                        static_cast<uint16_t>(clipped[2]),
                        static_cast<uint16_t>(clipped[3]));
      bgfx::setViewFrameBuffer(clearView, output);
      bgfx::setViewClear(clearView, BGFX_CLEAR_DEPTH, 1.0f, 0);
      bgfx::setViewTransform(clearView, nullptr, nullptr);
      bgfx::touch(clearView);
    }
    // The authored scene has its own opaque/deferred passes after this barrier.
    if (layerDraw.isSceneLayer()) continue;
    // Overlays are immediate traversals: transparent depth-writing geometry
    // may intentionally occlude later opaque or textured primitives. A split
    // into opaque/transparent views would undo the stable per-layer ordering.
    const bgfx::ViewId overlayView = nextView++;
    bgfx::setViewName(overlayView, "overlay_immediate");
    bgfx::setViewMode(overlayView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(overlayView, 0, 0, static_cast<uint16_t>(width),
                      static_cast<uint16_t>(height));
    bgfx::setViewFrameBuffer(overlayView, output);
    bgfx::setViewClear(overlayView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(overlayView, nullptr, nullptr);
    bgfx::touch(overlayView);
    for (const CoinBgfxDraw & draw : draws) {
      if (draw.renderLayer != encodedLayer) continue;
      bgfx::setTransform(draw.mvp);
      bgfx::setVertexBuffer(0, vertices);
      bgfx::setIndexBuffer(indices, draw.firstIndex, draw.indexCount);
      bgfx::setState(drawState(draw) | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
      if (!setDrawScissor(draw, width, height)) continue;
      this->submitSurfaceDraw(overlayView, draw, textures, frame, shadowPlan, shadowMaps,
        width, height, sampleDepth);

    }
  }
  return true;
}

CoinRenderSubmitResult
CoinBgfxBackend::submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target)
{
  return this->submit(frame, target, CoinRenderFrameReuseDecision());
}

CoinRenderSubmitResult
CoinBgfxBackend::submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
                          const CoinRenderFrameReuseDecision & reuse)
{
  return this->submitInternal(frame, target, reuse, nullptr);
}

CoinRenderSubmitResult
CoinBgfxBackend::submitAsync(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
  CoinRenderReadbackTicket & ticket, const CoinRenderFrameReuseDecision & reuse)
{
  ticket = CoinRenderReadbackTicket{};
  return this->submitInternal(frame, target, reuse, &ticket);
}

void
CoinBgfxBackend::bindShadowReceiver(
  const CoinRenderFramePlan & frame, const CoinRenderShadowPlan & shadowPlan,
  const std::vector<bgfx::FrameBufferHandle> & shadowMaps,
  const CoinBgfxDraw & original, int targetHeight, unsigned declaredShadowSlots)
{
  // Every sampler declared by the active program needs its own typed binding,
  // including dynamically inactive shadow slots. Otherwise a fresh GL program
  // can leave a sampler2D at unit zero, aliasing peeling's sampler2DMS there and
  // invalidating the whole draw. Ordinary receivers declare 2/4/8 slots;
  // transparency receivers always declare eight. Never bind absent stages.
  const unsigned shadowSlots = declaredShadowSlots ? declaredShadowSlots :
    (shadowPlan.passes.size() > 4 ? 8u : shadowPlan.passes.size() > 2 ? 4u : 2u);
  for (unsigned slot = 0; !this->shadowArrayActive && slot < shadowSlots; ++slot)
    bgfx::setTexture(static_cast<uint8_t>(8 + slot), this->shadowSampler[slot], this->defaultTexture);
  CoinBgfxDraw shaded = original;
  shaded.lightCount[1] = 0.0f;
  float indices[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  const auto & state = frame.renderStates[original.renderStateSlot];
  const auto & flatMaterial = frame.materials[state.materialSlot];
  const float flatParams[4] = {flatMaterial.shininess,
    state.flatShadowPrimary ? 1.0f : 0.0f, 0.0f, 0.0f};
  bgfx::setUniform(this->shadowFlatDiffuseUniform, flatMaterial.diffuse);
  bgfx::setUniform(this->shadowFlatSpecularUniform, flatMaterial.specular);
  bgfx::setUniform(this->shadowFlatParamsUniform, flatParams);
  const uint32_t flags = coin_render_shadow_shading_flags(frame, state);
  const float quality[4] = {float((flags & COIN_RENDER_SHADOW_VERTEX_LIGHTING) != 0),
    float((flags & COIN_RENDER_SHADOW_ORDINARY_FRAGMENT) != 0),
    float((flags & COIN_RENDER_SHADOW_GROUP_LIGHTING) != 0), 0};
  bgfx::setUniform(this->shadowQualityUniform, quality);
  if (state.lightingSlot < frame.lightingStates.size()) {
    const auto & lights = frame.lightingStates[state.lightingSlot].lights;
    for (size_t i = 0; i < lights.size() && i < COIN_RENDER_MAX_LIGHTS; ++i)
      if (coin_render_shadow_suppresses_ordinary_light(frame, state, lights[i]))
        shaded.lightColorIntensity[i][3] = 0.0f;
  }
  const SbMatrix clipConversion(
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 0.5f, 1.0f);
  for (size_t slot = 0; slot < shadowPlan.passes.size(); ++slot) {
    const size_t passSlot = slot;
    const auto & pass = shadowPlan.passes[passSlot];
    if (state.shadowGroupSlot != pass.groupSlot) continue;
    const int32_t index = pass.lightingIndexByState[original.renderStateSlot];
    if (index < 0) continue;
    // Unshadowed shapes retain ordinary lighting; only receivers sample VSM.
    if ((state.shadowStyle & 2u) == 0) continue;
    indices[slot] = static_cast<float>(index);
    const SbMatrix observerView = state.rasterShadowAnchorValid &&
      state.cameraSlot < frame.cameras.size()
        ? frame.cameras[state.cameraSlot].viewMatrix : state.view;
    const SbMatrix viewToLight = observerView.inverse() * pass.view;
    // Receiver clip tests use [0,1] independently of the rasterizer's clip domain.
    // This matrix samples the map; caster rasterization keeps its native projection.
    const SbMatrix projection = pass.projectionCoin * clipConversion;
    const SbMatrix viewToClip = viewToLight * projection;
    const float params[4] = {pass.nearDistance, pass.vsmFarDistance,
                             pass.epsilon, pass.threshold};
    const float meta[4] = {
      frame.shadowLights[pass.lightSlot].type == CoinRenderLightType::SPOT ? 1.0f : 0.0f,
      pass.maxShadowDistance, pass.distanceFalloffCoefficient,
      bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f};
    bgfx::setUniform(this->shadowViewToLightUniform[slot], viewToLight.getValue());
    bgfx::setUniform(this->shadowViewToClipUniform[slot], viewToClip.getValue());
    bgfx::setUniform(this->shadowParamsUniform[slot], params);
    bgfx::setUniform(this->shadowMetaUniform[slot], meta);
    const float texel[4] = {1.0f / float(pass.mapSize), 1.0f / float(pass.mapSize),
      1.0f / float(this->shadowArraySize), float(slot)};
    bgfx::setUniform(this->shadowTexelUniform[slot], texel);
    if (!this->shadowArrayActive) bgfx::setTexture(static_cast<uint8_t>(8 + slot), this->shadowSampler[slot],
      bgfx::getTexture(shadowMaps[passSlot], 0));
  }
  if (this->shadowArrayActive) bgfx::setTexture(10,this->shadowArraySampler,this->shadowArrayTexture);
  bgfx::setUniform(this->shadowLightIndicesUniform, indices);
  bgfx::setUniform(this->shadowLightIndicesExtraUniform, indices + 4);
  this->bindDrawLighting(shaded, targetHeight);
}

// Admission precedes allocation, cache changes and producer submission.
static CoinRenderSubmitResult bgfxTextureProfile(const CoinRenderFramePlan & frame) {
  std::string bumpDiagnostic;
  if (!coin_render_shader_execution_profile(frame,bumpDiagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED,bumpDiagnostic};
  if(!CoinBgfxLowering::validateBump(frame,bumpDiagnostic))return {CoinRenderBackendStatus::UNSUPPORTED,bumpDiagnostic};
  const auto rasterAdmission = bgfxRasterImageProfile(frame);
  if (rasterAdmission.status != CoinRenderBackendStatus::SUCCESS) return rasterAdmission;
  std::string cubeDiagnostic;
  if (!CoinRenderCubeTextureCore::validate(frame, cubeDiagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, cubeDiagnostic};
  const auto * caps = sharedRuntime().references ? bgfx::getCaps() : nullptr;
  for (const auto & sampler : frame.samplers)
    if (sampler.maxAnisotropy != 1 && sampler.maxAnisotropy != 16)
      return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX exposes native maximum anisotropy only (option 16); factors 2/4/8 are unavailable"};
  if (!caps) return {}; // Selected-adapter admission follows prepare.
  const bool extendedBump = coin_render_uses_extended_bump(frame);
  if (extendedBump && (caps->limits.maxTextureSamplers < 11 ||
                       caps->limits.maxVertexAttributes < 15 || caps->limits.maxTextureLayers < 2))
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "Bump receiver requires fifteen vertex attributes, eleven texture stages and two array "
            "layers"};
  for(const auto & image:frame.bumpImages)if(image.width>caps->limits.maxTextureSize || image.height>caps->limits.maxTextureSize)
    return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX bump image exceeds selected GPU texture dimension limit"};
  if(caps->rendererType==bgfx::RendererType::OpenGL && !frame.textures.empty()) {
    // This GL connector changes the object's mip range. A mixed binding gets
    // an independent base-level object after lowering.
    std::vector<uint8_t> usage(frame.textures.size(),0);
    for(const auto & state:frame.renderStates)for(size_t unit=0;unit<COIN_RENDER_MAX_TEXTURE_UNITS;++unit) {
      const auto layer=coin_render_texture_unit(state,unit);
      if(!layer.enabled || layer.imageSlot>=usage.size() || layer.samplerSlot>=frame.samplers.size())continue;
      usage[layer.imageSlot]|=CoinRenderTextureSamplingCore::mipFilter(frame.samplers[layer.samplerSlot].filter)?2:1;
      if(usage[layer.imageSlot]==3 &&
          (frame.textures[layer.imageSlot].gpuToken != 0 ||
           frame.textures[layer.imageSlot].producerId != 0) &&
          !bgfx::isTextureValid(1, false, 1,
            coinBgfxTextureFormat(frame.textures[layer.imageSlot].format),
            coinBgfxTextureFlags(frame.textures[layer.imageSlot].format) | BGFX_TEXTURE_BLIT_DST))
        return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX OpenGL cannot create an independent base view for this direct texture format"};
    }
  }
  for (const auto & image : frame.textures) {
    const auto format = coinBgfxTextureFormat(image.format);
    if (!CoinRenderTextureFormatCore::valid(image.format) ||
        !(caps->formats[format] & (CoinRenderTextureFormatCore::srgb(image.format) ? BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB : BGFX_CAPS_FORMAT_TEXTURE_2D)) ||
        image.width > caps->limits.maxTextureSize || image.height > caps->limits.maxTextureSize ||
        !bgfx::isTextureValid(1, false, 1, format, coinBgfxTextureFlags(image.format)))
      return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX texture format or extent unavailable"};
  }
  return {};
}

template <typename Items, typename Deferred, typename Additive>
static uint64_t bgfxDeferredViewCount(const Items & items, bool object, uint32_t sourceViews,
                                     Deferred deferred, Additive additive) {
  if (object) return 1;
  uint64_t views = 0; bool open = false, previousAdd = false;
  for (const auto & item : items) {
    if (!deferred(item)) continue;
    const bool add = additive(item);
    if (!open || previousAdd != add) { views += add ? 1 : sourceViews; open = true; previousAdd = add; }
  }
  return views;
}

// Plan the whole submission before any clear/draw/dispatch. Each target owns
// an expandable span of isolated 16-view blocks, including final resolve/copies.
static bool bgfxCompositionViewBudget(const CoinRenderFramePlan & frame,
  const std::vector<CoinBgfxDraw> & draws, CoinBgfxTransparencyStrategy strategy,
  size_t shadowViews, bool direct, int width, int height,
  uint32_t outputViews, bool multisampled, std::string & diagnostic, uint32_t * requiredViews = nullptr)
{
  (void)direct;
  const bool images = std::any_of(frame.renderStates.begin(), frame.renderStates.end(),
    [](const CoinRenderRenderStateSnapshot & state) { return state.rasterTextureSourceSlot != UINT32_MAX; });
  const uint64_t fixedViews = (images ? 1 : 0) + shadowViews + (shadowViews && bgfxShadowMultitexture(frame) ? 1 : 0);
  uint64_t views = fixedViews;
  views += 1 + bgfxDeferredViewCount(draws, strategy == CoinBgfxTransparencyStrategy::OBJECT,
    strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS ? frame.transparency.layers + 1 : 2,
    [](const CoinBgfxDraw & draw) { return draw.isSceneLayer() && draw.blend && draw.deferred; },
    [](const CoinBgfxDraw & draw) { return draw.additive; });
  std::map<uint32_t, const CoinBgfxDraw *> layers;
  for (const auto & draw : draws) {
    if (draw.renderLayer) {
      auto inserted = layers.emplace(draw.renderLayer, nullptr);
      if (draw.clearDepthBefore && !inserted.first->second) inserted.first->second = &draw;
    }
  }

  for (const auto & layer : layers) {
    ++views; // Overlay draw view, or initial frame clear for a remapped authored scene.
    int32_t clipped[4];
    if (layer.second && CoinBgfxLowering::clipViewport(layer.second->viewport, width, height, clipped))
      ++views;
  }
  views += (multisampled ? 1 : 0);
  if (frame.numPasses > 1)
    views = fixedViews + (views - fixedViews + 1) * uint64_t(frame.numPasses) + 1;
  views += outputViews;
  const uint32_t maximumViews = bgfx::getCaps() ? bgfx::getCaps()->limits.maxViews : targetViewCount;
  if (requiredViews) *requiredViews = uint32_t(views);
  if (views > maximumViews) {
    diagnostic = "BGFX composition, clears, resolve and output exceed available view IDs";
    return false;
  }
  return true;
}

// RTT preflight runs before any producer publishes resources. Count the
// object MSAA composition without lowering geometry or resolving its textures.
static bool bgfxMsaaFrameViewBudget(const CoinRenderFramePlan & frame,
  int width, int height, uint32_t outputViews, uint32_t samples, bool direct,
  std::string & diagnostic, uint32_t * requiredViews = nullptr)
{
  (void)direct;
  const auto * targetCaps = bgfx::getCaps();
  if (width <= 0 || height <= 0 || (targetCaps &&
      (uint32_t(width) > targetCaps->limits.maxTextureSize || uint32_t(height) > targetCaps->limits.maxTextureSize))) {
    diagnostic = "BGFX graph target extent exceeds attachment capabilities";
    return false;
  }
  if (samples > 1 && uint64_t(width) * uint64_t(height) * (8ull * samples) > 128ull * 1024 * 1024) {
    diagnostic = "BGFX graph MSAA attachments exceed 128 MiB";
    return false;
  }
  std::vector<CoinRenderCompositionItem> order;
  if (!coin_render_composition_order(frame, order, diagnostic)) return false;
  const bool weighted = std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
    return item.blend && item.deferred && !item.additive &&
      item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT;
  });
  const bool peeling = std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
    return item.blend && item.deferred && !item.additive &&
      item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS;
  });
  if (weighted || peeling) {
    auto options = frame.transparency;
    if (!peeling) options.layers = 1;
    uint64_t required = 0;
    if (!coin_render_transparency_sample_budget(width, height, options, true, samples, required, diagnostic))
      return false;
  }
  std::map<uint32_t, const CoinRenderViewportSnapshot *> layers;
  for (const auto & item : order) {
    const auto & draw = frame.draws[item.drawIndex];
    if (!draw.renderLayer) continue;
    auto inserted = layers.emplace(draw.renderLayer, nullptr);
    if (draw.clearDepthBefore && !inserted.first->second)
      inserted.first->second = &frame.viewports[frame.renderStates[draw.renderStateSlot].viewportSlot];
  }
  CoinRenderShadowPlan shadows;
  if (!frame.shadowGroups.empty()) {
    if (!coin_render_plan_shadows(frame, shadows, diagnostic) ||
        !coin_render_shadow_object_profile(frame, shadows, shadows.passes.size(), diagnostic) ||
        !bgfxShadowBatchSupported(frame, shadows, diagnostic)) return false;
    const auto * caps = bgfx::getCaps();
    const uint32_t stages = bgfxShadowMultitexture(frame) ? 11 : (weighted || peeling) ? 16 : shadows.passes.size() > 4 ? 16 : shadows.passes.size() > 2 ? 12 : 10;
    if (caps && caps->limits.maxTextureSamplers < stages) {
      diagnostic = "BGFX MSAA shadow receiver has insufficient texture stages";
      return false;
    }
    for (const auto & pass : shadows.passes) {
      if (caps && (pass.mapSize > caps->limits.maxTextureSize ||
          !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, shadowTextureFlags))) {
        diagnostic = "BGFX MSAA cannot render the planned shadow maps";
        return false;
      }
    }
  }
  if (bgfxShadowMultitexture(frame) && !coin_render_shadow_array_budget(shadows,2,diagnostic))return false;
  if (bgfxShadowMultitexture(frame) && !shadows.passes.empty() && targetCaps) {
    uint32_t extent=1; for (const auto & pass:shadows.passes) extent=std::max(extent,pass.mapSize);
    const auto layers=uint16_t(std::max<size_t>(2,shadows.passes.size()));
    if (extent>targetCaps->limits.maxTextureSize || layers>targetCaps->limits.maxTextureLayers ||
        !bgfx::isTextureValid(1,false,layers,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_BLIT_DST)) {
      diagnostic="Shadow moment array extent/layers/format unavailable"; return false;
    }
  }
  // VSM maps remain single-sample; only the final scene is multisampled.
  const bool images = std::any_of(frame.renderStates.begin(), frame.renderStates.end(),
    [](const CoinRenderRenderStateSnapshot & state) { return state.rasterTextureSourceSlot != UINT32_MAX; });
  uint64_t views = (images ? 1 : 0) + shadows.passes.size() +
    (!shadows.passes.empty() && bgfxShadowMultitexture(frame) ? 1 : 0) +
    1 + bgfxDeferredViewCount(order, !weighted && !peeling,
      peeling ? frame.transparency.layers + 1 : 2,
      [&](const CoinRenderCompositionItem & item) { return frame.draws[item.drawIndex].isSceneLayer() && item.blend && item.deferred; },
      [](const CoinRenderCompositionItem & item) { return item.additive; }) +
    (samples > 1 ? 1 : 0) + outputViews;
  for (const auto & layer : layers) {
    ++views;
    if (layer.second) {
      const auto & v = *layer.second;
      const int32_t viewport[4] = {v.x, v.y, v.width, v.height};
      int32_t clipped[4];
      if (CoinBgfxLowering::clipViewport(viewport, width, height, clipped)) ++views;
    }
  }
  if (frame.numPasses > 1) {
    const uint64_t fixedViews = (images ? 1 : 0) + shadows.passes.size() +
      (!shadows.passes.empty() && bgfxShadowMultitexture(frame) ? 1 : 0);
    views = fixedViews + (views - fixedViews - outputViews + 1) *
      uint64_t(frame.numPasses) + 1 + outputViews;
  }
  const uint32_t maximumViews = targetCaps ? targetCaps->limits.maxViews : targetViewCount;
  if (requiredViews) *requiredViews = uint32_t(views);
  if (views > maximumViews) {
    diagnostic = "BGFX composition, clears, resolve and output exceed available view IDs";
    return false;
  }
  return true;
}

// BGFX/OpenGL sets the sampled mip range on the GL texture object. A base-only
// binding therefore needs its own object when another binding uses the mips.
static void bgfxSplitSamplerViews(CoinBgfxPlan & plan) {
  const auto * caps = bgfx::getCaps();
  if (!caps || caps->rendererType != bgfx::RendererType::OpenGL) return;
  const size_t originalCount = plan.textures.size();
  std::vector<uint8_t> usage(originalCount, 0);
  const auto mark = [&](const CoinBgfxDraw & draw) {
    if (draw.hasTexture && draw.textureSlot < originalCount)
      usage[draw.textureSlot] |= CoinRenderTextureSamplingCore::mipFilter(draw.filter) ? 2 : 1;
    for (const auto & layer : draw.extraTextures)
      if (layer.enabled && layer.slot < originalCount)
        usage[layer.slot] |= CoinRenderTextureSamplingCore::mipFilter(layer.filter) ? 2 : 1;
  };
  for (const auto & draw : plan.draws) mark(draw);
  for (const auto & draw : plan.shadowDraws) mark(draw);
  std::vector<size_t> baseSlots(originalCount, SIZE_MAX);
  for (size_t slot = 0; slot < originalCount; ++slot) {
    if (usage[slot] != 3) continue;
    const auto & source = plan.textures[slot];
    if(source.cubeFaceSize || source.volumeDepth)continue;
    CoinBgfxTexture base;
    base.format = source.format;
    base.width = source.width;
    base.height = source.height;
    if (source.gpuToken != 0) base.gpuCopySourceSlot = slot;
    else {
      const size_t baseBytes = CoinRenderTextureFormatCore::levelBytes(
        source.width, source.height, source.format);
      if (baseBytes > source.pixelsRgba.size()) continue; // Existing upload validation rejects it.
      base.pixelsRgba.assign(source.pixelsRgba.begin(),
        source.pixelsRgba.begin() + baseBytes);
    }
    baseSlots[slot] = plan.textures.size();
    plan.textures.push_back(std::move(base));
  }
  const auto redirect = [&](CoinBgfxDraw & draw) {
    if (draw.hasTexture && draw.textureSlot < originalCount &&
        !CoinRenderTextureSamplingCore::mipFilter(draw.filter) &&
        baseSlots[draw.textureSlot] != SIZE_MAX)
      draw.textureSlot = static_cast<uint32_t>(baseSlots[draw.textureSlot]);
    for (auto & layer : draw.extraTextures)
      if (layer.enabled && layer.slot < originalCount &&
          !CoinRenderTextureSamplingCore::mipFilter(layer.filter) &&
          baseSlots[layer.slot] != SIZE_MAX)
        layer.slot = static_cast<uint32_t>(baseSlots[layer.slot]);
  };
  for (auto & draw : plan.draws) redirect(draw);
  for (auto & draw : plan.shadowDraws) redirect(draw);
}

template<typename Value>
static uint64_t bgfxVectorBytes(const std::vector<Value> & values)
{
  return uint64_t(values.size())*sizeof(Value);
}

static bool bgfxBumpSnapshotFits(const CoinRenderFramePlan & frame)
{
  // Cache admission never limits rendering. Retain at most the last small
  // source payload; large scenes still reuse their image objects below.
  if(!frame.shadowGroups.empty() || !frame.shadowLights.empty() || !frame.rasterOccurrences.empty())return false;
  uint64_t bytes=sizeof(frame);
  bytes+=bgfxVectorBytes(frame.vertices)+bgfxVectorBytes(frame.indices)+bgfxVectorBytes(frame.materials)+
    bgfxVectorBytes(frame.bumpCoords)+bgfxVectorBytes(frame.bumpTangents)+bgfxVectorBytes(frame.bumpTransforms)+
    bgfxVectorBytes(frame.bumpImages)+bgfxVectorBytes(frame.lightingStates)+bgfxVectorBytes(frame.cameras)+
    bgfxVectorBytes(frame.viewports)+bgfxVectorBytes(frame.renderStates)+bgfxVectorBytes(frame.textures)+
    bgfxVectorBytes(frame.samplers)+bgfxVectorBytes(frame.draws);
  for(const auto & image:frame.bumpImages)bytes+=bgfxVectorBytes(image.rgb);
  for(const auto & image:frame.textures)bytes+=bgfxVectorBytes(image.pixelsRgba)+bgfxVectorBytes(image.mipmapsRgba);
  for(const auto & state:frame.lightingStates)bytes+=bgfxVectorBytes(state.lights);
  for(const auto & state:frame.renderStates)bytes+=bgfxVectorBytes(state.clipPlanesWorld);
  return bytes<=8u*1024u*1024u;
}

// Original image slots retain their order in the lowered plan. GL may append
// base-only views and bump images after them. Revisions/digests do not prove
// the identity of a transported source; compare every original image and mip.
static bool bgfxSameSourceTextures(const CoinRenderFramePlan & frame,
                                  const CoinBgfxPlan & plan)
{
  if (frame.textures.size() > plan.textures.size()) return false;
  for (size_t i = 0; i < frame.textures.size(); ++i) {
    const auto & source = frame.textures[i];
    const auto & cached = plan.textures[i];
    if (source.producerId || source.format != cached.format ||
        source.width != cached.width || source.height != cached.height ||
        source.cubeFaceSize != cached.cubeFaceSize ||
        source.cubeSourceComponents != cached.cubeSourceComponents ||
        source.cubeMipmapped != cached.cubeMipmapped ||
        source.volumeWidth != cached.volumeWidth || source.volumeHeight != cached.volumeHeight ||
        source.volumeDepth != cached.volumeDepth || source.volumeColumns != cached.volumeColumns ||
        source.volumeSourceComponents != cached.volumeSourceComponents ||
        source.volumeMipmapped != cached.volumeMipmapped || source.mipmapped != cached.mipmapped ||
        source.gpuToken != cached.gpuToken ||
        source.pixelsRgba.size() + source.mipmapsRgba.size() != cached.pixelsRgba.size() ||
        !std::equal(source.pixelsRgba.begin(),source.pixelsRgba.end(),cached.pixelsRgba.begin()) ||
        !std::equal(source.mipmapsRgba.begin(),source.mipmapsRgba.end(),
                    cached.pixelsRgba.begin()+source.pixelsRgba.size()))
      return false;
  }
  return true;
}

static bool bgfxSameBumpTextures(const std::vector<CoinBgfxTexture> & a,
                                 const std::vector<CoinBgfxTexture> & b)
{
  if(a.size()!=b.size())return false;
  for(size_t i=0;i<a.size();++i) {
    const auto & x=a[i];const auto & y=b[i];
    if(x.width!=y.width || x.height!=y.height || x.format!=y.format || x.mipmapped!=y.mipmapped ||
       x.gpuToken!=y.gpuToken || x.gpuCopySourceSlot!=y.gpuCopySourceSlot ||
       x.cubeFaceSize!=y.cubeFaceSize || x.cubeSourceComponents!=y.cubeSourceComponents || x.cubeMipmapped!=y.cubeMipmapped ||
       x.volumeWidth!=y.volumeWidth || x.volumeHeight!=y.volumeHeight || x.volumeDepth!=y.volumeDepth ||
       x.volumeColumns!=y.volumeColumns || x.volumeSourceComponents!=y.volumeSourceComponents ||
       x.volumeMipmapped!=y.volumeMipmapped || x.pixelsRgba!=y.pixelsRgba)return false;
  }
  return true;
}

CoinRenderSubmitResult
CoinBgfxBackend::submitInternal(const CoinRenderFramePlan & frame, CoinRenderTargetP & target,
  const CoinRenderFrameReuseDecision & reuse, CoinRenderReadbackTicket * outTicket)
{
  if (!this->initialized || this->status != CoinRenderBackendStatus::SUCCESS || !this->onApiThread()) {
    this->lastError = "BGFX submission requires a prepared backend on its API thread";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY, this->lastError);
  }
  if (!CoinRenderCubeTextureCore::validate(frame, this->lastError) ||
      !CoinRenderTextureSamplingCore::validVolumeFrame(frame, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  PortablePreflightRelease releasePreflight{this};
  for(const auto & image:frame.textures)if(image.cubeFaceSize && image.gpuToken) {
    if(!CoinRenderCubeTextureCore::valid(image))return {CoinRenderBackendStatus::UNSUPPORTED,"Invalid GPU cube atlas metadata"};
    const auto found=std::find_if(this->directTextures.begin(),this->directTextures.end(),[&image](const DirectTextureResource & resource) {
      return resource.token==image.gpuToken && resource.inUse && resource.width==int(image.width) &&
        resource.height==int(image.height) && resource.cubeFaceSize==image.cubeFaceSize &&
        resource.cubeMipmapped==image.cubeMipmapped && resource.format==image.format;
    });
    if(found==this->directTextures.end())return {CoinRenderBackendStatus::UNSUPPORTED,"GPU cube token belongs to another owner/generation or has mismatched metadata"};
  }
  const CoinRenderBackendStatus initialRuntimeStatus = this->checkRuntimeFailure("BGFX shared renderer failed before submission");
  if (initialRuntimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(initialRuntimeStatus, this->lastError);
  if(injectOutOfMemoryAfterSubmits()) {
    this->lastError="Injected BGFX out of memory after successful logical submissions";
    return {CoinRenderBackendStatus::OUT_OF_MEMORY,this->lastError};
  }
  if ((target.kind == CoinRenderTargetP::KIND_WINDOW) != this->presentToWindow ||
      target.directTextureOutput) {
    this->lastError = "BGFX supports native window presentation or offscreen readback";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  const auto multipassAdmission = this->preflightMultipass(frame,target.size);
  if (multipassAdmission.status != CoinRenderBackendStatus::SUCCESS) return multipassAdmission;
  const bool releaseMultipassWindow = this->presentToWindow && this->multipassSceneRequired && frame.numPasses == 1;
  this->multipassSceneRequired = frame.numPasses > 1;
  const auto textureAdmission = bgfxTextureProfile(frame);
  if (textureAdmission.status != CoinRenderBackendStatus::SUCCESS) return textureAdmission;
  const auto msaaAdmission = this->msaaProfile(frame);
  if (msaaAdmission.status != CoinRenderBackendStatus::SUCCESS) return msaaAdmission;
  if (releaseMultipassWindow) {
    const bgfx::FrameBufferHandle nativeOutput = this->frameBuffer;
    this->frameBuffer = BGFX_INVALID_HANDLE;
    this->destroyFrameBuffers();
    this->frameBuffer = nativeOutput;
  }
  if (std::any_of(frame.renderStates.begin(), frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot & state) {
        return state.rasterTextureSourceSlot != UINT32_MAX;
      }) && !this->prepareRasterImageResources())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  const bool hasShadows = !frame.shadowGroups.empty();
  CoinRenderShadowPlan shadowPlan;
  if (hasShadows) {
    if (outTicket || target.directTextureOutput) {
      this->lastError = "BGFX opaque shadows require synchronous output";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    if (!coin_render_plan_shadows(frame, shadowPlan, this->lastError) ||
        !coin_render_shadow_object_profile(frame, shadowPlan,
          shadowPlan.passes.size(), this->lastError))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    if (!bgfxShadowBatchSupported(frame, shadowPlan, this->lastError))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    const bgfx::Caps * caps = bgfx::getCaps();
    if (caps && caps->limits.maxTextureSamplers < (bgfxShadowMultitexture(frame)?11:shadowPlan.passes.size() > 4 ? 16 : shadowPlan.passes.size() > 2 ? 12 : 10)) {
      this->lastError = "BGFX shadow receiver needs one texture stage per shadow pass";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    for (const auto & pass : shadowPlan.passes) {
      if (pass.mapSize > caps->limits.maxTextureSize ||
          !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, shadowTextureFlags)) {
        this->lastError = "BGFX cannot render RGBA32F/D32F shadow maps at the planned size";
        return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
      }
    }
    if (bgfxShadowMultitexture(frame) && !coin_render_shadow_array_budget(shadowPlan,2,this->lastError))
      return {CoinRenderBackendStatus::UNSUPPORTED,this->lastError};
    if (!this->prepareShadowPrograms(shadowPlan.passes.size(),
                                     !bgfxShadowMultitexture(frame) && this->sampleCount == 1 &&
              bgfxFrameProgramNeeds(frame).ordinary))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  }
  if (outTicket) {
    auto & runtime = sharedRuntime();
    std::lock_guard<std::mutex> guard(runtime.mutex);
    uint64_t pendingBytes = 0;
    for (const auto & entry : asyncEntries())
      pendingBytes += entry.second->ticket.colorBytes + entry.second->ticket.depthBytes;
    const uint64_t requested = uint64_t(target.size[0]) * target.size[1] * (target.depthReadbackEnabled ? 8 : 4);
    if (this->presentToWindow ||
        !coin_render_readback_admitted(asyncEntries().size(), pendingBytes, requested))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY,
        "BGFX async readback requires offscreen output, at most sixteen tickets and 128 MiB of pending payloads");
  }
  if (!outTicket && this->readbackDepthEnabled != target.depthReadbackEnabled) {
    for (auto & slot : this->readbackSlots) {
      if (slot.pending) {
        uint32_t completed = bgfx::frame();
        while (static_cast<int32_t>(completed - slot.readyFrame) < 0) completed = bgfx::frame();
      }
      slot.pending = false;
    }
    this->lastPublishedReadback.clear(); this->lastPublishedDepth.clear();
    this->lastPublishedSequence = 0;
    this->readbackDepthEnabled = target.depthReadbackEnabled;
  }
  typedef std::chrono::steady_clock Clock;
  const Clock::time_point begin = Clock::now();
  const bool tracePhases = CoinRenderDiagnosticShell::phaseTracingEnabled();
  const char * gpuTimestampFlag = std::getenv("COIN_WGPU_GPU_TIMESTAMPS");
  const bool traceGpu = tracePhases && gpuTimestampFlag != nullptr &&
                        std::strcmp(gpuTimestampFlag, "1") == 0;
  if (traceGpu) bgfx::setDebug(BGFX_DEBUG_PROFILER);
  const bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;
  const bgfx::Caps * caps = bgfx::getCaps();
  const char * disableInstancing = std::getenv("COIN_BGFX_DISABLE_INSTANCING");
  // Recent BGFX versions expose this through maxInstanceData; older versions
  // additionally require the explicit capability bit.
#ifdef BGFX_CAPS_INSTANCING
  const bool instanceCaps = (caps->supported & BGFX_CAPS_INSTANCING) != 0;
#else
  const bool instanceCaps = true;
#endif
  const bool instancingEnabled = frame.numPasses == 1 && !hasShadows && this->drawBatchingEnabled &&
    !(disableInstancing && std::strcmp(disableInstancing, "1") == 0) &&
    instanceCaps &&
    caps->limits.maxInstanceData >= 10 && caps->limits.maxVertexAttributes >= 12;
  const bool cacheDimensionsMatch =
    this->cachedPassCount == frame.numPasses && this->cachedPassBudget == frame.multipassBufferBudget &&
    target.size[0] == this->cachedWidth &&
    target.size[1] == this->cachedHeight &&
    homogeneousDepth == this->cachedHomogeneousDepth;
  const bool activeBump=std::any_of(frame.bumpCoords.begin(),frame.bumpCoords.end(),[](const CoinRenderBumpCoordOverride & b){return b.imageSlot!=0;});
  const bool cachedBump=std::any_of(this->cachedPlan.draws.begin(),this->cachedPlan.draws.end(),[](const CoinBgfxDraw & draw){return draw.bumpSlot!=0;});
  const bool bumpPayloadHit=activeBump && cachedBump && this->bumpReuseFrame &&
    frame.hasSamePayload(*this->bumpReuseFrame) && bgfx::isValid(this->cachedVertexBuffer) &&
    bgfx::isValid(this->cachedIndexBuffer);
  const bool portableFrame = !frame.portableShaders.empty() || std::any_of(this->cachedPlan.draws.begin(), this->cachedPlan.draws.end(),
    [](const CoinBgfxDraw &draw) { return draw.portableShaderSlot != 0; });
  const bool sourceTexturesMatch = bgfxSameSourceTextures(frame, this->cachedPlan);
  // Public Wiring supplies a Core payload comparison. Raw private ingress
  // with only a repeated revision must repack textured vertex/sampler state.
  const bool texturedReuse = bumpPayloadHit ||
    (frame.textures.empty() && this->cachedPlan.textures.empty()) ||
    (reuse.kind == CoinRenderFrameReuseKind::REUSE && reuse.baseRevision == this->cachedRevision);
  const bool cacheHit = frame.numPasses == 1 && !portableFrame && !hasShadows && cacheDimensionsMatch &&
    sourceTexturesMatch && texturedReuse &&
    (bumpPayloadHit || (!activeBump && !cachedBump && frame.revision!=0 && frame.revision==this->cachedRevision)) &&
    (!this->cachedPlan.usesInstancing ||
      (instancingEnabled && bgfx::isValid(this->cachedInstanceBuffer)));
  // Invalidate before any changed GPU payload can displace the old anchor.
  if(!cacheHit)this->bumpReuseFrame.reset();
  bool bumpCacheCandidate=false;
  const auto rememberBump=[&]() {
    if(!activeBump || !bumpCacheCandidate) {this->bumpReuseFrame.reset();return;}
    this->cachedRevision=frame.revision;
    if(this->bumpReuseFrame || !bgfxBumpSnapshotFits(frame))return;
    try { this->bumpReuseFrame.reset(new CoinRenderFramePlan(frame)); }
    catch(const std::bad_alloc &) { this->bumpReuseFrame.reset(); }
  };
  const char * disableInstancedCameraPatch = std::getenv("COIN_BGFX_DISABLE_INSTANCED_CAMERA_PATCH");
  const bool instancedCameraPatchEnabled = instancingEnabled &&
    !(disableInstancedCameraPatch && std::strcmp(disableInstancedCameraPatch, "1") == 0) &&
    bgfx::isValid(this->cachedInstanceBuffer);
  const bool cameraPatchEligible = frame.numPasses == 1 &&
    !portableFrame && !activeBump && !cachedBump && !hasShadows && this->cameraPatchEnabled &&
    sourceTexturesMatch &&
    (!this->cachedPlan.usesInstancing || instancedCameraPatchEnabled) &&
    reuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH &&
    reuse.baseRevision != 0 && reuse.baseRevision == this->cachedRevision &&
    frame.revision != 0 && frame.revision != this->cachedRevision &&
    cacheDimensionsMatch &&
    (this->cachedPlan.draws.empty() ||
      (bgfx::isValid(this->cachedVertexBuffer) &&
       bgfx::isValid(this->cachedIndexBuffer)));
  std::vector<CoinBgfxDraw> cameraDraws;
  std::vector<CoinBgfxInstance> cameraInstances;
  const bool cameraPatchUsed = cameraPatchEligible &&
    CoinBgfxLowering::patchCamera(frame, target.size[0], target.size[1],
                                homogeneousDepth, this->cachedPlan,
                                cameraDraws, this->lastError, &cameraInstances);
  if (tracePhases && cameraPatchEligible && this->cachedPlan.usesInstancing && !cameraPatchUsed)
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_instanced_camera_patch_declined reason=%s\n", this->lastError.c_str());
  CoinBgfxPlan freshPlan;
  const CoinBgfxPlan * plan = &this->cachedPlan;
  if (!cacheHit && !cameraPatchUsed) {
    const char * disableCompact = std::getenv("COIN_BGFX_DISABLE_COMPACT_VERTICES");
    const bool compact = bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL &&
      !(disableCompact && std::strcmp(disableCompact, "1") == 0);
    const bool instanced = !activeBump && instancingEnabled && CoinBgfxLowering::lowerInstanced(
      frame, target.size[0], target.size[1], homogeneousDepth, freshPlan,
      this->lastError, target.submissionPreflight(frame),
      // Match the general path's composed model*view before vertex evaluation.
      // Split model/view transforms move boundary coverage on OpenGL/NVIDIA
      // and Vulkan/AMD. Keep instancing, with instance-buffer camera patches.
      false);
    if (!instanced && CoinRenderDiagnosticShell::phaseTracingEnabled())
      std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_instancing_declined enabled=%d max_instance_data=%u max_vertex_attributes=%u reason=%s\n",
        instancingEnabled ? 1 : 0, static_cast<unsigned int>(caps->limits.maxInstanceData),
        static_cast<unsigned int>(caps->limits.maxVertexAttributes), this->lastError.c_str());
    if (!instanced && !CoinBgfxLowering::lower(frame, target.size[0], target.size[1],
                              homogeneousDepth, freshPlan, this->lastError, hasShadows,
                              this->drawBatchingEnabled && frame.numPasses == 1, target.submissionPreflight(frame), compact)) {
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    bgfxSplitSamplerViews(freshPlan);
    plan = &freshPlan;
  }
  std::vector<CoinBgfxVertexRange> materialRanges;
  const bool materialPatchEligible = frame.numPasses == 1 && !portableFrame && !activeBump && !cachedBump && !cacheHit && !cameraPatchUsed &&
    !this->cachedPlan.usesInstancing && !freshPlan.usesInstancing &&
    reuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD &&
    reuse.baseRevision != 0 && reuse.baseRevision == this->cachedRevision &&
    cacheDimensionsMatch && bgfx::isValid(this->cachedVertexBuffer) &&
    bgfx::isValid(this->cachedIndexBuffer);
  const bool materialPatchUsed = materialPatchEligible &&
    CoinBgfxLowering::materialPatchRanges(this->cachedPlan, freshPlan,
                                        materialRanges);
  const std::vector<CoinBgfxDraw> & strategyDraws =
    cameraPatchUsed ? cameraDraws : plan->draws;
  CoinBgfxTransparencyStrategy selectedStrategy;
  if (!CoinBgfxLowering::selectTransparencyStrategy(strategyDraws,
        this->transparencyMode, this->weightedOitSupported,
        this->sortedLayersSupported, selectedStrategy, this->lastError)) {
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  const bool rebuildSceneTarget = selectedStrategy != this->activeTransparencyStrategy ||
    (selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
     this->peelPassCount != frame.transparency.layers) ||
    this->width != target.size[0] || this->height != target.size[1];
  const bool presentSceneTarget = this->presentToWindow && frame.numPasses > 1 ? true : rebuildSceneTarget ?
    (this->presentToWindow &&
     (bgfx::isValid(this->frameBuffer) ||
      (this->sampleCount > 1 && caps->rendererType == bgfx::RendererType::OpenGL)) &&
     (caps->rendererType == bgfx::RendererType::OpenGL ||
      selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS)) :
    bgfx::isValid(this->windowSceneFrameBuffer);
  const uint32_t outputViews = this->presentToWindow ? (presentSceneTarget ? 1 : 0) :
    (target.depthReadbackEnabled ? 2 : 1);
  uint32_t requiredViews = 0;
  if (!bgfxCompositionViewBudget(frame, strategyDraws, selectedStrategy,
        shadowPlan.passes.size(), false, target.size[0], target.size[1],
        outputViews, this->sampleCount > 1, this->lastError, &requiredViews) || !this->reserveViews(requiredViews))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (frame.numPasses > 1 && !this->prepareMultipassPrograms())
    return {CoinRenderBackendStatus::BACKEND_ERROR,this->lastError};
  if (!this->prepareTransparencyPrograms(selectedStrategy, bgfxNeedsFixedSurfacePrograms(strategyDraws)))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (!this->prepareBaseProgram(strategyDraws, hasShadows, selectedStrategy, plan->usesInstancing,
                                this->presentToWindow && frame.numPasses == 1, coinBgfxTextureFormat(frame.outputColorFormat)))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (!this->preparePortablePrograms(frame, strategyDraws, hasShadows, selectedStrategy,
        this->presentToWindow && frame.numPasses == 1, coinBgfxTextureFormat(frame.outputColorFormat)))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (hasShadows && !bgfxShadowMultitexture(frame) && selectedStrategy != CoinBgfxTransparencyStrategy::OBJECT &&
      !this->prepareShadowTransparencyPrograms())
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  CoinRenderTransparencyOptions allocationOptions = frame.transparency;
  if (selectedStrategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT)
    allocationOptions.layers = 1;
  uint64_t requiredBytes = 0;
  if (!coin_render_transparency_sample_budget(target.size[0], target.size[1], allocationOptions,
                                       selectedStrategy != CoinBgfxTransparencyStrategy::OBJECT,
                                       this->sampleCount, requiredBytes, this->lastError))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  if (selectedStrategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT && this->sampleCount > 1 &&
      !bgfxWeightedSamples(this->sampleCount, this->layout, this->viewBase, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS && this->sampleCount > 1 &&
      !bgfxPeelingSamples(this->sampleCount, this->layout, this->viewBase, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (selectedStrategy != this->activeTransparencyStrategy ||
      (selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
       this->peelPassCount != frame.transparency.layers)) {
    // Keep the native surface when opaque-only frames choose OBJECT or later
    // frames require OIT/peeling. BGFX creates resources before retiring old
    // ones: recreating a swapchain on the same X window here would race its
    // old EGL surface and fail with EGL_BAD_ALLOC.
    const bgfx::FrameBufferHandle windowOutput = this->frameBuffer;
    if (this->presentToWindow) this->frameBuffer = BGFX_INVALID_HANDLE;
    this->destroyFrameBuffers();
    if (this->presentToWindow) this->frameBuffer = windowOutput;
    this->activeTransparencyStrategy = selectedStrategy;
    this->peelPassCount = frame.transparency.layers;
  }
  const Clock::time_point lowered = Clock::now();
  if (!this->resize(target.size[0], target.size[1])) {
    const CoinRenderBackendStatus resizeStatus = this->status == CoinRenderBackendStatus::SUCCESS ?
      CoinRenderBackendStatus::OUT_OF_MEMORY : this->status;
    return CoinRenderSubmitResult(resizeStatus, this->lastError);
  }
  if (!this->presentToWindow && target.depthReadbackEnabled && !this->prepareDepthReadbackResources(!outTicket))
    return CoinRenderSubmitResult(this->status, this->lastError);
  const auto msaaPreparation = this->prepareMsaa(this->width, this->height);
  if (msaaPreparation.status != CoinRenderBackendStatus::SUCCESS) return msaaPreparation;
  if (this->presentToWindow && caps->rendererType == bgfx::RendererType::OpenGL) {
    // Prepare the image program before the first dispatch, alongside all
    // surface/shadow/compositor programs prepared above. This boundary must
    // precede geometry, texture-copy, clear and image-prepass encoding.
    const bool inheritedImages = std::any_of(frame.renderStates.begin(), frame.renderStates.end(),
      [](const CoinRenderRenderStateSnapshot & state) {
        return state.rasterTextureSourceSlot != UINT32_MAX;
      });
    if (inheritedImages && !bgfx::isValid(this->rasterImageProgram))
      this->rasterImageProgram = createRasterImageProgram();
    if (inheritedImages && !bgfx::isValid(this->rasterImageProgram))
      return {CoinRenderBackendStatus::BACKEND_ERROR, "BGFX image prepass program allocation failed"};
    auto & runtime = sharedRuntime();
    if (runtime.programCommandsMaterialized != runtime.programCommandsQueued) {
      // Execute resource commands without presenting or encoding a scene.
      // The final frame() below waits for this resource frame to finish before
      // it queues the one scene submission. Thus cold shader compilation is
      // charged to apply(), rather than continuing after SUCCESS and delaying
      // the first physical frame (or timing out a requested screenshot).
      bgfx::frame(BGFX_FRAME_FLUSH);
      runtime.programCommandsMaterialized = runtime.programCommandsQueued;
      const auto resourceStatus = this->checkRuntimeFailure("BGFX program preparation failed");
      if (resourceStatus != CoinRenderBackendStatus::SUCCESS)
        return {resourceStatus, this->lastError};
      if (tracePhases)
        std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_program_resource_frame programs=%llu scene_draws=0\n",
          static_cast<unsigned long long>(runtime.programCommandsMaterialized));
    }
  }
  if (plan->vertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(CoinBgfxVertex) ||
      plan->packedVertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(CoinBgfxVertexPrefix) ||
      plan->instancedVertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(CoinBgfxInstancedVertex) ||
      plan->instances.size() > std::numeric_limits<uint32_t>::max() / sizeof(CoinBgfxInstance) ||
      plan->indices.size() > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t)) {
    this->lastError = "BGFX geometry exceeds buffer size limits";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
  }
  bgfx::DynamicVertexBufferHandle vb = this->cachedVertexBuffer;
  bgfx::DynamicIndexBufferHandle ib = this->cachedIndexBuffer;
  bgfx::DynamicVertexBufferHandle instanceBuffer = this->cachedInstanceBuffer;
  const bool bumpTextureReuse=activeBump && cachedBump &&
    this->cachedTextures.size()==plan->textures.size() &&
    std::all_of(this->cachedTextures.begin(),this->cachedTextures.end(),[](bgfx::TextureHandle t){return bgfx::isValid(t);}) &&
    bgfxSameBumpTextures(this->cachedPlan.textures,plan->textures);
  bool retained = cacheHit || cameraPatchUsed || materialPatchUsed;
  bool geometryBufferReused = retained;
  uint32_t materialPatchVertices = 0;
  if (materialPatchUsed) {
    for (const CoinBgfxVertexRange & range : materialRanges) {
      materialPatchVertices += range.count;
      bgfx::update(vb, range.first, bgfx::copy(
        freshPlan.vertices.data() + range.first,
        range.count * static_cast<uint32_t>(sizeof(CoinBgfxVertex))));
    }
    this->cachedPlan = std::move(freshPlan);
    this->cachedRevision = frame.revision;
    this->cachedPassCount = frame.numPasses;
    this->cachedPassBudget = frame.multipassBufferBudget;
    this->cachedWidth = target.size[0];
    this->cachedHeight = target.size[1];
    this->cachedHomogeneousDepth = homogeneousDepth;
    plan = &this->cachedPlan;
  }
  if (cameraPatchUsed && !cameraInstances.empty())
    bgfx::update(instanceBuffer, 0, bgfx::copy(cameraInstances.data(),
      static_cast<uint32_t>(cameraInstances.size() * sizeof(CoinBgfxInstance))));
  if (!retained) {
    if (!plan->draws.empty()) {
      const bool vertexPoolHit = bgfx::isValid(vb) &&
        this->cachedCompactVertices == plan->usesCompactVertices &&
        this->cachedInstancedVertices == plan->usesInstancing &&
        this->cachedVertexCapacity >= plan->vertexCount();
      const bool indexPoolHit = bgfx::isValid(ib) &&
        this->cachedIndexCapacity >= plan->indices.size();
      const bool instancePoolHit = !plan->usesInstancing ||
        (bgfx::isValid(instanceBuffer) && this->cachedInstanceCapacity >= plan->instances.size());
      geometryBufferReused = vertexPoolHit && indexPoolHit && instancePoolHit;
      if (!vertexPoolHit) {
        if (bgfx::isValid(vb)) bgfx::destroy(vb);
        this->cachedVertexCapacity = pooledCapacity(plan->vertexCount());
        vb = bgfx::createDynamicVertexBuffer(this->cachedVertexCapacity,
          plan->usesInstancing ? this->instancedLayout :
            plan->usesCompactVertices ? this->compactLayout : this->layout);
        this->cachedVertexBuffer = vb;
        this->cachedCompactVertices = plan->usesCompactVertices;
        this->cachedInstancedVertices = plan->usesInstancing;
      }
      if (!indexPoolHit) {
        if (bgfx::isValid(ib)) bgfx::destroy(ib);
        this->cachedIndexCapacity = pooledCapacity(plan->indices.size());
        ib = bgfx::createDynamicIndexBuffer(this->cachedIndexCapacity,
                                             BGFX_BUFFER_INDEX32);
        this->cachedIndexBuffer = ib;
      }
      if (!instancePoolHit) {
        if (bgfx::isValid(instanceBuffer)) bgfx::destroy(instanceBuffer);
        this->cachedInstanceCapacity = pooledCapacity(plan->instances.size());
        instanceBuffer = bgfx::createDynamicVertexBuffer(this->cachedInstanceCapacity, this->instanceLayout);
        this->cachedInstanceBuffer = instanceBuffer;
      }
      if (!bgfx::isValid(vb) || !bgfx::isValid(ib) ||
          (plan->usesInstancing && !bgfx::isValid(instanceBuffer))) {
        this->lastError = "BGFX geometry upload failed";
        return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
      }
      bgfx::update(vb, 0, uploadVertices(freshPlan));
      bgfx::update(ib, 0, bgfx::copy(plan->indices.data(),
        static_cast<uint32_t>(plan->indices.size() * sizeof(uint32_t))));
      if (plan->usesInstancing)
        bgfx::update(instanceBuffer, 0, bgfx::copy(plan->instances.data(),
          static_cast<uint32_t>(plan->instances.size() * sizeof(CoinBgfxInstance))));
    }
    if(!bumpTextureReuse) {
      for (bgfx::TextureHandle texture : this->cachedTextures)
        if (bgfx::isValid(texture)) bgfx::destroy(texture);
      this->cachedTextures.clear();
    }
    this->cachedRevision = 0;
    if ((frame.revision != 0 || activeBump) && CoinBgfxLowering::retainForReuse(freshPlan,
          32u * 1024u * 1024u, 128u * 1024u * 1024u, true)) {
      this->cachedPlan = std::move(freshPlan);
      this->cachedRevision = frame.revision;
      this->cachedPassCount = frame.numPasses;
    this->cachedPassBudget = frame.multipassBufferBudget;
    this->cachedWidth = target.size[0];
      this->cachedHeight = target.size[1];
      this->cachedHomogeneousDepth = homogeneousDepth;
      plan = &this->cachedPlan;
      retained = true;
    }
  }

  bumpCacheCandidate=activeBump && retained && plan==&this->cachedPlan;
  std::vector<bgfx::TextureHandle> textures(
    plan->textures.size(), BGFX_INVALID_HANDLE);
  bool hasDirectTextures = false;
  for (const CoinBgfxTexture & texture : plan->textures)
    if (texture.gpuToken != 0) hasDirectTextures = true;
  std::vector<bool> textureOwned(plan->textures.size(), false);
  bool textureCacheHit = !hasDirectTextures &&
    (bumpTextureReuse || cacheHit || cameraPatchUsed || materialPatchUsed) &&
    this->cachedTextures.size() == plan->textures.size();
  for (bgfx::TextureHandle texture : this->cachedTextures) {
    if (!bgfx::isValid(texture)) textureCacheHit = false;
  }
  if (textureCacheHit) textures = this->cachedTextures;
  for (size_t textureIndex = 0;
       !textureCacheHit && textureIndex < plan->textures.size(); ++textureIndex) {
    const CoinBgfxTexture & texture = plan->textures[textureIndex];
    if (texture.gpuCopySourceSlot != SIZE_MAX) {
      textures[textureIndex] = bgfx::createTexture2D(
        static_cast<uint16_t>(texture.width), static_cast<uint16_t>(texture.height),
        false, 1, coinBgfxTextureFormat(texture.format),
        coinBgfxTextureFlags(texture.format) | BGFX_TEXTURE_BLIT_DST);
      if (!bgfx::isValid(textures[textureIndex])) {
        this->lastError = "BGFX direct texture base-view allocation failed";
        for (size_t i = 0; i < textures.size(); ++i)
          if (textureOwned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
        return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
      }
      textureOwned[textureIndex] = true;
      continue;
    }
    if (texture.gpuToken != 0) {
      for (const DirectTextureResource & resource : this->directTextures) {
        if (resource.token == texture.gpuToken && resource.format == texture.format &&
            resource.cubeFaceSize==texture.cubeFaceSize && resource.cubeMipmapped==texture.cubeMipmapped &&
            (!texture.mipmapped || resource.mipmapped) && resource.width == int(texture.width) && resource.height == int(texture.height)) {
          textures[textureIndex] = bgfx::getTexture(resource.frameBuffer, 0);
          break;
        }
      }
      if (!bgfx::isValid(textures[textureIndex])) {
        this->lastError = "BGFX direct texture token is missing or expired";
        return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
      }
      continue;
    }
    if (texture.width == 0 || texture.height == 0 ||
        texture.width > UINT16_MAX || texture.height > UINT16_MAX ||
        texture.pixelsRgba.size() !=
          CoinRenderTextureFormatCore::levelBytes(texture.width,texture.height,texture.format) +
          (texture.mipmapped ? CoinRenderTextureSamplingCore::mipBytes(texture.width,texture.height,texture.format) : 0)) {
      this->lastError = "BGFX texture dimensions or RGBA payload are invalid";
      for (size_t i = 0; i < textures.size(); ++i)
        if (textureOwned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    textures[textureIndex] = bgfx::createTexture2D(
      static_cast<uint16_t>(texture.width),
      static_cast<uint16_t>(texture.height), texture.mipmapped, 1,
      coinBgfxTextureFormat(texture.format), coinBgfxTextureFlags(texture.format),
      bgfx::copy(texture.pixelsRgba.data(),
                 static_cast<uint32_t>(texture.pixelsRgba.size())));
    if (!bgfx::isValid(textures[textureIndex])) {
      this->lastError = "BGFX texture upload failed";
      for (size_t i = 0; i < textures.size(); ++i)
        if (textureOwned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
    }
    textureOwned[textureIndex] = true;
  }
  if(this->portableSampling) for(size_t i=0;i<textures.size();++i)
    this->samplingBoundTextureSizes[textures[i].idx]={plan->textures[i].width,plan->textures[i].height};
  if (!textureCacheHit) for (size_t i = 0; i < plan->textures.size(); ++i) {
    const size_t sourceSlot = plan->textures[i].gpuCopySourceSlot;
    if (sourceSlot == SIZE_MAX) continue;
    bgfx::TextureRegion destination, source;
    destination.handle = textures[i];
    source.handle = textures[sourceSlot];
    bgfx::blit(this->viewBase, destination, source);
  }
  bool texturesRetained = textureCacheHit;
  if (!textureCacheHit && retained && !hasDirectTextures) {
    this->cachedTextures = textures;
    texturesRetained = true;
  }
  const auto destroyTextures = [&]() {
    if (texturesRetained) return;
    for (size_t i = 0; i < textures.size(); ++i)
      if (textureOwned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
  };

  if (!this->prepareShadowArray(shadowPlan,bgfxShadowMultitexture(frame))) {
    destroyTextures(); return {CoinRenderBackendStatus::UNSUPPORTED,this->lastError};
  }
  struct RasterImageOwner {
    std::vector<bgfx::TextureHandle> textures;
    ~RasterImageOwner() { for (auto handle : textures) if (bgfx::isValid(handle)) bgfx::destroy(handle); }
  } rasterImages;
  const auto rasterResult = this->encodeRasterImages(frame, textures, rasterImages.textures);
  if (rasterResult.status != CoinRenderBackendStatus::SUCCESS) { destroyTextures(); return rasterResult; }
  const bgfx::ViewId rasterViewOffset = rasterImages.textures.empty() ? 0 : 1;
  const Clock::time_point uploaded = Clock::now();
  // Private floating sums never share an attachment with the scene: every
  // sample resolves first, then reads the preceding sum and writes the other.
  struct MultipassTargets {
    CoinBgfxCallback * callback;
    bgfx::FrameBufferHandle sums[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    explicit MultipassTargets(CoinBgfxCallback * value) : callback(value) {}
    ~MultipassTargets() {
      if (callback && callback->failed()) return;
      for (auto handle : sums) if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }
  } accumulation(static_cast<CoinBgfxCallback *>(this->callback.get()));
  if (frame.numPasses > 1) {
    for (unsigned int i = 0; i < 2; ++i) {
      accumulation.sums[i] = bgfx::createFrameBuffer(this->width, this->height,
        bgfx::TextureFormat::RGBA32F, BGFX_TEXTURE_RT |
          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
          BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
      if (!bgfx::isValid(accumulation.sums[i])) {
        destroyTextures();
        return {CoinRenderBackendStatus::OUT_OF_MEMORY,
          "BGFX multipass float attachment allocation failed"};
      }
    }
  }
  struct ShadowMapOwner {
    CoinBgfxCallback * callback;
    std::vector<bgfx::FrameBufferHandle> buffers;
    ~ShadowMapOwner() {
      if (callback && callback->failed()) return;
      for (auto handle : buffers)
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }
  } shadowMaps{static_cast<CoinBgfxCallback *>(this->callback.get()), {}};
  if (hasShadows) {
    if (consumeTestFault("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE")) {
      destroyTextures();
      this->lastError = "Injected BGFX shadow-map allocation failure";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
    }
    for (const auto & pass : shadowPlan.passes) {
      const uint16_t size = static_cast<uint16_t>(pass.mapSize);
      bgfx::TextureHandle moments = bgfx::createTexture2D(size, size, false, 1,
        bgfx::TextureFormat::RGBA32F, shadowTextureFlags);
      bgfx::TextureHandle depth = bgfx::createTexture2D(size, size, false, 1,
        bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT_WRITE_ONLY);
      const bgfx::TextureHandle attachments[2] = {moments, depth};
      bgfx::FrameBufferHandle output = BGFX_INVALID_HANDLE;
      if (bgfx::isValid(moments) && bgfx::isValid(depth))
        output = bgfx::createFrameBuffer(2, attachments, true);
      if (!bgfx::isValid(output)) {
        if (bgfx::isValid(moments)) bgfx::destroy(moments);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
        destroyTextures();
        this->lastError = "BGFX shadow-map attachment allocation failed";
        return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
      }
      shadowMaps.buffers.push_back(output);
    }
    const SbMatrix clipConversion(
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f);
    const float white[4] = {1, 1, 1, 1};
    bgfx::setPaletteColor(3, white);
    for (size_t slot = 0; slot < shadowPlan.passes.size(); ++slot) {
      const auto & pass = shadowPlan.passes[slot];
      const bgfx::ViewId mapView = this->viewBase + rasterViewOffset + static_cast<bgfx::ViewId>(slot);
      bgfx::setViewName(mapView, "shadow_moments");
      bgfx::setViewMode(mapView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(mapView, 0, 0, static_cast<uint16_t>(pass.mapSize),
                        static_cast<uint16_t>(pass.mapSize));
      bgfx::setViewFrameBuffer(mapView, shadowMaps.buffers[slot]);
      bgfx::setViewClear(mapView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 3);
      bgfx::setViewTransform(mapView, nullptr, nullptr);
      bgfx::touch(mapView);
      const SbMatrix projection = homogeneousDepth ? pass.projectionCoin :
        pass.projectionCoin * clipConversion;
      const float depthParams[4] = {pass.nearDistance, pass.vsmFarDistance,
        frame.shadowLights[pass.lightSlot].type == CoinRenderLightType::SPOT ? 1.0f : 0.0f, 0.0f};
      for (const CoinBgfxDraw & draw : plan->shadowDraws) {
        if (std::find(pass.casterDraws.begin(), pass.casterDraws.end(),
                      draw.sourceDrawSlot) == pass.casterDraws.end()) continue;
        const auto & state = frame.renderStates[draw.renderStateSlot];
        const SbMatrix modelView = state.model *
          (state.shadowMapCameraOverride ? state.view : pass.view);
        const SbMatrix casterProjection = state.shadowMapCameraOverride ?
          (bgfx::getCaps()->homogeneousDepth ? state.projectionCoin :
           state.projectionCoin * clipConversion) : projection;
        SbMatrix viewportTransform;
        int32_t scissor[4];
        if (!coin_render_shadow_caster_viewport(frame, state, pass.mapSize, viewportTransform,
                                                scissor, this->lastError))
          return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
        if (!scissor[2] || !scissor[3])
          continue;
        bgfx::setScissor(uint16_t(scissor[0]),
                         uint16_t(int32_t(pass.mapSize) - scissor[1] - scissor[3]),
                         uint16_t(scissor[2]), uint16_t(scissor[3]));
        const SbMatrix mvp = modelView * casterProjection * viewportTransform;
        bgfx::setTransform(mvp.getValue());
        bgfx::setVertexBuffer(0, vb);
        bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
        bgfx::setUniform(this->shadowModelViewUniform, modelView.getValue());
        const SbMatrix clipModelView = state.model * state.view;
        bgfx::setUniform(this->shadowClipModelViewUniform, clipModelView.getValue());
        bgfx::setUniform(this->clipMetaUniform, draw.clipMeta);
        bgfx::setUniform(this->clipPlanesUniform, draw.clipPlanes, COIN_RENDER_MAX_CLIP_PLANES);
        bgfx::setUniform(this->shadowDepthUniform, depthParams);
        const float casterAlphaTest[4] = {static_cast<float>(state.alphaTestFunction),
          state.alphaTestReference, 0.0f, 0.0f};
        bgfx::setUniform(this->alphaTestUniform, casterAlphaTest);
        const float raster[4] = {0.0f, 0.0f, this->canonicalCoverage ? float(pass.mapSize) : 0.0f,
                                 this->canonicalCoverage ? float(pass.mapSize) : 0.0f};
        bgfx::setUniform(this->rasterViewportUniform, raster);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
          BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(mapView, this->shadowMomentsProgram);
      }
      bgfx::setScissor();
    }
  }
  bgfx::ViewId opaqueView = this->viewBase + rasterViewOffset + (hasShadows ? static_cast<bgfx::ViewId>(shadowPlan.passes.size()) : 0);
  if (this->shadowArrayActive) this->encodeShadowArray(shadowPlan,shadowMaps.buffers,opaqueView++);
  const bgfx::FrameBufferHandle resolvedOutput = bgfx::isValid(this->windowSceneFrameBuffer)
    ? this->windowSceneFrameBuffer : this->frameBuffer;
  const bgfx::FrameBufferHandle sceneOutput = this->sampleCount > 1 ?
    this->msaaFrameBuffer : resolvedOutput;
  if (selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
      !bgfx::isValid(bgfx::getTexture(sceneOutput, 1))) {
    destroyTextures();
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
      "BGFX sorted layers requires a sampleable opaque depth attachment");
  }
  const std::vector<CoinBgfxDraw> & sourceDraws =
    cameraPatchUsed ? cameraDraws : plan->draws;
  std::vector<CoinBgfxDraw> groupedDraws;
  const bool groupDraws = frame.numPasses == 1 && this->drawGroupingEnabled && !activeBump && !hasShadows && !plan->usesInstancing;
  if (groupDraws)
    CoinBgfxLowering::groupOpaqueDraws(sourceDraws, groupedDraws);
  const std::vector<CoinBgfxDraw> & baseDraws =
    groupDraws ? groupedDraws : sourceDraws;
  const LogicalDrawStats drawStats = logicalDrawStats(sourceDraws, baseDraws);
  const bool useSortedLayers =
    selectedStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS;
  const bgfx::TextureHandle surfaceSampleDepth = useSortedLayers && this->sampleCount > 1 ?
    bgfx::getTexture(this->peelFrameBuffers[0], 2) : bgfx::TextureHandle BGFX_INVALID_HANDLE;
  bgfx::ViewId nextView = opaqueView;
  for (int pass = 0; pass < frame.numPasses; ++pass) {
    opaqueView = nextView;
    std::vector<CoinBgfxDraw> jitteredDraws;
    if (frame.numPasses > 1) {
      jitteredDraws = baseDraws;
      for (CoinBgfxDraw & draw : jitteredDraws) {
        if (draw.multipassRasterFixed) continue;
        const auto & state = frame.renderStates[draw.renderStateSlot];
        const auto & viewport = frame.viewports[state.viewportSlot];
        SbMatrix shifted;
        CoinRenderMultipassCore::projection(state.projectionCoin, frame.numPasses, pass, viewport, shifted);
        const SbMatrix projection = CoinRenderTransformCore::projection(shifted, homogeneousDepth);
        const SbMatrix mvp = state.model * state.view * projection *
          CoinRenderTransformCore::viewportTransform(viewport, this->width, this->height);
        std::memcpy(draw.mvp, mvp.getValue(), sizeof(draw.mvp));
      }
      // Retain layer barriers even when this sample has no pixels from the
      // bitmap which originally established them. They submit no geometry.
      for (CoinBgfxDraw & draw : jitteredDraws)
        if (draw.multipassMask && !(draw.multipassMask & (1u << pass)) &&
            (draw.clearDepthBefore || draw.authoredScene)) draw.indexCount = 0;
      jitteredDraws.erase(std::remove_if(jitteredDraws.begin(), jitteredDraws.end(),
        [pass](const CoinBgfxDraw & draw) { return draw.multipassMask && !(draw.multipassMask & (1u << pass)) &&
          !draw.clearDepthBefore && !draw.authoredScene; }), jitteredDraws.end());
    }
    const std::vector<CoinBgfxDraw> & draws = frame.numPasses > 1 ? jitteredDraws : baseDraws;
  bgfx::setViewName(opaqueView, "opaque");
  bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(opaqueView, 0, 0, static_cast<uint16_t>(this->width),
                    static_cast<uint16_t>(this->height));
  bgfx::setViewFrameBuffer(opaqueView, sceneOutput);
  // The packed clear API quantizes Coin's float color before the GL clear.
  // Keep the float until the renderer converts it to its target format.
  bgfx::setPaletteColor(0, plan->clearColor);
  bgfx::setViewClear(opaqueView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 0);
  bgfx::setViewTransform(opaqueView, nullptr, nullptr);
  bgfx::touch(opaqueView);
  if (std::any_of(draws.begin(), draws.end(), [](const CoinBgfxDraw & draw) { return draw.authoredScene; })) {
    // The original view clears the frame once; backgrounds execute before the
    // authored scene and its depth barrier without clearing their color.
    ++opaqueView;
    this->encodeOverlayLayers(draws, vb, ib, sceneOutput, textures, opaqueView,
      this->width, this->height, frame, shadowPlan, shadowMaps.buffers, true, surfaceSampleDepth);
    bgfx::setViewName(opaqueView, "authored_scene");
    bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(opaqueView, 0, 0, this->width, this->height);
    bgfx::setViewFrameBuffer(opaqueView, sceneOutput);
    bgfx::setViewClear(opaqueView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(opaqueView, nullptr, nullptr);
    bgfx::touch(opaqueView);
  }
  nextView = opaqueView + 2;
  for (const CoinBgfxDraw & draw : draws) {
    if (!draw.indexCount || !draw.isSceneLayer() || (draw.blend && draw.deferred)) continue;
    // A skipped draw has no submit to discard its instance binding. Cull it
    // before encoding any geometry so later fullscreen passes stay ordinary.
    if (!setDrawScissor(draw, this->width, this->height)) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    if (plan->usesInstancing)
      bgfx::setInstanceDataBuffer(instanceBuffer, draw.firstInstance, draw.instanceCount);
    bgfx::setState(drawState(draw) | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
    this->submitSurfaceDraw(opaqueView, draw, textures, frame, shadowPlan, shadowMaps.buffers,
      this->width, this->height, surfaceSampleDepth);

  }

  nextView = this->encodeDeferredRuns(draws,vb,ib,sceneOutput,textures,opaqueView+1,
    this->width,this->height,frame,shadowPlan,shadowMaps.buffers,selectedStrategy,
    std::vector<bgfx::FrameBufferHandle>(this->peelFrameBuffers,this->peelFrameBuffers+this->peelPassCount),this->oitFrameBuffer,surfaceSampleDepth);
  if (!this->encodeOverlayLayers(draws, vb, ib,
        sceneOutput,
        textures, nextView, this->width, this->height, frame, shadowPlan, shadowMaps.buffers, false, surfaceSampleDepth)) {
    destroyTextures();
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  if (this->sampleCount > 1)
    this->encodeMsaaResolve(nextView++, resolvedOutput, this->width, this->height, 1.0f);
    if (frame.numPasses > 1) {
      const bgfx::ViewId sumView = nextView++;
      bgfx::setViewName(sumView, "multipass_sum");
      bgfx::setViewMode(sumView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(sumView, 0, 0, this->width, this->height);
      bgfx::setViewFrameBuffer(sumView, accumulation.sums[pass & 1]);
      bgfx::setViewClear(sumView, BGFX_CLEAR_NONE);
      bgfx::setViewTransform(sumView, nullptr, nullptr);
      const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
      const float info[4] = {1.0f / this->width, 1.0f / this->height,
        pass ? 1.0f : 0.0f, 1.0f / frame.numPasses};
      const uint32_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
      bgfx::setTransform(identity);
      bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
      bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
      bgfx::setUniform(this->multipassInfoUniform, info);
      bgfx::setTexture(0, this->multipassSceneSampler, bgfx::getTexture(resolvedOutput, 0), flags);
      // Bind a valid texture even when the branch does not consume a prior sum.
      bgfx::setTexture(1, this->multipassPreviousSampler,
        pass ? bgfx::getTexture(accumulation.sums[(pass - 1) & 1], 0) :
          bgfx::getTexture(resolvedOutput, 0), flags);
      bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
      bgfx::submit(sumView, this->multipassSumProgram);
    }
  }
  if (frame.numPasses > 1) {
    const bgfx::ViewId meanView = nextView++;
    bgfx::setViewName(meanView, "multipass_mean");
    bgfx::setViewMode(meanView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(meanView, 0, 0, this->width, this->height);
    bgfx::setViewFrameBuffer(meanView, resolvedOutput);
    bgfx::setViewClear(meanView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(meanView, nullptr, nullptr);
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float info[4] = {1.0f / this->width, 1.0f / this->height,
      0.0f, 1.0f / frame.numPasses};
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->multipassInfoUniform, info);
    bgfx::setTexture(0, this->multipassSceneSampler,
      bgfx::getTexture(accumulation.sums[(frame.numPasses - 1) & 1], 0),
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
      BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(meanView, this->multipassMeanProgram);
  }
  if (bgfx::isValid(this->windowSceneFrameBuffer)) {
    const bgfx::ViewId presentView = nextView++;
    bgfx::setViewName(presentView, "window_scene_present");
    bgfx::setViewMode(presentView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(presentView, 0, 0, this->width, this->height);
    bgfx::setViewFrameBuffer(presentView, this->frameBuffer);
    bgfx::setViewClear(presentView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(presentView, nullptr, nullptr);
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float pixelSize[4] = {1.0f / this->width, 1.0f / this->height, 0, 0};
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, pixelSize);
    bgfx::setTexture(0, this->layerSampler,
      bgfx::getTexture(this->windowSceneFrameBuffer, 0),
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
      BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(presentView, this->compositeProgram);
  }
  const Clock::time_point drawsEncoded = Clock::now();
  if (consumeTestFault("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE")) {
    static_cast<CoinBgfxCallback *>(this->callback.get())->inject(
      bgfx::Fatal::DeviceLost, "injected device loss during submission");
  }
  CoinRenderBackendStatus runtimeStatus = this->checkRuntimeFailure("BGFX submission failed");
  if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(runtimeStatus, this->lastError);
  if (this->presentToWindow) {
    CoinBgfxCallback * screenshotCallback =
      static_cast<CoinBgfxCallback *>(this->callback.get());
    if (target.windowReadbackRequested) {
      const std::string requestName =
        screenshotCallback->beginScreenshot(this->width, this->height);
      bgfx::requestScreenShot(this->frameBuffer, requestName.c_str());
    }
    const uint32_t submittedFrame = bgfx::frame();
    if (target.windowReadbackRequested) {
      // frame() waits for its predecessor, then releases the submitted scene
      // to the renderer thread. The renderer performs synchronous native
      // screenshot readback before signalling that scene's completion. Fence
      // it with an empty resource frame so cold program/pipeline preparation
      // and rendering finish inside apply(), before the callback deadline.
      // FLUSH suppresses presentation of this empty frame; no scene is replayed.
      bgfx::frame(BGFX_FRAME_FLUSH);
    }
    runtimeStatus = this->checkRuntimeFailure("BGFX frame submission failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS) {
      screenshotCallback->cancelScreenshot();
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
    }
    const Clock::time_point submitted = Clock::now();
    if (target.windowReadbackRequested &&
        !screenshotCallback->takeScreenshot(target.colorBuffer, this->lastError)) {
      if(this->lastError.empty()) this->lastError = "BGFX window screenshot unavailable or incomplete";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    }
    CoinBgfxPhaseSample sample;
    sample.gpuTimingRequested = traceGpu;
    const Clock::time_point gpuDrainBegin = Clock::now();
    uint32_t gpuQueryFrames = 0;
    for (int attempts = 0;
         traceGpu && !captureGpuPasses(submittedFrame, sample) && attempts < 4;
         ++attempts) {
      bgfx::frame();
      ++gpuQueryFrames;
    }
    runtimeStatus = this->checkRuntimeFailure("BGFX profiling drain failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
    const Clock::time_point gpuDrainComplete = Clock::now();
    destroyTextures();
    if (cameraPatchUsed) {
      this->cachedPlan.draws.swap(cameraDraws);
      if (!cameraInstances.empty() && !this->cachedPlan.instances.empty())
        this->cachedPlan.instances.swap(cameraInstances);
      this->cachedRevision = frame.revision;
    }
    if (!target.windowReadbackRequested) target.colorBuffer.clear();
    target.depthBuffer.clear();
    target.needsReconfigure = false;
    if (tracePhases) {
      const auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
      };
      sample.lowerMs = ms(begin, lowered);
      sample.uploadMs = ms(lowered, uploaded);
      sample.encodeMs = ms(uploaded, drawsEncoded);
      sample.drawEncodeMs = sample.encodeMs;
      sample.submitFrameMs = ms(drawsEncoded, submitted);
      sample.gpuQueryDrainMs = ms(gpuDrainBegin, gpuDrainComplete);
      sample.gpuQueryFrames = gpuQueryFrames;
      sample.vertices = plan->vertexCount();
      sample.draws = plan->draws.size();
      sample.resourceCacheHit = cacheHit || cameraPatchUsed;
      sample.textureCacheHit = textureCacheHit;
      sample.geometryBufferReused = geometryBufferReused;
      sample.vertexBufferCapacity = this->cachedVertexCapacity;
      sample.indexBufferCapacity = this->cachedIndexCapacity;
      sample.cameraPatchUsed = cameraPatchUsed;
      sample.materialPatchUsed = materialPatchUsed;
      sample.materialPatchRanges = static_cast<uint32_t>(materialRanges.size());
      sample.materialPatchVertices = materialPatchVertices;
      sample.readbackPublishedBytes = target.windowReadbackRequested
        ? target.colorBuffer.size() : 0;
      copyLogicalDrawStats(drawStats, this->drawGroupingEnabled, sample);
      std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_geometry compact_vertices=%d vertex_stride=%u instancing=%d instances=%zu world_space=%d instance_stride=%u instance_buffer_capacity=%u\n",
        plan->usesCompactVertices ? 1 : 0,
        static_cast<unsigned int>(plan->usesInstancing ? sizeof(CoinBgfxInstancedVertex) :
          plan->usesCompactVertices ? sizeof(CoinBgfxVertexPrefix) : sizeof(CoinBgfxVertex)),
        plan->usesInstancing ? 1 : 0, plan->instanceCount(), plan->instancedWorldSpace ? 1 : 0,
        static_cast<unsigned int>(sizeof(CoinBgfxInstance)), this->cachedInstanceCapacity);
      std::fprintf(stderr, "%s\n", CoinRenderDiagnosticShell::formatBgfxPhase(sample).c_str());
    }
    this->lastError.clear();
    rememberBump();
    logicalSubmitSucceeded();
    return CoinRenderSubmitResult(CoinRenderBackendStatus::SUCCESS, "", ++this->serial);
  }
  if (this->readbackSlots.empty()) {
    this->lastError = "BGFX readback staging ring is not allocated";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY, this->lastError);
  }
  std::shared_ptr<AsyncEntry> asyncEntry;
  ReadbackSlot * writeSlot = NULL;
  if (outTicket) {
    try {
      asyncEntry = std::make_shared<AsyncEntry>();
      asyncEntry->slot.pixels.resize(size_t(this->width) * this->height * 4);
      if (target.depthReadbackEnabled) asyncEntry->slot.depth.resize(size_t(this->width) * this->height);
    } catch (const std::bad_alloc &) {
      bgfx::frame(); destroyTextures();
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX async CPU staging allocation failed");
    }
    auto & slot = asyncEntry->slot;
    slot.texture = bgfx::createTexture2D(this->width, this->height, false, 1,
      bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    if (target.depthReadbackEnabled) slot.depthTexture = bgfx::createTexture2D(this->width, this->height, false, 1,
      bgfx::TextureFormat::R32F, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    if (!bgfx::isValid(slot.texture) || (target.depthReadbackEnabled && !bgfx::isValid(slot.depthTexture))) {
      if (bgfx::isValid(slot.texture)) bgfx::destroy(slot.texture);
      if (bgfx::isValid(slot.depthTexture)) bgfx::destroy(slot.depthTexture);
      destroyTextures();
      bgfx::frame();
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX async staging allocation failed");
    }
    auto & ticket = asyncEntry->ticket;
    ticket.token = nextTicketToken++;
    ticket.generation = target.generation;
    ticket.submissionSerial = ++this->serial;
    ticket.width = this->width; ticket.height = this->height;
    ticket.colorRowPitch = this->width * 4; ticket.colorBytes = slot.pixels.size();
    if (target.depthReadbackEnabled) {
      ticket.depthFormat = 1; ticket.depthRowPitch = ticket.colorRowPitch; ticket.depthBytes = slot.depth.size() * 4;
    }
    asyncEntry->bottomLeft = bgfx::getCaps()->originBottomLeft;
    try {
      auto & runtime = sharedRuntime();
      std::lock_guard<std::mutex> guard(runtime.mutex);
      asyncEntries().emplace(ticket.token, asyncEntry);
      ++runtime.references;
    } catch (const std::bad_alloc &) {
      bgfx::destroy(slot.texture);
      if (bgfx::isValid(slot.depthTexture)) bgfx::destroy(slot.depthTexture);
      bgfx::frame(); destroyTextures();
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX async ticket allocation failed");
    }
    writeSlot = &slot;
  }
  uint32_t preBlitWaitFrames = 0;
  for (size_t offset = 0; !writeSlot && offset < this->readbackSlots.size(); ++offset) {
    ReadbackSlot & candidate = this->readbackSlots[
      (this->readbackCursor + offset) % this->readbackSlots.size()];
    if (!candidate.pending) {
      writeSlot = &candidate;
      this->readbackCursor = static_cast<uint32_t>(
        (&candidate - this->readbackSlots.data() + 1) % this->readbackSlots.size());
      break;
    }
  }
  if (!writeSlot) {
    ReadbackSlot * oldest = &this->readbackSlots[0];
    for (ReadbackSlot & slot : this->readbackSlots) {
      if (slot.sequence < oldest->sequence) oldest = &slot;
    }
    uint32_t completed = bgfx::frame();
    ++preBlitWaitFrames;
    for (int attempts = 0; completed < oldest->readyFrame && attempts < 16;
         ++attempts) {
      completed = bgfx::frame();
      ++preBlitWaitFrames;
    }
    runtimeStatus = this->checkRuntimeFailure("BGFX readback backpressure failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
    if (completed < oldest->readyFrame) {
      this->lastError = "BGFX readback pipeline backpressure exceeded sixteen frames";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
    }
    if (bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL &&
        !CoinRenderImageCore::flipRgba8Rows(oldest->pixels, target.size)) {
      this->lastError = "BGFX OpenGL returned an invalid pipelined readback size";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
    }
    if (oldest->depthRequested && bgfx::getCaps()->originBottomLeft)
      for (int y = 0; y < this->height / 2; ++y)
        std::swap_ranges(oldest->depth.begin() + y * this->width, oldest->depth.begin() + (y + 1) * this->width,
                         oldest->depth.begin() + (this->height - y - 1) * this->width);
    if (oldest->depthRequested) this->lastPublishedDepth = oldest->depth;
    else this->lastPublishedDepth.clear();
    this->lastPublishedReadback = oldest->pixels;
    this->lastPublishedSequence = oldest->sequence;
    oldest->pending = false;
    writeSlot = oldest;
  }
  if (target.depthReadbackEnabled) {
    if (this->sampleCount > 1) {
      this->encodeMsaaResolve(nextView++, this->depthReadFrameBuffer,
                              this->width, this->height, 2.0f);
    } else {
      bgfx::setViewName(nextView, "depth_readback_conversion");
      bgfx::setViewRect(nextView, 0, 0, this->width, this->height);
      bgfx::setViewFrameBuffer(nextView, this->depthReadFrameBuffer);
      bgfx::setViewClear(nextView, BGFX_CLEAR_NONE);
      bgfx::setViewTransform(nextView, nullptr, nullptr);
      const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
      const float info[4] = {1.0f / this->width, 1.0f / this->height, 0, 0};
      bgfx::setTransform(identity);
      bgfx::setUniform(this->depthInfoUniform, info);
      bgfx::setTexture(0, this->readDepthSampler, bgfx::getTexture(this->frameBuffer, 1));
      bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
      bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
      bgfx::setState(BGFX_STATE_WRITE_R);
      bgfx::submit(nextView++, this->depthReadProgram);
    }
    bgfx::TextureRegion depthDst, depthSrc;
    depthDst.handle = writeSlot->depthTexture;
    depthSrc.handle = bgfx::getTexture(this->depthReadFrameBuffer);
    bgfx::blit(nextView, depthDst, depthSrc);
  }
  bgfx::TextureRegion destination;
  destination.handle = writeSlot->texture;
  bgfx::TextureRegion source;
  source.handle = bgfx::getTexture(this->frameBuffer);
  const bgfx::ViewId blitView = nextView;
  bgfx::setViewName(blitView, "blit_readback");
  bgfx::blit(blitView, destination, source);
  const Clock::time_point encoded = Clock::now();
  const size_t bytes = static_cast<size_t>(this->width) * static_cast<size_t>(this->height) * 4;
  // A synchronous publication exchanges this storage with the target's
  // previous image. Reestablish its size before handing the pointer to BGFX.
  writeSlot->pixels.resize(bytes);
  writeSlot->depthRequested = target.depthReadbackEnabled;
  if (writeSlot->depthRequested)
    writeSlot->depth.resize(static_cast<size_t>(this->width) * this->height);
  bgfx::TextureRegion readRegion;
  readRegion.handle = writeSlot->texture;
  writeSlot->sequence = ++this->readbackSequence;
  writeSlot->readyFrame = bgfx::read(readRegion, writeSlot->pixels.data());
  if (target.depthReadbackEnabled) {
    readRegion.handle = writeSlot->depthTexture;
    writeSlot->readyFrame = std::max(writeSlot->readyFrame, bgfx::read(readRegion, writeSlot->depth.data()));
  }
  writeSlot->pending = true;
  if (outTicket) {
    auto & ticket = asyncEntry->ticket;
    bgfx::frame(); // Submit only; polling advances completion without a wait loop.
    destroyTextures();
    if (consumeTestFault("COIN_BGFX_TEST_DEVICE_LOST_AFTER_ASYNC_ONCE"))
      static_cast<CoinBgfxCallback*>(this->callback.get())
          ->inject(bgfx::Fatal::DeviceLost, "injected device loss after async ticket allocation");
    const auto asyncStatus = this->checkRuntimeFailure("BGFX asynchronous submission failed");
    // Return the private allocation on failure so the common owner can cancel it.
    *outTicket = ticket;
    if (asyncStatus == CoinRenderBackendStatus::SUCCESS) {
      target.colorBuffer.clear();
      target.depthBuffer.clear();
      target.needsReconfigure = false;
    }
    if(asyncStatus==CoinRenderBackendStatus::SUCCESS) {rememberBump();logicalSubmitSucceeded();}
    return CoinRenderSubmitResult(asyncStatus, this->lastError, ticket.submissionSerial);
  }
  const Clock::time_point readRequested = Clock::now();
  uint32_t completedFrame = bgfx::frame();
  runtimeStatus = this->checkRuntimeFailure("BGFX frame submission failed");
  if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(runtimeStatus, this->lastError);
  uint32_t readWaitFrames = preBlitWaitFrames + 1;
  const uint32_t submittedFrame = completedFrame;
  const Clock::time_point submitted = Clock::now();
  destroyTextures();
  CoinBgfxPhaseSample gpuSample;
  gpuSample.gpuTimingRequested = traceGpu;
  const auto captureGpuFrame = [&]() {
    if (!traceGpu) return;
    captureGpuPasses(submittedFrame, gpuSample);
  };
  captureGpuFrame();
  ReadbackSlot * publishSlot = NULL;
  const auto selectReady = [&]() {
    ReadbackSlot * selected = static_cast<ReadbackSlot *>(NULL);
    for (ReadbackSlot & slot : this->readbackSlots) {
      if (slot.pending && slot.readyFrame <= completedFrame &&
          (!selected || slot.sequence < selected->sequence)) selected = &slot;
    }
    return selected;
  };
  publishSlot = selectReady();
  bool readbackBootstrap = false;
  if (!publishSlot && (this->readbackPipelineDepth == 1 ||
                       this->lastPublishedReadback.empty())) {
    readbackBootstrap = true;
    for (int attempts = 0;
         !publishSlot && attempts < 16; ++attempts) {
      completedFrame = bgfx::frame();
      ++readWaitFrames;
      captureGpuFrame();
      publishSlot = selectReady();
    }
    runtimeStatus = this->checkRuntimeFailure("BGFX readback wait failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
  }
  if (!publishSlot && (this->readbackPipelineDepth == 1 || this->lastPublishedReadback.empty())) {
    this->lastError = "BGFX readback did not complete within sixteen frames";
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  }
  const Clock::time_point framesCompleted = Clock::now();
  uint32_t readbackLatencyFrames = 0;
  const Clock::time_point readComplete = Clock::now();
  const Clock::time_point gpuDrainBegin = Clock::now();
  uint32_t gpuQueryFrames = 0;
  for (int attempts = 0; traceGpu && gpuSample.gpuFrameMs < 0.0 && attempts < 4; ++attempts) {
    bgfx::frame();
    ++gpuQueryFrames;
    captureGpuFrame();
  }
  runtimeStatus = this->checkRuntimeFailure("BGFX profiling drain failed");
  if (runtimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(runtimeStatus, this->lastError);
  const Clock::time_point gpuDrainComplete = Clock::now();
  // GL readback follows the framebuffer's bottom-left origin; Vulkan's
  // readback in this profile already matches the target's top-left RGBA view.
  // The row-swap is symmetric, so the shared mechanical image helper applies.
  if (publishSlot) {
    if (bgfx::getCaps()->rendererType == bgfx::RendererType::OpenGL &&
        !CoinRenderImageCore::flipRgba8Rows(publishSlot->pixels, target.size)) {
      this->lastError = "BGFX OpenGL returned an invalid RGBA readback size";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
    }
    if (publishSlot->depthRequested && bgfx::getCaps()->originBottomLeft)
      for (int y = 0; y < this->height / 2; ++y)
        std::swap_ranges(publishSlot->depth.begin() + y * this->width, publishSlot->depth.begin() + (y + 1) * this->width,
                         publishSlot->depth.begin() + (this->height - y - 1) * this->width);
    if (this->readbackPipelineDepth == 1) {
      // The synchronous contract publishes this frame before returning. Its
      // target storage can be recycled without maintaining a duplicate image.
      target.colorBuffer.swap(publishSlot->pixels);
      if (publishSlot->depthRequested) target.depthBuffer.swap(publishSlot->depth);
    } else {
      if (publishSlot->depthRequested) this->lastPublishedDepth = publishSlot->depth;
      else this->lastPublishedDepth.clear();
      this->lastPublishedReadback = publishSlot->pixels;
    }
    this->lastPublishedSequence = publishSlot->sequence;
    publishSlot->pending = false;
  }
  if (this->readbackPipelineDepth != 1) target.colorBuffer = this->lastPublishedReadback;
  readbackLatencyFrames = static_cast<uint32_t>(
    this->readbackSequence - this->lastPublishedSequence);
  const Clock::time_point readbackNormalized = Clock::now();
  if (cameraPatchUsed) {
    this->cachedPlan.draws.swap(cameraDraws);
    if (!cameraInstances.empty() && !this->cachedPlan.instances.empty())
      this->cachedPlan.instances.swap(cameraInstances);
    this->cachedRevision = frame.revision;
  }
  if (target.depthReadbackEnabled && this->readbackPipelineDepth != 1)
    target.depthBuffer = this->lastPublishedDepth;
  else if (!target.depthReadbackEnabled) target.depthBuffer.clear();
  if (tracePhases) {
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    CoinBgfxPhaseSample sample = gpuSample;
    sample.lowerMs = ms(begin, lowered);
    sample.uploadMs = ms(lowered, uploaded);
    sample.encodeMs = ms(uploaded, encoded);
    sample.drawEncodeMs = ms(uploaded, drawsEncoded);
    sample.blitEncodeMs = ms(drawsEncoded, encoded);
    sample.gpuQueryDrainMs = ms(gpuDrainBegin, gpuDrainComplete);
    sample.gpuQueryFrames = gpuQueryFrames;
    sample.readRequestMs = ms(encoded, readRequested);
    sample.submitFrameMs = ms(readRequested, submitted);
    sample.readWaitMs = ms(submitted, readComplete);
    sample.frameWaitMs = ms(submitted, framesCompleted);
    sample.rowFlipMs = ms(gpuDrainComplete, readbackNormalized);
    sample.vertices = plan->vertexCount();
    sample.draws = plan->draws.size();
    sample.readWaitFrames = readWaitFrames;
    sample.readbackPipelineDepth = this->readbackPipelineDepth;
    sample.readbackLatencyFrames = readbackLatencyFrames;
    sample.readbackGpuStagingBytes = static_cast<uint64_t>(bytes) *
      this->readbackSlots.size();
    sample.readbackCpuStagingBytes = sample.readbackGpuStagingBytes;
    sample.readbackPublishedBytes = target.colorBuffer.size();
    sample.readbackPipelineBytes = sample.readbackGpuStagingBytes +
      sample.readbackCpuStagingBytes;
    sample.readbackBootstrap = readbackBootstrap;
    sample.resourceCacheHit = cacheHit || cameraPatchUsed;
    sample.textureCacheHit = textureCacheHit;
    sample.geometryBufferReused = geometryBufferReused;
    sample.vertexBufferCapacity = this->cachedVertexCapacity;
    sample.indexBufferCapacity = this->cachedIndexCapacity;
    sample.cameraPatchUsed = cameraPatchUsed;
    sample.materialPatchUsed = materialPatchUsed;
    sample.materialPatchRanges = static_cast<uint32_t>(materialRanges.size());
    sample.materialPatchVertices = materialPatchVertices;
    copyLogicalDrawStats(drawStats, this->drawGroupingEnabled, sample);
    static_cast<CoinBgfxCallback *>(this->callback.get())->traceProgramCache();
    std::fprintf(stderr, "COIN_RENDER_PHASE bgfx_geometry compact_vertices=%d vertex_stride=%u instancing=%d instances=%zu world_space=%d instance_stride=%u instance_buffer_capacity=%u\n",
      plan->usesCompactVertices ? 1 : 0,
      static_cast<unsigned int>(plan->usesInstancing ? sizeof(CoinBgfxInstancedVertex) :
        plan->usesCompactVertices ? sizeof(CoinBgfxVertexPrefix) : sizeof(CoinBgfxVertex)),
      plan->usesInstancing ? 1 : 0, plan->instanceCount(), plan->instancedWorldSpace ? 1 : 0,
      static_cast<unsigned int>(sizeof(CoinBgfxInstance)), this->cachedInstanceCapacity);
    std::fprintf(stderr, "%s\n", CoinRenderDiagnosticShell::formatBgfxPhase(sample).c_str());
  }
  this->lastError.clear();
  rememberBump();
  logicalSubmitSucceeded();
  return CoinRenderSubmitResult(CoinRenderBackendStatus::SUCCESS, "", ++this->serial);
}

CoinRenderDeviceDomain CoinBgfxBackend::resourceDomain() const {
  if (!this->initialized || !this->onApiThread() ||
      static_cast<CoinBgfxCallback*>(this->callback.get())->failed())
    return {};
  CoinRenderDeviceDomain result;
  result.device = 1; // One BGFX runtime per process; generation distinguishes recreation.
  result.generation = sharedRuntime().generation;
  return result;
}

CoinRenderSubmitResult
CoinBgfxBackend::submitDirectTexture(const CoinRenderFramePlan & frame,
                                       const SbVec2i32 & size,
                                       uint64_t producerKey,
                                       uint64_t & token)
{
  if (!CoinRenderCubeTextureCore::validate(frame, this->lastError) ||
      !CoinRenderTextureSamplingCore::validVolumeFrame(frame, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  token = 0;
  const auto outputFormat=coinBgfxTextureFormat(frame.outputColorFormat);
  const auto * runtimeCaps=bgfx::getCaps();
  if(!runtimeCaps) return {CoinRenderBackendStatus::NOT_READY,"BGFX direct RTT runtime unavailable"};
  const bool manualMips = frame.outputMipmaps &&
    (!CoinRenderTextureSamplingCore::powerOfTwo(size[0]) ||
     !CoinRenderTextureSamplingCore::powerOfTwo(size[1]));
  const char * timestampFlag = std::getenv("COIN_WGPU_GPU_TIMESTAMPS");
  const bool traceNpotGpu = manualMips &&
    CoinRenderDiagnosticShell::phaseTracingEnabled() && timestampFlag &&
    std::strcmp(timestampFlag, "1") == 0;
  if(frame.outputMipmaps && !manualMips &&
     !(runtimeCaps->formats[outputFormat]&BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN))
    return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX format lacks direct GPU mip generation"};
  if(manualMips &&
     (!(runtimeCaps->formats[outputFormat]&BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
      !bgfx::isTextureValid(1, false, 1, outputFormat,
        BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT)))
    return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX format lacks NPOT GPU area-reduction attachments"};

  if (!this->initialized || !this->onApiThread() ||
      producerKey == 0 || size[0] <= 0 || size[1] <= 0 ||
      size[0] > CoinRenderRttPlan::maxExtent || size[1] > CoinRenderRttPlan::maxExtent) {
    return CoinRenderSubmitResult(CoinRenderBackendStatus::NOT_READY,
                        "BGFX direct RTT requires a prepared backend and valid extent");
  }
  for(const auto & image:frame.textures)if(image.cubeFaceSize && image.gpuToken) {
    if(!CoinRenderCubeTextureCore::valid(image))return {CoinRenderBackendStatus::UNSUPPORTED,"Invalid GPU cube atlas metadata"};
    const auto found=std::find_if(this->directTextures.begin(),this->directTextures.end(),[&image](const DirectTextureResource & resource) {
      return resource.token==image.gpuToken && resource.inUse && resource.width==int(image.width) &&
        resource.height==int(image.height) && resource.cubeFaceSize==image.cubeFaceSize &&
        resource.cubeMipmapped==image.cubeMipmapped && resource.format==image.format;
    });
    if(found==this->directTextures.end())return {CoinRenderBackendStatus::UNSUPPORTED,"GPU cube token belongs to another owner/generation or has mismatched metadata"};
  }
  const CoinRenderBackendStatus initialRuntimeStatus = this->checkRuntimeFailure("BGFX shared renderer failed before direct RTT");
  if (initialRuntimeStatus != CoinRenderBackendStatus::SUCCESS)
    return CoinRenderSubmitResult(initialRuntimeStatus, this->lastError);
  if(injectOutOfMemoryAfterSubmits()) {
    this->lastError="Injected BGFX out of memory after successful logical submissions";
    return {CoinRenderBackendStatus::OUT_OF_MEMORY,this->lastError};
  }
  if (size[0] > runtimeCaps->limits.maxTextureSize ||
      size[1] > runtimeCaps->limits.maxTextureSize)
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "BGFX direct RTT exceeds the adapter texture dimension"};
  if (traceNpotGpu) bgfx::setDebug(BGFX_DEBUG_PROFILER);
  const auto textureAdmission = bgfxTextureProfile(frame);
  if (textureAdmission.status != CoinRenderBackendStatus::SUCCESS) return textureAdmission;
  const auto msaaAdmission = this->msaaProfile(frame);
  if (msaaAdmission.status != CoinRenderBackendStatus::SUCCESS) return msaaAdmission;
  if (std::any_of(frame.renderStates.begin(), frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot & state) {
        return state.rasterTextureSourceSlot != UINT32_MAX;
      }) && !this->prepareRasterImageResources())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  const bool hasShadows = !frame.shadowGroups.empty();
  CoinRenderShadowPlan shadowPlan;
  if (hasShadows) {
    if (!coin_render_plan_shadows(frame, shadowPlan, this->lastError) ||
        !coin_render_shadow_object_profile(frame, shadowPlan,
          shadowPlan.passes.size(), this->lastError))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    if (!bgfxShadowBatchSupported(frame, shadowPlan, this->lastError))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
    const bgfx::Caps * caps = bgfx::getCaps();
    if (caps && caps->limits.maxTextureSamplers < (bgfxShadowMultitexture(frame)?11:shadowPlan.passes.size() > 4 ? 16 : shadowPlan.passes.size() > 2 ? 12 : 10))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
        "BGFX direct RTT shadow receiver has insufficient texture stages");
    for (const auto & pass : shadowPlan.passes)
      if (pass.mapSize > caps->limits.maxTextureSize ||
          !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
          !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, shadowTextureFlags))
        return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
          "BGFX direct RTT cannot render the planned shadow maps");
    if (bgfxShadowMultitexture(frame) && !coin_render_shadow_array_budget(shadowPlan,2,this->lastError))
      return {CoinRenderBackendStatus::UNSUPPORTED,this->lastError};
    if (!this->prepareShadowPrograms(shadowPlan.passes.size(),
                                     !bgfxShadowMultitexture(frame) && this->sampleCount == 1 &&
              bgfxFrameProgramNeeds(frame).ordinary))
      return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  }
  CoinBgfxPlan plan;
  if (!CoinBgfxLowering::lower(frame, size[0], size[1],
        bgfx::getCaps()->homogeneousDepth, plan, this->lastError, hasShadows)) {
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  bgfxSplitSamplerViews(plan);
  CoinBgfxTransparencyStrategy strategy;
  if (!CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
        CoinBgfxTransparencyMode::AUTO, this->weightedOitSupported,
        this->sortedLayersSupported, strategy, this->lastError))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  uint32_t requiredViews = 0;
  if (!bgfxCompositionViewBudget(frame, plan.draws, strategy,
        shadowPlan.passes.size(), true, size[0], size[1],
        0, this->sampleCount > 1, this->lastError, &requiredViews) || !this->reserveViews(requiredViews))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (!this->prepareTransparencyPrograms(strategy, bgfxNeedsFixedSurfacePrograms(plan.draws)))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (!this->prepareBaseProgram(plan.draws, hasShadows, strategy, false, false,
                                coinBgfxTextureFormat(frame.outputColorFormat)))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (!this->preparePortablePrograms(frame, plan.draws, hasShadows, strategy, false,
        coinBgfxTextureFormat(frame.outputColorFormat)))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (manualMips && !this->prepareNpotMipProgram())
    return CoinRenderSubmitResult(CoinRenderBackendStatus::BACKEND_ERROR, this->lastError);
  if (hasShadows && !bgfxShadowMultitexture(frame) && strategy != CoinBgfxTransparencyStrategy::OBJECT &&
      !this->prepareShadowTransparencyPrograms())
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  const auto msaaPreparation = this->prepareMsaa(size[0], size[1]);
  if (msaaPreparation.status != CoinRenderBackendStatus::SUCCESS) return msaaPreparation;
  auto allocationOptions = frame.transparency;
  if (strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT) allocationOptions.layers = 1;
  uint64_t requiredBytes = 0;
  if (!coin_render_transparency_sample_budget(size[0], size[1], allocationOptions,
        strategy != CoinBgfxTransparencyStrategy::OBJECT, this->sampleCount, requiredBytes, this->lastError))
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  if (strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT && this->sampleCount > 1 &&
      !bgfxWeightedSamples(this->sampleCount, this->layout, this->viewBase, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  if (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS && this->sampleCount > 1 &&
      !bgfxPeelingSamples(this->sampleCount, this->layout, this->viewBase, this->lastError))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  // Private attachments let the producer reuse the same compositor without
  // changing the consumer target's dimensions or persistent resources.
  struct TransparencyAttachments {
    std::vector<bgfx::FrameBufferHandle> layers;
    bgfx::FrameBufferHandle oit = BGFX_INVALID_HANDLE;
    ~TransparencyAttachments() {
      for (auto layer : layers) bgfx::destroy(layer);
      if (bgfx::isValid(oit)) bgfx::destroy(oit);
    }
  } transparency;
  const unsigned count = strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS ?
    frame.transparency.layers : strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT ? 1 : 0;
  for (unsigned layer = 0; layer < count; ++layer) {
    const bool weighted = strategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
    bgfx::TextureHandle attachments[3] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    const uint64_t flags = weightedTextureFlags(this->sampleCount);
    attachments[0] = bgfx::createTexture2D(size[0], size[1], false, 1,
      bgfx::TextureFormat::RGBA16F, flags);
    if (weighted) attachments[1] = bgfx::createTexture2D(size[0], size[1], false, 1,
      bgfx::TextureFormat::R16F, flags);
    if (!weighted) attachments[1] = bgfx::createTexture2D(size[0], size[1], false, 1,
      bgfx::TextureFormat::R16F, flags);
    const unsigned depthSlot = 2;
    attachments[depthSlot] = bgfx::createTexture2D(size[0], size[1], false, 1,
      bgfx::TextureFormat::D32F, flags);
    bool valid = true;
    for (unsigned i = 0; i <= depthSlot; ++i) valid = valid && bgfx::isValid(attachments[i]);
    bgfx::FrameBufferHandle buffer = BGFX_INVALID_HANDLE;
    if (valid) buffer = bgfx::createFrameBuffer(depthSlot + 1, attachments, true);
    if (!bgfx::isValid(buffer)) {
      for (unsigned i = 0; i <= depthSlot; ++i)
        if (bgfx::isValid(attachments[i])) bgfx::destroy(attachments[i]);
      return {CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX direct RTT transparency allocation failed"};
    }
    if (weighted) transparency.oit = buffer;
    else transparency.layers.push_back(buffer);
  }

  const auto multipassAdmission = this->preflightMultipass(frame, size);
  if (multipassAdmission.status != CoinRenderBackendStatus::SUCCESS) return multipassAdmission;
  if (frame.numPasses > 1 && !this->prepareMultipassPrograms())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  DirectTextureResource * cachedResource = NULL;
  for (DirectTextureResource & resource : this->directTextures) {
    if (resource.producerKey == producerKey && !resource.inUse) {
      cachedResource = &resource;
      break;
    }
  }
  const bool replaceCachedOutput = cachedResource &&
    (cachedResource->width != size[0] || cachedResource->height != size[1] ||
     cachedResource->format != frame.outputColorFormat ||
     cachedResource->mipmapped != frame.outputMipmaps ||
     (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
      !cachedResource->depthSampleable));
  bgfx::FrameBufferHandle output = BGFX_INVALID_HANDLE;
  if (cachedResource && !manualMips && !replaceCachedOutput)
    output = cachedResource->frameBuffer;
  const bool newOutput = !bgfx::isValid(output);
  if (newOutput) {
    const bgfx::Caps * caps = bgfx::getCaps();
    const bgfx::TextureFormat::Enum depthFormat =
      strategy != CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
      (caps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
        ? bgfx::TextureFormat::D24S8 : bgfx::TextureFormat::D32F;
    bgfx::TextureHandle color = bgfx::createTexture2D(
      static_cast<uint16_t>(size[0]), static_cast<uint16_t>(size[1]), frame.outputMipmaps, 1,
      coinBgfxTextureFormat(frame.outputColorFormat), peelTextureFlags);
    bgfx::TextureHandle depth = bgfx::createTexture2D(
      static_cast<uint16_t>(size[0]), static_cast<uint16_t>(size[1]), false, 1,
      depthFormat, strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS
                       ? peelTextureFlags : BGFX_TEXTURE_RT_WRITE_ONLY);
    if (bgfx::isValid(color) && bgfx::isValid(depth)) {
      if (manualMips) {
        bgfx::Attachment attachments[2];
        attachments[0].init(color, bgfx::Access::Write, 0, 1, 0,
                            BGFX_ATTACHMENT_NONE);
        attachments[1].init(depth, bgfx::Access::Write, 0, 1, 0,
                            BGFX_ATTACHMENT_NONE);
        output = bgfx::createFrameBuffer(2, attachments, true);
      } else {
        const bgfx::TextureHandle attachments[2] = {color, depth};
        output = bgfx::createFrameBuffer(2, attachments, true);
      }
    }
    if (!bgfx::isValid(color) || !bgfx::isValid(depth) || !bgfx::isValid(output)) {
      if (bgfx::isValid(output)) bgfx::destroy(output);
      else {
        if (bgfx::isValid(color)) bgfx::destroy(color);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
      }
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY,
                          "BGFX could not allocate direct RTT framebuffer");
    }
  }
  if (strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
      !bgfx::isValid(bgfx::getTexture(output, 1))) {
    if (newOutput) bgfx::destroy(output);
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
      "BGFX direct sorted layers requires a sampleable opaque depth attachment");
  }

  bgfx::DynamicVertexBufferHandle vb = BGFX_INVALID_HANDLE;
  bgfx::DynamicIndexBufferHandle ib = BGFX_INVALID_HANDLE;
  if (!plan.draws.empty()) {
    vb = bgfx::createDynamicVertexBuffer(bgfx::copy(plan.vertices.data(),
      static_cast<uint32_t>(plan.vertices.size() * sizeof(CoinBgfxVertex))),
      this->layout);
    ib = bgfx::createDynamicIndexBuffer(bgfx::copy(plan.indices.data(),
      static_cast<uint32_t>(plan.indices.size() * sizeof(uint32_t))),
      BGFX_BUFFER_INDEX32);
  }
  if ((!plan.draws.empty()) && (!bgfx::isValid(vb) || !bgfx::isValid(ib))) {
    if (bgfx::isValid(vb)) bgfx::destroy(vb);
    if (bgfx::isValid(ib)) bgfx::destroy(ib);
    if (newOutput) bgfx::destroy(output);
    return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY,
                        "BGFX could not allocate direct RTT geometry");
  }

  std::vector<bgfx::TextureHandle> textures(plan.textures.size(), BGFX_INVALID_HANDLE);
  std::vector<bool> owned(plan.textures.size(), false);
  for (size_t i = 0; i < plan.textures.size(); ++i) {
    const CoinBgfxTexture & source = plan.textures[i];
    if (source.gpuCopySourceSlot != SIZE_MAX) {
      textures[i] = bgfx::createTexture2D(static_cast<uint16_t>(source.width),
        static_cast<uint16_t>(source.height), false, 1,
        coinBgfxTextureFormat(source.format),
        coinBgfxTextureFlags(source.format) | BGFX_TEXTURE_BLIT_DST);
      owned[i] = true;
    } else if (source.gpuToken != 0) {
      for (const DirectTextureResource & resource : this->directTextures) {
        if (resource.token == source.gpuToken && resource.format == source.format &&
            resource.cubeFaceSize==source.cubeFaceSize && resource.cubeMipmapped==source.cubeMipmapped &&
            (!source.mipmapped || resource.mipmapped) && resource.width == int(source.width) && resource.height == int(source.height))
          textures[i] = bgfx::getTexture(resource.frameBuffer, 0);
      }
    } else {
      textures[i] = bgfx::createTexture2D(static_cast<uint16_t>(source.width),
        static_cast<uint16_t>(source.height), source.mipmapped, 1, coinBgfxTextureFormat(source.format),
        coinBgfxTextureFlags(source.format), bgfx::copy(source.pixelsRgba.data(),
          static_cast<uint32_t>(source.pixelsRgba.size())));
      owned[i] = true;
    }
    if (!bgfx::isValid(textures[i])) {
      for (size_t j = 0; j < textures.size(); ++j)
        if (owned[j] && bgfx::isValid(textures[j])) bgfx::destroy(textures[j]);
      if (bgfx::isValid(vb)) bgfx::destroy(vb);
      if (bgfx::isValid(ib)) bgfx::destroy(ib);
      if (newOutput) bgfx::destroy(output);
      return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED,
                          "BGFX direct RTT references an invalid producer texture");
    }
  }

  if(this->portableSampling) for(size_t i=0;i<textures.size();++i)
    this->samplingBoundTextureSizes[textures[i].idx]={plan.textures[i].width,plan.textures[i].height};
  for (size_t i = 0; i < plan.textures.size(); ++i) {
    const size_t sourceSlot = plan.textures[i].gpuCopySourceSlot;
    if (sourceSlot == SIZE_MAX) continue;
    bgfx::TextureRegion destination, source;
    destination.handle = textures[i];
    source.handle = textures[sourceSlot];
    bgfx::blit(this->viewBase, destination, source);
  }
  const auto cleanupDirect = [&]() {
    for (size_t i = 0; i < textures.size(); ++i)
      if (owned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
    if (bgfx::isValid(vb)) bgfx::destroy(vb);
    if (bgfx::isValid(ib)) bgfx::destroy(ib);
    if (newOutput) bgfx::destroy(output);
  };
  if (!this->prepareShadowArray(shadowPlan,bgfxShadowMultitexture(frame))) {
    cleanupDirect(); return {CoinRenderBackendStatus::UNSUPPORTED,this->lastError};
  }
  struct RasterImageOwner {
    std::vector<bgfx::TextureHandle> textures;
    ~RasterImageOwner() { for (auto handle : textures) if (bgfx::isValid(handle)) bgfx::destroy(handle); }
  } rasterImages;
  const auto rasterResult = this->encodeRasterImages(frame, textures, rasterImages.textures);
  if (rasterResult.status != CoinRenderBackendStatus::SUCCESS) { cleanupDirect(); return rasterResult; }
  const bgfx::ViewId rasterViewOffset = rasterImages.textures.empty() ? 0 : 1;
  struct ShadowMapOwner {
    CoinBgfxCallback * callback;
    std::vector<bgfx::FrameBufferHandle> buffers;
    ~ShadowMapOwner() {
      if (callback && callback->failed()) return;
      for (auto handle : buffers)
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }
  } shadowMaps{static_cast<CoinBgfxCallback *>(this->callback.get()), {}};
  if (hasShadows) {
    if (consumeTestFault("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE")) {
      cleanupDirect();
      this->lastError = "Injected BGFX shadow-map allocation failure";
      return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
    }
    for (const auto & pass : shadowPlan.passes) {
      const uint16_t size = static_cast<uint16_t>(pass.mapSize);
      bgfx::TextureHandle moments = bgfx::createTexture2D(size, size, false, 1,
        bgfx::TextureFormat::RGBA32F, shadowTextureFlags);
      bgfx::TextureHandle depth = bgfx::createTexture2D(size, size, false, 1,
        bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT_WRITE_ONLY);
      const bgfx::TextureHandle attachments[2] = {moments, depth};
      bgfx::FrameBufferHandle output = BGFX_INVALID_HANDLE;
      if (bgfx::isValid(moments) && bgfx::isValid(depth))
        output = bgfx::createFrameBuffer(2, attachments, true);
      if (!bgfx::isValid(output)) {
        if (bgfx::isValid(moments)) bgfx::destroy(moments);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
        cleanupDirect();
        this->lastError = "BGFX shadow-map attachment allocation failed";
        return CoinRenderSubmitResult(CoinRenderBackendStatus::OUT_OF_MEMORY, this->lastError);
      }
      shadowMaps.buffers.push_back(output);
    }
    const SbMatrix clipConversion(
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f);
    const float white[4] = {1, 1, 1, 1};
    bgfx::setPaletteColor(3, white);
    for (size_t slot = 0; slot < shadowPlan.passes.size(); ++slot) {
      const auto & pass = shadowPlan.passes[slot];
      const bgfx::ViewId mapView = this->viewBase + rasterViewOffset + static_cast<bgfx::ViewId>(slot);
      bgfx::setViewName(mapView, "shadow_moments");
      bgfx::setViewMode(mapView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(mapView, 0, 0, static_cast<uint16_t>(pass.mapSize),
                        static_cast<uint16_t>(pass.mapSize));
      bgfx::setViewFrameBuffer(mapView, shadowMaps.buffers[slot]);
      bgfx::setViewClear(mapView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 3);
      bgfx::setViewTransform(mapView, nullptr, nullptr);
      bgfx::touch(mapView);
      const SbMatrix projection = bgfx::getCaps()->homogeneousDepth ? pass.projectionCoin :
        pass.projectionCoin * clipConversion;
      const float depthParams[4] = {pass.nearDistance, pass.vsmFarDistance,
        frame.shadowLights[pass.lightSlot].type == CoinRenderLightType::SPOT ? 1.0f : 0.0f, 0.0f};
      for (const CoinBgfxDraw & draw : plan.shadowDraws) {
        if (std::find(pass.casterDraws.begin(), pass.casterDraws.end(),
                      draw.sourceDrawSlot) == pass.casterDraws.end()) continue;
        const auto & state = frame.renderStates[draw.renderStateSlot];
        const SbMatrix modelView = state.model *
          (state.shadowMapCameraOverride ? state.view : pass.view);
        const SbMatrix casterProjection = state.shadowMapCameraOverride ?
          (bgfx::getCaps()->homogeneousDepth ? state.projectionCoin :
           state.projectionCoin * clipConversion) : projection;
        SbMatrix viewportTransform;
        int32_t scissor[4];
        if (!coin_render_shadow_caster_viewport(frame, state, pass.mapSize, viewportTransform,
                                                scissor, this->lastError))
          return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
        if (!scissor[2] || !scissor[3])
          continue;
        bgfx::setScissor(uint16_t(scissor[0]),
                         uint16_t(int32_t(pass.mapSize) - scissor[1] - scissor[3]),
                         uint16_t(scissor[2]), uint16_t(scissor[3]));
        const SbMatrix mvp = modelView * casterProjection * viewportTransform;
        bgfx::setTransform(mvp.getValue());
        bgfx::setVertexBuffer(0, vb);
        bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
        bgfx::setUniform(this->shadowModelViewUniform, modelView.getValue());
        const SbMatrix clipModelView = state.model * state.view;
        bgfx::setUniform(this->shadowClipModelViewUniform, clipModelView.getValue());
        bgfx::setUniform(this->clipMetaUniform, draw.clipMeta);
        bgfx::setUniform(this->clipPlanesUniform, draw.clipPlanes, COIN_RENDER_MAX_CLIP_PLANES);
        bgfx::setUniform(this->shadowDepthUniform, depthParams);
        const float casterAlphaTest[4] = {static_cast<float>(state.alphaTestFunction),
          state.alphaTestReference, 0.0f, 0.0f};
        bgfx::setUniform(this->alphaTestUniform, casterAlphaTest);
        const float raster[4] = {0.0f, 0.0f, this->canonicalCoverage ? float(pass.mapSize) : 0.0f,
                                 this->canonicalCoverage ? float(pass.mapSize) : 0.0f};
        bgfx::setUniform(this->rasterViewportUniform, raster);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
          BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(mapView, this->shadowMomentsProgram);
      }
      bgfx::setScissor();
    }
  }
  // Private floating sums never share an attachment with the scene: every
  // sample resolves first, then reads the preceding sum and writes the other.
  struct MultipassTargets {
    CoinBgfxCallback * callback;
    bgfx::FrameBufferHandle sums[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    explicit MultipassTargets(CoinBgfxCallback * value) : callback(value) {}
    ~MultipassTargets() {
      if (callback && callback->failed()) return;
      for (auto handle : sums) if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }
  } accumulation(static_cast<CoinBgfxCallback *>(this->callback.get()));
  if (frame.numPasses > 1) {
    for (unsigned int i = 0; i < 2; ++i) {
      accumulation.sums[i] = bgfx::createFrameBuffer(static_cast<uint16_t>(size[0]), static_cast<uint16_t>(size[1]),
        bgfx::TextureFormat::RGBA32F, BGFX_TEXTURE_RT |
          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
          BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
      if (!bgfx::isValid(accumulation.sums[i])) {
        cleanupDirect();
        return {CoinRenderBackendStatus::OUT_OF_MEMORY,
          "BGFX multipass float attachment allocation failed"};
      }
    }
  }
  const bgfx::FrameBufferHandle sceneOutput = this->sampleCount > 1 ?
    this->msaaFrameBuffer : output;
  bgfx::ViewId opaqueView = this->viewBase + rasterViewOffset + static_cast<bgfx::ViewId>(shadowPlan.passes.size());
  if (this->shadowArrayActive) this->encodeShadowArray(shadowPlan,shadowMaps.buffers,opaqueView++);
  bgfx::ViewId nextView = opaqueView;
  for (int pass = 0; pass < frame.numPasses; ++pass) {
    opaqueView = nextView;
    std::vector<CoinBgfxDraw> jitteredDraws;
    if (frame.numPasses > 1) {
      jitteredDraws = plan.draws;
      for (CoinBgfxDraw & draw : jitteredDraws) {
        if (draw.multipassRasterFixed) continue;
        const auto & state = frame.renderStates[draw.renderStateSlot];
        const auto & viewport = frame.viewports[state.viewportSlot];
        SbMatrix shifted;
        CoinRenderMultipassCore::projection(state.projectionCoin, frame.numPasses, pass, viewport, shifted);
        const SbMatrix projection = CoinRenderTransformCore::projection(shifted, bgfx::getCaps()->homogeneousDepth);
        const SbMatrix mvp = state.model * state.view * projection *
          CoinRenderTransformCore::viewportTransform(viewport, size[0], size[1]);
        std::memcpy(draw.mvp, mvp.getValue(), sizeof(draw.mvp));
      }
      // Retain layer barriers even when this sample has no pixels from the
      // bitmap which originally established them. They submit no geometry.
      for (CoinBgfxDraw & draw : jitteredDraws)
        if (draw.multipassMask && !(draw.multipassMask & (1u << pass)) &&
            (draw.clearDepthBefore || draw.authoredScene)) draw.indexCount = 0;
      jitteredDraws.erase(std::remove_if(jitteredDraws.begin(), jitteredDraws.end(),
        [pass](const CoinBgfxDraw & draw) { return draw.multipassMask && !(draw.multipassMask & (1u << pass)) &&
          !draw.clearDepthBefore && !draw.authoredScene; }), jitteredDraws.end());
    }
    const auto & sampleDraws = frame.numPasses > 1 ? jitteredDraws : plan.draws;
  bgfx::setPaletteColor(0, plan.clearColor);
  bgfx::setViewName(opaqueView, "rtt_opaque");
  bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(opaqueView, 0, 0, static_cast<uint16_t>(size[0]),
                    static_cast<uint16_t>(size[1]));
  bgfx::setViewFrameBuffer(opaqueView, sceneOutput);
  bgfx::setViewClear(opaqueView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.0f, 0, 0);
  bgfx::touch(opaqueView);
  const bgfx::TextureHandle surfaceSampleDepth = strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS && this->sampleCount > 1 ?
    bgfx::getTexture(transparency.layers[0], 2) : bgfx::TextureHandle BGFX_INVALID_HANDLE;
  if (std::any_of(sampleDraws.begin(), sampleDraws.end(), [](const CoinBgfxDraw & draw) { return draw.authoredScene; })) {
    ++opaqueView;
    this->encodeOverlayLayers(sampleDraws, vb, ib, sceneOutput, textures, opaqueView,
      size[0], size[1], frame, shadowPlan, shadowMaps.buffers, true, surfaceSampleDepth);
    bgfx::setViewName(opaqueView, "rtt_authored_scene");
    bgfx::setViewMode(opaqueView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(opaqueView, 0, 0, size[0], size[1]);
    bgfx::setViewFrameBuffer(opaqueView, sceneOutput);
    bgfx::setViewClear(opaqueView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(opaqueView, nullptr, nullptr);
    bgfx::touch(opaqueView);
  }
  for (const CoinBgfxDraw & draw : sampleDraws) {
    if (!draw.indexCount || !draw.isSceneLayer() || (draw.blend && draw.deferred)) continue;
    bgfx::setTransform(draw.mvp);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setIndexBuffer(ib, draw.firstIndex, draw.indexCount);
    bgfx::setState(drawState(draw) | (this->sampleCount > 1 ? BGFX_STATE_MSAA : 0));
    if (!setDrawScissor(draw, size[0], size[1])) continue;
    this->submitSurfaceDraw(opaqueView, draw, textures, frame, shadowPlan, shadowMaps.buffers,
      size[0], size[1], surfaceSampleDepth);
  }

  nextView = this->encodeDeferredRuns(sampleDraws,vb,ib,sceneOutput,textures,opaqueView+1,
    size[0],size[1],frame,shadowPlan,shadowMaps.buffers,strategy,transparency.layers,transparency.oit,surfaceSampleDepth);

  if (!this->encodeOverlayLayers(sampleDraws, vb, ib, sceneOutput, textures, nextView,
                                size[0], size[1], frame, shadowPlan, shadowMaps.buffers, false, surfaceSampleDepth)) {
    cleanupDirect();
    return CoinRenderSubmitResult(CoinRenderBackendStatus::UNSUPPORTED, this->lastError);
  }
  if (this->sampleCount > 1)
    this->encodeMsaaResolve(nextView++, output, size[0], size[1], 1.0f);
    if (frame.numPasses > 1) {
      const bgfx::ViewId sumView = nextView++;
      bgfx::setViewName(sumView, "multipass_sum");
      bgfx::setViewMode(sumView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(sumView, 0, 0, size[0], size[1]);
      bgfx::setViewFrameBuffer(sumView, accumulation.sums[pass & 1]);
      bgfx::setViewClear(sumView, BGFX_CLEAR_NONE);
      bgfx::setViewTransform(sumView, nullptr, nullptr);
      const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
      const float info[4] = {1.0f / size[0], 1.0f / size[1],
        pass ? 1.0f : 0.0f, 1.0f / frame.numPasses};
      const uint32_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
      bgfx::setTransform(identity);
      bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
      bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
      bgfx::setUniform(this->multipassInfoUniform, info);
      bgfx::setTexture(0, this->multipassSceneSampler, bgfx::getTexture(output, 0), flags);
      // Bind a valid texture even when the branch does not consume a prior sum.
      bgfx::setTexture(1, this->multipassPreviousSampler,
        pass ? bgfx::getTexture(accumulation.sums[(pass - 1) & 1], 0) :
          bgfx::getTexture(output, 0), flags);
      bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
      bgfx::submit(sumView, this->multipassSumProgram);
    }
  }
  if (frame.numPasses > 1) {
    const bgfx::ViewId meanView = nextView++;
    bgfx::setViewName(meanView, "multipass_mean");
    bgfx::setViewMode(meanView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(meanView, 0, 0, size[0], size[1]);
    bgfx::setViewFrameBuffer(meanView, output);
    bgfx::setViewClear(meanView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(meanView, nullptr, nullptr);
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float info[4] = {1.0f / size[0], 1.0f / size[1],
      0.0f, 1.0f / frame.numPasses};
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->multipassInfoUniform, info);
    bgfx::setTexture(0, this->multipassSceneSampler,
      bgfx::getTexture(accumulation.sums[(frame.numPasses - 1) & 1], 0),
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
      BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(meanView, this->multipassMeanProgram);
  }
  bgfx::frame();
  if (manualMips) {
    auto runtimeStatus = this->checkRuntimeFailure("BGFX direct RTT base render failed");
    if (runtimeStatus != CoinRenderBackendStatus::SUCCESS) {
      cleanupDirect();
      return CoinRenderSubmitResult(runtimeStatus, this->lastError);
    }
    const bgfx::TextureHandle color = bgfx::getTexture(output, 0);
    int sourceWidth = size[0], sourceHeight = size[1];
    uint16_t level = 1;
    std::map<uint32_t, unsigned> mipFrames;
    std::map<unsigned, double> mipGpuMilliseconds;
    std::map<unsigned, double> mipFrameMilliseconds;
    const auto captureMipGpu = [&]() {
      const bgfx::Stats * stats = bgfx::getStats();
      if (!stats || stats->gpuTimerFreq <= 0) return;
      const double millisecondsPerTick = 1000.0 / double(stats->gpuTimerFreq);
      const auto frame = mipFrames.find(stats->gpuFrameNum);
      if (frame != mipFrames.end() && stats->gpuTimeEnd > stats->gpuTimeBegin)
        mipFrameMilliseconds.emplace(frame->second,
          double(stats->gpuTimeEnd - stats->gpuTimeBegin) * millisecondsPerTick);
      for (uint16_t index = 0; index < stats->numViews; ++index) {
        const bgfx::ViewStats & view = stats->viewStats[index];
        if (view.view != this->viewBase + 1) continue;
        // BGFX can reuse the view name before delayed timestamps arrive.
        // The submitted frame number identifies this reduction level.
        const auto found = mipFrames.find(view.gpuFrameNum);
        if (found == mipFrames.end() ||
            view.gpuTimeEnd <= view.gpuTimeBegin) continue;
        mipGpuMilliseconds.emplace(found->second,
          double(view.gpuTimeEnd - view.gpuTimeBegin) *
          millisecondsPerTick);
      }
    };
    if (traceNpotGpu) {
      // Isolate timer queries from the base scene and its shadow/OIT views.
      for (unsigned settle = 0; settle < 8; ++settle) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        bgfx::frame();
      }
    }
    while (sourceWidth > 1 || sourceHeight > 1) {
      const int destinationWidth = std::max(1, sourceWidth / 2);
      const int destinationHeight = std::max(1, sourceHeight / 2);
      // Fault after one completed level: a partially generated replacement
      // must never displace an older published texture or advance the serial.
      // A one-level chain instead fails before its only allocation.
      if ((level == 2 || (level == 1 && destinationWidth == 1 &&
                          destinationHeight == 1)) &&
          consumeTestFault("COIN_BGFX_TEST_NPOT_MIP_ALLOC_ONCE")) {
        cleanupDirect();
        return {CoinRenderBackendStatus::OUT_OF_MEMORY,
                "Injected BGFX NPOT mip allocation failure"};
      }
      const uint64_t scratchFlags = BGFX_TEXTURE_BLIT_DST |
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
      bgfx::TextureHandle scratch = bgfx::createTexture2D(
        static_cast<uint16_t>(sourceWidth), static_cast<uint16_t>(sourceHeight),
        false, 1, outputFormat, scratchFlags);
      bgfx::FrameBufferHandle mipBuffer = BGFX_INVALID_HANDLE;
      if (bgfx::isValid(scratch)) {
        bgfx::Attachment attachment;
        attachment.init(color, bgfx::Access::Write, 0, 1, level,
                        BGFX_ATTACHMENT_NONE);
        mipBuffer = bgfx::createFrameBuffer(1, &attachment, false);
      }
      if (!bgfx::isValid(scratch) || !bgfx::isValid(mipBuffer)) {
        if (bgfx::isValid(scratch)) bgfx::destroy(scratch);
        if (bgfx::isValid(mipBuffer)) bgfx::destroy(mipBuffer);
        cleanupDirect();
        return {CoinRenderBackendStatus::OUT_OF_MEMORY,
                "BGFX could not allocate NPOT mip GPU scratch or attachment"};
      }
      const bgfx::ViewId copyView = this->viewBase;
      const bgfx::ViewId reduceView = this->viewBase + 1;
      bgfx::resetView(copyView);
      bgfx::resetView(reduceView);
      bgfx::TextureRegion source, destination;
      source.handle = color;
      source.mip = static_cast<uint8_t>(level - 1);
      destination.handle = scratch;
      bgfx::blit(copyView, destination, source);
      bgfx::setViewName(reduceView, "rtt_npot_area_mip");
      bgfx::setViewMode(reduceView, bgfx::ViewMode::Sequential);
      bgfx::setViewRect(reduceView, 0, 0,
        static_cast<uint16_t>(destinationWidth),
        static_cast<uint16_t>(destinationHeight));
      bgfx::setViewFrameBuffer(reduceView, mipBuffer);
      bgfx::setViewClear(reduceView, BGFX_CLEAR_NONE);
      bgfx::setViewTransform(reduceView, nullptr, nullptr);
      const float extent[4] = {float(sourceWidth), float(sourceHeight),
                               float(destinationWidth), float(destinationHeight)};
      bgfx::setUniform(this->npotMipExtentUniform, extent);
      bgfx::setTexture(0, this->npotMipSampler, scratch);
      bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
      bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
      bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
      bgfx::submit(reduceView, this->npotMipProgram);
      const uint32_t mipFrame = bgfx::frame();
      if (traceNpotGpu) {
        mipFrames.emplace(mipFrame, unsigned(level));
        std::fprintf(stderr, "COIN_RENDER_PHASE rtt_npot_mip_frame level=%u frame=%u reduce_view=%u\n",
                     unsigned(level), mipFrame, unsigned(reduceView));
        captureMipGpu();
        // BGFX retains only the newest timer result per view. Resolve each
        // submitted reduction before reusing its view for another mip level.
        // This serialization belongs only to the opt-in GPU cost trace.
        for (unsigned drain = 0; drain < 16 &&
             (mipGpuMilliseconds.count(level) == 0 ||
              mipFrameMilliseconds.count(level) == 0); ++drain) {
          captureMipGpu();
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          bgfx::resetView(reduceView);
          bgfx::setViewName(reduceView, "rtt_npot_level_drain");
          bgfx::setViewFrameBuffer(reduceView, mipBuffer);
          bgfx::setViewRect(reduceView, 0, 0, 1, 1);
          bgfx::setViewClear(reduceView, BGFX_CLEAR_NONE);
          bgfx::touch(reduceView);
          bgfx::frame();
          captureMipGpu();
        }
      }
      runtimeStatus = this->checkRuntimeFailure("BGFX NPOT mip area reduction failed");
      bgfx::destroy(mipBuffer);
      bgfx::destroy(scratch);
      if (runtimeStatus != CoinRenderBackendStatus::SUCCESS) {
        cleanupDirect();
        return CoinRenderSubmitResult(runtimeStatus, this->lastError);
      }
      sourceWidth = destinationWidth;
      sourceHeight = destinationHeight;
      ++level;
    }
    for (int attempts = 0; traceNpotGpu &&
         (mipGpuMilliseconds.size() < mipFrames.size() ||
          mipFrameMilliseconds.size() < mipFrames.size()) && attempts < 8;
         ++attempts) {
      // Keep a view in each drain frame: BGFX exposes delayed per-view
      // timestamps through the current frame's view-stat array.
      bgfx::resetView(this->viewBase + 1);
      bgfx::setViewName(this->viewBase + 1, "rtt_npot_mip_drain");
      bgfx::setViewFrameBuffer(this->viewBase + 1, output);
      bgfx::setViewRect(this->viewBase + 1, 0, 0, 1, 1);
      bgfx::setViewClear(this->viewBase + 1, BGFX_CLEAR_NONE);
      bgfx::touch(this->viewBase + 1);
      bgfx::frame();
      captureMipGpu();
      runtimeStatus = this->checkRuntimeFailure("BGFX NPOT mip GPU timestamp drain failed");
      if (runtimeStatus != CoinRenderBackendStatus::SUCCESS) {
        cleanupDirect();
        return CoinRenderSubmitResult(runtimeStatus, this->lastError);
      }
    }
    if (traceNpotGpu) {
      double areaMs = 0.0, frameMs = 0.0;
      for (const auto & entry : mipGpuMilliseconds) areaMs += entry.second;
      for (const auto & entry : mipFrameMilliseconds) frameMs += entry.second;
      std::fprintf(stderr,
        "COIN_RENDER_PHASE rtt_npot_mips_gpu levels=%zu area_resolved=%zu area_ms=",
        mipFrames.size(), mipGpuMilliseconds.size());
      if (mipGpuMilliseconds.size() == mipFrames.size())
        std::fprintf(stderr, "%.6f", areaMs);
      else
        std::fprintf(stderr, "unavailable");
      std::fprintf(stderr, " frame_resolved=%zu frame_ms=",
                   mipFrameMilliseconds.size());
      if (mipFrameMilliseconds.size() == mipFrames.size())
        std::fprintf(stderr, "%.6f", frameMs);
      else
        std::fprintf(stderr, "unavailable");
      for (const auto & entry : mipGpuMilliseconds)
        std::fprintf(stderr, " level%u_ms=%.6f", entry.first, entry.second);
      std::fprintf(stderr, "\n");
    }
    bgfx::resetView(this->viewBase);
    bgfx::resetView(this->viewBase + 1);
  }
  for (size_t i = 0; i < textures.size(); ++i)
    if (owned[i] && bgfx::isValid(textures[i])) bgfx::destroy(textures[i]);
  if (bgfx::isValid(vb)) bgfx::destroy(vb);
  if (bgfx::isValid(ib)) bgfx::destroy(ib);
  if (!cachedResource) {
    DirectTextureResource resource;
    resource.token = ++this->directTextureSerial;
    resource.producerKey = producerKey;
    resource.width = size[0];
    resource.height = size[1];
    resource.format=frame.outputColorFormat; resource.mipmapped=frame.outputMipmaps;
    resource.frameBuffer = output;
    resource.depthSampleable = strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS;
    resource.inUse = true;
    this->directTextures.push_back(resource);
    token = resource.token;
  } else {
    if (cachedResource->frameBuffer.idx != output.idx &&
        bgfx::isValid(cachedResource->frameBuffer))
      bgfx::destroy(cachedResource->frameBuffer);
    cachedResource->width = size[0];
    cachedResource->height = size[1];
    cachedResource->format=frame.outputColorFormat;cachedResource->mipmapped=frame.outputMipmaps;
    cachedResource->frameBuffer = output;
    if (newOutput)
      cachedResource->depthSampleable =
        strategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS;
    cachedResource->inUse = true;
    token = cachedResource->token;
  }
  logicalSubmitSucceeded();
  return CoinRenderSubmitResult(CoinRenderBackendStatus::SUCCESS, "", ++this->serial);
}

CoinRenderSubmitResult CoinBgfxBackend::assembleCubeRtt(const std::array<uint64_t,6> & faces,
    uint32_t n,bool mips,CoinRenderTargetP &,uint64_t & token)
{
  token=0;CoinRenderTextureImageSnapshot description;
  if(!this->initialized || !this->onApiThread())return {CoinRenderBackendStatus::NOT_READY,"Cube assembly needs the live BGFX API thread"};
  if(!CoinRenderCubeTextureCore::describe(n,mips,description))return {CoinRenderBackendStatus::UNSUPPORTED,"Invalid cube atlas layout"};
  const auto * caps=bgfx::getCaps();
  const uint64_t flags=BGFX_TEXTURE_RT|BGFX_TEXTURE_BLIT_DST|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
    BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
  if(!caps || !caps->limits.maxBlits ||
     description.width>caps->limits.maxTextureSize || description.height>caps->limits.maxTextureSize ||
     !bgfx::isTextureValid(1,false,1,bgfx::TextureFormat::RGBA8,flags))
    return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX cube atlas clear/copy extent or format unavailable"};
  std::array<bgfx::TextureHandle,6> sources;
  for(size_t face=0;face<6;++face) {
    sources[face]=BGFX_INVALID_HANDLE;
    for(size_t previous=0;previous<face;++previous)if(faces[previous]==faces[face])
      return {CoinRenderBackendStatus::UNSUPPORTED,"Cube faces alias the same GPU resource"};
    for(const auto & resource:this->directTextures)
      if(resource.token==faces[face] && resource.inUse && !resource.cubeFaceSize &&
         resource.width==int(n) && resource.height==int(n) && resource.format==CoinRenderTextureFormat::RGBA8_LINEAR &&
         (!mips||resource.mipmapped))sources[face]=bgfx::getTexture(resource.frameBuffer,0);
    if(!bgfx::isValid(sources[face]))return {CoinRenderBackendStatus::UNSUPPORTED,"Cube face token is stale, mismatched or from another owner"};
  }
  const auto color=bgfx::createTexture2D(uint16_t(description.width),uint16_t(description.height),false,1,bgfx::TextureFormat::RGBA8,flags);
  bgfx::FrameBufferHandle output=BGFX_INVALID_HANDLE;
  if(bgfx::isValid(color))output=bgfx::createFrameBuffer(1,&color,true);
  if(!bgfx::isValid(output)) {
    if(bgfx::isValid(color))bgfx::destroy(color);
    return {CoinRenderBackendStatus::OUT_OF_MEMORY,"Cannot allocate cube atlas candidate"};
  }
  // Clear in its own ordered resource frame: BGFX executes blits before the
  // draws/clear of a view, so a same-view clear would erase copied face data.
  bgfx::resetView(this->viewBase);bgfx::setViewFrameBuffer(this->viewBase,output);
  bgfx::setViewRect(this->viewBase,0,0,uint16_t(description.width),uint16_t(description.height));
  bgfx::setViewClear(this->viewBase,BGFX_CLEAR_COLOR,0);bgfx::touch(this->viewBase);bgfx::frame();
  bgfx::resetView(this->viewBase);
  uint64_t levelOffset=0;uint32_t extent=n,level=0,blits=0;
  for(;;) {
    for(uint32_t face=0;face<6;++face)for(uint32_t y=0;y<extent;++y) {
      uint32_t x=0;
      while(x<extent) {
        const uint64_t linear=levelOffset+uint64_t(face)*extent*extent+uint64_t(y)*extent+x;
        const uint32_t dx=uint32_t(linear%description.width),dy=uint32_t(linear/description.width);
        const uint32_t count=std::min(extent-x,description.width-dx);
        const uint32_t sy=caps->originBottomLeft?y:extent-1-y;
        bgfx::TextureRegion destination,source;
        destination.init(color,uint16_t(dx),uint16_t(dy),uint16_t(count),1);
        destination.depth=1;
        source.init(sources[face],uint16_t(x),uint16_t(sy),uint16_t(count),1);
        source.mip=uint8_t(level);source.depth=1;
        bgfx::blit(this->viewBase,destination,source);
        x+=count;
        if(++blits==caps->limits.maxBlits){bgfx::frame();blits=0;}
      }
    }
    levelOffset+=6*uint64_t(extent)*extent;
    if(!mips||extent==1)break;extent/=2;++level;
  }
  if(blits)bgfx::frame();
  bgfx::frame(BGFX_FRAME_FLUSH);
  const auto status=this->checkRuntimeFailure("BGFX cube atlas assembly failed");
  if(status!=CoinRenderBackendStatus::SUCCESS){bgfx::destroy(output);return {status,this->lastError};}
  DirectTextureResource resource;resource.token=++this->directTextureSerial;
  resource.width=description.width;resource.height=description.height;resource.frameBuffer=output;
  resource.cubeFaceSize=n;resource.cubeMipmapped=mips;resource.inUse=true;
  this->directTextures.push_back(resource);token=resource.token;
  return {};
}

void
CoinBgfxBackend::retireRtt(const std::vector<uint64_t> & tokens)
{
  if (!this->initialized || !this->onApiThread()) return;
  if (this->checkRuntimeFailure("BGFX shared renderer failed before candidate retirement") !=
      CoinRenderBackendStatus::SUCCESS) return;
  for (auto it=this->directTextures.begin();it!=this->directTextures.end();) {
    if (std::find(tokens.begin(),tokens.end(),it->token)==tokens.end()) { ++it; continue; }
    // bgfx::destroy queues ordered destruction and defers handle reuse. The
    // framebuffer owns its attachments; no wait/frame or other token changes.
    if (bgfx::isValid(it->frameBuffer)) bgfx::destroy(it->frameBuffer);
    it=this->directTextures.erase(it);
  }
}

void
CoinBgfxBackend::releaseDirectTexture(uint64_t token)
{
  for (std::vector<DirectTextureResource>::iterator it = this->directTextures.begin();
       it != this->directTextures.end(); ++it) {
    if (it->token != token) continue;
    it->inUse = false;
    return;
  }
}

void
CoinBgfxBackend::finishDirectTextures(const std::vector<uint64_t> & usedTokens)
{
  if (!this->initialized || !this->onApiThread()) return;
  // finishRtt ends the whole transaction, including STAGED rollback before
  // the root submit. Subset retirement must keep the graph's pins intact.
  this->releasePortablePreflightPins();
  if (this->checkRuntimeFailure("BGFX shared renderer failed before RTT retirement") !=
      CoinRenderBackendStatus::SUCCESS) return;
  for (std::vector<DirectTextureResource>::iterator it = this->directTextures.begin();
       it != this->directTextures.end();) {
    if (std::find(usedTokens.begin(), usedTokens.end(), it->token) == usedTokens.end()) {
      if (bgfx::isValid(it->frameBuffer)) bgfx::destroy(it->frameBuffer);
      it = this->directTextures.erase(it);
    } else {
      it->inUse = false;
      ++it;
    }
  }
}

CoinRenderTarget::ReadbackStatus
CoinBgfxBackend::pollReadback(const CoinRenderReadbackTicket & ticket,
  std::vector<uint8_t> & color, std::vector<float> & depth, SbString * diagnostic)
{
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  auto lost = lostReadbacks.find(ticket.token);
  if (lost != lostReadbacks.end() && sameTicket(lost->second.ticket, ticket)) {
    if (lost->second.thread != std::this_thread::get_id())
      return CoinRenderTarget::READBACK_ERROR;
    if (diagnostic)
      *diagnostic = "Readback belongs to a retired device generation";
    return CoinRenderTarget::READBACK_DEVICE_LOST;
  }
  auto found = asyncEntries().find(ticket.token);
  if (found == asyncEntries().end() || !sameTicket(found->second->ticket, ticket)) {
    if (diagnostic) *diagnostic = "Invalid or consumed BGFX readback ticket";
    return CoinRenderTarget::READBACK_INVALID_TICKET;
  }
  if (runtime.apiThread != std::this_thread::get_id()) {
    if (diagnostic) *diagnostic = "BGFX readback must be polled on its API thread";
    return CoinRenderTarget::READBACK_ERROR;
  }
  auto * cb = static_cast<CoinBgfxCallback *>(runtime.callback.get());
  if (cb->failed()) {
    if (diagnostic) *diagnostic = cb->diagnostic().c_str();
    return cb->deviceLost() ? CoinRenderTarget::READBACK_DEVICE_LOST : CoinRenderTarget::READBACK_ERROR;
  }
  const uint32_t completed = bgfx::frame();
  // The render worker may report a fatal while frame() waits for it. Do not
  // publish (or normalize) staging after that failure, even if its frame ID
  // would otherwise look complete.
  if (cb->failed()) {
    if (diagnostic) *diagnostic = cb->diagnostic().c_str();
    return cb->deviceLost() ? CoinRenderTarget::READBACK_DEVICE_LOST : CoinRenderTarget::READBACK_ERROR;
  }
  auto & entry = *found->second;
  if (static_cast<int32_t>(completed - entry.slot.readyFrame) < 0)
    return CoinRenderTarget::READBACK_NOT_READY;
  if (entry.bottomLeft) {
    CoinRenderImageCore::flipRgba8Rows(entry.slot.pixels, SbVec2i32(ticket.width, ticket.height));
    for (uint32_t y = 0; !entry.slot.depth.empty() && y < ticket.height / 2; ++y)
      std::swap_ranges(entry.slot.depth.begin() + y * ticket.width,
        entry.slot.depth.begin() + (y + 1) * ticket.width,
        entry.slot.depth.begin() + (ticket.height - y - 1) * ticket.width);
  }
  color.swap(entry.slot.pixels); depth.swap(entry.slot.depth);
  releaseAsync(ticket.token);
  return CoinRenderTarget::READBACK_READY;
}

bool
CoinBgfxBackend::cancelReadback(const CoinRenderReadbackTicket & ticket)
{
  auto & runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  auto lost = lostReadbacks.find(ticket.token);
  if (lost != lostReadbacks.end() && sameTicket(lost->second.ticket, ticket)) {
    if (lost->second.thread != std::this_thread::get_id())
      return false;
    lostReadbacks.erase(lost);
    return true;
  }
  auto found = asyncEntries().find(ticket.token);
  if (found == asyncEntries().end() || !sameTicket(found->second->ticket, ticket) ||
      runtime.apiThread != std::this_thread::get_id()) return false;
  auto * cb = static_cast<CoinBgfxCallback *>(runtime.callback.get());
  if (cb->failed()) {
    // Shutdown is the only safe retirement fence after device loss.
    releaseAsync(ticket.token, true);
    return true;
  }
  uint32_t completed = bgfx::frame();
  for (int attempts = 0; !cb->failed() &&
       static_cast<int32_t>(completed - found->second->slot.readyFrame) < 0 && attempts < 16; ++attempts)
    completed = bgfx::frame();
  if (cb->failed()) {
    // Keep CPU destinations alive until the existing shutdown retirement
    // fence; another frame cannot establish completion after a fatal.
    releaseAsync(ticket.token, true);
    return true;
  }
  if (static_cast<int32_t>(completed - found->second->slot.readyFrame) < 0) return false;
  releaseAsync(ticket.token);
  return true;
}

bool CoinBgfxBackend::readbackLoad(uint64_t& jobs, uint64_t& bytes) const {
  auto& runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  jobs = asyncEntries().size();
  bytes = 0;
  for (const auto& item : asyncEntries())
    bytes += item.second->ticket.colorBytes + item.second->ticket.depthBytes;
  return true;
}
void CoinBgfxBackend::retireLostReadbacks() {
  auto& runtime = sharedRuntime();
  std::lock_guard<std::mutex> guard(runtime.mutex);
  while (!asyncEntries().empty()) {
    const auto entry = asyncEntries().begin()->second;
    lostReadbacks.emplace(entry->ticket.token, LostReadback{entry->ticket, runtime.apiThread});
    releaseAsync(entry->ticket.token, true);
  }
}
void
CoinBgfxBackend::poll()
{
  // Synchronous readback in submit() already advances BGFX frames.
}

CoinRenderSubmitResult CoinBgfxBackend::preflightRtt(const CoinRenderRttPlan& graph,
                                                     const CoinRenderFramePlan& root,
                                                     const SbVec2i32& size) const {
  uint64_t shaderBytes = 0;
  std::string shaderDiagnostic;
  if (!graph.shaderResourceBytes(root, shaderBytes, shaderDiagnostic))
    return {CoinRenderBackendStatus::UNSUPPORTED, shaderDiagnostic};
  if(std::any_of(graph.producers.begin(),graph.producers.end(),[](const CoinRenderRttProducer & p){return p.cube;})) {
    uint64_t nominal=0;std::string diagnostic;
    if(!graph.cubeNominalBytes(root,this->sampleCount,nominal,diagnostic))return {CoinRenderBackendStatus::UNSUPPORTED,diagnostic};
    const auto * caps=this->initialized?bgfx::getCaps():nullptr;
    for(size_t i=0;i<graph.producers.size();++i)if(graph.producers[i].cube) {
      CoinRenderTextureImageSnapshot atlas;
      if(!CoinRenderCubeTextureCore::describe(uint32_t(graph.producers[i].size[0]),graph.requestsMips(i+1,root),atlas))
        return {CoinRenderBackendStatus::UNSUPPORTED,"Invalid cube atlas dimensions"};
      const uint64_t atlasFlags=BGFX_TEXTURE_RT|BGFX_TEXTURE_BLIT_DST|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
        BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
      if(caps && (atlas.width>caps->limits.maxTextureSize || atlas.height>caps->limits.maxTextureSize ||
          (graph.mode==COIN_RENDER_SCENE_TEXTURE_DIRECT && (!caps->limits.maxBlits ||
            !bgfx::isTextureValid(1,false,1,bgfx::TextureFormat::RGBA8,atlasFlags)))))
        return {CoinRenderBackendStatus::UNSUPPORTED,"Cube atlas exceeds BGFX copy/texture capabilities"};
    }
    const auto expanded=graph.inspection(root);return this->preflightRtt(expanded,root,size);
  }
  auto admission = bgfxTextureProfile(root);
  if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
  admission = this->msaaProfile(root);
  if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
  for (const auto & producer : graph.producers) {
    admission = bgfxTextureProfile(producer.plan);
    if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
    admission = this->msaaProfile(producer.plan);
    if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
    if (this->sampleCount > 1 && producer.format != CoinRenderTextureFormat::RGBA8_LINEAR)
      return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX MSAA RTT requires RGBA8 linear output"};
  }
  const auto bumpViewport=[](const CoinRenderFramePlan & frame,const SbVec2i32 & size) {
    const bool active=std::any_of(frame.bumpCoords.begin(),frame.bumpCoords.end(),[](const CoinRenderBumpCoordOverride & b){return b.imageSlot!=0;});
    if(!active)return true;
    for(const auto & draw:frame.draws) {
      if (draw.shadowLightSlot)
        continue;
      const auto & viewport=frame.viewports[frame.renderStates[draw.renderStateSlot].viewportSlot];
      if(viewport.x!=0 || viewport.y!=0 || viewport.width!=size[0] || viewport.height!=size[1])return false;
    }
    return true;
  };
  if(!bumpViewport(root,size))return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX bump requires the full target viewport"};
  for(const auto & producer:graph.producers)if(!bumpViewport(producer.plan,producer.size))
    return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX bump producer requires its full target viewport"};
  bool multipassProgramsNeeded = root.numPasses > 1;
  for (const auto & producer : graph.producers)
    multipassProgramsNeeded = multipassProgramsNeeded || producer.plan.numPasses > 1;
  if (this->initialized && multipassProgramsNeeded &&
      !const_cast<CoinBgfxBackend *>(this)->prepareMultipassPrograms())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  const auto prepareBumpFrame = [this](const CoinRenderFramePlan &frame, bool windowTarget) {
    if (!this->initialized || !bgfxFrameProgramNeeds(frame).bump)
      return true;
    auto *backend = const_cast<CoinBgfxBackend *>(this);
    if (!coin_render_uses_extended_bump(frame))
      return backend->prepareBumpPrograms();
    std::vector<CoinRenderBumpGpuVertex> stream;
    if (!coin_render_build_bump_gpu_stream(frame, stream, backend->lastError))
      return false;
    for (const auto &source : frame.draws) {
      if (source.shadowLightSlot || !source.geometry.indexCount || stream.empty())
        continue;
      if (source.geometry.firstIndex >= frame.indices.size() ||
          frame.indices[source.geometry.firstIndex] >= stream.size()) {
        backend->lastError = "Invalid bump receiver capability geometry range";
        return false;
      }
      if (!stream[frame.indices[source.geometry.firstIndex]].imageSlot)
        continue;
      const auto &state = frame.renderStates[source.renderStateSlot];
      CoinBgfxDraw draw{};
      draw.depthTest = state.depthTest;
      draw.depthWrite = state.depthWrite;
      draw.depthFunction = state.depthFunction;
      draw.colorWrite = state.colorWrite;
      draw.colorWriteMask = state.colorWriteMask;
      draw.cullMode = state.cullMode;
      draw.frontFace = state.frontFace;
      if (!backend->prepareBumpReceiverPrograms(draw, windowTarget,
                                              coinBgfxTextureFormat(frame.outputColorFormat)))
        return false;
    }
    return true;
  };
  if (!prepareBumpFrame(root, this->presentToWindow && root.numPasses == 1))
    return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  for (const auto &producer : graph.producers)
    if (!prepareBumpFrame(producer.plan, false))
      return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
  const auto prepareImages = [this](const CoinRenderFramePlan &frame) {
    return !this->initialized || !std::any_of(frame.renderStates.begin(), frame.renderStates.end(),
      [](const CoinRenderRenderStateSnapshot & state) { return state.rasterTextureSourceSlot != UINT32_MAX; }) ||
      const_cast<CoinBgfxBackend *>(this)->prepareRasterImageResources();
  };
  if (!prepareImages(root)) return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  for (const auto & producer : graph.producers)
    if (!prepareImages(producer.plan)) return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  const auto needsWeighted = [](const CoinRenderFramePlan & plan) {
    std::vector<CoinRenderCompositionItem> order;
    std::string diagnostic;
    if (!coin_render_composition_order(plan, order, diagnostic)) return false;
    return std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
      return item.blend && item.deferred && !item.additive &&
        item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT;
    });
  };
  const auto needsPeeling = [](const CoinRenderFramePlan & plan) {
    std::vector<CoinRenderCompositionItem> order;
    std::string diagnostic;
    if (!coin_render_composition_order(plan, order, diagnostic)) return false;
    return std::any_of(order.begin(), order.end(), [](const CoinRenderCompositionItem & item) {
      return item.blend && item.deferred && !item.additive &&
        item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS;
    });
  };
  bool peeling = needsPeeling(root);
  for (const auto & producer : graph.producers) peeling = peeling || needsPeeling(producer.plan);
  bool weighted = needsWeighted(root);
  for (const auto & producer : graph.producers) weighted = weighted || needsWeighted(producer.plan);
  // The second graph preflight follows prepare(), when this target's sample
  // count and readback policy are known. Reject the entire graph before the
  // first producer can encode a scene or replace a cached RTT image.
  if (this->initialized) {
    std::string diagnostic;
    const auto * caps = bgfx::getCaps();
    const uint32_t outputViews = this->presentToWindow ?
      (caps && caps->rendererType == bgfx::RendererType::OpenGL ? 1 : 0) :
      (this->preparedDepthReadbackEnabled ? 2 : 1);
    uint32_t rootViews = 0, producerViews = 0, largestProducerViews = 0;
    if (!bgfxMsaaFrameViewBudget(root, size[0], size[1], outputViews, this->sampleCount, false, diagnostic, &rootViews))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
    for (const auto & producer : graph.producers) {
      // STAGED renders to its own synchronous offscreen target; DIRECT resolves
      // into the producer texture without a color readback/blit view.
      const uint32_t producerOutputViews = graph.mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? 0 : 1;
      if (!bgfxMsaaFrameViewBudget(producer.plan, producer.size[0], producer.size[1],
            producerOutputViews, this->sampleCount, graph.mode == COIN_RENDER_SCENE_TEXTURE_DIRECT, diagnostic, &producerViews))
        return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
      largestProducerViews = std::max(largestProducerViews,producerViews);
    }
    if (!this->canReserveGraphViews(rootViews,largestProducerViews,
        graph.mode == COIN_RENDER_SCENE_TEXTURE_STAGED && !graph.producers.empty(),diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED,diagnostic};
    if (peeling && !bgfxPeelingSamples(this->sampleCount, this->layout, this->viewBase, diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
    if (weighted && !bgfxWeightedSamples(this->sampleCount, this->layout, this->viewBase, diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
  }
  const auto shadowMultitexturePreflight = [&](const CoinRenderFramePlan & frame) -> CoinRenderSubmitResult {
    if (!bgfxShadowMultitexture(frame)) return {};
    CoinRenderShadowPlan plan; std::string diagnostic;
    if (!coin_render_plan_shadows(frame, plan, diagnostic) || !coin_render_shadow_object_profile(frame, plan, plan.passes.size(), diagnostic))
      return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
    const bool ordinary = bgfxFrameProgramNeeds(frame).ordinary;
    const bool customDepth = this->sampleCount > 1 && std::any_of(frame.renderStates.begin(), frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot & state) {
      return state.depthRange[0] != 0.0f || state.depthRange[1] != 1.0f ||
        (state.polygonOffsetEnabled && (state.polygonOffsetStyles & state.polygonOffsetPrimitiveStyle) &&
         (state.polygonOffsetFactor != 0.0f || state.polygonOffsetUnits != 0.0f || state.polygonOffsetSlopeBias != 0.0f));
    });
    if (this->initialized && ordinary &&
        (!const_cast<CoinBgfxBackend *>(this)->prepareShadowMultitextureProgram() ||
         (customDepth &&
          !const_cast<CoinBgfxBackend *>(this)->prepareShadowMultitextureProgram(true))))
      return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
    if(!coin_render_shadow_array_budget(plan,2,diagnostic))return {CoinRenderBackendStatus::UNSUPPORTED,diagnostic};
    std::vector<CoinRenderCompositionItem> order;
    if(!coin_render_composition_order(frame,order,diagnostic))return {CoinRenderBackendStatus::UNSUPPORTED,diagnostic};
    const bool peel=std::any_of(order.begin(),order.end(),[](const CoinRenderCompositionItem &item){return item.blend&&item.deferred&&!item.additive&&item.transparencyStrategy==CoinRenderCompositionItem::SORTED_LAYERS;});
    const bool weighted=std::any_of(order.begin(),order.end(),[](const CoinRenderCompositionItem &item){return item.blend&&item.deferred&&!item.additive&&item.transparencyStrategy==CoinRenderCompositionItem::WEIGHTED_OIT;});
    if (this->initialized && ordinary &&
        ((peel &&
          !const_cast<CoinBgfxBackend *>(this)->prepareShadowMultitextureProgram(false, 1)) ||
         (weighted &&
          (!const_cast<CoinBgfxBackend *>(this)->prepareShadowMultitextureProgram(false, 2) ||
           (this->sampleCount > 1 &&
            !const_cast<CoinBgfxBackend *>(this)->prepareShadowMultitextureProgram(true))))))
      return {CoinRenderBackendStatus::UNSUPPORTED,this->lastError};
    return {};
  };
  admission = shadowMultitexturePreflight(root); if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
  for (const auto & producer : graph.producers) {
    admission = shadowMultitexturePreflight(producer.plan); if (admission.status != CoinRenderBackendStatus::SUCCESS) return admission;
  }
  // DIRECT-only allocation and mip capabilities do not constrain STAGED.
  // Both modes still reach the whole-graph module preparation below.
  if (graph.mode == COIN_RENDER_SCENE_TEXTURE_DIRECT) {
    for (size_t producerIndex=0;producerIndex<graph.producers.size();++producerIndex) {
      const auto& producer=graph.producers[producerIndex];
      const bool requestsMips=graph.requestsMips(producerIndex+1,root);
      const bool manualMips=requestsMips &&
        (!CoinRenderTextureSamplingCore::powerOfTwo(producer.size[0]) ||
         !CoinRenderTextureSamplingCore::powerOfTwo(producer.size[1]));
      const auto format=coinBgfxTextureFormat(producer.format);
      const auto * caps=sharedRuntime().references ? bgfx::getCaps() : nullptr;
      if(caps && (!(caps->formats[format]&BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
         (requestsMips && !manualMips && !(caps->formats[format]&BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN))))
        return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX RTT format/mip generation unavailable"};
      if(caps && manualMips &&
         !bgfx::isTextureValid(1, false, 1, format,
           BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
           BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT))
        return {CoinRenderBackendStatus::UNSUPPORTED,"BGFX NPOT mip GPU blit destination unavailable"};
      if (!producer.plan.shadowGroups.empty()) {
        CoinRenderShadowPlan shadowPlan;
        std::string diagnostic;
        if (!coin_render_plan_shadows(producer.plan, shadowPlan, diagnostic) ||
            !coin_render_shadow_object_profile(producer.plan, shadowPlan,
              shadowPlan.passes.size(), diagnostic))
          return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
        if (!bgfxShadowBatchSupported(producer.plan, shadowPlan, diagnostic))
          return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
        const bgfx::Caps * caps = sharedRuntime().references ? bgfx::getCaps() : nullptr;
        if (caps && caps->limits.maxTextureSamplers < (bgfxShadowMultitexture(producer.plan)?11:shadowPlan.passes.size() > 4 ? 16 : shadowPlan.passes.size() > 2 ? 12 : 10))
          return {CoinRenderBackendStatus::UNSUPPORTED,
                  "BGFX direct RTT shadow receiver has insufficient texture stages"};
        for (const auto & pass : shadowPlan.passes)
          if (caps && (pass.mapSize > caps->limits.maxTextureSize ||
              !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
              !(caps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
              !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, shadowTextureFlags)))
            return {CoinRenderBackendStatus::UNSUPPORTED,
                    "BGFX direct RTT cannot render the planned shadow maps"};
      }
      std::vector<CoinRenderCompositionItem> order;
      std::string diagnostic;
      if (!coin_render_composition_order(producer.plan, order, diagnostic))
        return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
      bool peeling = false, weighted = false;
      for (const auto& item : order) {
        // The Coin mode can name peeling on an opaque draw; Core only defers
        // effective transparency. Match the execution strategy's admission.
        if (!item.blend || !item.deferred) continue;
        peeling = peeling || item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS;
        weighted = weighted || item.transparencyStrategy == CoinRenderCompositionItem::WEIGHTED_OIT;
      }
      // The graph is inspected once before prepareBackend() and once after it.
      // A peer target may already own the shared runtime, but this backend's
      // capability flags are populated only when this target is prepared.
      if (this->initialized &&
          ((peeling && !this->sortedLayersSupported) ||
           (weighted && !this->weightedOitSupported)))
        return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX direct RTT transparency is unavailable"};
      auto options = producer.plan.transparency;
      if (weighted && !peeling) options.layers = 1;
      uint64_t requiredBytes = 0;
      if (!coin_render_transparency_sample_budget(producer.size[0], producer.size[1], options,
            peeling || weighted, this->sampleCount, requiredBytes, diagnostic))
        return {CoinRenderBackendStatus::UNSUPPORTED, diagnostic};
    }
  }
  // Compile every consumed module before the first producer can publish or
  // submit. Lowering here owns only CPU snapshots; no texture upload, scene
  // draw, presentation or readback is issued by this preparation.
  if (this->initialized) {
    auto *backend = const_cast<CoinBgfxBackend *>(this);
    backend->releasePortablePreflightPins();
    PortablePreflightRelease releaseOnFailure{backend};
    const auto prepareModules = [&](const CoinRenderFramePlan &frame,
                                    const SbVec2i32 &extent, bool window, bgfx::TextureFormat::Enum colorFormat) {
      if (!frame.hasPortableShaderDraws()) return true;
      CoinBgfxPlan plan;
      const bool shadows = !frame.shadowGroups.empty();
      if (!CoinBgfxLowering::lowerPortablePreflight(frame, extent[0], extent[1], bgfx::getCaps()->homogeneousDepth,
                                                  plan, backend->lastError, shadows)) return false;
      CoinBgfxTransparencyStrategy strategy;
      if (!CoinBgfxLowering::selectTransparencyStrategy(plan.draws, backend->transparencyMode,
            backend->weightedOitSupported, backend->sortedLayersSupported, strategy, backend->lastError)) return false;
      if (!backend->prepareTransparencyPrograms(strategy, bgfxNeedsFixedSurfacePrograms(plan.draws)) ||
          !backend->prepareBaseProgram(plan.draws, shadows, strategy, false, window && frame.numPasses == 1,
                                      colorFormat)) return false;
      return backend->preparePortablePrograms(frame, plan.draws, shadows, strategy, window && frame.numPasses == 1, colorFormat, true);
    };
    if (!prepareModules(root, size, this->presentToWindow, coinBgfxTextureFormat(root.outputColorFormat)))
      return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
    for (const auto &producer : graph.producers)
      if (!prepareModules(producer.plan, producer.size, false, coinBgfxTextureFormat(producer.format)))
        return {CoinRenderBackendStatus::UNSUPPORTED, this->lastError};
    releaseOnFailure.owner = nullptr;
  }

  return {};
}
