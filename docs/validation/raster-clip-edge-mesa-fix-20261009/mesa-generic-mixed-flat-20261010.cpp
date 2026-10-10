#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char **argv) {
  const int count = 1;
  const bool points = argc > 1 && std::atoi(argv[1]) == 1;
  const bool flat = argc > 2 && std::atoi(argv[2]) == 1;
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
  glDisable(GL_TEXTURE_2D);
  const char *vs = "#version 130\nsmooth out vec4 extraColor;\nflat out vec4 linearColor;\nvoid main() { float w = 1.0 + 0.5 * gl_Vertex.x; gl_Position = vec4(gl_Vertex.xyz * w, w); gl_ClipDistance[0] = gl_Vertex.x * w; gl_ClipDistance[1] = gl_Vertex.y; extraColor = gl_Color; linearColor = gl_Color; }";
  const char *fs = "#version 130\nsmooth in vec4 extraColor;\nflat in vec4 linearColor;\nvoid main() { gl_FragColor = vec4(extraColor.r, linearColor.g, extraColor.b, 1.0); }";
  GLuint v=glCreateShader(GL_VERTEX_SHADER), f=glCreateShader(GL_FRAGMENT_SHADER), p=glCreateProgram();
  glShaderSource(v,1,&vs,nullptr); glShaderSource(f,1,&fs,nullptr);
  glCompileShader(v); glCompileShader(f); glAttachShader(p,v); glAttachShader(p,f); glLinkProgram(p);
  GLint vl=0,fl=0,pl=0; glGetShaderiv(v,GL_COMPILE_STATUS,&vl); glGetShaderiv(f,GL_COMPILE_STATUS,&fl); glGetProgramiv(p,GL_LINK_STATUS,&pl);
  std::fprintf(stderr,"shader=%d,%d,%d\n",vl,fl,pl); glUseProgram(p);
  glShadeModel(flat ? GL_FLAT : GL_SMOOTH); glPointSize(6); glLineWidth(1);
  glPolygonMode(GL_FRONT_AND_BACK,points ? GL_POINT : GL_LINE);
  glBegin(GL_QUADS);
  glColor3f(1,0,0); glVertex2f(-.8f,-.8f);
  glColor3f(0,1,0); glVertex2f(.8f,-.8f);
  glColor3f(0,0,1); glVertex2f(.8f,.8f);
  glColor3f(1,1,0); glVertex2f(-.8f,.8f);
  glEnd(); glFinish();
  GLenum error=glGetError();
  std::vector<unsigned char> pixels(64*64*4);
  glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  const int samples[6][2]={{32,6},{58,6},{58,58},{32,58},{32,45},{45,32}};
  for (int i=0;i<6;++i) {
    int x=samples[i][0],y=samples[i][1],v=(y*64+x)*4;
    std::printf("mode=%s shade=%s sample=%d,%d rgb=%u,%u,%u\n",
      points?"point":"line",flat?"flat":"smooth",x,y,
      pixels[v],pixels[v+1],pixels[v+2]);
  }
  std::printf("error=%u\n",error);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroyContext(display,context); eglDestroySurface(display,surface); eglTerminate(display);
  return error ? 1 : 0;
}
