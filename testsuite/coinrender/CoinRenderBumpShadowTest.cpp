#include "CoinRenderBumpShadowFixture.h"
#include "CoinRenderVolumeOracle.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoScale.h>
#include <chrono>
#include <cstring>
#include <limits>

#include "CoinRenderBumpShadowRaw.h"

using namespace bump_shadow;

namespace {
struct Settings {
  unsigned samples = 1;
  bool srgb = false;
  bool only2D = false;
  CoinRenderSceneTextureMode route = COIN_RENDER_SCENE_TEXTURE_STAGED;
  std::string mode = "smoke";
};

// Both baseline and candidate first publish the SAME ordinary scene, then
// attempt the SAME combined scene. Only the expected outcome differs.
bool ordinaryControl(Scene &scene, Harness &h, Frame &ordinary) {
  const RGB spec = scene.spec;
  scene.setSpec({{0, 0, 0}});
  scene.bumpStage->whichChild = SO_SWITCH_NONE;
  const bool ok =
      h.render(scene.root, ordinary, "ordinary previous publication") &&
      oracle(scene, ordinary, false);
  scene.setSpec(spec);
  scene.bumpStage->whichChild = SO_SWITCH_ALL;
  return ok;
}

bool baseline(Scene &scene, Harness &h, const Frame &ordinary) {
  bool ok = h.reject(scene.root, ordinary, "initial combined profile refusal");
  ok &= check(h.action.getLastStatus() == CoinRenderAction::UNSUPPORTED,
              "baseline rejects the feature, not an unrelated target error");
  scene.setSpec({{0, 0, 0}});
  scene.bumpStage->whichChild = SO_SWITCH_NONE;
  Frame restored;
  ok &= h.render(scene.root, restored, "ordinary baseline recovery") &&
        oracle(scene, restored, false) && exact(ordinary, restored);
  return ok;
}

bool smoke(Scene &scene, Harness &h, const Frame &initial) {
  Frame changed, restored;
  const auto normal = scene.normal;
  scene.setNormal({{210, 145, 205}});
  bool ok =
      h.render(scene.root, changed, "normal mutation with fixed caster") &&
      oracle(scene, changed) &&
      check(difference(initial, changed) > 1000,
            "normal map observably changes lighting");
  scene.setNormal(normal);
  ok &= h.render(scene.root, restored, "normal A/B/A") &&
        oracle(scene, restored) && exact(initial, restored);
  scene.casterAt(.8);
  ok &= h.render(scene.root, changed, "caster mutation with fixed normal") &&
        oracle(scene, changed) &&
        check(difference(initial, changed) > 1000,
              "caster observably changes visibility without displacement");
  scene.casterAt(0);
  ok &= h.render(scene.root, restored, "caster A/B/A") &&
        oracle(scene, restored) && exact(initial, restored);

  scene.style->style = SoShadowStyle::NO_SHADOWING;
  scene.receives = false;
  ok &= h.render(scene.root, changed, "receiver disabled") &&
        oracle(scene, changed, true, false) &&
        check(difference(initial, changed) > 1000,
              "receiver disabled restores lit bump rather than white texture");
  scene.style->style = SoShadowStyle::SHADOWED;
  scene.receives = true;
  scene.directional->maxShadowDistance = .5f;
  ok &=
      h.render(scene.root, changed, "finite nonintersecting directional map") &&
      oracle(scene, changed, true, false);
  scene.directional->maxShadowDistance = -1;
  ok &= h.render(scene.root, restored, "neutral map A/B/A") &&
        oracle(scene, restored) && exact(initial, restored);
  return ok;
}

bool lighting(Scene &scene, Harness &h, const Frame &initial) {
  Frame changed, restored;
  const RGB diffuse = scene.diffuse, spec = scene.spec, lamp = scene.lamp;
  const double shine = scene.shine, intensity = scene.intensity;
  bool ok = true;
  for (unsigned variation = 0; variation < 4; ++variation) {
    if (variation == 0)
      scene.setDiffuse({{.35, .7, .25}});
    if (variation == 1)
      scene.setSpec({{.5, .18, .09}});
    if (variation == 2) {
      scene.shine = .5;
      scene.setSpec(spec);
    }
    if (variation == 3) {
      scene.lamp = {{.35, .9, .65}};
      scene.intensity = .45;
      scene.directional->color.setValue(.35f, .9f, .65f);
      scene.directional->intensity = .45f;
    }
    ok &=
        h.render(scene.root, changed,
                 "authored lighting parameter " + std::to_string(variation)) &&
        oracle(scene, changed) &&
        check(difference(initial, changed) > 100,
              "nonzero diffuse/specular64 and light parameters observable");
    scene.setDiffuse(diffuse);
    scene.shine = shine;
    scene.setSpec(spec);
    scene.lamp = lamp;
    scene.intensity = intensity;
    scene.directional->color.setValue(float(lamp[0]), float(lamp[1]),
                                      float(lamp[2]));
    scene.directional->intensity = float(intensity);
    ok &= h.render(scene.root, restored, "lighting parameter A/B/A") &&
          oracle(scene, restored) && exact(initial, restored);
  }
  return ok;
}

bool projection(Scene &scene, Harness &h, const Frame &initial) {
  Frame changed, restored;
  bool ok = true;
  scene.raw(7, SbVec3f(.5f / 3, .5f / 5, .5f / 7));
  ok &= h.render(scene.root, changed, "unit seven distinct R slice") &&
        oracle(scene, changed) &&
        check(difference(initial, changed) > 100,
              "R7 selects independently authored volume layer");
  scene.raw(7, SbVec3f(.5f / 3, .5f / 5, 6.5f / 7));
  SbMatrix matrix = SbMatrix::identity();
  matrix[3][3] = 2;
  scene.matrices[7]->matrix = matrix;
  ok &=
      h.render(scene.root, restored, "shadow DIRECT_ST keeps transformed R") &&
      oracle(scene, restored) && exact(initial, restored);
  scene.style->style = SoShadowStyle::NO_SHADOWING;
  scene.receives = false;
  ok &= h.render(scene.root, changed, "unshadowed PROJECTIVE divides R by Q") &&
        oracle(scene, changed, true, false);
  scene.raw(4, SbVec3f(0, 0, -1));
  matrix[3][3] = 0;
  scene.matrices[4]->matrix = matrix;
  Frame cube;
  ok &= h.render(scene.root, cube, "cube negative Z ignores finite zero Q") &&
        oracle(scene, cube, true, false) &&
        check(difference(changed, cube) > 100,
              "extra cubemap retains sign of R and separate image identity");
  scene.raw(4, SbVec3f(0, 0, 1));
  scene.matrices[4]->matrix = SbMatrix::identity();
  scene.matrices[7]->matrix = SbMatrix::identity();
  scene.style->style = SoShadowStyle::SHADOWED;
  scene.receives = true;
  ok &= h.render(scene.root, restored, "STRQ binding A/B/A") &&
        oracle(scene, restored) && exact(initial, restored);
  return ok;
}

bool invalid(Scene &scene, Harness &h, const Frame &initial) {
  Frame restored;
  bool ok = true;
  auto recovery = [&]() {
    return h.render(scene.root, restored, "invalid source recovery") &&
           oracle(scene, restored) && exact(initial, restored);
  };
  scene.material->transparency = .5f;
  ok &=
      h.reject(scene.root, initial, "transparent bump shadow outside profile");
  scene.material->transparency = 0;
  ok &= recovery();
  auto *extra = new SoShadowSpotLight;
  scene.group->insertChild(extra, 1);
  ok &= h.reject(scene.root, initial,
                 "spot shadow light outside directional-only extension");
  scene.group->removeChild(extra);
  ok &= recovery();
  const SbVec2f uv = scene.uv->point[0];
  scene.uv->point.set1Value(0,
                            SbVec2f(std::numeric_limits<float>::infinity(), 0));
  ok &= h.reject(scene.root, initial, "nonfinite authored bump UV");
  scene.uv->point.set1Value(0, uv);
  ok &= recovery();
  SbMatrix matrix = SbMatrix::identity();
  matrix[0][0] = std::numeric_limits<float>::quiet_NaN();
  scene.matrices[7]->matrix = matrix;
  ok &= h.reject(scene.root, initial, "nonfinite spatial texture matrix");
  scene.matrices[7]->matrix = SbMatrix::identity();
  ok &= recovery();
  const SbVec3f originalCoord4 = scene.coords[4]->point[0];
  scene.raw(4, scene.only2D
                   ? SbVec3f(std::numeric_limits<float>::infinity(), 0, 0)
                   : SbVec3f(0, 0, 0));
  ok &= h.reject(scene.root, initial,
                 scene.only2D ? "nonfinite 2D unit four coordinates"
                              : "zero cubemap direction");
  scene.raw(4, originalCoord4);
  ok &= recovery();
  scene.style->style = SoShadowStyle::NO_SHADOWING;
  scene.receives = false;
  Frame unshadowed;
  ok &= h.render(scene.root, unshadowed, "unshadowed extra spatial units") &&
        oracle(scene, unshadowed, true, false);
  matrix = SbMatrix::identity();
  matrix[3][3] = 0;
  scene.matrices[7]->matrix = matrix;
  ok &= h.reject(scene.root, unshadowed, "projective texture Q zero");
  scene.matrices[7]->matrix = SbMatrix::identity();
  scene.style->style = SoShadowStyle::SHADOWED;
  scene.receives = true;
  ok &= recovery();
  return ok;
}

// The ordinary VSM receiver is a separately qualified white equivalent.
// Its integer output bounds V; the combined equation is then evaluated from
// authored N/L/E/material/texture inputs. Analytic V=0/1 gates remain separate.
bool fractional(Scene &scene, Harness &h) {
  const RGB spec = scene.spec;
  bool ok = true;
  for (float quality : {.5f, 1.f}) {
    scene.group->quality = quality;
    scene.group->epsilon = .01f;
    scene.caster->translation.setValue(.017f, .011f, .75f);
    scene.bumpStage->whichChild = SO_SWITCH_NONE;
    scene.setSpec({{0, 0, 0}});
    scene.paint(false);
    Frame white, combined;
    ok &= h.render(scene.root, white, "ordinary white equivalent for VSM");
    scene.paint(true);
    scene.setSpec(spec);
    scene.bumpStage->whichChild = SO_SWITCH_ALL;
    ok &= h.render(scene.root, combined, "fractional VSM combined equation");
    CoinRenderShadowPlan captured;
    std::string diagnostic;
    const auto &frame = h.audit->plan;
    const bool planned = coin_render_plan_shadows(frame, captured, diagnostic);
    uint32_t lightIndex = UINT32_MAX;
    for (uint32_t i = 0; i < frame.shadowLights.size(); ++i)
      if (frame.shadowLights[i].sourceRevision ==
          scene.directional->getNodeId())
        lightIndex = i;
    bool metadata = planned && captured.passes.size() == 1 &&
                    lightIndex < frame.shadowLights.size() &&
                    frame.shadowGroups.size() == 1;
    if (metadata) {
      const auto &light = frame.shadowLights[lightIndex];
      const auto &pass = captured.passes[0];
      SbVec3f receiver, casterFront;
      pass.view.multVecMatrix(SbVec3f(0, 0, 0), receiver);
      pass.view.multVecMatrix(SbVec3f(0, 0, 1), casterFront);
      metadata &=
          light.enabled && light.shadowEligible && light.hasCustomScene &&
          light.mapSceneCaptured && light.bboxCenter == SbVec3f(0, 0, 0) &&
          light.bboxSize == SbVec3f(4, 4, 8) && pass.lightSlot == lightIndex &&
          pass.groupSlot == light.groupSlot && pass.visible &&
          !pass.casterDraws.empty() && !pass.receiverDraws.empty() &&
          std::abs(pass.nearDistance) < 1e-4 &&
          std::abs(pass.farDistance - 8.08) < 1e-4 &&
          std::abs(pass.vsmFarDistance - 8.888) < 1e-4 &&
          std::abs(pass.epsilon - .01) < 1e-7 && pass.threshold == 0 &&
          std::abs(receiver[2] + 4) < 1e-4 &&
          std::abs(casterFront[2] + 3) < 1e-4;
      std::cout << "fractional_metadata light=" << lightIndex
                << " near=" << pass.nearDistance << " far=" << pass.farDistance
                << " vsm_far=" << pass.vsmFarDistance
                << " epsilon=" << pass.epsilon
                << " threshold=" << pass.threshold
                << " receiver_z=" << receiver[2]
                << " caster_front_z=" << casterFront[2] << " valid=" << metadata
                << '\n';
    }
    ok &= check(metadata, "captured light, authored bbox and VSM range match "
                          "independent closed-box derivation " +
                              diagnostic);
    if (white.rgba.size() != side * side * 4 ||
        combined.rgba.size() != white.rgba.size())
      return false;
    // Independently authored geometry fixes the uniform moments in the deep
    // caster interior. The light fits the sphere of the root-view/bbox
    // intersection (4,4,7), so cameraZ=4, near=0, vsmFar=8*1.01*1.1.
    // Front caster Z=1, receiver Z=0: their normalized distance delta is
    // 1/range.
    const double delta = 1 / (8 * 1.01 * 1.1);
    const double epsilon = .01;
    const double probability = epsilon / (epsilon + delta * delta);
    const double analyticV =
        probability * probability * probability * (3 - 2 * probability);
    unsigned analyticCount = 0;
    unsigned partial = 0, dark = 0, lit = 0, bad = 0;
    int maximum = 0;
    for (unsigned y = 20; y < 76; ++y)
      for (unsigned x = 20; x < 76; ++x) {
        const size_t p = y * side + x;
        double lo = 0, hi = 1;
        for (unsigned c = 0; c < 3; ++c) {
          const double coefficient =
              255 * scene.diffuse[c] * scene.lamp[c] * scene.intensity;
          lo = std::max(lo, (white.rgba[4 * p + c] - 1.) / coefficient);
          hi = std::min(hi, (white.rgba[4 * p + c] + 1.) / coefficient);
        }
        if (lo > hi) {
          ++bad;
          continue;
        }
        const double worldX = (x + .5) * 4 / side - 2;
        const double worldY = 2 - (y + .5) * 4 / side;
        if (std::abs(worldX - .017) < .18 && std::abs(worldY - .011) < .18) {
          ++analyticCount;
          const auto expected = scene.predicted(x, y, analyticV);
          for (unsigned c = 0; c < 3; ++c) {
            const double whiteExpected = 255 * scene.diffuse[c] *
                                         scene.lamp[c] * scene.intensity *
                                         analyticV;
            const int whiteDelta = std::abs(int(white.rgba[4 * p + c]) -
                                            int(std::lround(whiteExpected)));
            const int combinedDelta = std::abs(int(combined.rgba[4 * p + c]) -
                                               int(std::lround(expected[c])));
            if ((whiteDelta > 3 || combinedDelta > 3) && bad++ < 4)
              std::cerr << "fractional_analytic_first xy=" << x << ',' << y
                        << " V=" << analyticV
                        << " white_expected=" << whiteExpected
                        << " white_actual=" << unsigned(white.rgba[4 * p + c])
                        << " combined_expected=" << expected[c]
                        << " combined_actual="
                        << unsigned(combined.rgba[4 * p + c]) << '\n';
          }
        }
        const double visibility = (lo + hi) * .5;
        partial += lo > .02 && hi < .98;
        dark += hi < .02;
        lit += lo > .98;
        const auto rgb = scene.predicted(x, y, visibility);
        for (unsigned c = 0; c < 3; ++c) {
          const int delta = std::abs(int(combined.rgba[4 * p + c]) -
                                     int(std::lround(rgb[c])));
          maximum = std::max(maximum, delta);
          if (delta > 3 && bad++ < 4)
            std::cerr << "fractional_first xy=" << x << ',' << y
                      << " V_interval=" << lo << ',' << hi
                      << " expected=" << rgb[c]
                      << " actual=" << unsigned(combined.rgba[4 * p + c])
                      << '\n';
        }
        bad += combined.rgba[4 * p + 3] != 255;
        bad += std::abs(combined.depth[p] - .375f) > 1e-6;
      }
    std::cout << "fractional_visibility quality=" << quality
              << " fractional=" << partial
              << " analytic_interior=" << analyticCount
              << " analytic_V=" << analyticV << " dark=" << dark
              << " lit=" << lit << " bad=" << bad << " max=" << maximum << '\n';
    ok &= check(partial > 4 && analyticCount > 20 && analyticV > .02 &&
                    analyticV < .98 && lit > 500 && bad == 0 &&
                    combined.depth == white.depth,
                "observably fractional ordinary VSM visibility multiplies "
                "authored diffuse and post-stack specular");
  }
  scene.caster->translation.setValue(0, 0, .75f);
  scene.group->quality = 1;
  scene.group->epsilon = 0;
  return ok;
}

bool coverage(Scene &scene, Harness &h, unsigned samples) {
  scene.setSpec({{0, 0, 0}});
  auto *rotation = new SoRotation;
  rotation->rotation.setValue(SbVec3f(0, 0, 1), .23f);
  scene.group->insertChild(rotation, scene.group->findChild(scene.positions));
  Frame frame;
  if (!h.render(scene.root, frame, "active-map combined sloped coverage"))
    return false;
  const RGB litRGB = scene.predicted(side / 2, side / 2, 1);
  unsigned empty = 0, full = 0, partial = 0, odd = 0, shadow = 0, bad = 0;
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      const size_t p = y * side + x;
      const unsigned alpha = frame.rgba[4 * p + 3];
      const unsigned k = unsigned(std::lround(alpha * samples / 255.));
      bad += k > samples ||
             std::abs(int(alpha) - int(std::lround(255. * k / samples))) > 1;
      empty += k == 0;
      full += k == samples;
      partial += k > 0 && k < samples;
      odd += k > 0 && k < samples && (k & 1);
      if (!alpha) {
        bad += frame.depth[p] != 1;
        for (unsigned c = 0; c < 3; ++c)
          bad += frame.rgba[4 * p + c] != 0;
        continue;
      }
      bad += std::abs(frame.depth[p] - .375f) > 1e-6;
      const double px = (x + .5) * 4 / side - 2, py = 2 - (y + .5) * 4 / side;
      double visibility;
      if (std::abs(px) < .18 && std::abs(py) < .18) {
        visibility = 0;
        ++shadow;
      } else if (std::max(std::abs(px), std::abs(py)) > .65)
        visibility = 1;
      else
        continue; // VSM transition: separately bounded by --fractional.
      for (unsigned c = 0; c < 3; ++c)
        bad += std::abs(
                   int(frame.rgba[4 * p + c]) -
                   int(std::lround(litRGB[c] * visibility * k / samples))) > 3;
    }
  std::cout << "combined_coverage samples=" << samples << " empty=" << empty
            << " full=" << full << " partial=" << partial << " odd=" << odd
            << " shadow=" << shadow << " bad=" << bad << '\n';
  return check(empty > 100 && full > 100 && shadow > 20 && bad == 0 &&
                   (samples == 1 || partial > 0) && (samples != 4 || odd > 0),
               "active combined map preserves true k/N color/alpha and "
               "per-sample geometric depth");
}

