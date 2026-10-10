// Direct-to-window benchmark for Coin/GL, BGFX and wgpu.
// This executable deliberately never reads pixels back to the CPU.

#include <Inventor/SoDB.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/SoSceneManager.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>

#include "CoinRenderBenchmarkAnimation.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>
#if defined(Status)
#undef Status
#endif

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct Options {
  std::string backend;
  std::string samplingPolicy = "native";
  std::string transparency;
  std::string scenePath;
  std::string animation;
  std::string samplesOutput;
  std::string imageOutput;
  int width;
  int height;
  int warmup;
  int frames;
  bool dynamic;
  bool materialDynamic;
  bool captureWindow;
  bool animationSpecified;
  bool allowVsync = false;
  int animatedPercent;
  int animationStep;
  Options() : backend("bgfx-vulkan"), transparency("object"),
    animation("static"), width(960), height(540), warmup(60), frames(600),
    dynamic(false), materialDynamic(false), captureWindow(false), animationSpecified(false),
    animatedPercent(100), animationStep(1) {}
};

struct FrameSample {
  int frameIndex;
  int64_t logicalFrame;
  bool warmup;
  double updateMs;
  double renderPresentMs;
  double totalMs;
  double eventMs;
};

double elapsedMs(Clock::time_point begin, Clock::time_point end)
{
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

struct GlxWindow {
  Display * display;
  Window window;
  Colormap colormap;
  GLXContext context;
  GlxWindow() : display(NULL), window(0), colormap(0), context(NULL) {}
};

void usage()
{
  std::cerr << "Usage: coin_render_window_benchmark"
               " --backend coin-gl|bgfx-opengl|bgfx-vulkan|wgpu-vulkan|wgpu-opengl"
               " --transparency object|weighted_oit|sorted_layers"
               " [--sampling-policy native|portable] [--allow-vsync] [--scene normalized.iv]"
               " [--width 960] [--height 540] [--warmup 60] [--frames 600]"
               " [--animation static|camera|transforms|materials|geometry]"
               " [--animated-percent 1..100] [--animation-step 1] [--samples-output frames.csv]"
               " [--dynamic|--material-dynamic] [--capture-window] [--image-output frame.ppm]\n";
}

bool parseInteger(const char * text, int & value)
{
  if (!text || !*text) return false;
  char * end = NULL;
  errno = 0;
  const long parsed = std::strtol(text, &end, 10);
  if (errno || !end || *end || parsed < INT_MIN || parsed > INT_MAX) return false;
  value = static_cast<int>(parsed);
  return true;
}

bool parseOptions(int argc, char ** argv, Options & options)
{
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc)
      options.backend = argv[++i];
    else if (std::strcmp(argv[i], "--sampling-policy") == 0 && i + 1 < argc)
      options.samplingPolicy = argv[++i];
    else if (std::strcmp(argv[i], "--transparency") == 0 && i + 1 < argc)
      options.transparency = argv[++i];
    else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc)
      options.scenePath = argv[++i];
    else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc)
      { if (!parseInteger(argv[++i], options.width)) return false; }
    else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc)
      { if (!parseInteger(argv[++i], options.height)) return false; }
    else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
      { if (!parseInteger(argv[++i], options.warmup)) return false; }
    else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
      { if (!parseInteger(argv[++i], options.frames)) return false; }
    else if (std::strcmp(argv[i], "--animation") == 0 && i + 1 < argc) {
      options.animation = argv[++i];
      options.animationSpecified = true;
    }
    else if (std::strcmp(argv[i], "--animated-percent") == 0 && i + 1 < argc)
      { if (!parseInteger(argv[++i], options.animatedPercent)) return false; }
    else if (std::strcmp(argv[i], "--animation-step") == 0 && i + 1 < argc)
      { if (!parseInteger(argv[++i], options.animationStep)) return false; }
    else if (std::strcmp(argv[i], "--samples-output") == 0 && i + 1 < argc) {
      options.samplesOutput = argv[++i];
      if (options.samplesOutput.empty()) return false;
    }
    else if (std::strcmp(argv[i], "--dynamic") == 0)
      options.dynamic = true;
    else if (std::strcmp(argv[i], "--material-dynamic") == 0)
      options.materialDynamic = true;
    else if (std::strcmp(argv[i], "--allow-vsync") == 0)
      options.allowVsync = true;
    else if (std::strcmp(argv[i], "--capture-window") == 0)
      options.captureWindow = true;
    else if (std::strcmp(argv[i], "--image-output") == 0 && i + 1 < argc)
      options.imageOutput = argv[++i];
    else return false;
  }
  const bool backend = options.backend == "coin-gl" ||
    options.backend == "bgfx-opengl" || options.backend == "bgfx-vulkan" ||
    options.backend == "wgpu-vulkan" || options.backend == "wgpu-opengl";
  const bool transparency = options.transparency == "object" ||
    options.transparency == "weighted_oit" ||
    options.transparency == "sorted_layers";
  CoinRenderBenchmarkAnimation::Mode animationMode;
  if (!backend || !transparency || (options.samplingPolicy != "native" && options.samplingPolicy != "portable") ||
      (options.backend == "coin-gl" && options.samplingPolicy != "native") ||
      !CoinRenderBenchmarkAnimation::parseMode(options.animation, animationMode) ||
      options.animatedPercent < 1 || options.animatedPercent > 100 || options.animationStep < 1 ||
      (!options.imageOutput.empty() && !options.captureWindow) ||
      (options.dynamic && options.materialDynamic) ||
      (options.animationSpecified && (options.dynamic || options.materialDynamic)) ||
      options.width < 1 || options.width > 8192 ||
      options.height < 1 || options.height > 8192 || options.warmup < 0 ||
      options.warmup > 100000 || options.frames < 1 || options.frames > 1000000)
    return false;
  // The wgpu bridge explicitly rejects weighted OIT on translucent geometry.
  return (options.backend != "coin-gl" && options.backend != "wgpu-vulkan" && options.backend != "wgpu-opengl") ||
    options.transparency != "weighted_oit";
}

