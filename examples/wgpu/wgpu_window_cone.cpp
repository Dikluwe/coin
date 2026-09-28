/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 *
 * Example: Real native window presentation using WebGPU (Onda 1B)
 * Presents a canonical SoCone using SoWgpuRenderAction on an X11 / GLFW window
 * without any OpenGL context or glfwSwapBuffers.
\**************************************************************************/

#include <Inventor/SoDB.h>
#if defined(COIN_EXAMPLE_BGFX)
#include <Inventor/actions/SoBGFXRenderAction.h>
using ExampleRenderAction = SoBGFXRenderAction;
#else
#include <Inventor/actions/SoWgpuRenderAction.h>
using ExampleRenderAction = SoWgpuRenderAction;
#endif
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoCone.h>

#include <cstring>
#include <cstdlib>
#include <iostream>

#if defined(HAVE_GLFW)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#endif

static bool g_framebufferResized = false;
static int g_pendingWidth = 960;
static int g_pendingHeight = 540;

static void framebufferSizeCallback(GLFWwindow *, int width, int height) {
  g_pendingWidth = width;
  g_pendingHeight = height;
  g_framebufferResized = true;
}

int main(int argc, char ** argv) {
  int maxFrames = -1;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      maxFrames = std::atoi(argv[++i]);
    }
  }

  SoDB::init();
  ExampleRenderAction::initClass();
  std::cout << "Starting Coin3D WebGPU Window Example (GLFW + X11)..." << std::endl;

  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return 1;
  }

  // Critical requirement: NEVER create an OpenGL context
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  GLFWwindow * window = glfwCreateWindow(960, 540, "Coin3D WebGPU Window (SoCone)", NULL, NULL);
  if (!window) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return 1;
  }

  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

  int fbWidth = 0, fbHeight = 0;
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

  // Setup tagged native surface descriptor
  SoWgpuNativeSurfaceDescriptor nativeDesc{};
  nativeDesc.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
  nativeDesc.structSize = sizeof(nativeDesc);
  nativeDesc.type = COIN_WGPU_SURFACE_XLIB;
  nativeDesc.reserved = 0;
#if defined(__linux__)
  nativeDesc.native.xlib.display = glfwGetX11Display();
  nativeDesc.native.xlib.window = glfwGetX11Window(window);
#endif

  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createWindow(nativeDesc, SbVec2i32(fbWidth, fbHeight));
  if (target->getStatus() == SoWgpuRenderTarget::TARGET_ERROR) {
    std::cerr << "Failed to create WebGPU window target: " << target->getLastError() << std::endl;
    delete target;
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  ExampleRenderAction action;
  action.setRenderTarget(target);
  action.setViewportRegion(SbViewportRegion(fbWidth, fbHeight));
  action.setBackgroundColor(SbColor4f(0.12f, 0.14f, 0.18f, 1.0f));

  // Build canonical cone scene
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0.0f, 0.0f, 4.0f);
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  root->addChild(camera);

  SoDirectionalLight * light = new SoDirectionalLight;
  light->direction.setValue(0.0f, 0.0f, -1.0f);
  light->color.setValue(1.0f, 1.0f, 1.0f);
  light->intensity = 1.0f;
  root->addChild(light);
#if defined(COIN_EXAMPLE_BGFX)
  SoLightModel * baseColor = new SoLightModel;
  baseColor->model = SoLightModel::BASE_COLOR;
  root->addChild(baseColor);
#endif

  SoMaterial * mat = new SoMaterial;
  mat->diffuseColor.setValue(0.9f, 0.3f, 0.2f);
  mat->ambientColor.setValue(0.2f, 0.05f, 0.05f);
  mat->specularColor.setValue(0.9f, 0.9f, 0.9f);
  mat->shininess = 0.6f;
  root->addChild(mat);

  SoTransform * xform = new SoTransform;
  root->addChild(xform);

  SoCone * cone = new SoCone;
  cone->bottomRadius = 1.2f;
  cone->height = 2.0f;
  root->addChild(cone);

  std::cout << "Rendering scene. Press ESC or close window to exit." << std::endl;

  int frameCount = 0;
  float angle = 0.0f;
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
      break;
    }

    if (g_framebufferResized) {
      g_framebufferResized = false;
      target->resize(SbVec2i32(g_pendingWidth, g_pendingHeight));
      action.setViewportRegion(SbViewportRegion(g_pendingWidth, g_pendingHeight));
    }

    angle += 0.02f;
    xform->rotation.setValue(SbVec3f(0.2f, 1.0f, 0.1f), angle);

    action.apply(root);

    auto st = action.getLastStatus();
    if (st != ExampleRenderAction::SUCCESS && st != ExampleRenderAction::NOT_READY) {
      std::cerr << "RenderAction error: " << action.getLastError().getString() << std::endl;
      break;
    }

    frameCount++;
    if (maxFrames > 0 && frameCount >= maxFrames) {
      break;
    }
  }

  root->unref();

  // Destroy target BEFORE destroying window
  delete target;
  glfwDestroyWindow(window);
  glfwTerminate();

  std::cout << "Clean shutdown." << std::endl;
  return 0;
}

