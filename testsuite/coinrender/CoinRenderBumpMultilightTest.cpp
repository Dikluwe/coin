#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderBumpShadowFixture.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoScale.h>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>

namespace multilamp {
using bump_profile::check;
using bump_profile::dot;
using bump_profile::exact;
using bump_profile::Frame;
using bump_profile::normalized;
using bump_profile::RGB;
constexpr unsigned side = bump_profile::side;
double clamp(double v) { return std::max(0., std::min(1., v)); }
struct Settings {
  int lights = 8, maps = 4;
  unsigned samples = 1;
  int depthMode = -1;
  bool general = true, baseline = false;
  std::string mode = "lighting";
};
struct Context {
  double x = 0, y = 0, z = 0, scale = 1, angle = 0;
  bool bump = true;
  RGB diffuse{{.7, .55, .45}};
};
struct Scene : bump_profile::Scene {
  Settings setting;
  SoSeparator *receiver = new SoSeparator;
  SoGroup *prefix = new SoGroup;
  SoGroup *geometry = new SoGroup;
  SoShadowGroup *group = nullptr;
  SoShadowStyle *style = nullptr;
  SoSwitch *bumpSwitch = new SoSwitch;
  std::vector<SoDirectionalLight *> lamps;
  std::vector<SoShadowDirectionalLight *> maps;
  std::vector<SoTranslation *> casters;
  std::vector<SoGroup *> owners;
  SoTexture2 *extra[8]{};
  SoMaterialBinding *binding = new SoMaterialBinding;
  SoDepthBuffer *authoredDepth = nullptr;
  std::vector<Context> contexts{Context()};
  std::vector<RGB> directions, colors;
  std::vector<double> powers, casterX;
  RGB spec{{.04, .035, .045}};
  double shine = .18;
  bool twoMaterials = false, patternNormal = false, patternTexture = false;
  bool heightNormal = false;
  double rawOffset = 0;
  std::array<uint8_t, 3> normalBytes{{154, 110, 246}};
  std::array<uint8_t, 4> texture[8];
  explicit Scene(const Settings &s) : bump_profile::Scene(false), setting(s) {
    prefix->ref();
    geometry->ref();
    if (s.depthMode >= 0) {
      authoredDepth = new SoDepthBuffer;
      authoredDepth->test = TRUE;
      authoredDepth->write = TRUE;
      authoredDepth->function =
          s.depthMode ? SoDepthBuffer::LEQUAL : SoDepthBuffer::LESS;
      authoredDepth->range.setValue(0, 1);
      prefix->addChild(authoredDepth);
    }
    root->removeChild(light);
    light = nullptr;
    while (root->getNumChildren() > 1) {
      auto *n = root->getChild(1);
      n->ref();
      root->removeChild(1);
      prefix->addChild(n);
      n->unref();
    }
    positions->ref();
    prefix->removeChild(positions);
    geometry->addChild(positions);
    positions->unref();
    mesh->ref();
    prefix->removeChild(mesh);
    geometry->addChild(mesh);
    mesh->unref();
    bump->ref();
    int index = prefix->findChild(bump);
    prefix->removeChild(bump);
    bumpSwitch->addChild(bump);
    bump->unref();
    bumpSwitch->whichChild = SO_SWITCH_ALL;
    prefix->insertChild(bumpSwitch, index);
    prefix->insertChild(binding, prefix->findChild(material) + 1);
    binding->value = SoMaterialBinding::OVERALL;
    receiver->addChild(prefix);
    receiver->addChild(geometry);
    if (s.maps) {
      group = new SoShadowGroup;
      group->precision = .125f;
      group->quality = 1;
      group->epsilon = 0;
      group->threshold = 0;
      group->intensity = 1;
    }
    for (int i = 0; i < s.lights; ++i) {
      double a = .73 * i + .2;
      RGB l = normalized({{.15 * std::cos(a), .15 * std::sin(a), 1}});
      RGB color{
          {.72 + .025 * (i % 5), .83 - .027 * (i % 4), .76 + .021 * (i % 6)}};
      directions.push_back(l);
      colors.push_back(color);
      powers.push_back(.7 / s.lights);
      SoDirectionalLight *node = nullptr;
      const bool mapped = i >= s.lights - s.maps;
      if (mapped) {
        auto *shadow = new SoShadowDirectionalLight;
        node = shadow;
        shadow->bboxCenter.setValue(0, 0, 0);
        shadow->bboxSize.setValue(4, 4, 8);
        auto *source = new SoSeparator;
        auto *cast = new SoShadowStyle;
        cast->style = SoShadowStyle::CASTS_SHADOW;
        source->addChild(cast);
        auto *position = new SoTranslation;
        position->translation.setValue(0, 0, .75f);
        source->addChild(position);
        auto *box = new SoCube;
        box->width = box->height = .7f;
        box->depth = .5f;
        source->addChild(box);
        shadow->shadowMapScene = source;
        maps.push_back(shadow);
        casters.push_back(position);
        casterX.push_back(0);
        group->addChild(shadow);
        owners.push_back(group);
      } else {
        node = new SoDirectionalLight;
        root->addChild(node);
        owners.push_back(root);
      }
      node->ref();
      lamps.push_back(node);
      node->direction.setValue(float(-l[0]), float(-l[1]), float(-l[2]));
      node->color.setValue(float(color[0]), float(color[1]), float(color[2]));
      node->intensity = float(powers.back());
    }
    if (group) {
      root->addChild(group);
      style = new SoShadowStyle;
      style->style = SoShadowStyle::SHADOWED;
      group->addChild(style);
      group->addChild(receiver);
    } else
      root->addChild(receiver);
    light = lamps.front();
    setNormal(normalBytes);
    material->shininess = float(shine);
    setSpec(spec);
    setDiffuse(contexts[0].diffuse);
    for (int u = 0; u < 8; ++u) {
      texture[u] = {
          {uint8_t(234 - u * 2), uint8_t(240 - u), uint8_t(230 + u), 255}};
      if (u == 0) {
        setBase(texture[0]);
        continue;
      }
      if (!s.general)
        continue;
      auto *select = new SoTextureUnit;
      select->unit = u;
      prefix->insertChild(select, prefix->findChild(bumpSwitch));
      extra[u] = new SoTexture2;
      extra[u]->model = SoTexture2::MODULATE;
      extra[u]->wrapS = extra[u]->wrapT = SoTexture2::CLAMP;
      extra[u]->image.setValue(SbVec2s(1, 1), 4, texture[u].data());
      prefix->insertChild(extra[u], prefix->findChild(bumpSwitch));
      auto *uvs = new SoTextureCoordinate2;
      for (int k = 0; k < 4; ++k)
        uvs->point.set1Value(k, SbVec2f(.31f, .67f));
      prefix->insertChild(uvs, prefix->findChild(bumpSwitch));
    }
    auto *zero = new SoTextureUnit;
    zero->unit = 0;
    prefix->insertChild(zero, prefix->findChild(bumpSwitch));
  }
  ~Scene() {
    prefix->unref();
    geometry->unref();
    for (auto *lamp : lamps)
      lamp->unref();
  }
  bool used(size_t i) const {
    return lamps[i]->on.getValue() && owners[i]->findChild(lamps[i]) >= 0;
  }
  void setSpec(RGB value) {
    spec = value;
    material->specularColor.setValue(float(value[0]), float(value[1]),
                                     float(value[2]));
  }
  void setNormal(std::array<uint8_t, 3> value) {
    normalBytes = value;
    heightNormal = false;
    patternNormal = false;
    bump_profile::Scene::setNormal(value);
  }
  void power(int i, double value) {
    powers[i] = value;
    lamps[i]->intensity = float(value);
  }
  void isolate(int selected) {
    for (int i = 0; i < setting.lights; ++i)
      power(i, selected < 0 ? .7 / setting.lights : (i == selected ? .7 : 0));
  }
  void casterAt(int i, double value) {
    casterX[i] = value;
    casters[i]->translation.setValue(float(value), 0, .75f);
  }
  void textureAt(int u, std::array<uint8_t, 4> value) {
    texture[u] = value;
    if (!u)
      setBase(value);
    else
      extra[u]->image.setValue(SbVec2s(1, 1), 4, value.data());
  }
  void faceMaterials(bool enabled) {
    twoMaterials = enabled;
    if (enabled) {
      binding->value = SoMaterialBinding::PER_FACE;
      const SbColor d[] = {SbColor(.72, .32, .57), SbColor(.28, .75, .43)},
                    s[] = {SbColor(.05, .015, .04), SbColor(.025, .055, .02)};
      const float h[] = {.12f, .31f};
      material->diffuseColor.setValues(0, 2, d);
      material->specularColor.setValues(0, 2, s);
      material->shininess.setValues(0, 2, h);
    } else {
      binding->value = SoMaterialBinding::OVERALL;
      material->diffuseColor.setValue(float(contexts[0].diffuse[0]),
                                      float(contexts[0].diffuse[1]),
                                      float(contexts[0].diffuse[2]));
      material->specularColor.setValue(float(spec[0]), float(spec[1]),
                                       float(spec[2]));
      material->shininess = float(shine);
    }
  }
  void layout(int mode, bool reverse = false, bool overlap = false) {
    receiver->removeAllChildren();
    contexts.clear();
    for (int i = 0; i < 3; ++i) {
      Context c;
      c.x = overlap ? 0 : i - 1;
      c.scale = overlap ? .65 : .35;
      c.z = overlap ? .2 * i : 0;
      c.bump = mode == 0 ? i != 1 : i == 1;
      c.diffuse = i == 0   ? RGB{{.75, .4, .25}}
                  : i == 1 ? RGB{{.25, .7, .4}}
                           : RGB{{.35, .3, .8}};
      contexts.push_back(c);
    }
    for (int j = 0; j < 3; ++j) {
      int i = reverse ? 2 - j : j;
      const auto &c = contexts[i];
      auto *branch = new SoSeparator;
      auto *move = new SoTranslation;
      move->translation.setValue(float(c.x), float(c.y), float(c.z));
      branch->addChild(move);
      auto *scale = new SoScale;
      scale->scaleFactor.setValue(float(c.scale), float(c.scale),
                                  float(c.scale));
      branch->addChild(scale);
      branch->addChild(prefix);
      if (!c.bump)
        branch->addChild(new SoBumpMap);
      auto *m = new SoMaterial;
      m->diffuseColor.setValue(float(c.diffuse[0]), float(c.diffuse[1]),
                               float(c.diffuse[2]));
      m->ambientColor.setValue(0, 0, 0);
      m->emissiveColor.setValue(0, 0, 0);
      m->specularColor.setValue(0, 0, 0);
      branch->addChild(m);
      branch->addChild(geometry);
      receiver->addChild(branch);
    }
  }
};
// Independent 2x1 periodic bilinear normal texture, including the public bump
// matrix. Native tangent handedness is S=+X,T=-Y; RGB normal is NOT normalized.
RGB normalAt(const Scene &s, double u) {
  if (!s.patternNormal)
    return {{s.normalBytes[0] * 2. / 255 - 1, s.normalBytes[1] * 2. / 255 - 1,
             s.normalBytes[2] * 2. / 255 - 1}};
  double t = (u + s.rawOffset) * s.matrix->scaleFactor.getValue()[0] +
             s.matrix->translation.getValue()[0];
  t -= std::floor(t);
  double x = 2 * t - .5;
  int ix = int(std::floor(x));
  double f = x - ix;
  const RGB texels[] = {RGB{{204, 128, 230}}, RGB{{52, 182, 225}}};
  RGB result;
  for (int c = 0; c < 3; ++c)
    result[c] = 2 *
                    ((1 - f) * texels[(ix % 2 + 2) % 2][c] +
                     f * texels[((ix + 1) % 2 + 2) % 2][c]) /
                    255. -
                1;
  return result;
}
int visibility(const Scene &s, int light, double x, double y) {
  if (light < s.setting.lights - s.setting.maps)
    return 1;
  int m = light - (s.setting.lights - s.setting.maps);
  if (s.casterX[m] > 2)
    return 1;
  const auto l = s.directions[light];
  const double dx = std::abs(x - (s.casterX[m] - .75 * l[0] / l[2])),
               dy = std::abs(y + .75 * l[1] / l[2]);
  if (dx < .16 && dy < .16)
    return 0;
  double extent =
      .35 + .25 * std::max(std::abs(l[0] / l[2]), std::abs(l[1] / l[2]));
  if (dx > extent + .17 || dy > extent + .17)
    return 1;
  return -1;
}
RGB expected(const Scene &s, const Context &context, double localX,
             double localY, int triangle, const double *w, const unsigned *ids,
             bool &known, double worldX, double worldY) {
  RGB eye{{0, 0, 0}};
  for (int k = 0; k < 3; ++k) {
    auto p = s.positions->point[ids[k]];
    double cs = std::cos(context.angle), sn = std::sin(context.angle);
    double cameraX = (-context.x) / context.scale,
           cameraY = (-context.y) / context.scale;
    RGB e = normalized({{cs * cameraX + sn * cameraY - p[0],
                         -(-sn * cameraX + cs * cameraY - p[1]),
                         (4 - context.z) / context.scale - p[2]}});
    for (int c = 0; c < 3; ++c)
      eye[c] += w[k] * e[c];
  }
  const auto n =
      context.bump ? normalAt(s, (localX + 1.4) / 2.7) : RGB{{0, 0, 1}};
  RGB diffuse = context.diffuse, spec = s.spec;
  double shine = s.shine;
  if (s.twoMaterials) {
    diffuse = triangle == 0 ? RGB{{.72, .32, .57}} : RGB{{.28, .75, .43}};
    // SoCallbackAction indexes diffuse/transparency only. Coin's lazy
    // specular and shininess are inherited values, not PER_FACE arrays.
    spec = RGB{{.05, .015, .04}};
    shine = .12;
  }
  RGB d{{0, 0, 0}}, sp{{0, 0, 0}};
  for (int i = 0; i < s.setting.lights; ++i) {
    if (!s.used(i))
      continue;
    double V = visibility(s, i, worldX, worldY);
    if (V < 0) {
      if (s.powers[i] == 0 && spec == RGB{{0, 0, 0}})
        continue;
      known = false;
      return d;
    }
    auto L = s.directions[i];
    double cs = std::cos(context.angle), sn = std::sin(context.angle);
    L = {{cs * L[0] + sn * L[1], sn * L[0] - cs * L[1], L[2]}};
    L = normalized(L);
    RGB half{{L[0] + eye[0], L[1] + eye[1], L[2] + eye[2]}};
    double D = clamp(dot(n, L)),
           S = std::pow(clamp(dot(n, normalized(half))),
                        shine * (context.bump ? 64 : 128));
    for (int c = 0; c < 3; ++c) {
      d[c] += V * diffuse[c] * s.colors[i][c] * s.powers[i] * D;
      sp[c] += V * spec[c] * S;
    }
  }
  for (int c = 0; c < 3; ++c) {
    d[c] = clamp(d[c]);
    for (int u = 0; u < (s.setting.general ? 8 : 1); ++u)
      d[c] *= s.texture[u][c] / 255.;
    d[c] = 255 * clamp(d[c] + sp[c]);
  }
  return d;
}
bool oracle(const Scene &s, const Frame &frame, unsigned extent = side,
            bool withDepth = true, bool report = true) {
  if (!check(frame.rgba.size() == extent * extent * 4 &&
                 (!withDepth || frame.depth.size() == extent * extent),
             "multilight complete image"))
    return false;
  unsigned count = 0, bad = 0, depthBad = 0, lit = 0, dark = 0;
  int maximum = 0;
  for (unsigned y = 8; y < extent - 8; ++y)
    for (unsigned x = 8; x < extent - 8; ++x) {
      double wx = (x + .5) * 4 / extent - 2, wy = 2 - (y + .5) * 4 / extent;
      const Context *chosen = nullptr;
      double lx = 0, ly = 0;
      for (const auto &c : s.contexts) {
        double cs = std::cos(c.angle), sn = std::sin(c.angle),
               dx = (wx - c.x) / c.scale, dy = (wy - c.y) / c.scale;
        double a = cs * dx + sn * dy, b = -sn * dx + cs * dy;
        if (a > -1.28 && a < 1.18 && b > -1.18 && b < 1.23 &&
            (!chosen || c.z > chosen->z)) {
          chosen = &c;
          lx = a;
          ly = b;
        }
      }
      if (!chosen)
        continue;
      const double u = (lx + 1.4) / 2.7, v = (ly + 1.3) / 2.65;
      unsigned ids[3];
      double w[3];
      int triangle = v <= u ? 0 : 1;
      if (!triangle) {
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
      // A material discontinuity is an authored triangle boundary, not an
      // interior witness. Leave the existing 2.1-pixel safety margin intact.
      if (s.twoMaterials &&
          std::abs(v - u) / std::sqrt(1 / (2.7 * 2.7) + 1 / (2.65 * 2.65)) *
                  chosen->scale <
              2.1 * 4 / extent)
        continue;
      bool known = true;
      auto e = expected(s, *chosen, lx, ly, triangle, w, ids, known, wx, wy);
      if (!known)
        continue;
      bool zero = true;
      for (int c = 0; c < 3; ++c)
        zero &= e[c] < 1;
      dark += zero;
      lit += !zero;
      size_t p = size_t(y * extent + x);
      for (int c = 0; c < 3; ++c) {
        int delta =
            std::abs(int(frame.rgba[p * 4 + c]) - int(std::lround(e[c])));
        maximum = std::max(maximum, delta);
        if (delta > 3 && bad++ < 4 && report)
          std::cerr << "multilight_first xy=" << x << ',' << y << " c=" << c
                    << " expected=" << e[c]
                    << " actual=" << unsigned(frame.rgba[p * 4 + c]) << '\n';
      }
      bad += frame.rgba[p * 4 + 3] != 255;
      if (withDepth)
        depthBad +=
            std::abs(frame.depth[p] - float((3 - chosen->z) / 8)) > 2e-5f;
      ++count;
    }
  if (report)
    std::cout << "multilight_oracle lights=" << s.setting.lights
              << " maps=" << s.setting.maps << " pixels=" << count
              << " lit=" << lit << " dark=" << dark << " bad=" << bad
              << " max=" << maximum << " depth_bad=" << depthBad << '\n';
  bool emitting = false;
  for (size_t i = 0; i < s.lamps.size(); ++i)
    emitting |= s.used(i) && (s.powers[i] > 0 || s.spec != RGB{{0, 0, 0}});
  if (!report)
    return count > 150 && (emitting ? lit > 100 : dark > 150) && bad == 0 &&
           depthBad == 0;
  return check(count > 150 && (emitting ? lit > 100 : dark > 150) && bad == 0 &&
                   depthBad == 0,
               "independent ordered light sum, per-light V, pow64/128, texture "
               "cascade, alpha/depth");
}
template <class Material>
bool authoredFaceMaterial(const Material &m, int face) {
  const float diffuse[2][3] = {{.72f, .32f, .57f}, {.28f, .75f, .43f}};
  const float specular[3] = {.05f, .015f, .04f};
  bool ok = m.shininess == .12f;
  for (int c = 0; c < 3; ++c)
    ok &= m.diffuse[c] == diffuse[face][c] && m.specular[c] == specular[c];
  return ok;
}
bool delivery(const Scene &s, const CoinRenderFramePlan &frame,
              unsigned extent = side) {
  CoinWgpuFfiFrame ffi;
  std::string error;
  const bool prepared = ffi.prepareBumpFrame(frame, extent, extent, error);
  if (!check(prepared, "multilight FFI bump prepare " + error))
    return false;
  const auto &view = ffi.getView();
  bool ok =
      check(view.abi_version == 63, "baseline private version63 is explicit");
  std::set<uint64_t> authored;
  for (size_t i = 0; i < s.lamps.size(); ++i)
    if (s.used(i))
      authored.insert(s.lamps[i]->getNodeId());
  unsigned witnesses = 0;
  std::set<uint32_t> materials;
  bool seenFaces[2] = {false, false};
  bool seenFfiFaces[2] = {false, false};
  std::vector<CoinRenderBumpGpuVertex> bumpStream;
  ok &= check(coin_render_build_bump_gpu_stream(frame, bumpStream, error),
              "Core bump stream construction " + error);
  for (const auto &draw : frame.draws) {
    if (draw.shadowLightSlot || draw.sourceNodeId != s.mesh->getNodeId())
      continue;
    ++witnesses;
    const auto &state = frame.renderStates[draw.renderStateSlot];
    if (!check(state.lightingSlot < frame.lightingStates.size(),
               "multilight state owns lighting")) {
      ok = false;
      continue;
    }
    bool active = false;
    for (const auto &c : s.contexts)
      if (std::abs(state.model[3][0] - c.x) < 1e-6 &&
          std::abs(state.model[3][1] - c.y) < 1e-6 &&
          std::abs(state.model[3][2] - c.z) < 1e-6)
        active =
            c.bump && s.bumpSwitch->whichChild.getValue() != SO_SWITCH_NONE;
    for (uint32_t vi = draw.geometry.firstVertex;
         vi < draw.geometry.firstVertex + draw.geometry.vertexCount; ++vi)
      ok &= check((vi < bumpStream.size() && bumpStream[vi].imageSlot != 0) ==
                      active,
                  "Core per-occurrence bump stream eligibility");
    for (uint64_t di = 0; di < view.draw_count; ++di)
      if (view.draws[di].render_state_slot == draw.renderStateSlot) {
        const auto &d = view.draws[di];
        for (uint64_t vi = d.first_vertex;
             vi < uint64_t(d.first_vertex) + d.vertex_count; ++vi)
          ok &= check((vi < view.bump_vertex_count &&
                       view.bump_vertices[vi].image_slot != 0) == active,
                      "FFI actual shared-shape occurrence bump eligibility");
      }
    const auto &lights = frame.lightingStates[state.lightingSlot].lights;
    const auto &gs = view.states[draw.renderStateSlot];
    if (s.setting.depthMode >= 0) {
      const auto expected = s.setting.depthMode
                                ? CoinRenderDepthFunction::LEQUAL
                                : CoinRenderDepthFunction::LESS;
      ok &= check(s.authoredDepth && s.authoredDepth->test.getValue() &&
                      s.authoredDepth->write.getValue() &&
                      s.authoredDepth->function.getValue() ==
                          (s.setting.depthMode ? SoDepthBuffer::LEQUAL
                                               : SoDepthBuffer::LESS),
                  "explicit authorial depth node test/write/function");
      ok &= check(state.depthTest && state.depthWrite &&
                      state.depthFunction == expected &&
                      state.depthRange[0] == 0 && state.depthRange[1] == 1,
                  "actual draw retains authored LESS/LEQUAL and default depth "
                  "range in Core");
      ok &= check(
          gs.depth_test == 1 && gs.depth_write == 1 &&
              gs.depth_function == uint32_t(expected) &&
              gs.depth_range[0] == 0 && gs.depth_range[1] == 1,
          "actual FFI draw retains authored LESS/LEQUAL, test/write/range");
    }

    ok &= check(lights.size() == authored.size() &&
                    gs.light_count == authored.size(),
                "every enabled light reaches actual draw and FFI");
    std::set<uint64_t> actual;
    for (size_t i = 0; i < lights.size(); ++i) {
      const auto &light = lights[i];
      actual.insert(light.sourceRevision);
      size_t source = s.lamps.size();
      for (size_t j = 0; j < s.lamps.size(); ++j)
        if (s.lamps[j]->getNodeId() == light.sourceRevision)
          source = j;
      if (!check(source < s.lamps.size(),
                 "each captured light resolves to author source")) {
        ok = false;
        continue;
      }
      for (int c = 0; c < 3; ++c) {
        ok &= check(std::abs(light.direction[c] + s.directions[source][c]) <
                            1e-6 &&
                        gs.lights[i].direction_cutoff[c] == light.direction[c],
                    "independent light direction and FFI ordering");
        ok &= check(std::abs(light.color[c] - s.colors[source][c]) < 1e-6 &&
                        gs.lights[i].color_intensity[c] == light.color[c],
                    "per-light color reaches FFI");
      }
      ok &= check(std::abs(light.intensity - s.powers[source]) < 1e-6 &&
                      gs.lights[i].color_intensity[3] == light.intensity,
                  "per-light intensity reaches FFI");
    }
    ok &= check(actual == authored, "no dropped or reused light identity");
    for (unsigned u = 0; u < (s.setting.general ? 8u : 1u); ++u) {
      auto layer = coin_render_texture_unit(state, u);
      ok &= check(layer.enabled && layer.imageSlot < frame.textures.size(),
                  "every authored inherited texture bound");
      if (layer.enabled && layer.imageSlot < frame.textures.size()) {
        const auto &image = frame.textures[layer.imageSlot];
        ok &=
            check(image.pixelsRgba == std::vector<uint8_t>(s.texture[u].begin(),
                                                           s.texture[u].end()),
                  "independent unit bytes retained");
      }
    }
    for (uint32_t v = draw.geometry.firstVertex;
         v < draw.geometry.firstVertex + draw.geometry.vertexCount; ++v) {
      if (v >= frame.vertices.size()) {
        ok = false;
        break;
      }
      const uint32_t slot = frame.vertices[v].materialSlot;
      materials.insert(slot);
      if (!check(slot < frame.materials.size() && slot < view.material_count,
                 "corner material slot exists in both packets")) {
        ok = false;
        continue;
      }
      const auto &m = frame.materials[slot];
      const auto &gm = view.materials[slot];
      ok &= check(m.shininess == gm.shininess,
                  "shininess retained per material slot");
      for (int c = 0; c < 3; ++c)
        ok &= check(m.diffuse[c] == gm.diffuse[c] &&
                        m.specular[c] == gm.specular[c],
                    "diffuse/specular retained per corner material slot");
    }
  }
  if (s.twoMaterials) {
    for (const auto &draw : frame.draws)
      if (!draw.shadowLightSlot && draw.sourceNodeId == s.mesh->getNodeId()) {
        for (uint32_t t = 0; t + 2 < draw.geometry.indexCount; t += 3) {
          uint32_t ids[3];
          double x = 0, y = 0;
          bool valid = true;
          for (int k = 0; k < 3; ++k) {
            size_t index = size_t(draw.geometry.firstIndex) + t + k;
            if (index >= frame.indices.size()) {
              valid = false;
              break;
            }
            ids[k] = frame.indices[index];
            if (ids[k] >= frame.vertices.size()) {
              valid = false;
              break;
            }
            x += frame.vertices[ids[k]].position[0] / 3.;
            y += frame.vertices[ids[k]].position[1] / 3.;
          }
          if (!check(valid, "material witness triangle indices valid")) {
            ok = false;
            continue;
          }
          const int face = (y + 1.3) / 2.65 > (x + 1.4) / 2.7 ? 1 : 0;
          seenFaces[face] = true;
          for (int k = 0; k < 3; ++k) {
            uint32_t slot = frame.vertices[ids[k]].materialSlot;
            ok &= check(slot < frame.materials.size() &&
                            authoredFaceMaterial(frame.materials[slot], face),
                        "CPU authored face diffuse and inherited first "
                        "specular/shininess");
          }
        }
      }
    for (uint64_t di = 0; di < view.draw_count; ++di) {
      const auto &d = view.draws[di];
      bool owns = false;
      for (const auto &sourceDraw : frame.draws)
        owns |= !sourceDraw.shadowLightSlot &&
                sourceDraw.sourceNodeId == s.mesh->getNodeId() &&
                sourceDraw.renderStateSlot == d.render_state_slot;
      if (!owns)
        continue;
      for (uint32_t t = 0; t + 2 < d.index_count; t += 3) {
        uint32_t ids[3];
        double x = 0, y = 0;
        bool valid = true;
        for (int k = 0; k < 3; ++k) {
          uint64_t index = uint64_t(d.first_index) + t + k;
          if (index >= view.index_count) {
            valid = false;
            break;
          }
          ids[k] = view.indices[index];
          if (ids[k] >= view.vertex_count) {
            valid = false;
            break;
          }
          x += view.vertices[ids[k]].position[0] / 3.;
          y += view.vertices[ids[k]].position[1] / 3.;
        }
        if (!check(valid, "FFI material witness triangle indices valid")) {
          ok = false;
          continue;
        }
        const int face = (y + 1.3) / 2.65 > (x + 1.4) / 2.7 ? 1 : 0;
        seenFfiFaces[face] = true;
        for (int k = 0; k < 3; ++k) {
          uint32_t slot = view.vertices[ids[k]].material_slot;
          ok &= check(slot < view.material_count &&
                          authoredFaceMaterial(view.materials[slot], face),
                      "FFI actual corner retains native effective material, "
                      "not flattened or reindexed specular");
        }
      }
    }
    ok &= check(seenFaces[0] && seenFaces[1] && seenFfiFaces[0] &&
                    seenFfiFaces[1],
                "both authored material faces witnessed independently in Core "
                "and actual FFI triangles");
  }
  if (s.twoMaterials)
    ok &= check(
        materials.size() >= 2,
        "per-face/corner materials are not flattened within the shared draw");
  if (s.setting.maps) {
    CoinRenderShadowPlan plan;
    if (!check(coin_render_plan_shadows(frame, plan, error),
               "multilight shadow identity Core plan " + error))
      return false;
    size_t activeMaps = 0;
    for (auto *map : s.maps)
      activeMaps += map->on.getValue() != FALSE && s.group->findChild(map) >= 0;
    ok &= check(plan.passes.size() == activeMaps,
                "all enabled directional maps retained");
    for (const auto &pass : plan.passes) {
      ok &= check(pass.lightSlot < frame.shadowLights.size(),
                  "map light slot in range");
      if (pass.lightSlot >= frame.shadowLights.size())
        continue;
      auto id = frame.shadowLights[pass.lightSlot].sourceRevision;
      bool found = false;
      for (auto *map : s.maps)
        found |= id == map->getNodeId();
      ok &= check(found && pass.visible && !pass.casterDraws.empty() &&
                      !pass.receiverDraws.empty(),
                  "real map owns caster/receiver and directional identity");
      for (uint32_t d : pass.receiverDraws) {
        if (d >= frame.draws.size() ||
            frame.draws[d].sourceNodeId != s.mesh->getNodeId())
          continue;
        const auto slot = frame.draws[d].renderStateSlot;
        const auto &state = frame.renderStates[slot];
        const auto &lights = frame.lightingStates[state.lightingSlot].lights;
        int expectedIndex = -1;
        for (size_t i = 0; i < lights.size(); ++i)
          if (lights[i].sourceRevision == id)
            expectedIndex = int(i);
        ok &= check(
            expectedIndex >= 0 && slot < pass.lightingIndexByState.size() &&
                pass.lightingIndexByState[slot] == expectedIndex,
            "VSM resolves actual per-draw light index, including high slots");
      }
    }
  }
  return check(witnesses > 0, "actual shared mesh delivery witnessed") && ok;
}
class Recorder : public CoinRenderBackend {
public:
  CoinRenderFramePlan frame;
  uint64_t submits = 0;
  std::string error;
  bool isGpuBackend() const override { return true; }
  bool supportsOpaqueBump() const override { return true; }
  bool supportsOffscreenShadows() const override { return true; }
  CoinRenderBackendStatus getStatus() const override {
    return CoinRenderBackendStatus::SUCCESS;
  }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override {
    return CoinRenderBackendStatus::SUCCESS;
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,
                                CoinRenderTargetP &) override {
    frame = f;
    return {CoinRenderBackendStatus::SUCCESS, "", ++submits};
  }
  void poll() override {}
  const std::string &getLastError() const override { return error; }
};
bool capture(const Settings &settings) {
  Scene scene(settings);
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(side, side)));
  if (!check(bool(target), "Core-only public target factory"))
    return false;
  auto *recorder = new Recorder;
  target->getPimpl()->backend.reset(recorder);
  CoinRenderAction action(SbViewportRegion(side, side));
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setRenderTarget(target.get());
  action.apply(scene.root);
  bool ok = check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                      recorder->submits == 1,
                  "Core-only multilight public capture") &&
            delivery(scene, recorder->frame);
  const auto before = recorder->submits;
  auto direction = scene.lamps.back()->direction.getValue();
  scene.lamps.back()->direction.setValue(
      std::numeric_limits<float>::quiet_NaN(), 0, -1);
  action.apply(scene.root);
  ok &= check(action.getLastStatus() != CoinRenderAction::SUCCESS &&
                  recorder->submits == before,
              "last malformed light rejected before recorder submission");
  scene.lamps.back()->direction = direction;
  action.apply(scene.root);
  ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  recorder->submits == before + 1,
              "Core-only light recovery") &&
        delivery(scene, recorder->frame);
  scene.faceMaterials(true);
  const auto beforeMaterials = recorder->submits;
  action.apply(scene.root);
  ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  recorder->submits == beforeMaterials + 1,
              "CPU capture of asymmetric native PER_FACE material") &&
        delivery(scene, recorder->frame);
  action.setRenderTarget(nullptr);
  return ok;
}
unsigned changed(const Frame &a, const Frame &b) {
  unsigned n = 0;
  for (size_t p = 0; p < a.rgba.size() && p < b.rgba.size(); ++p)
    if (p % 4 < 3 && std::abs(int(a.rgba[p]) - int(b.rgba[p])) > 3)
      ++n;
  return n;
}
// A Native MSAA restoration checks authorial inputs, not old edge bytes.
// This fingerprint is specific to this public fixture and contains no cache,
// backend output, node revision, or captured plan data.
std::string inputs(const Scene &s) {
  std::ostringstream out;
  out << std::setprecision(17);
  auto vec2 = [&](const SbVec2f &v) { out << v[0] << ',' << v[1] << ';'; };
  auto vec3 = [&](const SbVec3f &v) {
    out << v[0] << ',' << v[1] << ',' << v[2] << ';';
  };
  for (size_t i = 0; i < s.lamps.size(); ++i) {
    out << s.lamps[i]->on.getValue() << ','
        << s.owners[i]->findChild(s.lamps[i]) << ','
        << s.lamps[i]->intensity.getValue() << ';';
    vec3(s.lamps[i]->direction.getValue());
    vec3(s.lamps[i]->color.getValue());
  }
  for (auto *c : s.casters)
    vec3(c->translation.getValue());
  auto image = [&](const SoSFImage &field) {
    SbVec2s n;
    int c = 0;
    auto *p = field.getValue(n, c);
    out << n[0] << ',' << n[1] << ',' << c << ':';
    if (p && n[0] > 0 && n[1] > 0 && c > 0)
      out.write(reinterpret_cast<const char *>(p), size_t(n[0]) * n[1] * c);
    out << ';';
  };
  image(s.bump->image);
  image(s.base->image);
  for (auto *t : s.extra)
    if (t)
      image(t->image);
  out << s.bump->wrapS.getValue() << ',' << s.bump->wrapT.getValue() << ','
      << s.bumpSwitch->whichChild.getValue() << ','
      << s.binding->value.getValue() << ';';
  for (int i = 0; i < s.material->diffuseColor.getNum(); ++i)
    vec3(s.material->diffuseColor[i]);
  out << '/';
  for (int i = 0; i < s.material->specularColor.getNum(); ++i)
    vec3(s.material->specularColor[i]);
  out << '/';
  for (int i = 0; i < s.material->shininess.getNum(); ++i)
    out << s.material->shininess[i] << ',';
  out << s.material->transparency[0] << ';';
  for (int i = 0; i < s.uv->point.getNum(); ++i)
    vec2(s.uv->point[i]);
  vec2(s.matrix->translation.getValue());
  vec2(s.matrix->scaleFactor.getValue());
  vec2(s.matrix->center.getValue());
  out << s.matrix->rotation.getValue() << ';';
  for (const auto &c : s.contexts) {
    out << c.x << ',' << c.y << ',' << c.z << ',' << c.scale << ',' << c.angle
        << ',' << c.bump << ';';
    for (auto v : c.diffuse)
      out << v << ',';
  }
  if (s.authoredDepth)
    out << "depth:" << s.authoredDepth->test.getValue() << ','
        << s.authoredDepth->write.getValue() << ','
        << s.authoredDepth->function.getValue() << ','
        << s.authoredDepth->range.getValue()[0] << ','
        << s.authoredDepth->range.getValue()[1] << ';';
  out << s.twoMaterials << s.patternNormal << s.heightNormal << s.rawOffset;
  return out.str();
}
struct Run {
  Scene scene;
  bump_shadow::Harness harness;
  Frame initial;
  std::map<uint64_t, std::string> authored;
  explicit Run(const Settings &s, const CoinRenderCapabilities &caps)
      : scene(s), harness(caps, s.samples) {}
  bool draw(Frame &f, const std::string &label) {
    const auto id = scene.root->getNodeId(), normalId = scene.bump->getNodeId();
    bool ok = harness.render(scene.root, f, label);
    ok &= check(scene.root->getNodeId() == id &&
                    scene.bump->getNodeId() == normalId,
                "apply does not mutate authored sources");
    ok &=
        check(harness.target->getOptions().sampleCount == scene.setting.samples,
              "requested sample count retained");
    if (ok)
      authored[f.serial] = inputs(scene);
    return ok && delivery(scene, harness.audit->plan) && oracle(scene, f);
  }
  bool refuse(SoNode *root, const Frame &stable, const std::string &label) {
    if (scene.setting.samples == 1)
      return harness.reject(root, stable, label);
    const auto serial = harness.target->getLastSubmissionSerial();
    const auto count = harness.audit->rootSubmits,
               producers = harness.audit->producers;
    harness.action.apply(root);
    Frame failed;
    harness.target->readbackRGBA(failed.rgba);
    harness.target->readbackDepth(failed.depth);
    const bool transparency = label.find("transparent") != std::string::npos;
    return check(
        harness.action.getLastStatus() != CoinRenderAction::SUCCESS &&
            harness.action.getLastError().getLength() &&
            harness.target->getLastSubmissionSerial() == serial &&
            harness.audit->producers == producers &&
            harness.audit->rootSubmits >= count &&
            harness.audit->rootSubmits <= count + (transparency ? 1 : 0) &&
            failed.rgba == stable.rgba && failed.depth == stable.depth,
        label + " exact refusal preservation, no producers or GPU publication");
  }
  bool restore(Frame &out, const Frame &reference, const std::string &label) {
    const auto found = authored.find(reference.serial);
    const bool sourceRestored =
        found != authored.end() && inputs(scene) == found->second;
    if (scene.setting.samples == 1)
      return draw(out, label) && exact(reference, out);
    return check(sourceRestored,
                 "Native MSAA restoration has identical authorial inputs") &&
           draw(out, label);
  }
  bool equivalent(const Frame &reference, const Frame &current) {
    if (scene.setting.samples == 1)
      return exact(reference, current);
    return check(current.serial > reference.serial &&
                     harness.target->getOptions().sampleCount ==
                         scene.setting.samples,
                 "new Native MSAA logical execution") &&
           delivery(scene, harness.audit->plan) && oracle(scene, current);
  }
};
bool lighting(Run &r) {
  auto &s = r.scene;
  Frame a, b, back;
  bool ok = true;
  const auto spec = s.spec;
  s.setSpec({{0, 0, 0}});
  for (int i = 0; i < s.setting.lights; ++i) {
    s.isolate(i);
    if (!r.draw(a, "isolated directional contribution")) {
      ok = false;
      continue;
    }
    const auto direction = s.directions[i];
    s.directions[i] = normalized({{-.4, .3, 1}});
    auto L = s.directions[i];
    s.lamps[i]->direction.setValue(float(-L[0]), float(-L[1]), float(-L[2]));
    if (r.draw(b, "isolated light direction mutation"))
      ok &= check(changed(a, b) > 16,
                  "every light direction visibly contributes to bump");
    else
      ok = false;
    s.directions[i] = direction;
    s.lamps[i]->direction.setValue(float(-direction[0]), float(-direction[1]),
                                   float(-direction[2]));
    ok &= r.restore(back, a, "isolated direction A/B/A");
    s.lamps[i]->on = FALSE;
    if (s.setting.lights > 1)
      ok &= r.draw(b, "one light disabled, all other authored lights retained");
    else
      ok &= r.refuse(s.root, a, "zero consumed lights explicitly refused");
    s.lamps[i]->on = TRUE;
    ok &= r.restore(back, a, "disabled light recovery");
    const int authoredIndex = s.owners[i]->findChild(s.lamps[i]);
    s.owners[i]->removeChild(s.lamps[i]);
    if (s.setting.lights > 1)
      ok &= r.draw(b, "one consumed light physically removed");
    else
      ok &= r.refuse(s.root, a, "physical removal leaves zero lights");
    s.owners[i]->insertChild(s.lamps[i], authoredIndex);
    ok &= r.restore(back, a, "physical light removal A/B/A");
  }
  s.isolate(-1);
  s.setSpec(spec);
  ok &= r.restore(back, r.initial, "joint specular64 and diffuse sum restored");
  auto normal = s.normalBytes;
  s.setNormal({{210, 145, 205}});
  if (r.draw(b, "joint decoded normal mutation"))
    ok &= check(changed(r.initial, b) > 100,
                "normal map affects the summed lighting");
  else
    ok = false;
  s.setNormal(normal);
  ok &= r.restore(back, r.initial, "normal A/B/A");
  // Preserve authorial ordering while changing it independently in each scope.
  // Every light is retained explicitly during detach/reinsert.
  for (auto *owner :
       {static_cast<SoGroup *>(s.root), static_cast<SoGroup *>(s.group)}) {
    if (!owner)
      continue;
    std::vector<std::pair<int, SoDirectionalLight *>> nodes;
    for (size_t i = 0; i < s.lamps.size(); ++i)
      if (s.owners[i] == owner)
        nodes.push_back({owner->findChild(s.lamps[i]), s.lamps[i]});
    for (auto node : nodes)
      owner->removeChild(node.second);
    for (size_t j = 0; j < nodes.size(); ++j)
      owner->insertChild(nodes[nodes.size() - 1 - j].second,
                         nodes.front().first + int(j));
    if (!nodes.empty()) {
      ok &= r.draw(b, "lights reordered without changing their sources");
      for (auto node : nodes)
        owner->removeChild(node.second);
      for (auto node : nodes)
        owner->insertChild(node.second, node.first);
      ok &= r.restore(back, r.initial, "authorial light order restored");
    }
  }
  return ok;
}
bool maps(Run &r) {
  auto &s = r.scene;
  bool ok = true;
  Frame a, b, back;
  auto spec = s.spec;
  s.setSpec({{0, 0, 0}});
  for (size_t m = 0; m < s.maps.size(); ++m) {
    int light = s.setting.lights - s.setting.maps + int(m);
    s.isolate(light);
    for (size_t j = 0; j < s.maps.size(); ++j)
      s.casterAt(int(j), 3);
    if (!r.draw(a, "isolated map clear control")) {
      ok = false;
      continue;
    }
    s.casterAt(int(m), 0);
    if (r.draw(b, "only associated map caster enters"))
      ok &= check(changed(a, b) > 16,
                  "every map including last affects its associated light");
    else
      ok = false;
    s.casterAt(int(m), 3);
    ok &= r.restore(back, a, "associated map caster A/B/A");
  }
  for (size_t m = 0; m < s.maps.size(); ++m)
    s.casterAt(int(m), 0);
  s.isolate(-1);
  s.setSpec(spec);
  return r.restore(back, r.initial,
                   "all maps and lights restore their exact associations") &&
         ok;
}
bool textures(Run &r) {
  auto &s = r.scene;
  Frame b, back;
  bool ok = true;
  for (int u = 0; u < (s.setting.general ? 8 : 1); ++u) {
    auto original = s.texture[u];
    auto altered = original;
    for (int c = 0; c < 3; ++c)
      altered[c] = uint8_t(original[c] - 70);
    s.textureAt(u, altered);
    if (r.draw(b, "one independent inherited texture changes"))
      ok &= check(changed(r.initial, b) > 16,
                  "each texture unit has an observable contribution");
    else
      ok = false;
    s.textureAt(u, original);
    ok &= r.restore(back, r.initial, "texture unit A/B/A");
  }
  s.faceMaterials(true);
  ok &= r.draw(b, "asymmetric materials per face/corner within shared draw");
  s.faceMaterials(false);
  return r.restore(back, r.initial, "material slots restored") && ok;
}
bool transform(Run &r) {
  auto &s = r.scene;
  Frame a, b, back;
  bool ok = true;
  // Uniform model scale plus noncommuting rotation/translation; eye/light
  // vectors are derived in object space, then projected onto signed S/T/N.
  s.receiver->removeChild(s.geometry);
  auto *branch = new SoSeparator;
  auto *move = new SoTranslation;
  move->translation.setValue(.12f, -.08f, .15f);
  branch->addChild(move);
  auto *rotation = new SoRotation;
  rotation->rotation.setValue(SbVec3f(0, 0, 1), .27f);
  branch->addChild(rotation);
  auto *scale = new SoScale;
  scale->scaleFactor.setValue(.8f, .8f, .8f);
  branch->addChild(scale);
  branch->addChild(s.geometry);
  s.receiver->addChild(branch);
  s.contexts[0].x = .12;
  s.contexts[0].y = -.08;
  s.contexts[0].z = .15;
  s.contexts[0].angle = .27;
  s.contexts[0].scale = .8;
  ok &= r.draw(a, "model-space L/E with rotation translation and scale");
  const unsigned char texels[] = {204, 128, 230, 52, 182, 225};
  s.bump->image.setValue(SbVec2s(2, 1), 3, texels);
  s.patternNormal = true;
  s.matrix->scaleFactor.setValue(.125f, .125f);
  s.matrix->translation.setValue(.125f, .25f);
  ok &= r.draw(a, "authored bump UV matrix selects first bilinear footprint");
  s.matrix->translation.setValue(.625f, .25f);
  if (r.draw(b, "authored bump matrix changes footprint"))
    ok &= check(changed(a, b) > 100,
                "bump matrix changes multilight illumination");
  else
    ok = false;
  s.matrix->translation.setValue(.125f, .25f);
  ok &= r.restore(back, a, "bump matrix A/B/A");
  for (int i = 0; i < 4; ++i) {
    auto uv = s.uv->point[i];
    uv[0] += 4;
    s.uv->point.set1Value(i, uv);
  } // Same .5 transformed shift as the matrix mutation.
  // The reference rawS changes by the authored offset as well.
  s.rawOffset = 4;
  ok &= r.draw(b, "authored UV selects the second footprint");
  for (int i = 0; i < 4; ++i) {
    auto uv = s.uv->point[i];
    uv[0] -= 4;
    s.uv->point.set1Value(i, uv);
  }
  s.rawOffset = 0;
  ok &= r.restore(back, a, "UV authorial A/B/A");
  s.setNormal({{154, 110, 246}});
  s.matrix->scaleFactor.setValue(1, 1);
  s.matrix->translation.setValue(0, 0);
  s.receiver->removeChild(branch);
  s.receiver->addChild(s.geometry);
  s.contexts[0] = Context();
  return r.restore(back, r.initial, "full model and bump transform restore") &&
         ok;
}
bool height(Run &r) {
  auto &s = r.scene;
  Frame a, b, back;
  bool ok = true;
  const auto source = s.normalBytes;
  for (int value : {20, 230}) {
    std::vector<unsigned char> raw(9, uint8_t(value));
    s.bump->image.setValue(SbVec2s(3, 3), 1, raw.data());
    s.heightNormal = true;
    s.normalBytes = {{128, 128, 255}};
    if (value == 20)
      ok &= r.draw(a, "constant height converted for all lights");
    else
      ok &= r.draw(b, "height additive constant has no physical derivative") &&
            r.equivalent(a, b);
  }
  s.setNormal({{128, 128, 255}});
  ok &= r.draw(b,
               "explicit RGB matches independently known flat height normal") &&
        r.equivalent(a, b);
  s.setNormal(source);
  return r.restore(back, r.initial, "height source recovery") && ok;
}
bool state(Run &r) {
  auto &s = r.scene;
  Frame stable = r.initial, back;
  bool ok = true;
  int last = s.setting.lights - 1;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  auto recover = [&]() {
    bool pass = r.restore(back, stable, "same target malformed recovery");
    return pass;
  };
  auto direction = s.lamps[last]->direction.getValue();
  s.lamps[last]->direction.setValue(nan, 0, -1);
  ok &= r.refuse(s.root, stable, "last consumed light nonfinite direction");
  s.lamps[last]->direction = direction;
  ok &= recover();
  auto color = s.lamps[last]->color.getValue();
  s.lamps[last]->color.setValue(1, nan, 1);
  ok &= r.refuse(s.root, stable, "last consumed light nonfinite color");
  s.lamps[last]->color = color;
  ok &= recover();
  s.lamps[last]->intensity = std::numeric_limits<float>::infinity();
  ok &= r.refuse(s.root, stable, "last consumed light nonfinite intensity");
  s.lamps[last]->intensity = float(s.powers[last]);
  ok &= recover();
  for (auto *lamp : s.lamps)
    lamp->on = FALSE;
  ok &= r.refuse(s.root, stable,
                 "zero lights do not silently shade ordinary geometry");
  for (auto *lamp : s.lamps)
    lamp->on = TRUE;
  ok &= recover();
  auto *ninth = new SoDirectionalLight;
  s.root->insertChild(ninth, 1);
  if (s.setting.lights == 8)
    ok &= r.refuse(s.root, stable, "nine consumed lights exceed real profile");
  s.root->removeChild(ninth);
  ok &= recover();
  s.matrix->translation.setValue(nan, 0);
  ok &= r.refuse(s.root, stable, "nonfinite consumed bump matrix");
  s.matrix->translation.setValue(0, 0);
  ok &= recover();
  auto uv = s.uv->point[3];
  s.uv->point.set1Value(3, SbVec2f(0, nan));
  ok &= r.refuse(s.root, stable, "last authored UV is malformed");
  s.uv->point.set1Value(3, uv);
  ok &= recover();
  auto base = s.texture[0];
  auto alpha = base;
  alpha[3] = 128;
  s.textureAt(0, alpha);
  ok &= r.refuse(s.root, stable,
                 "transparent inherited texture remains outside profile");
  s.textureAt(0, base);
  ok &= recover();
  s.material->transparency = .5f;
  ok &=
      r.refuse(s.root, stable, "transparent material remains outside profile");
  s.material->transparency = 0;
  ok &= recover();
  // An ignored OFF light must not impose a restriction from unused fields.
  s.setSpec({{0, 0, 0}});
  s.lamps[last]->on = FALSE;
  s.lamps[last]->direction.setValue(nan, 0, -1);
  if (s.setting.lights > 1)
    ok &= r.draw(back,
                 "unused OFF malformed light does not poison consumed lights");
  s.lamps[last]->direction = direction;
  s.lamps[last]->on = TRUE;
  s.setSpec({{.04, .035, .045}});
  ok &= recover();
  return ok;
}
bool mixed(Run &r) {
  auto &s = r.scene;
  Frame a, b, back;
  bool ok = true;
  s.setSpec({{0, 0, 0}});
  // A deliberately tilted encoded normal makes active/ordinary contrast
  // large even after eight modulation stages; eligibility is not a 1-LSB gate.
  s.setNormal({{210, 145, 205}});
  s.layout(0);
  ok &= r.draw(a, "shared shape active ordinary active separate contexts");
  s.layout(1);
  if (r.draw(b, "shared shape ordinary active ordinary contexts"))
    ok &= check(changed(a, b) > 16,
                "shared shape bump eligibility changes actual shading");
  else
    ok = false;
  s.layout(0);
  ok &= r.restore(back, a, "shared contexts A/B/A");
  s.layout(0, true);
  ok &= r.draw(b, "mixed authorial order reversed, same visible contexts") &&
        r.equivalent(a, b);
  s.layout(0, false, true);
  ok &= r.draw(a, "overlapping ordinary and bump with independent depths");
  s.layout(0, true, true);
  ok &= r.draw(b, "reverse overlap order preserves nearest opaque depth") &&
        r.equivalent(a, b);
  return ok;
}
void coverageScene(Scene &s) {
  s.setSpec({{0, 0, 0}});
  for (size_t i = 0; i < s.maps.size(); ++i)
    s.casterAt(int(i), 3);
  s.receiver->removeChild(s.geometry);
  auto *branch = new SoSeparator;
  auto *rotation = new SoRotation;
  rotation->rotation.setValue(SbVec3f(0, 0, 1), .23f);
  branch->addChild(rotation);
  branch->addChild(s.geometry);
  s.receiver->addChild(branch);
  s.contexts[0].angle = .23;
}
bool coverageMath(const Scene &s, const Frame &f, unsigned samples,
                  unsigned extent, bool opaqueWindow, bool withDepth,
                  bool report = true) {
  if (f.rgba.size() != size_t(extent) * extent * 4 ||
      (withDepth && f.depth.size() != size_t(extent) * extent))
    return false;
  const auto &context = s.contexts[0];
  unsigned ids[3] = {0, 1, 2};
  double w[3] = {.3, .4, .3};
  bool known = true;
  const auto lit = expected(s, context, 0, 0, 0, w, ids, known, 0, 0);
  if (!known)
    return false;
  unsigned full = 0, empty = 0, partial = 0, odd = 0, bad = 0;
  const double cs = std::cos(context.angle), sn = std::sin(context.angle),
               margin = 2.1 * 4 / extent;
  for (unsigned y = 0; y < extent; ++y)
    for (unsigned x = 0; x < extent; ++x) {
      size_t p = size_t(y * extent + x);
      double wx = (x + .5) * 4 / extent - 2, wy = 2 - (y + .5) * 4 / extent;
      double lx = cs * wx + sn * wy, ly = -sn * wx + cs * wy;
      bool inside = lx > -1.4 + margin && lx < 1.3 - margin &&
                    ly > -1.3 + margin && ly < 1.35 - margin;
      bool outside = lx < -1.4 - margin || lx > 1.3 + margin ||
                     ly < -1.3 - margin || ly > 1.35 + margin;
      int selected = -1;
      if (!opaqueWindow) {
        selected = int(std::lround(f.rgba[p * 4 + 3] * samples / 255.));
        bad += selected > int(samples) ||
               std::abs(int(f.rgba[p * 4 + 3]) -
                        int(std::lround(255. * selected / samples))) > 1;
      } else {
        bad += f.rgba[p * 4 + 3] != 255;
        double best = 1e30;
        for (unsigned k = 0; k <= samples; ++k) {
          double error = 0;
          bool valid = true;
          for (int c = 0; c < 3; ++c) {
            double delta = f.rgba[p * 4 + c] - lit[c] * k / samples;
            error += delta * delta;
            valid &= std::abs(delta) <= 3;
          }
          if (valid && error < best) {
            selected = int(k);
            best = error;
          }
        }
      }
      bad += selected < 0 || (inside && selected != int(samples)) ||
             (outside && selected != 0);
      for (int c = 0; c < 3; ++c)
        bad += selected < 0 || std::abs(double(f.rgba[p * 4 + c]) -
                                        lit[c] * selected / samples) > 3;
      empty += selected == 0;
      full += selected == int(samples);
      partial += selected > 0 && selected < int(samples);
      odd += selected > 0 && selected < int(samples) && (selected & 1);
      if (withDepth)
        bad += std::abs(f.depth[p] - (selected == 0 ? 1.f : 3.f / 8)) > 2e-5f;
    }
  if (report)
    std::cout << "multilight_coverage extent=" << extent
              << " samples=" << samples << " full=" << full
              << " empty=" << empty << " partial=" << partial << " odd=" << odd
              << " bad=" << bad << '\n';
  return bad == 0 && full > 100 && empty > 100 &&
         (samples == 1 || partial > 0) && (samples != 4 || odd > 0);
}
bool coverage(Run &r) {
  coverageScene(r.scene);
  Frame f;
  return r.draw(
             f,
             "actual multisample attachment coverage with all maps enabled") &&
         check(coverageMath(r.scene, f, r.scene.setting.samples, side, false,
                            true),
               "nonempty alpha k/N, odd4x, RGB and sample depth");
}
} // namespace multilamp
int main(int argc, char **argv) {
  using namespace multilamp;
  configureSamplingTest(argc, argv);
  Settings setting;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--portable-sampling") {
    } else if (arg == "--baseline")
      setting.baseline = true;
    else if (arg == "--simple")
      setting.general = false;
    else if (arg == "--general")
      setting.general = true;
    else if (arg == "--less")
      setting.depthMode = 0;
    else if (arg == "--lequal")
      setting.depthMode = 1;
    else if ((arg == "--lights" || arg == "--maps" || arg == "--mode" ||
              arg == "--samples") &&
             i + 1 < argc) {
      std::string value = argv[++i];
      if (arg == "--samples") {
        if (value != "1" && value != "2" && value != "4")
          return 2;
        setting.samples = unsigned(std::atoi(value.c_str()));
      } else if (arg == "--mode") {
        if (value != "delivery" && value != "lighting" && value != "maps" &&
            value != "textures" && value != "state" && value != "transform" &&
            value != "height" && value != "mixed" && value != "coverage")
          return 2;
        setting.mode = value;
      } else {
        if (value.size() != 1 || value[0] < '0' || value[0] > '8')
          return 2;
        int n = value[0] - '0';
        if (arg == "--lights")
          setting.lights = n;
        else
          setting.maps = n;
      }
    } else
      return 2;
  }
  if (setting.lights < 1 || setting.maps > setting.lights)
    return 2;
  SoDB::init();
  CoinRenderAction::initClass();
  if (setting.mode == "delivery")
    return capture(setting) ? 0 : 1;
  CoinRenderCapabilities caps{};
  int result = coin_render_query_capabilities(
      COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps));
  std::cout << "multilight_environment query=" << result
            << " gpu=" << caps.gpu_available << " backend=" << caps.backend
            << " renderer=" << caps.renderer << " adapter=" << caps.adapter_name
            << '\n';
  if (result != 0)
    return 1;
  if (!caps.gpu_available || caps.renderer == COIN_RENDER_RENDERER_UNKNOWN)
    return 77;
  Run run(setting, caps);
  if (!run.harness.target)
    return 1;
  auto &scene = run.scene;
  auto spec = scene.spec;
  scene.setSpec({{0, 0, 0}});
  scene.bumpSwitch->whichChild = SO_SWITCH_NONE;
  scene.contexts[0].bump = false;
  Frame ordinary, back;
  bool ok = run.draw(ordinary, "ordinary prior publication");
  scene.bumpSwitch->whichChild = SO_SWITCH_ALL;
  scene.contexts[0].bump = true;
  scene.setSpec(spec);
  if (!ok)
    return 1;
  if (setting.baseline) {
    ok &= run.harness.reject(
        scene.root, ordinary,
        "baseline multilight bump refusal preserves ordinary publication");
    scene.setSpec({{0, 0, 0}});
    scene.bumpSwitch->whichChild = SO_SWITCH_NONE;
    scene.contexts[0].bump = false;
    return (run.draw(back, "baseline ordinary recovery") &&
            exact(ordinary, back) && ok)
               ? 0
               : 1;
  }
  if (!run.draw(run.initial, "joint directional bump and mapped contributions"))
    return 1;
  ok = setting.mode == "coverage"    ? coverage(run)
       : setting.mode == "lighting"  ? lighting(run)
       : setting.mode == "maps"      ? maps(run)
       : setting.mode == "textures"  ? textures(run)
       : setting.mode == "state"     ? state(run)
       : setting.mode == "transform" ? transform(run)
       : setting.mode == "height"    ? height(run)
                                     : mixed(run);
  return ok ? 0 : 1;
}