void addQuad(SoSeparator * root, const SbColor & color, float alpha,
             float cx, float cy, float z, float halfSize)
{
  SoSeparator * object = new SoSeparator;
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(color);
  material->transparency.setValue(1.0f - alpha);
  object->addChild(material);
  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.set1Value(0, SbVec3f(cx - halfSize, cy - halfSize, z));
  coordinates->point.set1Value(1, SbVec3f(cx + halfSize, cy - halfSize, z));
  coordinates->point.set1Value(2, SbVec3f(cx + halfSize, cy + halfSize, z));
  coordinates->point.set1Value(3, SbVec3f(cx - halfSize, cy + halfSize, z));
  object->addChild(coordinates);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  const int32_t indices[] = { 0, 1, 2, 3, -1 };
  face->coordIndex.setValues(0, 5, indices);
  object->addChild(face);
  root->addChild(object);
}

SoSeparator * createScene(const Options & options, SoTransform ** animationOut,
                          SoCamera ** cameraOut)
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoCamera * camera;
  const bool fittedImportedCamera = options.animationSpecified && !options.scenePath.empty();
  if (!fittedImportedCamera) {
    SoOrthographicCamera * orthographic = new SoOrthographicCamera;
    orthographic->position.setValue(0.0f, 0.0f, 8.0f);
    orthographic->height = 5.5f;
    orthographic->nearDistance = 0.1f;
    orthographic->farDistance = 20.0f;
    camera = orthographic;
  } else {
    camera = new SoPerspectiveCamera;
    camera->orientation.setValue(SbRotation(SbVec3f(0.0f, 0.0f, -1.0f),
                                            SbVec3f(-0.5f, -0.35f, -1.0f)));
  }
  *cameraOut = camera;
  root->addChild(camera);
  SoLightModel * baseColor = new SoLightModel;
  baseColor->model = SoLightModel::BASE_COLOR;
  root->addChild(baseColor);
  SoTransform * animation = options.animationSpecified ? NULL : new SoTransform;
  if (animation) root->addChild(animation);
  *animationOut = animation;
  if (!options.scenePath.empty()) {
    SoInput input;
    if (!input.openFile(options.scenePath.c_str())) { root->unref(); return NULL; }
    SoSeparator * imported = SoDB::readAll(&input);
    if (!imported || imported->getNumChildren() == 0) {
      root->unref();
      return NULL;
    }
    root->addChild(imported);
    if (fittedImportedCamera)
      camera->viewAll(root, SbViewportRegion(options.width, options.height), 1.15f);
    return root;
  }

  // One opaque background plus 48 intersecting transparent objects. The
  // geometry and traversal order are identical for every backend/mode.
  addQuad(root, SbColor(0.08f, 0.10f, 0.16f), 1.0f,
          0.0f, 0.0f, -2.0f, 3.2f);
  const SbColor colors[] = {
    SbColor(0.95f, 0.16f, 0.10f), SbColor(0.08f, 0.72f, 0.28f),
    SbColor(0.10f, 0.34f, 0.95f), SbColor(0.92f, 0.72f, 0.08f)
  };
  for (int layer = 0; layer < 6; ++layer) {
    for (int item = 0; item < 8; ++item) {
      const float angle = float(item) * 0.78539816339f + float(layer) * 0.19f;
      const float radius = 0.35f + float(item % 4) * 0.48f;
      addQuad(root, colors[(layer + item) % 4], 0.22f + 0.08f * float(layer),
              std::cos(angle) * radius, std::sin(angle) * radius,
              -1.2f + float(layer) * 0.42f + float(item % 2) * 0.08f,
              0.72f + 0.06f * float(item % 3));
    }
  }
  return root;
}

bool consumeEvents(Display * display, Window window, int width, int height)
{
  bool sizeUnchanged = true;
  while (XPending(display) > 0) {
    XEvent event;
    XNextEvent(display, &event);
    if (event.type == ConfigureNotify && event.xconfigure.window == window &&
        (event.xconfigure.width != width || event.xconfigure.height != height)) {
      std::cerr << "Window drawable changed size during benchmark: requested="
        << width << 'x' << height << " actual=" << event.xconfigure.width
        << 'x' << event.xconfigure.height << '\n';
      sizeUnchanged = false;
    }
  }
  return sizeUnchanged;
}

