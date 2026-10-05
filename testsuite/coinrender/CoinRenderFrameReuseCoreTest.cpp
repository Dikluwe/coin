#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"
#include <Inventor/SbRotation.h>
#include "rendering/coinrender/CoinRenderTargetP.h"

#include <Inventor/SoDB.h>

#include <iostream>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
bool
check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinRenderFrameReuseCoreTest: " << message << '\n';
  return condition;
}

CoinRenderFramePlan
makePlan(uint64_t revision)
{
  CoinRenderFramePlan plan;
  plan.revision = revision;
  plan.vertices.resize(3);
  plan.vertices[0].position[0] = -0.5f;
  plan.vertices[1].position[0] = 0.5f;
  plan.vertices[2].position[1] = 0.5f;
  plan.indices = {0, 1, 2};
  plan.materials.push_back(CoinRenderMaterialSnapshot{});
  plan.lightingStates.push_back(CoinRenderLightingSnapshot{});
  plan.cameras.push_back(CoinRenderCameraSnapshot{});
  plan.viewports.push_back(CoinRenderViewportSnapshot{});
  CoinRenderRenderStateSnapshot state;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  plan.renderStates.push_back(state);
  CoinRenderDrawPacket draw;
  draw.topology = CoinRenderPrimitiveTopology::TRIANGLE_LIST;
  draw.geometry.vertexCount = 3;
  draw.geometry.indexCount = 3;
  draw.stableNodeId = 7;
  draw.sourceRevision = 11;
  plan.draws.push_back(draw);
  return plan;
}

bool nearValue(float a, float b) {
  return std::abs(a-b) <= 2.0e-5f * (1.0f + std::abs(b));
}

bool sameMatrixBits(const SbMatrix & a, const SbMatrix & b) {
  return std::memcmp(a.getValue(), b.getValue(), sizeof(float) * 16) == 0;
}

