#include "config.h"
#include <Inventor/SoDB.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/rendering/SoRenderIR.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoCube.h>
#include <iostream>
#include <memory>
#include <string>
int main() {
  SoDB::init();
  CoinRenderAction::initClass();
  CoinRenderOptions options;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),options));
  if (!target) return 1;
  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera; camera->position.setValue(0,0,5);
  camera->nearDistance = 1; camera->farDistance = 10; root->addChild(camera);
  root->addChild(new SoCube);
  CoinRenderRetainedRenderer renderer(SbViewportRegion(32,32));
  bool ok = renderer.render(root,*target) == CoinRenderAction::SUCCESS;
  std::vector<uint8_t> previous, current;
  target->readbackRGBA(previous);
  ok &= !previous.empty();
  const auto serial = target->getLastSubmissionSerial();
  const auto captures = renderer.getCaptureCount(), plans = renderer.getPlanBuildCount();
  auto * material = new SoPBRMaterial; root->insertChild(material,1);
  ok &= renderer.render(root,*target) == CoinRenderAction::UNSUPPORTED;
  ok &= renderer.getCaptureCount() == captures+1 && renderer.getPlanBuildCount() == plans+1;
  ok &= target->getLastSubmissionSerial() == serial;
  target->readbackRGBA(current);
  ok &= current == previous;
  ok &= std::string(renderer.getLastError().getString()).find("PBR executor transport") != std::string::npos;
  ok &= renderer.getDrawList().getCommandSnapshot(0).material.pbr != nullptr;
  // The separate callback-capture entry point must also refuse unsupported shading.
  CoinRenderAction currentCapture(SbViewportRegion(32,32));
  currentCapture.setRenderTarget(target.get());currentCapture.apply(root);
  ok &= currentCapture.getLastStatus() == CoinRenderAction::UNSUPPORTED;
  target->readbackRGBA(current);
  ok &= current == previous && target->getLastSubmissionSerial() == serial;
  currentCapture.setRenderTarget(nullptr);
  root->removeChild(material);
  ok &= renderer.render(root,*target) == CoinRenderAction::SUCCESS;
  root->unref();
  std::cout << "Retained PBR admission and publication: " << (ok ? "PASS" : "FAIL") << '\n';
  return ok ? 0 : 1;
}
