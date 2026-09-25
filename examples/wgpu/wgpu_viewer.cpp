// Minimal X11 WebGPU viewer for the experimental Coin 4 module.
// The native window stays owned by this executable, never by Coin.

#include <Inventor/SoDB.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include <Inventor/rendering/SoWgpuSceneManager.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoTransparencyType.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#if defined(Status)
#undef Status
#endif

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <unistd.h>

namespace {
void addTransparencyQuad(SoSeparator * scene, const SbColor & color,
                         float alpha, float leftZ, float rightZ)
{
  SoSeparator * object = new SoSeparator;
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(color);
  material->transparency.setValue(1.0f - alpha);
  object->addChild(material);
  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.set1Value(0, SbVec3f(-0.8f, -0.8f, leftZ));
  coordinates->point.set1Value(1, SbVec3f( 0.8f, -0.8f, rightZ));
  coordinates->point.set1Value(2, SbVec3f( 0.8f,  0.8f, rightZ));
  coordinates->point.set1Value(3, SbVec3f(-0.8f,  0.8f, leftZ));
  object->addChild(coordinates);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, 3, -1};
  face->coordIndex.setValues(0, 5, indices);
  object->addChild(face);
  scene->addChild(object);
}
}

int main(int argc, char ** argv) {
  int maxFrames = -1;
  bool transparencyDemo = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      maxFrames = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--transparency-demo") == 0) {
      transparencyDemo = true;
    } else {
      std::cerr << "Usage: wgpu_viewer [--frames N] [--transparency-demo]\n";
      return 2;
    }
  }

  SoDB::init();
  SoWgpuRenderAction::initClass();
  CoinWgpuExperimentalCapabilities caps{};
  if (coin_wgpu_experimental_query_capabilities(
        COIN_WGPU_EXPERIMENTAL_XLIB_WINDOW, &caps, sizeof(caps)) != 0 ||
      (!caps.gpu_available && caps.backend != COIN_WGPU_EXPERIMENTAL_BGFX_EVALUATION)) {
    std::cerr << "Xlib WebGPU presentation or adapter unavailable\n";
    return 2;
  }

  Display * display = XOpenDisplay(nullptr);
  if (!display) {
    std::cerr << "Cannot open X11 display\n";
    return 2;
  }
  const int screen = DefaultScreen(display);
  Window window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                      80, 80, 960, 540, 0,
                                      BlackPixel(display, screen),
                                      WhitePixel(display, screen));
  const char * transparencyMode = std::getenv("COIN_BGFX_TRANSPARENCY");
  XStoreName(display, window, transparencyDemo ?
    (transparencyMode && std::strcmp(transparencyMode, "sorted_layers") == 0 ?
      "Coin BGFX transparency: sorted_layers" : "Coin BGFX transparency: object") :
    "Coin WebGPU experimental viewer");
  XSelectInput(display, window,
               ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask);
  Atom closeWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, window, &closeWindow, 1);
  XMapWindow(display, window);
  XFlush(display);

  SoWgpuNativeSurfaceDescriptor surface{};
  surface.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
  surface.structSize = sizeof(surface);
  surface.type = COIN_WGPU_SURFACE_XLIB;
  surface.native.xlib.display = display;
  surface.native.xlib.window = window;

  int exitCode = 0;
  {
    std::unique_ptr<SoWgpuSceneManager> manager(
      new SoWgpuSceneManager(surface, SbVec2i32(960, 540)));
    if (manager->getRenderTarget()->getStatus() == SoWgpuRenderTarget::TARGET_ERROR) {
      std::cerr << "Cannot create WebGPU target: "
                << manager->getLastError().getString() << '\n';
      exitCode = 1;
    } else {
      SoSeparator * root = new SoSeparator;
      root->ref();
      SoPerspectiveCamera * camera = new SoPerspectiveCamera;
      camera->nearDistance = 0.1f;
      camera->farDistance = 20.0f;
      root->addChild(camera);
      if (!transparencyDemo) {
        SoDirectionalLight * light = new SoDirectionalLight;
        light->direction.setValue(-0.2f, -0.4f, -1.0f);
        root->addChild(light);
      }
      if (transparencyDemo ||
          caps.backend == COIN_WGPU_EXPERIMENTAL_BGFX_EVALUATION) {
        SoLightModel * baseColor = new SoLightModel;
        baseColor->model = SoLightModel::BASE_COLOR;
        root->addChild(baseColor);
      }
      if (transparencyDemo) {
        SoTransparencyType * transparency = new SoTransparencyType;
        transparency->value = SoTransparencyType::SORTED_OBJECT_BLEND;
        root->addChild(transparency);
      } else {
        SoMaterial * material = new SoMaterial;
        material->diffuseColor.setValue(0.85f, 0.3f, 0.15f);
        material->specularColor.setValue(0.6f, 0.6f, 0.6f);
        material->shininess = 0.5f;
        root->addChild(material);
      }
      SoTransform * rotation = new SoTransform;
      root->addChild(rotation);
      if (transparencyDemo) {
        addTransparencyQuad(root, SbColor(1.0f, 0.0f, 0.0f), 0.5f, 0.4f, -0.4f);
        addTransparencyQuad(root, SbColor(0.0f, 0.0f, 1.0f), 1.0f, -1.0f, -1.0f);
        addTransparencyQuad(root, SbColor(0.0f, 1.0f, 0.0f), 0.5f, -0.4f, 0.4f);
      } else {
        root->addChild(new SoCone);
      }
      manager->setSceneGraph(root);
      root->unref();
      manager->setBackgroundColor(transparencyDemo ?
        SbColor4f(0.0f, 0.0f, 0.0f, 1.0f) :
        SbColor4f(0.12f, 0.14f, 0.18f, 1.0f));

      std::cout << "Left/Right: rotate, +/- or wheel: zoom, Space: animate, "
                   "R: reset, Esc/Q: quit\n";
      bool running = true;
      bool animate = !transparencyDemo;
      float angle = 0.0f;
      float distance = transparencyDemo ? 3.0f : 4.0f;
      int frames = 0;
      while (running) {
        while (XPending(display) > 0) {
          XEvent event;
          XNextEvent(display, &event);
          if (event.type == ClientMessage &&
              static_cast<Atom>(event.xclient.data.l[0]) == closeWindow) {
            running = false;
          } else if (event.type == ConfigureNotify) {
            manager->resize(SbVec2i32(event.xconfigure.width,
                                       event.xconfigure.height));
          } else if (event.type == ButtonPress) {
            if (event.xbutton.button == Button4) distance -= 0.35f;
            if (event.xbutton.button == Button5) distance += 0.35f;
          } else if (event.type == KeyPress) {
            const KeySym key = XLookupKeysym(&event.xkey, 0);
            if (key == XK_Escape || key == XK_q || key == XK_Q) running = false;
            if (key == XK_space) animate = !animate;
            if (key == XK_Left) angle -= 0.15f;
            if (key == XK_Right) angle += 0.15f;
            if (key == XK_plus || key == XK_equal || key == XK_KP_Add) distance -= 0.35f;
            if (key == XK_minus || key == XK_KP_Subtract) distance += 0.35f;
            if (key == XK_r || key == XK_R) {
              angle = 0.0f;
              distance = transparencyDemo ? 3.0f : 4.0f;
            }
          }
        }
        if (!running) break;
        distance = std::max(2.3f, std::min(12.0f, distance));
        camera->position.setValue(0.0f, 0.0f, distance);
        if (animate) angle += 0.01f;
        rotation->rotation.setValue(SbVec3f(0.15f, 1.0f, 0.0f), angle);
        const SoWgpuRenderAction::Status status = manager->render();
        if (status != SoWgpuRenderAction::SUCCESS &&
            status != SoWgpuRenderAction::NOT_READY) {
          std::cerr << "WebGPU render failed: "
                    << manager->getLastError().getString() << '\n';
          exitCode = 1;
          break;
        }
        if (maxFrames > 0 && ++frames >= maxFrames) break;
        usleep(16000);
      }
    }
  } // Release target before the Xlib window and display.

  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return exitCode;
}
