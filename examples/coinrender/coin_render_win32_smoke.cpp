/* Win32 surface smoke for the experimental BGFX and wgpu connectors.
 * Run on a real Windows GPU; CoinRender never owns either HWND. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
unsigned int dpiChanges = 0;
bool windowReadback = true;
std::vector<RECT> monitorBounds;
BOOL CALLBACK collectMonitor(HMONITOR, HDC, LPRECT bounds, LPARAM) {
  monitorBounds.push_back(*bounds);
  return TRUE;
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_DPICHANGED) {
    ++dpiChanges;
    const RECT * suggested = reinterpret_cast<const RECT *>(lparam);
    SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                 suggested->right - suggested->left,
                 suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

void pumpMessages() {
  MSG message{};
  while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

SbVec2i32 framebufferSize(HWND hwnd) {
  RECT client{};
  if (!GetClientRect(hwnd, &client)) return SbVec2i32(-1, -1);
  return SbVec2i32(client.right - client.left, client.bottom - client.top);
}

uint64_t fnv64(const std::vector<uint8_t> & bytes) {
  uint64_t value = UINT64_C(14695981039346656037);
  for (uint8_t byte : bytes) {
    value ^= byte;
    value *= UINT64_C(1099511628211);
  }
  return value;
}

struct WindowRun {
  HWND hwnd = NULL;
  std::unique_ptr<CoinRenderTarget> target;
  std::unique_ptr<CoinRenderAction> action;
};

bool createTarget(WindowRun & run, HINSTANCE instance, CoinRenderRenderer renderer) {
  const SbVec2i32 size = framebufferSize(run.hwnd);
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize = sizeof(native);
  native.type = COIN_RENDER_SURFACE_WIN32;
  native.native.win32.hinstance = instance;
  native.native.win32.hwnd = run.hwnd;
  CoinRenderOptions options;
  options.renderer = renderer;
  run.target.reset(CoinRenderTarget::createWindow(native, size, options));
  if (!run.target || run.target->getStatus() != CoinRenderTarget::TARGET_READY) {
    std::cerr << "Win32 target creation failed: " << (run.target ? run.target->getLastError() : "null target") << '\n';
    return false;
  }
  run.action.reset(new CoinRenderAction);
  run.action->setRenderTarget(run.target.get());
  run.action->setViewportRegion(SbViewportRegion(size[0], size[1]));
  return true;
}

bool render(WindowRun & run, SoSeparator * scene, bool capture) {
  capture = capture && windowReadback;
  pumpMessages();
  const SbVec2i32 size = framebufferSize(run.hwnd);
  if (size[0] <= 0 || size[1] <= 0) return false;
  if (size != run.target->getSize() && !run.target->resize(size)) return false;
  run.action->setViewportRegion(SbViewportRegion(size[0], size[1]));
  if (capture && !run.target->requestWindowReadbackRGBA()) return false;
  const uint64_t priorSerial = run.target->getLastSubmissionSerial();
  run.action->apply(scene);
  if (run.action->getLastStatus() != CoinRenderAction::SUCCESS) {
    std::cerr << "Win32 render failed: " << run.action->getLastError().getString() << '\n';
    return false;
  }
  if (run.target->getLastSubmissionSerial() <= priorSerial) {
    std::cerr << "Window submission serial did not advance\n";
    return false;
  }
  std::vector<uint8_t> rgba;
  run.target->readbackRGBA(rgba);
  if (!capture) return rgba.empty();
  if (rgba.size() != size_t(size[0]) * size_t(size[1]) * 4u) return false;
  const size_t center = (size_t(size[1] / 2) * size_t(size[0]) + size_t(size[0] / 2)) * 4u;
  if (rgba[center] < rgba[center + 1] + 50 || rgba[center] < rgba[center + 2] + 50) return false;
  std::cout << "window_rgba_fnv64=0x" << std::hex << fnv64(rgba) << std::dec
            << " size=" << size[0] << 'x' << size[1]
            << " dpi=" << GetDpiForWindow(run.hwnd) << '\n';
  return true;
}

bool compareOffscreen(WindowRun& run, SoSeparator* scene, const char* fixture) {
  run.action->setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  if (!windowReadback) {
    const bool ok = render(run, scene, false);
    std::cout << "window_present fixture=" << fixture
              << " pixel_comparison_enabled=0 serial="
              << run.target->getLastSubmissionSerial() << '\n';
    return ok;
  }
  if (!render(run, scene, true)) return false;
  const SbVec2i32 size = run.target->getSize();
  std::unique_ptr<CoinRenderTarget> offscreen(
    CoinRenderTarget::createOffscreen(size, run.target->getOptions()));
  if (!offscreen || offscreen->getStatus() != CoinRenderTarget::TARGET_READY) {
    std::cerr << "Offscreen comparison target creation failed\n";
    return false;
  }
  CoinRenderAction action(SbViewportRegion(size[0], size[1]));
  action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  action.setRenderTarget(offscreen.get());
  action.apply(scene);
  std::vector<uint8_t> windowPixels, offscreenPixels;
  run.target->readbackRGBA(windowPixels);
  offscreen->readbackRGBA(offscreenPixels);
  if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
      windowPixels.empty() || windowPixels.size() != offscreenPixels.size()) return false;
  int maximumDifference = 0;
  for (size_t i = 0; i < windowPixels.size(); ++i) {
    const int difference = std::abs(int(windowPixels[i]) - int(offscreenPixels[i]));
    if (difference > maximumDifference) maximumDifference = difference;
  }
  std::cout << "window_offscreen fixture=" << fixture
            << " max_channel_delta=" << maximumDifference << " tolerance=3\n";
  return maximumDifference <= 3;
}

bool detachedTicket(SoSeparator * scene, const CoinRenderOptions & options,
                    CoinRenderReadbackTicket & ticket, std::vector<uint8_t> & expected) {
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(32, 32), options));
  if (!target || target->getStatus() != CoinRenderTarget::TARGET_READY) return false;
  CoinRenderAction action(SbViewportRegion(32, 32));
  action.setRenderTarget(target.get());
  action.apply(scene);
  target->readbackRGBA(expected);
  if (action.getLastStatus() != CoinRenderAction::SUCCESS || expected.size() != 32 * 32 * 4)
    return false;
  action.applyAsync(scene, ticket);
  return action.getLastStatus() == CoinRenderAction::SUCCESS && ticket.token &&
    ticket.width == 32 && ticket.height == 32 && ticket.submissionSerial &&
    ticket.submissionSerial == target->getLastSubmissionSerial();
  // Both producer objects are destroyed before the HWND is replaced.
}

bool consumeDetachedTicket(const CoinRenderReadbackTicket & ticket,
                           const std::vector<uint8_t> & expected) {
  std::vector<uint8_t> color;
  std::vector<float> depth;
  for (int i = 0; i < 5000; ++i) {
    const auto status = CoinRenderTarget::pollReadback(ticket, color, depth);
    if (status == CoinRenderTarget::READBACK_READY) {
      if (color != expected) return false;
      return CoinRenderTarget::pollReadback(ticket, color, depth) ==
        CoinRenderTarget::READBACK_INVALID_TICKET;
    }
    if (status != CoinRenderTarget::READBACK_NOT_READY) return false;
    Sleep(1);
  }
  return false;
}
}

int main(int argc, char** argv) {
  CoinRenderRenderer renderer = COIN_RENDER_RENDERER_D3D12;
  bool expectNoWindowReadback = false;
  for (int arg = 1; arg < argc; ++arg) {
    if (std::strcmp(argv[arg], "--vulkan") == 0) renderer = COIN_RENDER_RENDERER_VULKAN;
    else if (std::strcmp(argv[arg], "--opengl") == 0) renderer = COIN_RENDER_RENDERER_OPENGL;
    else if (std::strcmp(argv[arg], "--expect-no-window-readback") == 0)
      expectNoWindowReadback = true;
    else {
      std::cerr << "Usage: coin_render_win32_smoke [--vulkan|--opengl] [--expect-no-window-readback]\n";
      return 2;
    }
  }
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  HINSTANCE instance = GetModuleHandleW(NULL);
  WNDCLASSW type{};
  type.lpfnWndProc = windowProc;
  type.hInstance = instance;
  type.lpszClassName = L"CoinRenderP21Smoke";
  if (!RegisterClassW(&type)) return 1;
  WindowRun windows[2];
  for (int i = 0; i < 2; ++i) {
    windows[i].hwnd = CreateWindowExW(0, type.lpszClassName, L"CoinRender Win32 P21",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100 + i * 400, 100, 360, 280,
      NULL, NULL, instance, NULL);
    if (!windows[i].hwnd) return 2;
  }
  pumpMessages();
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

  bool ok = createTarget(windows[0], instance, renderer) && createTarget(windows[1], instance, renderer);
  if (ok && expectNoWindowReadback) {
    windowReadback = false;
    ok = render(windows[0], scene, false);
    const uint64_t serial = windows[0].target->getLastSubmissionSerial();
    ok = ok && windows[0].target->requestWindowReadbackRGBA();
    if (ok) {
      windows[0].action->apply(scene);
      std::vector<uint8_t> pixels;
      windows[0].target->readbackRGBA(pixels);
      ok = windows[0].action->getLastStatus() != CoinRenderAction::SUCCESS &&
        std::strstr(windows[0].action->getLastError().getString(), "COPY_SRC") &&
        windows[0].target->getLastSubmissionSerial() == serial && pixels.empty() &&
        render(windows[0], scene, false);
    }
    std::cout << "window_capture_unsupported expected=1 serial_preserved=" << ok
              << " recovered=" << ok << " pixel_comparison_enabled=0\n";
  }
  if (ok) {
    ok = render(windows[0], scene, true) && render(windows[1], scene, true) &&
         render(windows[0], scene, false) && render(windows[1], scene, false);
  }
  if (ok) {
    SetWindowPos(windows[0].hwnd, NULL, 0, 0, 520, 390, SWP_NOMOVE | SWP_NOZORDER);
    ok = render(windows[0], scene, true) && render(windows[1], scene, true);
  }
  if (ok) {
    ShowWindow(windows[0].hwnd, SW_MINIMIZE);
    pumpMessages();
    ok = windows[0].target->resize(SbVec2i32(0, 0)) &&
         windows[0].target->getStatus() == CoinRenderTarget::TARGET_NOT_READY;
    ShowWindow(windows[0].hwnd, SW_RESTORE);
    ok = ok && render(windows[0], scene, true) && render(windows[1], scene, false);
  }
  if (ok) {
    ok = compareOffscreen(windows[0], scene, "opaque");
    material->transparency = 0.5f;
    ok = ok && compareOffscreen(windows[0], scene, "transparent");
  }
  // A target owns its surface, while the host owns its HWND. Exercise real
  // handle replacement without changing the surviving window's resources.
  material->transparency = 0.0f;
  std::vector<uint8_t> survivorPixels;
  const SbVec2i32 survivorSize = framebufferSize(windows[1].hwnd);
  if (ok) {
    ok = render(windows[1], scene, true);
    windows[1].target->readbackRGBA(survivorPixels);
  }
  for (int cycle = 0; ok && cycle < 3; ++cycle) {
    CoinRenderReadbackTicket ticket{};
    std::vector<uint8_t> expected;
    const uint64_t survivorSerial = windows[1].target->getLastSubmissionSerial();
    ok = detachedTicket(scene, windows[0].target->getOptions(), ticket, expected);
    if (!ok) {
      if (ticket.token) CoinRenderTarget::cancelReadback(ticket);
      break;
    }
    const HWND old = windows[0].hwnd;
    windows[0].action.reset();
    windows[0].target.reset();
    // Allocate before destroying old so handle reuse cannot disguise this test.
    windows[0].hwnd = CreateWindowExW(0, type.lpszClassName, L"CoinRender recreated P21",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 360, 280,
      NULL, NULL, instance, NULL);
    ok = windows[0].hwnd && windows[0].hwnd != old;
    ok = DestroyWindow(old) && ok;
    pumpMessages();
    ok = ok && !IsWindow(old) && createTarget(windows[0], instance, renderer) &&
      render(windows[0], scene, false) && compareOffscreen(windows[0], scene, "recreated");
    ok = ok && windows[1].target->getLastSubmissionSerial() == survivorSerial &&
      consumeDetachedTicket(ticket, expected);
    if (!ok) CoinRenderTarget::cancelReadback(ticket);
    ok = ok && render(windows[1], scene, true);
    std::vector<uint8_t> after;
    windows[1].target->readbackRGBA(after);
    ok = ok && framebufferSize(windows[1].hwnd) == survivorSize && after == survivorPixels;
    std::cout << "hwnd_recreation cycle=" << cycle + 1 << " survivor_unchanged=" << ok
              << " detached_ticket_exact=" << ok << " survivor_serial_isolated=" << ok
              << " pixel_comparison_enabled=" << windowReadback << '\n';
  }
  if (ok) {
    EnumDisplayMonitors(NULL, NULL, collectMonitor, 0);
    const UINT initialDpi = GetDpiForWindow(windows[0].hwnd);
    bool distinctDpi = false;
    for (const RECT & bounds : monitorBounds) {
      const unsigned int before = dpiChanges;
      SetWindowPos(windows[0].hwnd, NULL, bounds.left + 40, bounds.top + 40,
                   360, 280, SWP_NOZORDER);
      pumpMessages();
      const UINT dpi = GetDpiForWindow(windows[0].hwnd);
      if (dpi != initialDpi) {
        distinctDpi = true;
        ok = dpiChanges > before;
      }
      ok = ok && compareOffscreen(windows[0], scene, "monitor") &&
        render(windows[1], scene, true);
      std::vector<uint8_t> after;
      windows[1].target->readbackRGBA(after);
      ok = ok && framebufferSize(windows[1].hwnd) == survivorSize && after == survivorPixels;
      std::cout << "monitor_dpi=" << dpi << " dpi_changed_events=" << dpiChanges
                << " survivor_unchanged=" << ok << '\n';
      if (!ok) break;
    }
    std::cout << "physical_distinct_dpi=" << distinctDpi
              << " monitors=" << monitorBounds.size() << '\n';
  }
  CoinRenderCapabilities caps{};
  if (ok) {
    const int32_t query = coin_render_query_capabilities_for_renderer(
      COIN_RENDER_EXPERIMENTAL_WIN32_WINDOW, renderer,
      &caps, sizeof(caps));
    ok = query == 0 && caps.gpu_available && caps.renderer == renderer;
    if (ok) {
      std::cout << "renderer=" << caps.renderer << " vendor_id=0x" << std::hex
                << caps.vendor_id << " device_id=0x" << caps.device_id << std::dec
                << " adapter=\"" << caps.adapter_name << "\"\n";
    } else {
      std::cerr << "Requested adapter capability query failed: " << query << '\n';
    }
  }
  for (auto & window : windows) {
    window.action.reset();
    window.target.reset();
    if (window.hwnd) DestroyWindow(window.hwnd);
  }
  scene->unref();
  UnregisterClassW(type.lpszClassName, instance);
  if (!ok) std::cerr << "P21 Win32 smoke failed\n";
  return ok ? 0 : 3;
}