void requestFixedWindowSize(Display * display, Window window, int width, int height)
{
  XSizeHints hints{};
  hints.flags = PMinSize | PMaxSize;
  hints.min_width = hints.max_width = width;
  hints.min_height = hints.max_height = height;
  XSetWMNormalHints(display, window, &hints);
}

bool validateWindowGeometry(Display * display, Window window, int width, int height)
{
  XSync(display, False);
  XWindowAttributes attributes{};
  if (!XGetWindowAttributes(display, window, &attributes)) {
    std::cerr << "Cannot query benchmark window drawable geometry\n";
    return false;
  }
  const int screen = DefaultScreen(display);
  const Window root = RootWindow(display, screen);
  int x = 0, y = 0;
  Window child = 0;
  const bool hasOrigin = XTranslateCoordinates(display, window, root, 0, 0, &x, &y, &child);
  long desktop = 0;
  Atom actualType = None;
  int format = 0;
  unsigned long items = 0, remaining = 0;
  unsigned char * property = NULL;
  const Atom desktopAtom = XInternAtom(display, "_NET_CURRENT_DESKTOP", True);
  if (desktopAtom != None && XGetWindowProperty(display, root, desktopAtom,
      0, 1, False, XA_CARDINAL, &actualType, &format, &items, &remaining, &property) == Success &&
      actualType == XA_CARDINAL && format == 32 && items == 1)
    desktop = static_cast<long>(*reinterpret_cast<unsigned long *>(property));
  if (property) XFree(property);
  property = NULL;
  long workarea[4] = {};
  const Atom workareaAtom = XInternAtom(display, "_NET_WORKAREA", True);
  const bool hasWorkarea = desktop >= 0 && desktop <= LONG_MAX / 4 && workareaAtom != None &&
    XGetWindowProperty(display, root, workareaAtom, desktop * 4, 4, False, XA_CARDINAL,
      &actualType, &format, &items, &remaining, &property) == Success &&
    actualType == XA_CARDINAL && format == 32 && items == 4;
  if (hasWorkarea) for (int i = 0; i < 4; ++i)
    workarea[i] = static_cast<long>(reinterpret_cast<unsigned long *>(property)[i]);
  if (property) XFree(property);
  const int outsideWorkarea = hasOrigin && hasWorkarea ?
    (int64_t(x) < workarea[0] || int64_t(y) < workarea[1] ||
     int64_t(x) + attributes.width > int64_t(workarea[0]) + workarea[2] ||
     int64_t(y) + attributes.height > int64_t(workarea[1]) + workarea[3] ? 1 : 0) : -1;
  std::cout << "window_geometry_detail requested=" << width << 'x' << height
    << " actual=" << attributes.width << 'x' << attributes.height
    << " size_policy=wm-fixed-request origin=" << x << ',' << y
    << " origin_available=" << (hasOrigin ? 1 : 0)
    << " screen=" << DisplayWidth(display, screen) << 'x' << DisplayHeight(display, screen)
    << " workarea=";
  if (hasWorkarea)
    std::cout << workarea[2] << 'x' << workarea[3] << '+' << workarea[0] << '+' << workarea[1];
  else std::cout << "unavailable";
  std::cout << " partially_outside_workarea=" << outsideWorkarea << '\n';
  if (attributes.width != width || attributes.height != height) {
    std::cerr << "Window manager did not honor the requested drawable size; benchmark aborted\n";
    return false;
  }
  return true;
}

void waitUntilMapped(Display * display, Window window)
{
  for (;;) {
    XEvent event;
    XNextEvent(display, &event);
    if (event.type == MapNotify && event.xmap.window == window) return;
  }
}

Window createXlibWindow(Display * display, int width, int height,
                        const char * title)
{
  const int screen = DefaultScreen(display);
  Window window = XCreateSimpleWindow(display, RootWindow(display, screen),
    80, 80, static_cast<unsigned int>(width), static_cast<unsigned int>(height),
    0, BlackPixel(display, screen), BlackPixel(display, screen));
  XStoreName(display, window, title);
  XSelectInput(display, window, StructureNotifyMask);
  requestFixedWindowSize(display, window, width, height);
  XMapWindow(display, window);
  XFlush(display);
  waitUntilMapped(display, window);
  return window;
}

