// Independent 4096-pixel oracle at a nearest-texel boundary. No Coin linkage.
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include "egl_context.h"
#include <vector>
#include <algorithm>
#include <string>
#include <fstream>
#include <stdexcept>
GLuint shader(GLenum type,const std::string &s){GLuint o=glCreateShader(type);const char*p=s.c_str();glShaderSource(o,1,&p,nullptr);glCompileShader(o);GLint ok;glGetShaderiv(o,GL_COMPILE_STATUS,&ok);if(!ok){char log[8192];glGetShaderInfoLog(o,8192,nullptr,log);throw std::runtime_error(log);}return o;}
int main(int argc,char**argv){if(argc!=2)return 2;EglContext ctx;if(!ctx.init())return 77;std::cout<<"adapter="<<glGetString(GL_RENDERER)<<'\n';GLuint t;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);for(int l=0;l<8;++l){int n=128>>l,block=std::max(1,8>>l);std::vector<unsigned char>b(n*n*4);for(int y=0;y<n;++y)for(int x=0;x<n;++x)for(int c=0;c<4;++c)b[(y*n+x)*4+c]=c==3?255:(l>3?100:((x/block+y/block)%2?160:40));glTexImage2D(GL_TEXTURE_2D,l,GL_RGBA8,n,n,0,GL_RGBA,GL_UNSIGNED_BYTE,b.data());}glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,7);
 auto vs=shader(GL_VERTEX_SHADER,"#version 450 compatibility\nvoid main(){gl_Position=gl_Vertex;}");bool ok=true;
 for(int mode=0;mode<3;++mode){std::string fs="#version 450 compatibility\n#define MODE "+std::to_string(mode)+R"(
uniform sampler2D image;out vec4 result;void main(){vec2 uv=vec2(.5+(gl_FragCoord.x-32.)/65536.,.53125);
#if MODE == 0
result=textureLod(image,uv,3.);
#elif MODE == 1
result=textureLod(image,(floor(uv*16.)+.5)/16.,3.);
#else
result=texelFetch(image,ivec2(floor(uv*16.)),3);
#endif
})";GLuint f=shader(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,f);glLinkProgram(p);glUseProgram(p);glUniform1i(glGetUniformLocation(p,"image"),0);glViewport(0,0,64,64);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();glFinish();std::vector<unsigned char>pixels(64*64*4);glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());int differences=0,first=-1;for(int y=0;y<64;++y)for(int x=0;x<64;++x){auto at=(y*64+x)*4;unsigned char wanted=x<32?160:40;for(int c=0;c<3;++c)differences+=pixels[at+c]!=wanted;differences+=pixels[at+3]!=255;if(y==0&&first<0&&pixels[at]==40)first=x;}
 std::ofstream out(std::string(argv[1])+"-"+std::to_string(mode)+".rgba",std::ios::binary);out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());std::cout<<"boundary mode="<<mode<<" pixels=4096 first_column="<<first<<" independent_channel_differences="<<differences<<'\n';if(mode>0)ok&=differences==0;ok&=bool(out)&&glGetError()==GL_NO_ERROR;glDeleteProgram(p);glDeleteShader(f);}
 return ok?0:1;}
