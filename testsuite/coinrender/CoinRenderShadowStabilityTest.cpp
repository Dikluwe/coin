#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>
#include <memory>
#include <cstdio>
#include <cmath>
#include <fstream>
#define CHECK(x,m) do{if(!(x)){std::fprintf(stderr,"FAIL: %s (%d)\n",m,__LINE__);return 1;}}while(0)
static CoinRenderShadowPlan capture(SoNode * root,int pixels=128) {
  CoinRenderAction a(SbViewportRegion(pixels,pixels));a.apply(root);
  return a.getPimpl()->lastRejectedShadowPlan;
}
static bool pixels(CoinRenderTarget & t,SoNode * root,std::vector<uint8_t>& p) {
  CoinRenderAction a(SbViewportRegion(128,128));a.setRenderTarget(&t);a.apply(root);
  if(a.getLastStatus()!=CoinRenderAction::SUCCESS){std::fprintf(stderr,"GPU: %s\n",a.getLastError().getString());return false;}
  t.readbackRGBA(p);return p.size()==128*128*4;
}
int main(int argc,char ** argv) {
  SoDB::init();CoinRenderAction::initClass();
  const SbVec3f direction(-.4f,-.3f,-1);
  const SbBox3f full(SbVec3f(-3,-2,-1),SbVec3f(3,2,1));
  CoinRenderShadowPass p;p.mapSize=1024;std::string error;
  CHECK(coin_render_shadow_stable_directional(full,full,direction,p,error),"Stable pure footprint");
  const auto clip=p.view*p.projectionCoin;
  for(float scale:{.00001f,.01f,1.f,100.f,100000.f}) {
    CoinRenderShadowPass scaled;scaled.mapSize=1024;
    SbBox3f box(full.getMin()*scale,full.getMax()*scale);
    CHECK(coin_render_shadow_stable_directional(box,box,direction,scaled,error),"World unit scales admitted without absolute guards");
    CHECK(std::abs(scaled.texelWorld/(p.texelWorld*scale)-1)<1e-6,"Texel footprint scales with drawing bounds");
    SbVec3f a,b;clip.multVecMatrix(SbVec3f(1,.5f,.25f),a);
    (scaled.view*scaled.projectionCoin).multVecMatrix(SbVec3f(1,.5f,.25f)*scale,b);
    CHECK((a-b).length()<1e-5,"Normalized projection independent of world units");
  }
  auto * root=new SoSeparator;root->ref();auto * camera=new SoOrthographicCamera;
  camera->position=SbVec3f(0,0,8);camera->height=8;camera->nearDistance=1;camera->farDistance=20;root->addChild(camera);
  auto * group=new SoShadowGroup;group->quality=1;root->addChild(group);
  auto * light=new SoShadowDirectionalLight;light->direction=direction;group->addChild(light);
  auto * material=new SoPBRMaterial;material->baseColor=SbColor(.7f,.4f,.2f);group->addChild(material);group->addChild(new SoCube);
  auto * floor=new SoSeparator;auto * style=new SoShadowStyle;style->style=SoShadowStyle::SHADOWED;floor->addChild(style);
  auto * translation=new SoTranslation;translation->translation=SbVec3f(0,0,-1.5f);floor->addChild(translation);
  auto * slab=new SoCube;slab->width=slab->height=6;slab->depth=.05f;floor->addChild(slab);group->addChild(floor);
  const auto initial=capture(root);CHECK(initial.passes.size()==1&&initial.passes[0].stabilized,"Native PBR directional capture selects stable projection");
  const auto worldClip=initial.passes[0].view*initial.passes[0].projectionCoin;
  for(float angle:{-.08f,0.f,.08f})for(float height:{7.9f,8.f,8.1f}) {
    camera->orientation=SbRotation(SbVec3f(0,1,0),angle);camera->position=SbVec3f(8*std::sin(angle),0,8*std::cos(angle));camera->height=height;
    const auto current=capture(root);CHECK(current.passes.size()==1,"Orbit/zoom has a visible map");
    CHECK(current.passes[0].view*current.passes[0].projectionCoin==worldClip,"Drawing fully covered: orbit and zoom do not slide or stretch the map");
  }
  camera->orientation=SbRotation::identity();camera->position=SbVec3f(0,0,8);camera->height=2;
  const auto partial=capture(root);CHECK(partial.passes.size()==1,"Close-up map");
  const auto reference=partial.passes[0];
  for(float move:{.0001f,.01f,.1f}) {
    camera->position=SbVec3f(move,0,8);const auto current=capture(root);CHECK(current.passes.size()==1,"Translated close-up map");const auto & q=current.passes[0];
    CHECK(q.stabilizationLevel==reference.stabilizationLevel&&q.projectionCoin==reference.projectionCoin,"Small translations retain stable extent");
    const double cells=(q.view[3][0]-reference.view[3][0])/reference.texelWorld;
    CHECK(std::abs(cells-std::round(cells))<.0001,"Projection center moves by integral shadow texels");
    CHECK(q.nearDistance==reference.nearDistance&&q.farDistance==reference.farDistance,"Offscreen caster depth is retained during view translations");
  }
  camera->position=SbVec3f(0,0,8);camera->height=8;
  if(argc>1&&std::string(argv[1])=="--gpu") {
    CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;options.shadowDiagnostic=COIN_RENDER_SHADOW_DIAGNOSTIC_DEPTH;
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(128,128),options));std::vector<uint8_t> before,after;
    CHECK(target&&pixels(*target,root,before),"Actual stable directional map GPU diagnostic");
    const auto cache=target->getPimpl()->backend->shadowCacheStats();
    for(float angle:{-.08f,.08f})for(float height:{7.9f,8.1f}) {
      camera->orientation=SbRotation(SbVec3f(0,1,0),angle);camera->position=SbVec3f(8*std::sin(angle),0,8*std::cos(angle));camera->height=height;
      CHECK(pixels(*target,root,after)&&before==after,"Real directional map is pixel-identical through covered orbit/zoom");
      const auto replay=target->getPimpl()->backend->shadowCacheStats();
      CHECK(replay.allocations==cache.allocations&&replay.renders==cache.renders,"Stable covered views reuse actual GPU map content");
    }
    if(argc>2) {
      std::ofstream out(argv[2],std::ios::binary);out<<"P6\n128 128\n255\n";
      for(size_t i=0;i<before.size();i+=4)out.write(reinterpret_cast<const char*>(before.data()+i),3);
      CHECK(bool(out),"Save actual stable directional diagnostic");
    }
  }
  root->unref();std::printf("Directional stability: relative unit scales, native orbit/zoom, fixed extent, integer texel centers and full caster depth PASS\n");return 0;
}
