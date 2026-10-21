#include "CoinRenderBumpProfileFixture.h"
#include "actions/CoinRenderActionP.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <chrono>
#include <thread>
#include <iomanip>
#ifdef COIN_BUMP_TOTAL_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#undef None
#endif
using namespace bump_profile;
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}
struct TotalScene : Scene {
  const RGB frontDiffuse{{.2, .8, .4}};
  TotalScene() : Scene(true) {
    std::vector<uint8_t> bytes(512 * 512 * 4);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = baseColor[i % 4];
    base->image.setValue(SbVec2s(512, 512), 4, bytes.data());
    // The nearer surface is submitted first; the later rear surface must not
    // overwrite it. This witnesses depth testing even on a native window.
    auto *front = new SoSeparator;
    auto *m = new SoMaterial;
    m->diffuseColor.setValue(float(frontDiffuse[0]), float(frontDiffuse[1]), float(frontDiffuse[2]));
    m->ambientColor.setValue(0, 0, 0); m->emissiveColor.setValue(0, 0, 0);
    m->specularColor.setValue(0, 0, 0); front->addChild(m);
    auto *p = new SoCoordinate3;
    const SbVec3f points[] = {SbVec3f(-.9f, -.8f, .5f), SbVec3f(-.15f, -.8f, .5f),
                            SbVec3f(-.15f, .8f, .5f), SbVec3f(-.9f, .8f, .5f)};
    p->point.setValues(0, 4, points); front->addChild(p);
    auto *f = new SoIndexedFaceSet; const int32_t indices[] = {0, 1, 2, -1, 0, 2, 3, -1};
    f->coordIndex.setValues(0, 8, indices); front->addChild(f);
    root->insertChild(front, root->findChild(positions));
  }
  RGB frontExpected() const {
    RGB out = expected();
    for (unsigned c = 0; c < 3; ++c) out[c] *= frontDiffuse[c] / diffuse[c];
    return out;
  }
};
bool imageOracle(const TotalScene &scene, const Frame &frame, bool window,
                 bool depth, bool report = true) {
  if (frame.rgba.size() != side * side * 4 || (depth && frame.depth.size() != side * side)) return false;
  const RGB rear = scene.expected(), front = scene.frontExpected();
  unsigned counts[3] = {0, 0, 0}, bad = 0;
  const double margin = 2. / (side / 4.);
  for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
    const double wx = (x + .5 - side / 2.) * 4 / side;
    const double wy = (side / 2. - y - .5) * 4 / side;
    // Fixed analytic exclusion around the two authored silhouettes, never
    // derived from rendered pixels or enlarged in response to a backend.
    bool edge = false;
    const double xs[] = {-1.4, 1.3, -.9, -.15}, ys[] = {-1.3, 1.35, -.8, .8};
    for (double v : xs) edge |= std::abs(wx - v) < margin;
    for (double v : ys) edge |= std::abs(wy - v) < margin;
    if (edge) continue;
    const bool covered = wx > -1.4 && wx < 1.3 && wy > -1.3 && wy < 1.35;
    const bool nearer = wx > -.9 && wx < -.15 && wy > -.8 && wy < .8;
    ++counts[nearer ? 2 : covered ? 1 : 0];
    const size_t pixel = y * side + x;
    for (unsigned c = 0; c < 3; ++c) {
      const double expected = nearer ? front[c] : covered ? rear[c] : 0;
      bad += std::abs(int(frame.rgba[4 * pixel + c]) - int(std::lround(expected))) > 3;
    }
    bad += frame.rgba[4 * pixel + 3] != (covered || window ? 255 : 0);
    if (depth) bad += std::abs(frame.depth[pixel] - (nearer ? .3125f : covered ? .375f : 1.f)) > 1e-6;
  }
  const bool ok = !bad && counts[0] > 100 && counts[1] > 100 && counts[2] > 100;
  if (report) std::cout << "total_oracle bad=" << bad << " clear=" << counts[0]
                        << " rear=" << counts[1] << " nearer=" << counts[2] << '\n';
  return ok;
}
bool delivery(const TotalScene &scene, const CoinRenderFramePlan &plan) {
  unsigned bases = 0;
  for (const auto &texture : plan.textures) if (texture.width == 512 && texture.height == 512) {
    ++bases;
    if (!check(texture.pixelsRgba.size() == 512 * 512 * 4, "owned 512 squared base byte count")) return false;
    for (size_t i = 0; i < texture.pixelsRgba.size(); ++i)
      if (!check(texture.pixelsRgba[i] == scene.baseColor[i % 4], "authored base texel ownership")) return false;
  }
  if (!check(plan.bumpImages.size() == 1 && plan.bumpImages[0].width == 1 &&
             plan.bumpImages[0].height == 1 && plan.bumpImages[0].rgb.size() == 3 &&
             std::equal(scene.normal.begin(), scene.normal.end(), plan.bumpImages[0].rgb.begin()),
             "current authored A/B/A normal-map bytes captured")) return false;
  CoinWgpuFfiFrame packet; std::string error;
  if (!check(packet.prepareBumpFrame(plan, side, side, error), "complete valid bump transport: " + error)) return false;
  const auto &view = packet.getView();
  return check(bases >= 1 && plan.draws.size() == 2 && plan.indices.size() == 12 &&
               view.draw_count == 2 && view.index_count == 12 && view.bump_images &&
               view.bump_image_count == 1 && view.bump_vertex_count == view.vertex_count,
               "two authored overlapping draws and full bump stream, no extra renderer");
}
struct NativeWindow {
#ifdef COIN_BUMP_TOTAL_X11
  Display *display = nullptr; Window handle = 0;
  bool open() {
    display = XOpenDisplay(nullptr); if (!display) return false;
    handle = XCreateSimpleWindow(display, DefaultRootWindow(display), 16, 16, side, side, 0, 0, 0);
    XSetWindowAttributes attr{}; attr.override_redirect = True;
    XChangeWindowAttributes(display, handle, CWOverrideRedirect, &attr);
    XMapRaised(display, handle); XSync(display, False); return handle != 0;
  }
  ~NativeWindow() { if (display) { if (handle) XDestroyWindow(display, handle); XCloseDisplay(display); } }
  CoinRenderNativeSurfaceDescriptor descriptor() const {
    CoinRenderNativeSurfaceDescriptor d{}; d.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    d.structSize = sizeof(d); d.type = COIN_RENDER_SURFACE_XLIB;
    d.native.xlib.display = display; d.native.xlib.window = handle; return d;
  }
  static uint8_t channel(unsigned long p, unsigned long mask) {
    if (!mask) return 0; while (!(mask & 1)) { p >>= 1; mask >>= 1; }
    return uint8_t((p & mask) * 255 / mask);
  }
  bool read(Frame &frame) {
    XSync(display, False);
    XImage *image = XGetImage(display, handle, 0, 0, side, side, AllPlanes, ZPixmap);
    if (!image) return false; frame.rgba.resize(side * side * 4);
    for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
      const auto pixel = XGetPixel(image, x, y); const size_t i = (y * side + x) * 4;
      frame.rgba[i] = channel(pixel, image->red_mask); frame.rgba[i + 1] = channel(pixel, image->green_mask);
      frame.rgba[i + 2] = channel(pixel, image->blue_mask); frame.rgba[i + 3] = 255;
    }
    XDestroyImage(image); return true;
  }
