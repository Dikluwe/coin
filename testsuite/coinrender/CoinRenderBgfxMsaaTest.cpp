#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int side=32;
bool check(bool valid,const std::string & label) {
  if(!valid) std::cerr << "bgfx_msaa FAIL " << label << '\n';
  return valid;
}
CoinRenderCapabilities capabilities(uint32_t targetKind) {
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities(targetKind,&caps,sizeof(caps))!=0)
    caps.gpu_available=false;
  std::cout << "bgfx_msaa backend=" << caps.backend << " renderer=" << caps.renderer
            << " vendor=" << caps.vendor_id << " adapter=" << caps.adapter_name
            << " gpu=" << caps.gpu_available << '\n';
  return caps;
}
bool bgfx(const CoinRenderCapabilities & caps) {
  return check(caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION &&
    caps.gpu_available && caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN,
    "actual BGFX GPU and renderer required");
}
CoinRenderOptions options(const CoinRenderCapabilities & caps,uint32_t samples,
                          CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED) {
  CoinRenderOptions result;
  result.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  result.sampleCount=samples;
  result.sceneTexture=route;
  return result;
}
struct Triangle {
  SoSeparator * root=new SoSeparator;
  SoMaterial * material=new SoMaterial;
  SoAlphaTest * alpha=new SoAlphaTest;
  Triangle() {
    root->ref();
    auto * camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,2);camera->height=2;
    camera->nearDistance=1;camera->farDistance=3;root->addChild(camera);
    auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;
    root->addChild(light);
    material->diffuseColor.setValue(1,0,0);root->addChild(material);
    alpha->function=SoAlphaTest::ALWAYS;alpha->value=.5f;root->addChild(alpha);
    auto * coords=new SoCoordinate3;
    const SbVec3f points[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
                            SbVec3f(-1,.83f,0)};
    coords->point.setValues(0,3,points);root->addChild(coords);
    auto * shape=new SoIndexedFaceSet;
    const int32_t indices[]={0,1,2,-1};shape->coordIndex.setValues(0,4,indices);
    root->addChild(shape);
  }
  ~Triangle(){root->unref();}
};
struct Frame {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial=0;
};
bool capture(CoinRenderAction & action,CoinRenderTarget & target,SoSeparator * root,
             Frame & frame,const std::string & label) {
  action.apply(root);
  target.readbackRGBA(frame.rgba);target.readbackDepth(frame.depth);
  frame.serial=target.getLastSubmissionSerial();
  const bool good=action.getLastStatus()==CoinRenderAction::SUCCESS &&
    frame.rgba.size()==size_t(target.getSize()[0]*target.getSize()[1]*4) &&
    frame.depth.size()==size_t(target.getSize()[0]*target.getSize()[1]) &&
    frame.serial>0;
  if(!good) std::cerr << label << " status=" << int(action.getLastStatus())
                      << " error=" << action.getLastError().getString()
                      << " rgba=" << frame.rgba.size() << " depth=" << frame.depth.size()
                      << " serial=" << frame.serial << '\n';
  return check(good,label);
}
struct Coverage {
  size_t empty=0,whole=0,partial=0;
  std::array<size_t,5> alphaBins{{0,0,0,0,0}};
};
Coverage classify(const Frame & frame) {
  Coverage result;
  for(size_t i=0;i<frame.depth.size();++i) {
    const unsigned alpha=frame.rgba[4*i+3];
    if(alpha==0) ++result.empty;
    else if(alpha==255) ++result.whole;
    else ++result.partial;
    if(alpha==0) ++result.alphaBins[0];
    else if(alpha<96) ++result.alphaBins[1];
    else if(alpha<160) ++result.alphaBins[2];
    else if(alpha<255) ++result.alphaBins[3];
    else ++result.alphaBins[4];
  }
  return result;
}
bool sampleQuantization(const Frame & frame,uint32_t samples) {
  size_t quarter=0,half=0,threeQuarter=0;
  for(size_t i=0;i<frame.depth.size();++i) {
    const int alpha=frame.rgba[4*i+3];
    if(alpha==0 || alpha==255) continue;
    const int count=int(std::lround(double(alpha)*samples/255.0));
    const int quantized=int(std::lround(255.0*count/samples));
    if(count<=0 || count>=int(samples) || std::abs(alpha-quantized)>1)
      return false;
    quarter+=(count==1);half+=(count*2==int(samples));
    threeQuarter+=(count==3 && samples==4);
  }
  std::cout << "bgfx_msaa sample_quantization samples=" << samples
            << " one_sample=" << quarter << " half=" << half
            << " three_samples=" << threeQuarter << '\n';
  return samples==1 ? quarter+half+threeQuarter==0 :
    samples==2 ? half>0 : quarter+threeQuarter>0;
}
bool depthContract(const Frame & one,const Frame & multi,uint32_t samples) {
  if(one.depth.size()!=side*side || multi.depth.size()!=side*side) return false;
  // Orthographic near=1, far=3, plane z=0, eye distance=2: exact depth 0.5.
  // This does not compare depth bits across different 1x/MSAA formats.
  constexpr float plane=.5f;
  auto mismatch=[&](size_t i,unsigned alpha,float expected,float actual,
                    const char *where) {
    uint32_t bits=0;std::memcpy(&bits,&actual,sizeof(bits));
    std::cerr << "bgfx_msaa depth_mismatch where=" << where << " samples=" << samples
              << " x=" << (i%side) << " y=" << (i/side) << " alpha=" << alpha
              << " expected=" << expected << " actual=" << actual
              << " bits=0x" << std::hex << bits << std::dec << '\n';
    return false;
  };
  size_t legacyInterior=0;
  for(size_t i=0;i<one.depth.size();++i)
    if(one.rgba[4*i+3]==255) {
      if(std::abs(one.depth[i]-plane)>1e-6f)
        return mismatch(i,one.rgba[4*i+3],plane,one.depth[i],"legacy-interior");
      ++legacyInterior;
    }
  if(legacyInterior==0) return false;
  size_t edge=0,interior=0,background=0;
  for(size_t i=0;i<multi.depth.size();++i) {
    const unsigned alpha=multi.rgba[4*i+3];
    if(alpha) {
      if(multi.depth[i]!=plane)
        return mismatch(i,alpha,plane,multi.depth[i],"msaa-covered");
      if(alpha<255) ++edge;else ++interior;
    } else {
      if(multi.depth[i]!=1.f)
        return mismatch(i,alpha,1.f,multi.depth[i],"msaa-background");
      ++background;
    }
  }
  std::cout << "bgfx_msaa depth samples=" << samples << " plane=" << plane
            << " edge=" << edge << " interior=" << interior
            << " background=" << background << '\n';
  return edge>0 && interior>0 && background>0;
}
bool offscreen() {
  const auto caps=capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if(!bgfx(caps)) return false;
  Triangle triangle;std::array<Frame,3> frames;
  const uint32_t counts[]={1,2,4};
  bool ok=true;
  for(size_t k=0;k<3;++k) {
    auto selected=options(caps,counts[k]);
    std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(side,side),selected));
    if(!check(bool(target) && target->getOptions().renderer==selected.renderer &&
              target->getOptions().sampleCount==counts[k],"offscreen options retained")) return false;
    target->setDepthReadbackEnabled(TRUE);
    CoinRenderAction action(SbViewportRegion(side,side));
    action.setBackgroundColor(SbColor4f(0,0,0,0));action.setRenderTarget(target.get());
    ok &= capture(action,*target,triangle.root,frames[k],"offscreen triangle");
    const auto coverage=classify(frames[k]);
    std::cout << "bgfx_msaa offscreen samples=" << counts[k]
              << " empty=" << coverage.empty << " whole=" << coverage.whole
              << " partial=" << coverage.partial << " alpha_bins="
              << coverage.alphaBins[0] << ',' << coverage.alphaBins[1] << ','
              << coverage.alphaBins[2] << ',' << coverage.alphaBins[3] << ','
              << coverage.alphaBins[4] << '\n';
    ok &= check(coverage.empty>0 && coverage.whole>0 &&
      (k==0?coverage.partial==0:coverage.partial>0),"sample-resolved edge coverage");
    ok &= check(sampleQuantization(frames[k],counts[k]),
                "alpha must encode the requested number of covered samples");
    if(k==2) {
      const Frame stable=frames[k];
      triangle.alpha->value=std::numeric_limits<float>::quiet_NaN();
      action.apply(triangle.root);
      Frame rejected;target->readbackRGBA(rejected.rgba);
      target->readbackDepth(rejected.depth);
      rejected.serial=target->getLastSubmissionSerial();
      ok &= check(action.getLastStatus()==CoinRenderAction::INVALID_SCENE &&
        rejected.rgba==stable.rgba && rejected.depth==stable.depth &&
        rejected.serial==stable.serial,"invalid alpha rolls back RGBA/depth/serial");
      triangle.alpha->value=.5f;
      Frame recovered;
      ok &= capture(action,*target,triangle.root,recovered,"offscreen recovery") &&
        check(recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
              recovered.serial>stable.serial,"offscreen exact recovery");
    }
    action.setRenderTarget(nullptr);
  }
  ok &= check(frames[0].rgba!=frames[1].rgba && frames[0].rgba!=frames[2].rgba,
              "2x and 4x must visibly resolve geometry differently from 1x");
  ok &= check(depthContract(frames[0],frames[1],2) &&
              depthContract(frames[0],frames[2],4),
              "multisample depth resolves minimum covered sample");
  for(uint32_t invalid : {0u,3u,8u}) {
    auto badOptions=options(caps,invalid);
    std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(side,side),badOptions));
    if(!target) continue;
    CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());
    action.apply(triangle.root);
    ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
      target->getLastSubmissionSerial()==0,"invalid sample count rejected before publication");
    action.setRenderTarget(nullptr);
  }
  return ok;
}
bool lifecycle() {
  const auto caps=capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if(!bgfx(caps)) return false;
  bool aggregate=true;
  for(uint32_t samples:{2u,4u}) {
  Triangle scene;const auto selected=options(caps,samples);
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(32,32),selected));
  if(!check(bool(target),"lifecycle target")) return false;
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(32,32));
  action.setBackgroundColor(SbColor4f(0,0,0,0));action.setRenderTarget(target.get());
  Frame before,larger,after;
  bool ok=capture(action,*target,scene.root,before,"lifecycle initial");
  ok &= check(target->resize(SbVec2i32(64,64)),"lifecycle 64 resize");
  action.setViewportRegion(SbViewportRegion(64,64));
  ok &= capture(action,*target,scene.root,larger,"lifecycle 64 capture");
  ok &= check(larger.rgba.size()==64u*64u*4u && larger.depth.size()==64u*64u &&
              larger.serial>before.serial,"lifecycle 64 published");
  ok &= check(target->resize(SbVec2i32(0,0)),"lifecycle suspend");
  const auto stableSerial=target->getLastSubmissionSerial();
  std::vector<uint8_t> stableRgba;std::vector<float> stableDepth;
  target->readbackRGBA(stableRgba);target->readbackDepth(stableDepth);
  action.apply(scene.root);
  std::vector<uint8_t> suspendedRgba;std::vector<float> suspendedDepth;
  target->readbackRGBA(suspendedRgba);target->readbackDepth(suspendedDepth);
  ok &= check(action.getLastStatus()==CoinRenderAction::NOT_READY &&
    target->getLastSubmissionSerial()==stableSerial &&
    suspendedRgba==stableRgba && suspendedDepth==stableDepth,
    "suspended target preserves publication");
  ok &= check(target->resize(SbVec2i32(32,32)),"lifecycle restore");
  action.setViewportRegion(SbViewportRegion(32,32));
  ok &= capture(action,*target,scene.root,after,"lifecycle restored");
  ok &= check(after.rgba==before.rgba && after.depth==before.depth &&
    after.serial>stableSerial,"lifecycle exact restored image");
  action.setRenderTarget(nullptr);target.reset();
  target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),selected));
  if(!check(bool(target),"lifecycle recreated target")) return false;
  target->setDepthReadbackEnabled(TRUE);action.setRenderTarget(target.get());
  Frame recreated;
  ok &= capture(action,*target,scene.root,recreated,"lifecycle recreated");
  ok &= check(recreated.rgba==before.rgba && recreated.depth==before.depth,
              "lifecycle recreated exact image");
  action.setRenderTarget(nullptr);
  std::cout << "bgfx_msaa lifecycle samples=" << samples << " passed=" << ok << '\n';
  aggregate &= ok;
  }
  return aggregate;
}
bool rejections() {
  const auto caps=capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if(!bgfx(caps)) return false;
  Triangle scene;const auto selected=options(caps,4);
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(side,side),selected));
  if(!check(bool(target),"rejection target")) return false;
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(side,side));
  action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(0,0,0,0));
  Frame stable;bool ok=capture(action,*target,scene.root,stable,"rejection anchor");
  const Frame opaqueAnchor=stable;
  auto rejected=[&](const char *label) {
    action.apply(scene.root);
    Frame after;target->readbackRGBA(after.rgba);target->readbackDepth(after.depth);
    after.serial=target->getLastSubmissionSerial();
    const bool result=action.getLastStatus()!=CoinRenderAction::SUCCESS &&
      after.rgba==stable.rgba && after.depth==stable.depth && after.serial==stable.serial;
    std::cout << "bgfx_msaa rejected=" << label << " status=" << int(action.getLastStatus())
              << " error=" << action.getLastError().getString() << '\n';
    return check(result,label);
  };
  scene.material->transparency=.5f;
  action.setTransparencyType(CoinRenderAction::BLEND);
  Frame blended;
  ok &= capture(action,*target,scene.root,blended,"BLEND supported at 4x") &&
    check(blended.serial==stable.serial+1 &&
      std::abs(int(blended.rgba[4*(size_t(24)*side+8)])-128)<=3,
      "BLEND publishes material-alpha color with one new serial");
  stable=blended;
  action.setTransparencyType(CoinRenderAction::SORTED_LAYERS_BLEND);
  ok &= rejected("peeling unsupported at 4x");
  action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  Frame sorted;
  ok &= capture(action,*target,scene.root,sorted,"sorted triangle supported at 4x") &&
    check(sorted.serial==stable.serial+1 &&
      std::abs(int(sorted.rgba[4*(size_t(24)*side+8)])-128)<=3,
      "sorted triangle mode publishes translucent geometry");
  stable=sorted;
  action.setTransparencyType(CoinRenderAction::NONE);
  scene.material->transparency=0.f;
  Frame restoredOpaque;
  ok &= capture(action,*target,scene.root,restoredOpaque,
                "opaque anchor restored after translucent modes") &&
    check(restoredOpaque.rgba==opaqueAnchor.rgba &&
      restoredOpaque.depth==opaqueAnchor.depth &&
      restoredOpaque.serial==sorted.serial+1,
      "opaque A/B/A restores exact image/depth with one publication");
  stable=restoredOpaque;
  auto * image=new SoImage;const uint8_t pixel[]={255,0,0,255};
  image->image.setValue(SbVec2s(1,1),4,pixel);scene.root->addChild(image);
  ok &= rejected("SoImage unsupported at 4x");
  scene.root->removeChild(image);
  auto * bump=new SoBumpMap;
  const uint8_t normal[]={128,128,255};
  bump->image.setValue(SbVec2s(1,1),3,normal);
  scene.root->insertChild(bump,scene.root->getNumChildren()-1);
  ok &= rejected("active bump unsupported at 4x");
  scene.root->removeChild(bump);
  // Valid active shadow maps are exercised by CoinRenderMsaaShadowTest.
  // This legacy BASE_COLOR cube fixture has no qualified shadow receiver.
  CoinRenderReadbackTicket ticket{};
  action.applyAsync(scene.root,ticket);
  Frame ticketAfter;target->readbackRGBA(ticketAfter.rgba);
  target->readbackDepth(ticketAfter.depth);
  ticketAfter.serial=target->getLastSubmissionSerial();
  ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
    ticket.token==0 && ticketAfter.rgba==stable.rgba &&
    ticketAfter.depth==stable.depth && ticketAfter.serial==stable.serial,
    "MSAA async ticket rejected without publication");
  Frame recovered;
  ok &= capture(action,*target,scene.root,recovered,"rejection recovery") &&
    check(recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
      recovered.serial>stable.serial,"rejected profiles recover exact anchor");
  action.setRenderTarget(nullptr);
  auto canonical=selected;canonical.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  std::unique_ptr<CoinRenderTarget> other(
    CoinRenderTarget::createOffscreen(SbVec2i32(side,side),canonical));
  if(other) {
    other->setDepthReadbackEnabled(TRUE);CoinRenderAction rejectedAction(SbViewportRegion(side,side));
    rejectedAction.setRenderTarget(other.get());rejectedAction.apply(scene.root);
    ok &= check(rejectedAction.getLastStatus()!=CoinRenderAction::SUCCESS &&
      other->getLastSubmissionSerial()==0,"canonical coverage unsupported at 4x");
    rejectedAction.setRenderTarget(nullptr);
  }
  auto oversized=selected;
  std::unique_ptr<CoinRenderTarget> huge(
    CoinRenderTarget::createOffscreen(SbVec2i32(2049,2049),oversized));
  if(huge) {
    CoinRenderAction oversizedAction(SbViewportRegion(2049,2049));
    oversizedAction.setRenderTarget(huge.get());oversizedAction.apply(scene.root);
    const SbString diagnostic=oversizedAction.getLastError();
    std::cout << "bgfx_msaa oversized status=" << int(oversizedAction.getLastStatus())
              << " error=" << diagnostic.getString()
              << " serial=" << huge->getLastSubmissionSerial() << '\n';
    ok &= check(oversizedAction.getLastStatus()==CoinRenderAction::UNSUPPORTED &&
      diagnostic.find("128")>=0 && huge->getLastSubmissionSerial()==0,
      "2049 squared 4x MSAA exceeds 128 MiB before publication");
    oversizedAction.setRenderTarget(nullptr);
  }
  return ok;
}
bool profiles() {
  const auto caps=capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if(!bgfx(caps)) return false;
  Triangle scene;
  auto * model=static_cast<SoLightModel *>(scene.root->getChild(1));
  model->model=SoLightModel::PHONG;
  auto * light=new SoDirectionalLight;light->direction.setValue(0,0,-1);
  scene.root->insertChild(light,2);
  scene.material->diffuseColor.setValue(1,1,1);
  auto * texture=new SoTexture2;const uint8_t texel[]={255,0,0,255};
  texture->image.setValue(SbVec2s(1,1),4,texel);
  texture->model=SoTexture2::MODULATE;
  scene.root->insertChild(texture,scene.root->getNumChildren()-1);
  auto * quality=new SoComplexity;quality->textureQuality=.1f;
  scene.root->insertChild(quality,scene.root->getNumChildren()-1);
  auto * clip=new SoClipPlane;clip->plane=SbPlane(SbVec3f(-1,0,0),0);
  scene.root->insertChild(clip,scene.root->getNumChildren()-1);
  bool ok=true;
  for(uint32_t samples:{1u,2u,4u}) {
    const auto selected=options(caps,samples);
    std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(side,side),selected));
    if(!check(bool(target),"profile target")) return false;
    target->setDepthReadbackEnabled(TRUE);
    CoinRenderAction action(SbViewportRegion(side,side));
    action.setBackgroundColor(SbColor4f(0,0,0,0));action.setRenderTarget(target.get());
    Frame frame;ok &= capture(action,*target,scene.root,frame,"texture/PHONG/clip/AlphaTest");
    if(frame.rgba.size()==side*side*4) {
      size_t lit=0,partial=0;
      for(size_t i=0;i<frame.depth.size();++i) {
        lit+=(frame.rgba[4*i+0]>0);partial+=(frame.rgba[4*i+3]>0 && frame.rgba[4*i+3]<255);
      }
      std::cout << "bgfx_msaa profile samples=" << samples << " lit=" << lit
                << " partial=" << partial << '\n';
      ok &= check(lit>0 && (samples==1?partial==0:partial>0),
                  "combined textured/PHONG/clip profile visible and resolved");
      light->on=FALSE;
      Frame unlit;
      ok &= capture(action,*target,scene.root,unlit,"profile light-off control") &&
        check(unlit.rgba!=frame.rgba,"PHONG lighting must change RGB");
      light->on=TRUE;
      clip->on=FALSE;
      Frame unclipped;
      ok &= capture(action,*target,scene.root,unclipped,"profile unclipped control") &&
        check(unclipped.rgba!=frame.rgba,"clip plane must change coverage");
      clip->on=TRUE;
      scene.alpha->function=SoAlphaTest::GREATER;
      scene.alpha->value=1.f;
      Frame discarded;
      ok &= capture(action,*target,scene.root,discarded,"profile alpha discard") &&
        check(std::all_of(discarded.rgba.begin(),discarded.rgba.end(),
          [](uint8_t v){return v==0;}),"AlphaTest GREATER 1 discards opaque profile");
      scene.alpha->function=SoAlphaTest::ALWAYS;
      scene.alpha->value=.5f;
      Frame recovered;
      ok &= capture(action,*target,scene.root,recovered,"profile recovered") &&
        check(recovered.rgba==frame.rgba && recovered.depth==frame.depth,
              "profile state restores exact image and depth");
    }
    action.setRenderTarget(nullptr);
  }
  return ok;
}
}
int main(int argc,char ** argv) {
  if(argc!=2) return 2;
  SoDB::init();CoinRenderAction::initClass();
  const std::string mode=argv[1];
  if(mode=="--offscreen") return offscreen()?0:1;
  if(mode=="--lifecycle") return lifecycle()?0:1;
  if(mode=="--rejections") return rejections()?0:1;
  if(mode=="--profiles") return profiles()?0:1;
  return 2;
}
