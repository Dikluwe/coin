#include "CoinRenderCubeRttFixture.h"
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
using namespace cube_rtt_test;

static bool dependencySampler(const CoinRenderRttPlan&graph,unsigned expected){
 unsigned witnesses=0;bool ok=true;
 for(const auto&producer:graph.producers)if(producer.cube)for(unsigned face=0;face<producer.faceCount();++face){
  const auto&frame=producer.face(face);
  for(const auto&draw:frame.draws)if(draw.renderStateSlot<frame.renderStates.size()){
   const auto unit=coin_render_texture_unit(frame.renderStates[draw.renderStateSlot],0);
   if(unit.enabled&&unit.imageSlot<frame.textures.size()&&frame.textures[unit.imageSlot].producerId){
    ++witnesses;ok &=check(unit.samplerSlot<frame.samplers.size()&&unsigned(frame.samplers[unit.samplerSlot].filter)==expected,"authored dependency quality crosses capture as the exact active sampler");
   }
  }
 }
 return check(witnesses>0,"dependency sampler actually used by geometry")&&ok;
}
static bool dependencyFilter(unsigned samples,bool cpu,CoinRenderSceneTextureMode route){
 Source outer,inner;outer.pale=true;outer.paint(0);SoComplexity*quality=nullptr;
 dependency(outer,inner,8,.1f,&quality);removeSourceCallbacks(outer);removeSourceCallbacks(inner);
 Scene scene;scene.cube->size.setValue(8,8);scene.cube->scene=outer.root;scene.enabled->whichChild=SO_SWITCH_ALL;
 Harness h(samples,cpu,route);Frame nearest,linear,restored;
 const auto a=FaceOracle(inner,8).sample(SbVec3f(0,0,1),0,0),b=FaceOracle(inner,8).sample(SbVec3f(0,0,1),0,2);
 std::cout<<"cube_dependency_filters nearest="<<a[0]<<','<<a[1]<<','<<a[2]<<" default_magnification="<<b[0]<<','<<b[1]<<','<<b[2]<<'\n';
 bool ok=h.render(scene.root,nearest,"explicit dependency NEAREST")&&gradientCenters(nearest,FaceOracle(outer,8),"explicit dependency NEAREST")&&dependencySampler(h.proxy->graph,0);
 quality->textureQuality=.5f;dependencyFactor(outer,inner,8,.5f);
 ok &=h.render(scene.root,linear,"explicit default dependency mip filter magnifies LINEAR")&&gradientCenters(linear,FaceOracle(outer,8),"dependency LINEAR magnification")&&dependencySampler(h.proxy->graph,2)&&check(nearest.rgba!=linear.rgba,"nearest versus central four-byte interpolation observably changes the nested cube");
 quality->textureQuality=.1f;dependencyFactor(outer,inner,8,.1f);
 ok &=h.render(scene.root,restored,"dependency filter A/B/A")&&gradientCenters(restored,FaceOracle(outer,8),"dependency filter restored")&&dependencySampler(h.proxy->graph,0)&&exact(nearest,restored);
 return ok;
}
static bool sharedDeclarative(unsigned samples,bool cpu,CoinRenderSceneTextureMode route){
 Source outer,inner;outer.pale=true;inner.pale=true;outer.paint(0);inner.paint(0);
 auto*shared=dependency(outer,inner,8);outer.root->insertChild(shared,outer.root->findChild(shared)+1);
 removeSourceCallbacks(outer);removeSourceCallbacks(inner);
 Scene scene;scene.cube->size.setValue(8,8);scene.cube->scene=outer.root;scene.enabled->whichChild=SO_SWITCH_ALL;
 Harness h(samples,cpu,route);Frame a,b,c;bool ok=h.render(scene.root,a,"declarative shared child binding twice per parent face")&&gradientCenters(a,FaceOracle(outer,8),"declarative shared child");
 if(!cpu&&h.proxy)ok &=check(h.proxy->graph.producers.size()==2,"equal declarative child occurrences share one completed logical resource");
 inner.pale=false;inner.paint(1);dependencyFactor(outer,inner,8);
 ok &=h.render(scene.root,b,"shared deep image mutation")&&gradientCenters(b,FaceOracle(outer,8),"shared deep image mutation")&&check(a.rgba!=b.rgba,"shared child mutation reaches all parent faces");
 inner.pale=true;inner.paint(0);dependencyFactor(outer,inner,8);
 ok &=h.render(scene.root,c,"shared declarative A/B/A")&&gradientCenters(c,FaceOracle(outer,8),"shared declarative restored")&&exact(a,c);
 shared->scene=outer.root;ok &=h.reject(scene.root,a,"shared descendant cycle is not hidden by a completed resource");
 shared->scene=inner.root;ok &=h.render(scene.root,c,"shared cycle recovery")&&exact(a,c);
 return ok;
}
static bool transitiveDepth(unsigned samples,bool cpu,CoinRenderSceneTextureMode route){
 std::vector<std::unique_ptr<Source>>source;std::vector<SoSceneTextureCubeMap*>nodes;
 for(unsigned i=0;i<9;++i){source.emplace_back(new Source);source.back()->pale=true;source.back()->paint(i%2);if(i)nodes.push_back(dependency(*source[i],*source[i-1],8));}
 for(auto&item:source)removeSourceCallbacks(*item);
 Scene scene;scene.cube->size.setValue(8,8);scene.cube->scene=source[7]->root;scene.enabled->whichChild=SO_SWITCH_ALL;
 // Complete this three-level tail first at shallow depth; the same authored node is then reached deeper.
 scene.enabled->insertChild(nodes[2],1);
 Harness h(samples,cpu,route);Frame a,b;bool ok=h.render(scene.root,a,"completed shared tail inside depth8")&&gradientCenters(a,FaceOracle(*source[7],8),"transitive depth8");
 scene.cube->scene=source[8]->root;ok &=h.reject(scene.root,a,"completed tail still checks transitive depth9");
 scene.cube->scene=source[7]->root;ok &=h.render(scene.root,b,"completed-tail depth8 exact recovery")&&exact(a,b);
 return ok;
}

