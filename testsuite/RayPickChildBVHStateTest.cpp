#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoDB.h>
#include <Inventor/SoPath.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/misc/SoRefPtr.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoSeparator.h>

#include <cstdio>

class TrackingFace : public SoIndexedFaceSet {
  SO_NODE_HEADER(TrackingFace);
public:
  static unsigned missing_coordinates;
  static void initClass() {
    SO_NODE_INIT_CLASS(TrackingFace, SoIndexedFaceSet, "IndexedFaceSet");
  }
  TrackingFace() { SO_NODE_CONSTRUCTOR(TrackingFace); }
  void computeBBox(SoAction * action, SbBox3f & box, SbVec3f & center) override {
    if (SoCoordinateElement::getInstance(action->getState())->getNum() < 18) {
      ++missing_coordinates;
    }
    SoIndexedFaceSet::computeBBox(action, box, center);
  }
};
SO_NODE_SOURCE(TrackingFace);
unsigned TrackingFace::missing_coordinates = 0;

namespace {

void
set_triangles(SoCoordinate3 * coords, float x, float nearest_z)
{
  SbVec3f points[18];
  for (int i = 0; i < 6; ++i) {
    const float z = nearest_z - float(i);
    points[i*3] = SbVec3f(x-1, -1, z);
    points[i*3+1] = SbVec3f(x+1, -1, z);
    points[i*3+2] = SbVec3f(x, 1, z);
  }
  coords->point.setValues(0, 18, points);
}

SoIndexedFaceSet *
add_face(SoGroup * parent, int index)
{
  SoIndexedFaceSet * face = new TrackingFace;
  const int32_t indices[] = {index*3, index*3+1, index*3+2, -1};
  face->coordIndex.setValues(0, 4, indices);
  parent->addChild(face);
  return face;
}

bool
check_hit(SoRayPickAction & pick, SoIndexedFaceSet * face, float x, float z,
          const char * scene)
{
  const SoPickedPoint * hit = pick.getPickedPoint();
  if (hit && hit->getPath()->getTail() == face && hit->getPoint() == SbVec3f(x,0,z)) {
    return true;
  }
  std::fprintf(stderr, "%s selected an unexpected face or point\n", scene);
  return false;
}

bool
test_stateful_sibling()
{
  const SbViewportRegion viewport(640,480);
  SoRefPtr<SoSeparator> root(new SoSeparator);
  auto * coords = new SoCoordinate3;
  root->addChild(coords);
  set_triangles(coords,20,-2);
  SoIndexedFaceSet * nearest = add_face(root.get(),0);
  for (int i = 1; i < 5; ++i) { add_face(root.get(),i); }
  SoIndexedFaceSet * last = add_face(root.get(),5);
  SoGetBoundingBoxAction bounds(viewport);
  bounds.apply(root.get());

  SoRayPickAction pick(viewport);
  pick.setRay(SbVec3f(20,0,0),SbVec3f(0,0,-1),0);
  TrackingFace::missing_coordinates = 0;
  pick.apply(root.get());
  if (!check_hit(pick,nearest,20,-2,"stateful sibling")) { return false; }
  if (TrackingFace::missing_coordinates != 0) {
    std::fprintf(stderr, "A bounding box was computed without sibling coordinates\n");
    return false;
  }

  SoRefPtr<SoPath> path(new SoPath(root.get()));
  path->append(6);
  pick.apply(path.get());
  return check_hit(pick,last,20,-7,"path through stateful sibling");
}

bool
test_inherited_state()
{
  const SbViewportRegion viewport(640,480);
  SoRefPtr<SoGroup> root(new SoGroup);
  auto * coords = new SoCoordinate3;
  root->addChild(coords);
  set_triangles(coords,20,-2);
  auto * container = new SoSeparator;
  root->addChild(container);
  auto * nearest = add_face(container,0);
  for (int i = 1; i < 6; ++i) {
    auto * layer = new SoSeparator;
    add_face(layer,i);
    container->addChild(layer);
  }
  SoGetBoundingBoxAction bounds(viewport);
  bounds.apply(root.get());

  SoRayPickAction pick(viewport);
  pick.setRay(SbVec3f(20,0,0),SbVec3f(0,0,-1),0);
  pick.apply(root.get());
  if (!check_hit(pick,nearest,20,-2,"child without eligible bounds")) { return false; }

  set_triangles(coords,40,-4);
  bounds.apply(root.get());
  pick.setRay(SbVec3f(40,0,0),SbVec3f(0,0,-1),0);
  pick.apply(root.get());
  return check_hit(pick,nearest,40,-4,"changed inherited coordinates");
}
}

int main()
{
  SoDB::init();
  TrackingFace::initClass();
  const bool passed = test_stateful_sibling() && test_inherited_state();
  SoDB::finish();
  return passed ? 0 : 1;
}
