#include "CoinRenderTestEnvironment.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>
#include "actions/CoinRenderActionP.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

// Independent metamorphic expectation: for coincident lights and identical
// VSM maps, splitting intensity into eight equal parts conserves RGB. This
// checks the portable executor contract, not a native eight-map CoinGL oracle.
int main() {
  if (!std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") &&
      !std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU")) return 77;
  SoDB::init(); CoinRenderAction::initClass();
  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 8); camera->height = 7;
  camera->nearDistance = 1; camera->farDistance = 20;
  root->addChild(camera);
  auto * group = new SoShadowGroup; group->quality = 1;
  root->addChild(group);
  std::vector<SoShadowSpotLight *> lights;
  for (int i = 0; i < 8; ++i) {
    auto * light = new SoShadowSpotLight;
    light->location.setValue(2, 2, 4);
    light->direction.setValue(-2, -2, -5); light->cutOffAngle = .9f;
    light->intensity = .8f; light->on = i == 0;
    group->addChild(light); lights.push_back(light);
  }
  auto * caster = new SoSeparator; group->addChild(caster);
  auto * red = new SoMaterial; red->diffuseColor.setValue(.6f, .1f, .1f);
  caster->addChild(red);
  auto * cube = new SoCube; cube->width = cube->height = cube->depth = 1.4f;
  caster->addChild(cube);
  auto * ground = new SoSeparator; group->addChild(ground);
  auto * style = new SoShadowStyle; style->style = SoShadowStyle::SHADOWED;
  ground->addChild(style);
  auto * white = new SoMaterial; white->diffuseColor.setValue(.7f, .7f, .7f);
  ground->addChild(white);
  auto * move = new SoTranslation; move->translation.setValue(0, 0, -1.5f);
  ground->addChild(move);
  auto * floor = new SoCube; floor->width = floor->height = 6; floor->depth = .05f;
  ground->addChild(floor);
  bool ok = true;
  {
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(128,128)));
    if (!target) { root->unref(); return 1; }
    CoinRenderAction action(SbViewportRegion(128,128)); action.setRenderTarget(target.get());
    auto capture = [&](std::vector<uint8_t> & rgba) {
      action.apply(root);
      if (action.getLastStatus() != CoinRenderAction::SUCCESS) {
        std::cerr << action.getLastError().getString() << '\n'; return false;
      }
      target->readbackRGBA(rgba); return rgba.size() == 128u*128u*4u;
    };
    std::vector<uint8_t> one, eight, seven, clear, recovered, rejected;
    ok = target && capture(one);
    for (auto * light : lights) { light->intensity = .1f; light->on = TRUE; }
    CoinRenderAction plan(SbViewportRegion(128,128)); plan.apply(root);
    std::string diagnostic;
    ok = ok && plan.getPimpl()->lastRejectedShadowPlan.passes.size() == 8 &&
      coin_render_shadow_object_profile(plan.getPimpl()->lastRejectedShadowFrame,
        plan.getPimpl()->lastRejectedShadowPlan, 8, diagnostic) && capture(eight);
    if (ok) {
      uint64_t sum = 0; int maximum = 0;
      for (size_t i = 0; i < eight.size(); ++i) if (i%4 != 3) {
        int delta = std::abs(int(eight[i])-int(one[i]));
        sum += delta; maximum = std::max(maximum, delta);
      }
      double mae = double(sum)/(128*128*3);
      std::cout << "eight-map intensity conservation rgb_mae=" << mae << " max=" << maximum << '\n';
      ok = mae <= .25 && maximum <= 3;
    }
    lights.back()->on = FALSE; ok = ok && capture(seven);
    lights.back()->on = TRUE;
    style->style = SoShadowStyle::NO_SHADOWING; ok = ok && capture(clear);
    style->style = SoShadowStyle::SHADOWED; ok = ok && capture(recovered) && recovered == eight;
    if (ok) {
      size_t eighthEffect = 0, shadowEffect = 0;
      for (size_t i = 0; i < eight.size(); i += 4) {
        int lightDelta = 0, shadowDelta = 0;
        for (int c = 0; c < 3; ++c) {
          lightDelta += int(eight[i+c])-int(seven[i+c]);
          shadowDelta += int(clear[i+c])-int(eight[i+c]);
        }
        eighthEffect += lightDelta > 10; shadowEffect += shadowDelta > 30;
      }
      std::cout << "eighth-light pixels=" << eighthEffect << " shadow pixels=" << shadowEffect << '\n';
      ok = eighthEffect > 100 && shadowEffect > 20;
    }
    const uint64_t serial = target->getLastSubmissionSerial();
    auto * ninth = static_cast<SoShadowSpotLight *>(lights.back()->copy(TRUE));
    group->insertChild(ninth,8); action.apply(root); target->readbackRGBA(rejected);
    std::cout << "ninth-light rejection status=" << action.getLastStatus()
      << " serial_preserved=" << (target->getLastSubmissionSerial() == serial)
      << " pixels_preserved=" << (rejected == eight) << '\n';
    ok = ok && action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      target->getLastSubmissionSerial() == serial && rejected == eight;
    group->removeChild(ninth);
    ok = ok && target->resize(SbVec2i32(128,128)) && capture(recovered) && recovered == eight;
    std::cout << "eight-map recovery_exact=" << (recovered == eight) << '\n';
    action.setRenderTarget(nullptr);
  }
  root->unref();
  if (!ok) std::cerr << "Portable eight-map contract failed\n";
  return ok ? 0 : 1;
}
