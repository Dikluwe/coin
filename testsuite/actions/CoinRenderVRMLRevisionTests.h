// Notification qualification must avoid graph work while preserving VRML capture.
#include <Inventor/misc/SoChildList.h>
#include <Inventor/fields/SoSFNode.h>
#include <Inventor/sensors/SoNodeSensor.h>
#include <Inventor/fields/SoMFNode.h>
#ifdef HAVE_VRML97
#include <Inventor/VRMLnodes/SoVRMLGroup.h>
#endif

namespace {
// Transparent instrumentation: inherit the native separator type and behavior.
// A newly registered custom SoType would deliberately disqualify camera reuse.
class RevisionCountGroup : public SoSeparator {
public:
  mutable unsigned queries = 0;
  SoChildList * getChildren() const override { ++queries; return SoSeparator::getChildren(); }
};

class RevisionNodeFields : public SoNode {
  SO_NODE_HEADER(RevisionNodeFields);
public:
  SoSFNode one;
  SoMFNode many;
  RevisionNodeFields() {
    SO_NODE_CONSTRUCTOR(RevisionNodeFields);
    SO_NODE_ADD_FIELD(one, (nullptr));
    SO_NODE_ADD_FIELD(many, (nullptr));
    many.setNum(0);
  }
  static void initClass() { SO_NODE_INIT_CLASS(RevisionNodeFields, SoNode, "Node"); }
};
SO_NODE_SOURCE(RevisionNodeFields);

int testVRMLRevisionQualification() {
  RevisionNodeFields::initClass();
  auto * root = new SoSeparator;
  root->ref();
  auto * camera = new SoPerspectiveCamera;
  root->addChild(camera);
  std::vector<RevisionCountGroup *> probes;
  for (int i = 0; i < 96; ++i) {
    auto * group = new RevisionCountGroup;
    group->addChild(new SoCube);
    probes.push_back(group);
    root->addChild(group);
  }
  CoinRenderAction action;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
  TEST_ASSERT(target, "qualification requires a normal render target shell");
  auto * backend = new CompositionCaptureBackend;
  target->getPimpl()->backend.reset(backend);
  target->getPimpl()->depthReadbackEnabled = false;
  action.setRenderTarget(target.get());
  action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "initial non-VRML qualification must capture");
  const auto revision = action.getPimpl()->lastValidPlan.revision;
  const auto * vertices = action.getPimpl()->lastValidPlan.vertices.data();
  const auto reset = [&]() { for (auto * p : probes) p->queries = 0; };
  const auto queries = [&]() { unsigned n = 0; for (auto * p : probes) n += p->queries; return n; };
  reset();
  for (int i = 0; i < 4; ++i) action.apply(root);
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && queries() == 0,
              "static non-VRML cache hits must not query every descendant child list");
  TEST_ASSERT(action.getPimpl()->lastValidPlan.revision == revision &&
              action.getPimpl()->lastValidPlan.vertices.data() == vertices,
              "static qualification must retain real frame reuse");
  reset();
  for (int i = 0; i < 4; ++i) {
    camera->position.setValue(float(i + 1), 0, 10);
    action.apply(root);
  }
  TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS && queries() == 0,
              "proven camera-only changes must not query descendant child lists");
  TEST_ASSERT(action.getPimpl()->lastValidPlan.vertices.data() == vertices,
              "camera qualification must retain geometry reuse");
  action.setRenderTarget(nullptr);
  root->unref();
  return 0;
}

