#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
namespace {
bool check(bool v, const std::string &message) {
  if (!v)
    std::cerr << message << '\n';
  return v;
}
class Capture : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderFramePlan frame;
  unsigned submits = 0;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &p,
                                CoinRenderTargetP &t) override {
    frame = p;
    ++submits;
    return CoinRenderCpuReferenceBackend::submit(p, t);
  }
};
struct Scene {
  Scene(int components, int model, int units, bool minify, bool varying)
      : root(new SoSeparator) {
    root->ref();
    auto *c = new SoOrthographicCamera;
    c->height = 2;
    c->position.setValue(0, 0, 4);
    root->addChild(c);
    auto *l = new SoLightModel;
    l->model = SoLightModel::BASE_COLOR;
    root->addChild(l);
    quality = new SoComplexity;
    root->addChild(quality);
    auto *m = new SoMaterial;
    m->diffuseColor.setValue(.7f, .5f, .3f);
    m->transparency = .2f;
    root->addChild(m);
    for (int i = 0; i < units; ++i) {
      auto *u = new SoTextureUnit;
      u->unit = units == 1 ? 0 : i * 3;
      root->addChild(u);
      auto *t = new SoTexture2;
      t->model = model;
      t->blendColor.setValue(.2f, .4f, .8f);
      t->wrapS = SoTexture2::REPEAT;
      t->wrapT = SoTexture2::REPEAT;
      const int size = minify ? 128 : 8;
      std::vector<uint8_t> bytes(size * size * components);
      for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
          const size_t at = (y * size + x) * components;
          const uint8_t lum = varying ? (((x + y) & 1) ? 0 : 255) : 160;
          bytes[at] = lum;
          if (components == 2)
            bytes[at + 1] = 192;
          if (components >= 3) {
            bytes[at + 1] = varying ? lum : 120;
            bytes[at + 2] = varying ? lum : 80;
          }
          if (components == 4)
            bytes[at + 3] = 192;
        }
      t->image.setValue(SbVec2s(size, size), components, bytes.data());
      root->addChild(t);
      images.push_back(t);
      auto *uv = new SoTextureCoordinate2;
      const SbVec2f p[] = {
          {.03f, .02f}, {.97f, .02f}, {.97f, .98f}, {.03f, .98f}};
      uv->point.setValues(0, 4, p);
      root->addChild(uv);
    }
    auto *coords = new SoCoordinate3;
    const float r = minify ? .25f : .8f;
    const SbVec3f p[] = {{-r, -r, 0}, {r, -r, 0}, {r, r, 0}, {-r, r, 0}};
    coords->point.setValues(0, 4, p);
    root->addChild(coords);
    auto *f = new SoIndexedFaceSet;
    const int32_t idx[] = {0, 1, 2, 3, -1};
    f->coordIndex.setValues(0, 5, idx);
    root->addChild(f);
  }
  ~Scene() { root->unref(); }
  SoSeparator *root;
  SoComplexity *quality;
  std::vector<SoTexture2 *> images;
};
struct Harness {
  Harness(bool gpu)
      : action(SbViewportRegion(64, 64)), gpuAction(SbViewportRegion(64, 64)),
        gl(SbViewportRegion(64, 64)) {
    cpu.reset(CoinRenderTarget::createOffscreen(SbVec2i32(64, 64)));
    capture = new Capture;
    cpu->getPimpl()->backend.reset(capture);
    action.setRenderTarget(cpu.get());
    if (gpu) {
      native.reset(CoinRenderTarget::createOffscreen(SbVec2i32(64, 64)));
      gpuAction.setRenderTarget(native.get());
    }
    action.setTransparencyType(CoinRenderAction::BLEND);
    gpuAction.setTransparencyType(CoinRenderAction::BLEND);
    action.setBackgroundColor(SbColor4f(.1f, .1f, .1f, 1));
    gpuAction.setBackgroundColor(SbColor4f(.1f, .1f, .1f, 1));
    gl.setComponents(SoOffscreenRenderer::RGB);
    gl.setBackgroundColor(SbColor(.1f, .1f, .1f));
    gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  }
  ~Harness() {
    action.setRenderTarget(nullptr);
    gpuAction.setRenderTarget(nullptr);
  }
  bool compare(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b,
               int channels, const std::string &label, int radius) {
    if (!check(a.size() == 64 * 64 * 4 && b.size() == 64 * 64 * channels,
               label + ": readback"))
      return false;
    int maximum = 0;
    double sum = 0;
    unsigned n = 0;
    for (int y = 32 - radius; y < 32 + radius; ++y)
      for (int x = 32 - radius; x < 32 + radius; ++x)
        for (int c = 0; c < 3; ++c) {
          int d = std::abs(int(a[(y * 64 + x) * 4 + c]) -
                           int(b[(y * 64 + x) * channels + c]));
          maximum = std::max(maximum, d);
          sum += d;
          ++n;
        }
    std::cout << label << " samples=" << n << " mae=" << sum / n
              << " max=" << maximum << '\n';
    return check(sum / n <= 1.5 && maximum <= 4, label + ": texture pixels");
  }
  bool render(Scene &s, const std::string &label, bool minify,
              bool fast = true) {
    action.setFastPathEnabled(fast);
    gpuAction.setFastPathEnabled(fast);
    action.apply(s.root);
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
               label + ": CPU " + action.getLastError().getString()))
      return false;
    if (native) {
      gpuAction.apply(s.root);
      if (!check(gpuAction.getLastStatus() == CoinRenderAction::SUCCESS,
                 label + ": GPU " + gpuAction.getLastError().getString()))
        return false;
      std::vector<uint8_t> a, b;
      cpu->readbackRGBA(a);
      native->readbackRGBA(b);
      const int radius = minify ? 5 : 18;
      if (!compare(a, b, 4, label + "/CPU-GPU", radius))
        return false;
      if (!check(gl.render(s.root) && gl.getBuffer(),
                 label + ": mandatory CoinGL"))
        return false;
      std::vector<uint8_t> reference(64 * 64 * 3);
      const auto *p = gl.getBuffer();
      for (int y = 0; y < 64; ++y)
        std::copy(p + (63 - y) * 64 * 3, p + (64 - y) * 64 * 3,
                  reference.begin() + y * 64 * 3);
      if (!compare(a, reference, 3, label + "/CPU-GL", radius) ||
          !compare(b, reference, 3, label + "/GPU-GL", radius))
        return false;
    }
    ++cases;
    return true;
  }
  bool rejected(Scene &s, const std::string &label) {
    std::vector<uint8_t> before, after;
    cpu->readbackRGBA(before);
    const auto serial = cpu->getLastSubmissionSerial();
    const auto submits = capture->submits;
    action.apply(s.root);
    cpu->readbackRGBA(after);
    if (!check(action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                   before == after &&
                   serial == cpu->getLastSubmissionSerial() &&
                   submits == capture->submits,
               label + ": CPU publication"))
      return false;
    if (native) {
      native->readbackRGBA(before);
      const auto serial = native->getLastSubmissionSerial();
      gpuAction.apply(s.root);
      native->readbackRGBA(after);
      if (!check(gpuAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                     before == after &&
                     serial == native->getLastSubmissionSerial(),
                 label + ": GPU publication"))
        return false;
    }
    ++rejects;
    return true;
  }
  std::unique_ptr<CoinRenderTarget> cpu, native;
  Capture *capture;
  CoinRenderAction action, gpuAction;
  SoOffscreenRenderer gl;
  unsigned cases = 0, rejects = 0;
};
bool core() {
  CoinRenderTextureImageSnapshot image;
  image.width = image.height = 2;
  image.pixelsRgba = {0, 10, 20, 255, 1, 11, 21, 255,
                      2, 12, 22, 255, 3, 13, 23, 255};
  if (!check(CoinRenderTextureSamplingCore::generate(image) &&
                 image.mipmapsRgba == std::vector<uint8_t>({2, 12, 22, 255}),
             "independent mip average/rounding"))
    return false;
  image.width = 3;
  const auto before = image.mipmapsRgba;
  if (!check(!CoinRenderTextureSamplingCore::generate(image) &&
                 image.mipmapsRgba == before,
             "NPOT fails atomically"))
    return false;
  image.width = 8192;
  image.height = 8192;
  if (!check(!CoinRenderTextureSamplingCore::validMipImage(image),
             "mip budget limit"))
    return false;
  return true;
}
bool run(bool gpu) {
  if (!core())
    return false;
  Harness h(gpu);
  for (float q : {.1f, .2f, .499f, .5f, .799f, .8f, .85f})
    for (int components = 1; components <= 4; ++components)
      for (int model : {SoTexture2::MODULATE, SoTexture2::REPLACE,
                        SoTexture2::DECAL, SoTexture2::BLEND})
        for (int units : {1, 2}) {
          if (model == SoTexture2::DECAL && components < 3)
            continue;
          Scene scene(components, model, units, false, false);
          scene.quality->textureQuality = q;
          if (!h.render(scene,
                        "quality-" + std::to_string(q) + "/nc-" +
                            std::to_string(components) + "/model-" +
                            std::to_string(model) + "/units-" +
                            std::to_string(units),
                        false))
            return false;
          const auto &frame = h.capture->frame;
          const int expected = q < .2f ? 0 : q < .5f ? 1 : q < .8f ? 2 : 3;
          if (!check(!frame.samplers.empty() &&
                         int(frame.samplers[0].filter) == expected,
                     "sampler contract") ||
              !check(frame.textures[0].mipmapped == (q >= .5f),
                     "mipmap capture"))
            return false;
        }
  for (float q : {.1f, .3f, .5f, .7f, .8f})
    for (int units : {1, 2})
      for (bool fast : {false, true}) {
        Scene scene(3, SoTexture2::REPLACE, units, true, true);
        scene.quality->textureQuality = q;
        if (!h.render(scene,
                      "minify/quality-" + std::to_string(q) + "/units-" +
                          std::to_string(units),
                      true, fast))
          return false;
        if (q >= .5f) {
          std::vector<uint8_t> pixels;
          h.cpu->readbackRGBA(pixels);
          if (!check(std::abs(int(pixels[(32 * 64 + 32) * 4]) - 108) <= 2,
                     "checker mip average independent oracle after material "
                     "alpha"))
            return false;
        }
      }
  for (float q : {.5f, .8f}) {
    Scene projective(3, SoTexture2::REPLACE, 1, true, true);
    projective.quality->textureQuality = q;
    std::vector<uint8_t> blocks(128 * 128 * 3);
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x)
        for (int c = 0; c < 3; ++c)
          blocks[(y * 128 + x) * 3 + c] = ((x / 8 + y / 8) & 1) ? 160 : 40;
    projective.images[0]->image.setValue(SbVec2s(128, 128), 3, blocks.data());
    auto *matrix = new SoTextureMatrixTransform;
    SbMatrix m = SbMatrix::identity();
    m[0][3] = .3f;
    matrix->matrix = m;
    projective.root->insertChild(matrix, projective.root->getNumChildren() - 2);
    if (!h.render(projective,
                  "projective mip footprint/quality-" + std::to_string(q),
                  true))
      return false;
  }
  Scene scene(4, SoTexture2::MODULATE, 2, false, false);
  scene.quality->textureQuality = .3f;
  if (!h.render(scene, "off-on-baseline", false))
    return false;
  for (float bad : {.9f, std::numeric_limits<float>::quiet_NaN()}) {
    scene.quality->textureQuality = bad;
    if (!h.rejected(scene, "invalid quality"))
      return false;
    scene.quality->textureQuality = .3f;
    if (!h.render(scene, "quality recovery", false))
      return false;
  }
  for (int unit = 0; unit < 2; ++unit) {
    auto *t = scene.images[unit];
    t->model = SoTexture2::DECAL;
    std::vector<uint8_t> la(8 * 8 * 2, 128);
    t->image.setValue(SbVec2s(8, 8), 2, la.data());
    if (!h.rejected(scene, "luminance DECAL"))
      return false;
    t->model = SoTexture2::MODULATE;
    std::vector<uint8_t> rgba(8 * 8 * 4, 192);
    t->image.setValue(SbVec2s(8, 8), 4, rgba.data());
    if (!h.render(scene, "format recovery", false))
      return false;
  }
  std::vector<uint8_t> npot(3 * 5 * 4, 192);
  scene.images[0]->image.setValue(SbVec2s(3, 5), 4, npot.data());
  scene.quality->textureQuality = .5f;
  if (!h.rejected(scene, "NPOT mipmap limit"))
    return false;
  scene.quality->textureQuality = .3f;
  if (!h.rejected(scene, "NPOT implicit-rescale limit"))
    return false;
  std::vector<uint8_t> pot(8 * 8 * 4, 192);
  scene.images[0]->image.setValue(SbVec2s(8, 8), 4, pot.data());
  if (!h.render(scene, "NPOT error recovery", false))
    return false;
  std::vector<uint8_t> white(4, 255);
  scene.images[0]->image.setValue(SbVec2s(2, 2), 1, white.data());
  if (!h.render(scene, "authored white luminance is not pending-file dummy",
                false))
    return false;
  scene.quality->textureQuality = 0;
  if (!h.render(scene, "quality off", false))
    return false;
  if (!check(h.capture->frame.textures.empty(), "off uploads no image"))
    return false;
  scene.quality->textureQuality = .8f;
  if (!h.render(scene, "mipmap reactivation", false))
    return false;
  for (int unit : {0, 1}) {
    scene.images[unit]->image.setValue(SbVec2s(0, 0), 0, nullptr);
    if (!h.render(scene,
                  "individual texture unit disabled-" + std::to_string(unit),
                  false))
      return false;
    if (!check(h.capture->frame.textures.size() == 1,
               "disabled unit has no upload"))
      return false;
    scene.images[unit]->image.setValue(SbVec2s(8, 8), 4, pot.data());
    if (!h.render(scene, "individual unit reactivated-" + std::to_string(unit),
                  false))
      return false;
  }
  if (h.native) {
    CoinRenderCapabilities caps{};
    if (!check(
            coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
                                           &caps, sizeof(caps)) == 0 &&
                (caps.features & COIN_RENDER_FEATURE_TEXTURE_MIPMAPS),
            "compiled mip capability"))
      return false;
  }
  std::cout << "P07 sampling cases=" << h.cases << " rejected=" << h.rejects
            << " GPU=" << gpu << '\n';
  return true;
}
} // namespace
int main(int argc, char **argv) {
  SoDB::init();
  CoinRenderAction::initClass();
  if (argc > 1 && std::string(argv[1]) == "--reject-config") {
    Harness h(false);
    Scene s(3, SoTexture2::MODULATE, 1, false, false);
    s.quality->textureQuality = .3f;
    return h.rejected(s, "custom quality threshold") ? 0 : 1;
  }
  return run(argc > 1 && std::string(argv[1]) == "--gpu") ? 0 : 1;
}
