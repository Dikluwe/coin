#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCube.h>
#include <cstdio>
#include <memory>
#include <fstream>
#define CHECK(x,m) do{if(!(x)){std::fprintf(stderr,"FAIL: %s (%d)\n",m,__LINE__);return 1;}}while(0)
static bool render(CoinRenderTarget & target,SoNode * root,SbList<SbVec3f> & hdr) {
  CoinRenderAction action(SbViewportRegion(256,256));action.setRenderTarget(&target);action.apply(root);
  if(action.getLastStatus()!=CoinRenderAction::SUCCESS){std::fprintf(stderr,"filter: %s\n",action.getLastError().getString());return false;}
  return target.getPimpl()->backend->readSceneLinear(hdr)&&hdr.getLength()==256*256;
}
static size_t soft(const SbList<SbVec3f> & pixels,const SbList<SbVec3f> & reference,const std::vector<float> & depth) {
  size_t result=0;
  for(int i=0;i<pixels.getLength();++i) {
    if(depth[i]<.44f||depth[i]>.45f||reference[i][0]<.1f)continue;
    const double visibility=(pixels[i][0]-.05)/(reference[i][0]-.05);
    result+=visibility>.03&&visibility<.97;
  }
  return result;
}
static bool save(CoinRenderTarget & target,const std::string & path) {
  std::vector<uint8_t> rgba;target.readbackRGBA(rgba);std::ofstream out(path.c_str(),std::ios::binary);out<<"P6\n256 256\n255\n";
  for(size_t i=0;i<rgba.size();i+=4)out.write(reinterpret_cast<const char*>(rgba.data()+i),3);
  return bool(out);
}
int main(int argc,char ** argv) {
  SoDB::init();CoinRenderAction::initClass();auto * root=new SoSeparator;root->ref();
  auto * camera=new SoOrthographicCamera;camera->height=7;camera->position=SbVec3f(0,0,8);camera->nearDistance=1;camera->farDistance=20;root->addChild(camera);
  auto * group=new SoShadowGroup;group->quality=1;root->addChild(group);
  auto * light=new SoShadowSpotLight;light->ref();light->location=SbVec3f(2,2,4);light->direction=SbVec3f(-2,-2,-5);
  light->cutOffAngle=.5f;light->nearDistance=1;light->farDistance=12;light->intensity=4;group->addChild(light);
  auto * material=new SoPBRMaterial;material->baseColor=SbColor(.7f,.7f,.7f);material->roughness=.5f;material->emissive=SbColor(.05f,.05f,.05f);group->addChild(material);
  auto * cube=new SoCube;cube->width=cube->height=cube->depth=1.4f;group->addChild(cube);
  auto * floor=new SoSeparator;auto * style=new SoShadowStyle;style->style=SoShadowStyle::SHADOWED;floor->addChild(style);
  auto * translation=new SoTranslation;translation->translation=SbVec3f(0,0,-1.5f);floor->addChild(translation);
  auto * slab=new SoCube;slab->width=slab->height=6;slab->depth=.05f;floor->addChild(slab);group->addChild(floor);
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;options.pbrShadowMaxPcssRadiusTexels=256;
  std::unique_ptr<CoinRenderTarget> vsm(CoinRenderTarget::createOffscreen(SbVec2i32(256,256),options));
  options.pbrShadowFilter=COIN_RENDER_PBR_SHADOW_PCF;options.pbrShadowPcfRadiusTexels=0;
  std::unique_ptr<CoinRenderTarget> hard(CoinRenderTarget::createOffscreen(SbVec2i32(256,256),options));
  options.pbrShadowPcfRadiusTexels=1;
  std::unique_ptr<CoinRenderTarget> pcf(CoinRenderTarget::createOffscreen(SbVec2i32(256,256),options));
  options.pbrShadowFilter=COIN_RENDER_PBR_SHADOW_PCSS;
  std::unique_ptr<CoinRenderTarget> pcss(CoinRenderTarget::createOffscreen(SbVec2i32(256,256),options));
  SbList<SbVec3f> reference,a,b,c;style->style=SoShadowStyle::NO_SHADOWING;
  CHECK(hard&&render(*hard,root,reference),"Unshadowed GGX reference");style->style=SoShadowStyle::SHADOWED;
  CHECK(render(*hard,root,a)&&pcss&&render(*pcss,root,b),"Hard PCF and zero-radius PCSS");
  for(int i=0;i<a.getLength();++i)CHECK((a[i]-b[i]).length()==0,"Zero-radius PCSS reduces exactly to compare-first hard shadows");
  std::vector<float> depth;hard->readbackDepth(depth);
  CHECK(soft(a,reference,depth)==0,"Hard depth comparison never invents variance bleeding");
  CHECK(pcf&&render(*pcf,root,c),"PCF 3x3 receiver");const size_t pcfSoft=soft(c,reference,depth);
  CHECK(pcfSoft>10,"PCF averages binary comparisons into a receiver edge");
  if(argc>1)CHECK(save(*pcf,std::string(argv[1])+"/pcf.ppm")&&save(*hard,std::string(argv[1])+"/hard.ppm"),"Save real PCF images");
  CHECK(vsm&&render(*vsm,root,c),"VSM receiver");
  if(argc>1)CHECK(save(*vsm,std::string(argv[1])+"/vsm.ppm"),"Save real VSM image");
  const auto cache=pcss->getPimpl()->backend->shadowCacheStats();
  light->sourceRadius=.125f;CHECK(render(*pcss,root,b),"Physical finite spot emitter");const size_t small=soft(b,reference,depth);
  light->sourceRadius=.25f;CHECK(render(*pcss,root,c),"Larger physical finite spot emitter");const size_t large=soft(c,reference,depth);
  std::printf("soft coverage: PCF=%zu spot .125=%zu .25=%zu\n",pcfSoft,small,large);
  CHECK(large>small&&large>pcfSoft,"Increasing physical emitter radius widens the measured receiver penumbra");
  const auto reused=pcss->getPimpl()->backend->shadowCacheStats();
  CHECK(reused.allocations==cache.allocations&&reused.renders==cache.renders,"Emitter filtering changes reuse the same center-depth maps");
  if(argc>1)CHECK(save(*pcss,std::string(argv[1])+"/pcss-spot.ppm"),"Save actual PCSS emitter result");
  std::vector<uint8_t> published,refused;pcss->readbackRGBA(published);const auto serial=pcss->getLastSubmissionSerial();
  light->sourceRadius=1;
  CoinRenderAction denied(SbViewportRegion(256,256));denied.setRenderTarget(pcss.get());denied.apply(root);pcss->readbackRGBA(refused);
  CHECK(denied.getLastStatus()==CoinRenderAction::UNSUPPORTED&&refused==published&&pcss->getLastSubmissionSerial()==serial,
    "Oversized physical emitter is refused without clipping its radius or publishing pixels");
  light->sourceRadius=.25f;CHECK(render(*pcss,root,b),"Emitter cap recovery");
  for(int i=0;i<c.getLength();++i)CHECK((b[i]-c[i]).length()==0,"Emitter refusal recovery is exact");
  CoinRenderAction captured(SbViewportRegion(256,256));captured.apply(root);const auto & frame=captured.getPimpl()->lastRejectedShadowFrame;
  CoinRenderShadowPlan plan;std::string diagnostic;CHECK(coin_render_plan_shadows(frame,plan,diagnostic),"Filter admission fixture");
  uint64_t samples=0;CHECK(coin_render_shadow_filter_admission(frame,plan,options,SbVec2i32(256,256),samples,diagnostic)&&samples>0,"Physical pixel/tap bound");
  auto highResolution=frame;for(auto & view:highResolution.viewports){view.x*=2;view.y*=2;view.width*=2;view.height*=2;}
  uint64_t doubled=0;CHECK(coin_render_shadow_filter_admission(highResolution,plan,options,SbVec2i32(512,512),doubled,diagnostic)&&doubled>samples*3,
    "Sample cost follows view pixel resolution rather than world-unit object size");
  options.pbrShadowSampleBudget=samples;
  std::unique_ptr<CoinRenderTarget> budgeted(CoinRenderTarget::createOffscreen(SbVec2i32(256,256),options));
  CHECK(budgeted&&render(*budgeted,root,a),"Exact sample budget boundary admitted");budgeted->readbackRGBA(published);const auto budgetSerial=budgeted->getLastSubmissionSerial();
  camera->height=1;
  CoinRenderAction zoom(SbViewportRegion(256,256));zoom.setRenderTarget(budgeted.get());zoom.apply(root);budgeted->readbackRGBA(refused);
  CHECK(zoom.getLastStatus()==CoinRenderAction::UNSUPPORTED&&refused==published&&budgeted->getLastSubmissionSerial()==budgetSerial,
    "Close-up increased triangle coverage refuses before publication without reducing resolution");
  CHECK(light->sourceRadius.getValue()==.25f,"View budget never changes physical emitter size");
  camera->height=7;CHECK(render(*budgeted,root,b),"Sample budget recovery");
  group->removeChild(light);
  auto * sun=new SoShadowDirectionalLight;sun->direction=SbVec3f(-2,-2,-5);sun->intensity=4;group->insertChild(sun,0);
  style->style=SoShadowStyle::NO_SHADOWING;CHECK(render(*pcss,root,reference),"Directional unshadowed reference");style->style=SoShadowStyle::SHADOWED;
  CHECK(render(*pcss,root,a),"Directional point emitter");sun->angularRadius=.02f;
  CHECK(render(*pcss,root,b),"Directional finite angular emitter");const size_t sunSoft=soft(b,reference,depth);
  CHECK(sunSoft>10&&soft(a,reference,depth)==0,"Angular radius produces a directional receiver penumbra");
  if(argc>1)CHECK(save(*pcss,std::string(argv[1])+"/pcss-directional.ppm"),"Save directional PCSS image");
  light->unref();root->unref();std::printf("Filters: compare-first PCF, PCSS punctual limit, physical/angle emitter penumbra, cache reuse and pixel/tap/radius budgets PASS; bound=%llu doubled=%llu\n",(unsigned long long)samples,(unsigned long long)doubled);return 0;
}
