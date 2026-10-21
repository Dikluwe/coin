#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include "actions/CoinRenderActionP.h"
#include "glue/simage_wrapper.h"

#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>
#include <cstdio>
#ifdef __linux__
#include <unistd.h>
#endif

namespace {
bool check(bool condition, const char * message) {
  if (!condition) std::cerr << "CoinRenderProductTest: " << message << '\n';
  return condition;
}
class ManagerGLAction : public SoGLRenderAction {
public:
  ManagerGLAction(SoRenderManager & source, const SbViewportRegion & viewport)
    : SoGLRenderAction(viewport), manager(source) {}
  void apply(SoNode * root) override {
    if (insideManager) {
      SoGLRenderAction::apply(root);
      return;
    }
    insideManager = true;
    manager.render(this, TRUE, TRUE, TRUE);
    insideManager = false;
  }
private:
  SoRenderManager & manager;
  bool insideManager = false;
};
}

int main() {
  SoDB::init();
  CoinRenderAction::initClass();

  CoinRenderCapabilities caps{};
  unsigned char untouched[sizeof(caps)];
  std::memset(untouched, 0x5a, sizeof(untouched));
  if (!check(coin_render_query_capabilities(
               COIN_RENDER_EXPERIMENTAL_OFFSCREEN, nullptr, sizeof(caps)) == 2,
             "null capability output was accepted") ||
      !check(coin_render_query_capabilities(
               COIN_RENDER_EXPERIMENTAL_OFFSCREEN, untouched, sizeof(untouched) - 1) == 2 &&
             untouched[0] == 0x5a,
             "short capability output was accepted or overwritten") ||
      !check(coin_render_query_capabilities(999, untouched, sizeof(untouched)) == 1 &&
             untouched[0] == 0x5a,
             "unknown target was accepted or overwrote output")) return 1;

  CoinRenderCapabilities legacyCaps{};
  const size_t legacyCapsSize = offsetof(CoinRenderCapabilities, probe_status);
  if (!check(coin_render_query_capabilities(
               COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &legacyCaps, legacyCapsSize) == 0 &&
             legacyCaps.struct_size == legacyCapsSize && legacyCaps.version == 1,
             "version 1 capability prefix is not compatible")) return 1;

#if defined(HAVE_COIN_BGFX)
  if (!check(coin_render_query_capabilities(
               COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) == 0 &&
             caps.struct_size == sizeof(caps) &&
             caps.version == COIN_RENDER_CAPABILITIES_VERSION &&
             caps.target == COIN_RENDER_EXPERIMENTAL_OFFSCREEN &&
             (caps.features & COIN_RENDER_FEATURE_COLOR_DEPTH) != 0 &&
             (caps.features & COIN_RENDER_FEATURE_LINES_POINTS) != 0 &&
             (caps.features & COIN_RENDER_FEATURE_TEXTURE_2D) != 0 &&
             (caps.features & COIN_RENDER_FEATURE_LIGHTS) != 0 &&
             caps.max_lights_per_draw == 8 && caps.max_texture_units == 8 &&
             (caps.features & COIN_RENDER_FEATURE_FOG) != 0 &&
             caps.max_scene_texture_depth == 0 &&
             caps.gpu_available == 1 &&
             caps.probe_status == COIN_RENDER_PROBE_AVAILABLE &&
             caps.renderer != COIN_RENDER_RENDERER_UNKNOWN &&
             caps.adapter_name[0] != '\0' && caps.diagnostic[0] != '\0' &&
             caps.max_framebuffer_attachments >= 1 &&
             (caps.format_rgba8 & COIN_RENDER_FORMAT_FRAMEBUFFER) != 0 &&
             (((caps.runtime_features & COIN_RENDER_RUNTIME_MRT) != 0) ==
              (caps.max_framebuffer_attachments > 1)),
             "BGFX runtime capability probe is incomplete")) return 1;
#else
  if (!check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps,
                                            sizeof(caps)) == 0 &&
                 caps.struct_size == sizeof(caps) &&
                 caps.version == COIN_RENDER_CAPABILITIES_VERSION &&
                 caps.target == COIN_RENDER_EXPERIMENTAL_OFFSCREEN &&
                 (caps.features & COIN_RENDER_FEATURE_COLOR_DEPTH) != 0 &&
                 caps.max_lights_per_draw == 8 && caps.max_texture_units == 8 &&
                 caps.max_scene_texture_depth == 8 &&
                 caps.max_scene_texture_bytes_per_apply == UINT64_C(64) * 1024 * 1024,
             "offscreen capability profile is incomplete"))
    return 1;
#endif

  CoinRenderCapabilities windowCaps{};
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE) && defined(__linux__)
  if (!check(caps.backend == COIN_RENDER_EXPERIMENTAL_RUST &&
             (caps.features & COIN_RENDER_FEATURE_ASYNC_READBACK) != 0 &&
             (caps.features & COIN_RENDER_FEATURE_DIRECT_RTT) != 0 &&
             coin_render_query_capabilities(
               COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW, &windowCaps, sizeof(windowCaps)) == 0 &&
             windowCaps.target == COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW &&
             (windowCaps.features & COIN_RENDER_FEATURE_ASYNC_READBACK) == 0 &&
             (windowCaps.features & COIN_RENDER_FEATURE_DIRECT_RTT) == 0,
             "Rust target capability split is wrong")) return 1;
#elif defined(HAVE_COIN_BGFX) && defined(__linux__)
  if (!check(coin_render_query_capabilities(
               COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW, &windowCaps, sizeof(windowCaps)) == 0 &&
             windowCaps.backend == COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION &&
             windowCaps.gpu_available == 1 &&
             windowCaps.probe_status == COIN_RENDER_PROBE_AVAILABLE &&
             std::strstr(windowCaps.diagnostic, "Xlib presentation requires") != nullptr,
             "BGFX Xlib capability probe is incomplete")) return 1;
#else
  if (!check(coin_render_query_capabilities(
               COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW, &windowCaps, sizeof(windowCaps)) == 1,
             "unsupported window target was advertised")) return 1;
#endif

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  if (!caps.gpu_available) {
    std::cout << "No WebGPU adapter; manager rendering skipped\n";
    return 0;
  }
#elif defined(HAVE_COIN_BGFX)
  const char * rendererEnv = std::getenv("COIN_BGFX_RENDERER");
  uint32_t expectedRenderer = rendererEnv != nullptr &&
    std::strcmp(rendererEnv, "opengl") == 0 ?
      COIN_RENDER_RENDERER_OPENGL : COIN_RENDER_RENDERER_VULKAN;
  if (rendererEnv && std::strcmp(rendererEnv, "d3d12") == 0)
    expectedRenderer = COIN_RENDER_RENDERER_D3D12;
#ifdef _WIN32
  if (!rendererEnv) expectedRenderer = COIN_RENDER_RENDERER_D3D12;
#endif
  if (!check(caps.backend == COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION &&
             caps.gpu_available == 1 && caps.renderer == expectedRenderer &&
             (caps.features & COIN_RENDER_FEATURE_ASYNC_READBACK) != 0 &&
             (caps.features & COIN_RENDER_FEATURE_DIRECT_RTT) == 0,
             "BGFX runtime renderer or compiled feature report is wrong")) return 1;
#else
  if (!check(caps.backend == COIN_RENDER_EXPERIMENTAL_RECORDING &&
             caps.gpu_available == 0 &&
             (caps.features & COIN_RENDER_FEATURE_ASYNC_READBACK) == 0 &&
             (caps.features & COIN_RENDER_FEATURE_DIRECT_RTT) == 0,
             "Recording backend advertised GPU-only features")) return 1;
#endif

  std::cout << "COIN_WGPU_CAPABILITIES"
            << " backend=" << caps.backend
            << " probe_status=" << caps.probe_status
            << " renderer=" << caps.renderer
            << " vendor_id=0x" << std::hex << std::setw(4) << std::setfill('0')
            << caps.vendor_id
            << " device_id=0x" << std::setw(4) << caps.device_id
            << " max_framebuffer_attachments=" << std::dec
            << caps.max_framebuffer_attachments
            << " runtime_features=0x" << std::hex << caps.runtime_features
            << " rgba8=0x" << caps.format_rgba8
            << " d24s8=0x" << caps.format_d24s8
            << " d32f=0x" << caps.format_d32f
            << " rgba16f=0x" << caps.format_rgba16f
            << " r16f=0x" << caps.format_r16f << std::dec
            << " adapter=" << caps.adapter_name
            << " diagnostic=" << caps.diagnostic << '\n';
  {
    SoRenderManager source;
    SoSeparator * adapterRoot = new SoSeparator;
    adapterRoot->ref();
    source.setSceneGraph(adapterRoot);
    adapterRoot->unref();
    source.setViewportRegion(SbViewportRegion(8, 8));
    source.setBackgroundColor(SbColor4f(0.3f, 0.2f, 0.1f, 1.0f));

    source.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_OBJECT_BLEND);
    CoinRenderManagerAdapter adapter(source, SbVec2i32(8, 8));
    if (!check(adapter.getSceneManager()->getTransparencyType() == CoinRenderAction::SORTED_OBJECT_BLEND,
               "adapter did not inherit the host transparency policy")) return 1;
    source.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
    if (!check(adapter.syncFromRenderManager() &&
               adapter.getSceneManager()->getTransparencyType() == CoinRenderAction::BLEND,
               "adapter did not synchronize a transparency policy change")) return 1;
#if defined(HAVE_COIN_BGFX)
    adapter.getSceneManager()->getRenderTarget()->setDepthReadbackEnabled(FALSE);
#endif
    if (!check(adapter.getSceneManager() != nullptr &&
               adapter.render() == CoinRenderAction::SUCCESS,
               "SoRenderManager adapter failed to render an exposure")) return 1;
