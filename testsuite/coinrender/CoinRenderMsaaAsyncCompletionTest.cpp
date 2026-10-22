#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderMsaaAsyncFixture.h"
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <limits>
using namespace MsaaAsync;
namespace {
void forgeAllMetadata(const Ticket&ticket) {
  for(unsigned field=0;field<12;++field) {
    Ticket forged=ticket;
    switch(field) {
      case 0:forged.token=0;break;
      case 1:++forged.generation;break;
      case 2:++forged.submissionSerial;break;
      case 3:++forged.width;break;
      case 4:++forged.height;break;
      case 5:++forged.colorFormat;break;
      case 6:++forged.depthFormat;break;
      case 7:++forged.colorRowPitch;break;
      case 8:++forged.depthRowPitch;break;
      case 9:++forged.colorBytes;break;
      case 10:++forged.depthBytes;break;
      default:forged.token=UINT64_MAX;break;
    }
    invalid(forged);require(!Target::cancelReadback(forged),"forged cancel consumed live ticket");
  }
}
void tickets(unsigned n) {
  Scene scene;Harness h(options(n));
  require(bool(h.target->isDepthReadbackEnabled())==(n==1),"MSAA depth requires explicit opt-in");
  // The first producer in this process is async, without a capability query or sync warmup.
  const Ticket cold=h.submit(scene.root);Frame first=poll(cold);const Frame firstSync=h.sync(scene.root);
  require(same(first,firstSync),"cold async frame differs from synchronous frame");invalid(cold);
  require(n==1?!first.depth.empty():first.depth.empty(),"default depth policy");
  h.depth(true);const Frame expected=h.sync(scene.root);coverage(expected,n);
  const Ticket a=h.submit(scene.root);forgeAllMetadata(a);
  scene.version(1);const Frame expectedB=h.sync(scene.root);const Ticket b=h.submit(scene.root);
  require(a.token!=b.token&&a.generation==b.generation&&a.submissionSerial<b.submissionSerial,
    "overlapping tickets have distinct tokens, ordered serials and stable generation");
  require(same(poll(b),expectedB),"newer ticket data");
  // Change policy, dimensions and destroy both owners before consuming the older frame.
  h.depth(false);require(h.target->resize(SbVec2i32(37,23)),"resize owner with pending readback");
  h.action.setRenderTarget(nullptr);h.target.reset();
  const Frame old=poll(a);coverage(old,n);require(same(old,expected),"old ticket survives resize/destruction");invalid(a);
  Harness ring(options(n));ring.depth(true);
  for(unsigned round=0;round<5;++round) {
    std::vector<Ticket> pending;std::vector<Frame> frames;
    for(unsigned i=0;i<5;++i) {
      scene.version(round*5+i);ring.depth((i&1)==0);
      frames.push_back(ring.sync(scene.root));pending.push_back(ring.submit(scene.root));
    }
    for(int i=4;i>=0;--i)require(same(poll(pending[size_t(i)]),frames[size_t(i)]),
      "staging reused across outstanding snapshots or depth policy changed a ticket");
  }
  require(ring.target->resize(SbVec2i32(37,23)),"odd row size resize");
  ring.action.setViewportRegion(SbViewportRegion(37,23));ring.depth(true);
  const Frame odd=ring.sync(scene.root);const Ticket oddTicket=ring.submit(scene.root);
  require(oddTicket.width==37&&oddTicket.height==23&&same(poll(oddTicket),odd),"odd row pitch packing");
  const Ticket cancelled=ring.submit(scene.root);require(Target::cancelReadback(cancelled),"cancel live ticket");
  require(!Target::cancelReadback(cancelled),"cancel is single use");invalid(cancelled);
}
void refusal(Harness&h,SoNode*root,Ticket&ticket,const std::string&label) {
  const Ticket previous=ticket;const Frame frame=read(*h.target);
  const uint64_t serial=h.target->getLastSubmissionSerial();size_t bytes=0;
  const auto *pointer=h.target->borrowRGBA(bytes);const size_t byteCount=bytes;
  CoinRenderCacheTelemetry before{},after{};const bool telemetry=h.target->getCacheTelemetry(before);
  h.action.applyAsync(root,ticket);
  require(h.action.getLastStatus()!=Action::SUCCESS,label+": refused status");
  require(sameTicket(ticket,previous),label+": refusal overwrote live ticket");
  require(same(read(*h.target),frame)&&h.target->getLastSubmissionSerial()==serial,label+": changed publication");
  require(h.target->borrowRGBA(bytes)==pointer&&bytes==byteCount,label+": changed borrowed output");
  if(telemetry&&h.target->getCacheTelemetry(after))require(after.cumulativeUploads==before.cumulativeUploads,
    label+": producer uploaded after admission refusal");
}
void admission(unsigned n) {
  Scene scene;Harness h(options(n));h.depth(true);std::vector<Ticket> pending;std::vector<Frame> frames;
  for(unsigned i=0;i<16;++i) {scene.version(i);frames.push_back(h.sync(scene.root));pending.push_back(h.submit(scene.root));}
  scene.version(20);h.sync(scene.root);RttScene rtt;
  refusal(h,rtt.root,pending[3],"queue16 with a live output ticket");
  Ticket excess;refusal(h,rtt.root,excess,"queue16 with empty output ticket");
  require(h.action.getLastStatus()==Action::NOT_READY&&excess.token==0,"queue16 refuses 17th ticket");
  require(Target::cancelReadback(pending[7]),"cancellation frees queue slot");
  const Frame replacementFrame=h.sync(scene.root);const Ticket replacement=h.submit(scene.root);
  for(int i=15;i>=0;--i)if(i!=7)require(same(poll(pending[size_t(i)]),frames[size_t(i)]),"queue refusal damaged existing snapshot");
  require(same(poll(replacement),replacementFrame),"queue recovery after cancellation");
  scene.version(0);const Frame anchor=h.sync(scene.root);Ticket live=h.submit(scene.root);h.sync(scene.root);
  scene.alpha->value=std::numeric_limits<float>::quiet_NaN();
  refusal(h,scene.root,live,"invalid alpha with live output ticket");
  require(h.action.getLastStatus()==Action::INVALID_SCENE,"NaN alpha rejected as invalid scene");
  scene.alpha->value=.5f;require(same(h.sync(scene.root),anchor),"valid frame recovers after invalid input");
  h.action.setNumPasses(0);refusal(h,rtt.root,live,"invalid multipass before RTT producer");h.action.setNumPasses(1);
  h.action.setNumPasses(3);h.action.setMultipassBufferBudget(0);
  refusal(h,rtt.root,live,"multipass budget before RTT producer");
  h.action.setNumPasses(1);h.action.setMultipassBufferBudget(256ull*1024*1024);
  auto *bump=new SoBumpMap;const uint8_t normal[]={128,128,255};bump->image.setValue(SbVec2s(1,1),3,normal);
  scene.root->insertChild(bump,scene.root->getNumChildren()-1);
  refusal(h,scene.root,live,"active bump async retains independent 1x gate");scene.root->removeChild(bump);
  // No public profile promises async shadows; an otherwise valid active shadow scene stays rejected.
  auto *shadowRoot=new SoShadowGroup;shadowRoot->ref();auto *sun=new SoShadowDirectionalLight;
  shadowRoot->addChild(sun);shadowRoot->addChild(scene.root);
  refusal(h,shadowRoot,live,"active shadows async retains independent 1x gate");shadowRoot->unref();
  require(same(poll(live),anchor),"failed attempt consumed supplied live ticket");
  for(unsigned bad:{0u,3u,8u}) {
    auto o=options(bad);std::unique_ptr<Target> target(Target::createOffscreen(SbVec2i32(side,side),o));
    if(!target)continue;Action action(SbViewportRegion(side,side));action.setRenderTarget(target.get());Ticket rejected;
    action.applyAsync(scene.root,rejected);require(action.getLastStatus()!=Action::SUCCESS&&rejected.token==0&&
      target->getLastSubmissionSerial()==0&&read(*target).rgba.empty(),"invalid sample count admitted or published");
    action.setRenderTarget(nullptr);
  }
  // Payload alone is exactly 16 MiB per depth-enabled frame. The ninth exceeds
  // 128 MiB before the queue16 bound, regardless of staging row alignment.
  Harness budget(options(n),2048,1024);budget.depth(true);std::vector<Ticket> held;
  bool refused=false;
  for(unsigned i=0;i<9;++i) {
    Ticket t;budget.action.applyAsync(scene.root,t);
    if(budget.action.getLastStatus()!=Action::SUCCESS) {
      require(budget.action.getLastStatus()==Action::NOT_READY&&t.token==0&&i==8&&held.size()==8,"128 MiB payload refusal after exactly eight 16 MiB tickets");
      refusal(budget,rtt.root,held.front(),"payload budget preserves live ticket before producers");refused=true;break;
    }
    require(t.token!=0,"budget accepted without ticket");held.push_back(t);
  }
  require(refused,"128 MiB pending payload limit was bypassed");
  for(const auto&t:held)require(Target::cancelReadback(t),"budget cancellation");
  const Ticket recovered=budget.submit(scene.root);require(Target::cancelReadback(recovered),"payload budget recovers");
  budget.action.setRenderTarget(nullptr);budget.target.reset();

  // Capacity counts the packed public payload, not private row padding.
  // 8 * (2047 * 1024 * 8) = 128 MiB - 64 KiB. A ninth 32x32
  // color+depth ticket takes 8 KiB and therefore still leaves 56 KiB free.
  // A row-aligned staging charge would incorrectly reject that small ticket.
  Harness oddBudget(options(n),2047,1024);oddBudget.depth(true);
  std::vector<Ticket> oddHeld;
  for(unsigned i=0;i<8;++i) {
    const Ticket t=oddBudget.submit(scene.root);
    require(t.colorBytes+t.depthBytes==uint64_t(2047)*1024*8,"odd ticket uses packed public payload bytes");
    oddHeld.push_back(t);
  }
  Harness small(options(n));small.depth(true);const Frame smallExpected=small.sync(scene.root);
  const Ticket smallTicket=small.submit(scene.root);
  require(smallTicket.colorBytes+smallTicket.depthBytes==8192,"small ticket uses 8 KiB payload");
  const Frame oddExpected=oddBudget.sync(scene.root);
  Ticket oddExcess;refusal(oddBudget,rtt.root,oddExcess,"odd payload budget rejects another large ticket");
  require(oddBudget.action.getLastStatus()==Action::NOT_READY&&oddExcess.token==0,
    "odd payload budget applies backpressure below queue16");
  refusal(oddBudget,rtt.root,oddHeld.front(),"odd payload refusal preserves live ticket");
  require(oddBudget.action.getLastStatus()==Action::NOT_READY,"live odd ticket refusal reports backpressure");
  require(same(poll(smallTicket),smallExpected),"odd payload refusal damaged accepted small ticket");
  require(same(poll(oddHeld.front()),oddExpected),"odd packed rows or accepted large snapshot corrupted");
  invalid(oddHeld.front());
  for(size_t i=1;i<oddHeld.size();++i)require(Target::cancelReadback(oddHeld[i]),"odd budget cancellation");
  const Ticket oddRecovered=oddBudget.submit(scene.root);
  require(Target::cancelReadback(oddRecovered),"odd payload capacity recovers after release");
}
void graph(unsigned n,const std::string&family,bool direct) {
  auto o=options(n,direct);
  if(family=="weighted")o.transparency=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  if(family=="peeling")o.transparency=COIN_RENDER_TRANSPARENCY_PEELING;
  Harness h(o);h.depth(true);Scene scene;RttScene rtt;SoNode *root=scene.root;
  if(family=="rtt")root=rtt.root;
  else if(family=="texture") {
    auto *texture=new SoTexture2;const uint8_t texels[]={255,32,8,255,16,240,32,255,32,16,248,255,224,128,16,255};
    texture->image.setValue(SbVec2s(2,2),4,texels);texture->model=SoTexture2::MODULATE;
    scene.root->insertChild(texture,scene.root->getNumChildren()-1);
    auto *uv=new SoTextureCoordinate2;const SbVec2f t[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(0,1)};
    uv->point.setValues(0,3,t);scene.root->insertChild(uv,scene.root->getNumChildren()-1);
  } else if(family=="blend"||family=="weighted"||family=="peeling") {
    scene.material->transparency=.375f;
    h.action.setTransparencyType(family=="peeling"?Action::SORTED_LAYERS_BLEND:Action::SORTED_OBJECT_BLEND);
    auto *second=new SoSeparator;auto *m=new SoMaterial;m->diffuseColor.setValue(0,1,0);m->transparency=.5f;
    second->addChild(m);second->addChild(quad());scene.root->insertChild(second,2);
  } else if(family=="layers") {
    auto *note=new SoAnnotation;auto *m=new SoMaterial;m->diffuseColor.setValue(0,0,1);note->addChild(m);
    auto *patch=quad();auto *v=static_cast<SoCoordinate3*>(patch->getChild(0));
    const SbVec3f p[]={SbVec3f(.6f,.6f,-.5f),SbVec3f(.95f,.6f,-.5f),SbVec3f(.95f,.95f,-.5f),SbVec3f(.6f,.95f,-.5f)};
    v->point.setValues(0,4,p);note->addChild(patch);scene.root->addChild(note);h.action.addManagerLayerBoundary(note,TRUE);
  } else if(family=="multipass")h.action.setNumPasses(3);
  else require(family=="plain","unknown graph family");
  // Each graph runs async first in a fresh process, including RTT producers.
  const Ticket cold=h.submit(root);const Frame first=poll(cold);const Frame firstSync=h.sync(root);
  require(same(first,firstSync),"cold graph async differs from sync: "+family);
  std::vector<Frame> expected;std::vector<Ticket> queued;
  for(unsigned i=0;i<5;++i) {
    if(family=="rtt")rtt.producer.version(i);else scene.version(i);
    expected.push_back(h.sync(root));queued.push_back(h.submit(root));
  }
  require(expected.front().rgba!=expected[1].rgba,"graph mutation must affect visible pixels");
  // A new synchronous frame while several producers remain live cannot replace their snapshots.
  if(family=="rtt")rtt.producer.version(13);else scene.version(13);h.sync(root);
  for(int i=4;i>=0;--i)require(same(poll(queued[size_t(i)]),expected[size_t(i)]),"graph out-of-order snapshot: "+family);
}
void pipeline(unsigned n,unsigned depth) {
  // Reference targets use depth1; pipeline targets are created only after restoring the requested ring.
  const std::string requested=std::to_string(depth);setenv("COIN_BGFX_READBACK_PIPELINE_DEPTH","1",1);
  Scene scene;Harness reference(options(n));reference.depth(true);
  std::vector<Frame> controls;
  for(unsigned i=0;i<12;++i){scene.version(i);reference.depth(i<4||i>=8);controls.push_back(reference.sync(scene.root));}
  setenv("COIN_BGFX_READBACK_PIPELINE_DEPTH",requested.c_str(),1);
  Harness h(options(n));h.depth(true);int last=-1;
  std::vector<Ticket> async;std::vector<Frame> asyncExpected;
  for(unsigned i=0;i<12;++i) {
    scene.version(i);if(i==4)h.depth(false);if(i==8)h.depth(true);
    h.action.apply(scene.root);require(h.action.getLastStatus()==Action::SUCCESS,
      std::string("MSAA pipeline sync failed: ")+h.action.getLastError().getString());
    const Frame result=read(*h.target);
    if(!result.rgba.empty()) {
      int matched=-1;for(unsigned j=(i>=depth?i-depth:0);j<=i;++j)if(same(result,controls[j]))matched=int(j);
      require(matched>=0,"pipeline combined color/depth from different frames or slot policies");
      require(matched>=last,"pipeline publication regressed");last=matched;
      if(depth==1)require(matched==int(i),"depth1 must publish current frame");
    }
    if(i==2||i==6||i==10) {async.push_back(h.submit(scene.root));asyncExpected.push_back(controls[i]);}
  }
  require(last>=int(12-depth),"pipeline did not advance within its bounded lag");
  for(int i=2;i>=0;--i)require(same(poll(async[size_t(i)]),asyncExpected[size_t(i)]),"pipeline reused ticket staging/tail");
  scene.material->diffuseColor.setValue(1,0,0);scene.setZ(0);h.depth(true);
  Frame settled;
  for(unsigned i=0;i<depth+2;++i){h.action.apply(scene.root);require(h.action.getLastStatus()==Action::SUCCESS,"pipeline settles");settled=read(*h.target);}
  coverage(settled,n);
  require(h.target->resize(SbVec2i32(48,40)),"pipeline resize");h.action.setViewportRegion(SbViewportRegion(48,40));
  for(unsigned i=0;i<depth+2;++i){h.action.apply(scene.root);require(h.action.getLastStatus()==Action::SUCCESS,"resized pipeline");}
  require(read(*h.target).rgba.size()==48u*40u*4u&&read(*h.target).depth.size()==48u*40u,"pipeline old dimensions leaked");
}
}
int main(int argc,char**argv) {
  unsigned n=1,depth=1;std::string mode="tickets",family="plain";bool direct=false;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--samples"&&i+1<argc)n=unsigned(std::stoul(argv[++i]));
    else if(arg=="--mode"&&i+1<argc)mode=argv[++i];
    else if(arg=="--family"&&i+1<argc)family=argv[++i];
    else if(arg=="--pipeline-depth"&&i+1<argc)depth=unsigned(std::stoul(argv[++i]));
    else if(arg=="--direct")direct=true;else if(arg=="--staged")direct=false;else return 2;
  }
  if((n!=1&&n!=2&&n!=4)||depth<1||depth>3)return 2;
  SoDB::init();Action::initClass();
  try {
    if(mode=="tickets")tickets(n);else if(mode=="admission")admission(n);
    else if(mode=="graph")graph(n,family,direct);else if(mode=="pipeline")pipeline(n,depth);else return 2;
    std::cout<<"msaa_async PASS mode="<<mode<<" samples="<<n<<" family="<<family<<" pipeline="<<depth<<'\n';return 0;
  }catch(const std::exception&e){std::cerr<<"msaa_async FAIL mode="<<mode<<" samples="<<n<<": "<<e.what()<<'\n';return 1;}
}
