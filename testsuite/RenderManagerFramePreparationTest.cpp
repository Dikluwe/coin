#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/sensors/SoOneShotSensor.h>
#include <Inventor/sensors/SoFieldSensor.h>
#include <Inventor/sensors/SoSensorManager.h>
#include <Inventor/fields/SoSFTime.h>
#include <cstdlib>
#include <iostream>

// Run with DISPLAY unset: an accidental GL traversal is detected before it can
// require a context, rather than relying on a driver-specific diagnostic.
class GLSpy : public SoGLRenderAction {
public:
  GLSpy() : SoGLRenderAction(SbViewportRegion(640, 480)), traversals(0) {}
  int traversals;
protected:
  void beginTraversal(SoNode *) override { ++traversals; }
};
struct Counts { int pre = 0; int post = 0; int sensor = 0; };
static void pre(void * data, SoRenderManager *) { ++static_cast<Counts *>(data)->pre; }
static void post(void * data, SoRenderManager *) { ++static_cast<Counts *>(data)->post; }
static void sensor(void * data, SoSensor *) { ++static_cast<Counts *>(data)->sensor; }
static void tick(void * data, SoSensor *) { ++*static_cast<int *>(data); }
static void require(bool condition, const char * message)
{
  if (!condition) { std::cerr << message << '\n'; std::exit(EXIT_FAILURE); }
}
int main()
{
  SoDB::init();
  SoRenderManager manager;
  GLSpy spy;
  manager.setGLRenderAction(&spy);
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  root->addChild(camera);
  root->addChild(new SoCube);
  camera->position = SbVec3f(0, 0, 10);
  camera->nearDistance = 0.1f;
  camera->farDistance = 1000.0f;
  manager.setSceneGraph(root);
  manager.setCamera(camera);
  manager.setAutoClipping(SoRenderManager::VARIABLE_NEAR_PLANE);
  Counts counts;
  manager.addPreRenderCallback(pre, &counts);
  manager.addPostRenderCallback(post, &counts);
  SoOneShotSensor delayed(sensor, &counts);
  delayed.setPriority(1);
  delayed.schedule();
  require(manager.prepareFrame(), "first preparation failed");
  require(!manager.prepareFrame(), "recursive preparation allowed");
  require(counts.pre == 1 && counts.sensor == 1, "callbacks/sensors not prepared");
  require(camera->farDistance.getValue() < 100.0f, "autoclip not updated");
  int ticks = 0;
  SoFieldSensor clockSensor(tick, &ticks);
  SoSFTime * clock = static_cast<SoSFTime *>(SoDB::getGlobalField("realTime"));
  clockSensor.attach(clock);
  manager.finishFrame();
  SoDB::getSensorManager()->processDelayQueue(FALSE);
  require(ticks == 1, "animation clock notification not preserved after rendering");
  manager.finishFrame();
  require(counts.post == 1, "completion must be paired exactly once");
  camera->position = SbVec3f(0, 0, 20);
  const float previousFar = camera->farDistance.getValue();
  require(manager.prepareFrame(FALSE), "next preparation failed");
  require(camera->farDistance.getValue() > previousFar, "camera change not prepared");
  const SbTime timeBeforeFailure = clock->getValue();
  manager.finishFrame(FALSE);
  require(clock->getValue() == timeBeforeFailure, "failed frame advanced animation clock");
  require(counts.pre == 2 && counts.post == 2, "failure lifecycle not completed");
  require(spy.traversals == 0, "GL rendering occurred during backend-independent frames");
  manager.setSceneGraph(nullptr);
  root->unref();
  std::cout << "frames=2 gl_traversals=0 sensors=1 callbacks=2 autoclip=updated\n";
}
