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
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoDirectionalLight.h>
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
      coin_render_shadow_late_only_opaque_profile(
        lateLightFrame, lateLightPlan, lateProfileDiagnostic);
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
  std::vector<unsigned char> lateSpotPixels, lateSpotNoReceivePixels;
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
    const uint64_t lateSerial = lateTarget->getLastSubmissionSerial();
    light->ref();
    group->removeChild(light);
    group->insertChild(light, 1); // Mixed traversal order remains outside this profile.
    light->unref();
    lateAction.apply(root);
    std::vector<unsigned char> afterMixedOrder;
    lateTarget->readbackRGBA(afterMixedOrder);
    lateSpotSubmitted = lateSpotSubmitted &&
      lateAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      lateTarget->getLastSubmissionSerial() == lateSerial &&
      afterMixedOrder == lateSpotNoReceivePixels;
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
  if (std::getenv("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU")) {
    group->quality = 1.0f; // Coin's directional per-fragment profile.
    action.apply(root);
    std::string lateDirectionalDiagnostic;
    const bool lateDirectionalProfile =
      action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
      coin_render_shadow_late_only_opaque_profile(
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
        if (!wgpuDirectionalSubmitted)
          std::cerr << "wgpu directional Action: "
                    << directionalAction.getLastError().getString() << '\n';
        directionalAction.setRenderTarget(nullptr);
        delete directionalActionTarget;
      }
      delete directionalTarget;
    }
  }
#endif
  group->removeChild(directionalCapture);
  light->on = TRUE;
  action.setRenderTarget(nullptr);
  delete target;
  if (!publishedOk || !rejected || !preserved || !recovered || !captureOk ||
      !spotProfile || !transparentExcludedFromFirstProfile ||
      !lateLightResolved || !lateSpotProfile ||
      !lightEligibility || !spotRangeCaptured || !directionalCaptured ||
      !directionalProjectionCoversGroup
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
      || !wgpuShadowSubmitted || !wgpuDirectionalSubmitted || !lateSpotSubmitted
#endif
      ) {
    std::cerr << "CoinRender shadow rejection did not preserve publication or recovery"
              << " profile=" << spotProfile << " (" << spotProfileDiagnostic << ")"
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
  std::vector<unsigned char> directionalShadow, directionalNoReceive;
  ok = ok && render(gl, root, directionalShadow);
  groundStyle->style = SoShadowStyle::NO_SHADOWING;
  ok = ok && render(gl, root, directionalNoReceive);
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
  int directionalDifference = 0;
  for (int y = 20; y < 105; ++y)
    for (int x = 20; x < 105; ++x)
      directionalDifference = std::max(directionalDifference,
        luminance(directionalNoReceive, x, y) - luminance(directionalShadow, x, y));
  int lateDirectionalDifference = 0;
  for (int y = 20; y < 105; ++y)
    for (int x = 20; x < 105; ++x)
      lateDirectionalDifference = std::max(lateDirectionalDifference,
        luminance(lateDirectionalShadow, x, y) -
        luminance(lateDirectionalNoReceive, x, y));
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
       std::abs(wgpuSpotAtMax - luminance(lateLightActive,
                                         spotLateX, spotLateY)) > 180)) {
    std::cerr << "wgpu late-light style relation differs from Coin/GL reference\n";
    return 1;
  }
#endif
  std::cout << " directional_style_delta=" << directionalDifference
            << " late_directional_style_delta=" << lateDirectionalDifference
            << " spot_late_style_delta=" << spotLateDifference
            << " at=" << spotLateX << ',' << spotLateY
            << " values=" << luminance(lateLightActive, spotLateX, spotLateY)
            << ',' << luminance(lateLightNoReceive, spotLateX, spotLateY)
            << ',' << luminance(inactive, spotLateX, spotLateY) << '\n';
  if (clearShadow - castShadow < 100 || clearShadow - lateLightShadow < 100 ||
      noReceive - castShadow < 100 ||
      withoutCaster - castShadow < 100 ||
      std::abs(restoredShadow - castShadow) > 30 || centerDifference > 45 ||
      directionalDifference < 100 || lateDirectionalDifference < 100 ||
      spotLateDifference < 100) {
    std::cerr << "Coin/GL shadow, style or cache reference changed\n";
    return 1;
  }
  return 0;
}
