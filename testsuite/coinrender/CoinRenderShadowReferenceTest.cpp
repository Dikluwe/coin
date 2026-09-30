#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include "actions/CoinRenderActionP.h"
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
#include "rendering/coinwgpu/CoinWgpuBackend.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#endif
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/SbColor.h>

#include <cstdlib>
#include <cmath>
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

int luminanceRgba(const std::vector<unsigned char> & rgba, int x, int y)
{
  const size_t i = static_cast<size_t>((y * side + x) * 4);
  return static_cast<int>(rgba[i]) + rgba[i + 1] + rgba[i + 2];
}

bool render(SoOffscreenRenderer & gl, SoNode * root,
            std::vector<unsigned char> & rgb)
{
  if (!gl.render(root) || !gl.getBuffer()) return false;
  const unsigned char * data = gl.getBuffer();
  rgb.assign(data, data + side * side * 3);
  return true;
}
#ifdef HAVE_COIN_BGFX
bool bgfxLightDelta(SoNode * root, SoLight * switched, int & difference)
{
  difference = 0;
  CoinRenderTarget * target = CoinRenderTarget::createOffscreen(SbVec2i32(side, side));
  CoinRenderAction action(SbViewportRegion(side, side));
  action.setRenderTarget(target);
  switched->on = FALSE;
  action.apply(root);
  const bool singleOk = action.getLastStatus() == CoinRenderAction::SUCCESS;
  std::vector<unsigned char> single, dual;
  if (singleOk) target->readbackRGBA(single);
  switched->on = TRUE;
  action.apply(root);
  const bool dualOk = action.getLastStatus() == CoinRenderAction::SUCCESS;
  if (dualOk) target->readbackRGBA(dual);
  const bool comparable = singleOk && dualOk && single.size() == dual.size();
  if (comparable)
    for (int y = 20; y < 105; ++y)
      for (int x = 20; x < 105; ++x)
        difference = std::max(difference,
          std::abs(luminanceRgba(single, x, y) - luminanceRgba(dual, x, y)));
  if (!comparable)
    std::cerr << "BGFX light pair failed: " << action.getLastError().getString() << '\n';
  action.setRenderTarget(nullptr);
  delete target;
  return comparable;
}
#endif
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
                   captured.shadowLights[0].intensity == 1.0f &&
                   captured.shadowLights[0].color == SbColor(1, 1, 1) &&
                   planned.passes.size() == 1 &&
                   !planned.passes[0].casterDraws.empty() &&
                   !planned.passes[0].receiverDraws.empty() &&
                   planned.passes[0].visible &&
                   planned.passes[0].perFragmentLighting &&
                   planned.passes[0].nearDistance > 0.0f &&
                   planned.passes[0].farDistance > planned.passes[0].nearDistance;
  std::string spotProfileDiagnostic;
  const bool spotProfile = coin_render_shadow_single_spot_opaque_profile(
    captured, planned, spotProfileDiagnostic);
#ifdef HAVE_COIN_BGFX
  std::vector<unsigned char> bgfxShadowImage;
  bool bgfxShadowSubmitted = true;
  if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU")) {
    CoinRenderTarget * bgfxTarget = CoinRenderTarget::createOffscreen(SbVec2i32(side, side));
    CoinRenderFramePlan withoutShadows = captured;
    withoutShadows.revision = 0;
    withoutShadows.shadowGroups.clear();
    withoutShadows.shadowLights.clear();
    for (auto & state : withoutShadows.renderStates) state.shadowGroupSlot = 0;
    const auto baseline = bgfxTarget->getPimpl()->executeFrame(withoutShadows);
    std::vector<unsigned char> unshadowed, shadowed;
    if (baseline.status == CoinRenderBackendStatus::SUCCESS)
      bgfxTarget->readbackRGBA(unshadowed);
    const auto result = bgfxTarget->getPimpl()->executeFrame(captured);
    if (result.status == CoinRenderBackendStatus::SUCCESS) {
      bgfxTarget->readbackRGBA(shadowed);
      bgfxShadowImage = shadowed;
    }
    bgfxShadowSubmitted = baseline.status == CoinRenderBackendStatus::SUCCESS &&
      result.status == CoinRenderBackendStatus::SUCCESS &&
      shadowed.size() == unshadowed.size();
    if (!bgfxShadowSubmitted)
      std::cerr << "BGFX shadow frame failed: baseline=" << baseline.diagnostic
                << " shadow=" << result.diagnostic << '\n';
    else {
      const int clear = luminanceRgba(unshadowed, 50, side - 1 - 50);
      const int shadow = luminanceRgba(shadowed, 50, side - 1 - 50);
      bgfxShadowSubmitted = clear - shadow > 100;
      if (!bgfxShadowSubmitted)
        std::cerr << "BGFX spot shadow did not darken the Coin/GL sample: "
                  << clear << " -> " << shadow << '\n';
    }
    if (bgfxShadowSubmitted) {
      CoinRenderTarget * actionTarget = CoinRenderTarget::createOffscreen(
        SbVec2i32(side, side));
      CoinRenderAction bgfxAction(SbViewportRegion(side, side));
      bgfxAction.setRenderTarget(actionTarget);
      bgfxAction.apply(root);
      std::vector<unsigned char> viaAction;
      if (bgfxAction.getLastStatus() == CoinRenderAction::SUCCESS)
        actionTarget->readbackRGBA(viaAction);
      bgfxShadowSubmitted = bgfxAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        viaAction == shadowed;
      if (!bgfxShadowSubmitted)
        std::cerr << "BGFX public Action shadow frame: "
                  << bgfxAction.getLastError().getString() << '\n';
      if (bgfxShadowSubmitted) {
        groundStyle->style = SoShadowStyle::NO_SHADOWING;
        bgfxAction.apply(root);
        std::vector<unsigned char> noReceive;
        if (bgfxAction.getLastStatus() == CoinRenderAction::SUCCESS)
          actionTarget->readbackRGBA(noReceive);
        groundStyle->style = SoShadowStyle::SHADOWED;
        bgfxAction.apply(root);
        std::vector<unsigned char> receiveAgain;
        if (bgfxAction.getLastStatus() == CoinRenderAction::SUCCESS)
          actionTarget->readbackRGBA(receiveAgain);
        bgfxShadowSubmitted = bgfxAction.getLastStatus() == CoinRenderAction::SUCCESS &&
          receiveAgain == shadowed && noReceive.size() == shadowed.size() &&
          luminanceRgba(noReceive, 50, side - 1 - 50) - luminanceRgba(shadowed, 50, side - 1 - 50) > 100;
        if (!bgfxShadowSubmitted)
          std::cerr << "BGFX receiver style or recovery failed: "
                    << bgfxAction.getLastError().getString() << '\n';
      }
      if (bgfxShadowSubmitted) {
        const uint64_t serial = actionTarget->getLastSubmissionSerial();
        castStyle->style = SoShadowStyle::NO_SHADOWING;
        bgfxAction.apply(root);
        std::vector<unsigned char> afterRejection;
        actionTarget->readbackRGBA(afterRejection);
        bgfxShadowSubmitted = bgfxAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
          actionTarget->getLastSubmissionSerial() == serial &&
          afterRejection == shadowed;
        castStyle->style = SoShadowStyle::CASTS_SHADOW_AND_SHADOWED;
        bgfxAction.apply(root);
        std::vector<unsigned char> recovered;
        if (bgfxAction.getLastStatus() == CoinRenderAction::SUCCESS)
          actionTarget->readbackRGBA(recovered);
        bgfxShadowSubmitted = bgfxShadowSubmitted &&
          bgfxAction.getLastStatus() == CoinRenderAction::SUCCESS &&
          recovered == shadowed && actionTarget->getLastSubmissionSerial() > serial;
        if (!bgfxShadowSubmitted)
          std::cerr << "BGFX caster rejection/recovery failed: "
                    << bgfxAction.getLastError().getString() << '\n';
      }
      if (bgfxShadowSubmitted) {
        const int resized = 160;
        bgfxShadowSubmitted = actionTarget->resize(SbVec2i32(resized, resized));
        bgfxAction.setViewportRegion(SbViewportRegion(resized, resized));
        bgfxAction.apply(root);
        std::vector<unsigned char> resizedPixels;
        if (bgfxAction.getLastStatus() == CoinRenderAction::SUCCESS)
          actionTarget->readbackRGBA(resizedPixels);
        bgfxShadowSubmitted = bgfxShadowSubmitted &&
          bgfxAction.getLastStatus() == CoinRenderAction::SUCCESS &&
          resizedPixels.size() == size_t(resized * resized * 4);
        if (bgfxShadowSubmitted) {
          bgfxShadowSubmitted = actionTarget->resize(SbVec2i32(side, side));
          bgfxAction.setViewportRegion(SbViewportRegion(side, side));
          bgfxAction.apply(root);
          std::vector<unsigned char> restored;
          if (bgfxAction.getLastStatus() == CoinRenderAction::SUCCESS)
            actionTarget->readbackRGBA(restored);
          bgfxShadowSubmitted = bgfxShadowSubmitted &&
            bgfxAction.getLastStatus() == CoinRenderAction::SUCCESS &&
            restored == shadowed;
        }
        if (!bgfxShadowSubmitted)
          std::cerr << "BGFX shadow resize/recovery failed: "
                    << bgfxAction.getLastError().getString() << '\n';
      }
      if (bgfxShadowSubmitted) {
        const uint64_t serial = actionTarget->getLastSubmissionSerial();
        setenv("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE", "1", 1);
        bgfxAction.apply(root);
        std::vector<unsigned char> afterFailure;
        actionTarget->readbackRGBA(afterFailure);
        bgfxShadowSubmitted = bgfxAction.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
          actionTarget->getLastSubmissionSerial() == serial &&
          afterFailure == shadowed;
        const auto otherTargetResult = bgfxTarget->getPimpl()->executeFrame(captured);
        std::vector<unsigned char> otherTargetPixels;
        if (otherTargetResult.status == CoinRenderBackendStatus::SUCCESS)
          bgfxTarget->readbackRGBA(otherTargetPixels);
        bgfxShadowSubmitted = bgfxShadowSubmitted &&
          otherTargetResult.status == CoinRenderBackendStatus::SUCCESS &&
          otherTargetPixels == shadowed;
        if (!bgfxShadowSubmitted)
          std::cerr << "BGFX shadow-map failure or peer recovery failed: "
                    << bgfxAction.getLastError().getString() << " / "
                    << otherTargetResult.diagnostic << '\n';
      }
      bgfxAction.setRenderTarget(nullptr);
      delete actionTarget;
    }
    delete bgfxTarget;
  }
  if (!bgfxShadowSubmitted) { std::cerr << "BGFX spot qualification failed\n"; return 1; }
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  std::vector<unsigned char> wgpuShadow, wgpuUnshadowed;
  bool wgpuShadowSubmitted = true;
  if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU")) {
    CoinRenderTarget * shadowTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    shadowTarget->getPimpl()->backend.reset(new CoinWgpuBackend);
    CoinRenderFramePlan withoutShadows = captured;
    withoutShadows.revision = 0;
    withoutShadows.shadowGroups.clear();
    withoutShadows.shadowLights.clear();
    for (auto & state : withoutShadows.renderStates) state.shadowGroupSlot = 0;
    const auto baseline = shadowTarget->getPimpl()->executeFrame(withoutShadows);
    wgpuShadowSubmitted = baseline.status == CoinRenderBackendStatus::SUCCESS;
    if (wgpuShadowSubmitted) shadowTarget->readbackRGBA(wgpuUnshadowed);
    else std::cerr << "wgpu unshadowed frame: " << baseline.diagnostic << '\n';
    if (wgpuShadowSubmitted) {
      const auto execution = shadowTarget->getPimpl()->executeFrame(captured);
      wgpuShadowSubmitted = execution.status == CoinRenderBackendStatus::SUCCESS;
      if (wgpuShadowSubmitted) shadowTarget->readbackRGBA(wgpuShadow);
      else std::cerr << "wgpu shadow frame: " << execution.diagnostic << '\n';
    }
    delete shadowTarget;
    // Exercise the public Action gate as well as the direct backend seam.
    CoinRenderTarget * actionTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    CoinRenderAction gpuAction(SbViewportRegion(side, side));
    gpuAction.setRenderTarget(actionTarget);
    gpuAction.apply(root);
    std::vector<unsigned char> viaAction;
    if (gpuAction.getLastStatus() == CoinRenderAction::SUCCESS)
      actionTarget->readbackRGBA(viaAction);
    wgpuShadowSubmitted = wgpuShadowSubmitted &&
      gpuAction.getLastStatus() == CoinRenderAction::SUCCESS &&
      viaAction == wgpuShadow;
    if (!wgpuShadowSubmitted)
      std::cerr << "wgpu Action shadow frame: " << gpuAction.getLastError().getString() << '\n';
    if (wgpuShadowSubmitted) {
      groundStyle->style = SoShadowStyle::NO_SHADOWING;
      gpuAction.apply(root);
      std::vector<unsigned char> noReceive;
      if (gpuAction.getLastStatus() == CoinRenderAction::SUCCESS)
        actionTarget->readbackRGBA(noReceive);
      groundStyle->style = SoShadowStyle::SHADOWED;
      gpuAction.apply(root);
      std::vector<unsigned char> receiveAgain;
      if (gpuAction.getLastStatus() == CoinRenderAction::SUCCESS)
        actionTarget->readbackRGBA(receiveAgain);
      const size_t sample = static_cast<size_t>(((side - 1 - 50) * side + 50) * 4);
      const auto luma = [sample](const std::vector<unsigned char> & rgba) {
        return int(rgba[sample]) + rgba[sample + 1] + rgba[sample + 2];
      };
      wgpuShadowSubmitted = noReceive.size() == wgpuShadow.size() &&
        receiveAgain == wgpuShadow &&
        luma(noReceive) - luma(wgpuShadow) > 100;
      if (!wgpuShadowSubmitted)
        std::cerr << "wgpu receiver style check failed: "
                  << gpuAction.getLastError().getString() << '\n';
    }
    if (wgpuShadowSubmitted) {
      const int resized = 160;
      wgpuShadowSubmitted = actionTarget->resize(SbVec2i32(resized, resized));
      gpuAction.setViewportRegion(SbViewportRegion(resized, resized));
      group->isActive = FALSE;
      gpuAction.apply(root);
      std::vector<unsigned char> resizedClear, resizedShadow;
      if (gpuAction.getLastStatus() == CoinRenderAction::SUCCESS)
        actionTarget->readbackRGBA(resizedClear);
      group->isActive = TRUE;
      gpuAction.apply(root);
      if (gpuAction.getLastStatus() == CoinRenderAction::SUCCESS)
        actionTarget->readbackRGBA(resizedShadow);
      const size_t pixel = static_cast<size_t>((96 * resized + 63) * 4);
      const auto luma = [pixel](const std::vector<unsigned char> & rgba) {
        return int(rgba[pixel]) + rgba[pixel + 1] + rgba[pixel + 2];
      };
      wgpuShadowSubmitted = wgpuShadowSubmitted &&
        resizedClear.size() == size_t(resized * resized * 4) &&
        resizedShadow.size() == resizedClear.size() &&
        luma(resizedClear) - luma(resizedShadow) > 100;
      if (wgpuShadowSubmitted) {
        const uint64_t serial = actionTarget->getLastSubmissionSerial();
        castStyle->style = SoShadowStyle::NO_SHADOWING;
        gpuAction.apply(root);
        std::vector<unsigned char> afterUnsupported;
        actionTarget->readbackRGBA(afterUnsupported);
        wgpuShadowSubmitted = gpuAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
          actionTarget->getLastSubmissionSerial() == serial &&
          afterUnsupported == resizedShadow;
        castStyle->style = SoShadowStyle::CASTS_SHADOW_AND_SHADOWED;
        gpuAction.apply(root);
        std::vector<unsigned char> afterRecovery;
        if (gpuAction.getLastStatus() == CoinRenderAction::SUCCESS)
          actionTarget->readbackRGBA(afterRecovery);
        wgpuShadowSubmitted = wgpuShadowSubmitted &&
          gpuAction.getLastStatus() == CoinRenderAction::SUCCESS &&
          actionTarget->getLastSubmissionSerial() > serial &&
          afterRecovery == resizedShadow;
      }
      if (!wgpuShadowSubmitted)
        std::cerr << "wgpu resize or publication check failed: "
                  << gpuAction.getLastError().getString() << '\n';
      if (wgpuShadowSubmitted) {
        const uint64_t serial = actionTarget->getLastSubmissionSerial();
        coin_wgpu_inject_fault(COIN_WGPU_FAULT_SHADOW_MAP_ALLOC);
        gpuAction.apply(root);
        coin_wgpu_inject_fault(0);
        std::vector<unsigned char> afterMapFailure;
        actionTarget->readbackRGBA(afterMapFailure);
        wgpuShadowSubmitted = gpuAction.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
          actionTarget->getLastSubmissionSerial() == serial &&
          afterMapFailure == resizedShadow;
        if (!wgpuShadowSubmitted)
          std::cerr << "wgpu shadow-map failure did not preserve publication\n";
      }
    }
    gpuAction.setRenderTarget(nullptr);
    delete actionTarget;
  }
