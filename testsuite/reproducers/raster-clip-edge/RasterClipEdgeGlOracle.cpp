// Minimal compatibility-GL control for CoinGL's clipped polygon-line edge.
// Build: c++ RasterClipEdgeGlOracle.cpp -o raster-clip-edge -lGL -lglut
#define GL_GLEXT_PROTOTYPES
#include <GL/freeglut.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr int width = 64;
constexpr int height = 64;
bool strictBoundary = false;
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
  int lower = 0, upper = 0;
  for (int y = 5; y <= 8; ++y) lower += pixels[(y * width + 16) * 3] > 127;
  for (int y = 29; y <= 32; ++y) upper += pixels[(y * width + 16) * 3] > 127;
  const bool boundary = !strictBoundary || !clipped || explicitEdge ||
    (mode == GL_LINE ? column(pixels, 16) == 18 :
     mode == GL_POINT ? lower == 4 && upper == 4 : true);
  std::printf("%s clipped=%d triangles=%d mode=%s x16=%d x17=%d x45=%d p16low=%d p16high=%d error=%u boundary=%d\n",
              label, clipped, triangulated,
              mode == GL_LINE ? "line" : mode == GL_POINT ? "point" : "fill",
              column(pixels, 16), column(pixels, 17), column(pixels, 45),
              lower, upper,
              static_cast<unsigned>(error), boundary);
  return error == GL_NO_ERROR && boundary;
}
} // namespace

int main(int argc, char **argv) {
  strictBoundary = argc == 2 && std::strcmp(argv[1], "--strict-boundary") == 0;
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
  glPointSize(6);
  ok = run("polygon-point", true, false, GL_POINT) && ok;
  ok = run("preclipped-polygon-point", false, false, GL_POINT, false, true) && ok;
  ok = run("unclipped-line", false, false, GL_LINE) && ok;
  ok = run("preclipped-polygon-line", false, false, GL_LINE, false, true) && ok;
  ok = run("explicit-cut-edge", false, false, GL_LINE, true) && ok;
  ok = run("explicit-cut-edge-clipped", true, false, GL_LINE, true) && ok;
  const char *vertexSource =
      "#version 130\n"
      "void main() {\n"
      "  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
      "  gl_ClipDistance[0] = gl_Vertex.x - (-0.484375);\n"
      "  gl_FrontColor = gl_Color;\n"
      "}\n";
  const char *fragmentSource =
      "#version 130\n"
      "void main() { gl_FragColor = gl_Color; }\n";
  const GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
  const GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(vertex, 1, &vertexSource, nullptr);
  glShaderSource(fragment, 1, &fragmentSource, nullptr);
  glCompileShader(vertex);
  glCompileShader(fragment);
  GLint vertexStatus = 0, fragmentStatus = 0, linkStatus = 0;
  glGetShaderiv(vertex, GL_COMPILE_STATUS, &vertexStatus);
  glGetShaderiv(fragment, GL_COMPILE_STATUS, &fragmentStatus);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
  std::printf("shader-status vertex=%d fragment=%d link=%d\n", vertexStatus,
              fragmentStatus, linkStatus);
  if (vertexStatus && fragmentStatus && linkStatus) {
    glUseProgram(program);
    ok = run("shader-clip-distance-polygon-line", true, false, GL_LINE) && ok;
    ok = run("shader-clip-distance-polygon-point", true, false, GL_POINT) && ok;
    ok = run("shader-clip-distance-polygon-fill", true, false, GL_FILL) && ok;
    glUseProgram(0);
  } else {
    ok = false;
  }
  glDeleteProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  glutDestroyWindow(glutGetWindow());
  return ok ? 0 : 1;
}
