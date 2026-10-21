// Private FramePlan ingress controls; this makes no public SoColorMask claim.
#include "CoinRenderBumpProfileFixture.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#ifdef COIN_LEGACY_MASK_X11
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#endif
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoTranslation.h>
#include <chrono>
#include <cstring>
namespace mask_test {
using namespace bump_profile;
const unsigned clearBytes[4] = {17, 207, 91, 63};
// At 96px/height4 the right edge is exactly x=79.5px. Unlike 79.2px,
// this crosses the two diagonal 2x sample positions (.25,.25)/(.75,.75).
constexpr double meshRight = 1.3125;
constexpr double meshWidth = meshRight + 1.4;
// Flush diagnostic boundaries even when a cold driver call never returns.
void phase(const char *name, const char *state = "", unsigned mask = 0,
           bool enabled = true) {
  static const auto start = std::chrono::steady_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count();
  std::cerr << "mask_phase name=" << name << " state=" << state
            << " mask=" << mask << " enabled=" << enabled
            << " elapsed_ms=" << ms << std::endl;
}
class Recorder : public CoinRenderBackend {
public:
  CoinRenderFramePlan frame;
  uint64_t submits = 0;
  std::string error;
  bool isGpuBackend() const override { return true; }
  bool supportsOpaqueBump() const override { return true; }
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
class Audit : public Proxy {
public:
  using Proxy::Proxy;
  uint64_t rootSubmits = 0;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,
                                CoinRenderTargetP &t) override {
    ++rootSubmits;
    return Proxy::submit(f, t);
  }
  CoinRenderSubmitResult
  submit(const CoinRenderFramePlan &f, CoinRenderTargetP &t,
         const CoinRenderFrameReuseDecision &r) override {
    ++rootSubmits;
    return Proxy::submit(f, t, r);
  }
};
struct Source {
  Scene scene{true};
  SoPointLight *point = new SoPointLight;
  std::unique_ptr<CoinRenderTarget> target;
  Recorder *recorder = nullptr;
  CoinRenderAction action{SbViewportRegion(side, side)};
  CoinRenderFramePlan original;
  const RGB lamp{{.6, -.3, 3}};
  const RGB nonzeroSpec{{.25, .16, .30}};
  const double shine = .125;
  bool specular = false, paired = false, reversed = false;
  SoSeparator *pairNodes[2] = {nullptr, nullptr};
  unsigned occurrenceMask(unsigned occurrence, unsigned requested) const {
    return !paired ? requested : ((occurrence == 0) != reversed ? 1u : 4u);
  }
  bool setPair(bool reverse) {
    if (!paired) {
      scene.positions->ref();
      scene.mesh->ref();
      scene.root->removeChild(scene.positions);
      scene.root->removeChild(scene.mesh);
      for (unsigned i = 0; i < 2; ++i) {
        pairNodes[i] = new SoSeparator;
        pairNodes[i]->ref();
        auto *t = new SoTranslation;
        t->translation.setValue(i ? .9f : -.9f, 0, 0);
        auto *z = new SoScale;
        z->scaleFactor.setValue(.45f, .45f, .45f);
        pairNodes[i]->addChild(t);
        pairNodes[i]->addChild(z);
        pairNodes[i]->addChild(scene.positions);
        pairNodes[i]->addChild(scene.mesh);
        scene.root->addChild(pairNodes[i]);
      }
      scene.positions->unref();
      scene.mesh->unref();
      paired = true;
    }
    scene.root->removeChild(pairNodes[0]);
    scene.root->removeChild(pairNodes[1]);
    scene.root->addChild(pairNodes[reverse ? 1 : 0]);
    scene.root->addChild(pairNodes[reverse ? 0 : 1]);
    reversed = reverse;
    action.apply(scene.root);
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
               "same shared POINT geometry captures two separated occurrences"))
      return false;
    original = recorder->frame;
    return true;
  }
  bool setSpecular(bool enabled) {
    specular = enabled;
    scene.material->specularColor.setValue(
        enabled ? float(nonzeroSpec[0]) : 0.f,
        enabled ? float(nonzeroSpec[1]) : 0.f,
        enabled ? float(nonzeroSpec[2]) : 0.f);
    scene.material->shininess = float(shine);
    action.apply(scene.root);
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
               "same public POINT scene captures authored specular mutation"))
      return false;
    original = recorder->frame;
    return true;
  }
  explicit Source(unsigned samples) {
    // Remove the directional source, rather than leaving an inactive snapshot.
    scene.root->removeChild(scene.light);
    scene.light = nullptr;
    point->location.setValue(float(lamp[0]), float(lamp[1]), float(lamp[2]));
    scene.root->insertChild(point, 1);
    // One original triangle gives a sloped edge without changing the qualified
    // tangent frame S=+X,T=-Y or the planar UV mapping.
    scene.positions->point.set1Value(1, SbVec3f(float(meshRight), -1.3f, 0));
    scene.positions->point.set1Value(2, SbVec3f(float(meshRight), 1.35f, 0));
    const int32_t triangle[] = {0, 1, 2, -1};
    scene.mesh->coordIndex.setValues(0, 4, triangle);
    scene.mesh->coordIndex.setNum(4);
    auto options = samplingTestOptions();
    options.sampleCount = samples;
    target.reset(
        CoinRenderTarget::createOffscreen(SbVec2i32(side, side), options));
    if (!target)
      return;
    recorder = new Recorder;
    target->getPimpl()->backend.reset(recorder);
    action.setRenderTarget(target.get());
    action.setTransparencyType(CoinRenderAction::NONE);
    action.apply(scene.root);
    if (action.getLastStatus() == CoinRenderAction::SUCCESS)
      original = recorder->frame;
  }
  ~Source() {
    action.setRenderTarget(nullptr);
    for (auto *p : pairNodes)
      if (p)
        p->unref();
  }
  CoinRenderFramePlan packet(bool enabled, unsigned mask,
                             bool window = false) const {
    auto frame = original;
    // This edited IR is not the immutable captured revision. Zero requests
    // common Target admission for every authored packet, including negatives.
    frame.revision = 0;
    frame.clearColor.setValue(clearBytes[0] / 255.f, clearBytes[1] / 255.f,
                              clearBytes[2] / 255.f,
                              window ? 1.f : clearBytes[3] / 255.f);
    unsigned occurrence = 0;
    for (const auto &draw : frame.draws)
      if (!draw.shadowLightSlot &&
          draw.sourceNodeId == scene.mesh->getNodeId()) {
        auto &state = frame.renderStates[draw.renderStateSlot];
        state.colorWrite = enabled;
        state.colorWriteMask = uint8_t(occurrenceMask(occurrence++, mask));
      }
    return frame;
  }
};
bool transport(const Source &source, const CoinRenderFramePlan &frame,
               bool enabled, unsigned mask) {
  std::string error;
  if (!check(frame.isValid(&error),
             "legal private color mask Core admission " + error))
    return false;
  CoinWgpuFfiFrame ffi;
  const bool prepared = ffi.prepareBumpFrame(frame, side, side, error);
  if (!check(prepared, "legal private color mask FFI " + error))
    return false;
  const auto &view = ffi.getView();
  unsigned witnesses = 0;
  bool ok = check(view.abi_version == 65 && frame.shadowGroups.empty() &&
                      view.shadow_map_size == 0 && view.bump_image_count == 1 &&
                      view.bump_vertex_count == frame.vertices.size(),
                  "ABI65 legacy bump stream is complete without shadow or "
                  "multilamp substitution");
  if (!ok)
    return false;
  ok &=
      check(view.bump_images[0].width == 1 && view.bump_images[0].height == 1 &&
                view.bump_images[0].rgb_len == 3 &&
                std::equal(source.scene.normal.begin(),
                           source.scene.normal.end(), view.bump_images[0].rgb),
            "exact normal RGB at private ingress");
  for (const auto &draw : frame.draws)
    if (!draw.shadowLightSlot &&
        draw.sourceNodeId == source.scene.mesh->getNodeId()) {
      const unsigned expectedMask = source.occurrenceMask(witnesses++, mask);
      const auto &s = frame.renderStates[draw.renderStateSlot];
      const auto &g = view.states[draw.renderStateSlot];
      if (source.paired) {
        const double center = ((witnesses == 1) != source.reversed) ? -.9 : .9;
        ok &= check(std::abs(s.model[3][0] - center) < 1e-5 &&
                        std::abs(s.model[0][0] - .45) < 1e-5 &&
                        std::abs(s.model[1][1] - .45) < 1e-5,
                    "each R/B mask retains its independent authored "
                    "shared-shape occurrence");
      }
      const auto &material = frame.materials[s.materialSlot];
      const auto &packedMaterial = view.materials[g.material_slot];
      bool authored = true;
      for (unsigned c = 0; c < 3; ++c) {
        const double spec = source.specular ? source.nonzeroSpec[c] : 0;
        authored &= std::abs(material.specular[c] - spec) < 1e-5 &&
                    std::abs(packedMaterial.specular[c] - spec) < 1e-5;
      }
      if (source.specular)
        authored &= std::abs(material.shininess - source.shine) < 1e-5 &&
                    std::abs(packedMaterial.shininess - source.shine) < 1e-5;
      ok &= check(
          authored,
          "captured and consumed specular material is independently authored");
      const auto &lights = frame.lightingStates[s.lightingSlot].lights;
      bool point = lights.size() == 1 &&
                   lights[0].type == CoinRenderLightType::POINT &&
                   g.light_count == 1 && g.lights[0].position_type[3] == 1;
      for (unsigned c = 0; point && c < 3; ++c) {
        const double position = source.lamp[c] - (c == 2 ? 4 : 0);
        point = std::abs(lights[0].position[c] - position) < 1e-5 &&
                std::abs(g.lights[0].position_type[c] - position) < 1e-5;
      }
      ok &= check(point,
                  "one authored POINT survives camera-space Core/FFI delivery");
      for (uint32_t i = draw.geometry.firstVertex;
           i < draw.geometry.firstVertex + draw.geometry.vertexCount; ++i) {
        const auto &vertex = view.bump_vertices[i];
        ok &=
            check(vertex.image_slot == 1 &&
                      std::abs(vertex.tangent_s[0] - 1) < 1e-5 &&
                      std::abs(vertex.tangent_s[1]) < 1e-5 &&
                      std::abs(vertex.tangent_t[0]) < 1e-5 &&
                      std::abs(vertex.tangent_t[1] + 1) < 1e-5,
                  "POINT corners retain qualified signed legacy tangent frame");
      }
      ok &= check(g.has_texture && g.texture_slot < view.texture_count &&
                      view.textures[g.texture_slot].pixel_bytes_len == 4 &&
                      std::equal(source.scene.baseColor.begin(),
                                 source.scene.baseColor.end(),
                                 view.textures[g.texture_slot].pixels),
                  "one opaque base texture is actually consumed");
      unsigned encoded = !enabled || !expectedMask ? 0
                         : expectedMask == 15      ? 1
                                                   : 0x10 | expectedMask;
      ok &= check(
          s.colorWrite == enabled && s.colorWriteMask == expectedMask &&
              g.color_write == encoded,
          "actual consumed draw retains bool/mask and ABI65 channel mapping");
      ok &=
          check(s.depthTest && s.depthWrite && g.depth_test && g.depth_write &&
                    g.depth_function == unsigned(s.depthFunction),
                "mask preserves independent test/write/function depth state");
    }
  return check(witnesses == (source.paired ? 2u : 1u),
               "private mask applies to real bump receiver draw") &&
         ok;
}
// The POINT vector is normalized at each original corner, projected through
// the signed legacy tangent frame, interpolated, then normalized again.
RGB litColor(const Source &source, unsigned x, unsigned y) {
  const auto &s = source.scene;
  double wx = (x + .5) * 4 / side - 2, wy = 2 - (y + .5) * 4 / side;
  const double scale = source.paired ? .45 : 1;
  const double center = source.paired ? (wx < 0 ? -.9 : .9) : 0;
  wx = (wx - center) / scale;
  wy /= scale;
  const RGB lamp{{(source.lamp[0] - center) / scale, source.lamp[1] / scale,
                  source.lamp[2] / scale}};
  const RGB camera{{-center / scale, 0, 4 / scale}};
  const double u = (wx + 1.4) / meshWidth, v = (wy + 1.3) / 2.65;
  const double w[] = {1 - u, u - v, v};
  RGB light{{0, 0, 0}}, eye{{0, 0, 0}};
  for (unsigned k = 0; k < 3; ++k) {
    const auto p = s.positions->point[k];
    auto l = normalized(RGB{{lamp[0] - p[0], lamp[1] - p[1], lamp[2] - p[2]}});
    l[1] = -l[1];
    auto e =
        normalized(RGB{{camera[0] - p[0], camera[1] - p[1], camera[2] - p[2]}});
    e[1] = -e[1];
    for (unsigned c = 0; c < 3; ++c) {
      light[c] += w[k] * l[c];
      eye[c] += w[k] * e[c];
    }
  }
  RGB n{{s.normal[0] * 2. / 255 - 1, s.normal[1] * 2. / 255 - 1,
         s.normal[2] * 2. / 255 - 1}};
  const double factor = std::max(0., std::min(1., dot(n, normalized(light))));
  RGB half{{light[0] + eye[0], light[1] + eye[1], light[2] + eye[2]}};
  const double spec =
      source.specular
          ? std::pow(std::max(0., std::min(1., dot(n, normalized(half)))),
                     64 * source.shine)
          : 0;
  RGB result;
  for (unsigned c = 0; c < 3; ++c)
    result[c] = std::min(255., s.diffuse[c] * factor * s.baseColor[c] +
                                   255 * source.nonzeroSpec[c] * spec);
  return result;
}
// The two regions are nonoverlapping authorial transforms of one shared
// triangle. Their assigned R/B masks remain attached to position on reordering.
bool pairImage(const Source &source, const Frame &frame, bool window,
               bool withDepth, bool report) {
  if (frame.rgba.size() != side * side * 4 ||
      (withDepth && frame.depth.size() != side * side))
    return false;
  unsigned inside[2] = {0, 0}, outside = 0, bad = 0, depthBad = 0;
  const double margin = 2.1 * 4 / side / .45;
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      const double wx = (x + .5) * 4 / side - 2, wy = 2 - (y + .5) * 4 / side;
      unsigned region = wx < 0 ? 0 : 1;
      const double center = region ? .9 : -.9;
      const double lx = (wx - center) / .45, ly = wy / .45;
      const double diagonal =
          ((lx + 1.4) / meshWidth - (ly + 1.3) / 2.65) /
          std::sqrt(1 / (meshWidth * meshWidth) + 1 / (2.65 * 2.65));
      const bool in = diagonal > margin && lx > -1.4 + margin &&
                      lx < meshRight - margin && ly > -1.3 + margin &&
                      ly < 1.35 - margin;
      const bool out = diagonal < -margin || lx < -1.4 - margin ||
                       lx > meshRight + margin || ly < -1.3 - margin ||
                       ly > 1.35 + margin;
      inside[region] += in;
      outside += out;
      const auto expected = litColor(source, x, y);
      const size_t p = (size_t(y) * side + x);
      const unsigned mask = region ? 4 : 1;
      for (unsigned c = 0; c < 4; ++c) {
        const double clear = window && c == 3 ? 255 : clearBytes[c];
        const bool write = mask & (1u << c);
        if (in || out || !write) {
          const double value = in && write ? expected[c] : clear;
          bad += std::abs(frame.rgba[p * 4 + c] - value) > (c == 3 ? 1 : 3);
        }
      }
      if (withDepth) {
        if (in)
          depthBad += std::abs(frame.depth[p] - 3.f / 8) > 2e-5f;
        else if (out)
          depthBad += frame.depth[p] != 1;
        else
          depthBad +=
              std::abs(frame.depth[p] - 3.f / 8) > 2e-5f && frame.depth[p] != 1;
      }
    }
  if (report)
    std::cout << "legacy_mask_pair reversed=" << source.reversed
              << " inside=" << inside[0] << ',' << inside[1]
              << " outside=" << outside << " bad=" << bad
              << " depth_bad=" << depthBad << '\n';
  return inside[0] > 100 && inside[1] > 100 && outside > 500 && bad == 0 &&
         depthBad == 0;
}
bool image(const Source &source, const Frame &frame, bool enabled,
           unsigned mask, bool window, bool withDepth, unsigned samples,
           bool report = true) {
  if (source.paired)
    return pairImage(source, frame, window, withDepth, report);
  if (frame.rgba.size() != side * side * 4 ||
      (withDepth && frame.depth.size() != side * side))
    return false;
  unsigned inside = 0, outside = 0, bad = 0, depthBad = 0, partial = 0, odd = 0;
  const double margin = 2.1 * 4 / side;
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      size_t p = size_t(y * side + x);
      double wx = (x + .5) * 4 / side - 2, wy = 2 - (y + .5) * 4 / side;
      auto lit = litColor(source, x, y);
      const double diagonal =
          ((wx + 1.4) / meshWidth - (wy + 1.3) / 2.65) /
          std::sqrt(1 / (meshWidth * meshWidth) + 1 / (2.65 * 2.65));
      bool in = diagonal > margin && wx > -1.4 + margin &&
                wx < meshRight - margin && wy > -1.3 + margin &&
                wy < 1.35 - margin;
      bool out = diagonal < -margin || wx < -1.4 - margin ||
                 wx > meshRight + margin || wy < -1.3 - margin ||
                 wy > 1.35 + margin;
      inside += in;
      outside += out;
      for (int c = 0; c < 4; ++c) {
        const double clear = c == 3 && window ? 255 : clearBytes[c];
        const double written = c == 3 ? 255 : lit[c];
        const bool write = enabled && (mask & (1u << c));
        double value = frame.rgba[p * 4 + c];
        if (in || out || !write) {
          double expected = in && write ? written : clear;
          bad += std::abs(value - expected) > (c == 3 ? 1 : 3);
        } else if (c == 3) {
          bool matched = false;
          unsigned best = 0;
          double error = 1e9;
          for (unsigned k = 0; k <= samples; ++k) {
            double e =
                std::abs(value - (clear + (written - clear) * k / samples));
            if (e < error) {
              error = e;
              best = k;
            }
          }
          matched = error <= 1;
          bad += !matched;
          if (matched && best > 0 && best < samples) {
            ++partial;
            odd += best % 2;
          }
        }
      }
      if (withDepth) {
        if (in)
          depthBad += std::abs(frame.depth[p] - 3.f / 8) > 2e-5f;
        else if (out)
          depthBad += frame.depth[p] != 1;
        else
          depthBad +=
              std::abs(frame.depth[p] - 3.f / 8) > 2e-5f && frame.depth[p] != 1;
      }
    }
  if (report)
    std::cout << "legacy_point_private_mask enabled=" << enabled
              << " mask=" << mask << " inside=" << inside
              << " outside=" << outside << " bad=" << bad
              << " partial=" << partial << " odd=" << odd
              << " depth_bad=" << depthBad << '\n';
  return inside > 500 && outside > 500 && bad == 0 && depthBad == 0 &&
         (window || samples == 1 || !enabled || !(mask & 8) ||
          (partial > 0 && (samples != 4 || odd > 0)));
}
Frame read(CoinRenderTarget &t, bool depth) {
  Frame f;
  t.readbackRGBA(f.rgba);
  if (depth)
    t.readbackDepth(f.depth);
  f.serial = t.getLastSubmissionSerial();
  return f;
}
bool cpu(Source &source) {
  if (!check(source.recorder && source.original.draws.size(),
             "CPU-only public authoring capture"))
    return false;
  bool ok = true;
  for (unsigned mask = 0; mask < 16; ++mask) {
    auto frame = source.packet(true, mask);
    ok &= transport(source, frame, true, mask);
    auto result = source.target->getPimpl()->executeFrame(frame);
    ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                "legal mask accepted by actual target/Core ingress") &&
          transport(source, source.recorder->frame, true, mask);
  }
  auto off = source.packet(false, 15);
  ok &= transport(source, off, false, 15);
  auto result = source.target->getPimpl()->executeFrame(off);
  ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
              "colorWrite false consumed independently of mask") &&
        transport(source, source.recorder->frame, false, 15);
  for (bool enabled : {false, true}) {
    auto invalid = source.packet(enabled, 0x10);
    std::string error;
    ok &= check(!invalid.isValid(&error) && !error.empty(),
                "unknown mask bit rejected by actual Core validation");
    CoinWgpuFfiFrame ffi;
    auto good = source.packet(true, 15);
    error.clear();
    const bool prepared = ffi.prepareBumpFrame(good, side, side, error);
    ok &=
        check(prepared, "same helper previous-good packet before invalid mask");
    if (!prepared)
      return false;
    const auto previous = ffi.getView();
    std::vector<CoinWgpuRenderState> states(
        previous.states, previous.states + previous.state_count);
    std::vector<CoinWgpuBumpVertex> vertices(previous.bump_vertices,
                                             previous.bump_vertices +
                                                 previous.bump_vertex_count);
    std::vector<uint8_t> rgb(previous.bump_images[0].rgb,
                             previous.bump_images[0].rgb +
                                 previous.bump_images[0].rgb_len);
    error.clear();
    bool accepted = ffi.prepareBumpFrame(invalid, side, side, error);
    ok &= check(!accepted && !error.empty(),
                "unknown mask bit rejected by actual FFI ingress");
    const auto rejectedPacket = ffi.getView();
    ok &= check(
        !accepted && rejectedPacket.states == previous.states &&
            rejectedPacket.bump_vertices == previous.bump_vertices &&
            rejectedPacket.bump_images == previous.bump_images &&
            rejectedPacket.state_count == states.size() &&
            rejectedPacket.bump_vertex_count == vertices.size() &&
            !std::memcmp(rejectedPacket.states, states.data(),
                         states.size() * sizeof(states[0])) &&
            !std::memcmp(rejectedPacket.bump_vertices, vertices.data(),
                         vertices.size() * sizeof(vertices[0])) &&
            std::equal(rgb.begin(), rgb.end(),
                       rejectedPacket.bump_images[0].rgb),
        "FFI invalid mask preserves previous-good packet pointers and bytes");
    error.clear();
    const bool recovered = ffi.prepareBumpFrame(good, side, side, error);
    ok &= check(recovered,
                "same FFI helper valid packet recovers after invalid mask");
    if (recovered) {
      const auto restored = ffi.getView();
      ok &= check(
          restored.state_count == states.size() &&
              !std::memcmp(restored.states, states.data(),
                           states.size() * sizeof(states[0])),
          "same helper recovery restores complete consumed render-state bytes");
    }

    auto stable = read(*source.target, true);
    auto submits = source.recorder->submits;
    result = source.target->getPimpl()->executeFrame(invalid);
    auto after = read(*source.target, true);
    std::cout << "mask_invalid_cpu enabled=" << enabled
              << " revision=" << invalid.revision
              << " captured_revision=" << source.original.revision
              << " status=" << unsigned(result.status) << " submits=" << submits
              << "->" << source.recorder->submits << " serial=" << stable.serial
              << "->" << after.serial
              << " rgba_preserved=" << (stable.rgba == after.rgba)
              << " depth_preserved=" << (stable.depth == after.depth)
              << " error=" << result.diagnostic << '\n';
    ok &= check(result.status != CoinRenderBackendStatus::SUCCESS &&
                    source.recorder->submits == submits &&
                    stable.serial == after.serial &&
                    stable.rgba == after.rgba && stable.depth == after.depth,
                "invalid private mask refuses before recorder and preserves "
                "publication");
    auto recovery = source.packet(true, 15);
    result = source.target->getPimpl()->executeFrame(recovery);
    ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                "CPU same-target full mask recovery") &&
          transport(source, source.recorder->frame, true, 15);
  }
  return ok;
}
#ifdef COIN_LEGACY_MASK_X11
unsigned channel(unsigned long pixel, unsigned long mask) {
  while (mask && !(mask & 1)) {
    mask >>= 1;
    pixel >>= 1;
  }
  return mask ? unsigned((pixel & mask) * 255 / mask) : 0;
}
bool physical(Display *d, Window w, Frame &f) {
  XSync(d, False);
  XImage *image = XGetImage(d, w, 0, 0, side, side, AllPlanes, ZPixmap);
  if (!image)
    return false;
  f.rgba.resize(side * side * 4);
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      auto pixel = XGetPixel(image, x, y);
      size_t p = size_t(y * side + x) * 4;
      f.rgba[p] = channel(pixel, image->red_mask);
      f.rgba[p + 1] = channel(pixel, image->green_mask);
      f.rgba[p + 2] = channel(pixel, image->blue_mask);
      f.rgba[p + 3] = 255;
    }
  XDestroyImage(image);
  return true;
}
#endif
int gpu(Source &source, unsigned samples, bool window) {
  CoinRenderCapabilities caps{};
  phase("capabilities_begin");
  int query = coin_render_query_capabilities(
      window ? COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW
             : COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      &caps, sizeof(caps));
  phase("capabilities_end");
  std::cout << "mask_environment query=" << query
            << " gpu=" << caps.gpu_available << " renderer=" << caps.renderer
            << " adapter=" << caps.adapter_name << '\n';
  if (query)
    return 1;
  if (!caps.gpu_available)
    return 77;
  auto options = samplingTestOptions();
  options.sampleCount = samples;
  options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
  std::unique_ptr<CoinRenderTarget> target;
  phase("target_factory_begin");
#ifdef COIN_LEGACY_MASK_X11
  Display *display = nullptr;
  Window nativeWindow = 0;
  if (window) {
    display = XOpenDisplay(nullptr);
    if (!display)
      return 77;
    nativeWindow = XCreateSimpleWindow(display, DefaultRootWindow(display), 12,
                                       12, side, side, 0, 0, 0);
    XSetWindowAttributes attributes{};
    attributes.override_redirect = True;
    XChangeWindowAttributes(display, nativeWindow, CWOverrideRedirect,
                            &attributes);
    XMapRaised(display, nativeWindow);
    XSync(display, False);
    CoinRenderNativeSurfaceDescriptor native{};
    native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    native.structSize = sizeof(native);
    native.type = COIN_RENDER_SURFACE_XLIB;
    native.native.xlib.display = display;
    native.native.xlib.window = nativeWindow;
    target.reset(
        CoinRenderTarget::createWindow(native, SbVec2i32(side, side), options));
  }
#else
  if (window)
    return 77;
#endif
  if (!window)
    target.reset(
        CoinRenderTarget::createOffscreen(SbVec2i32(side, side), options));
  phase("target_factory_end");
  if (!check(bool(target), "private mask actual target factory"))
    return 1;
  auto &p = target->getPimpl().get();
  phase("prepare_backend_begin");
  const auto prepared = p.prepareBackend();
  phase("prepare_backend_end");
  if (!check(prepared == CoinRenderBackendStatus::SUCCESS && p.backend &&
                 p.backend->isGpuBackend(),
             "private mask requested real GPU backend"))
    return 1;
  auto *audit = new Audit(std::move(p.backend));
  p.backend.reset(audit);
  if (!window)
    target->setDepthReadbackEnabled(TRUE);
  Frame stable, stablePhysical;
  bool ok = true;
  auto execute = [&](bool enabled, unsigned mask, const char *phase) {
    mask_test::phase("packet_begin", phase, mask, enabled);
    auto frame = source.packet(enabled, mask, window);
    std::cerr << "mask_packet state=" << phase << " samples=" << samples
              << " lights=1 maps=0"
              << " draws=" << frame.draws.size()
              << " states=" << frame.renderStates.size() << std::endl;
    mask_test::phase("transport_begin", phase, mask, enabled);
    bool pass = transport(source, frame, enabled, mask);
    mask_test::phase("transport_end", phase, mask, enabled);
    auto old = target->getLastSubmissionSerial();
    auto submits = audit->rootSubmits;
    if (window)
      pass &= target->requestWindowReadbackRGBA() != FALSE;
    mask_test::phase("execute_begin", phase, mask, enabled);
    auto result = p.executeFrame(frame);
    mask_test::phase("execute_end", phase, mask, enabled);
    mask_test::phase("readback_begin", phase, mask, enabled);
    auto current = read(*target, !window);
    mask_test::phase("readback_end", phase, mask, enabled);
    pass &=
        check(result.status == CoinRenderBackendStatus::SUCCESS &&
                  current.serial > old && audit->rootSubmits == submits + 1 &&
                  target->getOptions().sampleCount == samples,
              "one private-frame GPU publication at requested sample count");
    pass &= transport(source, audit->plan, enabled, mask);
    pass &= check(
        image(source, current, enabled, mask, window, !window, samples),
        "independent clear-plus-enabled-channel image and unchanged depth");
    Frame shown;
#ifdef COIN_LEGACY_MASK_X11
    if (window && pass) {
      auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(5);
      bool present = false;
      unsigned attempts = 0;
      do {
        ++attempts;
        if (physical(display, nativeWindow, shown) &&
            image(source, shown, enabled, mask, true, false, samples, false)) {
          bool match = shown.rgba.size() == current.rgba.size();
          for (size_t i = 0; match && i < shown.rgba.size(); ++i)
            if (i % 4 < 3)
              match = std::abs(int(shown.rgba[i]) - int(current.rgba[i])) <= 3;
          if (match && std::chrono::steady_clock::now() <= deadline) {
            present = true;
            break;
          }
        }
      } while (std::chrono::steady_clock::now() < deadline);
      std::cout << "mask_present phase=" << phase << " attempts=" << attempts
                << " current=" << present << '\n';
      pass &= check(present, "current whole masked physical image within "
                             "original five seconds after one execution");
    }
#endif
    std::cout << "mask_execute phase=" << phase << " mask=" << mask
              << " enabled=" << enabled << " status=" << unsigned(result.status)
              << " error=" << result.diagnostic << '\n';
    if (pass) {
      stable = std::move(current);
      stablePhysical = std::move(shown);
    }
    return pass;
  };
  for (unsigned lighting = 0; lighting < 2; ++lighting) {
    if (lighting && !source.setSpecular(true)) {
      ok = false;
      break;
    }
    std::cerr << "mask_lighting specular=" << source.specular << std::endl;
    ok &= execute(true, 15, "initial all channels");
    std::vector<unsigned> masks =
        window         ? std::vector<unsigned>{1, 4, 15}
        : samples == 1 ? std::vector<unsigned>{0, 1, 2, 4, 8, 3, 5, 15}
                       : std::vector<unsigned>{1, 4, 15};
    for (unsigned mask : masks) {
      ok &= execute(true, mask, "sampled channel mask");
      ok &= execute(true, 15, "full mask recovery");
    }
    ok &= execute(false, 15, "color write disabled with depth retained");
    ok &= execute(true, 15, "color write true recovery");
    auto invalid = source.packet(true, 0x10, window);
    auto before = stable;
    auto physicalBefore = stablePhysical;
    auto submits = audit->rootSubmits;
    auto producers = audit->producers;
    if (window)
      target->requestWindowReadbackRGBA();
    phase("invalid_execute_begin");
    auto result = p.executeFrame(invalid);
    phase("invalid_execute_end");
    auto rejected = read(*target, !window);
    std::cout << "mask_invalid_gpu revision=" << invalid.revision
              << " status=" << unsigned(result.status) << " submits=" << submits
              << "->" << audit->rootSubmits << " producers=" << producers
              << "->" << audit->producers << " serial=" << before.serial << "->"
              << rejected.serial
              << " rgba_preserved=" << (before.rgba == rejected.rgba)
              << " depth_preserved=" << (before.depth == rejected.depth)
              << " error=" << result.diagnostic << '\n';
    ok &= check(
        result.status != CoinRenderBackendStatus::SUCCESS &&
            audit->rootSubmits == submits && audit->producers == producers &&
            rejected.serial == before.serial && rejected.rgba == before.rgba &&
            rejected.depth == before.depth,
        "invalid private mask atomic refusal before "
        "commands/producers/publication");
#ifdef COIN_LEGACY_MASK_X11
    if (window) {
      Frame shown;
      ok &= check(physical(display, nativeWindow, shown) &&
                      shown.rgba == physicalBefore.rgba,
                  "invalid private mask preserves exact physical framebuffer");
    }
#endif
    ok &= execute(true, 15, "invalid private mask same-target recovery");
  }
  if (source.specular) {
    ok &= source.setSpecular(false);
    ok &= execute(true, 15, "diffuse/specular/diffuse same-target restoration");
  }
  if (source.setSpecular(true) && source.setPair(false)) {
    ok &= execute(true, 15, "same-frame shared POINT draws R/B");
    ok &= source.setPair(true);
    ok &= execute(true, 15, "same-frame shared POINT draws reversed B/R");
    ok &= source.setPair(false);
    ok &= execute(true, 15, "same-frame R/B authorial order restoration");
  } else
    ok = false;
  phase("target_destroy_begin");
  target.reset();
  phase("target_destroy_end");
#ifdef COIN_LEGACY_MASK_X11
  if (nativeWindow)
    XDestroyWindow(display, nativeWindow);
  if (display)
    XCloseDisplay(display);
#endif
  return ok ? 0 : 1;
}
} // namespace mask_test
int main(int argc, char **argv) {
  using namespace mask_test;
  bool cpuMode = false, window = false;
  unsigned samples = 1;
  configureSamplingTest(argc, argv);
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--cpu")
      cpuMode = true;
    else if (arg == "--gpu")
      cpuMode = false;
    else if (arg == "--window")
      window = true;
    else if (arg == "--portable-sampling") {
    } else if (arg == "--samples" && i + 1 < argc) {
      std::string value = argv[++i];
      if (value != "1" && value != "2" && value != "4")
        return 2;
      samples = unsigned(std::atoi(value.c_str()));
    } else
      return 2;
  }
#ifdef COIN_LEGACY_MASK_X11
  if (window && !XInitThreads())
    return 1;
#endif
  SoDB::init();
  CoinRenderAction::initClass();
  Source source(samples);
  if (!source.recorder || source.original.draws.empty())
    return 1;
  if (cpuMode) {
    bool ok = cpu(source);
    if (source.setSpecular(true))
      ok &= cpu(source);
    else
      ok = false;
    for (bool reverse : {false, true, false}) {
      if (!source.setPair(reverse)) {
        ok = false;
        break;
      }
      auto packet = source.packet(true, 15);
      ok &= transport(source, packet, true, 15);
      auto result = source.target->getPimpl()->executeFrame(packet);
      ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                  "CPU same-frame two-mask ingress") &&
            transport(source, source.recorder->frame, true, 15);
    }
    return ok ? 0 : 1;
  }
  return gpu(source, samples, window);
}
