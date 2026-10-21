#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderMsaaShadowFixture.h"
#include <Inventor/SbPlane.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
using namespace msaa_shadow_test;
namespace {
struct PeelingHarness {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(side,side)};
  unsigned maps=0;
  PeelingHarness(const CoinRenderCapabilities &caps,unsigned samples,unsigned mapCount)
    :maps(mapCount) {
    CoinRenderOptions o=options(caps,samples);
    // The typed target selects peeling without setting the global
    // SORTED_LAYERS_BLEND transparency override. Local ADD can then retain
    // its native meaning within the same captured scene.
    o.transparency=COIN_RENDER_TRANSPARENCY_PEELING;
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),o));
    if(target){target->setDepthReadbackEnabled(TRUE);action.setRenderTarget(target.get());}
    action.setBackgroundColor(SbColor4f(0,0,0,0));
    action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
    action.setSortedLayersNumPasses(2);
  }
  ~PeelingHarness(){action.setRenderTarget(nullptr);}
  bool render(SoNode *scene,Frame &frame,const char *label){
    if(!check(bool(target),"peeling state target exists"))return false;
    action.apply(scene);
    target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
    frame.serial=target->getLastSubmissionSerial();
    std::cout<<"msaa_peeling_state maps="<<maps<<" phase="<<label
      <<" status="<<int(action.getLastStatus())
      <<" rgba="<<frame.rgba.size()<<" depth="<<frame.depth.size()
      <<" serial="<<frame.serial
      <<" error="<<action.getLastError().getString()<<'\n';
    return check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
      frame.rgba.size()==size_t(side)*side*4 &&
      frame.depth.size()==size_t(side)*side && frame.serial>0,label);
  }
};
bool run(const CoinRenderCapabilities &caps) {
  bool all=true;
  for(unsigned samples:{1u,2u,4u}) for(unsigned maps:{1u,2u,4u,8u}) {
    bool ok=true;
    Scene scene(maps,false);scene.receiverMaterial->transparency=.5f;
    auto *environment=new SoEnvironment;
    environment->fogType=SoEnvironment::NONE;
    environment->fogColor.setValue(.12f,.61f,.84f);
    environment->fogVisibility=14.f;
    scene.root->insertChild(environment,2);
    auto *clip=new SoClipPlane;
    clip->plane.setValue(SbPlane(SbVec3f(1,0,0),0));
    clip->on=FALSE;scene.root->insertChild(clip,3);
    auto *alpha=new SoAlphaTest;
    alpha->function=SoAlphaTest::ALWAYS;
    scene.receiver->insertChild(alpha,1);
    PeelingHarness harness(caps,samples,maps);
    if(!check(harness.target && harness.target->getOptions().renderer==caps.renderer &&
       harness.target->getOptions().sampleCount==samples,
       "peeling state actual GPU/API and requested sample count"))return false;
    auto capture=[&](Frame &f,const char *label) {
      return harness.render(scene.root,f,label);
    };
    Frame base,clipped,pass,reject,restored;
    std::array<Frame,3> fogged;
    ok &= capture(base,"shadowed transparent peeling receiver");
    const SoEnvironment::FogType modes[]={SoEnvironment::HAZE,
      SoEnvironment::FOG,SoEnvironment::SMOKE};
    for(int i=0;i<3;++i) {
      environment->fogType=modes[i];
      ok &= capture(fogged[size_t(i)],"geometric fog on peeling receiver");
    }
    environment->fogType=SoEnvironment::NONE;
    clip->on=TRUE;ok &= capture(clipped,"world clip on peeling receiver");
    alpha->function=SoAlphaTest::GREATER;alpha->value=0;
    ok &= capture(pass,"alpha test keeps half-alpha peeling receiver");
    alpha->function=SoAlphaTest::NEVER;
    ok &= capture(reject,"alpha test removes peeling receiver");
    alpha->function=SoAlphaTest::ALWAYS;clip->on=FALSE;
    ok &= capture(restored,"peeling state exact restoration");
    if(!ok){all=false;continue;}
    const size_t center=at(64,64),left=at(32,64),right=at(96,64);
    const float ratio=7.f/14.f;
    const float transmittance[]={1.f-ratio,std::exp(-5.545f*ratio),
      std::exp(-std::pow(2.35f*ratio,2.f))};
    const int fogColor[]={31,156,214};
    for(int mode=0;mode<3;++mode)for(int c=0;c<3;++c) {
      // The fogged fragment is alpha .5 and is blended over transparent
      // black. Thus the fog contribution to final RGB is multiplied by .5.
      const int wanted=int(std::lround(base.rgba[4*center+c]*transmittance[mode]+
        .5f*fogColor[c]*(1.f-transmittance[mode])));
      const int actual=fogged[size_t(mode)].rgba[4*center+c];
      std::cout<<"msaa_peeling_state samples="<<samples<<" maps="<<maps<<" fog="<<mode
        <<" channel="<<c<<" actual="<<actual<<" expected="<<wanted<<'\n';
      ok &= check(std::abs(actual-wanted)<=3,
        "HAZE/FOG/SMOKE use geometric eye distance before alpha composition");
    }
    const bool samePassRgba=clipped.rgba==pass.rgba;
    const bool samePassDepth=clipped.depth==pass.depth;
    bool passRgbWithinExistingTolerance=true;
    bool samePassCoverage=true;
    bool passAlphaWithinExistingTolerance=true;
    for(size_t p=0;p<clipped.depth.size();++p) {
      samePassCoverage &= (clipped.rgba[4*p+3]==0)==(pass.rgba[4*p+3]==0);
      passAlphaWithinExistingTolerance &=
        std::abs(int(clipped.rgba[4*p+3])-int(pass.rgba[4*p+3]))<=3;
      for(int c=0;c<3;++c)
        passRgbWithinExistingTolerance &=
          std::abs(int(clipped.rgba[4*p+c])-int(pass.rgba[4*p+c]))<=3;
    }
    std::cout<<"msaa_peeling_state clip_alpha samples="<<samples<<" maps="<<maps
      <<" base_left_alpha="<<int(base.rgba[4*left+3])
      <<" clipped_left_alpha="<<int(clipped.rgba[4*left+3])
      <<" clipped_right_alpha="<<int(clipped.rgba[4*right+3])
      <<" pass_left_alpha="<<int(pass.rgba[4*left+3])
      <<" pass_right_alpha="<<int(pass.rgba[4*right+3])
      <<" clipped_center_alpha="<<int(clipped.rgba[4*center+3])
      <<" pass_center_alpha="<<int(pass.rgba[4*center+3])
      <<" pass_equal_rgba="<<samePassRgba
      <<" pass_equal_coverage="<<samePassCoverage
      <<" pass_alpha_within_3="<<passAlphaWithinExistingTolerance
      <<" pass_rgb_within_3="<<passRgbWithinExistingTolerance
      <<" pass_equal_depth="<<samePassDepth
      <<" reject_center_alpha="<<int(reject.rgba[4*center+3])
      <<" reject_center_depth="<<reject.depth[center]<<'\n';
    if(!samePassRgba || !samePassDepth) {
      for(size_t p=0;p<clipped.depth.size();++p) {
        if(clipped.depth[p]==pass.depth[p] &&
           clipped.rgba[4*p]==pass.rgba[4*p] &&
           clipped.rgba[4*p+1]==pass.rgba[4*p+1] &&
           clipped.rgba[4*p+2]==pass.rgba[4*p+2] &&
           clipped.rgba[4*p+3]==pass.rgba[4*p+3])continue;
        std::cout<<"msaa_peeling_state clip_alpha_first_diff samples="<<samples
          <<" maps="<<maps
          <<" xy="<<p%side<<','<<p/side
          <<" clip_rgba="<<int(clipped.rgba[4*p])<<','
          <<int(clipped.rgba[4*p+1])<<','<<int(clipped.rgba[4*p+2])<<','
          <<int(clipped.rgba[4*p+3])
          <<" pass_rgba="<<int(pass.rgba[4*p])<<','
          <<int(pass.rgba[4*p+1])<<','<<int(pass.rgba[4*p+2])<<','
          <<int(pass.rgba[4*p+3])
          <<" clip_depth="<<clipped.depth[p]
          <<" pass_depth="<<pass.depth[p]<<'\n';
        break;
      }
    }
    ok &= check(base.rgba[4*left+3]>0 && clipped.rgba[4*left+3]==0 &&
      clipped.rgba[4*right+3]>0 && samePassCoverage &&
      passRgbWithinExistingTolerance && passAlphaWithinExistingTolerance &&
      samePassDepth &&
      reject.rgba[4*center+3]==0 && reject.depth[center]==1.f,
      "clip and AlphaTest preserve only eligible peeling fragments");
    ok &= check(restored.rgba==base.rgba && restored.depth==base.depth &&
      base.serial+1==fogged[0].serial && fogged[0].serial+1==fogged[1].serial &&
      fogged[1].serial+1==fogged[2].serial && fogged[2].serial+1==clipped.serial &&
      clipped.serial+1==pass.serial && pass.serial+1==reject.serial &&
      reject.serial+1==restored.serial,
      "state A/B/A restores exact RGBA/depth and one publication per frame");
    auto *quality=new SoComplexity;quality->textureQuality=.1f;
    auto *texture=new SoTexture2;
    const uint8_t texels[]={255,0,0,64,0,0,255,192,
      255,0,0,64,0,0,255,192};
    texture->image.setValue(SbVec2s(2,2),4,texels);
    texture->wrapS=SoTexture2::CLAMP;texture->wrapT=SoTexture2::CLAMP;
    auto *uv=new SoTextureCoordinate2;
    const SbVec2f coords[]={SbVec2f(0,0),SbVec2f(1,0),
      SbVec2f(1,1),SbVec2f(0,1)};
    uv->point.setValues(0,4,coords);
    scene.receiver->insertChild(quality,3);
    scene.receiver->insertChild(texture,4);
    scene.receiver->insertChild(uv,5);
    Frame modulate,replace,plainAgain;
    texture->model=SoTexture2::MODULATE;
    ok &= capture(modulate,"asymmetric RGBA 2D MODULATE under peeling");
    texture->model=SoTexture2::REPLACE;
    ok &= capture(replace,"asymmetric RGBA 2D REPLACE under peeling");
    scene.receiver->removeChild(uv);
    scene.receiver->removeChild(texture);
    scene.receiver->removeChild(quality);
    ok &= capture(plainAgain,"peeling texture removal restores A");
    if(ok){
      const size_t lhs=at(32,64),rhs=at(96,64);
      const auto alphaNear=[&](const Frame &f,size_t p,int wanted){
        return std::abs(int(f.rgba[4*p+3])-wanted)<=3;
      };
      const auto rgbNear=[&](const Frame &f,size_t p,int r,int g,int b){
        return std::abs(int(f.rgba[4*p])-r)<=3 &&
          std::abs(int(f.rgba[4*p+1])-g)<=3 &&
          std::abs(int(f.rgba[4*p+2])-b)<=3;
      };
      // MODULATE retains the independently captured illuminated base color,
      // multiplied by texel RGB and its alpha. REPLACE ignores PHONG/VSM
      // color and supplies the texel's own RGB and alpha before composition.
      const int modulateLeftRed=int(std::lround(base.rgba[4*lhs]*64.f/255.f));
      const int modulateRightBlue=int(std::lround(base.rgba[4*rhs+2]*192.f/255.f));
      std::cout<<"msaa_peeling_state samples="<<samples<<" maps="<<maps
        <<" MODULATE_alpha="
        <<int(modulate.rgba[4*lhs+3])<<','<<int(modulate.rgba[4*rhs+3])
        <<" REPLACE_alpha="<<int(replace.rgba[4*lhs+3])<<','
        <<int(replace.rgba[4*rhs+3])
        <<" MODULATE_rgb_left="<<int(modulate.rgba[4*lhs])<<','
        <<int(modulate.rgba[4*lhs+1])<<','<<int(modulate.rgba[4*lhs+2])
        <<" MODULATE_rgb_right="<<int(modulate.rgba[4*rhs])<<','
        <<int(modulate.rgba[4*rhs+1])<<','<<int(modulate.rgba[4*rhs+2])
        <<" REPLACE_rgb_left="<<int(replace.rgba[4*lhs])<<','
        <<int(replace.rgba[4*lhs+1])<<','<<int(replace.rgba[4*lhs+2])
        <<" REPLACE_rgb_right="<<int(replace.rgba[4*rhs])<<','
        <<int(replace.rgba[4*rhs+1])<<','<<int(replace.rgba[4*rhs+2])
        <<" expected_modulate="<<modulateLeftRed<<','<<modulateRightBlue<<'\n';
      ok &= check(alphaNear(modulate,lhs,32)&&alphaNear(modulate,rhs,96)&&
        alphaNear(replace,lhs,64)&&alphaNear(replace,rhs,192)&&
        rgbNear(modulate,lhs,modulateLeftRed,0,0)&&
        rgbNear(modulate,rhs,0,0,modulateRightBlue)&&
        rgbNear(replace,lhs,64,0,0)&&rgbNear(replace,rhs,0,0,192)&&
        modulate.rgba!=replace.rgba && plainAgain.rgba==base.rgba &&
        plainAgain.depth==base.depth &&
        restored.serial+1==modulate.serial && modulate.serial+1==replace.serial &&
        replace.serial+1==plainAgain.serial,
        "2D RGBA texture models preserve asymmetric texel alpha and exact recovery");
    }
    auto *addition=new SoSeparator;addition->ref();
    auto *mode=new SoTransparencyType;mode->value=SoTransparencyType::ADD;
    addition->addChild(mode);
    auto *noDepth=new SoDepthBuffer;noDepth->test=FALSE;noDepth->write=FALSE;
    addition->addChild(noDepth);
    auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;
    addition->addChild(unlit);
    auto *red=new SoMaterial;red->diffuseColor.setValue(1,0,0);
    red->transparency=.75f;addition->addChild(red);
    auto *addCoords=new SoCoordinate3;
    const SbVec3f q[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
      SbVec3f(1,1,0),SbVec3f(-1,1,0)};
    addCoords->point.setValues(0,4,q);addition->addChild(addCoords);
    auto *face=new SoFaceSet;face->numVertices.set1Value(0,4);
    addition->addChild(face);scene.root->addChild(addition);
    Frame immediate,delayed,postAdd;
    ok &= capture(immediate,"immediate ADD before peeled receiver");
    mode->value=SoTransparencyType::DELAYED_ADD;
    ok &= capture(delayed,"DELAYED_ADD after peeled receiver");
    scene.root->removeChild(addition);
    ok &= capture(postAdd,"ADD removal restores peeling");
    if(ok){
      const size_t p=at(64,64);
      const int baseRed=int(plainAgain.rgba[4*p]);
      const int immediateIncrement=int(immediate.rgba[4*p])-baseRed;
      const int delayedIncrement=int(delayed.rgba[4*p])-baseRed;
      std::cout<<"msaa_peeling_state samples="<<samples<<" maps="<<maps
        <<" add_rgb_increment="
        <<immediateIncrement<<','<<delayedIncrement<<'\n';
      ok &= check(std::abs(immediateIncrement-32)<=3 &&
        std::abs(delayedIncrement-64)<=3 &&
        immediate.depth==plainAgain.depth && delayed.depth==plainAgain.depth &&
        postAdd.rgba==plainAgain.rgba && postAdd.depth==plainAgain.depth &&
        plainAgain.serial+1==immediate.serial && immediate.serial+1==delayed.serial &&
        delayed.serial+1==postAdd.serial,
        "immediate ADD precedes peeling, DELAYED_ADD follows it, exact A/B/A");
    }
    addition->unref();
    all &= ok;
  }
  return all;
}
}
int main(int argc,char **argv) {
  if(argc!=2 || std::string(argv[1])!="--state")return 2;
  SoDB::init();CoinRenderAction::initClass();
  const auto caps=capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if(!check(caps.gpu_available && caps.renderer!=COIN_RENDER_RENDERER_UNKNOWN &&
    (caps.backend==COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ||
     caps.backend==COIN_RENDER_EXPERIMENTAL_RUST),
    "identified GPU backend required"))return 1;
  return run(caps)?0:1;
}
