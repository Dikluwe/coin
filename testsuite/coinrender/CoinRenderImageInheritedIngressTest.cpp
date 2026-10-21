#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
bool check(bool good,const std::string & label) {
  if(!good) std::cerr << "image_inherited_ingress FAIL " << label << '\n';
  return good;
}
class Capture : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderFramePlan plan;
  bool captured=false;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame,
                               CoinRenderTargetP & target) override {
    plan=frame;captured=true;
    return CoinRenderCpuReferenceBackend::submit(frame,target);
  }
};
struct Scene {
  SoSeparator * root=new SoSeparator;
  Scene() {
    root->ref();
    auto * camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=.1f;camera->farDistance=10;root->addChild(camera);
    auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;
    root->addChild(light);
    auto * quality=new SoComplexity;quality->textureQuality=.1f;root->addChild(quality);
    auto * texture=new SoTexture2;
    const unsigned char texel[]={37,149,231,255};
    texture->image.setValue(SbVec2s(1,1),4,texel);
    texture->model=SoTexture2::REPLACE;root->addChild(texture);
    auto * image=new SoImage;
    image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;
    std::vector<unsigned char> payload(16*16*3,255);
    image->image.setValue(SbVec2s(16,16),3,payload.data());root->addChild(image);
  }
  ~Scene(){root->unref();}
};
bool ingress() {
  Scene scene;
  std::unique_ptr<CoinRenderTarget> capturedTarget(
    CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if(!check(bool(capturedTarget),"capture target")) return false;
  auto * capture=new Capture;
  capturedTarget->getPimpl()->backend.reset(capture);
  CoinRenderAction action(SbViewportRegion(64,64));
  action.setRenderTarget(capturedTarget.get());
  action.setBackgroundColor(SbColor4f(0,0,0,1));
  action.apply(scene.root);
  const bool captured=capture->captured &&
    action.getLastStatus()==CoinRenderAction::SUCCESS;
  CoinRenderFramePlan good=capture->plan;
  action.setRenderTarget(nullptr);
  if(!check(captured,"valid inherited-image capture")) return false;
  size_t raster=good.renderStates.size();
  for(size_t index=0;index<good.renderStates.size();++index)
    if(good.renderStates[index].rasterPixels) raster=index;
  if(!check(raster<good.renderStates.size(),"captured raster state")) return false;
  const uint32_t source=good.renderStates[raster].rasterTextureSourceSlot;
  if(!check(source<good.renderStates.size() && source!=raster &&
     !good.renderStates[source].rasterPixels &&
     good.renderStates[source].hasTexture,
     "valid non-raster inherited texture source")) return false;
  CoinRenderTargetP direct(SbVec2i32(64,64));
  direct.backend.reset(new CoinRenderCpuReferenceBackend);
  direct.depthReadbackEnabled=true;
  good.revision=100;
  const auto first=direct.executeFrame(good);
  if(!check(first.status==CoinRenderBackendStatus::SUCCESS &&
    direct.colorBuffer.size()==64u*64u*4u &&
    direct.depthBuffer.size()==64u*64u,
    "direct CPU valid inherited image")) return false;
  const size_t center=(32u*64u+32u)*4u;
  if(!check(direct.colorBuffer[center]==37 &&
    direct.colorBuffer[center+1]==149 &&
    direct.colorBuffer[center+2]==231,
    "direct CPU executes inherited texture rather than image payload")) return false;
  uint64_t revision=101;
  const auto rejects=[&](const CoinRenderFramePlan & candidate,
                         const std::string & label) {
    CoinRenderFramePlan broken=candidate;
    broken.revision=revision++;
    const auto rgba=direct.colorBuffer;
    const auto depth=direct.depthBuffer;
    const uint64_t serial=direct.lastSubmissionSerial;
    // Bypass Target/Core validation here. The adversarial contract belongs
    // to the CPU executor's own ingress, before it clears or writes a pixel.
    const auto result=direct.backend->submit(broken,direct);
    const bool refused=result.status!=CoinRenderBackendStatus::SUCCESS &&
      !result.diagnostic.empty() && direct.colorBuffer==rgba &&
      direct.depthBuffer==depth && direct.lastSubmissionSerial==serial;
    std::cout << "image_inherited_ingress " << label << " status="
              << int(result.status) << " diagnostic=" << result.diagnostic
              << " preserved=" << refused << '\n';
    if(!check(refused,label+" must reject before changing published frame"))
      return false;
    good.revision=revision++;
    const auto recovery=direct.executeFrame(good);
    return check(recovery.status==CoinRenderBackendStatus::SUCCESS &&
      direct.colorBuffer==rgba && direct.depthBuffer==depth &&
      direct.lastSubmissionSerial>serial,
      label+" valid frame recovers identically");
  };
  bool ok=true;
  auto changed=good;
  changed.renderStates[raster].rasterTextureSourceSlot=
    static_cast<uint32_t>(good.renderStates.size());
  ok &= rejects(changed,"source-out-of-range");
  changed=good;changed.renderStates[raster].rasterTextureSourceSlot=
    static_cast<uint32_t>(raster);
  ok &= rejects(changed,"source-self-reference");
  changed=good;changed.renderStates[source].rasterPixels=true;
  changed.renderStates[source].rasterTextureSourceSlot=static_cast<uint32_t>(raster);
  ok &= rejects(changed,"recursive-raster-source");
  changed=good;
  changed.renderStates[raster].rasterTextureCoordinates[0][0]=
    std::numeric_limits<float>::quiet_NaN();
  ok &= rejects(changed,"raw-S-NaN");
  changed=good;changed.renderStates[raster].rasterTextureCoordinates[0][3]=0;
  ok &= rejects(changed,"raw-Q-zero");
  changed=good;
  changed.renderStates[raster].rasterTextureProvenance[0]=255;
  ok &= rejects(changed,"provenance-enum-invalid");
  changed=good;
  changed.renderStates[source].textureMatrix[3][3]=
    std::numeric_limits<float>::quiet_NaN();
  ok &= rejects(changed,"source-texture-matrix-NaN");
  changed=good;
  changed.renderStates[source].textureImageSlot=
    static_cast<uint32_t>(good.textures.size());
  ok &= rejects(changed,"source-image-slot-out-of-range");
  changed=good;
  changed.renderStates[source].samplerSlot=
    static_cast<uint32_t>(good.samplers.size());
  ok &= rejects(changed,"source-sampler-slot-out-of-range");
  return ok;
}
}
int main() {
  SoDB::init();CoinRenderAction::initClass();
  return ingress()?0:1;
}
