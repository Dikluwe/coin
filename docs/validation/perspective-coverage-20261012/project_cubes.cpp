#include <Inventor/SoDB.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <cmath>
#include <iomanip>
#include <iostream>

// Recreate frame five of CoinRenderCameraReuseReferenceTest without rendering.
// Each line is one projected cube corner in top-down pixel coordinates.
int main()
{
  SoDB::init();
  const int step = 5;
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->ref();
  camera->position.setValue(.6f * std::sin(step * .7f),
                            .4f * std::cos(step * .8f), 28 - step * .25f);
  camera->orientation.setValue(SbVec3f(0, 1, 0), .035f * step);
  camera->heightAngle = .78f + .01f * step;
  camera->nearDistance = 1 + .03f * step;
  camera->farDistance = 80;
  const SbMatrix projection = camera->getViewVolume(1.0f).getMatrix();
  SbMatrix parent;
  parent.setTransform(SbVec3f(.3f, -.2f, .4f),
                      SbRotation(SbVec3f(0, 1, 0), .15f),
                      SbVec3f(1, 1, 1));
  std::cout << std::setprecision(12);
  for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
    SbMatrix model;
    model.setTransform(SbVec3f(x - 7.5f, y - 7.5f, .15f * ((x + y) % 3)),
                       SbRotation(SbVec3f(1, 2, 3), .05f * (x % 4)),
                       SbVec3f(.32f, .37f, .45f));
    model.multRight(parent);
    for (int zsign : {-1, 1}) for (int ysign : {-1, 1}) for (int xsign : {-1, 1}) {
      SbVec3f world;
      model.multVecMatrix(SbVec3f(float(xsign), float(ysign), float(zsign)), world);
      SbVec4f clip;
      projection.multVecMatrix(SbVec4f(world[0], world[1], world[2], 1), clip);
      std::cout << x << ' ' << y << ' ' << xsign << ' ' << ysign << ' ' << zsign
                << ' ' << (double(clip[0]) / clip[3] + 1) * 128
                << ' ' << (1 - double(clip[1]) / clip[3]) * 128 << '\n';
    }
  }
  camera->unref();
}
