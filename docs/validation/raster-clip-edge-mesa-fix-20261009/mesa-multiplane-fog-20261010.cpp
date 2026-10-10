#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
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
  const GLfloat fog_color[4]={0,1,0,1};
  glFogfv(GL_FOG_COLOR,fog_color);
  glFogi(GL_FOG_MODE,GL_LINEAR);
  glFogf(GL_FOG_START,0);
  glFogf(GL_FOG_END,1);
  glFogi(GL_FOG_COORDINATE_SOURCE,GL_FOG_COORDINATE);
  glEnable(GL_FOG);
  glColor3f(1,0,0); glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
  glBegin(GL_QUADS);
  glFogCoordf(0); glVertex2f(-.8f,-.8f);
  glFogCoordf(1); glVertex2f(.8f,-.8f);
  glFogCoordf(1); glVertex2f(.8f,.8f);
  glFogCoordf(0); glVertex2f(-.8f,.8f);
  glEnd(); glFinish();
  GLenum error=glGetError();
  std::vector<unsigned char> pixels(64*64*4);
  glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  auto sample=[&](int x,int y){const int p=(y*64+x)*4; std::printf("pixel %d,%d=%u,%u,%u,%u\n",x,y,pixels[p],pixels[p+1],pixels[p+2],pixels[p+3]);};
  sample(32,40); sample(40,32);
  for(int x=30;x<=34;++x){int n=0,r=0,g=0;for(int y=36;y<=52;++y){const int k=(y*64+x)*4;if(pixels[k]||pixels[k+1]){++n;r+=pixels[k];g+=pixels[k+1];}}std::printf("column %d n=%d r=%d g=%d\n",x,n,r,g);}
  for(int y=30;y<=34;++y){int n=0,r=0,g=0;for(int x=36;x<=52;++x){const int k=(y*64+x)*4;if(pixels[k]||pixels[k+1]){++n;r+=pixels[k];g+=pixels[k+1];}}std::printf("row %d n=%d r=%d g=%d\n",y,n,r,g);}
  std::printf("planes=%d error=%u\n",count,error);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroyContext(display,context); eglDestroySurface(display,surface); eglTerminate(display);
  int vn=0,hn=0,vr=0,vg=0,hr=0,hg=0;
  for(int y=36;y<=52;++y) for(int x=30;x<=34;++x) {const int k=(y*64+x)*4;if(pixels[k]||pixels[k+1]){++vn;vr+=pixels[k];vg+=pixels[k+1];}}
  for(int y=30;y<=34;++y) for(int x=36;x<=52;++x) {const int k=(y*64+x)*4;if(pixels[k]||pixels[k+1]){++hn;hr+=pixels[k];hg+=pixels[k+1];}}
  std::printf("fog totals vertical=%d,%d,%d horizontal=%d,%d,%d\n",vn,vr,vg,hn,hr,hg);
  return error || vn != 17 || hn != 17 || vr != 2176 || vg != 2176 || hr != 1105 || hg != 3230 ? 1 : 0;
}
