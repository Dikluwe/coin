#include <EGL/egl.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

int main(int argc, char **argv) {
  const bool points = argc > 1 && std::atoi(argv[1]) == 1;
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major=0,minor=0;
  if(display==EGL_NO_DISPLAY || !eglInitialize(display,&major,&minor)) return 77;
  eglBindAPI(EGL_OPENGL_API);
  const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
                        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,
                        EGL_DEPTH_SIZE,24,EGL_STENCIL_SIZE,8,EGL_NONE};
  EGLConfig config; EGLint count=0;
  if(!eglChooseConfig(display,attrs,&config,1,&count) || !count) return 77;
  const EGLint pbuf[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};
  EGLSurface surface=eglCreatePbufferSurface(display,config,pbuf);
  EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,nullptr);
  if(surface==EGL_NO_SURFACE || context==EGL_NO_CONTEXT ||
     !eglMakeCurrent(display,surface,surface,context)) return 77;
  std::printf("renderer=%s mode=%s\n",glGetString(GL_RENDERER),points?"point":"line");
  glViewport(0,0,64,64);
  glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(-1,1,-1,1,-1,1);
  glMatrixMode(GL_MODELVIEW);glLoadIdentity();
  const GLdouble px[]={1,0,0,0},py[]={0,1,0,0};
  glClipPlane(GL_CLIP_PLANE0,px);glClipPlane(GL_CLIP_PLANE1,py);
  glEnable(GL_CLIP_PLANE0);glEnable(GL_CLIP_PLANE1);
  glClearColor(0,0,0,1);glClearDepth(1);glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
  glPolygonMode(GL_FRONT_AND_BACK,points?GL_POINT:GL_LINE);
  glPointSize(6);glLineWidth(1);
  glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
  glEnable(GL_STENCIL_TEST);glStencilFunc(GL_EQUAL,0,0xff);
  glStencilOp(GL_KEEP,GL_KEEP,GL_INCR);
  glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(1,0,0,.5f);
  glBegin(GL_QUADS);
  glVertex2f(-.8f,-.8f);glVertex2f(.8f,-.8f);
  glVertex2f(.8f,.8f);glVertex2f(-.8f,.8f);
  glEnd();glFinish();
  const int x=points?32:31,y=points?32:40;
  unsigned char rgb[4]={},stencil=0;
  float depth=0;
  glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgb);
  glReadPixels(x,y,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);
  glReadPixels(x,y,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencil);
  GLenum error=glGetError();
  std::printf("pixel=%d,%d rgb=%u,%u,%u alpha=%u depth=%.5f stencil=%u error=%u\n",
              x,y,rgb[0],rgb[1],rgb[2],rgb[3],depth,stencil,error);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
  return error || rgb[0]<120 || rgb[0]>135 || rgb[1]>2 || rgb[2]>2 ||
         std::fabs(depth-.5f)>.01f || stencil!=1 ? 1:0;
}
