#include "CoinRenderHighlightRttShadowFixture.h"
#include <limits>
#include <sstream>
#include <Inventor/nodes/SoFrustumCamera.h>
#include <Inventor/nodes/SoDepthBuffer.h>
namespace highlight_rtt_shadow {
struct Producer {
  SoSeparator *root=new SoSeparator;
  SoShadowGroup *group=new SoShadowGroup;
  std::vector<SoOrthographicCamera*> cameras;
  std::vector<SoSeparator*> mapRoots;
  std::vector<SoTranslation*> moves;
  std::vector<SoCube*> casters;
  std::vector<SoLight*> lights;
  std::array<SoMaterial*,4> materials;
  std::array<SoIndexedFaceSet*,4> shapes;
  bool edited=false,moved=false;Config config;
  explicit Producer(const Config &c):config(c) {
    root->ref();auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,3);
    camera->height=2;camera->nearDistance=1;camera->farDistance=5;root->addChild(camera);
    auto *model=new SoLightModel;model->model=SoLightModel::PHONG;root->addChild(model);
    group->quality=1;group->precision=.125f;group->epsilon=0;group->threshold=0;group->intensity=1;root->addChild(group);
    const double k=c.spot?3*std::tan(.7):std::sqrt(6.);
    for(unsigned i=0;i<c.maps;++i) {
      auto *map=new SoSeparator;mapRoots.push_back(map);auto *view=new SoOrthographicCamera;
      view->position.setValue(float(.01*i),0,3);view->height=2;view->nearDistance=1;view->farDistance=float(5+.125*i);
      cameras.push_back(view);map->addChild(view);auto *style=new SoShadowStyle;style->style=SoShadowStyle::CASTS_SHADOW;map->addChild(style);
      auto *move=new SoTranslation;move->translation.setValue(float(.01*i+.35/k),float(-.6/k),.75f);moves.push_back(move);map->addChild(move);
      auto *cube=new SoCube;cube->width=cube->height=float(.5/k);cube->depth=.2f;casters.push_back(cube);map->addChild(cube);
      SoLight *light=nullptr;
      if(c.spot) {auto *spot=new SoShadowSpotLight;spot->location.setValue(0,0,3);spot->direction.setValue(0,0,-1);spot->cutOffAngle=.7f;spot->dropOffRate=0;spot->nearDistance=1;spot->farDistance=5;spot->shadowMapScene=map;light=spot;}
      else {auto *directional=new SoShadowDirectionalLight;directional->direction.setValue(0,0,-1);directional->bboxCenter.setValue(0,0,0);directional->bboxSize.setValue(2,2,4);directional->shadowMapScene=map;light=directional;}
      light->on=TRUE;light->intensity=float(1./c.maps);lights.push_back(light);group->addChild(light);
    }
    auto *style=new SoShadowStyle;style->style=SoShadowStyle::SHADOWED;group->addChild(style);
    const int32_t quad[]={0,1,2,-1,0,2,3,-1};
    for(unsigned q=0;q<4;++q) {
      auto *branch=new SoSeparator;materials[q]=new SoMaterial;materials[q]->ambientColor.setValue(0,0,0);materials[q]->specularColor.setValue(0,0,0);materials[q]->emissiveColor.setValue(0,0,0);materials[q]->transparency=0;branch->addChild(materials[q]);
      auto *normal=new SoNormal;const SbVec3f n[]={SbVec3f(0,0,1),SbVec3f(0,0,1),SbVec3f(0,0,1),SbVec3f(0,0,1)};normal->vector.setValues(0,4,n);branch->addChild(normal);
      auto *coordinates=new SoCoordinate3;const float x0=q%2?0:-1.1f,x1=q%2?1.1f:0,y0=q/2?0:-1.1f,y1=q/2?1.1f:0;
      const SbVec3f p[]={SbVec3f(x0,y0,0),SbVec3f(x1,y0,0),SbVec3f(x1,y1,0),SbVec3f(x0,y1,0)};coordinates->point.setValues(0,4,p);branch->addChild(coordinates);
      shapes[q]=new SoIndexedFaceSet;shapes[q]->coordIndex.setValues(0,8,quad);branch->addChild(shapes[q]);group->addChild(branch);
    }
    paint(false);
  }
  ~Producer(){root->unref();}
  Color palette(unsigned q)const {
    static const Color a[]={{{.75,.25,.5}},{{.25,.875,.375}},{{.5,.375,.875}},{{.875,.625,.125}}};
    static const Color b[]={{{.25,.625,.875}},{{.875,.375,.25}},{{.625,.875,.125}},{{.375,.125,.75}}};
    return edited?b[q]:a[q];
  }
  void paint(bool value){edited=value;for(unsigned q=0;q<4;++q){const auto rgb=palette(q);materials[q]->diffuseColor.setValue(float(rgb[0]),float(rgb[1]),float(rgb[2]));}}
  void moveLast(bool away) {
    moved=away;const double k=config.spot?3*std::tan(.7):std::sqrt(6.);const unsigned i=unsigned(moves.size()-1);
    moves.back()->translation.setValue(float(.01*i+(away?0:.35/k)),float((away?.7:-.6)/k),.75f);
  }
};
struct Combined {
  Scene receiver;Producer producer;SoSceneTexture2 *texture=new SoSceneTexture2;
  explicit Combined(const Config &c):receiver(c),producer(c) {
    for(int i=0;i<receiver.shared->getNumChildren();++i)if(receiver.shared->getChild(i)->isOfType(SoTexture2::getClassTypeId())) {
      receiver.shared->removeChild(i);receiver.shared->insertChild(texture,i);break;
    }
    texture->scene=producer.root;texture->size.setValue(64,64);texture->type=SoSceneTexture2::RGBA8;
    texture->transparencyFunction=SoSceneTexture2::NONE;texture->backgroundColor.setValue(0,0,0,1);texture->model=SoSceneTexture2::MODULATE;texture->wrapS=texture->wrapT=SoSceneTexture2::CLAMP;
    if(c.mips)for(int i=0;i<receiver.shared->getNumChildren();++i)if(receiver.shared->getChild(i)->isOfType(SoComplexity::getClassTypeId()))
      static_cast<SoComplexity*>(receiver.shared->getChild(i))->textureQuality=.9f;
    for(int i=0;i<receiver.shared->getNumChildren();++i)if(receiver.shared->getChild(i)->isOfType(SoTextureCoordinate2::getClassTypeId())) {
      auto *uv=static_cast<SoTextureCoordinate2*>(receiver.shared->getChild(i));const SbVec2f p[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(0,1)};uv->point.setValues(0,3,p);
    }
  }
  std::vector<SbUniqueId> stamp()const {
    auto s=receiver.stamp();s.push_back(texture->getNodeId());s.push_back(producer.root->getNodeId());s.push_back(producer.group->getNodeId());
    for(auto *node:producer.materials)s.push_back(node->getNodeId());for(auto *node:producer.cameras)s.push_back(node->getNodeId());for(auto *node:producer.moves)s.push_back(node->getNodeId());for(auto *node:producer.lights)s.push_back(node->getNodeId());return s;
  }
};
class Audit:public BaseAudit {
public:
  using BaseAudit::BaseAudit;mutable CoinRenderRttPlan graph;
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g,const CoinRenderFramePlan &f,const SbVec2i32 &size)const override {
    graph=g.inspection(f);return Proxy::preflightRtt(g,f,size);
  }
};
bool graphDelivery(const Combined &scene,const Config &c,const CoinRenderRttPlan &graph,const CoinRenderFramePlan &frame,bool stroke,bool resolved) {
  bool ok=delivery(scene.receiver,c,frame,stroke);
  if(!check(graph.mode==c.route&&graph.producers.size()==1,"shared base paths reference exactly one producer through requested route"))return false;
  const auto &p=graph.producers.front();const auto policy=c.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE;
  ok &= check(p.sourceRevision==scene.texture->getNodeId()&&!p.cube&&p.size==SbVec2i32(64,64)&&p.format==CoinRenderTextureFormat::RGBA8_LINEAR&&p.plan.textureSamplingPolicy==policy,
    "authored RTT identity/extent/format and sampling reach producer");
  CoinRenderShadowPlan shadows;std::string error;
  if(!check(coin_render_plan_shadows(p.plan,shadows,error),"real producer shadow plan: "+error))return false;
  ok &= check(shadows.passes.size()==c.maps&&p.plan.shadowGroups.size()==1&&p.plan.shadowLights.size()==c.maps,"producer retains its own real map/group/light inventory");
  if(p.plan.shadowGroups.size()==1) {
    const auto &group=p.plan.shadowGroups.front();
    ok &= check(group.sourceRevision==scene.producer.group->getNodeId()&&group.hasEntryCamera&&
      matrixNear(group.entryCamera.viewMatrix,sourceView(0))&&matrixNear(group.entryCamera.projectionMatrixCoin,sourceProjection(0))&&
      group.entryCamera.nearDistance==1&&group.entryCamera.farDistance==5,
      "producer keeps its own group identity and entry camera");
  }
  for(const auto &pass:shadows.passes) {
    if(pass.lightSlot>=c.maps){ok=false;continue;}const unsigned slot=pass.lightSlot;
    unsigned casters=0,receivers=0;
    for(unsigned drawIndex:pass.casterDraws) {
      if(drawIndex>=p.plan.draws.size()){ok=false;continue;}const auto &d=p.plan.draws[drawIndex];casters+=d.geometry.indexCount;
      ok &= check(d.sourceNodeId==scene.producer.casters[slot]->getNodeId()&&d.shadowLightSlot==slot+1&&d.isSceneLayer(),"producer map owns only its actual caster, no highlight");
      if(d.renderStateSlot>=p.plan.renderStates.size()){ok=false;continue;}
      const auto &state=p.plan.renderStates[d.renderStateSlot];
      if(state.cameraSlot>=p.plan.cameras.size()||state.viewportSlot>=p.plan.viewports.size()){ok=false;continue;}
      const auto &camera=p.plan.cameras[state.cameraSlot];const auto &viewport=p.plan.viewports[state.viewportSlot];
      ok &= check(state.shadowMapCameraOverride&&state.shadowMapSize==256&&viewport.width==256&&viewport.height==256&&
        !camera.isPerspective&&camera.nearDistance==1&&camera.farDistance==float(5+.125*slot)&&
        matrixNear(camera.viewMatrix,sourceView(slot))&&matrixNear(camera.projectionMatrixCoin,sourceProjection(slot)),
        "each producer caster retains its distinct raw authorial camera and real map viewport");
    }
    for(unsigned drawIndex:pass.receiverDraws) {
      if(drawIndex>=p.plan.draws.size()){ok=false;continue;}const auto &d=p.plan.draws[drawIndex];receivers+=d.geometry.indexCount;
      bool source=false;for(auto *shape:scene.producer.shapes)source |= shape->getNodeId()==d.sourceNodeId;
      ok &= check(source&&!d.shadowLightSlot&&d.isSceneLayer(),"producer receiver is an authored quadrant");
      if(d.renderStateSlot>=p.plan.renderStates.size()||d.renderStateSlot>=pass.lightingIndexByState.size()){ok=false;continue;}
      const auto &state=p.plan.renderStates[d.renderStateSlot];const int index=pass.lightingIndexByState[d.renderStateSlot];
      if(state.lightingSlot>=p.plan.lightingStates.size()||index<0||size_t(index)>=p.plan.lightingStates[state.lightingSlot].lights.size()){ok=false;continue;}
      const auto &light=p.plan.lightingStates[state.lightingSlot].lights[size_t(index)];
      ok &= check(state.lightModel==CoinRenderLightModel::PHONG&&state.shadowStyle==2&&light.sourceRevision==scene.producer.lights[slot]->getNodeId()&&
        light.type==(c.spot?CoinRenderLightType::SPOT:CoinRenderLightType::DIRECTIONAL)&&light.intensity==float(1./c.maps)&&
        light.color[0]==1&&light.color[1]==1&&light.color[2]==1,
        "each producer quadrant resolves the real associated light and its complete intensity/color");
    }
    ok &= check(pass.visible&&casters==36&&receivers==24&&pass.mapSize==256,"each source map consumes original caster and four receiver quads");
    if(slot>=p.plan.shadowLights.size()){ok=false;continue;}
    const auto &light=p.plan.shadowLights[slot];
    ok &= check(light.sourceRevision==scene.producer.lights[slot]->getNodeId()&&light.enabled&&light.hasCustomScene&&light.mapSceneCaptured&&
      light.customSceneNodeId==scene.producer.mapRoots[slot]->getNodeId()&&matrixNear(pass.view,lookupView(c))&&matrixNear(pass.projectionCoin,lookupProjection(c)),
      "producer source/camera/light association is independent of the consumer map");
  }
  std::set<uint64_t> resources;
  for(const auto &d:frame.draws)if(d.isSceneLayer()&&!d.shadowLightSlot&&d.sourceNodeId==scene.receiver.mesh->getNodeId()) {
    if(d.renderStateSlot>=frame.renderStates.size()){ok=false;continue;}const auto &state=frame.renderStates[d.renderStateSlot];
    if(!state.hasTexture||state.textureImageSlot>=frame.textures.size()){ok=false;continue;}const auto &image=frame.textures[state.textureImageSlot];
    if(state.samplerSlot>=frame.samplers.size()){ok=false;continue;}
    const auto &sampler=frame.samplers[state.samplerSlot];
    ok &= check(sampler.filter==(c.mips?CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR:CoinRenderTextureFilter::LINEAR)&&sampler.wrapS==CoinRenderTextureWrap::CLAMP&&sampler.wrapT==CoinRenderTextureWrap::CLAMP,
      "authored linear or trilinear/clamp RTT sampler survives the combined shadow receiver path");
    ok &= check(image.width==64&&image.height==64&&image.components==4,"actual receiver samples full source extent");
    ok &= check(image.mipmapped==c.mips&&graph.requestsMips(1,frame)==c.mips,
      "authored complete mip-chain request is retained across the shared RTT dependency");
    if(resolved&&c.mips&&c.route==COIN_RENDER_SCENE_TEXTURE_STAGED)
      ok &= check(image.mipmapsRgba.size()==5460,"STAGED carries the complete64-to1 RGBA8 mip tail");
    if(!resolved){ok &= check(image.producerId!=0,"captured receiver retains logical RTT dependency");resources.insert(image.producerId);}
    else if(c.route==COIN_RENDER_SCENE_TEXTURE_DIRECT){ok &= check(image.gpuToken!=0&&image.pixelsRgba.empty(),"DIRECT receiver consumes real GPU token without CPU pixels");resources.insert(image.gpuToken);}
    else ok &= check(image.pixelsRgba.size()==64*64*4,"STAGED receiver consumes complete real source readback");
  }
  if(!resolved||c.route==COIN_RENDER_SCENE_TEXTURE_DIRECT)ok &= check(resources.size()==1,"both base occurrences share one actual RTT resource");
  return ok;
}
struct Metrics {unsigned lit=0,consumerDark=0,producerDark=0,stroke=0,box=0,line=0,outside=0,bad=0,depthBad=0;std::array<unsigned,4> quadrants{{0,0,0,0}};};
Metrics image(const Combined &s,const Config &c,const Frame &frame,unsigned side,bool visible,float width,uint16_t pattern,const Color &color,bool depth=true,bool report=true) {
  Metrics m;if(frame.rgba.size()!=size_t(side)*side*4||(depth&&frame.depth.size()!=size_t(side)*side)){m.bad=1;return m;}
  const double pixel=2./side,margin=2.1*pixel,sourceMargin=std::max(2.1*2./64,margin*2/.72),band=(width*.5+2.1)*pixel;
  const bool active=visible&&s.receiver.selected>=0&&pattern;
  for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
    const size_t at=size_t(y)*side+x;const double wx=(x+.5)*pixel-1,wy=1-(y+.5)*pixel;
    double contour=1e9;
    if(active) {
      const double lx=wx-(s.receiver.selected?.48:-.48);
      contour=std::min({segment(lx,wy,-.36,-.34,.36,-.34),segment(lx,wy,-.36,.38,-.36,-.34),
        c.line?segment(lx,wy,.36,-.34,-.36,.38):segment(lx,wy,.36,-.34,.36,.38)});
      if(!c.line)contour=std::min(contour,segment(lx,wy,-.36,.38,.36,.38));
      bool full=true;for(unsigned lane=0;lane<3;++lane)full &= std::abs(frame.rgba[4*at+lane]-255*color[lane])<=3;
      if(full){++m.stroke;m.bad+=contour>band;if(std::abs(lx)<.17&&std::abs(wy-.38)<band)++m.box;if(std::abs(lx)<.17&&std::abs(lx+wy-.02)/std::sqrt(2.)<band)++m.line;}
    }
    if(active&&contour<=band)continue;
    bool outside=true;
    for(unsigned occurrence=0;occurrence<2;++occurrence) {
      const double local=wx-(occurrence?.48:-.48);
      const double edge=std::min({local+.36,wy+.34,(.02-local-wy)/std::sqrt(2.)});outside &= edge < -margin;if(edge<=margin)continue;
      const double sx=2*(local+.36)/.72-1,sy=2*(wy+.34)/.72-1;
      // Exclude texture quadrant boundaries and both independently derived
      // shadow boundaries by the already-qualified interior margins.
      if(std::abs(sx)<sourceMargin||std::abs(sy)<sourceMargin)continue;
      const double cd=std::min(.13-std::abs(wx+.64),.13-std::abs(wy+.14));
      const double pd=std::min(.25-std::abs(sx-.35),.25-std::abs(sy+.6));
      if(std::abs(cd)<=margin||std::abs(pd)<=sourceMargin)continue;
      const bool consumer=cd>margin,producer=pd>sourceMargin;
      const double vc=consumer?(s.receiver.moved?1./c.maps:0.):1.;
      const double vp=producer?(s.producer.moved?1./c.maps:0.):1.;
      const unsigned q=(sx>0?1:0)+(sy>0?2:0);++m.quadrants[q];
      if(consumer&&!producer)++m.consumerDark;else if(producer&&!consumer)++m.producerDark;else if(!consumer&&!producer)++m.lit;
      const auto texel=s.producer.palette(q);const Color diffuse{{.8,.6,.4}};
      for(unsigned lane=0;lane<3;++lane) {
        const double expected=255*texel[lane]*diffuse[lane]*.8*vc*vp;
        if(!c.spot)m.bad+=std::abs(frame.rgba[4*at+lane]-expected)>3;
        else { // Spot attenuation is the exact finite Lambert factor at each plane.
          const double lp=3/std::sqrt(9+sx*sx+sy*sy),lc=3/std::sqrt(9+wx*wx+wy*wy);
          m.bad+=std::abs(frame.rgba[4*at+lane]-expected*lp*lc)>3;
        }
      }
      m.bad+=frame.rgba[4*at+3]!=255;if(depth)m.depthBad+=std::abs(frame.depth[at]-.5)>2e-5;
    }
    if(outside){++m.outside;for(unsigned lane=0;lane<3;++lane)m.bad+=frame.rgba[4*at+lane]>3;if(depth)m.depthBad+=frame.depth[at]!=1;}
  }
  if(report)std::cout<<"highlight_rtt_shadow_image lit="<<m.lit<<" consumer_shadow="<<m.consumerDark<<" producer_shadow="<<m.producerDark<<" stroke="<<m.stroke<<" box="<<m.box<<" line="<<m.line<<" outside="<<m.outside<<" q="<<m.quadrants[0]<<","<<m.quadrants[1]<<","<<m.quadrants[2]<<","<<m.quadrants[3]<<" bad="<<m.bad<<" depth_bad="<<m.depthBad<<std::endl;return m;
}
unsigned phases(const std::string &trace) {
#ifdef COIN_HIGHLIGHT_RTT_SHADOW_BGFX
  const std::string marker="COIN_RENDER_PHASE bgfx lower_ms=";
#else
  const std::string marker="COIN_RENDER_PHASE bridge pack_ms=";
#endif
  unsigned count=0;size_t at=0;while((at=trace.find(marker,at))!=std::string::npos){++count;at+=marker.size();}return count;
}
class Inspection:public CoinRenderBackend {
public:
  mutable unsigned calls=0;unsigned submits=0;mutable CoinRenderRttPlan graph;mutable CoinRenderFramePlan frame;std::string error="independent combined graph capture stops before execution";
  bool isGpuBackend()const override{return true;}bool supportsOffscreenShadows()const override{return true;}
  CoinRenderBackendStatus getStatus()const override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderBackendStatus prepare(CoinRenderTargetP&)override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g,const CoinRenderFramePlan &f,const SbVec2i32&)const override {++calls;graph=g.inspection(f);frame=f;return {CoinRenderBackendStatus::UNSUPPORTED,error};}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP&)override{frame=f;++submits;return {CoinRenderBackendStatus::UNSUPPORTED,error};}
  void poll()override{}const std::string &getLastError()const override{return error;}
};
template<class Highlight> struct Run {
  Config config;Combined scene;Highlight highlight;SoRenderManager manager;std::unique_ptr<CoinRenderManagerAdapter> adapter;CoinRenderTarget *target=nullptr;Audit *audit=nullptr;
#ifdef COIN_HIGHLIGHT_SHADOW_X11
  Display *display=nullptr;Window window=0;
#endif
  std::vector<uint8_t> stablePhysical;
  Frame stable;bool visible=false;unsigned side=96;float width=3;uint16_t pattern=0xffff;Color color{{1,0,1}};
  explicit Run(const Config &c):config(c),scene(c) {
    manager.setViewportRegion(SbViewportRegion(side,side));manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);manager.setStereoMode(SoRenderManager::MONO);manager.setRenderMode(SoRenderManager::AS_IS);manager.setBackgroundColor(SbColor4f(0,0,0,1));manager.setSceneGraph(scene.receiver.root);manager.setGLRenderAction(&highlight);highlight.setTransparencyType(SoGLRenderAction::NONE);highlight.setVisible(FALSE);highlight.setLineWidth(width);highlight.setColor(SbColor(1,0,1));
    auto options=samplingTestOptions();options.sampleCount=c.samples;options.renderer=c.renderer;options.sceneTexture=c.route;options.textureSamplingPolicy=c.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE;
#ifdef COIN_HIGHLIGHT_SHADOW_X11
    if(c.window) {
      display=XOpenDisplay(nullptr);if(!display)return;
      window=XCreateSimpleWindow(display,DefaultRootWindow(display),16,16,side,side,0,0,0);
      XSetWindowAttributes attributes{};attributes.override_redirect=True;XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
      XMapRaised(display,window);XSync(display,False);
      CoinRenderNativeSurfaceDescriptor surface{};surface.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;surface.structSize=sizeof(surface);surface.type=COIN_RENDER_SURFACE_XLIB;surface.native.xlib.display=display;surface.native.xlib.window=window;
      adapter.reset(new CoinRenderManagerAdapter(manager,surface,SbVec2i32(side,side),options));
    } else
#endif
      adapter.reset(new CoinRenderManagerAdapter(manager,SbVec2i32(side,side),options));
    target=adapter->getSceneManager()->getRenderTarget();
  }
  ~Run(){adapter.reset();manager.setGLRenderAction(nullptr);manager.setSceneGraph(nullptr);
#ifdef COIN_HIGHLIGHT_SHADOW_X11
    if(window)XDestroyWindow(display,window);if(display)XCloseDisplay(display);
#endif
  }
  bool prepare(){if(!target)return false;auto &p=target->getPimpl().get();if(p.prepareBackend()!=CoinRenderBackendStatus::SUCCESS||!p.backend||!p.backend->isGpuBackend())return false;audit=new Audit(std::move(p.backend));p.backend.reset(audit);if(!config.window)target->setDepthReadbackEnabled(TRUE);return check(target->getOptions().sampleCount==config.samples&&target->getOptions().textureSamplingPolicy==(config.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE)&&target->getOptions().sceneTexture==config.route,"real target options match public request");}
  Frame read()const{Frame f;target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);f.serial=target->getLastSubmissionSerial();return f;}
  bool draw(const char *label) {
    const auto stamp=scene.stamp();const auto roots=audit->roots,producers=audit->producers;const uint64_t before=target->getLastSubmissionSerial();
    startup::Trace trace;if(!check(trace.start(),"passive execution phase capture"))return false;
    if(config.window&&!check(target->requestWindowReadbackRGBA(),"same-apply explicit window readback"))return false;
    const auto status=adapter->render();const auto events=trace.stop();const Frame current=read();
    std::cout<<"highlight_rtt_shadow "<<label<<" status="<<status<<" serial="<<before<<"->"<<current.serial<<" roots="<<audit->roots-roots<<" direct_producers="<<audit->producers-producers<<" phases="<<phases(events)<<" error="<<adapter->getLastError().getString()<<std::endl;
    bool ok=check(status==CoinRenderAction::SUCCESS&&current.serial>before&&audit->roots==roots+1&&audit->producers==producers+(config.route==COIN_RENDER_SCENE_TEXTURE_DIRECT?1:0)&&scene.stamp()==stamp&&highlight.calls==0,"one main publication and deduplicated real producer execution without source/fallback mutation");
#ifdef COIN_HIGHLIGHT_RTT_SHADOW_BGFX
    const unsigned count=config.route==COIN_RENDER_SCENE_TEXTURE_DIRECT?1:2;
#else
    const unsigned count=config.window?1:2;
#endif
    ok &= check(phases(events)==count,"exact actual root/factory-child completion phases");
    if(status!=CoinRenderAction::SUCCESS)return false;
    ok &= graphDelivery(scene,config,audit->graph,audit->plan,visible&&scene.receiver.selected>=0&&pattern,true);
    const auto m=image(scene,config,current,side,visible,width,pattern,color,!config.window);
    ok &= check(!m.bad&&!m.depthBad&&m.lit>30&&m.consumerDark>8&&m.producerDark>4&&m.outside>500&&m.quadrants[0]>4&&m.quadrants[1]>4&&m.quadrants[2]>4&&
      (!(visible&&scene.receiver.selected>=0&&pattern)||(m.stroke>8&&(config.line?m.line>4&&m.box==0:m.box>4&&m.line==0))),"independent source quadrants, consumer/source shadow, stroke, alpha and depth witnesses");
    if(ok&&config.window)ok &= observe(current);
    if(ok)stable=current;return ok;
  }
  std::vector<uint8_t> physical()const {
    std::vector<uint8_t> bytes;
#ifdef COIN_HIGHLIGHT_SHADOW_X11
    XSync(display,False);XWindowAttributes attributes{};
    if(!XGetWindowAttributes(display,window,&attributes)||attributes.width!=int(side)||attributes.height!=int(side))return bytes;
    XImage *capture=XGetImage(display,window,0,0,side,side,AllPlanes,ZPixmap);if(!capture)return bytes;
    bytes.resize(size_t(side)*side*4);
    auto channel=[](unsigned long pixel,unsigned long mask) {
      if(!mask)return uint8_t(0);unsigned shift=0;while(!(mask&1)){mask>>=1;++shift;}
      return uint8_t(((((pixel>>shift)&mask)*255)+mask/2)/mask);
    };
    for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
      const auto value=XGetPixel(capture,x,y);const size_t at=4*(size_t(y)*side+x);
      bytes[at]=channel(value,capture->red_mask);bytes[at+1]=channel(value,capture->green_mask);bytes[at+2]=channel(value,capture->blue_mask);bytes[at+3]=255;
    }
    XDestroyImage(capture);