bool testTranslationOverlay() {
  bool ok = true;
  const SbRotation rotation(SbVec3f(1, 2, 3), .37f);
  const SbRotation scaleOrientation(SbVec3f(2, -1, 1), -.21f);
  const SbVec3f scale(-1.3f, .7f, 2.1f);
  SbMatrix prefix;
  prefix.setTransform(SbVec3f(3, -2, 5), SbRotation(SbVec3f(-1, 3, 2), .41f),
                      SbVec3f(.6f, -1.2f, 1.7f), scaleOrientation, SbVec3f(1, -.5f, 2));
  const SbVec3f originalPosition(.2f, -.4f, 1.1f);
  SbMatrix local, anchor;
  local.setTransform(originalPosition, rotation, scale, scaleOrientation, SbVec3f(0, 0, 0));
  anchor = local; anchor.multRight(prefix);
  SbMatrix translated = SbMatrix::identity();
  for (const SbVec3f position : {SbVec3f(2, -3, .25f), SbVec3f(-.1f, .8f, -1.5f), originalPosition}) {
    local.setTransform(position, rotation, scale, scaleOrientation, SbVec3f(0, 0, 0));
    SbMatrix expected = local; expected.multRight(prefix);
    ok &= check(CoinRenderFrameReuseCore::translatedModel(anchor, prefix, position, translated) &&
                sameMatrixBits(translated, expected),
                "translation through rotated/scaled/reflected prefix must match full Coin multiplication by bytes");
  }
  ok &= check(sameMatrixBits(translated, anchor),
              "returning to the anchor position must not accumulate matrix drift");
  SbMatrix translationAnchor;
  translationAnchor.setTranslate(originalPosition); translationAnchor.multRight(prefix);
  SbMatrix expectedTranslation;
  expectedTranslation.setTranslate(SbVec3f(1, -2, 3)); expectedTranslation.multRight(prefix);
  ok &= check(CoinRenderFrameReuseCore::translatedModel(translationAnchor, prefix, SbVec3f(1, -2, 3), translated) &&
              sameMatrixBits(translated, expectedTranslation),
              "SoTranslation must follow its actual prefix without undoing scale");
  const SbMatrix saved = translated;
  for (int kind = 0; kind < 5; ++kind) {
    SbMatrix badPrefix = prefix, badAnchor = anchor;
    SbVec3f badPosition(1, 2, 3);
    if (kind == 0) badPrefix[0][3] = .25f;
    if (kind == 1) badAnchor[3][3] = 2;
    if (kind == 2) badPosition[0] = std::numeric_limits<float>::quiet_NaN();
    if (kind == 3) badPrefix[0][0] = 32769;
    if (kind == 4) badPosition[2] = 32769;
    ok &= check(!CoinRenderFrameReuseCore::translatedModel(badAnchor, badPrefix, badPosition, translated) &&
                sameMatrixBits(translated, saved),
                "invalid/projective/out-of-domain translation must reject without changing output");
  }
  SbMatrix enlarged = SbMatrix::identity(); enlarged[0][0] = 2;
  ok &= check(!CoinRenderFrameReuseCore::translatedModel(anchor, enlarged, SbVec3f(20000, 0, 0), translated) &&
              sameMatrixBits(translated, saved),
              "a resulting out-of-domain model must reject transactionally");

  CoinRenderFramePlan plan = makePlan(1500);
  plan.renderStates.push_back(plan.renderStates[0]);
  plan.renderStates[0].model = anchor;
  plan.renderStates[1].model = translationAnchor;
  CoinRenderPlanAssemblyCore::sortingCenter(plan.draws[0], anchor, SbVec3f(0, 0, 0));
  const CoinRenderFramePlan original = plan;
  const auto * vertices = plan.vertices.data(); const auto * indices = plan.indices.data();
  std::vector<CoinRenderModelUpdate> updates(2);
  updates[0].stateSlot = 0; updates[0].model = saved; updates[0].sortingDrawSlot = 0;
  updates[1].stateSlot = 1; updates[1].model = expectedTranslation;
  CoinRenderTranslationOverlayUndo undo;
  ok &= check(CoinRenderFrameReuseCore::beginTranslationOverlay(plan, updates, 1501, undo) && undo.active &&
              undo.revision == 1500 && plan.revision == 1501 && plan.vertices.data() == vertices &&
              plan.indices.data() == indices && sameMatrixBits(plan.renderStates[0].model, saved),
              "model overlay must retain geometry and keep its previous revision for rollback");
  ok &= check(!CoinRenderFrameReuseCore::beginTranslationOverlay(plan, updates, 1502, undo) &&
              plan.revision == 1501, "an already active undo must not be overwritten");
  CoinRenderFrameReuseCore::rollbackTranslationOverlay(plan, undo);
  ok &= check(!undo.active && plan.revision == original.revision && plan.hasSamePayload(original),
              "rollback must restore every model and the revision byte for byte");
  for (int kind = 0; kind < 5; ++kind) {
    auto invalidUpdates = updates;
    if (kind == 0) invalidUpdates[1].stateSlot = invalidUpdates[0].stateSlot;
    if (kind == 1) invalidUpdates[1].stateSlot = 2;
    if (kind == 2) invalidUpdates[1].model[0][0] = std::numeric_limits<float>::infinity();
    if (kind == 3) invalidUpdates.resize(65537);
    if (kind == 4) invalidUpdates[1].sortingDrawSlot = 0;
    CoinRenderTranslationOverlayUndo invalidUndo;
    ok &= check(!CoinRenderFrameReuseCore::beginTranslationOverlay(plan, invalidUpdates, 1503, invalidUndo) &&
                !invalidUndo.active && plan.revision == original.revision && plan.hasSamePayload(original),
                "duplicate/late-invalid/oversized update lists must preserve the entire prior plan");
  }
  CoinRenderTranslationOverlayUndo sameRevision;
  ok &= check(!CoinRenderFrameReuseCore::beginTranslationOverlay(plan, updates, 1500, sameRevision) &&
              !CoinRenderFrameReuseCore::beginTranslationOverlay(plan, updates, 0, sameRevision),
              "unversioned or unchanged revisions must not publish a model overlay");
  return ok;
}
bool sameLightsWithinPrecision(const CoinRenderFramePlan & a, const CoinRenderFramePlan & b) {
  if (a.lightingStates.size() != b.lightingStates.size()) return false;
  for (size_t i=0;i<a.lightingStates.size();++i) {
    const auto & x=a.lightingStates[i]; const auto & y=b.lightingStates[i];
    if (x.lights.size()!=y.lights.size() || x.ambientIntensity!=y.ambientIntensity ||
        std::memcmp(x.ambientColor, y.ambientColor, sizeof(x.ambientColor)) != 0) return false;
    for(size_t j=0;j<x.lights.size();++j) {
      const auto & l=x.lights[j]; const auto & r=y.lights[j];
      if(l.type!=r.type || l.sourceRevision!=r.sourceRevision || l.intensity!=r.intensity ||
         l.cutOffAngle!=r.cutOffAngle || l.dropOffRate!=r.dropOffRate) return false;
      for(int k=0;k<3;++k)
        if(!nearValue(l.position[k],r.position[k]) || !nearValue(l.direction[k],r.direction[k]) ||
           l.color[k]!=r.color[k] || l.attenuation[k]!=r.attenuation[k]) return false;
      for(int row=0;row<4;++row) for(int col=0;col<4;++col)
        if(!nearValue(l.sourceModel[row][col],r.sourceModel[row][col])) return false;
    }
  }
  return true;
}

