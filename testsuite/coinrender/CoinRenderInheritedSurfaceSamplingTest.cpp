#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
// Reuse the immutable authored sampling fixture. Its old executable and gates
// remain unchanged; this executable adds only the finite surface consumers.
#define main coin_render_inherited_sampling_original_main
#include "CoinRenderInheritedShadowSamplingTest.cpp"
#undef main
#include <Inventor/nodes/SoTransparencyType.h>

namespace inherited_surfaces {
using inherited_sampling::Settings;
using inherited_sampling::RasterScene;
using inherited_sampling::pixels;
using inherited_sampling::imageOracle;
using inherited_sampling::Audit;

Settings configuration(unsigned n) {
  Settings s;
  const int units[] = {0, 3, 7, -1, -1, 7};
  const int maps[] = {1, 4, 8, 8, 4, 1};
  const int qualities[] = {0, 1, 2, 3, 1, 3};
  s.unit = units[n]; s.maps = maps[n]; s.quality = qualities[n];
  s.directional = n == 1 || n == 3 || n == 5;
  s.repeat = n == 0 || n == 2 || n == 4;
  s.holes = n >= 4;
  return s;
}
void selectPolicy(unsigned n) {
  samplingTestPolicy = n >= 4 ? COIN_RENDER_SAMPLING_PORTABLE : COIN_RENDER_SAMPLING_NATIVE;
  std::cout << "inherited_surface configuration=" << n
            << " sampling_policy=" << samplingTestPolicy << '\n';
}
class SurfaceAudit : public Audit {
public:
  mutable CoinRenderRttPlan graph;
  mutable CoinRenderFramePlan capturedRoot;
  explicit SurfaceAudit(std::unique_ptr<CoinRenderBackend> p) : Audit(std::move(p)) {}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g,
      const CoinRenderFramePlan &f, const SbVec2i32 &size) const override {
    graph = g; capturedRoot = f;
    return Proxy::preflightRtt(g, f, size);
  }
};
struct SurfaceHarness {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(side, side)};
  SurfaceAudit *audit = nullptr;
  explicit SurfaceHarness(const CoinRenderCapabilities &caps, unsigned samples,
      CoinRenderSceneTextureMode route = COIN_RENDER_SCENE_TEXTURE_STAGED) {
    auto options = samplingTestOptions();
    options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
    options.sampleCount = samples; options.sceneTexture = route;
    target.reset(samplingTestOffscreen(SbVec2i32(side, side), options));
    if (!check(bool(target), "MSAA physical target factory")) return;
    auto &state = target->getPimpl().get();
    // Preparing the runtime is not a scene application or a published frame.
    if (!check(state.prepareBackend() == CoinRenderBackendStatus::SUCCESS &&
               state.backend && state.backend->isGpuBackend() &&
               target->getLastSubmissionSerial() == 0,
               "real MSAA backend prepared with no prior publication")) {
      std::cerr << target->getLastError() << '\n'; target.reset(); return;
    }
    if (!check(target->getOptions().sampleCount == samples &&
               target->getOptions().renderer == caps.renderer &&
               target->getOptions().textureSamplingPolicy == samplingTestPolicy,
               "requested physical samples, API and sampling policy retained")) {
      target.reset(); return;
    }
    audit = new SurfaceAudit(std::move(state.backend)); state.backend.reset(audit);
    target->setDepthReadbackEnabled(TRUE);
    action.setRenderTarget(target.get());
  }
  ~SurfaceHarness() { action.setRenderTarget(nullptr); }
  bool render(SoNode *root, Frame &f, const std::string &label) {
    if (!target) return false;
    const auto serial = target->getLastSubmissionSerial();
    const auto calls = audit->submits;
    action.apply(root);
    target->readbackRGBA(f.rgba); target->readbackDepth(f.depth);
    f.serial = target->getLastSubmissionSerial();
    std::cout << "inherited_surface phase=" << label << " serial=" << f.serial
              << " status=" << action.getLastStatus()
              << " error=" << action.getLastError().getString() << '\n';
    return check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                 f.serial > serial && audit->submits == calls + 1 &&
                 f.rgba.size() == size_t(side * side * 4) &&
                 f.depth.size() == size_t(side * side),
                 "one first apply publishes complete RGBA/depth: " + label);
  }
};
bool associations(const RasterScene &s, const CoinRenderFramePlan &plan) {
  if (s.rasterStyle->style.getValue() != SoShadowStyle::SHADOWED) return true;
  CoinRenderShadowPlan shadows;
  return inherited_sampling::rasterOnlyPlan(s, plan, shadows);
}
bool surfaceDelivery(const RasterScene &s, const CoinRenderFramePlan &frame) {
  CoinWgpuFfiFrame ffi;
  std::string error;
  if (!check(ffi.prepare(frame, side, side, error, false),
             "raster inherited FFI prepare " + error))
    return false;
  const auto &view = ffi.getView();
  bool ok =
      check(view.abi_version == COIN_WGPU_ABI_VERSION &&
                frame.shadowLights.size() == size_t(s.setting.maps),
            "current ABI and all lights remain captured, including intensity zero");
  if (s.rasterStyle->style.getValue() == SoShadowStyle::SHADOWED) {
    const uint32_t sizes[] = {view.shadow_map_size, view.shadow_map_size_second,
                              view.shadow_map_size_third,
                              view.shadow_map_size_fourth};
    for (int slot = 0; slot < s.setting.maps; ++slot) {
      const uint32_t extent =
          slot < 4 ? sizes[slot]
                   : (size_t(slot - 4) < view.extra_shadow_pass_count
                          ? view.extra_shadow_passes[slot - 4].map_size
                          : 0);
      ok &= check(
          extent == 256,
          "every real map reaches FFI, including slots five through eight");
    }
    ok &= check(view.extra_shadow_pass_count ==
                    size_t(std::max(0, s.setting.maps - 4)),
                "exact additional map count");
  }
  unsigned witnesses = 0;
  for (const auto &draw : frame.draws) {
    if (draw.shadowLightSlot || draw.sourceNodeId != s.image->getNodeId())
      continue;
    const auto &raster = frame.renderStates[draw.renderStateSlot];
    if (!raster.rasterPixels)
      continue;
    ++witnesses;
    if (!check(raster.rasterTextureSourceSlot < frame.renderStates.size(),
               "raster source state exists")) {
      ok = false;
      continue;
    }
    const auto &source = frame.renderStates[raster.rasterTextureSourceSlot];
    ok &= check(
        source.textureProjection ==
            (s.rasterStyle->style.getValue() == SoShadowStyle::SHADOWED
                 ? CoinRenderTextureProjection::DIRECT_ST
                 : CoinRenderTextureProjection::PROJECTIVE),
        "effective shadow DIRECT_ST versus ordinary PROJECTIVE classification");
    const auto &gpu = view.states[draw.renderStateSlot];
    const auto &gs = view.states[raster.rasterTextureSourceSlot];
    ok &= check(gs.texture_projection == uint32_t(source.textureProjection),
                "effective projection survives FFI");
    ok &= check(gpu.raster_texture_source_slot ==
                    raster.rasterTextureSourceSlot + 1,
                "FFI source state is one based");
    for (int u = 0; u < 8; ++u) {
      auto layer = coin_render_texture_unit(source, u);
      bool enabled =
          std::find(s.active.begin(), s.active.end(), u) != s.active.end();
      ok &= check(bool(layer.enabled) == enabled,
                  "exact authored active unit mask");
      if (!enabled)
        continue;
      if (!check(layer.imageSlot < frame.textures.size() &&
                     layer.samplerSlot < frame.samplers.size(),
                 "unit owns image and sampler")) {
        ok = false;
        continue;
      }
      const auto &image = frame.textures[layer.imageSlot];
      const auto &sampler = frame.samplers[layer.samplerSlot];
      auto bytes = pixels(u, s.white, u == s.alternate, s.active.size() > 1);
      ok &= check(image.width == 8 && image.height == 8 &&
                      image.pixelsRgba == bytes,
                  "nonconstant per-unit exact base bytes");
      ok &= check(unsigned(sampler.filter) == unsigned(s.setting.quality) &&
                      sampler.wrapS == (s.setting.repeat
                                            ? CoinRenderTextureWrap::REPEAT
                                            : CoinRenderTextureWrap::CLAMP) &&
                      sampler.wrapT == sampler.wrapS,
                  "authored quality and wraps");
      ok &= check(image.mipmapped == (s.setting.quality >= 2),
                  "mip chain present exactly when minification requests it");
      if (s.setting.quality >= 2) {
        std::vector<uint8_t> mip;
        for (int n : {4, 2, 1})
          for (int p = 0; p < n * n; ++p) {
            for (int c = 0; c < 3; ++c)
              mip.push_back(s.white ? 255
                                    : (s.active.size() > 1 ? 226 : 150) -
                                          (u == s.alternate ? 40 : 0));
            mip.push_back(255);
          }
        ok &= check(image.mipmapsRgba == mip,
                    "independent exact lower checker mip levels");
      }
      const SbVec4f raw(s.coordinates[u]->factorS.getValue()[3],
                        s.coordinates[u]->factorT.getValue()[3],
                        s.coordinates[u]->factorR.getValue()[3], 2);
      for (int c = 0; c < 4; ++c)
        ok &= check(raster.rasterTextureCoordinates[u][c] == raw[c] &&
                        gpu.raster_texture_coordinates[u][c] == raw[c],
                    "raw constant STRQ survives Core and FFI");
      const auto matrix = s.transforms[u]->matrix.getValue();
      const float *gm = u ? gs.extra_textures[u - 1].matrix : gs.texture_matrix;
      for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
          ok &= check(layer.matrix[r][c] == matrix[r][c] &&
                          gm[r * 4 + c] == matrix[r][c],
                      "texture matrix retained exactly once on source");
      ok &= check((u ? gs.extra_textures[u - 1].texture_slot
                     : gs.texture_slot) == layer.imageSlot &&
                      (u ? gs.extra_textures[u - 1].sampler_slot
                         : gs.sampler_slot) == layer.samplerSlot,
                  "FFI unit resource identity");
      const auto &tx = view.textures[layer.imageSlot];
      size_t count = bytes.size() + image.mipmapsRgba.size();
      ok &= check(tx.pixel_bytes_len == count && tx.pixels &&
                      std::memcmp(tx.pixels, bytes.data(), bytes.size()) == 0,
                  "FFI base plus mip payload size and bytes");
      if (tx.pixels && image.mipmapsRgba.size() && tx.pixel_bytes_len == count)
        ok &= check(std::memcmp(tx.pixels + bytes.size(),
                                image.mipmapsRgba.data(),
                                image.mipmapsRgba.size()) == 0,
                    "FFI mip bytes intact");
    }
  }
  return check(witnesses > 0,
               "actual SoImage inherited raster draw witnessed") &&
         ok;
}

