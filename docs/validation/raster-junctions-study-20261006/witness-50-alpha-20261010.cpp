#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <cstdio>
#include <vector>

int main() {
  EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY); EGLint major=0,minor=0;
  if(display==EGL_NO_DISPLAY || !eglInitialize(display,&major,&minor)) return 77;
  eglBindAPI(EGL_OPENGL_API);
  const EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig config; EGLint count=0;
  if(!eglChooseConfig(display,config_attrs,&config,1,&count) || !count) return 77;
  const EGLint surface_attrs[]={EGL_WIDTH,80,EGL_HEIGHT,80,EGL_NONE};
  EGLSurface surface=eglCreatePbufferSurface(display,config,surface_attrs);
  EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,nullptr);
  if(surface==EGL_NO_SURFACE || context==EGL_NO_CONTEXT || !eglMakeCurrent(display,surface,surface,context)) return 77;
  std::fprintf(stderr,"renderer=%s GL=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
  glViewport(0,0,80,80);
  glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(-1.5,1.5,-1.5,1.5,-1,1);
  glMatrixMode(GL_MODELVIEW);glLoadIdentity();
  glDisable(GL_DEPTH_TEST);glDisable(GL_LINE_SMOOTH);glDisable(GL_MULTISAMPLE);
  glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);
  glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
  glLineWidth(4);
  auto line=[](bool reverse,bool blue) {
    glColor4f(blue?0.f:1.f,0.f,blue?1.f:0.f,.5f);
    glBegin(GL_LINES);
    if(reverse) {glVertex2f(-.5416687727f,.4987918437f);glVertex2f(-.4319664538f,.6254651546f);}
    else {glVertex2f(-.4319664538f,.6254651546f);glVertex2f(-.5416687727f,.4987918437f);}
    glEnd();
  };
  const char *labels[]={"authored", "reversed", "same-pair", "opposite-pair"};
  for(int mode=0;mode<4;++mode) {
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    if(mode==0) line(false,false);
    if(mode==1) line(true,false);
    if(mode==2) {line(false,false);line(false,true);}
    if(mode==3) {line(false,false);line(true,true);}
    glFinish();
    for(int ytop: {22,23,24,25,26,27}) {
      unsigned char pixel[4]={};glReadPixels(26,79-ytop,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
      std::printf("%s pixel=26,%d rgba=%u,%u,%u,%u\n",labels[mode],ytop,pixel[0],pixel[1],pixel[2],pixel[3]);
    }
  }
  GLenum error=glGetError();std::printf("error=%u\n",error);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
  return error?1:0;
}