#else
  bool open() { return false; }
  CoinRenderNativeSurfaceDescriptor descriptor() const { return CoinRenderNativeSurfaceDescriptor{}; }
  bool read(Frame &) { return false; }
#endif
  bool observe(const TotalScene &scene, const Frame &readback, Frame &physical) {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    do {
      bool same = read(physical) && physical.rgba.size() == readback.rgba.size();
      if (same) for (size_t i = 0; i < physical.rgba.size(); ++i)
        same &= i % 4 == 3 ? physical.rgba[i] == readback.rgba[i]
                          : std::abs(int(physical.rgba[i]) - int(readback.rgba[i])) <= 3;
      if (same && imageOracle(scene, physical, true, false, false) && Clock::now() <= deadline) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (Clock::now() < deadline);
    return false;
  }
};
void statistics(const char *stage, const std::vector<double> &all) {
  for (unsigned first : {0u, 1u}) {
    std::vector<double> sorted(all.begin() + first, all.end()); std::sort(sorted.begin(), sorted.end());
    const size_t n = sorted.size();
    const double median = (sorted[(n - 1) / 2] + sorted[n / 2]) / 2;
    const size_t rank95 = size_t(std::ceil(.95 * n)) - 1;
    std::cout << "total_statistics stage=" << stage << " population=" << (first ? "remaining31" : "all32")
              << " n=" << n << " first_ms=" << all[0] << " median_ms=" << median
              << " p95_nearest_rank_ms=" << sorted[rank95] << " max_ms=" << sorted.back() << '\n';
  }
}
}
int main(int argc, char **argv) {
  std::cout << std::unitbuf << std::setprecision(9);
  if (argc != 2) return 2;
  const std::string mode = argv[1]; const bool cpu = mode == "--cpu", window = mode == "--window";
  if (!cpu && !window && mode != "--offscreen") return 2;
#ifdef COIN_BUMP_TOTAL_X11
  if (window && !XInitThreads()) return 1;
#endif
  const auto setupStart = Clock::now();
  SoDB::init(); CoinRenderAction::initClass();
  auto options = samplingTestOptions(); options.sampleCount = 1;
  options.rasterizationPolicy = COIN_RENDER_RASTER_NATIVE;
  options.storedTextureColorSpace = COIN_RENDER_TEXTURE_LINEAR;
  if (!cpu) {
    CoinRenderCapabilities caps{};
    const int result = coin_render_query_capabilities(window ? COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW : COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps));
    std::cout << "total_environment query=" << result << " gpu=" << caps.gpu_available << " backend=" << caps.backend
              << " renderer=" << caps.renderer << " adapter=" << caps.adapter_name << " diagnostic=" << caps.diagnostic << '\n';
    if (result) return 1; if (!caps.gpu_available) return 77;
    options.renderer = static_cast<CoinRenderRenderer>(caps.renderer);
  }
  NativeWindow native; if (window && !native.open()) return 77;
  TotalScene scene;
  std::unique_ptr<CoinRenderTarget> target(window
    ? CoinRenderTarget::createWindow(native.descriptor(), SbVec2i32(side, side), options)
    : CoinRenderTarget::createOffscreen(SbVec2i32(side, side), options));
  if (!check(bool(target), "public target factory")) return 1;
  if (!window && !check(target->setDepthReadbackEnabled(TRUE), "offscreen numeric depth enabled")) return 1;
  CoinRenderAction action(SbViewportRegion(side, side)); action.setRenderTarget(target.get());
  action.setBackgroundColor(SbColor4f(0, 0, 0, window ? 1.f : 0.f));
  action.setTransparencyType(CoinRenderAction::NONE);
  action.getPimpl()->planOnly = cpu; action.getPimpl()->sceneTextureBumpProducer = cpu;
  std::cout << "total_setup mode=" << mode << " setup_ms=" << milliseconds(setupStart, Clock::now())
            << " frames=32 warmup=0 base=512x512 target=96x96 samples=1 depth_readback_supported=" << (!window)
            << " measurement=" << (cpu ? "capture_only" : "public_frame")
            << " cache_comparison=descriptive_no_public_cache_off validation=outside_timed_intervals\n";
  std::vector<double> applies, rgbaTimes, depthTimes, observations, totals;
  Frame first, firstShown; bool ok = true;
  for (unsigned frame = 0; frame < 32; ++frame) {
    if (frame == 16) scene.setNormal({{52, 182, 225}});
    if (frame == 24) scene.setNormal({{204, 128, 230}});
    if (window && !check(target->requestWindowReadbackRGBA(), "one current window readback request")) { ok = false; break; }
    const auto before = target->getLastSubmissionSerial(); Frame current, shown;
    const auto begin = Clock::now(); action.apply(scene.root); const auto applied = Clock::now();
    target->readbackRGBA(current.rgba); const auto rgbaRead = Clock::now();
    target->readbackDepth(current.depth); const auto depthRead = Clock::now();
    const bool observed = !window || native.observe(scene, current, shown); const auto end = Clock::now();
    current.serial = target->getLastSubmissionSerial();
    applies.push_back(milliseconds(begin, applied)); rgbaTimes.push_back(milliseconds(applied, rgbaRead));
    depthTimes.push_back(milliseconds(rgbaRead, depthRead)); observations.push_back(milliseconds(depthRead, end));
    totals.push_back(milliseconds(begin, end));
    std::cout << "total_frame index=" << frame << " phase=" << (frame >= 16 && frame < 24 ? 'B' : 'A')
              << " serial=" << current.serial << " status=" << action.getLastStatus()
              << " apply_ms=" << applies.back() << " rgba_ms=" << rgbaTimes.back() << " depth_ms=" << depthTimes.back()
              << " observation_ms=" << observations.back() << " total_ms=" << totals.back()
              << " error=" << action.getLastError().getString() << '\n';
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS && observed &&
               (cpu ? current.serial == 0 && current.rgba.empty() && current.depth.empty() : current.serial > before),
               "one apply succeeds, publishes current frame or capture-only performs zero submissions")) { ok = false; break; }
    ok &= delivery(scene, action.getPimpl()->lastValidPlan);
    if (!cpu) {
      ok &= check(imageOracle(scene, current, window, !window), "independent RGB alpha and authored geometric depth");
      if (window) ok &= check(current.depth.empty() && imageOracle(scene, shown, true, false), "physical current color and depth-occlusion witness");
      if (!frame) { first = current; firstShown = shown; }
      else if (frame < 16 || frame >= 24) {
        ok &= exact(first, current);
        if (window) ok &= check(firstShown.rgba == shown.rgba, "same-environment physical A restoration exact");
      } else {
        unsigned changed = 0;
        for (size_t i = 0; i < current.rgba.size(); i += 4)
          changed += std::abs(int(first.rgba[i]) - int(current.rgba[i])) > 3;
        ok &= check(changed > 500 && first.depth == current.depth, "normal-map B changes current RGB while geometry depth is constant");
      }
    }
    if (!ok) break;
  }
  action.setRenderTarget(nullptr); target.reset();
  if (ok && applies.size() == 32) {
    statistics("apply", applies); statistics("readbackRGBA", rgbaTimes); statistics("readbackDepth", depthTimes);
    if (window) statistics("physical_observation", observations);
    statistics("total", totals);
  }
  return ok && applies.size() == 32 ? 0 : 1;
}
