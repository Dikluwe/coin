// A small native-GLU oracle independent of Coin's offscreen renderer.
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#elif defined(__APPLE__)
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#error This oracle is for Windows and macOS.
#endif

namespace {
#ifdef _WIN32
struct Context {
  HWND window = nullptr;
  HDC dc = nullptr;
  HGLRC gl = nullptr;
  bool create() {
    window = CreateWindowA("STATIC", "Coin GLU oracle", WS_OVERLAPPEDWINDOW,
                           0, 0, 64, 64, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (!window) return false;
    dc = GetDC(window);
    if (!dc) return false;
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    const int format = ChoosePixelFormat(dc, &pfd);
    if (!format || !SetPixelFormat(dc, format, &pfd)) return false;
    gl = wglCreateContext(dc);
    return gl && wglMakeCurrent(dc, gl);
  }
  ~Context() {
    if (gl) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(gl); }
    if (dc) ReleaseDC(window, dc);
    if (window) DestroyWindow(window);
  }
};
#else
struct Context {
  CGLPixelFormatObj format = nullptr;
  CGLContextObj gl = nullptr;
  bool create() {
    // Coin's legacy offscreen/pbuffer attributes are unsupported on some
    // hosted macOS runners. GLU only needs a current context for pixel store.
    const CGLPixelFormatAttribute choices[][3] = {
        {static_cast<CGLPixelFormatAttribute>(kCGLPFAOpenGLProfile),
         static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_Legacy),
         static_cast<CGLPixelFormatAttribute>(0)},
        {static_cast<CGLPixelFormatAttribute>(kCGLPFAOpenGLProfile),
         static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),
         static_cast<CGLPixelFormatAttribute>(0)},
        {static_cast<CGLPixelFormatAttribute>(0),
         static_cast<CGLPixelFormatAttribute>(0),
         static_cast<CGLPixelFormatAttribute>(0)}};
    for (const auto &choice : choices) {
      GLint count = 0;
      if (CGLChoosePixelFormat(const_cast<CGLPixelFormatAttribute *>(choice),
                               &format, &count) != kCGLNoError || !format)
        continue;
      if (CGLCreateContext(format, nullptr, &gl) == kCGLNoError && gl &&
          CGLSetCurrentContext(gl) == kCGLNoError)
        return true;
      if (gl) { CGLDestroyContext(gl); gl = nullptr; }
      CGLDestroyPixelFormat(format);
      format = nullptr;
    }
    return false;
  }
  ~Context() {
    if (gl) { CGLSetCurrentContext(nullptr); CGLDestroyContext(gl); }
    if (format) CGLDestroyPixelFormat(format);
  }
};
#endif
} // namespace

int main() {
  Context context;
  if (!context.create()) {
    std::cerr << "Native OpenGL context unavailable for GLU oracle\n";
    return 2;
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  const unsigned char input[] = {10, 0, 0, 255, 30, 0, 0, 255};
  unsigned char output[16] = {};
  const GLint result = gluScaleImage(GL_RGBA, 2, 1, GL_UNSIGNED_BYTE, input,
                                     4, 1, GL_UNSIGNED_BYTE, output);
  const char *vendor = reinterpret_cast<const char *>(glGetString(GL_VENDOR));
  const char *version = reinterpret_cast<const char *>(gluGetString(GLU_VERSION));
  std::cout << "GL=" << (vendor ? vendor : "unknown")
            << " GLU=" << (version ? version : "unknown")
            << " resize=" << result << " red=" << int(output[0]) << ','
            << int(output[4]) << ',' << int(output[8]) << ','
            << int(output[12]) << '\n';
  const int expected[] = {15, 15, 25, 25};
  for (int i = 0; i < 4; ++i)
    if (std::abs(int(output[i * 4]) - expected[i]) > 1 ||
        output[i * 4 + 3] != 255)
      return 1;
  return result == 0 ? 0 : 1;
}
