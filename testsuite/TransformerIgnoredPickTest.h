#ifndef COIN_TRANSFORMER_IGNORED_PICK_TEST_H
#define COIN_TRANSFORMER_IGNORED_PICK_TEST_H

#include <Inventor/draggers/SoTransformerDragger.h>
#include <Inventor/actions/SoHandleEventAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/SoPath.h>
#include <Inventor/SbViewportRegion.h>

// Real ray picking without a GL context. Checks the public behavior of an
// ignored drag, not the non-virtual internal drag() call count. Removing only
// the modifier guard is not observable here because drag(NONE) is a no-op.
namespace TransformerIgnoredPickTest {
typedef void Check(bool, const char *);

inline void valueChanged(void * data, SoDragger *)
{
  ++*static_cast<int *>(data);
}

struct Scene {
  SoSeparator * root;
  SoTransformerDragger * dragger;
  bool registered;
  int changes;
  Scene() : root(new SoSeparator), dragger(new SoTransformerDragger),
            registered(false), changes(0)
  {
    root->ref();
    root->addChild(dragger);
  }
  ~Scene()
  {
    dragger->removeValueChangedCallback(valueChanged, &changes);
    // Break root -> dragger -> surrogate path -> root before releasing it.
    if (registered) dragger->setPartAsPath("surroundScale", NULL);
    root->unref();
  }
private:
  Scene(const Scene &);
  Scene & operator=(const Scene &);
};

inline void run(bool modifiers, Check * check)
{
  Scene scene;
  SoSeparator * root = scene.root;
  SoTransformerDragger * dragger = scene.dragger;
  int & changes = scene.changes;
  dragger->addValueChangedCallback(valueChanged, &changes);
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  root->insertChild(camera, 0);
  SoSeparator * surrogate = new SoSeparator;
  SoTranslation * translation = new SoTranslation;
  translation->translation.setValue(5, 0, 0);
  surrogate->addChild(translation);
  SoCube * cube = new SoCube;
  surrogate->addChild(cube);
  root->addChild(surrogate);

  SbViewportRegion viewport(400, 400);
  camera->viewAll(root, viewport);
  SoSearchAction search;
  search.setNode(cube);
  search.apply(root);
  SoPath * path = search.getPath();
  check(path != NULL, "surrogate cube path exists");
  if (!path) return;
  scene.registered = dragger->setPartAsPath("surroundScale", path) != FALSE;
  check(scene.registered, "surrogate registered for an unrecognized part");
  if (!scene.registered) return;

  SbVec3f screen;
  camera->getViewVolume(viewport.getViewportAspectRatio()).projectToScreen(
    SbVec3f(5, 0, 0), screen);
  const SbVec2s size = viewport.getViewportSizePixels();
  const SbVec2s position(static_cast<short>(screen[0] * size[0]),
                        static_cast<short>(screen[1] * size[1]));
  SoHandleEventAction action(viewport);
  const SbMatrix original = dragger->getMotionMatrix();
  SoMouseButtonEvent button;
  button.setButton(SoMouseButtonEvent::BUTTON1);
  button.setState(SoButtonEvent::DOWN);
  button.setPosition(position);
  action.setEvent(&button);
  action.apply(root);
  const bool active = dragger->isActive.getValue() != FALSE;
  check(active, "mouse-down reaches the dragger (required, not a skip)");
  if (!active) return;
  check(dragger->getCurrentState() == SoTransformerDragger::INACTIVE,
        "unrecognized part leaves transformer state inactive");

  if (modifiers) {
    SoKeyboardEvent key;
    key.setState(SoButtonEvent::DOWN);
    key.setPosition(position);
    key.setKey(SoKeyboardEvent::LEFT_SHIFT);
    key.setShiftDown(TRUE);
    action.setEvent(&key);
    action.apply(root);
    key.setKey(SoKeyboardEvent::LEFT_CONTROL);
    key.setCtrlDown(TRUE);
    action.apply(root);
    check(dragger->isActive.getValue() != FALSE,
          "modifier events execute while the base dragger is active");
    check(dragger->getCurrentState() == SoTransformerDragger::INACTIVE,
          "modifier events preserve inactive transformer state");
    check(dragger->getMotionMatrix() == original,
          "modifier events preserve the original matrix");
    check(changes == 0, "modifier events do not emit value changes");
  }

  SoLocation2Event move;
  move.setPosition(SbVec2s(position[0] + 5, position[1] + 5));
  action.setEvent(&move);
  action.apply(root);
  check(dragger->getMotionMatrix() == original,
        "movement after an ignored pick preserves the matrix");
  button.setState(SoButtonEvent::UP);
  button.setPosition(move.getPosition());
  action.setEvent(&button);
  action.apply(root);
  check(!dragger->isActive.getValue(), "mouse-up releases the dragger");
  check(dragger->getCurrentState() == SoTransformerDragger::INACTIVE,
        "mouse-up preserves inactive transformer state");
  check(dragger->getMotionMatrix() == original, "mouse-up preserves the matrix");
  check(changes == 0, "ignored drag does not emit value changes");
}
}
#endif
