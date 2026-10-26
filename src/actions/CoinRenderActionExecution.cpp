#include "actions/CoinRenderActionIncludesP.h"

#include "actions/CoinRenderRttCaptureMemoP.h"

namespace {
size_t
captureStorageEstimate(SoNode * root)
{
  if (!root) return 0;
  // Inspect at most 64 nodes through two child levels. Broad groups already
  // supply a useful hint; their descendants are never scanned here.
  struct Pending { SoNode * node; unsigned depth; };
  Pending pending[64] = {{root, 0}};
  size_t next = 0, end = 1, estimate = 0;
  while (next < end) {
    const Pending current = pending[next++];
    const SoChildList * children = current.node->getChildren();
    if (!children) continue;
    const int count = children->getLength();
    estimate = std::max(estimate, std::min(size_t(count), size_t(65536)));
    if (current.depth == 2 || count >= 256) continue;
    for (int i = 0; i < count && end < 64; ++i)
      pending[end++] = {(*children)[i], current.depth + 1};
  }
  return estimate;
}

}

namespace {
bool appendPassSnapshot(CoinRenderFramePlan & output, const CoinRenderFramePlan & snapshot,
                        unsigned pass, std::string & diagnostic)
{
  if (!snapshot.shadowGroups.empty() || !snapshot.shadowLights.empty() || !snapshot.bumpImages.empty() ||
      !snapshot.rasterOccurrences.empty() || snapshot.hasPortableShaderDraws() || snapshot.smoothing) {
    diagnostic = "Snapshot passes exclude shadow, bump, raster, portable shaders and smoothing"; return false;
  }
  for (const auto & texture : snapshot.textures)
    if (texture.producerId || texture.gpuToken || texture.volumeDepth || texture.cubeFaceSize ||
        (texture.format != CoinRenderTextureFormat::RGBA8_LINEAR && texture.format != CoinRenderTextureFormat::RGBA8_SRGB)) {
      diagnostic = "Snapshot passes support local RGBA2D textures without RTT"; return false;
    }
  std::vector<CoinRenderCompositionItem> order;
  if (!coin_render_composition_order(snapshot, order, diagnostic)) return false;
  for (const auto & item : order)
    if (item.blend || item.deferred || item.screenDoorLevel || item.transparencyStrategy != CoinRenderCompositionItem::OBJECT) {
      diagnostic = "Snapshot passes require opaque ordinary geometry"; return false;
    }
  for (const auto & state : snapshot.renderStates)
    if (state.rasterPixels || state.shadowGroupSlot) {
      diagnostic = "Snapshot passes exclude raster and spatial shader states"; return false;
    }
  uint64_t bytes = uint64_t(output.vertices.size() + snapshot.vertices.size()) * sizeof(CoinRenderVertexSnapshot) +
    uint64_t(output.indices.size() + snapshot.indices.size()) * sizeof(uint32_t) +
    uint64_t(output.renderStates.size() + snapshot.renderStates.size()) * sizeof(CoinRenderRenderStateSnapshot);
  bytes += uint64_t(output.materials.size() + snapshot.materials.size()) * sizeof(CoinRenderMaterialSnapshot) +
    uint64_t(output.cameras.size() + snapshot.cameras.size()) * sizeof(CoinRenderCameraSnapshot) +
    uint64_t(output.viewports.size() + snapshot.viewports.size()) * sizeof(CoinRenderViewportSnapshot) +
    uint64_t(output.samplers.size() + snapshot.samplers.size()) * sizeof(CoinRenderSamplerSnapshot);
  for (const CoinRenderFramePlan * frame : {static_cast<const CoinRenderFramePlan *>(&output), &snapshot}) {
    for (const auto & texture : frame->textures) bytes += texture.pixelsRgba.size() + texture.mipmapsRgba.size();
    for (const auto & lighting : frame->lightingStates) bytes += lighting.lights.size() * sizeof(CoinRenderLightSourceSnapshot);
  }
  if (bytes > output.multipassBufferBudget) { diagnostic = "Snapshot payload exceeds multipass buffer budget"; return false; }
  const uint32_t vertexBase = static_cast<uint32_t>(output.vertices.size());
  const uint32_t indexBase = static_cast<uint32_t>(output.indices.size());
  const uint32_t materialBase = static_cast<uint32_t>(output.materials.size());
  const uint32_t lightingBase = static_cast<uint32_t>(output.lightingStates.size());
  const uint32_t cameraBase = static_cast<uint32_t>(output.cameras.size());
  const uint32_t viewportBase = static_cast<uint32_t>(output.viewports.size());
  const uint32_t textureBase = static_cast<uint32_t>(output.textures.size());
  const uint32_t samplerBase = static_cast<uint32_t>(output.samplers.size());
  const uint32_t stateBase = static_cast<uint32_t>(output.renderStates.size());
  output.passClearColors.push_back(snapshot.clearColor);
  output.materials.insert(output.materials.end(), snapshot.materials.begin(), snapshot.materials.end());
  output.lightingStates.insert(output.lightingStates.end(), snapshot.lightingStates.begin(), snapshot.lightingStates.end());
  output.cameras.insert(output.cameras.end(), snapshot.cameras.begin(), snapshot.cameras.end());
  output.viewports.insert(output.viewports.end(), snapshot.viewports.begin(), snapshot.viewports.end());
  output.textures.insert(output.textures.end(), snapshot.textures.begin(), snapshot.textures.end());
  output.samplers.insert(output.samplers.end(), snapshot.samplers.begin(), snapshot.samplers.end());
  for (auto vertex : snapshot.vertices) { vertex.materialSlot += materialBase; output.vertices.push_back(vertex); }
  for (uint32_t index : snapshot.indices) output.indices.push_back(index + vertexBase);
  for (auto state : snapshot.renderStates) {
    state.materialSlot += materialBase; state.lightingSlot += lightingBase;
    state.cameraSlot += cameraBase; state.viewportSlot += viewportBase;
    if (state.hasTexture) { state.textureImageSlot += textureBase; state.samplerSlot += samplerBase; }
    for (size_t slot = 0; slot < state.extraTextures.size(); ++slot) {
      auto & unit = state.extraTextures[slot];
      if (unit.enabled) { unit.imageSlot += textureBase; unit.samplerSlot += samplerBase; }
    }
    output.renderStates.push_back(std::move(state));
  }
  for (auto draw : snapshot.draws) {
    draw.geometry.firstVertex += vertexBase; draw.geometry.firstIndex += indexBase;
    draw.renderStateSlot += stateBase; draw.multipassMask = uint16_t(1u << pass);
    draw.stableNodeId = 0; // Snapshot-owned payload; never alias a previous mutable mesh.
    output.draws.push_back(draw);
  }
  return true;
}
}

