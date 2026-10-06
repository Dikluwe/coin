#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/nodes/SoTransform.h>
#include "actions/CoinRenderActionP.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
constexpr int side = 256;
bool check(bool value, const char * message) {
  if (!value) std::cerr << "CoinRenderCameraReuseReferenceTest: " << message << '\n';
  return value;
}
void disableOverlay(bool value) {
#ifdef _WIN32
  _putenv_s("COIN_RENDER_DISABLE_CAMERA_OVERLAY", value ? "1" : "");
#else
  if (value) setenv("COIN_RENDER_DISABLE_CAMERA_OVERLAY", "1", 1);
  else unsetenv("COIN_RENDER_DISABLE_CAMERA_OVERLAY");
#endif
}
// RGB only: native readback is top-down, legacy Coin/GL is bottom-up.
bool compare(const std::vector<uint8_t> & actual, const std::vector<uint8_t> & reference,
             unsigned channels, bool flip, const char * name, int frame) {
  if (!check(actual.size() == side * side * 4u && reference.size() == side * side * channels,
             "missing RGB image")) return false;
  uint64_t sum = 0, over3 = 0;
  int maximum = 0;
  for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
    bool pixelOver3 = false;
    for (int c = 0; c < 3; ++c) {
      const size_t a = (size_t(y) * side + x) * 4 + c;
      const size_t b = (size_t(flip ? side - y - 1 : y) * side + x) * channels + c;
      const int difference = std::abs(int(actual[a]) - int(reference[b]));
      sum += difference;
      maximum = std::max(maximum, difference);
      pixelOver3 |= difference > 3;
    }
    over3 += pixelOver3;
  }
  const double mae = double(sum) / (side * side * 3);
  std::cout << name << " frame=" << frame << " rgb_mae=" << mae
            << " max=" << maximum << " pixels_over3=" << over3 << '\n';
  // Floating-point rasterization can differ at isolated silhouette edges.
  return check(mae <= 0.10 && over3 <= side * side / 500u,
               "camera reuse changed RGB beyond the fixed reference gate");
}

