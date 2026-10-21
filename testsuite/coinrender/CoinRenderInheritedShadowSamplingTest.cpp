#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
// Reuse only the established scene lifetime, backend forwarding and delivery
// infrastructure. Sampling and the raster image arithmetic below are
// independent.
#define COIN_RENDER_SHADOW_MULTITEXTURE_FIXTURE_ONLY
#include "CoinRenderShadowMultitextureTest.cpp"
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoTextureCoordinateObject.h>

namespace inherited_sampling {
struct Settings {
  int maps = 4, unit = -1, quality = 1;
  bool directional = false, repeat = true, holes = false, core = false;
};
const float qualities[] = {.1f, .3f, .6f, .9f};

// Every 2x2 tile has the same mean. Thus all lower mip levels have a known
// integer value, independently of the mip generator. They are present but
// constant raster coordinates exercise magnification at level zero, not LOD>0.
std::vector<uint8_t> pixels(int unit, bool white, bool alternate, bool many) {
  std::vector<uint8_t> out(8 * 8 * 4);
  const int low = (many ? 210 : 60) - (alternate ? 40 : 0),
            high = (many ? 242 : 240) - (alternate ? 40 : 0);
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) {
      const int bits[] = {(x + unit + int(alternate)) & 1, (y + unit) & 1,
                          (x + y + unit + int(alternate)) & 1};
      for (int c = 0; c < 3; ++c)
        out[(y * 8 + x) * 4 + c] = white ? 255 : uint8_t(bits[c] ? high : low);
      out[(y * 8 + x) * 4 + 3] = 255;
    }
  return out;
}
int index(int x, bool repeat) {
  return repeat ? ((x % 8) + 8) % 8 : std::max(0, std::min(7, x));
}
double texel(const std::vector<uint8_t> &p, int x, int y, int c, bool repeat) {
  return p[(index(y, repeat) * 8 + index(x, repeat)) * 4 + c] / 255.;
}
double sample(const std::vector<uint8_t> &p, double s, double t, int c,
              bool repeat, bool linear) {
  if (repeat) {
    s -= std::floor(s);
    t -= std::floor(t);
  } else {
    s = std::max(0., std::min(1., s));
    t = std::max(0., std::min(1., t));
  }
  if (!linear)
    return texel(p, int(std::floor(s * 8)), int(std::floor(t * 8)), c, repeat);
  const double x = s * 8 - .5, y = t * 8 - .5;
  const int ix = int(std::floor(x)), iy = int(std::floor(y));
  const double a = x - ix, b = y - iy;
  return (1 - b) * ((1 - a) * texel(p, ix, iy, c, repeat) +
                    a * texel(p, ix + 1, iy, c, repeat)) +
         b * ((1 - a) * texel(p, ix, iy + 1, c, repeat) +
              a * texel(p, ix + 1, iy + 1, c, repeat));
}
struct RasterScene : msaa_shadow_test::Scene {
  Settings setting;
  SoImage *image = new SoImage;
  SoShadowStyle *rasterStyle = new SoShadowStyle;
  SoComplexity *qualityNode = new SoComplexity;
  SoTexture2 *textures[8]{};
  SoTextureCoordinateObject *coordinates[8]{};
  SoTextureMatrixTransform *transforms[8]{};
  std::vector<int> active;
  std::vector<uint8_t> imageBytes;
  bool white = true;
  int alternate = -1;
  explicit RasterScene(const Settings &s)
      : msaa_shadow_test::Scene(s.maps, s.directional), setting(s) {
    // The bitmap is on z=-1, the observer at z=6 (near1/far20). Its
    // 17x17 raster footprint is centred at (64,64), depth=(7-1)/19.
    receiverMaterial->ref();
    receiver->removeAllChildren();
    rasterStyle->style = SoShadowStyle::SHADOWED;
    receiver->addChild(rasterStyle);
    receiverMaterial->diffuseColor.setValue(1, 1, 1);
    receiverMaterial->ambientColor.setValue(0, 0, 0);
    receiverMaterial->specularColor.setValue(0, 0, 0);
    receiverMaterial->emissiveColor.setValue(0, 0, 0);
    receiver->addChild(receiverMaterial);
    receiverMaterial->unref();
    auto *environment = new SoEnvironment;
    environment->ambientIntensity = 0;
    receiver->addChild(environment);
    auto *normal = new SoNormal;
    normal->vector.setValue(0, 0, 1);
    receiver->addChild(normal);
    qualityNode->textureQuality = qualities[s.quality];
    receiver->addChild(qualityNode);
    for (int u = 0; u < 8; ++u) {
      if (s.unit >= 0 && u != s.unit)
        continue;
      active.push_back(u);
      auto *select = new SoTextureUnit;
      select->unit = u;
      receiver->addChild(select);
      textures[u] = new SoTexture2;
      textures[u]->model = SoTexture2::MODULATE;
      textures[u]->wrapS = textures[u]->wrapT =
          s.repeat ? SoTexture2::REPEAT : SoTexture2::CLAMP;
      receiver->addChild(textures[u]);
      coordinates[u] = new SoTextureCoordinateObject;
      coordinates[u]->factorS.setValue(0, 0, 0, .18f + .03f * u);
      coordinates[u]->factorT.setValue(0, 0, 0, .69f - .02f * u);
      coordinates[u]->factorR.setValue(0, 0, 0, .13f + .01f * u);
      coordinates[u]->factorQ.setValue(0, 0, 0, 2);
      receiver->addChild(coordinates[u]);
      transforms[u] = new SoTextureMatrixTransform;
      // Raw Q=2 and transformed Q=1. Translation depends on raw Q, exposing
      // a lost Q or a repeated matrix without relying on a projection policy.
      const SbMatrix m(.7f, -.08f, 0, 0, .11f, .9f, 0, 0, 0, 0, 1, 0,
                       u % 2 ? .47f : -.21f, .17f, 0, .5f);
      transforms[u]->matrix = m;
      receiver->addChild(transforms[u]);
    }
    auto *move = new SoTranslation;
    move->translation.setValue(0, 0, -1);
    receiver->addChild(move);
    image->horAlignment = SoImage::CENTER;
    image->vertAlignment = SoImage::HALF;
    imageBytes.resize(17 * 17 * 4);
    for (int y = 0; y < 17; ++y)
      for (int x = 0; x < 17; ++x) {
        size_t p = (y * 17 + x) * 4;
        imageBytes[p] = uint8_t(220 + (3 * x + y) % 16);
        imageBytes[p + 1] = uint8_t(180 + (x + 5 * y) % 21);
        imageBytes[p + 2] = uint8_t(160 + (x + 2 * y) % 31);
        imageBytes[p + 3] = s.holes && ((x / 3 + y / 3) % 3 == 0) ? 0 : 255;
      }
    image->image.setValue(SbVec2s(17, 17), 4, imageBytes.data());
    receiver->addChild(image);
    group->quality = 1;
    group->epsilon = 0;
    group->threshold = 0;
    for (int i = 0; i < s.maps; ++i)
      casterPositions[i]->translation.setValue(3, 0, .35f);
    paint(true);
  }
  void paint(bool value, int mutation = -1) {
    white = value;
    alternate = mutation;
    for (int u : active) {
      auto p = pixels(u, value, u == mutation, active.size() > 1);
      textures[u]->image.setValue(SbVec2s(8, 8), 4, p.data());
    }
  }
  void isolate(int slot) {
    for (int i = 0; i < setting.maps; ++i) {
      float intensity = slot < 0 ? .6f / setting.maps : (i == slot ? .6f : 0.f);
      if (setting.directional)
        directionals[i]->intensity = intensity;
      else
        spots[i]->intensity = intensity;
    }
  }
  void shadow(int slot, bool blocked) {
    casterPositions[slot]->translation.setValue(blocked ? 0.f : 3.f, 0, .35f);
  }
  std::array<double, 2> st(int u) const {
    const double s = coordinates[u]->factorS.getValue()[3],
                 t = coordinates[u]->factorT.getValue()[3];
    return {
        {double(.7f) * s + double(.11f) * t + 2. * double(u % 2 ? .47f : -.21f),
         double(-.08f) * s + double(.9f) * t + 2. * double(.17f)}};
  }
  std::array<double, 3> product() const {
    std::array<double, 3> out{{1, 1, 1}};
    for (int u : active) {
      const auto v = st(u);
      auto p = pixels(u, white, u == alternate, active.size() > 1);
      // Baseline public quality contract: every filter except NEAREST uses
      // LINEAR magnification, including NEAREST_MIPMAP_LINEAR.
      for (int c = 0; c < 3; ++c)
        out[c] *=
            sample(p, v[0], v[1], c, setting.repeat, setting.quality != 0);
    }
    return out;
  }
};

