#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
struct WGLNative {
  HWND window = NULL;
  HDC dc = NULL;
  HGLRC context = NULL;
  bool create() {
    static const wchar_t * name = L"CoinValidationWGL";
    WNDCLASSW wc = {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = name;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    window = CreateWindowW(name, L"Coin WGL validation", WS_POPUP, 0, 0, 64, 64,
                           NULL, NULL, wc.hInstance, NULL);
    if (!window) return false;
    dc = GetDC(window);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    const int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd)) return false;
    context = wglCreateContext(dc);
    return context != NULL;
  }
  bool select() { return wglMakeCurrent(dc, context) == TRUE; }
  void destroy() {
    if (context) wglDeleteContext(context);
    if (dc) ReleaseDC(window, dc);
    if (window) DestroyWindow(window);
    context = NULL; dc = NULL; window = NULL;
  }
};
static void releaseWGL() { wglMakeCurrent(NULL, NULL); }
static GLboolean isGLObject(const char * name, GLuint value) {
  typedef GLboolean (APIENTRY *IsObject)(GLuint);
  IsObject fn = reinterpret_cast<IsObject>(wglGetProcAddress(name));
  return fn ? fn(value) : GL_FALSE;
}
