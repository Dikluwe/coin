#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderScreenRasterCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoIndexedMarkerSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
// Only immutable defaultFont bitmap data/metrics are shared. This test owns
// projection, mask addressing, fog equations, composition and depth oracles.
// CMake gives these data accessors test-local symbol names.
#include "fonts/defaultfonts.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
const int side = 128;
bool passed = true;
bool require(bool value, const std::string &label) {
  if (!value) {
    passed = false;
    std::cerr << "planar_fog FAIL " << label << '\n';
  }
  return value;
}
enum Kind { IMAGE, HOLED_IMAGE, TEXT, MARKER, INDEXED };
const char *name(Kind k) {
  return k == IMAGE         ? "image"
         : k == HOLED_IMAGE ? "image-rgba"
         : k == TEXT        ? "text"
         : k == MARKER      ? "marker"
                            : "indexed";
}
const int foreground[3] = {210, 55, 35}, fogColor[3] = {31, 156, 214};
const float visibility = 1.7f;
double ndc(bool perspective, double distance) {
  return perspective ? 1.2 - 2.2 / distance : (2 * distance - 12) / 10;
}
double transmission(int mode, double d) {
  const double r = d / visibility;
  return mode == 0   ? 1
         : mode == 1 ? std::max(0., 1 - r)
         : mode == 2 ? std::exp(-5.545 * r)
                     : std::exp(-std::pow(2.35 * r, 2));
}
int markerId = -1;
bool markerBit(int x, int y) {
  return (x >= 2 && x <= 7 && y >= 2 && y <= 20) ||
         (x >= 13 && x <= 21 && y >= 3 && y <= 8) ||
         (x >= 15 && x <= 20 && y >= 15 && y <= 21);
}
void installMarker() {
  std::vector<unsigned char> bits(25 * 4, 0);
  for (int y = 0; y < 25; ++y)
    for (int x = 0; x < 25; ++x)
      if (markerBit(x, y))
        bits[size_t(y * 4 + x / 8)] |= uint8_t(0x80u >> (x % 8));
  markerId = SoMarkerSet::getNumDefinedMarkers() + 17;
  // Bytes are explicitly bottom-up, MSB first.
  SoMarkerSet::addMarker(markerId, SbVec2s(25, 25), bits.data(), FALSE, FALSE);
}
std::vector<uint8_t> imageBytes(bool holes) {
  std::vector<uint8_t> v(25 * 21 * 4);
  for (int y = 0; y < 21; ++y)
    for (int x = 0; x < 25; ++x) {
      const size_t p = size_t(y * 25 + x) * 4;
      v[p] = uint8_t(160 + 17 * (x % 5));
      v[p + 1] = uint8_t(30 + 13 * (y % 7));
      v[p + 2] = uint8_t(20 + (x * 3 + y * 5) % 100);
      v[p + 3] = holes && ((x / 5 + y / 5) % 2) ? 0 : 255;
    }
  return v;
}
struct Pixel {
  bool painted = false, depthWritten = false;
  int anchor = 0;
  std::array<int, 3> rgb{{0, 0, 0}};
};
struct Scene {
  SoSeparator *root = new SoSeparator;
  SoEnvironment *environment = new SoEnvironment;
  SoDepthBuffer *depth = new SoDepthBuffer;
  SoMaterial *material = new SoMaterial;
  Kind kind;
  bool perspective, emptyGroup;
  std::vector<uint8_t> bytes;
  SoImage *imageNode = nullptr;
  std::array<int, 3> color{{210, 55, 35}};
  Scene(Kind k, bool p, bool group)
      : kind(k), perspective(p), emptyGroup(group),
        bytes(imageBytes(k == HOLED_IMAGE)) {
    root->ref();
    SoCamera *c = nullptr;
    if (p) {
      auto *camera = new SoPerspectiveCamera;
      camera->heightAngle = float(1.5707963267948966);
      c = camera;
    } else {
      auto *camera = new SoOrthographicCamera;
      camera->height = 4;
      c = camera;
    }
    c->position.setValue(0, 0, 6);
    c->nearDistance = 1;
    c->farDistance = 11;
    c->aspectRatio = 1;
    root->addChild(c);
    environment->fogType = SoEnvironment::NONE;
    environment->fogVisibility = visibility;
    environment->fogColor.setValue(fogColor[0] / 255.f, fogColor[1] / 255.f,
                                   fogColor[2] / 255.f);
    root->addChild(environment);
    SoGroup *parent = root;
    if (group) {
      auto *g = new SoShadowGroup;
      g->isActive = TRUE;
      g->precision = .125f;
      root->addChild(g);
      parent = g;
    }
    auto *lighting = new SoLightModel;
    lighting->model = SoLightModel::BASE_COLOR;
    parent->addChild(lighting);
    depth->function = SoDepthBuffer::LEQUAL;
    parent->addChild(depth);
    material->diffuseColor.setValue(
        foreground[0] / 255.f, foreground[1] / 255.f, foreground[2] / 255.f);
    material->transparency = 0;
    parent->addChild(material);
    if (k == MARKER || k == INDEXED) {
      auto *coords = new SoCoordinate3;
      // Author x from desired pixel centers with a quarter-pixel guard against
      // float projection rounding. Both anchors remain in one public node.
      const float tangent = std::tan(float(1.5707963267948966) / 2.f);
      SbVec3f points[3];
      for (int i = 0; i < 2; ++i) {
        const double d = i ? 7 : 3, px = i ? 96.25 : 32.25;
        points[i].setValue(float((px - 64) * (p ? d * tangent : 2) / 64), 0,
                           6 - float(d));
      }
      points[2].setValue(99, 99, 0);
      coords->point.setValues(0, 3, points);
      parent->addChild(coords);
      if (k == MARKER) {
        auto *m = new SoMarkerSet;
        m->numPoints = 2;
        const int32_t glyphs[] = {markerId, markerId};
        m->markerIndex.setValues(0, 2, glyphs);
        parent->addChild(m);
      } else {
        auto *m = new SoIndexedMarkerSet;
        const int32_t indices[] = {1, 0};
        const int32_t glyphs[] = {markerId, markerId};
        m->coordIndex.setValues(0, 2, indices);
        m->markerIndex.setValues(0, 2, glyphs);
        parent->addChild(m);
      }
    } else {
      auto *t = new SoTranslation;
      const double d = 3, tangent = std::tan(float(1.5707963267948966) / 2.f);
      t->translation.setValue(float(.25 * (p ? d * tangent : 2) / 64), 0, 3);
      parent->addChild(t);
      if (k == TEXT) {
        auto *f = new SoFont;
        f->name = "defaultFont";
        f->size = 33;
        parent->addChild(f);
        auto *text = new SoText2;
        text->string = "M";
        parent->addChild(text);
      } else {
        auto *i = new SoImage;
        imageNode = i;
        i->horAlignment = SoImage::CENTER;
        i->vertAlignment = SoImage::HALF;
        i->image.setValue(SbVec2s(25, 21), 4, bytes.data());
        parent->addChild(i);
      }
    }
  }
  ~Scene() { root->unref(); }
  void mutate(bool changed) {
    if (kind == IMAGE || kind == HOLED_IMAGE) {
      bytes = imageBytes(kind == HOLED_IMAGE);
      if (changed)
        for (size_t at = 0; at < bytes.size(); at += 4)
          for (int c = 0; c < 3; ++c)
            bytes[at + c] = uint8_t(255 - bytes[at + c]);
      imageNode->image.setValue(SbVec2s(25, 21), 4, bytes.data());
    } else {
      for (int c = 0; c < 3; ++c)
        color[c] = changed ? (c == 0 ? 20 : c == 1 ? 200 : 85) : foreground[c];
      material->diffuseColor.setValue(color[0] / 255.f, color[1] / 255.f,
                                      color[2] / 255.f);
    }
  }
  Pixel pixel(int x, int bottomY) const {
    Pixel p;
    if (kind == IMAGE || kind == HOLED_IMAGE) {
      const int sx = x - 52, sy = bottomY - 54;
      if (sx < 0 || sx >= 25 || sy < 0 || sy >= 21)
        return p;
      const size_t at = size_t(sy * 25 + sx) * 4;
      p.depthWritten = true;
      p.painted = bytes[at + 3] != 0;
      for (int c = 0; c < 3; ++c)
        p.rgb[c] = bytes[at + c];
      return p;
    }
    if (kind == TEXT) {
      const int height = coin_default2dfont_get_height(33),
                bearing = coin_default2dfont_get_bearing(33);
      const int sx = x - 64, sy = bottomY - (64 + bearing - height);
      if (sx < 0 || sx >= 32 || sy < 0 || sy >= height)
        return p;
      const auto *glyph =
          coin_default2dfont_get_data(33) + size_t('M') * height * 4;
      p.painted = (glyph[size_t(sy * 4 + sx / 8)] & (0x80u >> (sx % 8))) != 0;
    } else {
      for (int anchor = 0; anchor < 2; ++anchor) {
        const int sx = x - (anchor ? 84 : 20), sy = bottomY - 52;
        if (sx < 0 || sx >= 25 || sy < 0 || sy >= 25)
          continue;
        p.anchor = anchor;
        p.painted = markerBit(sx, sy);
        break;
      }
    }
    p.depthWritten = p.painted;
    for (int c = 0; c < 3; ++c)
      p.rgb[c] = color[c];
    return p;
  }
};
struct Frame {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial = 0;
};
class Audit : public CoinRenderBackend {
public:
  std::unique_ptr<CoinRenderBackend> real;
  CoinRenderFramePlan last;
  unsigned calls = 0, producers = 0;
  explicit Audit(std::unique_ptr<CoinRenderBackend> b) : real(std::move(b)) {}
  bool isGpuBackend() const override { return real->isGpuBackend(); }
  bool initializesCpuDepthBuffer() const override {
    return real->initializesCpuDepthBuffer();
  }
  CoinRenderBackendStatus getStatus() const override {
    return real->getStatus();
  }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &t) override {
    return real->prepare(t);
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,
                                CoinRenderTargetP &t) override {
    ++calls;
    last = f;
    return real->submit(f, t);
  }
  CoinRenderSubmitResult
  submit(const CoinRenderFramePlan &f, CoinRenderTargetP &t,
         const CoinRenderFrameReuseDecision &r) override {
    ++calls;
    last = f;
    return real->submit(f, t, r);
  }
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan &f,
                                   const SbVec2i32 &s, uint64_t revision,
                                   CoinRenderTargetP &t,
                                   uint64_t &token) override {
    ++producers;
    return real->submitRtt(f, s, revision, t, token);
  }
  bool resetOnActionDetach() const override {
    return real->resetOnActionDetach();
  }
  CoinRenderDeviceDomain resourceDomain() const override {
    return real->resourceDomain();
  }
  bool readbackLoad(uint64_t &a, uint64_t &b) const override {
    return real->readbackLoad(a, b);
  }
  void poll() override { real->poll(); }
  const std::string &getLastError() const override {
    return real->getLastError();
  }
};
struct Harness {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(side, side)};
  Audit *audit = nullptr;
  Harness(bool gpu, bool canonical, const CoinRenderCapabilities &caps) {
    CoinRenderOptions options;
    options.sampleCount = 1;
    if (canonical)
      options.rasterizationPolicy = COIN_RENDER_RASTER_CANONICAL_8BIT;
    if (gpu)
      options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
    target.reset(
        CoinRenderTarget::createOffscreen(SbVec2i32(side, side), options));
    if (!target)
      return;
    if (!gpu)
      target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    auto &p = target->getPimpl().get();
    if (p.prepareBackend() != CoinRenderBackendStatus::SUCCESS || !p.backend)
      return;
    require(p.backend->isGpuBackend() == gpu, "explicit requested backend");
    audit = new Audit(std::move(p.backend));
    p.backend.reset(audit);
    target->setDepthReadbackEnabled(TRUE);
    action.setRenderTarget(target.get());
    action.setTransparencyType(CoinRenderAction::BLEND);
    action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    require(target->getOptions().sampleCount == 1 &&
                target->getOptions().rasterizationPolicy ==
                    options.rasterizationPolicy,
            "actual 1x and requested raster policy");
  }
  ~Harness() { action.setRenderTarget(nullptr); }
  Frame read() {
    Frame f;
    target->readbackRGBA(f.rgba);
    target->readbackDepth(f.depth);
    f.serial = target->getLastSubmissionSerial();
    return f;
  }
  Frame render(Scene &scene, const std::string &label) {
    const auto source = scene.root->getNodeId(),
               before = target->getLastSubmissionSerial();
    const unsigned calls = audit->calls;
    action.apply(scene.root);
    auto f = read();
    require(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                f.serial > before && audit->calls == calls + 1,
            label +
                " one valid publication: " + action.getLastError().getString());
    require(scene.root->getNodeId() == source,
            label + " source unchanged during apply");
    const bool noCasterPass =
        std::all_of(audit->last.draws.begin(), audit->last.draws.end(),
                    [](const CoinRenderDrawPacket &draw) {
                      return draw.shadowLightSlot == 0;
                    });
    require(audit->last.shadowLights.empty() && noCasterPass,
            label + " no effective maps from a scene authored without lights");
    return f;
  }
};
void checkFrame(const Scene &scene, const Frame &frame, int mode,
                const std::string &label) {
  if (!require(frame.rgba.size() == side * side * 4 &&
                   frame.depth.size() == side * side,
               label + " actual color/depth"))
    return;
  unsigned covered[2] = {0, 0}, holes = 0, bad = 0;
  int maximum = 0;
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x) {
      const auto p = scene.pixel(x, side - 1 - y);
      const size_t at = size_t(y * side + x);
      const double z = ndc(scene.perspective, p.anchor ? 7 : 3),
                   t = transmission(mode, std::abs(z));
      if (p.painted)
        ++covered[p.anchor];
      else
        ++holes;
      for (int c = 0; c < 3; ++c) {
        const double expected =
            p.painted ? fogColor[c] + (p.rgb[c] - fogColor[c]) * t : 0;
        const int delta =
            int(std::ceil(std::abs(frame.rgba[at * 4 + c] - expected)));
        maximum = std::max(maximum, delta);
        bad += std::abs(frame.rgba[at * 4 + c] - expected) > 3;
      }
      const float depth = p.depthWritten ? float(.5 + .5 * z) : 1.f;
      bad += frame.rgba[at * 4 + 3] != 255 || !std::isfinite(frame.depth[at]) ||
             std::abs(frame.depth[at] - depth) > 1e-5;
    }
  std::cout << "planar_fog_image " << label << " mode=" << mode
            << " covered=" << covered[0] << ',' << covered[1]
            << " holes=" << holes << " bad=" << bad << " max=" << maximum
            << '\n';
  require(
      covered[0] > 20 && holes > 100 &&
          ((scene.kind != MARKER && scene.kind != INDEXED) || covered[1] > 20),
      label + " authored visible mask and separated anchors");
  require(bad == 0, label + " independent RGB3, mask, alpha and depth");
}
void delivery(const Scene &scene, const Audit &audit,
              const std::string &label) {
  unsigned counted[2] = {0, 0};
  for (const auto &v : audit.last.vertices) {
    const int anchor =
        (scene.kind == MARKER || scene.kind == INDEXED) && v.position[0] > 0
            ? 1
            : 0;
    const double expected = std::abs(ndc(scene.perspective, anchor ? 7 : 3));
    require(std::abs(v.fogEyeDepth - expected) < 1e-6,
            label + " Core retains per-anchor abs(NDCz)");
    ++counted[anchor];
  }
  require(counted[0] > 0 && ((scene.kind != MARKER && scene.kind != INDEXED) ||
                             counted[1] > 0),
          label + " all raster anchor payloads delivered");
}
bool core() {
  unsigned markerPixels = 0, fontPixels = 0;
  for (int y = 0; y < 25; ++y)
    for (int x = 0; x < 25; ++x)
      markerPixels += markerBit(x, y);
  const int fontHeight = coin_default2dfont_get_height(33);
  const auto *glyph =
      coin_default2dfont_get_data(33) + size_t('M') * fontHeight * 4;
  for (int y = 0; y < fontHeight; ++y)
    for (int x = 0; x < 32; ++x)
      fontPixels += (glyph[size_t(y * 4 + x / 8)] & (0x80u >> (x % 8))) != 0;
  require(markerPixels == 210 && fontPixels == 75,
          "independent authored bitmap witnesses are nonempty and sparse");
  for (float z : {-1.f, -.6f, -0.f, 0.f, .2f, .8f, 1.f})
    require(CoinRenderScreenRasterCore::planarRasterFogDepth(z) == std::abs(z),
            "finite planar depth is absolute NDC, including zero");
  for (bool perspective : {false, true})
    for (double distance : {3., 7.}) {
      const double z = ndc(perspective, distance);
      CoinRenderViewportSnapshot viewport;
      viewport.width = viewport.height = side;
      const SbMatrix modelView(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0,
                               float(-distance), 1);
      const SbMatrix projection = perspective
                                      ? SbMatrix(1, 0, 0, 0, 0, 1, 0, 0, 0, 0,
                                                 -1.2f, -1, 0, 0, -2.2f, 0)
                                      : SbMatrix(.5f, 0, 0, 0, 0, .5f, 0, 0, 0,
                                                 0, -.2f, 0, 0, 0, -1.2f, 1);
      CoinRenderScreenRasterAnchor anchor;
      std::string diagnostic;
      require(CoinRenderScreenRasterCore::matrixAnchor(
                  modelView * projection, viewport, anchor, diagnostic) &&
                  std::abs(anchor.pixelX - 64) < 1e-5 &&
                  std::abs(anchor.pixelY - 64) < 1e-5 &&
                  std::abs(anchor.depthCoin - z) < 1e-6,
              "Core anchor projection matches independently authored "
              "perspective/orthographic planes");
      require(
          std::abs(CoinRenderScreenRasterCore::planarRasterFogDepth(float(z)) -
                   std::abs(z)) < 1e-7,
          "planar Core absolute authored NDC depth");
      for (int a = 1; a <= 3; ++a)
        for (int b = a + 1; b <= 3; ++b)
          require(std::abs(transmission(a, std::abs(z)) -
                           transmission(b, std::abs(z))) *
                          180 >
                      6,
                  "fog functions have disjoint RGB3 witnesses");
    }
  for (bool perspective : {false, true})
    for (int mode = 1; mode <= 3; ++mode)
      require(std::abs(transmission(mode, std::abs(ndc(perspective, 3))) -
                       transmission(mode, std::abs(ndc(perspective, 7)))) *
                      180 >
                  6,
              "two anchors distinguish one-depth-for-node mutation");
  const auto bytes = imageBytes(true);
  CoinRenderTextureImageSnapshot image;
  bool transparent = false;
  std::string error;
  require(CoinRenderScreenRasterCore::imagePayload(bytes.data(), bytes.size(),
                                                   25, 21, 4, image,
                                                   transparent, error) &&
              transparent && image.pixelsRgba == bytes,
          "Core retains authored asymmetric image RGBA including holes");
  return passed;
}
void campaign(Harness &h, Scene &scene) {
  const std::string label =
      std::string(name(scene.kind)) + "/" +
      (scene.perspective ? "perspective" : "orthographic") +
      (scene.emptyGroup ? "/empty-active-group" : "/no-group");
  auto baseline = h.render(scene, label + " NONE cold");
  checkFrame(scene, baseline, 0, label);
  delivery(scene, *h.audit, label);
  const int modes[] = {SoEnvironment::HAZE, SoEnvironment::FOG,
                       SoEnvironment::SMOKE};
  for (int i = 0; i < 3; ++i) {
    scene.environment->fogType = modes[i];
    auto f = h.render(scene, label + " fog");
    checkFrame(scene, f, i + 1, label);
    delivery(scene, *h.audit, label);
    if (i == 1) {
      scene.depth->range.setValue(0, std::numeric_limits<float>::quiet_NaN());
      const auto source = scene.root->getNodeId();
      const unsigned calls = h.audit->calls, producers = h.audit->producers;
      h.action.apply(scene.root);
      auto bad = h.read();
      require(h.action.getLastStatus() != CoinRenderAction::SUCCESS &&
                  h.action.getLastError().getLength() > 0 &&
                  bad.serial == f.serial && bad.rgba == f.rgba &&
                  bad.depth == f.depth && h.audit->calls == calls &&
                  h.audit->producers == producers &&
                  source == scene.root->getNodeId(),
              label + " invalid depth range preserves publication before "
                      "executor/producers");
      scene.depth->range.setValue(0, 1);
      auto back = h.render(scene, label + " recovery");
      checkFrame(scene, back, i + 1, label);
      delivery(scene, *h.audit, label);
    }
  }
  scene.mutate(true);
  auto changed = h.render(scene, label + " SMOKE source mutation");
  checkFrame(scene, changed, 3, label);
  delivery(scene, *h.audit, label);
  scene.mutate(false);
  auto restored = h.render(scene, label + " SMOKE source restored");
  checkFrame(scene, restored, 3, label);
  delivery(scene, *h.audit, label);
  scene.environment->fogType = SoEnvironment::NONE;
  auto back = h.render(scene, label + " NONE restored");
  checkFrame(scene, back, 0, label);
  delivery(scene, *h.audit, label);
}
} // namespace
int main(int argc, char **argv) {
  std::string mode;
  int kind = -1, projection = -1, group = -1, policy = -1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--core" || arg == "--cpu" || arg == "--gpu") {
      if (!mode.empty())
        return 2;
      mode = arg;
    } else if (arg == "--canonical")
      policy = 1;
    else if (arg == "--native")
      policy = 0;
    else if (arg == "--kind" || arg == "--projection" || arg == "--group") {
      if (++i == argc)
        return 2;
      const std::string v = argv[i];
      if (arg == "--kind") {
        if (v == "image")
          kind = IMAGE;
        else if (v == "image-rgba")
          kind = HOLED_IMAGE;
        else if (v == "text")
          kind = TEXT;
        else if (v == "marker")
          kind = MARKER;
        else if (v == "indexed")
          kind = INDEXED;
        else
          return 2;
      } else if (arg == "--projection") {
        if (v != "orthographic" && v != "perspective")
          return 2;
        projection = v == "perspective";
      } else {
        if (v != "none" && v != "empty")
          return 2;
        group = v == "empty";
      }
    } else
      return 2;
  }
  if (mode.empty())
    return 2;
  SoDB::init();
  CoinRenderAction::initClass();
  if (mode == "--core")
    return core() ? 0 : 1;
  const bool gpu = mode == "--gpu";
  CoinRenderCapabilities caps{};
  if (gpu && (coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
                                             &caps, sizeof(caps)) ||
              !caps.gpu_available))
    return 77;
  if (gpu)
    std::cout << "planar_fog_GPU backend=" << caps.backend
              << " renderer=" << caps.renderer
              << " adapter=" << caps.adapter_name << '\n';
  installMarker();
  for (int c = 0; c < 2; ++c)
    if (policy < 0 || policy == c) {
      Harness h(gpu, c != 0, caps);
      if (!require(h.target && h.audit, "real requested executor prepared"))
        return 1;
      for (int p = 0; p < 2; ++p)
        if (projection < 0 || projection == p)
          for (int g = 0; g < 2; ++g)
            if (group < 0 || group == g)
              for (int k = 0; k <= INDEXED; ++k)
                if (kind < 0 || kind == k) {
                  Scene scene(Kind(k), p != 0, g != 0);
                  campaign(h, scene);
                }
    }
  SoMarkerSet::removeMarker(markerId);
  return passed ? 0 : 1;
}