template <typename F>
void CoinRenderActionP::executePassSnapshots(F traversalFn, SoNode * cacheRoot)
{
  const int count = this->numPasses;
  CoinRenderTarget * const destination = this->target;
  if (count < 1 || count > 16 || !destination || this->asyncTicket || this->planOnly ||
      destination->getOptions().sampleCount != 1 ||
      destination->getPimpl()->kind != CoinRenderTargetP::KIND_OFFSCREEN ||
      this->updateOrigin != SbVec2f(0, 0) || this->updateSize != SbVec2f(1, 1) || this->smoothing ||
      destination->getOptions().transparency != COIN_RENDER_TRANSPARENCY_COIN) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      count < 1 || count > 16 ? CoinRenderAction::INVALID_SCENE : !destination && this->asyncTicket ? CoinRenderAction::NO_TARGET : CoinRenderAction::UNSUPPORTED,
      CoinRenderDiagnosticDomain::ACTION, SbString("Snapshot callbacks require mono synchronous RGBA8 offscreen, one sample and a full update")));
    return;
  }
  auto saved = std::move(this->lastValidPlan);
  const bool hadSaved = this->hasLastValidPlan;
  const bool oldPlanOnly = this->planOnly;
  const auto callback = this->passCallback; void * const userdata = this->passUserdata;
  struct SnapshotScope {
    CoinRenderActionP * owner; CoinRenderFramePlan & saved; bool hadSaved, oldPlanOnly, committed = false;
    ~SnapshotScope() {
      if (!committed) { owner->lastValidPlan = std::move(saved); owner->hasLastValidPlan = hadSaved; }
      owner->planOnly = oldPlanOnly; owner->capturingPassSnapshots = owner->skipSnapshotPreRender = false;
      owner->currentPass = 0; owner->isApplying = false; owner->cachedRoot = nullptr;
      owner->clearTranslationProof();
    }
  } scope{this, saved, hadSaved, oldPlanOnly};
  this->capturingPassSnapshots = true;
  this->hasLastValidPlan = false; this->cachedRoot = nullptr;
  this->planOnly = true;
  try {
    CoinRenderFramePlan combined;
    combined.numPasses = count; combined.multipassBufferBudget = this->multipassBufferBudget;
    combined.textureSamplingPolicy = destination->getOptions().textureSamplingPolicy;
    combined.rasterizationPolicy = destination->getOptions().rasterizationPolicy;
    combined.transparency = this->transparencyOptions;
    combined.transparency.mode = destination->getOptions().transparency;
    combined.delayedObjDepthWrite = this->delayedObjDepthWrite != FALSE;
    for (int pass = 0; pass < count; ++pass) {
      if (pass) {
        this->currentPass = pass - 1;
        this->isApplying = true;
        callback(userdata, this->master);
        this->isApplying = false;
        if (this->hasReentrancyError || this->candidateAborted || this->invalidAbortCode) {
          this->setDiagnostic(CoinRenderDiagnosticShell::action(this->candidateAborted ? CoinRenderAction::ABORTED : CoinRenderAction::INVALID_SCENE,
            CoinRenderDiagnosticDomain::ACTION, SbString(this->invalidAbortCode ? "Abort callback returned an invalid code" : "Snapshot callback aborted or attempted nested apply")));
          return;
        }
      }
      if (this->numPasses != count || this->externalPass || this->target != destination) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
          CoinRenderDiagnosticDomain::ACTION, SbString("Snapshot callback changed target or sample mode"))); return;
      }
      this->currentPass = pass; this->skipSnapshotPreRender = pass != 0;
      if (pass) { CoinRenderFramePlanBuilder fresh; std::swap(this->builder, fresh); }
      this->executeApply(traversalFn, cacheRoot);
      if (this->lastStatus != CoinRenderAction::SUCCESS) return;
      if (this->numPasses != count || this->externalPass || this->target != destination) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
          CoinRenderDiagnosticDomain::ACTION, SbString("Snapshot capture changed target or sample mode"))); return;
      }
      for (const auto & camera : this->frameStereoStates)
        if (camera.mode != static_cast<int>(SoCamera::MONOSCOPIC)) {
          this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
            CoinRenderDiagnosticDomain::ACTION, SbString("Snapshot callbacks require a mono camera"))); return;
        }
      std::string diagnostic;
      if (!appendPassSnapshot(combined, this->lastValidPlan, unsigned(pass), diagnostic)) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(diagnostic.find("budget") != std::string::npos ? CoinRenderAction::OUT_OF_MEMORY : CoinRenderAction::UNSUPPORTED,
          CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(diagnostic.c_str()))); return;
      }
    }
    combined.clearColor = combined.passClearColors.front();
    combined.revision = CoinRenderFramePlanBuilder::nextRevision();
    std::string diagnostic;
    if (!combined.isValid(&diagnostic)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(diagnostic.c_str()))); return;
    }
    const auto result = destination->getPimpl()->executeFrame(combined);
    if (result.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(result)); return;
    }
    this->lastValidPlan = std::move(combined); this->hasLastValidPlan = true;
    this->recordingLogValid = false;
    scope.committed = true;
    this->setDiagnostic(CoinRenderDiagnosticShell::success());
  } catch (const std::bad_alloc &) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::OUT_OF_MEMORY,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Cannot allocate pass snapshots")));
  }
}

