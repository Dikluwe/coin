#pragma once
#include <GL/gl.h>
#include <GL/glext.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

struct Probe {
  float quality, offset;
  std::string route, output;
  bool ok;
};
static std::vector<uint8_t> mip(unsigned level) {
  const unsigned side = 128u >> level;
  std::vector<uint8_t> bytes(side * side * 3);
  for (unsigned y=0; y<side; ++y) for (unsigned x=0; x<side; ++x)
    for (unsigned c=0; c<3; ++c)
      bytes[(y*side+x)*3+c] = level >= 4 ? 100 :
        (((x/(8u>>level) + y/(8u>>level)) & 1) ? 160 : 40);
  return bytes;
}
static void inspect(Probe &p) {
  std::cout << "route=" << p.route << " vendor=" << glGetString(GL_VENDOR)
            << " renderer=" << glGetString(GL_RENDERER)
            << " version=" << glGetString(GL_VERSION) << '\n';
  GLint binding=0, minFilter=0, magFilter=0, base=0, maximum=0, wrapS=0, wrapT=0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
  glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&minFilter);
  glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&magFilter);
  glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&base);
  glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,&maximum);
  glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&wrapS);
  glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&wrapT);
  GLfloat anisotropy=0, bias=0, matrix[16];
  glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,&anisotropy);
  glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_LOD_BIAS,&bias);
  glGetFloatv(GL_TEXTURE_MATRIX,matrix);
  std::cout << "sampler binding=" << binding << " min=" << minFilter << " mag=" << magFilter
            << " base=" << base << " max=" << maximum << " wrap=" << wrapS << ',' << wrapT
            << " anisotropy=" << anisotropy << " bias=" << bias << " matrix=";
  for (float v : matrix) std::cout << v << ',';
  std::cout << '\n';
  const GLint expectedMin = p.quality < .8f ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR;
  p.ok &= binding != 0 && minFilter == expectedMin && magFilter == GL_LINEAR &&
          base == 0 && wrapS == GL_REPEAT && wrapT == GL_REPEAT && anisotropy == 1 && bias == 0;
  GLint pack=4; glGetIntegerv(GL_PACK_ALIGNMENT,&pack); glPixelStorei(GL_PACK_ALIGNMENT,1);
  for (unsigned l=0; l<8; ++l) {
    GLint w=0,h=0,format=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,l,GL_TEXTURE_WIDTH,&w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,l,GL_TEXTURE_HEIGHT,&h);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,l,GL_TEXTURE_INTERNAL_FORMAT,&format);
    auto expected=mip(l); std::vector<uint8_t> bytes(std::max(0,w*h*3));
    if (!bytes.empty()) glGetTexImage(GL_TEXTURE_2D,l,GL_RGB,GL_UNSIGNED_BYTE,bytes.data());
    size_t differences=0;
    if (bytes.size()==expected.size()) {
      for (size_t i=0;i<bytes.size();++i) differences += bytes[i]!=expected[i];
    } else differences=expected.size();
    p.ok &= w==int(128u>>l) && h==w && differences==0;
    std::cout << "mip=" << l << " width=" << w << " height=" << h << " format=" << format
              << " bytes=" << bytes.size() << " independent_differences=" << differences << '\n';
  }
  glPixelStorei(GL_PACK_ALIGNMENT,pack);
}
static GLuint shader(GLenum kind, const std::string &text, Probe &p) {
  GLuint s=glCreateShader(kind); const char *source=text.c_str();
  glShaderSource(s,1,&source,nullptr); glCompileShader(s);
  GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
  if (!ok) { char log[4096]={}; glGetShaderInfoLog(s,sizeof(log),nullptr,log);
    std::cerr << p.route << " shader: " << log << '\n'; p.ok=false; }
  return s;
}
static GLuint program(Probe &p) {
  const std::string vertex=R"GLSL(#version 450 compatibility
out vec3 uvq;
void main() {
  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
  uvq = vec3(gl_MultiTexCoord0.xy, gl_MultiTexCoord0.w);
}
)GLSL";
  const std::string fragment=R"GLSL(#version 450 compatibility