bool createGlxWindow(int width, int height, GlxWindow & result)
{
  result.display = XOpenDisplay(NULL);
  if (!result.display) return false;
  int attributes[] = {
    GLX_RGBA, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
    GLX_ALPHA_SIZE, 8, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None
  };
  const int screen = DefaultScreen(result.display);
  XVisualInfo * visual = glXChooseVisual(result.display, screen, attributes);
  if (!visual) return false;
  result.colormap = XCreateColormap(result.display,
    RootWindow(result.display, screen), visual->visual, AllocNone);
  XSetWindowAttributes attrs;
  std::memset(&attrs, 0, sizeof(attrs));
  attrs.colormap = result.colormap;
  attrs.event_mask = StructureNotifyMask;
  result.window = XCreateWindow(result.display, RootWindow(result.display, screen),
    80, 80, static_cast<unsigned int>(width), static_cast<unsigned int>(height),
    0, visual->depth, InputOutput, visual->visual, CWColormap | CWEventMask, &attrs);
  XStoreName(result.display, result.window, "Coin window benchmark: Coin/GL");
  result.context = glXCreateContext(result.display, visual, NULL, True);
  XFree(visual);
  if (!result.context) return false;
  requestFixedWindowSize(result.display, result.window, width, height);
  XMapWindow(result.display, result.window);
  XFlush(result.display);
  waitUntilMapped(result.display, result.window);
  return glXMakeCurrent(result.display, result.window, result.context) == True;
}

void destroyGlxWindow(GlxWindow & window)
{
  if (!window.display) return;
  if (window.context) {
    glXMakeCurrent(window.display, None, NULL);
    glXDestroyContext(window.display, window.context);
  }
  if (window.window) XDestroyWindow(window.display, window.window);
  if (window.colormap) XFreeColormap(window.display, window.colormap);
  XCloseDisplay(window.display);
}

void disableGlxSwapInterval(Display * display, Window window)
{
  typedef void (*SwapIntervalExt)(Display *, GLXDrawable, int);
  typedef int (*SwapIntervalMesa)(unsigned int);
  typedef int (*SwapIntervalSgi)(int);
  SwapIntervalExt ext = reinterpret_cast<SwapIntervalExt>(
    glXGetProcAddressARB(reinterpret_cast<const GLubyte *>("glXSwapIntervalEXT")));
  if (ext) { ext(display, window, 0); return; }
  SwapIntervalMesa mesa = reinterpret_cast<SwapIntervalMesa>(
    glXGetProcAddressARB(reinterpret_cast<const GLubyte *>("glXSwapIntervalMESA")));
  if (mesa && mesa(0) == 0) return;
  SwapIntervalSgi sgi = reinterpret_cast<SwapIntervalSgi>(
    glXGetProcAddressARB(reinterpret_cast<const GLubyte *>("glXSwapIntervalSGI")));
  if (sgi) (void)sgi(0);
}

struct TimingStats {
  double median;
  double p95;
  double p99;
  double minimum;
  double maximum;
  uint64_t over60Hz;
  uint64_t over30Hz;
};

TimingStats timingStats(std::vector<double> times)
{
  TimingStats result{};
  if (times.empty()) return result;
  for (double time : times) {
    result.over60Hz += time > 1000.0 / 60.0;
    result.over30Hz += time > 1000.0 / 30.0;
  }
  std::sort(times.begin(), times.end());
  const size_t middle = times.size() / 2;
  result.median = times.size() % 2 ? times[middle] : (times[middle - 1] + times[middle]) * .5;
  result.p95 = times[(times.size() * 95 + 99) / 100 - 1];
  result.p99 = times[(times.size() * 99 + 99) / 100 - 1];
  result.minimum = times.front(); result.maximum = times.back();
  return result;
}

void reportStats(const char * prefix, const TimingStats & stats)
{
  std::cout << ' ' << prefix << "_median_ms=" << stats.median
    << ' ' << prefix << "_p95_ms=" << stats.p95
    << ' ' << prefix << "_p99_ms=" << stats.p99
    << ' ' << prefix << "_min_ms=" << stats.minimum
    << ' ' << prefix << "_max_ms=" << stats.maximum
    << ' ' << prefix << "_over_16_666667_ms=" << stats.over60Hz
    << ' ' << prefix << "_over_33_333333_ms=" << stats.over30Hz;
}

void writeSamples(std::ostream * output, const std::vector<FrameSample> & samples)
{
  if (!output) return;
  *output << "frame_index,logical_frame,warmup,t_seconds,update_ms,render_present_ms,total_ms,event_ms\n"
          << std::fixed << std::setprecision(9);
  for (const FrameSample & sample : samples)
    *output << sample.frameIndex << ',' << sample.logicalFrame << ',' << (sample.warmup ? 1 : 0)
      << ',' << double(sample.logicalFrame) / 60.0 << ',' << sample.updateMs
      << ',' << sample.renderPresentMs << ',' << sample.totalMs << ',' << sample.eventMs << '\n';
}

void updateScene(const Options & options, int frame, CoinRenderBenchmarkAnimation & animator,
                 SoTransform * legacyAnimation, SoMaterial * animatedMaterial)
{
  if (options.animationSpecified)
    animator.update(static_cast<int64_t>(frame) * options.animationStep);
  if (options.dynamic)
    legacyAnimation->rotation.setValue(SbVec3f(0.0f, 0.0f, 1.0f),
      float(frame + options.warmup + 1) * 0.0005f);
  if (options.materialDynamic)
    animatedMaterial->diffuseColor.setValue(
      0.55f + float((frame + options.warmup) % 7) * 0.035f,
      0.12f + float((frame + options.warmup) % 5) * 0.025f, 0.18f);
}