bool runMsaa(const Settings &s, const CoinRenderCapabilities &caps, unsigned samples) {
  SurfaceHarness h(caps, samples);
  if (!h.target)
    return false;
  auto *audit = h.audit;
  h.action.setTransparencyType(CoinRenderAction::BLEND);
  h.action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  RasterScene scene(s);
  bool ok = true;
  Frame clear, shadow, colored, back;
  auto draw = [&](Frame &f, const char *label) {
    auto count = audit->submits;
    const auto sourceId = scene.root->getNodeId(),
               imageId = scene.image->getNodeId();
    bool pass = h.render(scene.root, f, label);
    pass &= check(sourceId == scene.root->getNodeId() &&
                      imageId == scene.image->getNodeId(),
                  "apply does not mutate authored source nodes");
    return check(audit->submits == count + 1, "one logical GPU submission") &&
           pass && surfaceDelivery(scene, audit->plan) && associations(scene, audit->plan);
  };
  for (int slot = 0; slot < s.maps; ++slot) {
    scene.isolate(slot);
    scene.paint(true);
    scene.shadow(slot, false);
    if (!draw(clear, "white lit slot")) {
      ok = false;
      continue;
    }
    ok &= imageOracle(scene, clear, nullptr, false);
    scene.shadow(slot, true);
    if (draw(shadow, "white shadow slot")) {
      ok &= imageOracle(scene, shadow, nullptr, true);
      unsigned changed = 0;
      for (int y = 58; y <= 69; ++y)
        for (int x = 58; x <= 69; ++x) {
          size_t p = size_t(y * side + x) * 4;
          changed += std::abs(int(clear.rgba[p]) - int(shadow.rgba[p])) > 20;
        }
      ok &= check(changed > 50,
                  "each isolated real map observably shadows the image");
    } else
      ok = false;
    scene.paint(false);
    if (draw(colored, "colored shadow slot"))
      ok &= imageOracle(scene, colored, &shadow, true);
    else
      ok = false;
    scene.shadow(slot, false);
    if (draw(colored, "colored lit slot"))
      ok &= imageOracle(scene, colored, &clear, false);
    else
      ok = false;
    scene.paint(true);
    if (draw(back, "white restored slot"))
      ok &= imageOracle(scene, back, nullptr, false) && exact(clear, back);
    else
      ok = false;
  }
  scene.isolate(-1);
  scene.paint(true);
  if (!draw(clear, "joint white"))
    return false;
  scene.paint(false);
  if (!draw(colored, "joint patterned"))
    return false;
  ok &= imageOracle(scene, colored, &clear, false);
  for (int unit : scene.active) {
    scene.paint(false, unit);
    if (draw(back, "unit bytes changed")) {
      ok &= imageOracle(scene, back, &clear, false);
      unsigned changed = 0;
      for (int y = 58; y <= 69; ++y)
        for (int x = 58; x <= 69; ++x) {
          size_t p = size_t(y * side + x) * 4;
          for (int c = 0; c < 3; ++c)
            changed +=
                std::abs(int(back.rgba[p + c]) - int(colored.rgba[p + c])) > 3;
        }
      ok &= check(changed > 16, "each inherited texture has a visible effect");
    } else
      ok = false;
    scene.paint(false);
    if (draw(back, "unit bytes restore"))
      ok &= imageOracle(scene, back, &clear, false) && exact(colored, back);
    else
      ok = false;
    const auto q = scene.coordinates[unit]->factorQ.getValue(),
               st = scene.coordinates[unit]->factorS.getValue();
    for (int invalid = 0; invalid < 2; ++invalid) {
      // Q divides only on PROJECTIVE. Keep all maps enabled, but author the
      // raster as UNSHADOWED for this control. NaN also covers DIRECT_ST.
      if (invalid == 0) {
        scene.rasterStyle->style = SoShadowStyle::NO_SHADOWING;
        if (!draw(back, "valid PROJECTIVE predecessor"))
          return false;
        ok &= imageOracle(scene, back, nullptr, false);
      }
      if (invalid == 0)
        scene.coordinates[unit]->factorQ.setValue(0, 0, 0, 0);
      else
        scene.coordinates[unit]->factorS.setValue(
            0, 0, 0, std::numeric_limits<float>::quiet_NaN());
      auto count = audit->submits, producers = audit->producers;
      const auto serial = h.target->getLastSubmissionSerial();
      h.action.apply(scene.root);
      Frame failed;
      h.target->readbackRGBA(failed.rgba);
      h.target->readbackDepth(failed.depth);
      ok &=
          check(h.action.getLastStatus() != CoinRenderAction::SUCCESS &&
                    audit->submits == count && audit->producers == producers &&
                    h.target->getLastSubmissionSerial() == serial &&
                    failed.rgba == back.rgba && failed.depth == back.depth,
                "invalid per-unit Q/NaN refuses before commands and preserves "
                "exact publication");
      scene.coordinates[unit]->factorQ = q;
      scene.coordinates[unit]->factorS = st;
      scene.rasterStyle->style = SoShadowStyle::SHADOWED;
      if (draw(back, "invalid coordinate recovery"))
        ok &= imageOracle(scene, back, &clear, false) && exact(colored, back);
      else
        ok = false;
    }
  }
  auto *unused = new SoTextureCoordinateObject;
  unused->factorS.setValue(0, 0, 0, std::numeric_limits<float>::quiet_NaN());
  scene.receiver->addChild(unused); // after the only image: no consumer
  if (draw(back, "unconsumed invalid coordinate state"))
    ok &= imageOracle(scene, back, &clear, false) && exact(colored, back);
  else ok = false;
  return ok;
}