#elif defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <unistd.h>
#if defined(Status)
#undef Status
#endif

int main(int argc, char ** argv) {
  int maxFrames = -1;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      maxFrames = std::atoi(argv[++i]);
    }
  }

  SoDB::init();
  ExampleRenderAction::initClass();
  std::cout << "Starting Coin3D WebGPU Window Example (Direct Xlib)..." << std::endl;

  Display * dpy = XOpenDisplay(NULL);
  if (!dpy) {
    std::cerr << "Could not open X11 Display." << std::endl;
    return 1;
  }

  int screen = DefaultScreen(dpy);
  Window rootWin = RootWindow(dpy, screen);
  Window win = XCreateSimpleWindow(dpy, rootWin, 100, 100, 960, 540, 1,
                                   BlackPixel(dpy, screen), WhitePixel(dpy, screen));
  XStoreName(dpy, win, "Coin3D WebGPU Window (SoCone - Direct Xlib)");
  XSelectInput(dpy, win, ExposureMask | StructureNotifyMask | KeyPressMask);

  Atom wmDeleteMessage = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(dpy, win, &wmDeleteMessage, 1);

  XMapWindow(dpy, win);
  XFlush(dpy);

  SoWgpuNativeSurfaceDescriptor nativeDesc{};
  nativeDesc.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
  nativeDesc.structSize = sizeof(nativeDesc);
  nativeDesc.type = COIN_WGPU_SURFACE_XLIB;
  nativeDesc.reserved = 0;
  nativeDesc.native.xlib.display = dpy;
  nativeDesc.native.xlib.window = win;

  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createWindow(nativeDesc, SbVec2i32(960, 540));
  if (target->getStatus() == SoWgpuRenderTarget::TARGET_ERROR) {
    std::cerr << "Failed to create WebGPU window target: " << target->getLastError() << std::endl;
    delete target;
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 1;
  }

  ExampleRenderAction action;
  action.setRenderTarget(target);
  action.setViewportRegion(SbViewportRegion(960, 540));
  action.setBackgroundColor(SbColor4f(0.12f, 0.14f, 0.18f, 1.0f));

  // Build canonical cone scene
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0.0f, 0.0f, 4.0f);
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  root->addChild(camera);

  SoDirectionalLight * light = new SoDirectionalLight;
  light->direction.setValue(0.0f, 0.0f, -1.0f);
  light->color.setValue(1.0f, 1.0f, 1.0f);
  light->intensity = 1.0f;
  root->addChild(light);
#if defined(COIN_EXAMPLE_BGFX)
  SoLightModel * baseColor = new SoLightModel;
  baseColor->model = SoLightModel::BASE_COLOR;
  root->addChild(baseColor);
#endif

  SoMaterial * mat = new SoMaterial;
  mat->diffuseColor.setValue(0.9f, 0.3f, 0.2f);
  mat->ambientColor.setValue(0.2f, 0.05f, 0.05f);
  mat->specularColor.setValue(0.9f, 0.9f, 0.9f);
  mat->shininess = 0.6f;
  root->addChild(mat);

  SoTransform * xform = new SoTransform;
  root->addChild(xform);

  SoCone * cone = new SoCone;
  cone->bottomRadius = 1.2f;
  cone->height = 2.0f;
  root->addChild(cone);

  std::cout << "Rendering scene on X11 window. Press ESC, Q or close the window to exit." << std::endl;

  bool running = true;
  int frameCount = 0;
  float angle = 0.0f;
  while (running) {
    while (XPending(dpy) > 0) {
      XEvent ev;
      XNextEvent(dpy, &ev);
      if (ev.type == ClientMessage) {
        if (static_cast<Atom>(ev.xclient.data.l[0]) == wmDeleteMessage) {
          running = false;
        }
      } else if (ev.type == KeyPress) {
        KeySym keysym = XLookupKeysym(&ev.xkey, 0);
        if (keysym == XK_Escape || keysym == XK_q || keysym == XK_Q) {
          running = false;
        }
      } else if (ev.type == ConfigureNotify) {
        int w = ev.xconfigure.width;
        int h = ev.xconfigure.height;
        if (w > 0 && h > 0) {
          target->resize(SbVec2i32(w, h));
          action.setViewportRegion(SbViewportRegion(w, h));
        }
      }
    }
    if (!running) break;

    angle += 0.02f;
    xform->rotation.setValue(SbVec3f(0.2f, 1.0f, 0.1f), angle);

    action.apply(root);

    auto st = action.getLastStatus();
    if (st != ExampleRenderAction::SUCCESS && st != ExampleRenderAction::NOT_READY) {
      std::cerr << "RenderAction error: " << action.getLastError().getString() << std::endl;
      break;
    }

    frameCount++;
    if (maxFrames > 0 && frameCount >= maxFrames) {
      break;
    }
    usleep(16000); // ~60 FPS
  }

  root->unref();
  delete target;
  XDestroyWindow(dpy, win);
  XCloseDisplay(dpy);

  std::cout << "Finished successfully." << std::endl;
  return 0;
}
#else
int main() {
  std::cout << "Window example not supported on this platform without GLFW or X11." << std::endl;
  return 0;
}
#endif
