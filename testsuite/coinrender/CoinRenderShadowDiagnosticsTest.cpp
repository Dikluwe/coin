#include "CoinRenderTestEnvironment.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCube.h>
#include <memory>
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstdlib>

#define REQUIRE(c,m) do { if (!(c)) {std::cerr << m << '\n'; return 1;} } while(0)
int main(int argc,char ** argv) {
  SoDB::init();CoinRenderAction::initClass();
  auto * root=new SoSeparator;root->ref();
  auto * camera=new SoOrthographicCamera;camera->position.setValue(0,0,8);
  camera->height=7;camera->nearDistance=1;camera->farDistance=20;root->addChild(camera);
  auto * group=new SoShadowGroup;group->quality=1;root->addChild(group);
  SoShadowSpotLight * lights[8];
  for(int i=0;i<8;++i) {
    lights[i]=new SoShadowSpotLight;lights[i]->location.setValue(2,2,4);
    // Keep a meaningful normalized-depth range in the 8-bit diagnostic.
    // Coin's wide-cone VSM far normalization otherwise compresses the
    // moments into less than one display level, despite correct float maps.
    lights[i]->direction.setValue(-2,-2,-5);lights[i]->cutOffAngle=.5f;
    lights[i]->nearDistance=1;lights[i]->farDistance=12;
    lights[i]->on=i==0;group->addChild(lights[i]);
  }
  auto * material=new SoMaterial;material->diffuseColor.setValue(.7f,.3f,.1f);group->addChild(material);
  auto * cube=new SoCube;cube->width=cube->height=cube->depth=1.4f;group->addChild(cube);
  auto * floor=new SoSeparator;group->addChild(floor);
  auto * style=new SoShadowStyle;style->style=SoShadowStyle::SHADOWED;floor->addChild(style);
  auto * white=new SoMaterial;white->diffuseColor.setValue(.7f,.7f,.7f);floor->addChild(white);
  auto * translation=new SoTranslation;translation->translation.setValue(0,0,-1.5f);floor->addChild(translation);
  auto * slab=new SoCube;slab->width=slab->height=6;slab->depth=.05f;floor->addChild(slab);
  const auto render=[&](CoinRenderTarget & target,std::vector<uint8_t>& rgba) {
    CoinRenderAction action(SbViewportRegion(128,128));action.setRenderTarget(&target);action.apply(root);
    if(action.getLastStatus()!=CoinRenderAction::SUCCESS){std::cerr<<action.getLastError().getString()<<'\n';return false;}
    target.readbackRGBA(rgba);return rgba.size()==128*128*4;
  };
  std::vector<uint8_t> images[3];
  for(int mode=1;mode<=3;++mode) {
    CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;
    options.shadowDiagnostic=static_cast<CoinRenderShadowDiagnostic>(mode);
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(128,128),options));
    REQUIRE(target && render(*target,images[mode-1]),"Diagnostic requires actual successful GPU output");
    if(mode==3) {
      style->style=SoShadowStyle::NO_SHADOWING;
      std::vector<uint8_t> clear,recovered;
      REQUIRE(render(*target,clear),"Disabled receiver diagnostic renders");
      size_t changed=0;
      for(size_t p=0;p<clear.size();p+=4)changed+=int(clear[p])-int(images[2][p])>50;
      REQUIRE(changed>20,"Visibility darkens actual receivers rather than only background pixels");
      style->style=SoShadowStyle::SHADOWED;
      REQUIRE(render(*target,recovered)&&recovered==images[2],"Diagnostic receiver restoration is exact");
    }
    if(argc>1) {
      const std::string output=std::string(argv[1])+"/shadow-diagnostic-"+std::to_string(mode)+".ppm";
      std::ofstream out(output.c_str(),std::ios::binary);
      out<<"P6\n128 128\n255\n";
      for(size_t p=0;p<images[mode-1].size();p+=4)out.write(reinterpret_cast<const char*>(images[mode-1].data()+p),3);
      REQUIRE(bool(out),"Real diagnostic image is saved");
    }
  }
  size_t measured=0,dark=0,lit=0;
  for(size_t p=0;p<images[0].size();p+=4) {
    REQUIRE(images[0][p]==images[0][p+1]&&images[0][p]==images[0][p+2],"Linear depth is grayscale");
    REQUIRE(images[1][p+2]==0 && images[1][p+3]==255,"Moment channels are RG with opaque alpha");
    REQUIRE(std::abs(int(images[1][p])-int(images[0][p]))<=1,"Both map diagnostics sample identical first moments");
    if(images[0][p]>5&&images[0][p]<250) {
      const double expected=double(images[0][p])*images[0][p]/255;
      REQUIRE(std::abs(images[1][p+1]-expected)<=3,"Second moment is the independently expected squared depth");
      ++measured;
    }
    REQUIRE(images[2][p]==images[2][p+1]&&images[2][p]==images[2][p+2],"Visibility diagnostic contains no Phong color");
    dark+=images[2][p]<80;lit+=images[2][p]>240;
  }
  REQUIRE(measured>20&&dark>20&&lit>100,"Actual geometry and shadow visibility are present");
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;
  options.shadowAttachmentBudgetBytes=128u*1024u*1024u;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(128,128),options));
  std::vector<uint8_t> before,after;
  REQUIRE(target&&render(*target,before),"One map fits the explicitly lower budget");
  const uint64_t serial=target->getLastSubmissionSerial();
  {
    // Exercise the actual allocator error boundary, not only plan rejection.
    group->precision=.25f; // Force attachment replacement rather than a cache hit.
#ifdef _WIN32
    _putenv_s("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE","1");
#else
    setenv("COIN_BGFX_TEST_SHADOW_MAP_ALLOC_ONCE","1",1);
#endif
    CoinRenderAction failed(SbViewportRegion(128,128));failed.setRenderTarget(target.get());failed.apply(root);
    target->readbackRGBA(after);
    REQUIRE(failed.getLastStatus()!=CoinRenderAction::SUCCESS && after==before &&
      target->getLastSubmissionSerial()==serial,"Attachment allocation failure preserves published pixels and serial");
    group->precision=.5f;
    REQUIRE(target->resize(SbVec2i32(128,128)),"Explicit target reconfiguration recovers allocation failure");
    REQUIRE(render(*target,after)&&after==before,"Allocation failure recovers without stale output");
  }
  const uint64_t recoveredSerial=target->getLastSubmissionSerial();
  for(auto * light:lights)light->on=TRUE;
  CoinRenderAction action(SbViewportRegion(128,128));action.setRenderTarget(target.get());action.apply(root);
  target->readbackRGBA(after);
  REQUIRE(action.getLastStatus()==CoinRenderAction::UNSUPPORTED && after==before && target->getLastSubmissionSerial()==recoveredSerial,
    "Full attachment budget refuses eight maps without publishing pixels or serial");
  for(int i=1;i<8;++i)lights[i]->on=FALSE;
  REQUIRE(render(*target,after)&&after==before,"Recovery preserves the qualified one-map result");
  std::cout<<"GPU map diagnostics verified: depth samples="<<measured<<" shadow pixels="<<dark<<" lit pixels="<<lit
    <<"; attachment budget rollback/recovery passed\n";
  root->unref();return 0;
}
