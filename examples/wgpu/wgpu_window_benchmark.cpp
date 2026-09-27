// Direct-to-window benchmark for Coin/GL and the experimental BGFX backend.
// This executable deliberately never reads pixels back to the CPU.

#include <Inventor/SoDB.h>
#include <Inventor/SoSceneManager.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include <Inventor/rendering/SoWgpuSceneManager.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoTransparencyType.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>
#if defined(Status)
#undef Status
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct Options {
  std::string backend;
  std::string transparency;
  int width;
  int height;
  int warmup;
  int frames;
  bool dynamic;
  Options() : backend("bgfx-vulkan"), transparency("object"),
    width(960), height(540), warmup(60), frames(600), dynamic(false) {}
};

struct GlxWindow {
  Display * display;
  Window window;
  Colormap colormap;
  GLXContext context;
  GlxWindow() : display(NULL), window(0), colormap(0), context(NULL) {}
};

void usage()
{
  std::cerr << "Usage: wgpu_window_benchmark"
               " --backend coin-gl|bgfx-opengl|bgfx-vulkan"
               " --transparency object|weighted_oit|sorted_layers"
               " [--width 960] [--height 540] [--warmup 60] [--frames 600]"
               " [--dynamic]\n";
}

bool parseOptions(int argc, char ** argv, Options & options)
{
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc)
      options.backend = argv[++i];
    else if (std::strcmp(argv[i], "--transparency") == 0 && i + 1 < argc)
      options.transparency = argv[++i];
    else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc)
      options.width = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc)
      options.height = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
      options.warmup = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
      options.frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--dynamic") == 0)
      options.dynamic = true;
    else return false;
  }
  const bool backend = options.backend == "coin-gl" ||
    options.backend == "bgfx-opengl" || options.backend == "bgfx-vulkan";
  const bool transparency = options.transparency == "object" ||
    options.transparency == "weighted_oit" ||
    options.transparency == "sorted_layers";
  if (!backend || !transparency || options.width < 1 || options.width > 8192 ||
      options.height < 1 || options.height > 8192 || options.warmup < 0 ||
      options.warmup > 100000 || options.frames < 1 || options.frames > 1000000)
    return false;
  // weighted_oit is an experimental BGFX policy with no Coin/GL equivalent.
  return options.backend != "coin-gl" || options.transparency != "weighted_oit";
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

SoSeparator * createScene(const Options & options, SoTransform ** animationOut)
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0.0f, 0.0f, 8.0f);
  camera->height = 5.5f;
  camera->nearDistance = 0.1f;
  camera->farDistance = 20.0f;
  root->addChild(camera);
  SoLightModel * baseColor = new SoLightModel;
  baseColor->model = SoLightModel::BASE_COLOR;
  root->addChild(baseColor);
  // BGFX consumes the traversal state and selects its experiment through
  // COIN_BGFX_TRANSPARENCY.  Coin/GL instead uses the action-level policy;
  // adding this node there would incorrectly override SORTED_LAYERS_BLEND.
  if (options.backend != "coin-gl") {
    SoTransparencyType * type = new SoTransparencyType;
    type->value = SoTransparencyType::SORTED_OBJECT_BLEND;
    root->addChild(type);
  }
  SoTransform * animation = new SoTransform;
  root->addChild(animation);
  *animationOut = animation;

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

