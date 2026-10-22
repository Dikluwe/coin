#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderPortableApiFixture.h"
#if !defined(COIN_RENDER_ACTION_OPTIONS_VERSION) || COIN_RENDER_ACTION_OPTIONS_VERSION < 1
int main() {
  std::cout<<"CoinRender portable action API contract absent (baseline headers)\n";
  return 78;
}
#else
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/lists/SoPathList.h>
#include <chrono>
#include <limits>
#include <set>
#include <thread>

using namespace portable_api;
namespace {
struct Visits {
  int count=0,delayed=0;
  static void call(void *data,SoAction *action) {
    auto &self=*static_cast<Visits*>(data);
    if(action->isOfType(CoinRenderAction::getClassTypeId())) {
      ++self.count;
      self.delayed+=static_cast<CoinRenderAction*>(action)->isRenderingDelayedPaths()!=FALSE;
    }
  }
  SoCallback *node() {auto *n=new SoCallback;n->setCallback(call,this);return n;}
};
struct Abort {
  CoinRenderAction *action=nullptr;
  SoNode *watched=nullptr;
  CoinRenderAction::AbortCode code=CoinRenderAction::CONTINUE;
  unsigned calls=0,hits=0,replays=0;
  bool validPath=true;
  static CoinRenderAction::AbortCode call(void *data) {
    auto &self=*static_cast<Abort*>(data);++self.calls;
    const auto *path=self.action->getCurPath();
    self.validPath &= path && path->getLength()>0;
    if(path&&path->getTail()==self.watched) {
      ++self.hits;self.replays+=self.action->isRenderingDelayedPaths()!=FALSE;return self.code;
    }
    return CoinRenderAction::CONTINUE;
  }
};
struct Queue {
  SoPath *path=nullptr;
  unsigned queued=0;
  bool accepted=true;
  static void call(void *data,SoAction *action) {
    if(!action->isOfType(CoinRenderAction::getClassTypeId()))return;
    auto &self=*static_cast<Queue*>(data);auto *render=static_cast<CoinRenderAction*>(action);
    if(!render->isRenderingDelayedPaths()&&self.path) {
      self.accepted &= render->addDelayedPath(self.path)!=FALSE;++self.queued;
      // The queue must own a copy and not borrow our mutable path object.
      self.path->pop();
    }
  }
};
struct Pre {
  std::string order;
  Quad *quad=nullptr;
  SoNode *nestedRoot=nullptr;
  bool editList=false,nested=false,nestedRefused=false;
  unsigned count=0;
  static void c(void *data,CoinRenderAction *) {static_cast<Pre*>(data)->order+='C';}
  static void b(void *data,CoinRenderAction *) {static_cast<Pre*>(data)->order+='B';}
  static void a(void *data,CoinRenderAction *action) {
    auto &self=*static_cast<Pre*>(data);self.order+='A';++self.count;
    if(self.quad)self.quad->material->diffuseColor.setValue(self.count%2?SbColor(0,1,0):SbColor(1,0,0));
    if(self.editList&&self.count==1) {
      action->removePreRenderCallback(b,data);action->addPreRenderCallback(c,data);
    }
    if(self.nested) {
      self.nested=false;action->apply(self.nestedRoot);
      self.nestedRefused=action->getLastStatus()!=CoinRenderAction::SUCCESS;
    }
  }
};
void callbacks(Fixture &f) {
  Scene scene;Quad quad;scene.root->addChild(quad.root);
  Pre pre;pre.quad=&quad;pre.editList=true;
  f.action.addPreRenderCallback(Pre::a,&pre);f.action.addPreRenderCallback(Pre::b,&pre);
  f.action.addPreRenderCallback(nullptr,nullptr);f.action.removePreRenderCallback(nullptr,nullptr);
  check(f.apply(scene.root,"callbacks first")&&pre.order=="AB","snapshot retains removed callback for current apply");
  centerColor(*f.target,0,255,0,"pre-render scene mutation reaches current frame");
  pre.order.clear();f.apply(scene.root,"callbacks next");
  check(pre.order=="AC","registration edits take effect exactly next apply");
  centerColor(*f.target,255,0,0,"second callback mutation");
  f.action.removePreRenderCallback(Pre::a,&pre);f.action.removePreRenderCallback(Pre::c,&pre);
  // Same function, different closure: removal is by the complete pair.
  Pre other;f.action.addPreRenderCallback(Pre::b,&pre);f.action.addPreRenderCallback(Pre::b,&other);
  f.action.removePreRenderCallback(Pre::b,&pre);pre.order.clear();
  f.apply(scene.root,"pair removal");check(pre.order.empty()&&other.order=="B","remove only matching closure");
  other.order.clear();f.apply(scene.root,"stable-ID apply");
  check(other.order=="B","pre-render invoked on stable-ID reuse");
  f.action.removePreRenderCallback(Pre::b,&other);
  Pre nested;nested.nestedRoot=scene.root;nested.nested=true;
  f.action.addPreRenderCallback(Pre::a,&nested);
  const auto beforeNested=read(*f.target);
  f.action.apply(scene.root);
  check(nested.count==1&&nested.nestedRefused&&f.action.getLastStatus()==CoinRenderAction::INVALID_SCENE&&
    f.action.getLastError().getLength()>0,"nested apply rejects both nested and outer candidates with coherent diagnostic");
  unchanged(*f.target,beforeNested,"nested apply in pre-render");
  f.action.removePreRenderCallback(Pre::a,&nested);
  Pre invalid;f.action.addPreRenderCallback(Pre::a,&invalid);
  f.action.setNumPasses(0);const auto stable=read(*f.target);f.action.apply(scene.root);
  check(f.action.getLastStatus()!=CoinRenderAction::SUCCESS&&invalid.count==0,
    "invalid request rejects before pre-render callbacks");unchanged(*f.target,stable,"invalid pass count");
  f.action.setNumPasses(1);f.action.setRenderTarget(nullptr);f.action.apply(scene.root);
  check(f.action.getLastStatus()==CoinRenderAction::SUCCESS&&invalid.count==1&&f.action.getRecordingLog().getLength()>0,
    "synchronous no-target Recording captures and calls pre-render once");
  invalid.count=0;CoinRenderReadbackTicket noTargetTicket;noTargetTicket.token=31;noTargetTicket.generation=73;
  noTargetTicket.submissionSerial=101;noTargetTicket.width=7;noTargetTicket.height=11;
  f.action.applyAsync(scene.root,noTargetTicket);
  check(f.action.getLastStatus()==CoinRenderAction::NO_TARGET&&invalid.count==0&&noTargetTicket.token==31&&
    noTargetTicket.generation==73&&noTargetTicket.submissionSerial==101&&noTargetTicket.width==7&&noTargetTicket.height==11,
    "async no-target rejects before callbacks and preserves output ticket");
  f.action.setRenderTarget(f.target.get());f.action.removePreRenderCallback(Pre::a,&invalid);
}

void aborts(Fixture &f) {
  Scene scene;Quad left(-.9f,-.1f),right(.1f,.9f);
  right.material->diffuseColor.setValue(0,1,0);Visits first,second;
  left.root->insertChild(first.node(),0);right.root->insertChild(second.node(),0);
  scene.root->addChild(left.root);scene.root->addChild(right.root);
  f.apply(scene.root,"abort baseline");const auto baseline=read(*f.target);
  Abort control;control.action=&f.action;control.watched=right.root;control.code=CoinRenderAction::ABORT;
  f.action.setAbortCallback(Abort::call,&control);
  auto *function=&Abort::call;void *closure=nullptr;
  f.action.getAbortCallback(function,closure);
  check(function==Abort::call&&closure==&control,"abort callback getter returns exact pair");
  const auto calls=control.calls;check(!f.action.abortNow()&&control.calls==calls,"abortNow is inert outside apply");
  const unsigned submits=f.recorder?f.recorder->submits:0;
  f.action.apply(scene.root);
  check(f.action.getLastStatus()==CoinRenderAction::ABORTED&&control.validPath&&control.hits>0,
    "abort reports ABORTED with current traversal path");
  check(!f.recorder||f.recorder->submits==submits,"abort never reaches backend");
  unchanged(*f.target,baseline,"abort after earlier captured sibling");
  for(int invalidCode:{-1,99}) {
    const auto previous=read(*f.target);const unsigned count=f.recorder?f.recorder->submits:0;
    std::size_t legacyBytes=0;const auto *legacy=f.target->borrowLegacyComponents(CoinRenderLegacyComponents::LUMINANCE,legacyBytes);
    const auto legacyCopy=legacy?std::vector<uint8_t>(legacy,legacy+legacyBytes):std::vector<uint8_t>();
    control.code=static_cast<CoinRenderAction::AbortCode>(invalidCode);f.action.apply(scene.root);
    check(f.action.getLastStatus()==CoinRenderAction::INVALID_SCENE,"invalid abort callback enum rejects as INVALID_SCENE");
    check(!f.recorder||f.recorder->submits==count,"invalid abort callback enum rejected before backend");
    unchanged(*f.target,previous,"invalid abort enum rollback");std::size_t afterBytes=0;
    const auto *after=f.target->borrowLegacyComponents(CoinRenderLegacyComponents::LUMINANCE,afterBytes);
    check(legacy&&after==legacy&&afterBytes==legacyBytes&&std::equal(legacyCopy.begin(),legacyCopy.end(),after),
      "invalid abort callback preserves active component borrow bytes and allocation");
    control.code=CoinRenderAction::CONTINUE;f.apply(scene.root,"valid abort callback recovery");
  }
  control.code=CoinRenderAction::PRUNE;first.count=second.count=0;
  f.apply(scene.root,"prune subtree");
  check(first.count==1&&second.count==0,"PRUNE skips only selected subtree");
  control.code=CoinRenderAction::CONTINUE;first.count=second.count=0;
  f.apply(scene.root,"continue without scene edit");
  check(first.count==1&&second.count==1,"callback change invalidates same-ID traversal proof");
  control.code=CoinRenderAction::DELAY;control.replays=0;first.count=second.count=second.delayed=0;
  f.apply(scene.root,"delay subtree");
  check(first.count==1&&second.count==1&&second.delayed==1,
    "DELAY replays subtree once with observable delayed state");
  check(!f.action.isRenderingDelayedPaths(),"delayed state restored after apply");
  control.code=CoinRenderAction::CONTINUE;
  SoPath *leftPath=new SoPath(scene.root);leftPath->ref();leftPath->append(2);
  SoPath *rightPath=new SoPath(scene.root);rightPath->ref();rightPath->append(3);
  first.count=second.count=0;f.action.apply(leftPath);
  check(f.action.getLastStatus()==CoinRenderAction::SUCCESS&&first.count==1&&second.count==0,
    "path application preserves selected traversal");
  first.count=second.count=0;SoPathList list;list.append(leftPath);list.append(rightPath);f.action.apply(list,FALSE);
  check(f.action.getLastStatus()==CoinRenderAction::SUCCESS&&first.count==1&&second.count==1,
    "path-list application handles both occurrences");
  leftPath->unref();rightPath->unref();f.action.setAbortCallback(nullptr,nullptr);
  // Capture callbacks before ABORT may run; executable producer/backend work may not.
  Scene withProducer;Quad stop;Visits producerCalls;auto *producer=new SoSeparator;
  producer->addChild(producerCalls.node());producer->addChild(new SoCube);
  auto *texture=new SoSceneTexture2;texture->size=SbVec2s(8,8);texture->scene=producer;
  withProducer.root->addChild(texture);withProducer.root->addChild(stop.root);
  control.watched=stop.root;control.code=CoinRenderAction::ABORT;
  f.action.setAbortCallback(Abort::call,&control);const auto prior=read(*f.target);
  const unsigned parentBefore=f.recorder?f.recorder->submits:0,producerBefore=f.recorder?f.recorder->rttSubmits:0;
  f.action.apply(withProducer.root);
  check(f.action.getLastStatus()==CoinRenderAction::ABORTED,
    "late abort rejects scene with earlier RTT capture");
  check(!f.recorder||(f.recorder->submits==parentBefore&&f.recorder->rttSubmits==producerBefore),
    "late abort prevents parent and RTT execution after any earlier capture callbacks");
  unchanged(*f.target,prior,"producer abort");
  f.action.setAbortCallback(nullptr,nullptr);
  Scene manual;Quad deferred;Visits manualVisits;deferred.root->insertChild(manualVisits.node(),0);
  Queue queue;auto *enqueue=new SoCallback;enqueue->setCallback(Queue::call,&queue);
  manual.root->addChild(enqueue);manual.root->addChild(deferred.root);
  queue.path=new SoPath(manual.root);queue.path->ref();queue.path->append(3);
  check(!f.action.addDelayedPath(nullptr),"NULL delayed path refuses safely");
  f.apply(manual.root,"explicit copied delayed path");
  check(queue.accepted&&queue.queued==1&&manualVisits.count==2&&manualVisits.delayed==1,
    "addDelayedPath copies caller path and replays once after normal traversal");
  queue.path->unref();queue.path=nullptr;
}

void invalidate(Fixture &f) {
  Scene scene;Quad quad;scene.root->addChild(quad.root);f.apply(scene.root,"invalidate baseline red");
  const auto stable=read(*f.target);const auto rootId=scene.root->getNodeId(),materialId=quad.material->getNodeId();
  const SbBool previous=quad.material->diffuseColor.enableNotify(FALSE);
  quad.material->diffuseColor.setValue(0,1,0);quad.material->diffuseColor.enableNotify(previous);
  check(scene.root->getNodeId()==rootId&&quad.material->getNodeId()==materialId,
    "unnotified field edit leaves traversal identity unchanged");
  f.action.invalidateState();unchanged(*f.target,stable,"invalidateState alone");
  f.apply(scene.root,"invalidate recaptures unnotified edit");centerColor(*f.target,0,255,0,"invalidateState observes current field");
  const auto green=read(*f.target);f.action.invalidateState();unchanged(*f.target,green,"repeat invalidate");
  f.apply(scene.root,"invalidate reuse recovery");centerColor(*f.target,0,255,0,"repeat invalidate keeps current scene");
}

struct Sorting {
  CoinRenderAction *action=nullptr;
  SoNode *red=nullptr,*blue=nullptr;
  float redDistance=2,blueDistance=1;
  unsigned redCalls=0,blueCalls=0;
  bool paths=true;
  static float call(void *data,CoinRenderAction *action) {
    auto &self=*static_cast<Sorting*>(data);const auto *path=action->getCurPath();
    self.paths &= action==self.action&&path&&path->getLength()>0;
    SoNode *tail=path?path->getTail():nullptr;
    if(tail==self.red){++self.redCalls;return self.redDistance;}
    if(tail==self.blue){++self.blueCalls;return self.blueDistance;}
    self.paths=false;return 0;
  }
};
class AuthoredCenterFace : public SoFaceSet {
public:
  float centerZ=-1;
protected:
  void computeBBox(SoAction *action,SbBox3f &box,SbVec3f &center) override {
    SoFaceSet::computeBBox(action,box,center);
    // The authored sorting center may differ from both the box midpoint and
    // the primitive vertex mean; Open Inventor supplies it independently.
    center.setValue(0,0,centerZ);
  }
};
std::vector<SbUniqueId> orderIds(const Recorder &recorder) {
  std::vector<SbUniqueId> result;
  for(const auto &item:recorder.order) if(item.blend&&item.sortObject)
    result.push_back(recorder.frame.draws[item.drawIndex].sourceNodeId);
  return result;
}
void sorting(Fixture &f) {
  Scene scene;scene.camera->position.setValue(0,0,10);scene.camera->nearDistance=1;scene.camera->farDistance=30;
  auto *redRoot=new SoSeparator,*blueRoot=new SoSeparator;
  auto *redMaterial=new SoMaterial,*blueMaterial=new SoMaterial;
  redMaterial->diffuseColor.setValue(1,0,0);blueMaterial->diffuseColor.setValue(0,0,1);
  redMaterial->transparency=.5f;blueMaterial->transparency=.5f;
  auto *red=new SoCube,*blue=new SoCube;red->width=1;red->height=1;red->depth=8;
  blue->width=1;blue->height=1;blue->depth=.5f;
  auto *translation=new SoTranslation;translation->translation.setValue(0,0,-1);
  redRoot->addChild(redMaterial);redRoot->addChild(red);
  blueRoot->addChild(translation);blueRoot->addChild(blueMaterial);blueRoot->addChild(blue);
  scene.root->addChild(redRoot);scene.root->addChild(blueRoot);
  f.action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  f.action.setDelayedObjDepthWrite(FALSE);
  for(auto strategy:{CoinRenderAction::BBOX_CENTER,CoinRenderAction::BBOX_CLOSEST_CORNER,
                     CoinRenderAction::BBOX_FARTHEST_CORNER,CoinRenderAction::BBOX_CENTER}) {
    f.action.setSortedObjectOrderStrategy(strategy);f.apply(scene.root,"authored bbox strategy");
    if(f.recorder) {
      auto ids=orderIds(*f.recorder);
      const auto first=strategy==CoinRenderAction::BBOX_FARTHEST_CORNER?red->getNodeId():blue->getNodeId();
      check(ids.size()==2&&ids[0]==first&&ids[1]!=(first),"center/closest/farthest transformed box order");
    }
  }
  {
    Scene author;Quad redFace,blueFace(-.75f,.75f,-.75f,.75f,-.25f);
    redFace.material->transparency=.5f;blueFace.material->transparency=.5f;
    blueFace.material->diffuseColor.setValue(0,0,1);
    auto *centered=new AuthoredCenterFace;centered->numVertices=4;
    redFace.root->replaceChild(redFace.shape,centered);redFace.shape=centered;
    author.root->addChild(redFace.root);author.root->addChild(blueFace.root);
    for(float centerZ:{-1.f,.5f,-1.f}) {
      centered->centerZ=centerZ;centered->touch();
      f.action.setSortedObjectOrderStrategy(CoinRenderAction::BBOX_CENTER);
      f.apply(author.root,"authored computeBBox center A/B/A");
      centerColor(*f.target,centerZ<0?64:128,0,centerZ<0?128:64,
        "computeBBox authored center orders independently of unchanged geometry and box");
      if(f.recorder) {
        const auto ids=orderIds(*f.recorder);
        check(ids.size()==2&&ids[0]==(centerZ<0?centered->getNodeId():blueFace.shape->getNodeId()),
          "authored center drives the actual submitted composition");
      }
    }
    for(auto strategy:{CoinRenderAction::BBOX_CLOSEST_CORNER,CoinRenderAction::BBOX_FARTHEST_CORNER}) {
      f.action.setSortedObjectOrderStrategy(strategy);f.apply(author.root,"author center with geometric corner strategy");
      centerColor(*f.target,128,0,64,"corner strategy still uses transformed box corners rather than authored center");
    }
    f.action.setSortedObjectOrderStrategy(CoinRenderAction::BBOX_CENTER);
  }
  // Faces, rather than solid cubes, give a direct two-source-over pixel oracle.
  Scene flat;Quad a,b;a.material->transparency=.5f;b.material->transparency=.5f;
  b.material->diffuseColor.setValue(0,0,1);flat.root->addChild(a.root);flat.root->addChild(b.root);
  Sorting custom;custom.action=&f.action;custom.red=a.shape;custom.blue=b.shape;
  for(int phase:{0,1,0}) {
    custom.redDistance=phase?1:2;custom.blueDistance=phase?2:1;custom.redCalls=custom.blueCalls=0;
    f.action.setSortedObjectOrderStrategy(CoinRenderAction::CUSTOM_CALLBACK,Sorting::call,&custom);
    f.apply(flat.root,"custom A/B/A sorting");
    check(custom.paths&&custom.redCalls==1&&custom.blueCalls==1,
      "custom called once per transparent occurrence with current path");
    centerColor(*f.target,phase?128:64,0,phase?64:128,"custom greater distance first");
  }
  custom.redDistance=custom.blueDistance=1;f.apply(flat.root,"stable equal distances");
  centerColor(*f.target,64,0,128,"custom ties preserve scene occurrence order");
  Sorting alternate=custom;alternate.redDistance=1;alternate.blueDistance=2;
  f.action.setSortedObjectOrderStrategy(CoinRenderAction::CUSTOM_CALLBACK,Sorting::call,&alternate);
  f.apply(flat.root,"change closure on unchanged graph");centerColor(*f.target,128,0,64,"closure change consumed");
  const auto stable=read(*f.target);const unsigned count=f.recorder?f.recorder->submits:0;
  for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity()}) {
    custom.redDistance=invalid;f.action.setSortedObjectOrderStrategy(CoinRenderAction::CUSTOM_CALLBACK,Sorting::call,&custom);
    f.action.apply(flat.root);check(f.action.getLastStatus()!=CoinRenderAction::SUCCESS,"nonfinite custom distance rejects");
    unchanged(*f.target,stable,"invalid sorted distance");
    check(!f.recorder||f.recorder->submits==count,"nonfinite distance rejected before backend");
  }
  f.action.setSortedObjectOrderStrategy(CoinRenderAction::CUSTOM_CALLBACK,nullptr,nullptr);
  f.action.apply(flat.root);check(f.action.getLastStatus()!=CoinRenderAction::SUCCESS,"missing custom callback rejects");
  unchanged(*f.target,stable,"missing sort callback");
  // A shared shape is two occurrences, even when its source node ID is equal.
  custom.redDistance=2;custom.redCalls=custom.blueCalls=0;
  b.root->removeChild(b.shape);b.root->addChild(a.shape);custom.blue=a.shape;
  f.action.setSortedObjectOrderStrategy(CoinRenderAction::CUSTOM_CALLBACK,Sorting::call,&custom);
  f.apply(flat.root,"shared shape occurrences");
  check(custom.redCalls==2&&custom.blueCalls==0,"callback once per occurrence, not once per source ID");
  // Immediate alpha does not consume an object sorting strategy.
  custom.redCalls=0;f.action.setTransparencyType(CoinRenderAction::BLEND);
  f.apply(flat.root,"immediate blend unaffected");check(custom.redCalls==0,"immediate blend never calls custom object sorter");
  f.action.setSortedObjectOrderStrategy(CoinRenderAction::BBOX_CENTER);
}