#ifdef HAVE_VRML97
int testVRMLRevisionInvalidation() {
  auto * root = new SoSeparator;
  root->ref();
  auto * cube = new SoCube;
  root->addChild(cube);
  auto * fields = new RevisionNodeFields;
  root->addChild(fields);
  auto * marker = new SoVRMLGroup;
  marker->ref();
  CoinRenderAction first, second;
  std::unique_ptr<CoinRenderTarget> firstTarget(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
  std::unique_ptr<CoinRenderTarget> secondTarget(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
  TEST_ASSERT(firstTarget && secondTarget, "invalidation requires normal target shells");
  firstTarget->getPimpl()->backend.reset(new CompositionCaptureBackend);
  secondTarget->getPimpl()->backend.reset(new CompositionCaptureBackend);
  firstTarget->getPimpl()->depthReadbackEnabled = secondTarget->getPimpl()->depthReadbackEnabled = false;
  first.setRenderTarget(firstTarget.get()); second.setRenderTarget(secondTarget.get());
  const auto apply = [&]() {
    first.apply(root); second.apply(root);
    return first.getLastStatus() == CoinRenderAction::SUCCESS &&
      second.getLastStatus() == CoinRenderAction::SUCCESS;
  };
  TEST_ASSERT(apply(), "ordinary baseline capture must succeed for independent actions");
  const auto silentChangeObserved = [&]() {
    // Establish steady state before each edit: neither an initial capture nor
    // a pending structural notification may conceal erroneous later reuse.
    for (int edit = 0; edit < 3; ++edit) {
      for (int steady = 0; steady < 3; ++steady) if (!apply()) return false;
      const auto beforeA = first.getPimpl()->lastValidPlan;
      const auto beforeB = second.getPimpl()->lastValidPlan;
      const SbBool notify = cube->width.enableNotify(FALSE);
      cube->width = cube->width.getValue() + 1.0f;
      cube->width.enableNotify(notify);
      if (!apply() || beforeA.hasSamePayload(first.getPimpl()->lastValidPlan) ||
          beforeB.hasSamePayload(second.getPimpl()->lastValidPlan)) return false;
    }
    return true;
  };
  root->addChild(marker);
  TEST_ASSERT(apply() && silentChangeObserved(),
              "inserting VRML must preserve subsequent silent geometry observation in both actions");
  root->removeChild(marker);
  TEST_ASSERT(apply(), "removing VRML must allow normal capture");
  const auto ordinary = first.getPimpl()->lastValidPlan.revision;
  TEST_ASSERT(apply() && first.getPimpl()->lastValidPlan.revision == ordinary,
              "removing final VRML must restore real frame reuse");
  fields->one = marker;
  TEST_ASSERT(apply() && silentChangeObserved(),
              "VRML reachable only through SFNode must preserve silent recapture");
  fields->one = nullptr;
  fields->many.set1Value(0, marker);
  TEST_ASSERT(apply() && silentChangeObserved(),
              "VRML reachable only through MFNode must preserve silent recapture");
  fields->many.setNum(0);
  TEST_ASSERT(apply(), "clearing node fields must invalidate the qualification");
  // Public semantic-type changes must invalidate both root qualifications.
  unsigned notifications = 0;
  SoNodeSensor typeSensor([](void * data, SoSensor *) {
    ++*static_cast<unsigned *>(data);
  }, &notifications);
  typeSensor.setPriority(0);
  typeSensor.attach(root);
  const auto inventorRevision = root->getNodeId();
  cube->setNodeType(SoNode::INVENTOR);
  TEST_ASSERT(root->getNodeId() == inventorRevision && notifications == 0,
              "setting the existing semantic type must be idempotent");
  cube->setNodeType(SoNode::VRML2);
  TEST_ASSERT(root->getNodeId() != inventorRevision && notifications == 1,
              "semantic type transition must notify and revise reachable roots");
  TEST_ASSERT(apply() && silentChangeObserved(),
              "INVENTOR to VRML2 transition must activate silent-change protection");
  const auto vrmlRevision = root->getNodeId();
  cube->setNodeType(SoNode::VRML2);
  TEST_ASSERT(root->getNodeId() == vrmlRevision && notifications == 1,
              "repeating VRML2 must preserve revision and avoid duplicate notification");
  cube->setNodeType(SoNode::INVENTOR);
  TEST_ASSERT(root->getNodeId() != vrmlRevision && notifications == 2 && apply(),
              "return to INVENTOR must notify both actions and invalidate VRML knowledge");
  const auto restoredRevision = first.getPimpl()->lastValidPlan.revision;
  TEST_ASSERT(apply() && first.getPimpl()->lastValidPlan.revision == restoredRevision,
              "return to INVENTOR must restore frame reuse");
  typeSensor.detach();
  auto * replacement = new SoSeparator;
  replacement->ref();
  auto * replacementCube = new SoCube;
  replacementCube->width = 17;
  replacement->addChild(replacementCube);
  first.apply(replacement);
  TEST_ASSERT(first.getLastStatus() == CoinRenderAction::SUCCESS &&
              !first.getPimpl()->lastValidPlan.hasSamePayload(second.getPimpl()->lastValidPlan),
              "switching root must not reuse prior-root payload or VRML knowledge");
  root->unref();
  second.apply(replacement);
  TEST_ASSERT(second.getLastStatus() == CoinRenderAction::SUCCESS &&
              first.getPimpl()->lastValidPlan.hasSamePayload(second.getPimpl()->lastValidPlan),
              "destroying old root must detach action observation safely");
  replacement->addChild(marker);
  second.apply(replacement);
  const auto before = second.getPimpl()->lastValidPlan;
  const SbBool notify = replacementCube->width.enableNotify(FALSE);
  replacementCube->width = 19;
  replacementCube->width.enableNotify(notify);
  second.apply(replacement);
  TEST_ASSERT(second.getLastStatus() == CoinRenderAction::SUCCESS &&
              !before.hasSamePayload(second.getPimpl()->lastValidPlan),
              "reused action must resume VRML protection after root replacement");
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(1, 1)));
  TEST_ASSERT(target, "failure fixture requires an offscreen target shell");
  auto * backend = new CompositionCaptureBackend;
  target->getPimpl()->backend.reset(backend);
  target->getPimpl()->depthReadbackEnabled = false;
  first.getPimpl()->planOnly = false;
  first.setRenderTarget(target.get());
  first.apply(replacement);
  TEST_ASSERT(first.getLastStatus() == CoinRenderAction::SUCCESS, "VRML target capture must succeed");
  const auto accepted = first.getPimpl()->lastValidPlan;
  backend->fail = true;
  const SbBool previousNotify = replacementCube->width.enableNotify(FALSE);
  replacementCube->width = 23;
  replacementCube->width.enableNotify(previousNotify);
  first.apply(replacement);
  TEST_ASSERT(first.getLastStatus() != CoinRenderAction::SUCCESS &&
              accepted.hasSamePayload(first.getPimpl()->lastValidPlan),
              "failed VRML submission must retain the accepted payload");
  backend->fail = false;
  TEST_ASSERT(target->resize(SbVec2i32(1, 1)), "target recovery must succeed");
  first.apply(replacement);
  TEST_ASSERT(first.getLastStatus() == CoinRenderAction::SUCCESS &&
              !accepted.hasSamePayload(first.getPimpl()->lastValidPlan),
              "recovery must recapture the silent VRML-scene edit after failed submission");
  first.setRenderTarget(nullptr); second.setRenderTarget(nullptr);
  replacement->unref(); marker->unref();
  return 0;
}
#endif
} // namespace