void consumeEvents(Display * display)
{
  while (XPending(display) > 0) {
    XEvent event;
    XNextEvent(display, &event);
  }
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
  XMapWindow(result.display, result.window);
  XFlush(result.display);
  waitUntilMapped(result.display, result.window);
  return glXMakeCurrent(result.display, result.window, result.context) == True;
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

void report(const Options & options, const std::vector<double> & frameMs,
            double totalMs, const char * adapter)
{
  std::vector<double> sorted = frameMs;
  std::sort(sorted.begin(), sorted.end());
  const size_t median = sorted.size() / 2;
  const size_t p95 = (sorted.size() * 95 + 99) / 100 - 1;
  std::cout << std::fixed << std::setprecision(6)
    << "window_benchmark backend=" << options.backend
    << " transparency=" << options.transparency
    << " size=" << options.width << 'x' << options.height
    << " warmup=" << options.warmup << " frames=" << options.frames
    << " scene_update=" << (options.dynamic ? "transform-each-frame" : "static")
    << " readback=none adapter=\"" << adapter << "\""
    << " cpu_frame_median_ms=" << sorted[median]
    << " cpu_frame_p95_ms=" << sorted[p95]
    << " cpu_frame_min_ms=" << sorted.front()
    << " cpu_frame_max_ms=" << sorted.back()
    << " total_ms=" << totalMs
    << " throughput_fps=" << double(options.frames) * 1000.0 / totalMs << '\n';
}

int runCoinGl(const Options & options, SoSeparator * root,
              SoTransform * animation)
{
  GlxWindow glx;
  if (!createGlxWindow(options.width, options.height, glx)) {
    std::cerr << "Cannot create a direct-rendering GLX window/context\n";
    return 2;
  }
  disableGlxSwapInterval(glx.display, glx.window);
  SoSceneManager manager;
  manager.setSceneGraph(root);
  manager.setWindowSize(SbVec2s(options.width, options.height));
  manager.setSize(SbVec2s(options.width, options.height));
  manager.setViewportRegion(SbViewportRegion(options.width, options.height));
  manager.setBackgroundColor(SbColor(0.0f, 0.0f, 0.0f));
  manager.getGLRenderAction()->setTransparencyType(
    options.transparency == "sorted_layers" ?
      SoGLRenderAction::SORTED_LAYERS_BLEND :
      SoGLRenderAction::SORTED_OBJECT_BLEND);
  std::vector<double> frameMs;
  frameMs.reserve(static_cast<size_t>(options.frames));
  Clock::time_point measuredBegin;
  for (int frame = -options.warmup; frame < options.frames; ++frame) {
    if (options.dynamic)
      animation->rotation.setValue(SbVec3f(0.0f, 0.0f, 1.0f),
        float(frame + options.warmup + 1) * 0.0005f);
    consumeEvents(glx.display);
    const Clock::time_point begin = Clock::now();
    manager.render();
    glXSwapBuffers(glx.display, glx.window);
    const Clock::time_point end = Clock::now();
    if (frame == 0) measuredBegin = begin;
    if (frame >= 0)
      frameMs.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
  }
  glFinish();
  const double totalMs = std::chrono::duration<double, std::milli>(
    Clock::now() - measuredBegin).count();
  const char * renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
  report(options, frameMs, totalMs, renderer ? renderer : "unknown OpenGL renderer");
  glXMakeCurrent(glx.display, None, NULL);
  glXDestroyContext(glx.display, glx.context);
  XDestroyWindow(glx.display, glx.window);
  XFreeColormap(glx.display, glx.colormap);
  XCloseDisplay(glx.display);
  return 0;
}

int runBgfx(const Options & options, SoSeparator * root,
            SoTransform * animation)
{
  setenv("COIN_BGFX_RENDERER",
    options.backend == "bgfx-opengl" ? "opengl" : "vulkan", 1);
  setenv("COIN_BGFX_TRANSPARENCY", options.transparency.c_str(), 1);
  CoinWgpuExperimentalCapabilities caps;
  std::memset(&caps, 0, sizeof(caps));
  if (coin_wgpu_experimental_query_capabilities(
        COIN_WGPU_EXPERIMENTAL_XLIB_WINDOW, &caps, sizeof(caps)) != 0 ||
      caps.backend != COIN_WGPU_EXPERIMENTAL_BGFX_EVALUATION ||
      !caps.gpu_available) {
    std::cerr << "BGFX Xlib window backend unavailable: " << caps.diagnostic << '\n';
    return 2;
  }
  Display * display = XOpenDisplay(NULL);
  if (!display) { std::cerr << "Cannot open X11 display\n"; return 2; }
  Window window = createXlibWindow(display, options.width, options.height,
    options.backend == "bgfx-opengl" ?
      "Coin window benchmark: BGFX/OpenGL" :
      "Coin window benchmark: BGFX/Vulkan");
  SoWgpuNativeSurfaceDescriptor surface;
  std::memset(&surface, 0, sizeof(surface));
  surface.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
  surface.structSize = sizeof(surface);
  surface.type = COIN_WGPU_SURFACE_XLIB;
  surface.native.xlib.display = display;
  surface.native.xlib.window = window;
  int exitCode = 0;
  {
    SoWgpuSceneManager manager(surface, SbVec2i32(options.width, options.height));
    if (manager.getRenderTarget()->getStatus() == SoWgpuRenderTarget::TARGET_ERROR) {
      std::cerr << "Cannot create BGFX target: "
                << manager.getLastError().getString() << '\n';
      exitCode = 1;
    } else {
      manager.setSceneGraph(root);
      manager.setBackgroundColor(SbColor4f(0.0f, 0.0f, 0.0f, 1.0f));
      std::vector<double> frameMs;
      frameMs.reserve(static_cast<size_t>(options.frames));
      Clock::time_point measuredBegin;
      for (int frame = -options.warmup; frame < options.frames; ++frame) {
        if (options.dynamic)
          animation->rotation.setValue(SbVec3f(0.0f, 0.0f, 1.0f),
            float(frame + options.warmup + 1) * 0.0005f);
        consumeEvents(display);
        const Clock::time_point begin = Clock::now();
        const SoWgpuRenderAction::Status status = manager.render();
        const Clock::time_point end = Clock::now();
        if (status != SoWgpuRenderAction::SUCCESS) {
          std::cerr << "BGFX frame failed: " << manager.getLastError().getString() << '\n';
          exitCode = 1;
          break;
        }
        if (frame == 0) measuredBegin = begin;
        if (frame >= 0)
          frameMs.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
      }
      if (!exitCode) {
        const double totalMs = std::chrono::duration<double, std::milli>(
          Clock::now() - measuredBegin).count();
        report(options, frameMs, totalMs,
          caps.adapter_name[0] ? caps.adapter_name : "BGFX adapter");
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
  Options options;
  if (!parseOptions(argc, argv, options)) { usage(); return 2; }
  SoDB::init();
  SoWgpuRenderAction::initClass();
  SoTransform * animation = NULL;
  SoSeparator * root = createScene(options, &animation);
  const int result = options.backend == "coin-gl" ?
    runCoinGl(options, root, animation) : runBgfx(options, root, animation);
  root->unref();
  return result;
}
