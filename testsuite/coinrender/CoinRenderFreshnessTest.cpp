#include "CoinRenderRetainedTestRenderer.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderShaderModule.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoFragmentShader.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <chrono>
#include <thread>
#include <memory>
#include <vector>
#include <iostream>

namespace {
int failures = 0;
CoinRenderOptions options;
const SbViewportRegion viewport(64,64);
void check(bool value, const char * message) {
  if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
struct Scene {
  SoSeparator * root = new SoSeparator;
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  Scene() {
    root->ref();
    camera->position.setValue(0,0,5); camera->nearDistance=1; camera->farDistance=20; camera->focalDistance=5;
    root->addChild(camera);
    auto * light = new SoLightModel; light->model=SoLightModel::BASE_COLOR; root->addChild(light);
    root->addChild(new SoCube);
  }
  ~Scene() { root->unref(); }
};
struct Consumer {
  std::unique_ptr<CoinRenderTarget> target{CoinRenderTarget::createOffscreen(SbVec2i32(64,64),options)};
  CoinRenderAction action{viewport};
  Consumer() { action.setRenderTarget(target.get()); }
  std::vector<uint8_t> render(SoNode * root) {
    action.apply(root);
    if(action.getLastStatus()!=CoinRenderAction::SUCCESS) std::cerr<<action.getLastError().getString()<<'\n';
    check(action.getLastStatus()==CoinRenderAction::SUCCESS,"frame succeeds");
    std::vector<uint8_t> pixels; target->readbackRGBA(pixels);
    check(pixels.size()==64*64*4,"complete RGBA frame"); return pixels;
  }
};
#ifdef COIN_FRESHNESS_GPU
CoinRenderReadbackTicket submit(Consumer & consumer, SoNode * root) {
  CoinRenderReadbackTicket ticket;
  consumer.action.applyAsync(root,ticket);
  check(consumer.action.getLastStatus()==CoinRenderAction::SUCCESS && ticket.token,"async submission succeeds");
  return ticket;
}
void staleHistorical(Consumer & consumer, const CoinRenderReadbackTicket & ticket,
                     const std::vector<uint8_t> & expected) {
  std::vector<uint8_t> pixels{42}; std::vector<float> depth{.25f}; SbString why;
  check(consumer.target->pollCurrentReadback(ticket,pixels,depth,&why)==CoinRenderTarget::READBACK_STALE_PRESENTATION,
        "old ticket refused before another render");
  check(pixels==std::vector<uint8_t>{42} && depth==std::vector<float>{.25f} && why.getLength(),
        "refusal is transactional and diagnosed");
  auto status=CoinRenderTarget::READBACK_NOT_READY;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(status==CoinRenderTarget::READBACK_NOT_READY && std::chrono::steady_clock::now()<deadline) {
    status=CoinRenderTarget::pollReadback(ticket,pixels,depth,&why);
    if(status==CoinRenderTarget::READBACK_NOT_READY) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  check(status==CoinRenderTarget::READBACK_READY && pixels==expected,"refusal leaves historical ticket readable and unchanged");
}
#endif
void stereo() {
  Scene scene; Consumer consumer;
  scene.camera->setStereoMode(SoCamera::LEFT_VIEW);
  for (int field=0; field<3; ++field) {
    const auto beforePixels=consumer.render(scene.root);
#ifdef COIN_FRESHNESS_GPU
    const auto ticket=submit(consumer,scene.root);
#endif
    const auto before=consumer.target->getPresentationReceipt();
    scene.camera->setStereoMode(scene.camera->getStereoMode());
    scene.camera->setStereoAdjustment(scene.camera->getStereoAdjustment());
    scene.camera->setBalanceAdjustment(scene.camera->getBalanceAdjustment());
    check(consumer.target->admitsPresentation(before),"no-op stereo setters preserve receipt");
    const auto nodeId=scene.root->getNodeId();
    if(field==0) scene.camera->setStereoMode(SoCamera::RIGHT_VIEW);
    if(field==1) scene.camera->setStereoAdjustment(.6f);
    if(field==2) scene.camera->setBalanceAdjustment(.6f);
    check(scene.root->getNodeId()==nodeId,"stereo publication leaves conventional graph notifications unchanged");
    check(!consumer.target->admitsPresentation(before),"each stereo setter revokes prior receipt immediately");
#ifdef COIN_FRESHNESS_GPU
    staleHistorical(consumer,ticket,beforePixels);
#endif
    const auto afterPixels=consumer.render(scene.root);
    const auto after=consumer.target->getPresentationReceipt();
    check(afterPixels!=beforePixels,"each stereo edit has independently observable pixels");
    check(after.getSceneRevision()>before.getSceneRevision() && after.getCameraRevision()>before.getCameraRevision(),
          "scene and camera revisions advance on stereo publication");
    Scene cold; cold.camera->setStereoMode(scene.camera->getStereoMode());
    cold.camera->setStereoAdjustment(scene.camera->getStereoAdjustment());
    cold.camera->setBalanceAdjustment(scene.camera->getBalanceAdjustment());
    Consumer control;
    check(control.render(cold.root)==afterPixels,"stereo result equals fresh nodes and consumer exactly");
    std::cout<<"FRESH stereo field="<<field<<" old_revision="<<before.getSceneRevision()
      <<" new_revision="<<after.getSceneRevision()<<" camera_revision="<<after.getCameraRevision()<<'\n';
  }
  // A retired observer must not keep a renderer or a node alive, or suppress
  // the live consumer's independent notification on the shared camera.
  { Consumer retired; retired.render(scene.root); }
  const auto before=consumer.target->getPresentationReceipt();
  scene.camera->setStereoAdjustment(.7f);
  check(!consumer.target->admitsPresentation(before),"retired observer does not break live observer");
  // Native retained IR is mono. Refuse unsupported stereo semantics instead of
  // attaching a fresh camera receipt to a mono image.
  scene.camera->setStereoMode(SoCamera::MONOSCOPIC);
  CoinRenderRetainedRenderer retained(viewport);
  check(retained.render(scene.root,*consumer.target)==CoinRenderAction::SUCCESS,"retained mono frame succeeds");
  const auto mono=consumer.target->getPresentationReceipt();
  const auto captures=retained.getCaptureCount();
  scene.camera->setStereoMode(SoCamera::LEFT_VIEW);
  check(!consumer.target->admitsPresentation(mono),"retained camera observes stereo publication immediately");
  check(retained.render(scene.root,*consumer.target)==CoinRenderAction::UNSUPPORTED && retained.getLastError().getLength(),
        "retained mono profile explicitly refuses active stereo");
  scene.camera->setStereoMode(SoCamera::MONOSCOPIC);
  check(retained.render(scene.root,*consumer.target)==CoinRenderAction::SUCCESS && retained.getCaptureCount()==captures+2,
        "retained recovery uses one capture for each requested revision");
}
CoinRenderShaderModule color(float red, float green) {
  CoinRenderShaderModule module; module.alphaMayVary=FALSE;
  CoinRenderShaderExpression expression; expression.value={{red,green,0,1}};
  module.expressions.push_back(expression); return module;
}
void shaders() {
#ifdef COIN_FRESHNESS_GPU
  Scene scene;
  auto * program=new SoShaderProgram; auto * fragment=new SoFragmentShader;
  fragment->sourceType=SoShaderObject::GLSL_PROGRAM;
  fragment->sourceProgram="void main(){gl_FragColor=vec4(1.0,0.0,0.0,1.0);}";
  fragment->isActive=TRUE; program->shaderObject.set1Value(0,fragment);
  scene.root->insertChild(program,2);
  Consumer consumer;
  check(consumer.action.setPortableShaderModule(program,color(1,0)),"initial shader registration succeeds");
  const auto red=consumer.render(scene.root);
  auto ticket=submit(consumer,scene.root);
  const auto before=ticket.presentation; const auto nodeId=scene.root->getNodeId();
  check(consumer.action.setPortableShaderModule(program,color(0,1)),"shader replacement succeeds");
  check(scene.root->getNodeId()==nodeId && !consumer.target->admitsPresentation(before),
        "shader replacement revokes receipt without graph mutation");
  staleHistorical(consumer,ticket,red);
  const auto green=consumer.render(scene.root);
  auto receipt=consumer.target->getPresentationReceipt();
  check(green!=red && receipt.getSceneRevision()>before.getSceneRevision(),"new shader pixels have a new scene revision");
  Scene cold; auto * coldProgram=new SoShaderProgram; auto * coldFragment=new SoFragmentShader;
  coldFragment->sourceType=SoShaderObject::GLSL_PROGRAM; coldFragment->sourceProgram=fragment->sourceProgram;
  coldProgram->shaderObject.set1Value(0,coldFragment); cold.root->insertChild(coldProgram,2);
  Consumer control; check(control.action.setPortableShaderModule(coldProgram,color(0,1)),"cold shader registration succeeds");
  check(control.render(cold.root)==green,"replacement equals independent cold shader scene");
  auto invalid=color(0,1);invalid.output=99;
  check(!consumer.action.setPortableShaderModule(program,invalid),"invalid shader registration refused");
  check(!consumer.action.setPortableShaderModule(nullptr,color(1,0)),"null program registration refused");
  check(consumer.target->admitsPresentation(receipt),"failed registration preserves publication authority");
  auto * absent=new SoShaderProgram;absent->ref();consumer.action.clearPortableShaderModule(absent);absent->unref();
  check(consumer.target->admitsPresentation(receipt),"removing absent association preserves authority");
  ticket=submit(consumer,scene.root);receipt=ticket.presentation;
  consumer.action.clearPortableShaderModule(program);
  check(!consumer.target->admitsPresentation(receipt),"single shader removal revokes pending ticket immediately");
  staleHistorical(consumer,ticket,green);
  // Inactive shader permits the ordinary path after registration removal.
  fragment->isActive=FALSE; consumer.render(scene.root);
  receipt=consumer.target->getPresentationReceipt(); consumer.action.clearPortableShaderModules();
  check(consumer.target->admitsPresentation(receipt),"clearing empty registry preserves receipt");
  fragment->isActive=TRUE;
  check(consumer.action.setPortableShaderModule(program,color(0,1)),"shader restores after removal");
  consumer.render(scene.root);ticket=submit(consumer,scene.root);
  consumer.action.clearPortableShaderModules();
  check(!consumer.target->admitsPresentation(ticket.presentation),"clear-all revokes pending ticket immediately");
  staleHistorical(consumer,ticket,green);
  fragment->isActive=FALSE; consumer.render(scene.root);
  std::cout<<"FRESH shader replacement/removal/clear/failure/cold controls passed\n";
#else
  std::cout<<"NOT_IMPLEMENTED CPU portable shader execution and async readback; stereo synchronous controls executed\n";
#endif
}
}
int main() {
  SoDB::init();CoinRenderAction::initClass();
#ifdef COIN_FRESHNESS_GPU
  options.renderer=COIN_RENDER_RENDERER_VULKAN;
  if(!coinRetainedTestRenderer(options)) return 1;
#endif
  stereo();shaders();
  std::cout<<"FRESH_SUMMARY failures="<<failures<<'\n';
  return failures?1:0;
}