static bool observableShared(unsigned samples,bool cpu,CoinRenderSceneTextureMode route){
 Source outer,inner;outer.pale=true;outer.paint(0);
 auto*shared=dependency(outer,inner,8);outer.root->insertChild(shared,outer.root->findChild(shared)+1);
 removeSourceCallbacks(outer);outer.multiplier={{1,1,1,1}};
 const std::array<RGBA,3> colors{{RGBA{{80,150,220,255}},RGBA{{190,110,70,255}},RGBA{{130,210,100,255}}}};
 unsigned phase=0;
 inner.onCapture=[&](){const auto color=colors[((inner.callbacks-1)/6)%3];SbColor values[4];for(auto&v:values)v.setValue(float((color[0]+phase*20)/255),float((color[1]+phase*20)/255),float((color[2]+phase*20)/255));for(auto*m:inner.materials)m->diffuseColor.setValues(0,4,values);};
 Scene scene;scene.cube->size.setValue(8,8);scene.cube->scene=outer.root;scene.enabled->whichChild=SO_SWITCH_ALL;
 Harness h(samples,cpu,route);Frame a,b,c;
 auto render=[&](Frame&frame,const char*label){inner.callbacks=0;for(unsigned face=0;face<6;++face)for(unsigned channel=0;channel<3;++channel)outer.faceMultiplier[face][channel]=(colors[(2*face+1)%3][channel]+phase*20)/255;
  bool result=h.render(scene.root,frame,label)&&gradientCenters(frame,FaceOracle(outer,8),label);
  std::cout<<"cube_shared_observable captures="<<inner.callbacks<<" expected=72 phase="<<phase<<'\n';
  return check(inner.callbacks==72,"two authored callback-bearing child occurrences execute all six captures in every parent face")&&result;};
 bool ok=render(a,"shared mutable callback A");phase=1;ok &=render(b,"shared mutable callback B")&&check(a.rgba!=b.rgba,"callback writes affect the current parent cube");phase=0;ok &=render(c,"shared callback A/B/A")&&exact(a,c);return ok;
}
static bool occurrenceCore(){
 CoinRenderRttPlan graph(COIN_RENDER_SCENE_TEXTURE_STAGED);std::string error;bool ok=true;
 const size_t bytes=size_t(1024)*1024*4;
 for(unsigned occurrence=0;occurrence<16;++occurrence){
  const bool entered=graph.enter(123,SbVec2i32(1024,1024),error);
  ok &=check(entered,"same 2D source occurrence enters below historical STAGED budget");
  if(entered)graph.leave();
  ok &=check(graph.chargedBytes==(occurrence+1)*bytes,"STAGED charges every occurrence even with identical source identity");
 }
 const auto charged=graph.chargedBytes;ok &=check(charged==CoinRenderRttPlan::budget(),"sixteen 4 MiB occurrences exactly fill 64 MiB");
 ok &=check(!graph.enter(123,SbVec2i32(1024,1024),error)&&graph.chargedBytes==charged&&graph.activeSources.empty()&&!error.empty(),"seventeenth occurrence refuses atomically before entering source stack");
 return ok;
}
static bool inheritedOccurrenceBudget(unsigned samples,CoinRenderSceneTextureMode route){
 // A 512-square 2D descriptor costs 1 MiB per STAGED occurrence.
 // Geometry never samples it, but it is authored inside every cube face.
 // Ten identical cube occurrences contain sixty 2D occurrences; eleven
 // contain sixty-six. Stop at preflight, before allocating any image.
 auto*empty=new SoSeparator;auto*source=new SoSeparator;source->ref();
 auto*twoD=new SoSceneTexture2;twoD->size.setValue(512,512);twoD->scene=empty;source->addChild(twoD);
 Scene scene;scene.cube->size.setValue(1,1);scene.cube->scene=source;scene.enabled->whichChild=SO_SWITCH_ALL;
 for(unsigned i=1;i<10;++i)scene.enabled->insertChild(scene.cube,scene.enabled->findChild(scene.cube)+1);
 Harness h(samples,true,route);if(!check(bool(h.target),"admission-only target prepared")){source->unref();return false;}h.proxy->rejectPreflight=true;const unsigned submits=h.proxy->submits,producers=h.proxy->producerCalls;
 h.action.apply(scene.root);bool ok=check(h.proxy->preflights==1&&h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&std::string(h.action.getLastError().getString())=="independent admission-only spy, no resource allocation","ten shared cube occurrences admit sixty unused 2D descendants to admission-only preflight");
 if(route==COIN_RENDER_SCENE_TEXTURE_STAGED)ok &=check(h.proxy->graph.chargedBytes>=size_t(60)*1024*1024,"completed subgraph hits retain all historical STAGED occurrence charges");
 scene.enabled->insertChild(scene.cube,scene.enabled->findChild(scene.cube)+1);h.action.apply(scene.root);
 std::cout<<"cube_occurrence_budget route="<<route<<" preflights="<<h.proxy->preflights<<" charged="<<h.proxy->graph.chargedBytes<<" error="<<h.action.getLastError().getString()<<'\n';
 if(route==COIN_RENDER_SCENE_TEXTURE_STAGED)ok &=check(h.proxy->preflights==1&&h.action.getLastStatus()!=CoinRenderAction::SUCCESS&&h.action.getLastError().getLength()>0,"eleventh shared cube occurrence refuses inherited 66 MiB budget before preflight");
 else ok &=check(h.proxy->preflights==2,"DIRECT identical resources retain distinct-producer rather than occurrence admission");
 ok &=check(h.target->getLastSubmissionSerial()==0&&h.proxy->submits==submits&&h.proxy->producerCalls==producers,"descriptor-only budget probes never execute producers or publish a root");
 scene.enabled->removeChild(scene.enabled->findChild(scene.cube));
 h.action.apply(scene.root);ok &=check(h.proxy->preflights==(route==COIN_RENDER_SCENE_TEXTURE_STAGED?2u:3u),"budget refusal restores exact admission of original ten-occurrence graph");
 source->unref();return ok;
}

