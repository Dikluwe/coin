/* Native Wayland surface smoke. GTK owns wl_display/wl_surface and event dispatch;
 * CoinRender only borrows the handles and publishes rendered frames. */
#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>

#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {
void pump() {
  while (g_main_context_iteration(nullptr, FALSE)) {}
}

SbVec2i32 pixels(GtkWidget * widget) {
  GdkWindow * window = gtk_widget_get_window(widget);
  if (!window) return SbVec2i32(0, 0);
  const int scale = gdk_window_get_scale_factor(window);
  return SbVec2i32(gdk_window_get_width(window) * scale,
                   gdk_window_get_height(window) * scale);
}

uint64_t checksum(const std::vector<uint8_t> & bytes) {
  uint64_t value = UINT64_C(14695981039346656037);
  for (uint8_t byte : bytes) {
    value ^= byte;
    value *= UINT64_C(1099511628211);
  }
  return value;
}

struct WindowRun {
  GtkWidget * widget = nullptr;
  std::unique_ptr<CoinRenderTarget> target;
  std::unique_ptr<CoinRenderAction> action;
};

bool create(WindowRun & run, GdkDisplay * display) {
  GdkWindow * window = gtk_widget_get_window(run.widget);
  if (!window || !GDK_IS_WAYLAND_WINDOW(window)) return false;
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize = sizeof(native);
  native.type = COIN_RENDER_SURFACE_WAYLAND;
  native.native.wayland.display = gdk_wayland_display_get_wl_display(display);
  native.native.wayland.surface = gdk_wayland_window_get_wl_surface(window);
  CoinRenderOptions options;
  options.renderer = COIN_RENDER_RENDERER_VULKAN;
  const SbVec2i32 size = pixels(run.widget);
  run.target.reset(CoinRenderTarget::createWindow(native, size, options));
  if (!run.target || run.target->getStatus() != CoinRenderTarget::TARGET_READY) {
    std::cerr << "Wayland target creation failed: "
              << (run.target ? run.target->getLastError() : "null target") << '\n';
    return false;
  }
  run.action.reset(new CoinRenderAction);
  run.action->setRenderTarget(run.target.get());
  return true;
}

bool render(WindowRun & run, SoSeparator * scene, bool capture) {
  pump();
  const SbVec2i32 size = pixels(run.widget);
  if (size[0] <= 0 || size[1] <= 0) return false;
  if (size != run.target->getSize() && !run.target->resize(size)) return false;
  run.action->setViewportRegion(SbViewportRegion(size[0], size[1]));
  if (capture && !run.target->requestWindowReadbackRGBA()) return false;
  run.action->apply(scene);
  if (run.action->getLastStatus() != CoinRenderAction::SUCCESS) {
    std::cerr << "Wayland render failed: " << run.action->getLastError().getString() << '\n';
    return false;
  }
  std::vector<uint8_t> rgba;
  run.target->readbackRGBA(rgba);
  if (!capture) return rgba.empty();
  if (rgba.size() != size_t(size[0]) * size_t(size[1]) * 4u) return false;
  const size_t center = (size_t(size[1] / 2) * size_t(size[0]) + size_t(size[0] / 2)) * 4u;
  if (rgba[center] < rgba[center + 1] + 50 || rgba[center] < rgba[center + 2] + 50) return false;
  std::cout << "wayland_rgba_fnv64=0x" << std::hex << checksum(rgba) << std::dec
            << " size=" << size[0] << 'x' << size[1]
            << " scale=" << gdk_window_get_scale_factor(gtk_widget_get_window(run.widget)) << '\n';
  return true;
}
}

int main(int argc, char ** argv) {
  if (!gtk_init_check(&argc, &argv)) return 1;
  GdkDisplay * display = gdk_display_get_default();
  if (!display || !GDK_IS_WAYLAND_DISPLAY(display)) {
    std::cerr << "GDK_BACKEND=wayland and a native Wayland compositor are required\n";
    return 2;
  }
  SoDB::init();
  CoinRenderAction::initClass();
  SoSeparator * scene = new SoSeparator;
  scene->ref();
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 4);
  camera->height = 3;
  scene->addChild(camera);
  SoLightModel * model = new SoLightModel;
  model->model = SoLightModel::BASE_COLOR;
  scene->addChild(model);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(1, 0, 0);
  scene->addChild(material);
  scene->addChild(new SoCube);

  WindowRun windows[2];
  for (auto & window : windows) {
    window.widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window.widget), 360, 280);
    gtk_widget_show_all(window.widget);
  }
  pump();
  bool ok = create(windows[0], display) && create(windows[1], display);
  if (ok) {
    ok = render(windows[0], scene, true) && render(windows[1], scene, true) &&
         render(windows[0], scene, false) && render(windows[1], scene, false);
  }
  if (ok) {
    const SbVec2i32 before = pixels(windows[0].widget);
    gtk_window_resize(GTK_WINDOW(windows[0].widget), 520, 390);
    for (int i = 0; i < 200 && pixels(windows[0].widget) == before; ++i) {
      pump();
      g_usleep(10000);
    }
    ok = pixels(windows[0].widget) != before &&
         render(windows[0], scene, true) && render(windows[1], scene, true);
  }
  if (ok) {
    ok = windows[0].target->resize(SbVec2i32(0, 0)) &&
         windows[0].target->getStatus() == CoinRenderTarget::TARGET_NOT_READY;
    if (ok) {
      windows[0].action->apply(scene);
      ok = windows[0].action->getLastStatus() == CoinRenderAction::NOT_READY &&
           render(windows[0], scene, true);
    }
  }
  CoinRenderCapabilities caps{};
  if (ok) {
    const int32_t query = coin_render_query_capabilities_for_renderer(
      COIN_RENDER_EXPERIMENTAL_WAYLAND_WINDOW, COIN_RENDER_RENDERER_VULKAN,
      &caps, sizeof(caps));
    ok = query == 0 && caps.gpu_available && caps.renderer == COIN_RENDER_RENDERER_VULKAN;
    if (ok) std::cout << "adapter=\"" << caps.adapter_name << "\" renderer=" << caps.renderer << '\n';
  }
  for (auto & window : windows) {
    window.action.reset();
    window.target.reset();
    if (window.widget) gtk_widget_destroy(window.widget);
  }
  scene->unref();
  if (!ok) std::cerr << "P22 Wayland smoke failed\n";
  return ok ? 0 : 3;
}