void reportFirstFrame(const Options & options, const FrameSample & sample,
                      Clock::time_point mainBegin, Clock::time_point begin,
                      Clock::time_point end)
{
  std::cout << std::fixed << std::setprecision(6)
    << "window_first_frame_detail backend=" << options.backend
    << " frame_index=" << sample.frameIndex << " logical_frame=" << sample.logicalFrame
    << " warmup=" << (sample.warmup ? 1 : 0)
    << " update_ms=" << sample.updateMs << " render_present_ms=" << sample.renderPresentMs
    << " total_ms=" << sample.totalMs << " event_ms=" << sample.eventMs
    << " before_frame_since_main_ms=" << elapsedMs(mainBegin, begin)
    << " result_since_main_ms=" << elapsedMs(mainBegin, end)
    << " includes_lazy_gpu_prepare=1 gpu_prepare_breakdown=COIN_RENDER_TRACE_PHASES\n";
}

void report(const Options & options, const std::vector<FrameSample> & samples,
            double totalMs, double finalDrainMs, CoinRenderBenchmarkAnimation & animator,
            const char * adapter, uint32_t vendor, uint32_t device)
{
  std::vector<double> updateMs, renderMs, frameMs;
  updateMs.reserve(options.frames); renderMs.reserve(options.frames); frameMs.reserve(options.frames);
  double sampleTotalMs = 0.0, eventTotalMs = 0.0;
  for (const FrameSample & sample : samples) if (!sample.warmup) {
    updateMs.push_back(sample.updateMs); renderMs.push_back(sample.renderPresentMs);
    frameMs.push_back(sample.totalMs);
    sampleTotalMs += sample.totalMs; eventTotalMs += sample.eventMs;
  }
  const TimingStats renderStats = timingStats(renderMs);
  const bool coinGl = options.backend == "coin-gl";
  std::cout << std::fixed << std::setprecision(6)
    << "window_benchmark backend=" << options.backend << " sampling_policy=" << options.samplingPolicy
    << " transparency=" << options.transparency
    << " size=" << options.width << 'x' << options.height
    << " warmup=" << options.warmup << " frames=" << options.frames
    << " scene_update=" << (options.dynamic ? "transform-each-frame" :
      options.materialDynamic ? "material-each-frame" : options.animation)
    << " animation=" << (options.dynamic ? "legacy-transform" :
      options.materialDynamic ? "legacy-material" : options.animation)
    << " animated_percent=" << options.animatedPercent
    << " animation_step=" << options.animationStep
    << " eligible=" << animator.eligibleCount() << " selected=" << animator.selectedCount()
    << " prepared_clones=" << animator.preparedCloneCount()
    << " selection_digest=0x" << std::hex << animator.selectionDigest()
    << " final_state_digest=0x" << animator.stateDigest() << std::dec
    << " scene=" << (options.scenePath.empty() ? "builtin-overlap" : options.scenePath)
    << " framing=" << (options.animationSpecified && !options.scenePath.empty() ?
      "perspective-viewAll-1.15" : "legacy-orthographic")
    << " readback=" << (options.captureWindow ? "rgba-on-request" : "none")
    << " adapter=\"" << adapter << "\""
    << " vendor_id=0x" << std::hex << vendor << " device_id=0x" << device << std::dec
    << " present_policy=" << (options.allowVsync ? "surface-default" : "off-requested")
    << " timing_scope=cpu-update-and-render-present-call"
    << " display_latency_measured=0 gpu_duration_measured=0"
    << " final_sync=" << (coinGl ? "glFinish" : "none-public-api")
    << " throughput_scope=" << (coinGl ? "cpu-loop-plus-final-GL-queue-drain" : "cpu-loop-native-queue-may-remain-pending")
    << " final_gpu_drain_ms=" << finalDrainMs
    << " cpu_frame_median_ms=" << renderStats.median
    << " cpu_frame_p95_ms=" << renderStats.p95
    << " cpu_frame_min_ms=" << renderStats.minimum
    << " cpu_frame_max_ms=" << renderStats.maximum;
  reportStats("update", timingStats(updateMs));
  reportStats("render_present", renderStats);
  reportStats("frame_total", timingStats(frameMs));
  std::cout << " sample_total_ms=" << sampleTotalMs << " event_ms=" << eventTotalMs
    << " total_ms=" << totalMs
    << " throughput_fps=" << double(options.frames) * 1000.0 / totalMs << '\n';
}

bool writeCapture(const Options & options, const std::vector<uint8_t> & rgba, bool bottomUp) {
  if (rgba.size() != size_t(options.width)*options.height*4u) return false;
  uint64_t hash = UINT64_C(14695981039346656037);
  for (uint8_t byte : rgba) { hash ^= byte; hash *= UINT64_C(1099511628211); }
  std::cout << "window_rgba_fnv64=0x" << std::hex << hash << std::dec << '\n';
  if (options.imageOutput.empty()) return true;
  std::ofstream image(options.imageOutput.c_str(), std::ios::binary);
  image << "P6\n" << options.width << ' ' << options.height << "\n255\n";
  for (int y=0; y<options.height; ++y) for (int x=0; x<options.width; ++x) {
    const size_t offset = (size_t(bottomUp ? options.height-y-1 : y)*options.width+x)*4;
    image.write(reinterpret_cast<const char *>(rgba.data()+offset),3);
  }
  return bool(image);
}

