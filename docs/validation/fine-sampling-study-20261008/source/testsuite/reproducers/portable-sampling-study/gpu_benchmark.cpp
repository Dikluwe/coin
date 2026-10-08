// Pure EGL/OpenGL GPU timing control: no Coin linkage, no readback in timed batches.
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include "egl_context.h"
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <stdexcept>
GLuint compile(GLenum type, const std::string &s) {
 GLuint o=glCreateShader(type);const char *p=s.c_str();glShaderSource(o,1,&p,nullptr);glCompileShader(o);GLint ok=0;glGetShaderiv(o,GL_COMPILE_STATUS,&ok);if(!ok){char log[8192];glGetShaderInfoLog(o,8192,nullptr,log);throw std::runtime_error(log);}return o;
}
int main(int argc,char **argv){
 if(argc!=4)return 2;int mode=std::stoi(argv[1]),linear=std::stoi(argv[2]),units=std::stoi(argv[3]);
 EglContext ctx;if(!ctx.init())return 77;
 std::cout<<"adapter="<<glGetString(GL_RENDERER)<<" driver="<<glGetString(GL_VERSION)<<'\n';
 GLuint fbo,target;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1280,720,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);
 if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 1;
 GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);std::vector<unsigned char> bytes(1024*1024*4);
 for(int y=0;y<1024;++y)for(int x=0;x<1024;++x)for(int c=0;c<4;++c)bytes[(y*1024+x)*4+c]=c==3?255:((x/8+y/8)%2?160:40);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1024,1024,0,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());glGenerateMipmap(GL_TEXTURE_2D);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linear?GL_LINEAR_MIPMAP_LINEAR:GL_NEAREST_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
 for(int i=0;i<units;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,tex);}
 std::string vs="#version 450 compatibility\nout vec2 st;void main(){gl_Position=gl_Vertex;st=(gl_Vertex.xy*.5+.5)*64.;}";
 std::string fs="#version 450 compatibility\n#define MODE "+std::to_string(mode)+"\n#define LINEAR "+std::to_string(linear)+R"(
 in vec2 st;out vec4 result;uniform sampler2D images[8];uniform vec2 suppliedSize;
 vec4 fetch(sampler2D t,ivec2 p,int l){ivec2 n=textureSize(t,l);
#if MODE == 4
p=ivec2(p.x<0?p.x+n.x:p.x>=n.x?p.x-n.x:p.x,p.y<0?p.y+n.y:p.y>=n.y?p.y-n.y:p.y);
#else
p=((p%n)+n)%n;
#endif
return texelFetch(t,p,l);}
 vec4 levelSample(sampler2D t,vec2 uv,int l){
#if MODE == 4
uv=fract(uv);
#endif
vec2 at=uv*vec2(textureSize(t,l));
 #if !LINEAR
 return fetch(t,ivec2(floor(at)),l);
 #else
 ivec2 p=ivec2(floor(at-.5));vec2 f=fract(at-.5);return mix(mix(fetch(t,p,l),fetch(t,p+ivec2(1,0),l),f.x),mix(fetch(t,p+ivec2(0,1),l),fetch(t,p+ivec2(1,1),l),f.x),f.y);
 #endif
 }
 vec4 sampleImage(sampler2D t,vec2 uv){
 #if MODE == 0
 return texture(t,uv);
 #else
 #if MODE == 6
 vec2 n=suppliedSize;
 #else
 vec2 n=vec2(textureSize(t,0));
 #endif
 float lod=clamp(log2(max(length(dFdxFine(uv)*n),length(dFdyFine(uv)*n))),0.,10.);
 #if MODE == 1
 return textureLod(t,uv,lod);
 #else
 int lo=int(floor(lod));
#if MODE == 3 || MODE == 5 || MODE == 6
#if LINEAR
return texture(t,uv);
#else
if(lod<=0.)return textureLod(t,uv,0.);
#if MODE == 6
vec2 a=vec2(max(ivec2(n)>>lo,ivec2(1)));
#else
vec2 a=vec2(textureSize(t,lo));
#endif
#if MODE == 5 || MODE == 6
return textureLod(t,(floor(fract(uv)*a)+.5)/a,lod);
#else
int hi=min(lo+1,10);vec2 b=vec2(textureSize(t,hi));
return mix(textureLod(t,(floor(fract(uv)*a)+.5)/a,float(lo)),textureLod(t,(floor(fract(uv)*b)+.5)/b,float(hi)),fract(lod));
#endif
#endif
#else
return mix(levelSample(t,uv,lo),levelSample(t,uv,min(lo+1,10)),fract(lod));
#endif
 #endif
 #endif
 }
 void main(){vec2 uv=st/(1.+.004*st.x);vec4 c=vec4(1.);
 )";
 for(int i=0;i<units;++i)fs+="c *= sampleImage(images["+std::to_string(i)+"],uv+vec2("+std::to_string(i)+".0/128.));\n";
 fs+="result=c;}";GLuint v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs),prog=glCreateProgram();glAttachShader(prog,v);glAttachShader(prog,f);glLinkProgram(prog);GLint ok;glGetProgramiv(prog,GL_LINK_STATUS,&ok);if(!ok)return 1;glUseProgram(prog);
 for(int i=0;i<units;++i)glUniform1i(glGetUniformLocation(prog,("images["+std::to_string(i)+"]").c_str()),i);
 glUniform2f(glGetUniformLocation(prog,"suppliedSize"),1024,1024);
 glViewport(0,0,1280,720);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);
 auto draw=[](){glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();};
 for(int i=0;i<30;++i)draw();glFinish();GLuint query;glGenQueries(1,&query);
 for(int r=0;r<5;++r){glBeginQuery(GL_TIME_ELAPSED,query);for(int i=0;i<60;++i)draw();glEndQuery(GL_TIME_ELAPSED);GLuint64 ns;glGetQueryObjectui64v(query,GL_QUERY_RESULT,&ns);std::cout<<"gpu_sample mode="<<mode<<" linear="<<linear<<" units="<<units<<" group="<<r<<" frames=60 gpu_ms="<<double(ns)/60000000.<<'\n';}
 return glGetError()==GL_NO_ERROR?0:1;
}
