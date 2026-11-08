#include "config.h"
#include "CoinRenderRetainedTestRenderer.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/sensors/SoNodeSensor.h>
#include "rendering/coinrender/CoinRenderRetainedTransport.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderBackend.h"
#include <array>
#include <functional>
#include <memory>
#include <thread>
#include <fstream>
#include <chrono>
#include <iostream>
#include <cstring>
using Status=CoinRenderAction::Status;
static CoinRenderOptions options;
static int failures=0, observations=0;
static std::string outputDir;
static const SbViewportRegion vp(64,64);
void require(bool v,const char *m) {if(!v){++failures;std::cerr<<"ASSERT_FAIL "<<m<<"\n";}}
struct Values {
  bool nativeFaceSet=false;
  int faceCount=3;
  float cameraHeight=3, cameraX=0;
  std::array<SbVec3f,4> points{{SbVec3f(-.4f,-.4f,0),SbVec3f(.4f,-.4f,0),SbVec3f(.4f,.4f,0),SbVec3f(-.4f,.4f,0)}};
  std::array<int32_t,8> indices{{0,1,2,-1,0,2,3,-1}};
  std::array<SbVec3f,2> moves{{SbVec3f(-.7f,0,0),SbVec3f(.7f,0,0)}};
  std::array<SbColor,2> colors{{SbColor(1,0,0),SbColor(0,0,1)}};
};
struct Scene {
  SoSeparator *root=new SoSeparator;
  SoCoordinate3 *points=new SoCoordinate3;
  SoIndexedFaceSet *mesh=nullptr;
  SoFaceSet *faces=nullptr;
  SoOrthographicCamera *camera=new SoOrthographicCamera;
  std::array<SoTranslation*,2> moves;
  std::array<SoMaterial*,2> materials;
  Scene(const Values &v) {
    root->ref();
    camera->position.setValue(v.cameraX,0,5);camera->height=v.cameraHeight;camera->nearDistance=1;camera->farDistance=10;
    root->addChild(camera);auto *light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
    points->point.setValues(0,4,v.points.data());
    if(v.nativeFaceSet){faces=new SoFaceSet;faces->numVertices=v.faceCount;}
    else{mesh=new SoIndexedFaceSet;mesh->coordIndex.setValues(0,8,v.indices.data());}
    for(int i=0;i<2;++i) {
      auto *part=new SoSeparator;root->addChild(part);
      moves[i]=new SoTranslation;moves[i]->translation=v.moves[i];part->addChild(moves[i]);
      materials[i]=new SoMaterial;materials[i]->diffuseColor=v.colors[i];part->addChild(materials[i]);
      part->addChild(points);part->addChild(faces?static_cast<SoNode*>(faces):static_cast<SoNode*>(mesh));
    }
  }
  ~Scene(){root->unref();}
  SoField *field(int type) {
    if(type==0)return &moves[1]->translation;
    if(type==1)return &materials[1]->diffuseColor;
    if(type==2)return &points->point;
    if(type==4)return &camera->height;
    if(type==5)return &camera->position;
    return faces?static_cast<SoField*>(&faces->numVertices):static_cast<SoField*>(&mesh->coordIndex);
  }
  void edit(int type,Values &v) {
    if(type==0){v.moves[1]=SbVec3f(.55f,.3f,0);moves[1]->translation=v.moves[1];}
    if(type==1){v.colors[1]=SbColor(0,1,0);materials[1]->diffuseColor=v.colors[1];}
    // Real container edits, not a copy retaining a node or primitive cache.
    if(type==2){v.points[2]=SbVec3f(.1f,.65f,0);auto *data=points->point.startEditing();data[2]=v.points[2];points->point.finishEditing();}
    if(type==3){v.indices[6]=1;if(faces){v.faceCount=4;faces->numVertices=4;}else{auto *data=mesh->coordIndex.startEditing();data[6]=1;mesh->coordIndex.finishEditing();}}
    if(type==4){v.cameraHeight=2.7f;camera->height=v.cameraHeight;}
    if(type==5){v.cameraX=.2f;camera->position.setValue(v.cameraX,0,5);}
  }
};
struct Image {std::vector<uint8_t> rgba;std::vector<float> depth;};
bool same(const Image&a,const Image&b){return !a.rgba.empty()&&a.rgba==b.rgba&&a.depth==b.depth;}
struct Recorder:CoinRenderBackend {
  std::unique_ptr<CoinRenderBackend> inner;
  CoinRenderFramePlan saved;
  mutable std::function<void()> admissionHook;
  std::function<void()> completionHook;
  unsigned submissions = 0;
  Recorder(std::unique_ptr<CoinRenderBackend> v):inner(std::move(v)){}
  bool isGpuBackend()const override{return inner->isGpuBackend();}
  bool initializesCpuDepthBuffer()const override{return inner->initializesCpuDepthBuffer();}
  CoinRenderBackendStatus getStatus()const override{return inner->getStatus();}
  CoinRenderBackendStatus prepare(CoinRenderTargetP&t)override{return inner->prepare(t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t)override{saved=f;return inner->submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&f,CoinRenderTargetP&t,const CoinRenderFrameReuseDecision&r)override{saved=f;++submissions;auto result=inner->submit(f,t,r);
    if(completionHook){auto hook=std::move(completionHook);completionHook={};hook();}return result;}
  void poll()override{inner->poll();}
  const std::string&getLastError()const override{return inner->getLastError();}
  CoinRenderDeviceDomain resourceDomain()const override{return inner->resourceDomain();}
  void configureMultipassTarget(const CoinRenderTargetP&t)override{inner->configureMultipassTarget(t);}
  CoinRenderSubmitResult preflightMultipass(const CoinRenderFramePlan&f,const SbVec2i32&s)const override{if(admissionHook){auto hook=std::move(admissionHook);admissionHook={};hook();}
    return inner->preflightMultipass(f,s);}
  bool resetOnActionDetach()const override{return inner->resetOnActionDetach();}
  bool requiresSharedRetirement()const override{return inner->requiresSharedRetirement();}
};
struct Renderer {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderRetainedRenderer retained{vp};
  Recorder *record;
  Renderer() {
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(64,64),options));
    require(bool(target),"target created");if(!target)std::exit(2);
    target->setDepthReadbackEnabled(TRUE);
    auto &impl=target->getPimpl().get();
    auto prepared=impl.prepareBackend();
    require(prepared==CoinRenderBackendStatus::SUCCESS,"backend prepared before observation wrapper");
    record=new Recorder(std::move(impl.backend));impl.backend.reset(record);
  }
  Image read(){Image i;target->readbackRGBA(i.rgba);target->readbackDepth(i.depth);return i;}
  Image render(Scene&s,const std::string&label) {
    auto status=retained.render(s.root,*target);
    if(status!=CoinRenderAction::SUCCESS)std::cerr<<"RENDER_ERROR "<<label<<' '<<retained.getLastError().getString()<<'\n';
    require(status==CoinRenderAction::SUCCESS,"render successful");auto image=read();
    require(image.rgba.size()==64*64*4 && image.depth.size()==64*64,"complete image");
    const auto receipt=target->getPresentationReceipt();
    require(target->admitsPresentation(receipt),"successful retained render has current presentation receipt");
    auto st=retained.getStatistics();const auto &list=retained.getDrawList();
    CoinRenderCacheTelemetry cache{};const bool telemetry=target->getCacheTelemetry(cache);
    std::cout<<"OBS "<<label<<" status="<<int(status)<<" root="<<s.root->getNodeId()
      <<" generation="<<list.getGeneration()<<" content="<<list.getContentRevision()
      <<" resources="<<list.getResourceRevision()<<" plan="<<list.getRenderPlanRevision()
      <<" capture="<<st.captures<<" plans="<<st.planBuilds<<" updates="<<st.incrementalUpdates
      <<" builds="<<st.transportBuilds<<" reuses="<<st.transportReuses
      <<" patches="<<st.transportPatchedCommands<<" geometryPatches="<<st.transportGeometryPatches
      <<" geometryResources="<<st.nativeGeometryResources<<" serial="<<target->getLastSubmissionSerial()
      <<" frameRevision="<<record->saved.revision
      <<" sceneIdentity="<<receipt.getSceneIdentity()<<" sceneRevision="<<receipt.getSceneRevision()
      <<" cameraIdentity="<<receipt.getCameraIdentity()<<" cameraRevision="<<receipt.getCameraRevision()<<" resourceReceipt="<<receipt.getResourceRevision()<<"\n";
    std::cout<<"CACHE "<<label<<" available="<<telemetry<<" uploads="<<cache.frameUploads<<" bytes="<<cache.frameUploadedBytes
      <<" hits="<<cache.frameHits<<" totalUploads="<<cache.cumulativeUploads<<"\n";
    std::string filename=label;for(auto &c:filename)if(c=='/')c='_';
    std::ofstream semantic(outputDir+"/"+filename+".semantic");
    semantic.precision(9);
    const auto &frame=record->saved;
    for(const auto&v:frame.vertices)semantic<<"V "<<v.position[0]<<' '<<v.position[1]<<' '<<v.position[2]<<'\n';
    for(auto index:frame.indices)semantic<<"I "<<index<<'\n';
    for(const auto&m:frame.materials)semantic<<"M "<<m.diffuse[0]<<' '<<m.diffuse[1]<<' '<<m.diffuse[2]<<' '<<m.diffuse[3]<<'\n';
    for(const auto&s:frame.renderStates){semantic<<"S "<<s.materialSlot<<' '<<s.cameraSlot<<' ';for(int i=0;i<4;++i)for(int j=0;j<4;++j)semantic<<s.model[i][j]<<' ';semantic<<'\n';
      semantic<<"C ";for(int i=0;i<4;++i)for(int j=0;j<4;++j)semantic<<s.view[i][j]<<' '<<s.projectionCoin[i][j]<<' ';semantic<<'\n';}
    for(const auto&d:frame.draws)semantic<<"D "<<d.geometry.firstVertex<<' '<<d.geometry.vertexCount<<' '<<d.geometry.firstIndex<<' '<<d.geometry.indexCount<<' '<<d.renderStateSlot<<'\n';
    ++observations;return image;
  }
};
Image cold(const Values&v,const std::string&label){Scene fresh(v);Renderer r;return r.render(fresh,"cold/"+label);}
void dump(const Image&i,const std::string&name,const char*dir){
  std::ofstream f(std::string(dir)+"/"+name+".rgba",std::ios::binary);f.write(reinterpret_cast<const char*>(i.rgba.data()),i.rgba.size());
  std::ofstream d(std::string(dir)+"/"+name+".depth",std::ios::binary);d.write(reinterpret_cast<const char*>(i.depth.data()),i.depth.size()*sizeof(float));
}
void mutation(int type,bool suppressed,const char*dir,bool native=false) {
  const std::string name=std::string(native?"faceset/":"indexed/")+(suppressed?"suppressed/":"published/")+std::to_string(type);
  Values values;values.nativeFaceSet=native;Scene scene(values);Renderer r,warm;
  const auto baseline=r.render(scene,name+"/baseline");warm.render(scene,name+"/warm-prime");
  const int resources=r.retained.getDrawList().getNumGeometryResources();
  if(native)require(resources==1,"native FaceSet mesh resource shared by both occurrences");
  else std::cout<<"PROFILE indexed shared source emits resources="<<resources<<" (generic primitive capture)\n";
  const auto baselineReceipt=r.target->getPresentationReceipt();
  const auto before=r.retained.getStatistics();
  const auto rootRevision=scene.root->getNodeId();
  const auto irRevision=r.retained.getDrawList().getContentRevision();
  auto *field=scene.field(type);if(suppressed)field->enableNotify(FALSE);
  scene.edit(type,values);
  if(suppressed)field->enableNotify(TRUE);
  require(bool(r.target->admitsPresentation(baselineReceipt))==suppressed,
    "published edits invalidate old presentation; silent edits retain the announced revision");
  const auto current=r.render(scene,name+"/incremental");
  const auto editedReceipt=r.target->getPresentationReceipt();
  require((editedReceipt.getSceneRevision()==baselineReceipt.getSceneRevision())==suppressed,
    "scene receipt follows producer notifications");
  if(type==4 || type==5) require((editedReceipt.getCameraRevision()==baselineReceipt.getCameraRevision())==suppressed,
    "camera receipt follows camera notifications without geometry edits");
  const auto truth=cold(values,name);
  require(!same(baseline,truth),"cold control visibly distinguishes authored edit");
  warm.retained.invalidate();const auto rebuilt=warm.render(scene,name+"/warm-rebuild");
  const bool warmMatches=same(rebuilt,truth);
  std::cout<<"RESULT "<<name<<" incrementalCold="<<same(current,truth)
    <<" warmCold="<<warmMatches<<" incrementalOld="<<same(current,baseline)<<"\n";
  if(suppressed){
    require(scene.root->getNodeId()==rootRevision,"suppressed edit publishes no root revision");
    require(r.retained.getDrawList().getContentRevision()==irRevision,"suppressed edit publishes no IR revision");
    require(same(current,baseline),"retained image stays at last announced revision");
    require(r.retained.getStatistics().captures==before.captures && r.retained.getStatistics().incrementalUpdates==before.incrementalUpdates,"no magical updater for unannounced edit");
    field->touch();
    const auto touched=r.render(scene,name+"/touch");
    const bool correct=same(touched,truth);
    std::cout<<"TOUCH "<<name<<" equalsCold="<<correct<<"\n";
    require(correct,"explicit publication reaches actual cold-scene state");
    // A serial is merely a successful submission counter, not the scene identity.
    require(r.target->getLastSubmissionSerial()>=3,"serial advanced despite earlier unannounced mutation");
  }else require(same(current,truth),"published mutation matches independently authored cold scene");
  dump(truth,std::string(native?"faceset_":"indexed_")+(suppressed?"suppressed":"published")+std::to_string(type),dir);
}
uint64_t digest(const CoinRenderFramePlan&f) {
  uint64_t h=1469598103934665603ULL;
  auto add=[&](const void*p,size_t n){auto*c=static_cast<const unsigned char*>(p);for(size_t i=0;i<n;++i){h^=c[i];h*=1099511628211ULL;}};
  for(const auto&v:f.vertices)add(v.position,sizeof(v.position));
  for(auto v:f.indices)add(&v,sizeof(v));
  for(const auto&m:f.materials)add(m.diffuse,sizeof(m.diffuse));
  for(const auto&t:f.renderStates){add(t.model.getValue(),16*sizeof(float));add(t.view.getValue(),16*sizeof(float));add(t.projectionCoin.getValue(),16*sizeof(float));}
  return h;
}
void transportAndReuse(const char*dir){
  Values values;CoinRenderFramePlan saved;Image old,latest;uint64_t hash=0;
  bool rootDeleted=false,meshDeleted=false;
  SoNodeSensor rootObserver,meshObserver;
  auto deleted=[](void *data,SoSensor*){*static_cast<bool*>(data)=true;};
  rootObserver.setDeleteCallback(deleted,&rootDeleted);meshObserver.setDeleteCallback(deleted,&meshDeleted);
  {
    Scene scene(values);Renderer producer;
    rootObserver.attach(scene.root);meshObserver.attach(scene.mesh);
    old=producer.render(scene,"snapshot/old");saved=producer.record->saved;hash=digest(saved);
    scene.edit(0,values);scene.edit(1,values);scene.edit(2,values);
    latest=producer.render(scene,"snapshot/new");
    require(digest(saved)==hash,"copied transport detached from producer edits");
  } // The scene, native capture, transport cache and source target are destroyed.
  require(rootDeleted&&meshDeleted,"deletion sensors confirm root and shared mesh destroyed before worker");
  uint64_t workerHash=0;
  std::thread worker([&]{workerHash=digest(saved);});worker.join();
  require(workerHash==hash,"worker reads copied DTO after producer destruction");
  std::cout<<"OWNED rootDeleted="<<rootDeleted<<" meshDeleted="<<meshDeleted<<" workerDigest="<<workerHash<<"\n";
  Renderer consumer;
  auto result=consumer.target->getPimpl()->executeFrame(saved);
  require(result.status==CoinRenderBackendStatus::SUCCESS,"saved owned DTO can be executed after producer destruction");
  require(same(consumer.read(),old),"old snapshot remains readable");
  const auto coldLatest=cold(values,"snapshot/new");
  require(same(latest,coldLatest),"new revision matches cold oracle");
  require(!same(consumer.read(),coldLatest),"old snapshot must not be confused with latest scene");
  require(!consumer.target->admitsPresentation(consumer.target->getPresentationReceipt()),
    "historical external DTO cannot authorize current presentation");
  std::cout<<"PRESENTATION old owned DTO remains readable/executable, but has no current-scene admission proof\n";
  dump(latest,"snapshot-new",dir);
}
void missingPayload(){
  Values values;Scene scene(values);Renderer r;r.render(scene,"pending/primer");
  const auto old=r.read();const auto serial=r.target->getLastSubmissionSerial();
  SoIRRenderAction capture(vp);capture.setCameraPolicy(SoIRRenderAction::CameraPolicy::CAMERA_IN_ROOT);capture.apply(scene.root);
  auto &list=capture.getMutableDrawList();
  auto &cmd=list.getCommand(0);
  cmd.geometry.positions=nullptr;
  if(cmd.geometryHandle)list.getGeometryResource(cmd.geometryHandle)->geometry.positions=nullptr;
  SoRenderPlan plan;SoRenderPlanner planner;planner.build(list,plan);
  CoinRenderRetainedTransport transport(*r.target);transport.initialize({});
  SoRenderParams params;params.viewport=vp;params.flags=SO_PARAM_CLEAR_WINDOW|SO_PARAM_CLEAR_DEPTH;
  auto start=std::chrono::steady_clock::now();
  transport.render(list,plan,params);
  const auto &result=transport.getResult();
  auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
  std::cout<<"PENDING nullGeometry status="<<int(result.status)<<" milliseconds="<<ms<<" diagnostic="<<result.diagnostic<<"\n";
  require(ms<2000,"missing resource execution bounded");
  // This is an invalid/missing payload control, not a PENDING state implementation.
  require(result.status!=CoinRenderBackendStatus::SUCCESS && !result.diagnostic.empty(),"missing payload explicitly rejected");
  require(r.target->getLastSubmissionSerial()==serial && same(old,r.read()),"refusal preserves old publication");
  std::cout<<"GAP case4 Native resource readiness/pending producer protocol is not exposed by retained facade\n";
}
void asyncBoundary(){
#if defined(HAVE_COIN_BGFX) || defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  Values values;const auto baseline=cold(values,"async/baseline");CoinRenderReadbackTicket ticket{};
  {
    Scene scene(values);
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64),options));
    target->setDepthReadbackEnabled(TRUE);
    CoinRenderAction action(vp);action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(0,0,0,1));
    action.applyAsync(scene.root,ticket);
    require(action.getLastStatus()==CoinRenderAction::SUCCESS && ticket.token,"conventional applyAsync accepts real ticket");
    Image notReady;target->readbackRGBA(notReady.rgba);target->readbackDepth(notReady.depth);
    require(notReady.rgba.empty()&&notReady.depth.empty(),"async accessors do not publish stale synchronous pixels");
    scene.edit(0,values);scene.edit(1,values);
    action.setRenderTarget(nullptr);
  }
  Image saved;SbString diagnostic;auto status=CoinRenderTarget::READBACK_NOT_READY;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(status==CoinRenderTarget::READBACK_NOT_READY && std::chrono::steady_clock::now()<deadline){
    status=CoinRenderTarget::pollReadback(ticket,saved.rgba,saved.depth,&diagnostic);
    if(status==CoinRenderTarget::READBACK_NOT_READY)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  require(status==CoinRenderTarget::READBACK_READY,"async ticket bounded completion after producer destruction");
  require(same(saved,baseline),"async ticket reads submitted revision, not later producer mutation");
  require(!same(saved,cold(values,"async/latest")),"old async ticket distinguishable from current cold scene");
  std::cout<<"ASYNC conventional token="<<ticket.token<<" deviceGeneration="<<ticket.generation
    <<" submissionSerial="<<ticket.submissionSerial<<" status="<<int(status)<<" diagnostic="<<diagnostic.getString()<<"\n";
  std::cout<<"PRESENTATION old ticket remains historical after producer destruction; current admission requires live authority\n";
#else
  std::cout<<"NOT_IMPLEMENTED recording asynchronous GPU ticket; no skip counted as success\n";
#endif
}

