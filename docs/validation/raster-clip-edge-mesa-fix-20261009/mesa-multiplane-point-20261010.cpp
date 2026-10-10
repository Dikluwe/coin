#include <EGL/egl.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char **argv) {
  const int count = argc > 1 ? std::atoi(argv[1]) : 2;
  if (count < 1 || count > 8) return 2;
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major=0, minor=0;
  if (display == EGL_NO_DISPLAY || !eglInitialize(display,&major,&minor)) return 77;
  eglBindAPI(EGL_OPENGL_API);
  const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
  EGLConfig config; EGLint nconfigs=0;
  if (!eglChooseConfig(display,attrs,&config,1,&nconfigs) || !nconfigs) return 77;
  const EGLint pbuf[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};
  EGLSurface surface=eglCreatePbufferSurface(display,config,pbuf);
  EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,nullptr);
  if (surface==EGL_NO_SURFACE || context==EGL_NO_CONTEXT || !eglMakeCurrent(display,surface,surface,context)) return 77;
  std::fprintf(stderr,"GL=%s renderer=%s planes=%d\n",glGetString(GL_VERSION),glGetString(GL_RENDERER),count);
  glViewport(0,0,64,64);
  glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1,1,-1,1,-1,1);
  glMatrixMode(GL_MODELVIEW); glLoadIdentity();
  const GLdouble planes[8][4]={{1,0,0,0},{0,1,0,0},{-1,0,0,.8},{0,-1,0,.8},{1,0,0,.5},{0,1,0,.5},{-1,0,0,1},{0,-1,0,1}};
  for (int i=0;i<count;++i) { glClipPlane(GL_CLIP_PLANE0+i,planes[i]); glEnable(GL_CLIP_PLANE0+i); }
  glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
  const unsigned char texels[6]={255,0,0, 0,255,0};
  GLuint texture=0; glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,2,1,0,GL_RGB,GL_UNSIGNED_BYTE,texels);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
  glEnable(GL_TEXTURE_2D);
  glColor3f(1,1,1); glPointSize(6); glPolygonMode(GL_FRONT_AND_BACK,GL_POINT);
  glBegin(GL_QUADS);
  glTexCoord2f(0,0); glVertex2f(-.8f,-.8f);
  glTexCoord2f(1,0); glVertex2f(.8f,-.8f);
  glTexCoord2f(1,1); glVertex2f(.8f,.8f);
  glTexCoord2f(0,1); glVertex2f(-.8f,.8f);
  glEnd(); glFinish();
  GLenum error=glGetError();
  std::vector<unsigned char> pixels(64*64*4);
  glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  int hits[4]={0,0,0,0};
  int centers[4][2]={{32,count>=2?32:6},{58,count>=2?32:6},{58,58},{32,58}};
  for (int c=0;c<4;++c) for(int y=centers[c][1]-2;y<=centers[c][1]+2;++y)
    for(int x=centers[c][0]-2;x<=centers[c][0]+2;++x) {
      int v=(y*64+x)*4;
      if(pixels[v]>127 || pixels[v+1]>127 || pixels[v+2]>127) ++hits[c];
    }
  std::printf("planes=%d error=%u p00=%d p10=%d p11=%d p01=%d\n",count,error,hits[0],hits[1],hits[2],hits[3]);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroyContext(display,context); eglDestroySurface(display,surface); eglTerminate(display);
  return error || hits[0]<16 || hits[1]<16 || hits[2]<16 || hits[3]<16 ? 1 : 0;
}