CoinRenderFramePlan phongReference(const SbMatrix & view, uint64_t revision) {
  CoinRenderFramePlan plan=makePlan(revision);
  plan.cameras[0].viewMatrix=view;
  plan.renderStates[0].view=view;
  plan.renderStates[0].lightModel=CoinRenderLightModel::PHONG;
  SbMatrix model(1.5f,.2f,0,0, 0,.8f,.15f,0, .1f,0,2,0, 2,-1,3,1);
  plan.renderStates[0].model=model;
  for(int i=0;i<3;++i) {
    CoinRenderLightSourceSnapshot light;
    light.type=static_cast<CoinRenderLightType>(i);
    light.sourceRevision=123+i;
    light.sourceModel=model*view;
    if(i!=0) {light.position[0]=1;light.position[1]=2;light.position[2]=-3;}
    if(i!=1) {light.direction[0]=.4f;light.direction[1]=-.8f;light.direction[2]=-.2f;}
    else light.direction[0]=light.direction[1]=light.direction[2]=0;
    light.attenuation[0]=.02f; light.attenuation[1]=.1f;
    CoinRenderPlanAssemblyCore::transformLight(light);
    plan.lightingStates[0].lights.push_back(light);
  }
  return plan;
}

bool testRigidCameraDelta() {
  bool ok = true;
  SbMatrix anchor;
  anchor.setRotate(SbRotation(SbVec3f(1, 2, 3), .37f));
  anchor[3][0] = -2; anchor[3][1] = 1; anchor[3][2] = -7;
  SbMatrix moved = anchor;
  moved[3][0] += .25f; moved[3][1] -= .5f; moved[3][2] += 2;
  SbMatrix delta, normal;
  ok &= check(CoinRenderTransformCore::rigidViewMatrix(anchor) &&
              CoinRenderTransformCore::cameraDelta(anchor, moved, delta, normal),
              "proper camera rotation/translation must qualify");
  const SbMatrix translation(1,0,0,0, 0,1,0,0, 0,0,1,0, .25f,-.5f,2,1);
  ok &= check(delta == translation && normal == translation.inverse().transpose(),
              "unchanged orientation must produce the exact anchored translation");
  SbMatrix rotated;
  rotated.setRotate(SbRotation(SbVec3f(0, 1, 0), -.51f));
  rotated[3][0] = 4; rotated[3][2] = -9;
  ok &= check(CoinRenderTransformCore::cameraDelta(anchor, rotated, delta, normal),
              "camera rotation must produce a finite anchored delta");
  const SbMatrix recomposed = anchor * delta;
  for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col)
    ok &= check(nearValue(recomposed[row][col], rotated[row][col]),
                "row-vector anchored delta must reproduce the current view");
  const SbMatrix savedDelta = delta, savedNormal = normal;
  for (int kind = 0; kind < 5; ++kind) {
    SbMatrix invalid = anchor;
    if (kind == 0) invalid[0][0] *= 2;
    if (kind == 1) invalid[1][0] += .25f;
    if (kind == 2) for (int col = 0; col < 3; ++col) invalid[0][col] *= -1;
    if (kind == 3) invalid[0][3] = .1f;
    if (kind == 4) invalid[3][0] = std::numeric_limits<float>::quiet_NaN();
    ok &= check(!CoinRenderTransformCore::rigidViewMatrix(invalid) &&
                !CoinRenderTransformCore::cameraDelta(anchor, invalid, delta, normal) &&
                delta == savedDelta && normal == savedNormal,
                "scale/shear/reflection/projective/non-finite views must reject transactionally");
  }
  return ok;
}