#endif
    return bytes;
  }
  bool observe(const Frame &current) {
    const auto start=std::chrono::steady_clock::now(),deadline=start+std::chrono::seconds(5);
    unsigned attempts=0,lastMax=0;
    do {
      Frame shown;shown.rgba=physical();++attempts;bool matches=shown.rgba.size()==current.rgba.size();lastMax=0;
      if(matches)for(size_t at=0;at<current.rgba.size();at+=4)for(unsigned lane=0;lane<3;++lane) {
        const unsigned error=unsigned(std::abs(int(shown.rgba[at+lane])-int(current.rgba[at+lane])));
        lastMax=std::max(lastMax,error);matches &= error<=3;
      }
      if(matches) {
        const auto m=image(scene,config,shown,side,visible,width,pattern,color,false,false);
        if(!m.bad&&m.lit>30&&m.consumerDark>8&&m.producerDark>4&&m.outside>500&&
          (!(visible&&scene.receiver.selected>=0&&pattern)||(m.stroke>8&&(config.line?m.line>4&&m.box==0:m.box>4&&m.line==0)))) {
          stablePhysical=std::move(shown.rgba);
          std::cout<<"highlight_rtt_shadow_present attempts="<<attempts<<" max_rgb="<<lastMax<<" present_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<std::endl;
          return true;
        }
      }
    }while(std::chrono::steady_clock::now()<deadline);
    std::cerr<<"highlight_rtt_shadow_present timeout attempts="<<attempts<<" last_max_rgb="<<lastMax<<std::endl;return false;
  }
  bool resize(unsigned extent) {
#ifdef COIN_HIGHLIGHT_SHADOW_X11
    if(config.window){XResizeWindow(display,window,extent,extent);XSync(display,False);}
#endif
    side=extent;manager.setViewportRegion(SbViewportRegion(short(side),short(side)));
    return check(adapter->resize(SbVec2i32(side,side)),"same manager/target resize to actual extent");
  }
  void style(bool show,float lineWidth,uint16_t bits,const Color &rgb) {
    visible=show;width=lineWidth;pattern=bits;color=rgb;
    highlight.setVisible(show);highlight.setLineWidth(lineWidth);highlight.setLinePattern(bits);highlight.setColor(SbColor(float(rgb[0]),float(rgb[1]),float(rgb[2])));
  }
  bool refuse(const char *label) {
    const auto stamp=scene.stamp();const auto roots=audit->roots,producers=audit->producers;startup::Trace trace;if(!trace.start())return false;
    const auto status=adapter->render();const auto events=trace.stop();const auto current=read();
    std::cout<<"highlight_rtt_shadow_refuse "<<label<<" status="<<status<<" phases="<<phases(events)<<" error="<<adapter->getLastError().getString()<<std::endl;
    return check(status==CoinRenderAction::INVALID_SCENE&&adapter->getLastError().getLength()>0&&audit->roots==roots&&audit->producers==producers&&phases(events)==0&&current.serial==stable.serial&&current.rgba==stable.rgba&&current.depth==stable.depth&&scene.stamp()==stamp&&(!config.window||physical()==stablePhysical),"capture refusal preserves exact parent buffers/serial/physical and executes neither producer nor root");
  }
};
// Separate process/target, no manager: full UV observes the quadrant excluded
// by the main triangular UV domain and consumes the real DIRECT/STAGED result.
// Three subpixel phases and a slanted row band exercise odd4x fractions;
// half-pixel alone only proves2/4. Pixel-sized consumer probes use constant
// texel-center STRQ, isolating one producer resolve even under LINEAR filtering.
bool producerControl(const Config &c) {
  Producer source(c);const double cut=.515625; // source pixel48.5 of64
  SoCoordinate3 *edgeCoordinates=nullptr;SoNormal *edgeNormals=nullptr;
  for(int i=0;i<source.group->getNumChildren();++i)if(source.group->getChild(i)->isOfType(SoSeparator::getClassTypeId())) {
    auto *branch=static_cast<SoSeparator*>(source.group->getChild(i));
    if(branch->findChild(source.shapes[3])>=0)for(int j=0;j<branch->getNumChildren();++j) {
      if(branch->getChild(j)->isOfType(SoCoordinate3::getClassTypeId()))edgeCoordinates=static_cast<SoCoordinate3*>(branch->getChild(j));
      if(branch->getChild(j)->isOfType(SoNormal::getClassTypeId()))edgeNormals=static_cast<SoNormal*>(branch->getChild(j));
    }
  }
  if(!check(edgeCoordinates&&edgeNormals,"authored last quadrant coordinate and normal source"))return false;
  std::vector<SbVec3f> sourceVertices,sourceNormals;std::vector<int32_t> sourceIndices;
  auto band=[&](double y0,double y1,double bottomCut,double topCut) {
    const int32_t first=int32_t(sourceVertices.size());
    sourceVertices.push_back(SbVec3f(0,float(y0),0));sourceVertices.push_back(SbVec3f(float(bottomCut),float(y0),0));
    sourceVertices.push_back(SbVec3f(float(topCut),float(y1),0));sourceVertices.push_back(SbVec3f(0,float(y1),0));
    for(unsigned i=0;i<4;++i)sourceNormals.push_back(SbVec3f(0,0,1));
    const int32_t quad[]={first,first+1,first+2,-1,first,first+2,first+3,-1};sourceIndices.insert(sourceIndices.end(),quad,quad+8);
  };
  if(c.samples==4) {
    // x=48+.25/.5/.75 pixel phases give complementary counts without
    // prescribing any GPU's exact sample pattern. The last eight pixel rows
    // also use a slanted cut: X-.5*Y<0 in pixel-local coordinates. It isolates
    // one sample in both a2x2 pattern and the usual rotated four-point pattern,
    // where vertical half-pixel cuts alone can produce only even counts.
    band(0,.25,.5+.25/32,.5+.25/32);band(.25,.5,cut,cut);
    band(.5,.75,.5+.75/32,.5+.75/32);
    for(unsigned row=56;row<64;++row)band(double(row)/32-1,double(row+1)/32-1,.5,.5+.5/32);
  } else band(0,1.1,cut,cut);
  edgeCoordinates->point.setValues(0,int(sourceVertices.size()),sourceVertices.data());
  edgeNormals->vector.setValues(0,int(sourceNormals.size()),sourceNormals.data());
  source.shapes[3]->coordIndex.setValues(0,int(sourceIndices.size()),sourceIndices.data());
  auto cutAt=[&](double y) {
    if(c.samples!=4)return cut;
    if(y<.25)return .5+.25/32;if(y<.5)return cut;if(y<.75)return .5+.75/32;
    const double pixelY=(y+1)*32;return .5+(pixelY-std::floor(pixelY))*.5/32;
  };
  SoSeparator *root=new SoSeparator;root->ref();auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,3);camera->height=2;camera->nearDistance=1;camera->farDistance=5;root->addChild(camera);
  auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);auto *quality=new SoComplexity;quality->textureQuality=.1f;root->addChild(quality);
  auto *material=new SoMaterial;material->diffuseColor.setValue(1,1,1);root->addChild(material);
  auto *texture=new SoSceneTexture2;texture->scene=source.root;texture->size.setValue(64,64);texture->type=SoSceneTexture2::RGBA8;texture->transparencyFunction=SoSceneTexture2::NONE;texture->backgroundColor.setValue(0,0,0,1);texture->wrapS=texture->wrapT=SoSceneTexture2::CLAMP;root->addChild(texture);
  auto *uv=new SoTextureCoordinate2;const SbVec2f st[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};uv->point.setValues(0,4,st);root->addChild(uv);
  auto *coordinates=new SoCoordinate3;const SbVec3f quad[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(1,1,0),SbVec3f(-1,1,0)};coordinates->point.setValues(0,4,quad);root->addChild(coordinates);
  auto *shape=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,-1,0,2,3,-1};shape->coordIndex.setValues(0,8,indices);shape->textureCoordIndex.setValues(0,8,indices);root->addChild(shape);
  // The main full-UV quad still observes every source quadrant. Only its
  // coverage-probe cells receive coplanar overlays with constant STRQ at the
  // exact source texel center: LINEAR then cannot mix adjacent resolved texels
  // as the consumer's sample positions change. No extra apply/pass is used.
  auto *probeDepth=new SoDepthBuffer;probeDepth->test=TRUE;probeDepth->write=TRUE;probeDepth->function=SoDepthBuffer::LEQUAL;root->addChild(probeDepth);
  auto *probeCoordinates=new SoCoordinate3;auto *probeUV=new SoTextureCoordinate2;
  std::vector<SbVec3f> probes;std::vector<SbVec2f> probeTexcoords;std::vector<int32_t> probeIndices;
  for(unsigned row=0;row<64;++row) {
    const double y=(row+.5)/32-1;if(y<=.4||y>=.9)continue;
    const int32_t first=int32_t(probes.size());const float y0=float(double(row)/32-1),y1=float(double(row+1)/32-1);
    probes.push_back(SbVec3f(.5f,y0,0));probes.push_back(SbVec3f(.53125f,y0,0));probes.push_back(SbVec3f(.53125f,y1,0));probes.push_back(SbVec3f(.5f,y1,0));
    for(unsigned i=0;i<4;++i)probeTexcoords.push_back(SbVec2f(48.5f/64,float((row+.5)/64)));
    const int32_t quad[]={first,first+1,first+2,-1,first,first+2,first+3,-1};probeIndices.insert(probeIndices.end(),quad,quad+8);
  }
  probeCoordinates->point.setValues(0,int(probes.size()),probes.data());probeUV->point.setValues(0,int(probeTexcoords.size()),probeTexcoords.data());
  root->addChild(probeUV);root->addChild(probeCoordinates);auto *probeShape=new SoIndexedFaceSet;
  probeShape->coordIndex.setValues(0,int(probeIndices.size()),probeIndices.data());probeShape->textureCoordIndex.setValues(0,int(probeIndices.size()),probeIndices.data());root->addChild(probeShape);
  auto options=samplingTestOptions();options.renderer=c.renderer;options.sampleCount=c.samples;options.sceneTexture=c.route;options.textureSamplingPolicy=c.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64),options));
  if(!target){root->unref();return false;}auto &p=target->getPimpl().get();
  if(p.prepareBackend()!=CoinRenderBackendStatus::SUCCESS||!p.backend||!p.backend->isGpuBackend()){root->unref();return false;}
  auto *audit=new Audit(std::move(p.backend));p.backend.reset(audit);target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(64,64));action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);
  startup::Environment tracing("COIN_RENDER_TRACE_PHASES","1");bool ok=true;
  for(unsigned step=0;step<5&&ok;++step) {
    source.paint(step==1);source.moveLast(step==3);
    const auto sourceId=source.root->getNodeId(),textureId=texture->getNodeId();const unsigned roots=audit->roots,producers=audit->producers;const uint64_t serial=target->getLastSubmissionSerial();
    startup::Trace trace;if(!trace.start()){ok=false;break;}action.apply(root);const auto events=trace.stop();Frame frame;target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
    unsigned bad=0,depthBad=0,partial=0,odd=0,sourceDark=0;std::array<unsigned,4> quadrant{{0,0,0,0}};
    if(frame.rgba.size()!=64*64*4||frame.depth.size()!=64*64)bad=1;
    else for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
      const size_t at=size_t(y)*64+x;const double wx=(x+.5)*2./64-1,wy=1-(y+.5)*2./64,margin=2.1*2./64;
      depthBad+=std::abs(frame.depth[at]-.5)>2e-5;bad+=frame.rgba[4*at+3]!=255;
      // Constant illumination and no map silhouette intersects this vertical
      // edge ROI. RGB k/N detects a real producer resolve; window alpha is
      // irrelevant. The constant-STRQ probe prevents a second LINEAR texel
      // mixture; each consumer sample sees the same already-resolved texel.
      if(c.samples>1&&!c.spot&&std::abs(wx-cut)<2./64&&wy>.4&&wy<.9) {
        const auto rgb=source.palette(3);int found=-1;
        for(unsigned k=0;k<=c.samples;++k) {
          bool matches=true;for(unsigned lane=0;lane<3;++lane)matches &= std::abs(frame.rgba[4*at+lane]-255*rgb[lane]*k/c.samples)<=3;
          if(matches)found=int(k);
        }
        if(found<0)++bad;else if(found>0&&found<int(c.samples)){++partial;if(found%2)++odd;}
      }
      if(std::abs(wx)<margin||std::abs(wy)<margin||std::abs(wx-cutAt(wy))<margin)continue;
      const double old=std::min(.25-std::abs(wx-.35),.25-std::abs(wy+.6));
      const double moved=std::min(.25-std::abs(wx),.25-std::abs(wy-.7));
      if(std::abs(old)<=margin||std::abs(moved)<=margin)continue;
      const unsigned q=(wx>0?1:0)+(wy>0?2:0);const bool covered=q!=3||wx<cutAt(wy);
      const double visibility=source.moved?1.-(old>margin?double(c.maps-1)/c.maps:0.)-(moved>margin?1./c.maps:0.):(old>margin?0.:1.);
      if(covered)++quadrant[q];if(old>margin&&covered)++sourceDark;
      const auto rgb=source.palette(q);const double lambert=c.spot?3/std::sqrt(9+wx*wx+wy*wy):1.;
      for(unsigned lane=0;lane<3;++lane)bad+=std::abs(frame.rgba[4*at+lane]-(covered?255*rgb[lane]*visibility*lambert:0))>3;
    }
    std::cout<<"highlight_rtt_shadow_source_control step="<<step<<" status="<<action.getLastStatus()<<" serial="<<serial<<"->"<<target->getLastSubmissionSerial()<<" partial="<<partial<<" odd="<<odd<<" source_dark="<<sourceDark<<" q="<<quadrant[0]<<","<<quadrant[1]<<","<<quadrant[2]<<","<<quadrant[3]<<" bad="<<bad<<" depth_bad="<<depthBad<<" error="<<action.getLastError().getString()<<std::endl;
    const unsigned expectedPhases=