// One public shape is traversed three times under independently scoped state.
// No result depends on the executor's grouping or pass architecture.
bool mixed(Scene &scene, Harness &h, bool specular = false,
           bool allTransitions = false) {
  struct Branch {
    SoSeparator *scope;
    SoTranslation *move;
    SoScale *scale;
    SoBumpMap *map;
    RGB diffuse;
    std::array<uint8_t, 3> normal;
    unsigned layer;
    SoMaterial *material;
    SoNormal *normalNode;
  };
  Branch branches[] = {{new SoSeparator,
                        new SoTranslation,
                        new SoScale,
                        new SoBumpMap,
                        {{.75, .35, .25}},
                        {{154, 110, 246}},
                        6,
                        nullptr,
                        nullptr},
                       {new SoSeparator,
                        new SoTranslation,
                        new SoScale,
                        new SoBumpMap,
                        {{.25, .7, .35}},
                        {{210, 145, 205}},
                        0,
                        nullptr,
                        nullptr},
                       {new SoSeparator,
                        new SoTranslation,
                        new SoScale,
                        new SoBumpMap,
                        {{.35, .3, .8}},
                        {{90, 190, 230}},
                        3,
                        nullptr,
                        nullptr}};
  scene.mesh->ref();
  scene.group->removeChild(scene.mesh);
  for (unsigned i = 0; i < 3; ++i) {
    auto &b = branches[i];
    b.scope->ref();
    b.scope->addChild(b.move);
    b.scale->scaleFactor.setValue(.28f, .8f, 1);
    b.scope->addChild(b.scale);
    auto *material = new SoMaterial;
    b.material = material;
    b.normalNode = new SoNormal;
    b.normalNode->vector.setValue(0, 0, 1);
    b.scope->addChild(b.normalNode);
    material->diffuseColor.setValue(float(b.diffuse[0]), float(b.diffuse[1]),
                                    float(b.diffuse[2]));
    material->ambientColor.setValue(0, 0, 0);
    material->emissiveColor.setValue(0, 0, 0);
    material->specularColor.setValue(
        specular ? .35f : 0.f, specular ? .25f : 0.f, specular ? .4f : 0.f);
    material->shininess = .25f;
    b.scope->addChild(material);
    b.scope->addChild(b.map);
    auto *select = new SoTextureUnit;
    select->unit = 7;
    b.scope->addChild(select);
    auto *coords = new SoTextureCoordinate3;
    for (unsigned corner = 0; corner < 4; ++corner)
      coords->point.set1Value(
          corner, SbVec3f(.5f / 3, .5f / 5, float(b.layer + .5) / 7));
    b.scope->addChild(coords);
    auto *restore = new SoTextureUnit;
    restore->unit = 0;
    b.scope->addChild(restore);
    b.scope->addChild(scene.mesh);
    scene.group->addChild(b.scope);
  }
  scene.mesh->unref();
  auto order = [&](bool reverse) {
    for (auto &b : branches)
      scene.group->removeChild(b.scope);
    for (unsigned i = 0; i < 3; ++i)
      scene.group->addChild(branches[reverse ? 2 - i : i].scope);
  };
  auto maps = [&](unsigned active, bool mutation) {
    for (unsigned i = 0; i < 3; ++i) {
      auto &b = branches[i];
      // Precompensate authored X for inverse-transpose(.28,.8,1), so
      // the ordinary VIEW normal really is unit(sqrt(1-.95^2),0,.95).
      // This keeps its Phong128 specular observably nonzero.
      b.normalNode->vector.setValue(
          specular && !(active & (1u << i))
              ? float(.28 * std::sqrt(1 - .95 * .95))
              : 0.f,
          0, specular && !(active & (1u << i)) ? .95f : 1.f);
      if (!(active & (1u << i)))
        b.map->image.setValue(SbVec2s(0, 0), 0, nullptr);
      else {
        auto n = b.normal;
        if (mutation) {
          n[0] = 180;
          n[1] = 100;
          n[2] = 190;
        }
        b.map->image.setValue(SbVec2s(1, 1), 3, n.data());
      }
    }
  };
  auto place = [&](bool overlap, bool ordinaryFront) {
    for (unsigned i = 0; i < 3; ++i) {
      const float z = overlap ? (i == 1 ? (ordinaryFront ? .7f : -.4f)
                                        : (i == 0 ? .4f : -.7f))
                              : 0;
      branches[i].move->translation.setValue(
          overlap ? 0.f : (int(i) - 1) * 1.1f, 0, z);
    }
  };
  auto expected = [&](unsigned branch, unsigned active, bool mutation,
                      double visibility, double worldX, double worldY,
                      double center, double z) {
    const auto &b = branches[branch];
    const double nz = !(active & (1u << branch))
                          ? (specular ? .95 : 1)
                          : ((mutation ? 190 : b.normal[2]) * 2. / 255 - 1);
    RGB rgb;
    for (unsigned c = 0; c < 3; ++c)
      rgb[c] = visibility * b.diffuse[c] * scene.lamp[c] * scene.intensity *
               clamp(nz);
    for (unsigned u = 0; u < 8; ++u) {
      auto texel = scene.texel(u);
      if (u == 7 && scene.volume[7]) {
        texel = Scene::bytes(u, b.layer);
        for (auto &v : texel) {
          v /= 255;
          if (scene.srgb)
            v = bump_profile::decode(v);
        }
      }
      for (unsigned c = 0; c < 3; ++c)
        rgb[c] = clamp(rgb[c] * texel[c]);
    }
    if (specular) {
      const RGB materialSpec{{.35, .25, .4}};
      const bool bump = active & (1u << branch);
      double factor;
      if (!bump)
        factor = std::pow(.95, .25 * 128);
      else {
        const double u = ((worldX - center) / .28 + 1.4) / 2.7;
        const double v = (worldY / .8 + 1.3) / 2.65;
        const unsigned idsA[] = {0, 1, 2}, idsB[] = {0, 2, 3};
        const double wa[] = {1 - u, u - v, v}, wb[] = {1 - v, u, v - u};
        const unsigned *ids = v <= u ? idsA : idsB;
        const double *weights = v <= u ? wa : wb;
        // Legacy R26: transform eye through MV inverse into OBJECT space
        // before subtracting each corner and normalizing. Nonuniform scale
        // therefore divides camera translation; it does not scale corner E.
        RGB eye{{0, 0, 0}};
        for (unsigned k = 0; k < 3; ++k) {
          const auto p = scene.positions->point[ids[k]];
          const RGB corner =
              bump_profile::normalized({{-center / .28 - p[0], p[1], 4 - z}});
          for (unsigned c = 0; c < 3; ++c)
            eye[c] += weights[k] * corner[c];
        }
        const RGB half =
            bump_profile::normalized({{eye[0], eye[1], 1 + eye[2]}});
        const RGB n =
            mutation
                ? RGB{{180 * 2. / 255 - 1, 100 * 2. / 255 - 1,
                       190 * 2. / 255 - 1}}
                : RGB{{b.normal[0] * 2. / 255 - 1, b.normal[1] * 2. / 255 - 1,
                       b.normal[2] * 2. / 255 - 1}};
        factor = std::pow(clamp(bump_profile::dot(n, half)), .25 * 64);
      }
      for (unsigned c = 0; c < 3; ++c)
        rgb[c] =
            clamp(rgb[c] + visibility * materialSpec[c] * factor *
                               (bump ? 1 : scene.lamp[c] * scene.intensity));
    }
    for (auto &v : rgb)
      v *= 255;
    return rgb;
  };
  auto inspect = [&](const Frame &frame, unsigned active, bool mutation,
                     bool overlap, bool ordinaryFront) {
    unsigned count = 0, lit = 0, dark = 0, bad = 0;
    int maximum = 0;
    for (unsigned b = 0; b < 3; ++b) {
      if (overlap && b != (ordinaryFront ? 1u : 0u))
        continue;
      const double center = overlap ? 0 : (int(b) - 1) * 1.1;
      const double z = overlap ? (ordinaryFront ? .7 : .4) : 0;
      for (unsigned y = 30; y < 66; ++y)
        for (unsigned x = 0; x < side; ++x) {
          const double px = (x + .5) * 4 / side - 2;
          const double py = 2 - (y + .5) * 4 / side;
          if (std::abs(px - center) > .2)
            continue;
          double visibility;
          if (std::abs(px) < .18 && std::abs(py) < .18 && z < .9) {
            visibility = 0;
            ++dark;
          } else if (std::max(std::abs(px), std::abs(py)) > .65 || z > 1.01) {
            visibility = 1;
            ++lit;
          } else
            continue;
          const RGB rgb =
              expected(b, active, mutation, visibility, px, py, center, z);
          const size_t p = y * side + x;
          for (unsigned c = 0; c < 3; ++c) {
            const int delta =
                std::abs(int(frame.rgba[4 * p + c]) - int(std::lround(rgb[c])));
            maximum = std::max(maximum, delta);
            if (delta > 3 && bad++ < 4)
              std::cerr << "mixed_first branch=" << b << " xy=" << x << ',' << y
                        << " expected=" << rgb[c]
                        << " actual=" << unsigned(frame.rgba[4 * p + c])
                        << " V=" << visibility << '\n';
          }
          bad += frame.rgba[p * 4 + 3] != 255;
          bad += std::abs(frame.depth[p] - float((3 - z) / 8)) > 1e-6;
          ++count;
        }
    }
    std::cout << "mixed_shared active=" << active << " overlap=" << overlap
              << " ordinary_front=" << ordinaryFront << " witnesses=" << count
              << " lit=" << lit << " dark=" << dark << " bad=" << bad
              << " max=" << maximum << '\n';
    return check(count > 20 && lit > 20 &&
                     (!overlap || ordinaryFront || dark > 20) && bad == 0,
                 "shared shape independently preserves bump/ordinary, R7, "
                 "shadow and geometric depth");
  };
  bool ok = true;
  Frame first, changed, back;
  for (unsigned family = 0; family < 2; ++family) {
    const unsigned active = family ? 2 : 5;
    place(false, false);
    maps(active, false);
    order(false);
    ok &= h.render(scene.root, first, "shared active/ordinary contexts") &&
          inspect(first, active, false, false, false);
    order(true);
    ok &= h.render(scene.root, back, "shared contexts reverse scene order") &&
          inspect(back, active, false, false, false) && exact(first, back);
    maps(active, true);
    ok &= h.render(scene.root, changed, "shared map identities mutate") &&
          inspect(changed, active, true, false, false) &&
          check(difference(first, changed) > 100,
                "shared active maps remain observable");
    maps(active, false);
    ok &= h.render(scene.root, back, "shared context A/B/A") &&
          exact(first, back);
  }
  if (allTransitions) {
    // Reuse the SAME geometry in every context, now removing and restoring
    // each executor family after the cold mixed first application.
    place(false, false);
    order(false);
    for (unsigned active : {0u, 7u, 5u, 0u, 7u}) {
      maps(active, false);
      ok &=
          h.render(scene.root, first,
                   "shared geometry all ordinary/all bump/mixed transition") &&
          inspect(first, active, false, false, false);
      maps(active, true);
      ok &=
          h.render(scene.root, changed, "transition active payload mutation") &&
          inspect(changed, active, true, false, false);
      maps(active, false);
      ok &= h.render(scene.root, back, "transition payload A/B/A") &&
            inspect(back, active, false, false, false) && exact(first, back);
    }
    SbMatrix malformed = SbMatrix::identity();
    malformed[0][0] = std::numeric_limits<float>::quiet_NaN();
    scene.matrices[7]->matrix = malformed;
    ok &= h.reject(scene.root, back,
                   "mixed executor reuse keeps preflight validation atomic");
    scene.matrices[7]->matrix = SbMatrix::identity();
    ok &= h.render(scene.root, changed, "mixed executor preflight recovery") &&
          inspect(changed, 7, false, false, false) && exact(back, changed);
  }
  maps(5, false);
  for (bool ordinaryFront : {false, true}) {
    place(true, ordinaryFront);
    order(false);
    ok &= h.render(scene.root, first, "overlapped mixed shared geometry") &&
          inspect(first, 5, false, true, ordinaryFront);
    order(true);
    ok &= h.render(scene.root, back, "overlapped mixed order inverted") &&
          inspect(back, 5, false, true, ordinaryFront) && exact(first, back);
  }
  for (auto &b : branches)
    b.scope->unref();
  return ok;
}