CoinRenderFramePlan pointLightReference(const SbMatrix & view, uint64_t revision) {
  auto plan = makePlan(revision);
  plan.cameras[0].viewMatrix = view;
  plan.renderStates[0].view = view;
  plan.renderStates[0].lightModel = CoinRenderLightModel::PHONG;
  CoinRenderLightSourceSnapshot light;
  light.type = CoinRenderLightType::POINT;
  light.sourceModel = view;
  light.position[2] = 1;
  light.direction[0] = light.direction[1] = light.direction[2] = 0;
  CoinRenderPlanAssemblyCore::transformLight(light);
  plan.lightingStates[0].lights.push_back(light);
  return plan;
}

bool testCameraPrecisionFallback() {
  bool ok = true;
  SbMatrix farView = SbMatrix::identity(); farView[3][2] = -1.0e8f;
  auto distant = pointLightReference(farView, 1000);
  const auto distantOriginal = distant;
  SbVec3f recovered;
  farView.inverse().multVecMatrix(SbVec3f(distant.lightingStates[0].lights[0].position), recovered);
  ok &= check(distant.lightingStates[0].lights[0].position[2] == -1.0e8f && recovered[2] == 0,
              "regression fixture must reproduce the lost unit-sized WORLD light coordinate");
  CoinRenderCameraOverlayBasis rejected;
  ok &= check(!CoinRenderFrameReuseCore::prepareCameraOverlayBasis(distant, rejected) && !rejected.owner &&
              distant.hasSamePayload(distantOriginal),
              "distant anchor must decline before reconstructing a lossy WORLD basis");
  auto nearCamera = distant.cameras[0]; nearCamera.viewMatrix = SbMatrix::identity();
  CoinRenderCameraOverlayUndo undo;
  ok &= check(!CoinRenderFrameReuseCore::beginCameraOverlay(distant, nearCamera, 1001, undo) && !undo.active &&
              distant.hasSamePayload(distantOriginal) && distant.revision == distantOriginal.revision,
              "distant-to-near camera movement must require a fresh capture and leave the old plan intact");
  auto output = makePlan(1100); const auto savedOutput = output;
  ok &= check(!CoinRenderFrameReuseCore::cameraOverlay(distant, nearCamera, 1002, output) &&
              output.hasSamePayload(savedOutput) && output.revision == savedOutput.revision,
              "failed distant-anchor overlay must not replace the caller's output");
  auto nearby = pointLightReference(SbMatrix::identity(), 1200);
  const auto nearbyOriginal = nearby;
  CoinRenderCameraOverlayBasis basis;
  ok &= check(CoinRenderFrameReuseCore::prepareCameraOverlayBasis(nearby, basis),
              "ordinary light/camera coordinates must still qualify");
  auto farCamera = nearby.cameras[0]; farCamera.viewMatrix = farView;
  ok &= check(!CoinRenderFrameReuseCore::beginCameraOverlay(nearby, farCamera, 1201, undo, basis) &&
              !undo.active && nearby.hasSamePayload(nearbyOriginal) && nearby.revision == nearbyOriginal.revision,
              "a distant current camera must also decline transactionally");
  SbMatrix delta = SbMatrix::identity(), normal = SbMatrix::identity();
  ok &= check(CoinRenderTransformCore::rigidViewMatrix(farView) &&
              !CoinRenderTransformCore::cameraReuseView(farView) &&
              !CoinRenderTransformCore::cameraDelta(farView, SbMatrix::identity(), delta, normal) &&
              delta == SbMatrix::identity() && normal == SbMatrix::identity(),
              "rigid but distant camera deltas must preserve their output on rejection");
  for (int kind = 0; kind < 4; ++kind) {
    auto excessive = nearbyOriginal;
    auto & light = excessive.lightingStates[0].lights[0];
    if (kind == 0) light.position[0] = 32769;
    if (kind == 1) light.sourceModel[0][0] = 32769;
    if (kind >= 2) {
      excessive.cameras[0].viewMatrix[3][2] = -16000;
      excessive.renderStates[0].view = excessive.cameras[0].viewMatrix;
      light.sourceModel = excessive.cameras[0].viewMatrix;
      if (kind == 2) light.position[2] = 20000;
      if (kind == 3) light.sourceModel[3][2] = 20000;
    }
    CoinRenderCameraOverlayBasis excessiveBasis;
    ok &= check(!CoinRenderFrameReuseCore::prepareCameraOverlayBasis(excessive, excessiveBasis) && !excessiveBasis.owner,
                "eye/WORLD light positions and source transforms must obey the precision domain");
  }
  auto edgeLight = nearbyOriginal;
  edgeLight.lightingStates[0].lights[0].position[2] = 30000;
  CoinRenderCameraOverlayBasis edgeBasis;
  ok &= check(CoinRenderFrameReuseCore::prepareCameraOverlayBasis(edgeLight, edgeBasis),
              "an in-domain anchor light must qualify");
  const auto edgeOriginal = edgeLight;
  auto edgeCamera = edgeLight.cameras[0]; edgeCamera.viewMatrix[3][2] = 10000;
  ok &= check(!CoinRenderFrameReuseCore::beginCameraOverlay(edgeLight, edgeCamera, 1300, undo, edgeBasis) &&
              !undo.active && edgeLight.hasSamePayload(edgeOriginal) && edgeLight.revision == edgeOriginal.revision,
              "an out-of-domain resulting eye-space light must fall back before publishing the overlay");
  return ok;
}