#endif
  CoinRenderFramePlan transparentFrame = captured;
  if (!transparentFrame.materials.empty())
    transparentFrame.materials[0].transparency = 0.25f;
  std::string transparentProfileDiagnostic;
  const bool transparentExcludedFromFirstProfile =
      !coin_render_shadow_single_spot_opaque_profile(
        transparentFrame, planned, transparentProfileDiagnostic);
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
    for (uint32_t d : planned.passes[0].receiverDraws) {
      const uint32_t stateSlot = captured.draws[d].renderStateSlot;
      const auto & source = planned.passes[0].resolvedLightByState[stateSlot];
      captureOk = captureOk && planned.passes[0].lightingIndexByState[stateSlot] == 0 &&
                  source.sourceRevision == captured.shadowLights[0].sourceRevision &&
                  source.type == CoinRenderLightType::SPOT &&
                  source.color == SbColor(1, 1, 1) &&
                  source.intensity == 1.0f &&
                  std::abs(source.position[0] - 2.0f) < 0.001f &&
                  std::abs(source.position[1] - 2.0f) < 0.001f &&
                  std::abs(source.position[2] + 4.0f) < 0.001f &&
                  source.direction[0] < 0.0f &&
                  source.direction[1] < 0.0f &&
                  source.direction[2] < 0.0f;
    }
  }
  // Qualify the common two-pass Coin contract and execute both maps on wgpu.
  auto * secondShadowLight = new SoShadowDirectionalLight;
  secondShadowLight->direction.setValue(-0.4f, -0.4f, -1.0f);
  secondShadowLight->intensity = 0.6f;
  group->insertChild(secondShadowLight, 1);
  const float previousQuality = group->quality.getValue();
  group->quality = 1.0f;
  action.apply(root);
  const auto & twoLightFrame = action.getPimpl()->lastRejectedShadowFrame;
  const auto & twoLightPlan = action.getPimpl()->lastRejectedShadowPlan;
  std::string twoLightDiagnostic;
  bool twoLightCaptured = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
    coin_render_shadow_spot_directional_opaque_profile(
      twoLightFrame, twoLightPlan, twoLightDiagnostic) &&
    !coin_render_shadow_single_spot_opaque_profile(
      twoLightFrame, twoLightPlan, twoLightDiagnostic);
  if (twoLightCaptured) {
    for (const auto & draw : twoLightFrame.draws) {
      const uint32_t slot = draw.renderStateSlot;
      twoLightCaptured = twoLightCaptured &&
        twoLightPlan.passes[0].lightingIndexByState[slot] == 0 &&
        twoLightPlan.passes[1].lightingIndexByState[slot] == 1;
    }
    CoinRenderFramePlan mismatched = twoLightFrame;
    const uint32_t lightingSlot = mismatched.renderStates[
      mismatched.draws[0].renderStateSlot].lightingSlot;
    if (lightingSlot < mismatched.lightingStates.size() &&
        mismatched.lightingStates[lightingSlot].lights.size() == 2) {
      mismatched.lightingStates[lightingSlot].lights[1].sourceRevision ^= 1;
      std::string mismatchDiagnostic;
      twoLightCaptured = twoLightCaptured &&
        !coin_render_shadow_spot_directional_opaque_profile(
          mismatched, twoLightPlan, mismatchDiagnostic);
    } else twoLightCaptured = false;
  }