int runCoinGl(const Options & options, SoSeparator * root,
              SoTransform * animation, SoMaterial * animatedMaterial,
              CoinRenderBenchmarkAnimation & animator, Clock::time_point mainBegin,
              std::ostream * samplesOutput)
{
  const Clock::time_point windowBegin = Clock::now();
  GlxWindow glx;
  if (!createGlxWindow(options.width, options.height, glx)) {
    std::cerr << "Cannot create a direct-rendering GLX window/context\n";
    destroyGlxWindow(glx);
    return 2;
  }
  if (!validateWindowGeometry(glx.display, glx.window, options.width, options.height)) {
    destroyGlxWindow(glx);
    return 2;
  }
  disableGlxSwapInterval(glx.display, glx.window);
  const Clock::time_point windowPrepared = Clock::now();
  SoSceneManager manager;
  manager.setSceneGraph(root);
  manager.setWindowSize(SbVec2s(options.width, options.height));
  manager.setSize(SbVec2s(options.width, options.height));
  manager.setViewportRegion(SbViewportRegion(options.width, options.height));
  manager.setBackgroundColor(SbColor(0.0f, 0.0f, 0.0f));
  std::vector<uint8_t> captured;
  if (options.captureWindow) captured.resize(size_t(options.width)*options.height*4u);
  manager.getGLRenderAction()->setTransparencyType(
    options.transparency == "sorted_layers" ?
      SoGLRenderAction::SORTED_LAYERS_BLEND :
      SoGLRenderAction::SORTED_OBJECT_BLEND);
  const Clock::time_point targetPrepared = Clock::now();
  std::cout << "window_prepare_detail backend=" << options.backend
    << " capability_probe_ms=0 native_surface_context_ms=" << elapsedMs(windowBegin, windowPrepared)
    << " manager_target_setup_ms=" << elapsedMs(windowPrepared, targetPrepared)
    << " before_loop_since_main_ms=" << elapsedMs(mainBegin, targetPrepared)
    << " lazy_gpu_prepare=in_first_render_present\n";
  std::vector<FrameSample> samples;
  samples.reserve(static_cast<size_t>(options.warmup) + options.frames);
  Clock::time_point measuredBegin, firstBegin, firstEnd;
  int exitCode = 0;
  for (int frame = -options.warmup; frame < options.frames; ++frame) {
    const Clock::time_point eventBegin = Clock::now();
    if (!consumeEvents(glx.display, glx.window, options.width, options.height)) {
      exitCode = 1;
      break;
    }
    const Clock::time_point begin = Clock::now();
    updateScene(options, frame, animator, animation, animatedMaterial);
    const Clock::time_point updated = Clock::now();
    manager.render();
    if (options.captureWindow) {
      GLint readBuffer = 0, alignment = 0;
      glGetIntegerv(GL_READ_BUFFER, &readBuffer); glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
      glReadBuffer(GL_BACK); glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0,0,options.width,options.height,GL_RGBA,GL_UNSIGNED_BYTE,captured.data());
      glPixelStorei(GL_PACK_ALIGNMENT, alignment); glReadBuffer(readBuffer);
      if (glGetError() != GL_NO_ERROR) { exitCode=1; break; }
    }
    glXSwapBuffers(glx.display, glx.window);
    const Clock::time_point end = Clock::now();
    FrameSample sample{frame, static_cast<int64_t>(frame) * options.animationStep, frame < 0,
      elapsedMs(begin, updated), elapsedMs(updated, end), elapsedMs(begin, end), elapsedMs(eventBegin, begin)};
    samples.push_back(sample);
    if (frame == -options.warmup) { firstBegin = begin; firstEnd = end; }
    if (frame == 0) measuredBegin = begin;
  }
  if (!exitCode) {
    const Clock::time_point drainBegin = Clock::now();
    glFinish();
    const Clock::time_point drained = Clock::now();
    const double totalMs = elapsedMs(measuredBegin, drained);
    const char * renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    reportFirstFrame(options, samples.front(), mainBegin, firstBegin, firstEnd);
    report(options, samples, totalMs, elapsedMs(drainBegin, drained), animator,
           renderer ? renderer : "unknown OpenGL renderer", 0, 0);
    writeSamples(samplesOutput, samples);
    if (options.captureWindow && !writeCapture(options, captured, true)) exitCode=1;
  }
  destroyGlxWindow(glx);
  return exitCode;
}

