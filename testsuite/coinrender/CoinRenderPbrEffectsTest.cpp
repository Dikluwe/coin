// Pixel and reuse contracts for the bounded native/offscreen PBR effects profile.
#include "config.h"
#include <Inventor/SoDB.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCube.h>
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinbgfx/CoinBgfxBackend.h"
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
namespace {
void check(bool ok,const char *text){if(!ok)throw std::runtime_error(text);}
struct Pixels {std::vector<uint8_t> color;std::vector<float> depth;};
void run(const char *environment,bool perspective) {
  const int w=192,h=160;
  auto *root=new SoSeparator;root->ref();
  SoCamera *camera=perspective?static_cast<SoCamera *>(new SoPerspectiveCamera):new SoOrthographicCamera;
  camera->position.setValue(7,5,8);camera->pointAt(SbVec3f(0,0,0),SbVec3f(0,1,0));
  camera->nearDistance=.5;camera->farDistance=40;
  if(perspective)static_cast<SoPerspectiveCamera *>(camera)->heightAngle=.65f;
  else static_cast<SoOrthographicCamera *>(camera)->height=9;
  root->addChild(camera);
  auto *model=new SoLightModel;model->model=SoLightModel::PHONG;root->addChild(model);
  auto *light=new SoDirectionalLight;light->direction.setValue(.65f,-1.f,.45f);light->intensity=2;root->addChild(light);
  auto *material=new SoPBRMaterial;material->baseColor.setValue(.5f,.4f,.3f);material->roughness=.65f;root->addChild(material);
  auto *box=new SoSeparator;auto *move=new SoTranslation;move->translation.setValue(0,1,0);box->addChild(move);box->addChild(new SoCube);root->addChild(box);
  auto *floor=new SoSeparator;auto *down=new SoTranslation;down->translation.setValue(0,-.1f,0);floor->addChild(down);
  auto *slab=new SoCube;slab->width=12;slab->height=.2f;slab->depth=12;floor->addChild(slab);root->addChild(floor);
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;options.pbrEnvironmentBundle=environment;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(w,h),options));check(bool(target),"target");
  CoinRenderRetainedRenderer renderer(SbViewportRegion(w,h));
  auto render=[&](){const auto status=renderer.render(root,*target);check(status==CoinRenderAction::SUCCESS,renderer.getLastError().getString());
    Pixels p;target->readbackRGBA(p.color);target->readbackDepth(p.depth);check(p.color.size()==w*h*4&&p.depth.size()==w*h,"readback");return p;};
  CoinRenderPbrEffects effects;effects.background=true;effects.shadowMapSize=512;effects.shadowBias=.0005f;effects.occlusionRadius=1.5f;effects.occlusionStrength=1;
  check(target->setPbrEffects(effects),"effects setter");const auto off=render();
  effects.shadows=true;target->setPbrEffects(effects);const auto shadows=render();
  effects.shadows=false;effects.ambientOcclusion=true;target->setPbrEffects(effects);const auto ao=render();
  effects.shadows=true;target->setPbrEffects(effects);const auto both=render();
  check(off.depth==shadows.depth&&off.depth==ao.depth&&off.depth==both.depth,"effects changed published depth");
  int darkerShadow=0,darkerAo=0,background=0;
  for(size_t i=0;i<off.depth.size();++i){
    int original=0,shadow=0,occluded=0;
    for(int c=0;c<3;++c){original+=off.color[i*4+c];shadow+=shadows.color[i*4+c];occluded+=ao.color[i*4+c];}
    if(off.depth[i]<1){darkerShadow+=original-shadow>9;darkerAo+=original-occluded>3;}
    else {++background;for(int c=0;c<4;++c)check(off.color[i*4+c]==both.color[i*4+c],"effects touched background");}
  }
  std::cout<<"pixels: shadow="<<darkerShadow<<" AO="<<darkerAo<<" background="<<background<<std::endl;
  check(darkerShadow>100,"no projected directional shadow");check(darkerAo>20,"no contact occlusion");check(background>100,"no background coverage");
  auto *backend=static_cast<CoinBgfxBackend *>(target->getPimpl()->backend.get());
  check(backend->getPbrShadowPassCount()==1&&backend->getPbrOcclusionDepthPassCount()==1,"depth cache not reused");
  const auto repeated=render();check(repeated.color==both.color,"static frame changed");
  check(renderer.getCaptureCount()==1&&renderer.getPlanBuildCount()==1,"effect toggles recaptured/replanned");
  const auto serial=target->getLastSubmissionSerial();auto invalid=effects;invalid.occlusionRadius=std::numeric_limits<float>::quiet_NaN();
  check(!target->setPbrEffects(invalid)&&target->getLastSubmissionSerial()==serial,"invalid setter published");
  invalid=effects;invalid.shadowMapSize=513;check(!target->setPbrEffects(invalid),"invalid map accepted");
  effects.shadows=false;effects.ambientOcclusion=false;target->setPbrEffects(effects);check(render().color==off.color,"off restoration mismatch");
  effects.shadows=true;effects.ambientOcclusion=true;target->setPbrEffects(effects);move->translation.setValue(1,1,0);
  const auto moved=render();check(moved.color!=both.color,"moving caster did not change image");
  check(backend->getPbrShadowPassCount()==2&&backend->getPbrOcclusionDepthPassCount()==2,"moving caster kept stale depths");
  check(renderer.getCaptureCount()==1&&renderer.getPlanBuildCount()==1,"translation recaptured/replanned");
  target->setPbrExposureStops(1);const auto exposed=render();
  for(size_t i=0;i<moved.depth.size();++i)if(moved.depth[i]==1)for(int c=0;c<4;++c)
    check(moved.color[i*4+c]==exposed.color[i*4+c],"exposure changed sRGB backdrop");
  const auto failedSerial=target->getLastSubmissionSerial();
  invalid=effects;invalid.resourceBudgetBytes=1;check(target->setPbrEffects(invalid),"budget setter");
  check(renderer.render(root,*target)==CoinRenderAction::UNSUPPORTED&&target->getLastSubmissionSerial()==failedSerial,"budget failure published");
  Pixels preserved;target->readbackRGBA(preserved.color);check(preserved.color==exposed.color,"budget failure changed image");
  target->setPbrEffects(effects);
  light->on=FALSE;check(renderer.render(root,*target)==CoinRenderAction::UNSUPPORTED,"missing first directional light admitted");
  root->unref();std::cout<<(perspective?"perspective":"orthographic")<<": shadows="<<darkerShadow<<" AO="<<darkerAo<<" PASS\n";
}
}
int main(int argc,char **argv){try{check(argc==2,"environment argument");SoDB::init();run(argv[1],true);run(argv[1],false);return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