bool testPhongWorldBasis() {
  bool ok=true;
  SbMatrix referenceView;
  referenceView.setRotate(SbRotation(SbVec3f(1,2,3),.3f));
  referenceView[3][0]=-2;referenceView[3][1]=1;referenceView[3][2]=-7;
  CoinRenderFramePlan plan=phongReference(referenceView,200);
  const CoinRenderFramePlan original=plan;
  CoinRenderCameraOverlayBasis basis;
  ok &= check(CoinRenderFrameReuseCore::prepareCameraOverlayBasis(plan,basis),"PHONG world-light basis must qualify");
  const auto * geometry=plan.vertices.data(); const auto * indices=plan.indices.data();
  CoinRenderCameraOverlayUndo undo;
  SbMatrix moved;
  moved.setRotate(SbRotation(SbVec3f(-1,.4f,2),-.6f));
  moved[3][0]=3;moved[3][1]=-4;moved[3][2]=-9;
  auto camera=plan.cameras[0];camera.viewMatrix=moved;camera.projectionMatrixCoin[0][0]=.75f;
  camera.farDistance=31;
  auto expected=phongReference(moved,201);
  ok &= check(CoinRenderFrameReuseCore::beginCameraOverlay(plan,camera,201,undo,basis) &&
              sameLightsWithinPrecision(plan,expected) && plan.vertices.data()==geometry &&
              plan.indices.data()==indices && plan.renderStates[0].model==original.renderStates[0].model &&
              plan.renderStates[0].projectionCoin==camera.projectionMatrixCoin,
              "rotation/translation/projection overlay must match captured transformed directional/point/spot lights");
  CoinRenderFrameReuseCore::rollbackCameraOverlay(plan,undo);
  ok &= check(plan.hasSamePayload(original) && plan.revision==original.revision,
              "rollback must restore lighting and camera byte-for-byte");
  camera = original.cameras[0];
  camera.viewMatrix[3][0] += .25f; camera.viewMatrix[3][2] -= 1;
  CoinRenderCameraOverlayUndo pan;
  const auto pannedReference = phongReference(camera.viewMatrix, 202);
  ok &= check(CoinRenderFrameReuseCore::beginCameraOverlay(plan,camera,202,pan,basis) &&
              sameLightsWithinPrecision(plan,pannedReference),
              "pan must match freshly captured transformed lights");
  for (size_t i = 0; i < plan.lightingStates[0].lights.size(); ++i)
    ok &= check(std::memcmp(plan.lightingStates[0].lights[i].direction,
                           original.lightingStates[0].lights[i].direction,
                           sizeof(float) * 3) == 0,
                "pan/dolly must preserve original light direction bits");
  CoinRenderFrameReuseCore::rollbackCameraOverlay(plan,pan);
  camera = original.cameras[0]; camera.projectionMatrixCoin[1][1] = .8f;
  CoinRenderCameraOverlayUndo projection;
  ok &= check(CoinRenderFrameReuseCore::beginCameraOverlay(plan,camera,203,projection,basis) &&
              plan.lightingStates[0].lights[0].sourceModel == original.lightingStates[0].lights[0].sourceModel &&
              sameLightsWithinPrecision(plan,original),
              "projection-only changes must leave eye-space lighting unchanged");
  CoinRenderFrameReuseCore::rollbackCameraOverlay(plan,projection);
  // Commit successive updates, always relative to the same world basis.
  for(int i=0;i<200;++i) {
    SbMatrix next;next.setRotate(SbRotation(SbVec3f(1,2,-1),float(i)*.007f));
    next[3][0]=std::sin(float(i)*.1f)*5;next[3][2]=-8;
    camera=original.cameras[0];camera.viewMatrix=next;
    CoinRenderCameraOverlayUndo committed;
    const auto reference=phongReference(next,300+i);
    ok &= check(CoinRenderFrameReuseCore::beginCameraOverlay(plan,camera,300+i,committed,basis) &&
                sameLightsWithinPrecision(plan,reference),"repeated camera changes must not accumulate light drift");
  }
  CoinRenderCameraOverlayUndo restored;
  ok &= check(CoinRenderFrameReuseCore::beginCameraOverlay(plan,original.cameras[0],900,restored,basis) &&
              plan.hasSamePayload(original),"returning to reference view must restore exact original lighting");
  CoinRenderFramePlan other=original;
  CoinRenderCameraOverlayUndo wrong;
  ok &= check(!CoinRenderFrameReuseCore::beginCameraOverlay(other,camera,901,wrong,basis) && !wrong.active,
              "a basis for another private plan must reject before mutation");
  const auto before=plan;
  auto invalid=camera;invalid.viewMatrix[0][0]=std::numeric_limits<float>::infinity();
  ok &= check(!CoinRenderFrameReuseCore::beginCameraOverlay(plan,invalid,902,wrong,basis) && plan.hasSamePayload(before),
              "non-finite camera must leave lighting/camera unchanged");
  invalid=original.cameras[0];invalid.viewMatrix[0][0]*=2;
  ok &= check(!CoinRenderFrameReuseCore::beginCameraOverlay(plan,invalid,903,wrong,basis),"scaled camera view must fall back");
  for(int kind=0;kind<7;++kind) {
    auto unsupported=original;
    if(kind==0) unsupported.renderStates[0].fogMode=CoinRenderFogMode::FOG;
    if(kind==1) unsupported.renderStates[0].clipPlanesWorld.push_back(SbPlane(SbVec3f(1,0,0),0));
    if(kind==2) unsupported.materials[0].transparency=.2f;
    if(kind==3) unsupported.renderStates[0].polygonOffsetEnabled=true;
    if(kind==4) unsupported.shadowGroups.resize(1);
    if(kind==5) {CoinRenderTextureImageSnapshot texture;texture.producerId=1;unsupported.textures.push_back(texture);}
    if(kind==6) unsupported.draws[0].renderLayer=1;
    CoinRenderCameraOverlayBasis rejected;
    ok &= check(!CoinRenderFrameReuseCore::prepareCameraOverlayBasis(unsupported,rejected) && !rejected.owner,
                "fog/clip/transparency/bias/shadow/RTT/annotation must decline PHONG overlay");
  }
  auto captured=phongReference(moved,904);
  ok &= check(CoinRenderFrameReuseCore::classify(original,captured).kind!=CoinRenderFrameReuseKind::CAMERA_PATCH,
              "external light changes must remain conservatively classified without Wiring's notification proof");
  return ok;
}

}

