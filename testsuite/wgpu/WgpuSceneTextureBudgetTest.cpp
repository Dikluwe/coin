#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoAction.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/elements/SoTextureOverrideElement.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char * message,
           const SoWgpuRenderAction & action) {
  if (!condition) {
    std::cerr << "WgpuSceneTextureBudgetTest: " << message << ": "
              << action.getLastError().getString() << "\n";
  }
  return condition;
}

void countTraversal(void * userdata, SoAction *) {
  ++*static_cast<int *>(userdata);
}

void enableImageOverride(void *, SoAction * action) {
  SoTextureOverrideElement::setImageOverride(action->getState(), TRUE);
}

SoSceneTexture2 * makeTexture(const SbVec2s & size, SoNode * scene) {
  SoSceneTexture2 * texture = new SoSceneTexture2;
  texture->size.setValue(size);
  texture->scene.setValue(scene);
  texture->type.setValue(SoSceneTexture2::RGBA8);
  return texture;
}

SoSeparator * makeChain(const std::vector<SbVec2s> & sizes,
                        int * leafTraversalCount = NULL) {
  SoSeparator * child = new SoSeparator;
  if (leafTraversalCount) {
    SoCallback * witness = new SoCallback;
    witness->setCallback(countTraversal, leafTraversalCount);
    child->addChild(witness);
  }
  for (std::vector<SbVec2s>::const_reverse_iterator it = sizes.rbegin();
       it != sizes.rend(); ++it) {
    SoSeparator * parent = new SoSeparator;
    parent->addChild(makeTexture(*it, child));
    child = parent;
  }
  return child;
}

SoSeparator * makeSkippedByQuality() {
  SoSeparator * group = new SoSeparator;
  SoComplexity * complexity = new SoComplexity;
  complexity->textureQuality.setValue(0.0f);
  group->addChild(complexity);
  group->addChild(makeTexture(SbVec2s(2048, 2048), new SoSeparator));
  return group;
}

SoSeparator * makeSkippedByOverride() {
  SoSeparator * group = new SoSeparator;
  SoCallback * overrideNode = new SoCallback;
  overrideNode->setCallback(enableImageOverride);
  group->addChild(overrideNode);
  group->addChild(makeTexture(SbVec2s(2048, 2048), new SoSeparator));
  return group;
}

bool isBudgetError(const SoWgpuRenderAction & action) {
  const std::string error(action.getLastError().getString());
  return action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED &&
         error.find("SoSceneTexture2") != std::string::npos &&
         error.find("64 MiB") != std::string::npos;
}

} // namespace

int main() {
  SoDB::init();
  SoWgpuRenderAction::initClass();

  SoWgpuRenderTarget * target =
    SoWgpuRenderTarget::createOffscreen(SbVec2i32(16, 16));
  if (!target || target->getStatus() != SoWgpuRenderTarget::TARGET_READY) {
    std::cerr << "WgpuSceneTextureBudgetTest: offscreen target unavailable\n";
    delete target;
    return 1;
  }
  SoWgpuRenderAction action(SbViewportRegion(16, 16));
  action.setRenderTarget(target);

  // Four 2048 x 2048 RGBA8 subpasses total exactly 64 MiB. The two
  // skipped passes would push the apply over the limit if they were charged.
  SoSeparator * exactLimit = new SoSeparator;
  exactLimit->ref();
  exactLimit->addChild(makeSkippedByQuality());
  exactLimit->addChild(makeSkippedByOverride());
  const std::vector<SbVec2s> fourLarge(4, SbVec2s(2048, 2048));
  exactLimit->addChild(makeChain(fourLarge));

  action.setBackgroundColor(SbColor4f(0.1f, 0.2f, 0.8f, 1.0f));
  action.apply(exactLimit);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
             "the exact 64 MiB boundary, including skipped passes, failed",
             action)) return 1;
  std::vector<uint8_t> published;
  target->readbackRGBA(published);
  if (!check(published.size() == 16u * 16u * 4u,
             "exact-boundary apply did not publish a frame", action)) return 1;

  // The fifth nested subpass costs just four bytes. Its scene must not
  // even be traversed once the shared reservation would exceed 64 MiB.
  std::vector<SbVec2s> overLimit = fourLarge;
  overLimit.push_back(SbVec2s(1, 1));
  int rejectedLeafTraversals = 0;
  SoSeparator * exceedsLimit = makeChain(overLimit, &rejectedLeafTraversals);
  exceedsLimit->ref();
  action.setBackgroundColor(SbColor4f(0.8f, 0.1f, 0.1f, 1.0f));
  action.apply(exceedsLimit);
  std::vector<uint8_t> afterFailure;
  target->readbackRGBA(afterFailure);
  if (!check(isBudgetError(action) && rejectedLeafTraversals == 0 &&
             afterFailure == published,
             "nested over-budget apply rendered a subpass or published a frame",
             action)) return 1;

  // A rejected apply must not leave any budget charged to the next apply.
  action.setBackgroundColor(SbColor4f(0.1f, 0.8f, 0.1f, 1.0f));
  action.apply(exactLimit);
  std::vector<uint8_t> recovered;
  target->readbackRGBA(recovered);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             recovered.size() == published.size() && recovered != published,
             "a new top-level apply did not start with a fresh budget",
             action)) return 1;

  exceedsLimit->unref();
  exactLimit->unref();
  delete target;
  std::cout << "WgpuSceneTextureBudgetTest passed\n";
  return 0;
}