bool delivery(Scene &scene, Harness &h);

bool coldStart(Scene &scene, Harness &h, const std::string &mode) {
  const auto begin = std::chrono::steady_clock::now();
  if (!check(h.target->getLastSubmissionSerial() == 0 &&
                 h.audit->rootSubmits == 0 && h.audit->producers == 0,
             "cold process target has no prior scene application or producer"))
    return false;
  bool ok = true;
  if (mode == "cold-mixed") {
    // mixed() constructs all contexts before its FIRST render. No ordinary
    // control or bump-only frame precedes this application.
    ok &= mixed(scene, h, true, true);
  } else {
    Frame first, ordinary, bump, back;
    if (mode == "cold-ordinary") {
      ok &= ordinaryControl(scene, h, first);
      ordinary = first;
      ok &=
          h.render(scene.root, bump, "ordinary to combined first transition") &&
          oracle(scene, bump) && delivery(scene, h);
    } else {
      ok &= h.render(scene.root, first, "cold bump-only first application") &&
            oracle(scene, first) && delivery(scene, h);
      bump = first;
      ok &= ordinaryControl(scene, h, ordinary);
    }
    ok &= h.render(scene.root, back, "ordinary to bump A/B/A") &&
          oracle(scene, back) && exact(bump, back);
    ok &= smoke(scene, h, bump);
    Frame ordinaryBack;
    ok &= ordinaryControl(scene, h, ordinaryBack) &&
          exact(ordinary, ordinaryBack);
    ok &= h.render(scene.root, back, "bump restored after ordinary reuse") &&
          oracle(scene, back) && exact(bump, back);
    ok &= invalid(scene, h, back);
    ok &= mixed(scene, h, true, true);
  }
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - begin)
          .count();
  std::cout << "cold_profile mode=" << mode << " elapsed_seconds=" << elapsed
            << " deadline_seconds=180" << std::endl;
  return check(elapsed <= 180, "cold first-apply and transitions keep original "
                               "180-second process budget") &&
         ok;
}

bool bindings(Scene &scene, Harness &h, const Frame &initial) {
  Frame changed, back;
  const RGB bytes{{36, 80, 120}};
  bool ok = true;
  for (unsigned u = 0; u < 8; ++u) {
    RGB sampled = bytes;
    for (auto &value : sampled) {
      value /= 255;
      if (scene.srgb)
        value = bump_profile::decode(value);
    }
    scene.customTexel[u] = sampled;
    auto pixels = [&](unsigned count) {
      std::vector<uint8_t> result(count * 4);
      for (unsigned i = 0; i < count; ++i) {
        for (unsigned c = 0; c < 3; ++c)
          result[i * 4 + c] = uint8_t(bytes[c]);
        result[i * 4 + 3] = 255;
      }
      return result;
    };
    if (!u)
      scene.setBase({{36, 80, 120, 255}});
    else if (scene.volume[u]) {
      const auto data = pixels(3 * 5 * 7);
      scene.volume[u]->images.setValue(SbVec3s(3, 5, 7), 4, data.data());
    } else if (scene.cube[u]) {
      const auto data = pixels(4);
      SoSFImage *faces[] = {
          &scene.cube[u]->imagePosX, &scene.cube[u]->imageNegX,
          &scene.cube[u]->imagePosY, &scene.cube[u]->imageNegY,
          &scene.cube[u]->imagePosZ, &scene.cube[u]->imageNegZ};
      for (auto *image : faces)
        image->setValue(SbVec2s(2, 2), 4, data.data());
    } else {
      const auto data = pixels(1);
      scene.extra2D[u]->image.setValue(SbVec2s(1, 1), 4, data.data());
    }
    ok &= h.render(scene.root, changed,
                   "independent unit payload mutation=" + std::to_string(u)) &&
          oracle(scene, changed) &&
          check(difference(initial, changed) > 500,
                "each one of eight unit resources has independent visible "
                "contribution");
    scene.customTexel[u] = {{-1, -1, -1}};
    scene.paint(true);
    ok &=
        h.render(scene.root, back, "unit payload A/B/A=" + std::to_string(u)) &&
        oracle(scene, back) && exact(initial, back);
  }
  const uint8_t tiles[] = {30, 220, 70,  255, 220, 50,  180, 255,
                           70, 100, 230, 255, 240, 200, 40,  255};
  scene.extra2D[3]->image.setValue(SbVec2s(2, 2), 4, tiles);
  auto expectedTile = [&](unsigned index) {
    for (unsigned c = 0; c < 3; ++c) {
      double value = tiles[index * 4 + c] / 255.;
      scene.customTexel[3][c] =
          scene.srgb ? bump_profile::decode(value) : value;
    }
  };
  for (unsigned tile = 0; tile < 4; ++tile) {
    scene.raw(
        3, SbVec3f((tile & 1) ? .75f : .25f, (tile & 2) ? .75f : .25f, .37f));
    expectedTile(tile);
    ok &=
        h.render(scene.root, changed,
                 "unit3 independent 2D S/T quadrant=" + std::to_string(tile)) &&
        oracle(scene, changed);
  }
  scene.raw(3, SbVec3f(-.25f, 1.25f, .37f));
  for (bool repeat : {true, false, true}) {
    scene.extra2D[3]->wrapS = scene.extra2D[3]->wrapT =
        repeat ? SoTexture2::REPEAT : SoTexture2::CLAMP;
    expectedTile(repeat ? 1 : 2);
    ok &= h.render(scene.root, changed, "unit3 2D out-of-range wrap S/T") &&
          oracle(scene, changed);
  }
  scene.extra2D[3]->wrapS = scene.extra2D[3]->wrapT = SoTexture2::CLAMP;
  scene.raw(3, SbVec3f(.25f, .75f, .37f));
  SbMatrix matrix = SbMatrix::identity();
  matrix[0][0] = .3f;
  matrix[1][1] = .5f;
  matrix[2][0] = .7f;
  matrix[3][0] = .3f;
  matrix[3][3] = 2;
  scene.matrices[3]->matrix = matrix;
  // S'=.3*.25+.7*.37+.3=.634,T'=.375: quadrant1.
  // Dropping R3 gives S'=.375 (wrong quadrant0). Q=2 is ignored in DIRECT_ST.
  expectedTile(1);
  ok &= h.render(scene.root, changed,
                 "2D shadow STR matrix mixes R into S and ignores Q") &&
        oracle(scene, changed);
  scene.style->style = SoShadowStyle::NO_SHADOWING;
  scene.receives = false;
  // PROJECTIVE divides S'/T' by Q and selects quadrant0.
  expectedTile(0);
  ok &= h.render(scene.root, changed, "2D unshadowed transformed ST/Q") &&
        oracle(scene, changed, true, false);
  scene.style->style = SoShadowStyle::SHADOWED;
  scene.receives = true;
  scene.customTexel[3] = {{-1, -1, -1}};
  scene.raw(3, SbVec3f(.5f, .5f, 0));
  scene.matrices[3]->matrix = SbMatrix::identity();
  scene.paint(true);
  ok &= h.render(scene.root, back, "2D STRQ/resource/sampler A/B/A") &&
        oracle(scene, back) && exact(initial, back);
  return ok;
}