void depths(Fixture &f) {
  check(!f.action.getDelayedObjDepthWrite(),"native default delayed depth write is FALSE");
  Scene scene;Quad transparent;transparent.material->transparency=.5f;scene.root->addChild(transparent.root);
  auto *authored=new SoDepthBuffer;authored->ref();authored->write=TRUE;
  for(auto modality:{CoinRenderAction::DELAYED_BLEND,CoinRenderAction::SORTED_OBJECT_BLEND,
                      CoinRenderAction::DELAYED_ADD,CoinRenderAction::SORTED_OBJECT_ADD}) {
    f.action.setTransparencyType(modality);
    for(int authoredWrite:{-1,0,1}) {
      if(authoredWrite>=0) {authored->write=authoredWrite!=0;transparent.root->insertChild(authored,0);}
      for(bool policy:{false,true,false}) {
        f.action.setDelayedObjDepthWrite(policy);
        check(bool(f.action.getDelayedObjDepthWrite())==policy,"delayed depth getter round trip");
        f.apply(scene.root,"delayed depth policy");
        const bool expected=authoredWrite<0?policy:authoredWrite!=0;
        auto pixels=read(*f.target);const size_t p=12*32+16;
        check(pixels.depth.size()==32*24&&std::abs(pixels.depth[p]-(expected?.5f:1.f))<1e-5f,
          "authored write overrides replay default without changing depth test/range");
        if(f.recorder)for(const auto &item:f.recorder->order)
          if(item.blend)check(item.depthWrite==expected&&item.depthTest&&
            item.depthFunction==(authoredWrite<0?CoinRenderDepthFunction::LEQUAL:CoinRenderDepthFunction::LESS)&&
            item.depthRange[0]==0&&item.depthRange[1]==1,
            "composition changes only delayed depth write default");
      }
      if(authoredWrite>=0)transparent.root->removeChild(authored);
    }
  }
  // Immediate/opaque paths retain their ordinary write state under either setting.
  for(auto modality:{CoinRenderAction::BLEND,CoinRenderAction::NONE}) {
    f.action.setTransparencyType(modality);f.action.setDelayedObjDepthWrite(FALSE);
    f.apply(scene.root,"immediate depth independent");
    auto pixels=read(*f.target);check(pixels.depth.size()==32*24&&std::abs(pixels.depth[12*32+16]-.5f)<1e-5f,
      "delayed depth option cannot suppress immediate or opaque writes");
  }
  authored->unref();
}

