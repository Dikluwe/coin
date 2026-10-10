import pathlib
root=pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\sampling-win-20261010\coin')
p=root/'testsuite/coinrender/CoinRenderSamplingPolicyTest.cpp'
s=p.read_text(encoding='utf-8')
windows=r'''
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
struct SamplingWindows {
  HWND native=nullptr, portable=nullptr;
  static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_DPICHANGED) {
      const auto* r=reinterpret_cast<const RECT*>(l);
      SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
      return 0;
    }
    return DefWindowProcW(h,m,w,l);
  }
  static void pump() {MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
  bool open() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW c{};c.lpfnWndProc=proc;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"CoinSamplingQualification";
    if(!RegisterClassW(&c) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    native=CreateWindowW(c.lpszClassName,L"Sampling native",WS_POPUP,50,50,64,64,nullptr,nullptr,c.hInstance,nullptr);
    portable=CreateWindowW(c.lpszClassName,L"Sampling portable",WS_POPUP,150,50,64,64,nullptr,nullptr,c.hInstance,nullptr);
    if(!native || !portable)return false;
    ShowWindow(native,SW_SHOWNOACTIVATE);ShowWindow(portable,SW_SHOWNOACTIVATE);pump();return true;
  }
  void size(HWND w,int n) {SetWindowPos(w,HWND_TOPMOST,0,0,n,n,SWP_NOMOVE|SWP_NOACTIVATE);pump();}
  CoinRenderNativeSurfaceDescriptor descriptor(HWND w) const {
    CoinRenderNativeSurfaceDescriptor d{};d.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;d.structSize=sizeof(d);
    d.type=COIN_RENDER_SURFACE_WIN32;d.native.win32.hinstance=GetModuleHandleW(nullptr);d.native.win32.hwnd=w;return d;
  }
  static bool capture(HWND w,int n,std::vector<uint8_t>& pixels) {
    pump();std::this_thread::sleep_for(std::chrono::milliseconds(50));
    HDC dc=GetDC(w),mem=dc ? CreateCompatibleDC(dc) : nullptr;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=n;info.bmiHeader.biHeight=-n;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* data=nullptr;HBITMAP bitmap=dc ? CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0) : nullptr;
    HGDIOBJ old=bitmap && mem ? SelectObject(mem,bitmap) : nullptr;
    bool ok=old && BitBlt(mem,0,0,n,n,dc,0,0,SRCCOPY|CAPTUREBLT);GdiFlush();pixels.clear();
    if(ok) {
      pixels.resize(size_t(n*n*4));const auto* b=static_cast<const uint8_t*>(data);
      for(size_t i=0;i<pixels.size();i+=4){pixels[i]=b[i+2];pixels[i+1]=b[i+1];pixels[i+2]=b[i];pixels[i+3]=255;}
    }
    if(old)SelectObject(mem,old);if(bitmap)DeleteObject(bitmap);if(mem)DeleteDC(mem);if(dc)ReleaseDC(w,dc);
    return ok;
  }
  ~SamplingWindows(){if(native)DestroyWindow(native);if(portable)DestroyWindow(portable);}
};
#endif
'''
s=s.replace('static bool check(bool value, const char* label) {',windows+'\nstatic bool check(bool value, const char* label) {',1)
s=s.replace('COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW : COIN_RENDER_EXPERIMENTAL_OFFSCREEN','samplingWindowBackend : COIN_RENDER_EXPERIMENTAL_OFFSCREEN')
s=s.replace('static bool selection(bool gpu, bool window = false) {','''#ifdef _WIN32
static constexpr auto samplingWindowBackend=COIN_RENDER_EXPERIMENTAL_WIN32_WINDOW;
#else
static constexpr auto samplingWindowBackend=COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW;
#endif
static bool selection(bool gpu, bool window = false) {''')
s=s.replace('ok &= check(coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_PORTABLE,1).reason==COIN_RENDER_SELECTION_SUPPORTED,"bounded Linux offscreen/Xlib portable qualification");','''#ifdef _WIN32
  ok &= check(coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_PORTABLE,0).reason==COIN_RENDER_SELECTION_SUPPORTED,"Windows admits explicit portable policy without fallback");
  ok &= check(coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_PORTABLE,1).reason==COIN_RENDER_SELECTION_UNQUALIFIED_PROFILE,"Windows does not invent a qualified profile from Linux evidence");
#else
  ok &= check(coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_PORTABLE,1).reason==COIN_RENDER_SELECTION_SUPPORTED,"bounded Linux offscreen/Xlib portable qualification");
#endif''')
# Shared creation, resize and manager/adapter blocks now admit Win32 as well.
s=s.replace('#ifdef COIN_SAMPLING_X11\n  SamplingWindows windows;', '#if defined(COIN_SAMPLING_X11) || defined(_WIN32)\n  SamplingWindows windows;')
s=s.replace('#ifdef COIN_SAMPLING_X11\n    if (window) return', '#if defined(COIN_SAMPLING_X11) || defined(_WIN32)\n    if (window) return')
s=s.replace('#ifdef COIN_SAMPLING_X11\n  if(window) windows.size', '#if defined(COIN_SAMPLING_X11) || defined(_WIN32)\n  if(window) windows.size')
s=s.replace('#ifdef COIN_SAMPLING_X11\n  if(window) {', '#if defined(COIN_SAMPLING_X11) || defined(_WIN32)\n  if(window) {')
s=s.replace('    target->readbackRGBA(pixels);\n  };', '''#ifdef _WIN32
    if(externalCapture && target->getPimpl()->kind==CoinRenderTargetP::KIND_WINDOW) {
      const auto n=target->getSize();
      SamplingWindows::capture(static_cast<HWND>(target->getPimpl()->nativeDesc.native.win32.hwnd),n[0],pixels);return;
    }
#endif
    target->readbackRGBA(pixels);
  };''')