static bool divergentContexts(unsigned samples,bool cpu,CoinRenderSceneTextureMode route){
 Source first,second,child;first.pale=second.pale=true;first.paint(0);second.paint(1);
 auto*shared=dependency(first,child,8,.1f);
 // Same node, same source, different inherited consumer samplers in one apply.
 auto*q=new SoComplexity;q->textureQuality=.5f;second.root->insertChild(q,2);second.root->insertChild(shared,3);
 auto*uv=new SoTextureCoordinate3;for(unsigned i=0;i<4;++i)uv->point.set1Value(i,SbVec3f(0,0,1));second.root->insertChild(uv,4);
 dependencyFactor(second,child,8,.5f);
 for(auto*source:{&first,&second,&child})removeSourceCallbacks(*source);
 Scene scene;scene.cube->size.setValue(8,8);scene.cube->scene=first.root;scene.cube->model=SoSceneTextureCubeMap::MODULATE;scene.enabled->whichChild=SO_SWITCH_ALL;
 auto*unit=new SoTextureUnit;unit->unit=3;scene.enabled->addChild(unit);
 auto*other=new SoSceneTextureCubeMap;other->scene=second.root;other->size.setValue(8,8);other->model=SoSceneTextureCubeMap::MODULATE;scene.enabled->addChild(other);
 for(unsigned cell=0;cell<54;++cell){auto*u=new SoTextureUnit;u->unit=3;auto*v=new SoTextureCoordinate3;for(unsigned i=0;i<4;++i)v->point.set1Value(i,scene.coordinates[cell]->point[0]);auto*restore=new SoTextureUnit;restore->unit=0;scene.panels[cell]->insertChild(u,0);scene.panels[cell]->insertChild(v,1);scene.panels[cell]->insertChild(restore,2);}
 Harness h(samples,cpu,route);auto math=[&](const Frame&frame){FaceOracle a(first,8),b(second,8);unsigned bad=0;for(unsigned cell=0;cell<54;++cell){const auto d=scene.coordinates[cell]->point[0];auto expected=a.sample(d,0,0),factor=b.sample(d,0,0);int x=int(std::lround(side*.5+(-3.2+.8*(cell%9))*24-.5)),y=int(std::lround(side*.5-(2.-.8*(cell/9))*24-.5));size_t offset=(size_t(y)*side+unsigned(x))*4;for(unsigned channel=0;channel<3;++channel)bad+=std::abs(frame.rgba[offset+channel]-std::lround(expected[channel]*factor[channel]/255))>3;}
  std::cout<<"cube_divergent_contexts bad="<<bad<<'\n';return check(!bad,"same child under different inherited sampler contexts retains both independently authored cascades");};
 Frame a,b,c;bool ok=h.render(scene.root,a,"shared child divergent contexts")&&math(a);
 q->textureQuality=.1f;dependencyFactor(second,child,8,.1f);
 ok &=h.render(scene.root,b,"second context changes independently")&&math(b)&&check(a.rgba!=b.rgba,"changing only second context observably changes two-parent composition");
 q->textureQuality=.5f;dependencyFactor(second,child,8,.5f);ok &=h.render(scene.root,c,"divergent contexts A/B/A")&&math(c)&&exact(a,c);return ok;
}

