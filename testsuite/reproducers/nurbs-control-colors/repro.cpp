#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoVertexProperty.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNurbsSurface.h>
#include <Inventor/nodes/SoIndexedNurbsSurface.h>
#include <cstdio>
#include <cmath>
#include <Inventor/SbColor.h>

// Regression for #413: planar geometry must still sample the color basis.
// Run with an X11/GLX display (for example, xvfb-run -a).
int main(int argc, char **) {
  Display *dpy = XOpenDisplay(NULL);
  if (!dpy) { std::puts("no X display"); return 2; }
  int screen = DefaultScreen(dpy);
  int attrs[] = {GLX_RGBA, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 16, None};
  XVisualInfo *vi = glXChooseVisual(dpy, screen, attrs);
  if (!vi) { std::puts("no GLX visual"); return 2; }
  Colormap cmap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
  XSetWindowAttributes swa{}; swa.colormap = cmap; swa.event_mask = ExposureMask;
  Window win = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 0, 0, 128, 128, 0, vi->depth, InputOutput, vi->visual, CWColormap|CWEventMask, &swa);
  XMapWindow(dpy, win); XSync(dpy, False);
  GLXContext ctx = glXCreateContext(dpy, vi, NULL, True);
  if (!ctx || !glXMakeCurrent(dpy, win, ctx)) { std::puts("GLX context creation failed"); return 2; }
  glViewport(0, 0, 128, 128); glDisable(GL_DITHER); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  SoDB::init();
  SoSeparator *root = new SoSeparator; root->ref();
  SoOrthographicCamera *camera = new SoOrthographicCamera; camera->position.setValue(0,0,3); camera->height=2.2f; root->addChild(camera);
  SoLightModel *lm = new SoLightModel; lm->model=SoLightModel::BASE_COLOR; root->addChild(lm);
  SoVertexProperty *vp = new SoVertexProperty;
  const SbVec3f points[4] = {SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(-1,1,0),SbVec3f(1,1,0)};
  const uint32_t colors[4] = {SbColor(1,0,0).getPackedValue(), SbColor(0,1,0).getPackedValue(), SbColor(0,0,1).getPackedValue(), SbColor(1,1,1).getPackedValue()};
  vp->vertex.setValues(0,4,points); vp->orderedRGBA.setValues(0,4,colors); vp->materialBinding=SoMaterialBinding::PER_VERTEX; root->addChild(vp);
  const float knots[4]={0,0,1,1};
  if (argc > 1) {
    SoIndexedNurbsSurface *surface = new SoIndexedNurbsSurface;
    surface->numUControlPoints=2; surface->numVControlPoints=2;
    const int32_t indices[4]={0,1,2,3};
    surface->coordIndex.setValues(0,4,indices);
    surface->uKnotVector.setValues(0,4,knots);
    surface->vKnotVector.setValues(0,4,knots); root->addChild(surface);
  }
  else {
    SoNurbsSurface *surface = new SoNurbsSurface;
    surface->numUControlPoints=2; surface->numVControlPoints=2;
    surface->uKnotVector.setValues(0,4,knots);
    surface->vKnotVector.setValues(0,4,knots); root->addChild(surface);
  }
  int result = 0;
  {
    SoGLRenderAction action(SbViewportRegion(128,128));
    action.setCacheContext(SoGLCacheContextElement::getUniqueCacheContext());
    action.apply(root); glFinish();
    // Compare a grid of pixels with the analytic bilinear color field.
    // A small tolerance allows finite tessellation and framebuffer rounding.
    for (int y=32; y<=96; y+=32) {
      for (int x=32; x<=96; x+=32) {
        unsigned char pixel[3];
        glReadPixels(x,y,1,1,GL_RGB,GL_UNSIGNED_BYTE,pixel);
        const float u = ((x+0.5f)/128.0f*2.2f-0.1f)/2.0f;
        const float v = ((y+0.5f)/128.0f*2.2f-0.1f)/2.0f;
        const float expected[3] = {1-u-v+2*u*v, u, v};
        for (int c=0; c<3; ++c) {
          if (std::fabs(pixel[c]-255*expected[c]) > 6.0f) result=1;
        }
        if (x==64 && y==64)
          std::printf("%s center RGB=(%u,%u,%u)\n",
                      argc>1 ? "indexed" : "nonindexed",pixel[0],pixel[1],pixel[2]);
      }
    }
  }
  root->unref();
  glXMakeCurrent(dpy, None, NULL); glXDestroyContext(dpy,ctx);
  XDestroyWindow(dpy,win); XFreeColormap(dpy,cmap); XFree(vi); XCloseDisplay(dpy);
  return result;
}