s=s.replace('    XUnmapWindow(windows.display, windows.portable); XSync(windows.display,False);','''#ifdef _WIN32
    ShowWindow(windows.portable,SW_HIDE);SamplingWindows::pump();
#else
    XUnmapWindow(windows.display, windows.portable); XSync(windows.display,False);
#endif''')
s=s.replace('    XMapWindow(windows.display,windows.portable);XSync(windows.display,False);\n    XEvent mapped;do {XWindowEvent(windows.display,windows.portable,StructureNotifyMask,&mapped);} while(mapped.type!=MapNotify);','''#ifdef _WIN32
    ShowWindow(windows.portable,SW_SHOWNOACTIVATE);SamplingWindows::pump();
#else
    XMapWindow(windows.display,windows.portable);XSync(windows.display,False);
    XEvent mapped;do {XWindowEvent(windows.display,windows.portable,StructureNotifyMask,&mapped);} while(mapped.type!=MapNotify);
#endif''')
p.write_text(s,encoding='utf-8',newline='\n')
p=root/'testsuite/CMakeLists.txt';s=p.read_text(encoding='utf-8')
s=s.replace('\tadd_test(NAME CoinRenderSamplingPolicyTest COMMAND CoinRenderSamplingPolicyTest)', '''\tif(WIN32)
\t\ttarget_compile_definitions(CoinRenderSamplingPolicyTest PRIVATE _WIN32_WINNT=0x0A00)
\t\ttarget_link_libraries(CoinRenderSamplingPolicyTest user32 gdi32)
\t\tadd_test(NAME CoinRenderSamplingPolicyWindowTest COMMAND CoinRenderSamplingPolicyTest --window)
\t\tset_tests_properties(CoinRenderSamplingPolicyWindowTest PROPERTIES TIMEOUT 120)
\tendif()
\tadd_test(NAME CoinRenderSamplingPolicyTest COMMAND CoinRenderSamplingPolicyTest)''')
p.write_text(s,encoding='utf-8',newline='\n')
