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

#include <cstdlib>
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
  int passOrdinal = 0;
  for (std::vector<SbVec2s>::const_reverse_iterator it = sizes.rbegin();
       it != sizes.rend(); ++it) {
    SoSeparator * parent = new SoSeparator;
    SoSceneTexture2 * texture = makeTexture(*it, child);
    texture->backgroundColor.setValue(float(++passOrdinal) / 10.0f, 0, 0, 1);
    parent->addChild(texture);
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

  // Staged: four RGBA8 passes; direct: two color+depth passes. Each
  // variant totals exactly 64 MiB. Skipped passes are not charged.
  SoSeparator * exactLimit = new SoSeparator;
  exactLimit->ref();
  exactLimit->addChild(makeSkippedByQuality());
  exactLimit->addChild(makeSkippedByOverride());
  const char * directMode = std::getenv("COIN_WGPU_RTT_GPU_DIRECT");
  const bool direct = directMode && directMode[0] == '1' && directMode[1] == '\0';
  const std::vector<SbVec2s> exactLarge(direct ? 2 : 4, SbVec2s(2048, 2048));
  exactLimit->addChild(makeChain(exactLarge));

  action.setBackgroundColor(SbColor4f(0.1f, 0.2f, 0.8f, 1.0f));
  action.apply(exactLimit);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
             "the exact 64 MiB boundary, including skipped passes, failed",
             action)) return 1;
  std::vector<uint8_t> published;
  target->readbackRGBA(published);
  if (!check(published.size() == 16u * 16u * 4u,
             "exact-boundary apply did not publish a frame", action)) return 1;

  // One more nested subpass exceeds 64 MiB. Direct mode must capture the
  // plan before deciding whether it shares a producer; staged mode can
  // reject before entering that subscene.
  std::vector<SbVec2s> overLimit = exactLarge;
  overLimit.push_back(SbVec2s(1, 1));
  int rejectedLeafTraversals = 0;
  SoSeparator * exceedsLimit = makeChain(overLimit, &rejectedLeafTraversals);
  exceedsLimit->ref();
  action.setBackgroundColor(SbColor4f(0.8f, 0.1f, 0.1f, 1.0f));
  action.apply(exceedsLimit);
  std::vector<uint8_t> afterFailure;
  target->readbackRGBA(afterFailure);
  const bool rejectedAsExpected = isBudgetError(action) &&
    rejectedLeafTraversals == (direct ? 1 : 0) && afterFailure == published;
  if (!rejectedAsExpected) {
    std::cerr << "budget diagnostic: status=" << action.getLastStatus()
              << " leaf=" << rejectedLeafTraversals
              << " preserved=" << (afterFailure == published) << "\n";
  }
  if (!check(rejectedAsExpected,
             "nested over-budget apply submitted or published a frame",
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
  if (direct) {
    // Three occurrences but only two distinct 2048² producers cost 64 MiB.
    // A fourth, distinct producer must be rejected without publishing.
    SoSeparator * shared = new SoSeparator;
    shared->ref();
    SoSceneTexture2 * first = makeTexture(SbVec2s(2048, 2048), new SoSeparator);
    first->backgroundColor.setValue(1, 0, 0, 1);
    SoSceneTexture2 * second = makeTexture(SbVec2s(2048, 2048), new SoSeparator);
    second->backgroundColor.setValue(0, 1, 0, 1);
    shared->addChild(first);
    shared->addChild(first);
    shared->addChild(second);
    action.apply(shared);
    if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
               "shared producer was charged twice at the 64 MiB boundary",
               action)) return 1;
    std::vector<uint8_t> sharedFrame;
    target->readbackRGBA(sharedFrame);

    SoSceneTexture2 * third = makeTexture(SbVec2s(2048, 2048), new SoSeparator);
    third->backgroundColor.setValue(0, 0, 1, 1);
    shared->addChild(third);
    action.apply(shared);
    std::vector<uint8_t> afterSharedFailure;
    target->readbackRGBA(afterSharedFailure);
    if (!check(isBudgetError(action) && afterSharedFailure == sharedFrame,
               "distinct producer exceeded the 64 MiB limit without rollback",
               action)) return 1;
    shared->unref();
  }
  exactLimit->unref();
  delete target;
  std::cout << "WgpuSceneTextureBudgetTest passed\n";
  return 0;
}
