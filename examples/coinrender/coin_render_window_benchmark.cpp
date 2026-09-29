// Direct-to-window benchmark for Coin/GL, BGFX and wgpu.
// This executable deliberately never reads pixels back to the CPU.

#include <Inventor/SoDB.h>
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
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>
#if defined(Status)
#undef Status
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
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
  std::string scenePath;
  int width;
  int height;
  int warmup;
  int frames;
  bool dynamic;
  bool materialDynamic;
  bool captureWindow;
  Options() : backend("bgfx-vulkan"), transparency("object"),
    width(960), height(540), warmup(60), frames(600), dynamic(false), materialDynamic(false), captureWindow(false) {}
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
  std::cerr << "Usage: coin_render_window_benchmark"
               " --backend coin-gl|bgfx-opengl|bgfx-vulkan|wgpu-vulkan"
               " --transparency object|weighted_oit|sorted_layers"
               " [--scene normalized.iv]"
               " [--width 960] [--height 540] [--warmup 60] [--frames 600]"
               " [--dynamic|--material-dynamic] [--capture-window]\n";
}

bool parseOptions(int argc, char ** argv, Options & options)
{
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc)
      options.backend = argv[++i];
    else if (std::strcmp(argv[i], "--transparency") == 0 && i + 1 < argc)
      options.transparency = argv[++i];
    else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc)
      options.scenePath = argv[++i];
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
    else if (std::strcmp(argv[i], "--material-dynamic") == 0)
      options.materialDynamic = true;
    else if (std::strcmp(argv[i], "--capture-window") == 0)
      options.captureWindow = true;
    else return false;
  }
  const bool backend = options.backend == "coin-gl" ||
    options.backend == "bgfx-opengl" || options.backend == "bgfx-vulkan" ||
    options.backend == "wgpu-vulkan";
  const bool transparency = options.transparency == "object" ||
    options.transparency == "weighted_oit" ||
    options.transparency == "sorted_layers";
  if (!backend || !transparency ||
      (options.captureWindow && options.backend == "coin-gl") ||
      (options.dynamic && options.materialDynamic) ||
      options.width < 1 || options.width > 8192 ||
      options.height < 1 || options.height > 8192 || options.warmup < 0 ||
      options.warmup > 100000 || options.frames < 1 || options.frames > 1000000)
    return false;
  // The wgpu bridge explicitly rejects weighted OIT on translucent geometry.
  return (options.backend != "coin-gl" && options.backend != "wgpu-vulkan") ||
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
  SoTransform * animation = new SoTransform;
  root->addChild(animation);
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
            double totalMs, const char * adapter, uint32_t vendor, uint32_t device)
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
    << " scene_update=" << (options.dynamic ? "transform-each-frame" :
      options.materialDynamic ? "material-each-frame" : "static")
    << " scene=" << (options.scenePath.empty() ? "builtin-overlap" : options.scenePath)
    << " readback=" << (options.captureWindow ? "rgba-on-request" : "none")
    << " adapter=\"" << adapter << "\""
    << " vendor_id=0x" << std::hex << vendor << " device_id=0x" << device << std::dec
    << " present_policy=off-requested"
    << " cpu_frame_median_ms=" << sorted[median]
    << " cpu_frame_p95_ms=" << sorted[p95]
    << " cpu_frame_min_ms=" << sorted.front()
    << " cpu_frame_max_ms=" << sorted.back()
    << " total_ms=" << totalMs
    << " throughput_fps=" << double(options.frames) * 1000.0 / totalMs << '\n';
}

int runCoinGl(const Options & options, SoSeparator * root,
              SoTransform * animation, SoMaterial * animatedMaterial)
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
    if (options.materialDynamic)
      animatedMaterial->diffuseColor.setValue(
        0.55f + float((frame + options.warmup) % 7) * 0.035f,
        0.12f + float((frame + options.warmup) % 5) * 0.025f, 0.18f);
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
  report(options, frameMs, totalMs, renderer ? renderer : "unknown OpenGL renderer", 0, 0);
  glXMakeCurrent(glx.display, None, NULL);
  glXDestroyContext(glx.display, glx.context);
  XDestroyWindow(glx.display, glx.window);
  XFreeColormap(glx.display, glx.colormap);
  XCloseDisplay(glx.display);
  return 0;
}