bool inside(int x,int y,const SbViewportRegion &viewport,const SbVec2f &origin,const SbVec2f &size) {
  const int left=int(std::floor(origin[0]*32)),bottom=int(std::floor(origin[1]*24));
  const int right=left+int(size[0]*32),top=bottom+int(size[1]*24);
  const auto vp=viewport.getViewportOriginPixels(),extent=viewport.getViewportSizePixels();
  return x>=left&&x<right&&y>=bottom&&y<top&&x>=vp[0]&&x<vp[0]+extent[0]&&y>=vp[1]&&y<vp[1]+extent[1];
}
void updates(Fixture &f) {
  SbVec2f origin,size;f.action.getUpdateArea(origin,size);
  check(origin==SbVec2f(0,0)&&size==SbVec2f(1,1),"default update covers canvas");
  Scene scene;Quad quad(-1,1,-1,1);scene.root->addChild(quad.root);f.apply(scene.root,"full initial red");
  const auto red=read(*f.target);quad.material->diffuseColor.setValue(0,1,0);
  SbViewportRegion viewport(32,24);viewport.setViewportPixels(4,3,24,18);f.action.setViewportRegion(viewport);
  const SbVec2f partialOrigin(.2f,.2f),partialSize(.4f,.35f);
  f.action.setUpdateArea(partialOrigin,partialSize);f.apply(scene.root,"partial green");
  f.action.getUpdateArea(origin,size);check(origin==partialOrigin&&size==partialSize,"update getter exact requested area");
  auto green=read(*f.target);unsigned bad=0,changed=0;
  if(check(green.rgba.size()==red.rgba.size()&&green.depth.size()==red.depth.size(),"partial image complete"))
    for(int y=0;y<24;++y)for(int x=0;x<32;++x) {
      const size_t p=(23-y)*32+x;
      if(inside(x,y,viewport,partialOrigin,partialSize)) {
        changed+=green.rgba[p*4+1]>250;
        bad+=green.rgba[p*4]>2||green.rgba[p*4+1]<253||green.rgba[p*4+2]>2||std::abs(green.depth[p]-.5f)>1e-5f;
      } else {
        for(unsigned c=0;c<4;++c)bad+=green.rgba[p*4+c]!=red.rgba[p*4+c];
        bad+=green.depth[p]!=red.depth[p];
      }
    }
  std::cout<<"update_area bad="<<bad<<" changed="<<changed<<" serial_before="<<red.serial<<" serial_after="<<green.serial<<'\n';
  check(bad==0&&changed>0&&green.serial==red.serial+1,"native bottom-left update intersects viewport and preserves exterior");
  const unsigned submits=f.recorder?f.recorder->submits:0;
  f.action.setUpdateArea(SbVec2f(.25f,.25f),SbVec2f(0,.5f));f.apply(scene.root,"empty update");
  unchanged(*f.target,green,"empty update");check(!f.recorder||f.recorder->submits==submits,"empty area skips backend");
  const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
  const SbVec2f invalidOrigins[]={SbVec2f(-.01f,0),SbVec2f(1.01f,0),SbVec2f(nan,0),SbVec2f(0,inf)};
  for(const auto &invalid:invalidOrigins) {
    f.action.setUpdateArea(invalid,SbVec2f(.25f,.25f));f.action.apply(scene.root);
    check(f.action.getLastStatus()!=CoinRenderAction::SUCCESS,"invalid update origin rejects");unchanged(*f.target,green,"invalid area origin");
  }
  for(const SbVec2f invalid:{SbVec2f(-.1f,.5f),SbVec2f(.8f,.5f),SbVec2f(.5f,nan),SbVec2f(inf,.5f)}) {
    f.action.setUpdateArea(SbVec2f(.25f,.25f),invalid);f.action.apply(scene.root);
    check(f.action.getLastStatus()!=CoinRenderAction::SUCCESS,"invalid update extent or overflowing sum rejects");
    unchanged(*f.target,green,"invalid area extent");
  }
  f.action.setViewportRegion(SbViewportRegion(32,24));f.action.setUpdateArea(partialOrigin,partialSize);
  f.target->resize(SbVec2i32(32,24));f.action.setBackgroundColor(SbColor4f(0,0,1,1));
  f.apply(scene.root,"first publication partial");auto first=read(*f.target);bad=0;
  if(check(first.rgba.size()==32*24*4&&first.depth.size()==32*24,"first partial complete"))
    for(int y=0;y<24;++y)for(int x=0;x<32;++x)if(!inside(x,y,f.action.getViewportRegion(),partialOrigin,partialSize)) {
      const size_t p=(23-y)*32+x;
      bad+=first.rgba[p*4]!=0||first.rgba[p*4+1]!=0||first.rgba[p*4+2]!=255||first.rgba[p*4+3]!=255||first.depth[p]!=1;
    }
  check(bad==0,"first partial exterior initialized to current background/depth one");
  const auto stable=read(*f.target);CoinRenderReadbackTicket ticket;
  f.action.applyAsync(scene.root,ticket);
  check(f.action.getLastStatus()==CoinRenderAction::UNSUPPORTED,"partial update async refuses unimplemented cross early");
  unchanged(*f.target,stable,"partial async refusal");
  f.action.setUpdateArea(SbVec2f(0,0),SbVec2f(1,1));f.apply(scene.root,"full area recovery");
  centerColor(*f.target,0,255,0,"full update recovery");
}

