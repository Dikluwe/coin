// Experimental GPU proof for generating clip-created boundaries in a geometry
// shader. It does not implement the full OpenGL polygon-mode semantics.
// Build: c++ RasterClipBoundaryGsProbe.cpp -o gs-probe -lGL -lglut
#define GL_GLEXT_PROTOTYPES
#include <GL/freeglut.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static constexpr int W = 64, H = 64;
static float coord(int p) { return (p + .5f) / 32.0f - 1.0f; }

static GLuint shader(GLenum type, const char *source) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &source, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::fprintf(stderr, "shader %u: %s\n", type, log);
    return 0;
  }
  return s;
}

static GLuint program(bool points, bool shaderClip) {
  const char *vs = shaderClip ?
    "#version 150 compatibility\n"
    "out vec4 colorVS;\n"
    "void main() { gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;"
    " colorVS=gl_Color; gl_ClipDistance[0]=gl_Vertex.x+0.484375; }\n" :
    "#version 150 compatibility\n"
    "out vec4 colorVS;\n"
    "void main() { gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex; colorVS=gl_Color; }\n";
  const char *gsLine =
    "#version 150 compatibility\n"
    "layout(triangles) in; layout(line_strip, max_vertices=2) out;\n"
    "in vec4 colorVS[]; out vec4 colorGS;\n"
    "uniform float cutX;\n"
    "void main() {\n"
    "  int count=0;\n"
    "  for(int i=0;i<3;i++) {\n"
    "    int j=(i+1)%3;\n"
    "    float a=gl_in[i].gl_Position.x-cutX*gl_in[i].gl_Position.w;\n"
    "    float b=gl_in[j].gl_Position.x-cutX*gl_in[j].gl_Position.w;\n"
    "    if ((a<0.0 && b>0.0)||(a>0.0 && b<0.0)) {\n"
    "      float t=a/(a-b);\n"
    "      gl_Position=mix(gl_in[i].gl_Position,gl_in[j].gl_Position,t);\n"
    "      colorGS=mix(colorVS[i],colorVS[j],t);\n"
    "      gl_ClipDistance[0]=0.0;\n"
    "      EmitVertex(); count++;\n"
    "    }\n"
    "  }\n"
    "  if(count==2) EndPrimitive();\n"
    "}\n";
  const char *gsPoint =
    "#version 150 compatibility\n"
    "layout(triangles) in; layout(points, max_vertices=2) out;\n"
    "in vec4 colorVS[]; out vec4 colorGS;\n"
    "uniform float cutX;\n"
    "void main() {\n"
    "  for(int i=0;i<3;i++) {\n"
    "    int j=(i+1)%3;\n"
    "    float a=gl_in[i].gl_Position.x-cutX*gl_in[i].gl_Position.w;\n"
    "    float b=gl_in[j].gl_Position.x-cutX*gl_in[j].gl_Position.w;\n"
    "    if ((a<0.0 && b>0.0)||(a>0.0 && b<0.0)) {\n"
    "      float t=a/(a-b);\n"
    "      gl_Position=mix(gl_in[i].gl_Position,gl_in[j].gl_Position,t);\n"
    "      colorGS=mix(colorVS[i],colorVS[j],t);\n"
    "      gl_ClipDistance[0]=0.0; gl_PointSize=6.0;\n"
    "      EmitVertex(); EndPrimitive();\n"
    "    }\n"
    "  }\n"
    "}\n";
  const char *fs =
    "#version 150 compatibility\n"
    "in vec4 colorGS; void main() { gl_FragColor=colorGS; }\n";
  std::string gsSource = points ? gsPoint : gsLine;
  if (shaderClip) {
    const char *oldDistance[2] = {
      "gl_in[i].gl_Position.x-cutX*gl_in[i].gl_Position.w",
      "gl_in[j].gl_Position.x-cutX*gl_in[j].gl_Position.w"
    };
    const char *newDistance[2] = {
      "gl_in[i].gl_ClipDistance[0]",
      "gl_in[j].gl_ClipDistance[0]"
    };
    for (int k = 0; k < 2; ++k) {
      size_t pos = gsSource.find(oldDistance[k]);
      if (pos == std::string::npos) return 0;
      gsSource.replace(pos, std::strlen(oldDistance[k]), newDistance[k]);
    }
  }
  GLuint v=shader(GL_VERTEX_SHADER,vs), g=shader(GL_GEOMETRY_SHADER,gsSource.c_str()), f=shader(GL_FRAGMENT_SHADER,fs);
  if(!v || !g || !f) return 0;
  GLuint p=glCreateProgram();
  glAttachShader(p,v); glAttachShader(p,g); glAttachShader(p,f); glLinkProgram(p);
  GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
  if(!ok) { char log[8192]; glGetProgramInfoLog(p,sizeof(log),nullptr,log); std::fprintf(stderr,"link: %s\n",log); return 0; }
  glDeleteShader(v); glDeleteShader(g); glDeleteShader(f);
  glUseProgram(p);
  glUniform1f(glGetUniformLocation(p,"cutX"),coord(16));
  return p;
}