int runNative(const Options & options, SoSeparator * root,
            SoTransform * animation, SoMaterial * animatedMaterial)
{
  const bool wgpu = options.backend == "wgpu-vulkan";
  if (wgpu) setenv("COIN_RENDER_BENCH_NO_VSYNC", "1", 1);
  if (!wgpu)
    setenv("COIN_BGFX_RENDERER",
      options.backend == "bgfx-opengl" ? "opengl" : "vulkan", 1);
  setenv("COIN_RENDER_TRANSPARENCY", options.transparency.c_str(), 1);
  CoinRenderCapabilities caps;
  std::memset(&caps, 0, sizeof(caps));
  if (coin_render_query_capabilities(
        COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW, &caps, sizeof(caps)) != 0 ||
      caps.backend != (wgpu ? COIN_RENDER_EXPERIMENTAL_RUST :
                       COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION) ||
      !caps.gpu_available) {
    std::cerr << "Requested Xlib window backend unavailable: " << caps.diagnostic << '\n';
    return 2;
  }
  Display * display = XOpenDisplay(NULL);
  if (!display) { std::cerr << "Cannot open X11 display\n"; return 2; }
  Window window = createXlibWindow(display, options.width, options.height,
    options.backend == "bgfx-opengl" ? "Coin window benchmark: BGFX/OpenGL" :
    wgpu ? "Coin window benchmark: wgpu/Vulkan" :
           "Coin window benchmark: BGFX/Vulkan");
  CoinRenderNativeSurfaceDescriptor surface;
  std::memset(&surface, 0, sizeof(surface));
  surface.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  surface.structSize = sizeof(surface);
  surface.type = COIN_RENDER_SURFACE_XLIB;
  surface.native.xlib.display = display;
  surface.native.xlib.window = window;
  int exitCode = 0;
  {
    CoinRenderSceneManager manager(surface, SbVec2i32(options.width, options.height));
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
      std::vector<double> frameMs;
      frameMs.reserve(static_cast<size_t>(options.frames));
      Clock::time_point measuredBegin;
      std::vector<uint8_t> captured;
      for (int frame = -options.warmup; frame < options.frames; ++frame) {
        if (options.dynamic)
          animation->rotation.setValue(SbVec3f(0.0f, 0.0f, 1.0f),
            float(frame + options.warmup + 1) * 0.0005f);
        if (options.materialDynamic)
          animatedMaterial->diffuseColor.setValue(
            0.55f + float((frame + options.warmup) % 7) * 0.035f,
            0.12f + float((frame + options.warmup) % 5) * 0.025f, 0.18f);
        consumeEvents(display);
        const Clock::time_point begin = Clock::now();
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
        if (frame == 0) measuredBegin = begin;
        if (frame >= 0)
          frameMs.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
      }
      if (!exitCode) {
        const double totalMs = std::chrono::duration<double, std::milli>(
          Clock::now() - measuredBegin).count();
        report(options, frameMs, totalMs,
          caps.adapter_name[0] ? caps.adapter_name : "native adapter", caps.vendor_id, caps.device_id);
        if (options.captureWindow) {
          uint64_t hash = UINT64_C(14695981039346656037);
          for (uint8_t byte : captured) {
            hash ^= byte;
            hash *= UINT64_C(1099511628211);
          }
          std::cout << "window_rgba_fnv64=0x" << std::hex << hash << std::dec << '\n';
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
  Options options;
  if (!parseOptions(argc, argv, options)) { usage(); return 2; }
  SoDB::init();
  CoinRenderAction::initClass();
  SoTransform * animation = NULL;
  SoSeparator * root = createScene(options, &animation);
  if (!root) { std::cerr << "Cannot read benchmark scene\n"; return 2; }
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
  const int result = options.backend == "coin-gl" ?
    runCoinGl(options, root, animation, animatedMaterial) :
    runNative(options, root, animation, animatedMaterial);
  root->unref();
  return result;
}