#if defined(HAVE_COIN_BGFX)
    CoinRenderCapabilities sharedCaps{};
    if (!check(coin_render_query_capabilities(
                 COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &sharedCaps, sizeof(sharedCaps)) == 0 &&
               sharedCaps.gpu_available != 0 &&
               sharedCaps.probe_status == COIN_RENDER_PROBE_AVAILABLE,
               "BGFX capability probe could not share active runtime")) return 1;
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "BGFX shared capability probe disturbed active target")) return 1;
#endif
    std::vector<uint8_t> adapterColor;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(adapterColor);
    if (!check(adapterColor.size() == 8u * 8u * 4u &&
               adapterColor[0] >= 75 && adapterColor[0] <= 77 &&
               adapterColor[1] >= 50 && adapterColor[1] <= 52 &&
               adapterColor[2] >= 24 && adapterColor[2] <= 26,
               "SoRenderManager state was not synchronized")) return 1;
    // CoinGL's setCamera() does not insert an external camera into the scene.
    // It participates in prepareFrame() autoclip, while traversal still uses
    // the scene graph. The adapter must preserve both parts of that contract.
    adapterRoot->addChild(new SoCube);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "scene with cube did not render before external camera")) return 1;
    std::vector<uint8_t> cameraBaselineColor;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(cameraBaselineColor);
    if (!check(cameraBaselineColor != adapterColor,
               "cube did not produce a visible baseline for external camera comparison")) return 1;
    SoPerspectiveCamera * externalCamera = new SoPerspectiveCamera;
    externalCamera->ref();
    externalCamera->position.setValue(0.0f, 0.0f, 5.0f);
    externalCamera->nearDistance = 0.125f;
    externalCamera->farDistance = 1234.0f;
    source.setCamera(externalCamera);
    source.setAutoClipping(SoRenderManager::FIXED_NEAR_PLANE);
    source.setNearPlaneValue(0.5f);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS &&
               externalCamera->nearDistance.getValue() > 0.5f &&
               externalCamera->farDistance.getValue() < 20.0f,
               "external manager camera was not used by autoclip")) return 1;
    std::vector<uint8_t> externalCameraColor;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(externalCameraColor);
    if (!check(externalCameraColor == cameraBaselineColor,
               "external manager camera unexpectedly changed scene traversal")) return 1;
    source.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
    source.setCamera(nullptr);
    externalCamera->unref();
    source.setViewportRegion(SbViewportRegion(16, 12));
    if (!check(adapter.resize(SbVec2i32(16, 12)) &&
               source.getViewportRegion().getViewportSizePixels() == SbVec2s(16, 12) &&
               adapter.render() == CoinRenderAction::SUCCESS,
               "SoRenderManager adapter failed its resize lifecycle")) return 1;
    source.setSceneGraph(nullptr);
  }

  {
    SoRenderManager source;
    SoSeparator * scene = new SoSeparator;
    scene->ref();
    SoPerspectiveCamera * camera = new SoPerspectiveCamera;
    camera->position.setValue(0, 0, 5);
    scene->addChild(camera);
    SoDrawStyle * authoredStyle = new SoDrawStyle;
    authoredStyle->style = SoDrawStyle::FILLED;
    scene->addChild(authoredStyle);
    SoTexture2 * authoredTexture = new SoTexture2;
    const uint8_t redTexture[] = {255, 0, 0};
    authoredTexture->image.setValue(SbVec2s(1, 1), 3, redTexture);
    authoredTexture->model = SoTexture2::REPLACE;
    scene->addChild(authoredTexture);
    SoComplexity * authoredComplexity = new SoComplexity;
    authoredComplexity->type = SoComplexity::OBJECT_SPACE;
    scene->addChild(authoredComplexity);
    scene->addChild(new SoSphere);
    source.setSceneGraph(scene);
    scene->unref();
    source.setViewportRegion(SbViewportRegion(64, 64));
    source.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    CoinRenderManagerAdapter adapter(source, SbVec2i32(64, 64));
    std::vector<uint8_t> filled, lines, points, bounds, untexturedBase, overlay, overlayBlue, texturedOverlay, hidden, texturedHidden, shadedHidden, untexturedLines;
    std::vector<uint8_t> retained, untextured, restored;
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "AS_IS manager render failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(filled);
    source.setRenderMode(SoRenderManager::WIREFRAME);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "WIREFRAME manager render failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(lines);
    source.setRenderMode(SoRenderManager::POINTS);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "POINTS manager render failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(points);
    if (!check(filled.size() == 64u * 64u * 4u && lines.size() == filled.size() &&
               points.size() == filled.size() && filled != lines && lines != points &&
               filled != points,
               "manager draw modes did not override authored FILLED style")) return 1;
    source.setRenderMode(SoRenderManager::BOUNDING_BOX);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "BOUNDING_BOX manager render failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(bounds);
    if (!check(bounds.size() == filled.size() && bounds != lines && bounds != filled,
               "manager BOUNDING_BOX did not override authored OBJECT_SPACE complexity")) return 1;
    source.setRenderMode(SoRenderManager::AS_IS);
    source.setTexturesEnabled(FALSE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "untextured overlay baseline failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(untexturedBase);
    source.setRenderMode(SoRenderManager::WIREFRAME_OVERLAY);
    source.setOverlayColor(SbColor4f(0, 1, 0, 1));
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "WIREFRAME_OVERLAY manager render failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(overlay);
    unsigned greenEdges = 0;
    for (size_t pixel = 0; pixel + 3 < overlay.size(); pixel += 4) {
      if (overlay[pixel + 1] > 150 && overlay[pixel + 1] > overlay[pixel] * 2) ++greenEdges;
    }
    const size_t center = (32u * 64u + 32u) * 4u;
    if (!check(overlay.size() == filled.size() && greenEdges > 5 &&
               overlay != untexturedBase &&
               std::memcmp(overlay.data() + center, untexturedBase.data() + center, 3) == 0 &&
               untexturedBase[center] > 0,
               "manager overlay did not retain filled faces and colored edges")) return 1;
    source.setOverlayColor(SbColor4f(0, 0, 1, 1));
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "manager overlay color change failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(overlayBlue);
    if (!check(overlayBlue.size() == overlay.size() && overlayBlue != overlay,
               "manager overlay color did not invalidate the cached frame")) return 1;
    source.setTexturesEnabled(TRUE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "textured manager overlay failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(texturedOverlay);
    if (!check(texturedOverlay.size() == filled.size() && texturedOverlay != overlayBlue &&
               std::memcmp(texturedOverlay.data() + center, filled.data() + center, 3) == 0,
               "manager overlay did not retain REPLACE texture semantics")) return 1;
    source.setTexturesEnabled(FALSE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "manager overlay texture restoration failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == overlayBlue,
               "manager overlay texture restoration changed the frame")) return 1;
    source.setRenderMode(SoRenderManager::WIREFRAME);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "untextured WIREFRAME control failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(untexturedLines);
    source.setRenderMode(SoRenderManager::HIDDEN_LINE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "HIDDEN_LINE manager render failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(hidden);
    unsigned visibleEdges = 0, allEdges = 0;
    for (size_t pixel = 0; pixel + 3 < hidden.size(); pixel += 4)
      visibleEdges += hidden[pixel] > 150 && hidden[pixel + 1] > 150 && hidden[pixel + 2] > 150;
    for (size_t pixel = 0; pixel + 3 < untexturedLines.size(); pixel += 4)
      allEdges += untexturedLines[pixel] > 150 && untexturedLines[pixel + 1] > 150 && untexturedLines[pixel + 2] > 150;
    if (!check(hidden.size() == filled.size() && hidden != overlayBlue &&
               hidden[center] < 5 && hidden[center + 1] < 5 &&
               visibleEdges > 5 && allEdges > visibleEdges,
               "HIDDEN_LINE failed to hide faces and retain visible edges")) return 1;
    source.setTexturesEnabled(TRUE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "textured HIDDEN_LINE manager render failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(texturedHidden);
    unsigned redEdges = 0;
    for (size_t pixel = 0; pixel + 3 < texturedHidden.size(); pixel += 4)
      redEdges += texturedHidden[pixel] > 150 && texturedHidden[pixel + 1] < 20;
    if (!check(texturedHidden.size() == hidden.size() && redEdges > 5 &&
               texturedHidden[center] < 5,
               "HIDDEN_LINE did not retain REPLACE texture on visible edges")) return 1;
    const uint8_t translucentTexture[] = {255, 0, 0, 128};
    authoredTexture->image.setValue(SbVec2s(1, 1), 4, translucentTexture);
    if (!check(adapter.render() == CoinRenderAction::UNSUPPORTED,
               "unqualified transparent HIDDEN_LINE was accepted")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == texturedHidden,
               "transparent HIDDEN_LINE rejection changed publication")) return 1;
    authoredTexture->image.setValue(SbVec2s(1, 1), 3, redTexture);
    source.setTexturesEnabled(FALSE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "HIDDEN_LINE recovery after transparent rejection failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == hidden,
               "HIDDEN_LINE recovery changed opaque pixels")) return 1;
    source.setRenderMode(SoRenderManager::SHADED_HIDDEN_LINES);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "SHADED_HIDDEN_LINES manager render failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(shadedHidden);
    if (!check(shadedHidden.size() == hidden.size() &&
               shadedHidden != hidden && shadedHidden != untexturedBase &&
               shadedHidden[center] == untexturedBase[center],
               "SHADED_HIDDEN_LINES did not retain faces and add lines")) return 1;
    CoinRenderAction passProbe(SbViewportRegion(64, 64));
    passProbe.getPimpl()->planOnly = true;
    passProbe.setDrawMode(CoinRenderAction::DrawMode::SHADED_HIDDEN_LINES);
    passProbe.setTexturesEnabled(FALSE);
    passProbe.apply(scene);
    const auto & probePlan = passProbe.getPimpl()->lastValidPlan;
    bool foundFaces = false, foundVisible = false, foundHidden = false;
    size_t visibleStrokeIndices = 0, hiddenStrokeIndices = 0;
    for (const auto & draw : probePlan.draws) {
      const auto & state = probePlan.renderStates[draw.renderStateSlot];
      if (state.depthWrite && state.colorWrite && state.polygonOffsetEnabled) foundFaces = true;
      if (!state.depthWrite && state.depthFunction == CoinRenderDepthFunction::LEQUAL &&
          state.linePattern == 0xffffu) {
        foundVisible = true;
        visibleStrokeIndices += draw.geometry.indexCount;
      }
      if (!state.depthWrite && state.depthFunction == CoinRenderDepthFunction::GREATER &&
          state.linePattern == 0xffffu && state.lineWidth == 1.0f) {
        foundHidden = true;
        hiddenStrokeIndices += draw.geometry.indexCount;
      }
    }
    if (!(passProbe.getLastStatus() == CoinRenderAction::SUCCESS &&
          foundFaces && foundVisible && foundHidden &&
          hiddenStrokeIndices > 0 && hiddenStrokeIndices < visibleStrokeIndices)) {
      std::cerr << "SHADED passes: faces=" << foundFaces << " visible=" << foundVisible
                << " hidden=" << foundHidden << " visible_indices=" << visibleStrokeIndices
                << " hidden_indices=" << hiddenStrokeIndices << '\n';
      return 1;
    }
    std::cout << "shaded_hidden_lines visible_indices=" << visibleStrokeIndices
              << " dashed_hidden_indices=" << hiddenStrokeIndices << '\n';
    if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
      SoRenderManager glSource;
      glSource.setSceneGraph(scene);
      glSource.setViewportRegion(SbViewportRegion(64, 64));
      glSource.setBackgroundColor(SbColor4f(0, 0, 0, 1));
      glSource.setRenderMode(SoRenderManager::SHADED_HIDDEN_LINES);
      glSource.setTexturesEnabled(FALSE);
      ManagerGLAction managerGL(glSource, SbViewportRegion(64, 64));
      SoOffscreenRenderer glReference(SbViewportRegion(64, 64));
      glReference.setGLRenderAction(&managerGL);
      glReference.setComponents(SoOffscreenRenderer::RGB);
      glReference.setBackgroundColor(SbColor(0, 0, 0));
      const bool prepared = glSource.prepareFrame();
      const bool rendered = prepared && glReference.render(scene);
      const uint8_t * glPixels = rendered ? glReference.getBuffer() : nullptr;
      glSource.finishFrame(rendered && glPixels);
      if (!check(prepared && rendered && glPixels,
                 "SHADED_HIDDEN_LINES CoinGL reference capture failed")) return 1;
      uint64_t rgbError = 0;
      uint32_t maxRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t c = 0; c < 3; ++c) {
          const uint32_t difference = std::abs(int(shadedHidden[pixel * 4 + c]) -
                                               int(glPixels[pixel * 3 + c]));
          rgbError += difference;
          maxRgbError = std::max(maxRgbError, difference);
        }
      std::cout << "shaded_hidden_lines CoinGL rgb_error=" << rgbError
                << " max_rgb_error=" << maxRgbError << '\n';
      if (!check(rgbError <= 20000 && maxRgbError <= 10,
                 "SHADED_HIDDEN_LINES exceeded CoinGL RGB gate")) return 1;
      glSource.setRenderMode(SoRenderManager::HIDDEN_LINE);
      const bool hiddenPrepared = glSource.prepareFrame();
      const bool hiddenRendered = hiddenPrepared && glReference.render(scene);
      const uint8_t * hiddenPixels = hiddenRendered ? glReference.getBuffer() : nullptr;
      glSource.finishFrame(hiddenRendered && hiddenPixels);
      if (!check(hiddenPrepared && hiddenRendered && hiddenPixels,
                 "HIDDEN_LINE CoinGL reference capture failed")) return 1;
      glPixels = glReference.getBuffer();
      rgbError = 0;
      maxRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t c = 0; c < 3; ++c) {
          const uint32_t difference = std::abs(int(hidden[pixel * 4 + c]) -
                                               int(glPixels[pixel * 3 + c]));
          rgbError += difference;
          maxRgbError = std::max(maxRgbError, difference);
        }
      std::cout << "hidden_line CoinGL rgb_error=" << rgbError
                << " max_rgb_error=" << maxRgbError << '\n';
      size_t coinEdge = 0, glEdge = 0, commonEdge = 0, coinOnly = 0, glOnly = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel) {
        const bool coinLit = hidden[pixel * 4] > 32 || hidden[pixel * 4 + 1] > 32 ||
                             hidden[pixel * 4 + 2] > 32;
        const bool glLit = glPixels[pixel * 3] > 32 || glPixels[pixel * 3 + 1] > 32 ||
                           glPixels[pixel * 3 + 2] > 32;
        coinEdge += coinLit;
        glEdge += glLit;
        commonEdge += coinLit && glLit;
        coinOnly += coinLit && !glLit;
        glOnly += glLit && !coinLit;
      }
      std::cout << "hidden_line CoinGL coverage coin=" << coinEdge
                << " gl=" << glEdge << " common=" << commonEdge
                << " coin_only=" << coinOnly << " gl_only=" << glOnly << '\n';
      const std::vector<uint8_t> glHiddenRgb(hiddenPixels, hiddenPixels + 64 * 64 * 3);
      glSource.setRenderMode(SoRenderManager::WIREFRAME);
      const bool wirePrepared = glSource.prepareFrame();
      const bool wireRendered = wirePrepared && glReference.render(scene);
      const uint8_t * wirePixels = wireRendered ? glReference.getBuffer() : nullptr;
      glSource.finishFrame(wireRendered && wirePixels);
      if (!check(wirePrepared && wireRendered && wirePixels,
                 "WIREFRAME CoinGL reference capture failed")) return 1;
      size_t coinWire = 0, glWire = 0, commonWire = 0, coinWireOnly = 0, glWireOnly = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel) {
        const bool coinLit = untexturedLines[pixel * 4] > 32 ||
                             untexturedLines[pixel * 4 + 1] > 32 ||
                             untexturedLines[pixel * 4 + 2] > 32;
        const bool glLit = wirePixels[pixel * 3] > 32 || wirePixels[pixel * 3 + 1] > 32 ||
                           wirePixels[pixel * 3 + 2] > 32;
        coinWire += coinLit;
        glWire += glLit;
        commonWire += coinLit && glLit;
        coinWireOnly += coinLit && !glLit;
        glWireOnly += glLit && !coinLit;
      }
      std::cout << "wireframe CoinGL coverage coin=" << coinWire
                << " gl=" << glWire << " common=" << commonWire
                << " coin_only=" << coinWireOnly << " gl_only=" << glWireOnly << '\n';
      size_t glHiddenWireMaskXor = 0;
      uint64_t glHiddenWireRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel) {
        const bool hiddenLit = glHiddenRgb[pixel * 3] > 32 ||
                               glHiddenRgb[pixel * 3 + 1] > 32 ||
                               glHiddenRgb[pixel * 3 + 2] > 32;
        const bool wireLit = wirePixels[pixel * 3] > 32 || wirePixels[pixel * 3 + 1] > 32 ||
                             wirePixels[pixel * 3 + 2] > 32;
        glHiddenWireMaskXor += hiddenLit != wireLit;
        for (size_t c = 0; c < 3; ++c)
          glHiddenWireRgbError += std::abs(int(glHiddenRgb[pixel * 3 + c]) -
                                           int(wirePixels[pixel * 3 + c]));
      }
      std::cout << "CoinGL hidden_vs_wire mask_xor=" << glHiddenWireMaskXor
                << " rgb_error=" << glHiddenWireRgbError << '\n';
      if (!check(glHiddenWireMaskXor > 0 && glHiddenWireRgbError > 0,
                 "CoinGL HIDDEN_LINE did not occlude rear edges")) return 1;
      SoSeparator * cubeScene = new SoSeparator;
      cubeScene->ref();
      SoPerspectiveCamera * cubeCamera = new SoPerspectiveCamera;
      cubeCamera->position.setValue(3, 2, 5);
      cubeCamera->pointAt(SbVec3f(0, 0, 0));
      cubeScene->addChild(cubeCamera);
      cubeScene->addChild(new SoCube);
      SoRenderManager cubeSource;
      cubeSource.setSceneGraph(cubeScene);
      cubeSource.setViewportRegion(SbViewportRegion(64, 64));
      cubeSource.setBackgroundColor(SbColor4f(0, 0, 0, 1));
      CoinRenderManagerAdapter cubeAdapter(cubeSource, SbVec2i32(64, 64));
      auto cubeCoin = [&](SoRenderManager::RenderMode mode, std::vector<uint8_t> & pixels) {
        cubeSource.setRenderMode(mode);
        if (cubeAdapter.render() != CoinRenderAction::SUCCESS) return false;
        cubeAdapter.getSceneManager()->getRenderTarget()->readbackRGBA(pixels);
        return pixels.size() == 64 * 64 * 4;
      };
      std::vector<uint8_t> cubeCoinWire, cubeCoinHidden;
      if (!check(cubeCoin(SoRenderManager::WIREFRAME, cubeCoinWire) &&
                 cubeCoin(SoRenderManager::HIDDEN_LINE, cubeCoinHidden),
                 "tilted cube CoinRender manager capture failed")) return 1;
      SoRenderManager cubeGLSource;
      cubeGLSource.setSceneGraph(cubeScene);
      cubeGLSource.setViewportRegion(SbViewportRegion(64, 64));
      cubeGLSource.setBackgroundColor(SbColor4f(0, 0, 0, 1));
      ManagerGLAction cubeGLAction(cubeGLSource, SbViewportRegion(64, 64));
      SoOffscreenRenderer cubeGLRenderer(SbViewportRegion(64, 64));
      cubeGLRenderer.setGLRenderAction(&cubeGLAction);
      cubeGLRenderer.setComponents(SoOffscreenRenderer::RGB);
      cubeGLRenderer.setBackgroundColor(SbColor(0, 0, 0));
      auto cubeGL = [&](SoRenderManager::RenderMode mode, std::vector<uint8_t> & pixels) {
        cubeGLSource.setRenderMode(mode);
        const bool ready = cubeGLSource.prepareFrame();
        const bool drawn = ready && cubeGLRenderer.render(cubeScene);
        const uint8_t * buffer = drawn ? cubeGLRenderer.getBuffer() : nullptr;
        cubeGLSource.finishFrame(drawn && buffer);
        if (!buffer) return false;
        pixels.assign(buffer, buffer + 64 * 64 * 3);
        return true;
      };
      std::vector<uint8_t> cubeGLWire, cubeGLHidden;
      if (!check(cubeGL(SoRenderManager::WIREFRAME, cubeGLWire) &&
                 cubeGL(SoRenderManager::HIDDEN_LINE, cubeGLHidden),
                 "tilted cube CoinGL manager capture failed")) return 1;
      size_t cubeCoinWireCount = 0, cubeCoinHiddenCount = 0;
      size_t cubeGLWireCount = 0, cubeGLHiddenCount = 0;
      size_t cubeGLMaskXor = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel) {
        const bool cw = cubeCoinWire[pixel * 4] > 32;
        const bool ch = cubeCoinHidden[pixel * 4] > 32;
        const bool gw = cubeGLWire[pixel * 3] > 32;
        const bool gh = cubeGLHidden[pixel * 3] > 32;
        cubeCoinWireCount += cw;
        cubeCoinHiddenCount += ch;
        cubeGLWireCount += gw;
        cubeGLHiddenCount += gh;
        cubeGLMaskXor += gw != gh;
      }
      std::cout << "tilted_cube hidden_vs_wire CoinRender=" << cubeCoinHiddenCount
                << '/' << cubeCoinWireCount << " CoinGL=" << cubeGLHiddenCount
                << '/' << cubeGLWireCount << " gl_mask_xor=" << cubeGLMaskXor << '\n';
      if (!check(cubeCoinHiddenCount > 0 && cubeCoinHiddenCount < cubeCoinWireCount &&
                 cubeGLHiddenCount > 0 && cubeGLHiddenCount < cubeGLWireCount &&
                 cubeGLMaskXor > 0,
                 "tilted cube HIDDEN_LINE did not occlude rear edges")) return 1;
      cubeSource.setSceneGraph(nullptr);
      cubeGLSource.setSceneGraph(nullptr);
      cubeScene->unref();
    }
    authoredTexture->image.setValue(SbVec2s(1, 1), 4, translucentTexture);
    source.setTexturesEnabled(TRUE);
    if (!check(adapter.render() == CoinRenderAction::UNSUPPORTED,
               "transparent SHADED_HIDDEN_LINES was accepted")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == shadedHidden, "manager mode rejection changed publication")) return 1;
    authoredTexture->image.setValue(SbVec2s(1, 1), 3, redTexture);
    source.setRenderMode(SoRenderManager::AS_IS);
    source.setTexturesEnabled(TRUE);
    source.setStereoMode(SoRenderManager::ANAGLYPH);
    if (!check(adapter.render() == CoinRenderAction::UNSUPPORTED,
               "unimplemented manager stereo mode was not rejected")) return 1;
    source.setStereoMode(SoRenderManager::MONO);
    source.setTexturesEnabled(FALSE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "manager texture-disable override failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(untextured);
    if (!check(untextured.size() == filled.size() && untextured != filled,
               "manager texture-disable override did not change visible pixels")) return 1;
    source.setTexturesEnabled(TRUE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "manager failed to recover after unsupported modes")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(restored);
    if (!check(restored == filled,
               "manager texture-enable restoration changed published pixels")) return 1;
    source.setRGBMode(FALSE);
    if (!check(adapter.render() == CoinRenderAction::UNSUPPORTED &&
               adapter.getLastError().getLength() > 0,
               "manager color-index mode was silently accepted")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == restored,
               "color-index rejection changed the last published frame")) return 1;
    source.setRGBMode(TRUE);
    SoSeparator * layerScene = new SoSeparator;
    layerScene->ref();
    layerScene->addChild(new SoCube);
    SoRenderManager::Superimposition * layer = source.addSuperimposition(layerScene,
      SoRenderManager::Superimposition::BACKGROUND |
      SoRenderManager::Superimposition::ZBUFFERON);
    source.setRenderMode(SoRenderManager::WIREFRAME);
    if (!check(source.hasEnabledSuperimpositions() &&
               adapter.render() == CoinRenderAction::UNSUPPORTED &&
               adapter.getLastError().getLength() > 0,
               "superimposition in WIREFRAME mode was silently accepted")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == restored,
               "superimposition rejection changed the last published frame")) return 1;
    source.setRenderMode(SoRenderManager::AS_IS);
    layer->setEnabled(FALSE);
    if (!check(!source.hasEnabledSuperimpositions() &&
               adapter.render() == CoinRenderAction::SUCCESS,
               "disabled superimposition blocked ordinary rendering")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == restored,
               "disabled superimposition changed ordinary rendering")) return 1;
    source.removeSuperimposition(layer);
    layerScene->unref();
    SoSeparator * foreground = new SoSeparator;
    foreground->ref();
    SoOrthographicCamera * foregroundCamera = new SoOrthographicCamera;
    foregroundCamera->position.setValue(0, 0, 4);
    foregroundCamera->height = 4.0f;
    foreground->addChild(foregroundCamera);
    SoLightModel * foregroundLighting = new SoLightModel;
    foregroundLighting->model = SoLightModel::BASE_COLOR;
    foreground->addChild(foregroundLighting);
    SoMaterial * foregroundColor = new SoMaterial;
    foregroundColor->diffuseColor.setValue(0, 1, 0);
    foreground->addChild(foregroundColor);
    SoCoordinate3 * foregroundCoordinates = new SoCoordinate3;
    const SbVec3f foregroundCorners[] = {
      SbVec3f(-0.4f, -0.4f, 0), SbVec3f(0.4f, -0.4f, 0),
      SbVec3f(0.4f, 0.4f, 0), SbVec3f(-0.4f, 0.4f, 0)
    };
    foregroundCoordinates->point.setValues(0, 4, foregroundCorners);
    foreground->addChild(foregroundCoordinates);
    SoFaceSet * foregroundQuad = new SoFaceSet;
    foregroundQuad->numVertices.set1Value(0, 4);
    foreground->addChild(foregroundQuad);
    auto * foregroundLayer = source.addSuperimposition(
      foreground, SoRenderManager::Superimposition::AUTOREDRAW);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "foreground superimposition without depth test failed")) return 1;
    std::vector<uint8_t> foregroundImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(foregroundImage);
    if (!(foregroundImage.size() == restored.size() &&
          foregroundImage != restored &&
          foregroundImage[center + 1] > foregroundImage[center] * 2)) {
      std::cerr << "foreground center=" << int(foregroundImage[center]) << ','
                << int(foregroundImage[center + 1]) << ',' << int(foregroundImage[center + 2])
                << " base=" << int(restored[center]) << ',' << int(restored[center + 1])
                << ',' << int(restored[center + 2]) << '\n';
      return 1;
    }
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "unchanged foreground superimposition frame failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == foregroundImage,
               "unchanged foreground superimposition changed pixels")) return 1;
    if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
      SoRenderManager foregroundGLSource;
      foregroundGLSource.setSceneGraph(scene);
      foregroundGLSource.setViewportRegion(SbViewportRegion(64, 64));
      foregroundGLSource.setBackgroundColor(SbColor4f(0, 0, 0, 1));
      foregroundGLSource.addSuperimposition(
        foreground, SoRenderManager::Superimposition::AUTOREDRAW);
      ManagerGLAction foregroundGLAction(foregroundGLSource, SbViewportRegion(64, 64));
      SoOffscreenRenderer foregroundGLRenderer(SbViewportRegion(64, 64));
      foregroundGLRenderer.setGLRenderAction(&foregroundGLAction);
      foregroundGLRenderer.setComponents(SoOffscreenRenderer::RGB);
      foregroundGLRenderer.setBackgroundColor(SbColor(0, 0, 0));
      const bool ready = foregroundGLSource.prepareFrame();
      const bool drawn = ready && foregroundGLRenderer.render(scene);
      const uint8_t * glBuffer = drawn ? foregroundGLRenderer.getBuffer() : nullptr;
      foregroundGLSource.finishFrame(drawn && glBuffer);
      if (!check(ready && drawn && glBuffer,
                 "foreground superimposition CoinGL capture failed")) return 1;
      uint64_t rgbError = 0;
      uint32_t maxRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t c = 0; c < 3; ++c) {
          const uint32_t difference = std::abs(int(foregroundImage[pixel * 4 + c]) -
                                               int(glBuffer[pixel * 3 + c]));
          rgbError += difference;
          maxRgbError = std::max(maxRgbError, difference);
        }
      std::cout << "foreground_superimposition CoinGL rgb_error=" << rgbError
                << " max_rgb_error=" << maxRgbError << '\n';
      size_t coinGreen = 0, glGreen = 0, commonGreen = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel) {
        const bool coin = foregroundImage[pixel * 4 + 1] > 100 &&
                          foregroundImage[pixel * 4 + 1] > foregroundImage[pixel * 4] * 2;
        const bool gl = glBuffer[pixel * 3 + 1] > 100 &&
                        glBuffer[pixel * 3 + 1] > glBuffer[pixel * 3] * 2;
        coinGreen += coin;
        glGreen += gl;
        commonGreen += coin && gl;
      }
      std::cout << "foreground_superimposition CoinGL green coin=" << coinGreen
                << " gl=" << glGreen << " common=" << commonGreen
                << " center=" << int(glBuffer[(32 * 64 + 32) * 3]) << ','
                << int(glBuffer[(32 * 64 + 32) * 3 + 1]) << ','
                << int(glBuffer[(32 * 64 + 32) * 3 + 2]) << '\n';
      const std::vector<uint8_t> glForeground(glBuffer, glBuffer + 64 * 64 * 3);
      SoRenderManager glBaseSource;
      glBaseSource.setSceneGraph(scene);
      glBaseSource.setViewportRegion(SbViewportRegion(64, 64));
      glBaseSource.setBackgroundColor(SbColor4f(0, 0, 0, 1));
      ManagerGLAction glBaseAction(glBaseSource, SbViewportRegion(64, 64));
      SoOffscreenRenderer glBaseRenderer(SbViewportRegion(64, 64));
      glBaseRenderer.setGLRenderAction(&glBaseAction);
      glBaseRenderer.setComponents(SoOffscreenRenderer::RGB);
      glBaseRenderer.setBackgroundColor(SbColor(0, 0, 0));
      const bool baseReady = glBaseSource.prepareFrame();
      const bool baseDrawn = baseReady && glBaseRenderer.render(scene);
      const uint8_t * glBase = baseDrawn ? glBaseRenderer.getBuffer() : nullptr;
      glBaseSource.finishFrame(baseDrawn && glBase);
      if (!check(baseReady && baseDrawn && glBase,
                 "foreground-disabled CoinGL baseline capture failed")) return 1;
      size_t coinOutsideChanged = 0, glOutsideChanged = 0;
      size_t firstGlOutsideChanged = 64 * 64;
      uint64_t overlayRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel) {
        const bool green = foregroundImage[pixel * 4 + 1] > 100 &&
                           foregroundImage[pixel * 4 + 1] > foregroundImage[pixel * 4] * 2;
        for (size_t c = 0; c < 3; ++c) {
          if (green)
            overlayRgbError += std::abs(int(foregroundImage[pixel * 4 + c]) -
                                        int(glForeground[pixel * 3 + c]));
          else {
            coinOutsideChanged += foregroundImage[pixel * 4 + c] != restored[pixel * 4 + c];
            const bool glChanged = glForeground[pixel * 3 + c] != glBase[pixel * 3 + c];
            glOutsideChanged += glChanged;
            if (glChanged && firstGlOutsideChanged == 64 * 64) firstGlOutsideChanged = pixel;
          }
        }
      }
      std::cout << "foreground_superimposition CoinGL layer_rgb_error=" << overlayRgbError
                << " coin_outside_changed=" << coinOutsideChanged
                << " gl_outside_changed=" << glOutsideChanged << '\n';
      if (firstGlOutsideChanged < 64 * 64)
        std::cout << "foreground_superimposition first_gl_outside="
                  << firstGlOutsideChanged % 64 << ',' << firstGlOutsideChanged / 64
                  << " composed=" << int(glForeground[firstGlOutsideChanged * 3]) << ','
                  << int(glForeground[firstGlOutsideChanged * 3 + 1]) << ','
                  << int(glForeground[firstGlOutsideChanged * 3 + 2])
                  << " base=" << int(glBase[firstGlOutsideChanged * 3]) << ','
                  << int(glBase[firstGlOutsideChanged * 3 + 1]) << ','
                  << int(glBase[firstGlOutsideChanged * 3 + 2]) << '\n';
      if (!check(coinGreen == glGreen && coinGreen == commonGreen && coinGreen > 0 &&
                 overlayRgbError == 0 && rgbError == 0 &&
                 coinOutsideChanged == 0 && glOutsideChanged == 0,
                 "foreground superimposition differs from CoinGL layer composition")) return 1;
    }
    SoSeparator * blueForeground = new SoSeparator;
    blueForeground->ref();
    SoOrthographicCamera * blueCamera = new SoOrthographicCamera;
    blueCamera->position.setValue(0, 0, 4);
    blueCamera->height = 4.0f;
    blueForeground->addChild(blueCamera);
    SoLightModel * blueLighting = new SoLightModel;
    blueLighting->model = SoLightModel::BASE_COLOR;
    blueForeground->addChild(blueLighting);
    SoMaterial * blueColor = new SoMaterial;
    blueColor->diffuseColor.setValue(0, 0, 1);
    blueForeground->addChild(blueColor);
    SoCoordinate3 * blueCoordinates = new SoCoordinate3;
    const SbVec3f blueCorners[] = {
      SbVec3f(-0.2f, -0.2f, 0), SbVec3f(0.2f, -0.2f, 0),
      SbVec3f(0.2f, 0.2f, 0), SbVec3f(-0.2f, 0.2f, 0)
    };
    blueCoordinates->point.setValues(0, 4, blueCorners);
    blueForeground->addChild(blueCoordinates);
    SoFaceSet * blueQuad = new SoFaceSet;
    blueQuad->numVertices.set1Value(0, 4);
    blueForeground->addChild(blueQuad);
    auto * blueLayer = source.addSuperimposition(
      blueForeground, SoRenderManager::Superimposition::AUTOREDRAW);
    if (!check(source.getNumSuperimpositions() == 2 &&
               adapter.render() == CoinRenderAction::SUCCESS,
               "second foreground superimposition failed")) return 1;
    std::vector<uint8_t> twoLayerImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(twoLayerImage);
    const size_t ring = (32 * 64 + 27) * 4;
    if (!check(twoLayerImage.size() == foregroundImage.size() &&
               twoLayerImage[center + 2] > twoLayerImage[center + 1] * 2 &&
               foregroundImage[ring + 1] > foregroundImage[ring] * 2 &&
               std::equal(twoLayerImage.begin() + ring,
                          twoLayerImage.begin() + ring + 4,
                          foregroundImage.begin() + ring),
               "second foreground did not cover the center and retain the green ring")) return 1;
    if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
      SoRenderManager twoLayerGLSource;
      twoLayerGLSource.setSceneGraph(scene);
      twoLayerGLSource.setViewportRegion(SbViewportRegion(64, 64));
      twoLayerGLSource.setBackgroundColor(SbColor4f(0, 0, 0, 1));
      twoLayerGLSource.addSuperimposition(
        foreground, SoRenderManager::Superimposition::AUTOREDRAW);
      twoLayerGLSource.addSuperimposition(
        blueForeground, SoRenderManager::Superimposition::AUTOREDRAW);
      ManagerGLAction twoLayerGLAction(twoLayerGLSource, SbViewportRegion(64, 64));
      SoOffscreenRenderer twoLayerGLRenderer(SbViewportRegion(64, 64));
      twoLayerGLRenderer.setGLRenderAction(&twoLayerGLAction);
      twoLayerGLRenderer.setComponents(SoOffscreenRenderer::RGB);
      twoLayerGLRenderer.setBackgroundColor(SbColor(0, 0, 0));
      const bool ready = twoLayerGLSource.prepareFrame();
      const bool drawn = ready && twoLayerGLRenderer.render(scene);
      const uint8_t * glBuffer = drawn ? twoLayerGLRenderer.getBuffer() : nullptr;
      twoLayerGLSource.finishFrame(drawn && glBuffer);
      if (!check(ready && drawn && glBuffer,
                 "two-layer CoinGL capture failed")) return 1;
      size_t coinBlue = 0, glBlue = 0, commonBlue = 0;
      uint64_t blueRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel) {
        const bool coin = twoLayerImage[pixel * 4 + 2] > 100 &&
                          twoLayerImage[pixel * 4 + 2] > twoLayerImage[pixel * 4 + 1] * 2;
        const bool gl = glBuffer[pixel * 3 + 2] > 100 &&
                        glBuffer[pixel * 3 + 2] > glBuffer[pixel * 3 + 1] * 2;
        coinBlue += coin;
        glBlue += gl;
        commonBlue += coin && gl;
        if (coin)
          for (size_t c = 0; c < 3; ++c)
            blueRgbError += std::abs(int(twoLayerImage[pixel * 4 + c]) -
                                     int(glBuffer[pixel * 3 + c]));
      }
      std::cout << "foreground_multilayer CoinGL blue coin=" << coinBlue
                << " gl=" << glBlue << " common=" << commonBlue
                << " rgb_error=" << blueRgbError << '\n';
      if (!check(coinBlue == glBlue && commonBlue == coinBlue &&
                 coinBlue > 0 && blueRgbError <= coinBlue * 3,
                 "two-layer blue mask/color differs from CoinGL")) return 1;
    }
    blueLayer->setEnabled(FALSE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "disabling second foreground failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == foregroundImage,
               "disabling second foreground did not reveal the first")) return 1;
    blueLayer->setEnabled(TRUE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "re-enabling second foreground failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == twoLayerImage,
               "re-enabling second foreground changed pixels")) return 1;
    source.removeSuperimposition(blueLayer);
    blueForeground->unref();
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "removing second foreground failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == foregroundImage,
               "removing second foreground did not restore the first")) return 1;
    std::cout << "foreground_multilayer order_and_toggle=pass"
              << " center_blue=" << int(twoLayerImage[center + 2])
              << " ring_green=" << int(twoLayerImage[ring + 1]) << '\n';
    foregroundLayer->setEnabled(FALSE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "disabling foreground superimposition failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == restored,
               "disabling foreground superimposition did not restore the scene")) return 1;
    foregroundLayer->setEnabled(TRUE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "re-enabling foreground superimposition failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
    if (!check(retained == foregroundImage,
               "re-enabling foreground superimposition changed its pixels")) return 1;
    source.removeSuperimposition(foregroundLayer);
    foreground->unref();
    source.setSceneGraph(nullptr);
    auto makeDepthQuad = [](float z, float extent, const SbColor & color,
                            SoCoordinate3 ** outCoordinates) {
      SoSeparator * root = new SoSeparator;
      root->ref();
      SoOrthographicCamera * camera = new SoOrthographicCamera;
      camera->position.setValue(0, 0, 4);
      camera->height = 4;
      root->addChild(camera);
      SoLightModel * lighting = new SoLightModel;
      lighting->model = SoLightModel::BASE_COLOR;
      root->addChild(lighting);
      SoMaterial * material = new SoMaterial;
      material->diffuseColor.setValue(color);
      root->addChild(material);
      SoCoordinate3 * coordinates = new SoCoordinate3;
      const SbVec3f corners[] = {
        SbVec3f(-extent, -extent, z), SbVec3f(extent, -extent, z),
        SbVec3f(extent, extent, z), SbVec3f(-extent, extent, z)
      };
      coordinates->point.setValues(0, 4, corners);
      root->addChild(coordinates);
      SoFaceSet * face = new SoFaceSet;
      face->numVertices.set1Value(0, 4);
      root->addChild(face);
      if (outCoordinates) *outCoordinates = coordinates;
      return root;
    };
    SoSeparator * depthBase = makeDepthQuad(0, 0.4f, SbColor(1, 0, 0), nullptr);
    SoCoordinate3 * depthCoordinates = nullptr;
    SoSeparator * depthOverlay = makeDepthQuad(-0.5f, 0.2f, SbColor(0, 1, 0),
                                              &depthCoordinates);
    source.setSceneGraph(depthBase);
    auto * depthLayer = source.addSuperimposition(depthOverlay,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::ZBUFFERON);
    SoRenderManager depthGLSource;
    depthGLSource.setSceneGraph(depthBase);
    depthGLSource.setViewportRegion(SbViewportRegion(64, 64));
    depthGLSource.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    depthGLSource.addSuperimposition(depthOverlay,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::ZBUFFERON);
    ManagerGLAction depthGLAction(depthGLSource, SbViewportRegion(64, 64));
    SoOffscreenRenderer depthGLRenderer(SbViewportRegion(64, 64));
    depthGLRenderer.setGLRenderAction(&depthGLAction);
    depthGLRenderer.setComponents(SoOffscreenRenderer::RGB);
    depthGLRenderer.setBackgroundColor(SbColor(0, 0, 0));
    auto captureDepthGL = [&]() {
      std::vector<uint8_t> pixels;
      if (!std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) return pixels;
      const bool ready = depthGLSource.prepareFrame();
      const bool drawn = ready && depthGLRenderer.render(depthBase);
      const uint8_t * buffer = drawn ? depthGLRenderer.getBuffer() : nullptr;
      depthGLSource.finishFrame(drawn && buffer);
      if (buffer) {
        pixels.resize(64 * 64 * 3);
        // SoOffscreenRenderer is bottom-up; CoinRenderTarget readback is top-down.
        for (size_t y = 0; y < 64; ++y)
          for (size_t x = 0; x < 64; ++x)
            for (size_t c = 0; c < 3; ++c)
              pixels[(y * 64 + x) * 3 + c] =
                buffer[((63 - y) * 64 + x) * 3 + c];
      }
      return pixels;
    };
    auto checkDepthFrame = [&](bool front, const char * label) {
      if (!check(adapter.render() == CoinRenderAction::SUCCESS, label)) return false;
      std::vector<uint8_t> pixels;
      adapter.getSceneManager()->getRenderTarget()->readbackRGBA(pixels);
      const size_t dominant = center + (front ? 1 : 0);
      const size_t other = center + (front ? 0 : 1);
      if (!check(pixels.size() == 64 * 64 * 4 &&
                 pixels[dominant] > pixels[other] * 2,
                 "CoinRender ZBUFFERON depth order is wrong")) return false;
      const std::vector<uint8_t> glPixels = captureDepthGL();
      if (!glPixels.empty()) {
        const size_t glCenter = (32 * 64 + 32) * 3;
        if (!check(glPixels[glCenter + (front ? 1 : 0)] >
                   glPixels[glCenter + (front ? 0 : 1)] * 2,
                   "CoinGL ZBUFFERON depth order is wrong")) return false;
        uint64_t rgbError = 0;
        for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
          for (size_t channel = 0; channel < 3; ++channel)
            rgbError += std::abs(int(pixels[pixel * 4 + channel]) -
                                 int(glPixels[pixel * 3 + channel]));
        std::cout << "foreground_depth " << label << " CoinGL rgb_error="
                  << rgbError << '\n';
        if (!check(rgbError == 0,
                   "ZBUFFERON CoinRender/CoinGL full frame differs")) return false;
      }
      return true;
    };
    if (!checkDepthFrame(false, "ZBUFFERON behind frame failed")) return 1;
    const SbVec3f frontCorners[] = {
      SbVec3f(-0.2f, -0.2f, 0.5f), SbVec3f(0.2f, -0.2f, 0.5f),
      SbVec3f(0.2f, 0.2f, 0.5f), SbVec3f(-0.2f, 0.2f, 0.5f)
    };
    depthCoordinates->point.setValues(0, 4, frontCorners);
    if (!checkDepthFrame(true, "ZBUFFERON ahead frame failed")) return 1;
    source.removeSuperimposition(depthLayer);
    depthGLSource.removeSuperimposition(depthGLSource.getSuperimposition(0));
    const SbVec3f rearCorners[] = {
      SbVec3f(-0.2f, -0.2f, -0.5f), SbVec3f(0.2f, -0.2f, -0.5f),
      SbVec3f(0.2f, 0.2f, -0.5f), SbVec3f(-0.2f, 0.2f, -0.5f)
    };
    depthCoordinates->point.setValues(0, 4, rearCorners);
    auto * clearedLayer = source.addSuperimposition(depthOverlay,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::ZBUFFERON |
      SoRenderManager::Superimposition::CLEARZBUFFER);
    depthGLSource.addSuperimposition(depthOverlay,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::ZBUFFERON |
      SoRenderManager::Superimposition::CLEARZBUFFER);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "CLEARZBUFFER foreground frame failed")) return 1;
    std::vector<uint8_t> clearedImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(clearedImage);
    if (!check(clearedImage.size() == 64 * 64 * 4 &&
               clearedImage[center + 1] > clearedImage[center] * 2,
               "CLEARZBUFFER did not release the base depth")) return 1;
    const std::vector<uint8_t> glCleared = captureDepthGL();
    if (!glCleared.empty()) {
      uint64_t rgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel)
          rgbError += std::abs(int(clearedImage[pixel * 4 + channel]) -
                               int(glCleared[pixel * 3 + channel]));
      std::cout << "foreground_depth CLEARZBUFFER CoinGL rgb_error=" << rgbError << '\n';
      if (!check(rgbError == 0,
                 "CLEARZBUFFER CoinRender/CoinGL full frame differs")) return 1;
    }
    source.removeSuperimposition(clearedLayer);
    depthGLSource.removeSuperimposition(depthGLSource.getSuperimposition(0));
    SoSeparator * emptyClearRoot = new SoSeparator;
    emptyClearRoot->ref();
    auto * emptyClearLayer = source.addSuperimposition(emptyClearRoot,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::ZBUFFERON |
      SoRenderManager::Superimposition::CLEARZBUFFER);
    auto * followingDepthLayer = source.addSuperimposition(depthOverlay,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::ZBUFFERON);
    depthGLSource.addSuperimposition(emptyClearRoot,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::ZBUFFERON |
      SoRenderManager::Superimposition::CLEARZBUFFER);
    depthGLSource.addSuperimposition(depthOverlay,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::ZBUFFERON);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "empty CLEARZBUFFER followed by depth layer failed")) return 1;
    std::vector<uint8_t> emptyClearImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(emptyClearImage);
    if (!check(emptyClearImage[center + 1] > emptyClearImage[center] * 2,
               "empty CLEARZBUFFER did not reach the following layer")) return 1;
    const std::vector<uint8_t> glEmptyClear = captureDepthGL();
    if (!glEmptyClear.empty()) {
      uint64_t rgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel)
          rgbError += std::abs(int(emptyClearImage[pixel * 4 + channel]) -
                               int(glEmptyClear[pixel * 3 + channel]));
      std::cout << "empty_CLEARZBUFFER CoinGL rgb_error=" << rgbError << '\n';
      if (!check(rgbError == 0,
                 "empty CLEARZBUFFER differs from CoinGL")) return 1;
    }
    SbViewportRegion insetViewport(64, 64);
    insetViewport.setViewportPixels(16, 16, 32, 32);
    source.setViewportRegion(insetViewport);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "empty CLEARZBUFFER inset viewport failed")) return 1;
    std::vector<uint8_t> insetImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(insetImage);
    const size_t outsideInset = (32 * 64 + 8) * 4;
    if (!check(insetImage.size() == 64 * 64 * 4 &&
               insetImage[center + 1] > insetImage[center] * 2 &&
               insetImage[outsideInset] == 0 &&
               insetImage[outsideInset + 1] == 0 &&
               insetImage[outsideInset + 2] == 0,
               "empty CLEARZBUFFER inset viewport leaked or missed depth")) return 1;
    source.setViewportRegion(SbViewportRegion(64, 64));
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "empty CLEARZBUFFER viewport restoration failed")) return 1;
    std::vector<uint8_t> fullRestored;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(fullRestored);
    if (!check(fullRestored == emptyClearImage,
               "empty CLEARZBUFFER viewport restoration changed pixels")) return 1;
    std::cout << "empty_CLEARZBUFFER inset_viewport=pass restore=pass\n";
    emptyClearLayer->setEnabled(FALSE);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "disabling empty CLEARZBUFFER failed")) return 1;
    std::vector<uint8_t> noClearImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(noClearImage);
    if (!check(noClearImage[center] > noClearImage[center + 1] * 2,
               "disabled empty CLEARZBUFFER still cleared depth")) return 1;
    source.removeSuperimposition(followingDepthLayer);
    source.removeSuperimposition(emptyClearLayer);
    depthGLSource.removeSuperimposition(depthGLSource.getSuperimposition(1));
    depthGLSource.removeSuperimposition(depthGLSource.getSuperimposition(0));
    emptyClearRoot->unref();
    SoSeparator * backgroundLayerRoot = makeDepthQuad(
      0.5f, 1.6f, SbColor(0, 0, 1), nullptr);
    auto * backgroundLayer = source.addSuperimposition(backgroundLayerRoot,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::BACKGROUND |
      SoRenderManager::Superimposition::ZBUFFERON);
    depthGLSource.addSuperimposition(backgroundLayerRoot,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::BACKGROUND |
      SoRenderManager::Superimposition::ZBUFFERON);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "BACKGROUND superimposition frame failed")) return 1;
    std::vector<uint8_t> backgroundImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(backgroundImage);
    const size_t backgroundSample = (32 * 64 + 12) * 4;
    if (!check(backgroundImage.size() == 64 * 64 * 4 &&
               backgroundImage[center] > backgroundImage[center + 2] * 2 &&
               backgroundImage[backgroundSample + 2] >
                 backgroundImage[backgroundSample] * 2,
               "BACKGROUND did not stay behind the main scene")) return 1;
    const std::vector<uint8_t> glBackground = captureDepthGL();
    if (!glBackground.empty()) {
      uint64_t rgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel)
          rgbError += std::abs(int(backgroundImage[pixel * 4 + channel]) -
                               int(glBackground[pixel * 3 + channel]));
      std::cout << "background_superimposition CoinGL rgb_error=" << rgbError << '\n';
      if (!check(rgbError == 0,
                 "BACKGROUND CoinRender/CoinGL full frame differs")) return 1;
    }
    SoSeparator * rearBackgroundRoot = makeDepthQuad(
      -0.5f, 0.8f, SbColor(0, 1, 0), nullptr);
    auto * rearBackground = source.addSuperimposition(rearBackgroundRoot,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::BACKGROUND |
      SoRenderManager::Superimposition::ZBUFFERON);
    depthGLSource.addSuperimposition(rearBackgroundRoot,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::BACKGROUND |
      SoRenderManager::Superimposition::ZBUFFERON);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "two depth-tested BACKGROUND layers failed")) return 1;
    std::vector<uint8_t> twoBackgrounds;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(twoBackgrounds);
    const size_t overlap = (32 * 64 + 22) * 4;
    if (!check(twoBackgrounds[overlap + 2] > twoBackgrounds[overlap + 1] * 2 &&
               twoBackgrounds[center] > twoBackgrounds[center + 2] * 2,
               "BACKGROUND depth or pre-base clear failed")) return 1;
    const std::vector<uint8_t> glTwoBackgrounds = captureDepthGL();
    if (!glTwoBackgrounds.empty()) {
      uint64_t rgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel)
          rgbError += std::abs(int(twoBackgrounds[pixel * 4 + channel]) -
                               int(glTwoBackgrounds[pixel * 3 + channel]));
      std::cout << "background_depth_layers CoinGL rgb_error=" << rgbError << '\n';
      if (!check(rgbError == 0,
                 "two BACKGROUND depth layers differ from CoinGL")) return 1;
    }
    source.removeSuperimposition(rearBackground);
    depthGLSource.removeSuperimposition(depthGLSource.getSuperimposition(1));
    rearBackgroundRoot->unref();
    source.removeSuperimposition(backgroundLayer);
    depthGLSource.removeSuperimposition(depthGLSource.getSuperimposition(0));
    SoSeparator * mixedForegroundRoot = makeDepthQuad(
      0, 0.2f, SbColor(0, 1, 0), nullptr);
    auto * mixedForeground = source.addSuperimposition(mixedForegroundRoot,
      SoRenderManager::Superimposition::AUTOREDRAW);
    auto * mixedBackground = source.addSuperimposition(backgroundLayerRoot,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::BACKGROUND |
      SoRenderManager::Superimposition::ZBUFFERON);
    depthGLSource.addSuperimposition(mixedForegroundRoot,
      SoRenderManager::Superimposition::AUTOREDRAW);
    depthGLSource.addSuperimposition(backgroundLayerRoot,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::BACKGROUND |
      SoRenderManager::Superimposition::ZBUFFERON);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "mixed BACKGROUND/foreground frame failed")) return 1;
    std::vector<uint8_t> mixedImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(mixedImage);
    const size_t redRing = (32 * 64 + 27) * 4;
    if (!check(mixedImage[center + 1] > mixedImage[center] * 2 &&
               mixedImage[redRing] > mixedImage[redRing + 1] * 2 &&
               mixedImage[backgroundSample + 2] > mixedImage[backgroundSample] * 2,
               "mixed layer registration order changed rendering order")) return 1;
    const std::vector<uint8_t> glMixed = captureDepthGL();
    if (!glMixed.empty()) {
      uint64_t rgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel)
          rgbError += std::abs(int(mixedImage[pixel * 4 + channel]) -
                               int(glMixed[pixel * 3 + channel]));
      std::cout << "mixed_superimpositions CoinGL rgb_error=" << rgbError << '\n';
      if (!check(rgbError == 0,
                 "mixed BACKGROUND/foreground CoinGL full frame differs")) return 1;
    }
    source.removeSuperimposition(mixedBackground);
    source.removeSuperimposition(mixedForeground);
    depthGLSource.removeSuperimposition(depthGLSource.getSuperimposition(1));
    depthGLSource.removeSuperimposition(depthGLSource.getSuperimposition(0));
    SoSeparator * translucentForeground = makeDepthQuad(
      0.5f, 0.2f, SbColor(0, 0, 1), nullptr);
    static_cast<SoMaterial *>(translucentForeground->getChild(2))->transparency = 0.5f;
    source.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
    depthGLSource.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
    auto * translucentLayer = source.addSuperimposition(translucentForeground,
      SoRenderManager::Superimposition::AUTOREDRAW);
    depthGLSource.addSuperimposition(translucentForeground,
      SoRenderManager::Superimposition::AUTOREDRAW);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "translucent foreground BLEND frame failed")) return 1;
    std::vector<uint8_t> translucentImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(translucentImage);
    if (!check(translucentImage[center] > 50 &&
               translucentImage[center + 2] > 50 &&
               translucentImage[center + 1] < 20,
               "translucent foreground did not blend red and blue")) return 1;
    const std::vector<uint8_t> glTranslucent = captureDepthGL();
    if (!glTranslucent.empty()) {
      uint64_t rgbError = 0;
      uint32_t maxRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel) {
          const uint32_t difference = std::abs(int(translucentImage[pixel * 4 + channel]) -
                                               int(glTranslucent[pixel * 3 + channel]));
          rgbError += difference;
          maxRgbError = std::max(maxRgbError, difference);
        }
      std::cout << "foreground_translucent CoinGL rgb_error=" << rgbError
                << " max_rgb_error=" << maxRgbError << " center="
                << int(translucentImage[center]) << ','
                << int(translucentImage[center + 1]) << ','
                << int(translucentImage[center + 2]) << '\n';
      if (!check(maxRgbError <= 1,
                 "translucent foreground differs from CoinGL beyond one level")) return 1;
    }
    SoSeparator * secondTranslucent = makeDepthQuad(
      0.6f, 0.1f, SbColor(0, 1, 0), nullptr);
    static_cast<SoMaterial *>(secondTranslucent->getChild(2))->transparency = 0.5f;
    auto * secondTranslucentLayer = source.addSuperimposition(secondTranslucent,
      SoRenderManager::Superimposition::AUTOREDRAW);
    depthGLSource.addSuperimposition(secondTranslucent,
      SoRenderManager::Superimposition::AUTOREDRAW);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "second translucent foreground frame failed")) return 1;
    std::vector<uint8_t> twoTranslucent;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(twoTranslucent);
    if (!check(twoTranslucent[center + 1] > translucentImage[center + 1] + 50 &&
               twoTranslucent[center] < translucentImage[center] &&
               twoTranslucent[center + 2] < translucentImage[center + 2],
               "second translucent layer did not blend in registration order")) return 1;
    const std::vector<uint8_t> glTwoTranslucent = captureDepthGL();
    if (!glTwoTranslucent.empty()) {
      uint64_t rgbError = 0;
      uint32_t maxRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel) {
          const uint32_t difference = std::abs(int(twoTranslucent[pixel * 4 + channel]) -
                                               int(glTwoTranslucent[pixel * 3 + channel]));
          rgbError += difference;
          maxRgbError = std::max(maxRgbError, difference);
        }
      std::cout << "foreground_two_translucent CoinGL rgb_error=" << rgbError
                << " max_rgb_error=" << maxRgbError << " center="
                << int(twoTranslucent[center]) << ','
                << int(twoTranslucent[center + 1]) << ','
                << int(twoTranslucent[center + 2]) << '\n';
      if (!check(maxRgbError <= 1,
                 "two translucent foreground layers differ from CoinGL beyond one level")) return 1;
    }
    source.removeSuperimposition(secondTranslucentLayer);
    depthGLSource.removeSuperimposition(depthGLSource.getSuperimposition(1));
    secondTranslucent->unref();
    source.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
    depthGLSource.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
    translucentLayer->setTransparencyType(SoGLRenderAction::BLEND);
    depthGLSource.getSuperimposition(0)->setTransparencyType(SoGLRenderAction::BLEND);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "foreground BLEND transparency override failed")) return 1;
    std::vector<uint8_t> overrideImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(overrideImage);
    if (!check(overrideImage == translucentImage,
               "foreground BLEND override changed the inherited BLEND image")) return 1;
    const std::vector<uint8_t> glOverride = captureDepthGL();
    if (!glOverride.empty()) {
      uint32_t maxRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel)
          maxRgbError = std::max(maxRgbError,
            static_cast<uint32_t>(std::abs(int(overrideImage[pixel * 4 + channel]) -
                                           int(glOverride[pixel * 3 + channel]))));
      std::cout << "foreground_transparency_override CoinGL max_rgb_error="
                << maxRgbError << '\n';
      if (!check(maxRgbError <= 1,
                 "foreground BLEND override differs from CoinGL")) return 1;
    }
    source.removeSuperimposition(translucentLayer);
    depthGLSource.removeSuperimposition(depthGLSource.getSuperimposition(0));
    auto * inheritedScreenDoor = source.addSuperimposition(translucentForeground,
      SoRenderManager::Superimposition::AUTOREDRAW);
    depthGLSource.addSuperimposition(translucentForeground,
      SoRenderManager::Superimposition::AUTOREDRAW);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "foreground inherited SCREEN_DOOR frame failed")) return 1;
    std::vector<uint8_t> screenDoorImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(screenDoorImage);
    if (!check(screenDoorImage != overrideImage,
               "foreground transparency override did not differ from inheritance")) return 1;
    std::cout << "foreground_transparency_override inherited_diff=pass\n";
    depthGLAction.setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
    const std::vector<uint8_t> glScreenDoor = captureDepthGL();
    source.removeSuperimposition(inheritedScreenDoor);
    depthGLSource.removeSuperimposition(depthGLSource.getSuperimposition(0));
    source.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
    depthGLAction.setTransparencyType(SoGLRenderAction::BLEND);
    auto * screenDoorOverride = source.addSuperimposition(translucentForeground,
      SoRenderManager::Superimposition::AUTOREDRAW);
    screenDoorOverride->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
    auto * glScreenDoorOverride = depthGLSource.addSuperimposition(translucentForeground,
      SoRenderManager::Superimposition::AUTOREDRAW);
    glScreenDoorOverride->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "foreground SCREEN_DOOR transparency override failed")) return 1;
    std::vector<uint8_t> screenDoorOverrideImage;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(screenDoorOverrideImage);
    if (!check(screenDoorOverrideImage == screenDoorImage,
               "SCREEN_DOOR override changed the inherited SCREEN_DOOR image")) return 1;
    const std::vector<uint8_t> glScreenDoorOverrideImage = captureDepthGL();
    if (!glScreenDoor.empty()) {
      if (!check(glScreenDoorOverrideImage == glScreenDoor,
                 "CoinGL SCREEN_DOOR override changed the inherited image")) return 1;
      uint32_t maxRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel)
          maxRgbError = std::max(maxRgbError,
            static_cast<uint32_t>(std::abs(int(screenDoorOverrideImage[pixel * 4 + channel]) -
                                           int(glScreenDoorOverrideImage[pixel * 3 + channel]))));
      std::cout << "foreground_SCREEN_DOOR_override CoinGL max_rgb_error="
                << maxRgbError << '\n';
      if (!check(maxRgbError == 0,
                 "foreground SCREEN_DOOR override differs from CoinGL")) return 1;
    }
    source.removeSuperimposition(screenDoorOverride);
    depthGLSource.removeSuperimposition(glScreenDoorOverride);
    auto checkScreenDoorDepth = [&](uint32_t flags, const char * label,
                                    bool expectBlue) {
      auto * layer = source.addSuperimposition(translucentForeground, flags);
      layer->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
      auto * glLayer = depthGLSource.addSuperimposition(translucentForeground, flags);
      glLayer->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
      const bool rendered = adapter.render() == CoinRenderAction::SUCCESS;
      std::vector<uint8_t> pixels;
      if (rendered) adapter.getSceneManager()->getRenderTarget()->readbackRGBA(pixels);
      const std::vector<uint8_t> glPixels = captureDepthGL();
      source.removeSuperimposition(layer);
      depthGLSource.removeSuperimposition(glLayer);
      if (!check(rendered && pixels.size() == 64 * 64 * 4,
                 "SCREEN_DOOR depth layer failed")) return false;
      uint32_t blueCount = 0, maxRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel) {
        blueCount += pixels[pixel * 4 + 2] > 128;
        if (!glPixels.empty())
          for (size_t channel = 0; channel < 3; ++channel)
            maxRgbError = std::max(maxRgbError,
              static_cast<uint32_t>(std::abs(int(pixels[pixel * 4 + channel]) -
                                             int(glPixels[pixel * 3 + channel]))));
      }
      std::cout << "foreground_SCREEN_DOOR_depth " << label
                << " blue=" << blueCount << " CoinGL max_rgb_error="
                << maxRgbError << '\n';
      return check((blueCount > 0) == expectBlue && maxRgbError == 0,
                   "SCREEN_DOOR depth/clear differs from CoinGL");
    };
    if (!checkScreenDoorDepth(SoRenderManager::Superimposition::AUTOREDRAW |
                              SoRenderManager::Superimposition::ZBUFFERON,
                              "front_ZBUFFERON", true)) return 1;
    const SbVec3f rearScreenDoorCorners[] = {
      SbVec3f(-0.2f, -0.2f, -0.5f), SbVec3f(0.2f, -0.2f, -0.5f),
      SbVec3f(0.2f, 0.2f, -0.5f), SbVec3f(-0.2f, 0.2f, -0.5f)
    };
    static_cast<SoCoordinate3 *>(translucentForeground->getChild(3))->point.setValues(
      0, 4, rearScreenDoorCorners);
    if (!checkScreenDoorDepth(SoRenderManager::Superimposition::AUTOREDRAW |
                              SoRenderManager::Superimposition::ZBUFFERON,
                              "rear_ZBUFFERON", false)) return 1;
    if (!checkScreenDoorDepth(SoRenderManager::Superimposition::AUTOREDRAW |
                              SoRenderManager::Superimposition::ZBUFFERON |
                              SoRenderManager::Superimposition::CLEARZBUFFER,
                              "rear_CLEARZBUFFER", true)) return 1;
    auto checkBlendDepth = [&](uint32_t flags, const char * label, bool expectBlue) {
      auto * layer = source.addSuperimposition(translucentForeground, flags);
      layer->setTransparencyType(SoGLRenderAction::BLEND);
      auto * glLayer = depthGLSource.addSuperimposition(translucentForeground, flags);
      glLayer->setTransparencyType(SoGLRenderAction::BLEND);
      const bool rendered = adapter.render() == CoinRenderAction::SUCCESS;
      std::vector<uint8_t> pixels;
      if (rendered) adapter.getSceneManager()->getRenderTarget()->readbackRGBA(pixels);
      const std::vector<uint8_t> glPixels = captureDepthGL();
      source.removeSuperimposition(layer);
      depthGLSource.removeSuperimposition(glLayer);
      if (!check(rendered && pixels.size() == 64 * 64 * 4,
                 "BLEND depth layer failed")) return false;
      uint32_t blueCount = 0, maxRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel) {
        blueCount += pixels[pixel * 4 + 2] > 32;
        if (!glPixels.empty())
          for (size_t channel = 0; channel < 3; ++channel)
            maxRgbError = std::max(maxRgbError,
              static_cast<uint32_t>(std::abs(int(pixels[pixel * 4 + channel]) -
                                             int(glPixels[pixel * 3 + channel]))));
      }
      std::cout << "foreground_BLEND_depth " << label << " blue="
                << blueCount << " CoinGL max_rgb_error=" << maxRgbError << '\n';
      return check((blueCount > 0) == expectBlue && maxRgbError <= 1,
                   "BLEND depth/clear differs from CoinGL");
    };
    const SbVec3f frontBlendCorners[] = {
      SbVec3f(-0.2f, -0.2f, 0.5f), SbVec3f(0.2f, -0.2f, 0.5f),
      SbVec3f(0.2f, 0.2f, 0.5f), SbVec3f(-0.2f, 0.2f, 0.5f)
    };
    static_cast<SoCoordinate3 *>(translucentForeground->getChild(3))->point.setValues(
      0, 4, frontBlendCorners);
    if (!checkBlendDepth(SoRenderManager::Superimposition::AUTOREDRAW |
                         SoRenderManager::Superimposition::ZBUFFERON,
                         "front_ZBUFFERON", true)) return 1;
    static_cast<SoCoordinate3 *>(translucentForeground->getChild(3))->point.setValues(
      0, 4, rearScreenDoorCorners);
    if (!checkBlendDepth(SoRenderManager::Superimposition::AUTOREDRAW |
                         SoRenderManager::Superimposition::ZBUFFERON,
                         "rear_ZBUFFERON", false)) return 1;
    if (!checkBlendDepth(SoRenderManager::Superimposition::AUTOREDRAW |
                         SoRenderManager::Superimposition::ZBUFFERON |
                         SoRenderManager::Superimposition::CLEARZBUFFER,
                         "rear_CLEARZBUFFER", true)) return 1;
    static_cast<SoCoordinate3 *>(translucentForeground->getChild(3))->point.setValues(
      0, 4, frontBlendCorners);
    SoSeparator * mixedGreen = makeDepthQuad(0.25f, 0.3f, SbColor(0, 1, 0), nullptr);
    static_cast<SoMaterial *>(mixedGreen->getChild(2))->transparency = 0.5f;
    const uint32_t mixedFlags = SoRenderManager::Superimposition::AUTOREDRAW;
    auto * blendLayer = source.addSuperimposition(translucentForeground, mixedFlags);
    blendLayer->setTransparencyType(SoGLRenderAction::BLEND);
    auto * doorLayer = source.addSuperimposition(mixedGreen, mixedFlags);
    doorLayer->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
    auto * glBlendLayer = depthGLSource.addSuperimposition(translucentForeground, mixedFlags);
    glBlendLayer->setTransparencyType(SoGLRenderAction::BLEND);
    auto * glDoorLayer = depthGLSource.addSuperimposition(mixedGreen, mixedFlags);
    glDoorLayer->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "mixed BLEND/SCREEN_DOOR layers failed")) return 1;
    std::vector<uint8_t> mixedTransparency;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(mixedTransparency);
    const std::vector<uint8_t> glMixedTransparency = captureDepthGL();
    if (!glMixedTransparency.empty()) {
      uint32_t maxRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel)
          maxRgbError = std::max(maxRgbError,
            static_cast<uint32_t>(std::abs(int(mixedTransparency[pixel * 4 + channel]) -
                                           int(glMixedTransparency[pixel * 3 + channel]))));
      std::cout << "foreground_mixed_transparency CoinGL max_rgb_error="
                << maxRgbError << '\n';
      if (!check(maxRgbError <= 1,
                 "mixed BLEND/SCREEN_DOOR differs from CoinGL")) return 1;
    }
    source.removeSuperimposition(doorLayer);
    source.removeSuperimposition(blendLayer);
    depthGLSource.removeSuperimposition(glDoorLayer);
    depthGLSource.removeSuperimposition(glBlendLayer);
    const std::vector<uint8_t> mixedForwardImage = mixedTransparency;
    doorLayer = source.addSuperimposition(mixedGreen, mixedFlags);
    doorLayer->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
    blendLayer = source.addSuperimposition(translucentForeground, mixedFlags);
    blendLayer->setTransparencyType(SoGLRenderAction::BLEND);
    glDoorLayer = depthGLSource.addSuperimposition(mixedGreen, mixedFlags);
    glDoorLayer->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
    glBlendLayer = depthGLSource.addSuperimposition(translucentForeground, mixedFlags);
    glBlendLayer->setTransparencyType(SoGLRenderAction::BLEND);
    if (!check(adapter.render() == CoinRenderAction::SUCCESS,
               "mixed SCREEN_DOOR/BLEND layers failed")) return 1;
    adapter.getSceneManager()->getRenderTarget()->readbackRGBA(mixedTransparency);
    if (!check(mixedTransparency != mixedForwardImage,
               "reversing mixed layers did not change the CoinRender image")) return 1;
    const std::vector<uint8_t> glMixedReverse = captureDepthGL();
    if (!glMixedReverse.empty()) {
      if (!check(glMixedReverse != glMixedTransparency,
                 "reversing mixed layers did not change the CoinGL image")) return 1;
      uint32_t maxRgbError = 0;
      for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel)
          maxRgbError = std::max(maxRgbError,
            static_cast<uint32_t>(std::abs(int(mixedTransparency[pixel * 4 + channel]) -
                                           int(glMixedReverse[pixel * 3 + channel]))));
      std::cout << "foreground_mixed_transparency_reverse CoinGL max_rgb_error="
                << maxRgbError << '\n';
      if (!check(maxRgbError <= 1,
                 "mixed SCREEN_DOOR/BLEND differs from CoinGL")) return 1;
    }
    source.removeSuperimposition(blendLayer);
    source.removeSuperimposition(doorLayer);
    depthGLSource.removeSuperimposition(glBlendLayer);
    depthGLSource.removeSuperimposition(glDoorLayer);
    auto captureMixedDepth = [&](bool reverse, uint32_t secondFlags, const char * label,
                                 std::vector<uint8_t> & image) {
      const uint32_t firstFlags = SoRenderManager::Superimposition::AUTOREDRAW |
                                  SoRenderManager::Superimposition::ZBUFFERON;
      SoNode * firstRoot = reverse ? static_cast<SoNode *>(mixedGreen) : translucentForeground;
      SoNode * secondRoot = reverse ? static_cast<SoNode *>(translucentForeground) : mixedGreen;
      const auto firstType = reverse ? SoGLRenderAction::SCREEN_DOOR : SoGLRenderAction::BLEND;
      const auto secondType = reverse ? SoGLRenderAction::BLEND : SoGLRenderAction::SCREEN_DOOR;
      auto * first = source.addSuperimposition(firstRoot, firstFlags);
      first->setTransparencyType(firstType);
      auto * second = source.addSuperimposition(secondRoot, secondFlags);
      second->setTransparencyType(secondType);
      auto * glFirst = depthGLSource.addSuperimposition(firstRoot, firstFlags);
      glFirst->setTransparencyType(firstType);
      auto * glSecond = depthGLSource.addSuperimposition(secondRoot, secondFlags);
      glSecond->setTransparencyType(secondType);
      const bool rendered = adapter.render() == CoinRenderAction::SUCCESS;
      if (rendered) adapter.getSceneManager()->getRenderTarget()->readbackRGBA(image);
      const std::vector<uint8_t> glPixels = captureDepthGL();
      source.removeSuperimposition(second);
      source.removeSuperimposition(first);
      depthGLSource.removeSuperimposition(glSecond);
      depthGLSource.removeSuperimposition(glFirst);
      if (!check(rendered && image.size() == 64 * 64 * 4,
                 "mixed depth layers failed")) return false;
      uint32_t maxRgbError = 0;
      if (!glPixels.empty())
        for (size_t pixel = 0; pixel < 64 * 64; ++pixel)
          for (size_t channel = 0; channel < 3; ++channel)
            maxRgbError = std::max(maxRgbError,
              static_cast<uint32_t>(std::abs(int(image[pixel * 4 + channel]) -
                                             int(glPixels[pixel * 3 + channel]))));
      std::cout << "foreground_mixed_depth " << label
                << " CoinGL max_rgb_error=" << maxRgbError << '\n';
      return check(maxRgbError <= 1, "mixed depth layers differ from CoinGL");
    };
    std::vector<uint8_t> mixedDepthBlocked, mixedDepthCleared;
    if (!captureMixedDepth(false, SoRenderManager::Superimposition::AUTOREDRAW |
                           SoRenderManager::Superimposition::ZBUFFERON,
                           "ZBUFFERON", mixedDepthBlocked)) return 1;
    if (!captureMixedDepth(false, SoRenderManager::Superimposition::AUTOREDRAW |
                           SoRenderManager::Superimposition::ZBUFFERON |
                           SoRenderManager::Superimposition::CLEARZBUFFER,
                           "CLEARZBUFFER", mixedDepthCleared)) return 1;
    if (!check(mixedDepthBlocked != mixedDepthCleared,
               "CLEARZBUFFER did not change mixed transparency")) return 1;
    static_cast<SoCoordinate3 *>(translucentForeground->getChild(3))->point.setValues(
      0, 4, rearScreenDoorCorners);
    if (!captureMixedDepth(true, SoRenderManager::Superimposition::AUTOREDRAW |
                           SoRenderManager::Superimposition::ZBUFFERON,
                           "reverse_ZBUFFERON", mixedDepthBlocked)) return 1;
    if (!captureMixedDepth(true, SoRenderManager::Superimposition::AUTOREDRAW |
                           SoRenderManager::Superimposition::ZBUFFERON |
                           SoRenderManager::Superimposition::CLEARZBUFFER,
                           "reverse_CLEARZBUFFER", mixedDepthCleared)) return 1;
    if (!check(mixedDepthBlocked != mixedDepthCleared,
               "CLEARZBUFFER did not change reverse mixed transparency")) return 1;
    mixedGreen->unref();
    translucentForeground->unref();
    source.setSceneGraph(nullptr);
    depthGLSource.setSceneGraph(nullptr);
    mixedForegroundRoot->unref();
    backgroundLayerRoot->unref();
    depthOverlay->unref();
    depthBase->unref();
  }


  CoinRenderSceneManager * manager = new CoinRenderSceneManager(SbVec2i32(8, 8));
