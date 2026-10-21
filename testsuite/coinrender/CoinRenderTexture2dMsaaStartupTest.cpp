// Reuse the independent B10.3b public scene and its environment switch.
#define main coin_render_qualified_texture2d_main
#include "CoinRenderTexture2dSelectionTest.cpp"
#undef main
#include "CoinRenderBumpProfileFixture.h"
#include "rendering/coinbgfx/CoinBgfxProgramSelection.h"
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoTranslation.h>
#ifdef COIN_TEXTURE2D_STARTUP_X11
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#endif
#include "CoinRenderTexture2dStartupTrace.h"
#include "CoinRenderShutdownAuditFixture.h"
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sstream>
#include <unistd.h>
namespace startup {
struct Fixture : ::Scene {
  SoCoordinate3 *points = nullptr;
  SoFaceSet *face = nullptr;
  SoDepthBuffer *depth = new SoDepthBuffer;
  SoSeparator *tail = nullptr;
  bool extra = false, mutated = false, pairedDepth = false;
  bool advanced = false, clipped = false, hidden = false;
  int fogMode = 0, alphaMode = 0, mask = 15;
  bool writeColor = true;
  float effectiveQuality = .1f;
  SoEnvironment *environment = nullptr;
  Fixture() {
    material->ambientColor.setValue(0, 0, 0);
    material->emissiveColor.setValue(0, 0, 0);
    material->specularColor.setValue(0, 0, 0);
    depth->test = TRUE;
    depth->write = TRUE;
    depth->function = SoDepthBuffer::LESS;
    root->insertChild(depth, root->findChild(material));
    for (int i = 0; i < root->getNumChildren(); ++i) {
      auto *n = root->getChild(i);
      if (n->isOfType(SoCoordinate3::getClassTypeId()))
        points = static_cast<SoCoordinate3 *>(n);
      if (n->isOfType(SoFaceSet::getClassTypeId()))
        face = static_cast<SoFaceSet *>(n);
    }
    const SbVec3f p[] = {SbVec3f(-.8f, -.7875f, 0), SbVec3f(.8f, -.7875f, 0),
                         SbVec3f(.8f, .8f, 0)};
    points->point.setValues(0, 3, p);
    points->point.setNum(3);
    face->numVertices.set1Value(0, 3);
    tail = new SoSeparator;
    tail->ref();
    auto *t = new SoTranslation;
    t->translation.setValue(0, 0, .2f);
    tail->addChild(t);
    auto *u = new SoTextureUnit;
    u->unit = 7;
    tail->addChild(u);
    auto *x = new SoTexture2;
    const unsigned char rgba[] = {128, 192, 64, 255};
    x->image.setValue(SbVec2s(1, 1), 4, rgba);
    x->model = SoTexture2::MODULATE;
    tail->addChild(x);
    auto *z = new SoTextureUnit;
    z->unit = 0;
    tail->addChild(z);
    tail->addChild(points);
    tail->addChild(face);
  }
  ~Fixture() {
    std::cerr
        << "texture2d_startup_teardown phase=fixture_tail_unref_begin fixture="
        << static_cast<const void *>(this) << std::endl;
    tail->unref();
    std::cerr
        << "texture2d_startup_teardown phase=fixture_tail_unref_end fixture="
        << static_cast<const void *>(this) << " next=base_scene_destructor"
        << std::endl;
  }
  void fallback(bool on) {
    if (on && !extra)
      root->addChild(tail);
    if (!on && extra)
      root->removeChild(tail);
    extra = on;
  }
};
const unsigned char texels[2][16] = {
    {80, 160, 220, 255, 220, 80, 40, 255, 40, 220, 80, 255, 200, 170, 90, 255},
    {17, 210, 65, 255, 230, 35, 180, 255, 165, 85, 245, 255, 45, 160, 210,
     255}};
std::array<double, 3> texel(const Fixture &s, double wx, double wy) {
  const double u = .25 + .5 * (wx + .8) / 1.6,
               v = .25 + .5 * (wy + .7875) / 1.5875;
  const auto &p = texels[s.mutated ? 1 : 0];
  std::array<double, 3> out{};
  if (s.quality->textureQuality.getValue() < .2f) {
    int x = std::max(0, std::min(1, int(std::floor(u * 2)))),
        y = std::max(0, std::min(1, int(std::floor(v * 2))));
    for (int c = 0; c < 3; ++c)
      out[c] = p[(y * 2 + x) * 4 + c];
  } else {
    const double x = u * 2 - .5, y = v * 2 - .5;
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i) {
        const int xx = (ix + i + 2) % 2, yy = (iy + j + 2) % 2;
        for (int c = 0; c < 3; ++c)
          out[c] +=
              p[(yy * 2 + xx) * 4 + c] * (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
      }
  }
  const double primary[3] = {.8, .3, .2};
  const double extra[3] = {128 / 255., 192 / 255., 64 / 255.};
  for (int c = 0; c < 3; ++c) {
    out[c] *= primary[c];
    if (s.extra)
      out[c] *= extra[c];
  }
  return out;
}
bool analytic(const Fixture &s, const Frame &f, const Config &c) {
  if (f.rgba.size() != side * side * 4 || f.depth.size() != side * side)
    return false;
  unsigned inside = 0, outside = 0, bad = 0, depthBad = 0, partial = 0, odd = 0,
           full = 0, empty = 0;
  const double margin = 2.1 * 2 / side;
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x) {
      double wx = (x + .5) * 2 / side - 1, wy = 1 - (y + .5) * 2 / side;
      const double u = (wx + .8) / 1.6, v = (wy + .7875) / 1.5875;
      const double diagonal =
          (u - v) / std::sqrt(1 / (1.6 * 1.6) + 1 / (1.5875 * 1.5875));
      const bool in = diagonal > margin && wx > -.8 + margin &&
                      wx < .8 - margin && wy > -.7875 + margin &&
                      wy < .8 - margin;
      const bool out = diagonal < -margin || wx < -.8 - margin ||
                       wx > .8 + margin || wy < -.7875 - margin ||
                       wy > .8 + margin;
      const size_t p = size_t(y) * side + x;
      const bool awayTexel =
          std::abs(wx) > margin && std::abs(wy - .00625) > margin;
      if (in && awayTexel) {
        ++inside;
        const auto rgb = texel(s, wx, wy);
        for (int k = 0; k < 3; ++k)
          bad += std::abs(f.rgba[p * 4 + k] - rgb[k]) > 3;
        bad += f.rgba[p * 4 + 3] != 255;
      }
      if (out) {
        ++outside;
        for (int k = 0; k < 4; ++k)
          bad += f.rgba[p * 4 + k] > (k == 3 ? 1 : 3);
      }
      const double plane = (3 - (s.extra ? .2 : 0) - .1) / 9.9;
      if (in)
        depthBad += std::abs(f.depth[p] - plane) > 2e-5;
      else if (out)
        depthBad += f.depth[p] != 1;
      else
        depthBad += std::abs(f.depth[p] - plane) > 2e-5 && f.depth[p] != 1;
      double best = 1e9;
      unsigned level = 0;
      for (unsigned k = 0; k <= c.samples; ++k) {
        double e = std::abs(f.rgba[p * 4 + 3] - 255. * k / c.samples);
        if (e < best) {
          best = e;
          level = k;
        }
      }
      bad += best > 1;
      if (!level)
        ++empty;
      else if (level == c.samples)
        ++full;
      else {
        ++partial;
        odd += level % 2;
      }
    }
  std::cout << "texture2d_startup_oracle samples=" << c.samples
            << " extra_last=" << s.extra << " inside=" << inside
            << " outside=" << outside << " bad=" << bad
            << " depth_bad=" << depthBad << " partial=" << partial
            << " odd=" << odd << '\n';
  return check(
      inside > 200 && outside > 500 && bad == 0 && depthBad == 0 && empty > 0 &&
          full > 0 &&
          (c.samples == 1 || (partial > 0 && (c.samples != 4 || odd > 0))),
      "independent asymmetric texture/interior/exterior/alpha sample "
      "coverage/actual depth");
}
std::array<double, 3> advancedColor(const Fixture &s, double wx, double wy) {
  const double raw[] = {.25 + .5 * (wx + .8) / 1.6,
                        .25 + .5 * (wy + .7875) / 1.5875, 0, 1};
  double stq[4] = {0, 0, 0, 0};
  const auto matrix = s.textureMatrix->matrix.getValue();
  for (unsigned c = 0; c < 4; ++c)
    for (unsigned k = 0; k < 4; ++k)
      stq[c] += raw[k] * matrix[k][c];
  double u = stq[0] / stq[3], v = stq[1] / stq[3];
  u -= std::floor(u);
  v -= std::floor(v);
  const auto &p = texels[s.mutated ? 1 : 0];
  std::array<double, 3> texture{};
  if (s.effectiveQuality < .2f) {
    const unsigned x = std::min(1u, unsigned(u * 2)),
                   y = std::min(1u, unsigned(v * 2));
    for (unsigned c = 0; c < 3; ++c)
      texture[c] = p[(y * 2 + x) * 4 + c] / 255.;
  } else {
    const double xx = u * 2 - .5, yy = v * 2 - .5;
    const int x = int(std::floor(xx)), y = int(std::floor(yy));
    const double fx = xx - x, fy = yy - y;
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i) {
        const unsigned pixel = ((y + j + 2) % 2) * 2 + (x + i + 2) % 2;
        for (unsigned c = 0; c < 3; ++c)
          texture[c] +=
              p[pixel * 4 + c] / 255. * (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
      }
  }
  std::array<double, 3> rgb{};
  const double primary[] = {.8, .3, .2}, fog[] = {.12, .35, .6};
  const double cosine = s.model->model.getValue() == SoLightModel::PHONG
                            ? 1 / std::sqrt(1.05)
                            : 1;
  for (unsigned c = 0; c < 3; ++c) {
    const double diffuse = primary[c] * cosine;
    switch (s.textureNode->model.getValue()) {
    case SoTexture2::REPLACE:
    case SoTexture2::DECAL:
      rgb[c] = texture[c];
      break;
    case SoTexture2::BLEND:
      rgb[c] = diffuse * (1 - texture[c]) +
               s.textureNode->blendColor.getValue()[c] * texture[c];
      break;
    default:
      rgb[c] = diffuse * texture[c];
      break;
    }
    if (s.fogMode) {
      const double factor = s.fogMode == 1 ? .25
                            : s.fogMode == 2
                                ? std::exp(-5.545 * 3 / 4)
                                : std::exp(-std::pow(2.35 * 3 / 4, 2));
      rgb[c] = fog[c] * (1 - factor) + rgb[c] * factor;
    }
    rgb[c] *= 255;
  }
  return rgb;
}
bool advancedAnalytic(const Fixture &s, const Frame &f, const Config &c,
                      unsigned extent, bool window, bool report = true) {
  if (f.rgba.size() != size_t(extent) * extent * 4 ||
      (!window && f.depth.size() != size_t(extent) * extent))
    return false;
  unsigned inside = 0, outside = 0, bad = 0, depthBad = 0, partial = 0, odd = 0,
           full = 0, empty = 0, visible = 0;
  const double margin = 2.1 * 2 / extent,
               opacity = s.alphaMode == 1 ? 1 - s.material->transparency[0] : 1;
  const bool alphaWritten = s.writeColor && (s.mask & 8);
  const bool anyRgb = s.writeColor && (s.mask & 7);
  for (unsigned y = 0; y < extent; ++y)
    for (unsigned x = 0; x < extent; ++x) {
      const double wx = (x + .5) * 2 / extent - 1,
                   wy = 1 - (y + .5) * 2 / extent;
      const double a = (wx + .8) / 1.6, b = (wy + .7875) / 1.5875,
                   diagonal = (a - b) / std::sqrt(1 / (1.6 * 1.6) +
                                                  1 / (1.5875 * 1.5875));
      const bool in = diagonal > margin && wx > -.8 + margin &&
                      wx < .8 - margin && wy > -.7875 + margin &&
                      wy < .8 - margin && (!s.clipped || wx > margin);
      const bool out = s.hidden || diagonal < -margin || wx < -.8 - margin ||
                       wx > .8 + margin || wy < -.7875 - margin ||
                       wy > .8 + margin || (s.clipped && wx < -margin);
      const size_t p = size_t(y) * extent + x;
      const bool stippled = s.alphaMode == 2 && f.rgba[p * 4 + 3] == 0;
      const auto rgb = advancedColor(s, wx, wy);
      const bool awayTexel =
          s.effectiveQuality >= .2f ||
          (std::abs(wx) > margin && std::abs(wy - .00625) > margin);
      if (in && awayTexel && !s.hidden) {
        ++inside;
        visible += !stippled;
        if (!stippled)
          for (unsigned ch = 0; ch < 3; ++ch) {
            const double value =
                s.writeColor && (s.mask & (1u << ch)) ? rgb[ch] * opacity : 0;
            bad += std::abs(f.rgba[p * 4 + ch] - value) > 3;
          }
        if (!window) {
          double expected = alphaWritten && !stippled ? 255 * opacity : 0;
          bad += std::abs(f.rgba[p * 4 + 3] - expected) > 1;
        }
      }
      if (out) {
        ++outside;
        for (unsigned ch = 0; ch < 3; ++ch)
          bad += f.rgba[p * 4 + ch] > 3;
        if (!window)
          bad += f.rgba[p * 4 + 3] > 1;
      }
      if (!window) {
        const auto range = s.depth->range.getValue();
        const double plane = range[0] + (range[1] - range[0]) * (2.9 / 9.9);
        if (out || stippled)
          depthBad += f.depth[p] != 1;
        else if (in)
          depthBad += std::abs(f.depth[p] - plane) > 2e-5;
        else
          depthBad += std::abs(f.depth[p] - plane) > 2e-5 && f.depth[p] != 1;
        if (alphaWritten && !s.hidden) {
          double best = 1e9;
          unsigned level = 0;
          for (unsigned k = 0; k <= c.samples; ++k) {
            double e =
                std::abs(f.rgba[p * 4 + 3] - 255 * opacity * k / c.samples);
            if (e < best) {
              best = e;
              level = k;
            }
          }
          bad += best > 1;
          if (!level)
            ++empty;
          else if (level == c.samples)
            ++full;
          else {
            ++partial;
            odd += level % 2;
          }
        } else
          bad += f.rgba[p * 4 + 3] > 1;
      } else {
        // RGB classifies real coverage only on the nearest, opaque controls.
        // Other states retain full-buffer forced-general comparison and
        // interiors.
        if (!s.advanced && awayTexel) {
          double best = 1e9;
          unsigned level = 0;
          bool match = false;
          for (unsigned k = 0; k <= c.samples; ++k) {
            bool valid = true;
            double error = 0;
            for (unsigned ch = 0; ch < 3; ++ch) {
              double d = std::abs(f.rgba[p * 4 + ch] - rgb[ch] * k / c.samples);
              valid &= d <= 3;
              error += d * d;
            }
            if (valid && error < best) {
              best = error;
              level = k;
              match = true;
            }
          }
          bad += !match;
          if (!level)
            ++empty;
          else if (level == c.samples)
            ++full;
          else {
            ++partial;
            odd += level % 2;
          }
        }
      }
    }
  // A complete 8x8 interior tile must contain exactly half of the half-level
  // screen-door mask. This checks its independent rank count, not its GL phase.
  if (s.alphaMode == 2 && !window) {
    unsigned tiles = 0;
    for (unsigned oy = 0; oy + 8 <= extent; oy += 8)
      for (unsigned ox = 0; ox + 8 <= extent; ox += 8) {
        bool interior = true;
        unsigned count = 0;
        for (unsigned y = oy; y < oy + 8; ++y)
          for (unsigned x = ox; x < ox + 8; ++x) {
            double wx = (x + .5) * 2 / extent - 1,
                   wy = 1 - (y + .5) * 2 / extent, a = (wx + .8) / 1.6,
                   b = (wy + .7875) / 1.5875;
            interior &=
                (a - b) / std::sqrt(1 / (1.6 * 1.6) + 1 / (1.5875 * 1.5875)) >
                    margin &&
                wx > -.8 + margin && wx < .8 - margin && wy > -.7875 + margin &&
                wy < .8 - margin;
            count += f.rgba[(size_t(y) * extent + x) * 4 + 3] > 0;
          }
        if (interior) {
          ++tiles;
          bad += count != 32;
        }
      }
    bad += tiles == 0;
  }
  if (report)
    std::cout << "texture2d_startup_extended extent=" << extent
              << " inside=" << inside << " outside=" << outside
              << " bad=" << bad << " depth_bad=" << depthBad
              << " partial=" << partial << " odd=" << odd << '\n';
  const bool coverage =
      (!s.hidden && (window ? !s.advanced : alphaWritten))
          ? (empty > 0 && full > 0 &&
             (c.samples == 1 || (partial > 0 && (c.samples != 4 || odd > 0))))
          : true;
  const bool valid = (s.hidden || inside > 200) && outside > 500 && bad == 0 &&
                     depthBad == 0 && coverage &&
                     (!anyRgb || s.hidden || visible > 0);
  return report ? check(valid, "extended independent shader state, coverage, "
                               "and depth witness")
                : valid;
}
bool equivalent(const Frame &a, const Frame &b) {
  if (a.rgba.size() != b.rgba.size() || a.depth.size() != b.depth.size())
    return false;
  unsigned bad = 0;
  for (size_t i = 0; i < a.rgba.size(); ++i)
    bad += std::abs(int(a.rgba[i]) - int(b.rgba[i])) > (i % 4 == 3 ? 0 : 1);
  for (size_t i = 0; i < a.depth.size(); ++i)
    bad += std::abs(a.depth[i] - b.depth[i]) > 1e-6f;
  std::cout << "texture2d_startup_full_compare bad=" << bad << '\n';
  return check(!bad, "full framebuffer specialized/general equality: RGB1 "
                     "alpha exact depth1e-6, including edges");
}
class Counter : public BackendDestructionEnd, public bump_profile::Proxy {
public:
  using Proxy::Proxy;
  ~Counter() override {
    if(shutdownAudit){++shutdownAudit->backendsEntered;shutdownAudit->mark("backend_destructor_begin");}
  }
  CoinRenderSubmitResult submitAsync(const CoinRenderFramePlan &f,CoinRenderTargetP &t,
      CoinRenderReadbackTicket &ticket,const CoinRenderFrameReuseDecision &reuse) override {
    if(shutdownAudit)++shutdownAudit->asyncSubmits;
    return Proxy::submitAsync(f,t,ticket,reuse);
  }
  unsigned commands = 0;
  int privateMask = -1;
  bool privateWrite = true;
  CoinRenderSubmitResult edited(const CoinRenderFramePlan &input,
                                CoinRenderTargetP &target) {
    auto packet = input;
    packet.revision = 0; // new private IR, not an immutable captured revision
    for (auto &state : packet.renderStates) {
      state.colorWrite = privateWrite;
      state.colorWriteMask = uint8_t(privateMask);
    }
    return Proxy::submit(packet, target);
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,
                                CoinRenderTargetP &t) override {
    ++commands;
    return privateMask < 0 ? Proxy::submit(f, t) : edited(f, t);
  }
  CoinRenderSubmitResult
  submit(const CoinRenderFramePlan &f, CoinRenderTargetP &t,
         const CoinRenderFrameReuseDecision &reuse) override {
    ++commands;
    return privateMask < 0 ? Proxy::submit(f, t, reuse) : edited(f, t);
  }
};
bool pairedDepthImage(const Fixture &, const Frame &, const Config &, unsigned);
struct Target {
  Config config;
  ShutdownAudit *shutdownAudit=nullptr;
  // The delegating observer outlives the target that borrows its address.
  std::unique_ptr<ShutdownRuntime> shutdownRuntime;
  std::unique_ptr<CoinRenderTarget> target;
  ActionDestructionEnd actionEnd;
  CoinRenderAction action{SbViewportRegion(side, side)};
  ActionDestructionBegin actionBegin;
  Counter *counter = nullptr;
  GeneralSwitch switcher;
  bool window = false;
  unsigned extent = side;
  Frame lastPhysical;
#ifdef COIN_TEXTURE2D_STARTUP_X11
  Display *display = nullptr;
  Window nativeWindow = 0;
  int callerSocket=-1;
  struct stat callerIdentity{};
#endif
  Target(const Config &c, const CoinRenderCapabilities &caps,
         bool shown = false,ShutdownAudit *audit=nullptr)
      : config(c), shutdownAudit(audit), window(shown) {
    actionBegin.audit=actionEnd.audit=audit;
    CoinRenderOptions o{};
    o.sampleCount = c.samples;
    o.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
    if (c.canonical)
      o.rasterizationPolicy = COIN_RENDER_RASTER_CANONICAL_8BIT;
    if (c.portable)
      o.textureSamplingPolicy = COIN_RENDER_SAMPLING_PORTABLE;
#ifdef COIN_TEXTURE2D_STARTUP_X11
    if (window) {
      display = XOpenDisplay(nullptr);
      if (!display)
        return;
      if(shutdownAudit){callerSocket=ConnectionNumber(display);shutdownAudit->require(
        fstat(callerSocket,&callerIdentity)==0,"caller X11 socket identity available");}
      nativeWindow = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                         16, 16, extent, extent, 0, 0, 0);
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
      target.reset(
          CoinRenderTarget::createWindow(native, SbVec2i32(extent, extent), o));
    }
#endif
    if (!window)
      target.reset(
          CoinRenderTarget::createOffscreen(SbVec2i32(extent, extent), o));
    if (!target)
      return;
    auto &p = target->getPimpl().get();
    if(shutdownAudit){shutdownRuntime.reset(new ShutdownRuntime(*p.runtime,*shutdownAudit));p.runtime=shutdownRuntime.get();}
    if (p.prepareBackend() != CoinRenderBackendStatus::SUCCESS || !p.backend ||
        !p.backend->isGpuBackend()) {
      target.reset();
      return;
    }
    counter = new Counter(std::move(p.backend));
    counter->shutdownAudit=shutdownAudit;
    p.backend.reset(counter);
    if (!window)
      target->setDepthReadbackEnabled(TRUE);
    action.setRenderTarget(target.get());
    action.setTransparencyType(CoinRenderAction::NONE);
    action.setBackgroundColor(SbColor4f(0, 0, 0, window ? 1 : 0));
  }
  ~Target() {
    auto mark = [&](const char *phase) {
      std::cerr << "texture2d_startup_teardown phase=" << phase
                << " owner=" << static_cast<const void *>(this)
                << " window=" << window << " samples=" << config.samples
                << " target=" << static_cast<const void *>(target.get())
                << std::endl;
    };
    if(shutdownAudit&&target&&counter){
      shutdownAudit->submits=counter->commands;
      shutdownAudit->lastSerial=target->getLastSubmissionSerial();
      shutdownAudit->mark("retirement_input",shutdownAudit->lastSerial);
    }
    mark("action_detach_begin");
    action.setRenderTarget(nullptr);
    mark("action_detach_end");
    if(shutdownAudit)shutdownAudit->require(action.getRenderTarget()==nullptr,
      "action releases its borrowed target before native retirement");
    mark("target_reset_begin");
    target.reset();
    mark("target_reset_end");
#ifdef COIN_TEXTURE2D_STARTUP_X11
    if(shutdownAudit&&display){
      struct stat current{};
      shutdownAudit->require(fstat(callerSocket,&current)==0&&current.st_dev==callerIdentity.st_dev&&
        current.st_ino==callerIdentity.st_ino,"backend retirement preserves caller X11 socket");
      if(nativeWindow){XWindowAttributes attrs{};shutdownAudit->require(
        XGetWindowAttributes(display,nativeWindow,&attrs)!=0&&attrs.map_state==IsViewable,
        "backend retirement preserves mapped caller Window");}
    }
    if (nativeWindow) {
      mark("x_destroy_window_begin");
      XDestroyWindow(display, nativeWindow);
      mark("x_destroy_window_end");
    }
    if (display) {
      mark("x_close_display_begin");
      XCloseDisplay(display);
      mark("x_close_display_end");
      if(shutdownAudit){struct stat current{};const int result=fstat(callerSocket,&current);
        shutdownAudit->require((result<0&&errno==EBADF)||(result==0&&
          (current.st_dev!=callerIdentity.st_dev||current.st_ino!=callerIdentity.st_ino)),
          "caller-owned X11 socket is actually closed");}
    }
#endif
    mark("target_destructor_body_end");
  }
  bool physical(Frame &f) {
#ifdef COIN_TEXTURE2D_STARTUP_X11
    if (!window || !display || !nativeWindow)
      return false;
    XSync(display, False);
    XWindowAttributes attrs{};
    if (!XGetWindowAttributes(display, nativeWindow, &attrs) ||
        attrs.width != int(extent) || attrs.height != int(extent))
      return false;
    XImage *image = XGetImage(display, nativeWindow, 0, 0, extent, extent,
                              AllPlanes, ZPixmap);
    if (!image)
      return false;
    auto channel = [](unsigned long pixel, unsigned long mask) {
      if (!mask)
        return uint8_t(0);
      unsigned shift = 0;
      while (!(mask & 1)) {
        mask >>= 1;
        ++shift;
      }
      return uint8_t((((pixel >> shift) & mask) * 255 + mask / 2) / mask);
    };
    f.rgba.resize(size_t(extent) * extent * 4);
    for (unsigned y = 0; y < extent; ++y)
      for (unsigned x = 0; x < extent; ++x) {
        const auto pixel = XGetPixel(image, x, y);
        const size_t p = (size_t(y) * extent + x) * 4;
        f.rgba[p] = channel(pixel, image->red_mask);
        f.rgba[p + 1] = channel(pixel, image->green_mask);
        f.rgba[p + 2] = channel(pixel, image->blue_mask);
        f.rgba[p + 3] = 255;
      }
    XDestroyImage(image);
    return true;
#else
    (void)f;
    return false;
#endif
  }
  bool observe(Fixture &scene, const Frame &current) {
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds(5);
    unsigned attempts = 0, bad = 0;
    do {
      Frame shown;
      ++attempts;
      if (!physical(shown))
        continue;
      bad = 0;
      for (size_t p = 0; p < shown.rgba.size(); ++p)
        if (p % 4 != 3)
          bad += std::abs(int(shown.rgba[p]) - int(current.rgba[p])) > 3;
      if (!bad && advancedAnalytic(scene, shown, config, extent, true, false)) {
        lastPhysical = std::move(shown);
        std::cout << "texture2d_startup_physical present_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count()
                  << " attempts=" << attempts << " extent=" << extent
                  << std::endl;
        return true;
      }
    } while (std::chrono::steady_clock::now() < deadline);
    return check(false, "current complete physical X11 image within original "
                        "five seconds, RGB3 and independent oracle");
  }
  bool resize(unsigned size) {
#ifdef COIN_TEXTURE2D_STARTUP_X11
    if (window) {
      XResizeWindow(display, nativeWindow, size, size);
      XSync(display, False);
    }
#endif
    if (!target->resize(SbVec2i32(size, size)))
      return false;
    extent = size;
    action.setViewportRegion(SbViewportRegion(size, size));
    return true;
  }
  bool draw(Fixture &s, Frame &f, bool forced, const char *phase) {
    if (!target)
      return false;
    if (window && !check(target->requestWindowReadbackRGBA(),
                         "same-apply window readback requested"))
      return false;
    switcher.disableTexture2d(forced);
    Environment tracing("COIN_RENDER_TRACE_PHASES", "1");
    const auto serial = target->getLastSubmissionSerial();
    const auto count = counter->commands;
    Trace trace;
    if (!check(trace.start(), "passive RAM trace capture available"))
      return false;
    std::cout << "texture2d_startup_apply_begin phase=" << phase
              << " serial=" << serial << std::endl;
    const auto start = std::chrono::steady_clock::now();
    action.apply(s.root);
    const auto applyEnd = std::chrono::steady_clock::now();
    const auto log = trace.stop();
    f.applyMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(applyEnd - start)
            .count();
    target->readbackRGBA(f.rgba);
    if (!window)
      target->readbackDepth(f.depth);
    f.serial = target->getLastSubmissionSerial();
    std::cout << "texture2d_startup phase=" << phase
              << " status=" << action.getLastStatus()
              << " apply_ms=" << f.applyMs << " serial=" << serial << "->"
              << f.serial << " error=" << action.getLastError().getString()
              << '\n';
    bool ok = check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                        f.serial > serial && counter->commands == count + 1 &&
                        target->getOptions().sampleCount == config.samples,
                    "one real GPU apply at requested samples") &&
              (s.pairedDepth ? pairedDepthImage(s, f, config, extent)
               : (s.advanced || window || extent != side)
                   ? advancedAnalytic(s, f, config, extent, window)
                   : analytic(s, f, config)) &&
              route(log, config, forced || s.extra);
    if (ok && window)
      ok &= observe(s, f);
    return ok;
  }
};
#include "CoinRenderTexture2dDepthPairCases.h"
#ifdef COIN_TEXTURE2D_STARTUP_BGFX
bool canonicalMsaaRefusal(const Config &requested,
                          const CoinRenderCapabilities &caps) {
  Config admitted = requested;
  admitted.canonical = false;
  Fixture scene;
  Target target(admitted, caps);
  Frame stable, after, recovery;
  bool ok =
      target.draw(scene, stable, false, "admitted native MSAA predecessor");
  if (!ok)
    return false;
  // A target's raster policy is immutable. This mismatched packet never
  // reaches a backend; it is distinct from backend canonical-MSAA support.
  auto packet = target.counter->plan;
  packet.revision = 0;
  packet.rasterizationPolicy = COIN_RENDER_RASTER_CANONICAL_8BIT;
  const auto serial = target.target->getLastSubmissionSerial();
  const auto commands = target.counter->commands;
  const auto result = target.target->getPimpl()->executeFrame(packet);
  target.target->readbackRGBA(after.rgba);
  target.target->readbackDepth(after.depth);
  std::cout << "texture2d_startup_immutable_policy status="
            << unsigned(result.status) << " error=" << result.diagnostic
            << std::endl;
  ok &= check(result.status == CoinRenderBackendStatus::UNSUPPORTED &&
                  result.diagnostic.find("immutable target policy") !=
                      std::string::npos &&
                  target.counter->commands == commands &&
                  target.target->getLastSubmissionSerial() == serial &&
                  stable.rgba == after.rgba && stable.depth == after.depth,
              "immutable raster-policy mismatch refuses before backend with "
              "exact old publication");
  {
    Target canonical(requested, caps);
    if (!check(bool(canonical.target),
               "public canonical target exists before profile admission"))
      return false;
    canonical.action.apply(scene.root);
    Frame unpublished;
    canonical.target->readbackRGBA(unpublished.rgba);
    canonical.target->readbackDepth(unpublished.depth);
    std::cout << "texture2d_startup_public_canonical_msaa status="
              << canonical.action.getLastStatus()
              << " backend_entries=" << canonical.counter->commands
              << " serial=" << canonical.target->getLastSubmissionSerial()
              << " error=" << canonical.action.getLastError().getString()
              << std::endl;
    ok &= check(canonical.action.getLastStatus() ==
                        CoinRenderAction::UNSUPPORTED &&
                    std::string(canonical.action.getLastError().getString())
                            .find("native RGBA8 linear geometry") !=
                        std::string::npos &&
                    canonical.counter->producers == 0 &&
                    canonical.target->getLastSubmissionSerial() == 0 &&
                    unpublished.rgba.empty() && unpublished.depth.empty(),
                "existing BGFX canonical MSAA domain rejects public input "
                "without a publication or producer");
  }
  target.target->readbackRGBA(after.rgba);
  target.target->readbackDepth(after.depth);
  ok &= check(target.target->getLastSubmissionSerial() == serial &&
                  target.counter->commands == commands &&
                  after.rgba == stable.rgba && after.depth == stable.depth,
              "canonical peer refusal/destruction leaves Native publication "
              "exact without applying it");
  ok &= target.draw(scene, recovery, false,
                    "admitted native recovery after canonical peer refusal");
  ok &= equivalent(stable, recovery);
  return ok;
}
#endif
bool states(Target &target, Fixture &scene) {
  scene.advanced = true;
  Frame specialized, general, restored;
  bool ok = true;
  auto pair = [&](const char *label) {
    bool current = target.draw(scene, specialized, false, label);
    current &= target.draw(scene, general, true, label);
    current &= equivalent(specialized, general);
    const auto &plan = target.counter->plan;
    const auto expectedFilter =
        scene.effectiveQuality < .2f   ? CoinRenderTextureFilter::NEAREST
        : scene.effectiveQuality < .5f ? CoinRenderTextureFilter::LINEAR
        : scene.effectiveQuality < .8f
            ? CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR
            : CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR;
    const bool mips = scene.effectiveQuality >= .5f;
    current &= check(plan.samplers.size() == 1 &&
                         plan.samplers[0].filter == expectedFilter &&
                         plan.textures.size() == 1 &&
                         plan.textures[0].mipmapped == mips &&
                         (!mips || !plan.textures[0].mipmapsRgba.empty()),
                     "effective upload quality, sampler and required mip "
                     "payload are actually captured");
    ok &= current;
  };
  // Coin retains quality for one image-upload revision. A complexity-only
  // edit must preserve the old sampler; publishing the same bytes starts the
  // new authored upload and adopts the new quality.
  scene.quality->textureQuality = .35f;
  pair("quality-only edit retains uploaded nearest sampler");
  scene.textureNode->image.touch();
  scene.effectiveQuality = .35f;
  pair("same-byte republication adopts linear sampler");
  // All public filters have magnification footprints here. Mips are present
  // for the two mipmapped samplers; this does not claim a positive mip LOD.
  for (float quality : {.1f, .3f, .6f, .9f}) {
    scene.quality->textureQuality = quality;
    scene.textureNode->image.touch();
    scene.effectiveQuality = quality;
    pair("public texture filter at magnification");
  }
  scene.quality->textureQuality = .3f;
  scene.textureNode->image.touch();
  scene.effectiveQuality = .3f;
  scene.textureNode->blendColor.setValue(.2f, .6f, .1f);
  for (int model : {int(SoTexture2::MODULATE), int(SoTexture2::DECAL),
                    int(SoTexture2::BLEND), int(SoTexture2::REPLACE)}) {
    scene.textureNode->model = model;
    pair("public texture model");
  }
  scene.textureNode->model = SoTexture2::MODULATE;
  scene.model->model = SoLightModel::PHONG;
  pair("opaque Phong geometric lighting");
  scene.model->model = SoLightModel::BASE_COLOR;
  SbMatrix projective = SbMatrix::identity();
  projective[0][0] = .75f;
  projective[1][1] = .8f;
  projective[3][0] = .1f;
  projective[3][1] = .05f;
  projective[3][3] = 1.25f;
  scene.textureMatrix->matrix = projective;
  pair("independent once-only projective STQ matrix");
  scene.textureMatrix->matrix = SbMatrix::identity();
  scene.environment = new SoEnvironment;
  scene.environment->fogColor.setValue(.12f, .35f, .6f);
  scene.environment->fogVisibility = 4;
  scene.root->insertChild(scene.environment,
                          scene.root->findChild(scene.material));
  const int fogs[] = {SoEnvironment::HAZE, SoEnvironment::FOG,
                      SoEnvironment::SMOKE};
  for (int i = 0; i < 3; ++i) {
    scene.fogMode = i + 1;
    scene.environment->fogType = fogs[i];
    pair("geometric view-distance fog after texture");
  }
  scene.fogMode = 0;
  scene.environment->fogType = SoEnvironment::NONE;
  auto *clip = new SoClipPlane;
  clip->plane = SbPlane(SbVec3f(1, 0, 0), 0);
  scene.root->insertChild(clip, scene.root->findChild(scene.material));
  scene.clipped = true;
  pair("authored half-plane clip");
  scene.root->removeChild(clip);
  scene.clipped = false;
  for (int function : {int(SoDepthBuffer::LESS), int(SoDepthBuffer::LEQUAL)}) {
    scene.depth->function = function;
    scene.depth->range.setValue(.2f, .8f);
    pair("explicit depth function and authored range");
  }
  auto *offset = new SoPolygonOffset;
  offset->on = TRUE;
  offset->styles = SoPolygonOffset::FILLED;
  offset->factor = 1;
  offset->units = 1;
  scene.root->insertChild(offset, scene.root->findChild(scene.material));
  // A planar triangle has zero slope. One depth unit remains below the
  // established depth2e-5 bound; full general comparison stays depth1e-6.
  pair("explicit polygon offset with custom range");
  bool offsetDelivered = false;
  for (const auto &state : target.counter->plan.renderStates)
    offsetDelivered |= state.hasTexture && state.polygonOffsetEnabled &&
                       state.polygonOffsetFactor == 1 &&
                       state.polygonOffsetUnits == 1;
  ok &= check(offsetDelivered, "authored offset is actually consumed");
  scene.root->removeChild(offset);
  scene.depth->range.setValue(0, 1);
  scene.depth->function = SoDepthBuffer::LESS;
  scene.alphaTest->function = SoAlphaTest::NEVER;
  scene.hidden = true;
  pair("alpha test discards all covered samples");
  scene.hidden = false;
  scene.alphaTest->function = SoAlphaTest::ALWAYS;
  scene.material->transparency = .5f;
  scene.alphaMode = 1;
  target.action.setTransparencyType(CoinRenderAction::BLEND);
  pair("object alpha blend with independent clear composition");
  scene.alphaMode = 2;
  target.action.setTransparencyType(CoinRenderAction::SCREEN_DOOR);
  pair("screen-door mask and depth preservation");
  scene.alphaMode = 0;
  scene.material->transparency = 0;
  target.action.setTransparencyType(CoinRenderAction::NONE);
  scene.quality->textureQuality = .1f;
  scene.textureNode->image.touch();
  scene.effectiveQuality = .1f;
  pair("restored opaque parameters");
  // The forced-general draw was the last successful publication. Refusal
  // must preserve that exact buffer, not an earlier specialized execution.
  const Frame stable = general;
  const auto serial = target.target->getLastSubmissionSerial();
  const auto commands = target.counter->commands;
  scene.alphaTest->value = std::numeric_limits<float>::quiet_NaN();
  target.action.apply(scene.root);
  Frame rejected;
  target.target->readbackRGBA(rejected.rgba);
  target.target->readbackDepth(rejected.depth);
  rejected.serial = target.target->getLastSubmissionSerial();
  ok &= check(
      target.action.getLastStatus() != CoinRenderAction::SUCCESS &&
          rejected.serial == serial && target.counter->commands == commands &&
          rejected.rgba == stable.rgba && rejected.depth == stable.depth,
      "nonfinite alpha-test input refuses before commands and preserves exact "
      "publication");
  scene.alphaTest->value = .5f;
  ok &= target.draw(scene, restored, false,
                    "same-target valid recovery after refusal");
  ok &= equivalent(stable, restored);
  return ok;
}
bool refuse(Target &target, Fixture &scene, const Frame &published) {
  const auto serial = target.target->getLastSubmissionSerial();
  const auto commands = target.counter->commands;
  const auto physicalBefore = target.lastPhysical.rgba;
  scene.alphaTest->value = std::numeric_limits<float>::quiet_NaN();
  if (target.window && !target.target->requestWindowReadbackRGBA())
    return false;
  target.action.apply(scene.root);
  Frame after;
  target.target->readbackRGBA(after.rgba);
  if (!target.window)
    target.target->readbackDepth(after.depth);
  bool ok = check(
      target.action.getLastStatus() != CoinRenderAction::SUCCESS &&
          target.target->getLastSubmissionSerial() == serial &&
          target.counter->commands == commands &&
          after.rgba == published.rgba && after.depth == published.depth,
      "invalid input preserves exact current publication before commands");
  if (target.window) {
    Frame physical;
    ok &= check(target.physical(physical) && physical.rgba == physicalBefore,
                "refusal leaves complete physical image bitwise intact");
  }
  scene.alphaTest->value = .5f;
  return ok;
}
bool lifecycle(Target &target, Fixture &scene,
               const CoinRenderCapabilities &caps) {
  Frame original, changed, restored;
  bool ok =
      target.draw(scene, original, false, "lifecycle original publication");
  const auto originalSerial = target.target->getLastSubmissionSerial();
  const auto originalPhysical = target.lastPhysical.rgba;
  {
    Fixture peerScene;
    Target peer(target.config, caps);
    Frame a, b, recovery;
    ok &= peer.draw(peerScene, a, false, "independent peer publication");
    peerScene.mutateTexture();
    peerScene.mutated = true;
    ok &= peer.draw(peerScene, b, false, "independent peer source mutation");
    ok &= refuse(peer, peerScene, b);
    ok &= peer.draw(peerScene, recovery, false, "independent peer recovery");
    ok &= equivalent(b, recovery);
  }
  Frame untouched;
  target.target->readbackRGBA(untouched.rgba);
  if (!target.window)
    target.target->readbackDepth(untouched.depth);
  ok &= check(target.target->getLastSubmissionSerial() == originalSerial &&
                  untouched.rgba == original.rgba &&
                  untouched.depth == original.depth,
              "peer mutation/refusal/recovery/destruction leaves original "
              "publication exact without applying it");
  if (target.window) {
    Frame shown;
    ok &= check(target.physical(shown) && shown.rgba == originalPhysical,
                "peer lifetime leaves complete original X11 image exact");
  }
  scene.mutateTexture();
  scene.mutated = true;
  ok &= target.draw(scene, changed, false,
                    "observable asymmetric source mutation");
  unsigned difference = 0;
  for (size_t i = 0; i < changed.rgba.size(); ++i)
    difference += i % 4 != 3 &&
                  std::abs(int(changed.rgba[i]) - int(original.rgba[i])) > 6;
  ok &= check(difference > 100,
              "authored source mutation has measurable RGB contrast");
  ok &= refuse(target, scene, changed);
  ok &= target.draw(scene, restored, false,
                    "new valid publication after refusal");
  ok &= equivalent(changed, restored);
  scene.textureNode->image.setValue(SbVec2s(2, 2), 4, texels[0]);
  scene.mutated = false;
  ok &= target.draw(scene, restored, false, "source A/B/A restoration");
  ok &= equivalent(original, restored);
  for (unsigned extent : {96u, 129u, 64u}) {
    ok &= check(target.resize(extent), "public target resize");
    ok &= target.draw(scene, changed, false, "current real resized extent");
    ok &= target.draw(scene, restored, true,
                      "resized extent forced-general reference");
    ok &= equivalent(changed, restored);
  }
  return ok;
}
bool masks(Target &target, Fixture &scene) {
  scene.advanced = true;
  bool ok = true;
  Frame optimized, general, recovery;
  for (int mask : {0, 1, 2, 4, 5, 8, 15})
    for (bool write : {true, false}) {
      scene.mask = mask;
      scene.writeColor = write;
      target.counter->privateMask = mask;
      target.counter->privateWrite = write;
      ok &=
          target.draw(scene, optimized, false, "private authored channel mask");
      ok &=
          target.draw(scene, general, true, "same private mask forced general");
      ok &= equivalent(optimized, general);
      bool found = false;
      for (const auto &state : target.counter->plan.renderStates)
        if (state.hasTexture) {
          found = true;
          ok &= check(state.colorWrite == write && state.colorWriteMask == mask,
                      "actual consumed private channel/write state");
        }
      ok &= check(found, "masked packet still consumes the authored texture");
    }
  target.counter->privateMask = -1;
  scene.mask = 15;
  scene.writeColor = true;
  ok &=
      target.draw(scene, recovery, false, "full write recovery on same target");
  auto invalid = target.counter->plan;
  invalid.revision = 0;
  for (auto &state : invalid.renderStates)
    state.colorWriteMask = 0x10;
  const auto serial = target.target->getLastSubmissionSerial();
  const auto commands = target.counter->commands;
  const auto result = target.target->getPimpl()->executeFrame(invalid);
  Frame after;
  target.target->readbackRGBA(after.rgba);
  if (!target.window)
    target.target->readbackDepth(after.depth);
  ok &= check(result.status != CoinRenderBackendStatus::SUCCESS &&
                  target.counter->commands == commands &&
                  target.target->getLastSubmissionSerial() == serial &&
                  after.rgba == recovery.rgba && after.depth == recovery.depth,
              "unknown private mask bit rejected by common ingress with exact "
              "old publication");
  if (target.window) {
    Frame shown;
    ok &=
        check(target.physical(shown) && shown.rgba == target.lastPhysical.rgba,
              "private mask refusal preserves whole physical image exactly");
  }
  ok &= target.draw(scene, optimized, false,
                    "valid full-write recovery after mask refusal");
  ok &= equivalent(recovery, optimized);
  return ok;
}
bool core() {
  CoinBgfxDraw d{};
  d.hasTexture = true;
  d.alpha = 1;
  d.alphaTestFunction = CoinRenderAlphaTestFunction::NONE;
  d.alphaTestReference = .5f;
  d.transparencyStrategy = CoinBgfxTransparencyStrategy::OBJECT;
  std::vector<CoinBgfxDraw> draws(3, d);
  draws[0].hasTexture = false;
  bool ok = check(coin_bgfx_texture2d_program(
                      draws, false, CoinBgfxTransparencyStrategy::OBJECT),
                  "mixed ordinary/texture2D last draw is eligible");
  for (unsigned mask = 0; mask < 16; ++mask)
    for (bool write : {false, true}) {
      auto v = draws;
      v.back().colorWrite = write;
      v.back().colorWriteMask = uint8_t(mask);
      v.back().depthFunction = mask % 2 ? CoinRenderDepthFunction::LESS
                                        : CoinRenderDepthFunction::LEQUAL;
      v.back().textureProjection =
          mask % 2 ? CoinRenderTextureProjection::DIRECT_ST
                   : CoinRenderTextureProjection::PROJECTIVE;
      ok &= check(coin_bgfx_texture2d_program(
                      v, false, CoinBgfxTransparencyStrategy::OBJECT),
                  "all legal channel masks/depth/texture projections retain "
                  "eligible family");
    }
  for (unsigned last : {0u, 2u})
    for (unsigned variant = 0; variant < 9; ++variant) {
      auto v = draws;
      auto &x = v[last];
      switch (variant) {
      case 0:
        x.extraTextures[6].enabled = true;
        break;
      case 1:
        x.cubeFaceSize = 2;
        x.cubeSourceComponents = 4;
        break;
      case 2:
        x.volumeWidth = x.volumeHeight = x.volumeDepth = 2;
        break;
      case 3:
        x.instanceCount = 1;
        break;
      case 4:
        x.transparencyStrategy = CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
        break;
      case 5:
        x.alpha = std::numeric_limits<float>::quiet_NaN();
        break;
      case 6:
        x.alphaTestReference = std::numeric_limits<float>::infinity();
        break;
      case 7:
        x.fogRange[2] = -std::numeric_limits<float>::infinity();
        break;
      default:
        x.clipMeta[3] = std::numeric_limits<float>::quiet_NaN();
      }
      ok &= check(
          !coin_bgfx_texture2d_program(v, false,
                                       CoinBgfxTransparencyStrategy::OBJECT),
          "first or LAST incompatible/nonfinite item cannot select texture2D");
    }
  ok &= check(
      !coin_bgfx_texture2d_program(draws, true,
                                   CoinBgfxTransparencyStrategy::OBJECT) &&
          !coin_bgfx_texture2d_program(
              draws, false, CoinBgfxTransparencyStrategy::SORTED_LAYERS) &&
          !coin_bgfx_texture2d_program({}, false,
                                       CoinBgfxTransparencyStrategy::OBJECT),
      "shadows/peeling/empty keep general selection");
  for (auto &x : draws)
    x.hasTexture = false;
  ok &= check(!coin_bgfx_texture2d_program(
                  draws, false, CoinBgfxTransparencyStrategy::OBJECT),
              "no texture does not claim texture2D family");
  return ok;
}
} // namespace startup
int main(int argc, char **argv) {
  startup::Config c;
  bool cpu = false, window = false, auditShutdown = false;
  std::string mode = "first";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--cpu")
      cpu = true;
    else if (a == "--window")
      window = true;
    else if (a == "--audit-shutdown")
      auditShutdown = true;
    else if (a == "--gpu")
      cpu = false;
    else if (a == "--baseline")
      c.baseline = true;
    else if (a == "--portable-sampling")
      c.portable = true;
    else if (a == "--canonical-raster")
      c.canonical = true;
    else if ((a == "--samples" || a == "--mode") && i + 1 < argc) {
      std::string v = argv[++i];
      if (a == "--mode")
        mode = v;
      else {
        if (v != "1" && v != "2" && v != "4")
          return 2;
        c.samples = unsigned(std::atoi(v.c_str()));
      }
    } else
      return 2;
  }
  if (mode != "first" && mode != "fallback" && mode != "states" &&
      mode != "lifecycle" && mode != "masks" && mode != "depthmix")
    return 2;
  if (auditShutdown && (cpu || !window || mode != "masks" || c.samples != 2))
    return 2;
  if (window && (mode == "states" || mode == "depthmix"))
    return 2;