int
main()
{
  SoDB::init();
  bool ok = testRigidCameraDelta() && testCameraPrecisionFallback() && testPhongWorldBasis() &&
    testTranslationOverlay();
  CoinRenderTextureImageSnapshot alphaImage;
  alphaImage.width = alphaImage.height = 1;
  alphaImage.producerId = 1;
  alphaImage.sceneTransparencyFunction = SoSceneTexture2::NONE;
  ok &= check(!coin_render_texture_has_transparency(alphaImage),
              "Coin NONE forces opaque classification despite unknown producer alpha");
  alphaImage.producerId = 0;
  alphaImage.pixelsRgba = {255,255,255,255};
  alphaImage.sceneTransparencyFunction = SoSceneTexture2::ALPHA_BLEND;
  ok &= check(coin_render_texture_has_transparency(alphaImage),
              "Coin ALPHA_BLEND stays transparent after staged resolution of opaque pixels");
  alphaImage.gpuToken = 1;
  alphaImage.pixelsRgba.clear();
  alphaImage.gpuOpaque = true;
  ok &= check(coin_render_texture_has_transparency(alphaImage),
              "direct RTT follows the same forced classification regardless of opacity hint");
  const CoinRenderFramePlan previous = makePlan(41);
  std::string diagnostic;
  ok &= check(previous.isValid(&diagnostic), "test fixture must be valid");

  CoinRenderFramePlan current = previous;
  current.revision = 42;
  CoinRenderFrameReuseDecision decision =
    CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::REUSE &&
              decision.baseRevision == 41,
              "equal payload must reuse its prior revision");

  current.renderStates[0].transparentTexture = true;
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD,
              "image alpha evidence must invalidate frame reuse even without texture sampling");
  current = previous;
  current.revision = 42;

  SbMatrix moved = SbMatrix::identity();
  moved.setTranslate(SbVec3f(0.25f, 0.0f, -1.0f));
  current.cameras[0].viewMatrix = moved;
  current.renderStates[0].view = moved;
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::CAMERA_PATCH,
              "camera-only dependency footprint must patch");
  CoinRenderFramePlan overlay;
  ok &= check(CoinRenderFrameReuseCore::cameraOverlay(
                previous, current.cameras[0], 46, overlay) &&
              overlay.revision == 46 &&
              overlay.vertices.size() == previous.vertices.size() &&
              overlay.vertices[0].position[0] == previous.vertices[0].position[0] &&
              overlay.indices == previous.indices &&
              overlay.renderStates[0].view == moved,
              "camera overlay must preserve geometry and replace camera state");
  CoinRenderFramePlan inPlace = previous;
  const CoinRenderVertexSnapshot * originalVertices = inPlace.vertices.data();
  const uint32_t * originalIndices = inPlace.indices.data();
  CoinRenderCameraOverlayUndo undo;
  ok &= check(CoinRenderFrameReuseCore::beginCameraOverlay(
                inPlace, current.cameras[0], 47, undo) &&
              undo.active && undo.revision == previous.revision &&
              inPlace.revision == 47 &&
              inPlace.vertices.data() == originalVertices &&
              inPlace.indices.data() == originalIndices &&
              inPlace.renderStates[0].view == moved,
              "transactional overlay must not copy geometry");
  CoinRenderFrameReuseCore::rollbackCameraOverlay(inPlace, undo);
  ok &= check(!undo.active && inPlace.revision == previous.revision &&
              inPlace.hasSamePayload(previous) &&
              inPlace.vertices.data() == originalVertices,
              "rollback must restore the exact previous plan");
  CoinRenderCameraSnapshot invalidCamera = current.cameras[0];
  invalidCamera.farDistance = invalidCamera.nearDistance;
  ok &= check(!CoinRenderFrameReuseCore::beginCameraOverlay(
                inPlace, invalidCamera, 48, undo) && !undo.active &&
              inPlace.revision == previous.revision,
              "invalid camera must fail before mutating the cached plan");
  CoinRenderFramePlan unsuitable = previous;
  unsuitable.renderStates[0].lightModel = CoinRenderLightModel::PHONG;
  ok &= check(CoinRenderFrameReuseCore::cameraOverlay(
                unsuitable, current.cameras[0], 47, overlay) && overlay.revision == 47,
              "opaque PHONG must qualify a world-based mechanical overlay");
  unsuitable = previous;
  unsuitable.renderStates[0].fogMode = CoinRenderFogMode::FOG;
  ok &= check(!CoinRenderFrameReuseCore::cameraOverlay(
                unsuitable, current.cameras[0], 48, overlay),
              "fog must reject mechanical overlay");

  current = previous;
  current.revision = 43;
  current.vertices[0].position[0] = 2.0f;
  current.draws[0].sourceRevision = 12;
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD,
              "stable draw structure with changed payload must rebuild resources");

  current = previous;
  current.revision = 44;
  current.vertices.push_back(CoinRenderVertexSnapshot{});
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::FULL_REBUILD,
              "changed execution structure must rebuild the full plan");

  current = previous;
  current.revision = 0;
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::UNKNOWN,
              "unversioned plans must fail closed as unknown");

  current = previous;
  current.revision = 45;
  CoinRenderTextureImageSnapshot opaque;
  opaque.width = 1;
  opaque.height = 1;
  opaque.gpuToken = 77;
  current.textures.push_back(opaque);
  decision = CoinRenderFrameReuseCore::classify(previous, current);
  ok &= check(decision.kind == CoinRenderFrameReuseKind::UNKNOWN,
              "opaque connector resources must not infer reusable ownership");

  CoinRenderFramePlan targetFrame = previous;
  targetFrame.viewports[0].width = 64;
  targetFrame.viewports[0].height = 64;
  CoinRenderTargetP target(SbVec2i32(64, 64));