#ifdef HAVE_COIN_BGFX
  bool twoLightBgfxSubmitted = true;
  int twoLightBgfxDifference = 0;
  if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU")) {
    CoinRenderTarget * dualTarget = CoinRenderTarget::createOffscreen(SbVec2i32(side, side));
    CoinRenderAction dualAction(SbViewportRegion(side, side));
    dualAction.setRenderTarget(dualTarget);
    secondShadowLight->on = FALSE;
    dualAction.apply(root);
    std::vector<unsigned char> singlePixels, dualPixels;
    const bool singleOk = dualAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (singleOk) dualTarget->readbackRGBA(singlePixels);
    secondShadowLight->on = TRUE;
    dualAction.apply(root);
    const bool dualOk = dualAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (dualOk) dualTarget->readbackRGBA(dualPixels);
    if (singleOk && dualOk && singlePixels.size() == dualPixels.size())
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          twoLightBgfxDifference = std::max(twoLightBgfxDifference,
            std::abs(luminanceRgba(singlePixels, x, y) -
                     luminanceRgba(dualPixels, x, y)));
    twoLightBgfxSubmitted = singleOk && dualOk && twoLightBgfxDifference > 100;
    if (twoLightBgfxSubmitted) {
      auto * third = new SoShadowSpotLight;
      third->location.setValue(-2.0f, 2.0f, 4.0f);
      third->direction.setValue(2.0f, -2.0f, -5.0f);
      third->cutOffAngle = 0.9f;
      group->insertChild(third, 2);
      const uint64_t serial = dualTarget->getLastSubmissionSerial();
      dualAction.apply(root);
      std::vector<unsigned char> afterThree;
      dualTarget->readbackRGBA(afterThree);
      twoLightBgfxSubmitted = dualAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
        dualTarget->getLastSubmissionSerial() == serial && afterThree == dualPixels;
      group->removeChild(third);
      dualAction.apply(root);
      std::vector<unsigned char> recovered;
      if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(recovered);
      twoLightBgfxSubmitted = twoLightBgfxSubmitted &&
        dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        dualTarget->getLastSubmissionSerial() > serial && recovered == dualPixels;
    }
    if (twoLightBgfxSubmitted) {
      const int resized = 160;
      twoLightBgfxSubmitted = dualTarget->resize(SbVec2i32(resized, resized));
      dualAction.setViewportRegion(SbViewportRegion(resized, resized));
      dualAction.apply(root);
      std::vector<unsigned char> resizedPixels;
      if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(resizedPixels);
      twoLightBgfxSubmitted = twoLightBgfxSubmitted &&
        dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        resizedPixels.size() == size_t(resized * resized * 4);
      if (twoLightBgfxSubmitted) {
        twoLightBgfxSubmitted = dualTarget->resize(SbVec2i32(side, side));
        dualAction.setViewportRegion(SbViewportRegion(side, side));
        dualAction.apply(root);
        std::vector<unsigned char> restored;
        if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
          dualTarget->readbackRGBA(restored);
        twoLightBgfxSubmitted = twoLightBgfxSubmitted &&
          dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
          restored == dualPixels;
      }
    }
    if (twoLightBgfxSubmitted) {
      const uint64_t serial = dualTarget->getLastSubmissionSerial();
      setenv("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE", "1", 1);
      dualAction.apply(root);
      std::vector<unsigned char> afterFault;
      dualTarget->readbackRGBA(afterFault);
      twoLightBgfxSubmitted = dualAction.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
        dualTarget->getLastSubmissionSerial() == serial && afterFault == dualPixels;
    }
    if (!twoLightBgfxSubmitted)
      std::cerr << "BGFX two-light frame: " << dualAction.getLastError().getString()
                << " delta=" << twoLightBgfxDifference << '\n';
    dualAction.setRenderTarget(nullptr);
    delete dualTarget;
  }
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  bool twoLightSubmittedOnGpu = true;
  int twoLightWgpuDifference = 0;
  if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU")) {
    CoinRenderTarget * dualTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    CoinRenderAction dualAction(SbViewportRegion(side, side));
    dualAction.setRenderTarget(dualTarget);
    secondShadowLight->on = FALSE;
    dualAction.apply(root);
    std::vector<unsigned char> singleGpu, dualGpu;
    const bool singleSubmitted = dualAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (singleSubmitted) dualTarget->readbackRGBA(singleGpu);
    secondShadowLight->on = TRUE;
    dualAction.apply(root);
    const bool dualSubmitted = dualAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (dualSubmitted) dualTarget->readbackRGBA(dualGpu);
    if (singleSubmitted && dualSubmitted && singleGpu.size() == dualGpu.size())
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          twoLightWgpuDifference = std::max(twoLightWgpuDifference,
            std::abs(luminanceRgba(singleGpu, x, y) - luminanceRgba(dualGpu, x, y)));
    twoLightSubmittedOnGpu = singleSubmitted && dualSubmitted &&
      twoLightWgpuDifference > 100;
    if (twoLightSubmittedOnGpu) {
      auto * thirdShadowLight = new SoShadowSpotLight;
      thirdShadowLight->location.setValue(-2.0f, 2.0f, 4.0f);
      thirdShadowLight->direction.setValue(2.0f, -2.0f, -5.0f);
      thirdShadowLight->cutOffAngle = 0.9f;
      group->insertChild(thirdShadowLight, 2);
      const uint64_t serial = dualTarget->getLastSubmissionSerial();
      dualAction.apply(root);
      std::vector<unsigned char> afterThreeLights;
      dualTarget->readbackRGBA(afterThreeLights);
      const bool threeRejected =
        dualAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
        dualTarget->getLastSubmissionSerial() == serial &&
        afterThreeLights == dualGpu;
      if (!threeRejected)
        std::cerr << "three-light boundary: status=" << dualAction.getLastStatus()
                  << " serial=" << dualTarget->getLastSubmissionSerial()
                  << " expected=" << serial << " diagnostic="
                  << dualAction.getLastError().getString() << '\n';
      twoLightSubmittedOnGpu = twoLightSubmittedOnGpu && threeRejected;
      group->removeChild(thirdShadowLight);
      CoinRenderAction recoveryAction(SbViewportRegion(side, side));
      recoveryAction.setRenderTarget(dualTarget);
      recoveryAction.apply(root);
      std::vector<unsigned char> recoveredDual;
      if (recoveryAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(recoveredDual);
      const bool threeRecovered =
        recoveryAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        dualTarget->getLastSubmissionSerial() > serial &&
        recoveredDual == dualGpu;
      if (!threeRecovered)
        std::cerr << "three-light recovery: status=" << recoveryAction.getLastStatus()
                  << " serial=" << dualTarget->getLastSubmissionSerial()
                  << " expected>" << serial << " diagnostic="
                  << recoveryAction.getLastError().getString() << '\n';
      twoLightSubmittedOnGpu = twoLightSubmittedOnGpu && threeRecovered;
      recoveryAction.setRenderTarget(nullptr);
    }
    if (twoLightSubmittedOnGpu) {
      const int resized = 160;
      twoLightSubmittedOnGpu = dualTarget->resize(SbVec2i32(resized, resized));
      dualAction.setViewportRegion(SbViewportRegion(resized, resized));
      dualAction.apply(root);
      std::vector<unsigned char> resizedDual;
      if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(resizedDual);
      twoLightSubmittedOnGpu = twoLightSubmittedOnGpu &&
        dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        resizedDual.size() == size_t(resized * resized * 4);
      if (twoLightSubmittedOnGpu) {
        twoLightSubmittedOnGpu = dualTarget->resize(SbVec2i32(side, side));
        dualAction.setViewportRegion(SbViewportRegion(side, side));
        dualAction.apply(root);
        std::vector<unsigned char> restoredDual;
        if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
          dualTarget->readbackRGBA(restoredDual);
        twoLightSubmittedOnGpu = twoLightSubmittedOnGpu &&
          dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
          restoredDual == dualGpu;
      }
    }
    if (twoLightSubmittedOnGpu) {
      const uint64_t serial = dualTarget->getLastSubmissionSerial();
      coin_wgpu_inject_fault(COIN_WGPU_FAULT_SHADOW_MAP_ALLOC);
      dualAction.apply(root);
      coin_wgpu_inject_fault(0);
      std::vector<unsigned char> afterFailure;
      dualTarget->readbackRGBA(afterFailure);
      twoLightSubmittedOnGpu =
        dualAction.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
        dualTarget->getLastSubmissionSerial() == serial &&
        afterFailure == dualGpu;
    }
    if (!twoLightSubmittedOnGpu)
      std::cerr << "wgpu two-light frame failed: "
                << dualAction.getLastError().getString()
                << " delta=" << twoLightWgpuDifference << '\n';
    dualAction.setRenderTarget(nullptr);
    delete dualTarget;
  }
#endif
#ifdef HAVE_COIN_BGFX
  if (!twoLightBgfxSubmitted) return 1;
#endif
  int twoLightGlDifference = 0;
  if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
    auto * dualRoot = static_cast<SoSeparator *>(root->copy(TRUE));
    dualRoot->ref();
    auto * singleRoot = static_cast<SoSeparator *>(dualRoot->copy(TRUE));
    singleRoot->ref();
    auto * singleGroup = static_cast<SoShadowGroup *>(singleRoot->getChild(1));
    singleGroup->removeChild(1);
    SoOffscreenRenderer dualGl(SbViewportRegion(side, side));
    dualGl.setComponents(SoOffscreenRenderer::RGB);
    std::vector<unsigned char> dualPixels, singlePixels;
    const bool dualRendered = render(dualGl, dualRoot, dualPixels) &&
                              render(dualGl, singleRoot, singlePixels);
    if (dualRendered)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          twoLightGlDifference = std::max(twoLightGlDifference,
            std::abs(luminance(dualPixels, x, y) -
                     luminance(singlePixels, x, y)));
    singleRoot->unref();
    dualRoot->unref();
    twoLightCaptured = twoLightCaptured && dualRendered && twoLightGlDifference > 100;
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
    if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU"))
      twoLightSubmittedOnGpu = twoLightSubmittedOnGpu &&
        std::abs(twoLightWgpuDifference - twoLightGlDifference) <= 80;
#endif
    std::cout << "Coin/GL two-light contribution delta=" << twoLightGlDifference
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
              << " wgpu=" << twoLightWgpuDifference
#endif
              << '\n';
  }
  bool orderedTwoLight = true;
  for (int position = 2; position <= 3; ++position) {
    secondShadowLight->ref();
    group->removeChild(secondShadowLight);
    group->insertChild(secondShadowLight, position);
    secondShadowLight->unref();
    action.apply(root);
    const auto & orderedFrame = action.getPimpl()->lastRejectedShadowFrame;
    const auto & orderedPlan = action.getPimpl()->lastRejectedShadowPlan;
    std::string orderedDiagnostic;
    bool capturedOrder = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      coin_render_shadow_spot_directional_opaque_profile(
        orderedFrame, orderedPlan, orderedDiagnostic);
    bool sawLate = false, sawEarly = false;
    if (capturedOrder) {
      for (const auto & draw : orderedFrame.draws) {
        const uint32_t stateSlot = draw.renderStateSlot;
        const int32_t index = orderedPlan.passes[1].lightingIndexByState[stateSlot];
        sawLate = sawLate || index == -1;
        sawEarly = sawEarly || index == 1;
        capturedOrder = capturedOrder && (index == -1 || index == 1);
      }
    }
    capturedOrder = capturedOrder && sawLate && (position == 3 || sawEarly);
    orderedTwoLight = orderedTwoLight && capturedOrder;
    if (!capturedOrder)
      std::cerr << "two-light traversal capture failed at " << position
                << ": " << orderedDiagnostic << '\n';
#ifdef HAVE_COIN_BGFX
    int orderedBgfxDifference = 0;
    if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") && capturedOrder) {
      const bool gpuOk = bgfxLightDelta(root, secondShadowLight, orderedBgfxDifference) &&
        orderedBgfxDifference > 100;
      orderedTwoLight = orderedTwoLight && gpuOk;
      if (!gpuOk)
        std::cerr << "BGFX two-light traversal failed at " << position
                  << " delta=" << orderedBgfxDifference << '\n';
    }
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
    int orderedWgpuDifference = 0;
    if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU") && capturedOrder) {
      CoinRenderTarget * orderedTarget = CoinRenderTarget::createOffscreen(
        SbVec2i32(side, side));
      CoinRenderAction orderedAction(SbViewportRegion(side, side));
      orderedAction.setRenderTarget(orderedTarget);
      secondShadowLight->on = FALSE;
      orderedAction.apply(root);
      std::vector<unsigned char> singlePixels, dualPixels;
      const bool singleOk = orderedAction.getLastStatus() == CoinRenderAction::SUCCESS;
      if (singleOk) orderedTarget->readbackRGBA(singlePixels);
      secondShadowLight->on = TRUE;
      orderedAction.apply(root);
      const bool dualOk = orderedAction.getLastStatus() == CoinRenderAction::SUCCESS;
      if (dualOk) orderedTarget->readbackRGBA(dualPixels);
      if (singleOk && dualOk && singlePixels.size() == dualPixels.size())
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            orderedWgpuDifference = std::max(orderedWgpuDifference,
              std::abs(luminanceRgba(singlePixels, x, y) -
                       luminanceRgba(dualPixels, x, y)));
      const bool orderedGpuOk = singleOk && dualOk && orderedWgpuDifference > 100;
      orderedTwoLight = orderedTwoLight && orderedGpuOk;
      if (!orderedGpuOk)
        std::cerr << "wgpu two-light traversal failed at " << position << ": "
                  << orderedAction.getLastError().getString()
                  << " delta=" << orderedWgpuDifference << '\n';
      orderedAction.setRenderTarget(nullptr);
      delete orderedTarget;
    }
