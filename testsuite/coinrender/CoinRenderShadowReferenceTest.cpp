#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include "actions/CoinRenderActionP.h"
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/SbColor.h>

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <vector>

namespace {
static const int side = 128;

int luminance(const std::vector<unsigned char> & rgb, int x, int y)
{
  const size_t i = static_cast<size_t>((y * side + x) * 3);
  return static_cast<int>(rgb[i]) + rgb[i + 1] + rgb[i + 2];
}

bool render(SoOffscreenRenderer & gl, SoNode * root,
            std::vector<unsigned char> & rgb)
{
  if (!gl.render(root) || !gl.getBuffer()) return false;
  const unsigned char * data = gl.getBuffer();
  rgb.assign(data, data + side * side * 3);
  return true;
}
}

int main()
{
  SoDB::init();
  CoinRenderAction::initClass();

  SoSeparator * root = new SoSeparator;
  root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 8);
  camera->height = 7;
  camera->nearDistance = 1;
  camera->farDistance = 20;
  root->addChild(camera);

  auto * group = new SoShadowGroup;
  root->addChild(group);
  auto * light = new SoShadowSpotLight;
  light->location.setValue(2, 2, 4);
  light->direction.setValue(-2, -2, -5);
  light->cutOffAngle = 0.9f;
  light->intensity = 1.0f;
  group->addChild(light);

  auto * caster = new SoSeparator;
  group->addChild(caster);
  auto * castStyle = new SoShadowStyle;
  castStyle->style = SoShadowStyle::CASTS_SHADOW_AND_SHADOWED;
  caster->addChild(castStyle);
  auto * red = new SoMaterial;
  red->diffuseColor.setValue(1, 0, 0);
  caster->addChild(red);
  auto * cube = new SoCube;
  cube->width = cube->height = cube->depth = 1.4f;
  caster->addChild(cube);

  auto * ground = new SoSeparator;
  group->addChild(ground);
  auto * groundStyle = new SoShadowStyle;
  groundStyle->style = SoShadowStyle::SHADOWED;
  ground->addChild(groundStyle);
  auto * white = new SoMaterial;
  white->diffuseColor.setValue(1, 1, 1);
  ground->addChild(white);
  auto * move = new SoTranslation;
  move->translation.setValue(0, 0, -1.5f);
  ground->addChild(move);
  auto * floor = new SoCube;
  floor->width = floor->height = 6;
  floor->depth = 0.05f;
  ground->addChild(floor);

  CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(side, side));
  target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  CoinRenderAction action(SbViewportRegion(side, side));
  action.setRenderTarget(target);
  std::vector<unsigned char> published, afterRejection, afterRecovery;
  group->isActive = FALSE;
  action.apply(root);
  bool publishedOk = action.getLastStatus() == CoinRenderAction::SUCCESS;
  target->readbackRGBA(published);
  const uint64_t originalSerial = target->getLastSubmissionSerial();
  group->isActive = TRUE;
  action.apply(root);
  const bool rejected = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                        action.getLastError().find("SoShadowGroup") >= 0;
  const auto & captured = action.getPimpl()->lastRejectedShadowFrame;
  const auto & planned = action.getPimpl()->lastRejectedShadowPlan;
  bool captureOk = captured.shadowGroups.size() == 1 &&
                   captured.shadowLights.size() == 1 &&
                   captured.shadowLights[0].type == CoinRenderLightType::SPOT &&
                   planned.passes.size() == 1 &&
                   !planned.passes[0].casterDraws.empty() &&
                   !planned.passes[0].receiverDraws.empty() &&
                   planned.passes[0].visible &&
                   planned.passes[0].nearDistance > 0.0f &&
                   planned.passes[0].farDistance > planned.passes[0].nearDistance;
  if (captureOk) {
    SbVec3f spotCenter;
    (planned.passes[0].view * planned.passes[0].projectionCoin)
      .multVecMatrix(SbVec3f(0, 0, 0), spotCenter);
    captureOk = std::abs(spotCenter[0]) <= 1.0f &&
                std::abs(spotCenter[1]) <= 1.0f &&
                std::abs(spotCenter[2]) <= 1.0f;
    for (uint32_t d : planned.passes[0].casterDraws) {
      const auto & state = captured.renderStates[captured.draws[d].renderStateSlot];
      captureOk = captureOk && state.shadowGroupSlot == 1 &&
                  state.shadowStyle == SoShadowStyle::CASTS_SHADOW_AND_SHADOWED;
    }
    bool receiverOnly = false;
    for (uint32_t d : planned.passes[0].receiverDraws) {
      const auto & state = captured.renderStates[captured.draws[d].renderStateSlot];
      receiverOnly = receiverOnly || state.shadowStyle == SoShadowStyle::SHADOWED;
    }
    captureOk = captureOk && receiverOnly;
  }
  target->readbackRGBA(afterRejection);
  const bool preserved = target->getLastSubmissionSerial() == originalSerial &&
                         !published.empty() && published == afterRejection;
  auto * ordinaryDirectional = new SoDirectionalLight;
  auto * ordinaryPoint = new SoPointLight;
  group->addChild(ordinaryDirectional);
  group->addChild(ordinaryPoint);
  action.apply(root);
  const auto & mixedFrame = action.getPimpl()->lastRejectedShadowFrame;
  const auto & mixedPlan = action.getPimpl()->lastRejectedShadowPlan;
  const bool lightEligibility = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      mixedFrame.shadowLights.size() == 3 &&
      mixedFrame.shadowLights[0].shadowEligible &&
      !mixedFrame.shadowLights[1].shadowEligible &&
      !mixedFrame.shadowLights[2].shadowEligible &&
      mixedPlan.passes.size() == 1 && mixedPlan.passes[0].lightSlot == 0;
  group->removeChild(ordinaryPoint);
  group->removeChild(ordinaryDirectional);
  light->nearDistance = 2.0f;
  light->farDistance = 12.0f;
  action.apply(root);
  const auto & overriddenPasses = action.getPimpl()->lastRejectedShadowPlan.passes;
  const bool spotRangeCaptured = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      overriddenPasses.size() == 1 &&
      overriddenPasses[0].nearDistance == 2.0f &&
      overriddenPasses[0].farDistance == 12.0f;
  light->nearDistance = -1.0f;
  light->farDistance = -1.0f;
  group->isActive = FALSE;
  action.apply(root);
  target->readbackRGBA(afterRecovery);
  const bool recovered = action.getLastStatus() == CoinRenderAction::SUCCESS &&
                         target->getLastSubmissionSerial() > originalSerial &&
                         afterRecovery == published;
  auto * directionalCapture = new SoShadowDirectionalLight;
  directionalCapture->direction.setValue(-0.4f, -0.4f, -1.0f);
  light->on = FALSE;
  group->addChild(directionalCapture);
  group->isActive = TRUE;
  action.apply(root);
  const auto & directionalFrame = action.getPimpl()->lastRejectedShadowFrame;
  const auto & directionalPlan = action.getPimpl()->lastRejectedShadowPlan;
  const bool directionalCaptured = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      directionalFrame.shadowLights.size() == 2 &&
      directionalFrame.shadowLights[0].type == CoinRenderLightType::SPOT &&
      !directionalFrame.shadowLights[0].enabled &&
      directionalFrame.shadowLights[1].type == CoinRenderLightType::DIRECTIONAL &&
      directionalPlan.passes.size() == 1 &&
      directionalPlan.passes[0].lightSlot == 1 &&
      directionalPlan.passes[0].visible;
  bool directionalProjectionCoversGroup = directionalCaptured;
  if (directionalProjectionCoversGroup) {
    const auto & pass = directionalPlan.passes[0];
    for (const auto & draw : directionalFrame.draws) {
      const auto & state = directionalFrame.renderStates[draw.renderStateSlot];
      if (state.shadowGroupSlot != pass.groupSlot) continue;
      const SbMatrix mvp = state.model * pass.view * pass.projectionCoin;
      for (uint32_t i = draw.geometry.firstVertex;
           i < draw.geometry.firstVertex + draw.geometry.vertexCount; ++i) {
        const auto & vertex = directionalFrame.vertices[i];
        SbVec3f projected;
        mvp.multVecMatrix(SbVec3f(vertex.position[0], vertex.position[1],
                                    vertex.position[2]), projected);
        for (int axis = 0; axis < 3; ++axis)
          directionalProjectionCoversGroup = directionalProjectionCoversGroup &&
              projected[axis] >= -1.02f && projected[axis] <= 1.02f;
      }
    }
  }
  group->removeChild(directionalCapture);
  light->on = TRUE;
  action.setRenderTarget(nullptr);
  delete target;
  if (!publishedOk || !rejected || !preserved || !recovered || !captureOk || !lightEligibility || !spotRangeCaptured || !directionalCaptured ||
      !directionalProjectionCoversGroup) {
    std::cerr << "CoinRender shadow rejection did not preserve publication or recovery\n";
    root->unref();
    return 1;
  }
  if (!std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
    root->unref();
    return 0;
  }
  if (!SoShadowGroup::isSupported()) {
    std::cerr << "Coin/GL shadow reference requires shadow-map support\n";
    root->unref();
    return 1;
  }

  SoOffscreenRenderer gl(SbViewportRegion(side, side));
  gl.setComponents(SoOffscreenRenderer::RGB);
  std::vector<unsigned char> inactive, active, unshadowed, activeAgain;
  group->isActive = FALSE;
  bool ok = render(gl, root, inactive);
  group->isActive = TRUE;
  ok = ok && render(gl, root, active);
  groundStyle->style = SoShadowStyle::NO_SHADOWING;
  ok = ok && render(gl, root, unshadowed);
  groundStyle->style = SoShadowStyle::SHADOWED;
  ok = ok && render(gl, root, activeAgain);
  castStyle->style = SoShadowStyle::NO_SHADOWING;
  std::vector<unsigned char> noCaster;
  ok = ok && render(gl, root, noCaster);
  castStyle->style = SoShadowStyle::CASTS_SHADOW_AND_SHADOWED;
  auto * directional = new SoShadowDirectionalLight;
  directional->direction.setValue(-0.4f, -0.4f, -1.0f);
  directional->intensity = 1.0f;
  group->replaceChild(light, directional);
  std::vector<unsigned char> directionalShadow, directionalNoReceive;
  ok = ok && render(gl, root, directionalShadow);
  groundStyle->style = SoShadowStyle::NO_SHADOWING;
  ok = ok && render(gl, root, directionalNoReceive);
  root->unref();
  if (!ok) {
    std::cerr << "Coin/GL could not render the shadow reference\n";
    return 1;
  }

  const int clearShadow = luminance(inactive, 50, 50);
  const int castShadow = luminance(active, 50, 50);
  const int noReceive = luminance(unshadowed, 50, 50);
  const int restoredShadow = luminance(activeAgain, 50, 50);
  const int withoutCaster = luminance(noCaster, 50, 50);
  const int centerDifference = std::abs(luminance(inactive, 64, 64) -
                                        luminance(active, 64, 64));
  std::cout << "Coin/GL shadow sample inactive=" << clearShadow
            << " active=" << castShadow
            << " no_receive=" << noReceive
            << " restored=" << restoredShadow
            << " no_caster=" << withoutCaster
            << " center_delta=" << centerDifference << '\n';
  int directionalDifference = 0;
  for (int y = 20; y < 105; ++y)
    for (int x = 20; x < 105; ++x)
      directionalDifference = std::max(directionalDifference,
        luminance(directionalNoReceive, x, y) - luminance(directionalShadow, x, y));
  std::cout << " directional_style_delta=" << directionalDifference << '\n';
  if (clearShadow - castShadow < 100 || noReceive - castShadow < 100 ||
      withoutCaster - castShadow < 100 ||
      std::abs(restoredShadow - castShadow) > 30 || centerDifference > 45 ||
      directionalDifference < 100) {
    std::cerr << "Coin/GL shadow, style or cache reference changed\n";
    return 1;
  }
  return 0;
}