#include "CoinRenderInheritedSurfaceRtt.h"

bool capture(unsigned samples) {
  bool ok = true;
  for (unsigned n = 0; n < 6; ++n) {
    selectPolicy(n);
    RasterScene scene(configuration(n));
    auto options = samplingTestOptions(); options.sampleCount = samples;
    std::unique_ptr<CoinRenderTarget> target(
        samplingTestOffscreen(SbVec2i32(side, side), options));
    if (!check(bool(target), "CPU capture target")) return false;
    auto *recorder = new Recorder;
    target->getPimpl()->backend.reset(recorder);
    CoinRenderAction action(SbViewportRegion(side, side));
    action.setTransparencyType(CoinRenderAction::BLEND);
    action.setRenderTarget(target.get());
    for (unsigned phase = 0; phase < 3; ++phase) {
      scene.paint(false, phase == 1 ? scene.active.back() : -1);
      const auto before = recorder->serial;
      action.apply(scene.root);
      ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  recorder->serial == before + 1 &&
                  target->getOptions().sampleCount == samples &&
                  recorder->plan.textureSamplingPolicy == samplingTestPolicy,
                  "finite MSAA capture publishes once with authored policy");
      if (action.getLastStatus() == CoinRenderAction::SUCCESS)
        ok &= surfaceDelivery(scene, recorder->plan) && associations(scene, recorder->plan);
    }
    const int unit = scene.active.back();
    const auto original = scene.coordinates[unit]->factorS.getValue();
    scene.coordinates[unit]->factorS.setValue(0, 0, 0, std::numeric_limits<float>::quiet_NaN());
    const auto before = recorder->serial;
    action.apply(scene.root);
    ok &= check(action.getLastStatus() != CoinRenderAction::SUCCESS && recorder->serial == before,
                "MSAA capture refuses consumed NaN before submission");
    scene.coordinates[unit]->factorS = original;
    action.apply(scene.root);
    ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS && recorder->serial == before + 1,
                "MSAA capture first valid recovery");
    action.setRenderTarget(nullptr);
  }
  return ok;
}
} // namespace inherited_surfaces

