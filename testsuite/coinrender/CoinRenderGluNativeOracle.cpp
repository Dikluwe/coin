// A small native-GLU oracle independent of Coin's offscreen renderer.
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include <algorithm>
#include <cmath>
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
#elif defined(__linux__)
#include <GL/freeglut.h>
#else
#error This oracle requires desktop OpenGL.
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
#elif defined(__APPLE__)
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
#else
struct Context {
  bool create() {
    int argc = 1;
    char name[] = "Coin GLU oracle";
    char *argv[] = {name, nullptr};
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA);
    return glutCreateWindow(name) > 0;
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
  if (result != 0) return 1;

  for (const int extent : {16, 32}) {
    CoinRenderTextureImageSnapshot image;
    image.width = 17;
    image.height = 19;
    image.pixelsRgba.resize(size_t(image.width) * image.height * 4);
    for (uint32_t y = 0; y < image.height; ++y)
      for (uint32_t x = 0; x < image.width; ++x)
        for (int c = 0; c < 4; ++c)
          image.pixelsRgba[(size_t(y) * image.width + x) * 4 + c] =
              static_cast<uint8_t>((x * 37 + y * 19 + c * 53) & 255);
    const auto source = image.pixelsRgba;
    std::vector<unsigned char> native(size_t(extent) * extent * 4);
    const GLint error = gluScaleImage(GL_RGBA, 17, 19, GL_UNSIGNED_BYTE,
                                      source.data(), extent, extent,
                                      GL_UNSIGNED_BYTE, native.data());
    if (error || !CoinRenderTextureSamplingCore::legacyResizeGlu(
                     image, uint32_t(extent), uint32_t(extent)))
      return 1;
    int maximum = 0;
    double total = 0;
    for (size_t i = 0; i < native.size(); ++i) {
      const int delta = std::abs(int(native[i]) - int(image.pixelsRgba[i]));
      maximum = std::max(maximum, delta);
      total += delta;
    }
    std::cout << "17x19->" << extent << 'x' << extent
              << " max=" << maximum << " mae=" << total / native.size() << '\n';
    if (maximum > 2 || total / native.size() > 0.6) return 1;
  }
  return 0;
}