bool profileRefusals(Scene &scene, Harness &h, const Frame &initial,
                     const CoinRenderCapabilities &caps, unsigned samples,
                     bool srgb) {
  bool ok = true;
  Frame restored;
  auto recovery = [&]() {
    return h.render(scene.root, restored, "profile refusal recovery") &&
           oracle(scene, restored) && exact(initial, restored);
  };
  auto *spot = new SoShadowSpotLight;
  spot->location.setValue(0, 0, 3);
  spot->direction.setValue(0, 0, -1);
  spot->cutOffAngle = .55f;
  spot->nearDistance = 1;
  spot->farDistance = 9;
  scene.directional->on = FALSE;
  scene.group->insertChild(spot, 0);
  ok &= h.reject(scene.root, initial,
                 "one spot shadow remains outside one DIR extension");
  scene.group->removeChild(spot);
  scene.directional->on = TRUE;
  ok &= recovery();
  auto *fog = new SoEnvironment;
  fog->ambientIntensity = 0;
  fog->fogType = SoEnvironment::FOG;
  fog->fogVisibility = 10;
  scene.group->insertChild(fog, scene.group->findChild(scene.mesh));
  ok &= h.reject(scene.root, initial,
                 "fog remains outside combined bump profile");
  scene.group->removeChild(fog);
  ok &= recovery();
  auto *clip = new SoClipPlane;
  clip->plane.setValue(SbPlane(SbVec3f(1, 0, 0), 0));
  scene.group->insertChild(clip, scene.group->findChild(scene.mesh));
  ok &= h.reject(scene.root, initial,
                 "clip plane remains outside combined bump profile");
  scene.group->removeChild(clip);
  ok &= recovery();
  scene.material->transparency = .5f;
  h.action.setTransparencyType(CoinRenderAction::SORTED_LAYERS_BLEND);
  ok &= h.reject(scene.root, initial,
                 "peeling remains outside opaque combined bump profile");
  h.action.setTransparencyType(CoinRenderAction::NONE);
  scene.material->transparency = 0;
  ok &= recovery();
  auto *depth = new SoDepthBuffer;
  depth->range.setValue(.2f, .8f);
  scene.group->insertChild(depth, scene.group->findChild(scene.mesh));
  ok &= h.reject(scene.root, initial,
                 "custom depth remains outside combined profile");
  scene.group->removeChild(depth);
  ok &= recovery();
  scene.setBase({{204, 208, 200, 128}});
  ok &= h.reject(
      scene.root, initial,
      "transparent source image remains outside opaque combined profile");
  scene.paint(true);
  ok &= recovery();
  auto *bad = new SoGroup;
  auto *unit = new SoTextureUnit;
  unit->unit = 8;
  bad->addChild(unit);
  auto *image = new SoTexture2;
  const uint8_t pixel[] = {90, 140, 200, 255};
  image->image.setValue(SbVec2s(1, 1), 4, pixel);
  bad->addChild(image);
  scene.group->insertChild(bad, scene.group->findChild(scene.mesh));
  ok &= h.reject(scene.root, initial,
                 "active texture unit eight outside finite slot domain");
  scene.group->removeChild(bad);
  ok &= recovery();
  CoinRenderReadbackTicket ticket;
  const auto serial = h.target->getLastSubmissionSerial();
  const auto submits = h.audit->rootSubmits, producers = h.audit->producers;
  h.action.applyAsync(scene.root, ticket);
  Frame retained;
  h.target->readbackRGBA(retained.rgba);
  h.target->readbackDepth(retained.depth);
  ok &= check(
      h.action.getLastStatus() != CoinRenderAction::SUCCESS &&
          h.action.getLastError().getLength() && ticket.token == 0 &&
          h.target->getLastSubmissionSerial() == serial &&
          h.audit->rootSubmits == submits && h.audit->producers == producers &&
          retained.rgba == initial.rgba && retained.depth == initial.depth,
      "combined async refuses without issuing ticket or producer/publication");
  ok &= recovery();
  {
    Scene canonical;
    canonical.srgb = false;
    // Canonical+MSAA is itself outside the initial ordinary profile.
    // Exercise the canonical bump refusal on a genuinely admitted 1x
    // ordinary publication, independently of this focal's outer MSAA.
    Harness peer(caps, 1, false, COIN_RENDER_SCENE_TEXTURE_STAGED,
                 COIN_RENDER_RASTER_CANONICAL_8BIT);
    std::cout << "canonical_peer outer_samples=" << samples
              << " focal_samples=1\n";
    ok &= check(peer.target && peer.target->getOptions().sampleCount == 1 &&
                    peer.target->getOptions().rasterizationPolicy ==
                        COIN_RENDER_RASTER_CANONICAL_8BIT,
                "canonical refusal uses explicit supported 1x ordinary target");
    Frame ordinary;
    ok &= ordinaryControl(canonical, peer, ordinary) &&
          baseline(canonical, peer, ordinary);
  }
  {
    Scene weighted(255, scene.only2D);
    weighted.srgb = srgb;
    Harness peer(caps, samples, srgb, COIN_RENDER_SCENE_TEXTURE_STAGED,
                 COIN_RENDER_RASTER_NATIVE,
                 COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT);
    Frame ordinary, returned;
    const RGB spec = weighted.spec;
    ok &= ordinaryControl(weighted, peer, ordinary);
    weighted.material->transparency = .5f;
    peer.action.setTransparencyType(CoinRenderAction::BLEND);
    ok &= peer.reject(weighted.root, ordinary,
                      "weighted OIT remains outside opaque bump extension");
    weighted.material->transparency = 0;
    weighted.bumpStage->whichChild = SO_SWITCH_NONE;
    weighted.setSpec({{0, 0, 0}});
    peer.action.setTransparencyType(CoinRenderAction::NONE);
    ok &= peer.render(weighted.root, returned,
                      "ordinary weighted target recovery") &&
          exact(ordinary, returned);
    (void)spec;
  }
  Frame retainedOriginal;
  h.target->readbackRGBA(retainedOriginal.rgba);
  h.target->readbackDepth(retainedOriginal.depth);
  ok &= check(h.target->getLastSubmissionSerial() == restored.serial &&
                  retainedOriginal.rgba == initial.rgba &&
                  retainedOriginal.depth == initial.depth,
              "canonical/weighted peer lifetimes do not affect original "
              "publication without reapply");
  return ok;
}

bool coordinates(Scene &scene, Harness &h, const Frame &initial) {
  const unsigned char texels[] = {230, 100, 245, 40, 180, 180};
  const RGB colors[] = {RGB{{230, 100, 245}}, RGB{{40, 180, 180}}};
  scene.bump->image.setValue(SbVec2s(2, 1), 3, texels);
  scene.matrix->scaleFactor.setValue(.35f, .25f);
  auto *translation = new SoTranslation;
  auto *rotation = new SoRotation;
  const int beforeGeometry = scene.group->findChild(scene.positions);
  scene.group->insertChild(translation, beforeGeometry);
  scene.group->insertChild(rotation, beforeGeometry + 1);
  bool ok = true;
  Frame first, changed, back;
  for (unsigned transformed = 0; transformed < 2; ++transformed) {
    scene.pose = transformed ? RGB{{.18, -.12, .5}} : RGB{{0, 0, 0}};
    scene.angle = transformed ? .23 : 0;
    translation->translation.setValue(
        float(scene.pose[0]), float(scene.pose[1]), float(scene.pose[2]));
    rotation->rotation.setValue(SbVec3f(0, 0, 1), float(scene.angle));
    for (unsigned phase = 0; phase < 3; ++phase) {
      const double shift = phase == 2 ? 4 : 0;
      const double offset = phase == 1 ? .6 : .1;
      scene.matrix->translation.setValue(float(offset), .25f);
      const SbVec2f uv[] = {
          SbVec2f(float(shift), 0), SbVec2f(float(1 + shift), 0),
          SbVec2f(float(1 + shift), 1), SbVec2f(float(shift), 1)};
      scene.uv->point.setValues(0, 4, uv);
      scene.normalAt = [=, &scene](unsigned x, unsigned y) {
        const auto local =
            scene.localAt((x + .5) * 4 / side - 2, 2 - (y + .5) * 4 / side);
        const double u = ((local[0] + 1.4) / 2.7 + shift) * .35 + offset;
        const double footprint = u * 2 - .5;
        const int left = int(std::floor(footprint));
        const unsigned a = unsigned((left % 2 + 2) % 2), b = (a + 1) % 2;
        const double fraction = footprint - left;
        RGB encoded;
        for (unsigned c = 0; c < 3; ++c)
          encoded[c] = colors[a][c] * (1 - fraction) + colors[b][c] * fraction;
        return encoded;
      };
      ok &= h.render(
                scene.root, changed,
                "linear normal UV/matrix/model phase=" + std::to_string(phase) +
                    " transformed=" + std::to_string(transformed)) &&
            oracle(scene, changed);
      if (!phase)
        first = changed;
      else
        ok &= check(difference(first, changed) > 500,
                    "authored normal-map coordinate variation is observable");
    }
  }
  scene.group->removeChild(rotation);
  scene.group->removeChild(translation);
  scene.pose = {{0, 0, 0}};
  scene.angle = 0;
  scene.matrix->translation.setValue(0, 0);
  scene.matrix->scaleFactor.setValue(1, 1);
  const SbVec2f originalUV[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1),
                                SbVec2f(0, 1)};
  scene.uv->point.setValues(0, 4, originalUV);
  scene.normalAt = {};
  scene.setNormal({{154, 110, 246}});
  ok &= h.render(scene.root, back, "model/normal UV/matrix A/B/A") &&
        oracle(scene, back) && exact(initial, back);
  return ok;
}