static void quad() {
  float l=coord(6), r=coord(45), b=coord(6), t=coord(31);
  glBegin(GL_TRIANGLES);
  glVertex2f(l,b); glVertex2f(r,b); glVertex2f(r,t);
  glVertex2f(l,b); glVertex2f(r,t); glVertex2f(l,t);
  glEnd();
}

static bool run(bool points, bool shaderClip) {
  glClear(GL_COLOR_BUFFER_BIT);
  GLuint p=program(points,shaderClip); if(!p) return false;
  glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
  if(shaderClip) glDisable(GL_CLIP_PLANE0); else glEnable(GL_CLIP_PLANE0);
  quad(); glFinish();
  std::vector<unsigned char> pixels(W*H*3);
  glReadPixels(0,0,W,H,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
  int center=0,lo=0,hi=0;
  for(int y=10;y<=27;y++) center+=pixels[(y*W+16)*3]>127;
  for(int y=5;y<=8;y++) lo+=pixels[(y*W+16)*3]>127;
  for(int y=29;y<=32;y++) hi+=pixels[(y*W+16)*3]>127;
  GLenum err=glGetError();
  std::printf("%s-%s x16-center=%d x16-low=%d x16-high=%d error=%u\n",
              shaderClip?"shader":"fixed",points?"point":"line",center,lo,hi,err);
  glUseProgram(0); glDeleteProgram(p);
  return err==GL_NO_ERROR && (points ? lo==4 && hi==4 && center==6 : center==18);
}

int main(int argc,char **argv) {
  glutInit(&argc,argv); glutInitDisplayMode(GLUT_RGB|GLUT_DOUBLE); glutInitWindowSize(W,H);
  if(glutCreateWindow("Mesa boundary GS probe")<=0) return 2;
  glViewport(0,0,W,H); glDrawBuffer(GL_BACK); glReadBuffer(GL_BACK);
  glDisable(GL_DITHER); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  glClearColor(0,0,0,1); glColor3f(1,1,1);
  glEnable(GL_PROGRAM_POINT_SIZE);
  glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1,1,-1,1,-1,1);
  glMatrixMode(GL_MODELVIEW); glLoadIdentity();
  double plane[]={1,0,0,-coord(16)}; glClipPlane(GL_CLIP_PLANE0,plane);
  std::printf("GL vendor=%s renderer=%s version=%s\n",
              reinterpret_cast<const char *>(glGetString(GL_VENDOR)),
              reinterpret_cast<const char *>(glGetString(GL_RENDERER)),
              reinterpret_cast<const char *>(glGetString(GL_VERSION)));
  bool ok=true;
  ok=run(false,false)&&ok; ok=run(true,false)&&ok;
  ok=run(false,true)&&ok; ok=run(true,true)&&ok;
  return ok?0:1;
}