class MutableCubeCoordinates:public SoNode{
 SO_NODE_HEADER(MutableCubeCoordinates);
public:
 static void initClass(){SO_NODE_INIT_CLASS(MutableCubeCoordinates,SoNode,"Node");}
 MutableCubeCoordinates(){SO_NODE_CONSTRUCTOR(MutableCubeCoordinates);}
 float s=.25f;unsigned calls=0,traversals=0;bool invalid=false;
 static const SbVec4f&generate(void*data,const SbVec3f&,const SbVec3f&){auto*self=static_cast<MutableCubeCoordinates*>(data);++self->calls;self->value.setValue(self->invalid?std::numeric_limits<float>::quiet_NaN():self->s,.5f,0,1);return self->value;}
 void doAction(SoAction*action)override{SoMultiTextureCoordinateElement::setFunction(action->getState(),this,SoTextureUnitElement::get(action->getState()),generate,this);}
 void callback(SoCallbackAction*action)override{++traversals;doAction(action);}
 void GLRender(SoGLRenderAction*action)override{doAction(action);}
protected:~MutableCubeCoordinates()override=default;
private:SbVec4f value;
};
SO_NODE_SOURCE(MutableCubeCoordinates);
static bool mutableCoordinates(unsigned samples,bool cpu,CoinRenderSceneTextureMode route){
 Source outer,inner;outer.pale=true;outer.paint(0);
 auto*shared=dependency(outer,inner,8);outer.root->insertChild(shared,outer.root->findChild(shared)+1);
 removeSourceCallbacks(outer);removeSourceCallbacks(inner);
 auto*q=new SoComplexity;q->textureQuality=.1f;auto*texture=new SoTexture2;
 const unsigned char bytes[]={220,150,190,255,110,230,160,255};texture->image.setValue(SbVec2s(2,1),4,bytes);texture->model=SoTexture2::MODULATE;
 auto*function=new MutableCubeCoordinates;inner.root->insertChild(q,0);inner.root->insertChild(texture,1);inner.root->insertChild(function,2);
 auto oracle=[&](unsigned texel){for(unsigned channel=0;channel<3;++channel)inner.multiplier[channel]=bytes[4*texel+channel]/255.;dependencyFactor(outer,inner,8);};
 oracle(0);
 Scene leaf;leaf.cube->size.setValue(8,8);leaf.cube->scene=inner.root;leaf.enabled->whichChild=SO_SWITCH_ALL;
 Scene scene;scene.cube->size.setValue(8,8);scene.cube->scene=outer.root;scene.enabled->whichChild=SO_SWITCH_ALL;
 Harness h(samples,cpu,route);Frame reference,a,b,c;bool ok=h.render(leaf.root,reference,"mutable function standalone leaf")&&gradientCenters(reference,FaceOracle(inner,8),"mutable function independent palette");
 const unsigned perCube=function->calls;ok &=check(perCube>0&&function->traversals==6,"standalone function and custom traversal execute six face actions");
 const auto functionRevision=function->getNodeId(),sourceRevision=inner.root->getNodeId();
 auto render=[&](Frame&frame,const char*label){function->calls=function->traversals=0;bool result=h.render(scene.root,frame,label)&&gradientCenters(frame,FaceOracle(outer,8),label);
  std::cout<<"cube_coordinate_function calls="<<function->calls<<" per_cube="<<perCube<<" traversals="<<function->traversals<<'\n';
  return check(function->calls==12*perCube&&function->traversals==72,"custom traversal/function remain observable for all twelve logical child occurrences")&&result;};
 ok &=render(a,"shared coordinate function A");
 function->s=.75f;oracle(1);ok &=check(functionRevision==function->getNodeId()&&sourceRevision==inner.root->getNodeId(),"plain mutable coordinate state retains authored node revisions")&&render(b,"shared coordinate function B")&&check(a.rgba!=b.rgba,"plain mutable function changes the current image");
 function->s=.25f;oracle(0);ok &=render(c,"shared coordinate function A/B/A")&&exact(a,c);
 function->invalid=true;ok &=h.reject(scene.root,a,"mutable descendant invalid function preflight");
 function->invalid=false;ok &=render(c,"mutable descendant function recovery")&&exact(a,c);return ok;
}
static bool stereoState(unsigned samples,bool cpu,CoinRenderSceneTextureMode route){
 Source source;source.authoredCamera(false,false);removeSourceCallbacks(source);source.camera->setStereoAdjustment(1.4f);source.camera->setBalanceAdjustment(.6f);
 Scene scene;scene.cube->scene=source.root;scene.enabled->whichChild=SO_SWITCH_ALL;Harness h(samples,cpu,route);Frame a,left,right,c;
 const auto cameraRevision=source.camera->getNodeId(),sceneRevision=source.root->getNodeId();
 bool ok=h.render(scene.root,a,"declarative camera mono")&&gradientCenters(a,FaceOracle(source,32),"stereo mono authored projection");
 source.camera->setStereoMode(SoCamera::LEFT_VIEW);
 ok &=check(cameraRevision==source.camera->getNodeId()&&sceneRevision==source.root->getNodeId(),"stereo mode mutation retains field-based scene revisions")&&h.render(scene.root,left,"non-field stereo LEFT")&&gradientCenters(left,FaceOracle(source,32),"independent LEFT skewed frustum")&&check(a.rgba!=left.rgba,"stereo LEFT changes cube image");
 source.camera->setStereoMode(SoCamera::RIGHT_VIEW);
 ok &=h.render(scene.root,right,"non-field stereo RIGHT")&&gradientCenters(right,FaceOracle(source,32),"independent RIGHT skewed frustum")&&check(left.rgba!=right.rgba,"distinct stereo eyes change cube image");
 source.camera->setStereoMode(SoCamera::MONOSCOPIC);ok &=h.render(scene.root,c,"non-field stereo A/B/A")&&exact(a,c);
 source.camera->setStereoMode(SoCamera::LEFT_VIEW);source.camera->setStereoAdjustment(std::numeric_limits<float>::quiet_NaN());ok &=h.reject(scene.root,a,"non-field stereo malformed adjustment");
 source.camera->setStereoAdjustment(1.4f);source.camera->setStereoMode(SoCamera::MONOSCOPIC);ok &=h.render(scene.root,c,"stereo invalid recovery")&&exact(a,c);
 return check(source.notifications==0&&cameraRevision==source.camera->getNodeId(),"capture never mutates or notifies authored camera")&&ok;
}