template <typename F>
void
CoinRenderActionP::executeApply(F traversalFn, SoNode * cacheRoot)
{
  if (this->isApplying) {
    this->hasReentrancyError = true;
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::ACTION,
      SbString("Nested apply() calls are not permitted on CoinRenderAction")));
    return;
  }
  if (!this->capturingPassSnapshots && !this->planOnly && !this->externalPass && this->passCallback && this->numPasses > 1) {
    this->executePassSnapshots(traversalFn, cacheRoot); return;
  }
  // Nested apply must not erase the outer capture's diagnostic counters.
  if (!this->isApplying) {
    this->captureCameraBasisCalls = this->captureCameraBasisPrepares = this->captureCameraBasisReuses = 0;
    this->captureCameraBasisPrepareMs = 0;
  }
  typedef std::chrono::steady_clock ProfileClock;
  const ProfileClock::time_point profileBegin = ProfileClock::now();
  if (this->target)
    this->executionOptions = this->target->pimpl->options;
  std::string optionsDiagnostic;
  if ((this->target && !this->target->pimpl->optionsDiagnostic.empty()) ||
      !coin_render_valid_options(this->executionOptions, optionsDiagnostic)) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::TARGET,
        SbString(this->target && !this->target->pimpl->optionsDiagnostic.empty()
                     ? this->target->pimpl->optionsDiagnostic.c_str()
                     : optionsDiagnostic.c_str())));
    return;
  }
  if (this->numPasses < 1 || this->numPasses > 16) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      this->numPasses < 1 ? CoinRenderAction::INVALID_SCENE : CoinRenderAction::UNSUPPORTED,
      CoinRenderDiagnosticDomain::ACTION, SbString("Multipass AA requires a requested count in 1..16")));
    return;
  }
  if (!this->externalPass && this->numPasses > 1 && this->multipassBufferBudget == 0) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::OUT_OF_MEMORY,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Multipass AA buffer budget is zero")));
    return;
  }
  const bool tracePhases = CoinRenderDiagnosticShell::phaseTracingEnabled();
  if (this->isApplying) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::ACTION,
        SbString("Nested apply() calls are not permitted on CoinRenderAction")));
    this->hasReentrancyError = true;
    return;
  }

  struct AnnotationScope {
    CoinRenderActionP * action;
    ~AnnotationScope() {
      for (const auto & entry : action->delayedAnnotations) entry.path->unref();
      action->delayedAnnotations.clear();
      for (const auto & entry : action->delayedOverlays) entry.path->unref();
      action->delayedOverlays.clear();
      action->replayingAnnotations = false;
      action->isApplying = false;
      if (!action->externalPass && !action->capturingPassSnapshots) action->currentPass = 0;
      if (action->stateInvalidationPending) action->master->invalidateState();
    }
  } annotationScope{this};
  this->isApplying = true;
  this->hasReentrancyError = false;
  this->candidateAborted = false;
  this->invalidAbortCode = false;
  this->invalidatedDuringApply = false;
  auto validateRequest = [&]() -> bool {
    for (int axis = 0; axis < 2; ++axis) {
      const float origin = this->updateOrigin[axis], size = this->updateSize[axis];
      if (!std::isfinite(origin) || !std::isfinite(size) || origin < 0 || size < 0 ||
          origin > 1 || size > 1 || origin + size > 1.0f) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
          CoinRenderDiagnosticDomain::ACTION, SbString("Invalid normalized update area")));
        return false;
      }
    }
    if (this->externalPass && (this->requestedExternalPass < 0 ||
        this->requestedExternalPass >= this->numPasses || this->numPasses > 16)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::ACTION, SbString("Invalid external pass index/count"))); return false;
    }
    if (this->externalPass && (this->asyncTicket || (this->target &&
        (this->target->getPimpl()->kind != CoinRenderTargetP::KIND_OFFSCREEN || this->target->getOptions().sampleCount != 1 ||
          this->target->getOptions().transparency != COIN_RENDER_TRANSPARENCY_COIN)))) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::ACTION, SbString("External samples require synchronous offscreen one-sample targets"))); return false;
    }
    if (this->delayedRenderType != CoinRenderAction::ONE_PASS &&
        this->delayedRenderType != CoinRenderAction::NONSOLID_SEPARATE_BACKFACE_PASS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::ACTION, SbString("Invalid delayed-object render type")));
      return false;
    }
    if (this->sortingStrategy < CoinRenderAction::BBOX_CENTER ||
        this->sortingStrategy > CoinRenderAction::CUSTOM_CALLBACK ||
        (this->sortingStrategy == CoinRenderAction::CUSTOM_CALLBACK && !this->sortingCallback)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::ACTION, SbString("Invalid object sorting strategy or missing callback")));
      return false;
    }
    if (this->smoothing && (this->asyncTicket || this->numPasses != 1 ||
        (this->target && (this->target->pimpl->kind != CoinRenderTargetP::KIND_OFFSCREEN ||
          this->target->getOptions().sampleCount != 1)))) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::ACTION, SbString("Smoothing requires mono synchronous RGBA8 with one sample/pass")));
      return false;
    }
    const bool partial = this->updateOrigin != SbVec2f(0, 0) || this->updateSize != SbVec2f(1, 1);
    if (partial && (!this->target || this->asyncTicket || this->planOnly ||
        this->target->pimpl->kind != CoinRenderTargetP::KIND_OFFSCREEN ||
        this->target->pimpl->directTextureOutput || this->target->getOptions().sampleCount != 1 ||
        this->numPasses != 1 || this->executionOptions.transparency != COIN_RENDER_TRANSPARENCY_COIN)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::ACTION, SbString("Partial updates require synchronous offscreen RGBA8, one sample/pass and Coin transparency")));
      return false;
    }
    if (this->asyncTicket && (!this->target || this->target->pimpl->kind != CoinRenderTargetP::KIND_OFFSCREEN)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(this->target ? CoinRenderAction::UNSUPPORTED : CoinRenderAction::NO_TARGET,
        CoinRenderDiagnosticDomain::ACTION, SbString("applyAsync requires an offscreen target")));
      return false;
    }
    if (this->target && this->target->getStatus() != CoinRenderTarget::TARGET_READY &&
        this->target->getStatus() != CoinRenderTarget::TARGET_LOST) {
      const CoinRenderTargetP * targetState = &this->target->getPimpl().get();
      // A transient timeout or lost surface leaves the target recoverable.
      // Let the next explicit apply reach executeFrame's prepareBackend
      // path; suspended/invalid targets must still stop before callbacks.
      const bool recoverableTarget =
        (this->target->getStatus() == CoinRenderTarget::TARGET_NOT_READY ||
         this->target->getStatus() == CoinRenderTarget::TARGET_SURFACE_LOST) &&
        !targetState->suspended && targetState->size[0] > 0 && targetState->size[1] > 0 &&
        (targetState->kind != CoinRenderTargetP::KIND_WINDOW ||
          (coin_render_surface_header_diagnostic(targetState->nativeDesc).empty() &&
           coin_render_surface_handles_diagnostic(targetState->nativeDesc).empty()));
      if (!recoverableTarget) {
        this->setDiagnostic(CoinRenderDiagnosticShell::fromTarget(this->target->getStatus(), this->target->getLastError()));
        return false;
      }
    }
    if (this->target) this->executionOptions = this->target->getOptions();
    std::string error;
    if (!coin_render_valid_options(this->executionOptions, error) || this->numPasses < 1 || this->numPasses > 16) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::ACTION, SbString("Invalid current render configuration")));
      return false;
    }
    return true;
  };
  if (!validateRequest()) return;
  const auto callbacks = this->skipSnapshotPreRender ? std::vector<PreRenderCallback>() : this->preRenderCallbacks;
  for (const auto & entry : callbacks) {
    entry.callback(entry.userdata, this->master);
    if (this->hasReentrancyError || this->candidateAborted || this->invalidAbortCode) break;
  }
  if (this->hasReentrancyError || this->invalidAbortCode) return;
  if (this->candidateAborted) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::ABORTED,
      CoinRenderDiagnosticDomain::ACTION, SbString("Render aborted by callback")));
    return;
  }
  if (!validateRequest()) return;
  if (this->stateInvalidationPending) {
    this->isApplying = false;
    this->master->invalidateState();
    this->isApplying = true;
  }
  const bool partialUpdate = this->updateOrigin != SbVec2f(0, 0) || this->updateSize != SbVec2f(1, 1);
  int updateRect[4] = {0, 0, 0, 0};
  if (partialUpdate) {
    const auto canvas = this->target->getSize();
    const auto viewport = this->master->getViewportRegion();
    const auto vpOrigin = viewport.getViewportOriginPixels(), vpSize = viewport.getViewportSizePixels();
    for (int axis = 0; axis < 2; ++axis) {
      const int first = std::max(0, std::max(int(vpOrigin[axis]), int(this->updateOrigin[axis] * canvas[axis])));
      const int last = std::min(int(canvas[axis]), std::min(int(vpOrigin[axis]) + int(vpSize[axis]),
        int(this->updateOrigin[axis] * canvas[axis]) + int(this->updateSize[axis] * canvas[axis])));
      updateRect[axis] = first;
      updateRect[axis + 2] = this->updateSize[axis] == 0 ? 0 : std::max(0, last - first);
    }
    if (!updateRect[2] || !updateRect[3]) {
      this->setDiagnostic(CoinRenderDiagnosticShell::success()); return;
    }
  }
  this->setDiagnostic(CoinRenderDiagnosticShell::success());
  struct RasterHistoryScope {
    CoinRenderFramePlanBuilder & builder;
    CoinRenderFramePlanBuilder::RasterHistory saved;
    bool committed = false;
    explicit RasterHistoryScope(CoinRenderFramePlanBuilder & owner)
      : builder(owner), saved(owner.rasterHistory()) {}
    ~RasterHistoryScope() { if (!committed) builder.restoreRasterHistory(saved); }
  } rasterHistoryScope(this->builder);
  // External callbacks can change capture or have observable side effects.
  // Registration after construction permanently requires their traversal.
  bool planCacheAllowed = this->managerCaptureSection < 0 && !this->externalPass && !this->capturingPassSnapshots && !partialUpdate && !this->abortCallback &&
    this->preRenderCallbacks.empty() && this->sortingStrategy == CoinRenderAction::BBOX_CENTER &&
    this->portableShaderModules.empty() &&
    this->master->callbackRegistrationRevision() == this->captureCallbackRevision &&
    !this->containsVRMLSnapshot(cacheRoot);
  if (planCacheAllowed && cacheRoot && cacheRoot == this->cachedRoot &&
      cacheRoot->getNodeId() == this->cachedRootId) {
    // SoCamera's stereo setters are not fields and do not touch the node.
    // Detect eye/offset changes before a cached plan or camera patch is used.
    for (const auto & saved : this->capturedStereoStates)
      if (saved.mode != static_cast<int>(saved.camera->getStereoMode()) ||
          saved.adjustment != saved.camera->getStereoAdjustment() ||
          saved.balance != saved.camera->getBalanceAdjustment()) {
        planCacheAllowed = false;
        break;
      }
  }

  const bool ownsSceneTexturePlan = !this->sceneTexturePlan;
  if (ownsSceneTexturePlan) {
    this->sceneTexturePlan =
        std::make_shared<CoinRenderRttPlan>(this->executionOptions.sceneTexture);
    this->sceneTextureCaptureMemo = std::make_shared<CoinRenderRttCaptureMemo>();
    this->sceneTextureCaptureMemo->allowed = planCacheAllowed;
    this->sceneTextureCaptureMemo->owner = this;
  }
  struct SceneTexturePlanScope {
    CoinRenderActionP * action;
    bool owns;
    ~SceneTexturePlanScope() {
      if (owns) {
        action->sceneTextureCaptureMemo.reset();
        action->sceneTexturePlan.reset();
      }
    }
  } sceneTexturePlanScope{this, ownsSceneTexturePlan};
  for (const auto& texture : this->lastValidPlan.textures)
    if (texture.producerId)
      planCacheAllowed = false;
  // The adapter rebuilds the list with clear/add on every sync. Compare its
  // final effective configuration once per logical apply, so changing only a
  // depth barrier is captured while identical lists keep the existing reuse.
  if (this->capturedManagerLayerBoundaries != this->managerLayerBoundaries) {
    this->cachedRoot = NULL;
    this->capturedManagerLayerBoundaries = this->managerLayerBoundaries;
  }
  const bool qualifyCapturedCamera = !this->hasLastValidPlan || this->cachedRoot != cacheRoot;
  const bool traversalSkipped = planCacheAllowed && cacheRoot && this->hasLastValidPlan &&
    this->cachedRoot == cacheRoot && this->cachedRootId == cacheRoot->getNodeId() &&
    !this->cameraOnlyDirty && !this->cameraPatchInvalidated && !this->translationInputDirty;
  CoinRenderFrameReuseDecision reuseDecision = traversalSkipped
    ? CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::REUSE,
                               this->lastValidPlan.revision)
    : CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::UNKNOWN, 0);
  this->sceneTexturePixels.clear();
  this->shadowSceneCaptures.clear();
  this->encounteredActiveBump = false;
  this->shadowStyleBeforeGroups.clear();
  this->managerHighlightDepth = 0;
  this->activeShadowGroupNodes.clear();
  this->lastRejectedShadowFrame = CoinRenderFramePlan();
  this->lastRejectedShadowPlan = CoinRenderShadowPlan();
  CoinRenderFramePlan plan;
  CoinRenderFramePreflight capturedPreflight;
  CoinRenderCameraOverlayUndo overlayUndo;
  CoinRenderObjectOverlayUndo translationUndo;
  struct CameraOverlayScope {
    CoinRenderFramePlan & frame;
    CoinRenderCameraOverlayUndo & undo;
    CoinRenderObjectOverlayUndo & translationUndo;
    bool committed;
    ~CameraOverlayScope() {
      if (!committed) {
        CoinRenderFrameReuseCore::rollbackCameraOverlay(frame, undo);
        CoinRenderFrameReuseCore::rollbackObjectOverlay(frame, translationUndo);
      }
    }
  } overlayScope{this->lastValidPlan, overlayUndo, translationUndo, false};
  ProfileClock::time_point profileTraversed = ProfileClock::now();
  ProfileClock::time_point profilePlanned = profileTraversed;
  const bool translationOverlay = !traversalSkipped && planCacheAllowed && !this->planOnly &&
    this->hasLastValidPlan && this->prepareTranslationOverlay(cacheRoot, translationUndo);
  const bool cameraOverlay = !translationOverlay && !traversalSkipped && planCacheAllowed &&
    !this->planOnly && this->hasLastValidPlan &&
    this->prepareCameraOverlay(cacheRoot, overlayUndo);
  if (cameraOverlay) {
    reuseDecision = CoinRenderFrameReuseDecision(
      CoinRenderFrameReuseKind::CAMERA_PATCH, overlayUndo.revision);
  }
  if (translationOverlay) {
    reuseDecision = CoinRenderFrameReuseDecision(
      CoinRenderFrameReuseKind::RESOURCE_REBUILD, translationUndo.revision);
  }
  if (!traversalSkipped && !cameraOverlay && !translationOverlay) {
    this->frameStereoStates.clear();
    this->beginTranslationCapture(planCacheAllowed && cacheRoot &&
      (qualifyCapturedCamera || this->translationInputDirty || this->cameraRecaptureRequired));
    this->builder.smoothing = this->smoothing != FALSE;
    this->builder.separateBackfaces = this->delayedRenderType == CoinRenderAction::NONSOLID_SEPARATE_BACKFACE_PASS;
    this->builder.delayedObjDepthWrite = this->delayedObjDepthWrite != FALSE;
    this->builder.beginFrame(this->backgroundColor, this->master->getViewportRegion(), this->externalPass ? 1 : this->numPasses);
    this->builder.sortingStrategy = static_cast<int>(this->sortingStrategy);
    this->builder.objectSortingEnabled = this->executionOptions.transparency == COIN_RENDER_TRANSPARENCY_COIN ||
      this->executionOptions.transparency == COIN_RENDER_TRANSPARENCY_OBJECT;
    this->builder.customSortingDistance = [this](SoCallbackAction *) {
      return this->sortingCallback(this->sortingUserdata, this->master);
    };
    this->managerCapturedLayers.clear();
    this->builder.configureTextures(this->executionOptions);
    this->builder.reserveCaptureStorage(captureStorageEstimate(cacheRoot));
    traversalFn();
    const uint32_t delayedLayers = (this->delayedOverlays.empty() ? 0 : 1) +
                                  (this->delayedAnnotations.empty() ? 0 : 1);
    if (delayedLayers && !this->master->hasTerminated()) {
      const uint32_t firstDelayedLayer = this->builder.reserveDelayedLayers(delayedLayers);
      for (uint32_t & layer : this->managerCapturedLayers)
        if (layer >= firstDelayedLayer) layer += delayedLayers;
      uint32_t layer = firstDelayedLayer;
      auto replay = [&](std::vector<DelayedAnnotation> & paths, bool clearDepth) {
        if (paths.empty() || this->master->hasTerminated()) return;
        std::stable_sort(paths.begin(), paths.end(),
          [](const DelayedAnnotation & a, const DelayedAnnotation & b) {
            return a.priority < b.priority;
          });
        this->builder.beginDelayedAnnotations(layer++, clearDepth);
        this->replayingAnnotations = true;
        for (const auto & entry : paths) {
          this->master->SoCallbackAction::apply(entry.path);
          if (this->master->hasTerminated()) break;
        }
        this->builder.endAnnotation();
        this->replayingAnnotations = false;
      };
      replay(this->delayedOverlays, false);
      replay(this->delayedAnnotations, true);
    }
    profileTraversed = ProfileClock::now();
  }
  // A node can register an observer while it traverses. Never retain a plan
  // or object proof based on the registration checked before that traversal.
  planCacheAllowed = planCacheAllowed && !this->invalidatedDuringApply &&
    this->master->callbackRegistrationRevision() == this->captureCallbackRevision;

  if (this->candidateAborted) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::ABORTED,
      CoinRenderDiagnosticDomain::ACTION, SbString("Render aborted by callback")));
    return;
  }

  if (this->hasReentrancyError) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::ACTION,
      SbString("Nested apply() calls are not permitted on CoinRenderAction")));
    return;
  }

  if (this->master->hasTerminated() && this->lastStatus != CoinRenderAction::SUCCESS) {
    return;
  }

  std::string err;
  // Pixel geometry is already projected. Camera/transform overlays cannot
  // patch it like object-space geometry, including a currently clipped node.
  if (this->builder.hasScreenContent()) planCacheAllowed = false;
  // Coordinate callbacks can depend on Coin state or mutable application data.
  // Snapshots may be reused by the executor only after a fresh capture.
  if (this->builder.hasCoordinateFunctions()) planCacheAllowed = false;
  CoinRenderTransparencyOptions capturedTransparency = this->transparencyOptions;
  capturedTransparency.mode = this->executionOptions.transparency;
  if (!traversalSkipped && !cameraOverlay && !translationOverlay &&
      !this->builder.build(plan, &err, true, &capturedTransparency,
                           this->target && !this->planOnly ? &capturedPreflight : nullptr)) {
    if (this->encounteredActiveBump) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("SoBumpMap requires a qualified bump/normal executor")));
      return;
    }
    const CoinRenderAction::Status status = this->builder.isUnsupportedBuild()
      ? CoinRenderAction::UNSUPPORTED : CoinRenderAction::INVALID_SCENE;
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      status, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(err.c_str())));
    return;
  }
  plan.numPasses = this->externalPass ? 1 : this->numPasses;
  if (this->externalPass) {
    if (!plan.shadowGroups.empty() || !plan.bumpImages.empty() || !plan.rasterOccurrences.empty() || plan.hasPortableShaderDraws()) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("External sample profile excludes spatial shaders and raster"))); return;
    }
    for (const auto & texture : plan.textures) if (texture.producerId || texture.gpuToken) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("External sample profile excludes RTT"))); return;
    }
    for (auto & state : plan.renderStates) {
      SbMatrix shifted;
      if (!CoinRenderMultipassCore::projection(state.projectionCoin, this->numPasses, this->requestedExternalPass,
          plan.viewports[state.viewportSlot], shifted)) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
          CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Invalid external sample projection"))); return;
      }
      state.projectionCoin = shifted;
    }
    capturedPreflight.invalidate();
  }
  plan.multipassBufferBudget = this->multipassBufferBudget;
  if (!CoinRenderMultipassCore::projectionsFinite(plan)) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Multipass translated projection is non-finite or invalid")));
    return;
  }


  plan.managerFrame = std::any_of(this->managerLayerBoundaries.begin(), this->managerLayerBoundaries.end(),
    [](const std::tuple<const SoNode *, bool, int> & boundary) {
      return std::get<2>(boundary) == COIN_RENDER_MANAGER_OWNER;
    });
  // Visiting a bump declaration is not consumption: a later empty map may
  // neutralize it before every shape. The builder captures and validates each
  // effective map while emitting its vertices; failed capture is still handled
  // above. Only emitted active image slots require the bump executor/profile.
  bool capturedActiveBump = false;
  { // Classification storage must not survive into admission or GPU execution.
    std::vector<CoinRenderBumpGpuVertex> capturedBumpStream;
    std::string bumpConsumptionDiagnostic;
    bool bumpConsumptionValid = coin_render_build_bump_gpu_stream(plan, capturedBumpStream,
                                                                 bumpConsumptionDiagnostic);
    if (bumpConsumptionValid && !capturedBumpStream.empty()) for (const auto &draw : plan.draws) {
      if (draw.shadowLightSlot) continue;
      uint32_t slot = 0;
      if (!coin_render_bump_draw_slot(plan, capturedBumpStream, draw, slot, bumpConsumptionDiagnostic)) {
        bumpConsumptionValid = false;
        break;
      }
      capturedActiveBump = capturedActiveBump || slot != 0;
    }
    if (!bumpConsumptionValid) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString(bumpConsumptionDiagnostic.c_str())));
      return;
    }
  }
  if (capturedActiveBump) {
    const bool targetQualified = (this->target && !this->planOnly &&
      this->target->getPimpl()->supportsOpaqueBump(this->asyncTicket != nullptr)) ||
      (this->planOnly && this->sceneTextureBumpProducer);
    std::string bumpProfileDiagnostic;
    const bool bumpProfileValid = coin_render_validate_extended_bump(plan, bumpProfileDiagnostic);
    const bool frameQualified =
        bumpProfileValid && !plan.draws.empty() && !plan.bumpImages.empty() &&
        !plan.bumpCoords.empty() &&
        size_t(std::count_if(plan.bumpCoords.begin(), plan.bumpCoords.end(),
                             [](const CoinRenderBumpCoordOverride &coord) {
                               return coord.imageSlot != 0;
                             })) == plan.bumpTangents.size() &&
        std::all_of(plan.draws.begin(), plan.draws.end(),
                    [&plan](const CoinRenderDrawPacket &draw) {
                      if (draw.renderStateSlot >= plan.renderStates.size())
                        return false;
                      const auto &state = plan.renderStates[draw.renderStateSlot];
                      // The common bump validator checks lighting only on
                      // indexed occurrences which consume a normal image.
                      if (draw.shadowLightSlot) return true;
                      return state.lightingSlot < plan.lightingStates.size() &&
                             state.cullMode == CoinRenderCullMode::NONE && state.depthTest &&
                             state.depthWrite &&
                             (state.depthFunction == CoinRenderDepthFunction::LESS ||
                              state.depthFunction == CoinRenderDepthFunction::LEQUAL) &&
                             !state.transparentMaterial && !state.transparentTexture &&
                             state.transparencyType == SoGLRenderAction::NONE &&
                             state.fogMode == CoinRenderFogMode::NONE &&
                             draw.renderLayer == 0 && !draw.clearDepthBefore;
                    });
    if (!targetQualified || !frameQualified) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString(bumpProfileDiagnostic.empty()
                       ? "SoBumpMap requires a qualified opaque bump/normal executor"
                       : bumpProfileDiagnostic.c_str())));
      return;
    }
    planCacheAllowed = false;
  }
  if ((this->drawMode == CoinRenderAction::DrawMode::HIDDEN_LINE ||
       this->drawMode == CoinRenderAction::DrawMode::SHADED_HIDDEN_LINES) &&
      !traversalSkipped && !cameraOverlay && !translationOverlay) {
    const auto textureHasUnqualifiedAlpha = [&plan](uint32_t slot) {
      if (slot >= plan.textures.size()) return true;
      const auto & texture = plan.textures[slot];
      if (texture.producerId || texture.gpuToken) return true;
      if (texture.pixelsRgba.empty()) return !texture.gpuOpaque;
      return coin_render_image_has_transparency(texture.pixelsRgba.data(),
                                                texture.pixelsRgba.size() / 4, 4);
    };
    for (const auto & draw : plan.draws) {
      const auto & state = plan.renderStates[draw.renderStateSlot];
      const bool managerForeground = draw.managerSuperimposition;
      bool textureAlpha = state.hasTexture && textureHasUnqualifiedAlpha(state.textureImageSlot);
      for (const auto & texture : state.extraTextures) {
        if (texture.enabled && textureHasUnqualifiedAlpha(texture.imageSlot)) textureAlpha = true;
      }
      if ((!managerForeground &&
           (state.transparentMaterial || state.transparentTexture || textureAlpha ||
            state.screenDoorTransparency > 0 ||
            (!draw.isSceneLayer() && (draw.renderLayer || draw.clearDepthBefore)))) ||
          (!draw.generatedHighlight && coin_render_alpha_test_active(state.alphaTestFunction)) || state.shadowGroupSlot) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("hidden-line modes require opaque base geometry and no shadows or alpha test")));
        return;
      }
    }
  }
  if (this->drawMode != CoinRenderAction::DrawMode::SCENE &&
      !this->managerCapturedLayers.empty() &&
      !traversalSkipped && !cameraOverlay && !translationOverlay) {
    for (const auto & draw : plan.draws) {
      if (!draw.managerSuperimposition || draw.generatedHighlight) continue;
      const auto & state = plan.renderStates[draw.renderStateSlot];
      if (coin_render_alpha_test_active(state.alphaTestFunction) ||
          state.shadowGroupSlot || draw.shadowLightSlot) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Manager technical layers do not support shadows or alpha test")));
        return;
      }
    }
  }
  if (!this->shadowSceneCaptures.empty()) capturedPreflight.invalidate();
  for (const auto & capture : this->shadowSceneCaptures) {
    for (size_t l = 0; l < plan.shadowLights.size(); ++l) {
      auto & light = plan.shadowLights[l];
      if (light.groupSlot != capture.groupSlot || light.sourceRevision != capture.lightRevision) continue;
      const bool authoredCamera = std::any_of(capture.frame.renderStates.begin(),
        capture.frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot &state) {
          return state.shadowMapCameraOverride;
        });
      if (authoredCamera) {
        const SbMatrix &entryModel = plan.shadowGroups[light.groupSlot - 1].entryModel;
        SbMatrix inverseEntry;
        if (!coin_render_shadow_camera_math::inverse(entryModel, inverseEntry)) {
          this->setDiagnostic(CoinRenderDiagnosticShell::action(
            CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
            SbString("shadowMapScene camera has a non-invertible group entry transform")));
          return;
        }
      }
      coin_render_append_shadow_scene(plan, capture.frame, static_cast<uint32_t>(l + 1),
                                      capture.inheritedClipPlaneCount);
      light.mapSceneCaptured = true;
    }
  }
  if (!plan.shadowGroups.empty()) {
    const bool hasShadowMap = std::any_of(plan.shadowLights.begin(), plan.shadowLights.end(),
      [](const CoinRenderShadowLightSnapshot & light) {
        return light.enabled && light.shadowEligible;
      });
    if (hasShadowMap) {
      for (auto &state : plan.renderStates) {
        if (!state.shadowGroupSlot || !(state.shadowStyle & 2u) ||
            !state.rasterShadowImagePhong) continue;
        if (!state.rasterPixels || !state.rasterShadowAnchorValid ||
            state.cameraSlot >= plan.cameras.size()) {
          this->setDiagnostic(CoinRenderDiagnosticShell::action(
            CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
            SbString("Raster image shadow receiver has no observer camera anchor")));
          return;
        }
        // SoImage retains the authored light model. With an active shadow
        // shader, PHONG modulates the pixel color by the existing receiver
        // lighting path; BASE_COLOR remains unlit.
        state.lightModel = CoinRenderLightModel::PHONG;
        state.textureModel = CoinRenderTextureModel::MODULATE;
      }
    }
    if (!hasShadowMap) {
      // An active group without a map is ordinary scene traversal. Keep its
      // captured lights and raster draws, but remove the inactive map scope so
      // the CPU and GPU paths use the same normal compositor.
      const bool flatReceiver = std::any_of(plan.renderStates.begin(), plan.renderStates.end(),
        [](const CoinRenderRenderStateSnapshot & state) { return state.flatShadowPrimary; });
      if (!flatReceiver) {
        for (auto & state : plan.renderStates) {
          if (state.shadowGroupSlot) {
            state.textureProjection = CoinRenderTextureProjection::PROJECTIVE;
            state.shadowGroupSlot = 0;
            state.shadowStyle = 0;
          }
        }
        plan.shadowGroups.clear();
        plan.shadowLights.clear();
      }
    }
  }
  if (!coin_render_valid_sampling(plan, err)) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(err.c_str())));
    return;
  }
  if (!this->shadowSceneCaptures.empty() && !plan.isValid(&err)) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(err.c_str())));
    return;
  }
  if (!plan.shadowGroups.empty()) {
    CoinRenderShadowPlan shadowPlan;
    std::string shadowDiagnostic;
    if (!coin_render_plan_shadows(plan, shadowPlan, shadowDiagnostic)) {
      this->lastRejectedShadowFrame = std::move(plan);
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString(shadowDiagnostic.c_str())));
      return;
    }
    if (!plan.rasterOccurrences.empty()) {
      if (!coin_render_materialize_raster_casters(plan, shadowPlan, shadowDiagnostic) ||
          !plan.isValid(&shadowDiagnostic) ||
          !coin_render_plan_shadows(plan, shadowPlan, shadowDiagnostic)) {
        this->lastRejectedShadowFrame = std::move(plan);
        this->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString(shadowDiagnostic.c_str())));
        return;
      }
    }
    bool executableShadow = false;
    // SoSceneTexture2 producers are captured without a target. The resolved
    // frame is later submitted by the selected staged or direct executor.
    if (this->planOnly &&
        (this->sceneTexturePlan->mode == COIN_RENDER_SCENE_TEXTURE_STAGED ||
         this->sceneTexturePlan->mode == COIN_RENDER_SCENE_TEXTURE_DIRECT)) {
      std::string profileDiagnostic;
      executableShadow = coin_render_shadow_object_profile(
        plan, shadowPlan, shadowPlan.passes.size(), profileDiagnostic);
    }
    if (this->target &&
        this->target->getPimpl()->supportsOffscreenShadows(this->asyncTicket != nullptr)) {
      std::string profileDiagnostic;
      executableShadow = coin_render_shadow_object_profile(
        plan, shadowPlan, shadowPlan.passes.size(), profileDiagnostic);
    }
    if (!executableShadow) {
      this->lastRejectedShadowFrame = std::move(plan);
      this->lastRejectedShadowPlan = std::move(shadowPlan);
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("Active SoShadowGroup requires a qualified opaque shadow executor for this target")));
      return;
    }
    // Shadow maps depend on captured light/geometry state; reuse and camera
    // overlays are not qualified for this first executable profile.
    planCacheAllowed = false;
  }
  profilePlanned = ProfileClock::now();
  if (!this->sceneTexturePlan->producers.empty())
    planCacheAllowed = false;
  if (!traversalSkipped && !cameraOverlay && !translationOverlay) {
    if (planCacheAllowed && cacheRoot && this->hasLastValidPlan &&
        this->cachedRoot == cacheRoot) {
      reuseDecision = CoinRenderFrameReuseCore::classify(this->lastValidPlan, plan);
    } else if (planCacheAllowed) {
      reuseDecision = CoinRenderFrameReuseDecision(
        CoinRenderFrameReuseKind::FULL_REBUILD, 0);
    }
  }
  const bool reusePreviousPlan =
    reuseDecision.kind == CoinRenderFrameReuseKind::REUSE &&
    this->hasLastValidPlan;
  const bool useCachedPlan = reusePreviousPlan || cameraOverlay || translationOverlay;
  if (useCachedPlan) capturedPreflight.invalidate();
  const CoinRenderFramePlan& capturedPlan = useCachedPlan ? this->lastValidPlan : plan;
  const CoinRenderFramePlan& framePlan = capturedPlan;

  if (this->planOnly) {
    if (!useCachedPlan) {
      this->lastValidPlan = std::move(plan);
      this->hasLastValidPlan = true;
      this->capturedStereoStates = this->frameStereoStates;
    }
    this->setDiagnostic(CoinRenderDiagnosticShell::success());
    return;
  }

  // From here to submission there are no application callbacks or mutable
  // aliases to this private snapshot. The existing sensors/stereo checks
  // authorize retaining its identity only for an unchanged cached apply.
  CoinRenderFrameBorrow executionBorrow(capturedPlan, this->executionBorrowIdentity,
    !traversalSkipped, planCacheAllowed && this->sceneTexturePlan->producers.empty());

  // The typed capture/transport increment is independently usable by Core
  // consumers. GPU execution is enabled only with the matching executor.
  bool portableExecution = capturedPlan.hasPortableShaderDraws();
  for (const auto &producer : this->sceneTexturePlan->producers)
    for (size_t face = 0; face < producer.faceCount(); ++face)
      portableExecution = portableExecution || producer.face(face).hasPortableShaderDraws();
  if (portableExecution) {
    std::string error;
    bool valid = coin_render_shader_execution_profile(capturedPlan, error);
    for (const auto &producer : this->sceneTexturePlan->producers)
      for (size_t face = 0; valid && face < producer.faceCount(); ++face)
        valid = coin_render_shader_execution_profile(producer.face(face), error);
    if (!valid) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(error.c_str())));
      return;
    }
  }
  if (portableExecution && (!this->target ||
      !this->target->getPimpl().get().supportsPortableShaderModules())) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("Portable shader module execution requires a GPU executor for this target")));
    return;
  }

  if (this->target && this->target->getStatus() == CoinRenderTarget::TARGET_ERROR) {
    this->setDiagnostic(CoinRenderDiagnosticShell::fromTarget(this->target->getStatus(),
                                                              this->target->getLastError()));
    return;
  }
  if (!this->target && this->asyncTicket) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::NO_TARGET, CoinRenderDiagnosticDomain::TARGET,
        SbString("applyAsync() requires an offscreen render target")));
    return;
  }
  if (this->target) {
    const auto admission =
        this->target->getPimpl().get().preflightSubmission(this->asyncTicket != nullptr);
    if (admission.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(admission));
      return;
    }
  }
  if (this->target) {
    const auto admission = this->target->getPimpl().get().preflightMultipass(
      capturedPlan, this->asyncTicket != nullptr, this->sceneTexturePlan.get());
    if (admission.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(admission));
      return;
    }
  } else {
    const SbVec2s viewport = this->master->getViewportRegion().getWindowSize();
    const auto admission = this->recordingBackend.preflightMultipassGraph(
      *this->sceneTexturePlan, capturedPlan, SbVec2i32(viewport[0], viewport[1]));
    if (admission.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(admission));
      return;
    }
  }
  if (this->smoothing && std::any_of(this->frameStereoStates.begin(), this->frameStereoStates.end(),
      [](const CameraStereoState & state) { return state.mode != static_cast<int>(SoCamera::MONOSCOPIC); })) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Smoothing stereo camera is outside the mono profile")));
    return;
  }
  if (partialUpdate) {
    if (!capturedPlan.shadowGroups.empty() || !capturedPlan.bumpImages.empty() ||
        capturedPlan.hasPortableShaderDraws()) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Partial update with spatial shaders is not qualified")));
      return;
    }
    std::vector<CoinRenderCompositionItem> partialOrder;
    std::string diagnostic;
    if (!coin_render_composition_order(capturedPlan, partialOrder, diagnostic)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(diagnostic.c_str())));
      return;
    }
    for (const auto & item : partialOrder)
      if (item.screenDoorLevel || item.transparencyStrategy != CoinRenderCompositionItem::OBJECT) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
          CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Stippled and OIT partial updates need a canvas-origin profile")));
        return;
      }
  }
  CoinRenderRttExecution rttExecution(this->target ? &this->target->getPimpl().get() : nullptr,
                                      this->executionOptions);
  CoinRenderFramePlan resolvedPlan;
  const bool hasSceneTextures = !this->sceneTexturePlan->producers.empty();
  if (hasSceneTextures) {
    capturedPreflight.invalidate();
    const auto prepared = rttExecution.prepare(*this->sceneTexturePlan, capturedPlan, resolvedPlan);
    if (prepared.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(prepared));
      return;
    }
  }
  const CoinRenderFramePlan& executionPlan = hasSceneTextures ? resolvedPlan : capturedPlan;
  const bool retainStagedPixels =
      hasSceneTextures && this->sceneTexturePlan->mode == COIN_RENDER_SCENE_TEXTURE_STAGED;

  if (this->target == NULL) {
    // Mode 0: Recording backend
    this->lastRecordingLog = this->recordingBackend.recordToString(executionPlan).c_str();
    this->recordingLogValid = true;
    if (!useCachedPlan) {
      this->lastValidPlan = retainStagedPixels ? std::move(resolvedPlan) : std::move(plan);
      this->hasLastValidPlan = true;
      this->capturedStereoStates = this->frameStereoStates;
    }
    if (cacheRoot) {
      this->cachedRoot = planCacheAllowed ? cacheRoot : NULL;
      this->cachedRootId = cacheRoot->getNodeId();
      if (cameraOverlay) {
        this->cameraOnlyDirty = false;
        this->clearTranslationProof();
      } else if (translationOverlay) this->commitTranslationOverlay();
      else if (!traversalSkipped)
        this->rememberFrameRoot(planCacheAllowed ? cacheRoot : NULL, qualifyCapturedCamera);
    }
    overlayScope.committed = true;
    this->setDiagnostic(CoinRenderDiagnosticShell::success());
    return;
  }

  // Execute frame on target
  const auto * submissionPreflight = capturedPreflight.compositionFor(executionPlan)
    ? &capturedPreflight : nullptr;
  CoinRenderFrameExecutionResult execRes =
      this->asyncTicket
          ? this->target->pimpl->executeFrameAsync(executionPlan, *this->asyncTicket, reuseDecision,
                                                  submissionPreflight)
          : partialUpdate
              ? this->target->pimpl->executePartialFrame(executionPlan, updateRect)
              : this->target->pimpl->executeFrame(executionPlan, reuseDecision, submissionPreflight);
  if (execRes.status != CoinRenderBackendStatus::SUCCESS) {
    this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(execRes));
    return;
  }

  rasterHistoryScope.committed = true;
  const ProfileClock::time_point profileExecuted = ProfileClock::now();
  if (tracePhases) {
    CoinRenderActionPhaseSample sample;
    sample.traversalMs = std::chrono::duration<double, std::milli>(
      profileTraversed - profileBegin).count();
    sample.framePlanMs = std::chrono::duration<double, std::milli>(
      profilePlanned - profileTraversed).count();
    sample.backendMs = std::chrono::duration<double, std::milli>(
      profileExecuted - profilePlanned).count();
    sample.vertices = framePlan.vertices.size();
    sample.indices = framePlan.indices.size();
    sample.draws = framePlan.draws.size();
    sample.planCacheHit = traversalSkipped || cameraOverlay || translationOverlay;
    sample.reuseKind = reuseDecision.kind;
    std::cerr << CoinRenderDiagnosticShell::formatActionPhase(sample) << '\n';
  }
  if (!useCachedPlan) {
    capturedPreflight.invalidate();
    this->lastValidPlan = retainStagedPixels ? std::move(resolvedPlan) : std::move(plan);
    this->hasLastValidPlan = true;
    this->capturedStereoStates = this->frameStereoStates;
    this->recordingLogValid = false;
  } else if (cameraOverlay || translationOverlay) {
    this->recordingLogValid = false;
  }
  if (cacheRoot) {
    this->cachedRoot = planCacheAllowed ? cacheRoot : NULL;
    this->cachedRootId = cacheRoot->getNodeId();
    if (cameraOverlay) {
      this->cameraOnlyDirty = false;
      this->clearTranslationProof();
    } else if (translationOverlay) this->commitTranslationOverlay();
    else if (!traversalSkipped)
      this->rememberFrameRoot(planCacheAllowed ? cacheRoot : NULL, qualifyCapturedCamera);
  }
  overlayScope.committed = true;
  this->setDiagnostic(CoinRenderDiagnosticShell::success());
}