#ifndef COIN_RENDER_INHERITED_SURFACE_FIXTURE_ONLY
int main(int argc, char **argv) {
  using namespace inherited_surfaces;
  bool cpu = false; unsigned samples = 2, selected = 0;
  std::string domain = "msaa";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--cpu") cpu = true;
    else if (arg == "--gpu") cpu = false;
    else if (arg == "--domain" && i + 1 < argc) {
      domain = argv[++i];
      if (domain != "msaa" && domain != "staged" && domain != "direct") return 2;
    } else if ((arg == "--samples" || arg == "--configuration") && i + 1 < argc) {
      const std::string value = argv[++i];
      if (arg == "--samples") {
        if (value != "2" && value != "4") return 2;
        samples = unsigned(std::atoi(value.c_str()));
      } else {
        if (value.size() != 1 || value[0] < '0' || value[0] > '5') return 2;
        selected = unsigned(value[0] - '0');
      }
    } else return 2;
  }
  std::cout << std::unitbuf;
  SoDB::init(); CoinRenderAction::initClass();
  if (cpu) return domain == "msaa" ? (capture(samples) ? 0 : 1) : 2;
  selectPolicy(selected);
  const auto caps = msaa_shadow_test::capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if (!caps.gpu_available || caps.renderer == COIN_RENDER_RENDERER_UNKNOWN) return 77;
  if (domain == "msaa") return runMsaa(configuration(selected), caps, samples) ? 0 : 1;
  return runRtt(configuration(selected), caps, domain == "direct"
      ? COIN_RENDER_SCENE_TEXTURE_DIRECT : COIN_RENDER_SCENE_TEXTURE_STAGED) ? 0 : 1;
}

#endif
