#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#define COIN_RENDER_SHADOW_MULTITEXTURE_FIXTURE_ONLY
#include "CoinRenderShadowMultitextureTest.cpp"
#include "rendering/coinrender/CoinRenderComposition.h"
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoTransparencyType.h>

namespace {
unsigned additiveSamples=1;int additiveRttMode=-1;uint32_t additiveBackendKind=0;
struct AdditiveLayer {
  SoSwitch* visible=new SoSwitch;
  SoMaterial* material=new SoMaterial;
  SoTransparencyType* mode=new SoTransparencyType;
  SoFaceSet* shape=new SoFaceSet;
  SoCoordinate3* points=new SoCoordinate3;
  AdditiveLayer(float z,const SbColor&color,float transparency,int type,bool write){
    visible->whichChild=SO_SWITCH_ALL;
    auto*state=new SoSeparator;visible->addChild(state);
    auto*style=new SoShadowStyle;style->style=SoShadowStyle::NO_SHADOWING;state->addChild(style);
    auto*model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;state->addChild(model);
    mode->value=type;state->addChild(mode);
    auto*depth=new SoDepthBuffer;depth->write=write;depth->function=SoDepthBuffer::LEQUAL;state->addChild(depth);
    material->diffuseColor=color;material->transparency=transparency;state->addChild(material);
    const SbVec3f quad[]={SbVec3f(-1.9f,-1.9f,z),SbVec3f(1.9f,-1.9f,z),SbVec3f(1.9f,1.9f,z),SbVec3f(-1.9f,1.9f,z)};
    points->point.setValues(0,4,quad);state->addChild(points);shape->numVertices.set1Value(0,4);state->addChild(shape);
  }
};
struct AdditiveScene:msaa_shadow_test::Scene {
  AdditiveLayer background{-2.f,SbColor(.125f,.1875f,.25f),0.f,SoGLRenderAction::NONE,true};
  AdditiveLayer over{-1.25f,SbColor(.25f,.125f,.0625f),.5f,SoGLRenderAction::SORTED_OBJECT_BLEND,false};
  SoSwitch* additive=new SoSwitch;
  SoTransparencyType* mode=new SoTransparencyType;
  SoLightModel* model=new SoLightModel;
  SoTranslation* position=new SoTranslation;
  SoShadowStyle* style=nullptr;
  SoNode* shape=nullptr;
  explicit AdditiveScene(int maps):msaa_shadow_test::Scene(maps,false){
    root->insertChild(background.visible,2);
    style=static_cast<SoShadowStyle*>(receiver->getChild(0));
    shape=receiver->getChild(receiver->getNumChildren()-1);
    auto*depth=new SoDepthBuffer;depth->write=FALSE;depth->function=SoDepthBuffer::LEQUAL;
    mode->value=SoGLRenderAction::DELAYED_ADD;model->model=SoLightModel::BASE_COLOR;
    receiver->insertChild(position,0);receiver->insertChild(depth,0);receiver->insertChild(mode,0);receiver->insertChild(model,0);
    receiverMaterial->diffuseColor.setValue(.25f,.1875f,.125f);
    receiverMaterial->transparency=.75f;receiverMaterial->ambientColor.setValue(.04f,.04f,.04f);
    style->style=SoShadowStyle::NO_SHADOWING;
    receiver->ref();group->removeChild(receiver);additive->whichChild=SO_SWITCH_ALL;
    additive->addChild(receiver);group->addChild(over.visible);group->addChild(additive);receiver->unref();
  }
  void sourceOnly(bool only,float alpha=.25f){
    background.visible->whichChild=over.visible->whichChild=only?SO_SWITCH_NONE:SO_SWITCH_ALL;
    additive->whichChild=SO_SWITCH_ALL;
    mode->value=only?SoGLRenderAction::NONE:SoGLRenderAction::DELAYED_ADD;
    receiverMaterial->transparency=only?0.f:1.f-alpha;
  }
  void authorAdditiveFirst(bool first){
    additive->ref();group->removeChild(additive);
    if(first)group->insertChild(additive,slots);else group->addChild(additive);
    additive->unref();
  }
};
class AdditiveProxy:public Proxy {
public:
  unsigned rootSubmits=0;mutable unsigned graphs=0;
  mutable CoinRenderRttPlan graph;mutable CoinRenderFramePlan capturedRoot;
  explicit AdditiveProxy(std::unique_ptr<CoinRenderBackend>backend):Proxy(std::move(backend)){}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t)override{++rootSubmits;return Proxy::submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t,const CoinRenderFrameReuseDecision&r)override{++rootSubmits;return Proxy::submit(f,t,r);}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&g,const CoinRenderFramePlan&f,const SbVec2i32&extent)const override{
    ++graphs;graph=g;capturedRoot=f;return Proxy::preflightRtt(g,f,extent);
  }
};
SoSeparator*additiveConsumer(SoSeparator*producerRoot){
  auto*root=new SoSeparator;root->ref();auto*camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,6);camera->height=4;camera->nearDistance=1;camera->farDistance=20;root->addChild(camera);
  auto*model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
  auto*mode=new SoTransparencyType;mode->value=SoGLRenderAction::NONE;root->addChild(mode);
  auto*depth=new SoDepthBuffer;depth->write=TRUE;depth->function=SoDepthBuffer::LEQUAL;root->addChild(depth);
  auto*quality=new SoComplexity;quality->textureQuality=.1f;root->addChild(quality);
  auto*texture=new SoSceneTexture2;texture->scene=producerRoot;texture->size.setValue(side,side);
  texture->type=SoSceneTexture2::RGBA8;texture->model=SoSceneTexture2::REPLACE;
  texture->wrapS=texture->wrapT=SoSceneTexture2::CLAMP;texture->transparencyFunction=SoSceneTexture2::NONE;
  texture->backgroundColor.setValue(.125f,.1875f,.25f,1.f);root->addChild(texture);
  auto*uv=new SoTextureCoordinate2;const SbVec2f st[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};
  uv->point.setValues(0,4,st);root->addChild(uv);
  auto*points=new SoCoordinate3;const SbVec3f quad[]={SbVec3f(-2,-2,0),SbVec3f(2,-2,0),SbVec3f(2,2,0),SbVec3f(-2,2,0)};
  points->point.setValues(0,4,quad);root->addChild(points);auto*shape=new SoFaceSet;shape->numVertices.set1Value(0,4);root->addChild(shape);
  return root;
}
struct AdditiveHarness {
  std::unique_ptr<CoinRenderTarget> target;AdditiveProxy*proxy=nullptr;
  CoinRenderAction action{SbViewportRegion(side,side)};uint64_t serial=0;
  explicit AdditiveHarness(CoinRenderTransparencyMode mechanism){
    auto options=samplingTestOptions();options.transparency=mechanism;options.sampleCount=additiveSamples;
    if(additiveRttMode>=0)options.sceneTexture=additiveRttMode?COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED;
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    if(!target)return;auto&state=target->getPimpl().get();
    if(state.prepareBackend()!=CoinRenderBackendStatus::SUCCESS||!state.backend||!state.backend->isGpuBackend()){target.reset();return;}
    proxy=new AdditiveProxy(std::move(state.backend));state.backend.reset(proxy);
    target->setDepthReadbackEnabled(TRUE);action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(0,0,0,0));
  }
  ~AdditiveHarness(){action.setRenderTarget(nullptr);}
  bool frame(AdditiveScene&s,Frame&f,const std::string&phase){
    if(!target)return check(false,"additive composition target prepared");
    const unsigned beforeRoots=proxy->rootSubmits,beforeProducers=proxy->producers,beforeGraphs=proxy->graphs;
    SoSeparator*consumer=additiveRttMode>=0?additiveConsumer(s.root):nullptr;
    action.apply(consumer?consumer:s.root);target->readbackRGBA(f.rgba);target->readbackDepth(f.depth);f.serial=target->getLastSubmissionSerial();
    if(consumer)consumer->unref();
    const uint64_t step=additiveRttMode>=0&&(additiveRttMode==1||additiveBackendKind==COIN_RENDER_EXPERIMENTAL_RUST)?2:1;
    const bool ok=target->getOptions().sampleCount==additiveSamples&&action.getLastStatus()==CoinRenderAction::SUCCESS&&f.rgba.size()==size_t(side)*side*4&&
      f.depth.size()==size_t(side)*side&&f.serial>0&&(!serial||f.serial==serial+step);
    bool graphOk=true;
    if(additiveRttMode>=0&&ok){
      const auto&g=proxy->graph;graphOk=proxy->graphs>beforeGraphs&&g.producers.size()==1&&
        proxy->rootSubmits==beforeRoots+1&&proxy->producers==beforeProducers+(additiveRttMode==1?1:0);
      if(g.producers.size()==1){const auto&producer=g.producers[0];
        graphOk &= producer.size==SbVec2i32(side,side)&&producer.format==CoinRenderTextureFormat::RGBA8_LINEAR&&
          producer.plan.clearColor[3]==1.f&&producer.plan.shadowLights.size()==size_t(s.slots)&&
          producer.plan.textureSamplingPolicy==samplingTestPolicy&&proxy->capturedRoot.textureSamplingPolicy==samplingTestPolicy;
        for(const auto&light:producer.plan.shadowLights)graphOk &= light.enabled&&light.shadowEligible&&light.mapSceneCaptured;
      }
      bool rootReplace=false;for(const auto&draw:proxy->capturedRoot.draws){if(draw.shadowLightSlot||draw.renderStateSlot>=proxy->capturedRoot.renderStates.size())continue;
        const auto&state=proxy->capturedRoot.renderStates[draw.renderStateSlot];
        rootReplace |= state.hasTexture&&state.textureModel==CoinRenderTextureModel::REPLACE&&state.depthWrite;
      }
      graphOk &= rootReplace;
      std::cout<<"shadow_additive_rtt route="<<additiveRttMode<<" graphs="<<proxy->graphs-beforeGraphs
        <<" producers_declared="<<g.producers.size()<<" direct_producers="<<proxy->producers-beforeProducers
        <<" root_submits="<<proxy->rootSubmits-beforeRoots<<" serial_step="<<step<<" graph_ok="<<graphOk<<'\n';
    }
    std::cout<<"shadow_additive maps="<<s.slots<<" samples="<<additiveSamples<<" mechanism="<<target->getOptions().transparency<<" phase="<<phase
      <<" serial="<<serial<<','<<f.serial<<" status="<<int(action.getLastStatus())<<" error="<<action.getLastError().getString()<<'\n';
    serial=f.serial;return check(ok&&graphOk,"current RGBA/depth/serial publication and declared RTT work: "+phase);
  }
};
// Linear UNORM8 rounds at the destination/compositor and final blend; the
// independently read source also has at most half a byte of quantization,
// multiplied by alpha<=.5. Two byte steps bound these attachment roundings;
// this is stricter than the existing three-step generic composition oracle.
constexpr int colorQuantization=2;
bool knownDestination(const Frame&f){unsigned checked=0,bad=0;
  const double background[]={.125,.1875,.25},source[]={.25,.125,.0625};
  for(int y=20;y<108;++y)for(int x=20;x<108;++x){const size_t p=size_t(y*side+x);
    for(int c=0;c<3;++c)bad+=std::abs(int(f.rgba[4*p+c])-int(std::lround(255.*(.5*source[c]+.5*background[c]))))>colorQuantization;
    bad+=f.rgba[4*p+3]!=255;
    bad+=!std::isfinite(f.depth[p])||std::abs(double(f.depth[p])-7./19.)>2./16777215.;++checked;
  }
  return check(checked>2000&&bad==0,"non-saturated source-over destination follows known linear color equation");
}
bool additiveEquation(const Frame&destination,const Frame&source,const Frame&actual,double alpha,double surviving=1.){
  unsigned checked=0,bad=0,saturated=0;int maximum=0;size_t first=SIZE_MAX;
  if(!check(destination.rgba.size()==actual.rgba.size()&&source.rgba.size()==actual.rgba.size()&&destination.depth.size()==actual.depth.size(),"additive equation has complete buffers"))return false;
  for(int y=12;y<116;++y)for(int x=12;x<116;++x){const size_t p=size_t(y*side+x);if(source.rgba[4*p+3]!=255)continue;
    for(int c=0;c<3;++c){const double expected=destination.rgba[4*p+c]+alpha*surviving*source.rgba[4*p+c];
      saturated+=expected>=255.;const int delta=std::abs(int(actual.rgba[4*p+c])-int(std::lround(std::min(255.,expected))));
      maximum=std::max(maximum,delta);if(delta>colorQuantization){++bad;if(first==SIZE_MAX)first=p;}}
    bad+=actual.rgba[4*p+3]!=255;bad+=actual.depth[p]!=destination.depth[p];++checked;
  }
  std::cout<<"shadow_additive_equation alpha="<<alpha<<" surviving="<<surviving<<" checked="<<checked<<" bad="<<bad<<" saturated="<<saturated<<" max_byte_error="<<maximum;
  if(first!=SIZE_MAX)std::cout<<" first="<<first%side<<','<<first/side;std::cout<<'\n';
  return check(checked>2000&&bad==0&&saturated==0,"additive contribution obeys non-saturated color math and preserves destination depth");
}
bool additiveSampleCoverage(const Frame&f){
  unsigned quarter=0,half=0,threeQuarter=0;bool valid=true;
  for(size_t p=3;p<f.rgba.size();p+=4){const int alpha=f.rgba[p];if(alpha==0||alpha==255)continue;
    const int k=int(std::lround(double(alpha)*additiveSamples/255.));
    valid &= k>0&&k<int(additiveSamples)&&std::abs(alpha-int(std::lround(255.*k/additiveSamples)))<=2;
    quarter+=additiveSamples==4&&k==1;half+=2*k==int(additiveSamples);threeQuarter+=additiveSamples==4&&k==3;
  }
  std::cout<<"shadow_additive_coverage samples="<<additiveSamples<<" quarter="<<quarter<<" half="<<half<<" three_quarter="<<threeQuarter<<'\n';
  return check(valid&&(additiveSamples==1?quarter+half+threeQuarter==0:
    additiveSamples==2?half>0:quarter>0&&threeQuarter>0),
    "actual resolved silhouette distinguishes requested 1/2/4 samples without a universal sample pattern");
}
bool additiveDelivery(const AdditiveScene&s,const CoinRenderFramePlan&plan,bool before){
  std::vector<CoinRenderCompositionItem>order;std::string diagnostic;
  if(!check(coin_render_composition_order(plan,order,diagnostic),"additive composition schedule: "+diagnostic))return false;
  CoinWgpuFfiFrame ffi;if(!check(ffi.prepare(plan,side,side,diagnostic,false),"additive GPU connector: "+diagnostic))return false;
  size_t firstAdd=SIZE_MAX,lastAdd=0,firstOver=SIZE_MAX,lastOver=0;unsigned additives=0,overs=0;bool ok=true;
  for(size_t i=0;i<order.size();++i){const auto&item=order[i];const auto&draw=plan.draws[item.drawIndex];if(draw.shadowLightSlot)continue;
    const bool add=draw.sourceNodeId==s.shape->getNodeId();const bool over=draw.sourceNodeId==s.over.shape->getNodeId();
    if(!add&&!over)continue;
    if(!check(draw.renderStateSlot<plan.renderStates.size(),"composition delivery state exists")){ok=false;continue;}
    const auto&state=plan.renderStates[draw.renderStateSlot];
    if(add){++additives;firstAdd=std::min(firstAdd,i);lastAdd=i;
      ok &= check(state.transparencyType==s.mode->value.getValue()&&item.blend&&item.additive&&
        item.deferred==(s.mode->value.getValue()!=SoGLRenderAction::ADD)&&!item.depthWrite&&
        state.materialSlot<plan.materials.size()&&plan.materials[state.materialSlot].transparency==s.receiverMaterial->transparency[0],
        "additive Coin mode, alpha, classification and depth policy survive capture");
      bool packed=false;for(size_t j=0;j<ffi.getView().draw_count;++j){const auto&gpu=ffi.getView().draws[j];
        if(gpu.stable_node_id==draw.stableNodeId&&gpu.render_state_slot==draw.renderStateSlot){packed=true;ok &= check((gpu.composition_flags&3u)==3u,"GPU connector retains blend plus additive flags");}}
      ok &= check(packed,"GPU connector includes authored additive receiver");
    }else{++overs;firstOver=std::min(firstOver,i);lastOver=i;
      ok &= check(item.blend&&!item.additive&&item.deferred&&item.sortObject&&
        item.transparencyStrategy!=CoinRenderCompositionItem::OBJECT,"source-over retains selected compositor and sorted-object traversal");}
  }
  ok &= check(additives>0&&overs>0&&(before?lastAdd<firstOver:lastOver<firstAdd),"noncommutative additive/compositor order is delivered explicitly");
  ok &= check(plan.shadowLights.size()==size_t(s.slots),"all authored shadow maps remain in additive composition delivery");
  return ok;
}
bool interleavedAdditive(AdditiveHarness&h,int maps){
  AdditiveScene s(maps);
  AdditiveLayer secondAdd{-1.1f,SbColor(.0625f,.25f,.1875f),.5f,SoGLRenderAction::SORTED_OBJECT_ADD,false};
  AdditiveLayer secondOver{-.9f,SbColor(.125f,.0625f,.25f),.75f,SoGLRenderAction::SORTED_OBJECT_BLEND,false};
  s.mode->value=SoGLRenderAction::SORTED_OBJECT_ADD;
  s.position->translation.setValue(0,0,-.6f);
  s.group->addChild(secondAdd.visible);s.group->addChild(secondOver.visible);
  AdditiveLayer occluder{-.5f,SbColor(.0625f,.375f,.125f),0.f,SoGLRenderAction::NONE,true};
  const SbVec3f frontQuad[]={SbVec3f(-1.9f,-1.9f,-.5f),SbVec3f(-.25f,-1.9f,-.5f),SbVec3f(-.25f,1.9f,-.5f),SbVec3f(-1.9f,1.9f,-.5f)};
  occluder.points->point.setValues(0,4,frontQuad);occluder.visible->whichChild=SO_SWITCH_NONE;s.group->addChild(occluder.visible);
  const auto delivery=[&](bool originalAddLast){
    const auto&plan=additiveRttMode>=0?h.proxy->graph.producers[0].plan:h.proxy->plan;
    std::vector<CoinRenderCompositionItem>order;std::string diagnostic;
    if(!check(coin_render_composition_order(plan,order,diagnostic),"alternating composition schedule: "+diagnostic))return false;
    const auto firstAdd=s.shape->getNodeId(),firstOver=s.over.shape->getNodeId();
    const auto nextAdd=secondAdd.shape->getNodeId(),nextOver=secondOver.shape->getNodeId();
    std::vector<SbUniqueId>actual;bool valid=true;
    for(const auto&item:order){const auto&draw=plan.draws[item.drawIndex];const auto node=draw.sourceNodeId;
      if(draw.shadowLightSlot||(node!=firstAdd&&node!=firstOver&&node!=nextAdd&&node!=nextOver))continue;
      const bool additive=node==firstAdd||node==nextAdd;
      valid &= item.blend&&item.deferred&&item.sortObject&&item.additive==additive&&!item.depthWrite;
      if(!additive)valid &= item.transparencyStrategy!=CoinRenderCompositionItem::OBJECT;
      if(actual.empty()||actual.back()!=node)actual.push_back(node);
    }
    const std::vector<SbUniqueId>expected=originalAddLast?std::vector<SbUniqueId>{firstOver,nextAdd,nextOver,firstAdd}:
      std::vector<SbUniqueId>{firstAdd,firstOver,nextAdd,nextOver};
    return check(valid&&actual==expected&&plan.shadowLights.size()==size_t(maps),"four alternating additive/source-over layers retain exact sorted depth order, maps and modality");
  };
  const auto equation=[&](const Frame&f,bool originalAddLast,bool withOccluder=false){
    double rgb[]={.125,.1875,.25};const double add1[]={.25,.1875,.125};
    const double over1[]={.25,.125,.0625},add2[]={.0625,.25,.1875},over2[]={.125,.0625,.25};
    for(int c=0;c<3;++c){
      if(!originalAddLast)rgb[c]+=.25*add1[c];
      rgb[c]=.5*over1[c]+.5*rgb[c];rgb[c]+=.5*add2[c];
      rgb[c]=.25*over2[c]+.75*rgb[c];
      if(originalAddLast)rgb[c]+=.25*add1[c];
    }
    unsigned checked=0,bad=0,front=0,behind=0;int maximum=0;const double frontColor[]={.0625,.375,.125};
    for(int y=20;y<108;++y)for(int x=20;x<108;++x){const size_t p=size_t(y*side+x);
      const double left=4.*x/side-2.,right=4.*(x+1)/side-2.;
      if(withOccluder&&left<=-.25&&right>=-.25)continue;
      const bool covered=withOccluder&&right<-.25;front+=covered;behind+=!covered;
      for(int c=0;c<3;++c){const double expected=covered?frontColor[c]:rgb[c];
        const int delta=std::abs(int(f.rgba[4*p+c])-int(std::lround(255.*expected)));
        maximum=std::max(maximum,delta);bad+=delta>colorQuantization;bad+=expected>=1.;}
      bad+=f.rgba[4*p+3]!=255;
      // RTT publishes this independently authored consumer plane's depth,
      // never the producer background or foreground depths.
      const double expectedDepth=additiveRttMode>=0?5./19.:(covered?5.5/19.:7./19.);
      bad+=!std::isfinite(f.depth[p])||std::abs(double(f.depth[p])-expectedDepth)>2./16777215.;++checked;
    }
    std::cout<<"shadow_additive_interleaved maps="<<maps<<" original_add_last="<<originalAddLast
      <<" expected="<<255.*rgb[0]<<','<<255.*rgb[1]<<','<<255.*rgb[2]
      <<" occluder="<<withOccluder<<" front="<<front<<" behind="<<behind
      <<" checked="<<checked<<" bad="<<bad<<" max_byte_error="<<maximum<<'\n';
    return check(checked>2000&&bad==0&&(!withOccluder||(front>0&&behind>0)),"alternating color equation and opaque depth match independent non-saturated arithmetic");
  };
  Frame a,permuted,moved,back;
  if(!h.frame(s,a,"alternating sorted add/over/add/over A"))return false;
  bool ok=equation(a,false)&&delivery(false);
  // Reverse authoring order while keeping every light before its receivers.
  std::vector<SoNode*>layers={s.additive,s.over.visible,secondAdd.visible,secondOver.visible};
  for(auto*layer:layers)layer->ref();for(auto*layer:layers)s.group->removeChild(layer);
  for(auto i=layers.rbegin();i!=layers.rend();++i)s.group->addChild(*i);
  for(auto*layer:layers)layer->unref();
  if(!h.frame(s,permuted,"alternating layers authored in reverse B"))return false;
  ok &= equation(permuted,false)&&delivery(false)&&exact(a,permuted);
  s.position->translation.setValue(0,0,.3f);
  if(!h.frame(s,moved,"original additive moved in front of both compositors C"))return false;
  ok &= equation(moved,true)&&delivery(true)&&check(difference(a,moved)>0,"Z permutation changes the noncommutative composition result");
  s.position->translation.setValue(0,0,-.6f);
  if(!h.frame(s,back,"alternating sorted depths restored A"))return false;
  ok &= equation(back,false)&&delivery(false)&&exact(a,back);
  Frame occluded,uncovered,frontRestored;occluder.visible->whichChild=SO_SWITCH_ALL;
  if(!h.frame(s,occluded,"opaque foreground blocks all alternating transparent layers A"))return false;
  ok &= equation(occluded,false,true)&&delivery(false);
  occluder.visible->whichChild=SO_SWITCH_NONE;
  if(!h.frame(s,uncovered,"opaque foreground disabled B"))return false;
  ok &= equation(uncovered,false)&&exact(a,uncovered);
  occluder.visible->whichChild=SO_SWITCH_ALL;
  if(!h.frame(s,frontRestored,"opaque foreground restored A"))return false;
  ok &= equation(frontRestored,false,true)&&exact(occluded,frontRestored);
  if(additiveRttMode>=0){
    // The producer has opaque alpha, so sample count must be observed through
    // color coverage, not alpha. A tilted opaque edge mixes two known colors.
    const SbVec3f tilted[]={SbVec3f(-1.9f,-1.9f,-.5f),SbVec3f(-.35f,-1.9f,-.5f),SbVec3f(-.15f,1.9f,-.5f),SbVec3f(-1.9f,1.9f,-.5f)};
    occluder.points->point.setValues(0,4,tilted);Frame coverage;
    if(!h.frame(s,coverage,"RTT producer effective sample-count color coverage"))return false;
    const double behind[]={.21875,.244140625,.26171875},front[]={.0625,.375,.125};
    unsigned quarter=0,half=0,threeQuarter=0,bad=0,frontPixels=0,backPixels=0;
    for(int y=20;y<108;++y)for(int x=20;x<108;++x){const size_t p=size_t(y*side+x);int best=-1,minimum=256;
      for(unsigned k=0;k<=additiveSamples;++k){int maximum=0;
        for(int c=0;c<3;++c){const double rgb=(k*front[c]+(additiveSamples-k)*behind[c])/additiveSamples;
          maximum=std::max(maximum,std::abs(int(coverage.rgba[4*p+c])-int(std::lround(255.*rgb))));}
        if(maximum<minimum){minimum=maximum;best=int(k);}}
      bad+=minimum>colorQuantization;bad+=coverage.rgba[4*p+3]!=255;
      bad+=!std::isfinite(coverage.depth[p])||std::abs(double(coverage.depth[p])-5./19.)>2./16777215.;
      frontPixels+=best==int(additiveSamples);backPixels+=best==0;
      quarter+=additiveSamples==4&&best==1;half+=2*best==int(additiveSamples);threeQuarter+=additiveSamples==4&&best==3;
    }
    std::cout<<"shadow_additive_rtt_coverage samples="<<additiveSamples<<" quarter="<<quarter<<" half="<<half
      <<" three_quarter="<<threeQuarter<<" bad="<<bad<<'\n';
    ok &= check(bad==0&&frontPixels>0&&backPixels>0&&(additiveSamples==1?quarter+half+threeQuarter==0:
      additiveSamples==2?half>0:quarter>0&&threeQuarter>0),
      "opaque RTT producer has real requested k/N color coverage and consumer geometric depth");
    ok &= delivery(false);
  }
  return ok;
}
bool additiveCase(AdditiveHarness&h,int maps){
  AdditiveScene s(maps);Frame destination,source,delayed,back,immediate,zero,half;bool ok=true;
  s.additive->whichChild=SO_SWITCH_NONE;
  if(!h.frame(s,destination,"known destination without additive"))return false;ok &= knownDestination(destination);
  s.sourceOnly(true);if(!h.frame(s,source,"opaque constant source control"))return false;
  ok &= expectedColor(source,{{64,48,32}})&&additiveSampleCoverage(source);
  s.sourceOnly(false);if(!h.frame(s,delayed,"delayed additive after compositor"))return false;
  ok &= additiveEquation(destination,source,delayed,.25)&&additiveDelivery(s,h.proxy->plan,false);
  s.receiverMaterial->transparency=1.f;if(!h.frame(s,zero,"zero-alpha negative contribution"))return false;
  ok &= exact(destination,zero);
  s.receiverMaterial->transparency=.5f;if(!h.frame(s,half,"double additive opacity"))return false;
  ok &= additiveEquation(destination,source,half,.5);
  s.receiverMaterial->transparency=.75f;if(!h.frame(s,back,"additive opacity A restored"))return false;ok &= exact(delayed,back);
  s.mode->value=SoGLRenderAction::ADD;if(!h.frame(s,immediate,"immediate additive before deferred source-over"))return false;
  ok &= additiveEquation(destination,source,immediate,.25,.5)&&additiveDelivery(s,h.proxy->plan,true)&&check(difference(delayed,immediate)>0,"known source-over attenuates an earlier additive contribution");
  s.mode->value=SoGLRenderAction::DELAYED_ADD;s.authorAdditiveFirst(true);
  if(!h.frame(s,back,"authored permutation retains sorted then unsorted delayed order"))return false;
  ok &= exact(delayed,back)&&additiveDelivery(s,h.proxy->plan,false);s.authorAdditiveFirst(false);
  s.mode->value=SoGLRenderAction::SORTED_OBJECT_ADD;
  if(!h.frame(s,back,"sorted additive in front of source-over"))return false;ok &= additiveEquation(destination,source,back,.25)&&additiveDelivery(s,h.proxy->plan,false);
  s.position->translation.setValue(0,0,-.5f);
  if(!h.frame(s,back,"sorted additive behind source-over"))return false;ok &= additiveEquation(destination,source,back,.25,.5)&&additiveDelivery(s,h.proxy->plan,true);
  s.position->translation.setValue(0,0,0);s.mode->value=SoGLRenderAction::DELAYED_ADD;
  if(!h.frame(s,back,"mode and sorted depth A restored"))return false;ok &= exact(delayed,back);
  // The lighting control supplies an independently captured opaque source.
  // Only alpha/composition changes; the shader still receives every real map.
  s.model->model=SoLightModel::PHONG;s.style->style=SoShadowStyle::SHADOWED;
  s.receiverMaterial->diffuseColor.setValue(.8f,.6f,.4f);
  s.sourceOnly(true,.5f);Frame shadowSource,clearSource,shadowed,clear;
  if(!h.frame(s,shadowSource,"opaque shadowed lighting source"))return false;
  s.sourceOnly(false,.5f);if(!h.frame(s,shadowed,"shadowed additive contribution"))return false;
  ok &= additiveEquation(destination,shadowSource,shadowed,.5)&&additiveDelivery(s,h.proxy->plan,false);
  s.style->style=SoShadowStyle::NO_SHADOWING;s.sourceOnly(true,.5f);
  if(!h.frame(s,clearSource,"opaque unshadowed lighting source"))return false;
  s.sourceOnly(false,.5f);if(!h.frame(s,clear,"unshadowed additive contribution"))return false;
  ok &= additiveEquation(destination,clearSource,clear,.5)&&check(difference(shadowed,clear)>0,"real shadow maps visibly modulate additive contribution");
  s.style->style=SoShadowStyle::SHADOWED;
  for(int map=0;map<maps;++map){const float x=s.casterPositions[size_t(map)]->translation.getValue()[0];s.casterX(map,x+.65f);
    s.sourceOnly(true,.5f);Frame movedSource,moved;
    if(!h.frame(s,movedSource,"independent map source "+std::to_string(map)))return false;
    s.sourceOnly(false,.5f);if(!h.frame(s,moved,"independent map additive "+std::to_string(map)))return false;
    ok &= additiveEquation(destination,movedSource,moved,.5)&&check(difference(shadowed,moved)>0,"every authored map contributes to additive receiver");
    s.casterX(map,x);s.sourceOnly(true,.5f);
    if(!h.frame(s,back,"map source restored A"))return false;ok &= exact(shadowSource,back);
    s.sourceOnly(false,.5f);if(!h.frame(s,back,"map additive restored A"))return false;ok &= exact(shadowed,back);
  }
  ok &= interleavedAdditive(h,maps);
  return ok;
}
}
int main(int argc,char**argv){configureSamplingTest(argc,argv);int mechanism=0,selectedMaps=0;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--portable-sampling"||arg=="--campaign")continue;
    if(arg=="--mechanism"&&i+1<argc){mechanism=std::atoi(argv[++i]);if(mechanism!=1&&mechanism!=2)return 2;}
    else if(arg=="--maps"&&i+1<argc){selectedMaps=std::atoi(argv[++i]);if(selectedMaps!=1&&selectedMaps!=2&&selectedMaps!=4&&selectedMaps!=8)return 2;}
    else if(arg=="--samples"&&i+1<argc){const std::string count=argv[++i];if(count!="1"&&count!="2"&&count!="4")return 2;additiveSamples=unsigned(std::stoi(count));}
    else if(arg=="--rtt-staged"||arg=="--rtt-direct"){if(additiveRttMode>=0)return 2;additiveRttMode=arg=="--rtt-direct"?1:0;}else return 2;}
  SoDB::init();CoinRenderAction::initClass();const auto caps=msaa_shadow_test::capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if(!caps.gpu_available)return 77;additiveBackendKind=caps.backend;
  // Prepared targets coexist so numerical qualification does not repeatedly
  // destroy/recreate the shared GPU runtime. Lifecycle has separate tests.
  std::unique_ptr<AdditiveHarness>peeling,weighted;
  if(!mechanism||mechanism==1)peeling.reset(new AdditiveHarness(COIN_RENDER_TRANSPARENCY_PEELING));
  if(!mechanism||mechanism==2)weighted.reset(new AdditiveHarness(COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT));
  bool ok=true;for(auto*h:{peeling.get(),weighted.get()})if(h)for(int maps:{1,2,4,8})if(!selectedMaps||selectedMaps==maps)
    ok &= additiveRttMode>=0?interleavedAdditive(*h,maps):additiveCase(*h,maps);
  return ok?0:1;
}
