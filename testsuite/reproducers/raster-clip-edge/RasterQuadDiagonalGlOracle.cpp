// Compare GL_QUADS interpolation with both explicit triangle diagonals.
// Build: c++ RasterQuadDiagonalGlOracle.cpp -o raster-quad-diagonal -lGL -lglut
#include <GL/freeglut.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
constexpr int side = 64;
float coord(int p) { return (p + .5f) / 32.0f - 1.0f; }
struct Vertex { float x, y, r, g, b; };
const Vertex vertices[] = {
    {coord(6), coord(6), .3f, .8f, .4f},
    {coord(29), coord(6), .37f, .74f, .44f},
    {coord(29), coord(47), .44f, .68f, .48f},
    {coord(6), coord(47), .51f, .62f, .52f},
};
void emit(int i) {
  const auto &v = vertices[i];
  glColor3f(v.r, v.g, v.b);
  glVertex2f(v.x, v.y);
}
std::vector<unsigned char> draw(const char *name, const int *indices, int count,
                                GLenum primitive) {
  glClear(GL_COLOR_BUFFER_BIT);
  glBegin(primitive);
  for (int i = 0; i < count; ++i) emit(indices[i]);
  glEnd();
  glFinish();
  std::vector<unsigned char> pixels(side * side * 3);
  glReadPixels(0, 0, side, side, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  std::printf("%s sample(17,30)=%u,%u,%u error=%u\n", name,
              pixels[(30 * side + 17) * 3], pixels[(30 * side + 17) * 3 + 1],
              pixels[(30 * side + 17) * 3 + 2], unsigned(glGetError()));
  return pixels;
}
void compare(const char *name, const std::vector<unsigned char> &a,
             const std::vector<unsigned char> &b) {
  int maximum = 0, mismatches = 0;
  for (int y = 9; y < 45; ++y)
    for (int x = 9; x < 27; ++x)
      for (int c = 0; c < 3; ++c) {
        const int i = (y * side + x) * 3 + c;
        const int difference = std::abs(int(a[i]) - int(b[i]));
        maximum = std::max(maximum, difference);
        mismatches += difference > 1;
      }
  std::printf("quad/%s interior max=%d channels-over-1=%d\n", name, maximum,
              mismatches);
}
} // namespace
int main(int argc, char **argv) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
  glutInitWindowSize(side, side);
  if (glutCreateWindow("Coin quad diagonal GL oracle") <= 0) return 2;
  glViewport(0, 0, side, side);
  glDrawBuffer(GL_BACK);
  glReadBuffer(GL_BACK);
  glDisable(GL_DITHER);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glShadeModel(GL_SMOOTH);
  glClearColor(0, 0, 0, 1);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-1, 1, -1, 1, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  const int quad[] = {0, 1, 2, 3};
  const int diagonal02[] = {0, 1, 2, 0, 2, 3};
  const int diagonal13[] = {0, 1, 3, 1, 2, 3};
  const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
  std::printf("GL renderer=%s\n", renderer ? renderer : "unknown");
  const auto q = draw("quad", quad, 4, GL_QUADS);
  compare("diagonal-0-2", q, draw("diagonal-0-2", diagonal02, 6, GL_TRIANGLES));
  compare("diagonal-1-3", q, draw("diagonal-1-3", diagonal13, 6, GL_TRIANGLES));
  glutDestroyWindow(glutGetWindow());
}
