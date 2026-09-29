#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

namespace {
bool check(bool value, const char* message) {
  if (!value)
    std::cerr << "CoinRenderSelectionTest: " << message << '\n';
  return value;
}
struct Environment {
  const char* name;
  std::string previous;
  bool present;
  Environment(const char* name) : name(name), present(std::getenv(name) != nullptr) {
    if (present)
      previous = std::getenv(name);
  }
  ~Environment() {
    if (present)
      setenv(name, previous.c_str(), 1);
    else
      unsetenv(name);
  }
};
bool selectionCore() {
  CoinRenderCapabilities caps{};
  caps.struct_size = sizeof(caps);
  caps.version = COIN_RENDER_CAPABILITIES_VERSION;
  caps.implemented_mechanisms = COIN_RENDER_MECHANISM_OBJECT | COIN_RENDER_MECHANISM_PEELING;
  caps.available_mechanisms = COIN_RENDER_MECHANISM_OBJECT | COIN_RENDER_MECHANISM_WEIGHTED_OIT;
  caps.qualified_profile_mechanisms = COIN_RENDER_MECHANISM_PEELING;
  if (!check(coin_render_select_mechanism(&caps, COIN_RENDER_MECHANISM_WEIGHTED_OIT, 0).reason ==
                 COIN_RENDER_SELECTION_NOT_IMPLEMENTED,
             "hardware does not imply implementation") ||
      !check(coin_render_select_mechanism(&caps, COIN_RENDER_MECHANISM_PEELING, 0).reason ==
                 COIN_RENDER_SELECTION_HARDWARE_UNAVAILABLE,
             "qualification does not imply availability") ||
      !check(coin_render_select_mechanism(&caps, COIN_RENDER_MECHANISM_OBJECT, 1).reason ==
                 COIN_RENDER_SELECTION_UNQUALIFIED_PROFILE,
             "execution does not imply qualification") ||
      !check(coin_render_select_mechanism(&caps, COIN_RENDER_MECHANISM_OBJECT, 0).reason ==
                 COIN_RENDER_SELECTION_SUPPORTED,
             "explicit experimental execution") ||
      !check(coin_render_select_mechanism(&caps, 3, 0).reason ==
                 COIN_RENDER_SELECTION_INVALID_REQUEST,
             "selection must request one mechanism") ||
      !check(coin_render_select_mechanism(nullptr, 1, 0).reason ==
                 COIN_RENDER_SELECTION_INVALID_REQUEST,
             "null capability record"))
    return false;
  caps.backend = COIN_RENDER_EXPERIMENTAL_RUST;
  caps.probe_status = COIN_RENDER_PROBE_BUSY;
  if (!check(coin_render_select_mechanism(&caps, COIN_RENDER_MECHANISM_PEELING, 0).reason ==
                 COIN_RENDER_SELECTION_RUNTIME_NOT_READY,
             "busy runtime does not mean absent hardware"))
    return false;
  if (!check(coin_render_select_mechanism(&caps, 3, 0).reason ==
                 COIN_RENDER_SELECTION_INVALID_REQUEST,
             "invalid request takes precedence over busy runtime"))
    return false;
  caps.probe_status = COIN_RENDER_PROBE_AVAILABLE;
  caps.available_mechanisms |= COIN_RENDER_MECHANISM_PEELING;
  const auto peel = coin_render_select_mechanism(&caps, COIN_RENDER_MECHANISM_PEELING, 1);
  return check(peel.reason == COIN_RENDER_SELECTION_SUPPORTED && peel.qualified_profile &&
                   peel.mechanism == COIN_RENDER_MECHANISM_PEELING,
               "bounded qualified selection");
}
SoSeparator* scene() {
  auto* root = new SoSeparator;
  root->ref();
  auto* camera = new SoOrthographicCamera;
  camera->position = SbVec3f(0, 0, 5);
  camera->height = 2;
  root->addChild(camera);
  auto* light = new SoLightModel;
  light->model = SoLightModel::BASE_COLOR;
  root->addChild(light);
  auto* material = new SoMaterial;
  material->diffuseColor = SbColor(1, 0, 0);
  material->transparency = .5f;
  root->addChild(material);
  auto* coordinates = new SoCoordinate3;
  const SbVec3f points[] = {SbVec3f(-.8f, -.8f, 0), SbVec3f(.8f, -.8f, 0), SbVec3f(0, .8f, 0)};
  coordinates->point.setValues(0, 3, points);
  root->addChild(coordinates);
  auto* face = new SoFaceSet;
  face->numVertices = 3;
  root->addChild(face);
  return root;
}
bool renderModes(bool cpu) {
  SoSeparator* root = scene();
  CoinRenderOptions options;
  options.transparency = COIN_RENDER_TRANSPARENCY_OBJECT;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), options));
  if (cpu)
    target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  CoinRenderAction action(SbViewportRegion(32, 32));
  action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  action.apply(root);
  std::vector<uint8_t> before, after;
  target->readbackRGBA(before);
  const auto serial = target->getLastSubmissionSerial();
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS && before.size() == 32 * 32 * 4,
             "typed object selection renders")) {
    root->unref();
    return false;
  }
  const size_t pixel = (20 * 32 + 16) * 4;
  if (!check(std::abs(int(before[pixel]) - 128) <= 2 && before[pixel + 1] == 0,
             "selected executor publishes expected alpha blend")) {
    root->unref();
    return false;
  }
  action.setTransparencyType(CoinRenderAction::SORTED_LAYERS_BLEND);
  action.apply(root);
  target->readbackRGBA(after);
  if (!check(action.getLastStatus() == CoinRenderAction::UNSUPPORTED && before == after &&
                 target->getLastSubmissionSerial() == serial &&
                 std::strstr(action.getLastError().getString(), "no fallback") != nullptr,
             "object conflict preserves frame and serial")) {
    root->unref();
    return false;
  }
  action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  action.apply(root);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
             "recovery after selection conflict")) {
    root->unref();
    return false;
  }
  action.setRenderTarget(nullptr);
  target.reset();
  for (auto mode : {COIN_RENDER_TRANSPARENCY_COIN, COIN_RENDER_TRANSPARENCY_PEELING,
                    COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT}) {
    options.transparency = mode;
    action.setRenderTarget(nullptr);
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), options));
    if (cpu)
      target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    action.setRenderTarget(target.get());
    action.setTransparencyType(mode == COIN_RENDER_TRANSPARENCY_COIN
                                   ? CoinRenderAction::SORTED_LAYERS_BLEND
                                   : CoinRenderAction::SORTED_OBJECT_BLEND);
    action.apply(root);
    bool implemented = mode != COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