bool delivery(const RasterScene &s, const CoinRenderFramePlan &frame) {
  CoinWgpuFfiFrame ffi;
  std::string error;
  if (!check(ffi.prepare(frame, side, side, error, false),
             "raster inherited FFI prepare " + error))
    return false;
  const auto &view = ffi.getView();
  bool ok =
      check(view.abi_version == COIN_WGPU_ABI_VERSION &&
                frame.shadowLights.size() == size_t(s.setting.maps),
            "current ABI and all lights remain captured, including intensity zero");
  if (s.rasterStyle->style.getValue() == SoShadowStyle::SHADOWED) {
    const uint32_t sizes[] = {view.shadow_map_size, view.shadow_map_size_second,
                              view.shadow_map_size_third,
                              view.shadow_map_size_fourth};
    for (int slot = 0; slot < s.setting.maps; ++slot) {
      const uint32_t extent =
          slot < 4 ? sizes[slot]
                   : (size_t(slot - 4) < view.extra_shadow_pass_count
                          ? view.extra_shadow_passes[slot - 4].map_size
                          : 0);
      ok &= check(
          extent == 256,
          "every real map reaches FFI, including slots five through eight");
    }
    ok &= check(view.extra_shadow_pass_count ==
                    size_t(std::max(0, s.setting.maps - 4)),
                "exact additional map count");
  }
  unsigned witnesses = 0;
  for (const auto &draw : frame.draws) {
    if (draw.shadowLightSlot || draw.sourceNodeId != s.image->getNodeId())
      continue;
    const auto &raster = frame.renderStates[draw.renderStateSlot];
    if (!raster.rasterPixels)
      continue;
    ++witnesses;
    if (!check(raster.rasterTextureSourceSlot < frame.renderStates.size(),
               "raster source state exists")) {
      ok = false;
      continue;
    }
    const auto &source = frame.renderStates[raster.rasterTextureSourceSlot];
    ok &= check(
        source.textureProjection ==
            (s.rasterStyle->style.getValue() == SoShadowStyle::SHADOWED
                 ? CoinRenderTextureProjection::DIRECT_ST
                 : CoinRenderTextureProjection::PROJECTIVE),
        "effective shadow DIRECT_ST versus ordinary PROJECTIVE classification");
    const auto &gpu = view.states[draw.renderStateSlot];
    const auto &gs = view.states[raster.rasterTextureSourceSlot];
    ok &= check(gs.texture_projection == uint32_t(source.textureProjection),
                "effective projection survives FFI");
    ok &= check(gpu.raster_texture_source_slot ==
                    raster.rasterTextureSourceSlot + 1,
                "FFI source state is one based");
    for (int u = 0; u < 8; ++u) {
      auto layer = coin_render_texture_unit(source, u);
      bool enabled =
          std::find(s.active.begin(), s.active.end(), u) != s.active.end();
      ok &= check(bool(layer.enabled) == enabled,
                  "exact authored active unit mask");
      if (!enabled)
        continue;
      if (!check(layer.imageSlot < frame.textures.size() &&
                     layer.samplerSlot < frame.samplers.size(),
                 "unit owns image and sampler")) {
        ok = false;
        continue;
      }
      const auto &image = frame.textures[layer.imageSlot];
      const auto &sampler = frame.samplers[layer.samplerSlot];
      auto bytes = pixels(u, s.white, u == s.alternate, s.active.size() > 1);
      ok &= check(image.width == 8 && image.height == 8 &&
                      image.pixelsRgba == bytes,
                  "nonconstant per-unit exact base bytes");
      ok &= check(unsigned(sampler.filter) == unsigned(s.setting.quality) &&
                      sampler.wrapS == (s.setting.repeat
                                            ? CoinRenderTextureWrap::REPEAT
                                            : CoinRenderTextureWrap::CLAMP) &&
                      sampler.wrapT == sampler.wrapS,
                  "authored quality and wraps");
      ok &= check(image.mipmapped == (s.setting.quality >= 2),
                  "mip chain present exactly when minification requests it");
      if (s.setting.quality >= 2) {
        std::vector<uint8_t> mip;
        for (int n : {4, 2, 1})
          for (int p = 0; p < n * n; ++p) {
            for (int c = 0; c < 3; ++c)
              mip.push_back(s.white ? 255
                                    : (s.active.size() > 1 ? 226 : 150) -
                                          (u == s.alternate ? 40 : 0));
            mip.push_back(255);
          }
        ok &= check(image.mipmapsRgba == mip,
                    "independent exact lower checker mip levels");
      }
      const SbVec4f raw(s.coordinates[u]->factorS.getValue()[3],
                        s.coordinates[u]->factorT.getValue()[3],
                        s.coordinates[u]->factorR.getValue()[3], 2);
      for (int c = 0; c < 4; ++c)
        ok &= check(raster.rasterTextureCoordinates[u][c] == raw[c] &&
                        gpu.raster_texture_coordinates[u][c] == raw[c],
                    "raw constant STRQ survives Core and FFI");
      const auto matrix = s.transforms[u]->matrix.getValue();
      const float *gm = u ? gs.extra_textures[u - 1].matrix : gs.texture_matrix;
      for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
          ok &= check(layer.matrix[r][c] == matrix[r][c] &&
                          gm[r * 4 + c] == matrix[r][c],
                      "texture matrix retained exactly once on source");
      ok &= check((u ? gs.extra_textures[u - 1].texture_slot
                     : gs.texture_slot) == layer.imageSlot &&
                      (u ? gs.extra_textures[u - 1].sampler_slot
                         : gs.sampler_slot) == layer.samplerSlot,
                  "FFI unit resource identity");
      const auto &tx = view.textures[layer.imageSlot];
      size_t count = bytes.size() + image.mipmapsRgba.size();
      ok &= check(tx.pixel_bytes_len == count && tx.pixels &&
                      std::memcmp(tx.pixels, bytes.data(), bytes.size()) == 0,
                  "FFI base plus mip payload size and bytes");
      if (tx.pixels && image.mipmapsRgba.size() && tx.pixel_bytes_len == count)
        ok &= check(std::memcmp(tx.pixels + bytes.size(),
                                image.mipmapsRgba.data(),
                                image.mipmapsRgba.size()) == 0,
                    "FFI mip bytes intact");
    }
  }
  return check(witnesses > 0,
               "actual SoImage inherited raster draw witnessed") &&
         ok;
}

