#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <cstdio>
#include <cstring>
#include <vector>

static GLuint make_shader(GLenum stage, const char *source) {
  GLuint s = glCreateShader(stage);
  glShaderSource(s, 1, &source, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096]; GLsizei len = 0;
    glGetShaderInfoLog(s, sizeof(log), &len, log);
    std::fprintf(stderr, "shader %x failed: %.*s\n", stage, int(len), log);
  }
  return ok ? s : 0;
}

int main(int argc, char **argv) {
  const bool use_gs = argc > 1 && std::strcmp(argv[1], "gs") == 0;
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major=0, minor=0;
  if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) return 77;
  eglBindAPI(EGL_OPENGL_API);
  const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
                        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
  EGLConfig config; EGLint count=0;
  if (!eglChooseConfig(display,attrs,&config,1,&count) || !count) return 77;
  const EGLint pbuf[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};
  EGLSurface surface=eglCreatePbufferSurface(display,config,pbuf);
  EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,nullptr);
  if (surface==EGL_NO_SURFACE || context==EGL_NO_CONTEXT ||
      !eglMakeCurrent(display,surface,surface,context)) return 77;
  std::fprintf(stderr,"renderer=%s GL=%s gs=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),use_gs);

  const char *vs_text = R"(#version 330 compatibility
    void main() {
      gl_Position = gl_Vertex;
      gl_ClipDistance[0] = gl_Vertex.x;
    })";
  const char *gs_text = R"(#version 330 compatibility
    layout(triangles) in;
    layout(triangle_strip, max_vertices=3) out;
    void main() {
      for (int i=0; i<3; i++) {
        gl_Position = gl_in[i].gl_Position;
        gl_ClipDistance[0] = gl_in[i].gl_ClipDistance[0];
        EmitVertex();
      }
      EndPrimitive();
    })";
  const char *fs_text = R"(#version 330 compatibility
    void main() { gl_FragColor = vec4(1,1,1,1); })";
  GLuint vs=make_shader(GL_VERTEX_SHADER,vs_text);
  GLuint gs=use_gs?make_shader(GL_GEOMETRY_SHADER,gs_text):0;
  GLuint fs=make_shader(GL_FRAGMENT_SHADER,fs_text);
  if (!vs || !fs || (use_gs && !gs)) return 3;
  GLuint prog=glCreateProgram();
  glAttachShader(prog,vs); if (gs) glAttachShader(prog,gs); glAttachShader(prog,fs);
  glLinkProgram(prog);
  GLint linked=0; glGetProgramiv(prog,GL_LINK_STATUS,&linked);
  if (!linked) {
    char log[4096]; GLsizei len=0;
    glGetProgramInfoLog(prog,sizeof(log),&len,log);
    std::fprintf(stderr,"link failed: %.*s\n",int(len),log);
    return 4;
  }
  glUseProgram(prog);
  glViewport(0,0,64,64);
  glEnable(GL_CLIP_DISTANCE0);
  glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
  glPolygonMode(GL_FRONT_AND_BACK,GL_POINT);
  glBegin(GL_TRIANGLES);
  glVertex2f(-.8f,-.8f);
  glVertex2f(.8f,-.8f);
  glVertex2f(0,.8f);
  glVertex2f(-.8f,-.8f);
  glVertex2f(.8f,-.8f);
  glVertex2f(0,.8f);
  glEnd(); glFinish();
  GLenum error=glGetError();
  std::vector<unsigned char> pixels(64*64*4);
  glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  for (int x=29; x<=34; ++x) {
    int lit=0;
    for (int y=15; y<=47; ++y) {
      const int k=(y*64+x)*4;
      if (pixels[k]>127) ++lit;
    }
    std::printf("gs=%d x=%d lit=%d\n",use_gs,x,lit);
  }
  int left=0, right=0;
  for (int y=5; y<=8; ++y) {
    for (int x=8; x<=55; ++x) {
      const int k=(y*64+x)*4;
      if (pixels[k]>127) {
        if (x<30) ++left;
        if (x>33) ++right;
      }
    }
  }
  std::printf("retained-edge left=%d right=%d\n",left,right);
  for (int y=0; y<64; ++y) for (int x=0; x<64; ++x) {
    const int k=(y*64+x)*4; if (pixels[k]>127) std::printf("pixel=%d,%d\n",x,y);
  }
  std::printf("error=%u\n",error);
  return error ? 5 : 0;
}