#ifdef COIN_TEXTURE2D_STARTUP_X11
  if (window && !XInitThreads())
    return 1;
#else
  if (window)
    return 77;
#endif
  SoDB::init();
  CoinRenderAction::initClass();
  if (cpu)
    return startup::core() ? 0 : 1;
  CoinRenderCapabilities caps{};
  int q = coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
                                         &caps, sizeof(caps));
  std::cout << "texture2d_startup_environment query=" << q
            << " gpu=" << caps.gpu_available << " renderer=" << caps.renderer
            << " adapter=" << caps.adapter_name << '\n';
  if (q)
    return 1;
  if (!caps.gpu_available)
    return 77;
#ifdef COIN_TEXTURE2D_STARTUP_BGFX
  if (c.canonical && c.samples > 1)
    return startup::canonicalMsaaRefusal(c, caps) ? 0 : 1;
#endif
  startup::ShutdownAudit shutdownAudit;
  bool ok=true;
  {
  startup::Fixture scene;
  startup::Target target(c, caps, window, auditShutdown?&shutdownAudit:nullptr);
  ::Frame first, general, back;
  ok = target.draw(scene, first, false, "first cold eligible 2D draw");
  ok &= target.draw(scene, general, true, "same input forced general");
  ok &= startup::equivalent(first, general);
  ok &= target.draw(scene, back, false, "same-target family A/B/A");
  ok &= startup::equivalent(first, back);
  if (mode == "fallback") {
    scene.fallback(true);
    ok &= target.draw(scene, first, false,
                      "last shared-geometry occurrence has unit7");
    ok &= target.draw(scene, general, true,
                      "same last-item fallback forced general");
    ok &= startup::equivalent(first, general);
    scene.fallback(false);
    ok &= target.draw(scene, back, false, "general to eligible2D restoration");
  }
  if (mode == "states")
    ok &= startup::states(target, scene);
  if (mode == "lifecycle")
    ok &= startup::lifecycle(target, scene, caps);
  if (mode == "masks")
    ok &= startup::masks(target, scene);
  if (mode == "depthmix")
    ok &= startup::depthPair(target, scene);
  std::cerr << "texture2d_startup_teardown phase=main_return result="
            << (ok ? 0 : 1) << " mode=" << mode << " window=" << window
            << " samples=" << c.samples << std::endl;
  } // Target (including Action) and scene must finish before the audit verdict.
  if(auditShutdown)ok=shutdownAudit.finish()&&ok;
  return ok ? 0 : 1;
}
