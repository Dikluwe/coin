#include "rendering/coinrender/CoinRenderTargetP.h"
#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderPbrEnvironment.h"
#include <Inventor/SoDB.h>
#include <Inventor/C/tidbits.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/annex/FXViz/nodes/SoShadowCulling.h>
#include <memory>
#include <cstdio>
#include <cmath>
#include <fstream>
#ifdef COIN_SHADOW_TEST_X11
#include <X11/Xlib.h>
#undef Status
#endif
#define CHECK(x,m) do{if(!(x)){std::fprintf(stderr,"FAIL: %s (%d)\n",m,__LINE__);return 1;}}while(0)
static bool render(CoinRenderTarget & t, SoNode * root, SbList<SbVec3f> & hdr) {
  if(t.getPimpl()->kind==CoinRenderTargetP::KIND_WINDOW&&!t.requestWindowReadbackRGBA()) {
    std::fprintf(stderr,"window: %s\n",t.getLastError());return false;
  }
  CoinRenderAction action(SbViewportRegion(128,128));action.setRenderTarget(&t);action.apply(root);
  if(action.getLastStatus()!=CoinRenderAction::SUCCESS) {
    std::fprintf(stderr,"render: %s\n",action.getLastError().getString());return false;
  }
  return t.getPimpl()->backend->readSceneLinear(hdr)&&hdr.getLength()==128*128;
}
int main(int argc,char ** argv) {
  SoDB::init();CoinRenderAction::initClass();SoPBRMaterial::initClass();
  auto * root=new SoSeparator;root->ref();
  auto * camera=new SoOrthographicCamera;camera->position.setValue(0,0,8);
  camera->height=7;camera->nearDistance=1;camera->farDistance=20;root->addChild(camera);
  auto * group=new SoShadowGroup;group->quality=1;root->addChild(group);
  SoShadowSpotLight * lights[2];
  for(int i=0;i<2;++i) {
    lights[i]=new SoShadowSpotLight;lights[i]->location.setValue(i?-2:2,2,4);
    lights[i]->direction.setValue(i?2:-2,-2,-5);lights[i]->cutOffAngle=.5f;
    lights[i]->nearDistance=1;lights[i]->farDistance=12;lights[i]->intensity=20;
    lights[i]->color= i?SbColor(.1f,.3f,1):SbColor(1,.3f,.1f);group->addChild(lights[i]);
  }
  auto * material=new SoPBRMaterial;material->baseColor=SbColor(.7f,.4f,.2f);
  material->roughness=.4f;material->emissive=SbColor(.2f,.1f,.05f);group->addChild(material);
  auto * cube=new SoCube;cube->width=cube->height=cube->depth=1.4f;group->addChild(cube);
  auto * floor=new SoSeparator;group->addChild(floor);
  auto * style=new SoShadowStyle;style->style=SoShadowStyle::SHADOWED;floor->addChild(style);
  auto * translation=new SoTranslation;translation->translation.setValue(0,0,-1.5f);floor->addChild(translation);
  auto * slab=new SoCube;slab->width=slab->height=6;slab->depth=.05f;floor->addChild(slab);
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;
  if(argc>3&&std::string(argv[3])=="--pcf")options.pbrShadowFilter=COIN_RENDER_PBR_SHADOW_PCF;
  if(argc>3&&std::string(argv[3])=="--pcss") {
    options.pbrShadowFilter=COIN_RENDER_PBR_SHADOW_PCSS;
    for(auto * light:lights)light->sourceRadius=.03f;
  }
  CoinRenderTarget * initial=nullptr;
#ifdef COIN_SHADOW_TEST_X11
  const bool windowMode=argc>2&&std::string(argv[2])=="--window";
  Display * display=nullptr;Window window=0;
  if(windowMode) {
    display=XOpenDisplay(nullptr);CHECK(display,"Native CAD view display");
    window=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,128,128,0,0,0);
    XMapWindow(display,window);XSync(display,False);
    CoinRenderNativeSurfaceDescriptor desc{};desc.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;desc.structSize=sizeof(desc);desc.type=COIN_RENDER_SURFACE_XLIB;
    desc.native.xlib.display=display;desc.native.xlib.window=window;
    initial=CoinRenderTarget::createWindow(desc,SbVec2i32(128,128),options);
  }
  else