#endif
    if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
      auto * dualRoot = static_cast<SoSeparator *>(root->copy(TRUE));
      dualRoot->ref();
      auto * singleRoot = static_cast<SoSeparator *>(dualRoot->copy(TRUE));
      singleRoot->ref();
      auto * singleGroup = static_cast<SoShadowGroup *>(singleRoot->getChild(1));
      singleGroup->removeChild(position);
      SoOffscreenRenderer orderGl(SbViewportRegion(side, side));
      orderGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> dualPixels, singlePixels;
      const bool rendered = render(orderGl, dualRoot, dualPixels) &&
                            render(orderGl, singleRoot, singlePixels);
      int orderedGlDifference = 0;
      if (rendered)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            orderedGlDifference = std::max(orderedGlDifference,
              std::abs(luminance(dualPixels, x, y) -
                       luminance(singlePixels, x, y)));
      orderedTwoLight = orderedTwoLight && rendered && orderedGlDifference > 100;
#ifdef HAVE_COIN_BGFX
      if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU"))
        orderedTwoLight = orderedTwoLight &&
          std::abs(orderedGlDifference - orderedBgfxDifference) <= 100;
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
      if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU"))
        orderedTwoLight = orderedTwoLight &&
          std::abs(orderedGlDifference - orderedWgpuDifference) <= 100;
      std::cout << "two-light order " << position << " GL/wgpu delta="
                << orderedGlDifference << '/' << orderedWgpuDifference << '\n';
#endif
      singleRoot->unref();
      dualRoot->unref();
    }
  }
  light->ref();
  secondShadowLight->ref();
  group->removeChild(light);
  group->removeChild(secondShadowLight);
  group->addChild(light);
  group->addChild(secondShadowLight);
  light->unref();
  secondShadowLight->unref();
  action.apply(root);
  const auto & bothLateFrame = action.getPimpl()->lastRejectedShadowFrame;
  const auto & bothLatePlan = action.getPimpl()->lastRejectedShadowPlan;
  std::string bothLateDiagnostic;
  bool bothLateOk = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
    coin_render_shadow_spot_directional_opaque_profile(
      bothLateFrame, bothLatePlan, bothLateDiagnostic);
  if (bothLateOk)
    for (const auto & draw : bothLateFrame.draws) {
      const uint32_t slot = draw.renderStateSlot;
      bothLateOk = bothLateOk &&
        bothLatePlan.passes[0].lightingIndexByState[slot] == -1 &&
        bothLatePlan.passes[1].lightingIndexByState[slot] == -1;
    }
#ifdef HAVE_COIN_BGFX
  int bothLateBgfxDifference = 0;
  if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") && bothLateOk)
    bothLateOk = bgfxLightDelta(root, secondShadowLight, bothLateBgfxDifference) &&
      bothLateBgfxDifference > 100;
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  int bothLateWgpuDifference = 0;
  if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU") && bothLateOk) {
    CoinRenderTarget * lateTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    CoinRenderAction lateAction(SbViewportRegion(side, side));
    lateAction.setRenderTarget(lateTarget);
    secondShadowLight->on = FALSE;
    lateAction.apply(root);
    std::vector<unsigned char> singlePixels, dualPixels;
    const bool singleOk = lateAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (singleOk) lateTarget->readbackRGBA(singlePixels);
    secondShadowLight->on = TRUE;
    lateAction.apply(root);
    const bool dualOk = lateAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (dualOk) lateTarget->readbackRGBA(dualPixels);
    if (singleOk && dualOk && singlePixels.size() == dualPixels.size())
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          bothLateWgpuDifference = std::max(bothLateWgpuDifference,
            std::abs(luminanceRgba(singlePixels, x, y) -
                     luminanceRgba(dualPixels, x, y)));
    bothLateOk = bothLateOk && singleOk && dualOk &&
      bothLateWgpuDifference > 100;
    if (!bothLateOk)
      std::cerr << "wgpu two late lights failed: "
                << lateAction.getLastError().getString()
                << " delta=" << bothLateWgpuDifference << '\n';
    lateAction.setRenderTarget(nullptr);
    delete lateTarget;
  }
#endif
  if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
    auto * dualRoot = static_cast<SoSeparator *>(root->copy(TRUE));
    dualRoot->ref();
    auto * singleRoot = static_cast<SoSeparator *>(dualRoot->copy(TRUE));
    singleRoot->ref();
    auto * singleGroup = static_cast<SoShadowGroup *>(singleRoot->getChild(1));
    singleGroup->removeChild(3);
    SoOffscreenRenderer bothLateGl(SbViewportRegion(side, side));
    bothLateGl.setComponents(SoOffscreenRenderer::RGB);
    std::vector<unsigned char> dualPixels, singlePixels;
    const bool rendered = render(bothLateGl, dualRoot, dualPixels) &&
                          render(bothLateGl, singleRoot, singlePixels);
    int bothLateGlDifference = 0;
    if (rendered)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          bothLateGlDifference = std::max(bothLateGlDifference,
            std::abs(luminance(dualPixels, x, y) -
                     luminance(singlePixels, x, y)));
    bothLateOk = bothLateOk && rendered && bothLateGlDifference > 100;
#ifdef HAVE_COIN_BGFX
    if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU"))
      bothLateOk = bothLateOk &&
        std::abs(bothLateGlDifference - bothLateBgfxDifference) <= 100;
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
    if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU"))
      bothLateOk = bothLateOk &&
        std::abs(bothLateGlDifference - bothLateWgpuDifference) <= 100;
    std::cout << "two late lights GL/wgpu delta=" <<
      bothLateGlDifference << '/' << bothLateWgpuDifference << '\n';
#endif
    singleRoot->unref();
    dualRoot->unref();
  }
  orderedTwoLight = orderedTwoLight && bothLateOk;
  light->ref();
  secondShadowLight->ref();
  group->removeChild(light);
  group->removeChild(secondShadowLight);
  group->insertChild(light, 0);
  group->insertChild(secondShadowLight, 1);
  light->unref();
  secondShadowLight->unref();
  light->ref();
  secondShadowLight->ref();
  group->removeChild(light);
  group->removeChild(secondShadowLight);
  group->insertChild(secondShadowLight, 0);
  group->insertChild(light, 1);
  light->unref();
  secondShadowLight->unref();
  for (int spotPosition : {1, 2, 3}) {
    light->ref();
    group->removeChild(light);
    group->insertChild(light, spotPosition);
    light->unref();
    action.apply(root);
    const auto & reversedFrame = action.getPimpl()->lastRejectedShadowFrame;
    const auto & reversedPlan = action.getPimpl()->lastRejectedShadowPlan;
    std::string reversedDiagnostic;
    bool reversedOk = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      coin_render_shadow_spot_directional_opaque_profile(
        reversedFrame, reversedPlan, reversedDiagnostic) &&
      reversedFrame.shadowLights[reversedPlan.passes[0].lightSlot].type ==
        CoinRenderLightType::DIRECTIONAL &&
      reversedFrame.shadowLights[reversedPlan.passes[1].lightSlot].type ==
        CoinRenderLightType::SPOT;
    if (reversedOk)
      for (const auto & draw : reversedFrame.draws) {
        const uint32_t slot = draw.renderStateSlot;
        reversedOk = reversedOk &&
          reversedPlan.passes[0].lightingIndexByState[slot] == 0 &&
          (reversedPlan.passes[1].lightingIndexByState[slot] == 1 ||
           reversedPlan.passes[1].lightingIndexByState[slot] == -1);
      }
#ifdef HAVE_COIN_BGFX
    int reversedBgfxDifference = 0;
    if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") && reversedOk)
      reversedOk = bgfxLightDelta(root, light, reversedBgfxDifference) &&
        reversedBgfxDifference > 100;
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
    int reversedWgpuDifference = 0;
    if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU") && reversedOk) {
      CoinRenderTarget * reversedTarget = CoinRenderTarget::createOffscreen(
        SbVec2i32(side, side));
      CoinRenderAction reversedAction(SbViewportRegion(side, side));
      reversedAction.setRenderTarget(reversedTarget);
      light->on = FALSE;
      reversedAction.apply(root);
      std::vector<unsigned char> singlePixels, dualPixels;
      const bool singleOk = reversedAction.getLastStatus() == CoinRenderAction::SUCCESS;
      if (singleOk) reversedTarget->readbackRGBA(singlePixels);
      light->on = TRUE;
      reversedAction.apply(root);
      const bool dualOk = reversedAction.getLastStatus() == CoinRenderAction::SUCCESS;
      if (dualOk) reversedTarget->readbackRGBA(dualPixels);
      if (singleOk && dualOk && singlePixels.size() == dualPixels.size())
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            reversedWgpuDifference = std::max(reversedWgpuDifference,
              std::abs(luminanceRgba(singlePixels, x, y) -
                       luminanceRgba(dualPixels, x, y)));
      reversedOk = reversedOk && singleOk && dualOk &&
        reversedWgpuDifference > 100;
      if (!reversedOk)
        std::cerr << "wgpu reversed lights failed at " << spotPosition << ": "
                  << reversedAction.getLastError().getString()
                  << " delta=" << reversedWgpuDifference << '\n';
      reversedAction.setRenderTarget(nullptr);
      delete reversedTarget;
    }
#endif
    if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
      auto * dualRoot = static_cast<SoSeparator *>(root->copy(TRUE));
      dualRoot->ref();
      auto * singleRoot = static_cast<SoSeparator *>(dualRoot->copy(TRUE));
      singleRoot->ref();
      auto * singleGroup = static_cast<SoShadowGroup *>(singleRoot->getChild(1));
      singleGroup->removeChild(spotPosition);
      SoOffscreenRenderer reversedGl(SbViewportRegion(side, side));
      reversedGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> dualPixels, singlePixels;
      const bool rendered = render(reversedGl, dualRoot, dualPixels) &&
                            render(reversedGl, singleRoot, singlePixels);
      int reversedGlDifference = 0;
      if (rendered)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            reversedGlDifference = std::max(reversedGlDifference,
              std::abs(luminance(dualPixels, x, y) -
                       luminance(singlePixels, x, y)));
      reversedOk = reversedOk && rendered && reversedGlDifference > 100;