#ifdef COIN_HIGHLIGHT_RTT_SHADOW_BGFX
      c.route==COIN_RENDER_SCENE_TEXTURE_DIRECT?1u:2u;
#else
      2u;
#endif
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial&&audit->roots==roots+1&&
      audit->producers==producers+(c.route==COIN_RENDER_SCENE_TEXTURE_DIRECT?1:0)&&phases(events)==expectedPhases&&source.root->getNodeId()==sourceId&&texture->getNodeId()==textureId&&
      !bad&&!depthBad&&sourceDark>8&&*std::min_element(quadrant.begin(),quadrant.end())>50&&
      (c.samples==1||c.spot||(partial>4&&(c.samples!=4||odd>4))),
      "full-UV real RTT preserves all four numerical quadrants, source VSM and actual MSAA coverage");
  }
  action.setRenderTarget(nullptr);target.reset();root->unref();return ok;
}
// Real foreground capture follows the highlight in the same manager frame.
// Ordinary BASE_COLOR/quality0/LINES at entry are never a replay marker.
template<class Highlight> bool coreScope(Config c) {
  c.samples=1;c.maps=1;c.window=false;Run<Highlight> run(c);Scene after(c);
  auto *entryColor=new SoLightModel;entryColor->model=SoLightModel::BASE_COLOR;
  auto *entryQuality=new SoComplexity;entryQuality->textureQuality=0;
  auto *entryLines=new SoDrawStyle;entryLines->style=SoDrawStyle::LINES;
  after.root->insertChild(entryColor,2);after.root->insertChild(entryQuality,3);after.root->insertChild(entryLines,4);
  auto *phong=new SoLightModel;phong->model=SoLightModel::PHONG;after.group->insertChild(phong,0);
  auto *filled=new SoDrawStyle;filled->style=SoDrawStyle::FILLED;after.group->insertChild(filled,1);
  auto *quality=new SoComplexity;quality->textureQuality=.1f;after.group->insertChild(quality,2);
  auto *mapFilled=new SoDrawStyle;mapFilled->style=SoDrawStyle::FILLED;after.mapRoots[0]->insertChild(mapFilled,0);
  auto *layer=run.manager.addSuperimposition(after.root,SoRenderManager::Superimposition::AUTOREDRAW|SoRenderManager::Superimposition::ZBUFFERON);
  auto *inspection=new Inspection;run.target->getPimpl().get().backend.reset(inspection);run.scene.receiver.select(0);bool ok=true;
  for(bool visible:{false,true,false}) {
    run.style(visible,3,0xffff,{{1,0,1}});const auto stamp=run.scene.stamp(),afterStamp=after.stamp();const auto calls=inspection->calls;
    const auto status=run.adapter->render();const auto &frame=inspection->frame;const auto &graph=inspection->graph;
    std::cout<<"highlight_rtt_shadow_scope visible="<<visible<<" status="<<status<<" groups="<<frame.shadowGroups.size()<<" lights="<<frame.shadowLights.size()<<" producers="<<graph.producers.size()<<" error="<<run.adapter->getLastError().getString()<<std::endl;
    ok &= check(status==CoinRenderAction::UNSUPPORTED&&inspection->calls==calls+1&&!inspection->submits&&run.highlight.calls==0&&!run.target->getLastSubmissionSerial()&&stamp==run.scene.stamp()&&afterStamp==after.stamp(),"one composed CPU capture, no execution/source mutation");
    ok &= check(graph.producers.size()==1&&frame.shadowGroups.size()==2&&frame.shadowLights.size()==2,"later ordinary group survives highlight while RTT stays deduplicated");
    if(graph.producers.size()==1)ok &= check(graph.mode==c.route&&frame.textureSamplingPolicy==(c.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE)&&
      graph.producers[0].plan.textureSamplingPolicy==frame.textureSamplingPolicy&&graph.producers[0].sourceRevision==run.scene.texture->getNodeId(),
      "composed foreground preserves original RTT identity/route/sampling without replay pollution");
    CoinRenderShadowPlan plan;std::string error;if(!coin_render_plan_shadows(frame,plan,error)){ok &= check(false,"scope actual shadow plan: "+error);continue;}
    unsigned mainLayer=0,laterLayer=0;std::set<SbUniqueId> seenGroups;
    for(const auto &pass:plan.passes) {
      if(!pass.groupSlot||pass.groupSlot>frame.shadowGroups.size()||pass.lightSlot>=frame.shadowLights.size()){ok=false;continue;}
      const auto &group=frame.shadowGroups[pass.groupSlot-1];const auto &light=frame.shadowLights[pass.lightSlot];const Scene *owner=nullptr;
      if(group.sourceRevision==run.scene.receiver.group->getNodeId())owner=&run.scene.receiver;
      else if(group.sourceRevision==after.group->getNodeId())owner=&after;
      if(!check(owner!=nullptr,"pass belongs to real main or subsequent authorial group")){ok=false;continue;}
      seenGroups.insert(group.sourceRevision);unsigned casters=0,receivers=0;
      ok &= check(pass.visible&&pass.mapSize==256&&group.hasEntryCamera&&light.enabled&&light.shadowEligible&&light.sourceRevision==owner->lights[0]->getNodeId()&&light.customSceneNodeId==owner->mapRoots[0]->getNodeId(),"per-layer entry camera/light/map identity retained");
      for(unsigned index:pass.casterDraws) {
        if(index>=frame.draws.size()){ok=false;continue;}const auto &draw=frame.draws[index];casters+=draw.geometry.indexCount;
        ok &= check(draw.sourceNodeId==owner->casters[0]->getNodeId()&&draw.shadowLightSlot==pass.lightSlot+1,"later map consumes its own caster");
      }
      for(unsigned index:pass.receiverDraws) {
        if(index>=frame.draws.size()){ok=false;continue;}const auto &draw=frame.draws[index];receivers+=draw.geometry.indexCount;
        if(draw.renderStateSlot>=frame.renderStates.size()){ok=false;continue;}const auto &state=frame.renderStates[draw.renderStateSlot];
        ok &= check(draw.sourceNodeId==owner->mesh->getNodeId()&&!draw.shadowLightSlot&&state.lightModel==CoinRenderLightModel::PHONG&&state.shadowStyle==2&&state.hasTexture,"authorial later receiver is neither replay nor generated stroke");
        if(owner==&after){laterLayer=std::max(laterLayer,draw.renderLayer);ok &= check(draw.renderLayer>0&&state.shadowGroupSlot==pass.groupSlot,"foreground draw belongs to its actual later layer and real authorial shadow group");}
        else mainLayer=std::max(mainLayer,draw.renderLayer);
      }
      ok &= check(casters==36&&receivers==6,"both actual receiver occurrences and caster survive each layer");
    }
    ok &= check(plan.passes.size()==2&&seenGroups.size()==2&&laterLayer>mainLayer,"same-frame foreground map executes after scene/highlight purpose ends");
  }
  run.manager.removeSuperimposition(layer);return ok;
}
// Quality0 explicitly disables texture consumption. An otherwise invalid RTT
// extent must remain idle; this is not permission to accept invalid active data.
template<class Highlight> bool coreQuality(Config c) {
  c.samples=1;c.window=false;c.maps=1;Run<Highlight> run(c);
  auto *inspection=new Inspection;run.target->getPimpl().get().backend.reset(inspection);
  SoComplexity *quality=nullptr;
  for(int i=0;i<run.scene.receiver.shared->getNumChildren();++i)
    if(run.scene.receiver.shared->getChild(i)->isOfType(SoComplexity::getClassTypeId()))quality=static_cast<SoComplexity*>(run.scene.receiver.shared->getChild(i));
  if(!check(quality!=nullptr,"authorial shared quality field"))return false;
  run.scene.receiver.select(0);run.style(true,3,0xffff,{{1,0,1}});
  for(const SbVec2s extent:{SbVec2s(64,0),SbVec2s(-1,64)}) {
    quality->textureQuality=0;run.scene.texture->size=extent;const auto stamp=run.scene.stamp();
    const auto before=inspection->submits,calls=inspection->calls;const auto status=run.adapter->render();
    std::string error;const auto &frame=inspection->frame;unsigned receivers=0,strokes=0;
    bool ok=check(status==CoinRenderAction::UNSUPPORTED&&inspection->calls==calls&&inspection->submits==before+1&&
      inspection->graph.producers.empty()&&!run.target->getLastSubmissionSerial()&&run.scene.stamp()==stamp&&run.highlight.calls==0&&frame.isValid(&error),
      "disabled RTT invalid extent remains inert with valid main capture and zero producer work/query");
    for(const auto &draw:frame.draws)if(!draw.shadowLightSlot&&draw.isSceneLayer()&&draw.sourceNodeId==run.scene.receiver.mesh->getNodeId()) {
      receivers+=draw.geometry.indexCount;if(draw.renderStateSlot>=frame.renderStates.size()){ok=false;continue;}
      const auto &state=frame.renderStates[draw.renderStateSlot];
      ok &= check(!state.hasTexture&&state.lightModel==CoinRenderLightModel::PHONG&&state.shadowStyle==2,
        "quality0 receiver keeps ordinary lighting/shadow state without sampling idle RTT");
    }
    for(const auto &draw:frame.draws)if(!draw.isSceneLayer()) {
      strokes+=draw.geometry.indexCount;
      if(draw.renderStateSlot>=frame.renderStates.size()){ok=false;continue;}
      const auto &state=frame.renderStates[draw.renderStateSlot];bool textured=false;
      for(unsigned unit=0;unit<8;++unit)textured |= coin_render_texture_unit_enabled(state,unit);
      ok &= check(!draw.shadowLightSlot&&!textured&&state.lightModel==CoinRenderLightModel::BASE_COLOR,
        "idle-RTT generated stroke remains separately unlit/untextured and outside all maps");
    }
    ok &= delivery(run.scene.receiver,c,frame,true);
    CoinRenderShadowPlan plan;
    ok &= check(receivers==6&&strokes>0&&coin_render_plan_shadows(frame,plan,error)&&plan.passes.size()==1&&plan.passes[0].visible,
      "idle RTT does not suppress either receiver or its live consumer shadow map");
    if(!ok)return false;
  }
  run.scene.texture->size.setValue(64,64);quality->textureQuality=.1f;
  const auto before=inspection->calls,submits=inspection->submits;const auto stamp=run.scene.stamp();const auto status=run.adapter->render();
  return check(status==CoinRenderAction::UNSUPPORTED&&inspection->calls==before+1&&inspection->submits==submits&&
    inspection->graph.producers.size()==1&&run.scene.stamp()==stamp&&!run.target->getLastSubmissionSerial(),
    "re-enabled valid RTT restores the real shared producer on the same CPU-inspected manager")&&
    graphDelivery(run.scene,c,inspection->graph,inspection->frame,true,false);
}
template<class Highlight> bool core(Config c) {
  Run<Highlight> run(c);auto *inspection=new Inspection;run.target->getPimpl().get().backend.reset(inspection);
  for(bool selected:{false,true}) {
    if(selected){run.scene.receiver.select(0);run.highlight.setVisible(TRUE);}
    const auto stamp=run.scene.stamp();const auto previous=inspection->calls;const auto status=run.adapter->render();
    bool ok=check(status==CoinRenderAction::UNSUPPORTED&&inspection->calls==previous+1&&!inspection->submits&&!run.target->getLastSubmissionSerial()&&run.highlight.calls==0&&run.scene.stamp()==stamp,"real combined manager graph captured CPU-only before any execution");
    if(!ok)return false;if(!graphDelivery(run.scene,c,inspection->graph,inspection->frame,selected,false))return false;
  }
  // Both concrete capture plans validate before any intentionally malformed
  // consumed slot is introduced. Edited packets are unversioned IR.
  std::string diagnostic;
  if(!check(inspection->graph.validate(inspection->frame,diagnostic),"positive captured graph admission: "+diagnostic))return false;
  for(bool producer:{false,true}) {
    CoinRenderRttPlan graph=inspection->graph;CoinRenderFramePlan frame=inspection->frame;
    CoinRenderFramePlan &packet=producer?graph.producers.back().plan:frame;packet.revision=0;
    CoinRenderShadowPlan shadows;std::string error;
    if(!check(coin_render_plan_shadows(packet,shadows,error)&&!shadows.passes.empty()&&!shadows.passes.back().casterDraws.empty(),"consumed map slot starts with a real valid caster"))return false;
    const unsigned caster=shadows.passes.back().casterDraws.front();if(caster>=packet.draws.size())return false;
    packet.draws[caster].renderStateSlot=uint32_t(packet.renderStates.size());
    if(!check(!packet.isValid(&error)&&!error.empty()&&!graph.validate(frame,error),"invalid consumed root/producer slot rejected by actual Core graph admission"))return false;
  }
  auto *oldCamera=run.scene.producer.cameras.back();oldCamera->ref();auto *map=run.scene.producer.mapRoots.back();
  auto *frustum=new SoFrustumCamera;frustum->position.setValue(0,0,3);frustum->nearDistance=1;frustum->farDistance=5;
  frustum->left=frustum->bottom=-1;frustum->right=frustum->top=1;
  map->replaceChild(oldCamera,frustum);const auto before=inspection->calls;const auto stamp=run.scene.stamp();
  const auto unsupported=run.adapter->render();
  const bool rejected=check(unsupported==CoinRenderAction::UNSUPPORTED&&inspection->calls==before&&!inspection->submits&&
    !run.target->getLastSubmissionSerial()&&run.scene.stamp()==stamp&&run.adapter->getLastError().getLength()>0,
    "finite valid unqualified Frustum camera remains UNSUPPORTED, distinct from invalid authorship");
  map->replaceChild(frustum,oldCamera);oldCamera->unref();
  if(!rejected)return false;
  const auto recovered=run.adapter->render();
  return check(recovered==CoinRenderAction::UNSUPPORTED&&inspection->calls==before+1&&!inspection->submits,
    "qualified camera restoration returns to capture-only preflight without any GPU query");
}
template<class Highlight> bool gpu(Config c) {
  Run<Highlight> run(c);if(!check(run.prepare(),"actual GPU manager target"))return false;
  startup::Environment tracing("COIN_RENDER_TRACE_PHASES","1");
  if(!run.draw("ordinary combined predecessor"))return false;
  run.scene.receiver.select(0);run.visible=true;run.highlight.setVisible(TRUE);if(!run.draw("selected highlighted occurrence"))return false;
  run.scene.producer.paint(true);if(!run.draw("asymmetric producer palette mutation"))return false;run.scene.producer.paint(false);if(!run.draw("producer palette restored"))return false;
  run.scene.receiver.moveLast(true);if(!run.draw("consumer last shadow independently moved"))return false;run.scene.receiver.moveLast(false);if(!run.draw("consumer shadow restored"))return false;
  run.scene.producer.moveLast(true);if(!run.draw("producer last shadow independently moved"))return false;run.scene.producer.moveLast(false);if(!run.draw("producer shadow restored"))return false;
  const float near=run.scene.producer.cameras.back()->nearDistance.getValue();run.scene.producer.cameras.back()->nearDistance=std::numeric_limits<float>::quiet_NaN();
  if(!run.refuse("invalid consumed producer camera"))return false;run.scene.producer.cameras.back()->nearDistance=near;if(!run.draw("same manager producer-camera recovery"))return false;
  run.scene.receiver.select(1);if(!run.draw("shared source selected through second path"))return false;
  run.style(true,3,0xffff,{{0,1,1}});if(!run.draw("highlight color mutation"))return false;
  run.style(true,5,0xffff,{{1,0,1}});if(!run.draw("highlight width mutation"))return false;
  run.style(true,3,0,{{1,0,1}});if(!run.draw("empty authored stroke pattern"))return false;
  run.style(true,3,0xffff,{{1,0,1}});if(!run.draw("highlight style restoration"))return false;
  run.style(true,std::numeric_limits<float>::quiet_NaN(),0xffff,{{1,0,1}});
  if(!run.refuse("invalid highlight width"))return false;run.style(true,3,0xffff,{{1,0,1}});if(!run.draw("highlight recovery"))return false;
  run.scene.receiver.mapCameras.back()->farDistance=std::numeric_limits<float>::quiet_NaN();
  if(!run.refuse("invalid last consumer map camera"))return false;run.scene.receiver.mapCameras.back()->farDistance=float(5+.125*(c.maps-1));if(!run.draw("consumer map-camera recovery"))return false;
  run.scene.texture->size.setValue(64,0);if(!run.refuse("invalid RTT extent"))return false;run.scene.texture->size.setValue(64,64);if(!run.draw("RTT extent recovery"))return false;
  for(bool producer:{false,true}) {
    SoLight *light=producer?run.scene.producer.lights.back():run.scene.receiver.lights.back();
    const float saved=light->intensity.getValue();light->intensity=std::numeric_limits<float>::quiet_NaN();
    if(!run.refuse(producer?"invalid producer last-light intensity":"invalid consumer last-light intensity"))return false;
    light->intensity=saved;if(!run.draw("last-light exact authorial recovery"))return false;
  }
  run.scene.texture->scene=run.scene.receiver.root;
  if(!run.refuse("cyclic authored RTT source"))return false;run.scene.texture->scene=run.scene.producer.root;
  if(!run.draw("acyclic source recovery on same manager"))return false;
  if(c.window)for(unsigned extent:{129u,160u,96u})if(!run.resize(extent)||!run.draw("resized current combined scene"))return false;
  return true;
}
} // namespace highlight_rtt_shadow
int main(int argc,char **argv) {
  using namespace highlight_rtt_shadow;Config c;c.projected=true;
  for(int i=1;i<argc;++i) {
    const std::string a=argv[i];if(a=="--core-quality"){c.core=true;c.coreQuality=true;}else if(a=="--mips")c.mips=true;else if(a=="--core-scope"){c.core=true;c.coreScope=true;}else if(a=="--core")c.core=true;else if(a=="--gpu")c.core=false;else if(a=="--line")c.line=true;else if(a=="--box")c.line=false;
    else if(a=="--direct")c.route=COIN_RENDER_SCENE_TEXTURE_DIRECT;else if(a=="--staged")c.route=COIN_RENDER_SCENE_TEXTURE_STAGED;
    else if(a=="--producer-control")c.producerControl=true;else if(a=="--window")c.window=true;else if(a=="--portable-sampling")c.portable=true;else if(a=="--spot")c.spot=true;
    else if((a=="--maps"||a=="--samples")&&i+1<argc){const unsigned n=unsigned(std::strtoul(argv[++i],nullptr,10));if(a=="--maps")c.maps=n;else c.samples=n;}
    else {std::cerr<<"unknown/incomplete option "<<a<<std::endl;return 2;}
  }
  if((c.maps!=1&&c.maps!=4&&c.maps!=8)||(c.samples!=1&&c.samples!=2&&c.samples!=4))return 2;
#ifdef COIN_HIGHLIGHT_SHADOW_X11
  if(c.window)XInitThreads();
#else
  if(c.window)return 77;
#endif
  SoDB::init();SoInteraction::init();CoinRenderAction::initClass();
  if(c.coreQuality)return(c.line?coreQuality<CountAction<SoLineHighlightRenderAction>>(c):coreQuality<CountAction<SoBoxHighlightRenderAction>>(c))?0:1;
  if(c.coreScope)return(c.line?coreScope<CountAction<SoLineHighlightRenderAction>>(c):coreScope<CountAction<SoBoxHighlightRenderAction>>(c))?0:1;
  if(c.core)return(c.line?core<CountAction<SoLineHighlightRenderAction>>(c):core<CountAction<SoBoxHighlightRenderAction>>(c))?0:1;
  CoinRenderCapabilities caps{};const int status=coin_render_query_capabilities(c.window?COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW:COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps));
  std::cout<<"highlight_rtt_shadow_environment query="<<status<<" gpu="<<caps.gpu_available<<" backend="<<caps.backend<<" renderer="<<caps.renderer<<" adapter="<<caps.adapter_name<<" diagnostic="<<caps.diagnostic<<std::endl;
  if(status!=0)return 1;if(!caps.gpu_available)return 77;c.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  if(c.producerControl)return producerControl(c)?0:1;
  return(c.line?gpu<CountAction<SoLineHighlightRenderAction>>(c):gpu<CountAction<SoBoxHighlightRenderAction>>(c))?0:1;
}
