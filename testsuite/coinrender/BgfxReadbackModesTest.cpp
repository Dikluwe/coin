#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoBGFXRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <cstdlib>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>
#include <stdexcept>

static void check(bool value, const char * message) { if (!value) throw std::runtime_error(message); }
static CoinRenderFramePlan scene(int mode)
{
  CoinRenderFramePlan f;
  f.clearColor = SbColor4f(0,0,0,1);
  f.materials.resize(2);
  for (auto & m : f.materials) { m.diffuse[0] = m.diffuse[1] = m.diffuse[2] = 0; }
  f.materials[0].diffuse[0] = 1; f.materials[0].diffuse[3] = 0.5f; f.materials[0].transparency = 0.5f;
  f.materials[1].diffuse[2] = 1;
  f.lightingStates.emplace_back(); f.cameras.emplace_back();
  CoinRenderViewportSnapshot vp; vp.width = vp.height = 32; f.viewports.push_back(vp);
  for (int i = 0; i < 2; ++i) {
    CoinRenderRenderStateSnapshot s; s.lightModel = CoinRenderLightModel::BASE_COLOR;
    s.projectionCoin = SbMatrix(1,0,0,0, 0,1,0,0, 0,0,-1,0, 0,0,-1,1);
    s.transparencyType = mode; s.materialSlot = i; f.renderStates.push_back(s);
    const float p[3][2] = {{-0.8f,-0.8f},{0.8f,-0.8f},{0,0.8f}};
    for (int j = 0; j < 3; ++j) {
      CoinRenderVertexSnapshot v; v.position[0] = p[j][0]; v.position[1] = p[j][1];
      v.position[2] = -0.5f - i; v.materialSlot = i;
      f.vertices.push_back(v); f.indices.push_back(i * 3 + j);
    }
    CoinRenderDrawPacket d; d.renderStateSlot = i; d.geometry.firstVertex = i * 3;
    d.geometry.firstIndex = i * 3; d.geometry.vertexCount = d.geometry.indexCount = 3;
    f.draws.push_back(d);
  }
  return f;
}
static void poll(const CoinRenderReadbackTicket & ticket, std::vector<uint8_t> & c, std::vector<float> & d)
{
  for (int i = 0; i < 32; ++i) {
    SbString diagnostic;
    const auto result = CoinRenderTarget::pollReadback(ticket,c,d,&diagnostic);
    if (result == CoinRenderTarget::READBACK_READY) return;
    if (result != CoinRenderTarget::READBACK_NOT_READY)
      throw std::runtime_error(diagnostic.getString());
  }
  throw std::runtime_error("Async ticket did not complete");
}
static void compareGl(const int * modes, size_t count)
{
  if (!std::getenv("COIN_BGFX_COMPARE_GL")) return;
  for (size_t modeIndex=0; modeIndex<count; ++modeIndex) {
    const auto mode=SoGLRenderAction::TransparencyType(modes[modeIndex]);
    // Coin/GL's sorted-layers shader depends on legacy extensions. Its RGB
    // baseline is covered separately by WgpuBgfxTransparencyGlReferenceTest.
    if (mode==SoGLRenderAction::SORTED_LAYERS_BLEND) continue;
    auto root=new SoSeparator; root->ref();
    auto camera=new SoOrthographicCamera; camera->position=SbVec3f(0,0,2);
    camera->height=2; camera->nearDistance=0.1f; camera->farDistance=10;
    root->addChild(camera);
    auto lighting=new SoLightModel; lighting->model=SoLightModel::BASE_COLOR; root->addChild(lighting);
    const bool perFace = mode==SoGLRenderAction::SCREEN_DOOR ||
      mode==SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND ||
      mode==SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD;
    if (perFace) {
      auto material=new SoMaterial;
      const SbColor colors[2]={SbColor(1,0,0),SbColor(0,0,1)};
      const float alpha[2]={0.5f,mode==SoGLRenderAction::SCREEN_DOOR?0.0f:0.5f};
      material->diffuseColor.setValues(0,2,colors); material->transparency.setValues(0,2,alpha);
      root->addChild(material);
      auto binding=new SoMaterialBinding; binding->value=SoMaterialBinding::PER_FACE; root->addChild(binding);
      auto coordinates=new SoCoordinate3;
      const SbVec3f p[6]={SbVec3f(-0.8f,-0.8f,0.5f),SbVec3f(0.8f,-0.8f,0.5f),SbVec3f(0,0.8f,0.5f),
        SbVec3f(-0.8f,-0.8f,-0.5f),SbVec3f(0.8f,-0.8f,-0.5f),SbVec3f(0,0.8f,-0.5f)};
      coordinates->point.setValues(0,6,p); root->addChild(coordinates);
      auto shape=new SoFaceSet; const int counts[2]={3,3}; shape->numVertices.setValues(0,2,counts); root->addChild(shape);
    }
    for (int i=0;!perFace && i<2;++i) {
      auto group=new SoSeparator; root->addChild(group);
      auto material=new SoMaterial; material->diffuseColor=SbColor(i?0:1,0,i?1:0);
      material->transparency=i?0:0.5f; group->addChild(material);
      auto coordinates=new SoCoordinate3;
      const SbVec3f p[3]={SbVec3f(-0.8f,-0.8f,i?-0.5f:0.5f),SbVec3f(0.8f,-0.8f,i?-0.5f:0.5f),SbVec3f(0,0.8f,i?-0.5f:0.5f)};
      coordinates->point.setValues(0,3,p); group->addChild(coordinates);
      auto shape=new SoFaceSet; shape->numVertices=3; group->addChild(shape);
    }
    // Delayed Coin blending uses LEQUAL, including coplanar transparent/opaque
    // paths. No explicit SoDepthBuffer is present to override the replay state.
    if (mode==SoGLRenderAction::DELAYED_BLEND || mode==SoGLRenderAction::SORTED_OBJECT_BLEND) {
      auto * group=static_cast<SoSeparator *>(root->getChild(3));
      auto * coordinates=static_cast<SoCoordinate3 *>(group->getChild(1));
      for (int i=0;i<3;++i) {
        auto p=coordinates->point[i]; p[2]=0.5f; coordinates->point.set1Value(i,p);
      }
    }
    if (mode==SoGLRenderAction::SORTED_OBJECT_BLEND) {
      auto * depth=new SoDepthBuffer;
      depth->test.setIgnored(TRUE); depth->range.setIgnored(TRUE);
      depth->write=FALSE; depth->function=SoDepthBuffer::LESS;
      static_cast<SoSeparator *>(root->getChild(2))->insertChild(depth,2);
    }
    SoOffscreenRenderer gl(SbViewportRegion(32,32));
    gl.setComponents(SoOffscreenRenderer::RGB);
    gl.getGLRenderAction()->setTransparencyType(mode);
    check(gl.render(root),"Coin/GL transparency reference could not render");
    std::vector<uint8_t> reference(gl.getBuffer(),gl.getBuffer()+32*32*3);
    auto target=CoinRenderTarget::createOffscreen(SbVec2i32(32,32));
    SoBGFXRenderAction action(SbViewportRegion(32,32)); action.setRenderTarget(target);
    action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(mode)); action.apply(root);
    std::vector<uint8_t> color; target->readbackRGBA(color);
    check(color.size()==4096,"BGFX action did not render GL reference scene");
    for (int y=19;y<27;++y) for(int x=12;x<20;++x) for(int c=0;c<3;++c) {
      const int expected=reference[((31-y)*32+x)*3+c], actual=color[(y*32+x)*4+c];
      if(std::abs(expected-actual)>2) {
        std::cerr << "GL reference mode=" << mode << " at " << x << "," << y << " channel=" << c << " GL=" << expected << " BGFX=" << actual << '\n';
        throw std::runtime_error("Coin/GL transparency RGB parity mismatch");
      }
    }
    delete target; root->unref();
  }
  std::cout << "Coin/GL transparency RGB reference passed\n";
}
int main()
{
  try {
    SoDB::init();
    SoBGFXRenderAction::initClass();
    auto target = std::unique_ptr<CoinRenderTargetP>(new CoinRenderTargetP(SbVec2i32(32,32)));
    const int modes[] = {SoGLRenderAction::NONE, SoGLRenderAction::SCREEN_DOOR,
      SoGLRenderAction::ADD, SoGLRenderAction::BLEND, SoGLRenderAction::DELAYED_ADD,
      SoGLRenderAction::DELAYED_BLEND, SoGLRenderAction::SORTED_OBJECT_ADD,
      SoGLRenderAction::SORTED_OBJECT_BLEND, SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD,
      SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND, SoGLRenderAction::SORTED_LAYERS_BLEND};
    compareGl(modes, sizeof(modes)/sizeof(modes[0]));
    const size_t pixel = 16 * 32 + 16;
    for (const int mode : modes) {
      auto f = scene(mode);
      const auto r = target->executeFrame(f);
      if (r.status != CoinRenderBackendStatus::SUCCESS) throw std::runtime_error(r.diagnostic);
      check(target->depthBuffer.size() == 1024, "GPU depth readback missing");
      const bool deferred = mode != SoGLRenderAction::ADD && mode != SoGLRenderAction::BLEND && mode != SoGLRenderAction::NONE;
      const bool additive = mode == SoGLRenderAction::ADD || mode == SoGLRenderAction::DELAYED_ADD ||
        mode == SoGLRenderAction::SORTED_OBJECT_ADD || mode == SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD;
      if (mode != SoGLRenderAction::SCREEN_DOOR) {
        const int red = mode == SoGLRenderAction::NONE ? 255 : 128;
        const int blue = deferred ? (additive ? 255 : 128) : 0;
        if (std::abs(int(target->colorBuffer[pixel*4])-red)>2 || std::abs(int(target->colorBuffer[pixel*4+2])-blue)>2) {
          std::cerr << "mode=" << mode << " RGB=" << int(target->colorBuffer[pixel*4]) << "," << int(target->colorBuffer[pixel*4+2]) << '\n';
          throw std::runtime_error("Coin transparency order/blend mismatch");
        }
        check(std::abs(target->depthBuffer[pixel] - (deferred ? 0.75f : 0.25f)) < 1e-5f,
          "Coin transparency depth-write mismatch");
      } else {
        int red = 0, blue = 0;
        for (int y=19; y<27; ++y) for (int x=12; x<20; ++x) {
          red += target->colorBuffer[(y*32+x)*4] > 200;
          blue += target->colorBuffer[(y*32+x)*4+2] > 200;
        }
        check(red==32 && blue==32, "Screen-door must cover 50%, with holes filled by later opaque geometry");
      }
      check(target->depthBuffer[0] == 1.0f, "GPU depth clear is not 1");
    }
    // The published depth is final GPU window depth, including Coin's range.
    auto rangeFrame=scene(SoGLRenderAction::BLEND);
    for(auto & state : rangeFrame.renderStates) { state.depthRange[0]=0.2f; state.depthRange[1]=0.8f; }
    check(target->executeFrame(rangeFrame).status==CoinRenderBackendStatus::SUCCESS &&
      std::abs(target->depthBuffer[pixel]-0.35f)<1e-5f,"GPU depth range readback mismatch");
    for(auto & state : rangeFrame.renderStates) state.depthWrite=false;
    check(target->executeFrame(rangeFrame).status==CoinRenderBackendStatus::SUCCESS &&
      target->depthBuffer[pixel]==1.0f,"Disabled depth writes changed readback");
    auto perspectiveFrame=scene(SoGLRenderAction::NONE);
    for(auto & state : perspectiveFrame.renderStates)
      state.projectionCoin=SbMatrix(1,0,0,0, 0,1,0,0, 0,0,-1.020202f,-1, 0,0,-0.2020202f,0);
    check(target->executeFrame(perspectiveFrame).status==CoinRenderBackendStatus::SUCCESS &&
      std::abs(target->depthBuffer[pixel]-0.8080808f)<1e-5f,"GPU perspective depth readback mismatch");
    auto f = scene(SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
    f.materials[1].diffuse[3] = 0.5f; f.materials[1].transparency = 0.5f;
    f.draws.resize(1); f.draws[0].geometry.vertexCount = f.draws[0].geometry.indexCount = 6;
    check(target->executeFrame(f).status == CoinRenderBackendStatus::SUCCESS, "Triangle sorting failed");
    check(std::abs(int(target->colorBuffer[pixel*4])-128)<=2 && std::abs(int(target->colorBuffer[pixel*4+2])-64)<=2,
      "Triangles within one mesh must sort back-to-front");

    f = scene(SoGLRenderAction::BLEND);
    f.vertices[0].position[1]=f.vertices[1].position[1]=0.1f; f.vertices[2].position[1]=0.9f;
    check(target->executeFrame(f).status==CoinRenderBackendStatus::SUCCESS &&
      std::abs(target->depthBuffer[8*32+16]-0.25f)<1e-5f &&
      std::abs(target->depthBuffer[24*32+16]-0.75f)<1e-5f,"Depth readback rows are inverted");
    std::vector<uint8_t> expected; std::vector<float> expectedDepth;
    check(target->executeFrame(f).status == CoinRenderBackendStatus::SUCCESS, "Sync frame failed");
    expected=target->colorBuffer; expectedDepth=target->depthBuffer;
    CoinRenderReadbackTicket a,b;
    check(target->executeFrameAsync(f,a).status == CoinRenderBackendStatus::SUCCESS && a.token, "Async submit failed");
    check(target->colorBuffer.empty(), "Async submit published synchronous pixels");
    check(target->executeFrameAsync(f,b).status == CoinRenderBackendStatus::SUCCESS && b.token!=a.token, "Second async submit failed");
    auto forged=a; ++forged.width;
    std::vector<uint8_t> color{7}; std::vector<float> depth{7};
    check(CoinRenderTarget::pollReadback(forged,color,depth)==CoinRenderTarget::READBACK_INVALID_TICKET && color[0]==7 && depth[0]==7,
      "Forged ticket changed outputs");
    CoinRenderTarget::ReadbackStatus wrongThread=CoinRenderTarget::READBACK_READY;
    std::thread worker([&] { wrongThread=CoinRenderTarget::pollReadback(a,color,depth); }); worker.join();
    check(wrongThread==CoinRenderTarget::READBACK_ERROR, "BGFX accepted polling on a foreign thread");
    target->depthReadbackEnabled=false;
    check(target->executeFrame(f).status==CoinRenderBackendStatus::SUCCESS && target->depthBuffer.empty(),"Color-only policy published old depth");
    target->depthReadbackEnabled=true;
    check(target->executeFrame(f).status==CoinRenderBackendStatus::SUCCESS && target->depthBuffer==expectedDepth,"Depth policy returned stale readback");
    target->size=SbVec2i32(48,48); target->needsReconfigure=true; ++target->generation;
    f.viewports[0].width=f.viewports[0].height=48;
    check(target->executeFrame(f).status==CoinRenderBackendStatus::SUCCESS, "Resize with pending tickets failed");
    target.reset();
    poll(a,color,depth);
    check(color==expected && depth==expectedDepth, "Ticket data changed after resize/destruction");
    check(CoinRenderTarget::pollReadback(a,color,depth)==CoinRenderTarget::READBACK_INVALID_TICKET,
      "Ready ticket was not consumed");
    check(CoinRenderTarget::cancelReadback(b), "Cancel after target destruction failed");
    check(!CoinRenderTarget::cancelReadback(b), "Canceled ticket was not consumed");

    SoSeparator * root=new SoSeparator; root->ref();
    auto camera=new SoOrthographicCamera; camera->position=SbVec3f(0,0,4); root->addChild(camera); root->addChild(new SoCube);
    auto publicTarget=CoinRenderTarget::createOffscreen(SbVec2i32(32,32));
    CoinRenderReadbackTicket publicTicket;
    { SoBGFXRenderAction action(SbViewportRegion(32,32)); action.setRenderTarget(publicTarget); action.applyAsync(root,publicTicket); }
    check(publicTicket.token!=0, "Public SoBGFXRenderAction::applyAsync failed");
    delete publicTarget; root->unref(); poll(publicTicket,color,depth);
    check(color.size()==4096 && depth.size()==1024 && depth[pixel]<1, "Public color/depth async outputs missing");
    target.reset(new CoinRenderTargetP(SbVec2i32(32,32)));
    target->depthReadbackEnabled=false;
    f=scene(SoGLRenderAction::BLEND);
    CoinRenderReadbackTicket colorOnly;
    check(target->executeFrameAsync(f,colorOnly).status==CoinRenderBackendStatus::SUCCESS &&
      colorOnly.depthFormat==0 && colorOnly.depthBytes==0 && colorOnly.depthRowPitch==0,"Color-only async metadata mismatch");
    target.reset(); poll(colorOnly,color,depth);
    check(depth.empty() && color.size()==4096,"Color-only async outputs mismatch");

    target.reset(new CoinRenderTargetP(SbVec2i32(32,32)));
    std::vector<CoinRenderReadbackTicket> pending(16);
    for(auto & ticket : pending)
      check(target->executeFrameAsync(f,ticket).status==CoinRenderBackendStatus::SUCCESS,"Async queue rejected a ticket before its limit");
    CoinRenderReadbackTicket excess;
    check(target->executeFrameAsync(f,excess).status==CoinRenderBackendStatus::NOT_READY && excess.token==0,"Async queue limit must apply backpressure");
    target.reset();
    for(const auto & ticket : pending) check(CoinRenderTarget::cancelReadback(ticket),"Queue cancellation failed");

    target.reset(new CoinRenderTargetP(SbVec2i32(32,32)));
    check(target->executeFrameAsync(f,a).status==CoinRenderBackendStatus::SUCCESS &&
      target->executeFrameAsync(f,b).status==CoinRenderBackendStatus::SUCCESS,"Device-loss setup failed");
    setenv("COIN_BGFX_TEST_DEVICE_LOST_ON_SUBMIT_ONCE","1",1);
    check(target->executeFrame(f).status==CoinRenderBackendStatus::DEVICE_LOST,"Injected device loss was not propagated");
    color={7}; depth={7};
    check(CoinRenderTarget::pollReadback(a,color,depth)==CoinRenderTarget::READBACK_DEVICE_LOST &&
      color[0]==7 && depth[0]==7,"Lost-device poll changed outputs or did not report device loss");
    target.reset();
    check(CoinRenderTarget::cancelReadback(a) && CoinRenderTarget::cancelReadback(b),"Lost-device ticket cancellation failed");
    target.reset(new CoinRenderTargetP(SbVec2i32(32,32)));
    check(target->executeFrame(f).status==CoinRenderBackendStatus::SUCCESS,"BGFX failed to recover after all lost-device tickets retired");
    std::cout << "Coin transparency modes, GPU depth, async lifetime and public action passed\n";
    return 0;
  } catch (const std::exception & e) { std::cerr << e.what() << '\n'; return 1; }
}