int runNative(const Options & options, SoSeparator * root,
            SoTransform * animation, SoMaterial * animatedMaterial,
            CoinRenderBenchmarkAnimation & animator, Clock::time_point mainBegin,
            std::ostream * samplesOutput)
{
  const bool wgpu = options.backend == "wgpu-vulkan" || options.backend == "wgpu-opengl";
  CoinRenderOptions targetOptions;
  targetOptions.renderer = options.backend.find("opengl") != std::string::npos ?
    COIN_RENDER_RENDERER_OPENGL : COIN_RENDER_RENDERER_VULKAN;
  targetOptions.transparency = options.transparency == "weighted_oit" ? COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT :
    options.transparency == "sorted_layers" ? COIN_RENDER_TRANSPARENCY_PEELING : COIN_RENDER_TRANSPARENCY_OBJECT;
  targetOptions.textureSamplingPolicy = options.samplingPolicy == "portable" ?
    COIN_RENDER_SAMPLING_PORTABLE : COIN_RENDER_SAMPLING_NATIVE;
  if (wgpu && !options.allowVsync) setenv("COIN_RENDER_BENCH_NO_VSYNC", "1", 1);
  else if(wgpu) unsetenv("COIN_RENDER_BENCH_NO_VSYNC");
  if (!wgpu)
    setenv("COIN_BGFX_RENDERER",
      options.backend == "bgfx-opengl" ? "opengl" : "vulkan", 1);
  setenv("COIN_RENDER_TRANSPARENCY", options.transparency.c_str(), 1);
  const Clock::time_point probeBegin = Clock::now();
  CoinRenderCapabilities caps;
  std::memset(&caps, 0, sizeof(caps));
  if (coin_render_query_capabilities_for_renderer(
        COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW, targetOptions.renderer, &caps, sizeof(caps)) != 0 ||
      caps.backend != (wgpu ? COIN_RENDER_EXPERIMENTAL_RUST :
                       COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION) ||
      !caps.gpu_available) {
    std::cerr << "Requested Xlib window backend unavailable: " << caps.diagnostic << '\n';
    return 2;
  }
  if (coin_render_select_sampling_policy(&caps, targetOptions.textureSamplingPolicy, 0).reason != COIN_RENDER_SELECTION_SUPPORTED) {
    std::cerr << "Requested sampling policy unavailable\n"; return 2;
  }
  const Clock::time_point probed = Clock::now();
  Display * display = XOpenDisplay(NULL);
  if (!display) { std::cerr << "Cannot open X11 display\n"; return 2; }
  Window window = createXlibWindow(display, options.width, options.height,
    options.backend == "bgfx-opengl" ? "Coin window benchmark: BGFX/OpenGL" :
    wgpu ? "Coin window benchmark: wgpu/Vulkan" :
           "Coin window benchmark: BGFX/Vulkan");
  if (!validateWindowGeometry(display, window, options.width, options.height)) {
    XDestroyWindow(display, window);
    XFlush(display);
    return 2;
  }
  CoinRenderNativeSurfaceDescriptor surface;
  std::memset(&surface, 0, sizeof(surface));
  surface.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  surface.structSize = sizeof(surface);
  surface.type = COIN_RENDER_SURFACE_XLIB;
  surface.native.xlib.display = display;
  surface.native.xlib.window = window;
  const Clock::time_point windowPrepared = Clock::now();
  int exitCode = 0;
  {
    CoinRenderSceneManager manager(surface, SbVec2i32(options.width, options.height), targetOptions);
    if (manager.getRenderTarget()->getStatus() == CoinRenderTarget::TARGET_ERROR) {
      std::cerr << "Cannot create native target: "
                << manager.getLastError().getString() << '\n';
      exitCode = 1;
    } else {
      manager.setSceneGraph(root);
      manager.setBackgroundColor(SbColor4f(0.0f, 0.0f, 0.0f, 1.0f));
      manager.setTransparencyType(options.transparency == "sorted_layers" ?
        CoinRenderAction::SORTED_LAYERS_BLEND :
        CoinRenderAction::SORTED_OBJECT_BLEND);
      const Clock::time_point targetPrepared = Clock::now();
      std::cout << "window_prepare_detail backend=" << options.backend
        << " capability_probe_ms=" << elapsedMs(probeBegin, probed)
        << " native_surface_context_ms=" << elapsedMs(probed, windowPrepared)
        << " manager_target_setup_ms=" << elapsedMs(windowPrepared, targetPrepared)
        << " before_loop_since_main_ms=" << elapsedMs(mainBegin, targetPrepared)
        << " lazy_gpu_prepare=in_first_render_present\n";
      std::vector<FrameSample> samples;
      samples.reserve(static_cast<size_t>(options.warmup) + options.frames);
      Clock::time_point measuredBegin, firstBegin, firstEnd;
      std::vector<uint8_t> captured;
      for (int frame = -options.warmup; frame < options.frames; ++frame) {
        const Clock::time_point eventBegin = Clock::now();
        if (!consumeEvents(display, window, options.width, options.height)) {
          exitCode = 1;
          break;
        }
        const Clock::time_point begin = Clock::now();
        updateScene(options, frame, animator, animation, animatedMaterial);
        const Clock::time_point updated = Clock::now();
        if (options.captureWindow &&
            !manager.getRenderTarget()->requestWindowReadbackRGBA()) {
          std::cerr << "Window capture request failed: "
                    << manager.getRenderTarget()->getLastError() << '\n';
          exitCode = 1;
          break;
        }
        const CoinRenderAction::Status status = manager.render();
        if (status == CoinRenderAction::SUCCESS && options.captureWindow) {
          manager.getRenderTarget()->readbackRGBA(captured);
          if (captured.size() != size_t(options.width) * options.height * 4u) {
            std::cerr << "Window capture returned incomplete RGBA\n";
            exitCode = 1;
            break;
          }
        }
        const Clock::time_point end = Clock::now();
        if (status != CoinRenderAction::SUCCESS) {
          std::cerr << "Native frame failed: " << manager.getLastError().getString() << '\n';
          exitCode = 1;
          break;
        }
        samples.push_back(FrameSample{frame, static_cast<int64_t>(frame) * options.animationStep, frame < 0,
          elapsedMs(begin, updated), elapsedMs(updated, end), elapsedMs(begin, end), elapsedMs(eventBegin, begin)});
        if (frame == -options.warmup) { firstBegin = begin; firstEnd = end; }
        if (frame == 0) measuredBegin = begin;
      }
      if (!exitCode) {
        const double totalMs = elapsedMs(measuredBegin, Clock::now());
        reportFirstFrame(options, samples.front(), mainBegin, firstBegin, firstEnd);
        report(options, samples, totalMs, -1.0, animator,
          caps.adapter_name[0] ? caps.adapter_name : "native adapter", caps.vendor_id, caps.device_id);
        writeSamples(samplesOutput, samples);
        if (options.captureWindow) {
          if (!writeCapture(options, captured, false)) exitCode=1;
        }
      }
    }
  }
  XDestroyWindow(display, window);
  if (options.backend == "bgfx-opengl") {
    XCloseDisplay(display);
  } else {
    // RADV registers an Xlib close-display callback in its Vulkan DSO. BGFX
    // unloads that DSO during shutdown, so XCloseDisplay would call a stale
    // function pointer after the target has already been destroyed. This is
    // a short-lived benchmark process; the OS reclaims the X connection.
    XFlush(display);
  }
  return exitCode;
}
}

