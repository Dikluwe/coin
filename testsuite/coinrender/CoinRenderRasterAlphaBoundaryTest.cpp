// Independent boundary witnesses; the older shadow gates remain unchanged.
#define main coin_render_shadow_alpha_legacy_main
#include "CoinRenderShadowAlphaTest.cpp"
#undef main
#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include <Inventor/SoPath.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoPointLight.h>

namespace boundary {
void raster(SoSeparator * parent,int kind,int fontSize=8,int alphaByte=255) {
  if(kind==0) {
    auto *font=new SoFont; font->name="defaultFont";font->size=fontSize;
    parent->addChild(font);auto *text=new SoText2;text->string="MW";parent->addChild(text);
  } else if(kind==1) {
    auto *image=new SoImage;image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;
    std::vector<unsigned char> pixels(17*17*4,255);
    for(int i=0;i<17*17;++i)pixels[4*i+3]=static_cast<unsigned char>(alphaByte);
    // This equality fixture has uniform alpha. A zero-alpha texel would
    // pass LESS and can write depth, so it cannot share this whole-frame oracle.
    image->image.setValue(SbVec2s(17,17),4,pixels.data());parent->addChild(image);
  } else {
    auto *coords=new SoCoordinate3;coords->point.setValue(0,0,0);parent->addChild(coords);
    if(kind==2) {auto *m=new SoMarkerSet;m->markerIndex=SoMarkerSet::SQUARE_FILLED_9_9;parent->addChild(m);}
    else {auto *m=new SoIndexedMarkerSet;m->coordIndex=0;m->markerIndex=SoMarkerSet::SQUARE_FILLED_9_9;parent->addChild(m);}
  }
}
struct Harness {
  Capture capture;
  explicit Harness(bool gpu,bool canonical):capture(canonical) {
    if(!capture.target)return;
    capture.target->setDepthReadbackEnabled(TRUE);
    if(!gpu)capture.target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  }
  bool render(SoNode *root,Frame &out) {
    const uint64_t before=capture.target->getLastSubmissionSerial();
    return capture.render(root,out)&&require(out.serial>before,"one apply publishes complete RGBA/depth and advances serial");
  }
};
bool same(const Frame&a,const Frame&b) {return a.rgba==b.rgba&&a.depth==b.depth;}
const int functions[]={SoAlphaTest::LESS,SoAlphaTest::LEQUAL,SoAlphaTest::EQUAL,
  SoAlphaTest::GEQUAL,SoAlphaTest::GREATER,SoAlphaTest::NOTEQUAL};
bool sweep(Harness &h,SoNode *root,SoAlphaTest *test,float incoming,const std::vector<float>&refs,const std::string&label) {
  Frame pass,fail;
  test->function=SoAlphaTest::ALWAYS;test->value=0;
  if(!h.render(root,pass))return false;
  test->function=SoAlphaTest::NEVER;
  if(!h.render(root,fail))return false;
  bool ok=require(rasterDifference(pass,fail)>0,label+" authored nonempty pass/fail witness");
  for(float reference:refs)for(int function:functions) {
    test->function=function;test->value=reference;Frame actual;
    if(!h.render(root,actual))return false;
    const bool accepted=alphaPass(function,incoming,reference);
    const bool equal=same(actual,accepted?pass:fail);
    if(!equal)std::cerr<<label<<" function="<<function<<" raw="<<incoming<<" ref="<<reference<<" expected="<<accepted<<'\n';
    ok &= require(equal,label+" comparison uses authored equality and signed phase");
  }
  test->function=SoAlphaTest::EQUAL;test->value=incoming;
  Frame stable,failed,recovered;
  if(!h.render(root,stable))return false;
  test->value=std::numeric_limits<float>::quiet_NaN();h.capture.action.apply(root);
  h.capture.target->readbackRGBA(failed.rgba);h.capture.target->readbackDepth(failed.depth);
  failed.serial=h.capture.target->getLastSubmissionSerial();
  ok &= require(h.capture.action.getLastStatus()!=CoinRenderAction::SUCCESS&&same(stable,failed)&&stable.serial==failed.serial,label+" invalid consumed reference atomically refuses publication");
  test->value=incoming;
  ok &= require(h.render(root,recovered)&&same(stable,recovered),label+" first valid recovery restores exact frame");
  return ok;
}
bool receivers(bool gpu,bool canonical) {
  bool ok=true;
  for(int kind=0;kind<4;++kind)for(bool interior:{false,true}) {
    Scene scene;
    // The GPU receiver exercises an active map. CPU tests ordinary raster without a GPU context.
    scene.group->isActive=gpu?TRUE:FALSE;
    auto *receiver=static_cast<SoSeparator*>(scene.group->getChild(1));
    receiver->removeChild(3);receiver->removeChild(2);
    auto *alpha=new SoAlphaTest;receiver->insertChild(alpha,1);
    auto *move=new SoTranslation;move->translation.setValue(0,0,-1);receiver->addChild(move);
    const float incoming=interior?128.f/255.f:1.f;
    if(interior&&kind!=1)static_cast<SoMaterial*>(receiver->getChild(2))->transparency=1.f-incoming;
    raster(receiver,kind,8,interior?128:255);
    Harness h(gpu,canonical);
    if(interior)h.capture.action.setTransparencyType(CoinRenderAction::BLEND);
    if(!require(bool(h.capture.target),"receiver target"))return false;
    // Interior alpha is exactly the authored UNORM8 value128/255. Image
    // obtains it from every texel; native mono text/markers obtain it from
    // packed primary color in BLEND. No observed frame calibrates equality.
    // Above/below/equal references distinguish all six operators, including
    // EQUAL versus LEQUAL which an opaque-only fixture cannot distinguish.
    const std::vector<float> refs=interior?std::vector<float>{.25f,incoming,.75f}:std::vector<float>{.5f,1.f,1.5f};
    ok &= sweep(h,scene.root,alpha,incoming,refs,"receiver/kind="+std::to_string(kind)+"/interior="+std::to_string(interior));
  }
  return ok;
}
bool casters(bool canonical) {
  bool ok=true;
  for(int kind=0;kind<4;++kind) {
    Scene scene;scene.group->precision=.03125f;
    static_cast<SoOrthographicCamera*>(scene.root->getChild(0))->height=1.5f;
    auto *receiver=static_cast<SoSeparator*>(scene.group->getChild(1));
    auto *coordinates=static_cast<SoCoordinate3*>(receiver->getChild(2));
    for(int i=0;i<4;++i){SbVec3f point=coordinates->point[i];point[0]=point[0]>0?.4f:-.4f;point[1]=point[1]>0?.4f:-.4f;point[2]=-.125f;coordinates->point.set1Value(i,point);}
    // A legal explicit line-shaped bbox has radius=half its Z extent.
    // Native directional fitting then gives near=0, far=extent*1.01,
    // VSM far=far*1.1. This authored extent makes the latter exactly one.
    // A custom orthographic map camera fixes the raster's projection and
    // unprojection to dyadic depth without changing the light's VSM range.
    const float extent=1.f/(1.01f*1.1f),half=.5f*extent;
    const float radius=.5f*std::sqrt(extent*extent);
    const float nearValue=(radius-half)*1.01f;
    const float farValue=((radius+half)*1.01f)*1.1f;
    if(!require(nearValue==0.f&&farValue==1.f,"authored directional VSM interval is exactly [0,1]"))return false;
    scene.light->bboxCenter.setValue(0,0,0);scene.light->bboxSize.setValue(0,0,extent);
    scene.map->removeChild(4);scene.map->removeChild(3);
    auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,radius);
    camera->nearDistance=0;camera->farDistance=1;camera->height=1;scene.map->insertChild(camera,0);
    auto *move=new SoTranslation;scene.map->addChild(move);raster(scene.map,kind);
    Harness h(true,canonical);
    if(!require(bool(h.capture.target),"caster target"))return false;
    for(float l:{11.f/32.f,10.f/32.f}) {
      move->translation.setValue(0,0,radius-l);
      const float raw=2.f*(64.f*l*l-std::floor(64.f*l*l))-1.f;
      if(!require(raw==(l==11.f/32.f?.125f:-.5f),"authored dyadic moment phase"))return false;
      ok &= sweep(h,scene.root,scene.alpha,raw,{-.5f,0.f,.125f,.25f},
        "caster/kind="+std::to_string(kind)+"/raw="+std::to_string(raw));
    }
  }
  return ok;
}

#include "CoinRenderRasterLightingWitnesses.h"
}
int main(int argc,char**argv) {
  bool gpu=false,canonical=false,castersOnly=false,paths=false,spot=false;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--gpu")gpu=true;else if(arg=="--canonical-raster")canonical=true;
    else if(arg=="--casters")castersOnly=true;else if(arg=="--paths")paths=true;else if(arg=="--spot")spot=true;else if(arg!="--cpu")return 2;
  }
  SoDB::init();CoinRenderAction::initClass();
  if(gpu) {CoinRenderCapabilities caps{};if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))!=0||!caps.gpu_available)return 77;}
  if(paths)return boundary::nonproducerPaths(gpu,canonical)?0:1;
  if(spot)return gpu?(boundary::spotNumerical(canonical)?0:1):2;
  if(castersOnly)return gpu?(boundary::casters(canonical)?0:1):2;
  return boundary::receivers(gpu,canonical)?0:1;
}
