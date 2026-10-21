#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#define main coin_render_map_camera_original_main
#include "CoinRenderShadowMapCameraTest.cpp"
#undef main
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <array>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include "actions/CoinRenderActionP.h"

namespace authored_composition {
struct TransparentScene : StereoScene {
  SoSeparator *objects[2]{};
  SoMaterial *materials[2]{};
  SoFaceSet *faces[2]{};
  bool reversed = false;
  float objectZ[2] = {-1, -1};
  explicit TransparentScene(bool ortho = false, bool stereo = false)
      : StereoScene(2, ortho, ortho, SoCamera::LEAVE_ALONE, 0, false) {
    if (stereo) objectZ[1] = -.5f;
    for (size_t slot = 0; slot < cameras.size(); ++slot) {
      if (!stereo) cameras[slot]->setStereoMode(SoCamera::MONOSCOPIC);
      cameras[slot]->position.setValue(slot ? .35f : -.35f, 0, 5);
      cameras[slot]->nearDistance = 1; cameras[slot]->farDistance = 10;
      cameras[slot]->aspectRatio = 1;
      occurrences[slot][0].viewport.kind = 0;
    }
    group->epsilon = 0; group->threshold = 0;
    groupReceiver->removeAllChildren();
    auto *style = new SoShadowStyle; style->style = SoShadowStyle::SHADOWED;
    groupReceiver->addChild(style);
    auto *environment = new SoEnvironment; environment->ambientIntensity = 0;
    groupReceiver->addChild(environment);
    for (unsigned i = 0; i < 2; ++i) {
      objects[i] = new SoSeparator; objects[i]->ref();
      materials[i] = new SoMaterial;
      materials[i]->diffuseColor.setValue(i ? .125f : .75f, i ? .5f : .25f, i ? .75f : .125f);
      materials[i]->ambientColor.setValue(0, 0, 0);
      materials[i]->specularColor.setValue(0, 0, 0);
      materials[i]->emissiveColor.setValue(0, 0, 0);
      materials[i]->transparency = i ? .75f : .5f;
      objects[i]->addChild(materials[i]);
      auto *depth = new SoDepthBuffer; depth->write = FALSE;
      objects[i]->addChild(depth);
      auto *normal = new SoNormal; normal->vector.setValue(0, 0, 1); objects[i]->addChild(normal);
      // One authored normal applies to all four corners; the native default is per vertex.
      auto *normalBinding = new SoNormalBinding; normalBinding->value = SoNormalBinding::OVERALL;
      objects[i]->addChild(normalBinding);
      auto *positions = new SoCoordinate3;
      const SbVec3f corners[] = {SbVec3f(-1.8f, -1.8f, objectZ[i]), SbVec3f(1.8f, -1.8f, objectZ[i]),
                                SbVec3f(1.8f, 1.8f, objectZ[i]), SbVec3f(-1.8f, 1.8f, objectZ[i])};
      positions->point.setValues(0, 4, corners); objects[i]->addChild(positions);
      faces[i] = new SoFaceSet; faces[i]->numVertices = 4; objects[i]->addChild(faces[i]);
      groupReceiver->addChild(objects[i]);
    }
    isolate(-1);
  }
  ~TransparentScene() { for (auto *object : objects) object->unref(); }
  void isolate(int slot) {
    if (slot < 0) together(); else StereoScene::isolate(size_t(slot));
  }
  void order(bool reverse) {
    groupReceiver->removeChild(objects[0]); groupReceiver->removeChild(objects[1]);
    groupReceiver->addChild(objects[reverse ? 1 : 0]);
    groupReceiver->addChild(objects[reverse ? 0 : 1]); reversed = reverse;
  }
};
class CompositionRecorder : public CoinRenderBackend {
  std::string error;
  uint64_t serial = 0;
public:
  bool isGpuBackend() const override { return true; }
  bool supportsOffscreenShadows() const override { return true; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &, CoinRenderTargetP &target) override {
    target.colorBuffer.assign(size_t(side * side * 4), 0);
    target.depthBuffer.assign(size_t(side * side), 1);
    return {CoinRenderBackendStatus::SUCCESS, "", ++serial};
  }
  void poll() override {}
  const std::string &getLastError() const override { return error; }
};
struct CompositionHarness {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(side, side)};
  NearAudit *audit = nullptr;
  explicit CompositionHarness(bool cpu, CoinRenderTransparencyMode mechanism = COIN_RENDER_TRANSPARENCY_COIN) {
    std::string diagnostic;
    auto options = CoinRenderDiagnosticShell::renderOptions(diagnostic);
    options.sampleCount = 1; options.rasterizationPolicy = COIN_RENDER_RASTER_NATIVE;
    options.transparency = mechanism;
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side, side), options));
    if (!check(bool(target), "composition target factory")) return;
    auto &state = target->getPimpl().get();
    if (cpu) state.backend.reset(new CompositionRecorder);
    else if (!check(state.prepareBackend() == CoinRenderBackendStatus::SUCCESS &&
                    state.backend && state.backend->isGpuBackend() &&
                    target->getLastSubmissionSerial() == 0,
                    "real first-use GPU runtime, no prior publication")) { target.reset(); return; }
    audit = new NearAudit(std::move(state.backend)); state.backend.reset(audit);
    target->setDepthReadbackEnabled(TRUE); action.setRenderTarget(target.get());
    action.setTransparencyType(CoinRenderAction::BLEND);
    action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  }
  ~CompositionHarness() { action.setRenderTarget(nullptr); }
  bool render(SoNode *root, Frame &frame, const char *phase) {
    if (!target) return false;
    const auto before = target->getLastSubmissionSerial();
    const auto count = audit->submissions;
    action.apply(root); target->readbackRGBA(frame.rgba); target->readbackDepth(frame.depth);
    frame.serial = target->getLastSubmissionSerial();
    std::cout << "authored_composition phase=" << phase << " serial=" << frame.serial
              << " status=" << action.getLastStatus() << " error=" << action.getLastError().getString() << '\n';
    return check(action.getLastStatus() == CoinRenderAction::SUCCESS && audit->submissions == count + 1 &&
                 frame.serial > before && frame.rgba.size() == size_t(side * side * 4) &&
                 frame.depth.size() == size_t(side * side), "one apply publishes a complete composition");
  }
  bool refuse(SoNode *root, const Frame &before) {
    const auto count = audit->submissions, producers = audit->producers;
    const auto serial = target->getLastSubmissionSerial();
    action.apply(root); Frame after;
    target->readbackRGBA(after.rgba); target->readbackDepth(after.depth);
    return check(action.getLastStatus() != CoinRenderAction::SUCCESS &&
                 audit->submissions == count && audit->producers == producers &&
                 target->getLastSubmissionSerial() == serial &&
                 after.rgba == before.rgba && after.depth == before.depth,
                 "invalid consumed map camera refuses atomically before execution");
  }
};
bool compositionDelivery(const StereoScene &scene, const NearAudit &audit,
                    int targetSide = side) {
  const auto &frame = audit.frame;
  CoinRenderShadowPlan plan;
  std::string error;
  const size_t count = scene.lights.size();
  if (!check(coin_render_plan_shadows(frame, plan, error) &&
                 plan.passes.size() == count &&
                 frame.shadowLights.size() == count,
             "all enabled stereo maps, including zero-intensity maps, reach "
             "Core: " +
                 error))
    return false;
  CoinWgpuFfiFrame ffi;
  if (!check(ffi.prepare(frame, targetSide, targetSide, error, false),
             "stereo private transport: " + error))
    return false;
  const auto &view = ffi.getView();
  bool ok = check(view.abi_version == COIN_WGPU_ABI_VERSION && sizeof(CoinWgpuShadowDraw) == 160 &&
                      view.struct_size == sizeof(CoinWgpuFrameView) &&
                      view.extra_shadow_pass_count == (count > 4 ? count - 4 : 0),
                  "Current ABI layout and extended shadow map slots preserved");
  if (scene.general) {
    bool eight = false;
    for (size_t i = 0; i < frame.renderStates.size(); ++i) {
      bool all = true;
      for (size_t unit = 0; unit < 8; ++unit)
        all &= coin_render_texture_unit_enabled(frame.renderStates[i], unit);
      if (!all)
        continue;
      eight = true;
      if (!check(view.states && i < view.state_count,
                 "general stereo receiver FFI state addressable"))
        return false;
      const auto &state = view.states[i];
      for (size_t unit = 0; unit < 8; ++unit) {
        const bool enabled =
            unit ? state.extra_textures[unit - 1].enabled : state.has_texture;
        const auto image = unit ? state.extra_textures[unit - 1].texture_slot
                                : state.texture_slot;
        const auto sampler = unit ? state.extra_textures[unit - 1].sampler_slot
                                  : state.sampler_slot;
        ok &= check(enabled && image < view.texture_count &&
                        sampler < view.sampler_count,
                    "eight independent stereo receiver image/sampler bindings "
                    "survive FFI");
        if (scene.coverage && image < view.texture_count) {
          SbVec2s extent;
          int components = 0;
          const auto *source =
              scene.textures[unit]->image.getValue(extent, components);
          const auto &transport = view.textures[image];
          ok &= check(
              source && extent == SbVec2s(1, 1) && components == 4 &&
                  transport.width == 1 && transport.height == 1 &&
                  transport.pixels && transport.pixel_bytes_len == 4 &&
                  std::memcmp(source, transport.pixels, 4) == 0,
              "current independent stereo receiver texel bytes survive FFI");
        }
      }
    }
    ok &= check(eight, "eight-unit stereo receiver reaches Core");
  }
  const SbMatrix clipConversion(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, .5f, 0, 0, 0, .5f,
                                1);
  for (size_t passIndex = 0; passIndex < count; ++passIndex) {
    const auto &pass = plan.passes[passIndex];
    if (!check(pass.lightSlot < frame.shadowLights.size(),
               "stereo pass light index valid"))
      return false;
    const auto &light = frame.shadowLights[pass.lightSlot];
    size_t slot = count;
    for (size_t i = 0; i < count; ++i)
      if (light.sourceRevision == scene.lights[i]->getNodeId())
        slot = i;
    if (!check(slot < count && light.enabled && light.shadowEligible &&
                   light.mapSceneCaptured && light.groupSlot == 1,
               "stereo pass retains current authorial light and map identity"))
      return false;
    ok &= check(std::abs(light.intensity -
                         scene.lights[slot]->intensity.getValue()) < 1e-7 &&
                    light.customSceneNodeId == scene.maps[slot]->getNodeId(),
                "stereo map intensity/source identity survives capture");
    const CoinWgpuShadowDraw *casters = nullptr;
    uint64_t casterCount = 0;
    uint32_t mapSize = 0;
    if (passIndex == 0) {
      casters = view.shadow_casters;
      casterCount = view.shadow_caster_count;
      mapSize = view.shadow_map_size;
    } else if (passIndex == 1) {
      casters = view.shadow_casters_second;
      casterCount = view.shadow_caster_count_second;
      mapSize = view.shadow_map_size_second;
    } else if (passIndex == 2) {
      casters = view.shadow_casters_third;
      casterCount = view.shadow_caster_count_third;
      mapSize = view.shadow_map_size_third;
    } else if (passIndex == 3) {
      casters = view.shadow_casters_fourth;
      casterCount = view.shadow_caster_count_fourth;
      mapSize = view.shadow_map_size_fourth;
    } else {
      if (!check(view.extra_shadow_passes &&
                     passIndex - 4 < view.extra_shadow_pass_count,
                 "stereo extension slots5..8 addressable"))
        return false;
      const auto &extra = view.extra_shadow_passes[passIndex - 4];
      casters = extra.casters;
      casterCount = extra.caster_count;
      mapSize = extra.map_size;
    }
    ok &= check(mapSize == 256 && casters && casterCount,
                "each stereo pass has real map256/caster payload");
    std::vector<bool> seen(scene.occurrences[slot].size(), false);
    for (size_t occurrence = 0; occurrence < scene.occurrences[slot].size();
         ++occurrence) {
      const auto &occ = scene.occurrences[slot][occurrence];
      if (occ.external)
        continue;
      bool found = false;
      const auto expectedView = scene.view(slot, occurrence),
                 expectedProjection = scene.projection(slot, occurrence);
      // This fixture's before matrices contain Rz/T and uniform scale. A
      // transformed view volume measures near/far along its transformed -Z
      // axis. Compare in authorial distance units, retaining the existing1e-6
      // bound; raw view/projection matrices keep their unchanged1e-5 gate.
      const double distanceUnits =
          std::sqrt(double(occ.before[2][0]) * occ.before[2][0] +
                    double(occ.before[2][1]) * occ.before[2][1] +
                    double(occ.before[2][2]) * occ.before[2][2]);
      ok &= check(
          std::isfinite(distanceUnits) && distanceUnits > 0,
          "captured camera volume has finite nonzero authorial distance units");
      for (const auto &camera : frame.cameras)
        found |=
            camera.isPerspective ==
                (!scene.orthographic || scene.cameras[slot]->getStereoMode() !=
                                            SoCamera::MONOSCOPIC) &&
            std::abs(double(camera.nearDistance) / distanceUnits -
                     scene.cameras[slot]->nearDistance.getValue()) < 1e-6 &&
            std::abs(double(camera.farDistance) / distanceUnits -
                     scene.cameras[slot]->farDistance.getValue()) < 1e-6 &&
            stereoMatrixClose(camera.viewMatrix, expectedView) &&
            stereoMatrixClose(camera.projectionMatrixCoin, expectedProjection);
      ok &= check(found, "Core stereo eye/skew/effective perspective and "
                         "per-occurrence native model order retained");
      ok &= check(occ.viewport.visits > 0 && occ.viewport.dimensionsCorrect,
                  "stereo viewport callback sees actual map256");
    }
    for (uint64_t i = 0; i < casterCount; ++i) {
      const auto &packed = casters[i];
      if (!check(packed.render_state_slot < frame.renderStates.size(),
                 "stereo packed caster state valid"))
        return false;
      const auto &state = frame.renderStates[packed.render_state_slot];
      ok &= check(packed.reserved == 0, "stereo FFI reserved flags stay zero");
      if (scene.raster) {
        bool viewportMatched = false;
        for (size_t occurrence = 0; occurrence < scene.occurrences[slot].size();
             ++occurrence) {
          const auto vp = scene.viewport(slot, occurrence);
          viewportMatched |= packed.viewport[0] == int(vp.x) &&
                             packed.viewport[1] == 256 - int(vp.y + vp.h) &&
                             packed.viewport[2] == int(vp.w) &&
                             packed.viewport[3] == int(vp.h);
        }
        ok &= check(viewportMatched, "pixel caster FFI retains its own "
                                     "authored stereo viewport/scissor");
      }
      if (scene.raster)
        continue; // Pixel-sized primitives have their own raster remapping;
                  // projection is independently checked above and in RGB.
      SbMatrix actualMV, actualMVP;
      actualMV.setValue(packed.model_view);
      actualMVP.setValue(packed.model_view_projection);
      bool matched = false;
      for (size_t occurrence = 0; occurrence < scene.occurrences[slot].size();
           ++occurrence) {
        const auto &occ = scene.occurrences[slot][occurrence];
        if (occ.external) {
          if (!state.shadowMapCameraOverride &&
              stereoMatrixClose(state.model, scene.model(slot, occurrence))) {
            seen[occurrence] = true;
            matched = true;
            ok &= check(packed.viewport[0] == 0 && packed.viewport[1] == 0 &&
                            packed.viewport[2] == 256 &&
                            packed.viewport[3] == 256,
                        "Separator restores external caster full-map viewport "
                        "and light camera");
          }
          continue;
        }
        const auto vp = scene.viewport(slot, occurrence);
        const auto expectedMV =
            scene.model(slot, occurrence) * scene.drawView(slot, occurrence);
        const SbMatrix viewportTransform(
            float(vp.w / 256), 0, 0, 0, 0, float(vp.h / 256), 0, 0, 0, 0, 1, 0,
            float((2 * vp.x + vp.w) / 256 - 1),
            float((2 * vp.y + vp.h) / 256 - 1), 0, 1);
        const auto expectedMVP = expectedMV *
                                 scene.projection(slot, occurrence) *
                                 clipConversion * viewportTransform;
        if (stereoMatrixClose(actualMV, expectedMV) &&
            stereoMatrixClose(actualMVP, expectedMVP)) {
          matched = true;
          seen[occurrence] = true;
          ok &= check(state.shadowMapCameraOverride &&
                          packed.viewport[0] == int(vp.x) &&
                          packed.viewport[1] == 256 - int(vp.y + vp.h) &&
                          packed.viewport[2] == int(vp.w) &&
                          packed.viewport[3] == int(vp.h),
                      "FFI retains stereo occurrence viewport/scissor once");
          for (const auto &point : {SbVec3f(0, 0, 0), SbVec3f(.2f, -.1f, .06f),
                                    SbVec3f(-.17f, .13f, -.08f)}) {
            SbVec3f actual, expected;
            actualMVP.multVecMatrix(point, actual);
            expectedMVP.multVecMatrix(point, expected);
            ok &= check((actual - expected).length() < 1e-5,
                        "stereo FFI homogeneous projection probes preserve "
                        "eye, perspective and signed clip depth");
          }
        }
      }
      ok &= check(matched, "every geometry caster FFI matrix belongs to its "
                           "exact source occurrence");
    }
    if (!scene.raster)
      for (bool present : seen)
        ok &= check(
            present,
            "all shared/external stereo occurrences delivered independently");
  }
  return ok;
}

