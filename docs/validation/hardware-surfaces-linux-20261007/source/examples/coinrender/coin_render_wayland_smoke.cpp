/* Native Wayland surface smoke. GTK owns wl_display/wl_surface and event dispatch;
 * CoinRender only borrows the handles and publishes rendered frames. */
#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>

#include <Inventor/SoDB.h>
#if COIN_WAYLAND_GL_REFERENCE
#include <Inventor/SoOffscreenRenderer.h>
#endif
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
#include <cstdlib>
#include <algorithm>
#include <cmath>
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
  std::vector<uint8_t> captured;
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
  if (const char * expected = std::getenv("COIN_RENDER_EXPECT_WAYLAND_SCALE")) {
    const int actual = gdk_window_get_scale_factor(gtk_widget_get_window(run.widget));
    if (std::atoi(expected) != actual) {
      std::cerr << "Wayland scale expected=" << expected << " actual=" << actual << '\n';
      return false;
    }
  }
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
  // Compare every RGB pixel with the common offscreen fixture at the actual
  // framebuffer size; this also checks surface orientation and color conversion.
  std::unique_ptr<CoinRenderTarget> reference(CoinRenderTarget::createOffscreen(size));
  if (!reference) return false;
  CoinRenderAction referenceAction(SbViewportRegion(size[0], size[1]));
  referenceAction.setRenderTarget(reference.get()); referenceAction.apply(scene);
  std::vector<uint8_t> expected; reference->readbackRGBA(expected);
  if (referenceAction.getLastStatus() != CoinRenderAction::SUCCESS || expected.size() != rgba.size()) return false;
  int maximum = 0;
  for (size_t i = 0; i < rgba.size(); ++i) if (i % 4 != 3)
    maximum = std::max(maximum, std::abs(int(rgba[i])-int(expected[i])));
  std::cout << "wayland_offscreen_rgb_max=" << maximum << '\n';
  if (maximum > 2) return false;
  if (std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
#if COIN_WAYLAND_GL_REFERENCE
    // Wayland owns presentation; DISPLAY identifies the independent GL oracle.
    SoOffscreenRenderer gl(SbViewportRegion(size[0],size[1]));
    gl.setComponents(SoOffscreenRenderer::RGB);
    if (!gl.render(scene) || !gl.getBuffer()) return false;
    maximum = 0;
    for (int y=0; y<size[1]; ++y) for (int x=0; x<size[0]; ++x) for (int c=0; c<3; ++c) {
      const size_t a = (size_t(y)*size[0]+x)*4+c;
      const size_t b = (size_t(size[1]-y-1)*size[0]+x)*3+c;
      maximum = std::max(maximum,std::abs(int(rgba[a])-int(gl.getBuffer()[b])));
    }
    std::cout << "wayland_CoinGL_rgb_max=" << maximum << '\n';
    if (maximum > 2) return false;
#else
    std::cerr << "Required Wayland CoinGL oracle needs COIN_BUILD_LEGACY_GL_RENDERER=ON\n";
    return false;
#endif
  }
  run.captured = rgba;
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
    const std::vector<uint8_t> secondPixels = windows[1].captured;
    const uint64_t secondSerial = windows[1].target->getLastSubmissionSerial();
    const SbVec2i32 before = pixels(windows[0].widget);
    gtk_window_resize(GTK_WINDOW(windows[0].widget), 520, 390);
    for (int i = 0; i < 200 && pixels(windows[0].widget) == before; ++i) {
      pump();
      g_usleep(10000);
    }
    ok = pixels(windows[0].widget) != before &&
         render(windows[0], scene, true) &&
         windows[1].target->getLastSubmissionSerial() == secondSerial &&
         render(windows[1], scene, true) && windows[1].captured == secondPixels;
  }
  // Destroy Coin objects before GTK invalidates the borrowed wl_surface. Repeat
  // real host-surface replacement while the second window remains usable.
  for (int cycle = 0; cycle < 3 && ok; ++cycle) {
    const std::vector<uint8_t> secondPixels = windows[1].captured;
    const uint64_t secondSerial = windows[1].target->getLastSubmissionSerial();
    windows[0].action.reset(); windows[0].target.reset();
    gtk_widget_destroy(windows[0].widget); pump();
    windows[0].widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(windows[0].widget), 360 + cycle*20, 280 + cycle*10);
    gtk_widget_show_all(windows[0].widget); pump();
    ok = create(windows[0], display) && render(windows[0], scene, true) &&
         render(windows[0], scene, false) &&
         windows[1].target->getLastSubmissionSerial() == secondSerial &&
         render(windows[1], scene, true) && windows[1].captured == secondPixels;
    std::cout << "wayland_surface_recreate=" << cycle + 1 << " success=" << ok << '\n';
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
    if (ok) std::cout << "adapter=\"" << caps.adapter_name << "\" renderer=" << caps.renderer
      << " vendor_id=0x" << std::hex << caps.vendor_id << " device_id=0x" << caps.device_id << std::dec << '\n';
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
