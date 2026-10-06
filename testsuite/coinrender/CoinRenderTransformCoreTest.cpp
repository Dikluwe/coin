#include "rendering/coinrender/CoinRenderTransformCore.h"
#include <Inventor/SbRotation.h>
#include <iostream>
#include <limits>
#define CHECK(c) do { if (!(c)) { std::cerr << "Failed: " << #c << " at " << __LINE__ << '\n'; return 1; } } while (0)
int main() {
  const auto identity = SbMatrix::identity();
  CHECK(CoinRenderTransformCore::projection(identity, true) == identity);
  const auto zeroToOne = CoinRenderTransformCore::projection(identity, false);
  for (int z = -1; z <= 1; ++z) {
    SbVec3f out;
    zeroToOne.multVecMatrix(SbVec3f(0,0,float(z)), out);
    CHECK(out[2] == (z + 1) * 0.5f);
  }
  SbMatrix scale; scale.setScale(SbVec3f(2,4,8));
  const auto normal = CoinRenderTransformCore::normalMatrix(scale);
  CHECK(normal[0][0] == 0.5f && normal[1][1] == 0.25f && normal[2][2] == 0.125f);
  scale.setScale(SbVec3f(0,1,1));
  CHECK(CoinRenderTransformCore::normalMatrix(scale) == identity);
  CHECK(CoinRenderTransformCore::finiteMatrix(identity));
  SbMatrix anchor; anchor.setRotate(SbRotation(SbVec3f(1,2,3), .45f));
  anchor[3][0] = -7; anchor[3][1] = 3; anchor[3][2] = -20;
  SbMatrix translated = anchor;
  translated[3][0] += 2; translated[3][1] -= 1; translated[3][2] += 3;
  SbMatrix delta, normalDelta;
  CHECK(CoinRenderTransformCore::cameraDelta(anchor, translated, delta, normalDelta));
  SbMatrix expectedTranslation; expectedTranslation.setTranslate(SbVec3f(2,-1,3));
  CHECK(delta == expectedTranslation);
  CHECK(normalDelta[0][0] == 1 && normalDelta[1][1] == 1 && normalDelta[2][2] == 1);
  SbMatrix rotated; rotated.setRotate(SbRotation(SbVec3f(-2,1,3), -.3f));
  rotated[3][0] = 4; rotated[3][1] = -5; rotated[3][2] = -12;
  CHECK(CoinRenderTransformCore::cameraDelta(anchor, rotated, delta, normalDelta));
  for (const auto & point : {SbVec3f(0,0,0), SbVec3f(4,-3,2), SbVec3f(-10,12,-8)}) {
    SbVec3f baked, actual, expected;
    anchor.multVecMatrix(point, baked); delta.multVecMatrix(baked, actual);
    rotated.multVecMatrix(point, expected);
    CHECK((actual - expected).length() < 1e-5f);
  }
  for (const auto & invalid : {scale, SbMatrix(-1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1)}) {
    const SbMatrix savedDelta = delta, savedNormal = normalDelta;
    CHECK(!CoinRenderTransformCore::cameraDelta(anchor, invalid, delta, normalDelta));
    CHECK(delta == savedDelta && normalDelta == savedNormal);
  }
  SbMatrix distant = identity; distant[3][2] = -1.0e8f;
  CHECK(CoinRenderTransformCore::rigidViewMatrix(distant));
  CHECK(!CoinRenderTransformCore::cameraReuseView(distant));
  CHECK(!CoinRenderTransformCore::cameraDelta(distant, identity, delta, normalDelta));
  for (float value : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    auto matrix = identity; matrix[0][2] = value;
    CHECK(!CoinRenderTransformCore::finiteMatrix(matrix));
  }
  CoinRenderViewportSnapshot vp;
  vp.x = -20; vp.y = 10; vp.width = 80; vp.height = 40;
  const auto transform = CoinRenderTransformCore::viewportTransform(vp,100,100);
  SbVec3f start, end;
  transform.multVecMatrix(SbVec3f(-1,-1,0), start);
  transform.multVecMatrix(SbVec3f(1,1,0), end);
  CHECK(std::abs(start[0] + 1.4f) < 1e-6f && std::abs(start[1] + 0.8f) < 1e-6f);
  CHECK(std::abs(end[0] - 0.2f) < 1e-6f && std::abs(end[1]) < 1e-6f);
  int32_t clipped[4] = {7,7,7,7};
  const int32_t viewport[4] = {-20,10,80,40};
  CHECK(CoinRenderTransformCore::clipViewport(viewport,100,100,clipped));
  CHECK(clipped[0]==0 && clipped[1]==10 && clipped[2]==60 && clipped[3]==40);
  const int32_t absent[4] = {100,100,50,50};
  CHECK(!CoinRenderTransformCore::clipViewport(absent,100,100,clipped));
  CHECK(clipped[0]==0 && clipped[1]==10 && clipped[2]==60 && clipped[3]==40);
  const int32_t huge[4] = {std::numeric_limits<int32_t>::max(),0,std::numeric_limits<int32_t>::max(),10};
  CHECK(!CoinRenderTransformCore::clipViewport(huge,100,100,clipped));
  CHECK(vp.x == -20 && vp.width == 80); // Projection was not clipped.
  std::cout << "Shared depth, normal, viewport and scissor transforms passed\n";
  return 0;
}
