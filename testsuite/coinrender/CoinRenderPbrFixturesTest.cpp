#include "CoinRenderPbrFixtures.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <cstdio>
#include <cmath>

struct GeometryCheck { int triangles = 0; bool valid = true; float minDot = 1.0f; };
static void triangle(void * data, SoCallbackAction *, const SoPrimitiveVertex * a,
                     const SoPrimitiveVertex * b, const SoPrimitiveVertex * c)
{
  GeometryCheck & check = *static_cast<GeometryCheck *>(data);
  ++check.triangles;
  SbVec3f cross = (b->getPoint() - a->getPoint()).cross(c->getPoint() - a->getPoint());
  if (cross.normalize() == 0.0f) check.valid = false;
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(a->getPoint()[i]) || !std::isfinite(a->getNormal()[i])) check.valid = false;
  }
  const float dot = cross.dot(a->getNormal());
  if (dot < check.minDot) check.minDot = dot;
  if (dot < 0.98f) check.valid = false;
}

int main()
{
  SoDB::init();
  SoSeparator * flange = coin_pbr_fixture::create("flange", FALSE);
  flange->ref();
  GeometryCheck check;
  SoCallbackAction capture;
  capture.addTriangleCallback(SoShape::getClassTypeId(), triangle, &check);
  capture.apply(flange);
  if (!check.valid || check.triangles < 5000) {
    std::fprintf(stderr, "Invalid fixture triangles=%d min_normal_dot=%g\n", check.triangles, check.minDot); return 1;
  }
  const SbViewportRegion viewport(64, 64);
  SoRayPickAction hole(viewport);
  hole.setRay(SbVec3f(0, 0, 4), SbVec3f(0, 0, -1)); hole.apply(flange);
  if (hole.getPickedPoint()) { std::fprintf(stderr, "The flange bore must be empty\n"); return 1; }
  SoRayPickAction cap(viewport);
  cap.setRay(SbVec3f(1.4f, 0, 4), SbVec3f(0, 0, -1)); cap.apply(flange);
  if (!cap.getPickedPoint() || std::fabs(cap.getPickedPoint()->getPoint()[2] - 0.175f) > 0.0001f ||
      cap.getPickedPoint()->getNormal()[2] < 0.999f) {
    std::fprintf(stderr, "Invalid flange cap hit=%p\n", static_cast<void *>(cap.getPickedPoint())); return 1;
  }
  const char * presets[] = {"iso-perspective", "iso-orthographic", "front", "top", "right", "detail"};
  for (const char * preset : presets) {
    SbVec3f center;
    SoCamera * camera = coin_pbr_fixture::camera(flange, preset, SbVec2i32(1280, 960), center);
    // Coin's orthographic viewAll deliberately permits a negative near plane.
    // Positive near distance is required only by the perspective projection.
    if (!camera || center.length() > 0.75f ||
        (camera->isOfType(SoPerspectiveCamera::getClassTypeId()) && camera->nearDistance.getValue() <= 0) ||
        camera->farDistance.getValue() <= camera->nearDistance.getValue()) {
      std::fprintf(stderr, "Invalid camera %s center=%g near=%g far=%g\n", preset,
        center.length(), camera ? camera->nearDistance.getValue() : 0,
        camera ? camera->farDistance.getValue() : 0); return 1;
    }
    camera->ref();
    if ((camera->isOfType(SoPerspectiveCamera::getClassTypeId()) != FALSE) !=
        (SbString(preset) == "iso-perspective")) return 1;
    SbVec3f direction;
    camera->orientation.getValue().multVec(SbVec3f(0, 0, -1), direction);
    SbVec3f expected = center - camera->position.getValue(); expected.normalize();
    if (direction.dot(expected) < 0.9999f) {
      std::fprintf(stderr, "Camera %s direction dot=%g\n", preset, direction.dot(expected)); return 1;
    }
    camera->unref();
  }
  flange->unref();
  const char * fixtures[] = {"assembly", "housing", "grid"};
  for (const char * name : fixtures) {
    SoSeparator * scene = coin_pbr_fixture::create(name, FALSE); scene->ref();
    SoGetBoundingBoxAction bounds(viewport); bounds.apply(scene);
    if (bounds.getBoundingBox().isEmpty()) return 1;
    scene->unref();
  }
  std::printf("Coin Sb CAD fixtures: geometry, bore, normals, picking and six cameras PASS\n");
  return 0;
}
