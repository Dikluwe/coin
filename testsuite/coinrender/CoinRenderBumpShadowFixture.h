
#ifndef COIN_RENDER_BUMP_SHADOW_FIXTURE_H
#define COIN_RENDER_BUMP_SHADOW_FIXTURE_H
#include "CoinRenderBumpProfileFixture.h"
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTranslation.h>
#include <chrono>
#include <functional>
#include <set>
namespace bump_shadow {
using bump_profile::check;
using bump_profile::exact;
using bump_profile::Frame;
using bump_profile::RGB;
constexpr unsigned side = bump_profile::side;
inline double clamp(double v) { return std::max(0., std::min(1., v)); }
// Reuse only the already-authored public R26 rectangle and tangent convention.
// The extension's visibility/compositing equations below are independent.
struct Scene : bump_profile::Scene {
  SoShadowGroup *group = new SoShadowGroup;
  SoGroup *extras = new SoGroup;
  RGB rttTexel{{-1, -1, -1}};
  RGB customTexel[8];
  int models[8];
  int combineOp = -1;
  bool combinePrimary = false;
  RGB combineConstant{{.25, .5, .75}};
  std::function<RGB(unsigned, unsigned)> sampled[8];
  std::function<RGB(unsigned, unsigned)> normalAt;
  RGB pose{{0, 0, 0}};
  double angle = 0;
  RGB localAt(double x, double y) const {
    const double cs = std::cos(angle), sn = std::sin(angle);
    x -= pose[0];
    y -= pose[1];
    return {{cs * x + sn * y, -sn * x + cs * y, 0}};
  }
  SoShadowDirectionalLight *directional = new SoShadowDirectionalLight;
  SoShadowStyle *style = new SoShadowStyle;
  SoSwitch *bumpStage = new SoSwitch;
  SoTranslation *caster = new SoTranslation;
  SoTexture3 *volume[8]{};
  SoTextureCubeMap *cube[8]{};
  SoTexture2 *extra2D[8]{};
  SoTextureCoordinate3 *coords[8]{};
  SoTextureMatrixTransform *matrices[8]{};
  RGB lamp{{.8, .7, .9}}, spec{{.24, .12, .3}};
  double intensity = .75, shine = .25, casterX = 0;
  bool colored = true, srgb = false, receives = true;
  unsigned mask = 255;
  bool only2D = false;
  explicit Scene(unsigned units = 255, bool all2D = false)
      : bump_profile::Scene(false), mask(units | 1u), only2D(all2D) {
    for (unsigned u = 0; u < 8; ++u) {
      customTexel[u] = {{-1, -1, -1}};
      models[u] = SoTexture2::MODULATE;
    }
    while (root->getNumChildren() > 1) {
      auto *node = root->getChild(1);
      node->ref();
      root->removeChild(1);
      group->addChild(node);
      node->unref();
    }
    root->addChild(group);
    group->removeChild(light);
    light = directional;
    group->insertChild(directional, 0);
    group->insertChild(style, 1);
    style->style = SoShadowStyle::SHADOWED;
    direction({{0, 0, 1}});
    directional->color.setValue(float(lamp[0]), float(lamp[1]), float(lamp[2]));
    directional->intensity = float(intensity);
    directional->bboxCenter.setValue(0, 0, 0);
    directional->bboxSize.setValue(4, 4, 8);
    group->quality = 1.f;
    group->precision = .125f;
    group->epsilon = 0;
    group->threshold = 0;
    group->intensity = 1.f;
    auto *map = new SoSeparator;
    auto *cast = new SoShadowStyle;
    cast->style = SoShadowStyle::CASTS_SHADOW;
    map->addChild(cast);
    caster->translation.setValue(0, 0, .75f);
    map->addChild(caster);
    auto *box = new SoCube;
    box->width = .7f;
    box->height = .7f;
    box->depth = .5f;
    map->addChild(box);
    directional->shadowMapScene = map;
    int index = group->findChild(bump);
    bump->ref();
    group->removeChild(bump);
    bumpStage->addChild(bump);
    bump->unref();
    bumpStage->whichChild = SO_SWITCH_ALL;
    group->insertChild(bumpStage, index);
    group->insertChild(extras, index);
    for (unsigned u = 1; u < 8; ++u)
      if (mask & (1u << u)) {
        auto *select = new SoTextureUnit;
        select->unit = int(u);
        extras->addChild(select);
        if (!only2D && (u == 2 || u == 5 || u == 7)) {
          volume[u] = new SoTexture3;
          volume[u]->wrapS = SoTexture3::CLAMP;
          volume[u]->wrapT = SoTexture3::CLAMP;
          volume[u]->wrapR = SoTexture3::CLAMP;
          extras->addChild(volume[u]);
        } else if (!only2D && (u == 1 || u == 4)) {
          cube[u] = new SoTextureCubeMap;
          cube[u]->wrapS = SoTextureCubeMap::CLAMP;
          cube[u]->wrapT = SoTextureCubeMap::CLAMP;
          extras->addChild(cube[u]);
        } else {
          extra2D[u] = new SoTexture2;
          extra2D[u]->wrapS = SoTexture2::CLAMP;
          extra2D[u]->wrapT = SoTexture2::CLAMP;
          extras->addChild(extra2D[u]);
        }
        coords[u] = new SoTextureCoordinate3;
        raw(u, volume[u] ? SbVec3f(.5f / 3, .5f / 5, 6.5f / 7)
               : cube[u] ? SbVec3f(0, 0, 1)
                         : SbVec3f(.5f, .5f, 0));
        extras->addChild(coords[u]);
        matrices[u] = new SoTextureMatrixTransform;
        extras->addChild(matrices[u]);
      }
    auto *restore = new SoTextureUnit;
    restore->unit = 0;
    extras->addChild(restore);
    for (unsigned i = 0; i < 4; ++i)
      baseUv->point.set1Value(i, SbVec2f(.5, .5));
    const int32_t indices[] = {0, 1, 2, -1, 0, 2, 3, -1};
    mesh->textureCoordIndex.setValues(0, 8, indices);
    setNormal({{154, 110, 246}});
    setDiffuse({{.7, .55, .45}});
    setSpec(spec);
    paint(true);
  }
  void setSpec(RGB v) {
    spec = v;
    material->specularColor.setValue(float(v[0]), float(v[1]), float(v[2]));
    material->shininess = float(shine);
  }
  void raw(unsigned u, const SbVec3f &v) {
    for (unsigned i = 0; i < 4; ++i)
      coords[u]->point.set1Value(i, v);
  }
  static RGB bytes(unsigned u, unsigned layer) {
    return {{double(204 + (u * 7 + layer * 11) % 46),
             double(208 + (u * 9 + layer * 13) % 42),
             double(200 + (u * 5 + layer * 17) % 50)}};
  }
  void paint(bool tint) {
    colored = tint;
    auto zero = bytes(0, 0);
    setBase({{uint8_t(tint ? zero[0] : 255), uint8_t(tint ? zero[1] : 255),
              uint8_t(tint ? zero[2] : 255), 255}});
    for (unsigned u = 1; u < 8; ++u)
      if (mask & (1u << u)) {
        if (volume[u]) {
          std::vector<unsigned char> pixels(3 * 5 * 7 * 4);
          for (unsigned z = 0; z < 7; ++z)
            for (unsigned i = 0; i < 15; ++i) {
              auto color = bytes(u, z);
              for (unsigned c = 0; c < 3; ++c)
                pixels[(z * 15 + i) * 4 + c] = uint8_t(tint ? color[c] : 255);
              pixels[(z * 15 + i) * 4 + 3] = 255;
            }
          volume[u]->images.setValue(SbVec3s(3, 5, 7), 4, pixels.data());
          volume[u]->model = SoTexture3::MODULATE;
        } else if (cube[u]) {
          SoSFImage *faces[] = {&cube[u]->imagePosX, &cube[u]->imageNegX,
                                &cube[u]->imagePosY, &cube[u]->imageNegY,
                                &cube[u]->imagePosZ, &cube[u]->imageNegZ};
          for (unsigned f = 0; f < 6; ++f) {
            auto color = bytes(u, f);
            unsigned char pixels[16];
            for (unsigned i = 0; i < 4; ++i) {
              for (unsigned c = 0; c < 3; ++c)
                pixels[i * 4 + c] = uint8_t(tint ? color[c] : 255);
              pixels[i * 4 + 3] = 255;
            }
            faces[f]->setValue(SbVec2s(2, 2), 4, pixels);
          }
          cube[u]->model = SoTextureCubeMap::MODULATE;
        } else {
          auto color = bytes(u, 0);
          unsigned char pixel[] = {uint8_t(tint ? color[0] : 255),
                                   uint8_t(tint ? color[1] : 255),
                                   uint8_t(tint ? color[2] : 255), 255};
          extra2D[u]->image.setValue(SbVec2s(1, 1), 4, pixel);
          extra2D[u]->model = SoTexture2::MODULATE;
        }
      }
  }
  void casterAt(double x) {
    casterX = x;
    caster->translation.setValue(float(x), 0, .75f);
  }
  RGB texel(unsigned u) const {
    if (customTexel[u][0] >= 0)
      return customTexel[u];
    if (u == 3 && rttTexel[0] >= 0)
      return rttTexel;
    unsigned layer = 0;
    if (u) {
      const auto authored = coords[u]->point[0];
      SbVec4f st;
      matrices[u]->matrix.getValue().multVecMatrix(
          SbVec4f(authored[0], authored[1], authored[2], 1), st);
      if (volume[u]) {
        double r = st[2];
        if (!receives)
          r /= st[3];
        layer = unsigned(std::max(0., std::min(6., std::floor(r * 7))));
      } else if (cube[u]) {
        double x = st[0], y = st[1], z = st[2], ax = std::abs(x),
               ay = std::abs(y), az = std::abs(z);
        layer = ax >= ay && ax >= az ? (x >= 0 ? 0 : 1)
                : ay >= az           ? (y >= 0 ? 2 : 3)
                                     : (z >= 0 ? 4 : 5);
      }
    }
    RGB result = colored ? bytes(u, layer) : RGB{{255, 255, 255}};
    for (auto &v : result) {
      v /= 255;
      if (srgb)
        v = bump_profile::decode(v);
    }
    return result;
  }
  RGB predicted(unsigned x, unsigned y, double visibility,
                bool activeBump = true, unsigned width = side,
                unsigned height = side) const {
    // Authored triangles 012/023: interpolate normalized per-corner E, then
    // normalize L+E. Legacy signed tangent axes are S=+X,T=-Y,N=+Z.
    double px = (x + .5) * 4 / width - 2, py = 2 - (y + .5) * 4 / height, u = 0,
           v = 0;
    const auto local = localAt(px, py);
    u = (local[0] + 1.4) / 2.7;
    v = (local[1] + 1.3) / 2.65;
    unsigned ids[3];
    double w[3];
    if (v <= u) {
      ids[0] = 0;
      ids[1] = 1;
      ids[2] = 2;
      w[0] = 1 - u;
      w[1] = u - v;
      w[2] = v;
    } else {
      ids[0] = 0;
      ids[1] = 2;
      ids[2] = 3;
      w[0] = 1 - v;
      w[1] = u;
      w[2] = v - u;
    }
    RGB eye{{0, 0, 0}};
    for (unsigned k = 0; k < 3; ++k) {
      auto p = positions->point[ids[k]];
      const double cs = std::cos(angle), sn = std::sin(angle);
      const double wx = cs * p[0] - sn * p[1] + pose[0];
      const double wy = sn * p[0] + cs * p[1] + pose[1];
      const auto worldEye =
          bump_profile::normalized({{-wx, -wy, 4 - p[2] - pose[2]}});
      const RGB e{{worldEye[0] * cs + worldEye[1] * sn,
                   worldEye[0] * sn - worldEye[1] * cs, worldEye[2]}};
      for (unsigned c = 0; c < 3; ++c)
        eye[c] += w[k] * e[c];
    }
    const RGB encoded =
        normalAt
            ? normalAt(x, y)
            : RGB{{double(normal[0]), double(normal[1]), double(normal[2])}};
    const RGB n = activeBump ? RGB{{encoded[0] * 2. / 255 - 1,
                                    encoded[1] * 2. / 255 - 1,
                                    encoded[2] * 2. / 255 - 1}}
                             : RGB{{0, 0, 1}},
              half{{eye[0], eye[1], 1 + eye[2]}};
    const double d = clamp(n[2]),
                 specFactor = std::pow(clamp(bump_profile::dot(
                                           n, bump_profile::normalized(half))),
                                       shine * 64);
    RGB primary;
    for (unsigned c = 0; c < 3; ++c)
      primary[c] = clamp(visibility * diffuse[c] * lamp[c] * intensity * d);
    const RGB originalPrimary = primary;
    for (unsigned unit = 0; unit < 8; ++unit)
      if (mask & (1u << unit)) {
        const auto tex = sampled[unit] ? sampled[unit](x, y) : texel(unit);
        const RGB previous = primary;
        if (unit == 7 && combineOp >= 0) {
          const RGB a = combinePrimary ? originalPrimary : previous;
          double dot = 0;
          for (unsigned c = 0; c < 3; ++c)
            dot += 4 * (tex[c] - .5) * (combineConstant[c] - .5);
          for (unsigned c = 0; c < 3; ++c) {
            double value = 0;
            if (combineOp == SoTextureCombine::REPLACE)
              value = a[c];
            else if (combineOp == SoTextureCombine::MODULATE)
              value = a[c] * tex[c];
            else if (combineOp == SoTextureCombine::ADD)
              value = a[c] + tex[c];
            else if (combineOp == SoTextureCombine::ADD_SIGNED)
              value = a[c] + tex[c] - .5;
            else if (combineOp == SoTextureCombine::SUBTRACT)
              value = a[c] - tex[c];
            else if (combineOp == SoTextureCombine::INTERPOLATE)
              value =
                  a[c] * combineConstant[c] + tex[c] * (1 - combineConstant[c]);
            else
              value = dot;
            primary[c] = clamp(value);
          }
        } else
          for (unsigned c = 0; c < 3; ++c) {
            if (models[unit] == SoTexture2::MODULATE)
              primary[c] *= tex[c];
            else if (models[unit] == SoTexture2::BLEND)
              primary[c] =
                  primary[c] * (1 - tex[c]) + RGB{{.2, .4, .6}}[c] * tex[c];
            else
              primary[c] = tex[c]; // Opaque DECAL and REPLACE.
            primary[c] = clamp(primary[c]);
          }
      }
    for (unsigned c = 0; c < 3; ++c)
      primary[c] = 255 * clamp(primary[c] + visibility * spec[c] * specFactor);
    return primary;
  }
};
class Audit : public bump_profile::Proxy {
public:
  unsigned rootSubmits = 0;
  bool failRoot = false;
  CoinRenderRttPlan graph;
  explicit Audit(std::unique_ptr<CoinRenderBackend> real)
      : bump_profile::Proxy(std::move(real)) {}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,
                                CoinRenderTargetP &t) override {
    if (failRoot)
      return {CoinRenderBackendStatus::OUT_OF_MEMORY,
              "independent late combined root OOM after RTT preparation", 0};
    ++rootSubmits;
    return bump_profile::Proxy::submit(f, t);
  }
  CoinRenderSubmitResult
  submit(const CoinRenderFramePlan &f, CoinRenderTargetP &t,
         const CoinRenderFrameReuseDecision &r) override {
    if (failRoot)
      return {CoinRenderBackendStatus::OUT_OF_MEMORY,
              "independent late combined root OOM after RTT preparation", 0};
    ++rootSubmits;
    return bump_profile::Proxy::submit(f, t, r);
  }
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g,
                                      const CoinRenderFramePlan &f,
                                      const SbVec2i32 &size) const override {
    const_cast<Audit *>(this)->graph = g;
    return bump_profile::Proxy::preflightRtt(g, f, size);
  }
  void retireRtt(const std::vector<uint64_t> &tokens) override {
    real->retireRtt(tokens);
  }
};
struct Harness {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(side, side)};
  Audit *audit = nullptr;
  Harness(
      const CoinRenderCapabilities &caps, unsigned samples, bool srgb = false,
      CoinRenderSceneTextureMode route = COIN_RENDER_SCENE_TEXTURE_STAGED,
      CoinRenderRasterizationPolicy raster = COIN_RENDER_RASTER_NATIVE,
      CoinRenderTransparencyMode mechanism = COIN_RENDER_TRANSPARENCY_COIN) {
    auto o = samplingTestOptions();
    o.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
    o.sampleCount = samples;
    o.sceneTexture = route;
    o.rasterizationPolicy = raster;
    o.transparency = mechanism;
    if (srgb)
      o.storedTextureColorSpace = COIN_RENDER_TEXTURE_SRGB;
    std::cout << "bump_shadow_target_phase create-begin samples=" << samples
              << std::endl;
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side, side), o));
    std::cout << "bump_shadow_target_phase create-end" << std::endl;
    if (target) {
      auto &p = target->getPimpl().get();
      std::cout << "bump_shadow_target_phase prepare-begin" << std::endl;
      if (p.prepareBackend() != CoinRenderBackendStatus::SUCCESS ||
          !p.backend || !p.backend->isGpuBackend()) {
        target.reset();
        return;
      }
      std::cout << "bump_shadow_target_phase prepare-end" << std::endl;
      audit = new Audit(std::move(p.backend));
      p.backend.reset(audit);
      target->setDepthReadbackEnabled(TRUE);
      action.setRenderTarget(target.get());
    }
    action.setTransparencyType(CoinRenderAction::NONE);
    action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  }
  ~Harness() { action.setRenderTarget(nullptr); }
  bool render(SoNode *root, Frame &f, const std::string &phase) {
    if (!check(bool(target), "requested real GPU"))
      return false;
    auto old = target->getLastSubmissionSerial();
    auto count = audit->rootSubmits;
    const auto begin = std::chrono::steady_clock::now();
    std::cout << "bump_shadow_apply_begin phase=" << phase << " serial=" << old
              << " root_submits=" << count << std::endl;
    action.apply(root);
    target->readbackRGBA(f.rgba);
    target->readbackDepth(f.depth);
    f.serial = target->getLastSubmissionSerial();
    std::cout << "bump_shadow phase=" << phase
              << " status=" << action.getLastStatus() << " serial=" << old
              << ',' << f.serial << " apply_ms="
              << std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - begin)
                     .count()
              << " root_submits=" << audit->rootSubmits - count
              << " error=" << action.getLastError().getString() << '\n';
    return check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                     f.serial > old && audit->rootSubmits == count + 1 &&
                     f.rgba.size() == side * side * 4 &&
                     f.depth.size() == side * side,
                 phase);
  }
  bool reject(SoNode *root, const Frame &stable, const std::string &phase) {
    auto serial = target->getLastSubmissionSerial();
    auto count = audit->rootSubmits, producers = audit->producers;
    action.apply(root);
    Frame f;
    target->readbackRGBA(f.rgba);
    target->readbackDepth(f.depth);
    std::cout << "bump_shadow_refusal phase=" << phase
              << " status=" << action.getLastStatus()
              << " error=" << action.getLastError().getString() << '\n';
    return check(action.getLastStatus() != CoinRenderAction::SUCCESS &&
                     action.getLastError().getLength() > 0 &&
                     target->getLastSubmissionSerial() == serial &&
                     audit->rootSubmits == count &&
                     audit->producers == producers && f.rgba == stable.rgba &&
                     f.depth == stable.depth,
                 phase + " zero submissions and unchanged publication");
  }
};
inline bool oracle(const Scene &s, const Frame &f, bool bumps = true,
                   bool receive = true) {
  if (!check(f.rgba.size() == side * side * 4 && f.depth.size() == side * side,
             "complete oracle image"))
    return false;
  unsigned lit = 0, dark = 0, bad = 0, depthBad = 0;
  int maximum = 0;
  for (unsigned y = 20; y < side - 20; ++y)
    for (unsigned x = 20; x < side - 20; ++x) {
      double px = (x + .5) * 4 / side - 2, py = 2 - (y + .5) * 4 / side,
             dx = std::abs(px - s.casterX), dy = std::abs(py), V = 1;
      const auto local = s.localAt(px, py);
      if (local[0] < -1.28 || local[0] > 1.18 || local[1] < -1.18 ||
          local[1] > 1.23)
        continue;
      if (receive) {
        if (dx < .18 && dy < .18) {
          V = 0;
          ++dark;
        } else if (std::max(dx, dy) > .65)
          ++lit;
        else
          continue;
      } else
        ++lit;
      auto e = s.predicted(x, y, V, bumps);
      size_t p = size_t(y * side + x);
      for (unsigned c = 0; c < 3; ++c) {
        int delta = std::abs(int(f.rgba[4 * p + c]) - int(std::lround(e[c])));
        maximum = std::max(maximum, delta);
        if (delta > 3 && bad++ < 4)
          std::cerr << "bump_shadow_first xy=" << x << ',' << y
                    << " channel=" << c << " V=" << V << " expected=" << e[c]
                    << " actual=" << unsigned(f.rgba[4 * p + c]) << '\n';
      }
      bad += f.rgba[p * 4 + 3] != 255;
      depthBad += std::abs(f.depth[p] - float((3 - s.pose[2]) / 8)) > 1e-6;
    }
  std::cout << "bump_shadow_oracle lit=" << lit << " occluded=" << dark
            << " bad=" << bad << " max=" << maximum << " depth_bad=" << depthBad
            << '\n';
  return check(lit > 500 && (!receive || dark > 20) && bad == 0 &&
                   depthBad == 0,
               "independent decoded N, corner L/E, pow64, V0/V1 geometry, "
               "texture cascade and post-stack specular");
}
inline unsigned difference(const Frame &a, const Frame &b) {
  unsigned n = 0;
  for (size_t i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i)
    if (i % 4 < 3)
      n += unsigned(std::abs(int(a.rgba[i]) - int(b.rgba[i])));
  return n;
}
} // namespace bump_shadow
#endif
