// Caller-owned connections must be releasable while a peer keeps the GPU alive.
#define COIN_RENDER_SHADOW_MULTITEXTURE_WINDOW_FIXTURE_ONLY
#include "CoinRenderShadowMultitextureWindowTest.cpp"
#include <dlfcn.h>
#include <cstdint>

namespace {
struct ConnectionObserver {
  void *library=dlopen("libX11-xcb.so.1",RTLD_NOW|RTLD_LOCAL);
  using GetConnection=void*(*)(Display*);
  GetConnection get=library?reinterpret_cast<GetConnection>(dlsym(library,"XGetXCBConnection")):nullptr;
  ~ConnectionObserver(){if(library)dlclose(library);}
  uintptr_t address(Display*d)const{return get?reinterpret_cast<uintptr_t>(get(d)):0;}
};

void simpleScene(Scene&s,const SbColor&color){
  s.root->removeAllChildren();
  auto*camera=new SoOrthographicCamera;camera->position.setValue(0,0,6);
  camera->height=4;camera->nearDistance=1;camera->farDistance=20;s.root->addChild(camera);
  auto*model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;s.root->addChild(model);
  auto*material=new SoMaterial;material->diffuseColor=color;s.root->addChild(material);
  auto*coords=new SoCoordinate3;
  const SbVec3f points[]={SbVec3f(-1.5f,-1.5f,0),SbVec3f(1.5f,-1.5f,0),
    SbVec3f(1.5f,1.5f,0),SbVec3f(-1.5f,1.1f,0)};
  coords->point.setValues(0,4,points);s.root->addChild(coords);
  auto*face=new SoFaceSet;face->numVertices.set1Value(0,4);s.root->addChild(face);
}

bool sampleCoverage(const Frame&frame){
  unsigned partial=0;bool quantized=true;
  for(size_t i=3;i<frame.rgba.size();i+=4){
    const int alpha=frame.rgba[i];if(alpha==0||alpha==255)continue;++partial;
    const int k=int(std::lround(double(alpha)*windowSamples/255.));
    quantized &= k>0&&k<int(windowSamples)&&std::abs(alpha-int(std::lround(255.*k/windowSamples)))<=2;
  }
  std::cout<<"x11_connection_lifetime samples="<<windowSamples<<" fractional_coverage_pixels="<<partial<<'\n';
  return check(quantized&&(windowSamples==1?partial==0:partial>4),
    "sloped primitive demonstrates requested k/N sample coverage without 1x fallback");
}

bool lifetime(const CoinRenderCapabilities&caps,bool requireReuse,bool adversarialMode){
  ConnectionObserver observer;
  if(!check(bool(observer.get),"passive XCB address observation available"))return false;
  WindowPair peer(caps);if(!peer.ready)return false;
  XMoveWindow(peer.d,peer.window,16,16);XSync(peer.d,False);
  if(!check(DisplayWidth(peer.d,DefaultScreen(peer.d))>=side*2+64,
    "display wide enough for two unobscured physical windows"))return false;
  Scene peerScene(0,2,1.f,false);simpleScene(peerScene,SbColor(.25f,.75f,.5f));
  Frame peerFrame;uint64_t latest=0;
  auto peerFrameNow=[&](const std::string&phase){
    peer.latest=latest;
    const bool ok=peer.frame(peerScene,peerFrame,phase,true);
    latest=peer.latest;return ok;
  };
  if(!peerFrameNow("peer before fresh connections"))return false;
  const auto peerPixels=peer.lastShown;
  std::set<uintptr_t> closedDisplays,closedConnections;
  unsigned reusedDisplays=0,reusedConnections=0,completed=0;
  const unsigned limit=requireReuse?32:8;
  for(unsigned cycle=0;cycle<limit;++cycle){
    uintptr_t displayAddress=0,connectionAddress=0;
    {
      // Own Display per cycle, never borrow the peer connection. The fixture's
      // destructor destroys targets, then the caller's window and Display.
      WindowPair fresh(caps,latest);if(!fresh.ready)return false;
      displayAddress=reinterpret_cast<uintptr_t>(fresh.d);
      connectionAddress=observer.address(fresh.d);
      if(!check(connectionAddress!=0&&fresh.d!=peer.d&&connectionAddress!=observer.address(peer.d),
        "fresh target has its own live X11/XCB connection"))return false;
      const bool reusedDisplay=closedDisplays.count(displayAddress)!=0;
      const bool reusedConnection=closedConnections.count(connectionAddress)!=0;
      reusedDisplays+=reusedDisplay;reusedConnections+=reusedConnection;
      std::cout<<"x11_connection_lifetime cycle="<<cycle<<" display="<<reinterpret_cast<void*>(displayAddress)
        <<" xcb="<<reinterpret_cast<void*>(connectionAddress)<<" observed_display_reuse="<<reusedDisplay
        <<" observed_xcb_reuse="<<reusedConnection<<" samples="<<windowSamples<<'\n';
      XMoveWindow(fresh.d,fresh.window,side+48,16);XSync(fresh.d,False);
      Scene freshScene(0,2,1.f,false);simpleScene(freshScene,cycle%2?SbColor(.75f,.25f,.5f):SbColor(.5f,.75f,.25f));Frame frame;
      if(!fresh.frame(freshScene,frame,"fresh connection initial",true))return false;
      if(!sampleCoverage(frame))return false;
      latest=fresh.latest;
      if(!peerFrameNow("peer during fresh connection"))return false;
      if(!check(peerPixels==peer.lastShown,"peer physical content preserved while fresh target publishes"))return false;
      fresh.latest=latest;
      if(cycle<2||reusedConnection){
        if(!fresh.resize(96)||!fresh.frame(freshScene,frame,"fresh connection resized",true))return false;
        if(!fresh.resize(side)||!fresh.frame(freshScene,frame,"fresh connection restored",true))return false;
      }
      if(adversarialMode&&(cycle==0||reusedConnection)){
        const auto previous=frame.rgba,physical=fresh.lastShown;
        auto*bad=new SoAlphaTest;bad->function=SoAlphaTest::GREATER;
        bad->value=std::numeric_limits<float>::quiet_NaN();freshScene.root->insertChild(bad,0);
        if(!fresh.refuse(freshScene,"invalid alpha on independent connection",true))return false;
        freshScene.root->removeChild(bad);
        if(!fresh.frame(freshScene,frame,"fresh connection after atomic rejection",true))return false;
        if(!check(frame.rgba==previous&&fresh.lastShown==physical,
          "recovered connection restores exact readback and physical content"))return false;
      }
      latest=fresh.latest;
    }
    // Record only addresses of connections already closed by the caller.
    closedDisplays.insert(displayAddress);closedConnections.insert(connectionAddress);++completed;
    if(!peerFrameNow("peer after caller destroys window and closes Display"))return false;
    if(!check(peerPixels==peer.lastShown,"peer remains renderable after caller closes fresh Display"))return false;
    if(requireReuse&&completed>=8&&reusedConnections>0)break;
  }
  std::cout<<"x11_connection_lifetime completed="<<completed<<" observed_display_reuses="<<reusedDisplays
    <<" observed_xcb_reuses="<<reusedConnections<<'\n';
  return check(completed>=8&&(!requireReuse||reusedConnections>0),
    "successive caller-owned connections qualified; required XCB address reuse observed");
}
}

int main(int argc,char**argv){
  configureSamplingTest(argc,argv);bool requireReuse=false,adversarialMode=false;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];
    if(arg=="--portable-sampling")continue;
    if(arg=="--require-reuse")requireReuse=true;
    else if(arg=="--adversarial")adversarialMode=true;
    else if(arg=="--samples"){
      if(++i==argc)return 2;const std::string count=argv[i];
      if(count!="1"&&count!="2"&&count!="4")return 2;windowSamples=unsigned(std::stoi(count));
    }else return 2;
  }
  if(!XInitThreads())return 1;SoDB::init();CoinRenderAction::initClass();
  const auto caps=msaa_shadow_test::capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW);
  if(!caps.gpu_available||caps.renderer==COIN_RENDER_RENDERER_UNKNOWN)return 77;
  return lifetime(caps,requireReuse,adversarialMode)?0:1;
}