#if defined(HAVE_COIN_BGFX)
  manager->getRenderTarget()->setDepthReadbackEnabled(FALSE);
#endif
  if (!check(manager->getRenderTarget() &&
             manager->render() == CoinRenderAction::INVALID_SCENE &&
             manager->getLastError().getLength() > 0,
             "missing scene was not rejected")) return 1;
  SoSeparator * root = new SoSeparator;
  root->ref();
  manager->setSceneGraph(root);
  root->unref(); // Manager retains its own scene reference.
  manager->setBackgroundColor(SbColor4f(0.2f, 0.4f, 0.6f, 0.35f));
  if (!check(manager->getSceneGraph() == root &&
             manager->render() == CoinRenderAction::SUCCESS,
             "manager failed to render retained scene")) return 1;
  std::vector<uint8_t> color;
  manager->getRenderTarget()->readbackRGBA(color);
  unsigned legacyPngRoundtrips = 0;
  unsigned legacyTiffRoundtrips = 0;
  unsigned legacyTiffAliasRoundtrips = 0;
  if (!check(color.size() == 8u * 8u * 4u &&
             color[0] >= 50 && color[0] <= 52 &&
             color[1] >= 101 && color[1] <= 103 &&
             color[2] >= 152 && color[2] <= 154 &&
             color[3] >= 88 && color[3] <= 90,
             "manager did not publish its clear color")) return 1;
  for (unsigned components = 1; components <= 4; ++components) {
    std::vector<uint8_t> legacy;
    if (!check(manager->getRenderTarget()->readbackLegacyComponents(
                 static_cast<CoinRenderLegacyComponents>(components), legacy) &&
               legacy.size() == 8u * 8u * components,
               "legacy component readback failed")) return 1;
    const unsigned char expectedLuminance = static_cast<unsigned char>(
      double(color[0]) * 0.30 + double(color[1]) * 0.59 + double(color[2]) * 0.11);
    if (!check(legacy[0] == (components < 3 ? expectedLuminance : color[0]) &&
               (components != 2 || legacy[1] == color[3]) &&
               (components != 4 || (legacy[1] == color[1] &&
                                     legacy[2] == color[2] &&
                                     legacy[3] == color[3])),
               "legacy component readback differs from RGBA publication")) return 1;
    std::vector<uint8_t> encoded;
    if (!check(manager->getRenderTarget()->encodeLegacyRGB(
                 static_cast<CoinRenderLegacyComponents>(components), encoded) &&
               encoded.size() == 512u + 8u * 8u * components &&
               encoded[512] == legacy[0],
               "SGI RGB export failed after offscreen publication")) return 1;
    std::vector<uint8_t> postscript;
    if (!check(manager->getRenderTarget()->encodeLegacyPostScript(
                 static_cast<CoinRenderLegacyComponents>(components),
                 SbVec2f(8.5f, 11.0f), 72.0f, postscript) &&
               postscript.size() > 400u && postscript[0] == '%' &&
               postscript[1] == '!',
               "PostScript export failed after offscreen publication")) return 1;
#ifdef __linux__
    const simage_wrapper_t * codec = simage_wrapper();
    for (const char * extension : {"png", "tiff", "tif", "TIFF"}) {
      const bool tiffAlias = std::strcmp(extension, "tif") == 0 ||
                             std::strcmp(extension, "TIFF") == 0;
      if (tiffAlias && components != 2) continue;
      if (!codec->available || !codec->versionMatchesAtLeast(1, 1, 0) ||
          !codec->simage_check_save_supported(extension)) continue;
      char path[] = "/tmp/coinrender-codec-XXXXXX";
      const int descriptor = mkstemp(path);
      if (!check(descriptor >= 0, "temporary codec path unavailable")) return 1;
      close(descriptor);
      const bool written = manager->getRenderTarget()->writeLegacyImageFile(
        SbString(path), SbString(extension),
        static_cast<CoinRenderLegacyComponents>(components)) != FALSE;
      int width = 0, height = 0, decodedChannels = 0;
      unsigned char * decoded = written ?
        codec->simage_read_image(path, &width, &height, &decodedChannels) : nullptr;
      const bool tiffLA = std::strcmp(extension, "png") != 0 && components == 2;
      bool matches = decoded && width == 8 && height == 8 &&
        decodedChannels == (tiffLA ? 4 : int(components));
      if (matches && tiffLA) {
        for (size_t pixel = 0; pixel < 64; ++pixel)
          for (size_t channel = 0; channel < 4; ++channel)
            matches &= decoded[pixel * 4 + channel] ==
              legacy[pixel * 2 + (channel == 3 ? 1 : 0)];
      } else if (matches) matches = std::memcmp(decoded, legacy.data(), legacy.size()) == 0;
      if (decoded) codec->simage_free_image(decoded);
      std::remove(path);
      if (std::strcmp(extension, "png") == 0) {
        if (!check(matches, "PNG codec roundtrip differs from legacy component buffer")) return 1;
        ++legacyPngRoundtrips;
      } else {
        if (!check(matches, "TIFF codec roundtrip lost a legacy component")) return 1;
        if (tiffAlias) ++legacyTiffAliasRoundtrips;
        else ++legacyTiffRoundtrips;
      }
    }
#endif
  }
  std::cout << "legacy_png_roundtrips=" << legacyPngRoundtrips
            << " legacy_tiff_roundtrips=" << legacyTiffRoundtrips
            << " legacy_tiff_la_alias_roundtrips=" << legacyTiffAliasRoundtrips << '\n';
  // Export must retain the full target dimensions and row order when the
  // rendered content occupies only an offset viewport.
  SoOrthographicCamera * exportCamera = new SoOrthographicCamera;
  exportCamera->position.setValue(0, 0, 4);
  exportCamera->height = 2;
  root->addChild(exportCamera);
  SoMaterial * exportMaterial = new SoMaterial;
  exportMaterial->diffuseColor.setValue(1, 0, 0);
  root->addChild(exportMaterial);
  SoCube * exportCube = new SoCube;
  exportCube->width = exportCube->height = exportCube->depth = 1;
  root->addChild(exportCube);
  SbViewportRegion exportViewport(8, 8);
  exportViewport.setViewportPixels(2, 1, 4, 6);
  manager->setViewportRegion(exportViewport);
  if (!check(manager->render() == CoinRenderAction::SUCCESS,
             "subviewport export scene failed")) return 1;
  std::vector<uint8_t> viewportRGBA;
  manager->getRenderTarget()->readbackRGBA(viewportRGBA);
  if (!check(viewportRGBA.size() == 8u * 8u * 4u &&
             viewportRGBA != color &&
             viewportRGBA[(4u * 8u + 4u) * 4u] != viewportRGBA[0] &&
             viewportRGBA[0] == color[0],
             "subviewport content did not change the published image")) return 1;
  for (unsigned components = 1; components <= 4; ++components) {
    std::vector<uint8_t> legacy, encoded;
    if (!check(manager->getRenderTarget()->readbackLegacyComponents(
                 static_cast<CoinRenderLegacyComponents>(components), legacy) &&
               manager->getRenderTarget()->encodeLegacyRGB(
                 static_cast<CoinRenderLegacyComponents>(components), encoded) &&
               legacy.size() == 64u * components &&
               encoded.size() == 512u + legacy.size(),
               "subviewport component/SGI export lost a pixel")) return 1;
    for (size_t pixel = 0; pixel < 64; ++pixel)
      for (unsigned channel = 0; channel < components; ++channel)
        if (!check(encoded[512 + size_t(channel) * 64 + pixel] ==
                   legacy[pixel * components + channel],
                   "subviewport SGI plane differs from published components")) return 1;
#ifdef __linux__
    const simage_wrapper_t * codec = simage_wrapper();
    for (const char * extension : {"png", "tiff"}) {
      if (!codec->available || !codec->versionMatchesAtLeast(1, 1, 0) ||
          !codec->simage_check_save_supported(extension)) continue;
      char path[] = "/tmp/coinrender-viewport-codec-XXXXXX";
      const int descriptor = mkstemp(path);
      if (!check(descriptor >= 0, "subviewport codec path unavailable")) return 1;
      close(descriptor);
      const bool written = manager->getRenderTarget()->writeLegacyImageFile(
        SbString(path), SbString(extension),
        static_cast<CoinRenderLegacyComponents>(components)) != FALSE;
      int width = 0, height = 0, channels = 0;
      unsigned char * decoded = written ?
        codec->simage_read_image(path, &width, &height, &channels) : nullptr;
      const bool tiffLA = std::strcmp(extension, "tiff") == 0 && components == 2;
      bool matches = decoded && width == 8 && height == 8 &&
        channels == (tiffLA ? 4 : int(components));
      if (matches) for (size_t pixel = 0; pixel < 64; ++pixel)
        for (int channel = 0; channel < channels; ++channel)
          matches &= decoded[pixel * channels + channel] ==
            legacy[pixel * components + (tiffLA ? (channel == 3 ? 1 : 0) : channel)];
      if (decoded) codec->simage_free_image(decoded);
      std::remove(path);
      if (!check(matches, "subviewport PNG/TIFF differs from published components")) return 1;
    }
#endif
  }
  std::cout << "legacy_subviewport_exports=4" << '\n';
  manager->setViewportRegion(SbViewportRegion(8, 8));
  root->removeAllChildren();
  if (!check(manager->resize(SbVec2i32(0, 0)) &&
             manager->render() == CoinRenderAction::NOT_READY,
             "zero-size suspension was not propagated")) return 1;
  if (!check(manager->resize(SbVec2i32(16, 16)) &&
             manager->render() == CoinRenderAction::SUCCESS,
             "manager did not recover after resize")) return 1;

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE) || defined(HAVE_COIN_BGFX)
  manager->getRenderTarget()->setDepthReadbackEnabled(TRUE);
  CoinRenderReadbackTicket ticket{};
  if (!check(manager->renderAsync(ticket) == CoinRenderAction::SUCCESS &&
             ticket.token != 0 && ticket.width == 16 && ticket.height == 16,
             "manager did not return an offscreen async ticket")) return 1;
  delete manager;
  std::vector<uint8_t> asyncColor;
  std::vector<float> asyncDepth;
  SbString diagnostic;
  CoinRenderTarget::ReadbackStatus status = CoinRenderTarget::READBACK_NOT_READY;
  for (int i = 0; i < 5000 && status == CoinRenderTarget::READBACK_NOT_READY; ++i) {
    status = CoinRenderTarget::pollReadback(ticket, asyncColor, asyncDepth, &diagnostic);
    if (status == CoinRenderTarget::READBACK_NOT_READY)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!check(status == CoinRenderTarget::READBACK_READY &&
             asyncColor.size() == 16u * 16u * 4u &&
             asyncDepth.size() == 16u * 16u,
             "async ticket did not survive manager/target destruction")) return 1;
#else
  CoinRenderReadbackTicket ticket{};
  if (!check(manager->renderAsync(ticket) == CoinRenderAction::UNSUPPORTED &&
             ticket.token == 0,
             "Recording manager did not reject async readback")) return 1;
  delete manager;
#endif
  std::cout << "CoinRenderProductTest passed\n";
  return 0;
}
