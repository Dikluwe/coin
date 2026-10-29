// Resolve/present immutable, successfully completed PBR HDR radiance. No extra image,
// capture, semantic plan, geometry upload or lighting/SSAO pass is created.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"
using namespace coin_bgfx_internal;

void CoinBgfxBackend::encodeWindowScenePresent(bgfx::ViewId presentView) {
    bgfx::setViewName(presentView, "window_scene_present");
    bgfx::setViewMode(presentView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(presentView, 0, 0, this->width, this->height);
    bgfx::setViewFrameBuffer(presentView, this->frameBuffer);
    bgfx::setViewClear(presentView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(presentView, nullptr, nullptr);
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float pixelSize[4] = {1.0f / this->width, 1.0f / this->height, 0, 0};
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, this->fullscreenVertexBuffer);
    bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
    bgfx::setUniform(this->depthInfoUniform, pixelSize);
    bgfx::setTexture(0, this->layerSampler,
      bgfx::getTexture(this->windowSceneFrameBuffer, 0),
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
      BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(presentView, this->compositeProgram);
}

bool CoinBgfxBackend::canReusePbrPresentation(const CoinRenderFramePlan & frame,
 const CoinRenderTargetP & target,const CoinRenderFrameBorrow::Identity & previous) const {
  const auto identity=CoinRenderFrameBorrow::identityFor(frame);
  if(!this->pbrPresentationReuseEnabled || !identity || identity!=previous ||
     !this->presentToWindow || target.windowReadbackRequested || target.directTextureOutput ||
     target.needsReconfigure || target.lastSubmissionSerial!=this->pbrPresentationSerial || target.resourceGeneration!=this->pbrPresentationGeneration ||
     target.size[0]!=this->pbrPresentationWidth || target.size[1]!=this->pbrPresentationHeight ||
     !bgfx::isValid(this->pbrFrameBuffer) || !bgfx::isValid(this->frameBuffer) ||
     (bgfx::isValid(this->windowSceneFrameBuffer)&&!bgfx::isValid(this->compositeProgram)) || CoinRenderDiagnosticShell::phaseTracingEnabled())return false;
  const auto & a=target.options;const auto & b=this->pbrPresentationOptions;
  // These are all public mutable PBR controls; also compare the immutable
  // profile options so internal callers cannot reuse after changing admission.
  return a.renderer==b.renderer && a.transparency==b.transparency && a.sceneTexture==b.sceneTexture &&
    a.storedTextureColorSpace==b.storedTextureColorSpace && a.maxTextureAnisotropy==b.maxTextureAnisotropy &&
    a.textureSamplingPolicy==b.textureSamplingPolicy && a.rasterizationPolicy==b.rasterizationPolicy &&
    a.sampleCount==b.sampleCount && a.pbrExposureStops==b.pbrExposureStops &&
    a.pbrEnvironmentBundle==b.pbrEnvironmentBundle && a.pbrEnvironmentRotation==b.pbrEnvironmentRotation &&
    a.pbrEnvironmentIntensity==b.pbrEnvironmentIntensity &&
    a.pbrAnisotropicEnvironmentSamples==b.pbrAnisotropicEnvironmentSamples &&
    a.pbrAnisotropyProducts==b.pbrAnisotropyProducts && a.pbrAnisotropyCpuBudgetBytes==b.pbrAnisotropyCpuBudgetBytes && a.pbrEnvironmentBudgetBytes==b.pbrEnvironmentBudgetBytes &&
    a.pbrEnergyCompensation==b.pbrEnergyCompensation && a.pbrSpecularFilterVariance==b.pbrSpecularFilterVariance &&
    a.pbrSpecularFilterThreshold==b.pbrSpecularFilterThreshold && a.pbrEffects==b.pbrEffects;
}

void CoinBgfxBackend::rememberPbrPresentation(const CoinRenderFramePlan & frame,const CoinRenderTargetP & target) {
  if(!this->presentToWindow || !bgfx::isValid(this->pbrFrameBuffer))return;
  this->pbrPresentationIdentity=CoinRenderFrameBorrow::identityFor(frame);
  this->pbrPresentationOptions=target.options;
  this->pbrPresentationGeneration=target.resourceGeneration;
  this->pbrPresentationSerial=target.lastSubmissionSerial+1;
  this->pbrPresentationWidth=target.size[0];this->pbrPresentationHeight=target.size[1];
}

CoinRenderSubmitResult CoinBgfxBackend::reusePbrPresentation(const CoinRenderFramePlan & frame,CoinRenderTargetP & target) {
  const bool privateOutput=bgfx::isValid(this->windowSceneFrameBuffer);
  if(!this->reserveViews(privateOutput?2:1))return {CoinRenderBackendStatus::UNSUPPORTED,this->lastError};
  this->encodePbrOutput(this->viewBase,privateOutput?this->windowSceneFrameBuffer:this->frameBuffer);
  if(privateOutput)this->encodeWindowScenePresent(this->viewBase+1);
  if(COIN_BGFX_CONSUME_TEST_FAULT("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE")) {
#ifdef COIN_WGPU_TEST_HOOKS
    static_cast<CoinBgfxCallback *>(this->callback.get())->inject(bgfx::Fatal::DeviceLost,"injected cached presentation loss");
#endif
  }
  const auto before=this->checkRuntimeFailure("BGFX cached presentation submission failed");
  if(before!=CoinRenderBackendStatus::SUCCESS)return {before,this->lastError};
  bgfx::frame();
  // The acknowledged logical image is already published and immutable. Queue
  // only its resolve/present: the next frame provides normal renderer-thread
  // backpressure. Changed scenes still take the original resource-frame fence;
  // shutdown drains this pending copy before the host releases the native XID.
  // No extra empty resource frame is needed for a static repaint.
  const auto status=this->checkRuntimeFailure("BGFX cached PBR presentation failed");
  if(status!=CoinRenderBackendStatus::SUCCESS)return {status,this->lastError};
  target.colorBuffer.clear();target.depthBuffer.clear();target.needsReconfigure=false;
  this->rememberPbrPresentation(frame,target);++this->pbrStaticPresentations;
  COIN_BGFX_TEST_SUBMIT_SUCCEEDED();this->lastError.clear();
  return {CoinRenderBackendStatus::SUCCESS,"",++this->serial};
}