bool rtt(Harness &h, bool srgb, CoinRenderSceneTextureMode route, bool only2D) {
  // Producer and consumer each independently have one DIR/map, bump, eight
  // mixed units. Unit3 of the consumer samples an analytically lit source
  // texel, not an opaque CPU-generated replacement for the producer.
  Scene producer(255, only2D), consumer(255, only2D);
  producer.srgb = consumer.srgb = srgb;
  auto *texture = new SoSceneTexture2;
  texture->scene = producer.root;
  texture->size.setValue(side, side);
  texture->type = SoSceneTexture2::RGBA8;
  texture->transparencyFunction = SoSceneTexture2::NONE;
  texture->model = SoSceneTexture2::MODULATE;
  const int index = consumer.extras->findChild(consumer.extra2D[3]);
  consumer.extras->removeChild(index);
  consumer.extra2D[3] = nullptr;
  consumer.extras->insertChild(texture, index);
  consumer.raw(3, SbVec3f(72.5f / side, 48.5f / side, 0));
  auto update = [&]() {
    consumer.rttTexel = producer.predicted(72, 48, 1);
    for (auto &v : consumer.rttTexel)
      v /= 255;
  };
  auto draw = [&](Frame &frame, const char *label) {
    update();
    return h.render(consumer.root, frame, label) && oracle(consumer, frame) &&
           check(h.audit->graph.producers.size() == 1 &&
                     h.target->getOptions().sceneTexture == route,
                 "real public RTT producer and requested route retained");
  };
  Frame first, changed, restored;
  bool ok = draw(first, "combined bump shadow RTT producer and consumer");
  if (!ok)
    return false;
  producer.setNormal({{210, 145, 205}});
  ok &= draw(changed, "RTT source normal mutation") &&
        check(difference(first, changed) > 100,
              "producer normal reaches consumer");
  producer.setNormal({{154, 110, 246}});
  ok &= draw(restored, "RTT producer A/B/A") && exact(first, restored);
  const auto originalUV = producer.uv->point[0];
  producer.uv->point.set1Value(
      0, SbVec2f(std::numeric_limits<float>::quiet_NaN(), 0));
  ok &=
      h.reject(consumer.root, restored, "invalid combined producer preflight");
  producer.uv->point.set1Value(0, originalUV);
  ok &= draw(restored, "RTT malformed producer recovery") &&
        exact(first, restored);

  // Deliberately fail the root only AFTER successful producer preparation.
  // Existing public OOM recovery is resize(same extent), not auto-reapply.
  const auto serial = h.target->getLastSubmissionSerial();
  const auto rootSubmits = h.audit->rootSubmits;
  const auto producers = h.audit->producers;
  Audit *const observer = h.audit;
  producer.setNormal({{210, 145, 205}});
  h.audit->failRoot = true;
  h.action.apply(consumer.root);
  Frame retained;
  h.target->readbackRGBA(retained.rgba);
  h.target->readbackDepth(retained.depth);
  std::cout << "combined_rtt_late status=" << h.action.getLastStatus()
            << " error=" << h.action.getLastError().getString()
            << " serial=" << h.target->getLastSubmissionSerial()
            << " producers=" << h.audit->producers - producers << '\n';
  ok &= check(h.action.getLastStatus() != CoinRenderAction::SUCCESS &&
                  h.target->getStatus() == CoinRenderTarget::TARGET_ERROR &&
                  h.target->getLastSubmissionSerial() == serial &&
                  h.audit->rootSubmits == rootSubmits &&
                  (route != COIN_RENDER_SCENE_TEXTURE_DIRECT ||
                   h.audit->producers > producers) &&
                  retained.rgba == first.rgba && retained.depth == first.depth,
              "late root OOM preserves prior publication after real "
              "staged/direct preparation");
  h.audit->failRoot = false;
  ok &= h.reject(consumer.root, first, "terminal OOM retry remains blocked");
  ok &= check(h.target->resize(SbVec2i32(side, side)) &&
                  h.target->getPimpl()->backend.get() == observer &&
                  h.target->getLastSubmissionSerial() == 0,
              "same-size public resize resets terminal state preserving "
              "real-backend observer");
  std::vector<uint8_t> empty;
  std::vector<float> emptyDepth;
  h.target->readbackRGBA(empty);
  h.target->readbackDepth(emptyDepth);
  ok &= check(empty.empty() && emptyDepth.empty(),
              "reset has no stale publication");
  producer.setNormal({{154, 110, 246}});
  ok &=
      draw(restored, "public RTT late OOM recovery") && exact(first, restored);
  return ok;
}

// A one-dimensional independent sampler is sufficient for the authored images:
// volume bytes vary only in R; cube bytes vary only in face-local S. Complete
// 3D area mip generation still includes every odd source R edge.
double axisSample(const volume_oracle::Level &level, unsigned axis, double u,
                  unsigned channel, bool linear, bool repeat, bool srgb) {
  const unsigned dimensions[] = {level.w, level.h, level.d};
  const unsigned n = dimensions[axis];
  const double position = u * n - (linear ? .5 : 0);
  const int lo = int(std::floor(position));
  const double weight = linear ? position - lo : 0;
  double result = 0;
  for (unsigned corner = 0; corner < 2; ++corner) {
    int address = lo + int(corner);
    address = repeat ? (address % int(n) + int(n)) % int(n)
                     : std::max(0, std::min(int(n) - 1, address));
    const size_t index =
        axis == 2 ? size_t(address) * level.w * level.h : size_t(address);
    double value = level.bytes[index * 4 + channel] / 255.;
    if (srgb)
      value = bump_profile::decode(value);
    result += (corner ? weight : 1 - weight) * value;
  }
  return result;
}
RGB sampleChain(const std::vector<volume_oracle::Level> &levels, unsigned axis,
                double coordinate, unsigned quality, double lod, bool repeat,
                bool srgb) {
  RGB result;
  const bool minified = quality >= 2 && lod > 0;
  const double level = minified ? std::min(lod, double(levels.size() - 1)) : 0;
  const unsigned a = unsigned(std::floor(level));
  const unsigned b = std::min(a + 1, unsigned(levels.size() - 1));
  const double fraction = level - a;
  const bool linear = minified ? quality == 3 : quality != 0;
  for (unsigned c = 0; c < 3; ++c)
    result[c] = (1 - fraction) * axisSample(levels[a], axis, coordinate, c,
                                            linear, repeat, srgb) +
                fraction * axisSample(levels[b], axis, coordinate, c, linear,
                                      repeat, srgb);
  return result;
}

bool sampling2D(Scene &scene, Harness &h, const Frame &initial) {
  const std::vector<uint8_t> pixels = {240, 32, 60,  255, 40,  220, 72, 255,
                                       60,  80, 236, 255, 232, 204, 44, 255};
  auto levels = volume_oracle::chain(2, 2, 1, pixels);
  // 2D sRGB mips average decoded light, then quantize the encoded result.
  // This oracle uses only the four authored texels, independently of Core.
  if (scene.srgb)
    for (unsigned c = 0; c < 3; ++c) {
      double average = 0;
      for (unsigned texel = 0; texel < 4; ++texel)
        average += bump_profile::decode(pixels[texel * 4 + c] / 255.) / 4;
      const double encoded = average <= .0031308
                                 ? 12.92 * average
                                 : 1.055 * std::pow(average, 1 / 2.4) - .055;
      levels[1].bytes[c] = uint8_t(std::floor(encoded * 255 + .5));
    }
  scene.extra2D[7]->image.setValue(SbVec2s(2, 2), 4, pixels.data());
  auto sample = [&](const volume_oracle::Level &image, double s, double t,
                    bool linear) {
    const double px = s * image.w - (linear ? .5 : 0),
                 py = t * image.h - (linear ? .5 : 0);
    const int ix = int(std::floor(px)), iy = int(std::floor(py));
    const double wx = linear ? px - ix : 0, wy = linear ? py - iy : 0;
    RGB rgb{{0, 0, 0}};
    for (unsigned by = 0; by < 2; ++by)
      for (unsigned bx = 0; bx < 2; ++bx) {
        const unsigned x =
            unsigned(std::max(0, std::min(int(image.w) - 1, ix + int(bx))));
        const unsigned y =
            unsigned(std::max(0, std::min(int(image.h) - 1, iy + int(by))));
        const double weight = (bx ? wx : 1 - wx) * (by ? wy : 1 - wy);
        for (unsigned c = 0; c < 3; ++c) {
          double value = image.bytes[(size_t(y) * image.w + x) * 4 + c] / 255.;
          if (scene.srgb)
            value = bump_profile::decode(value);
          rgb[c] += value * weight;
        }
      }
    return rgb;
  };
  bool ok = true;
  Frame frame, restored;
  for (unsigned q = 0; q < 4; ++q)
    for (double lod : {-.5, .5, 1.5}) {
      scene.quality->textureQuality =
          std::array<float, 4>{{.1f, .3f, .6f, .8f}}[q];
      // SoTexture2 retains upload quality until an image revision changes.
      // Explicitly reauthor the same bytes after selecting each quality.
      scene.extra2D[7]->image.setValue(SbVec2s(2, 2), 4, pixels.data());
      scene.extra2D[7]->image.touch();
      const double gradient = std::pow(2., lod) / 2 * side / 4;
      for (unsigned i = 0; i < 4; ++i) {
        const auto p = scene.positions->point[i];
        scene.coords[7]->point.set1Value(
            i, SbVec3f(float(.37 + p[0] * gradient),
                       float(.425 + p[1] * gradient), .37f));
      }
      scene.sampled[7] = [=, &scene, &sample](unsigned x, unsigned y) {
        const double s = .37 + ((x + .5) * 4 / side - 2) * gradient;
        const double t = .425 + (2 - (y + .5) * 4 / side) * gradient;
        if (q < 2 || lod <= 0)
          return sample(levels[0], s, t, q != 0);
        const double mip = std::min(1., lod);
        const RGB a = sample(levels[0], s, t, q == 3),
                  b = sample(levels[1], s, t, q == 3);
        RGB rgb;
        for (unsigned c = 0; c < 3; ++c)
          rgb[c] = (1 - mip) * a[c] + mip * b[c];
        return rgb;
      };
      const bool rendered =
          h.render(scene.root, frame,
                   "eight 2D texture filter q=" + std::to_string(q) +
                       " lod=" + std::to_string(lod));
      bool witnessed = false;
      for (const auto &draw : h.audit->plan.draws)
        if (draw.sourceNodeId == scene.mesh->getNodeId() &&
            draw.renderStateSlot < h.audit->plan.renderStates.size()) {
          const auto layer = coin_render_texture_unit(
              h.audit->plan.renderStates[draw.renderStateSlot], 7);
          if (!layer.enabled ||
              layer.imageSlot >= h.audit->plan.textures.size() ||
              layer.samplerSlot >= h.audit->plan.samplers.size())
            continue;
          const auto &image = h.audit->plan.textures[layer.imageSlot];
          const auto &sampler = h.audit->plan.samplers[layer.samplerSlot];
          const bool mip = q >= 2;
          const bool bytes = image.pixelsRgba == pixels &&
                             (!mip || image.mipmapsRgba == levels[1].bytes);
          witnessed = image.width == 2 && image.height == 2 &&
                      unsigned(sampler.filter) == q &&
                      sampler.wrapS == CoinRenderTextureWrap::CLAMP &&
                      sampler.wrapT == CoinRenderTextureWrap::CLAMP &&
                      image.mipmapped == mip && bytes;
          std::cout << "sampling2D_delivery q=" << q
                    << " filter=" << unsigned(sampler.filter)
                    << " mipmapped=" << image.mipmapped
                    << " mip_bytes=" << image.mipmapsRgba.size()
                    << " bytes_match=" << bytes << '\n';
        }
      ok &= rendered &&
            check(witnessed, "authored 2D quality, clamp, base "
                             "bytes and independent mip delivered") &&
            oracle(scene, frame);
    }
  scene.sampled[7] = {};
  scene.quality->textureQuality = .1f;
  scene.raw(7, SbVec3f(.5f, .5f, 0));
  scene.paint(true);
  ok &= h.render(scene.root, restored, "2D sampler/image/mip A/B/A") &&
        oracle(scene, restored) && exact(initial, restored);
  return ok;
}

bool srgbSpatialRefusal(Scene &scene, Harness &h, const Frame &initial) {
  bool ok =
      check(scene.srgb && scene.only2D,
            "sRGB spatial negative starts from admitted eight-2D publication");
  Frame restored;
  for (bool cube : {false, true}) {
    auto *stage = new SoGroup;
    auto *select = new SoTextureUnit;
    select->unit = 7;
    stage->addChild(select);
    if (cube) {
      auto *image = new SoTextureCubeMap;
      image->wrapS = image->wrapT = SoTextureCubeMap::CLAMP;
      const uint8_t pixels[] = {210, 180, 90, 255, 210, 180, 90, 255,
                                210, 180, 90, 255, 210, 180, 90, 255};
      SoSFImage *faces[] = {&image->imagePosX, &image->imageNegX,
                            &image->imagePosY, &image->imageNegY,
                            &image->imagePosZ, &image->imageNegZ};
      for (auto *face : faces)
        face->setValue(SbVec2s(2, 2), 4, pixels);
      stage->addChild(image);
      auto *coords = new SoTextureCoordinate3;
      coords->point.setValue(0, 0, 1);
      stage->addChild(coords);
    } else {
      auto *image = new SoTexture3;
      image->wrapS = image->wrapT = image->wrapR = SoTexture3::CLAMP;
      std::vector<uint8_t> pixels(3 * 5 * 7 * 4, 255);
      image->images.setValue(SbVec3s(3, 5, 7), 4, pixels.data());
      stage->addChild(image);
    }
    auto *unitZero = new SoTextureUnit;
    unitZero->unit = 0;
    stage->addChild(unitZero);
    scene.group->insertChild(stage, scene.group->findChild(scene.mesh));
    ok &= h.reject(scene.root, initial,
                   cube ? "initial sRGB cube domain remains refused"
                        : "initial sRGB volume domain remains refused");
    scene.group->removeChild(stage);
    ok &= h.render(scene.root, restored,
                   "sRGB spatial-domain recovery to eight 2D") &&
          oracle(scene, restored) && exact(initial, restored);
  }
  return ok;
}