#ifdef HAVE_COIN_BGFX
    if (!cpu)
      implemented = true;
#endif
    if (!check(action.getLastStatus() ==
                   (implemented ? CoinRenderAction::SUCCESS : CoinRenderAction::UNSUPPORTED),
               "explicit mechanism implementation split")) {
      root->unref();
      return false;
    }
  }
  for (int invalid = 0; invalid < 3; ++invalid) {
    CoinRenderOptions bad;
    if (invalid == 0)
      bad.renderer = COIN_RENDER_RENDERER_OTHER;
    if (invalid == 1)
      bad.transparency = static_cast<CoinRenderTransparencyMode>(99);
    if (invalid == 2)
      bad.sceneTexture = static_cast<CoinRenderSceneTextureMode>(99);
    action.setRenderTarget(nullptr);
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), bad));
    action.setRenderTarget(target.get());
    action.apply(root);
    if (!check(action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                   target->getLastSubmissionSerial() == 0,
               "invalid typed options do not submit")) {
      root->unref();
      return false;
    }
  }
  root->unref();
  return true;
}
bool probe() {
  CoinRenderCapabilities caps{};
  if (!check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps,
                                            sizeof(caps)) == 0 &&
                 caps.version == 3 && caps.max_texture_units == 8 && caps.max_peel_layers == 8 &&
                 (caps.implemented_mechanisms & COIN_RENDER_MECHANISM_PEELING),
             "current compiled profile"))
    return false;
  if (caps.gpu_available && !check(caps.known_hardware_facts && caps.renderer &&
                                       (caps.format_rgba16f & COIN_RENDER_FORMAT_FRAMEBUFFER) &&
                                       (caps.available_mechanisms & COIN_RENDER_MECHANISM_PEELING),
                                   "runtime peeling facts"))
    return false;
  if (caps.gpu_available) {
    CoinRenderCapabilities explicitCaps{};
    if (!check(coin_render_query_capabilities_for_renderer(
                   COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
                   static_cast<CoinRenderRenderer>(caps.renderer), &explicitCaps,
                   sizeof(explicitCaps)) == 0 &&
                   explicitCaps.gpu_available && explicitCaps.renderer == caps.renderer,
               "explicit renderer probe"))
      return false;
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
    if (!check((caps.known_formats & COIN_RENDER_KNOWN_D24S8) == 0 && caps.format_d24s8 == 0,
               "abstract wgpu depth format is not concrete D24S8"))
      return false;
#endif
  }
  // Exact old sizes must not overwrite the next byte, including a V2 caller.
  for (const size_t oldSize : {offsetof(CoinRenderCapabilities, probe_status),
                               offsetof(CoinRenderCapabilities, known_hardware_facts)}) {
    std::array<unsigned char, sizeof(CoinRenderCapabilities) + 8> buffer;
    buffer.fill(0xa5);
    if (!check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN, buffer.data(),
                                              oldSize) == 0,
               "legacy capability layout"))
      return false;
    uint32_t version = 0;
    std::memcpy(&version, buffer.data() + 4, 4);
    if (!check(version == (oldSize == offsetof(CoinRenderCapabilities, probe_status) ? 1u : 2u) &&
                   buffer[oldSize] == 0xa5,
               "legacy version and write boundary"))
      return false;
  }
  CoinRenderCapabilities window{};
  if (coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW, &window,
                                     sizeof(window)) == 0)
    if (!check(window.qualified_profiles == 0 && window.qualified_profile_mechanisms == 0 &&
                   coin_render_select_mechanism(&window, COIN_RENDER_MECHANISM_PEELING, 1).reason ==
                       COIN_RENDER_SELECTION_UNQUALIFIED_PROFILE,
               "adapter probe does not qualify a window"))
      return false;
  return true;
}
} // namespace
int main() {
  SoDB::init();
  CoinRenderAction::initClass();
  if (!selectionCore() || !renderModes(true))
    return 1;
  if (!CoinRenderAction::isGpuBackendAvailable())
    return 77;
  if (!probe() || !renderModes(false))
    return 1;
  Environment direct("COIN_RENDER_RTT_GPU_DIRECT");
  setenv(direct.name, "1", 1);
  std::string diagnostic;
  auto defaults = CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if (!check(diagnostic.empty() && defaults.sceneTexture == COIN_RENDER_SCENE_TEXTURE_DIRECT,
             "Shell parses direct RTT"))
    return 1;
  CoinRenderOptions explicitOptions;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(8, 8), explicitOptions));
  if (!check(target->getOptions().sceneTexture == COIN_RENDER_SCENE_TEXTURE_STAGED,
             "typed options override environment"))
    return 1;
#ifdef HAVE_COIN_BGFX
  Environment mode("COIN_BGFX_TRANSPARENCY");
  setenv(mode.name, "invalid", 1);
  CoinRenderCapabilities caps{};
  if (!check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps,
                                            sizeof(caps)) == 0 &&
                 caps.gpu_available,
             "hardware probe is independent of transparency option"))
    return 1;
  target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(8, 8), explicitOptions));
  SoSeparator* empty = new SoSeparator;
  empty->ref();
  CoinRenderAction action(SbViewportRegion(8, 8));
  action.setRenderTarget(target.get());
  action.apply(empty);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
             "typed selection bypasses invalid environment"))
    return 1;
  action.setRenderTarget(nullptr);
  target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(8, 8)));
  action.setRenderTarget(target.get());
  action.apply(empty);
  empty->unref();
  if (!check(action.getLastStatus() == CoinRenderAction::UNSUPPORTED,
             "invalid textual option rejects explicitly"))
    return 1;
#endif
  std::cout << "P11 typed selection and capability matrix passed\n";
  return 0;
}
