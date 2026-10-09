// Minimal compatibility-GL control for CoinGL's clipped polygon-line edge.
// Build: c++ RasterClipEdgeGlOracle.cpp -o raster-clip-edge -lGL -lglut
#include <GL/freeglut.h>
#include <cstdio>
#include <vector>

namespace {
constexpr int width = 64;
constexpr int height = 64;
float coordinate(int pixel) { return (pixel + .5f) / 32.0f - 1.0f; }

int column(const std::vector<unsigned char> &pixels, int x) {
  int covered = 0;
  for (int y = 10; y <= 27; ++y)
    covered += pixels[(y * width + x) * 3] > 127;
  return covered;
}

void quad(bool triangulated, bool preclipped) {
  const float left = coordinate(preclipped ? 16 : 6), right = coordinate(45);
  const float bottom = coordinate(6), top = coordinate(31);
  if (triangulated) {
    glBegin(GL_TRIANGLES);
    glVertex2f(left, bottom); glVertex2f(right, bottom); glVertex2f(right, top);
    glVertex2f(left, bottom); glVertex2f(right, top); glVertex2f(left, top);
  } else {
    glBegin(GL_POLYGON);
    glVertex2f(left, bottom); glVertex2f(right, bottom);
    glVertex2f(right, top); glVertex2f(left, top);
  }
  glEnd();
}

void cutEdge() {
  glBegin(GL_LINES);
  glVertex2f(coordinate(16), coordinate(6));
  glVertex2f(coordinate(16), coordinate(31));
  glEnd();
}

bool run(const char *label, bool clipped, bool triangulated, GLenum mode,
         bool explicitEdge = false, bool preclipped = false) {
  glClear(GL_COLOR_BUFFER_BIT);
  glPolygonMode(GL_FRONT_AND_BACK, mode);
  if (clipped) glEnable(GL_CLIP_PLANE0);
  else glDisable(GL_CLIP_PLANE0);
  if (explicitEdge) cutEdge();
  else quad(triangulated, preclipped);
  glFinish();
  std::vector<unsigned char> pixels(width * height * 3);
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  const GLenum error = glGetError();
  std::printf("%s clipped=%d triangles=%d mode=%s x16=%d x17=%d x45=%d error=%u\n",
              label, clipped, triangulated, mode == GL_LINE ? "line" : "fill",
              column(pixels, 16), column(pixels, 17), column(pixels, 45),
              static_cast<unsigned>(error));
  return error == GL_NO_ERROR;
}
} // namespace

int main(int argc, char **argv) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
  glutInitWindowSize(width, height);
  if (glutCreateWindow("Coin clip-edge GL oracle") <= 0) return 2;
  glViewport(0, 0, width, height);
  glDrawBuffer(GL_BACK);
  glReadBuffer(GL_BACK);
  glDisable(GL_DITHER);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glClearColor(0, 0, 0, 1);
  glColor3f(1, 1, 1);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-1, 1, -1, 1, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  const double plane[] = {1, 0, 0, -double(coordinate(16))};
  glClipPlane(GL_CLIP_PLANE0, plane);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  const char *vendor = reinterpret_cast<const char *>(glGetString(GL_VENDOR));
  const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
  const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
  std::printf("GL vendor=%s renderer=%s version=%s\n",
              vendor ? vendor : "unknown", renderer ? renderer : "unknown",
              version ? version : "unknown");
  bool ok = true;
  ok = run("polygon-line", true, false, GL_LINE) && ok;
  ok = run("triangles-line", true, true, GL_LINE) && ok;
  ok = run("polygon-fill", true, false, GL_FILL) && ok;
  ok = run("unclipped-line", false, false, GL_LINE) && ok;
  ok = run("preclipped-polygon-line", false, false, GL_LINE, false, true) && ok;
  ok = run("explicit-cut-edge", false, false, GL_LINE, true) && ok;
  ok = run("explicit-cut-edge-clipped", true, false, GL_LINE, true) && ok;
  glutDestroyWindow(glutGetWindow());
  return ok ? 0 : 1;
}