#ifdef HAVE_COIN_BGFX
      if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU"))
        reversedOk = reversedOk &&
          std::abs(reversedGlDifference - reversedBgfxDifference) <= 100;
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
      if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU"))
        reversedOk = reversedOk &&
          std::abs(reversedGlDifference - reversedWgpuDifference) <= 100;
      std::cout << "reversed lights " << spotPosition << " GL/wgpu delta="
                << reversedGlDifference << '/' << reversedWgpuDifference << '\n';
#endif
      singleRoot->unref();
      dualRoot->unref();
    }
    orderedTwoLight = orderedTwoLight && reversedOk;
  }
  light->ref();
  secondShadowLight->ref();
  group->removeChild(light);
  group->removeChild(secondShadowLight);
  group->insertChild(light, 0);
  group->insertChild(secondShadowLight, 1);
  light->unref();
  secondShadowLight->unref();
  group->removeChild(secondShadowLight);
  group->quality = previousQuality;
  auto verifySameTypePair = [&](SoSeparator * pairRoot, SoLight * second,
                                const char * label) -> bool {
    pairRoot->ref();
    auto * pairGroup = static_cast<SoShadowGroup *>(pairRoot->getChild(1));
    pairGroup->quality = 1.0f;
    pairGroup->insertChild(second, 1);
    action.apply(pairRoot);
    const auto & pairFrame = action.getPimpl()->lastRejectedShadowFrame;
    const auto & pairPlan = action.getPimpl()->lastRejectedShadowPlan;
    std::string pairDiagnostic;
    bool pairOk = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      coin_render_shadow_two_opaque_profile(pairFrame, pairPlan, pairDiagnostic) &&
      pairPlan.passes.size() == 2;
#ifdef HAVE_COIN_BGFX
    int bgfxDifference = 0;
    if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") && pairOk)
      pairOk = bgfxLightDelta(pairRoot, second, bgfxDifference) && bgfxDifference > 80;
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
    int wgpuDifference = 0;
    if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU") && pairOk) {
      CoinRenderTarget * pairTarget = CoinRenderTarget::createOffscreen(
        SbVec2i32(side, side));
      CoinRenderAction pairAction(SbViewportRegion(side, side));
      pairAction.setRenderTarget(pairTarget);
      second->on = FALSE;
      pairAction.apply(pairRoot);
      std::vector<unsigned char> singlePixels, dualPixels;
      const bool singleOk = pairAction.getLastStatus() == CoinRenderAction::SUCCESS;
      if (singleOk) pairTarget->readbackRGBA(singlePixels);
      second->on = TRUE;
      pairAction.apply(pairRoot);
      const bool dualOk = pairAction.getLastStatus() == CoinRenderAction::SUCCESS;
      if (dualOk) pairTarget->readbackRGBA(dualPixels);
      if (singleOk && dualOk && singlePixels.size() == dualPixels.size())
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            wgpuDifference = std::max(wgpuDifference,
              std::abs(luminanceRgba(singlePixels, x, y) -
                       luminanceRgba(dualPixels, x, y)));
      pairOk = pairOk && singleOk && dualOk && wgpuDifference > 80;
      if (!pairOk)
        std::cerr << "wgpu " << label << " failed: "
                  << pairAction.getLastError().getString()
                  << " delta=" << wgpuDifference << '\n';
      pairAction.setRenderTarget(nullptr);
      delete pairTarget;
    }
#endif
    if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
      auto * singleRoot = static_cast<SoSeparator *>(pairRoot->copy(TRUE));
      singleRoot->ref();
      auto * singleGroup = static_cast<SoShadowGroup *>(singleRoot->getChild(1));
      singleGroup->removeChild(1);
      SoOffscreenRenderer pairGl(SbViewportRegion(side, side));
      pairGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> dualPixels, singlePixels;
      const bool rendered = render(pairGl, pairRoot, dualPixels) &&
                            render(pairGl, singleRoot, singlePixels);
      int glDifference = 0;
      if (rendered)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            glDifference = std::max(glDifference,
              std::abs(luminance(dualPixels, x, y) -
                       luminance(singlePixels, x, y)));
      pairOk = pairOk && rendered && glDifference > 80;
#ifdef HAVE_COIN_BGFX
      if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU"))
        pairOk = pairOk && std::abs(glDifference - bgfxDifference) <= 120;
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
      if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU"))
        pairOk = pairOk && std::abs(glDifference - wgpuDifference) <= 120;
      std::cout << label << " GL/wgpu delta=" << glDifference << '/'
                << wgpuDifference << '\n';
#endif
      singleRoot->unref();
    }
    if (!pairOk)
      std::cerr << label << " profile: " << pairDiagnostic << '\n';
    pairRoot->unref();
    return pairOk;
  };
  auto * twoSpotsRoot = static_cast<SoSeparator *>(root->copy(TRUE));
  auto * extraSpot = new SoShadowSpotLight;
  extraSpot->location.setValue(-2.0f, 2.0f, 4.0f);
  extraSpot->direction.setValue(2.0f, -2.0f, -5.0f);
  extraSpot->cutOffAngle = 0.9f;
  extraSpot->intensity = 0.6f;
  const bool twoSpotsOk = verifySameTypePair(twoSpotsRoot, extraSpot, "two spots");
  auto * twoDirectionalsRoot = static_cast<SoSeparator *>(root->copy(TRUE));
  auto * twoDirectionalsGroup = static_cast<SoShadowGroup *>(
    twoDirectionalsRoot->getChild(1));
  auto * firstDirectional = new SoShadowDirectionalLight;
  firstDirectional->direction.setValue(-0.4f, -0.4f, -1.0f);
  firstDirectional->intensity = 1.0f;
  twoDirectionalsGroup->replaceChild(0, firstDirectional);
  auto * extraDirectional = new SoShadowDirectionalLight;
  extraDirectional->direction.setValue(0.4f, -0.4f, -1.0f);
  extraDirectional->intensity = 0.6f;
  extraDirectional->maxShadowDistance = 10.0f;
  const bool twoDirectionalsOk = verifySameTypePair(
    twoDirectionalsRoot, extraDirectional, "two directionals");
  target->readbackRGBA(afterRejection);
  const bool preserved = target->getLastSubmissionSerial() == originalSerial &&
                         !published.empty() && published == afterRejection;
  light->ref();
  group->removeChild(light);
  group->addChild(light);
  light->unref();
  action.apply(root);
  const auto & lateLightFrame = action.getPimpl()->lastRejectedShadowFrame;
  const auto & lateLightPlan = action.getPimpl()->lastRejectedShadowPlan;
  bool lateLightResolved = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      lateLightFrame.shadowLights.size() == 1 && lateLightPlan.passes.size() == 1;
  std::string lateProfileDiagnostic;
  const bool lateSpotProfile =
      coin_render_shadow_single_spot_opaque_profile(
        lateLightFrame, lateLightPlan, lateProfileDiagnostic);
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  std::vector<unsigned char> lateSpotPixels, lateSpotNoReceivePixels;
  std::vector<unsigned char> mixedSpotPixels, mixedSpotNoReceivePixels;
  bool lateSpotSubmitted = true;
  if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU") && lateSpotProfile) {
    CoinRenderTarget * lateTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    CoinRenderAction lateAction(SbViewportRegion(side, side));
    lateAction.setRenderTarget(lateTarget);
    lateAction.apply(root);
    lateSpotSubmitted = lateAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (lateSpotSubmitted) lateTarget->readbackRGBA(lateSpotPixels);
    else std::cerr << "wgpu late spot: " << lateAction.getLastError().getString() << '\n';
    groundStyle->style = SoShadowStyle::NO_SHADOWING;
    lateAction.apply(root);
    lateSpotSubmitted = lateSpotSubmitted &&
      lateAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (lateSpotSubmitted) lateTarget->readbackRGBA(lateSpotNoReceivePixels);
    groundStyle->style = SoShadowStyle::SHADOWED;
    light->ref();
    group->removeChild(light);
    group->insertChild(light, 1); // Light follows the caster but precedes the floor.
    light->unref();
    lateAction.apply(root);
    lateSpotSubmitted = lateSpotSubmitted &&
      lateAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (lateSpotSubmitted) lateTarget->readbackRGBA(mixedSpotPixels);
    groundStyle->style = SoShadowStyle::NO_SHADOWING;
    lateAction.apply(root);
    lateSpotSubmitted = lateSpotSubmitted &&
      lateAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (lateSpotSubmitted) lateTarget->readbackRGBA(mixedSpotNoReceivePixels);
    groundStyle->style = SoShadowStyle::SHADOWED;
    light->ref();
    group->removeChild(light);
    group->addChild(light);
    light->unref();
    lateAction.setRenderTarget(nullptr);
    delete lateTarget;
  }