bool run(bool perspective, bool requireGl) {
  auto * root = new SoSeparator;
  root->ref();
  auto * camera = perspective ? static_cast<SoCamera *>(new SoPerspectiveCamera)
                             : static_cast<SoCamera *>(new SoOrthographicCamera);
  camera->position.setValue(0, 0, 28);
  camera->nearDistance = 1;
  camera->farDistance = 80;
  if (!perspective) static_cast<SoOrthographicCamera *>(camera)->height = 20;
  root->addChild(camera);
  auto * lightTransform = new SoTransform;
  lightTransform->translation.setValue(.3f, -.2f, .4f);
  lightTransform->rotation.setValue(SbVec3f(0,1,0), .15f);
  root->addChild(lightTransform);
  auto * directional = new SoDirectionalLight;
  directional->direction.setValue(.2f, -.3f, -1);
  directional->intensity = .55f;
  root->addChild(directional);
  auto * point = new SoPointLight;
  point->location.setValue(-4, 3, 10);
  point->intensity = .20f;
  root->addChild(point);
  auto * spot = new SoSpotLight;
  spot->location.setValue(3, -2, 8);
  spot->direction.setValue(-.2f, .1f, -1);
  spot->cutOffAngle = .9f;
  spot->dropOffRate = .03f;
  spot->intensity = .20f;
  root->addChild(spot);
  auto * material = new SoMaterial;
  material->diffuseColor.setValue(.55f, .45f, .3f);
  material->specularColor.setValue(.25f, .2f, .15f);
  material->shininess = .18f;
  root->addChild(material);
  auto * cube = new SoCube;
  SoTransform * firstObject = nullptr;
  for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
    auto * group = new SoSeparator;
    auto * transform = new SoTransform;
    transform->translation.setValue(x - 7.5f, y - 7.5f, .15f * ((x+y)%3));
    transform->scaleFactor.setValue(.32f, .37f, .45f);
    transform->rotation.setValue(SbVec3f(1, 2, 3), .05f * (x%4));
    if (!firstObject) firstObject = transform;
    group->addChild(transform); group->addChild(cube); root->addChild(group);
  }
  bool ok = true;
  {
    std::unique_ptr<CoinRenderTarget> optimized(CoinRenderTarget::createOffscreen(SbVec2i32(side, side)));
    std::unique_ptr<CoinRenderTarget> rebuilt(CoinRenderTarget::createOffscreen(SbVec2i32(side, side)));
    if (!optimized || !rebuilt) { root->unref(); return false; }
    optimized->setDepthReadbackEnabled(FALSE); rebuilt->setDepthReadbackEnabled(FALSE);
    CoinRenderAction fast(SbViewportRegion(side, side)), full(SbViewportRegion(side, side));
    fast.setRenderTarget(optimized.get()); full.setRenderTarget(rebuilt.get());
    SoOffscreenRenderer gl(SbViewportRegion(side, side));
    gl.setComponents(SoOffscreenRenderer::RGB);
    std::vector<uint8_t> anchorPixels;
    const void * anchorVertices = nullptr;
    for (int frame = 0; frame < 9 && ok; ++frame) {
      const int step = frame == 8 ? 0 : frame;
      camera->position.setValue(.6f * std::sin(step*.7f), .4f * std::cos(step*.8f), 28 - step*.25f);
      camera->orientation.setValue(SbVec3f(0,1,0), .035f * step);
      if (perspective) static_cast<SoPerspectiveCamera *>(camera)->heightAngle = .78f + .01f * step;
      else static_cast<SoOrthographicCamera *>(camera)->height = 20 + .2f * step;
      camera->nearDistance = 1 + .03f * step;
      disableOverlay(false); fast.apply(root);
      ok &= check(fast.getLastStatus() == CoinRenderAction::SUCCESS, fast.getLastError().getString());
      if (!ok) break;
      const void * vertices = fast.getPimpl()->lastValidPlan.vertices.data();
      if (frame == 0) {
        anchorVertices = vertices;
        ok &= check(fast.getPimpl()->cachedCamera == camera, "scene did not qualify for camera overlay");
      } else {
        ok &= check(vertices == anchorVertices && !fast.getPimpl()->cameraOnlyDirty,
                    "camera update recaptured geometry instead of reusing the anchor");
      }
      disableOverlay(true); full.apply(root); disableOverlay(false);
      ok &= check(full.getLastStatus() == CoinRenderAction::SUCCESS, full.getLastError().getString());
      std::vector<uint8_t> fastPixels, fullPixels;
      optimized->readbackRGBA(fastPixels); rebuilt->readbackRGBA(fullPixels);
      ok &= compare(fastPixels, fullPixels, 4, false, perspective ? "perspective/rebuild" : "ortho/rebuild", frame);
      if (frame == 0) anchorPixels = fastPixels;
      else if (frame == 8) ok &= check(fastPixels == anchorPixels, "return to the camera anchor drifted");
      else ok &= check(fastPixels != anchorPixels, "camera image did not move");
      if (requireGl) {
        ok &= check(gl.render(root) && gl.getBuffer(), "required Coin/GL reference unavailable");
        if (ok) {
          const uint8_t * bytes = gl.getBuffer();
          std::vector<uint8_t> pixels(bytes, bytes + side * side * 3);
          ok &= compare(fastPixels, pixels, 3, true, perspective ? "perspective/CoinGL" : "ortho/CoinGL", frame);
        }
      }
    }
    // A non-camera mutation must invalidate the overlay, including after several
    // successful camera-only frames. Compare against a full capture again.
    if (ok) {
      firstObject->translation.setValue(-7, -7, 1);
      material->diffuseColor.setValue(.2f, .7f, .4f);
      point->location.setValue(-2, 1, 5);
      camera->position.setValue(.2f, -.1f, 26);
      ok &= check(fast.getPimpl()->cameraPatchInvalidated, "scene mutation failed to invalidate the camera overlay");
      fast.apply(root);
      disableOverlay(true); full.apply(root); disableOverlay(false);
      ok &= check(fast.getLastStatus() == CoinRenderAction::SUCCESS && full.getLastStatus() == CoinRenderAction::SUCCESS,
                  "mutation fallback failed");
      std::vector<uint8_t> a, b;
      optimized->readbackRGBA(a); rebuilt->readbackRGBA(b);
      ok &= compare(a, b, 4, false, "scene-mutation/rebuild", 9);
    }
  }
  root->unref();
  return ok;
}
}
int main() {
  if (!std::getenv("COIN_RENDER_REQUIRE_CAMERA_REFERENCE")) return 77;
  SoDB::init(); CoinRenderAction::initClass();
  const bool requireGl = std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") != nullptr;
  return run(true, requireGl) && run(false, requireGl) ? 0 : 1;
}
