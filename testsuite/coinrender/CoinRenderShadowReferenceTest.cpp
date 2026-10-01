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
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
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
  bool clippedShadowQualified = true;
  if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") ||
      std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU")) {
    auto * uncutRoot = static_cast<SoSeparator *>(root->copy(TRUE));
    auto * cutRoot = static_cast<SoSeparator *>(root->copy(TRUE));
    uncutRoot->ref();
    cutRoot->ref();
    auto * cutGroup = static_cast<SoShadowGroup *>(cutRoot->getChild(1));
    auto * cutCaster = static_cast<SoSeparator *>(cutGroup->getChild(1));
    auto * clip = new SoClipPlane;
    clip->plane = SbPlane(SbVec3f(1, 0, 0), 0.1f);
    cutCaster->insertChild(clip, 1);
    CoinRenderTarget * clipTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    CoinRenderAction clipAction(SbViewportRegion(side, side));
    clipAction.setRenderTarget(clipTarget);
    clipAction.apply(uncutRoot);
    std::vector<unsigned char> gpuUncut, gpuCut;
    if (clipAction.getLastStatus() == CoinRenderAction::SUCCESS)
      clipTarget->readbackRGBA(gpuUncut);
    clipAction.apply(cutRoot);
    if (clipAction.getLastStatus() == CoinRenderAction::SUCCESS)
      clipTarget->readbackRGBA(gpuCut);
    clippedShadowQualified = clipAction.getLastStatus() == CoinRenderAction::SUCCESS &&
      gpuUncut.size() == gpuCut.size() && !gpuCut.empty();
    int gpuDelta = 0, glDelta = 0;
    if (clippedShadowQualified)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          gpuDelta = std::max(gpuDelta, std::abs(
            luminanceRgba(gpuUncut, x, y) - luminanceRgba(gpuCut, x, y)));
    clippedShadowQualified = clippedShadowQualified && gpuDelta > 0;
    if (clippedShadowQualified && std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
      SoOffscreenRenderer clipGl(SbViewportRegion(side, side));
      clipGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> glUncut, glCut;
      clippedShadowQualified = render(clipGl, uncutRoot, glUncut) &&
        render(clipGl, cutRoot, glCut);
      if (clippedShadowQualified)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            glDelta = std::max(glDelta, std::abs(
              luminance(glUncut, x, y) - luminance(glCut, x, y)));
      clippedShadowQualified = clippedShadowQualified && glDelta > 0 &&
        std::abs(gpuDelta - glDelta) <= 200;
    }
    std::cout << "clipped caster Coin/GL/GPU delta=" << glDelta << '/' << gpuDelta << '\n';
    if (!clippedShadowQualified)
      std::cerr << "clipped shadow frame: " << clipAction.getLastError().getString() << '\n';
    clipAction.setRenderTarget(nullptr);
    delete clipTarget;
    cutRoot->unref();
    uncutRoot->unref();
  }
  bool shadowTargetsIndependent = true;
  if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") ||
      std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU")) {
    CoinRenderTarget * first = CoinRenderTarget::createOffscreen(SbVec2i32(side, side));
    CoinRenderTarget * second = CoinRenderTarget::createOffscreen(SbVec2i32(160, 160));
    CoinRenderAction firstAction(SbViewportRegion(side, side));
    CoinRenderAction secondAction(SbViewportRegion(160, 160));
    firstAction.setRenderTarget(first);
    secondAction.setRenderTarget(second);
    firstAction.apply(root);
    std::vector<unsigned char> firstPixels, secondPixels, firstAgain;
    if (firstAction.getLastStatus() == CoinRenderAction::SUCCESS)
      first->readbackRGBA(firstPixels);
    const uint64_t firstSerial = first->getLastSubmissionSerial();
    secondAction.apply(root);
    if (secondAction.getLastStatus() == CoinRenderAction::SUCCESS)
      second->readbackRGBA(secondPixels);
    firstAction.apply(root);
    if (firstAction.getLastStatus() == CoinRenderAction::SUCCESS)
      first->readbackRGBA(firstAgain);
    shadowTargetsIndependent = firstAction.getLastStatus() == CoinRenderAction::SUCCESS &&
      secondAction.getLastStatus() == CoinRenderAction::SUCCESS &&
      firstPixels.size() == size_t(side * side * 4) &&
      secondPixels.size() == size_t(160 * 160 * 4) &&
      firstAgain == firstPixels && first->getLastSubmissionSerial() > firstSerial;
    if (!shadowTargetsIndependent)
      std::cerr << "parallel shadow targets: " << firstAction.getLastError().getString()
                << " / " << secondAction.getLastError().getString() << '\n';
    firstAction.setRenderTarget(nullptr);
    secondAction.setRenderTarget(nullptr);
    delete second;
    delete first;
  }
  bool siblingShadowGroupsQualified = true;
  if (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") ||
      std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU")) {
    auto * siblings = new SoSeparator;
    siblings->ref();
    auto * siblingCamera = static_cast<SoOrthographicCamera *>(camera->copy(TRUE));
    siblingCamera->height = 12.0f;
    siblings->addChild(siblingCamera);
    SoShadowStyle * secondGroundStyle = nullptr;
    SoShadowGroup * secondGroup = nullptr;
    for (int sideIndex = 0; sideIndex < 2; ++sideIndex) {
      auto * branch = new SoSeparator;
      auto * translation = new SoTranslation;
      translation->translation.setValue(sideIndex == 0 ? -3.0f : 3.0f, 0.0f, 0.0f);
      branch->addChild(translation);
      auto * childGroup = static_cast<SoShadowGroup *>(group->copy(TRUE));
      branch->addChild(childGroup);
      siblings->addChild(branch);
      if (sideIndex == 1) {
        secondGroup = childGroup;
        auto * groundBranch = static_cast<SoSeparator *>(childGroup->getChild(2));
        secondGroundStyle = static_cast<SoShadowStyle *>(groundBranch->getChild(0));
      }
    }
    CoinRenderTarget * siblingTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    CoinRenderAction siblingAction(SbViewportRegion(side, side));
    siblingAction.setRenderTarget(siblingTarget);
    secondGroundStyle->style = SoShadowStyle::NO_SHADOWING;
    siblingAction.apply(siblings);
    std::vector<unsigned char> gpuNoReceive, gpuReceive;
    const bool firstOk = siblingAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (firstOk) siblingTarget->readbackRGBA(gpuNoReceive);
    secondGroundStyle->style = SoShadowStyle::SHADOWED;
    siblingAction.apply(siblings);
    const bool secondOk = siblingAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (secondOk) siblingTarget->readbackRGBA(gpuReceive);
    int gpuDelta = 0, glDelta = 0;
    siblingShadowGroupsQualified = firstOk && secondOk &&
      gpuNoReceive.size() == gpuReceive.size() && !gpuNoReceive.empty();
    if (siblingShadowGroupsQualified)
      for (int y = 20; y < 105; ++y)
        for (int x = 65; x < 125; ++x)
          gpuDelta = std::max(gpuDelta, std::abs(
            luminanceRgba(gpuNoReceive, x, y) - luminanceRgba(gpuReceive, x, y)));
    siblingShadowGroupsQualified = siblingShadowGroupsQualified && gpuDelta > 0;
    if (siblingShadowGroupsQualified)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 50; ++x)
          siblingShadowGroupsQualified = siblingShadowGroupsQualified &&
            luminanceRgba(gpuNoReceive, x, y) == luminanceRgba(gpuReceive, x, y);
    if (siblingShadowGroupsQualified) {
      const uint64_t serial = siblingTarget->getLastSubmissionSerial();
      secondGroup->isActive = FALSE;
      siblingAction.apply(siblings);
      std::vector<unsigned char> gpuMixed;
      if (siblingAction.getLastStatus() == CoinRenderAction::SUCCESS)
        siblingTarget->readbackRGBA(gpuMixed);
      int mixedGpuDelta = 0;
      siblingShadowGroupsQualified =
        siblingAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        siblingTarget->getLastSubmissionSerial() > serial &&
        gpuMixed.size() == gpuReceive.size();
      if (siblingShadowGroupsQualified)
        for (int y = 20; y < 105; ++y) {
          for (int x = 65; x < 125; ++x)
            mixedGpuDelta = std::max(mixedGpuDelta, std::abs(
              luminanceRgba(gpuMixed, x, y) - luminanceRgba(gpuReceive, x, y)));
          for (int x = 20; x < 50; ++x)
            siblingShadowGroupsQualified = siblingShadowGroupsQualified &&
              luminanceRgba(gpuMixed, x, y) == luminanceRgba(gpuReceive, x, y);
        }
      siblingShadowGroupsQualified = siblingShadowGroupsQualified && mixedGpuDelta > 30;
      if (siblingShadowGroupsQualified &&
          std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
        auto * glMixed = static_cast<SoSeparator *>(siblings->copy(TRUE));
        glMixed->ref();
        secondGroup->isActive = TRUE;
        auto * glBoth = static_cast<SoSeparator *>(siblings->copy(TRUE));
        glBoth->ref();
        SoOffscreenRenderer mixedGl(SbViewportRegion(side, side));
        mixedGl.setComponents(SoOffscreenRenderer::RGB);
        std::vector<unsigned char> glMixedPixels, glBothPixels;
        const bool rendered = render(mixedGl, glMixed, glMixedPixels) &&
          render(mixedGl, glBoth, glBothPixels);
        int mixedGlDelta = 0;
        if (rendered)
          for (int y = 20; y < 105; ++y)
            for (int x = 65; x < 125; ++x)
              mixedGlDelta = std::max(mixedGlDelta, std::abs(
                luminance(glMixedPixels, x, y) - luminance(glBothPixels, x, y)));
        siblingShadowGroupsQualified = rendered && mixedGlDelta > 30 &&
          std::abs(mixedGlDelta - mixedGpuDelta) <= 180;
        std::cout << "active/inactive sibling Coin/GL/GPU delta="
                  << mixedGlDelta << '/' << mixedGpuDelta << '\n';
        glBoth->unref();
        glMixed->unref();
      }
      secondGroup->isActive = TRUE;
    }
    if (siblingShadowGroupsQualified && std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
      secondGroundStyle->style = SoShadowStyle::NO_SHADOWING;
      auto * glNoReceiveRoot = static_cast<SoSeparator *>(siblings->copy(TRUE));
      glNoReceiveRoot->ref();
      secondGroundStyle->style = SoShadowStyle::SHADOWED;
      auto * glReceiveRoot = static_cast<SoSeparator *>(siblings->copy(TRUE));
      glReceiveRoot->ref();
      SoOffscreenRenderer groupGl(SbViewportRegion(side, side));
      groupGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> glNoReceive, glReceive;
      siblingShadowGroupsQualified = render(groupGl, glNoReceiveRoot, glNoReceive) &&
        render(groupGl, glReceiveRoot, glReceive);
      if (siblingShadowGroupsQualified)
        for (int y = 20; y < 105; ++y)
          for (int x = 65; x < 125; ++x)
            glDelta = std::max(glDelta, std::abs(
              luminance(glNoReceive, x, y) - luminance(glReceive, x, y)));
      siblingShadowGroupsQualified = siblingShadowGroupsQualified && glDelta > 0 &&
        std::abs(glDelta - gpuDelta) <= 180;
      glReceiveRoot->unref();
      glNoReceiveRoot->unref();
    }
    std::cout << "sibling groups Coin/GL/GPU delta=" << glDelta << '/' << gpuDelta << '\n';
    if (siblingShadowGroupsQualified) {
      const float baseEpsilon = secondGroup->epsilon.getValue();
      const float baseThreshold = secondGroup->threshold.getValue();
      secondGroup->epsilon = 0.00002f;
      secondGroup->threshold = 0.12f;
      siblingAction.apply(siblings);
      std::vector<unsigned char> gpuDistinct, gpuShared;
      const bool distinctOk = siblingAction.getLastStatus() == CoinRenderAction::SUCCESS;
      if (distinctOk) siblingTarget->readbackRGBA(gpuDistinct);
      secondGroup->epsilon = baseEpsilon;
      secondGroup->threshold = baseThreshold;
      siblingAction.apply(siblings);
      const bool sharedOk = siblingAction.getLastStatus() == CoinRenderAction::SUCCESS;
      if (sharedOk) siblingTarget->readbackRGBA(gpuShared);
      int distinctGpuDelta = 0, distinctGlDelta = 0;
      bool distinctQualified = distinctOk && sharedOk &&
        gpuDistinct.size() == gpuShared.size();
      if (distinctQualified)
        for (int y = 20; y < 105; ++y) {
          for (int x = 65; x < 125; ++x)
            distinctGpuDelta = std::max(distinctGpuDelta, std::abs(
              luminanceRgba(gpuDistinct, x, y) - luminanceRgba(gpuShared, x, y)));
          for (int x = 20; x < 50; ++x)
            distinctQualified = distinctQualified &&
              luminanceRgba(gpuDistinct, x, y) == luminanceRgba(gpuShared, x, y);
        }
      if (distinctQualified && std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
        auto * glShared = static_cast<SoSeparator *>(siblings->copy(TRUE));
        glShared->ref();
        secondGroup->epsilon = 0.00002f;
        secondGroup->threshold = 0.12f;
        auto * glDistinct = static_cast<SoSeparator *>(siblings->copy(TRUE));
        glDistinct->ref();
        secondGroup->epsilon = baseEpsilon;
        secondGroup->threshold = baseThreshold;
        SoOffscreenRenderer distinctGl(SbViewportRegion(side, side));
        distinctGl.setComponents(SoOffscreenRenderer::RGB);
        std::vector<unsigned char> glSharedPixels, glDistinctPixels;
        distinctQualified = render(distinctGl, glShared, glSharedPixels) &&
          render(distinctGl, glDistinct, glDistinctPixels);
        if (distinctQualified)
          for (int y = 20; y < 105; ++y)
            for (int x = 65; x < 125; ++x)
              distinctGlDelta = std::max(distinctGlDelta, std::abs(
                luminance(glSharedPixels, x, y) -
                luminance(glDistinctPixels, x, y)));
        glDistinct->unref();
        glShared->unref();
      }
      std::cout << "distinct sibling VSM Coin/GL/GPU delta=" <<
        distinctGlDelta << '/' << distinctGpuDelta << '\n';
      siblingShadowGroupsQualified = distinctQualified && distinctGpuDelta > 0 &&
        (!std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") ||
         (distinctGlDelta > 0 &&
          std::abs(distinctGlDelta - distinctGpuDelta) <= 150));
    }
    if (siblingShadowGroupsQualified) {
      auto * rttSiblings = new SoSeparator;
      rttSiblings->ref();
      auto * rttCamera = new SoOrthographicCamera;
      rttCamera->position.setValue(0, 0, 8);
      rttCamera->height = 7;
      rttCamera->nearDistance = 1;
      rttCamera->farDistance = 20;
      rttSiblings->addChild(rttCamera);
      auto * rttLighting = new SoLightModel;
      rttLighting->model = SoLightModel::BASE_COLOR;
      rttSiblings->addChild(rttLighting);
      auto * rttTexture = new SoSceneTexture2;
      rttTexture->size.setValue(side, side);
      rttTexture->type = SoSceneTexture2::RGBA8;
      rttTexture->transparencyFunction = SoSceneTexture2::NONE;
      rttTexture->scene = siblings;
      rttSiblings->addChild(rttTexture);
      auto * rttUv = new SoTextureCoordinate2;
      rttUv->point.set1Value(0, SbVec2f(0, 0));
      rttUv->point.set1Value(1, SbVec2f(1, 0));
      rttUv->point.set1Value(2, SbVec2f(1, 1));
      rttUv->point.set1Value(3, SbVec2f(0, 1));
      rttSiblings->addChild(rttUv);
      auto * rttPoints = new SoCoordinate3;
      rttPoints->point.set1Value(0, SbVec3f(-3, -3, 0));
      rttPoints->point.set1Value(1, SbVec3f(3, -3, 0));
      rttPoints->point.set1Value(2, SbVec3f(3, 3, 0));
      rttPoints->point.set1Value(3, SbVec3f(-3, 3, 0));
      rttSiblings->addChild(rttPoints);
      auto * rttDisplay = new SoIndexedFaceSet;
      const int32_t rttIndices[] = {0, 1, 2, 3, -1};
      rttDisplay->coordIndex.setValues(0, 5, rttIndices);
      rttDisplay->textureCoordIndex.setValues(0, 5, rttIndices);
      rttSiblings->addChild(rttDisplay);
      const float baseEpsilon = secondGroup->epsilon.getValue();
      const float baseThreshold = secondGroup->threshold.getValue();
      int rttGlDelta = 0;
      if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
        auto * glShared = static_cast<SoSeparator *>(rttSiblings->copy(TRUE));
        glShared->ref();
        secondGroup->epsilon = 0.00002f;
        secondGroup->threshold = 0.12f;
        auto * glDistinct = static_cast<SoSeparator *>(rttSiblings->copy(TRUE));
        glDistinct->ref();
        SoOffscreenRenderer rttGl(SbViewportRegion(side, side));
        rttGl.setComponents(SoOffscreenRenderer::RGB);
        std::vector<unsigned char> glSharedPixels, glDistinctPixels;
        siblingShadowGroupsQualified =
          render(rttGl, glShared, glSharedPixels) &&
          render(rttGl, glDistinct, glDistinctPixels);
        if (siblingShadowGroupsQualified)
          for (int y = 20; y < 105; ++y)
            for (int x = 65; x < 125; ++x)
              rttGlDelta = std::max(rttGlDelta, std::abs(
                luminance(glSharedPixels, x, y) -
                luminance(glDistinctPixels, x, y)));
        glDistinct->unref();
        glShared->unref();
      }
      for (int mode = 0; mode < 2 && siblingShadowGroupsQualified; ++mode) {
        CoinRenderOptions options{};
        options.sceneTexture = mode == 0 ?
          COIN_RENDER_SCENE_TEXTURE_STAGED : COIN_RENDER_SCENE_TEXTURE_DIRECT;
        options.transparency = COIN_RENDER_TRANSPARENCY_OBJECT;
        CoinRenderTarget * rttTarget = CoinRenderTarget::createOffscreen(
          SbVec2i32(side, side), options);
        CoinRenderAction rttAction(SbViewportRegion(side, side));
        rttAction.setRenderTarget(rttTarget);
        secondGroup->epsilon = 0.00002f;
        secondGroup->threshold = 0.12f;
        rttAction.apply(rttSiblings);
        std::vector<unsigned char> gpuDistinct, gpuShared;
        const bool distinctOk = rttAction.getLastStatus() == CoinRenderAction::SUCCESS;
        if (distinctOk) rttTarget->readbackRGBA(gpuDistinct);
        secondGroup->epsilon = baseEpsilon;
        secondGroup->threshold = baseThreshold;
        rttAction.apply(rttSiblings);
        const bool sharedOk = rttAction.getLastStatus() == CoinRenderAction::SUCCESS;
        if (sharedOk) rttTarget->readbackRGBA(gpuShared);
        int rttGpuDelta = 0;
        siblingShadowGroupsQualified = distinctOk && sharedOk &&
          gpuDistinct.size() == gpuShared.size();
        if (siblingShadowGroupsQualified)
          for (int y = 20; y < 105; ++y) {
            for (int x = 65; x < 125; ++x)
              rttGpuDelta = std::max(rttGpuDelta, std::abs(
                luminanceRgba(gpuDistinct, x, y) -
                luminanceRgba(gpuShared, x, y)));
            for (int x = 20; x < 50; ++x)
              siblingShadowGroupsQualified = siblingShadowGroupsQualified &&
                luminanceRgba(gpuDistinct, x, y) ==
                luminanceRgba(gpuShared, x, y);
          }
        siblingShadowGroupsQualified = siblingShadowGroupsQualified &&
          rttGpuDelta > 0 &&
          (!std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") ||
           (rttGlDelta > 0 && std::abs(rttGlDelta - rttGpuDelta) <= 150));
        std::cout << (mode == 0 ? "staged" : "direct") <<
          " RTT distinct VSM Coin/GL/GPU delta=" <<
          rttGlDelta << '/' << rttGpuDelta << '\n';
        if (!siblingShadowGroupsQualified)
          std::cerr << "RTT distinct VSM: " <<
            rttAction.getLastError().getString() << '\n';
        rttAction.setRenderTarget(nullptr);
        delete rttTarget;
      }
      secondGroup->epsilon = baseEpsilon;
      secondGroup->threshold = baseThreshold;
      rttSiblings->unref();
    }
    if (!siblingShadowGroupsQualified)
      std::cerr << "sibling shadow groups: " << siblingAction.getLastError().getString() << '\n';
    siblingAction.setRenderTarget(nullptr);
    delete siblingTarget;
    siblings->unref();
  }
  // A regular Coin directional light contributes alongside one shadow-map light.
  auto * ordinaryRoot = static_cast<SoSeparator *>(root->copy(TRUE));
  ordinaryRoot->ref();
  auto * ordinaryGroup = static_cast<SoShadowGroup *>(ordinaryRoot->getChild(1));
  ordinaryGroup->quality = 1.0f;
  auto * plainDirectional = new SoDirectionalLight;
  plainDirectional->direction.setValue(-0.4f, -0.3f, -1.0f);
  plainDirectional->color.setValue(0.1f, 0.8f, 0.2f);
  plainDirectional->intensity = 0.6f;
  ordinaryRoot->insertChild(plainDirectional, 1); // Inherited at group entry.
  action.apply(ordinaryRoot);
  const CoinRenderFramePlan ordinaryFrame = action.getPimpl()->lastRejectedShadowFrame;
  const CoinRenderShadowPlan ordinaryPlan = action.getPimpl()->lastRejectedShadowPlan;
  std::string ordinaryDiagnostic;
  bool ordinaryLightQualified =
    action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
    ordinaryFrame.shadowLights.size() == 1 &&
    coin_render_shadow_single_spot_opaque_profile(
      ordinaryFrame, ordinaryPlan, ordinaryDiagnostic);
  if (ordinaryLightQualified &&
      (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") ||
       std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU"))) {
    CoinRenderTarget * ordinaryTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    CoinRenderAction ordinaryAction(SbViewportRegion(side, side));
    ordinaryAction.setRenderTarget(ordinaryTarget);
    plainDirectional->on = FALSE;
    ordinaryAction.apply(ordinaryRoot);
    std::vector<unsigned char> gpuOff, gpuOn;
    const bool offOk = ordinaryAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (offOk) ordinaryTarget->readbackRGBA(gpuOff);
    plainDirectional->on = TRUE;
    ordinaryAction.apply(ordinaryRoot);
    const bool onOk = ordinaryAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (onOk) ordinaryTarget->readbackRGBA(gpuOn);
    int gpuDelta = 0, glDelta = 0;
    ordinaryLightQualified = offOk && onOk && gpuOff.size() == gpuOn.size();
    if (ordinaryLightQualified)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          gpuDelta = std::max(gpuDelta, std::abs(
            luminanceRgba(gpuOff, x, y) - luminanceRgba(gpuOn, x, y)));
    if (ordinaryLightQualified && std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
      plainDirectional->on = FALSE;
      auto * glOffRoot = static_cast<SoSeparator *>(ordinaryRoot->copy(TRUE));
      glOffRoot->ref();
      plainDirectional->on = TRUE;
      auto * glOnRoot = static_cast<SoSeparator *>(ordinaryRoot->copy(TRUE));
      glOnRoot->ref();
      SoOffscreenRenderer ordinaryGl(SbViewportRegion(side, side));
      ordinaryGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> glOff, glOn;
      ordinaryLightQualified = render(ordinaryGl, glOffRoot, glOff) &&
        render(ordinaryGl, glOnRoot, glOn);
      if (ordinaryLightQualified)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            glDelta = std::max(glDelta, std::abs(
              luminance(glOff, x, y) - luminance(glOn, x, y)));
      glOnRoot->unref();
      glOffRoot->unref();
    }
    std::cout << "inherited directional with shadow group Coin/GL/GPU delta="
              << glDelta << '/' << gpuDelta << '\n';
    ordinaryLightQualified = ordinaryLightQualified && gpuDelta > 20 &&
      (!std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") ||
       (glDelta > 20 && std::abs(glDelta - gpuDelta) <= 180));
    if (!ordinaryLightQualified)
      std::cerr << "inherited directional with shadow group: "
                << ordinaryAction.getLastError().getString() << " profile="
                << ordinaryDiagnostic << '\n';
    ordinaryAction.setRenderTarget(nullptr);
    delete ordinaryTarget;
  }
  plainDirectional->ref();
  ordinaryRoot->removeChild(plainDirectional);
  ordinaryGroup->insertChild(plainDirectional, 1);
  plainDirectional->unref();
  action.apply(ordinaryRoot);
  std::string internalOrdinaryDiagnostic;
  const bool internalOrdinaryRejected =
    action.getPimpl()->lastRejectedShadowFrame.shadowLights.size() == 2 &&
    !coin_render_shadow_single_spot_opaque_profile(
      action.getPimpl()->lastRejectedShadowFrame,
      action.getPimpl()->lastRejectedShadowPlan, internalOrdinaryDiagnostic) &&
    internalOrdinaryDiagnostic.find("inside an active shadow group") !=
      std::string::npos;
  ordinaryLightQualified = ordinaryLightQualified && internalOrdinaryRejected;
  ordinaryRoot->unref();
  auto * pointRoot = static_cast<SoSeparator *>(root->copy(TRUE));
  pointRoot->ref();
  auto * inheritedPoint = new SoPointLight;
  inheritedPoint->location.setValue(0.0f, 2.0f, 4.0f);
  pointRoot->insertChild(inheritedPoint, 1);
  action.apply(pointRoot);
  std::string pointDiagnostic;
  bool inheritedPointQualified =
    coin_render_shadow_single_spot_opaque_profile(
      action.getPimpl()->lastRejectedShadowFrame,
      action.getPimpl()->lastRejectedShadowPlan, pointDiagnostic);
  if (inheritedPointQualified &&
      (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") ||
       std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU"))) {
    CoinRenderTarget * pointTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    CoinRenderAction pointAction(SbViewportRegion(side, side));
    pointAction.setRenderTarget(pointTarget);
    inheritedPoint->on = FALSE;
    pointAction.apply(pointRoot);
    std::vector<unsigned char> gpuOff, gpuOn;
    const bool offOk = pointAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (offOk) pointTarget->readbackRGBA(gpuOff);
    inheritedPoint->on = TRUE;
    pointAction.apply(pointRoot);
    const bool onOk = pointAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (onOk) pointTarget->readbackRGBA(gpuOn);
    int gpuDelta = 0, glDelta = 0;
    inheritedPointQualified = offOk && onOk && gpuOff.size() == gpuOn.size();
    if (inheritedPointQualified)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          gpuDelta = std::max(gpuDelta, std::abs(
            luminanceRgba(gpuOff, x, y) - luminanceRgba(gpuOn, x, y)));
    if (inheritedPointQualified && std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
      inheritedPoint->on = FALSE;
      auto * glOffRoot = static_cast<SoSeparator *>(pointRoot->copy(TRUE));
      glOffRoot->ref();
      inheritedPoint->on = TRUE;
      auto * glOnRoot = static_cast<SoSeparator *>(pointRoot->copy(TRUE));
      glOnRoot->ref();
      SoOffscreenRenderer pointGl(SbViewportRegion(side, side));
      pointGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> glOff, glOn;
      inheritedPointQualified = render(pointGl, glOffRoot, glOff) &&
        render(pointGl, glOnRoot, glOn);
      if (inheritedPointQualified)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            glDelta = std::max(glDelta, std::abs(
              luminance(glOff, x, y) - luminance(glOn, x, y)));
      glOnRoot->unref();
      glOffRoot->unref();
    }
    std::cout << "inherited point with shadow group Coin/GL/GPU delta=" <<
      glDelta << '/' << gpuDelta << '\n';
    inheritedPointQualified = inheritedPointQualified && gpuDelta > 20 &&
      (!std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") ||
       (glDelta > 20 && std::abs(glDelta - gpuDelta) <= 180));
    if (!inheritedPointQualified)
      std::cerr << "inherited point with shadow group: " <<
        pointAction.getLastError().getString() << " profile=" <<
        pointDiagnostic << '\n';
    pointAction.setRenderTarget(nullptr);
    delete pointTarget;
  }
  ordinaryLightQualified = ordinaryLightQualified && inheritedPointQualified;
  pointRoot->unref();
  auto * texturedRoot = static_cast<SoSeparator *>(root->copy(TRUE));
  texturedRoot->ref();
  auto * texturedGroup = static_cast<SoShadowGroup *>(texturedRoot->getChild(1));
  auto * texturedGround = static_cast<SoSeparator *>(texturedGroup->getChild(2));
  auto * opaqueTexture = new SoTexture2;
  const unsigned char greenPixel[] = {20, 220, 40};
  opaqueTexture->image.setValue(SbVec2s(1, 1), 3, greenPixel);
  texturedGround->insertChild(opaqueTexture, 2);
  auto * floorUv = new SoTextureCoordinate2;
  floorUv->point.set1Value(0, SbVec2f(0, 0));
  floorUv->point.set1Value(1, SbVec2f(1, 0));
  floorUv->point.set1Value(2, SbVec2f(1, 1));
  floorUv->point.set1Value(3, SbVec2f(0, 1));
  texturedGround->insertChild(floorUv, 3);
  action.apply(texturedRoot);
  std::string texturedDiagnostic;
  bool texturedShadowQualified =
    action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
    coin_render_shadow_single_spot_opaque_profile(
      action.getPimpl()->lastRejectedShadowFrame,
      action.getPimpl()->lastRejectedShadowPlan, texturedDiagnostic);
  if (!texturedShadowQualified)
    std::cerr << "textured shadow CPU: status=" << action.getLastStatus()
              << " error=" << action.getLastError().getString()
              << " diagnostic=" << texturedDiagnostic
              << " textures=" << action.getPimpl()->lastRejectedShadowFrame.textures.size()
              << " draws=" << action.getPimpl()->lastRejectedShadowFrame.draws.size()
              << '\n';
  if (texturedShadowQualified &&
      (std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") ||
       std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU"))) {
    CoinRenderTarget * texturedTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side));
    CoinRenderAction texturedAction(SbViewportRegion(side, side));
    texturedAction.setRenderTarget(texturedTarget);
    texturedAction.apply(root);
    std::vector<unsigned char> gpuPlain, gpuTextured;
    const bool plainOk = texturedAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (plainOk) texturedTarget->readbackRGBA(gpuPlain);
    texturedAction.apply(texturedRoot);
    const bool texturedOk = texturedAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (texturedOk) texturedTarget->readbackRGBA(gpuTextured);
    int gpuDelta = 0, glDelta = 0;
    texturedShadowQualified = plainOk && texturedOk &&
      gpuPlain.size() == gpuTextured.size();
    if (texturedShadowQualified)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          gpuDelta = std::max(gpuDelta, std::abs(
            luminanceRgba(gpuPlain, x, y) - luminanceRgba(gpuTextured, x, y)));
    if (texturedShadowQualified &&
        std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
      auto * glPlain = static_cast<SoSeparator *>(root->copy(TRUE));
      glPlain->ref();
      auto * glTextured = static_cast<SoSeparator *>(texturedRoot->copy(TRUE));
      glTextured->ref();
      SoOffscreenRenderer texturedGl(SbViewportRegion(side, side));
      texturedGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> glPlainPixels, glTexturedPixels;
      texturedShadowQualified = render(texturedGl, glPlain, glPlainPixels) &&
        render(texturedGl, glTextured, glTexturedPixels);
      if (texturedShadowQualified)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            glDelta = std::max(glDelta, std::abs(
              luminance(glPlainPixels, x, y) -
              luminance(glTexturedPixels, x, y)));
      glTextured->unref();
      glPlain->unref();
    }
    std::cout << "opaque textured shadow receiver Coin/GL/GPU delta="
              << glDelta << '/' << gpuDelta << '\n';
    texturedShadowQualified = texturedShadowQualified && gpuDelta > 40 &&
      (!std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") ||
       (glDelta > 40 && std::abs(glDelta - gpuDelta) <= 180));
    if (texturedShadowQualified) {
      const uint64_t serial = texturedTarget->getLastSubmissionSerial();
      const unsigned char translucentPixel[] = {20, 220, 40, 128};
      opaqueTexture->image.setValue(SbVec2s(1, 1), 4, translucentPixel);
      texturedAction.apply(texturedRoot);
      std::vector<unsigned char> afterRejected;
      texturedTarget->readbackRGBA(afterRejected);
      texturedShadowQualified =
        texturedAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
        texturedTarget->getLastSubmissionSerial() == serial &&
        afterRejected == gpuTextured;
      opaqueTexture->image.setValue(SbVec2s(1, 1), 3, greenPixel);
    }
    if (!texturedShadowQualified)
      std::cerr << "opaque textured shadow receiver: "
                << texturedAction.getLastError().getString() << " profile="
                << texturedDiagnostic << '\n';
    texturedAction.setRenderTarget(nullptr);
    delete texturedTarget;
  }
  texturedRoot->unref();
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
    SoOffscreenRenderer multiGl(SbViewportRegion(side, side));
    multiGl.setComponents(SoOffscreenRenderer::RGB);
    std::vector<unsigned char> glDual, glThree, glFour;
    const bool compareGl = std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") != nullptr;
    if (twoLightBgfxSubmitted && compareGl) {
      auto * copy = static_cast<SoSeparator *>(root->copy(TRUE));
      copy->ref();
      twoLightBgfxSubmitted = render(multiGl, copy, glDual);
      copy->unref();
    }
    if (twoLightBgfxSubmitted) {
      auto * third = new SoShadowSpotLight;
      third->location.setValue(-2.0f, 2.0f, 4.0f);
      third->direction.setValue(2.0f, -2.0f, -5.0f);
      third->cutOffAngle = 0.9f;
      group->insertChild(third, 2);
      const uint64_t serial = dualTarget->getLastSubmissionSerial();
      dualAction.apply(root);
      std::vector<unsigned char> afterThree;
      if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(afterThree);
      const bool threeSubmitted = dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        dualTarget->getLastSubmissionSerial() > serial &&
        afterThree.size() == dualPixels.size() && afterThree != dualPixels;
      if (!threeSubmitted)
        std::cerr << "BGFX three-light frame: " << dualAction.getLastError().getString()
                  << " changed=" << (afterThree != dualPixels) << '\n';
      twoLightBgfxSubmitted = twoLightBgfxSubmitted && threeSubmitted;
      if (threeSubmitted && compareGl) {
        auto * copy = static_cast<SoSeparator *>(root->copy(TRUE));
        copy->ref();
        twoLightBgfxSubmitted = render(multiGl, copy, glThree);
        copy->unref();
        int bgfxDelta = 0, glDelta = 0;
        if (twoLightBgfxSubmitted)
          for (int y = 20; y < 105; ++y)
            for (int x = 20; x < 105; ++x) {
              bgfxDelta = std::max(bgfxDelta, std::abs(
                luminanceRgba(dualPixels, x, y) - luminanceRgba(afterThree, x, y)));
              glDelta = std::max(glDelta, std::abs(
                luminance(glDual, x, y) - luminance(glThree, x, y)));
            }
        std::cout << "three lights Coin/GL/BGFX delta=" << glDelta << '/' << bgfxDelta << '\n';
        twoLightBgfxSubmitted = twoLightBgfxSubmitted && glDelta > 0 &&
          bgfxDelta > 0 && std::abs(glDelta - bgfxDelta) <= 130;
      }
      auto * fourth = new SoShadowDirectionalLight;
      fourth->direction.setValue(0.5f, -0.4f, -1.0f);
      fourth->intensity = 0.35f;
      group->insertChild(fourth, 3);
      dualAction.apply(root);
      std::vector<unsigned char> afterFour;
      if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(afterFour);
      const bool fourSubmitted = dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        afterFour.size() == dualPixels.size();
      if (!fourSubmitted)
        std::cerr << "BGFX four-light frame: " << dualAction.getLastError().getString() << '\n';
      twoLightBgfxSubmitted = twoLightBgfxSubmitted && fourSubmitted;
      if (fourSubmitted && compareGl) {
        auto * copy = static_cast<SoSeparator *>(root->copy(TRUE));
        copy->ref();
        twoLightBgfxSubmitted = render(multiGl, copy, glFour);
        copy->unref();
        int bgfxDelta = 0, glDelta = 0;
        if (twoLightBgfxSubmitted)
          for (int y = 20; y < 105; ++y)
            for (int x = 20; x < 105; ++x) {
              bgfxDelta = std::max(bgfxDelta, std::abs(
                luminanceRgba(afterThree, x, y) - luminanceRgba(afterFour, x, y)));
              glDelta = std::max(glDelta, std::abs(
                luminance(glThree, x, y) - luminance(glFour, x, y)));
            }
        std::cout << "four lights Coin/GL/BGFX delta=" << glDelta << '/' << bgfxDelta << '\n';
        twoLightBgfxSubmitted = twoLightBgfxSubmitted && glDelta > 0 &&
          bgfxDelta > 0 && std::abs(glDelta - bgfxDelta) <= 130;
      }
      auto * fifth = new SoShadowDirectionalLight;
      fifth->direction.setValue(-0.5f, -0.4f, -1.0f);
      fifth->intensity = 0.45f;
      group->insertChild(fifth, 4);
      CoinRenderAction fiveCapture(SbViewportRegion(side, side));
      fiveCapture.apply(root);
      std::string fiveDiagnostic;
      const bool fiveCoreQualified =
        fiveCapture.getPimpl()->lastRejectedShadowPlan.passes.size() == 5 &&
        coin_render_shadow_opaque_profile(
          fiveCapture.getPimpl()->lastRejectedShadowFrame,
          fiveCapture.getPimpl()->lastRejectedShadowPlan, 5, fiveDiagnostic);
      const uint64_t fourSerial = dualTarget->getLastSubmissionSerial();
      dualAction.apply(root);
      std::vector<unsigned char> afterFive;
      if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(afterFive);
      if (dualAction.getLastStatus() != CoinRenderAction::SUCCESS)
        std::cerr << "BGFX five-light frame: " << dualAction.getLastError().getString() << '\n';
      twoLightBgfxSubmitted = twoLightBgfxSubmitted && fiveCoreQualified &&
        dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        dualTarget->getLastSubmissionSerial() > fourSerial &&
        afterFive.size() == afterFour.size();
      std::vector<unsigned char> glFive;
      if (twoLightBgfxSubmitted && compareGl) {
        auto * copy = static_cast<SoSeparator *>(root->copy(TRUE));
        copy->ref();
        twoLightBgfxSubmitted = render(multiGl, copy, glFive);
        copy->unref();
        int bgfxDelta = 0, glDelta = 0;
        if (twoLightBgfxSubmitted)
          for (int y = 20; y < 105; ++y)
            for (int x = 20; x < 105; ++x) {
              bgfxDelta = std::max(bgfxDelta, std::abs(
                luminanceRgba(afterFour, x, y) - luminanceRgba(afterFive, x, y)));
              glDelta = std::max(glDelta, std::abs(
                luminance(glFour, x, y) - luminance(glFive, x, y)));
            }
        std::cout << "five lights Coin/GL/BGFX delta=" << glDelta << '/' << bgfxDelta << '\n';
        twoLightBgfxSubmitted = twoLightBgfxSubmitted && glDelta > 0 &&
          bgfxDelta > 0 && std::abs(glDelta - bgfxDelta) <= 130;
      }
      std::vector<SoShadowDirectionalLight *> extraLights;
      for (int i = 0; i < 3; ++i) {
        auto * extra = new SoShadowDirectionalLight;
        extra->direction.setValue(0.25f * float(i - 1), -0.3f, -1.0f);
        extra->intensity = 0.08f;
        group->insertChild(extra, 5 + i);
        extraLights.push_back(extra);
      }
      CoinRenderAction eightCapture(SbViewportRegion(side, side));
      eightCapture.apply(root);
      std::string eightDiagnostic;
      const bool eightCoreQualified =
        eightCapture.getPimpl()->lastRejectedShadowPlan.passes.size() == 8 &&
        coin_render_shadow_opaque_profile(
          eightCapture.getPimpl()->lastRejectedShadowFrame,
          eightCapture.getPimpl()->lastRejectedShadowPlan, 8, eightDiagnostic);
      twoLightBgfxSubmitted = twoLightBgfxSubmitted && eightCoreQualified;
      const uint64_t fiveSerial = dualTarget->getLastSubmissionSerial();
      dualAction.apply(root);
      std::vector<unsigned char> afterEight;
      if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(afterEight);
      if (dualAction.getLastStatus() != CoinRenderAction::SUCCESS)
        std::cerr << "BGFX eight-light frame: " << dualAction.getLastError().getString() << '\n';
      twoLightBgfxSubmitted = twoLightBgfxSubmitted &&
        dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        dualTarget->getLastSubmissionSerial() > fiveSerial &&
        afterEight.size() == afterFive.size();
      if (twoLightBgfxSubmitted && compareGl) {
        auto * copy = static_cast<SoSeparator *>(root->copy(TRUE));
        copy->ref();
        std::vector<unsigned char> glEight;
        twoLightBgfxSubmitted = render(multiGl, copy, glEight);
        copy->unref();
        int bgfxDelta = 0, glDelta = 0;
        if (twoLightBgfxSubmitted)
          for (int y = 20; y < 105; ++y)
            for (int x = 20; x < 105; ++x) {
              bgfxDelta = std::max(bgfxDelta, std::abs(
                luminanceRgba(afterFive, x, y) - luminanceRgba(afterEight, x, y)));
              glDelta = std::max(glDelta, std::abs(
                luminance(glFive, x, y) - luminance(glEight, x, y)));
            }
        std::cout << "eight lights Coin/GL/BGFX delta=" << glDelta << '/' << bgfxDelta << '\n';
        twoLightBgfxSubmitted = twoLightBgfxSubmitted && glDelta > 0 &&
          bgfxDelta > 0 && std::abs(glDelta - bgfxDelta) <= 130;
      }
      if (twoLightBgfxSubmitted) {
        auto * rttRoot = new SoSeparator;
        rttRoot->ref();
        auto * camera = new SoOrthographicCamera;
        camera->position.setValue(0, 0, 8);
        camera->height = 7;
        camera->nearDistance = 1;
        camera->farDistance = 20;
        rttRoot->addChild(camera);
        auto * baseColor = new SoLightModel;
        baseColor->model = SoLightModel::BASE_COLOR;
        rttRoot->addChild(baseColor);
        auto * texture = new SoSceneTexture2;
        texture->size.setValue(side, side);
        texture->type = SoSceneTexture2::RGBA8;
        texture->transparencyFunction = SoSceneTexture2::NONE;
        texture->scene = root;
        rttRoot->addChild(texture);
        auto * uv = new SoTextureCoordinate2;
        uv->point.set1Value(0, SbVec2f(0, 0));
        uv->point.set1Value(1, SbVec2f(1, 0));
        uv->point.set1Value(2, SbVec2f(1, 1));
        uv->point.set1Value(3, SbVec2f(0, 1));
        rttRoot->addChild(uv);
        auto * coords = new SoCoordinate3;
        coords->point.set1Value(0, SbVec3f(-3, -3, 0));
        coords->point.set1Value(1, SbVec3f(3, -3, 0));
        coords->point.set1Value(2, SbVec3f(3, 3, 0));
        coords->point.set1Value(3, SbVec3f(-3, 3, 0));
        rttRoot->addChild(coords);
        auto * face = new SoIndexedFaceSet;
        const int32_t indices[] = {0, 1, 2, 3, -1};
        face->coordIndex.setValues(0, 5, indices);
        face->textureCoordIndex.setValues(0, 5, indices);
        rttRoot->addChild(face);
        CoinRenderOptions options{};
        options.sceneTexture = COIN_RENDER_SCENE_TEXTURE_DIRECT;
        options.transparency = COIN_RENDER_TRANSPARENCY_OBJECT;
        CoinRenderTarget * rttTarget = CoinRenderTarget::createOffscreen(
          SbVec2i32(side, side), options);
        CoinRenderAction rttAction(SbViewportRegion(side, side));
        rttAction.setRenderTarget(rttTarget);
        rttAction.apply(rttRoot);
        std::vector<unsigned char> rttEight, rttFour;
        const bool eightOk = rttAction.getLastStatus() == CoinRenderAction::SUCCESS;
        if (eightOk) rttTarget->readbackRGBA(rttEight);
        fifth->on = FALSE;
        for (auto * light : extraLights) light->on = FALSE;
        rttAction.apply(rttRoot);
        const bool fourOk = rttAction.getLastStatus() == CoinRenderAction::SUCCESS;
        if (fourOk) rttTarget->readbackRGBA(rttFour);
        fifth->on = TRUE;
        for (auto * light : extraLights) light->on = TRUE;
        int rttDelta = 0;
        if (eightOk && fourOk && rttEight.size() == rttFour.size())
          for (int y = 20; y < 105; ++y)
            for (int x = 20; x < 105; ++x)
              rttDelta = std::max(rttDelta, std::abs(
                luminanceRgba(rttEight, x, y) - luminanceRgba(rttFour, x, y)));
        int glRttDelta = 0;
        bool glRttOk = true;
        if (compareGl) {
          auto * glEightRoot = static_cast<SoSeparator *>(rttRoot->copy(TRUE));
          glEightRoot->ref();
          fifth->on = FALSE;
          for (auto * light : extraLights) light->on = FALSE;
          auto * glFourRoot = static_cast<SoSeparator *>(rttRoot->copy(TRUE));
          glFourRoot->ref();
          fifth->on = TRUE;
          for (auto * light : extraLights) light->on = TRUE;
          SoOffscreenRenderer rttGl(SbViewportRegion(side, side));
          rttGl.setComponents(SoOffscreenRenderer::RGB);
          std::vector<unsigned char> glEightPixels, glFourPixels;
          glRttOk = render(rttGl, glEightRoot, glEightPixels) &&
            render(rttGl, glFourRoot, glFourPixels);
          if (glRttOk)
            for (int y = 20; y < 105; ++y)
              for (int x = 20; x < 105; ++x)
                glRttDelta = std::max(glRttDelta, std::abs(
                  luminance(glEightPixels, x, y) -
                  luminance(glFourPixels, x, y)));
          glFourRoot->unref();
          glEightRoot->unref();
        }
        std::cout << "eight lights direct RTT Coin/GL/BGFX delta=" <<
          glRttDelta << '/' << rttDelta << '\n';
        twoLightBgfxSubmitted = eightOk && fourOk && rttDelta > 0 &&
          (!compareGl || (glRttOk && glRttDelta > 0 &&
            std::abs(glRttDelta - rttDelta) <= 180));
        if (!twoLightBgfxSubmitted)
          std::cerr << "BGFX eight-light direct RTT: " <<
            rttAction.getLastError().getString() << " delta=" << rttDelta << '\n';
        rttAction.setRenderTarget(nullptr);
        delete rttTarget;
        rttRoot->unref();
      }
      for (auto it = extraLights.rbegin(); it != extraLights.rend(); ++it)
        group->removeChild(*it);
      group->removeChild(fifth);
      auto * lateSpot = new SoShadowSpotLight;
      lateSpot->location.setValue(0.0f, 4.0f, 4.0f);
      lateSpot->direction.setValue(0.0f, -3.0f, -5.0f);
      lateSpot->cutOffAngle = 0.9f;
      group->insertChild(lateSpot, 4);
      const uint64_t eightSerial = dualTarget->getLastSubmissionSerial();
      dualAction.apply(root);
      std::vector<unsigned char> afterRejectedSpot;
      dualTarget->readbackRGBA(afterRejectedSpot);
      twoLightBgfxSubmitted = twoLightBgfxSubmitted &&
        dualAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
        dualTarget->getLastSubmissionSerial() == eightSerial &&
        afterRejectedSpot == afterEight;
      group->removeChild(lateSpot);
      group->removeChild(fourth);
      group->removeChild(third);
      dualAction.apply(root);
      std::vector<unsigned char> recovered;
      if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(recovered);
      twoLightBgfxSubmitted = twoLightBgfxSubmitted &&
        dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        dualTarget->getLastSubmissionSerial() > fiveSerial && recovered == dualPixels;
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
      SoOffscreenRenderer multiGl(SbViewportRegion(side, side));
      multiGl.setComponents(SoOffscreenRenderer::RGB);
      const bool compareGl = std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") != nullptr;
      std::vector<unsigned char> glDual, glThree, glFour;
      if (compareGl) {
        thirdShadowLight->on = FALSE;
        auto * copy = static_cast<SoSeparator *>(root->copy(TRUE));
        copy->ref();
        twoLightSubmittedOnGpu = render(multiGl, copy, glDual);
        copy->unref();
        thirdShadowLight->on = TRUE;
      }
      dualAction.apply(root);
      std::vector<unsigned char> afterThreeLights;
      if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(afterThreeLights);
      const bool threeSubmitted =
        dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        dualTarget->getLastSubmissionSerial() > serial &&
        afterThreeLights.size() == dualGpu.size() && afterThreeLights != dualGpu;
      if (!threeSubmitted)
        std::cerr << "wgpu three-light frame: " << dualAction.getLastError().getString() << '\n';
      twoLightSubmittedOnGpu = twoLightSubmittedOnGpu && threeSubmitted;
      if (threeSubmitted && compareGl) {
        auto * copy = static_cast<SoSeparator *>(root->copy(TRUE));
        copy->ref();
        twoLightSubmittedOnGpu = render(multiGl, copy, glThree);
        copy->unref();
        int gpuDelta = 0, glDelta = 0;
        if (twoLightSubmittedOnGpu)
          for (int y = 20; y < 105; ++y)
            for (int x = 20; x < 105; ++x) {
              gpuDelta = std::max(gpuDelta, std::abs(
                luminanceRgba(dualGpu, x, y) - luminanceRgba(afterThreeLights, x, y)));
              glDelta = std::max(glDelta, std::abs(
                luminance(glDual, x, y) - luminance(glThree, x, y)));
            }
        std::cout << "three lights Coin/GL/wgpu delta=" << glDelta << '/' << gpuDelta << '\n';
        twoLightSubmittedOnGpu = twoLightSubmittedOnGpu && glDelta > 0 &&
          gpuDelta > 0 && std::abs(glDelta - gpuDelta) <= 130;
      }
      auto * fourthShadowLight = new SoShadowDirectionalLight;
      fourthShadowLight->direction.setValue(0.5f, -0.4f, -1.0f);
      fourthShadowLight->intensity = 0.35f;
      group->insertChild(fourthShadowLight, 3);
      dualAction.apply(root);
      std::vector<unsigned char> afterFourLights;
      if (dualAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(afterFourLights);
      const bool fourSubmitted = dualAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        afterFourLights.size() == dualGpu.size();
      if (!fourSubmitted)
        std::cerr << "wgpu four-light frame: " << dualAction.getLastError().getString() << '\n';
      twoLightSubmittedOnGpu = twoLightSubmittedOnGpu && fourSubmitted;
      if (fourSubmitted && compareGl) {
        auto * copy = static_cast<SoSeparator *>(root->copy(TRUE));
        copy->ref();
        twoLightSubmittedOnGpu = render(multiGl, copy, glFour);
        copy->unref();
        int gpuDelta = 0, glDelta = 0;
        if (twoLightSubmittedOnGpu)
          for (int y = 20; y < 105; ++y)
            for (int x = 20; x < 105; ++x) {
              gpuDelta = std::max(gpuDelta, std::abs(
                luminanceRgba(afterThreeLights, x, y) - luminanceRgba(afterFourLights, x, y)));
              glDelta = std::max(glDelta, std::abs(
                luminance(glThree, x, y) - luminance(glFour, x, y)));
            }
        std::cout << "four lights Coin/GL/wgpu delta=" << glDelta << '/' << gpuDelta << '\n';
        twoLightSubmittedOnGpu = twoLightSubmittedOnGpu && glDelta > 0 &&
          gpuDelta > 0 && std::abs(glDelta - gpuDelta) <= 130;
      }
      auto * fifthShadowLight = new SoShadowSpotLight;
      fifthShadowLight->location.setValue(0.0f, 4.0f, 4.0f);
      fifthShadowLight->direction.setValue(0.0f, -3.0f, -5.0f);
      fifthShadowLight->cutOffAngle = 0.9f;
      group->insertChild(fifthShadowLight, 4);
      CoinRenderAction fiveCapture(SbViewportRegion(side, side));
      fiveCapture.apply(root);
      std::string fiveDiagnostic;
      const bool fiveCoreQualified =
        fiveCapture.getPimpl()->lastRejectedShadowPlan.passes.size() == 5 &&
        coin_render_shadow_opaque_profile(
          fiveCapture.getPimpl()->lastRejectedShadowFrame,
          fiveCapture.getPimpl()->lastRejectedShadowPlan, 5, fiveDiagnostic);
      const uint64_t fourSerial = dualTarget->getLastSubmissionSerial();
      dualAction.apply(root);
      std::vector<unsigned char> afterFiveLights;
      dualTarget->readbackRGBA(afterFiveLights);
      twoLightSubmittedOnGpu = twoLightSubmittedOnGpu && fiveCoreQualified &&
        dualAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
        dualTarget->getLastSubmissionSerial() == fourSerial &&
        afterFiveLights == afterFourLights;
      std::vector<SoShadowSpotLight *> extraLights;
      for (int i = 0; i < 3; ++i) {
        auto * extra = new SoShadowSpotLight;
        extra->location.setValue(float(i - 1), 4.0f, 4.0f);
        extra->direction.setValue(float(1 - i), -3.0f, -5.0f);
        extra->cutOffAngle = 0.9f;
        group->insertChild(extra, 5 + i);
        extraLights.push_back(extra);
      }
      CoinRenderAction eightCapture(SbViewportRegion(side, side));
      eightCapture.apply(root);
      std::string eightDiagnostic;
      const bool eightCoreQualified =
        eightCapture.getPimpl()->lastRejectedShadowPlan.passes.size() == 8 &&
        coin_render_shadow_opaque_profile(
          eightCapture.getPimpl()->lastRejectedShadowFrame,
          eightCapture.getPimpl()->lastRejectedShadowPlan, 8, eightDiagnostic);
      twoLightSubmittedOnGpu = twoLightSubmittedOnGpu && eightCoreQualified;
      for (auto it = extraLights.rbegin(); it != extraLights.rend(); ++it)
        group->removeChild(*it);
      group->removeChild(fifthShadowLight);
      group->removeChild(fourthShadowLight);
      group->removeChild(thirdShadowLight);
      CoinRenderAction recoveryAction(SbViewportRegion(side, side));
      recoveryAction.setRenderTarget(dualTarget);
      recoveryAction.apply(root);
      std::vector<unsigned char> recoveredDual;
      if (recoveryAction.getLastStatus() == CoinRenderAction::SUCCESS)
        dualTarget->readbackRGBA(recoveredDual);
      twoLightSubmittedOnGpu = twoLightSubmittedOnGpu &&
        recoveryAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        dualTarget->getLastSubmissionSerial() > fourSerial &&
        recoveredDual == dualGpu;
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
      directionalPlan.passes[0].perFragmentLighting;
  group->quality = 0.2f;
  action.apply(root);
  const auto & lowQualityFrame = action.getPimpl()->lastRejectedShadowFrame;
  const auto & lowQualityPlan = action.getPimpl()->lastRejectedShadowPlan;
  std::string lowQualityDiagnostic;
  bool lowQualityDirectionalQualified = directionalCaptured &&
    lowQualityPlan.passes.size() == 1 &&
    !lowQualityPlan.passes[0].perFragmentLighting &&
    coin_render_shadow_single_directional_opaque_profile(
      lowQualityFrame, lowQualityPlan, lowQualityDiagnostic);
  if (lowQualityDirectionalQualified && !lowQualityFrame.draws.empty()) {
    CoinRenderFramePlan withSpecular = lowQualityFrame;
    withSpecular.materials[withSpecular.renderStates[
      withSpecular.draws[0].renderStateSlot].materialSlot].specular[0] = 0.1f;
    std::string excludedDiagnostic;
    lowQualityDirectionalQualified = !coin_render_shadow_single_directional_opaque_profile(
      withSpecular, lowQualityPlan, excludedDiagnostic);
    CoinRenderFramePlan withSmoothNormal = lowQualityFrame;
    const auto & geometry = withSmoothNormal.draws[0].geometry;
    if (geometry.indexCount >= 3) {
      const uint32_t vertex = withSmoothNormal.indices[geometry.firstIndex + 1];
      withSmoothNormal.vertices[vertex].normal[0] += 0.1f;
      lowQualityDirectionalQualified = lowQualityDirectionalQualified &&
        !coin_render_shadow_single_directional_opaque_profile(
          withSmoothNormal, lowQualityPlan, excludedDiagnostic);
    } else lowQualityDirectionalQualified = false;
  }
  const bool lowQualityGpuRequested =
    std::getenv("COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU") ||
    std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU");
  int lowQualityGpuDelta = 0;
  if (lowQualityDirectionalQualified && lowQualityGpuRequested) {
    CoinRenderTarget * lowTarget = CoinRenderTarget::createOffscreen(SbVec2i32(side, side));
    CoinRenderAction lowAction(SbViewportRegion(side, side));
    lowAction.setRenderTarget(lowTarget);
    lowAction.apply(root);
    std::vector<unsigned char> gpuShadow, gpuNoReceive;
    const bool shadowOk = lowAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (shadowOk) lowTarget->readbackRGBA(gpuShadow);
    groundStyle->style = SoShadowStyle::NO_SHADOWING;
    lowAction.apply(root);
    const bool clearOk = lowAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (clearOk) lowTarget->readbackRGBA(gpuNoReceive);
    groundStyle->style = SoShadowStyle::SHADOWED;
    lowQualityDirectionalQualified = shadowOk && clearOk &&
      gpuShadow.size() == gpuNoReceive.size() && !gpuShadow.empty();
    if (lowQualityDirectionalQualified)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          lowQualityGpuDelta = std::max(lowQualityGpuDelta,
            std::abs(luminanceRgba(gpuNoReceive, x, y) -
                     luminanceRgba(gpuShadow, x, y)));
    lowQualityDirectionalQualified = lowQualityDirectionalQualified &&
      lowQualityGpuDelta > 50;
    if (!lowQualityDirectionalQualified)
      std::cerr << "low-quality directional GPU: "
                << lowAction.getLastError().getString() << " delta="
                << lowQualityGpuDelta << '\n';
    lowAction.setRenderTarget(nullptr);
    delete lowTarget;
  }
  if (lowQualityDirectionalQualified && lowQualityGpuRequested &&
      std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
    auto * glShadowRoot = static_cast<SoSeparator *>(root->copy(TRUE));
    glShadowRoot->ref();
    auto * glLitRoot = static_cast<SoSeparator *>(root->copy(TRUE));
    glLitRoot->ref();
    auto * glGroup = static_cast<SoShadowGroup *>(glLitRoot->getChild(1));
    auto * glGround = static_cast<SoSeparator *>(glGroup->getChild(2));
    static_cast<SoShadowStyle *>(glGround->getChild(0))->style =
      SoShadowStyle::NO_SHADOWING;
    SoOffscreenRenderer lowGl(SbViewportRegion(side, side));
    lowGl.setComponents(SoOffscreenRenderer::RGB);
    std::vector<unsigned char> glShadow, glNoReceive;
    const bool rendered = render(lowGl, glShadowRoot, glShadow) &&
      render(lowGl, glLitRoot, glNoReceive);
    int glDelta = 0;
    if (rendered)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          glDelta = std::max(glDelta,
            std::abs(luminance(glNoReceive, x, y) -
                     luminance(glShadow, x, y)));
    lowQualityDirectionalQualified = rendered && glDelta > 50 &&
      std::abs(glDelta - lowQualityGpuDelta) <= 120;
    std::cout << "low-quality directional Coin/GL/GPU delta="
              << glDelta << '/' << lowQualityGpuDelta << '\n';
    glLitRoot->unref();
    glShadowRoot->unref();
  }
  group->quality = 0.5f;
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
  auto * customRoot = new SoSeparator;
  customRoot->ref();
  auto * customCamera = new SoOrthographicCamera;
  customCamera->position.setValue(0, 0, 8);
  customCamera->height = 7;
  customCamera->nearDistance = 1;
  customCamera->farDistance = 20;
  customRoot->addChild(customCamera);
  auto * customGroup = new SoShadowGroup;
  customGroup->quality = 1.0f;
  customRoot->addChild(customGroup);
  auto * customLight = new SoShadowSpotLight;
  customLight->location.setValue(2, 2, 4);
  customLight->direction.setValue(-2, -2, -5);
  customLight->cutOffAngle = 0.9f;
  customGroup->addChild(customLight);
  auto * selectedCaster = new SoCube;
  selectedCaster->width = selectedCaster->height = selectedCaster->depth = 1.4f;
  customGroup->addChild(selectedCaster); // Direct child, identity model.
  auto * otherCaster = new SoSeparator;
  auto * otherMove = new SoTranslation;
  otherMove->translation.setValue(1.7f, 0.0f, 0.0f);
  otherCaster->addChild(otherMove);
  auto * otherRotation = new SoRotation;
  otherRotation->rotation.setValue(SbVec3f(0, 0, 1), 0.35f);
  otherCaster->addChild(otherRotation);
  auto * otherScale = new SoScale;
  otherScale->scaleFactor.setValue(1.2f, 0.8f, 1.0f);
  otherCaster->addChild(otherScale);
  auto * otherCube = new SoCube;
  otherCube->width = otherCube->height = otherCube->depth = 1.2f;
  otherCaster->addChild(otherCube);
  auto * nestedCaster = new SoSeparator;
  auto * nestedMove = new SoTranslation;
  nestedMove->translation.setValue(0.0f, 0.4f, 0.0f);
  nestedCaster->addChild(nestedMove);
  auto * nestedTransform = new SoTransform;
  nestedTransform->translation.setValue(0.1f, 0.0f, 0.0f);
  nestedTransform->rotation.setValue(SbVec3f(0, 1, 0), 0.25f);
  nestedTransform->scaleFactor.setValue(0.9f, 1.1f, 1.0f);
  nestedCaster->addChild(nestedTransform);
  auto * nestedCube = new SoCube;
  nestedCube->width = nestedCube->height = nestedCube->depth = 0.7f;
  nestedCaster->addChild(nestedCube);
  otherCaster->addChild(nestedCaster);
  customGroup->addChild(otherCaster);
  otherCaster->ref();
  customGroup->removeChild(otherCaster);
  customGroup->insertChild(otherCaster, 1); // Isolated subtree follows only the light.
  otherCaster->unref();
  auto * customGround = new SoSeparator;
  auto * customGroundStyle = new SoShadowStyle;
  customGroundStyle->style = SoShadowStyle::SHADOWED;
  customGround->addChild(customGroundStyle);
  auto * customGroundMove = new SoTranslation;
  customGroundMove->translation.setValue(0, 0, -1.5f);
  customGround->addChild(customGroundMove);
  auto * customFloor = new SoCube;
  customFloor->width = customFloor->height = 6;
  customFloor->depth = 0.05f;
  customGround->addChild(customFloor);
  customGroup->addChild(customGround);
  customLight->shadowMapScene = selectedCaster;
  action.apply(customRoot);
  const CoinRenderFramePlan customFrame = action.getPimpl()->lastRejectedShadowFrame;
  const CoinRenderShadowPlan customPlan = action.getPimpl()->lastRejectedShadowPlan;
  std::string customDiagnostic;
  bool directCustomSceneQualified =
    action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
    customFrame.shadowLights.size() == 1 &&
    customFrame.shadowLights[0].customSceneDirectShape &&
    customPlan.passes.size() == 1 && customPlan.passes[0].casterDraws.size() == 1 &&
    coin_render_shadow_single_spot_opaque_profile(
      customFrame, customPlan, customDiagnostic);
  customLight->shadowMapScene = nullptr;
  action.apply(customRoot);
  directCustomSceneQualified = directCustomSceneQualified &&
    action.getPimpl()->lastRejectedShadowPlan.passes.size() == 1 &&
    action.getPimpl()->lastRejectedShadowPlan.passes[0].casterDraws.size() == 3;
  customLight->shadowMapScene = otherCaster;
  action.apply(customRoot);
  const CoinRenderFramePlan subtreeFrame = action.getPimpl()->lastRejectedShadowFrame;
  const CoinRenderShadowPlan subtreePlan = action.getPimpl()->lastRejectedShadowPlan;
  std::string subtreeDiagnostic;
  bool subtreeCustomSceneQualified =
    action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
    subtreeFrame.shadowLights.size() == 1 &&
    subtreeFrame.shadowLights[0].customSceneDirectSubtree &&
    subtreeFrame.shadowLights[0].customSceneShapeNodeIds.size() == 2 &&
    subtreePlan.passes.size() == 1 && subtreePlan.passes[0].casterDraws.size() == 2 &&
    std::abs(subtreeFrame.renderStates[subtreeFrame.draws[
      subtreePlan.passes[0].casterDraws[0]].renderStateSlot].model[0][1]) > 0.1f &&
    coin_render_shadow_single_spot_opaque_profile(
      subtreeFrame, subtreePlan, subtreeDiagnostic);
  customLight->shadowMapScene = nullptr;
  if (directCustomSceneQualified && lowQualityGpuRequested) {
    CoinRenderTarget * customTarget = CoinRenderTarget::createOffscreen(SbVec2i32(side, side));
    CoinRenderAction customAction(SbViewportRegion(side, side));
    customAction.setRenderTarget(customTarget);
    customLight->shadowMapScene = selectedCaster;
    customAction.apply(customRoot);
    std::vector<unsigned char> onlySelected, allCasters;
    const bool selectedOk = customAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (selectedOk) customTarget->readbackRGBA(onlySelected);
    customLight->shadowMapScene = nullptr;
    customAction.apply(customRoot);
    const bool allOk = customAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (allOk) customTarget->readbackRGBA(allCasters);
    int gpuDelta = 0;
    if (selectedOk && allOk && onlySelected.size() == allCasters.size())
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          gpuDelta = std::max(gpuDelta,
            std::abs(luminanceRgba(onlySelected, x, y) -
                     luminanceRgba(allCasters, x, y)));
    directCustomSceneQualified = selectedOk && allOk && gpuDelta > 40;
    if (directCustomSceneQualified) {
      const uint64_t beforeUnsupported = customTarget->getLastSubmissionSerial();
      customLight->shadowMapScene = customGround; // Styled subtree remains unqualified.
      customAction.apply(customRoot);
      std::vector<unsigned char> afterUnsupported;
      customTarget->readbackRGBA(afterUnsupported);
      directCustomSceneQualified =
        customAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
        customTarget->getLastSubmissionSerial() == beforeUnsupported &&
        afterUnsupported == allCasters;
      customLight->shadowMapScene = nullptr;
    }
    if (directCustomSceneQualified &&
        std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
      customLight->shadowMapScene = selectedCaster;
      auto * glSelected = static_cast<SoSeparator *>(customRoot->copy(TRUE));
      glSelected->ref();
      customLight->shadowMapScene = nullptr;
      auto * glAll = static_cast<SoSeparator *>(customRoot->copy(TRUE));
      glAll->ref();
      SoOffscreenRenderer customGl(SbViewportRegion(side, side));
      customGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> glOnly, glAllPixels;
      const bool rendered = render(customGl, glSelected, glOnly) &&
        render(customGl, glAll, glAllPixels);
      int glDelta = 0;
      if (rendered)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            glDelta = std::max(glDelta,
              std::abs(luminance(glOnly, x, y) - luminance(glAllPixels, x, y)));
      directCustomSceneQualified = rendered && glDelta > 40 &&
        std::abs(glDelta - gpuDelta) <= 180;
      std::cout << "direct shadowMapScene Coin/GL/GPU delta="
                << glDelta << '/' << gpuDelta << '\n';
      glAll->unref();
      glSelected->unref();
    }
    if (!directCustomSceneQualified)
      std::cerr << "direct shadowMapScene: "
                << customAction.getLastError().getString() << " delta="
                << gpuDelta << '\n';
    customAction.setRenderTarget(nullptr);
    delete customTarget;
  }
  if (subtreeCustomSceneQualified && lowQualityGpuRequested) {
    CoinRenderTarget * subtreeTarget = CoinRenderTarget::createOffscreen(SbVec2i32(side, side));
    CoinRenderAction subtreeAction(SbViewportRegion(side, side));
    subtreeAction.setRenderTarget(subtreeTarget);
    customLight->shadowMapScene = otherCaster;
    subtreeAction.apply(customRoot);
    std::vector<unsigned char> gpuOnly, gpuAll;
    const bool onlyOk = subtreeAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (onlyOk) subtreeTarget->readbackRGBA(gpuOnly);
    customLight->shadowMapScene = nullptr;
    subtreeAction.apply(customRoot);
    const bool allOk = subtreeAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (allOk) subtreeTarget->readbackRGBA(gpuAll);
    int gpuDelta = 0;
    if (onlyOk && allOk && gpuOnly.size() == gpuAll.size())
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          gpuDelta = std::max(gpuDelta, std::abs(
            luminanceRgba(gpuOnly, x, y) - luminanceRgba(gpuAll, x, y)));
    subtreeCustomSceneQualified = onlyOk && allOk && gpuDelta > 40;
    if (subtreeCustomSceneQualified &&
        std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
      customLight->shadowMapScene = otherCaster;
      auto * glOnlyRoot = static_cast<SoSeparator *>(customRoot->copy(TRUE));
      glOnlyRoot->ref();
      customLight->shadowMapScene = nullptr;
      auto * glAllRoot = static_cast<SoSeparator *>(customRoot->copy(TRUE));
      glAllRoot->ref();
      SoOffscreenRenderer subtreeGl(SbViewportRegion(side, side));
      subtreeGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> glOnly, glAll;
      const bool rendered = render(subtreeGl, glOnlyRoot, glOnly) &&
        render(subtreeGl, glAllRoot, glAll);
      int glDelta = 0;
      if (rendered)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            glDelta = std::max(glDelta, std::abs(
              luminance(glOnly, x, y) - luminance(glAll, x, y)));
      subtreeCustomSceneQualified = rendered && glDelta > 40 &&
        std::abs(glDelta - gpuDelta) <= 180;
      std::cout << "subtree shadowMapScene Coin/GL/GPU delta="
                << glDelta << '/' << gpuDelta << '\n';
      glAllRoot->unref();
      glOnlyRoot->unref();
    }
    if (!subtreeCustomSceneQualified)
      std::cerr << "subtree shadowMapScene: "
                << subtreeAction.getLastError().getString() << " delta="
                << gpuDelta << '\n';
    subtreeAction.setRenderTarget(nullptr);
    delete subtreeTarget;
  }
  bool stagedShadowRttQualified = true;
  bool directShadowRttQualified = true;
  bool shadowedRttReceiverQualified = true;
  if (lowQualityGpuRequested) {
    CoinRenderOptions stagedOptions{};
    stagedOptions.sceneTexture = COIN_RENDER_SCENE_TEXTURE_STAGED;
    stagedOptions.transparency = COIN_RENDER_TRANSPARENCY_OBJECT;
    CoinRenderTarget * stagedTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side), stagedOptions);
    CoinRenderAction stagedAction(SbViewportRegion(side, side));
    stagedAction.setRenderTarget(stagedTarget);
    auto * stagedRoot = new SoSeparator;
    stagedRoot->ref();
    auto * stagedCamera = new SoOrthographicCamera;
    stagedCamera->position.setValue(0, 0, 8);
    stagedCamera->height = 7;
    stagedCamera->nearDistance = 1;
    stagedCamera->farDistance = 20;
    stagedRoot->addChild(stagedCamera);
    auto * stagedLighting = new SoLightModel;
    stagedLighting->model = SoLightModel::BASE_COLOR;
    stagedRoot->addChild(stagedLighting);
    auto * stagedTexture = new SoSceneTexture2;
    stagedTexture->size.setValue(side, side);
    stagedTexture->type = SoSceneTexture2::RGBA8;
    stagedTexture->transparencyFunction = SoSceneTexture2::NONE;
    stagedTexture->scene = customRoot;
    stagedRoot->addChild(stagedTexture);
    auto * displayUv = new SoTextureCoordinate2;
    displayUv->point.set1Value(0, SbVec2f(0, 0));
    displayUv->point.set1Value(1, SbVec2f(1, 0));
    displayUv->point.set1Value(2, SbVec2f(1, 1));
    displayUv->point.set1Value(3, SbVec2f(0, 1));
    stagedRoot->addChild(displayUv);
    auto * displayPoints = new SoCoordinate3;
    displayPoints->point.set1Value(0, SbVec3f(-3, -3, 0));
    displayPoints->point.set1Value(1, SbVec3f(3, -3, 0));
    displayPoints->point.set1Value(2, SbVec3f(3, 3, 0));
    displayPoints->point.set1Value(3, SbVec3f(-3, 3, 0));
    stagedRoot->addChild(displayPoints);
    auto * display = new SoIndexedFaceSet;
    const int32_t displayIndices[] = {0, 1, 2, 3, -1};
    display->coordIndex.setValues(0, 5, displayIndices);
    display->textureCoordIndex.setValues(0, 5, displayIndices);
    stagedRoot->addChild(display);
    customGroup->isActive = TRUE;
    stagedAction.apply(stagedRoot);
    std::vector<unsigned char> stagedShadow, stagedClear;
    const bool shadowOk = stagedAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (shadowOk) stagedTarget->readbackRGBA(stagedShadow);
    customGroup->isActive = FALSE;
    stagedAction.apply(stagedRoot);
    const bool clearOk = stagedAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (clearOk) stagedTarget->readbackRGBA(stagedClear);
    int rttDelta = 0;
    if (shadowOk && clearOk && stagedShadow.size() == stagedClear.size())
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          rttDelta = std::max(rttDelta,
            std::abs(luminanceRgba(stagedShadow, x, y) -
                     luminanceRgba(stagedClear, x, y)));
    stagedShadowRttQualified = shadowOk && clearOk && rttDelta > 40;
    std::cout << "staged shadow RTT GPU delta=" << rttDelta << '\n';
    if (stagedShadowRttQualified &&
        std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
      customGroup->isActive = TRUE;
      auto * glShadowParent = static_cast<SoSeparator *>(stagedRoot->copy(TRUE));
      glShadowParent->ref();
      customGroup->isActive = FALSE;
      auto * glClearParent = static_cast<SoSeparator *>(stagedRoot->copy(TRUE));
      glClearParent->ref();
      SoOffscreenRenderer stagedGl(SbViewportRegion(side, side));
      stagedGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> glShadow, glClear;
      const bool rendered = render(stagedGl, glShadowParent, glShadow) &&
        render(stagedGl, glClearParent, glClear);
      int glDelta = 0;
      if (rendered)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            glDelta = std::max(glDelta,
              std::abs(luminance(glShadow, x, y) -
                       luminance(glClear, x, y)));
      stagedShadowRttQualified = rendered && glDelta > 40 &&
        std::abs(glDelta - rttDelta) <= 180;
      std::cout << "staged shadow RTT Coin/GL/GPU delta="
                << glDelta << '/' << rttDelta << '\n';
      glClearParent->unref();
      glShadowParent->unref();
    }
    if (stagedShadowRttQualified) {
      const uint64_t beforeFailure = stagedTarget->getLastSubmissionSerial();
      customGroup->isActive = TRUE;
#ifdef HAVE_COIN_BGFX
      setenv("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE", "1", 1);
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
      coin_wgpu_inject_fault(COIN_WGPU_FAULT_SHADOW_MAP_ALLOC);
#endif
      stagedAction.apply(stagedRoot);
#ifdef HAVE_COIN_BGFX
      unsetenv("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE");
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
      coin_wgpu_inject_fault(0);
#endif
      std::vector<unsigned char> afterFailure;
      stagedTarget->readbackRGBA(afterFailure);
      stagedShadowRttQualified =
        stagedAction.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
        stagedTarget->getLastSubmissionSerial() == beforeFailure &&
        afterFailure == stagedClear;
      if (stagedShadowRttQualified) {
        stagedAction.apply(stagedRoot);
        std::vector<unsigned char> recovered;
        if (stagedAction.getLastStatus() == CoinRenderAction::SUCCESS)
          stagedTarget->readbackRGBA(recovered);
        stagedShadowRttQualified =
          stagedAction.getLastStatus() == CoinRenderAction::SUCCESS &&
          recovered == stagedShadow;
      }
      if (stagedShadowRttQualified) {
        const int resized = 160;
        stagedShadowRttQualified = stagedTarget->resize(SbVec2i32(resized, resized));
        stagedAction.setViewportRegion(SbViewportRegion(resized, resized));
        stagedAction.apply(stagedRoot);
        std::vector<unsigned char> resizedShadow, resizedClear;
        if (stagedAction.getLastStatus() == CoinRenderAction::SUCCESS)
          stagedTarget->readbackRGBA(resizedShadow);
        customGroup->isActive = FALSE;
        stagedAction.apply(stagedRoot);
        if (stagedAction.getLastStatus() == CoinRenderAction::SUCCESS)
          stagedTarget->readbackRGBA(resizedClear);
        int resizedDelta = 0;
        if (resizedShadow.size() == size_t(resized * resized * 4) &&
            resizedClear.size() == resizedShadow.size())
          for (int y = 25; y < 130; ++y)
            for (int x = 25; x < 130; ++x) {
              const size_t pixel = size_t((y * resized + x) * 4);
              const int shadowValue = resizedShadow[pixel] +
                resizedShadow[pixel + 1] + resizedShadow[pixel + 2];
              const int clearValue = resizedClear[pixel] +
                resizedClear[pixel + 1] + resizedClear[pixel + 2];
              resizedDelta = std::max(resizedDelta,
                std::abs(clearValue - shadowValue));
            }
        stagedShadowRttQualified = stagedAction.getLastStatus() == CoinRenderAction::SUCCESS &&
          resizedDelta > 40;
        std::cout << "staged shadow RTT resized GPU delta=" << resizedDelta << '\n';
      }
    }
    if (!stagedShadowRttQualified)
      std::cerr << "staged shadow RTT: "
                << stagedAction.getLastError().getString() << '\n';
    customGroup->isActive = TRUE;
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE) || defined(HAVE_COIN_BGFX)
    CoinRenderOptions directOptions = stagedOptions;
    directOptions.sceneTexture = COIN_RENDER_SCENE_TEXTURE_DIRECT;
    CoinRenderTarget * directTarget = CoinRenderTarget::createOffscreen(
      SbVec2i32(side, side), directOptions);
    CoinRenderAction directAction(SbViewportRegion(side, side));
    directAction.setRenderTarget(directTarget);
    directAction.apply(stagedRoot);
    std::vector<unsigned char> directShadow, directClear;
    const bool directShadowOk = directAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (!directShadowOk)
      std::cerr << "direct shadow RTT producer: status=" << directAction.getLastStatus()
                << " error=" << directAction.getLastError().getString() << '\n';
    if (directShadowOk) directTarget->readbackRGBA(directShadow);
    customGroup->isActive = FALSE;
    directAction.apply(stagedRoot);
    const bool directClearOk = directAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (!directClearOk)
      std::cerr << "direct shadow RTT clear: status=" << directAction.getLastStatus()
                << " error=" << directAction.getLastError().getString() << '\n';
    if (directClearOk) directTarget->readbackRGBA(directClear);
    int directGpuDelta = 0, directGlDelta = 0;
    directShadowRttQualified = directShadowOk && directClearOk &&
      directShadow.size() == directClear.size();
    if (directShadowRttQualified)
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          directGpuDelta = std::max(directGpuDelta, std::abs(
            luminanceRgba(directShadow, x, y) -
            luminanceRgba(directClear, x, y)));
    if (directShadowRttQualified &&
        std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
      customGroup->isActive = TRUE;
      auto * glDirectShadow = static_cast<SoSeparator *>(stagedRoot->copy(TRUE));
      glDirectShadow->ref();
      customGroup->isActive = FALSE;
      auto * glDirectClear = static_cast<SoSeparator *>(stagedRoot->copy(TRUE));
      glDirectClear->ref();
      SoOffscreenRenderer directGl(SbViewportRegion(side, side));
      directGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> glShadowPixels, glClearPixels;
      directShadowRttQualified = render(directGl, glDirectShadow, glShadowPixels) &&
        render(directGl, glDirectClear, glClearPixels);
      if (directShadowRttQualified)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            directGlDelta = std::max(directGlDelta, std::abs(
              luminance(glShadowPixels, x, y) -
              luminance(glClearPixels, x, y)));
      glDirectClear->unref();
      glDirectShadow->unref();
    }
    std::cout << "direct shadow RTT Coin/GL/GPU delta=" <<
      directGlDelta << '/' << directGpuDelta << '\n';
    directShadowRttQualified = directShadowRttQualified && directGpuDelta > 40 &&
      (!std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") ||
       (directGlDelta > 40 && std::abs(directGlDelta - directGpuDelta) <= 180));
    if (directShadowRttQualified) {
      const uint64_t serial = directTarget->getLastSubmissionSerial();
      customGroup->isActive = TRUE;
#ifdef HAVE_COIN_BGFX
      setenv("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE", "1", 1);
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
      coin_wgpu_inject_fault(COIN_WGPU_FAULT_SHADOW_MAP_ALLOC);
#endif
      directAction.apply(stagedRoot);
#ifdef HAVE_COIN_BGFX
      unsetenv("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE");
#endif
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
      coin_wgpu_inject_fault(0);
#endif
      std::vector<unsigned char> afterFault;
      directTarget->readbackRGBA(afterFault);
      directShadowRttQualified =
        directAction.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
        directTarget->getLastSubmissionSerial() == serial &&
        afterFault == directClear;
      if (directShadowRttQualified) {
        directAction.apply(stagedRoot);
        std::vector<unsigned char> recovered;
        if (directAction.getLastStatus() == CoinRenderAction::SUCCESS)
          directTarget->readbackRGBA(recovered);
        directShadowRttQualified =
          directAction.getLastStatus() == CoinRenderAction::SUCCESS &&
          recovered == directShadow;
      }
    }
    if (directShadowRttQualified) {
      const int resized = 160;
      directShadowRttQualified = directTarget->resize(SbVec2i32(resized, resized));
      directAction.setViewportRegion(SbViewportRegion(resized, resized));
      directAction.apply(stagedRoot);
      std::vector<unsigned char> resizedShadow, resizedClear;
      if (directAction.getLastStatus() == CoinRenderAction::SUCCESS)
        directTarget->readbackRGBA(resizedShadow);
      customGroup->isActive = FALSE;
      directAction.apply(stagedRoot);
      if (directAction.getLastStatus() == CoinRenderAction::SUCCESS)
        directTarget->readbackRGBA(resizedClear);
      int resizedDelta = 0;
      if (resizedShadow.size() == size_t(resized * resized * 4) &&
          resizedClear.size() == resizedShadow.size())
        for (int y = 25; y < 130; ++y)
          for (int x = 25; x < 130; ++x)
            {
              const size_t pixel = size_t((y * resized + x) * 4);
              const int shadowValue = resizedShadow[pixel] +
                resizedShadow[pixel + 1] + resizedShadow[pixel + 2];
              const int clearValue = resizedClear[pixel] +
                resizedClear[pixel + 1] + resizedClear[pixel + 2];
              resizedDelta = std::max(resizedDelta,
                std::abs(clearValue - shadowValue));
            }
      directShadowRttQualified = directShadowRttQualified &&
        directAction.getLastStatus() == CoinRenderAction::SUCCESS &&
        resizedDelta > 40;
      std::cout << "direct shadow RTT resized GPU delta=" << resizedDelta << '\n';
    }
    if (!directShadowRttQualified)
      std::cerr << "direct shadow RTT: "
                << directAction.getLastError().getString() << '\n';
    directAction.setRenderTarget(nullptr);
    delete directTarget;
#endif
    customGroup->isActive = TRUE;
    if (stagedShadowRttQualified && directShadowRttQualified) {
      stagedLighting->model = SoLightModel::PHONG;
      stagedTexture->backgroundColor.setValue(0.15f, 0.15f, 0.15f, 1.0f);
      display->ref();
      stagedRoot->removeChild(display);
      auto * receiverGroup = new SoShadowGroup;
      receiverGroup->quality = 0.5f;
      auto * receiverLight = new SoShadowSpotLight;
      receiverLight->location.setValue(2, 2, 4);
      receiverLight->direction.setValue(-2, -2, -5);
      receiverLight->cutOffAngle = 0.9f;
      receiverGroup->addChild(receiverLight);
      auto * receiverCaster = new SoSeparator;
      auto * casterMove = new SoTranslation;
      casterMove->translation.setValue(0, 0, 1.4f);
      receiverCaster->addChild(casterMove);
      auto * casterCube = new SoCube;
      casterCube->width = casterCube->height = casterCube->depth = 1.4f;
      receiverCaster->addChild(casterCube);
      receiverGroup->addChild(receiverCaster);
      receiverGroup->addChild(display);
      display->unref();
      stagedRoot->addChild(receiverGroup);
      int receiverGlDelta = 0;
      if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
        receiverGroup->isActive = TRUE;
        auto * glActive = static_cast<SoSeparator *>(stagedRoot->copy(TRUE));
        glActive->ref();
        receiverGroup->isActive = FALSE;
        auto * glInactive = static_cast<SoSeparator *>(stagedRoot->copy(TRUE));
        glInactive->ref();
        SoOffscreenRenderer receiverGl(SbViewportRegion(side, side));
        receiverGl.setComponents(SoOffscreenRenderer::RGB);
        std::vector<unsigned char> activePixels, inactivePixels;
        const bool rendered = render(receiverGl, glActive, activePixels) &&
          render(receiverGl, glInactive, inactivePixels);
        shadowedRttReceiverQualified = rendered;
        if (rendered)
          for (int y = 20; y < 105; ++y)
            for (int x = 20; x < 105; ++x)
              receiverGlDelta = std::max(receiverGlDelta, std::abs(
                luminance(activePixels, x, y) -
                luminance(inactivePixels, x, y)));
        glInactive->unref();
        glActive->unref();
      }
      for (int mode = 0; mode < 2 && shadowedRttReceiverQualified; ++mode) {
        CoinRenderOptions receiverOptions = stagedOptions;
        receiverOptions.sceneTexture = mode == 0 ?
          COIN_RENDER_SCENE_TEXTURE_STAGED : COIN_RENDER_SCENE_TEXTURE_DIRECT;
        CoinRenderTarget * receiverTarget = CoinRenderTarget::createOffscreen(
          SbVec2i32(side, side), receiverOptions);
        CoinRenderAction receiverAction(SbViewportRegion(side, side));
        receiverAction.setRenderTarget(receiverTarget);
        receiverGroup->isActive = TRUE;
        receiverAction.apply(stagedRoot);
        std::vector<unsigned char> activePixels, inactivePixels;
        const bool activeOk = receiverAction.getLastStatus() == CoinRenderAction::SUCCESS;
        if (activeOk) receiverTarget->readbackRGBA(activePixels);
        receiverGroup->isActive = FALSE;
        receiverAction.apply(stagedRoot);
        const bool inactiveOk = receiverAction.getLastStatus() == CoinRenderAction::SUCCESS;
        if (inactiveOk) receiverTarget->readbackRGBA(inactivePixels);
        int gpuDelta = 0;
        if (activeOk && inactiveOk && activePixels.size() == inactivePixels.size())
          for (int y = 20; y < 105; ++y)
            for (int x = 20; x < 105; ++x)
              gpuDelta = std::max(gpuDelta, std::abs(
                luminanceRgba(activePixels, x, y) -
                luminanceRgba(inactivePixels, x, y)));
        std::cout << (mode == 0 ? "staged" : "direct") <<
          " RTT shadow receiver Coin/GL/GPU delta=" <<
          receiverGlDelta << '/' << gpuDelta << '\n';
        shadowedRttReceiverQualified = activeOk && inactiveOk && gpuDelta > 40 &&
          (!std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") ||
           (receiverGlDelta > 40 && std::abs(receiverGlDelta - gpuDelta) <= 200));
        if (shadowedRttReceiverQualified) {
          const uint64_t serial = receiverTarget->getLastSubmissionSerial();
          stagedTexture->transparencyFunction = SoSceneTexture2::ALPHA_BLEND;
          receiverGroup->isActive = TRUE;
          receiverAction.apply(stagedRoot);
          std::vector<unsigned char> afterRejected;
          receiverTarget->readbackRGBA(afterRejected);
          shadowedRttReceiverQualified =
            receiverAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
            receiverTarget->getLastSubmissionSerial() == serial &&
            afterRejected == inactivePixels;
          stagedTexture->transparencyFunction = SoSceneTexture2::NONE;
          if (shadowedRttReceiverQualified) {
            stagedTexture->backgroundColor.setValue(0.15f, 0.15f, 0.15f, 0.0f);
            receiverAction.apply(stagedRoot);
            receiverTarget->readbackRGBA(afterRejected);
            shadowedRttReceiverQualified =
              receiverAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
              receiverTarget->getLastSubmissionSerial() == serial &&
              afterRejected == inactivePixels;
            stagedTexture->backgroundColor.setValue(0.15f, 0.15f, 0.15f, 1.0f);
          }
        }
        if (!shadowedRttReceiverQualified)
          std::cerr << "RTT shadow receiver: " <<
            receiverAction.getLastError().getString() << '\n';
        receiverAction.setRenderTarget(nullptr);
        delete receiverTarget;
      }
      receiverGroup->isActive = TRUE;
    }
    stagedAction.setRenderTarget(nullptr);
    delete stagedTarget;
    stagedRoot->unref();
  }
  bool inheritedCustomSceneQualified = true;
  if (subtreeCustomSceneQualified && lowQualityGpuRequested) {
    auto * entryMove = new SoTranslation;
    entryMove->translation.setValue(0.4f, 0.2f, 0.0f);
    customRoot->insertChild(entryMove, 1);
    customLight->shadowMapScene = otherCaster;
    CoinRenderAction inheritedCapture(SbViewportRegion(side, side));
    inheritedCapture.apply(customRoot);
    const auto & inheritedFrame = inheritedCapture.getPimpl()->lastRejectedShadowFrame;
    const auto & inheritedPlan = inheritedCapture.getPimpl()->lastRejectedShadowPlan;
    std::string inheritedDiagnostic;
    inheritedCustomSceneQualified =
      inheritedFrame.shadowGroups.size() == 1 &&
      inheritedFrame.shadowGroups[0].entryModel != SbMatrix::identity() &&
      inheritedFrame.shadowLights.size() == 1 &&
      inheritedFrame.shadowLights[0].customSceneDirectSubtree &&
      coin_render_shadow_opaque_profile(inheritedFrame, inheritedPlan, 1,
                                       inheritedDiagnostic);
    CoinRenderTarget * inheritedTarget = CoinRenderTarget::createOffscreen(SbVec2i32(side, side));
    CoinRenderAction inheritedAction(SbViewportRegion(side, side));
    inheritedAction.setRenderTarget(inheritedTarget);
    inheritedAction.apply(customRoot);
    std::vector<unsigned char> gpuSelected, gpuAll;
    const bool selectedOk = inheritedAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (selectedOk) inheritedTarget->readbackRGBA(gpuSelected);
    customLight->shadowMapScene = nullptr;
    inheritedAction.apply(customRoot);
    const bool allOk = inheritedAction.getLastStatus() == CoinRenderAction::SUCCESS;
    if (allOk) inheritedTarget->readbackRGBA(gpuAll);
    int gpuDelta = 0, glDelta = 0;
    if (selectedOk && allOk && gpuSelected.size() == gpuAll.size())
      for (int y = 20; y < 105; ++y)
        for (int x = 20; x < 105; ++x)
          gpuDelta = std::max(gpuDelta, std::abs(
            luminanceRgba(gpuSelected, x, y) - luminanceRgba(gpuAll, x, y)));
    inheritedCustomSceneQualified = inheritedCustomSceneQualified &&
      selectedOk && allOk && gpuDelta > 40;
    if (inheritedCustomSceneQualified &&
        std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") && SoShadowGroup::isSupported()) {
      customLight->shadowMapScene = otherCaster;
      auto * glSelectedRoot = static_cast<SoSeparator *>(customRoot->copy(TRUE));
      glSelectedRoot->ref();
      customLight->shadowMapScene = nullptr;
      auto * glAllRoot = static_cast<SoSeparator *>(customRoot->copy(TRUE));
      glAllRoot->ref();
      SoOffscreenRenderer inheritedGl(SbViewportRegion(side, side));
      inheritedGl.setComponents(SoOffscreenRenderer::RGB);
      std::vector<unsigned char> glSelected, glAll;
      const bool rendered = render(inheritedGl, glSelectedRoot, glSelected) &&
        render(inheritedGl, glAllRoot, glAll);
      if (rendered)
        for (int y = 20; y < 105; ++y)
          for (int x = 20; x < 105; ++x)
            glDelta = std::max(glDelta, std::abs(
              luminance(glSelected, x, y) - luminance(glAll, x, y)));
      inheritedCustomSceneQualified = rendered && glDelta > 40 &&
        std::abs(glDelta - gpuDelta) <= 180;
      glAllRoot->unref();
      glSelectedRoot->unref();
    }
    std::cout << "inherited shadowMapScene Coin/GL/GPU delta=" <<
      glDelta << '/' << gpuDelta << '\n';
    if (!inheritedCustomSceneQualified)
      std::cerr << "inherited shadowMapScene: " <<
        inheritedAction.getLastError().getString() << " profile=" <<
        inheritedDiagnostic << '\n';
    inheritedAction.setRenderTarget(nullptr);
    delete inheritedTarget;
    customRoot->removeChild(entryMove);
  }
  customRoot->unref();
  action.setRenderTarget(nullptr);
  delete target;
  if (!publishedOk || !rejected || !preserved || !recovered || !captureOk ||
      !spotProfile || !transparentExcludedFromFirstProfile || !clippedShadowQualified || !shadowTargetsIndependent || !siblingShadowGroupsQualified || !ordinaryLightQualified || !texturedShadowQualified || !twoLightCaptured ||
      !orderedTwoLight || !twoSpotsOk || !twoDirectionalsOk ||
      !lateLightResolved || !lateSpotProfile || !mixedOrderResolved ||
      !lightEligibility || !spotRangeCaptured || !directionalCaptured ||
      !lowQualityDirectionalQualified || !directCustomSceneQualified ||
      !subtreeCustomSceneQualified || !inheritedCustomSceneQualified ||
      !stagedShadowRttQualified || !directShadowRttQualified ||
      !shadowedRttReceiverQualified ||
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