void
CoinRenderAction::apply(SoNode * root)
{
  this->pimpl->executeApply([&]() {
    if (root) {
      this->inherited::apply(root);
    }
  }, root);
}

void
CoinRenderAction::applyAsync(SoNode * root, CoinRenderReadbackTicket & outTicket)
{
  if (this->pimpl->isApplying) {
    this->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
      INVALID_SCENE, CoinRenderDiagnosticDomain::ACTION,
      SbString("Nested applyAsync() calls are not permitted")));
    this->pimpl->hasReentrancyError = true;
    return;
  }
  this->pimpl->asyncTicket = &outTicket;
  this->apply(root);
  this->pimpl->asyncTicket = NULL;
}

void
CoinRenderAction::apply(SoPath * path)
{
  this->pimpl->executeApply([&]() {
    if (path) {
      this->inherited::apply(path);
    }
  });
}

void
CoinRenderAction::apply(const SoPathList & pathlist, SbBool obeysrules)
{
  this->pimpl->executeApply([&]() {
    this->inherited::apply(pathlist, obeysrules);
  });
}

void
CoinRenderAction::beginTraversal(SoNode * root)
{
  if (this->pimpl->isApplying) {
    SoState * state = this->getState();
    state->push();
    SoShapeStyleElement::setTransparencyType(
      state, static_cast<int32_t>(this->pimpl->transparencyType));
    SoLazyElement::setTransparencyType(
      state, static_cast<int32_t>(this->pimpl->transparencyType));
    if (root && ((!this->pimpl->planOnly && this->pimpl->transparencyType == SORTED_LAYERS_BLEND) ||
                 this->pimpl->inheritedTransparencyOverride)) {
      SoOverrideElement::setTransparencyTypeOverride(state, root, TRUE);
    }
    // The adapter marks its base explicitly. Each superimposition is an
    // independent authored traversal; only the base receives technical and
    // texture-quality overrides. Keep every traversal in this same builder.
    if (root && root->isOfType(SoSeparator::getClassTypeId())) {
      const auto * group = static_cast<const SoGroup *>(root);
      int baseIndex = -1;
      for (int i = 0; i < group->getNumChildren(); ++i)
        for (const auto & boundary : this->pimpl->managerLayerBoundaries)
          if (std::get<0>(boundary) == group->getChild(i) &&
              std::get<2>(boundary) == COIN_RENDER_MANAGER_BASE_LAYER) baseIndex = i;
      if (baseIndex >= 0) {
        for (int i = 0; i < group->getNumChildren() && !this->hasTerminated(); ++i) {
          SoNode * child = group->getChild(i);
          const bool highlight = std::any_of(this->pimpl->managerLayerBoundaries.begin(),
            this->pimpl->managerLayerBoundaries.end(),
            [child](const std::tuple<const SoNode *, bool, int> & boundary) {
              return std::get<0>(boundary) == child &&
                std::get<2>(boundary) == COIN_RENDER_MANAGER_HIGHLIGHT_LAYER;
            });
          const int section = i < baseIndex ? 0 : (i == baseIndex || highlight) ? 1 : 2;
          if (this->pimpl->managerCaptureSection >= 0 &&
              this->pimpl->managerCaptureSection != section) continue;
          this->pushCurPath(i, child);
          if (i == baseIndex) {
            // CoinGL HIDDEN_LINE clears both buffers even after backgrounds.
            // Their traversal/RTT captures still occur, but their draws no
            // longer contribute to this frame's attachments.
            if (this->pimpl->drawMode == DrawMode::HIDDEN_LINE)
              this->pimpl->builder.discardDrawOutput();
            const auto * base = static_cast<const SoGroup *>(child);
            const bool hasBase = base->getNumChildren() != 0;
            this->pimpl->builder.beginAuthoredScene(hasBase ||
              this->pimpl->drawMode == DrawMode::HIDDEN_LINE);
            if (base->getNumChildren()) {
              this->pushCurPath(0, base->getChild(0));
              this->beginTraversal(base->getChild(0));
              this->popCurPath();
            }
            this->pimpl->builder.capturePendingDepthClear(this, !hasBase);
            this->pimpl->builder.endAuthoredScene();
          }
          else this->inherited::beginTraversal(child);
          this->popCurPath();
        }
        state->pop();
        return;
      }
    }
    struct TechnicalCaptureScope {
      CoinRenderActionP * p;
      explicit TechnicalCaptureScope(CoinRenderActionP * owner) : p(owner) {
        p->technicalRttCaptures.clear();
        p->technicalRttReplay = false;
        p->technicalRttCapture = p->drawMode == DrawMode::HIDDEN_LINE ||
          p->drawMode == DrawMode::SHADED_HIDDEN_LINES || p->drawMode == DrawMode::WIREFRAME_OVERLAY;
      }
      ~TechnicalCaptureScope() {
        p->technicalRttCaptures.clear();
        p->technicalRttCapture = p->technicalRttReplay = false;
      }
    } technicalScope(&this->pimpl.get());
    const auto traverseBase = [&]() {
      this->inherited::beginTraversal(root);
      if (this->pimpl->technicalRttCapture) this->pimpl->technicalRttReplay = true;
    };
    if (root && !this->pimpl->texturesEnabled) {
      SoTextureQualityElement::set(state, 0.0f);
      SoTextureOverrideElement::setQualityOverride(state, TRUE);
    }
    if (root && this->pimpl->drawMode == DrawMode::HIDDEN_LINE) {
      state->push();
      SoMaterialBindingElement::set(state, root, SoMaterialBindingElement::OVERALL);
      SoLightModelElement::set(state, root, SoLightModelElement::BASE_COLOR);
      SoPolygonOffsetElement::set(state, root, 1.0f, 1.0f,
                                  SoPolygonOffsetElement::FILLED, TRUE);
      SoOverrideElement::setMaterialBindingOverride(state, root, TRUE);
      SoOverrideElement::setLightModelOverride(state, root, TRUE);
      SoOverrideElement::setPolygonOffsetOverride(state, root, TRUE);
      this->pimpl->builder.setColorWriteEnabled(false);
      traverseBase();
      this->pimpl->builder.setColorWriteEnabled(true);
      state->pop();

      state->push();
      SoMaterialBindingElement::set(state, root, SoMaterialBindingElement::OVERALL);
      SoLightModelElement::set(state, root, SoLightModelElement::BASE_COLOR);
      SoPolygonOffsetElement::set(state, root, 0.0f, 0.0f,
                                  SoPolygonOffsetElement::FILLED, FALSE);
      SoDrawStyleElement::set(state, root, SoDrawStyleElement::LINES);
      SoOverrideElement::setLightModelOverride(state, root, TRUE);
      SoOverrideElement::setPolygonOffsetOverride(state, root, TRUE);
      SoOverrideElement::setDrawStyleOverride(state, root, TRUE);
      traverseBase();
      state->pop();
      state->pop();
      return;
    }
    if (root && this->pimpl->drawMode == DrawMode::SHADED_HIDDEN_LINES) {
      state->push();
      SoDrawStyleElement::set(state, root, SoDrawStyleElement::FILLED);
      SoLightModelElement::set(state, root, SoLightModelElement::PHONG);
      SoPolygonOffsetElement::set(state, root, 1.0f, 1.0f,
                                  SoPolygonOffsetElement::FILLED, TRUE);
      SoOverrideElement::setDrawStyleOverride(state, root, TRUE);
      SoOverrideElement::setLightModelOverride(state, root, TRUE);
      SoOverrideElement::setPolygonOffsetOverride(state, root, TRUE);
      traverseBase();

      SoPolygonOffsetElement::set(state, root, 0.0f, 0.0f,
                                  SoPolygonOffsetElement::FILLED, FALSE);
      SoDrawStyleElement::set(state, root, SoDrawStyleElement::LINES);
      SoLinePatternElement::set(state, root, 0xffff, 1);
      SoOverrideElement::setLinePatternOverride(state, root, TRUE);
      SoDepthBufferElement::set(state, TRUE, FALSE, SoDepthBufferElement::LEQUAL,
                                SoDepthBufferElement::getRange(state));
      traverseBase();

      SoLineWidthElement::set(state, root, 1.0f);
      SoOverrideElement::setLineWidthOverride(state, root, TRUE);
      SoLinePatternElement::set(state, root, 0xf0f0, 2);
      this->pimpl->builder.setLinePatternOverride(true, 0xf0f0u, 2);
      SoDepthBufferElement::set(state, TRUE, FALSE, SoDepthBufferElement::GREATER,
                                SoDepthBufferElement::getRange(state));
      traverseBase();
      this->pimpl->builder.setLinePatternOverride(false);
      state->pop();
      state->pop();
      return;
    }
    if (root && this->pimpl->drawMode == DrawMode::WIREFRAME_OVERLAY) {
      state->push();
      SoPolygonOffsetElement::set(state, root, 1.0f, 1.0f,
                                  SoPolygonOffsetElement::FILLED, TRUE);
      SoOverrideElement::setPolygonOffsetOverride(state, root, TRUE);
      traverseBase();
      state->pop();

      state->push();
      SoPolygonOffsetElement::set(state, root, 0.0f, 0.0f,
                                  SoPolygonOffsetElement::FILLED, FALSE);
      SoOverrideElement::setPolygonOffsetOverride(state, root, TRUE);
      const uint32_t packed = this->pimpl->overlayColor.getPackedValue();
      SoLazyElement::setPacked(state, root, 1, &packed, TRUE);
      SoLightModelElement::set(state, root, SoLightModelElement::BASE_COLOR);
      SoMaterialBindingElement::set(state, root, SoMaterialBindingElement::OVERALL);
      SoDrawStyleElement::set(state, root, SoDrawStyleElement::LINES);
      SoOverrideElement::setLightModelOverride(state, root, TRUE);
      SoOverrideElement::setDiffuseColorOverride(state, root, TRUE);
      SoOverrideElement::setMaterialBindingOverride(state, root, TRUE);
      SoOverrideElement::setDrawStyleOverride(state, root, TRUE);
      traverseBase();
      state->pop();
      state->pop();
      return;
    }
    if (root && this->pimpl->drawMode != DrawMode::SCENE) {
      SoDrawStyleElement::set(state, root,
        this->pimpl->drawMode == DrawMode::POINTS
          ? SoDrawStyleElement::POINTS : SoDrawStyleElement::LINES);
      SoLightModelElement::set(state, root, SoLightModelElement::BASE_COLOR);
      SoOverrideElement::setDrawStyleOverride(state, root, TRUE);
      SoOverrideElement::setLightModelOverride(state, root, TRUE);
      if (this->pimpl->drawMode == DrawMode::BOUNDING_BOX) {
        SoComplexityTypeElement::set(state, root, SoComplexityTypeElement::BOUNDING_BOX);
        SoOverrideElement::setComplexityTypeOverride(state, root, TRUE);
      }
    }
    if (root) {
      traverseBase();
    }
    state->pop();
    return;
  }

  this->pimpl->executeApply([&]() {
    if (root) {
      this->inherited::beginTraversal(root);
    }
  });
}

