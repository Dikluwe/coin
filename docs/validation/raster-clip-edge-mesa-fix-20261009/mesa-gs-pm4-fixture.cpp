#include <EGL/egl.h>
#include <GL/gl.h>
#include <cstdio>

static void draw(bool triangles) {
  glClear(GL_COLOR_BUFFER_BIT);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  glEnable(GL_CLIP_PLANE0);
  std::fprintf(stderr, "SHIM before %s\n", triangles ? "triangles" : "polygon");
  if (triangles) {
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f, -0.8f); glVertex2f(0.8f, -0.8f); glVertex2f(0.8f, 0.8f);
    glVertex2f(-0.8f, -0.8f); glVertex2f(0.8f, 0.8f); glVertex2f(-0.8f, 0.8f);
  } else {
    glBegin(GL_POLYGON);
    glVertex2f(-0.8f, -0.8f); glVertex2f(0.8f, -0.8f);
    glVertex2f(0.8f, 0.8f); glVertex2f(-0.8f, 0.8f);
  }
  glEnd();
  if (triangles) glFlush();
  std::fprintf(stderr, "SHIM after %s error=%u\n", triangles ? "triangles" : "polygon", glGetError());
}

int main() {
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major = 0, minor = 0;
  if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) {
    std::fprintf(stderr, "EGL initialize failed: 0x%x\n", eglGetError()); return 2;
  }
  if (!eglBindAPI(EGL_OPENGL_API)) return 3;
  const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (!eglChooseConfig(display, attrs, &config, 1, &count) || !count) return 4;
  const EGLint pbuf[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
  EGLSurface surface = eglCreatePbufferSurface(display, config, pbuf);
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, nullptr);
  if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
      !eglMakeCurrent(display, surface, surface, context)) {
    std::fprintf(stderr, "EGL context failed: 0x%x\n", eglGetError()); return 5;
  }
  std::fprintf(stderr, "SHIM GL=%s renderer=%s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
  glViewport(0, 0, 64, 64);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1, 1, -1, 1, -1, 1);
  glMatrixMode(GL_MODELVIEW); glLoadIdentity();
  const GLdouble plane[] = {1, 0, 0, 0};
  glClipPlane(GL_CLIP_PLANE0, plane);
  draw(false);
  draw(true);
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglDestroySurface(display, surface);
  eglTerminate(display);
  return 0;
}
