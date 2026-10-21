// Reuse B's qualified fixture/arithmetic; no candidate implementation is used.
#define main coin_render_qualified_multilight_main
#include "CoinRenderBumpMultilightTest.cpp"
#undef main
#ifdef COIN_MULTILIGHT_RTT_X11
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#endif
namespace multi_rtt {
using namespace multilamp;
constexpr double right = 1.3125, width = right + 1.4;
const RGB tint{{.65, .8, .55}}, background{{11, 29, 61}};
struct Graph {
  Scene producer;
  SoSeparator *root = new SoSeparator;
  SoSceneTexture2 *texture = new SoSceneTexture2;
  SoTextureCoordinate2 *coords = nullptr;
  bool producerCoverage = false;
  SoCoordinate3 *positions = new SoCoordinate3;
  SoIndexedFaceSet *mesh = new SoIndexedFaceSet;
  explicit Graph(const Settings &s) : producer(s) {
    root->ref();
    auto *c = new SoOrthographicCamera;
    c->position.setValue(0, 0, 4);
    c->height = 4;
    c->nearDistance = 1;
    c->farDistance = 9;
    root->addChild(c);
    auto *lm = new SoLightModel;
    lm->model = SoLightModel::BASE_COLOR;
    root->addChild(lm);
    auto *m = new SoMaterial;
    m->diffuseColor.setValue(float(tint[0]), float(tint[1]), float(tint[2]));
    m->transparency = 0;
    root->addChild(m);
    auto *q = new SoComplexity;
    q->textureQuality = .1f;
    root->addChild(q);
    texture->scene = producer.root;
    texture->size.setValue(side, side);
    texture->type = SoSceneTexture2::RGBA8;
    texture->transparencyFunction = SoSceneTexture2::NONE;
    texture->model = SoSceneTexture2::MODULATE;
    texture->backgroundColor.setValue(11 / 255.f, 29 / 255.f, 61 / 255.f, 1);
    root->addChild(texture);
    auto *uv = new SoTextureCoordinate2;
    coords = uv;
    const SbVec2f coords[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1)};
    uv->point.setValues(0, 3, coords);
    root->addChild(uv);
    const SbVec3f p[] = {SbVec3f(-1.4f, -1.3f, 0),
                         SbVec3f(float(right), -1.3f, 0),
                         SbVec3f(float(right), 1.35f, 0)};
    positions->point.setValues(0, 3, p);
    root->addChild(positions);
    const int32_t indices[] = {0, 1, 2, -1};
    mesh->coordIndex.setValues(0, 4, indices);
    root->addChild(mesh);
  }
  ~Graph() { root->unref(); }
};
// Source coordinates are GL bottom-left. Quantize NEAREST in the 96px source,
// then derive world coordinates from its authored orthographic projection.
RGB sourceTexel(const Graph &g, double u, double v, bool &known, bool &bg) {
  const int x = std::max(0, std::min(int(side) - 1, int(std::floor(u * side))));
  const int y = std::max(0, std::min(int(side) - 1, int(std::floor(v * side))));
  const double px = (x + .5) * 4 / side - 2, py = (y + .5) * 4 / side - 2,
               m = 2.1 * 4 / side;
  const bool in =
      px > -1.4 + m && px < 1.3 - m && py > -1.3 + m && py < 1.35 - m;
  bg = px < -1.4 - m || px > 1.3 + m || py < -1.3 - m || py > 1.35 + m;
  if (bg)
    return background;
  if (!in) {
    known = false;
    return RGB{{0, 0, 0}};
  }
  double a = (px + 1.4) / 2.7, b = (py + 1.3) / 2.65;
  double w[3];
  unsigned ids[3];
  int triangle;
  if (b <= a) {
    triangle = 0;
    ids[0] = 0;
    ids[1] = 1;
    ids[2] = 2;
    w[0] = 1 - a;
    w[1] = a - b;
    w[2] = b;
  } else {
    triangle = 1;
    ids[0] = 0;
    ids[1] = 2;
    ids[2] = 3;
    w[0] = 1 - b;
    w[1] = a;
    w[2] = b - a;
  }
  return expected(g.producer, g.producer.contexts[0], px, py, triangle, w, ids,
                  known, px, py);
}
// A constant nearest texel on the SOURCE silhouette isolates the producer
// resolve. Interior consumer samples all see this one already-resolved texel;
// no second coverage factor is mixed into this numerical witness.
bool producerCoverageImage(const Graph &g, const Frame &f, unsigned extent,
                           unsigned samples, bool window, bool depth,
                           bool report) {
  if (f.rgba.size() != size_t(extent) * extent * 4 ||
      (depth && f.depth.size() != size_t(extent) * extent))
    return false;
  const RGB clear{{233, 17, 211}};
  bool known = true;
  const double w[] = {.3, .3, .4};
  const unsigned ids[] = {0, 1, 2};
  const auto fullSource = expected(g.producer, g.producer.contexts[0], 0, 0, 0,
                                   w, ids, known, 0, 0);
  if (!known)
    return false;
  unsigned inside = 0, outside = 0, bad = 0, depthBad = 0, rootPartial = 0,
           rootOdd = 0;
  int sourceK = -1;
  const double margin = 2.1 * 4 / extent;
  for (unsigned y = 0; y < extent; ++y)
    for (unsigned x = 0; x < extent; ++x) {
      const double wx = (x + .5) * 4 / extent - 2,
                   wy = 2 - (y + .5) * 4 / extent;
      const double u = (wx + 1.4) / width, v = (wy + 1.3) / 2.65;
      const double diagonal =
          (u - v) / std::sqrt(1 / (width * width) + 1 / (2.65 * 2.65));
      const bool in = diagonal > margin && wx > -1.4 + margin &&
                      wx < right - margin && wy > -1.3 + margin &&
                      wy < 1.35 - margin;
      const bool out = diagonal < -margin || wx < -1.4 - margin ||
                       wx > right + margin || wy < -1.3 - margin ||
                       wy > 1.35 + margin;
      const size_t p = size_t(y) * extent + x;
      if (in) {
        ++inside;
        bool matched = false;
        unsigned bestK = 0;
        double best = 1e9;
        for (unsigned k = 0; k <= samples; ++k) {
          double error = 0;
          bool candidate = true;
          for (unsigned c = 0; c < 3; ++c) {
            const double value =
                tint[c] * (clear[c] + (fullSource[c] - clear[c]) * k / samples);
            const double delta = std::abs(f.rgba[p * 4 + c] - value);
            candidate &= delta <= 3;
            error += delta * delta;
          }
          if (candidate && error < best) {
            matched = true;
            best = error;
            bestK = k;
          }
        }
        bad += !matched;
        if (matched) {
          if (sourceK < 0)
            sourceK = int(bestK);
          else
            bad += sourceK != int(bestK);
        }
        bad += f.rgba[p * 4 + 3] != 255;
      }
      if (out) {
        ++outside;
        for (unsigned c = 0; c < 3; ++c)
          bad += f.rgba[p * 4 + c] > 3;
        bad += std::abs(int(f.rgba[p * 4 + 3]) - (window ? 255 : 0)) > 1;
      }
      if (depth) {
        if (in)
          depthBad += std::abs(f.depth[p] - 3.f / 8) > 2e-5f;
        else if (out)
          depthBad += f.depth[p] != 1;
        else
          depthBad += std::abs(f.depth[p] - 3.f / 8) > 2e-5f && f.depth[p] != 1;
      }
      if (!window) {
        double best = 1e9;
        unsigned level = 0;
        for (unsigned k = 0; k <= samples; ++k) {
          const double e = std::abs(f.rgba[p * 4 + 3] - 255. * k / samples);
          if (e < best) {
            best = e;
            level = k;
          }
        }
        bad += best > 1;
        if (level && level < samples) {
          ++rootPartial;
          rootOdd += level % 2;
        }
      }
    }
  if (report)
    std::cout << "multilight_rtt_source_coverage samples=" << samples
              << " source_k=" << sourceK << " inside=" << inside
              << " outside=" << outside << " bad=" << bad
              << " depth_bad=" << depthBad << " root_partial=" << rootPartial
              << " root_odd=" << rootOdd << '\n';
  return inside > 500 && outside > 500 && bad == 0 && depthBad == 0 &&
         (samples == 1 || (sourceK > 0 && sourceK < int(samples) &&
                           (samples != 4 || sourceK % 2 == 1))) &&
         (window || samples == 1 ||
          (rootPartial > 0 && (samples != 4 || rootOdd > 0)));
}
bool image(const Graph &g, const Frame &f, unsigned extent, unsigned samples,
           bool window, bool depth, bool report = true) {
  if (g.producerCoverage)
    return producerCoverageImage(g, f, extent, samples, window, depth, report);
  if (f.rgba.size() != size_t(extent) * extent * 4 ||
      (depth && f.depth.size() != size_t(extent) * extent))
    return false;
  unsigned inside = 0, outside = 0, bad = 0, depthBad = 0, partial = 0, odd = 0,
           full = 0, empty = 0, oriented[2] = {0, 0};
  double maximum = 0;
  const double margin = 2.1 * 4 / extent;
  for (unsigned y = 0; y < extent; ++y)
    for (unsigned x = 0; x < extent; ++x) {
      const double wx = (x + .5) * 4 / extent - 2,
                   wy = 2 - (y + .5) * 4 / extent;
      const double u = (wx + 1.4) / width, v = (wy + 1.3) / 2.65;
      const double diagonal =
          (u - v) / std::sqrt(1 / (width * width) + 1 / (2.65 * 2.65));
      const bool in = diagonal > margin && wx > -1.4 + margin &&
                      wx < right - margin && wy > -1.3 + margin &&
                      wy < 1.35 - margin;
      const bool out = diagonal < -margin || wx < -1.4 - margin ||
                       wx > right + margin || wy < -1.3 - margin ||
                       wy > 1.35 + margin;
      const size_t p = size_t(y) * extent + x;
      bool known = true, bg = false;
      auto rgb = sourceTexel(g, u, v, known, bg);
      if (in && known) {
        ++inside;
        ++oriented[v > .5];
        for (unsigned c = 0; c < 3; ++c) {
          const double e = rgb[c] * tint[c],
                       d = std::abs(f.rgba[p * 4 + c] - e);
          maximum = std::max(maximum, d);
          bad += d > 3;
        }
        bad += f.rgba[p * 4 + 3] != 255;
      }
      if (out) {
        ++outside;
        for (unsigned c = 0; c < 3; ++c)
          bad += f.rgba[p * 4 + c] > 3;
        bad += std::abs(int(f.rgba[p * 4 + 3]) - (window ? 255 : 0)) > 1;
      }
      if (depth) {
        if (in)
          depthBad += std::abs(f.depth[p] - 3.f / 8) > 2e-5f;
        else if (out)
          depthBad += f.depth[p] != 1;
        else
          depthBad += std::abs(f.depth[p] - 3.f / 8) > 2e-5f && f.depth[p] != 1;
      }
      // Alpha is a SINGLE resolve: the entire producer, including its clear,
      // has alpha1. No consumer texture-alpha blend can introduce k/N^2 here.
      if (!window) {
        double error = 1e9;
        unsigned kbest = 0;
        for (unsigned k = 0; k <= samples; ++k) {
          double e = std::abs(f.rgba[p * 4 + 3] - 255. * k / samples);
          if (e < error) {
            error = e;
            kbest = k;
          }
        }
        bad += error > 1;
        if (kbest == 0)
          ++empty;
        else if (kbest == samples)
          ++full;
        else {
          ++partial;
          odd += kbest % 2;
        }
      } else if ((u > .94 || v < .04) && ((std::abs(wx - right) < margin) ||
                                          (std::abs(wy + 1.3) < margin))) {
        // In these edge strips every producer sample is its uniform opaque
        // clear. Thus physical RGB isolates one consumer resolve independently
        // of VSM.
        double best = 1e9;
        unsigned kbest = 0;
        bool matched = false;
        for (unsigned k = 0; k <= samples; ++k) {
          double score = 0;
          bool valid = true;
          for (unsigned c = 0; c < 3; ++c) {
            double e = std::abs(f.rgba[p * 4 + c] -
                                background[c] * tint[c] * k / samples);
            valid &= e <= 3;
            score += e * e;
          }
          if (valid && score < best) {
            matched = true;
            best = score;
            kbest = k;
          }
        }
        bad += !matched;
        if (matched) {
          if (kbest == 0)
            ++empty;
          else if (kbest == samples)
            ++full;
          else {
            ++partial;
            odd += kbest % 2;
          }
        }
      }
    }
  if (report)
    std::cout << "multilight_rtt_oracle extent=" << extent
              << " samples=" << samples << " inside=" << inside
              << " outside=" << outside << " halves=" << oriented[0] << ','
              << oriented[1] << " bad=" << bad << " max=" << maximum
              << " depth_bad=" << depthBad << " partial=" << partial
              << " odd=" << odd << '\n';
  return inside > 150 && outside > 500 && oriented[0] > 20 &&
         oriented[1] > 20 && bad == 0 && depthBad == 0 && empty > 0 &&
         full > 0 &&
         (samples == 1 || (partial > 0 && (samples != 4 || odd > 0)));
}
class Inspector : public Recorder {
public:
  CoinRenderRttPlan graph;
  CoinRenderFramePlan root;
  unsigned calls = 0;
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g,
                                      const CoinRenderFramePlan &f,
                                      const SbVec2i32 &) const override {
    auto *self = const_cast<Inspector *>(this);
    self->graph = g;
    self->root = f;
    ++self->calls;
    return {CoinRenderBackendStatus::UNSUPPORTED,
            "independent graph capture stop before GPU", 0};
  }
};
bool graphDelivery(const Graph &g, const CoinRenderRttPlan &graph,
                   CoinRenderSceneTextureMode route) {
  bool ok = check(graph.mode == route && graph.producers.size() == 1,
                  "one actual RTT dependency and authored route");
  if (!ok)
    return false;
  const auto &p = graph.producers[0];
  ok &= check(!p.cube && p.size == SbVec2i32(side, side) &&
                  p.format == CoinRenderTextureFormat::RGBA8_LINEAR,
              "actual producer dimensions/format preserved");
  ok &= delivery(g.producer, p.plan);
  return ok;
}
bool cpu() {
  bool ok = true;
  const int configs[][2] = {{1, 0}, {1, 1}, {2, 0}, {2, 1},
                            {4, 4}, {8, 0}, {8, 4}, {8, 8}};
  for (auto c : configs)
    for (auto route :
         {COIN_RENDER_SCENE_TEXTURE_STAGED, COIN_RENDER_SCENE_TEXTURE_DIRECT}) {
      Settings s;
      s.lights = c[0];
      s.maps = c[1];
      Graph graph(s);
      auto options = samplingTestOptions();
      options.sceneTexture = route;
      std::unique_ptr<CoinRenderTarget> t(
          CoinRenderTarget::createOffscreen(SbVec2i32(side, side), options));
      if (!t)
        return false;
      auto *inspector = new Inspector;
      t->getPimpl()->backend.reset(inspector);
      CoinRenderAction a(SbViewportRegion(side, side));
      a.setTransparencyType(CoinRenderAction::NONE);
      a.setRenderTarget(t.get());
      a.apply(graph.root);
      ok &= check(a.getLastStatus() != CoinRenderAction::SUCCESS &&
                      inspector->calls > 0 && inspector->submits == 0 &&
                      t->getLastSubmissionSerial() == 0,
                  "CPU-only public capture stops before any backend "
                  "execution/publication") &&
            graphDelivery(graph, inspector->graph, route);
      bool dependency = false;
      for (const auto &i : inspector->root.textures)
        dependency |= i.producerId == 1 && i.width == side && i.height == side;
      ok &=
          check(dependency, "consumer stores real scoped producer dependency");
      if (c[0] == 8 && c[1] == 8 && inspector->graph.producers.size() == 1) {
        const auto good = inspector->graph.producers[0].plan;
        for (unsigned malformed = 0; malformed < 2; ++malformed) {
          auto broken = good;
          std::string diagnostic;
          if (malformed == 0)
            broken.bumpImages.back().rgb.pop_back();
          else
            for (const auto &draw : broken.draws)
              if (draw.sourceNodeId == graph.producer.mesh->getNodeId()) {
                auto &lights =
                    broken
                        .lightingStates[broken
                                            .renderStates[draw.renderStateSlot]
                                            .lightingSlot]
                        .lights;
                lights.back().direction[0] =
                    std::numeric_limits<float>::quiet_NaN();
                break;
              }
          CoinWgpuFfiFrame ffi;
          ok &= check(
              !coin_render_validate_extended_bump(broken, diagnostic) &&
                  !diagnostic.empty(),
              "malformed final producer payload/light rejected by bump Core");
          diagnostic.clear();
          const bool admitted =
              ffi.prepareBumpFrame(broken, side, side, diagnostic);
          ok &= check(!admitted && !diagnostic.empty(),
                      "malformed producer snapshot rejected by FFI");
          diagnostic.clear();
          ok &= check(ffi.prepareBumpFrame(good, side, side, diagnostic),
                      "same FFI malformed source recovery");
        }
      }
      a.setRenderTarget(nullptr);
    }
  return ok;
}
#ifdef COIN_MULTILIGHT_RTT_X11
unsigned channel(unsigned long p, unsigned long mask) {
  while (mask && !(mask & 1)) {
    mask >>= 1;
    p >>= 1;
  }
  return mask ? unsigned((p & mask) * 255 / mask) : 0;
}
bool physical(Display *d, Window w, unsigned extent, Frame &f) {
  XSync(d, False);
  XWindowAttributes a{};
  if (!XGetWindowAttributes(d, w, &a) || a.width != int(extent) ||
      a.height != int(extent))
    return false;
  XImage *i = XGetImage(d, w, 0, 0, extent, extent, AllPlanes, ZPixmap);
  if (!i)
    return false;
  f.rgba.resize(size_t(extent) * extent * 4);
  for (unsigned y = 0; y < extent; ++y)
    for (unsigned x = 0; x < extent; ++x) {
      const auto p = XGetPixel(i, x, y);
      size_t j = (size_t(y) * extent + x) * 4;
      f.rgba[j] = channel(p, i->red_mask);
      f.rgba[j + 1] = channel(p, i->green_mask);
      f.rgba[j + 2] = channel(p, i->blue_mask);
      f.rgba[j + 3] = 255;
    }
  XDestroyImage(i);
  return true;
}
#endif
class Audit : public bump_shadow::Audit {
public:
  using bump_shadow::Audit::Audit;
  unsigned producerSamples = 0;
  CoinRenderFramePlan directPlan, attempted;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,
                                CoinRenderTargetP &t) override {
    attempted = f;
    return bump_shadow::Audit::submit(f, t);
  }
  CoinRenderSubmitResult
  submit(const CoinRenderFramePlan &f, CoinRenderTargetP &t,
         const CoinRenderFrameReuseDecision &r) override {
    attempted = f;
    return bump_shadow::Audit::submit(f, t, r);
  }
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan &f,
                                   const SbVec2i32 &size, uint64_t id,
                                   CoinRenderTargetP &t,
                                   uint64_t &token) override {
    directPlan = f;
    producerSamples = t.options.sampleCount;
    return bump_profile::Proxy::submitRtt(f, size, id, t, token);
  }
};
struct Run {
  Graph graph;
  Settings settings;
  CoinRenderSceneTextureMode route;
  bool window = false;
  unsigned extent = side;
  std::unique_ptr<CoinRenderTarget> target;
  Audit *audit = nullptr;
  CoinRenderAction action{SbViewportRegion(side, side)};
  Frame last, lastPhysical;
  std::map<uint64_t, std::string> authored;
#ifdef COIN_MULTILIGHT_RTT_X11
  Display *display = nullptr;
  Window nativeWindow = 0;
#endif
  Run(const Settings &s, const CoinRenderCapabilities &caps,
      CoinRenderSceneTextureMode r, bool w)
      : graph(s), settings(s), route(r), window(w) {
    auto options = samplingTestOptions();
    options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
    options.sampleCount = s.samples;
    options.sceneTexture = route;
#ifdef COIN_MULTILIGHT_RTT_X11
    if (window) {
      display = XOpenDisplay(nullptr);
      if (!display)
        return;
      nativeWindow = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                         12, 12, extent, extent, 0, 0, 0);
      XSetWindowAttributes attrs{};
      attrs.override_redirect = True;
      XChangeWindowAttributes(display, nativeWindow, CWOverrideRedirect,
                              &attrs);
      XMapRaised(display, nativeWindow);
      XSync(display, False);
      CoinRenderNativeSurfaceDescriptor native{};
      native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
      native.structSize = sizeof(native);
      native.type = COIN_RENDER_SURFACE_XLIB;
      native.native.xlib.display = display;
      native.native.xlib.window = nativeWindow;
      target.reset(CoinRenderTarget::createWindow(
          native, SbVec2i32(extent, extent), options));
    }
#endif
    if (!window)
      target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(extent, extent),
                                                     options));
    if (!target)
      return;
    auto &p = target->getPimpl().get();
    if (p.prepareBackend() != CoinRenderBackendStatus::SUCCESS || !p.backend ||
        !p.backend->isGpuBackend()) {
      target.reset();
      return;
    }
    audit = new Audit(std::move(p.backend));
    p.backend.reset(audit);
    if (!window)
      target->setDepthReadbackEnabled(TRUE);
    action.setRenderTarget(target.get());
    action.setTransparencyType(CoinRenderAction::NONE);
    action.setBackgroundColor(SbColor4f(0, 0, 0, window ? 1 : 0));
  }
  ~Run() {
    action.setRenderTarget(nullptr);
    target.reset();
#ifdef COIN_MULTILIGHT_RTT_X11
    if (nativeWindow)
      XDestroyWindow(display, nativeWindow);
    if (display)
      XCloseDisplay(display);
#endif
  }
  bool delivery() {
    bool ok = graphDelivery(graph, audit->graph, route);
    CoinWgpuFfiFrame ffi;
    std::string error;
    const bool packed = ffi.prepare(audit->plan, extent, extent, error, false);
    ok &= check(packed, "resolved consumer ordinary FFI " + error);
    if (!packed)
      return false;
    const auto &view = ffi.getView();
    bool found = false;
    for (size_t i = 0; i < audit->plan.textures.size(); ++i) {
      const auto &t = audit->plan.textures[i];
      if (t.width != side || t.height != side)
        continue;
      found = true;
      const auto &f = view.textures[i];
      if (route == COIN_RENDER_SCENE_TEXTURE_DIRECT)
        ok &=
            check(t.gpuToken && t.pixelsRgba.empty() && t.mipmapsRgba.empty() &&
                      f.format == 1 && !f.pixels && f.pixel_bytes_len == 0 &&
                      audit->producerSamples == settings.samples,
                  "DIRECT owns GPU texture without producer CPU pixels and "
                  "retains requested samples");
      else {
        bool opaque = t.pixelsRgba.size() == side * side * 4;
        for (size_t j = 3; opaque && j < t.pixelsRgba.size(); j += 4)
          opaque = t.pixelsRgba[j] == 255;
        ok &= check(
            !t.gpuToken && opaque && f.pixel_bytes_len == side * side * 4,
            "STAGED delivers complete opaque bottom-left producer pixels");
        // Audit the real producer payload itself, independently of the
        // consumer.
        Frame producer;
        producer.rgba.resize(side * side * 4);
        producer.depth.assign(side * side, 3.f / 8);
        for (unsigned y = 0; y < side; ++y)
          std::copy_n(t.pixelsRgba.data() + size_t(side - 1 - y) * side * 4,
                      side * 4, producer.rgba.data() + size_t(y) * side * 4);
        ok &= oracle(graph.producer, producer, side, false);
      }
    }
    ok &= check(found,
                "resolved consumer actually consumes one produced texture");
    return ok;
  }
  bool draw(const char *label) {
    if (!target)
      return false;
    const auto serial = target->getLastSubmissionSerial();
    const auto submits = audit->rootSubmits, producers = audit->producers;
    const auto signature = inputs(graph.producer);
    bool requested = !window || target->requestWindowReadbackRGBA();
    std::cout << "multilight_rtt_apply_begin phase=" << label
              << " serial=" << serial << std::endl;
    action.apply(graph.root);
    Frame current, shown;
    target->readbackRGBA(current.rgba);
    if (!window)
      target->readbackDepth(current.depth);
    current.serial = target->getLastSubmissionSerial();
    bool ok = check(
        requested && action.getLastStatus() == CoinRenderAction::SUCCESS &&
            current.serial > serial && audit->rootSubmits == submits + 1 &&
            target->getOptions().sampleCount == settings.samples &&
            target->getOptions().sceneTexture == route &&
            inputs(graph.producer) == signature,
        "single public RTT apply, current publication, requested options and "
        "unmutated source");
    ok &= check(route == COIN_RENDER_SCENE_TEXTURE_DIRECT
                    ? audit->producers == producers + 1
                    : audit->producers == producers,
                "real DIRECT submitRtt or STAGED child execution route");
    if (ok)
      ok &= delivery() &&
            image(graph, current, extent, settings.samples, window, !window);
#ifdef COIN_MULTILIGHT_RTT_X11
    if (ok && window) {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(5);
      bool present = false;
      unsigned attempts = 0;
      do {
        ++attempts;
        if (physical(display, nativeWindow, extent, shown) &&
            image(graph, shown, extent, settings.samples, true, false, false)) {
          bool same = shown.rgba.size() == current.rgba.size();
          for (size_t j = 0; same && j < shown.rgba.size(); ++j)
            if (j % 4 < 3)
              same = std::abs(int(shown.rgba[j]) - int(current.rgba[j])) <= 3;
          if (same && std::chrono::steady_clock::now() <= deadline) {
            present = true;
            break;
          }
        }
      } while (std::chrono::steady_clock::now() < deadline);
      ok &= check(present, "same-apply current readback and whole physical "
                           "framebuffer within five seconds");
      std::cout << "multilight_rtt_present attempts=" << attempts
                << " current=" << present << '\n';
    }
#endif
    std::cout << "multilight_rtt phase=" << label
              << " status=" << action.getLastStatus()
              << " error=" << action.getLastError().getString() << '\n';
    if (ok) {
      authored[current.serial] = signature;
      last = std::move(current);
      lastPhysical = std::move(shown);
    }
    return ok;
  }
  bool restore(const Frame &reference, const char *label) {
    const auto found = authored.find(reference.serial);
    const bool same =
        found != authored.end() && inputs(graph.producer) == found->second;
    bool ok =
        check(same, "restoration retains exact authorial producer parameters");
    ok &= draw(label);
    if (settings.samples == 1)
      ok &= check(exact(reference, last),
                  "deterministic 1x same-environment RTT image/depth A/B/A");
    return ok;
  }
  bool retained(const Frame &reference, const Frame &physicalReference) {
    Frame current;
    target->readbackRGBA(current.rgba);
    if (!window)
      target->readbackDepth(current.depth);
    bool ok = check(target->getLastSubmissionSerial() == reference.serial &&
                        current.rgba == reference.rgba &&
                        current.depth == reference.depth,
                    "other target activity preserves exact original "
                    "publication without reapply");
#ifdef COIN_MULTILIGHT_RTT_X11
    if (window) {
      Frame shown;
      ok &= check(physical(display, nativeWindow, extent, shown) &&
                      shown.rgba == physicalReference.rgba,
                  "other target activity preserves whole original physical "
                  "framebuffer");
    }
#endif
    return ok;
  }
  bool refuse(const char *label) {
    const auto serial = target->getLastSubmissionSerial();
    const auto count = audit->rootSubmits;
    const auto producers = audit->producers;
    if (window && !target->requestWindowReadbackRGBA())
      return false;
    action.apply(graph.root);
    Frame current;
    target->readbackRGBA(current.rgba);
    if (!window)
      target->readbackDepth(current.depth);
    bool ok = check(
        action.getLastStatus() != CoinRenderAction::SUCCESS &&
            action.getLastError().getLength() > 0 &&
            target->getLastSubmissionSerial() == serial &&
            audit->rootSubmits == count && audit->producers == producers &&
            current.rgba == last.rgba && current.depth == last.depth,
        "preflight refusal: zero producers/commands and exact prior "
        "RGBA/depth/serial");
#ifdef COIN_MULTILIGHT_RTT_X11
    if (window) {
      Frame shown;
      ok &= check(physical(display, nativeWindow, extent, shown) &&
                      shown.rgba == lastPhysical.rgba,
                  "preflight refusal preserves whole exact physical image");
    }
#endif
    std::cout << "multilight_rtt_refusal phase=" << label
              << " status=" << action.getLastStatus()
              << " error=" << action.getLastError().getString() << '\n';
    return ok;
  }
  bool resize(unsigned size) {
#ifdef COIN_MULTILIGHT_RTT_X11
    if (window) {
      XResizeWindow(display, nativeWindow, size, size);
      XSync(display, False);
    }
#endif
    const bool resized = target->resize(SbVec2i32(size, size));
    extent = size;
    action.setViewportRegion(SbViewportRegion(size, size));
    std::vector<uint8_t> rgba;
    std::vector<float> depth;
    target->readbackRGBA(rgba);
    target->readbackDepth(depth);
    return check(
        resized && target->getSize() == SbVec2i32(size, size) &&
            target->getLastSubmissionSerial() == 0 && rgba.empty() &&
            depth.empty(),
        "public resize changes actual extent and invalidates old publication");
  }
};
unsigned difference(const Frame &a, const Frame &b) {
  if (a.rgba.size() != b.rgba.size())
    return 0;
  unsigned count = 0;
  for (size_t i = 0; i < a.rgba.size(); ++i)
    if (i % 4 < 3)
      count += std::abs(int(a.rgba[i]) - int(b.rgba[i])) > 3;
  return count;
}
bool mutations(Run &r) {
  auto &s = r.graph.producer;
  const Frame baseline = r.last;
  bool ok = true;
  const int last = s.setting.lights - 1;
  const double power = s.powers[last];
  s.power(last, .8);
  ok &= r.draw("RTT last light changes current producer");
  ok &= check(difference(baseline, r.last) > 100,
              "last directional light is numerically observable through RTT");
  s.power(last, power);
  ok &= r.restore(baseline, "RTT last light A/B/A");
  if (s.setting.maps) {
    s.casterAt(s.setting.maps - 1, .85);
    ok &= r.draw("RTT last associated map caster changes");
    ok &= check(difference(baseline, r.last) > 100,
                "last map affects its own light through produced texture");
    s.casterAt(s.setting.maps - 1, 0);
    ok &= r.restore(baseline, "RTT last map A/B/A");
  }
  const auto normal = s.normalBytes;
  s.setNormal({{210, 145, 205}});
  ok &= r.draw("RTT normal RGB mutation");
  ok &= check(difference(baseline, r.last) > 100,
              "actual normal map lighting reaches current consumer");
  s.setNormal(normal);
  ok &= r.restore(baseline, "RTT normal RGB A/B/A");
  const int u = s.setting.general ? 7 : 0;
  const auto texel = s.texture[u];
  s.textureAt(u, {{70, 150, 220, 255}});
  ok &= r.draw("RTT last stored texture binding mutation");
  ok &= check(difference(baseline, r.last) > 100,
              "final texture unit contributes through RTT");
  s.textureAt(u, texel);
  ok &= r.restore(baseline, "RTT last unit A/B/A");
  Frame height;
  for (int value : {20, 230}) {
    std::vector<uint8_t> raw(9, uint8_t(value));
    s.bump->image.setValue(SbVec2s(3, 3), 1, raw.data());
    s.heightNormal = true;
    s.normalBytes = {{128, 128, 255}};
    ok &= r.draw("RTT independently known flat-height conversion");
    if (value == 20)
      height = r.last;
    else if (r.settings.samples == 1)
      ok &=
          check(exact(height, r.last),
                "height offset preserves derivative and complete 1x RTT image");
  }
  s.setNormal({{128, 128, 255}});
  ok &= r.draw("RTT height versus explicit encoded flat normal");
  if (r.settings.samples == 1)
    ok &=
        check(exact(height, r.last), "height converter and explicit RGB normal "
                                     "have identical physical RTT result");
  s.setNormal(normal);
  ok &= r.restore(baseline, "RTT height source recovery");
  const unsigned char normalPair[] = {204, 128, 230, 52, 182, 225};
  const auto matrixScale = s.matrix->scaleFactor.getValue(),
             matrixTranslation = s.matrix->translation.getValue();
  s.bump->image.setValue(SbVec2s(2, 1), 3, normalPair);
  s.patternNormal = true;
  s.matrix->scaleFactor.setValue(.125f, .125f);
  s.matrix->translation.setValue(.125f, .25f);
  ok &= r.draw("RTT normal-map transform first footprint");
  const Frame footprint = r.last;
  s.matrix->translation.setValue(.625f, .25f);
  ok &= r.draw("RTT transform-only current normal-map footprint");
  ok &=
      check(difference(footprint, r.last) > 100,
            "normal UV transform alone changes produced and consumed lighting");
  s.matrix->scaleFactor = matrixScale;
  s.matrix->translation = matrixTranslation;
  s.setNormal(normal);
  ok &= r.restore(baseline, "RTT UV/matrix source restoration");
  if (s.setting.lights > 1) {
    s.lamps[last]->on = FALSE;
    ok &= r.draw("RTT light OFF snapshots do not contribute");
    ok &=
        check(difference(baseline, r.last) > 100,
              "last light OFF changes producer without disabling other lights");
    s.lamps[last]->on = TRUE;
    ok &= r.restore(baseline, "RTT light ON recovery");
    const int index = s.owners[last]->findChild(s.lamps[last]);
    s.owners[last]->removeChild(s.lamps[last]);
    ok &= r.draw("RTT last light physically removed");
    s.owners[last]->insertChild(s.lamps[last], index);
    ok &= r.restore(baseline, "RTT last source removal recovery");
  }
  return ok;
}
bool state(Run &r) {
  auto &s = r.graph.producer;
  const Frame baseline = r.last;
  bool ok = true;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  auto uv = s.uv->point[0];
  s.uv->point.set1Value(0, SbVec2f(nan, 0));
  ok &= r.refuse("last producer malformed bump UV");
  s.uv->point.set1Value(0, uv);
  ok &= r.restore(baseline, "same target UV recovery");
  auto direction = s.lamps.back()->direction.getValue();
  s.lamps.back()->direction.setValue(nan, 0, -1);
  ok &= r.refuse("last consumed producer light malformed");
  s.lamps.back()->direction = direction;
  ok &= r.restore(baseline, "same target last-light recovery");
  const auto size = r.graph.texture->size.getValue();
  r.graph.texture->size.setValue(0, side);
  ok &= r.refuse("invalid final producer dimensions");
  r.graph.texture->size = size;
  ok &= r.restore(baseline, "same target producer-dimension recovery");
  // OOM is intentionally injected after real source preparation, not preflight.
  auto *observer = r.audit;
  const auto count = r.audit->rootSubmits;
  const auto producers = r.audit->producers;
  const auto serial = r.target->getLastSubmissionSerial();
  s.setNormal({{210, 145, 205}});
  r.audit->failRoot = true;
  if (r.window)
    r.target->requestWindowReadbackRGBA();
  r.action.apply(r.graph.root);
  Frame current;
  r.target->readbackRGBA(current.rgba);
  if (!r.window)
    r.target->readbackDepth(current.depth);
  bool prepared = false;
  for (const auto &t : r.audit->attempted.textures)
    prepared |= t.width == side && t.height == side &&
                (r.route == COIN_RENDER_SCENE_TEXTURE_DIRECT
                     ? (t.gpuToken && t.pixelsRgba.empty())
                     : t.pixelsRgba.size() == side * side * 4);
  ok &= check(r.action.getLastStatus() != CoinRenderAction::SUCCESS &&
                  r.target->getStatus() == CoinRenderTarget::TARGET_ERROR &&
                  r.target->getLastSubmissionSerial() == serial &&
                  r.audit->rootSubmits == count && prepared &&
                  (r.route != COIN_RENDER_SCENE_TEXTURE_DIRECT ||
                   r.audit->producers == producers + 1) &&
                  current.rgba == r.last.rgba && current.depth == r.last.depth,
              "execution-time root OOM after real prepared RTT preserves exact "
              "parent publication");
#ifdef COIN_MULTILIGHT_RTT_X11
  if (r.window) {
    Frame shown;
    ok &= check(physical(r.display, r.nativeWindow, r.extent, shown) &&
                    shown.rgba == r.lastPhysical.rgba,
                "late execution failure preserves exact whole physical parent");
  }
#endif
  r.audit->failRoot = false;
  ok &= r.refuse("terminal OOM retry blocked before producers");
  const unsigned extent = r.extent;
  ok &= r.resize(extent);
  ok &= check(r.target->getPimpl()->backend.get() == observer,
              "public reset preserves real backend observer");
  s.setNormal({{154, 110, 246}});
  ok &= r.restore(
      baseline, "explicit public reset recovers after execution-time failure");
  return ok;
}
bool lifecycle(Run &r, const CoinRenderCapabilities &caps) {
  const Frame original = r.last, shown = r.lastPhysical;
  bool ok = true;
  {
    Run peer(r.settings, caps, r.route, false);
    peer.graph.producer.setNormal({{210, 145, 205}});
    peer.graph.producer.textureAt(peer.settings.general ? 7 : 0,
                                  {{70, 150, 220, 255}});
    ok &= peer.draw("concurrent independent RTT peer first image");
    ok &= r.retained(original, shown);
    const Frame prior = peer.last;
    auto uv = peer.graph.producer.uv->point[0];
    peer.graph.producer.uv->point.set1Value(
        0, SbVec2f(std::numeric_limits<float>::quiet_NaN(), 0));
    ok &= peer.refuse("peer invalid producer");
    ok &= r.retained(original, shown);
    peer.graph.producer.uv->point.set1Value(0, uv);
    ok &= peer.restore(prior, "peer recovery without applying original");
    ok &= r.retained(original, shown);
  }
  ok &= r.retained(original, shown);
  for (unsigned extent : {129u, 160u, side}) {
    ok &= r.resize(extent);
    ok &= r.draw("public RTT consumer resize with actual physical extent");
  }
  return ok;
}
} // namespace multi_rtt
#ifndef COIN_MULTILIGHT_RTT_NO_MAIN
int main(int argc, char **argv) {
  using namespace multi_rtt;
  Settings settings;
  bool cpuMode = false, window = false;
  CoinRenderSceneTextureMode route = COIN_RENDER_SCENE_TEXTURE_STAGED;
  std::string mode = "smoke";
  configureSamplingTest(argc, argv);
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--cpu")
      cpuMode = true;
    else if (a == "--gpu")
      cpuMode = false;
    else if (a == "--window")
      window = true;
    else if (a == "--simple")
      settings.general = false;
    else if (a == "--direct")
      route = COIN_RENDER_SCENE_TEXTURE_DIRECT;
    else if (a == "--staged")
      route = COIN_RENDER_SCENE_TEXTURE_STAGED;
    else if (a == "--portable-sampling") {
    } else if ((a == "--samples" || a == "--lights" || a == "--maps" ||
                a == "--mode") &&
               i + 1 < argc) {
      std::string value = argv[++i];
      if (a != "--mode" &&
          (value.empty() ||
           value.find_first_not_of("0123456789") != std::string::npos ||
           value.size() > 2))
        return 2;
      if (a == "--mode")
        mode = value;
      else if (a == "--samples")
        settings.samples = unsigned(std::atoi(value.c_str()));
      else if (a == "--lights")
        settings.lights = std::atoi(value.c_str());
      else
        settings.maps = std::atoi(value.c_str());
    } else
      return 2;
  }
  if ((mode != "smoke" && mode != "mutations" && mode != "state" &&
       mode != "lifecycle" && mode != "coverage") ||
      (settings.samples != 1 && settings.samples != 2 &&
       settings.samples != 4) ||
      settings.lights < 1 || settings.lights > 8 || settings.maps < 0 ||
      settings.maps > settings.lights)
    return 2;