in vec3 uvq;
uniform sampler2D image;
uniform int mode, linear_mode;
out vec4 result;
vec3 fetch(int level, ivec2 at) {
  ivec2 n = textureSize(image, level);
  return texelFetch(image, ((at % n)+n)%n, level).rgb;
}
vec3 levelSample(int level, vec2 uv) {
  vec2 at = uv * vec2(textureSize(image, level));
  if (linear_mode == 0) {
    if (mode == 4) at = floor(at*256.0+.5)/256.0;
    return fetch(level, ivec2(floor(at)));
  }
  at -= .5;
  ivec2 lo = ivec2(floor(at)); vec2 f = fract(at);
  return mix(mix(fetch(level,lo),fetch(level,lo+ivec2(1,0)),f.x),
             mix(fetch(level,lo+ivec2(0,1)),fetch(level,lo+ivec2(1,1)),f.x),f.y);
}
void main() {
  vec2 uv = uvq.xy / uvq.z;
  vec2 gx = dFdxFine(uv)*vec2(textureSize(image,0));
  vec2 gy = dFdyFine(uv)*vec2(textureSize(image,0));
  float lod = clamp(log2(max(length(gx),length(gy))),0.,7.);
  if (mode >= 6) {
    // Exact binary-fraction coordinates and fixed mip remove interpolation
    // and implicit LOD. Each pixel advances 1/4096 of a mip-3 texel.
    vec2 at = vec2(.5 + (gl_FragCoord.x-32.)/65536., .53125);
    vec3 color = mode == 6 ? textureLod(image,at,3.).rgb
                          : texelFetch(image,ivec2(floor(at*16.)),3).rgb;
    result = vec4(color,1.);
  } else if (mode == 5) {
    float code = floor(clamp(textureQueryLod(image,uv).y,0.,15.999)*4096.+.5);
    result = vec4(floor(code/256.)/255.,mod(code,256.)/255.,0.,1.);
  } else if (mode == 1) result = vec4(texture(image,uv).rgb,.8);
  else if (mode == 2) result = vec4(textureLod(image,uv,lod).rgb,.8);
  else {
    int lo=int(floor(lod)), hi=min(7,lo+1);
    result = vec4(mix(levelSample(lo,uv),levelSample(hi,uv),fract(lod)),.8);
  }
}
)GLSL";
  GLuint vs=shader(GL_VERTEX_SHADER,vertex,p), fs=shader(GL_FRAGMENT_SHADER,fragment,p);
  GLuint prog=glCreateProgram(); glAttachShader(prog,vs); glAttachShader(prog,fs); glLinkProgram(prog);
  GLint ok=0; glGetProgramiv(prog,GL_LINK_STATUS,&ok);
  if (!ok) { char log[4096]={}; glGetProgramInfoLog(prog,sizeof(log),nullptr,log);
    std::cerr << p.route << " link: " << log << '\n'; p.ok=false; }
  glDeleteShader(vs); glDeleteShader(fs); return prog;
}
static void drawRaw(Probe &p) {
  GLint oldProgram=0, oldActive=0; glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
  glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
  glPushAttrib(GL_ALL_ATTRIB_BITS); glActiveTexture(GL_TEXTURE0);
  glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-1,1,-1,1,-1,1);
  glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
  glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
  glDisable(GL_LIGHTING); glDisable(GL_FOG); glDisable(GL_CULL_FACE); glDisable(GL_ALPHA_TEST);
  glDisable(GL_STENCIL_TEST); glDisable(GL_SCISSOR_TEST);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
  glViewport(0,0,64,64);
  GLuint texture=0; glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  for (unsigned l=0;l<8;++l) { auto bytes=mip(l); int n=128u>>l;
    glTexImage2D(GL_TEXTURE_2D,l,GL_RGB8,n,n,0,GL_RGB,GL_UNSIGNED_BYTE,bytes.data()); }
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,p.quality<.8f?GL_NEAREST_MIPMAP_LINEAR:GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,7);
  glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,1);
  glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_LOD_BIAS,0);
  inspect(p);
  int mode=0;
  if(p.route=="implicit")mode=1;
  if(p.route=="explicit-lod")mode=2;
  if(p.route=="fetch")mode=3;
  if(p.route=="fetch-round256")mode=4;
  if(p.route=="query-lod")mode=5;
  if(p.route=="boundary-scan")mode=6;
  if(p.route=="boundary-fetch")mode=7;
  GLuint prog=0;
  if (mode) {
    prog=program(p); glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog,"image"),0);
    glUniform1i(glGetUniformLocation(prog,"mode"),mode);
    glUniform1i(glGetUniformLocation(prog,"linear_mode"),p.quality>=.8f);
    if (mode>=5) glDisable(GL_BLEND);
  } else { glUseProgram(0); glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE); }
  const float xy[][2]={{-.25f,-.25f},{.25f,-.25f},{.25f,.25f},{-.25f,.25f}};
  const float st[][2]={{.03f,.02f},{.97f,.02f},{.97f,.98f},{.03f,.98f}};
  const unsigned indices[]={0,1,2,0,2,3};
  glColor4f(1,1,1,.8f); glBegin(GL_TRIANGLES);
  for (unsigned i : indices) {
    glTexCoord4f(st[i][0]+p.offset,st[i][1]+p.offset,0,1+.3f*st[i][0]);
    const float scale=mode>=6 ? 4.f : 1.f;
    glVertex3f(xy[i][0]*scale,xy[i][1]*scale,0);
  }
  glEnd(); glFinish();
  GLenum error=glGetError(); if(error!=GL_NO_ERROR) { p.ok=false;std::cerr<<p.route<<" GL error="<<error<<'\n'; }
  glUseProgram(oldProgram); if(prog)glDeleteProgram(prog); glDeleteTextures(1,&texture);
  glMatrixMode(GL_TEXTURE); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
  glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
  glPopAttrib(); glActiveTexture(oldActive);
}
