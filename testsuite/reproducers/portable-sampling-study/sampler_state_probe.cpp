#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include "egl_context.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

static GLuint shader(GLenum type, const std::string & source) {
  GLuint result=glCreateShader(type); const char * text=source.c_str();
  glShaderSource(result,1,&text,nullptr);glCompileShader(result);
  GLint ok=0;glGetShaderiv(result,GL_COMPILE_STATUS,&ok);
  if(!ok) {char log[8192]={};glGetShaderInfoLog(result,sizeof(log),nullptr,log);throw std::runtime_error(log);}
  return result;
}
int main() {
  EglContext context;if(!context.init())return 77;
  std::cout<<"adapter="<<glGetString(GL_RENDERER)<<" driver="<<glGetString(GL_VERSION)<<'\n';
  GLuint texture;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
  for(int level=0;level<8;++level) {
    int n=128>>level;std::vector<unsigned char> pixels(n*n*4);
    for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
      int at=(y*n+x)*4;int value=n==1?100:(x<n/2?40:160);
      pixels[at]=pixels[at+1]=pixels[at+2]=value;pixels[at+3]=255;
    }
    glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,n,n,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  }
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,7);
  GLuint vertex=shader(GL_VERTEX_SHADER,"#version 450 compatibility\nvoid main(){gl_Position=gl_Vertex;}");
  GLuint fragment=shader(GL_FRAGMENT_SHADER,R"(#version 450 compatibility
    uniform sampler2D image;uniform float level;uniform int center;out vec4 color;
    void main(){vec2 uv=vec2(.5+(gl_FragCoord.x-32.)/65536.,.53125);
      if(center!=0 && level>0.) {vec2 n=vec2(textureSize(image,int(floor(level))));uv=(floor(uv*n)+.5)/n;}
      color=textureLod(image,uv,level);}
  )");
  GLuint program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
  GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 2;
  glUseProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
  glViewport(0,0,64,64);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);
  bool ok=true;
  for(int mode=0;mode<3;++mode)for(float level:{0.f,3.f,3.5f}) {
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,mode==1?GL_NEAREST:GL_LINEAR);
    glUniform1f(glGetUniformLocation(program,"level"),level);
    glUniform1i(glGetUniformLocation(program,"center"),mode==2);
    glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();glFinish();
    std::vector<unsigned char> pixels(64*64*4);glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    int maximum=0,changed=0,first=-1;double mae=0;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
      double u=.5+(x+.5-32.)/65536.;
      double wanted=level>0 ? (x<32?40:160) : 40+120*(u*128-63.5);
      int value=pixels[(y*64+x)*4],error=std::abs(value-int(std::lround(wanted)));
      maximum=std::max(maximum,error);mae+=error;changed+=error>1;
      if(y==0 && first<0 && value>=160)first=x;
    }
    std::cout<<"sampler-state mode="<<mode<<" min=nearest-mip-linear mag="<<(mode==1?"nearest":"linear")
             <<" lod="<<level<<" linear_magnification_or_floor_min_oracle_mae="<<mae/4096
             <<" maximum="<<maximum<<" pixels_over_1="<<changed<<" first_high_column="<<first<<'\n';
    // Mode1 is an intentionally invalid workaround during magnification.
    if(mode==2 || (mode==1 && level>0) || (mode==0 && level==0))ok &= maximum<=1;
    ok &= glGetError()==GL_NO_ERROR;
  }
  return ok?0:1;
}