bool sampling(Scene &scene, Harness &h, const Frame &initial) {
  std::vector<uint8_t> source(3 * 5 * 7 * 4);
  for (unsigned z = 0; z < 7; ++z)
    for (unsigned i = 0; i < 15; ++i) {
      const auto rgb = Scene::bytes(7, z);
      for (unsigned c = 0; c < 3; ++c)
        source[(z * 15 + i) * 4 + c] = uint8_t(rgb[c]);
      source[(z * 15 + i) * 4 + 3] = 255;
    }
  const auto levels = volume_oracle::chain(3, 5, 7, source);
  Frame frame, back;
  bool ok = true;
  for (unsigned quality = 0; quality < 4; ++quality)
    for (double lod : {-.5, 1., 1.5}) {
      scene.quality->textureQuality =
          std::array<float, 4>{{.1f, .3f, .6f, .8f}}[quality];
      const double gradient = std::pow(2., lod) / 3 * side / 4;
      for (unsigned i = 0; i < 4; ++i) {
        const double x = scene.positions->point[i][0];
        scene.coords[7]->point.set1Value(
            i, SbVec3f(float(.37 + x * gradient), .425f, .37f));
      }
      // Capture a pointer rather than copying noncopyable public scene state.
      scene.sampled[7] = [=, &scene](unsigned, unsigned) {
        return sampleChain(levels, 2, .37, quality, lod, false, scene.srgb);
      };
      ok &= h.render(
                scene.root, frame,
                "NPOT odd-edge volume filter/mip q=" + std::to_string(quality) +
                    " lod=" + std::to_string(lod)) &&
            oracle(scene, frame);
    }
  scene.quality->textureQuality = .3f;
  for (bool repeat : {true, false, true}) {
    scene.volume[7]->wrapR = repeat ? SoTexture3::REPEAT : SoTexture3::CLAMP;
    scene.raw(7, SbVec3f(-.17f, 1.21f, 1.37f));
    scene.sampled[7] = [=, &scene](unsigned, unsigned) {
      return sampleChain(levels, 2, 1.37, 1, 0, repeat, scene.srgb);
    };
    ok &= h.render(scene.root, frame,
                   repeat ? "spatial REPEAT outside normalized STR"
                          : "spatial CLAMP outside normalized STR") &&
          oracle(scene, frame);
  }
  scene.volume[7]->wrapR = SoTexture3::CLAMP;
  scene.sampled[7] = {};
  scene.raw(7, SbVec3f(.5f / 3, .5f / 5, 6.5f / 7));

  std::vector<uint8_t> stripe(256 * 4);
  for (unsigned x = 0; x < 256; ++x) {
    const unsigned char rgb[] = {uint8_t((x % 8 < 4) ? 230 : 40),
                                 uint8_t((x % 8 < 4) ? 60 : 220),
                                 uint8_t((x % 8 < 4) ? 200 : 80)};
    for (unsigned c = 0; c < 3; ++c)
      stripe[x * 4 + c] = rgb[c];
    stripe[x * 4 + 3] = 255;
  }
  const auto cubeLevels = volume_oracle::chain(256, 1, 1, stripe);
  SoSFImage *faces[] = {&scene.cube[4]->imagePosX, &scene.cube[4]->imageNegX,
                        &scene.cube[4]->imagePosY, &scene.cube[4]->imageNegY,
                        &scene.cube[4]->imagePosZ, &scene.cube[4]->imageNegZ};
  std::vector<uint8_t> face(256 * 256 * 4);
  for (unsigned y = 0; y < 256; ++y)
    std::copy(stripe.begin(), stripe.end(), face.begin() + y * 256 * 4);
  for (auto *image : faces)
    image->setValue(SbVec2s(256, 256), 4, face.data());
  for (unsigned quality = 0; quality < 4; ++quality)
    for (double lod : {-.5, 1.5}) {
      scene.quality->textureQuality =
          std::array<float, 4>{{.1f, .3f, .6f, .8f}}[quality];
      const double gradient = 2 * std::pow(2., lod) / 256 * side / 4;
      for (unsigned i = 0; i < 4; ++i)
        scene.coords[4]->point.set1Value(
            i, SbVec3f(float(gradient * scene.positions->point[i][0]), 0, 1));
      scene.sampled[4] = [=, &scene](unsigned x, unsigned) {
        const double worldX = (x + .5) * 4 / side - 2;
        return sampleChain(cubeLevels, 0, .5 + .5 * gradient * worldX, quality,
                           lod, false, scene.srgb);
      };
      ok &= h.render(scene.root, frame,
                     "cube +Z stripes filter/mip q=" + std::to_string(quality) +
                         " lod=" + std::to_string(lod)) &&
            oracle(scene, frame);
    }
  scene.sampled[4] = {};
  scene.raw(4, SbVec3f(0, 0, 1));
  scene.quality->textureQuality = .1f;
  scene.paint(true);
  ok &= h.render(scene.root, back, "sampler/image/LOD A/B/A") &&
        oracle(scene, back) && exact(initial, back);
  return ok;
}

bool combines(Scene &scene, Harness &h, const Frame &initial) {
  auto *authoring = new SoGroup;
  auto *select = new SoTextureUnit;
  select->unit = 7;
  authoring->addChild(select);
  auto *node = new SoTextureCombine;
  authoring->addChild(node);
  auto *restore = new SoTextureUnit;
  restore->unit = 0;
  authoring->addChild(restore);
  scene.group->insertChild(authoring, scene.group->findChild(scene.mesh));
  node->alphaOperation = SoTextureCombine::REPLACE;
  node->alphaSource.set1Value(0, SoTextureCombine::PREVIOUS);
  bool ok = true;
  Frame frame, back;
  for (bool primary : {false, true})
    for (int operation :
         {SoTextureCombine::REPLACE, SoTextureCombine::MODULATE,
          SoTextureCombine::ADD, SoTextureCombine::ADD_SIGNED,
          SoTextureCombine::SUBTRACT, SoTextureCombine::INTERPOLATE,
          SoTextureCombine::DOT3_RGB, SoTextureCombine::DOT3_RGBA}) {
      scene.combinePrimary = primary;
      scene.combineOp = operation;
      scene.combineConstant = operation == SoTextureCombine::DOT3_RGBA
                                  ? RGB{{1, 1, 1}}
                                  : RGB{{.25, .5, .75}};
      const bool dot = operation == SoTextureCombine::DOT3_RGB ||
                       operation == SoTextureCombine::DOT3_RGBA;
      const int sources[] = {dot       ? SoTextureCombine::TEXTURE
                             : primary ? SoTextureCombine::PRIMARY_COLOR
                                       : SoTextureCombine::PREVIOUS,
                             dot ? SoTextureCombine::CONSTANT
                                 : SoTextureCombine::TEXTURE,
                             SoTextureCombine::CONSTANT};
      node->rgbSource.setValues(0, 3, sources);
      for (unsigned i = 0; i < 3; ++i)
        node->rgbOperand.set1Value(i, SoTextureCombine::SRC_COLOR);
      node->rgbOperation = operation;
      node->constantColor.setValue(float(scene.combineConstant[0]),
                                   float(scene.combineConstant[1]),
                                   float(scene.combineConstant[2]), 1);
      ok &= h.render(scene.root, frame,
                     "opaque combined operation=" + std::to_string(operation) +
                         " primary=" + std::to_string(primary)) &&
            oracle(scene, frame);
    }
  scene.combineOp = -1;
  scene.group->removeChild(authoring);
  ok &= h.render(scene.root, back, "combine scope A/B/A") &&
        oracle(scene, back) && exact(initial, back);
  return ok;
}

bool models(Scene &scene, Harness &h, const Frame &initial) {
  bool ok = true;
  Frame frame, back;
  // A proportional cover, not a Cartesian campaign: L and RGBA volume,
  // opaque LA cube, and RGB 2D each traverse all four native models.
  for (unsigned components = 1; components <= 4; ++components) {
    const unsigned unit = components == 2 ? 4 : components == 3 ? 3 : 7;
    const unsigned layer = scene.only2D ? 0 : unit == 4 ? 4 : unit == 7 ? 6 : 0;
    auto encoded = Scene::bytes(unit, layer);
    if (components <= 2)
      encoded[1] = encoded[2] = encoded[0];
    RGB sampled = encoded;
    for (auto &value : sampled) {
      value /= 255;
      if (scene.srgb)
        value = bump_profile::decode(value);
    }
    scene.customTexel[unit] = sampled;
    auto image = [&](unsigned pixels, unsigned sourceLayer) {
      const auto rgb = Scene::bytes(unit, sourceLayer);
      std::vector<unsigned char> bytes(size_t(pixels) * components);
      for (unsigned p = 0; p < pixels; ++p)
        for (unsigned c = 0; c < components; ++c)
          bytes[p * components + c] =
              c == components - 1 && (components == 2 || components == 4)
                  ? 255
                  : uint8_t(rgb[components <= 2 ? 0 : c]);
      return bytes;
    };
    if (scene.volume[unit]) {
      std::vector<unsigned char> bytes;
      for (unsigned z = 0; z < 7; ++z) {
        const auto plane = image(15, z);
        bytes.insert(bytes.end(), plane.begin(), plane.end());
      }
      scene.volume[unit]->images.setValue(SbVec3s(3, 5, 7), components,
                                          bytes.data());
    } else if (scene.cube[unit]) {
      SoSFImage *faces[] = {
          &scene.cube[unit]->imagePosX, &scene.cube[unit]->imageNegX,
          &scene.cube[unit]->imagePosY, &scene.cube[unit]->imageNegY,
          &scene.cube[unit]->imagePosZ, &scene.cube[unit]->imageNegZ};
      for (unsigned f = 0; f < 6; ++f) {
        const auto bytes = image(4, f);
        faces[f]->setValue(SbVec2s(2, 2), components, bytes.data());
      }
    } else {
      const auto bytes = image(1, 0);
      scene.extra2D[unit]->image.setValue(SbVec2s(1, 1), components,
                                          bytes.data());
    }
    for (int model : {SoTexture2::MODULATE, SoTexture2::DECAL,
                      SoTexture2::BLEND, SoTexture2::REPLACE}) {
      scene.models[unit] = model;
      if (scene.volume[unit]) {
        scene.volume[unit]->model = model;
        scene.volume[unit]->blendColor.setValue(.2f, .4f, .6f);
      } else if (scene.cube[unit]) {
        scene.cube[unit]->model = model;
        scene.cube[unit]->blendColor.setValue(.2f, .4f, .6f);
      } else {
        scene.extra2D[unit]->model = model;
        scene.extra2D[unit]->blendColor.setValue(.2f, .4f, .6f);
      }
      const std::string phase =
          "native opaque model unit=" + std::to_string(unit) +
          " channels=" + std::to_string(components) +
          " model=" + std::to_string(model);
      if (model == SoTexture2::DECAL && components < 3) {
        // DECAL is undefined for L/LA and was explicitly refused by C17.2.
        // Keep the negative submission, old publication and full recovery.
        ok &= h.reject(scene.root, frame, phase + " undefined L/LA DECAL");
        scene.models[unit] = SoTexture2::MODULATE;
        if (scene.volume[unit])
          scene.volume[unit]->model = SoTexture3::MODULATE;
        else if (scene.cube[unit])
          scene.cube[unit]->model = SoTextureCubeMap::MODULATE;
        else
          scene.extra2D[unit]->model = SoTexture2::MODULATE;
        ok &= h.render(scene.root, back, "L/LA model-domain recovery") &&
              oracle(scene, back) && exact(frame, back);
      } else {
        ok &= h.render(scene.root, frame, phase) && oracle(scene, frame);
      }
    }
    scene.models[unit] = SoTexture2::MODULATE;
    scene.customTexel[unit] = {{-1, -1, -1}};
    scene.paint(true);
  }
  ok &= h.render(scene.root, back, "native image-component/model A/B/A") &&
        oracle(scene, back) && exact(initial, back);
  return ok;
}