#ifdef COIN_MULTILIGHT_RTT_X11
  if (window && !XInitThreads())
    return 1;
#else
  if (window)
    return 77;
#endif
  SoDB::init();
  CoinRenderAction::initClass();
  if (cpuMode)
    return cpu() ? 0 : 1;
  CoinRenderCapabilities caps{};
  const int query = coin_render_query_capabilities(
      window ? COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW
             : COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      &caps, sizeof(caps));
  std::cout << "multilight_rtt_environment query=" << query
            << " gpu=" << caps.gpu_available << " backend=" << caps.backend
            << " renderer=" << caps.renderer << " adapter=" << caps.adapter_name
            << '\n';
  if (query)
    return 1;
  if (!caps.gpu_available)
    return 77;
  multi_rtt::Run run(settings, caps, route, window);
  if (mode == "coverage") {
    run.graph.producerCoverage = true;
    run.graph.producer.setSpec({{0, 0, 0}});
    for (size_t i = 0; i < run.graph.producer.maps.size(); ++i)
      run.graph.producer.casterAt(int(i), 3);
    run.graph.texture->backgroundColor.setValue(233 / 255.f, 17 / 255.f,
                                                211 / 255.f, 1);
    // Source y=80.4px top crosses the 2x pattern; y=16.8px bottom
    // crosses an odd number of the rotated 4x samples. Source size is 96.
    const float v = (settings.samples == 4 ? 16.5f : 80.5f) / side;
    for (unsigned i = 0; i < 3; ++i)
      run.graph.coords->point.set1Value(i, SbVec2f(.75f, v));
  }
  bool ok = run.draw("first combined multilamp RTT source and tinted consumer");
  if (ok && mode == "mutations")
    ok &= mutations(run);
  if (ok && mode == "state")
    ok &= state(run);
  if (ok && mode == "lifecycle")
    ok &= lifecycle(run, caps);
  return ok ? 0 : 1;
}

#endif // COIN_MULTILIGHT_RTT_NO_MAIN
