#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoFragmentShader.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
#include <Inventor/nodes/SoSceneTextureCubeMap.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <iostream>
#include <memory>

// Characterization of P15 blockers, not certification of these features.
class InventoryGlOnlyNode : public SoNode {
  SO_NODE_HEADER(InventoryGlOnlyNode);

public:
  static void initClass() { SO_NODE_INIT_CLASS(InventoryGlOnlyNode, SoNode, "Node"); }
  InventoryGlOnlyNode() { SO_NODE_CONSTRUCTOR(InventoryGlOnlyNode); }
  unsigned glCalls = 0;

protected:
  ~InventoryGlOnlyNode() override {}
  void GLRender(SoGLRenderAction*) override { ++glCalls; }
};
SO_NODE_SOURCE(InventoryGlOnlyNode);
class InventoryFaces : public SoIndexedFaceSet {
  SO_NODE_HEADER(InventoryFaces);

public:
  static void initClass() {
    SO_NODE_INIT_CLASS(InventoryFaces, SoIndexedFaceSet, "IndexedFaceSet");
  }
  InventoryFaces() { SO_NODE_CONSTRUCTOR(InventoryFaces); }
  unsigned callbacks = 0;

protected:
  ~InventoryFaces() override {}
  void callback(SoCallbackAction* action) override {
    ++callbacks;
    SoIndexedFaceSet::callback(action);
  }
};
SO_NODE_SOURCE(InventoryFaces);
struct Witness {
  CoinRenderFramePlan frame;
  unsigned submits = 0;
};
class CaptureBackend : public CoinRenderBackend {
public:
  explicit CaptureBackend(Witness& value) : witness(value) {}
  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP&) override {
    return CoinRenderBackendStatus::SUCCESS;
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan& frame, CoinRenderTargetP&) override {
    witness.frame = frame;
    return {CoinRenderBackendStatus::SUCCESS, "", ++witness.submits};
  }
  void poll() override {}
  const std::string& getLastError() const override { return diagnostic; }

private:
  Witness& witness;
  std::string diagnostic;
};
static bool check(bool result, const char* message) {
  if (!result)
    std::cerr << "CoinRenderNodeInventoryTest: " << message << '\n';
  return result;
}
int main() {
  SoDB::init();
  CoinRenderAction::initClass();
  InventoryGlOnlyNode::initClass();
  InventoryFaces::initClass();
  Witness witness;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64, 64)));
  target->getPimpl().get().backend.reset(new CaptureBackend(witness));
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target.get());
  SoSeparator* root = new SoSeparator;
  root->ref();
  auto* camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 3);
  root->addChild(camera);
  auto capture = [&](SoNode* node) {
    root->addChild(node);
    action.apply(root);
    root->removeChild(node);
    return action.getLastStatus() == CoinRenderAction::SUCCESS;
  };
  auto* glOnly = new InventoryGlOnlyNode;
  glOnly->ref();
  bool ok = check(capture(glOnly) && !glOnly->glCalls && witness.frame.draws.empty(),
                  "GL-only node currently succeeds with no draws: a blocker, not support");
  glOnly->unref();
  auto* text = new SoText2;
  text->string = "P15";
  ok &= check(capture(text) && witness.frame.draws.empty(),
              "SoText2 has no callback primitives and is silently omitted");
  auto* image = new SoImage;
  std::vector<unsigned char> bytes(4 * 4 * 4, 255);
  image->image.setValue(SbVec2s(4, 4), 4, bytes.data());
  ok &=
      check(capture(image) && !witness.frame.draws.empty() && witness.frame.textures.empty(),
            "standalone SoImage emits a quad but its image is not captured as an enabled texture");
  auto* coords = new SoCoordinate3;
  const SbVec3f points[] = {SbVec3f(-1, -1, 0), SbVec3f(1, -1, 0), SbVec3f(0, 1, 0)};
  coords->point.setValues(0, 3, points);
  root->addChild(coords);
  auto* faces = new InventoryFaces;
  faces->ref();
  const int32_t indices[] = {0, 1, 2, -1};
  faces->coordIndex.setValues(0, 4, indices);
  for (bool fast : {true, false}) {
    action.setFastPathEnabled(fast);
    const unsigned before = faces->callbacks;
    ok &= check(capture(faces) && faces->callbacks == before + 1 && !witness.frame.draws.empty(),
                "indexed subclasses retain their virtual callback in fast and generic paths");
  }
  faces->unref();
  auto* program = new SoShaderProgram;
  auto* shader = new SoFragmentShader;
  shader->sourceType = SoShaderObject::GLSL_PROGRAM;
  shader->sourceProgram = "void main() { gl_FragColor = vec4(1.0,0.0,0.0,1.0); }";
  program->shaderObject.set1Value(0, shader);
  root->addChild(program);
  const unsigned beforeShader = witness.submits;
  ok &= check(!capture(new SoCube) &&
              action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
              witness.submits == beforeShader &&
              action.getLastError().find("SoShaderProgram") >= 0,
              "active shader must fail before publication");
  root->removeChild(program);
  ok &= check(capture(new SoTexture3) && capture(new SoTextureCubeMap),
              "empty texture nodes must remain inert");
  const unsigned beforeEffects = witness.submits;
  auto* volume = new SoTexture3;
  const unsigned char whiteVolume[8] = {255,255,255,255,255,255,255,255};
  volume->images.setValue(SbVec3s(2, 2, 2), 1, whiteVolume);
  ok &= check(!capture(volume) &&
              action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
              witness.submits == beforeEffects &&
              action.getLastError().find("SoTexture3") >= 0,
              "3D texture must fail before publication");
  auto* cubeMap = new SoTextureCubeMap;
  const unsigned char whiteFace[4] = {255,255,255,255};
  cubeMap->imagePosX.setValue(SbVec2s(2, 2), 1, whiteFace);
  ok &= check(!capture(cubeMap) &&
              action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
              witness.submits == beforeEffects &&
              action.getLastError().find("SoTextureCubeMap") >= 0,
              "cube texture must fail before publication");
  auto* cubeRtt = new SoSceneTextureCubeMap;
  cubeRtt->scene = new SoCube;
  ok &= check(!capture(cubeRtt) &&
              action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
              witness.submits == beforeEffects &&
              action.getLastError().find("SoSceneTextureCubeMap") >= 0,
              "cube scene texture must fail before publication");
  auto* shadows = new SoShadowGroup;
  shadows->ref();
  shadows->addChild(new SoCube);
  ok &= check(!capture(shadows) &&
              action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
              witness.submits == beforeEffects &&
              action.getLastError().find("SoShadowGroup") >= 0,
              "active shadow group must fail before publication");
  shadows->isActive = FALSE;
  ok &= check(capture(shadows) && witness.submits == beforeEffects + 1 &&
              !witness.frame.draws.empty(),
              "inactive shadow group must preserve ordinary child traversal");
  shadows->unref();
  action.setRenderTarget(nullptr);
  root->unref();
  if (!ok)
    return 1;
  std::cout << "P15/P25: GL-only/Text2 omission, SoImage texture gap, effect gates and subclass "
               "callbacks characterized\n";
  return 0;
}