int main(int argc, char ** argv)
{
  const Clock::time_point mainBegin = Clock::now();
  Options options;
  if (!parseOptions(argc, argv, options)) { usage(); return 2; }
  std::ofstream samples;
  if (!options.samplesOutput.empty()) {
    samples.open(options.samplesOutput.c_str(), std::ios::out | std::ios::trunc);
    if (!samples) { std::cerr << "Cannot open samples output: " << options.samplesOutput << '\n'; return 2; }
  }
  const Clock::time_point initBegin = Clock::now();
  SoDB::init();
  CoinRenderAction::initClass();
  const Clock::time_point initialized = Clock::now();
  SoTransform * animation = NULL;
  SoCamera * camera = NULL;
  SoSeparator * root = createScene(options, &animation, &camera);
  if (!root) { std::cerr << "Cannot read benchmark scene\n"; return 2; }
  const Clock::time_point loaded = Clock::now();
  SoMaterial * animatedMaterial = NULL;
  if (options.materialDynamic) {
    SoSearchAction search;
    search.setType(SoMaterial::getClassTypeId());
    search.setInterest(SoSearchAction::FIRST);
    search.apply(root);
    if (search.getPath())
      animatedMaterial = static_cast<SoMaterial *>(search.getPath()->getTail());
    if (!animatedMaterial) {
      std::cerr << "Material animation requires a SoMaterial in the scene\n";
      root->unref();
      return 2;
    }
  }
  const Clock::time_point animationBegin = Clock::now();
  CoinRenderBenchmarkAnimation animator;
  CoinRenderBenchmarkAnimation::Mode mode = CoinRenderBenchmarkAnimation::Mode::STATIC;
  CoinRenderBenchmarkAnimation::parseMode(options.animation, mode);
  std::string diagnostic;
  if (!animator.initialize(root, camera, mode,
                           static_cast<unsigned>(options.animatedPercent), diagnostic)) {
    std::cerr << "Cannot prepare benchmark animation: " << diagnostic << '\n';
    root->unref();
    return 2;
  }
  const Clock::time_point animationPrepared = Clock::now();
  std::cout << std::fixed << std::setprecision(6)
    << "window_startup_detail arguments_output_ms=" << elapsedMs(mainBegin, initBegin)
    << " coin_init_ms=" << elapsedMs(initBegin, initialized)
    << " scene_load_fit_ms=" << elapsedMs(initialized, loaded)
    << " legacy_material_search_ms=" << elapsedMs(loaded, animationBegin)
    << " animation_prepare_ms=" << elapsedMs(animationBegin, animationPrepared)
    << " prepared_since_main_ms=" << elapsedMs(mainBegin, animationPrepared) << '\n';
  std::ostream * samplesOutput = options.samplesOutput.empty() ? NULL : &samples;
  int result = options.backend == "coin-gl" ?
    runCoinGl(options, root, animation, animatedMaterial, animator, mainBegin, samplesOutput) :
    runNative(options, root, animation, animatedMaterial, animator, mainBegin, samplesOutput);
  if (samplesOutput) {
    samples.flush();
    if (!samples) { std::cerr << "Cannot write samples output: " << options.samplesOutput << '\n'; result = 1; }
  }
  root->unref();
  return result;
}
