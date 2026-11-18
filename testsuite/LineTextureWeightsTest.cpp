// Public picking regression: texture coordinates follow the point on the line.
// Normal interpolation and material selection deliberately retain their legacy
// distance-to-same-endpoint convention; this test does not redefine them.
#include <Inventor/SoDB.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoGroup.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char * label) {
  ++checks;
  if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", label); }
}
void near(float got, float want, const char * label) {
  ++checks;
  if (!std::isfinite(got) || std::fabs(got-want) > 2e-6f) {
    ++failures;
    std::fprintf(stderr, "FAIL %s: got %.9g, want %.9g\n", label, got, want);
  }
}
class Primitive : public SoCube {
public:
  SoPrimitiveVertex vertices[3];
  bool triangle = false;
  Primitive() {
    vertices[0].setPoint(0,0,0);
    vertices[1].setPoint(1,0,0);
    vertices[2].setPoint(0,1,0);
    vertices[0].setTextureCoords(-3,2,6,2);
    vertices[1].setTextureCoords(13,-6,-2,10);
    vertices[2].setTextureCoords(5,10,14,18);
    vertices[0].setNormal(1,0,0);
    vertices[1].setNormal(0,1,0);
    vertices[2].setNormal(0,0,1);
    vertices[0].setMaterialIndex(17);
    vertices[1].setMaterialIndex(29);
    vertices[2].setMaterialIndex(43);
  }
  void rayPick(SoRayPickAction * action) override { SoShape::rayPick(action); }
  void computeBBox(SoAction *, SbBox3f & box, SbVec3f & center) override {
    box.makeEmpty();
    for (int i=0; i<(triangle?3:2); ++i) box.extendBy(vertices[i].getPoint());
    center = box.getCenter();
  }
protected:
  void generatePrimitives(SoAction * action) override {
    if (triangle) invokeTriangleCallbacks(action, &vertices[0], &vertices[1], &vertices[2]);
    else invokeLineSegmentCallbacks(action, &vertices[0], &vertices[1]);
  }
};
void inspect(const SoPickedPoint & hit, const SbVec4f & texture,
             const SbVec3f & normal, int material, const char * label) {
  const SbVec4f got = hit.getTextureCoords();
  for (int i=0; i<4; ++i) near(got[i], texture[i], label);
  // Q is inspected directly: homogeneous division would erase this contract.
  const SbVec3f n = hit.getNormal();
  for (int i=0; i<3; ++i) near(n[i], normal[i], "legacy normal interpolation");
  check(hit.getMaterialIndex()==material, "legacy material selection");
}
void lineCases() {
  const float positions[] = {0, .125f, .25f, .5f, .75f, .875f, 1};
  for (int reversed=0; reversed<2; ++reversed) {
    SoGroup * root = new SoGroup; root->ref();
    Primitive * shape = new Primitive;
    if (reversed) {
      const SoPrimitiveVertex first = shape->vertices[0];
      shape->vertices[0] = shape->vertices[1]; shape->vertices[1] = first;
    }
    root->addChild(shape);
    // Reusing this shape for several picks also checks for stale hit data.
    for (unsigned sample=0; sample<sizeof(positions)/sizeof(positions[0]); ++sample) {
      const float x = positions[sample];
      SoRayPickAction action(SbViewportRegion(64,64));
      action.enableCulling(FALSE);
      action.setRay(SbVec3f(x,0,1), SbVec3f(0,0,-1));
      action.apply(root);
      const SoPickedPoint * hit = action.getPickedPoint();
      check(hit!=NULL, "line hit exists at endpoint/interior");
      if (hit) {
        // Explicit affine values for the fixed endpoint data above. These
        // formulas do not use the implementation's distances or weights.
        const SbVec4f texture(-3+16*x, 2-8*x, 6-8*x, 2+8*x);
        const float normalLength = std::sqrt(x*x+(1-x)*(1-x));
        const SbVec3f normal(x/normalLength, (1-x)/normalLength, 0);
        // The historical tie chooses the first supplied endpoint.
        const int material = x==.5f ? (reversed?29:17) : x>.5f?17:29;
        inspect(*hit,texture,normal,material,"texture follows position along line");
        const SbVec3f point = hit->getPoint();
        near(point[0],x,"picked point X"); near(point[1],0,"picked point Y");
        near(point[2],0,"picked point Z");
      }
    }
    root->unref();
  }
}
void controls() {
  SoGroup * root = new SoGroup; root->ref();
  Primitive * shape = new Primitive; shape->triangle=true; root->addChild(shape);
  SoRayPickAction action(SbViewportRegion(64,64)); action.enableCulling(FALSE);
  action.setRay(SbVec3f(.25f,.25f,1), SbVec3f(0,0,-1)); action.apply(root);
  const SoPickedPoint * hit = action.getPickedPoint();
  check(hit!=NULL,"triangle control hit exists");
  if (hit) {
    const float length=std::sqrt(.5f*.5f+.25f*.25f+.25f*.25f);
    inspect(*hit,SbVec4f(3,2,6,8),SbVec3f(.5f/length,.25f/length,.25f/length),17,
            "triangle interpolation remains unchanged");
  }
  root->unref();
}
void degenerate() {
  SoGroup * root = new SoGroup; root->ref();
  Primitive * shape = new Primitive;
  shape->vertices[1].setPoint(0,0,0); root->addChild(shape);
  SoRayPickAction action(SbViewportRegion(64,64)); action.enableCulling(FALSE);
  action.setRay(SbVec3f(0,0,1),SbVec3f(0,0,-1)); action.apply(root);
  const SoPickedPoint * hit=action.getPickedPoint();
  if (hit) {
    inspect(*hit,SbVec4f(-3,2,6,2),SbVec3f(1,0,0),17,
            "degenerate line retains first endpoint values");
  }
  else std::puts("Degenerate line has no picked primitive; no texture result is observable.");
  root->unref();
}
}
int main(int argc, char ** argv) {
  const char * mode=argc>1?argv[1]:"lines";
  if (std::strcmp(mode,"lines") && std::strcmp(mode,"controls") &&
      std::strcmp(mode,"degenerate")) return 2;
  SoDB::init();
  if (!std::strcmp(mode,"lines")) lineCases();
  else if (!std::strcmp(mode,"controls")) controls();
  else degenerate();
  SoDB::finish();
  std::printf("%s: %d checks, %d failures\n",mode,checks,failures);
  return failures?1:0;
}
