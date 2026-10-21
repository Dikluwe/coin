#include <Inventor/SoDB.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoBumpMapCoordinate.h>
#include <Inventor/nodes/SoBumpMapTransform.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "actions/CoinRenderActionP.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#define CHECK(test, message) do { if (!(test)) { \
  std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, message); return 1; } } while (0)

// Initialize after the action to exercise derived-type registration refresh.
class LateBump : public SoBumpMap {
  SO_NODE_HEADER(LateBump);
public:
  static void initClass() { SO_NODE_INIT_CLASS(LateBump, SoBumpMap, "BumpMap"); }
  LateBump() { SO_NODE_CONSTRUCTOR(LateBump); }
protected:
  ~LateBump() override {}
};
SO_NODE_SOURCE(LateBump);

int main(int argc, char **argv) {
  const bool gpu = argc == 2 && std::strcmp(argv[1], "--gpu") == 0;
  SoDB::init();
  CoinRenderAction::initClass();
  if (gpu && !CoinRenderAction::isGpuBackendAvailable()) {
    std::fprintf(stderr,"SKIP: compiled GPU executor unavailable\n"); return 77;
  }
  CoinRenderRenderer requestedRenderer=COIN_RENDER_RENDERER_UNKNOWN;
  bool bgfxBackend = false;
  if (gpu) {
    CoinRenderCapabilities caps{};
    CHECK(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))==0&&
      caps.gpu_available && caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN,"GPU receipt");
    requestedRenderer=static_cast<CoinRenderRenderer>(caps.renderer);
    bgfxBackend = caps.backend == COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION;
    std::printf("bump_gpu adapter=%s renderer=%u vendor=0x%x device=0x%x\n",caps.adapter_name,unsigned(caps.renderer),unsigned(caps.vendor_id),unsigned(caps.device_id));
  }
  CoinRenderOptions initialOptions;
  initialOptions.renderer=requestedRenderer;
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(32,32),initialOptions));
  CHECK(target.get() && (!gpu || target->getOptions().renderer==requestedRenderer),
    "initial target uses queried renderer");
  if (!gpu) target->getPimpl().get().backend.reset(new CoinRenderCpuReferenceBackend);
  auto *root = new SoSeparator; root->ref();
  auto *camera = new SoOrthographicCamera; camera->position.setValue(0,0,4); camera->height=4; root->addChild(camera);
  auto *light = new SoLightModel; light->model = SoLightModel::BASE_COLOR; root->addChild(light);
  auto *material = new SoMaterial; material->diffuseColor.setValue(.8f,.2f,.1f); root->addChild(material);
  auto *bump = new SoBumpMap; root->addChild(bump);
  root->addChild(new SoCube);
  CoinRenderAction action(SbViewportRegion(32,32)); action.setRenderTarget(target.get());
  action.apply(root);
  CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS, "empty bump is inert");
  std::vector<uint8_t> original, color; std::vector<float> depth, originalDepth;
  target->readbackRGBA(original); target->readbackDepth(originalDepth);
  CHECK(original.size()==32*32*4 && originalDepth.size()==32*32, "complete publication");
  CHECK(original[(16*32+16)*4] > original[0], "baseline contains cube, not just clear");
  auto rejected = [&]() {
    const auto serial=target->getLastSubmissionSerial();
    action.apply(root);
    target->readbackRGBA(color); target->readbackDepth(depth);
    return action.getLastStatus()==CoinRenderAction::UNSUPPORTED &&
      (std::strstr(action.getLastError().getString(), "bump") ||
       std::strstr(action.getLastError().getString(), "Bump")) &&
      target->getLastSubmissionSerial()==serial && color==original && depth==originalDepth;
  };
  auto recover = [&]() {
    bump->image.setValue(SbVec2s(0,0),0,nullptr);
    action.apply(root); target->readbackRGBA(color); target->readbackDepth(depth);
    return action.getLastStatus()==CoinRenderAction::SUCCESS && color==original && depth==originalDepth;
  };
  unsigned cases=0;
  const unsigned char pixels[]={128,128,255,255};
  for (int components=1;components<=4;++components) {
    bump->image.setValue(SbVec2s(1,1),components,pixels);
    CHECK(rejected(), "active height/normal map rejects, preserving color/depth/serial");
    CHECK(recover(), "same action and target recover after removing image"); ++cases;
  }
  // The legacy GL node reads image even when its field is marked ignored.
  bump->image.setValue(SbVec2s(1,1),3,pixels); bump->image.setIgnored(TRUE);
  CHECK(rejected(), "ignored image field does not silently bypass active GL effect");
  bump->image.setIgnored(FALSE); CHECK(recover(), "recover from ignored field case"); ++cases;
  auto *sw=new SoSwitch; auto *hidden=new SoBumpMap;
  hidden->image.setValue(SbVec2s(1,1),3,pixels); sw->addChild(hidden);
  sw->whichChild=SO_SWITCH_NONE; root->insertChild(sw,3);
  action.apply(root); CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS, "untraversed bump does not reject");
  sw->whichChild=SO_SWITCH_ALL;
  const auto neutralSerial=target->getLastSubmissionSerial();
  action.apply(root);target->readbackRGBA(color);target->readbackDepth(depth);
  CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS && target->getLastSubmissionSerial()>neutralSerial &&
        color==original && depth==originalDepth,
        "later empty bump neutralizes earlier traversed declaration before consumption");
  sw->ref();root->removeChild(sw);root->insertChild(sw,root->findChild(bump)+1);sw->unref();
  CHECK(rejected(), "traversed switch bump after empty declaration is consumed and rejects");
  root->removeChild(sw); CHECK(recover(), "recover after removing switch"); ++cases;
  root->insertChild(new SoBumpMapCoordinate,3); root->insertChild(new SoBumpMapTransform,4);
  action.apply(root); CHECK(action.getLastStatus()==CoinRenderAction::SUCCESS, "bump coordinates/transform alone are inert"); ++cases;
  LateBump::initClass(); auto *late=new LateBump;
  late->image.setValue(SbVec2s(1,1),3,pixels); root->insertChild(late,root->findChild(bump)+1);
  CHECK(rejected(), "late subclass cannot bypass rejection");
  root->removeChild(late); CHECK(recover(), "recover after late subclass"); ++cases;
  // A pending filename must not be silently ignored; no file IO in this test.
  bump->filename.enableNotify(FALSE);
  bump->filename.setValue("pending-bump-not-loaded.png");
  bump->touch(); // Invalidate retained traversal without scheduling the file sensor.
  CHECK(rejected(), "pending filename without pixels rejects conservatively");
  bump->filename.setValue(""); bump->filename.enableNotify(TRUE);
  CHECK(recover(), "recover after pending filename cleared"); ++cases;
  // Child captures must use the same rejection before a parent publishes.
  auto *child=new SoSeparator; child->addChild(new SoOrthographicCamera);
  auto *childBump=new SoBumpMap; childBump->image.setValue(SbVec2s(1,1),3,pixels);
  child->addChild(childBump); child->addChild(new SoCube);
  auto *rtt=new SoSceneTexture2; rtt->size.setValue(8,8); rtt->scene=child;
  root->insertChild(rtt,3);
  CHECK(rejected(), "bump in RTT child preserves parent color/depth/serial");
  root->removeChild(rtt); CHECK(recover(), "recover after unsupported RTT child removed"); ++cases;
  // Rejection must remain visible on repeated apply (no reuse of a stale proof).
  bump->image.setValue(SbVec2s(1,1),3,pixels);
  CHECK(rejected() && rejected(), "repeated unsupported graph cannot replay cached frame as success");
  CHECK(recover(), "final recovery"); ++cases;
  root->unref();
  if (gpu) {
    auto * qualified = new SoSeparator; qualified->ref();
    auto * view = new SoOrthographicCamera;
    view->position.setValue(0,0,4); view->height=4;
    qualified->addChild(view);
    auto * sun = new SoDirectionalLight;
    sun->direction.setValue(0,0,-1);
    qualified->addChild(sun);
    auto * hints = new SoShapeHints;
    hints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;
    qualified->addChild(hints);
    auto * colorMaterial = new SoMaterial;
    colorMaterial->diffuseColor.setValue(.8f,.2f,.1f);
    qualified->addChild(colorMaterial);
    auto * normalMap = new SoBumpMap;
    normalMap->image.setValue(SbVec2s(1,1),3,pixels);
    qualified->addChild(normalMap);
    qualified->addChild(new SoCube);
    std::unique_ptr<CoinRenderTarget> qualifiedTarget(
      CoinRenderTarget::createOffscreen(SbVec2i32(32,32),initialOptions));
    CHECK(qualifiedTarget.get() &&
      qualifiedTarget->getOptions().renderer==requestedRenderer,
      "R26 positive target uses queried renderer");
    CoinRenderAction qualifiedAction(SbViewportRegion(32,32));
    qualifiedAction.setTransparencyType(CoinRenderAction::NONE);
    qualifiedAction.setRenderTarget(qualifiedTarget.get());
    qualifiedAction.apply(qualified);
    CHECK(qualifiedAction.getLastStatus()==CoinRenderAction::SUCCESS,
          qualifiedAction.getLastError().getString());
    std::vector<uint8_t> image;
    qualifiedTarget->readbackRGBA(image);
    CHECK(image.size()==32*32*4 && image[(16*32+16)*4] > image[0],
          "qualified public bump scene must publish shaded pixels");
    CHECK(qualifiedTarget->getLastSubmissionSerial()>0,
          "qualified public bump scene must publish a serial");
    std::printf("R26 public bump PASS: center=[%u,%u,%u,%u] serial=%llu\n",
      unsigned(image[(16*32+16)*4]), unsigned(image[(16*32+16)*4+1]),
      unsigned(image[(16*32+16)*4+2]), unsigned(image[(16*32+16)*4+3]),
      static_cast<unsigned long long>(qualifiedTarget->getLastSubmissionSerial()));
    auto * base = new SoTexture2;
    const unsigned char basePixels[] = {128,128,128,255};
    base->image.setValue(SbVec2s(1,1),4,basePixels);
    base->model = SoTexture2::MODULATE;
    qualified->insertChild(base,4);
    const uint64_t beforeTexture = qualifiedTarget->getLastSubmissionSerial();
    qualifiedAction.apply(qualified);
    CHECK(qualifiedAction.getLastStatus()==CoinRenderAction::SUCCESS,
          qualifiedAction.getLastError().getString());
    qualifiedTarget->readbackRGBA(image);
    const size_t center = (16*32+16)*4;
    const uint8_t texturedExpected[] = {102,26,13,255};
    CHECK(image.size()==32*32*4 &&
          std::memcmp(image.data()+center,texturedExpected,4)==0 &&
          qualifiedTarget->getLastSubmissionSerial()>beforeTexture,
          "public bump plus SoTexture2 must modulate and publish a fresh frame");
    std::printf("R26 public textured bump PASS: center=[%u,%u,%u,%u] serial=%llu\n",
      unsigned(image[center]), unsigned(image[center+1]),
      unsigned(image[center+2]), unsigned(image[center+3]),
      static_cast<unsigned long long>(qualifiedTarget->getLastSubmissionSerial()));
    CoinRenderOptions srgbOptions;
    srgbOptions.renderer=requestedRenderer;
    srgbOptions.storedTextureColorSpace = COIN_RENDER_TEXTURE_SRGB;
    // BGFX exposes only isotropic or native maximum, while wgpu admits 8.
    // This is the documented initial backend domain, independent of vendor.
    const unsigned srgbAnisotropy = bgfxBackend ? 16u : 8u;
    srgbOptions.maxTextureAnisotropy = srgbAnisotropy;
    base->ref();
    qualified->removeChild(base);
    auto * highQuality = new SoComplexity;
    highQuality->textureQuality = 1.0f;
    qualified->insertChild(highQuality,4);
    auto * qualityBase = new SoTexture2;
    qualityBase->image.setValue(SbVec2s(1,1),4,basePixels);
    qualityBase->model = SoTexture2::MODULATE;
    qualified->insertChild(qualityBase,5);
    std::unique_ptr<CoinRenderTarget> srgbTarget(
      CoinRenderTarget::createOffscreen(SbVec2i32(32,32), srgbOptions));
    CHECK(srgbTarget.get() && (!gpu || srgbTarget->getOptions().renderer==requestedRenderer),
      "sRGB bump target uses queried renderer");
    CoinRenderAction srgbAction(SbViewportRegion(32,32));
    srgbAction.setTransparencyType(CoinRenderAction::NONE);
    srgbAction.setRenderTarget(srgbTarget.get());
    srgbAction.apply(qualified);
    std::vector<uint8_t> srgbImage;
    srgbTarget->readbackRGBA(srgbImage);
    CHECK(srgbAction.getLastStatus()==CoinRenderAction::SUCCESS &&
          srgbImage.size()==image.size() && srgbTarget->getLastSubmissionSerial()>0,
          "public bump plus sRGB SoTexture2 must publish");
    const auto & srgbPlan = srgbAction.getPimpl()->lastValidPlan;
    bool capturedRequestedAnisotropy = false;
    for (const auto & sampler : srgbPlan.samplers) {
      std::printf("R26 sRGB sampler filter=%u anisotropy=%u\n",
        unsigned(sampler.filter), unsigned(sampler.maxAnisotropy));
      capturedRequestedAnisotropy |= sampler.maxAnisotropy==srgbAnisotropy;
    }
    CHECK(capturedRequestedAnisotropy,
          "public bump must capture the exact admitted requested anisotropy");
    const uint8_t srgbExpected[] = {44,11,6,255};
    for (size_t channel=0; channel<4; ++channel)
      CHECK(std::abs(int(srgbImage[center+channel])-int(srgbExpected[channel]))<=1,
            "public bump sRGB decode must modulate diffuse in linear space");
    CHECK(srgbImage[center] < image[center],
          "sRGB opt-in must change the bump base color");
    std::printf("R26 public sRGB bump PASS: center=[%u,%u,%u,%u] serial=%llu\n",
      unsigned(srgbImage[center]), unsigned(srgbImage[center+1]),
      unsigned(srgbImage[center+2]), unsigned(srgbImage[center+3]),
      static_cast<unsigned long long>(srgbTarget->getLastSubmissionSerial()));
    std::vector<float> srgbDepth, srgbRejectedDepth;
    srgbTarget->readbackDepth(srgbDepth);
    if (bgfxBackend) {
      // The same captured frame/resources and live target retain a good sRGB
      // publication. Only the unsupported sampler factor changes for admission.
      auto unsupportedFactor = srgbPlan;
      for (auto &sampler : unsupportedFactor.samplers)
        sampler.maxAnisotropy = 8;
      const auto beforeFactor = srgbTarget->getLastSubmissionSerial();
      const auto refusal = srgbTarget->getPimpl()->executeFrame(unsupportedFactor);
      std::vector<uint8_t> factorColor;
      std::vector<float> factorDepth;
      srgbTarget->readbackRGBA(factorColor); srgbTarget->readbackDepth(factorDepth);
      CHECK(refusal.status == CoinRenderBackendStatus::UNSUPPORTED &&
            refusal.diagnostic.find("anisotrop") != std::string::npos &&
            refusal.submissionSerial == 0 &&
            srgbTarget->getLastSubmissionSerial() == beforeFactor &&
            factorColor == srgbImage && factorDepth == srgbDepth,
            "BGFX unavailable anisotropy 8 refuses atomically on the live sRGB target");
      srgbAction.apply(qualified);
      srgbTarget->readbackRGBA(factorColor); srgbTarget->readbackDepth(factorDepth);
      CHECK(srgbAction.getLastStatus() == CoinRenderAction::SUCCESS &&
            srgbTarget->getLastSubmissionSerial() > beforeFactor &&
            factorColor == srgbImage && factorDepth == srgbDepth,
            "first public valid native-maximum sRGB apply recovers exactly after factor 8 refusal");
    }
    const auto srgbSerial = srgbTarget->getLastSubmissionSerial();
    const unsigned char srgbTranslucent[] = {128,128,128,128};
    qualityBase->image.setValue(SbVec2s(1,1),4,srgbTranslucent);
    srgbAction.apply(qualified);
    std::vector<uint8_t> srgbRejected;
    srgbTarget->readbackRGBA(srgbRejected);
    srgbTarget->readbackDepth(srgbRejectedDepth);
    CHECK(srgbAction.getLastStatus()==CoinRenderAction::UNSUPPORTED &&
          srgbTarget->getLastSubmissionSerial()==srgbSerial &&
          srgbRejected==srgbImage && srgbRejectedDepth==srgbDepth,
          "sRGB alpha rejection must preserve color, depth and serial");
    qualityBase->image.setValue(SbVec2s(1,1),4,basePixels);
    srgbAction.apply(qualified);
    srgbTarget->readbackRGBA(srgbRejected);
    CHECK(srgbAction.getLastStatus()==CoinRenderAction::SUCCESS &&
          srgbRejected==srgbImage &&
          srgbTarget->getLastSubmissionSerial()>srgbSerial,
          "sRGB bump must recover after restoring opaque alpha");
    srgbAction.setRenderTarget(nullptr);
    qualified->removeChild(highQuality);
    qualified->removeChild(qualityBase);
    qualified->insertChild(base,4);
    base->unref();
    const auto opaqueImage = image;
    std::vector<float> opaqueDepth;
    qualifiedTarget->readbackDepth(opaqueDepth);
    const uint64_t opaqueSerial = qualifiedTarget->getLastSubmissionSerial();
    const unsigned char translucentPixels[] = {128,128,128,128};
    base->image.setValue(SbVec2s(1,1),4,translucentPixels);
    qualifiedAction.apply(qualified);
    std::vector<float> rejectedDepth;
    qualifiedTarget->readbackRGBA(image);
    qualifiedTarget->readbackDepth(rejectedDepth);
    CHECK(qualifiedAction.getLastStatus()==CoinRenderAction::UNSUPPORTED &&
          image==opaqueImage && rejectedDepth==opaqueDepth &&
          qualifiedTarget->getLastSubmissionSerial()==opaqueSerial,
          "public translucent base with bump must reject without publication");
    base->image.setValue(SbVec2s(1,1),4,basePixels);
    qualifiedAction.apply(qualified);
    qualifiedTarget->readbackRGBA(image);
    CHECK(qualifiedAction.getLastStatus()==CoinRenderAction::SUCCESS &&
          image==opaqueImage && qualifiedTarget->getLastSubmissionSerial()>opaqueSerial,
          "public bump texture must recover after restoring opaque alpha");
    std::printf("R26 public alpha gate PASS: rejected without color/depth/serial change; recovered serial=%llu\n",
      static_cast<unsigned long long>(qualifiedTarget->getLastSubmissionSerial()));
    qualifiedAction.setRenderTarget(nullptr);
    qualified->unref();
  }
  std::printf("B12 PASS: %u cases; executor=%s; rejection, color/depth/serial and recovery\n",cases,gpu?"compiled GPU":"CPU reference");
  return 0;
}