// Use the established white equivalent primary to separate VSM visibility
// from the independently sampled inherited cascade. The finite, fully lit
// primary is also checked analytically; it is not calibrated from a GPU.
bool imageOracle(const RasterScene &s, const Frame &f, const Frame *white,
                 bool blocked) {
  if (f.rgba.size() != size_t(side * side * 4) ||
      f.depth.size() != size_t(side * side))
    return false;
  unsigned checked = 0, holes = 0, bad = 0;
  double maximum = 0;
  const auto product = s.product();
  // Avoid the one-pixel alignment border; these are authored bitmap interiors.
  for (int y = 58; y <= 69; ++y)
    for (int x = 58; x <= 69; ++x) {
      const int ix = x - 56, iy = 71 - y;
      const size_t source = (iy * 17 + ix) * 4, p = size_t(y * side + x);
      const bool hole = s.imageBytes[source + 3] == 0;
      holes += hole;
      for (int c = 0; c < 3; ++c) {
        // Ordinary DrawPixels uses image PRIMARY. Only the shadow receiver
        // has the authored diffuse illumination factor .6.
        const double primaryFactor =
            s.rasterStyle->style.getValue() == SoShadowStyle::SHADOWED ? .6
                                                                       : 1.;
        double expected =
            blocked || hole ? 0 : s.imageBytes[source + c] * primaryFactor;
        if (white)
          expected = white->rgba[p * 4 + c];
        expected *= product[c];
        const double delta = std::abs(double(f.rgba[p * 4 + c]) - expected);
        maximum = std::max(maximum, delta);
        bad += delta > 3;
      }
      bad += f.rgba[p * 4 + 3] != 255;
      bad += std::abs(f.depth[p] - float(6. / 19.)) > 2e-5f;
      ++checked;
    }
  std::cout << "inherited_image checked=" << checked << " holes=" << holes
            << " bad=" << bad << " max=" << maximum << " blocked=" << blocked
            << '\n';
  return check(
      checked >= 100 && (!s.setting.holes || holes > 20) && bad == 0,
      "independent raster texel product, alpha holes and geometric depth");
}
class Audit : public Proxy {
public:
  unsigned submits = 0;
  explicit Audit(std::unique_ptr<CoinRenderBackend> p) : Proxy(std::move(p)) {}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,
                                CoinRenderTargetP &t) override {
    ++submits;
    return Proxy::submit(f, t);
  }
  CoinRenderSubmitResult
  submit(const CoinRenderFramePlan &f, CoinRenderTargetP &t,
         const CoinRenderFrameReuseDecision &r) override {
    ++submits;
    return Proxy::submit(f, t, r);
  }
};
bool runGpu(const Settings &s, const CoinRenderCapabilities &caps) {
  Harness h(caps);
  if (!h.target)
    return false;
  auto &state = h.target->getPimpl().get();
  auto *audit = new Audit(std::move(state.backend));
  state.backend.reset(audit);
  h.proxy = audit;
  h.action.setTransparencyType(CoinRenderAction::BLEND);
  h.action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  RasterScene scene(s);
  bool ok = true;
  Frame clear, shadow, colored, back;
  auto draw = [&](Frame &f, const char *label) {
    auto count = audit->submits;
    const auto sourceId = scene.root->getNodeId(),
               imageId = scene.image->getNodeId();
    bool pass = h.render(scene.root, f, label);
    pass &= check(sourceId == scene.root->getNodeId() &&
                      imageId == scene.image->getNodeId(),
                  "apply does not mutate authored source nodes");
    return check(audit->submits == count + 1, "one logical GPU submission") &&
           pass && delivery(scene, audit->plan);
  };
  for (int slot = 0; slot < s.maps; ++slot) {
    scene.isolate(slot);
    scene.paint(true);
    scene.shadow(slot, false);
    if (!draw(clear, "white lit slot")) {
      ok = false;
      continue;
    }
    ok &= imageOracle(scene, clear, nullptr, false);
    scene.shadow(slot, true);
    if (draw(shadow, "white shadow slot")) {
      ok &= imageOracle(scene, shadow, nullptr, true);
      unsigned changed = 0;
      for (int y = 58; y <= 69; ++y)
        for (int x = 58; x <= 69; ++x) {
          size_t p = size_t(y * side + x) * 4;
          changed += std::abs(int(clear.rgba[p]) - int(shadow.rgba[p])) > 20;
        }
      ok &= check(changed > 50,
                  "each isolated real map observably shadows the image");
    } else
      ok = false;
    scene.paint(false);
    if (draw(colored, "colored shadow slot"))
      ok &= imageOracle(scene, colored, &shadow, true);
    else
      ok = false;
    scene.shadow(slot, false);
    if (draw(colored, "colored lit slot"))
      ok &= imageOracle(scene, colored, &clear, false);
    else
      ok = false;
    scene.paint(true);
    if (draw(back, "white restored slot"))
      ok &= imageOracle(scene, back, nullptr, false) && exact(clear, back);
    else
      ok = false;
  }
  scene.isolate(-1);
  scene.paint(true);
  if (!draw(clear, "joint white"))
    return false;
  scene.paint(false);
  if (!draw(colored, "joint patterned"))
    return false;
  ok &= imageOracle(scene, colored, &clear, false);
  for (int unit : scene.active) {
    scene.paint(false, unit);
    if (draw(back, "unit bytes changed")) {
      ok &= imageOracle(scene, back, &clear, false);
      unsigned changed = 0;
      for (int y = 58; y <= 69; ++y)
        for (int x = 58; x <= 69; ++x) {
          size_t p = size_t(y * side + x) * 4;
          for (int c = 0; c < 3; ++c)
            changed +=
                std::abs(int(back.rgba[p + c]) - int(colored.rgba[p + c])) > 3;
        }
      ok &= check(changed > 16, "each inherited texture has a visible effect");
    } else
      ok = false;
    scene.paint(false);
    if (draw(back, "unit bytes restore"))
      ok &= imageOracle(scene, back, &clear, false) && exact(colored, back);
    else
      ok = false;
    const auto q = scene.coordinates[unit]->factorQ.getValue(),
               st = scene.coordinates[unit]->factorS.getValue();
    for (int invalid = 0; invalid < 2; ++invalid) {
      // Q divides only on PROJECTIVE. Keep all maps enabled, but author the
      // raster as UNSHADOWED for this control. NaN also covers DIRECT_ST.
      if (invalid == 0) {
        scene.rasterStyle->style = SoShadowStyle::NO_SHADOWING;
        if (!draw(back, "valid PROJECTIVE predecessor"))
          return false;
        ok &= imageOracle(scene, back, nullptr, false);
      }
      if (invalid == 0)
        scene.coordinates[unit]->factorQ.setValue(0, 0, 0, 0);
      else
        scene.coordinates[unit]->factorS.setValue(
            0, 0, 0, std::numeric_limits<float>::quiet_NaN());
      auto count = audit->submits, producers = audit->producers;
      const auto serial = h.target->getLastSubmissionSerial();
      h.action.apply(scene.root);
      Frame failed;
      h.target->readbackRGBA(failed.rgba);
      h.target->readbackDepth(failed.depth);
      ok &=
          check(h.action.getLastStatus() != CoinRenderAction::SUCCESS &&
                    audit->submits == count && audit->producers == producers &&
                    h.target->getLastSubmissionSerial() == serial &&
                    failed.rgba == back.rgba && failed.depth == back.depth,
                "invalid per-unit Q/NaN refuses before commands and preserves "
                "exact publication");
      scene.coordinates[unit]->factorQ = q;
      scene.coordinates[unit]->factorS = st;
      scene.rasterStyle->style = SoShadowStyle::SHADOWED;
      if (draw(back, "invalid coordinate recovery"))
        ok &= imageOracle(scene, back, &clear, false) && exact(colored, back);
      else
        ok = false;
    }
  }
  return ok;
}
// This scene has exactly one physical receiver, the image anchor (0,0,-1).
// Its observer quad is in NDC and the cubes belong to external map scenes;
// neither supplies physical main-group bounds. No artificial plane is added.
bool rasterOnlyPlan(const RasterScene &scene, const CoinRenderFramePlan &frame,
                    CoinRenderShadowPlan &shadows) {
  bool ok = true;
  std::string error;
  uint32_t groupSlot = 0;
  std::vector<uint32_t> receivers;
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const auto &draw = frame.draws[i];
    if (draw.shadowLightSlot || draw.sourceNodeId != scene.image->getNodeId())
      continue;
    if (!check(draw.renderStateSlot < frame.renderStates.size(),
               "raster-only receiver state in range"))
      return false;
    const auto &state = frame.renderStates[draw.renderStateSlot];
    ok &= check(state.rasterPixels && state.rasterShadowAnchorValid,
                "physical raster receiver anchor captured");
    ok &= check(state.rasterShadowAnchorWorld == SbVec3f(0, 0, -1),
                "receiver anchor equals authored translation, not NDC quad");
    if (groupSlot)
      ok &= check(groupSlot == state.shadowGroupSlot,
                  "one authored receiver group");
    groupSlot = state.shadowGroupSlot;
    receivers.push_back(uint32_t(i));
  }
  if (!check(groupSlot && !receivers.empty(),
             "raster-only physical receiver exists"))
    return false;
  SbBox3f bounds;
  if (!check(coin_render_shadow_group_bounds(frame, groupSlot, bounds, error),
             "raster-only physical bounds calculation " + error))
    return false;
  ok &=
      check(!bounds.isEmpty() && bounds.getMin() == SbVec3f(0, 0, -1) &&
                bounds.getMax() == SbVec3f(0, 0, -1),
            "only the physical receiver anchor determines this group's bounds");
  if (!check(coin_render_plan_shadows(frame, shadows, error),
             "raster-only Core shadow plan " + error))
    return false;
  ok &= check(shadows.passes.size() == size_t(scene.setting.maps),
              "all ON lights produce Core passes despite zero intensity");
  std::vector<bool> found(scene.setting.maps, false);
  for (const auto &pass : shadows.passes) {
    if (!check(pass.lightSlot < frame.shadowLights.size(),
               "Core light index in range")) {
      ok = false;
      continue;
    }
    const auto &light = frame.shadowLights[pass.lightSlot];
    size_t authored = scene.setting.maps;
    for (int i = 0; i < scene.setting.maps; ++i) {
      const auto id = scene.setting.directional
                          ? scene.directionals[i]->getNodeId()
                          : scene.spots[i]->getNodeId();
      if (light.sourceRevision == id)
        authored = size_t(i);
    }
    if (!check(authored < size_t(scene.setting.maps),
               "pass resolves to an actual authored light")) {
      ok = false;
      continue;
    }
    ok &= check(!found[authored], "each authored light has exactly one pass");
    found[authored] = true;
    std::vector<uint32_t> casters;
    for (size_t d = 0; d < frame.draws.size(); ++d) {
      const auto &draw = frame.draws[d];
      if (draw.shadowLightSlot != pass.lightSlot + 1)
        continue;
      if (draw.renderStateSlot >= frame.renderStates.size()) {
        ok = false;
        continue;
      }
      const auto &state = frame.renderStates[draw.renderStateSlot];
      ok &= check(
          !state.rasterPixels && (state.shadowStyle & 1u),
          "map-owned caster is original cube geometry, never observer bitmap");
      casters.push_back(uint32_t(d));
    }
    ok &= check(light.enabled && pass.groupSlot == groupSlot && pass.visible &&
                    pass.mapSize == 256,
                "physical raster anchor keeps every actual map visible");
    ok &= check(!casters.empty() && pass.casterDraws == casters &&
                    pass.receiverDraws == receivers,
                "Core caster and receiver lists contain exactly the authored "
                "occurrences");
    std::cout << "inherited_raster_core light=" << authored
              << " visible=" << pass.visible
              << " casters=" << pass.casterDraws.size()
              << " receivers=" << pass.receiverDraws.size()
              << " bounds_empty=" << bounds.isEmpty() << '\n';
  }
  ok &= check(std::find(found.begin(), found.end(), false) == found.end(),
              "all authored map identities witnessed");
  CoinWgpuFfiFrame ffi;
  if (!check(ffi.prepare(frame, side, side, error, false),
             "raster-only Core/FFI shadow prepare " + error))
    return false;
  const auto &view = ffi.getView();
  const uint64_t casterCounts[] = {
      view.shadow_caster_count, view.shadow_caster_count_second,
      view.shadow_caster_count_third, view.shadow_caster_count_fourth};
  const uint64_t receiverCounts[] = {
      view.shadow_receiver_count, view.shadow_receiver_count_second,
      view.shadow_receiver_count_third, view.shadow_receiver_count_fourth};
  for (int i = 0; i < scene.setting.maps; ++i) {
    const bool extra = i >= 4;
    if (extra && !check(size_t(i - 4) < view.extra_shadow_pass_count,
                        "FFI extra raster receiver pass exists")) {
      ok = false;
      continue;
    }
    const uint64_t c =
        extra ? view.extra_shadow_passes[i - 4].caster_count : casterCounts[i];
    const uint64_t r = extra ? view.extra_shadow_passes[i - 4].receiver_count
                             : receiverCounts[i];
    ok &=
        check(c > 0 && r > 0,
              "nonempty raster receiver and real caster reach every FFI pass");
  }
  return ok;
}
bool rasterOnlyCore() {
  bool ok = true;
  for (bool directional : {false, true})
    for (int maps : {1, 4, 8}) {
      Settings settings;
      settings.maps = maps;
      settings.directional = directional;
      RasterScene scene(settings);
      scene.paint(false);
      std::unique_ptr<CoinRenderTarget> target(
          CoinRenderTarget::createOffscreen(SbVec2i32(side, side)));
      if (!check(bool(target), "raster-only Core factory"))
        return false;
      auto *recorder = new Recorder;
      target->getPimpl()->backend.reset(recorder);
      CoinRenderAction action(SbViewportRegion(side, side));
      action.setTransparencyType(CoinRenderAction::BLEND);
      action.setRenderTarget(target.get());
      for (int isolated = 0; isolated < maps; ++isolated) {
        scene.isolate(isolated);
        CoinRenderShadowPlan clear, blocked;
        for (int pose = 0; pose < 2; ++pose) {
          scene.shadow(isolated, pose != 0);
          const auto id = scene.root->getNodeId();
          action.apply(scene.root);
          ok &= check(
              action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  scene.root->getNodeId() == id,
              "raster-only Core capture succeeds without mutating source");
          auto &plan = pose ? blocked : clear;
          ok &= rasterOnlyPlan(scene, recorder->plan, plan) &&
                delivery(scene, recorder->plan);
        }
        if (clear.passes.size() == blocked.passes.size())
          for (size_t i = 0; i < clear.passes.size(); ++i) {
            const auto &a = clear.passes[i];
            const auto &b = blocked.passes[i];
            ok &= check(a.lightSlot == b.lightSlot && a.view == b.view &&
                            a.projectionCoin == b.projectionCoin &&
                            a.nearDistance == b.nearDistance &&
                            a.farDistance == b.farDistance,
                        "moving only an external caster cannot change the "
                        "receiver-derived fit");
          }
        scene.shadow(isolated, false);
      }
      action.setRenderTarget(nullptr);
    }
  return ok;
}
bool core() {
  bool ok = rasterOnlyCore();
  const auto p = pixels(0, false, false, false);
  ok &= check(sample(p, .0625, .0625, 0, true, false) == 60. / 255.,
              "independent nearest centre");
  ok &= check(std::abs(sample(p, 0, 0, 0, true, true) - 150. / 255.) < 1e-12,
              "repeat bilinear seam averages neighbours");
  ok &= check(sample(p, -.2, -.1, 0, false, true) == 60. / 255.,
              "clamp holds edge texel");
  for (int quality = 0; quality < 4; ++quality)
    for (bool repeat : {false, true})
      for (int unit : {0, 3, 7, -1}) {
        Settings s;
        s.quality = quality;
        s.repeat = repeat;
        s.unit = unit;
        RasterScene scene(s);
        std::unique_ptr<CoinRenderTarget> target(
            CoinRenderTarget::createOffscreen(SbVec2i32(side, side)));
        if (!check(bool(target), "Core recorder factory target"))
          return false;
        auto *recorder = new Recorder;
        target->getPimpl()->backend.reset(recorder);
        CoinRenderAction action(SbViewportRegion(side, side));
        action.setTransparencyType(CoinRenderAction::BLEND);
        action.setRenderTarget(target.get());
        scene.paint(false);
        action.apply(scene.root);
        ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS,
                    "Core public raster capture") &&
              delivery(scene, recorder->plan);
        const auto baseline = scene.product();
        for (int u : scene.active) {
          scene.paint(false, u);
          const auto changed = scene.product();
          double contrast = 0;
          for (int c = 0; c < 3; ++c)
            contrast = std::max(contrast,
                                std::abs(changed[c] - baseline[c]) * 160 * .6);
          ok &=
              check(contrast > 4.5, "authored per-unit mutation has calculable "
                                    "RGB contrast at the darkest image value");
        }
        scene.paint(false);
        scene.rasterStyle->style = SoShadowStyle::NO_SHADOWING;
        action.apply(scene.root);
        ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS,
                    "Core positive PROJECTIVE raster") &&
              delivery(scene, recorder->plan);
        for (int u : scene.active) {
          const auto q = scene.coordinates[u]->factorQ.getValue();
          const auto count = recorder->serial;
          scene.coordinates[u]->factorQ.setValue(0, 0, 0, 0);
          action.apply(scene.root);
          ok &= check(action.getLastStatus() != CoinRenderAction::SUCCESS &&
                          recorder->serial == count,
                      "Core PROJECTIVE zero Q fails before recorder submit");
          scene.coordinates[u]->factorQ = q;
          action.apply(scene.root);
          ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS,
                      "Core PROJECTIVE Q recovery") &&
                delivery(scene, recorder->plan);
        }
        action.setRenderTarget(nullptr);
      }
  return ok;
}
} // namespace inherited_sampling
int main(int argc, char **argv) {
  using namespace inherited_sampling;
  configureSamplingTest(argc, argv);
  Settings s;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--core")
      s.core = true;
    else if (arg == "--gpu")
      s.core = false;
    else if (arg == "--holes")
      s.holes = true;
    else if (arg == "--portable-sampling") {
    } else if ((arg == "--maps" || arg == "--unit" || arg == "--quality" ||
                arg == "--wrap" || arg == "--light") &&
               i + 1 < argc) {
      std::string value = argv[++i];
      if (arg == "--maps") {
        if (value != "1" && value != "4" && value != "8")
          return 2;
        s.maps = std::atoi(value.c_str());
      } else if (arg == "--unit") {
        if (value != "all" && value != "0" && value != "3" && value != "7")
          return 2;
        s.unit = value == "all" ? -1 : std::atoi(value.c_str());
      } else if (arg == "--quality") {
        if (value != "0" && value != "1" && value != "2" && value != "3")
          return 2;
        s.quality = std::atoi(value.c_str());
      } else if (arg == "--wrap") {
        if (value != "repeat" && value != "clamp")
          return 2;
        s.repeat = value == "repeat";
      } else {
        if (value != "spot" && value != "directional")
          return 2;
        s.directional = value == "directional";
      }
    } else
      return 2;
  }
  SoDB::init();
  CoinRenderAction::initClass();
  if (s.core)
    return core() ? 0 : 1;
  const auto caps =
      msaa_shadow_test::capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN);
  if (!caps.gpu_available || caps.renderer == COIN_RENDER_RENDERER_UNKNOWN)
    return 77;
  return runGpu(s, caps) ? 0 : 1;
}