void presentationBoundary() {
  Values values; Scene scene(values); Renderer r;
  auto baseline=r.render(scene,"presentation/baseline");
  auto receipt=r.target->getPresentationReceipt();
  require(receipt.getSceneIdentity() && receipt.getSceneRevision() && receipt.getCameraIdentity() && receipt.getCameraRevision(),
    "opaque receipt carries producer scene and camera revisions");
  require(!r.target->admitsPresentation({}),"default receipt cannot authorize presentation");
  Renderer other;
  require(!other.target->admitsPresentation(receipt),"different target rejects receipt");
  const auto count=r.record->submissions;
  const auto serial=r.target->getLastSubmissionSerial();
  const auto captures=r.retained.getCaptureCount(), plans=r.retained.getPlanBuildCount();
  r.record->admissionHook=[&](){scene.edit(4,values);};
  require(r.retained.render(scene.root,*r.target)==Status::NOT_READY,
    "camera publication after capture refused before submission");
  require(r.record->submissions==count && r.target->getLastSubmissionSerial()==serial && same(r.read(),baseline),
    "pre-submit refusal preserves pixels and serial and queues no work");
  require(r.retained.getCaptureCount()==captures && r.retained.getPlanBuildCount()==plans,
    "freshness admission performs no capture or planning");
  baseline=r.render(scene,"presentation/recover-camera");
  const auto serial2=r.target->getLastSubmissionSerial();
  r.record->completionHook=[&](){scene.edit(1,values);};
  require(r.retained.render(scene.root,*r.target)==Status::NOT_READY,
    "scene publication after GPU submission refused before target publication");
  require(r.target->getLastSubmissionSerial()==serial2 && same(r.read(),baseline),
    "post-submit refusal preserves exact previous publication");
  require(!r.target->admitsPresentation(r.target->getPresentationReceipt()),
    "preserved historical publication is not announced as current");
  r.render(scene,"presentation/recover-scene");
  receipt=r.target->getPresentationReceipt();
  const auto oldCamera=receipt.getCameraRevision();
  scene.camera->height.enableNotify(FALSE);scene.camera->height=2.5f;scene.camera->height.enableNotify(TRUE);
  require(r.target->admitsPresentation(receipt),"unannounced camera edit does not invent a revision");
  scene.camera->height.touch();
  require(!r.target->admitsPresentation(receipt),"explicit camera touch invalidates old presentation immediately");
  r.render(scene,"presentation/touch-camera");
  require(r.target->getPresentationReceipt().getCameraRevision()>oldCamera,"camera touch published in receipt");
  receipt=r.target->getPresentationReceipt();
  require(r.target->resize(SbVec2i32(64,64)),"same-size offscreen resize succeeds");
  require(!r.target->admitsPresentation(receipt),"resize invalidates receipt even with same extent");
  r.render(scene,"presentation/resize");
  receipt=r.target->getPresentationReceipt();
  require(!r.target->setPbrExposureStops(100),"invalid output configuration refused");
  require(r.target->admitsPresentation(receipt),"invalid configuration preserves receipt");
  require(r.target->setPbrExposureStops(1),"valid output configuration accepted");
  require(!r.target->admitsPresentation(receipt),"new output configuration invalidates current receipt");
  r.target->setPbrExposureStops(0);r.render(scene,"presentation/configuration");
  receipt=r.target->getPresentationReceipt();
  const auto beforeInvalidation=r.retained.getStatistics();
  r.retained.invalidate();
  require(!r.target->admitsPresentation(receipt),"explicit rebuild invalidates prior revision before capture");
  r.render(scene,"presentation/explicit-publication");
  const auto afterInvalidation=r.retained.getStatistics();
  require(afterInvalidation.captures==beforeInvalidation.captures+1 &&
    afterInvalidation.planBuilds==beforeInvalidation.planBuilds+1 &&
    r.target->getPresentationReceipt().getSceneRevision()>receipt.getSceneRevision(),
    "explicit publication has a new revision and exactly one capture and plan");
  receipt=r.target->getPresentationReceipt();
  const auto saved=r.record->saved;
  scene.edit(0,values);
  require(r.target->getPimpl()->executeFrame(saved).status==CoinRenderBackendStatus::SUCCESS,
    "external old DTO remains executable as historical work");
  require(!r.target->admitsPresentation(r.target->getPresentationReceipt()),
    "copying old DTO or changing its revision cannot mint a producer receipt");
  require(!r.target->admitsPresentation(receipt),"historical replay cannot restore obsolete receipt");
  CoinRenderPresentationReceipt retired;
  {
    CoinRenderRetainedRenderer producer(vp);
    require(producer.render(scene.root,*other.target)==Status::SUCCESS,"temporary producer publishes");
    retired=other.target->getPresentationReceipt();
    require(other.target->admitsPresentation(retired),"live producer authorizes receipt");
  }
  require(!other.target->admitsPresentation(retired),"producer retirement revokes current admission without owning scene memory");
  Image stillReadable;other.target->readbackRGBA(stillReadable.rgba);
  require(!stillReadable.rgba.empty(),"producer retirement preserves historical pixels");
  uint64_t workerIdentity=0;
  std::thread receiptWorker([&]{workerIdentity=retired.getSceneIdentity();});receiptWorker.join();
  require(workerIdentity==retired.getSceneIdentity() && workerIdentity,"worker reads owned receipt after producer retirement");
  std::cout<<"PRESENTATION deterministic pre-submit and post-submit negatives passed; capture/planning unchanged at admission\n";
}
void managerRootBoundary() {
  Values values;Scene first(values);
  values.colors[1]=SbColor(0,1,0);Scene second(values);
  CoinRenderSceneManager manager(SbVec2i32(64,64),options);
  manager.setSceneGraph(first.root);
  require(manager.render()==Status::SUCCESS,"manager initial root renders");
  auto * target=manager.getRenderTarget();
  const auto before=target->getPresentationReceipt();
  require(target->admitsPresentation(before),"manager mono frame has current receipt");
  manager.setSceneGraph(second.root);
  require(!target->admitsPresentation(before),"manager root replacement immediately supersedes old live root");
  require(manager.render()==Status::SUCCESS,"manager replacement root renders");
  const auto after=target->getPresentationReceipt();
  require(target->admitsPresentation(after) && after.getSceneIdentity()!=before.getSceneIdentity(),
    "replacement root receives distinct scene identity");
  manager.setSceneGraph(nullptr);
  require(!target->admitsPresentation(after),"clearing manager root revokes current admission");
  std::cout<<"PRESENTATION manager root replacement and clear supersede previous live root\n";
}
void currentAsyncBoundary() {
#if defined(HAVE_COIN_BGFX) || defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  Values values;Scene scene(values);
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64),options));
  CoinRenderAction action(vp);action.setRenderTarget(target.get());action.setBackgroundColor(SbColor4f(0,0,0,1));
  const auto submit=[&](){CoinRenderReadbackTicket ticket;action.applyAsync(scene.root,ticket);
    require(action.getLastStatus()==Status::SUCCESS && ticket.token,"qualified async submission succeeds");return ticket;};
  const auto historical=[&](const CoinRenderReadbackTicket&t){Image image;SbString why;
    auto status=CoinRenderTarget::READBACK_NOT_READY;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(status==CoinRenderTarget::READBACK_NOT_READY && std::chrono::steady_clock::now()<deadline){
      status=CoinRenderTarget::pollReadback(t,image.rgba,image.depth,&why);
      if(status==CoinRenderTarget::READBACK_NOT_READY)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    require(status==CoinRenderTarget::READBACK_READY,"stale presentation ticket still readable as historical result");
    return image;};
  auto first=submit();
  require(target->admitsPresentation(first.presentation),"new ticket qualified as current");
  require(first.presentation.getSceneIdentity() && first.presentation.getCameraRevision(),"ticket transports scene and camera authority");
  auto baseline=historical(first);
  auto old=submit();scene.edit(4,values);
  Image sentinel;sentinel.rgba={42};sentinel.depth={.25f};SbString why;
  require(target->pollCurrentReadback(old,sentinel.rgba,sentinel.depth,&why)==CoinRenderTarget::READBACK_STALE_PRESENTATION,
    "camera-only edit refuses old ticket for current presentation");
  require(sentinel.rgba==std::vector<uint8_t>{42} && sentinel.depth==std::vector<float>{.25f} && why.getLength(),
    "stale refusal preserves output transaction and supplies diagnostic");
  require(same(historical(old),baseline),"camera refusal leaves old ticket unconsumed");
  old=submit();scene.edit(1,values);
  auto latest=submit();auto forged=old;forged.presentation=latest.presentation;
  require(target->pollCurrentReadback(forged,sentinel.rgba,sentinel.depth)==CoinRenderTarget::READBACK_STALE_PRESENTATION,
    "swapping fresh receipt onto stale ticket cannot bypass admission");
  historical(old);
  auto status=CoinRenderTarget::READBACK_NOT_READY;Image current;
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(status==CoinRenderTarget::READBACK_NOT_READY && std::chrono::steady_clock::now()<deadline){
    status=target->pollCurrentReadback(latest,current.rgba,current.depth,&why);
    if(status==CoinRenderTarget::READBACK_NOT_READY)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  require(status==CoinRenderTarget::READBACK_READY && same(current,cold(values,"current-async/latest")),
    "current presentation readback completes and matches independent cold scene");
  auto cancelled=submit();require(CoinRenderTarget::cancelReadback(cancelled),"readback cancellation succeeds");
  require(target->pollCurrentReadback(cancelled,sentinel.rgba,sentinel.depth)!=CoinRenderTarget::READBACK_READY,
    "cancelled readback cannot become a current image");
  auto resized=submit();target->resize(SbVec2i32(64,64));
  require(target->pollCurrentReadback(resized,sentinel.rgba,sentinel.depth)==CoinRenderTarget::READBACK_STALE_PRESENTATION,
    "resize refuses old ticket even when scene revision unchanged");
  historical(resized);
  std::cout<<"PRESENTATION async camera/scene, tampered receipt, cancellation, resize and current readback controls passed\n";
#endif
}

int main(int argc,char**argv){
  std::cout.setf(std::ios::unitbuf);
  SoDB::init();
  CoinRenderAction::initClass();
#if defined(HAVE_COIN_BGFX) || defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  options.renderer=COIN_RENDER_RENDERER_VULKAN;
  if(!coinRetainedTestRenderer(options))return 2;
#endif
  if(argc<2)return 2;
  outputDir=argv[1];
  if(argc>2 && std::string(argv[2])=="--async-only"){asyncBoundary();return failures?1:0;}
  for(int t=0;t<6;++t){mutation(t,false,argv[1]);mutation(t,true,argv[1]);}
  for(int t=0;t<4;++t){mutation(t,false,argv[1],true);mutation(t,true,argv[1],true);}
  transportAndReuse(argv[1]);missingPayload();presentationBoundary();managerRootBoundary();currentAsyncBoundary();
  if(argc<3 || std::string(argv[2])!="--no-async")asyncBoundary();
  std::cout<<"SUMMARY observations="<<observations<<" failures="<<failures<<"\n";
  return failures?1:0;
}