#endif
  if (lateLightResolved) {
    for (uint32_t d : lateLightPlan.passes[0].receiverDraws) {
      const uint32_t stateSlot = lateLightFrame.draws[d].renderStateSlot;
      lateLightResolved = lateLightResolved &&
          lateLightPlan.passes[0].lightingIndexByState[stateSlot] == -1 &&
          lateLightPlan.passes[0].resolvedLightByState[stateSlot].sourceRevision ==
            lateLightFrame.shadowLights[0].sourceRevision;
    }
  }
  light->ref();
  group->removeChild(light);
  group->insertChild(light, 1);
  light->unref();
  action.apply(root);
  const auto & mixedOrderFrame = action.getPimpl()->lastRejectedShadowFrame;
  const auto & mixedOrderPlan = action.getPimpl()->lastRejectedShadowPlan;
  bool mixedOrderResolved = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
    mixedOrderPlan.passes.size() == 1 && mixedOrderFrame.draws.size() >= 2;
  if (mixedOrderResolved) {
    const auto & indices = mixedOrderPlan.passes[0].lightingIndexByState;
    const uint32_t casterState = mixedOrderFrame.draws[0].renderStateSlot;
    const uint32_t floorState = mixedOrderFrame.draws[1].renderStateSlot;
    mixedOrderResolved = casterState < indices.size() && floorState < indices.size() &&
      indices[casterState] == -1 && indices[floorState] == 0;
  }
  light->ref();
  group->removeChild(light);
  group->insertChild(light, 0);
  light->unref();
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
      directionalPlan.passes[0].visible &&
      !directionalPlan.passes[0].perFragmentLighting;
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
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  bool wgpuDirectionalSubmitted = true;
  int wgpuDirectionalDifference = 0;
  int wgpuLateDirectionalDifference = 0;
  int wgpuMixedDirectionalDifference = 0;
  std::vector<unsigned char> wgpuDistancePixels, wgpuDirectionalFullPixels;
  if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU")) {
    group->quality = 1.0f; // Coin's directional per-fragment profile.
    action.apply(root);
    std::string lateDirectionalDiagnostic;
    const bool lateDirectionalProfile =
      action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      coin_render_shadow_single_directional_opaque_profile(
        action.getPimpl()->lastRejectedShadowFrame,
        action.getPimpl()->lastRejectedShadowPlan, lateDirectionalDiagnostic);
    wgpuDirectionalSubmitted = wgpuDirectionalSubmitted && lateDirectionalProfile;
    if (wgpuDirectionalSubmitted) {
      CoinRenderTarget * lateTarget = CoinRenderTarget::createOffscreen(
        SbVec2i32(side, side));
      CoinRenderAction lateAction(SbViewportRegion(side, side));
      lateAction.setRenderTarget(lateTarget);
      lateAction.apply(root);
      std::vector<unsigned char> lateShadow, lateNoReceive;
      if (lateAction.getLastStatus() == CoinRenderAction::SUCCESS)
        lateTarget->readbackRGBA(lateShadow);
      groundStyle->style = SoShadowStyle::NO_SHADOWING;
      lateAction.apply(root);
      if (lateAction.getLastStatus() == CoinRenderAction::SUCCESS)
        lateTarget->readbackRGBA(lateNoReceive);
      groundStyle->style = SoShadowStyle::SHADOWED;
      wgpuDirectionalSubmitted = lateAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        !lateShadow.empty() && lateShadow.size() == lateNoReceive.size();
      int lateDirectionalDelta = 0;
      if (wgpuDirectionalSubmitted)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x) {
            const size_t pixel = static_cast<size_t>((y * side + x) * 4);
            const int lit = lateShadow[pixel] + lateShadow[pixel + 1] + lateShadow[pixel + 2];
            const int dark = lateNoReceive[pixel] + lateNoReceive[pixel + 1] + lateNoReceive[pixel + 2];
            lateDirectionalDelta = std::max(lateDirectionalDelta, lit - dark);
          }
      std::cout << "wgpu late directional style delta=" << lateDirectionalDelta << '\n';
      wgpuLateDirectionalDifference = lateDirectionalDelta;
      wgpuDirectionalSubmitted = wgpuDirectionalSubmitted && lateDirectionalDelta > 100;
      if (!wgpuDirectionalSubmitted)
        std::cerr << "wgpu late directional style check: "
                  << lateAction.getLastError().getString() << '\n';
      directionalCapture->ref();
      group->removeChild(directionalCapture);
      group->insertChild(directionalCapture, 2); // Between caster and floor.
      directionalCapture->unref();
      lateAction.apply(root);
      std::vector<unsigned char> mixedShadow, mixedNoReceive;
      if (lateAction.getLastStatus() == CoinRenderAction::SUCCESS)
        lateTarget->readbackRGBA(mixedShadow);
      groundStyle->style = SoShadowStyle::NO_SHADOWING;
      lateAction.apply(root);
      if (lateAction.getLastStatus() == CoinRenderAction::SUCCESS)
        lateTarget->readbackRGBA(mixedNoReceive);
      groundStyle->style = SoShadowStyle::SHADOWED;
      wgpuDirectionalSubmitted = wgpuDirectionalSubmitted &&
        lateAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        !mixedShadow.empty() && mixedShadow.size() == mixedNoReceive.size();
      if (wgpuDirectionalSubmitted)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x) {
            const size_t pixel = static_cast<size_t>((y * side + x) * 4);
            const int lit = mixedShadow[pixel] + mixedShadow[pixel + 1] + mixedShadow[pixel + 2];
            const int dark = mixedNoReceive[pixel] + mixedNoReceive[pixel + 1] + mixedNoReceive[pixel + 2];
            wgpuMixedDirectionalDifference = std::max(wgpuMixedDirectionalDifference,
                                                      std::abs(lit - dark));
          }
      directionalCapture->ref();
      group->removeChild(directionalCapture);
      group->addChild(directionalCapture);
      directionalCapture->unref();
      lateAction.setRenderTarget(nullptr);
      delete lateTarget;
    }
    directionalCapture->ref();
    group->removeChild(directionalCapture);
    group->insertChild(directionalCapture, 1); // Light before both shapes.
    directionalCapture->unref();
    action.apply(root); // CPU target keeps the captured plan for direct comparison.
    const CoinRenderFramePlan & directionalGpuFrame =
      action.getPimpl()->lastRejectedShadowFrame;
    const CoinRenderShadowPlan & directionalGpuPlan =
      action.getPimpl()->lastRejectedShadowPlan;
    std::string directionalProfileDiagnostic;
    wgpuDirectionalSubmitted = wgpuDirectionalSubmitted &&
      action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      coin_render_shadow_single_directional_opaque_profile(
        directionalGpuFrame, directionalGpuPlan, directionalProfileDiagnostic);
    if (!wgpuDirectionalSubmitted)
      std::cerr << "directional profile: " << directionalProfileDiagnostic << '\n';
    if (wgpuDirectionalSubmitted) {
      CoinRenderTarget * directionalTarget = CoinRenderTarget::createOffscreen(
        SbVec2i32(side, side));
      directionalTarget->getPimpl()->backend.reset(new CoinWgpuBackend);
      CoinRenderFramePlan unshadowed = directionalGpuFrame;
      unshadowed.revision = 0;
      unshadowed.shadowGroups.clear();
      unshadowed.shadowLights.clear();
      for (auto & state : unshadowed.renderStates) state.shadowGroupSlot = 0;
      const auto base = directionalTarget->getPimpl()->executeFrame(unshadowed);
      std::vector<unsigned char> basePixels, shadowPixels;
      if (base.status == CoinRenderBackendStatus::SUCCESS)
        directionalTarget->readbackRGBA(basePixels);
      const auto shadow = directionalTarget->getPimpl()->executeFrame(
        directionalGpuFrame);
      if (shadow.status == CoinRenderBackendStatus::SUCCESS)
        directionalTarget->readbackRGBA(shadowPixels);
      wgpuDirectionalSubmitted = base.status == CoinRenderBackendStatus::SUCCESS &&
        shadow.status == CoinRenderBackendStatus::SUCCESS &&
        basePixels.size() == shadowPixels.size() && !basePixels.empty();
      if (!wgpuDirectionalSubmitted)
        std::cerr << "wgpu directional submit: " << base.diagnostic << " / "
                  << shadow.diagnostic << '\n';
      if (wgpuDirectionalSubmitted) {
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x) {
            const size_t pixel = static_cast<size_t>((y * side + x) * 4);
            const int clear = basePixels[pixel] + basePixels[pixel + 1] +
                              basePixels[pixel + 2];
            const int dark = shadowPixels[pixel] + shadowPixels[pixel + 1] +
                             shadowPixels[pixel + 2];
            wgpuDirectionalDifference = std::max(wgpuDirectionalDifference,
                                                 clear - dark);
          }
        wgpuDirectionalSubmitted = wgpuDirectionalDifference > 100;
        std::cout << "wgpu directional max_delta=" << wgpuDirectionalDifference << '\n';
        CoinRenderTarget * directionalActionTarget =
          CoinRenderTarget::createOffscreen(SbVec2i32(side, side));
        CoinRenderAction directionalAction(SbViewportRegion(side, side));
        directionalAction.setRenderTarget(directionalActionTarget);
        directionalAction.apply(root);
        std::vector<unsigned char> actionPixels;
        if (directionalAction.getLastStatus() == CoinRenderAction::SUCCESS)
          directionalActionTarget->readbackRGBA(actionPixels);
        wgpuDirectionalSubmitted = wgpuDirectionalSubmitted &&
          directionalAction.getLastStatus() == CoinRenderAction::SUCCESS &&
          actionPixels == shadowPixels;
        wgpuDirectionalFullPixels = actionPixels;
        if (!wgpuDirectionalSubmitted)
          std::cerr << "wgpu directional Action: "
                    << directionalAction.getLastError().getString() << '\n';
        directionalCapture->maxShadowDistance = 10.0f;
        action.apply(root);
        const auto & distancePlan = action.getPimpl()->lastRejectedShadowPlan;
        wgpuDirectionalSubmitted = wgpuDirectionalSubmitted &&
          action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
          distancePlan.passes.size() == 1 && distancePlan.passes[0].visible &&
          distancePlan.passes[0].maxShadowDistance == 10.0f &&
          distancePlan.passes[0].distanceFalloffCoefficient == 2.35f;
        directionalAction.apply(root);
        wgpuDirectionalSubmitted = wgpuDirectionalSubmitted &&
          directionalAction.getLastStatus() == CoinRenderAction::SUCCESS;
        if (wgpuDirectionalSubmitted)
          directionalActionTarget->readbackRGBA(wgpuDistancePixels);
        else
          std::cerr << "wgpu directional max distance: "
                    << directionalAction.getLastError().getString() << '\n';
        const uint64_t distanceSerial = directionalActionTarget->getLastSubmissionSerial();
        directionalCapture->maxShadowDistance = 0.5f; // Before the camera near plane.
        directionalAction.apply(root);
        std::vector<unsigned char> afterInvisibleDistance;
        directionalActionTarget->readbackRGBA(afterInvisibleDistance);
        wgpuDirectionalSubmitted = wgpuDirectionalSubmitted &&
          directionalAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
          directionalActionTarget->getLastSubmissionSerial() == distanceSerial &&
          afterInvisibleDistance == wgpuDistancePixels;
        directionalCapture->maxShadowDistance = -1.0f;
        directionalAction.setRenderTarget(nullptr);
        delete directionalActionTarget;
      }
      delete directionalTarget;
    }
  }
#endif
  directionalCapture->ref();
  group->removeChild(directionalCapture);
  group->insertChild(directionalCapture, 1);
  directionalCapture->unref();
  const float previousDirectionalQuality = group->quality.getValue();
  group->quality = 1.0f;
  directionalCapture->maxShadowDistance = 10.0f;
  action.apply(root);
  const auto & entryFrame = action.getPimpl()->lastRejectedShadowFrame;
  const auto & entryPlan = action.getPimpl()->lastRejectedShadowPlan;
  bool multipleCameras = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
    entryPlan.passes.size() == 1 && entryFrame.shadowGroups.size() == 1 &&
    entryFrame.shadowGroups[0].hasEntryCamera;
  SbMatrix entryView = SbMatrix::identity();
  SbMatrix entryProjection = SbMatrix::identity();
  if (multipleCameras) {
    entryView = entryPlan.passes[0].view;
    entryProjection = entryPlan.passes[0].projectionCoin;
  }
  auto * innerCamera = new SoOrthographicCamera;
  innerCamera->position.setValue(0.35f, 0.0f, 8.0f);
  innerCamera->height = 7.0f;
  innerCamera->nearDistance = 1.0f;
  innerCamera->farDistance = 20.0f;
  group->insertChild(innerCamera, 3); // After caster, before floor.
  action.apply(root);
  const auto & cameraFrame = action.getPimpl()->lastRejectedShadowFrame;
  const auto & cameraPlan = action.getPimpl()->lastRejectedShadowPlan;
  std::string cameraDiagnostic;
  multipleCameras = multipleCameras &&
    action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
    coin_render_shadow_single_directional_opaque_profile(
      cameraFrame, cameraPlan, cameraDiagnostic) &&
    cameraFrame.cameras.size() >= 2 &&
    cameraPlan.passes[0].view == entryView &&
    cameraPlan.passes[0].projectionCoin == entryProjection;
  if (multipleCameras) {
    bool sawDifferentDrawCameras = false;
    const uint32_t firstCamera = cameraFrame.renderStates[
      cameraFrame.draws.front().renderStateSlot].cameraSlot;
    for (const auto & draw : cameraFrame.draws)
      sawDifferentDrawCameras = sawDifferentDrawCameras ||
        cameraFrame.renderStates[draw.renderStateSlot].cameraSlot != firstCamera;
    multipleCameras = sawDifferentDrawCameras;
  }
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  int multiCameraWgpuDifference = 0;
  if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU") && multipleCameras) {
    CoinRenderTarget * cameraTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    CoinRenderAction cameraAction(SbViewportRegion(side, side));
    cameraAction.setRenderTarget(cameraTarget);
    cameraAction.apply(root);
    std::vector<unsigned char> shadowedPixels, litPixels;
    const bool shadowedOk = cameraAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (shadowedOk) cameraTarget->readbackRGBA(shadowedPixels);
    groundStyle->style = SoShadowStyle::NO_SHADOWING;
    cameraAction.apply(root);
    const bool litOk = cameraAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (litOk) cameraTarget->readbackRGBA(litPixels);
    groundStyle->style = SoShadowStyle::SHADOWED;
    if (shadowedOk && litOk && shadowedPixels.size() == litPixels.size())
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          multiCameraWgpuDifference = std::max(multiCameraWgpuDifference,
            std::abs(luminanceRgba(shadowedPixels, x, y) -
                     luminanceRgba(litPixels, x, y)));
    multipleCameras = multipleCameras && shadowedOk && litOk &&
      multiCameraWgpuDifference > 50;
    if (!multipleCameras)
      std::cerr << "wgpu multi-camera directional shadow failed: "
                << cameraAction.getLastError().getString()
                << " delta=" << multiCameraWgpuDifference << '\n';
    cameraAction.setRenderTarget(nullptr);
    delete cameraTarget;
  }
