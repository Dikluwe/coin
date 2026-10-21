#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#define COIN_RENDER_SHADOW_ALPHA_FIXTURE_ONLY
#include "CoinRenderShadowMultitextureAlphaTest.cpp"
namespace {
struct C036Producer {
  SoSeparator*root=new SoSeparator;SoMaterial*material=new SoMaterial;SoSceneTexture2*texture=new SoSceneTexture2;
  const std::array<float,3>color;const float alpha;
  C036Producer(const std::array<float,3>&rgb,float opacity,int width,int height):color(rgb),alpha(opacity){
    root->ref();texture->ref();auto*camera=new SoOrthographicCamera;camera->position.setValue(0,0,2);camera->nearDistance=1;camera->farDistance=3;camera->height=2;root->addChild(camera);
    auto*model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
    auto*mode=new SoTransparencyType;mode->value=SoGLRenderAction::NONE;root->addChild(mode);
    auto*depth=new SoDepthBuffer;depth->write=TRUE;root->addChild(depth);root->addChild(material);
    material->diffuseColor.setValue(rgb[0],rgb[1],rgb[2]);material->transparency=1-opacity;
    auto*uv=new SoTextureCoordinate2;const SbVec2f st[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};uv->point.setValues(0,4,st);root->addChild(uv);
    auto*points=new SoCoordinate3;const SbVec3f p[]={SbVec3f(-2,-2,0),SbVec3f(2,-2,0),SbVec3f(2,2,0),SbVec3f(-2,2,0)};points->point.setValues(0,4,p);root->addChild(points);
    auto*face=new SoFaceSet;face->numVertices.set1Value(0,4);root->addChild(face);
    texture->scene=root;texture->size.setValue(width,height);texture->type=SoSceneTexture2::RGBA8;texture->model=SoSceneTexture2::MODULATE;
    texture->transparencyFunction=SoSceneTexture2::ALPHA_BLEND;texture->wrapS=SoSceneTexture2::REPEAT;texture->wrapT=SoSceneTexture2::CLAMP;
    texture->backgroundColor.setValue(rgb[0],rgb[1],rgb[2],opacity);
  }
  ~C036Producer(){texture->unref();root->unref();}
};
class C036Proxy:public CountedProxy {
public:mutable CoinRenderRttPlan graph;mutable CoinRenderFramePlan root;mutable unsigned graphs=0;
  explicit C036Proxy(std::unique_ptr<CoinRenderBackend>real):CountedProxy(std::move(real)){}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&g,const CoinRenderFramePlan&f,const SbVec2i32&size)const override{
    ++graphs;graph=g;root=f;return CountedProxy::preflightRtt(g,f,size);
  }
};
struct C036Scene:AlphaScene {
  C036Producer a{{{.75f,.875f,1.f}},.75f,32,32},b{{{.5f,.75f,.875f}},.5f,48,32};
  const bool nested;
  explicit C036Scene(bool chain):AlphaScene(8,8,false,1.f),nested(chain){
    receiverMaterial->diffuseColor.setValue(1,1,1);receiverMaterial->transparency=0;
    mode->value=SoGLRenderAction::SORTED_OBJECT_BLEND;
    for(int u:units){const uint8_t white[]={255,255,255,255};images[u]->image.setValue(SbVec2s(1,1),4,white);}
    stages[0]->insertChild(a.texture,2);stages[3]->insertChild(a.texture,2);stages[7]->insertChild(b.texture,2);
    b.texture->wrapS=SoSceneTexture2::CLAMP;b.texture->wrapT=SoSceneTexture2::REPEAT;
    if(nested){auto*unit=new SoTextureUnit;unit->unit=0;b.root->insertChild(unit,2);b.root->insertChild(a.texture,3);}
  }
};
std::pair<C036Proxy*,C036Proxy*>c036Install(WindowPair&h){
  const auto install=[](std::unique_ptr<CoinRenderTarget>&target,CountedProxy*&visible){auto&state=target->getPimpl().get();
    auto*proxy=new C036Proxy(std::move(state.backend));state.backend.reset(proxy);visible=proxy;return proxy;};
  return {install(h.off,h.offProxy),install(h.shown,h.windowProxy)};
}
bool c036Delivery(const C036Scene&s,const C036Proxy&p,bool direct){
  bool ok=check(p.graph.producers.size()==2,"C03.6 captures exactly two distinct producers despite shared node occurrences");
  if(p.graph.producers.size()!=2)return false;
  ok &= check(p.graph.producers[0].sourceRevision==s.a.texture->getNodeId()&&p.graph.producers[1].sourceRevision==s.b.texture->getNodeId()&&
    p.graph.producers[0].size==SbVec2i32(32,32)&&p.graph.producers[1].size==SbVec2i32(48,32),"C03.6 producer identity/order/independent extents survive capture");
  const auto receiver=[&](const CoinRenderFramePlan&frame)->const CoinRenderRenderStateSnapshot*{
    for(const auto&draw:frame.draws)if(!draw.shadowLightSlot&&draw.sourceNodeId==s.mesh->getNodeId())return &frame.renderStates[draw.renderStateSlot];return nullptr;};
  const auto*captured=receiver(p.root);const auto*resolved=receiver(p.plan);if(!check(captured&&resolved,"C03.6 captured/resolved consumer receiver exists"))return false;
  const auto capturedId=[&](int unit){const auto t=coin_render_texture_unit(*captured,size_t(unit));return t.imageSlot<p.root.textures.size()?p.root.textures[t.imageSlot].producerId:0;};
  ok &= check(capturedId(0)==1&&capturedId(3)==1&&capturedId(7)==2,"C03.6 units0/3 share producer1 while unit7 references independent producer2");
  if(s.nested){unsigned references=0;for(const auto&image:p.graph.producers[1].plan.textures)references+=image.producerId==1;
    ok &= check(references>0,"C03.6 nested producer2 references already scheduled shared producer1");}
  CoinWgpuFfiFrame ffi;std::string why;if(!ffi.prepare(p.plan,side,side,why,false))return check(false,"C03.6 resolved consumer FFI: "+why);
  const auto&view=ffi.getView();std::array<const CoinRenderTextureImageSnapshot*,8>images{};
  for(int unit:s.units){const auto t=coin_render_texture_unit(*resolved,size_t(unit));
    ok &= check(t.enabled&&t.imageSlot<p.plan.textures.size()&&t.samplerSlot<p.plan.samplers.size(),"C03.6 all eight actual consumer images/samplers remain enabled");
    if(!t.enabled||t.imageSlot>=p.plan.textures.size())continue;images[unit]=&p.plan.textures[t.imageSlot];
    if(unit!=0&&unit!=3&&unit!=7){SbVec2s size;int components=0;const auto*authored=s.images[unit]->image.getValue(size,components);
      ok &= check(components==4&&images[unit]->pixelsRgba==std::vector<uint8_t>(authored,authored+4),"C03.6 ordinary unit bytes preserve independent authored white/alpha");continue;}
    const auto&image=*images[unit];const auto&sampler=p.plan.samplers[t.samplerSlot];const bool second=unit==7;
    ok &= check(image.producerId==0&&image.width==uint32_t(second?48:32)&&image.height==32&&image.components==4&&image.sceneTransparencyFunction==SoSceneTexture2::ALPHA_BLEND&&
      image.format==CoinRenderTextureFormat::RGBA8_LINEAR&&sampler.filter==CoinRenderTextureFilter::LINEAR&&
      sampler.wrapS==(second?CoinRenderTextureWrap::CLAMP:CoinRenderTextureWrap::REPEAT)&&sampler.wrapT==(second?CoinRenderTextureWrap::REPEAT:CoinRenderTextureWrap::CLAMP),
      "C03.6 actual RTT extent/alpha/sampler is independently delivered per unit");
    if(t.imageSlot>=view.texture_count){ok=false;continue;}const auto&gpu=view.textures[t.imageSlot];
    if(direct)ok &= check(image.gpuToken!=0&&image.pixelsRgba.empty()&&gpu.format==1&&gpu.content_digest==image.gpuToken&&gpu.pixels==nullptr&&gpu.pixel_bytes_len==0&&
      bool(gpu.reserved&1u)==image.gpuOpaque,"C03.6 DIRECT exact token/opacity flag and absence of CPU substitution survive FFI");
    else ok &= check(image.gpuToken==0&&image.pixelsRgba.size()==size_t(image.width)*image.height*4&&gpu.pixels&&gpu.pixel_bytes_len==image.pixelsRgba.size()&&
      std::memcmp(gpu.pixels,image.pixelsRgba.data(),image.pixelsRgba.size())==0,"C03.6 STAGED actual producer bytes/alpha survive FFI");
  }
  if(images[0]&&images[3]&&images[7])ok &= check(direct?(images[0]->gpuToken==images[3]->gpuToken&&images[0]->gpuToken!=images[7]->gpuToken):
    (images[0]->pixelsRgba==images[3]->pixelsRgba&&images[0]->pixelsRgba!=images[7]->pixelsRgba),"C03.6 shared identity is reused while distinct producers never alias");
  ok &= check(p.root.shadowLights.size()==8&&p.plan.shadowLights.size()==8,"C03.6 real VSM map family remains complete with multiple RTT units");return ok;
}
bool c036Math(WindowPair&h,const C036Scene&s,const Frame&f){
  const auto ac=s.a.material->diffuseColor[0],bc=s.b.material->diffuseColor[0];const double aa=1-s.a.material->transparency[0],ba=1-s.b.material->transparency[0];
  const unsigned power=s.nested?3:2;const double alpha=std::pow(aa,power)*ba;unsigned checked=0,bad=0;
  for(int y=side/4;y<3*side/4;++y)for(int x=side/4;x<3*side/4;++x){const size_t p=size_t(y*side+x);++checked;
    for(int c=0;c<3;++c){const double source=std::pow(ac[c],power)*bc[c];const int expected=int(std::lround(255*source*alpha+alphaBackground[c]*(1-alpha)));
      const int actual=f.rgba[4*p+c];if(std::abs(actual-expected)>3&&bad==0)std::cout<<"c036 first_rgb x="<<x<<" y="<<y<<" channel="<<c<<" expected="<<expected<<" actual="<<actual<<" alpha="<<alpha<<'\n';
      bad+=std::abs(actual-expected)>3;bad+=std::abs(int(h.lastShown[4*p+c])-expected)>3;bad+=std::abs(int(h.lastReadback[4*p+c])-expected)>3;}
    bad+=f.depth[p]!=1.f;bad+=std::abs(int(f.rgba[4*p+3])-255)>1;}
  return check(checked>0&&bad==0,"C03.6 independent product-of-RGBA source-over equation, physical/readback RGB and preserved depth");
}
bool c036MultiRtt(WindowPair&h){
  const auto proxies=c036Install(h);bool ok=true;
  for(bool nested:{false,true}){C036Scene s(nested);if(h.off->getOptions().transparency!=COIN_RENDER_TRANSPARENCY_OBJECT)s.mode->value=SoGLRenderAction::SORTED_LAYERS_BLEND;
    const auto render=[&](Frame&f,const std::string&phase){const unsigned off=proxies.first->producers,shown=proxies.second->producers;
      const bool rendered=h.frame(s,f,phase,true,false,2);if(!rendered)return false;
      const bool direct=h.off->getOptions().sceneTexture==COIN_RENDER_SCENE_TEXTURE_DIRECT;
      return c036Delivery(s,*proxies.first,direct)&&c036Delivery(s,*proxies.second,direct)&&c036Math(h,s,f)&&
        check(proxies.first->producers==off+(direct?2:0)&&proxies.second->producers==shown+(direct?2:0),"C03.6 each distinct DIRECT producer executes once; shared references add no producer");};
    Frame a,b,c;if(!render(a,nested?"nested/shared A":"multi/shared A")){ok=false;continue;}
    for(int mutation=0;mutation<4;++mutation){
      if(mutation==0)s.a.material->diffuseColor.setValue(.375f,.875f,1);
      if(mutation==1)s.b.material->diffuseColor.setValue(.5f,.375f,.875f);
      if(mutation==2)s.a.material->transparency=.625f;
      if(mutation==3)s.b.material->transparency=.75f;
      const bool changed=render(b,"independent producer color/alpha B "+std::to_string(mutation));ok &= changed;
      if(changed)ok &= check(alphaChanges(a,b)>0,"C03.6 every producer color/alpha affects the consumer independently");
      s.a.material->diffuseColor.setValue(.75f,.875f,1);s.b.material->diffuseColor.setValue(.5f,.75f,.875f);s.a.material->transparency=.25f;s.b.material->transparency=.5f;
      const bool restored=render(c,"multi producer A restored");ok &= restored;if(restored)ok &= exact(a,c);
    }
  }return ok;
}
}
#include "CoinRenderShadowRttDepthControls.h"
#include "CoinRenderShadowRttGroups.h"
#include "CoinRenderShadowRttPeer.h"
#include "CoinRenderShadowRttBudget.h"
#include "CoinRenderShadowRttAtomic.h"
#include "CoinRenderShadowRttModes.h"
#include "CoinRenderShadowRttPayload.h"
int main(int argc,char**argv){
  configureSamplingTest(argc,argv);CoinRenderTransparencyMode mechanism=COIN_RENDER_TRANSPARENCY_OBJECT;CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED;bool multi=false,depth=false,groups=false,budget=false,atomic=false,depthDiagnostic=false,modes=false,capturePayload=false;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--portable-sampling")continue;if(arg=="--multi-rtt"){multi=true;continue;}if(arg=="--depth"){depth=true;continue;}if(arg=="--depth-diagnostic"){depth=true;depthDiagnostic=true;continue;}if(arg=="--groups"){groups=true;continue;}if(arg=="--heterogeneous-budget"){budget=true;continue;}if(arg=="--atomic"){atomic=true;continue;}if(arg=="--object-depth-modes"){modes=true;continue;}if(arg=="--capture-payload"){capturePayload=true;continue;}
    if(arg=="--rtt-staged"||arg=="--rtt-direct"){route=arg=="--rtt-direct"?COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED;continue;}
    if(arg=="--samples"){if(++i==argc)return 2;const std::string n=argv[i];if(n!="1"&&n!="2"&&n!="4")return 2;windowSamples=unsigned(std::stoi(n));continue;}
    if(arg=="--mechanism"){if(++i==argc)return 2;const std::string n=argv[i];if(n=="object")mechanism=COIN_RENDER_TRANSPARENCY_OBJECT;else if(n=="peeling")mechanism=COIN_RENDER_TRANSPARENCY_PEELING;else if(n=="weighted")mechanism=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;else return 2;continue;}return 2;}
  if(!multi&&!depth&&!groups&&!budget&&!atomic&&!modes&&!capturePayload)return 2;alphaRtt=false;if(capturePayload){SoDB::init();CoinRenderAction::initClass();return c036CapturePayload()?0:1;}if(!XInitThreads())return 1;SoDB::init();CoinRenderAction::initClass();const auto caps=msaa_shadow_test::capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW);
  if(!caps.gpu_available||caps.renderer==COIN_RENDER_RENDERER_UNKNOWN)return 77;WindowPair h(caps,0,nullptr,mechanism,route);if(!h.ready)return 1;
  h.offAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));h.windowAction.setBackgroundColor(SbColor4f(16.f/255,24.f/255,32.f/255,1));h.offAction.setSortedLayersNumPasses(4);h.windowAction.setSortedLayersNumPasses(4);
  bool ok=true;if(multi)ok &= c036MultiRtt(h);if(depth)ok &= c036Depth(h,depthDiagnostic);if(groups)ok &= c036RttGroups(h);if(budget)ok &= c036HeterogeneousBudget(h);if(atomic)ok &= c036MultiRttAtomic(h);if(modes)ok &= c036ObjectDepthModes(h);return ok?0:1;
}