#if defined(HAVE_COIN_BGFX)
  target.depthReadbackEnabled = false;
#endif
  CoinRenderFrameExecutionResult executed = target.executeFrame(targetFrame);
  ok &= check(executed.status == CoinRenderBackendStatus::SUCCESS &&
              target.lastValidatedPlanRevision == 41,
              "target must validate the initial frame");
  targetFrame.revision = 42;
  targetFrame.cameras[0].viewMatrix = moved;
  targetFrame.renderStates[0].view = moved;
  executed = target.executeFrame(targetFrame,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH, 41));
  ok &= check(executed.status == CoinRenderBackendStatus::SUCCESS &&
              target.lastValidatedPlanRevision == 42,
              "target must advance the validated camera-patch revision");
  targetFrame.revision = 43;
  targetFrame.indices[0] = 99;
  executed = target.executeFrame(targetFrame,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH, 41));
  ok &= check(executed.status != CoinRenderBackendStatus::SUCCESS &&
              target.lastValidatedPlanRevision == 42,
              "stale base must revalidate and reject invalid geometry");
  targetFrame.indices[0] = 0;
  ok &= check(target.resize(SbVec2i32(128, 128)) &&
              target.lastValidatedPlanRevision == 0,
              "resize must invalidate the target validation revision");
  targetFrame.revision = 44;
  executed = target.executeFrame(targetFrame,
    CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH, 42));
  ok &= check(executed.status == CoinRenderBackendStatus::SUCCESS &&
              target.lastValidatedPlanRevision == 44,
              "a valid subviewport must survive target resize and camera reuse");

  auto optionsChanged = targetFrame;
  optionsChanged.revision = targetFrame.revision + 1;
  optionsChanged.transparency.layers = 8;
  ok &= check(!targetFrame.hasSamePayload(optionsChanged) &&
                  CoinRenderFrameReuseCore::classify(targetFrame, optionsChanged).kind ==
                      CoinRenderFrameReuseKind::FULL_REBUILD,
              "peel count changes must not reuse a captured camera/geometry plan");
  optionsChanged = targetFrame;
  optionsChanged.revision = targetFrame.revision + 1;
  optionsChanged.transparency.bufferBudget /= 2;
  ok &= check(!targetFrame.hasSamePayload(optionsChanged) &&
                  CoinRenderFrameReuseCore::classify(targetFrame, optionsChanged).kind ==
                      CoinRenderFrameReuseKind::FULL_REBUILD,
              "budget changes must not reuse the old plan");
  if (!ok) return 1;
  std::cout << "CoinRenderFrameReuseCoreTest passed\n";
  return 0;
}
