#include <cstdio>
#include <cstdlib>

#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/SbViewportRegion.h>

static SoSeparator * buildCube(void) {
  SoSeparator * root = new SoSeparator;

  SoShapeHints * hints = new SoShapeHints;
  hints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
  hints->shapeType = SoShapeHints::SOLID;
  root->addChild(hints);

  static const float coords[8][3] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
  };
  SoCoordinate3 * coord = new SoCoordinate3;
  coord->point.setValues(0, 8, coords);
  root->addChild(coord);

  static const int32_t idx[] = {
    0,1,2,3,-1,  4,5,6,7,-1,
    0,1,5,4,-1,  1,2,6,5,-1,
    2,3,7,6,-1,  3,0,4,7,-1
  };
  SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
  ifs->coordIndex.setValues(0, sizeof(idx) / sizeof(idx[0]), idx);
  root->addChild(ifs);

  return root;
}

int main(int argc, char ** argv) {
  SoDB::init();

  SoSeparator * root = buildCube();
  root->ref();

  SbViewportRegion vp(64, 64);

  printf("Rendering with offscreen renderer #1 (context A)...\n");
  SoOffscreenRenderer renderer1(vp);
  for (int i = 0; i < 3; i++) {
    SbBool ok = renderer1.render(root);
    printf("  render #1 pass %d: %s\n", i, ok ? "OK" : "FAILED");
  }

  printf("Rendering same scene with offscreen renderer #2 (context B)...\n");
  SoOffscreenRenderer renderer2(vp);
  for (int i = 0; i < 3; i++) {
    SbBool ok = renderer2.render(root);
    printf("  render #2 pass %d: %s\n", i, ok ? "OK" : "FAILED");
  }

  printf("Rendering again with renderer #1 (repeat context A)...\n");
  SbBool ok3 = renderer1.render(root);
  printf("  render #1 (repeat): %s\n", ok3 ? "OK" : "FAILED");

  root->unref();
  printf("Done.\n");
  return 0;
}