#endif
  if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
    auto * shadowedRoot = static_cast<SoSeparator *>(root->copy(TRUE));
    shadowedRoot->ref();
    auto * litRoot = static_cast<SoSeparator *>(shadowedRoot->copy(TRUE));
    litRoot->ref();
    auto * litGroup = static_cast<SoShadowGroup *>(litRoot->getChild(1));
    auto * litGround = static_cast<SoSeparator *>(litGroup->getChild(4));
    static_cast<SoShadowStyle *>(litGround->getChild(0))->style =
      SoShadowStyle::NO_SHADOWING;
    SoOffscreenRenderer multiCameraGl(SbViewportRegion(side, side));
    multiCameraGl.setComponents(SoOffscreenRenderer::RGB);
    std::vector<unsigned char> shadowedPixels, litPixels;
    const bool rendered = render(multiCameraGl, shadowedRoot, shadowedPixels) &&
                          render(multiCameraGl, litRoot, litPixels);
    int multiCameraGlDifference = 0;
    if (rendered)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          multiCameraGlDifference = std::max(multiCameraGlDifference,
            std::abs(luminance(shadowedPixels, x, y) -
                     luminance(litPixels, x, y)));
    multipleCameras = multipleCameras && rendered && multiCameraGlDifference > 50;
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
    if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU"))
      multipleCameras = multipleCameras &&
        std::abs(multiCameraGlDifference - multiCameraWgpuDifference) <= 80;
    std::cout << "multi-camera directional GL/wgpu delta=" <<
      multiCameraGlDifference << '/' << multiCameraWgpuDifference << '\n';
#endif
    litRoot->unref();
    shadowedRoot->unref();
  }
  group->removeChild(innerCamera);
  auto * perspectiveRoot = static_cast<SoSeparator *>(root->copy(TRUE));
  perspectiveRoot->ref();
  auto * perspectiveCamera = new SoPerspectiveCamera;
  perspectiveCamera->position.setValue(0.0f, 0.0f, 8.0f);
  perspectiveCamera->heightAngle = 0.85f;
  perspectiveCamera->nearDistance = 1.0f;
  perspectiveCamera->farDistance = 20.0f;
  perspectiveRoot->replaceChild(0, perspectiveCamera);
  action.apply(perspectiveRoot);
  const auto & perspectiveFrame = action.getPimpl()->lastRejectedShadowFrame;
  const auto & perspectivePlan = action.getPimpl()->lastRejectedShadowPlan;
  std::string perspectiveDiagnostic;
  bool perspectiveFrustum = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
    perspectiveFrame.shadowGroups.size() == 1 &&
    perspectiveFrame.shadowGroups[0].hasEntryCamera &&
    perspectiveFrame.shadowGroups[0].entryCamera.isPerspective &&
    coin_render_shadow_single_directional_opaque_profile(
      perspectiveFrame, perspectivePlan, perspectiveDiagnostic) &&
    perspectivePlan.passes[0].visible;
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  int perspectiveWgpuDifference = 0;
  if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU") && perspectiveFrustum) {
    CoinRenderTarget * perspectiveTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    CoinRenderAction perspectiveAction(SbViewportRegion(side, side));
    perspectiveAction.setRenderTarget(perspectiveTarget);
    perspectiveAction.apply(perspectiveRoot);
    std::vector<unsigned char> shadowedPixels, litPixels;
    const bool shadowedOk = perspectiveAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (shadowedOk) perspectiveTarget->readbackRGBA(shadowedPixels);
    auto * perspectiveGroup = static_cast<SoShadowGroup *>(perspectiveRoot->getChild(1));
    auto * perspectiveGround = static_cast<SoSeparator *>(perspectiveGroup->getChild(3));
    auto * perspectiveStyle = static_cast<SoShadowStyle *>(perspectiveGround->getChild(0));
    perspectiveStyle->style = SoShadowStyle::NO_SHADOWING;
    perspectiveAction.apply(perspectiveRoot);
    const bool litOk = perspectiveAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (litOk) perspectiveTarget->readbackRGBA(litPixels);
    perspectiveStyle->style = SoShadowStyle::SHADOWED;
    if (shadowedOk && litOk && shadowedPixels.size() == litPixels.size())
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          perspectiveWgpuDifference = std::max(perspectiveWgpuDifference,
            std::abs(luminanceRgba(shadowedPixels, x, y) -
                     luminanceRgba(litPixels, x, y)));
    perspectiveFrustum = perspectiveFrustum && shadowedOk && litOk &&
      perspectiveWgpuDifference > 50;
    if (!perspectiveFrustum)
      std::cerr << "wgpu perspective directional shadow failed: "
                << perspectiveAction.getLastError().getString()
                << " delta=" << perspectiveWgpuDifference << '\n';
    perspectiveAction.setRenderTarget(nullptr);
    delete perspectiveTarget;
  }
#endif
  if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
    auto * litRoot = static_cast<SoSeparator *>(perspectiveRoot->copy(TRUE));
    litRoot->ref();
    auto * litGroup = static_cast<SoShadowGroup *>(litRoot->getChild(1));
    auto * litGround = static_cast<SoSeparator *>(litGroup->getChild(3));
    static_cast<SoShadowStyle *>(litGround->getChild(0))->style =
      SoShadowStyle::NO_SHADOWING;
    SoOffscreenRenderer perspectiveGl(SbViewportRegion(side, side));
    perspectiveGl.setComponents(SoOffscreenRenderer::RGB);
    std::vector<unsigned char> shadowedPixels, litPixels;
    const bool rendered = render(perspectiveGl, perspectiveRoot, shadowedPixels) &&
                          render(perspectiveGl, litRoot, litPixels);
    int perspectiveGlDifference = 0;
    if (rendered)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          perspectiveGlDifference = std::max(perspectiveGlDifference,
            std::abs(luminance(shadowedPixels, x, y) -
                     luminance(litPixels, x, y)));
    perspectiveFrustum = perspectiveFrustum && rendered && perspectiveGlDifference > 50;
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
    if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU"))
      perspectiveFrustum = perspectiveFrustum &&
        std::abs(perspectiveGlDifference - perspectiveWgpuDifference) <= 100;
    std::cout << "perspective directional GL/wgpu delta=" <<
      perspectiveGlDifference << '/' << perspectiveWgpuDifference << '\n';
