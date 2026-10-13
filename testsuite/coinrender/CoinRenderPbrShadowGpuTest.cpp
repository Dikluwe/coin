#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderPbrEnvironment.h"
#include <Inventor/SoDB.h>
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
  lights[1]->intensity=0;CHECK(render(*target,root,first),"First shadow light alone");
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
  if(argc>1) {
    std::vector<uint8_t> rgba;target->readbackRGBA(rgba);
    std::ofstream out((std::string(argv[1])+"/pbr-shadows.ppm").c_str(),std::ios::binary);out<<"P6\n128 128\n255\n";
    for(size_t p=0;p<rgba.size();p+=4)out.write(reinterpret_cast<const char*>(rgba.data()+p),3);
    CHECK(bool(out),"Save actual PBR shadow capture");
  }
  CoinRenderPbrEnvironment env;SbString error;
  const SbString path=argc>1?SbString(argv[1])+"/shadow-white.cibl":SbString("/tmp/coin-shadow-white.cibl");
  CHECK(env.bake(env.WHITE,16,32,32,512,env,error)&&env.write(path,error),"Independent constant environment fixture");
  options.pbrEnvironmentBundle=path;
  std::unique_ptr<CoinRenderTarget> ibl(CoinRenderTarget::createOffscreen(SbVec2i32(128,128),options));
  lights[0]->intensity=lights[1]->intensity=0;
  CHECK(ibl&&render(*ibl,root,first),"IBL plus shadow groups");style->style=SoShadowStyle::NO_SHADOWING;
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
