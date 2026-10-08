// Isolate nearest texel choice at explicit LOD. Independent of Coin and derivatives.
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include "egl_context.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>
static GLuint shader(GLenum kind,const std::string &s) {
  GLuint o=glCreateShader(kind);const char*p=s.c_str();glShaderSource(o,1,&p,nullptr);glCompileShader(o);
  GLint ok=0;glGetShaderiv(o,GL_COMPILE_STATUS,&ok);
  if(!ok){char b[8192]={};glGetShaderInfoLog(o,sizeof(b),nullptr,b);throw std::runtime_error(b);}return o;
}
static int extent(int base,int level){for(int l=0;l<level;++l)base=std::max(1,base/2);return base;}
static float texel(int n,double u){if(n==1)return 100;int x=int(std::floor((u-std::floor(u))*n));return x<n/2?40:160;}
int main(int argc,char**argv) {
  if(argc!=3&&argc!=4)return 2;const bool derived=argc==4;const int size=std::stoi(argv[1]);const std::string prefix=argv[2];
  EglContext ctx;if(!ctx.init())return 77;
  std::cout<<"adapter="<<glGetString(GL_RENDERER)<<" driver="<<glGetString(GL_VERSION)<<'\n';
  const int maximum=int(std::floor(std::log2(size)));const bool pot=(size&(size-1))==0;
  GLuint t;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);
  for(int l=0;l<=maximum;++l){int n=extent(size,l);std::vector<unsigned char>b(n*n*4);
    for(int y=0;y<n;++y)for(int x=0;x<n;++x){auto p=(y*n+x)*4;unsigned char c=n==1?100:(x<n/2?40:160);b[p]=b[p+1]=b[p+2]=c;b[p+3]=255;}
    glTexImage2D(GL_TEXTURE_2D,l,GL_RGBA8,n,n,0,GL_RGBA,GL_UNSIGNED_BYTE,b.data());
  }
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,maximum);
  GLuint v=shader(GL_VERTEX_SHADER,"#version 450 compatibility\nvoid main(){gl_Position=gl_Vertex;}");
  const char*names[]={"native","base","base_uniform","fine","center","fetch"};
  bool ok=true;unsigned cases=0;
  for(int mode=0;mode<6;++mode){
    std::string fs="#version 450 compatibility\n#define MODE "+std::to_string(mode)+R"(
      uniform sampler2D image;uniform float lod;uniform int derived_control;uniform vec2 base_size;uniform int is_pot;out vec4 result;
      vec2 center(vec2 uv,int level){vec2 n=vec2(textureSize(image,level));return(floor(uv*n)+.5)/n;}
      vec4 two(vec2 uv,int lo,int hi,float w){vec4 a=textureLod(image,center(uv,lo),float(lo)),b=textureLod(image,center(uv,hi),float(hi));return a+(b-a)*w;}
      vec4 fetch(vec2 uv,int l){ivec2 n=textureSize(image,l),p=ivec2(floor(uv*vec2(n)));return texelFetch(image,((p%n)+n)%n,l);}
      void main(){vec2 uv=vec2(.5+(gl_FragCoord.x-32.)/65536.,.53125);
        if(derived_control!=0)uv=vec2(.49999237060546875+(gl_FragCoord.x-31.5)/16.,.53125);
        float lambda=lod;
        if(derived_control!=0){lambda=log2(max(length(dFdxFine(uv)*base_size),length(dFdyFine(uv)*base_size)));if(abs(lambda-8.)>0.000001){result=vec4(1,0,1,1);return;}}
        int lo=int(floor(lambda)),hi=min(lo+1,int(log2(base_size.x)));float w=fract(lambda);
      #if MODE == 0
        result=textureLod(image,uv,lambda);
      #elif MODE == 1
        result=is_pot!=0?textureLod(image,center(uv,0),lambda):two(uv,lo,hi,w);
      #elif MODE == 2
        result=is_pot!=0?textureLod(image,(floor(uv*base_size)+.5)/base_size,lambda):two(uv,lo,hi,w);
      #elif MODE == 3
        result=is_pot!=0?textureLod(image,center(uv,lo),lambda):two(uv,lo,hi,w);
      #elif MODE == 4
        vec4 a=textureLod(image,center(uv,lo),float(lo)),b=textureLod(image,center(uv,hi),float(hi));result=a+(b-a)*w;
      #else
        vec4 a=fetch(uv,lo);
        vec4 b=fetch(uv,hi);result=a+(b-a)*w;
      #endif
      }
    )";
    GLuint f=shader(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
    GLint linked;glGetProgramiv(p,GL_LINK_STATUS,&linked);if(!linked)return 1;
    glUseProgram(p);glUniform1i(glGetUniformLocation(p,"derived_control"),derived);glUniform1i(glGetUniformLocation(p,"is_pot"),pot);glUniform1i(glGetUniformLocation(p,"image"),0);glUniform2f(glGetUniformLocation(p,"base_size"),float(size),float(size));
    bool dumped=false;
    for(float lod : {1.25f,3.5f,5.5f,7.25f,8.0f,8.25f,8.5f,9.25f,10.75f}) {
      if(lod>maximum||(derived&&lod!=8.0f))continue;glUniform1f(glGetUniformLocation(p,"lod"),lod);
      glViewport(0,0,64,64);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);
      glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();glFinish();
      std::vector<unsigned char>b(64*64*4);glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,b.data());
      const int lo=int(std::floor(lod)),hi=std::min(lo+1,maximum);const float w=lod-lo;
      unsigned bad=0;int maximumError=0;double sum=0;
      for(int y=0;y<64;++y)for(int x=0;x<64;++x){const double u=derived?.49999237060546875+(x+.5-31.5)/16.:.5+(x+.5-32)/65536.;
        const float a=texel(extent(size,lo),u),c=texel(extent(size,hi),u);
        const int wanted=int(std::lround(a+(c-a)*w));auto at=(y*64+x)*4;
        int error=std::abs(int(b[at])-wanted);bad+=error>1;maximumError=std::max(maximumError,error);sum+=error;
        ok&=b[at]==b[at+1]&&b[at]==b[at+2]&&b[at+3]==255;
      }
      std::cout<<"case size="<<size<<" pot="<<pot<<" derived="<<derived<<" mode="<<names[mode]<<" lod="<<lod<<" bad_pixels="<<bad<<" mae="<<sum/4096<<" max="<<maximumError<<" pixel31="<<int(b[31*4])<<'\n';
      if((bad&&!dumped)||(size==4096&&lod==8.25f)){std::ofstream out(prefix+"-"+names[mode]+"-"+std::to_string(lod)+".rgba",std::ios::binary);out.write(reinterpret_cast<const char*>(b.data()),b.size());ok&=bool(out);dumped=true;}
      if(mode==4||mode==5||mode==3)ok&=bad==0;
      ok&=glGetError()==GL_NO_ERROR;++cases;
    }
    glDeleteProgram(p);glDeleteShader(f);
  }
  std::cout<<"controls="<<cases<<" correct_paths="<<(ok?"PASS":"FAIL")<<'\n';return ok?0:1;
}