bool transparentDelivery(const TransparentScene &scene, const NearAudit &audit) {
  bool ok = compositionDelivery(scene, audit);
  unsigned seen[2] = {0, 0};
  for (const auto &draw : audit.frame.draws) {
    if (draw.shadowLightSlot) continue;
    for (unsigned object = 0; object < 2; ++object) {
      if (draw.sourceNodeId != scene.faces[object]->getNodeId()) continue;
      ++seen[object];
      const auto &state = audit.frame.renderStates[draw.renderStateSlot];
      ok &= check(state.depthTest && !state.depthWrite && state.transparencyType == SoGLRenderAction::BLEND &&
                  state.materialSlot < audit.frame.materials.size(),
                  "each transparent occurrence retains literal BLEND and explicit depth write off");
      if (state.materialSlot < audit.frame.materials.size()) {
        const auto &material = audit.frame.materials[state.materialSlot];
        ok &= check(material.transparency == scene.materials[object]->transparency[0],
                    "independent object opacity survives capture");
      }
    }
  }
  return check(seen[0] && seen[1], "both independently colored overlapping objects reach the plan") && ok;
}
// The camera/viewport algebra computes interior binary map visibility. The
// source-over equation below uses authored colors/opacities, never GPU primaries.
bool blendOracle(const TransparentScene &scene, const Frame &frame) {
  if (frame.rgba.size() != size_t(side * side * 4) || frame.depth.size() != size_t(side * side)) return false;
  std::vector<StereoShape> footprints;
  for (size_t slot = 0; slot < scene.lights.size(); ++slot) footprints.push_back(stereoShape(scene, slot, 0));
  unsigned lit = 0, shadowed = 0, bad = 0; double maximum = 0;
  for (int y = 12; y < side - 12; ++y) for (int x = 12; x < side - 12; ++x) {
    const double wx = (x + .5 - side / 2.) * 4 / side, wy = (side / 2. - y - .5) * 4 / side;
    const double mx = (.5 + wx / scene.lookupWidth()) * 256, my = (.5 + wy / scene.lookupWidth()) * 256;
    double strength = 0; bool measurable = true, shadow = false;
    for (size_t slot = 0; slot < footprints.size(); ++slot) {
      const double intensity = scene.lights[slot]->intensity.getValue();
      if (!intensity) continue;
      const bool hit = stereoContains(footprints[slot].inner, mx, my, 2.1);
      const bool clear = !stereoContains(footprints[slot].outer, mx, my, -2.1);
      if (!hit && !clear) { measurable = false; break; }
      shadow |= hit; if (!hit) strength += intensity;
    }
    if (!measurable) continue;
    shadowed += shadow; lit += !shadow;
    const double lambert = 6 / std::sqrt(36 + wx * wx + wy * wy);
    double rgba[4] = {0, 0, 0, 0};
    for (unsigned index = 0; index < 2; ++index) {
      const unsigned object = scene.reversed ? 1 - index : index;
      const double alpha = 1. - scene.materials[object]->transparency[0];
      const auto color = scene.materials[object]->diffuseColor[0];
      for (int c = 0; c < 3; ++c)
        rgba[c] = std::round(255 * color[c] * lambert * strength * alpha + rgba[c] * (1 - alpha));
      rgba[3] = std::round(255 * alpha + rgba[3] * (1 - alpha));
    }
    const size_t pixel = size_t(y * side + x);
    for (int c = 0; c < 4; ++c) {
      const double error = std::abs(double(frame.rgba[4 * pixel + c]) - rgba[c]);
      maximum = std::max(maximum, error); bad += error > 3;
    }
    bad += frame.depth[pixel] != 1.f;
  }
  std::cout << "composition_oracle lit=" << lit << " shadowed=" << shadowed
            << " bad=" << bad << " max_rgba=" << maximum << '\n';
  return check(lit > 500 && shadowed >= 4 && bad == 0,
               "independent camera visibility, ordered source-over RGBA and preserved depth");
}
bool exactComposition(const Frame &a, const Frame &b) {
  return check(a.rgba == b.rgba && a.depth == b.depth, "same-environment complete A/B/A exact restoration");
}
bool t1(bool cpu) {
  CompositionHarness h(cpu); if (!h.target) return false;
  TransparentScene scene; bool ok = true;
  auto draw = [&](Frame &f, const char *label) {
    const auto authored = stereoAuthorState(scene);
    return h.render(scene.root, f, label) &&
           check(stereoAuthorState(scene) == authored, "apply preserves authored map cameras") &&
           transparentDelivery(scene, *h.audit) && (cpu || blendOracle(scene, f));
  };
  for (int slot : {0, 1, -1}) {
    scene.isolate(slot); scene.order(false);
    Frame a, b, back;
    if (!draw(a, "T1 original order")) { ok = false; continue; }
    scene.order(true);
    if (!draw(b, "T1 reversed order")) { ok = false; continue; }
    if (!cpu) {
      unsigned changed = 0;
      for (size_t i = 0; i < a.rgba.size(); i += 4)
        changed += std::abs(int(a.rgba[i]) - int(b.rgba[i])) > 3;
      ok &= check(changed > 500, "literal BLEND preserves observable authorial order");
    }
    scene.order(false); ok &= draw(back, "T1 order restored") && exactComposition(a, back);
    for (size_t camera = 0; camera < scene.cameras.size(); ++camera) {
      const float nearValue = scene.cameras[camera]->nearDistance.getValue();
      scene.cameras[camera]->nearDistance = std::numeric_limits<float>::quiet_NaN();
      ok &= h.refuse(scene.root, back);
      scene.cameras[camera]->nearDistance = nearValue;
      ok &= draw(back, "T1 camera first valid recovery") && exactComposition(a, back);
    }
  }
  // An unused camera after every shape has no consumed occurrence.
  auto *unused = new SoPerspectiveCamera;
  unused->nearDistance = std::numeric_limits<float>::quiet_NaN();
  scene.root->addChild(unused);
  Frame idle; ok &= draw(idle, "T1 invalid unused trailing camera");
  return ok;
}
#include "CoinRenderAuthoredDelayedComposition.h"
#include "CoinRenderAuthoredScreenComposition.h"
#include "CoinRenderAuthoredOitComposition.h"
#include "CoinRenderAuthoredRttComposition.h"
#include "CoinRenderAuthoredCropRttComposition.h"
} // namespace authored_composition
int main(int argc, char **argv) {
  bool cpu = false; std::string profile = "T1";
  CoinRenderSceneTextureMode route = COIN_RENDER_SCENE_TEXTURE_DIRECT;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--cpu") cpu = true;
    else if (arg == "--gpu") cpu = false;
    else if (arg == "--profile" && i + 1 < argc) { profile = argv[++i]; if (profile != "T1" && profile != "T2" && profile != "T3" && profile != "T4" && profile != "T5" && profile != "R1" && profile != "R2" && profile != "R3") return 2; }
    else if (arg == "--route" && i + 1 < argc) {
      const std::string value = argv[++i];
      if (value != "direct" && value != "staged") return 2;
      route = value == "direct" ? COIN_RENDER_SCENE_TEXTURE_DIRECT : COIN_RENDER_SCENE_TEXTURE_STAGED;
    }
    else return 2;
  }
  std::cout << std::unitbuf; SoDB::init(); CoinRenderAction::initClass();
  if (!cpu) {
    CoinRenderCapabilities caps{};
    if (coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) != 0) return 77;
    std::cout << "backend=" << caps.backend << " renderer=" << caps.renderer << '\n';
    if (!caps.gpu_available || caps.renderer == COIN_RENDER_RENDERER_UNKNOWN) return 77;
  }
  if (profile == "R1") return authored_composition::r1(cpu, route) ? 0 : 1;
  if (profile == "R2") return authored_composition::r2(cpu, route) ? 0 : 1;
  if (profile == "R3") return authored_composition::r3(cpu, route) ? 0 : 1;
  return (profile == "T1" ? authored_composition::t1(cpu) : profile == "T2" ? authored_composition::t2(cpu) : profile == "T3" ? authored_composition::t3(cpu) : authored_composition::oit(cpu, profile == "T5")) ? 0 : 1;
}
