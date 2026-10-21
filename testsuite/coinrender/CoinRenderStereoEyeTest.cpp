#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCube.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {
struct Coverage { size_t count = 0; double centroid = 0.0; };
Coverage measure(const unsigned char * pixels, size_t channels)
{
  Coverage result;
  double xSum = 0.0;
  for (size_t pixel = 0; pixel < 96u * 96u; ++pixel) {
    const unsigned char * color = pixels + pixel * channels;
    if (color[0] > 120 && color[1] < 40 && color[2] < 40) {
      ++result.count;
      xSum += double(pixel % 96);
    }
  }
  if (result.count) result.centroid = xSum / double(result.count);
  return result;
}
}

int main()
{
  SoDB::init();
  CoinRenderAction::initClass();
  std::string diagnostic;
  const CoinRenderOptions options = CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if (!diagnostic.empty()) { std::fprintf(stderr, "%s\n", diagnostic.c_str()); return 2; }
  CoinRenderCapabilities caps{};
  if (coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      options.renderer, &caps, sizeof(caps)) || !caps.gpu_available) return 77;
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(96, 96), options));
  if (!target || target->getStatus() != CoinRenderTarget::TARGET_READY) return 1;
  SoSeparator * scene = new SoSeparator;
  scene->ref();
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0, 0, 6);
  camera->nearDistance = 1;
  camera->farDistance = 20;
  camera->focalDistance = 6;
  camera->setStereoAdjustment(2.0f);
  scene->addChild(camera);
  SoLightModel * lighting = new SoLightModel;
  lighting->model = SoLightModel::BASE_COLOR;
  scene->addChild(lighting);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(1, 0, 0);
  scene->addChild(material);
  SoTranslation * translation = new SoTranslation;
  translation->translation.setValue(0, 0, 2);
  scene->addChild(translation);
  SoCube * cube = new SoCube;
  cube->width = 1.2f; cube->height = 1.2f; cube->depth = 1.2f;
  scene->addChild(cube);
  CoinRenderAction action(SbViewportRegion(96, 96));
  action.setRenderTarget(target.get());
  if (std::getenv("COIN_RENDER_STEREO_PROBE_DISABLE_FAST_PATH"))
    action.setFastPathEnabled(FALSE);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  auto render = [&](SoCamera::StereoMode mode, std::vector<uint8_t> & pixels) {
    camera->setStereoMode(mode);
    action.apply(scene);
    if (action.getLastStatus() != CoinRenderAction::SUCCESS) {
      std::fprintf(stderr, "stereo eye render failed: %s\n", action.getLastError().getString());
      return false;
    }
    target->readbackRGBA(pixels);
    return pixels.size() == 96u * 96u * 4u;
  };
  std::vector<uint8_t> mono, left, right, leftAgain, widerLeft, balancedLeft,
    restoredLeft, restoredMono;
  bool ok = render(SoCamera::MONOSCOPIC, mono) &&
    render(SoCamera::LEFT_VIEW, left) && render(SoCamera::RIGHT_VIEW, right) &&
    render(SoCamera::LEFT_VIEW, leftAgain);
  camera->setStereoAdjustment(3.0f);
  ok = ok && render(SoCamera::LEFT_VIEW, widerLeft);
  camera->setStereoAdjustment(2.0f);
  camera->setBalanceAdjustment(0.75f);
  ok = ok && render(SoCamera::LEFT_VIEW, balancedLeft);
  camera->setBalanceAdjustment(1.0f);
  ok = ok && render(SoCamera::LEFT_VIEW, restoredLeft);
  ok = ok && render(SoCamera::MONOSCOPIC, restoredMono);
  if (!ok) { action.setRenderTarget(nullptr); scene->unref(); return 1; }
  const Coverage monoCoverage = measure(mono.data(), 4);
  const Coverage leftCoverage = measure(left.data(), 4);
  const Coverage rightCoverage = measure(right.data(), 4);
  const Coverage widerCoverage = measure(widerLeft.data(), 4);
  ok = monoCoverage.count > 100 && leftCoverage.count > 100 &&
    rightCoverage.count > 100 && left != mono && right != mono && left != right &&
    leftAgain == left && restoredLeft == left && restoredMono == mono &&
    widerLeft != left && balancedLeft != left &&
    std::abs(widerCoverage.centroid - monoCoverage.centroid) >
      std::abs(leftCoverage.centroid - monoCoverage.centroid);
  std::printf("stereo_eye_capture mono=%.3f left=%.3f right=%.3f wider_left=%.3f counts=%zu,%zu,%zu cache_restore=%d result=%s\n",
    monoCoverage.centroid, leftCoverage.centroid, rightCoverage.centroid,
    widerCoverage.centroid, monoCoverage.count, leftCoverage.count,
    rightCoverage.count,
    int(leftAgain == left && restoredLeft == left && restoredMono == mono),
    ok ? "PASS" : "FAIL");
  if (ok && std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
    SoOffscreenRenderer reference(SbViewportRegion(96, 96));
    reference.setComponents(SoOffscreenRenderer::RGB);
    reference.setBackgroundColor(SbColor(0, 0, 0));
    auto glEye = [&](SoCamera::StereoMode mode, Coverage & coverage) {
      camera->setStereoMode(mode);
      if (!reference.render(scene)) return false;
      const unsigned char * image = reference.getBuffer();
      if (!image) return false;
      coverage = measure(image, 3);
      return coverage.count > 100;
    };
    Coverage glLeft, glRight;
    ok = glEye(SoCamera::LEFT_VIEW, glLeft) && glEye(SoCamera::RIGHT_VIEW, glRight) &&
      std::abs(leftCoverage.centroid - glLeft.centroid) <= 1.5 &&
      std::abs(rightCoverage.centroid - glRight.centroid) <= 1.5 &&
      std::abs((leftCoverage.centroid - rightCoverage.centroid) -
               (glLeft.centroid - glRight.centroid)) <= 1.0;
    std::printf("stereo_eye_CoinGL left=%.3f right=%.3f counts=%zu,%zu result=%s\n",
      glLeft.centroid, glRight.centroid, glLeft.count, glRight.count,
      ok ? "PASS" : "FAIL");
  }
  material->diffuseColor.setValue(1, 1, 1);
  std::vector<uint8_t> whiteLeft, whiteRight, anaglyph;
  if (ok) ok = render(SoCamera::LEFT_VIEW, whiteLeft) &&
               render(SoCamera::RIGHT_VIEW, whiteRight);
  if (ok) {
    camera->setStereoMode(SoCamera::MONOSCOPIC);
    SoRenderManager manager;
    manager.setSceneGraph(scene);
    manager.setCamera(camera);
    manager.setViewportRegion(SbViewportRegion(96, 96));
    manager.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    manager.setStereoMode(SoRenderManager::ANAGLYPH);
    manager.setStereoOffset(2.0f);
    CoinRenderManagerAdapter adapter(manager, SbVec2i32(96, 96), options);
    ok = adapter.render() == CoinRenderAction::SUCCESS;
    if (ok) adapter.getSceneManager()->getRenderTarget()->readbackRGBA(anaglyph);
    ok = ok && anaglyph.size() == whiteLeft.size();
    if (!ok) std::fprintf(stderr, "manager ANAGLYPH failed: %s\n", adapter.getLastError().getString());
    size_t leftOnly = 0, rightOnly = 0;
    if (ok) for (size_t i = 0; i < anaglyph.size(); i += 4) {
      ok = ok && std::abs(int(anaglyph[i]) - int(whiteLeft[i])) <= 1 &&
           std::abs(int(anaglyph[i + 1]) - int(whiteRight[i + 1])) <= 1 &&
           std::abs(int(anaglyph[i + 2]) - int(whiteRight[i + 2])) <= 1;
      leftOnly += whiteLeft[i] > 120 && whiteRight[i] < 40;
      rightOnly += whiteRight[i] > 120 && whiteLeft[i] < 40;
    }
    ok = ok && leftOnly > 50 && rightOnly > 50 &&
         camera->getStereoMode() == SoCamera::MONOSCOPIC;
    if (ok) {
      material->transparency = 0.5f;
      manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::DELAYED_BLEND);
      std::vector<uint8_t> retained;
      const bool rejected = adapter.render() == CoinRenderAction::UNSUPPORTED;
      adapter.getSceneManager()->getRenderTarget()->readbackRGBA(retained);
      material->transparency = 0.0f;
      manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
      const bool recovered = adapter.render() == CoinRenderAction::SUCCESS;
      std::vector<uint8_t> restored;
      if (recovered) adapter.getSceneManager()->getRenderTarget()->readbackRGBA(restored);
      ok = rejected && retained == anaglyph && recovered && restored == anaglyph;
      std::printf("manager_anaglyph_delayed_blend rollback=%s recovery=%s\n",
                  rejected && retained == anaglyph ? "PASS" : "FAIL",
                  recovered && restored == anaglyph ? "PASS" : "FAIL");
    }
    std::printf("manager_anaglyph channels=%s left_only=%zu right_only=%zu\n",
                ok ? "PASS" : "FAIL", leftOnly, rightOnly);
    if (ok) {
      material->transparency = 0.5f;
      action.setTransparencyType(CoinRenderAction::SCREEN_DOOR);
      manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
      std::vector<uint8_t> doorLeft, doorRight, doorAnaglyph;
      ok = render(SoCamera::LEFT_VIEW, doorLeft) &&
           render(SoCamera::RIGHT_VIEW, doorRight);
      camera->setStereoMode(SoCamera::MONOSCOPIC);
      if (ok) ok = adapter.render() == CoinRenderAction::SUCCESS;
      if (ok) adapter.getSceneManager()->getRenderTarget()->readbackRGBA(doorAnaglyph);
      ok = ok && doorAnaglyph.size() == doorLeft.size();
      size_t covered = 0;
      if (ok) for (size_t i = 0; i < doorAnaglyph.size(); i += 4) {
        ok = ok && std::abs(int(doorAnaglyph[i]) - int(doorLeft[i])) <= 1 &&
             std::abs(int(doorAnaglyph[i + 1]) - int(doorRight[i + 1])) <= 1 &&
             std::abs(int(doorAnaglyph[i + 2]) - int(doorRight[i + 2])) <= 1;
        covered += doorAnaglyph[i] > 120 || doorAnaglyph[i + 1] > 120;
      }
      ok = ok && covered > 100 && covered < leftCoverage.count + rightCoverage.count &&
           doorAnaglyph != anaglyph;
      unsigned glMaxError = 0;
      if (ok && std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
        SoOffscreenRenderer reference(SbViewportRegion(96, 96));
        reference.setComponents(SoOffscreenRenderer::RGB);
        reference.setBackgroundColor(SbColor(0, 0, 0));
        reference.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
        for (int eye = 0; eye < 2 && ok; ++eye) {
          camera->setStereoMode(eye == 0 ? SoCamera::LEFT_VIEW : SoCamera::RIGHT_VIEW);
          ok = reference.render(scene);
          const unsigned char * gl = ok ? reference.getBuffer() : nullptr;
          ok = ok && gl;
          if (ok) for (size_t pixel = 0; pixel < 96u * 96u; ++pixel)
            for (size_t c = 0; c < 3; ++c) {
              const auto & direct = eye == 0 ? doorLeft : doorRight;
              const size_t glPixel = (95u - pixel / 96u) * 96u + pixel % 96u;
              glMaxError = std::max(glMaxError,
                unsigned(std::abs(int(direct[pixel * 4 + c]) - int(gl[glPixel * 3 + c]))));
            }
        }
        camera->setStereoMode(SoCamera::MONOSCOPIC);
        ok = ok && glMaxError <= 1;
      }
      std::printf("manager_anaglyph_screen_door covered=%zu channels=%s\n",
                  covered, ok ? "PASS" : "FAIL");
      if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE"))
        std::printf("manager_anaglyph_screen_door_CoinGL max_rgb=%u result=%s\n",
                    glMaxError, ok ? "PASS" : "FAIL");
      material->transparency = 0.0f;
      action.setTransparencyType(CoinRenderAction::BLEND);
      manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
    }
    if (ok) {
      material->transparency = 0.5f;
      std::vector<uint8_t> blendLeft, blendRight, blendAnaglyph;
      ok = render(SoCamera::LEFT_VIEW, blendLeft) &&
           render(SoCamera::RIGHT_VIEW, blendRight);
      camera->setStereoMode(SoCamera::MONOSCOPIC);
      if (ok) ok = adapter.render() == CoinRenderAction::SUCCESS;
      if (ok) adapter.getSceneManager()->getRenderTarget()->readbackRGBA(blendAnaglyph);
      ok = ok && blendAnaglyph.size() == blendLeft.size();
      unsigned channelError = 0, glMaxError = 0;
      if (ok) for (size_t i = 0; i < blendAnaglyph.size(); i += 4) {
        channelError = std::max(channelError,
          unsigned(std::abs(int(blendAnaglyph[i]) - int(blendLeft[i]))));
        channelError = std::max(channelError,
          unsigned(std::abs(int(blendAnaglyph[i + 1]) - int(blendRight[i + 1]))));
        channelError = std::max(channelError,
          unsigned(std::abs(int(blendAnaglyph[i + 2]) - int(blendRight[i + 2]))));
      }
      if (ok && std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
        SoOffscreenRenderer reference(SbViewportRegion(96, 96));
        reference.setComponents(SoOffscreenRenderer::RGB);
        reference.setBackgroundColor(SbColor(0, 0, 0));
        reference.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
        for (int eye = 0; eye < 2 && ok; ++eye) {
          camera->setStereoMode(eye == 0 ? SoCamera::LEFT_VIEW : SoCamera::RIGHT_VIEW);
          ok = reference.render(scene);
          const unsigned char * gl = ok ? reference.getBuffer() : nullptr;
          ok = ok && gl;
          if (ok) for (size_t pixel = 0; pixel < 96u * 96u; ++pixel)
            for (size_t c = 0; c < 3; ++c) {
              const auto & direct = eye == 0 ? blendLeft : blendRight;
              const size_t glPixel = (95u - pixel / 96u) * 96u + pixel % 96u;
              glMaxError = std::max(glMaxError,
                unsigned(std::abs(int(direct[pixel * 4 + c]) - int(gl[glPixel * 3 + c]))));
            }
        }
        camera->setStereoMode(SoCamera::MONOSCOPIC);
      }
      ok = ok && channelError <= 1 && glMaxError <= 1 && blendAnaglyph != anaglyph;
      std::printf("manager_anaglyph_blend channel_max=%u CoinGL_max=%u result=%s\n",
                  channelError, glMaxError, ok ? "PASS" : "FAIL");
      material->transparency = 0.0f;
    }
    std::vector<uint8_t> textureOnlyAnaglyph;
    for (int textureProfile = 0; textureProfile < 3 && ok; ++textureProfile) {
      SoTexture2 * texture = new SoTexture2;
      const uint8_t whiteAlpha[] = {255, 255, 255, 128};
      const uint8_t variedAlpha[] = {
        255, 255, 255, 128, 255, 255, 255, 64,
        255, 255, 255, 192, 255, 255, 255, 96
      };
      const bool probe2x2 = textureProfile >= 1;
      const bool mixedAlpha = textureProfile == 2;
      if (probe2x2) texture->image.setValue(SbVec2s(2, 2), 4, variedAlpha);
      else texture->image.setValue(SbVec2s(1, 1), 4, whiteAlpha);
      texture->model = SoTexture2::MODULATE;
      scene->insertChild(texture, 3);
      if (mixedAlpha) material->transparency = 0.5f;
      const bool probePlane = textureProfile == 2 &&
        std::getenv("COIN_RENDER_STEREO_TEXTURE_PROBE_PLANE") != nullptr;
      if (probePlane) {
        scene->removeChild(cube);
        SoCoordinate3 * coordinates = new SoCoordinate3;
        const SbVec3f corners[] = {
          SbVec3f(-0.6f, -0.4f, 0.6f), SbVec3f(0.6f, -0.4f, 0.6f),
          SbVec3f(0.6f, 0.8f, 0.6f), SbVec3f(-0.6f, 0.8f, 0.6f)
        };
        coordinates->point.setValues(0, 4, corners);
        scene->addChild(coordinates);
        SoTextureCoordinate2 * uv = new SoTextureCoordinate2;
        const SbVec2f texcoords[] = {
          SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1), SbVec2f(0, 1)
        };
        uv->point.setValues(0, 4, texcoords);
        scene->addChild(uv);
        SoFaceSet * face = new SoFaceSet;
        face->numVertices.set1Value(0, 4);
        scene->addChild(face);
      }
      std::vector<uint8_t> textureLeft, textureRight, textureAnaglyph;
      ok = render(SoCamera::LEFT_VIEW, textureLeft) &&
           render(SoCamera::RIGHT_VIEW, textureRight);
      camera->setStereoMode(SoCamera::MONOSCOPIC);
      if (ok) ok = adapter.render() == CoinRenderAction::SUCCESS;
      if (ok) adapter.getSceneManager()->getRenderTarget()->readbackRGBA(textureAnaglyph);
      ok = ok && textureAnaglyph.size() == textureLeft.size();
      unsigned channelError = 0, glMaxError = 0;
      unsigned directMin = 255, directMax = 0, glMin = 255, glMax = 0;
      if (ok) for (size_t i = 0; i < textureAnaglyph.size(); i += 4) {
        channelError = std::max(channelError,
          unsigned(std::abs(int(textureAnaglyph[i]) - int(textureLeft[i]))));
        channelError = std::max(channelError,
          unsigned(std::abs(int(textureAnaglyph[i + 1]) - int(textureRight[i + 1]))));
        channelError = std::max(channelError,
          unsigned(std::abs(int(textureAnaglyph[i + 2]) - int(textureRight[i + 2]))));
      }
      if (ok && std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
        SoOffscreenRenderer reference(SbViewportRegion(96, 96));
        reference.setComponents(SoOffscreenRenderer::RGB);
        reference.setBackgroundColor(SbColor(0, 0, 0));
        reference.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
        for (int eye = 0; eye < 2 && ok; ++eye) {
          camera->setStereoMode(eye == 0 ? SoCamera::LEFT_VIEW : SoCamera::RIGHT_VIEW);
          ok = reference.render(scene);
          const unsigned char * gl = ok ? reference.getBuffer() : nullptr;
          ok = ok && gl;
          if (ok) for (size_t pixel = 0; pixel < 96u * 96u; ++pixel)
            for (size_t c = 0; c < 3; ++c) {
              const auto & direct = eye == 0 ? textureLeft : textureRight;
              const size_t glPixel = (95u - pixel / 96u) * 96u + pixel % 96u;
              if (probe2x2 && c == 0 && direct[pixel * 4] > 0 && gl[glPixel * 3] > 0) {
                directMin = std::min(directMin, unsigned(direct[pixel * 4]));
                directMax = std::max(directMax, unsigned(direct[pixel * 4]));
                glMin = std::min(glMin, unsigned(gl[glPixel * 3]));
                glMax = std::max(glMax, unsigned(gl[glPixel * 3]));
              }
              glMaxError = std::max(glMaxError,
                unsigned(std::abs(int(direct[pixel * 4 + c]) - int(gl[glPixel * 3 + c]))));
            }
        }
        camera->setStereoMode(SoCamera::MONOSCOPIC);
      }
      ok = ok && channelError <= 1 && glMaxError <= 2 &&
           textureAnaglyph != anaglyph;
      if (mixedAlpha) ok = ok && textureAnaglyph != textureOnlyAnaglyph;
      else if (probe2x2) textureOnlyAnaglyph = textureAnaglyph;
      std::printf("manager_anaglyph_texture_alpha_%s%s%s channel_max=%u CoinGL_max=%u result=%s\n",
                  probe2x2 ? "2x2" : "1x1", mixedAlpha ? "_material" : "",
                  probePlane ? "_plane" : "",
                  channelError, glMaxError, ok ? "PASS" : "FAIL");
      if (probe2x2) std::printf("manager_anaglyph_texture_alpha_2x2 ranges direct=%u..%u CoinGL=%u..%u\n",
                                directMin, directMax, glMin, glMax);
      scene->removeChild(texture);
      material->transparency = 0.0f;
    }
    manager.setCamera(nullptr);
    manager.setSceneGraph(nullptr);
  }
  camera->setStereoMode(SoCamera::MONOSCOPIC);
  action.setRenderTarget(nullptr);
  scene->unref();
  std::printf("stereo_eye_adapter backend=%u renderer=%u vendor=0x%x adapter=%s\n",
    caps.backend, caps.renderer, caps.vendor_id, caps.adapter_name);
  return ok ? 0 : 1;
}