#endif
    litRoot->unref();
  }
  perspectiveRoot->unref();
  directionalCapture->maxShadowDistance = -1.0f;
  group->quality = previousDirectionalQuality;
  group->removeChild(directionalCapture);
  light->on = TRUE;
  action.setRenderTarget(nullptr);
  delete target;
  if (!publishedOk || !rejected || !preserved || !recovered || !captureOk ||
      !spotProfile || !transparentExcludedFromFirstProfile || !twoLightCaptured ||
      !orderedTwoLight || !twoSpotsOk || !twoDirectionalsOk ||
      !lateLightResolved || !lateSpotProfile || !mixedOrderResolved ||
      !lightEligibility || !spotRangeCaptured || !directionalCaptured ||
      !directionalProjectionCoversGroup || !multipleCameras || !perspectiveFrustum
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
      || !wgpuShadowSubmitted || !wgpuDirectionalSubmitted || !lateSpotSubmitted ||
      !twoLightSubmittedOnGpu
#endif
      ) {
    std::cerr << "CoinRender shadow rejection did not preserve publication or recovery"
              << " profile=" << spotProfile << " (" << spotProfileDiagnostic << ")"
              << " two_light=" << twoLightCaptured << " (" << twoLightDiagnostic << ")"
              << " ordered=" << orderedTwoLight
              << " two_spots=" << twoSpotsOk
              << " two_directionals=" << twoDirectionalsOk
              << " multi_camera=" << multipleCameras
              << " perspective=" << perspectiveFrustum
              << " late_profile=" << lateSpotProfile
              << " (" << lateProfileDiagnostic << ")\n";
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
#ifdef HAVE_COIN_BGFX
  if (ok && std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU")) {
    const int bgfxSpot = luminanceRgba(bgfxShadowImage, 50, side - 1 - 50);
    const int glSpot = luminance(active, 50, 50);
    ok = ok && std::abs(bgfxSpot - glSpot) <= 130;
    if (!ok)
      std::cerr << "BGFX spot sample differs from Coin/GL: " << bgfxSpot
                << '/' << glSpot << '\n';
  }
#endif
  light->ref();
  group->removeChild(light);
  group->addChild(light);
  light->unref();
  std::vector<unsigned char> lateLightActive, lateLightNoReceive;
  ok = ok && render(gl, root, lateLightActive);
  groundStyle->style = SoShadowStyle::NO_SHADOWING;
  ok = ok && render(gl, root, lateLightNoReceive);
  groundStyle->style = SoShadowStyle::SHADOWED;
  light->ref();
  group->removeChild(light);
  group->insertChild(light, 1);
  light->unref();
  std::vector<unsigned char> mixedLightActive, mixedLightNoReceive;
  ok = ok && render(gl, root, mixedLightActive);
  groundStyle->style = SoShadowStyle::NO_SHADOWING;
  ok = ok && render(gl, root, mixedLightNoReceive);
  groundStyle->style = SoShadowStyle::SHADOWED;
  light->ref();
  group->removeChild(light);
  group->insertChild(light, 0);
  light->unref();
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
  std::vector<unsigned char> directionalShadow, directionalNoReceive, directionalDistance;
  ok = ok && render(gl, root, directionalShadow);
  auto * distanceRoot = static_cast<SoSeparator *>(root->copy(TRUE));
  distanceRoot->ref();
  auto * distanceGroup = static_cast<SoShadowGroup *>(distanceRoot->getChild(1));
  auto * distanceDirectional = static_cast<SoShadowDirectionalLight *>(
    distanceGroup->getChild(0));
  distanceDirectional->maxShadowDistance = 10.0f;
  SoOffscreenRenderer distanceGl(SbViewportRegion(side, side));
  distanceGl.setComponents(SoOffscreenRenderer::RGB);
  ok = ok && render(distanceGl, distanceRoot, directionalDistance);
  distanceRoot->unref();
  groundStyle->style = SoShadowStyle::NO_SHADOWING;
  ok = ok && render(gl, root, directionalNoReceive);
  groundStyle->style = SoShadowStyle::SHADOWED;
  directional->ref();
  group->removeChild(directional);
  group->insertChild(directional, 1);
  directional->unref();
  std::vector<unsigned char> mixedDirectionalShadow, mixedDirectionalNoReceive;
  ok = ok && render(gl, root, mixedDirectionalShadow);
  groundStyle->style = SoShadowStyle::NO_SHADOWING;
  ok = ok && render(gl, root, mixedDirectionalNoReceive);
  groundStyle->style = SoShadowStyle::SHADOWED;
  directional->ref();
  group->removeChild(directional);
  group->addChild(directional);
  directional->unref();
  std::vector<unsigned char> lateDirectionalShadow, lateDirectionalNoReceive;
  ok = ok && render(gl, root, lateDirectionalShadow);
  groundStyle->style = SoShadowStyle::NO_SHADOWING;
  ok = ok && render(gl, root, lateDirectionalNoReceive);
  root->unref();
  if (!ok) {
    std::cerr << "Coin/GL could not render the shadow reference\n";
    return 1;
  }

  const int clearShadow = luminance(inactive, 50, 50);
  const int castShadow = luminance(active, 50, 50);
  const int lateLightShadow = luminance(lateLightActive, 50, 50);
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  if (!lateSpotPixels.empty()) {
    const size_t pixel = static_cast<size_t>(((side - 1 - 50) * side + 50) * 4);
    const int lateWgpu = lateSpotPixels[pixel] + lateSpotPixels[pixel + 1] +
                         lateSpotPixels[pixel + 2];
    std::cout << "wgpu late spot sample=" << lateWgpu << '\n';
    if (std::abs(lateWgpu - lateLightShadow) > 45) {
      std::cerr << "wgpu late spot differs from Coin/GL reference\n";
      return 1;
    }
  }
#endif
  const int noReceive = luminance(unshadowed, 50, 50);
  const int restoredShadow = luminance(activeAgain, 50, 50);
  const int withoutCaster = luminance(noCaster, 50, 50);
  const int centerDifference = std::abs(luminance(inactive, 64, 64) -
                                        luminance(active, 64, 64));
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  if (!wgpuShadow.empty() && !wgpuUnshadowed.empty()) {
    // SoOffscreenRenderer returns its GL rows bottom-up; wgpu readback is top-down.
    const size_t sample = static_cast<size_t>(((side - 1 - 50) * side + 50) * 4);
    const int wgpuShadowLuma = wgpuShadow[sample] + wgpuShadow[sample + 1] +
                               wgpuShadow[sample + 2];
    const int wgpuClearLuma = wgpuUnshadowed[sample] + wgpuUnshadowed[sample + 1] +
                              wgpuUnshadowed[sample + 2];
    std::cout << "wgpu spot sample unshadowed=" << wgpuClearLuma
              << " shadowed=" << wgpuShadowLuma << '\n';
    if (wgpuClearLuma - wgpuShadowLuma < 100 ||
        std::abs(wgpuShadowLuma - castShadow) > 130) {
      std::cerr << "wgpu spot did not match the Coin/GL shadow relation\n";
      return 1;
    }
  }
#endif
  std::cout << "Coin/GL shadow sample inactive=" << clearShadow
            << " active=" << castShadow
            << " late_light=" << lateLightShadow
            << " no_receive=" << noReceive
            << " restored=" << restoredShadow
            << " no_caster=" << withoutCaster
            << " center_delta=" << centerDifference << '\n';
  int directionalDistanceDifference = 0;
  for (int y = 20; y < 105; ++y)
    for (int x = 20; x < 105; ++x)
      directionalDistanceDifference = std::max(directionalDistanceDifference,
        luminance(directionalDistance, x, y) -
        luminance(directionalShadow, x, y));
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  int wgpuDistanceDifference = 0;
  if (!wgpuDistancePixels.empty())
    for (int y = 20; y < 105; ++y)
      for (int x = 20; x < 105; ++x) {
        const size_t pixel = static_cast<size_t>((y * side + x) * 4);
        const int limited = wgpuDistancePixels[pixel] + wgpuDistancePixels[pixel + 1] + wgpuDistancePixels[pixel + 2];
        const int full = wgpuDirectionalFullPixels[pixel] + wgpuDirectionalFullPixels[pixel + 1] + wgpuDirectionalFullPixels[pixel + 2];
        wgpuDistanceDifference = std::max(wgpuDistanceDifference, limited - full);
      }
  std::cout << " directional_distance_delta=" << directionalDistanceDifference
            << "/" << wgpuDistanceDifference << '\n';
  if (!wgpuDistancePixels.empty() &&
      (wgpuDistanceDifference < 100 ||
       std::abs(wgpuDistanceDifference - directionalDistanceDifference) > 130)) {
    std::cerr << "wgpu distance fade differs from Coin/GL reference\n";
    return 1;
  }
#endif
  int directionalDifference = 0;
  for (int y = 20; y < 105; ++y)
    for (int x = 20; x < 105; ++x)
      directionalDifference = std::max(directionalDifference,
        luminance(directionalNoReceive, x, y) - luminance(directionalShadow, x, y));
  int mixedDirectionalDifference = 0;
  for (int y = 20; y < 105; ++y)
    for (int x = 20; x < 105; ++x)
      mixedDirectionalDifference = std::max(mixedDirectionalDifference,
        std::abs(luminance(mixedDirectionalShadow, x, y) -
                 luminance(mixedDirectionalNoReceive, x, y)));
  int lateDirectionalDifference = 0;
  for (int y = 20; y < 105; ++y)
    for (int x = 20; x < 105; ++x)
      lateDirectionalDifference = std::max(lateDirectionalDifference,
        luminance(lateDirectionalShadow, x, y) -
        luminance(lateDirectionalNoReceive, x, y));
  int spotMixedDifference = 0;
  for (int y = 20; y < 105; ++y)
    for (int x = 20; x < 105; ++x)
      spotMixedDifference = std::max(spotMixedDifference,
        std::abs(luminance(mixedLightNoReceive, x, y) -
                 luminance(mixedLightActive, x, y)));
  int spotLateDifference = 0;
  int spotLateX = 0, spotLateY = 0;
  for (int y = 20; y < 105; ++y)
    for (int x = 20; x < 105; ++x) {
      const int delta = std::abs(luminance(lateLightNoReceive, x, y) -
                                 luminance(lateLightActive, x, y));
      if (delta > spotLateDifference) {
        spotLateDifference = delta;
        spotLateX = x;
        spotLateY = y;
      }
    }
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  int wgpuLateSpotDelta = 0;
  if (!lateSpotPixels.empty() && lateSpotPixels.size() == lateSpotNoReceivePixels.size())
    for (int y = 20; y < 105; ++y)
      for (int x = 20; x < 105; ++x) {
        const size_t pixel = static_cast<size_t>((y * side + x) * 4);
        const int lit = lateSpotPixels[pixel] + lateSpotPixels[pixel + 1] + lateSpotPixels[pixel + 2];
        const int dark = lateSpotNoReceivePixels[pixel] + lateSpotNoReceivePixels[pixel + 1] + lateSpotNoReceivePixels[pixel + 2];
        wgpuLateSpotDelta = std::max(wgpuLateSpotDelta, lit - dark);
      }
  int wgpuMixedSpotDelta = 0;
  if (!mixedSpotPixels.empty() && mixedSpotPixels.size() == mixedSpotNoReceivePixels.size())
    for (int y = 20; y < 105; ++y)
      for (int x = 20; x < 105; ++x) {
        const size_t pixel = static_cast<size_t>((y * side + x) * 4);
        const int lit = mixedSpotPixels[pixel] + mixedSpotPixels[pixel + 1] + mixedSpotPixels[pixel + 2];
        const int dark = mixedSpotNoReceivePixels[pixel] + mixedSpotNoReceivePixels[pixel + 1] + mixedSpotNoReceivePixels[pixel + 2];
        wgpuMixedSpotDelta = std::max(wgpuMixedSpotDelta, std::abs(lit - dark));
      }
  std::cout << " wgpu_mixed_spot_style_delta=" << wgpuMixedSpotDelta;
  std::cout << " wgpu_late_spot_style_delta=" << wgpuLateSpotDelta;
  const size_t wgpuSpotPixel = static_cast<size_t>(
    ((side - 1 - spotLateY) * side + spotLateX) * 4);
  const int wgpuSpotAtMax = lateSpotPixels.empty() ? 0 :
    lateSpotPixels[wgpuSpotPixel] + lateSpotPixels[wgpuSpotPixel + 1] +
    lateSpotPixels[wgpuSpotPixel + 2];
  std::cout << " late_spot_at_gl_max=" << wgpuSpotAtMax
            << "/" << luminance(lateLightActive, spotLateX, spotLateY) << '\n';
  if (!lateSpotPixels.empty() &&
      (std::abs(wgpuLateSpotDelta - spotLateDifference) > 130 ||
       std::abs(wgpuLateDirectionalDifference - lateDirectionalDifference) > 130 ||
       std::abs(wgpuMixedSpotDelta - spotMixedDifference) > 130 ||
       std::abs(wgpuMixedDirectionalDifference - mixedDirectionalDifference) > 130 ||
       std::abs(wgpuSpotAtMax - luminance(lateLightActive,
                                         spotLateX, spotLateY)) > 180)) {
    std::cerr << "wgpu late-light style relation differs from Coin/GL reference\n";
    return 1;
  }
#endif
  std::cout << " directional_style_delta=" << directionalDifference
            << " late_directional_style_delta=" << lateDirectionalDifference
            << " mixed_directional_style_delta=" << mixedDirectionalDifference
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
            << "/" << wgpuMixedDirectionalDifference
#endif
            << " spot_late_style_delta=" << spotLateDifference
            << " spot_mixed_style_delta=" << spotMixedDifference
            << " at=" << spotLateX << ',' << spotLateY
            << " values=" << luminance(lateLightActive, spotLateX, spotLateY)
            << ',' << luminance(lateLightNoReceive, spotLateX, spotLateY)
            << ',' << luminance(inactive, spotLateX, spotLateY) << '\n';
  if (clearShadow - castShadow < 100 || clearShadow - lateLightShadow < 100 ||
      noReceive - castShadow < 100 ||
      withoutCaster - castShadow < 100 ||
      std::abs(restoredShadow - castShadow) > 30 || centerDifference > 45 ||
      directionalDifference < 100 || directionalDistanceDifference < 100 ||
      lateDirectionalDifference < 100 ||
      mixedDirectionalDifference < 100 ||
      spotLateDifference < 100 || spotMixedDifference < 100) {
    std::cerr << "Coin/GL shadow, style or cache reference changed\n";
    return 1;
  }
  return 0;
}