bool height(Scene &scene, Harness &h, const Frame &initial) {
  const auto normal = scene.normal;
  scene.normal = {{128, 128, 255}};
  Frame first, changed, back;
  const unsigned char flat[] = {20, 20, 20, 20};
  scene.bump->image.setValue(SbVec2s(2, 2), 1, flat);
  bool ok =
      h.render(scene.root, first,
               "flat heightmap conversion with real VSM and eight textures") &&
      oracle(scene, first);
  const unsigned char other[] = {230, 230, 230, 230};
  scene.bump->image.setValue(SbVec2s(2, 2), 1, other);
  ok &=
      h.render(scene.root, changed,
               "height offset does not displace caster or alter zero slope") &&
      oracle(scene, changed) && exact(first, changed);
  std::vector<uint8_t> heights = {0, 31, 63, 95, 127, 159, 191, 223, 255};
  auto sobel = [](const std::vector<uint8_t> &input) {
    std::vector<uint8_t> encoded;
    const int weight[] = {1, 2, 1};
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 3; ++x) {
        double dx = 0, dy = 0;
        for (int k = -1; k <= 1; ++k) {
          dx += weight[k + 1] *
                (int(input[((y + k + 3) % 3) * 3 + (x + 1) % 3]) -
                 int(input[((y + k + 3) % 3) * 3 + (x + 2) % 3])) /
                255.;
          dy += weight[k + 1] *
                (int(input[((y + 2) % 3) * 3 + (x + k + 3) % 3]) -
                 int(input[((y + 1) % 3) * 3 + (x + k + 3) % 3])) /
                255.;
        }
        const auto n = bump_profile::normalized({{-dx, -dy, 1}});
        for (double component : n)
          encoded.push_back(uint8_t(std::min(255., (component + 1) * 128)));
      }
    return encoded;
  };
  scene.matrix->scaleFactor.setValue(.0001f, .0001f);
  scene.matrix->translation.setValue(.49995f, .49995f);
  Frame heightImage;
  for (unsigned mutation = 0; mutation < 2; ++mutation) {
    if (mutation)
      heights[1] = 240;
    const auto encoded = sobel(heights);
    scene.normalAt = [=, &scene](unsigned x, unsigned y) {
      const auto local =
          scene.localAt((x + .5) * 4 / side - 2, 2 - (y + .5) * 4 / side);
      const double s = ((local[0] + 1.4) / 2.7) * .0001 + .49995;
      const double t = ((local[1] + 1.3) / 2.65) * .0001 + .49995;
      const double fx = s * 3 - .5, fy = t * 3 - .5;
      const int ix = int(std::floor(fx)), iy = int(std::floor(fy));
      const double wx = fx - ix, wy = fy - iy;
      RGB value{{0, 0, 0}};
      for (unsigned by = 0; by < 2; ++by)
        for (unsigned bx = 0; bx < 2; ++bx) {
          const int sx = (ix + int(bx) + 3) % 3, sy = (iy + int(by) + 3) % 3;
          const double weight = (bx ? wx : 1 - wx) * (by ? wy : 1 - wy);
          for (unsigned c = 0; c < 3; ++c)
            value[c] += weight * encoded[(sy * 3 + sx) * 3 + c];
        }
      return value;
    };
    scene.bump->image.setValue(SbVec2s(3, 3), 1, heights.data());
    ok &= h.render(scene.root, heightImage,
                   "periodic Sobel height + VSM + eight textures") &&
          oracle(scene, heightImage);
    ok &= check(h.audit->plan.bumpImages.size() == 1 &&
                    h.audit->plan.bumpImages[0].convertedHeight &&
                    h.audit->plan.bumpImages[0].rgb == encoded,
                "captured Sobel RGB matches independent periodic "
                "convolution/normalization/byte truncation");
    scene.bump->image.setValue(SbVec2s(3, 3), 3, encoded.data());
    ok &= h.render(scene.root, changed,
                   "explicit RGB equivalent of converted height") &&
          oracle(scene, changed) && exact(heightImage, changed);
    scene.bump->image.setValue(SbVec2s(3, 3), 1, heights.data());
    ok &= h.render(scene.root, changed, "height/RGB/height identity") &&
          exact(heightImage, changed);
  }
  scene.normalAt = {};
  scene.matrix->scaleFactor.setValue(1, 1);
  scene.matrix->translation.setValue(0, 0);
  scene.setNormal(normal);
  ok &= h.render(scene.root, back, "height to RGB normal A/B/A") &&
        oracle(scene, back) && exact(initial, back);
  return ok;
}

bool cameraTransform(Scene &scene, Harness &h, const Frame &initial) {
  auto state = [&](SbMatrix &model, SbMatrix &view) {
    const auto &frame = h.audit->plan;
    for (const auto &draw : frame.draws)
      if (draw.sourceNodeId == scene.mesh->getNodeId() &&
          draw.renderStateSlot < frame.renderStates.size()) {
        model = frame.renderStates[draw.renderStateSlot].model;
        view = frame.renderStates[draw.renderStateSlot].view;
        return true;
      }
    return false;
  };
  SbMatrix originalModel, originalView;
  if (!check(state(originalModel, originalView),
             "original actual receiver camera/model captured"))
    return false;
  auto *translation = new SoTranslation;
  auto *rotation = new SoRotation;
  translation->translation.setValue(.3f, -.2f, .4f);
  rotation->rotation.setValue(SbVec3f(0, 0, 1), .2f);
  // Common rigid transform precedes both camera and entire scene, so local
  // camera/receiver/light vectors are invariant. No GPU result defines this.
  scene.root->insertChild(translation, 0);
  scene.root->insertChild(rotation, 1);
  Frame transformed, restored;
  bool ok = h.render(scene.root, transformed,
                     "camera and source under common model transform") &&
            oracle(scene, transformed);
  SbMatrix model, view;
  ok &= check(state(model, view) && model != originalModel &&
                  view != originalView,
              "authored camera/model transforms are actually delivered");
  const SbMatrix before = originalModel * originalView, after = model * view;
  double maximum = 0;
  for (unsigned r = 0; r < 4; ++r)
    for (unsigned c = 0; c < 4; ++c)
      maximum = std::max(maximum, double(std::abs(before[r][c] - after[r][c])));
  ok &= check(maximum < 2e-5, "camera/common-model inverse cancels by "
                              "independent rigid-transform algebra");
  scene.root->removeChild(rotation);
  scene.root->removeChild(translation);
  ok &= h.render(scene.root, restored, "camera transform A/B/A") &&
        oracle(scene, restored) && exact(initial, restored);
  return ok;
}

bool tickets(Scene &scene, Harness &h, const Frame &initial) {
  bump_profile::Scene ordinary;
  ordinary.bump->image.setValue(SbVec2s(0, 0), 0, nullptr);
  Frame expected;
  RGB rgb;
  for (unsigned c = 0; c < 3; ++c)
    rgb[c] = 255 * ordinary.diffuse[c] * .8;
  bool ok = h.render(ordinary.root, expected, "ordinary prior ticket source") &&
            bump_profile::color(expected, rgb,
                                "ordinary geometric directional diffuse");
  if (!ok)
    return false;
  CoinRenderReadbackTicket ticket;
  h.action.applyAsync(ordinary.root, ticket);
  ok &= check(h.action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  ticket.token != 0,
              "ordinary pre-existing asynchronous ticket is real and live");
  if (!ok)
    return false;
  Frame combined, restored;
  ok &= h.render(
            scene.root, combined,
            "combined publication while ordinary ticket remains unconsumed") &&
        oracle(scene, combined) && exact(initial, combined);
  scene.material->transparency = .5f;
  ok &= h.reject(scene.root, combined,
                 "unsupported combined alpha preserves prior live ticket");
  scene.material->transparency = 0;
  std::vector<uint8_t> read{7, 9, 11};
  std::vector<float> depth{.125f};
  SbString diagnostic;
  auto status = CoinRenderTarget::READBACK_NOT_READY;
  const auto begin = std::chrono::steady_clock::now();
  const auto deadline = begin + std::chrono::seconds(5);
  unsigned attempts = 0;
  do {
    ++attempts;
    status = CoinRenderTarget::pollReadback(ticket, read, depth, &diagnostic);
    if (status != CoinRenderTarget::READBACK_NOT_READY)
      break;
  } while (std::chrono::steady_clock::now() < deadline);
  std::cout << "combined_old_ticket status=" << status
            << " attempts=" << attempts << " elapsed_ms="
            << std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - begin)
                   .count()
            << " diagnostic=" << diagnostic.getString() << '\n';
  ok &= check(status == CoinRenderTarget::READBACK_READY &&
                  read == expected.rgba && depth == expected.depth,
              "failed combined preflight never invalidates or corrupts "
              "independently owned ordinary ticket pixels/depth");
  const auto savedRGBA = read;
  const auto savedDepth = depth;
  ok &=
      check(CoinRenderTarget::pollReadback(ticket, read, depth, &diagnostic) ==
                    CoinRenderTarget::READBACK_INVALID_TICKET &&
                read == savedRGBA && depth == savedDepth,
            "successful poll consumes only that ticket atomically");
  ok &= h.render(scene.root, restored,
                 "combined recovery after old ticket completion") &&
        oracle(scene, restored) && exact(initial, restored);
  return ok;
}