#endif
    initial=CoinRenderTarget::createOffscreen(SbVec2i32(128,128),options);
  std::unique_ptr<CoinRenderTarget> target(initial);
  SbList<SbVec3f> both,first,second,zero,unshadowed;
  CHECK(target&&render(*target,root,both),"Native PBR shadow receiver renders");
  auto cache=target->getPimpl()->backend->shadowCacheStats();
  CHECK(cache.allocations==2&&cache.renders==2&&cache.residentBytes==40u*1024u*1024u,"Initial persistent map attachments are accounted");
  CHECK(render(*target,root,first),"Persistent shadow replay");
  auto replayCache=target->getPimpl()->backend->shadowCacheStats();
  CHECK(replayCache.allocations==cache.allocations&&replayCache.renders==cache.renders&&replayCache.hits==2,
    "Static replay neither reallocates attachments nor resubmits moment maps");
  cube->width=1.8f;CHECK(render(*target,root,first),"Moved CAD caster contents");
  auto movedCache=target->getPimpl()->backend->shadowCacheStats();
  CHECK(movedCache.allocations==cache.allocations&&movedCache.renders==cache.renders+2,"Caster edit dirties content while retaining attachments");
  cube->width=1.4f;CHECK(render(*target,root,first),"Restored caster");
  for(int i=0;i<both.getLength();++i)CHECK((first[i]-both[i]).length()==0,"Cache never publishes stale restored geometry");
  const auto oldDomain=target->getPimpl()->backend->resourceDomain();
  const uint64_t oldSerial=target->getLastSubmissionSerial();std::vector<uint8_t> published,failedPixels;
  target->readbackRGBA(published);
  coin_setenv("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE","1",1);
  CoinRenderAction failed(SbViewportRegion(128,128));failed.setRenderTarget(target.get());failed.apply(root);
  coin_unsetenv("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE");target->readbackRGBA(failedPixels);
  CHECK(failed.getLastStatus()==CoinRenderAction::DEVICE_LOST&&failedPixels==published&&
    target->getLastSubmissionSerial()==oldSerial&&!target->getPimpl()->backend,"Device loss retires cache and preserves publication");
  CHECK(target->resize(SbVec2i32(128,128))&&render(*target,root,first),"Shadow target reconstruction after device loss");
  const auto newDomain=target->getPimpl()->backend->resourceDomain();
  replayCache=target->getPimpl()->backend->shadowCacheStats();
  CHECK(newDomain.generation>oldDomain.generation&&replayCache.allocations==2&&replayCache.renders==2&&replayCache.hits==0,
    "A reconstructed device allocates and renders its own maps");
  for(int i=0;i<both.getLength();++i)CHECK((first[i]-both[i]).length()==0,"Reconstructed device reproduces radiance exactly");
  cache=target->getPimpl()->backend->shadowCacheStats();
  lights[1]->intensity=0;CHECK(render(*target,root,first),"First shadow light alone");
  replayCache=target->getPimpl()->backend->shadowCacheStats();
  CHECK(replayCache.renders==cache.renders,"Light intensity changes receiver radiance without redrawing unchanged maps");
  lights[0]->intensity=0;lights[1]->intensity=20;CHECK(render(*target,root,second),"Second shadow light alone");
  lights[1]->intensity=0;CHECK(render(*target,root,zero),"Emission alone with active shadow maps");
  size_t samples=0;float peak=0;
  for(int i=0;i<both.getLength();++i) {
    if(zero[i][0]==0)continue;++samples;
    CHECK(std::abs(zero[i][0]-.2f)<.001f,"Shadow maps do not occlude emission");
    for(int c=0;c<3;++c) {
      const float expected=first[i][c]+second[i][c]-zero[i][c];
      CHECK(std::abs(both[i][c]-expected)<.012f*std::max(1.f,expected),"Visibility belongs to each light, HDR contributions add before tone mapping");
      peak=std::max(peak,both[i][c]);
    }
  }
  CHECK(samples>8000&&peak>1,"Geometry and direct illumination retain HDR radiance");
  lights[0]->intensity=lights[1]->intensity=20;style->style=SoShadowStyle::NO_SHADOWING;
  CHECK(render(*target,root,unshadowed),"Receiver disable renders");size_t shadowPixels=0;
  for(int i=0;i<both.getLength();++i)shadowPixels+=unshadowed[i][0]-both[i][0]>.2f;
  CHECK(shadowPixels>30,"Map visibility reduces actual GGX receiver radiance");
  style->style=SoShadowStyle::SHADOWED;SbList<SbVec3f> restored;
  CHECK(render(*target,root,restored),"Restored PBR receiver");
  for(int i=0;i<both.getLength();++i)CHECK((restored[i]-both[i]).length()==0,"Receiver restoration is exact");
  // World-unit changes preserve camera/light projections and the texel-based
  // correction. A few raster edge pixels may differ after float conversion.
  for(float scale:{.01f,100.f,1.f}) {
    camera->height=7*scale;camera->position.setValue(0,0,8*scale);
    camera->nearDistance=scale;camera->farDistance=20*scale;
    for(int l=0;l<2;++l) {
      lights[l]->location.setValue((l?-2:2)*scale,2*scale,4*scale);
      lights[l]->nearDistance=scale;lights[l]->farDistance=12*scale;
      if(options.pbrShadowFilter==COIN_RENDER_PBR_SHADOW_PCSS)lights[l]->sourceRadius=.03f*scale;
    }
    cube->width=cube->height=cube->depth=1.4f*scale;
    slab->width=slab->height=6*scale;slab->depth=.05f*scale;
    translation->translation.setValue(0,0,-1.5f*scale);
    CHECK(render(*target,root,restored),"CAD unit-scale equivalent renders");size_t equivalent=0;
    for(int i=0;i<both.getLength();++i)
      equivalent+=(restored[i]-both[i]).length()<.03f*std::max(1.f,both[i].length());
    CHECK(equivalent>size_t(both.getLength())*995/1000,"Bias is relative to projection texels rather than an absolute world minimum");
  }
  if(argc>1) {
    std::vector<uint8_t> rgba;target->readbackRGBA(rgba);
    std::ofstream out((std::string(argv[1])+"/pbr-shadows.ppm").c_str(),std::ios::binary);out<<"P6\n128 128\n255\n";
    for(size_t p=0;p<rgba.size();p+=4)out.write(reinterpret_cast<const char*>(rgba.data()+p),3);
    CHECK(bool(out),"Save actual PBR shadow capture");
  }
  if(options.pbrShadowFilter==COIN_RENDER_PBR_SHADOW_VSM) {
  CoinRenderOptions rawOptions=options;rawOptions.pbrShadowMomentCorrection=false;
  rawOptions.pbrShadowBiasTexels=rawOptions.pbrShadowSlopeBiasTexels=0;
  std::unique_ptr<CoinRenderTarget> raw(CoinRenderTarget::createOffscreen(SbVec2i32(128,128),rawOptions));
  rawOptions.pbrShadowMomentCorrection=true;
  std::unique_ptr<CoinRenderTarget> correctedTarget(CoinRenderTarget::createOffscreen(SbVec2i32(128,128),rawOptions));
  group->precision=.03125f;group->epsilon=0;group->threshold=0;
  translation->translation.setValue(0,0,-.72f);
  CHECK(correctedTarget&&render(*correctedTarget,root,first)&&raw&&render(*raw,root,second),"Corrected and original VSM moments render independently");
  size_t corrected=0;float correctionPeak=0;
  for(int i=0;i<first.getLength();++i) {
    for(int c=0;c<3;++c)CHECK(first[i][c]+.001f>=second[i][c],"Nonnegative moment variance correction cannot darken a receiver");
    corrected+=first[i][0]-second[i][0]>.001f;
    correctionPeak=std::max(correctionPeak,first[i][0]-second[i][0]);
  }
  std::printf("moment correction pixels=%zu delta=%g\n",corrected,correctionPeak);
  CHECK(corrected>20,"GPU second-moment derivative correction affects sloped actual receivers");
  group->precision=.5f;group->epsilon=.00001f;group->threshold=.1f;
  translation->translation.setValue(0,0,-1.5f);
  }
  cube->ref();group->removeChild(cube);
  auto * plate=new SoSeparator;plate->ref();
  auto * hints=new SoShapeHints;hints->shapeType=SoShapeHints::SOLID;
  hints->vertexOrdering=SoShapeHints::CLOCKWISE;plate->addChild(hints);
  auto * culling=new SoShadowCulling;culling->mode=SoShadowCulling::AS_IS_CULLING;plate->addChild(culling);
  auto * plateCoords=new SoCoordinate3;
  const SbVec3f points[]={SbVec3f(-.7f,-.7f,0),SbVec3f(.7f,-.7f,0),SbVec3f(.7f,.7f,0),SbVec3f(-.7f,.7f,0)};
  plateCoords->point.setValues(0,4,points);plate->addChild(plateCoords);
  auto * face=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,3,-1};face->coordIndex.setValues(0,5,indices);plate->addChild(face);
  group->addChild(plate);CHECK(render(*target,root,first),"Back-facing thin CAD plate with native culling");
  culling->mode=SoShadowCulling::NO_CULLING;CHECK(render(*target,root,second),"Two-sided thin CAD plate caster");
  size_t plateShadow=0;for(int i=0;i<first.getLength();++i)plateShadow+=first[i][0]-second[i][0]>.2f;
  CHECK(plateShadow>30,"NO_CULLING preserves thin CAD shadow casters without changing their visible culling");
  culling->mode=SoShadowCulling::AS_IS_CULLING;CHECK(render(*target,root,restored),"AS_IS restores native captured hints");
  for(int i=0;i<first.getLength();++i)CHECK((restored[i]-first[i]).length()==0,"AS_IS restoration retains original shape hints");
  group->removeChild(plate);plate->unref();group->insertChild(cube,3);cube->unref();
  CoinRenderPbrEnvironment env;SbString error;
  const SbString path=argc>1?SbString(argv[1])+"/shadow-white.cibl":SbString("/tmp/coin-shadow-white.cibl");
  CHECK(env.bake(env.WHITE,16,32,32,512,env,error)&&env.write(path,error),"Independent constant environment fixture");
  options.pbrEnvironmentBundle=path;
  std::unique_ptr<CoinRenderTarget> ibl(CoinRenderTarget::createOffscreen(SbVec2i32(128,128),options));
  lights[0]->intensity=lights[1]->intensity=0;
  CHECK(ibl&&render(*ibl,root,first),"IBL plus shadow groups");style->style=SoShadowStyle::NO_SHADOWING;
  size_t environmentPixels=0;
  for(int i=0;i<first.getLength();++i)environmentPixels+=first[i][0]>.3f;
  CHECK(environmentPixels>8000,"Actual environment radiance is present beyond authored emission");
  CoinRenderAction referenceCapture(SbViewportRegion(128,128));referenceCapture.apply(root);
  auto environmentFrame=referenceCapture.getPimpl()->lastRejectedShadowFrame;
  environmentFrame.shadowGroups.clear();environmentFrame.shadowLights.clear();
  for(auto & state:environmentFrame.renderStates)state.shadowGroupSlot=0;
  environmentFrame.draws.erase(std::remove_if(environmentFrame.draws.begin(),environmentFrame.draws.end(),
    [](const CoinRenderDrawPacket & draw){return draw.shadowLightSlot!=0;}),environmentFrame.draws.end());
  CoinRenderTargetP environmentReference(SbVec2i32(128,128));environmentReference.options=options;
  CHECK(environmentReference.executeFrame(environmentFrame).status==CoinRenderBackendStatus::SUCCESS,
    "Independent existing IBL program without shadow groups");
  SbList<SbVec3f> oracleEnvironment;
  CHECK(environmentReference.backend->readSceneLinear(oracleEnvironment)&&oracleEnvironment.getLength()==first.getLength(),"IBL control radiance readback");
  for(int i=0;i<first.getLength();++i)CHECK((first[i]-oracleEnvironment[i]).length()<.003f,
    "Shadow+IBL program agrees with qualified unshadowed environment program");
  CHECK(render(*ibl,root,second),"IBL with receiver disabled");
  for(int i=0;i<first.getLength();++i)CHECK((first[i]-second[i]).length()==0,"Direct shadow visibility does not suppress environment radiance");
  style->style=SoShadowStyle::SHADOWED;lights[0]->intensity=lights[1]->intensity=20;
  auto * annotation=new SoAnnotation;auto * unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;annotation->addChild(unlit);
  auto * legacy=new SoMaterial;legacy->diffuseColor=SbColor(0,1,0);annotation->addChild(legacy);
  auto * coords=new SoCoordinate3;coords->point.set1Value(0,-2,2,1);coords->point.set1Value(1,2,2,1);annotation->addChild(coords);
  auto * line=new SoLineSet;line->numVertices=2;annotation->addChild(line);root->addChild(annotation);
  CHECK(render(*ibl,root,restored),"CAD technical line with HDR IBL shadows");
  std::vector<uint8_t> technicalPixels;ibl->readbackRGBA(technicalPixels);size_t green=0;
  for(size_t p=0;p<technicalPixels.size();p+=4)
    green+=technicalPixels[p]==0&&technicalPixels[p+1]==255&&technicalPixels[p+2]==0;
  CHECK(green>50,"CAD technical line retains authored display color after tone mapping");
  root->removeChild(annotation);CHECK(render(*ibl,root,both),"CAD face reference without technical layer");
  for(int i=0;i<both.getLength();++i)CHECK((restored[i]-both[i]).length()==0,"Technical layer never casts into the scene or contaminates HDR radiance");