static bool sharedStereo(unsigned samples,bool cpu,CoinRenderSceneTextureMode route){
 Source outer,inner;outer.pale=true;outer.paint(0);inner.authoredCamera(false,false);
 inner.camera->setStereoAdjustment(3.f);inner.camera->setBalanceAdjustment(.3f);
 auto*shared=dependency(outer,inner,8);(void)shared;removeSourceCallbacks(inner);
 outer.multiplier={{1,1,1,1}};unsigned phase=0;
 const auto cameraRevision=inner.camera->getNodeId(),sourceRevision=inner.root->getNodeId();
 outer.onCapture=[&](){inner.camera->setStereoMode(((outer.callbacks-1+phase)%2)?SoCamera::RIGHT_VIEW:SoCamera::LEFT_VIEW);};
 Scene scene;scene.cube->size.setValue(8,8);scene.cube->scene=outer.root;scene.enabled->whichChild=SO_SWITCH_ALL;
 Harness h(samples,cpu,route);Frame a,b,c;
 auto render=[&](Frame&frame,const char*label){
  for(unsigned face=0;face<6;++face){inner.camera->setStereoMode(((face+phase)%2)?SoCamera::RIGHT_VIEW:SoCamera::LEFT_VIEW);const auto factor=FaceOracle(inner,8).sample(SbVec3f(0,0,1),0,2);for(unsigned channel=0;channel<3;++channel)outer.faceMultiplier[face][channel]=factor[channel]/255;}
  outer.callbacks=0;const bool good=h.render(scene.root,frame,label)&&gradientCenters(frame,FaceOracle(outer,8),label);
  return check(outer.callbacks==6&&cameraRevision==inner.camera->getNodeId()&&sourceRevision==inner.root->getNodeId()&&inner.notifications==0,"each parent face changes descendant stereo without changing any child source field revision or notification")&&good;
 };
 bool ok=render(a,"shared descendant alternating non-field stereo");
 phase=1;ok &=render(b,"shared descendant reversed stereo eyes")&&check(a.rgba!=b.rgba,"reversed per-occurrence eyes change current cube faces");
 phase=0;ok &=render(c,"per-occurrence stereo A/B/A")&&exact(a,c);return ok;
}
int main(int argc,char**argv){
 SoDB::init();CoinRenderAction::initClass();MutableCubeCoordinates::initClass();configureSamplingTest(argc,argv);unsigned samples=1;bool cpu=false;std::string mode="--shared";CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED;
 for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--recording"||arg=="--public-cpu"){recordingFactoryMode=true;cpu=true;}else if(arg=="--samples"&&i+1<argc){if(!unsignedOption(argv[++i],samples))return 2;}else if(arg=="--cpu")cpu=true;else if(arg=="--direct")route=COIN_RENDER_SCENE_TEXTURE_DIRECT;else if(arg=="--staged")route=COIN_RENDER_SCENE_TEXTURE_STAGED;else if(arg=="--portable-sampling"){}else if(arg=="--stereo-shared"||arg=="--function"||arg=="--stereo"||arg=="--contexts"||arg=="--shared"||arg=="--dependency-filter"||arg=="--observable"||arg=="--transitive"||arg=="--occurrence-core"||arg=="--occurrence-budget")mode=arg;else{std::cerr<<"unknown or incomplete option: "<<arg<<'\n';return 2;}}
 if(samples!=1&&samples!=2&&samples!=4)return 2;
 if(mode=="--occurrence-core")return occurrenceCore()?0:1;
 if(mode=="--occurrence-budget")return inheritedOccurrenceBudget(samples,route)?0:1;
 CoinRenderCapabilities caps{};if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps)))return 1;if(cpu&&caps.backend==COIN_RENDER_EXPERIMENTAL_RECORDING&&!caps.gpu_available)recordingFactoryMode=true;if(recordingFactoryMode){if(!check(caps.backend==COIN_RENDER_EXPERIMENTAL_RECORDING&&!caps.gpu_available&&samples==1&&route==COIN_RENDER_SCENE_TEXTURE_STAGED,"sharing public CPU requires genuine RECORDING/STAGED/1x"))return 1;}else if(!cpu&&!caps.gpu_available)return 77;
 return (mode=="--stereo-shared"?sharedStereo(samples,cpu,route):mode=="--function"?mutableCoordinates(samples,cpu,route):mode=="--stereo"?stereoState(samples,cpu,route):mode=="--contexts"?divergentContexts(samples,cpu,route):mode=="--shared"?sharedDeclarative(samples,cpu,route):mode=="--dependency-filter"?dependencyFilter(samples,cpu,route):mode=="--observable"?observableShared(samples,cpu,route):transitiveDepth(samples,cpu,route))?0:1;
}
