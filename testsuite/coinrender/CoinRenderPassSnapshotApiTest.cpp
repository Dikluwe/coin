#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderPortableApiFixture.h"
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <array>
#include <limits>

#if !defined(COIN_RENDER_ACTION_OPTIONS_VERSION) || COIN_RENDER_ACTION_OPTIONS_VERSION < 1
int main(){std::cout<<"portable per-pass snapshot API absent\n";return 78;}
#else
using namespace portable_api;
namespace {
using RGB=std::array<float,3>;
const RGB palette[]={{{1,0,0}},{{0,1,0}},{{0,0,1}},{{1,1,1}}};
const std::array<float,4> clearColors[]={{{.1f,.2f,.3f,.25f}},{{.7f,.1f,.2f,.5f}},{{.2f,.8f,.4f,.75f}},{{.5f,.4f,.9f,1.f}}};
struct Snapshot {
  Fixture &fixture;
  Scene scene;
  Quad quad{-.6f,.6f,-.6f,.6f};
  SoTranslation *translation=new SoTranslation;
  SoTexture2 *texture=nullptr;
  std::vector<std::string> events;
  Image previous;
  unsigned preCalls=0,betweenCalls=0,captures=0;
  int requested=1,abortPass=-1,invalidPass=-1,nestedPass=-1;
  bool stable=true,indices=true,nestedRefused=false;
  Snapshot(Fixture &f,bool textured):fixture(f) {
    auto *visit=new SoCallback;visit->setCallback(node,this);scene.root->addChild(visit);
    quad.root->insertChild(translation,0);
    if(textured) {
      texture=new SoTexture2;texture->model=SoTexture2::REPLACE;quad.root->insertChild(texture,0);
      auto *uv=new SoTextureCoordinate2;const SbVec2f coords[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};
      uv->point.setValues(0,4,coords);quad.root->insertChild(uv,1);
    }
    scene.root->addChild(quad.root);set(0);
  }
  void set(int pass) {
    const auto &rgb=palette[pass%4];quad.material->diffuseColor.setValue(rgb[0],rgb[1],rgb[2]);
    const auto &clear=clearColors[pass%4];fixture.action.setBackgroundColor(SbColor4f(clear[0],clear[1],clear[2],clear[3]));
    const int motion=pass%3;translation->translation.setValue(0,0,.2f*motion);
    scene.camera->position.setValue(0,0,3+.1f*motion);
    if(texture) {
      uint8_t bytes[16];for(int p=0;p<4;++p){for(int c=0;c<3;++c)bytes[p*4+c]=uint8_t(rgb[c]*255);bytes[p*4+3]=255;}
      texture->image.setValue(SbVec2s(2,2),4,bytes);
    }
  }
  void reset(int passes) {
    requested=passes;preCalls=betweenCalls=captures=0;events.clear();stable=indices=true;nestedRefused=false;
    set(0);previous=read(*fixture.target);
  }
  static void pre(void *data,CoinRenderAction *){++static_cast<Snapshot*>(data)->preCalls;}
  static void node(void *data,SoAction *action) {
    if(!action->isOfType(CoinRenderAction::getClassTypeId()))return;
    auto &self=*static_cast<Snapshot*>(data);auto *render=static_cast<CoinRenderAction*>(action);
    self.events.push_back("n"+std::to_string(render->getCurPass()));
    self.indices &= render->getCurPass()==int(self.captures)&&render->getNumPasses()==self.requested;++self.captures;
  }
  static void between(void *data,CoinRenderAction *action) {
    auto &self=*static_cast<Snapshot*>(data);const int previous=action->getCurPass();
    self.events.push_back("b"+std::to_string(previous));
    self.indices &= previous==int(self.betweenCalls)&&action->getNumPasses()==self.requested;
    const auto now=read(*self.fixture.target);
    self.stable &= now.rgba==self.previous.rgba&&now.depth==self.previous.depth&&now.serial==self.previous.serial&&now.borrow==self.previous.borrow;
    ++self.betweenCalls;const int next=previous+1;self.set(next);
    if(next==self.invalidPass)self.scene.camera->nearDistance=std::numeric_limits<float>::quiet_NaN();
    if(next==self.nestedPass) {
      action->apply(self.scene.root);self.nestedRefused=action->getLastStatus()!=CoinRenderAction::SUCCESS;
    }
  }
  static CoinRenderAction::AbortCode abort(void *data) {
    auto &self=*static_cast<Snapshot*>(data);auto &action=self.fixture.action;
    const auto *path=action.getCurPath();
    return action.getCurPass()==self.abortPass&&path&&path->getTail()==self.quad.shape?CoinRenderAction::ABORT:CoinRenderAction::CONTINUE;
  }
};
void mean(Fixture &f,bool texture) {
  Snapshot test(f,texture);f.action.setNumPasses(1);f.action.setPassCallback(nullptr,nullptr);
  f.apply(test.scene.root,"snapshot initial publication");
  f.action.addPreRenderCallback(Snapshot::pre,&test);f.action.setPassCallback(Snapshot::between,&test);
  for(int n:{1,2,3,4,16,3}) {
    test.reset(n);f.action.setNumPasses(n);const auto serial=f.target->getLastSubmissionSerial();
    if(!f.apply(test.scene.root,texture?"texture snapshots":"material/camera snapshots"))continue;
    check(test.preCalls==1&&test.betweenCalls==unsigned(n-1)&&test.captures==unsigned(n)&&test.indices&&test.stable,
      "one pre-render, N captures, N-1 between callbacks, correct indices and previous publication visible");
    std::vector<std::string> expectedEvents;
    for(int i=0;i<n;++i){if(i)expectedEvents.push_back("b"+std::to_string(i-1));expectedEvents.push_back("n"+std::to_string(i));}
    check(test.events==expectedEvents&&f.action.getCurPass()==0,"per-pass event ordering and restored internal index");
    check(f.target->getLastSubmissionSerial()==serial+1,"N snapshots publish one logical frame");
    auto image=read(*f.target);if(!check(image.rgba.size()==32*24*4&&image.depth.size()==32*24,"snapshot image complete"))continue;
    std::array<double,4> expectedCenter={{0,0,0,1}},expectedClear={{0,0,0,0}};
    for(int pass=0;pass<n;++pass){for(int c=0;c<3;++c)expectedCenter[c]+=palette[pass%4][c]/double(n);
      for(int c=0;c<4;++c)expectedClear[c]+=clearColors[pass%4][c]/double(n);}
    unsigned bad=0;
    for(int y=10;y<14;++y)for(int x=14;x<18;++x) {
      const size_t p=(y*32+x)*4;for(int c=0;c<4;++c)bad+=std::abs(image.rgba[p+c]-expectedCenter[c]*255)>2;
      bad+=std::abs(image.depth[p/4]-(.5f-.025f*((n-1)%3)))>1e-5f;
    }
    for(int c=0;c<4;++c)bad+=std::abs(image.rgba[c]-expectedClear[c]*255)>2;
    bad+=image.depth[0]!=1;
    check(!bad,"independent linear mean of distinct RGB/alpha/background snapshots and final camera/depth");
  }
  f.action.removePreRenderCallback(Snapshot::pre,&test);f.action.setPassCallback(nullptr,nullptr);
  // The established no-callback path remains capture-once even at N=4.
  test.reset(4);f.action.setNumPasses(4);f.apply(test.scene.root,"capture-once compatibility");
  check(test.captures==1&&test.betweenCalls==0,"default R28 traversal remains capture-once");
  f.action.setNumPasses(1);
}
void failures(Fixture &f) {
  Snapshot test(f,false);f.action.setNumPasses(1);f.apply(test.scene.root,"failure baseline");
  f.action.setPassCallback(Snapshot::between,&test);f.action.setAbortCallback(Snapshot::abort,&test);
  for(int rejected:{1,2}) {
    test.reset(3);test.abortPass=rejected;f.action.setNumPasses(3);const auto old=read(*f.target);const unsigned submits=f.recorder?f.recorder->submits:0;
    f.action.apply(test.scene.root);check(f.action.getLastStatus()==CoinRenderAction::ABORTED&&f.action.getCurPass()==0,
      "abort in later snapshot rejects complete candidate and restores index");
    unchanged(*f.target,old,"later-pass abort");check(!f.recorder||f.recorder->submits==submits,"all passes validated before backend");
    test.abortPass=-1;test.reset(3);f.apply(test.scene.root,"recovery after later-pass abort");
  }
  test.reset(3);test.invalidPass=2;f.action.setNumPasses(3);const auto old=read(*f.target);const unsigned submits=f.recorder?f.recorder->submits:0;
  f.action.apply(test.scene.root);check(f.action.getLastStatus()!=CoinRenderAction::SUCCESS&&f.action.getCurPass()==0,"invalid final camera rejects all snapshots");
  unchanged(*f.target,old,"later camera invalid");check(!f.recorder||f.recorder->submits==submits,"invalid snapshot before backend");
  test.invalidPass=-1;test.scene.camera->nearDistance=1;test.reset(3);f.apply(test.scene.root,"camera error recovery");
  test.reset(3);test.nestedPass=1;const auto stable=read(*f.target);f.action.apply(test.scene.root);
  check(test.nestedRefused&&f.action.getLastStatus()==CoinRenderAction::INVALID_SCENE,"nested apply during pass callback rejects candidate");
  unchanged(*f.target,stable,"nested later-pass apply");
  test.nestedPass=-1;test.reset(3);f.apply(test.scene.root,"nested-pass recovery");
  f.action.setAbortCallback(nullptr,nullptr);f.action.setPassCallback(nullptr,nullptr);f.action.setNumPasses(1);
}
void external(Fixture &f) {
  Scene scene;Quad edge(-.5f,float(16.33/16-1),-.75f,.75f);scene.root->addChild(edge.root);
  Snapshot unused(f,false);f.action.setPassCallback(Snapshot::between,&unused);
  f.action.setBackgroundColor(SbColor4f(0,0,0,0));
  const double sampleX[]={.5033922635,.7806016275,.2261828938};
  for(int selected:{0,1,2,0}) {
    f.action.setCurPass(selected,3);check(f.action.getCurPass()==selected&&f.action.getNumPasses()==3,"external sample getters");
    f.apply(scene.root,"external sample");auto image=read(*f.target);unsigned bad=0;
    if(check(image.rgba.size()==32*24*4&&image.depth.size()==32*24,"external complete image"))
      for(int y=8;y<16;++y)for(int x=3;x<29;++x) {
        const bool covered=x+.5-(sampleX[selected]-.5)>8&&x+.5-(sampleX[selected]-.5)<16.33;
        const size_t p=(y*32+x)*4;
        bad+=image.rgba[p]!=(covered?255:0)||image.rgba[p+1]!=0||image.rgba[p+2]!=0||image.rgba[p+3]!=(covered?255:0);
        bad+=std::abs(image.depth[p/4]-(covered?.5f:1.f))>1e-5f;
      }
    check(!bad&&unused.betweenCalls==0&&f.action.getCurPass()==selected,"external jitter sample is one full-strength render with callbacks disabled");
  }
  const auto stable=read(*f.target);
  for(const auto invalid:{std::pair<int,int>(-1,3),{3,3},{0,0},{0,17},{1,-1}}) {
    f.action.setCurPass(invalid.first,invalid.second);f.action.apply(scene.root);
    check(f.action.getLastStatus()!=CoinRenderAction::SUCCESS,"invalid external sample rejects");unchanged(*f.target,stable,"invalid external sample");
  }
  f.action.setNumPasses(1);check(f.action.getNumPasses()==1&&f.action.getCurPass()==0,"setNumPasses restores internal mode/index");
  f.action.setPassCallback(nullptr,nullptr);f.apply(scene.root,"external invalid recovery");
}
}
int main(int argc,char **argv) {
  const bool gpu=argc==2&&std::string(argv[1])=="--gpu";
  if(argc!=2||(!gpu&&std::string(argv[1])!="--cpu"))return 2;
  SoDB::init();CoinRenderAction::initClass();Fixture f(gpu);if(!f.target)return gpu&&good()?77:1;
  check(f.action.getCurPass()==0,"initial pass zero");mean(f,false);mean(f,true);failures(f);external(f);
  return good()?0:1;
}
#endif
