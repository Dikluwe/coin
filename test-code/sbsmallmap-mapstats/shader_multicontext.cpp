#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/gl.h>

#include <Inventor/SoDB.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoVertexShader.h>
#include <Inventor/nodes/SoFragmentShader.h>
#include <Inventor/nodes/SoShaderParameter.h>
#include <Inventor/SbViewportRegion.h>

struct GLWindow {
  Display * display;
  Window win;
  GLXContext ctx;
  int width, height;
};

static GLWindow createWindow(Display * display, const char * title, int w, int h) {
  GLWindow gw;
  gw.display = display;
  gw.width = w;
  gw.height = h;

  int attrs[] = {
    GLX_RGBA, GLX_DOUBLEBUFFER,
    GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
    GLX_DEPTH_SIZE, 16,
    None
  };
  XVisualInfo * vi = glXChooseVisual(display, DefaultScreen(display), attrs);
  if (!vi) { fprintf(stderr, "no visual\n"); exit(1); }

  Window root = RootWindow(display, vi->screen);
  Colormap cmap = XCreateColormap(display, root, vi->visual, AllocNone);
  XSetWindowAttributes swa;
  swa.colormap = cmap;
  swa.event_mask = ExposureMask;

  gw.win = XCreateWindow(display, root, 0, 0, w, h, 0, vi->depth, InputOutput,
                          vi->visual, CWColormap | CWEventMask, &swa);
  XStoreName(display, gw.win, title);
  XMapWindow(display, gw.win);

  gw.ctx = glXCreateContext(display, vi, NULL, True /* direct rendering */);
  if (!gw.ctx) { fprintf(stderr, "no glx context\n"); exit(1); }
  XFree(vi);

  XSync(display, False);
  return gw;
}

int main(int argc, char ** argv) {
  Display * display = XOpenDisplay(NULL);
  if (!display) { fprintf(stderr, "cannot open display\n"); return 1; }

  SoDB::init();

  // build scene: a cube using an indexed face set, same node reused for both windows
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoPerspectiveCamera * cam = new SoPerspectiveCamera;
  cam->position.setValue(0, 0, 5);
  cam->nearDistance = 1.0f;
  cam->farDistance = 10.0f;
  root->addChild(cam);

  SoShaderProgram * shaderprogram = new SoShaderProgram;
  SoVertexShader * vertexshader = new SoVertexShader;
  vertexshader->sourceType = SoShaderObject::GLSL_PROGRAM;
  vertexshader->sourceProgram = "void main(void) { gl_Position = ftransform(); }";
  SoFragmentShader * fragmentshader = new SoFragmentShader;
  fragmentshader->sourceType = SoShaderObject::GLSL_PROGRAM;
  fragmentshader->sourceProgram =
    "uniform float gain; void main(void) { gl_FragColor = vec4(gain, 0.2, 0.3, 1.0); }";
  SoShaderParameter1f * gain = new SoShaderParameter1f;
  gain->name = "gain";
  gain->value = 0.8f;
  fragmentshader->parameter.set1Value(0, gain);
  shaderprogram->shaderObject.set1Value(0, vertexshader);
  shaderprogram->shaderObject.set1Value(1, fragmentshader);
  root->addChild(shaderprogram);

  SoShapeHints * hints = new SoShapeHints;
  hints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
  hints->shapeType = SoShapeHints::SOLID;
  root->addChild(hints);

  static const float coords[8][3] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
  };
  SoCoordinate3 * coord = new SoCoordinate3;
  coord->point.setValues(0, 8, coords);
  root->addChild(coord);

  static const int32_t idx[] = {
    0,1,2,3,-1,  4,5,6,7,-1,
    0,1,5,4,-1,  1,2,6,5,-1,
    2,3,7,6,-1,  3,0,4,7,-1
  };
  static const float facenormals[6][3] = {
    { 0, 0,-1}, { 0, 0, 1},
    { 0,-1, 0}, { 1, 0, 0},
    { 0, 1, 0}, {-1, 0, 0}
  };
  // one (unnormalized-corner-direction) normal per coordinate, so the
  // normal array can reuse coordIndex directly (nindices == NULL case),
  // which is required for the vertex-array/VBO fast path to engage
  static const float vertexnormals[8][3] = {
    {-0.577f,-0.577f,-0.577f}, { 0.577f,-0.577f,-0.577f},
    { 0.577f, 0.577f,-0.577f}, {-0.577f, 0.577f,-0.577f},
    {-0.577f,-0.577f, 0.577f}, { 0.577f,-0.577f, 0.577f},
    { 0.577f, 0.577f, 0.577f}, {-0.577f, 0.577f, 0.577f}
  };
  SoNormal * normal = new SoNormal;
  normal->vector.setValues(0, 8, vertexnormals);
  root->addChild(normal);

  SoNormalBinding * nbind = new SoNormalBinding;
  nbind->value = SoNormalBinding::PER_VERTEX_INDEXED;
  root->addChild(nbind);

  SoIndexedFaceSet * ifs = new SoIndexedFaceSet;
  ifs->coordIndex.setValues(0, sizeof(idx) / sizeof(idx[0]), idx);
  root->addChild(ifs);

  const int W = 200, H = 200;
  SbViewportRegion vp(W, H);

  GLWindow winA = createWindow(display, "MapStats Window A", W, H);
  GLWindow winB = createWindow(display, "MapStats Window B", W, H);

  // --- render into window A, a couple of times ---
  glXMakeCurrent(display, winA.win, winA.ctx);
  glViewport(0, 0, W, H);
  uint32_t ctxA = SoGLCacheContextElement::getUniqueCacheContext();
  printf("Window A: GLX direct=%d, cache context id=%u\n", glXIsDirect(display, winA.ctx), ctxA);
  for (int i = 0; i < 3; i++) {
    SoGLRenderAction actionA(vp);
    actionA.setCacheContext(ctxA);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    actionA.apply(root);
    glXSwapBuffers(display, winA.win);
    printf("  window A render pass %d done\n", i);
  }

  // --- render into window B, a different real GL context, same scene ---
  glXMakeCurrent(display, winB.win, winB.ctx);
  glViewport(0, 0, W, H);
  uint32_t ctxB = SoGLCacheContextElement::getUniqueCacheContext();
  printf("Window B: GLX direct=%d, cache context id=%u\n", glXIsDirect(display, winB.ctx), ctxB);
  for (int i = 0; i < 3; i++) {
    SoGLRenderAction actionB(vp);
    actionB.setCacheContext(ctxB);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    actionB.apply(root);
    glXSwapBuffers(display, winB.win);
    printf("  window B render pass %d done\n", i);
  }

  // --- go back to window A once more (repeat-context hit path) ---
  glXMakeCurrent(display, winA.win, winA.ctx);
  {
    SoGLRenderAction actionA2(vp);
    actionA2.setCacheContext(ctxA);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    actionA2.apply(root);
    glXSwapBuffers(display, winA.win);
    printf("  window A repeat render done\n");
  }

  // leave the windows up briefly so they are visible, then close
  sleep(2);

  glXMakeCurrent(display, None, NULL);
  glXDestroyContext(display, winA.ctx);
  glXDestroyContext(display, winB.ctx);
  XDestroyWindow(display, winA.win);
  XDestroyWindow(display, winB.win);

  root->unref();
  XCloseDisplay(display);
  printf("Done.\n");
  return 0;
}
