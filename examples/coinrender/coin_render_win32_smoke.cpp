/* Win32 surface smoke for the experimental wgpu/D3D12 connector.
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
#include <iostream>
#include <memory>
#include <vector>

namespace {
LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_DPICHANGED) {
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

bool createTarget(WindowRun & run, HINSTANCE instance) {
  const SbVec2i32 size = framebufferSize(run.hwnd);
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize = sizeof(native);
  native.type = COIN_RENDER_SURFACE_WIN32;
  native.native.win32.hinstance = instance;
  native.native.win32.hwnd = run.hwnd;
  CoinRenderOptions options;
  options.renderer = COIN_RENDER_RENDERER_D3D12;
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
  pumpMessages();
  const SbVec2i32 size = framebufferSize(run.hwnd);
  if (size[0] <= 0 || size[1] <= 0) return false;
  if (size != run.target->getSize() && !run.target->resize(size)) return false;
  run.action->setViewportRegion(SbViewportRegion(size[0], size[1]));
  if (capture && !run.target->requestWindowReadbackRGBA()) return false;
  run.action->apply(scene);
  if (run.action->getLastStatus() != CoinRenderAction::SUCCESS) {
    std::cerr << "Win32 render failed: " << run.action->getLastError().getString() << '\n';
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
}

int main() {
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

  bool ok = createTarget(windows[0], instance) && createTarget(windows[1], instance);
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
  CoinRenderCapabilities caps{};
  if (ok) {
    const int32_t query = coin_render_query_capabilities_for_renderer(
      COIN_RENDER_EXPERIMENTAL_WIN32_WINDOW, COIN_RENDER_RENDERER_D3D12,
      &caps, sizeof(caps));
    ok = query == 0 && caps.gpu_available && caps.renderer == COIN_RENDER_RENDERER_D3D12;
    if (ok) {
      std::cout << "renderer=" << caps.renderer << " vendor_id=0x" << std::hex
                << caps.vendor_id << " device_id=0x" << caps.device_id << std::dec
                << " adapter=\"" << caps.adapter_name << "\"\n";
    } else {
      std::cerr << "D3D12 adapter capability query failed: " << query << '\n';
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