void tickets(Fixture &f) {
  Scene scene;Quad quad;scene.root->addChild(quad.root);f.apply(scene.root,"ticket synchronous red");
  quad.material->diffuseColor.setValue(0,1,0);
  CoinRenderReadbackTicket ticket;f.action.applyAsync(scene.root,ticket);
  if(!check(f.action.getLastStatus()==CoinRenderAction::SUCCESS&&ticket.token!=0,"pending ticket created"))return;
  const auto stable=read(*f.target);
  Abort abort;abort.action=&f.action;abort.watched=scene.root;abort.code=CoinRenderAction::ABORT;
  f.action.setAbortCallback(Abort::call,&abort);
  for(int invalidCode:{-1,99}) {
    abort.code=static_cast<CoinRenderAction::AbortCode>(invalidCode);f.action.apply(scene.root);
    check(f.action.getLastStatus()==CoinRenderAction::INVALID_SCENE,"invalid abort callback while ticket pending rejects");
    unchanged(*f.target,stable,"invalid abort enum with pending ticket");
  }
  abort.code=CoinRenderAction::ABORT;f.action.apply(scene.root);
  check(f.action.getLastStatus()==CoinRenderAction::ABORTED,"abort while readback pending");unchanged(*f.target,stable,"abort pending ticket");
  std::vector<uint8_t> color;std::vector<float> depth;SbString diagnostic;
  auto status=CoinRenderTarget::READBACK_NOT_READY;
  while(status==CoinRenderTarget::READBACK_NOT_READY) {
    status=CoinRenderTarget::pollReadback(ticket,color,depth,&diagnostic);
    if(status==CoinRenderTarget::READBACK_NOT_READY)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  check(status==CoinRenderTarget::READBACK_READY&&color.size()==32*24*4&&depth.size()==32*24,
    "preexisting independent ticket survives abort");
  if(color.size()==32*24*4)check(color[(12*32+16)*4]==0&&color[(12*32+16)*4+1]==255,"ticket retains its green frame");
  f.action.setAbortCallback(nullptr,nullptr);
}
}
int main(int argc,char **argv) {
  bool gpu=false;std::string mode;
  for(int i=1;i<argc;++i) {const std::string arg=argv[i];if(arg=="--cpu")gpu=false;else if(arg=="--gpu")gpu=true;else if(mode.empty())mode=arg;else return 2;}
  SoDB::init();CoinRenderAction::initClass();Fixture fixture(gpu);
  if(!fixture.target)return gpu&&good()?77:1;
  if(mode=="--callbacks")callbacks(fixture);else if(mode=="--abort")aborts(fixture);
  else if(mode=="--invalidate")invalidate(fixture);
  else if(mode=="--sorting")sorting(fixture);else if(mode=="--depth")depths(fixture);
  else if(mode=="--update")updates(fixture);else if(mode=="--tickets"&&gpu)tickets(fixture);else return 2;
  return good()?0:1;
}
#endif