#ifdef COIN_SHADOW_TEST_X11
  if(windowMode) {
  Window iblWindow=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,128,128,0,0,0);
  XMapWindow(display,iblWindow);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor desc{};desc.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;desc.structSize=sizeof(desc);desc.type=COIN_RENDER_SURFACE_XLIB;
  desc.native.xlib.display=display;desc.native.xlib.window=iblWindow;
  {
    std::unique_ptr<CoinRenderTarget> view(CoinRenderTarget::createWindow(desc,SbVec2i32(128,128),options));
    CHECK(view&&view->requestWindowReadbackRGBA()&&render(*view,root,restored),"Native window PBR shadows with IBL");
    for(int i=0;i<both.getLength();++i)CHECK((restored[i]-both[i]).length()==0,"Window and offscreen scene radiance agree");
    std::vector<uint8_t> a,b;view->readbackRGBA(a);ibl->readbackRGBA(b);
    CHECK(a==b,"Native CAD view and offscreen display pixels agree");
    CHECK(view->requestWindowReadbackRGBA()&&render(*view,root,restored),"Native CAD view replay");
    view->readbackRGBA(b);CHECK(a==b,"Native CAD view replay is exact");
  }
  XDestroyWindow(display,iblWindow);
  target.reset();XDestroyWindow(display,window);
  }
#endif
  root->unref();std::printf("PBR shadows: per-light additivity, emission, HDR peak=%g, %zu shadow receiver pixels, IBL and CAD layers PASS\n",peak,shadowPixels);return 0;
}