bool rttNoneAlpha(Harness &h, Scene &producer, Scene &consumer,
                  CoinRenderSceneTextureMode route, bool srgb) {
  producer.srgb = consumer.srgb = srgb;
  auto *texture = new SoSceneTexture2;
  texture->scene = producer.root;
  texture->size.setValue(side, side);
  texture->type = SoSceneTexture2::RGBA8;
  texture->transparencyFunction = SoSceneTexture2::NONE;
  texture->backgroundColor.setValue(.2f, .4f, .7f, .25f);
  texture->model = SoSceneTexture2::MODULATE;
  const int index = consumer.extras->findChild(consumer.extra2D[3]);
  consumer.extras->removeChild(index);
  consumer.extra2D[3] = nullptr;
  consumer.extras->insertChild(texture, index);
  // Both coordinates are outside the producer's authored rectangle. The
  // sampled clear alpha is .25, not an inferred opaque interior value.
  consumer.raw(3, SbVec3f(.03f, .03f, 0));
  auto *authoring = new SoGroup;
  auto *select = new SoTextureUnit;
  select->unit = 3;
  authoring->addChild(select);
  auto *combine = new SoTextureCombine;
  authoring->addChild(combine);
  combine->rgbOperation = SoTextureCombine::MODULATE;
  const int args[] = {SoTextureCombine::PREVIOUS, SoTextureCombine::TEXTURE};
  combine->rgbSource.setValues(0, 2, args);
  combine->rgbOperand.set1Value(0, SoTextureCombine::SRC_COLOR);
  combine->alphaOperation = SoTextureCombine::REPLACE;
  combine->alphaSource.set1Value(0, SoTextureCombine::PREVIOUS);
  auto *zero = new SoTextureUnit;
  zero->unit = 0;
  authoring->addChild(zero);
  consumer.group->insertChild(authoring,
                              consumer.group->findChild(consumer.mesh));
  auto metadata = [&](double alpha) {
    const auto &frame = h.audit->plan;
    uint32_t slot = UINT32_MAX;
    for (const auto &draw : frame.draws)
      if (draw.sourceNodeId == consumer.mesh->getNodeId() &&
          draw.renderStateSlot < frame.renderStates.size()) {
        const auto layer = coin_render_texture_unit(
            frame.renderStates[draw.renderStateSlot], 3);
        if (layer.enabled)
          slot = layer.imageSlot;
      }
    if (!check(slot < frame.textures.size(),
               "NONE RTT is bound to the actual unit-three receiver"))
      return false;
    const auto &image = frame.textures[slot];
    bool result =
        check(image.sceneTransparencyFunction == SoSceneTexture2::NONE &&
                  image.width == side && image.height == side,
              "authorial NONE declaration survives RTT resolution with "
              "physical extent");
    if (route == COIN_RENDER_SCENE_TEXTURE_DIRECT)
      return check(image.gpuToken && image.pixelsRgba.empty(),
                   "DIRECT declared NONE keeps actual GPU resource with zero "
                   "CPU pixel copies") &&
             result;
    if (!check(image.pixelsRgba.size() == side * side * 4,
               "STAGED keeps complete unmodified raw RGBA"))
      return false;
    unsigned clear = 0, geometry = 0, bad = 0;
    const int expectedAlpha = int(std::lround(alpha * 255));
    for (unsigned y = 0; y < side; ++y)
      for (unsigned x = 0; x < side; ++x) {
        const size_t p = (size_t(y) * side + x) * 4;
        const double wx = (x + .5) * 4 / side - 2, wy = 2 - (y + .5) * 4 / side;
        if (std::abs(wx) > 1.6 || std::abs(wy) > 1.6) {
          ++clear;
          const int rgb[] = {51, 102, 179};
          for (unsigned c = 0; c < 3; ++c)
            bad += std::abs(int(image.pixelsRgba[p + c]) - rgb[c]) > 1;
          bad += std::abs(int(image.pixelsRgba[p + 3]) - expectedAlpha) > 1;
        } else if (std::abs(wx) < .9 && std::abs(wy) < .9) {
          ++geometry;
          bad += image.pixelsRgba[p + 3] != 255;
        }
      }
    CoinWgpuFfiFrame ffi;
    std::string diagnostic;
    result &= check(ffi.prepareBumpFrame(frame, side, side, diagnostic),
                    "NONE STAGED private transport packs " + diagnostic);
    if (!result)
      return false;
    const auto &view = ffi.getView();
    if (!check(slot < view.texture_count,
               "NONE STAGED packed texture slot valid"))
      return false;
    const auto &packed = view.textures[slot];
    result &= check((packed.reserved & 0x80000000u) && packed.pixels &&
                        packed.pixel_bytes_len == image.pixelsRgba.size() &&
                        std::equal(image.pixelsRgba.begin(),
                                   image.pixelsRgba.end(), packed.pixels) &&
                        (!image.contentDigest ||
                         packed.content_digest == image.contentDigest),
                    "ABI63 NONE bit31 preserves raw alpha, every payload byte "
                    "and known digest");
    std::cout << "NONE_staged_payload clear=" << clear
              << " geometry=" << geometry << " expected_alpha=" << expectedAlpha
              << " bad=" << bad << " reserved=" << packed.reserved
              << " gpu_opaque=" << image.gpuOpaque << '\n';
    return check(clear > 1000 && geometry > 500 && bad == 0,
                 "NONE is effective opacity declaration, not destructive "
                 "alpha255 conversion") &&
           result;
  };
  bool ok = true;
  Frame first, changed, restored;
  for (bool inverse : {false, true}) {
    combine->rgbOperand.set1Value(1, inverse
                                         ? SoTextureCombine::ONE_MINUS_SRC_ALPHA
                                         : SoTextureCombine::SRC_ALPHA);
    auto draw = [&](double alpha, Frame &frame, const char *phase) {
      texture->backgroundColor.setValue(.2f, .4f, .7f, float(alpha));
      const double factor = inverse ? 1 - alpha : alpha;
      consumer.customTexel[3] = {{factor, factor, factor}};
      return h.render(consumer.root, frame, phase) && oracle(consumer, frame) &&
             metadata(alpha);
    };
    ok &= draw(.25, first,
               inverse ? "NONE RTT ONE_MINUS_SRC_ALPHA clear .25"
                       : "NONE RTT SRC_ALPHA clear .25");
    ok &= draw(.75, changed, "NONE RTT source alpha changes while RGB fixed") &&
          check(difference(first, changed) > 500,
                "raw source alpha observably drives RGB through combine");
    ok &=
        draw(.25, restored, "NONE clear alpha A/B/A") && exact(first, restored);
    for (int policy :
         {SoSceneTexture2::ALPHA_BLEND, SoSceneTexture2::ALPHA_TEST}) {
      texture->transparencyFunction = policy;
      ok &= h.reject(
          consumer.root, restored,
          "RTT BLEND/TEST remains outside opaque bump receiver profile");
      texture->transparencyFunction = SoSceneTexture2::NONE;
      ok &= draw(.25, restored, "NONE after unsupported RTT alpha policy") &&
            exact(first, restored);
    }
  }
  return ok;
}

bool delivery(Scene &scene, Harness &h) {
  // Core and private transport are delivery witnesses, never the RGB oracle.
  const auto frame = h.audit->plan;
  std::string error;
  std::vector<CoinRenderBumpGpuVertex> stream;
  bool ok = check(frame.bumpImages.size() == 1 && !frame.shadowLights.empty() &&
                      !frame.vertices.empty(),
                  "same capture contains real bump, shadow and geometry");
  ok &= check(coin_render_build_bump_gpu_stream(frame, stream, error) &&
                  stream.size() == frame.vertices.size(),
              "complete per-corner bump stream " + error);
  if (!ok)
    return false;
  unsigned witnesses = 0;
  for (size_t i = 0; i < stream.size(); ++i) {
    const auto &b = stream[i];
    if (!b.imageSlot)
      continue;
    const auto &v = frame.vertices[i];
    ok &=
        check(b.imageSlot == 1 && std::abs(b.tangentS[0] - 1) < 1e-5 &&
                  std::abs(b.tangentT[1] + 1) < 1e-5,
              "authored normal image and signed tangent axes survive capture");
    ok &= check(
        std::abs(v.textureR[7] - scene.coords[7]->point[0][2]) < 1e-5 &&
            std::abs(v.textureQ[7] - 1) < 1e-5,
        "last active authored texture has complete STRQ on every bump corner");
    ++witnesses;
  }
  ok &= check(witnesses >= 6,
              "both authored triangles have bump stream witnesses");
  CoinWgpuFfiFrame ffi;
  ok &= check(ffi.prepareBumpFrame(frame, side, side, error),
              "combined private packet accepts captured public input " + error);
  if (!ok)
    return false;
  const auto &view = ffi.getView();
  ok &=
      check(view.abi_version == 63 && view.bump_vertex_count == stream.size() &&
                view.bump_image_count == 1 && view.texture_count >= 8 &&
                view.shadow_receiver_count > 0,
            "ABI63 simultaneously owns all texture, bump and shadow packets");
  for (size_t i = 0; i < stream.size(); ++i)
    ok &= check(view.bump_vertices[i].image_slot == stream[i].imageSlot &&
                    !std::memcmp(view.bump_vertices[i].coord, stream[i].coord,
                                 sizeof(stream[i].coord)),
                "private transport preserves each bump coordinate and slot");
  return ok;
}
} // namespace

int main(int argc, char **argv) {
  SoDB::init();
  CoinRenderAction::initClass();
  configureSamplingTest(argc, argv);
  Settings settings;
  bool selected = false;
  const std::set<std::string> modes = {"cold-bump",
                                       "cold-ordinary",
                                       "cold-mixed",
                                       "smoke",
                                       "baseline",
                                       "lighting",
                                       "projection",
                                       "invalid",
                                       "delivery",
                                       "fractional",
                                       "coverage",
                                       "mixed",
                                       "rtt",
                                       "sampling",
                                       "combine",
                                       "height",
                                       "models",
                                       "raw",
                                       "coords",
                                       "mixed-lighting",
                                       "bindings",
                                       "profiles",
                                       "camera",
                                       "tickets",
                                       "srgb-spatial-refusal",
                                       "rtt-none-alpha"};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--samples") {
      if (++i == argc)
        return 2;
      const std::string value = argv[i];
      if (value != "1" && value != "2" && value != "4")
        return 2;
      settings.samples = unsigned(std::atoi(value.c_str()));
    } else if (arg == "--srgb")
      settings.srgb = true;
    else if (arg == "--2d-only")
      settings.only2D = true;
    else if (arg == "--direct")
      settings.route = COIN_RENDER_SCENE_TEXTURE_DIRECT;
    else if (arg == "--staged")
      settings.route = COIN_RENDER_SCENE_TEXTURE_STAGED;
    else if (arg == "--portable-sampling") {
    } else if (arg.size() > 2 && modes.count(arg.substr(2))) {
      if (selected)
        return 2;
      selected = true;
      settings.mode = arg.substr(2);
    } else {
      std::cerr << "unknown or incomplete option: " << arg << '\n';
      return 2;
    }
  }
  if (settings.mode == "srgb-spatial-refusal")
    settings.srgb = true;
  settings.only2D = settings.only2D || settings.srgb;
  if (settings.mode == "raw" && settings.only2D) {
    std::cerr << "--raw is the linear spatial cache-ingress fixture; "
                 "--srgb/--2d-only require public 2D focals\n";
    return 2;
  }
  if (settings.mode == "tickets" && settings.samples != 1) {
    std::cerr << "--tickets requires the existing ordinary async 1x profile\n";
    return 2;
  }
  CoinRenderCapabilities caps{};
  const int query = coin_render_query_capabilities(
      COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps));
  std::cout << "bump_shadow_environment query=" << query
            << " gpu=" << caps.gpu_available << " backend=" << caps.backend
            << " renderer=" << caps.renderer << " adapter=" << caps.adapter_name
            << " samples=" << settings.samples
            << " diagnostic=" << caps.diagnostic << '\n';
  if (query != 0)
    return 1;
  if (!caps.gpu_available)
    return 77;
  std::cout << "bump_shadow_authored_layout only_2d=" << settings.only2D
            << " stored_srgb=" << settings.srgb << " mode=" << settings.mode
            << '\n';
  Scene scene(255, settings.only2D);
  scene.srgb = settings.srgb;
  Harness harness(caps, settings.samples, settings.srgb, settings.route);
  if (!check(bool(harness.target), "requested GPU target initialized"))
    return 1;
  bool ok =
      check(harness.target->getOptions().sampleCount == settings.samples &&
                harness.target->getOptions().renderer == caps.renderer,
            "actual target retains requested renderer and samples");
  if (settings.mode.compare(0, 5, "cold-") == 0)
    return ok && coldStart(scene, harness, settings.mode) ? 0 : 1;
  Frame ordinary, initial;
  ok &= ordinaryControl(scene, harness, ordinary);
  if (!ok)
    return 1;
  if (settings.mode == "baseline")
    return baseline(scene, harness, ordinary) ? 0 : 1;
  ok &=
      harness.render(scene.root, initial, "combined directional bump shadow") &&
      oracle(scene, initial);
  if (!ok)
    return 1;
  if (settings.mode == "smoke")
    ok &= smoke(scene, harness, initial);
  if (settings.mode == "lighting")
    ok &= lighting(scene, harness, initial);
  if (settings.mode == "projection")
    ok &= scene.only2D ? bindings(scene, harness, initial)
                       : projection(scene, harness, initial);
  if (settings.mode == "invalid")
    ok &= invalid(scene, harness, initial);
  if (settings.mode == "rtt-none-alpha") {
    Scene producer(255, settings.only2D), consumer(255, settings.only2D);
    ok &= rttNoneAlpha(harness, producer, consumer, settings.route,
                       settings.srgb);
  }
  if (settings.mode == "delivery")
    ok &= delivery(scene, harness);
  if (settings.mode == "raw") {
#if defined(COIN_BUMP_SHADOW_TEST_RUST)
    ok &= raw(scene, harness, settings.samples);
#else
    std::cerr << "--raw requires the Rust bridge build\n";
    return 2;
#endif
  }
  if (settings.mode == "srgb-spatial-refusal")
    ok &= srgbSpatialRefusal(scene, harness, initial);
  if (settings.mode == "tickets")
    ok &= tickets(scene, harness, initial);
  if (settings.mode == "camera")
    ok &= cameraTransform(scene, harness, initial);
  if (settings.mode == "bindings")
    ok &= bindings(scene, harness, initial);
  if (settings.mode == "profiles")
    ok &= profileRefusals(scene, harness, initial, caps, settings.samples,
                          settings.srgb);
  if (settings.mode == "coords")
    ok &= coordinates(scene, harness, initial);
  if (settings.mode == "mixed-lighting")
    ok &= mixed(scene, harness, true);
  if (settings.mode == "models")
    ok &= models(scene, harness, initial);
  if (settings.mode == "sampling")
    ok &= scene.only2D ? sampling2D(scene, harness, initial)
                       : sampling(scene, harness, initial);
  if (settings.mode == "combine")
    ok &= combines(scene, harness, initial);
  if (settings.mode == "height")
    ok &= height(scene, harness, initial);
  if (settings.mode == "rtt")
    ok &= rtt(harness, settings.srgb, settings.route, settings.only2D);
  if (settings.mode == "mixed")
    ok &= mixed(scene, harness);
  if (settings.mode == "fractional")
    ok &= fractional(scene, harness);
  if (settings.mode == "coverage")
    ok &= coverage(scene, harness, settings.samples);
  return ok ? 0 : 1;
}
