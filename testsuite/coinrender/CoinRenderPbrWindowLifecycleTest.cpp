#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderPbrEnvironment.h"
#include <Inventor/SoDB.h>
#include <Inventor/C/tidbits.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <X11/Xlib.h>
#undef Status
#include <cstdio>
#define CHECK(x,m) do{if(!(x)){std::fprintf(stderr,"FAIL: %s (%d)\n",m,__LINE__);return 1;}}while(0)
static bool capture(CoinRenderSceneManager & manager,std::vector<uint8_t> & pixels) {
  auto * target=manager.getRenderTarget();
  if(!target->requestWindowReadbackRGBA()||manager.render()!=CoinRenderAction::SUCCESS) {
    std::fprintf(stderr,"capture: %s\n",manager.getLastError().getString());return false;
  }
  target->readbackRGBA(pixels);return pixels.size()==size_t(target->getSize()[0])*target->getSize()[1]*4;
}
static bool centerEqual(const std::vector<uint8_t> & a,int aw,int ah,
                        const std::vector<uint8_t> & b,int bw,int bh) {
  for(int c=0;c<4;++c)if(a[(ah/2*aw+aw/2)*4+c]!=b[(bh/2*bw+bw/2)*4+c])return false;
  return true;
}
int main(int argc,char ** argv) {
  SoDB::init();CoinRenderAction::initClass();
  Display * display=XOpenDisplay(nullptr);if(!display)return 77;
  SbString path=argc>1?argv[1]:"/tmp/coin-pbr-window.cibl",error;CoinRenderPbrEnvironment environment;
  CHECK(environment.bake(environment.WHITE,8,32,32,512,environment,error)&&environment.write(path,error),"white environment bake");
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),20,20,64,64,0,0,0);
  XSetWindowAttributes attrs{};attrs.override_redirect=True;XChangeWindowAttributes(display,window,CWOverrideRedirect,&attrs);
  XMapWindow(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor desc{};desc.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;desc.structSize=sizeof(desc);
  desc.type=COIN_RENDER_SURFACE_XLIB;desc.native.xlib.display=display;desc.native.xlib.window=window;
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;options.pbrEnvironmentBundle=path;
  auto * root=new SoSeparator;root->ref();auto * camera=new SoOrthographicCamera;camera->height=3;camera->position=SbVec3f(0,0,4);root->addChild(camera);
  auto * material=new SoPBRMaterial;material->baseColor=SbColor(1,1,1);material->metallic=1;material->roughness=.8f;root->addChild(material);root->addChild(new SoCube);
  {
    CoinRenderSceneManager manager(desc,SbVec2i32(64,64),options);manager.setSceneGraph(root);
    CoinRenderSceneManager peer(SbVec2i32(32,32),options);peer.setSceneGraph(root);
    std::vector<uint8_t> first,again,resized,peerBefore,peerAfter;
    CHECK(capture(manager,first)&&capture(manager,again)&&first==again,"window first/replay pixels");
    CHECK(peer.render()==CoinRenderAction::SUCCESS,"offscreen peer publication");peer.getRenderTarget()->readbackRGBA(peerBefore);
    auto * target=manager.getRenderTarget();const auto identity=target->getPimpl()->pbrEnvironmentSnapshot->identity;
    std::remove(path.getString());
    XResizeWindow(display,window,96,48);XSync(display,False);
    CHECK(manager.resize(SbVec2i32(96,48))&&capture(manager,resized)&&centerEqual(first,64,64,resized,96,48),"native resize reconstructs HDR/SDR attachments");
    CHECK(manager.resize(SbVec2i32(0,0))&&manager.render()==CoinRenderAction::NOT_READY,"manager suspension skips zero viewport capture");
    target->readbackRGBA(again);CHECK(again.empty(),"suspension clears public pixels");
    CHECK(manager.resize(SbVec2i32(96,48))&&capture(manager,again)&&again==resized,"resume exact output with missing source file");
    const auto publishedSerial=target->getLastSubmissionSerial();
    coin_setenv("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE","1",1);
    const auto failure=manager.render();coin_unsetenv("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE");
    CHECK(failure==CoinRenderAction::DEVICE_LOST,"controlled shared device loss reported");
    target->readbackRGBA(again);CHECK(again==resized&&target->getLastSubmissionSerial()==publishedSerial&&
      target->getStatus()==CoinRenderTarget::TARGET_LOST&&!target->getPimpl()->backend,
      "loss retires GPU resources and preserves the previous CPU publication");
    CHECK(manager.render()==CoinRenderAction::SUCCESS,"ordinary redraw recreates the lost window before a capture request");
    CHECK(capture(manager,again)&&again==resized&&target->getPimpl()->pbrEnvironmentSnapshot->identity==identity,"window reconstructs frozen CPU environment");
    CHECK(peer.render()==CoinRenderAction::SUCCESS,"peer reconstructs shared resource domain");peer.getRenderTarget()->readbackRGBA(peerAfter);
    CHECK(peerBefore==peerAfter&&peer.getRenderTarget()->getPimpl()->pbrEnvironmentSnapshot->identity==identity,"peer pixels and environment survive reconstruction");
  }
  root->unref();XDestroyWindow(display,window);
  // Keep the process-owned Xlib connection through deferred driver teardown.
  std::printf("PBR Xlib: first/replay, native resize, suspend/resume, controlled device loss, frozen environment and offscreen peer PASS\n");return 0;
}
