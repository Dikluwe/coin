#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using Core=CoinRenderCubeTextureCore;
bool check(bool good,const std::string & label) {
  if(!good) std::cerr << "cube_texture_core FAIL " << label << '\n';
  return good;
}
bool near(float actual,float expected,float tolerance=2e-5f) {
  return std::isfinite(actual) && std::abs(actual-expected)<=tolerance;
}
bool same(const Core::Atlas & a,const Core::Atlas & b) {
  return a.faceSize==b.faceSize && a.sourceComponents==b.sourceComponents &&
    a.width==b.width && a.height==b.height && a.mipmapped==b.mipmapped &&
    a.pixelsRgba==b.pixelsRgba;
}
bool same(const SbVec4f & a,const SbVec4f & b) {
  for(int i=0;i<4;++i) if(a[i]!=b[i]) return false;
  return true;
}
std::array<Core::Face,6> uniformFaces(uint32_t size,uint32_t components) {
  std::array<Core::Face,6> faces;
  for(uint32_t face=0;face<6;++face) {
    faces[face].width=size;faces[face].height=size;
    faces[face].components=components;
    faces[face].pixels.resize(size_t(size)*size*components);
    for(size_t pixel=0;pixel<size_t(size)*size;++pixel)
      for(uint32_t channel=0;channel<components;++channel)
        faces[face].pixels[pixel*components+channel]=uint8_t(15+face*31+channel*9);
  }
  return faces;
}
bool admissionAndAtomicity() {
  bool ok=true;
  Core::Atlas marker;
  marker.faceSize=9;marker.sourceComponents=4;
  marker.width=18;marker.height=3;marker.mipmapped=true;
  marker.pixelsRgba={3,7,11,13};
  auto faces=uniformFaces(2,4);
  Core::Atlas result=marker;
  ok &= check(Core::prepare(faces,false,result) && Core::valid(result),
    "valid 2x2 RGBA faces admitted");
  auto reject=[&](const std::string & label,const std::array<Core::Face,6> & input) {
    Core::Atlas kept=marker;
    return check(!Core::prepare(input,true,kept) && same(kept,marker),
      label+" rejects without changing output");
  };
  auto bad=faces;bad[5].width=4;ok &= reject("mismatched face dimension",bad);
  bad=faces;bad[2].height=1;ok &= reject("non-square face",bad);
  bad=faces;bad[3].components=3;ok &= reject("mismatched components",bad);
  bad=faces;bad[0].pixels.pop_back();ok &= reject("short image bytes",bad);
  bad=faces;bad[0].pixels.push_back(0);ok &= reject("extra image bytes",bad);
  bad=faces;bad[0].components=0;ok &= reject("zero components",bad);
  bad=faces;bad[0].components=5;ok &= reject("five components",bad);
  bad=uniformFaces(3,4);ok &= reject("non-POT face size",bad);
  bad=faces;bad[0].width=0;bad[0].height=0;bad[0].pixels.clear();
  ok &= reject("empty face",bad);
  bad=faces;bad[0].width=4096;bad[0].height=4096;
  ok &= reject("face exceeds maximum without allocating",bad);
  bad=faces;bad[0].width=std::numeric_limits<uint32_t>::max();
  bad[0].height=std::numeric_limits<uint32_t>::max();
  ok &= reject("checked-size overflow without allocating",bad);
  Core::Atlas invalid=result;
  invalid.width=0;ok &= check(!Core::valid(invalid),"invalid atlas width");
  invalid=result;invalid.height+=1;ok &= check(!Core::valid(invalid),"invalid atlas height");
  invalid=result;invalid.pixelsRgba.pop_back();
  ok &= check(!Core::valid(invalid),"truncated atlas bytes");
  invalid=result;invalid.sourceComponents=5;
  ok &= check(!Core::valid(invalid),"invalid source component count");
  invalid=result;invalid.faceSize=3;
  ok &= check(!Core::valid(invalid),"invalid face chain");
  invalid=result;invalid.mipmapped=true;
  ok &= check(!Core::valid(invalid),"mip flag must match physical chain");
  Core::Atlas padded;
  ok &= check(Core::prepare(uniformFaces(4,4),true,padded),"padding fixture admitted");
  if(padded.pixelsRgba.size()>126*4) {
    padded.pixelsRgba[126*4]=1;
    ok &= check(!Core::valid(padded),"nonzero final padding rejected");
  }
  return ok;
}
bool projection() {
  bool ok=true;
  const SbVec3f axes[]={SbVec3f(1,0,0),SbVec3f(-1,0,0),
    SbVec3f(0,1,0),SbVec3f(0,-1,0),SbVec3f(0,0,1),SbVec3f(0,0,-1)};
  const SbVec3f quadrants[]={SbVec3f(1,-.5f,-.5f),SbVec3f(-1,-.5f,.5f),
    SbVec3f(.5f,1,.5f),SbVec3f(.5f,-1,-.5f),
    SbVec3f(.5f,-.5f,1),SbVec3f(-.5f,-.5f,-1)};
  for(uint32_t expected=0;expected<6;++expected) {
    uint32_t face=99;SbVec2f st(-7,-9);
    ok &= check(Core::project(axes[expected],face,st) && face==expected &&
      near(st[0],.5f,1e-6f) && near(st[1],.5f,1e-6f),
      "axis face and center "+std::to_string(expected));
    ok &= check(Core::project(quadrants[expected],face,st) && face==expected &&
      near(st[0],.75f,1e-6f) && near(st[1],.75f,1e-6f),
      "table 3.19 orientation "+std::to_string(expected));
  }
  struct Tie {SbVec3f direction;uint32_t expected;};
  const Tie ties[]={{SbVec3f(1,1,1),0},{SbVec3f(-1,-1,-1),1},
    {SbVec3f(0,1,1),2},{SbVec3f(0,-1,-1),3}};
  for(const auto & tie:ties) {
    uint32_t face=99;SbVec2f st(-7,-9);
    ok &= check(Core::project(tie.direction,face,st) && face==tie.expected,
      "portable major-axis tie priority");
  }
  for(float scale:{std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::denorm_min()}) {
    uint32_t face=99;SbVec2f st(-7,-9);
    ok &= check(Core::project(SbVec3f(scale,0,0),face,st) && face==0 &&
      near(st[0],.5f,1e-6f) && near(st[1],.5f,1e-6f),
      "extreme finite direction without overflow/underflow");
  }
  {
    uint32_t face=99;SbVec2f st(-7,-9);
    const float max=std::numeric_limits<float>::max();
    ok &= check(Core::project(SbVec3f(max,max*.5f,0),face,st) && face==0 &&
      near(st[0],.5f,1e-6f) && near(st[1],.25f,1e-6f),
      "mixed extreme finite direction keeps ratios stable");
  }
  const float nan=std::numeric_limits<float>::quiet_NaN();
  const float inf=std::numeric_limits<float>::infinity();
  for(const SbVec3f bad:{SbVec3f(0,0,0),SbVec3f(nan,1,0),
                       SbVec3f(1,inf,0),SbVec3f(-inf,0,1)}) {
    uint32_t face=77;SbVec2f st(-7,-9);
    ok &= check(!Core::project(bad,face,st) && face==77 &&
      st==SbVec2f(-7,-9),"invalid direction leaves projection outputs unchanged");
  }
  return ok;
}
bool expansionAndLayout() {
  bool ok=true;
  for(uint32_t components=1;components<=4;++components) {
    auto faces=uniformFaces(1,components);
    Core::Atlas atlas;
    ok &= check(Core::prepare(faces,false,atlas) && Core::valid(atlas) &&
      atlas.faceSize==1 && atlas.sourceComponents==components &&
      atlas.width==2 && atlas.height==3 && atlas.pixelsRgba.size()==24,
      "1x1 six-face layout components="+std::to_string(components));
    for(uint32_t face=0;face<6;++face) {
      const uint8_t l=uint8_t(15+face*31);
      const uint8_t second=uint8_t(l+9),third=uint8_t(l+18),fourth=uint8_t(l+27);
      const std::array<uint8_t,4> expected=components==1?
        std::array<uint8_t,4>{{l,l,l,255}}:components==2?
        std::array<uint8_t,4>{{l,l,l,second}}:components==3?
        std::array<uint8_t,4>{{l,second,third,255}}:
        std::array<uint8_t,4>{{l,second,third,fourth}};
      for(int channel=0;channel<4;++channel)
        ok &= check(atlas.pixelsRgba[size_t(face)*4+channel]==expected[size_t(channel)],
          "component expansion face="+std::to_string(face));
    }
  }
  auto faces=uniformFaces(4,4);
  for(uint32_t face=0;face<6;++face) for(uint32_t y=0;y<4;++y)
    for(uint32_t x=0;x<4;++x) for(uint32_t channel=0;channel<4;++channel)
      faces[face].pixels[((size_t(y)*4+x)*4)+channel]=
        uint8_t(5+face*17+x*x*3+y*y*5+channel*7);
  Core::Atlas atlas;
  ok &= check(Core::prepare(faces,true,atlas) && Core::valid(atlas) &&
    atlas.width==8 && atlas.height==16 && atlas.pixelsRgba.size()==512,
    "4x4 full mip chain packed into 8x16 atlas");
  const size_t levelOffsets[]={0,6*16,6*16+6*4};
  for(uint32_t level=0;level<3;++level) {
    const uint32_t size=4u>>level;
    for(uint32_t face=0;face<6;++face) for(uint32_t y=0;y<size;++y)
      for(uint32_t x=0;x<size;++x) for(uint32_t channel=0;channel<4;++channel) {
        uint8_t expected=0;
        if(level==0) expected=faces[face].pixels[((size_t(y)*4+x)*4)+channel];
        else if(level==1) {
          unsigned sum=0;
          for(uint32_t dy=0;dy<2;++dy) for(uint32_t dx=0;dx<2;++dx)
            sum+=faces[face].pixels[((size_t(2*y+dy)*4+2*x+dx)*4)+channel];
          expected=uint8_t((sum+2)/4);
        } else {
          unsigned sum=0;
          for(uint32_t yy=0;yy<2;++yy) for(uint32_t xx=0;xx<2;++xx) {
            unsigned fine=0;
            for(uint32_t dy=0;dy<2;++dy) for(uint32_t dx=0;dx<2;++dx)
              fine+=faces[face].pixels[((size_t(2*yy+dy)*4+2*xx+dx)*4)+channel];
            sum+=(fine+2)/4;
          }
          expected=uint8_t((sum+2)/4);
        }
        const size_t at=(levelOffsets[level]+size_t(face)*size*size+y*size+x)*4+channel;
        ok &= check(atlas.pixelsRgba[at]==expected,
          "face-local mip recurrence level="+std::to_string(level)+
          " face="+std::to_string(face));
      }
  }
  for(size_t at=126*4;at<atlas.pixelsRgba.size();++at)
    ok &= check(atlas.pixelsRgba[at]==0,"atlas padding remains zero");
  return ok;
}
bool sampling() {
  bool ok=true;
  {
    auto singleFaces=uniformFaces(1,4);
    Core::Atlas single;
    ok &= check(Core::prepare(singleFaces,false,single),"six-face sample fixture admitted");
    const SbVec3f axes[]={SbVec3f(1,0,0),SbVec3f(-1,0,0),
      SbVec3f(0,1,0),SbVec3f(0,-1,0),SbVec3f(0,0,1),SbVec3f(0,0,-1)};
    for(uint32_t face=0;face<6;++face) {
      SbVec4f sampled(-7,-8,-9,-10);
      const bool accepted=Core::sample(single,axes[face],0,
        CoinRenderTextureFilter::NEAREST,sampled);
      bool expected=accepted;
      for(uint32_t channel=0;channel<4;++channel)
        expected=expected && near(sampled[int(channel)],
          float(15+face*31+channel*9)/255.f);
      ok &= check(expected,"sampling selected face "+std::to_string(face));
    }
  }
  auto faces=uniformFaces(2,4);
  const uint8_t colors[4][4]={{255,0,0,255},{0,255,0,255},
                              {0,0,255,255},{255,255,255,255}};
  for(int pixel=0;pixel<4;++pixel) for(int channel=0;channel<4;++channel)
    faces[0].pixels[size_t(pixel)*4+channel]=colors[pixel][channel];
  Core::Atlas atlas,base;
  ok &= check(Core::prepare(faces,true,atlas) && Core::prepare(faces,false,base),
    "sampling fixtures admitted");
  const SbVec3f lowerLeft(1,.5f,.5f),upperRight(1,-.5f,-.5f),
    center(1,0,0),nearLowerLeft(1,.25f,.25f);
  SbVec4f value(-7,-8,-9,-10);
  ok &= check(Core::sample(atlas,lowerLeft,3,CoinRenderTextureFilter::NEAREST,value) &&
    near(value[0],1) && near(value[1],0) && near(value[2],0) && near(value[3],1),
    "nearest base texel ignores LOD");
  ok &= check(Core::sample(atlas,upperRight,0,CoinRenderTextureFilter::NEAREST,value) &&
    near(value[0],1) && near(value[1],1) && near(value[2],1),
    "nearest opposite corner");
  for(const auto filter:{CoinRenderTextureFilter::NEAREST,
                         CoinRenderTextureFilter::LINEAR}) {
    const std::string filterName=filter==CoinRenderTextureFilter::NEAREST?
      "nearest":"bilinear";
    ok &= check(Core::sample(atlas,SbVec3f(1,1,1),0,filter,value) &&
      near(value[0],1) && near(value[1],0) && near(value[2],0) &&
      near(value[3],1),
      filterName+" clamps face +X at st(0,0) without wrap or neighbor face");
    ok &= check(Core::sample(atlas,SbVec3f(1,-1,-1),0,filter,value) &&
      near(value[0],1) && near(value[1],1) && near(value[2],1) &&
      near(value[3],1),
      filterName+" clamps face +X at st(1,1) without wrap or neighbor face");
  }
  ok &= check(Core::sample(atlas,center,0,CoinRenderTextureFilter::LINEAR,value) &&
    near(value[0],.5f) && near(value[1],.5f) && near(value[2],.5f),
    "bilinear interpolation within one face");
  ok &= check(Core::sample(atlas,lowerLeft,.5f,
    CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR,value) &&
    near(value[0],.5f*(1.f+128.f/255.f)) &&
    near(value[1],.5f*128.f/255.f) && near(value[2],.5f*128.f/255.f),
    "nearest texel with fractional LOD blend");
  ok &= check(Core::sample(atlas,center,1,
    CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR,value) &&
    near(value[0],128.f/255.f) && near(value[1],128.f/255.f),
    "linear mip final level");
  ok &= check(Core::sample(base,lowerLeft,100,
    CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR,value) &&
    near(value[0],1) && near(value[1],0),
    "mipmap filter without mip chain samples base");
  ok &= check(Core::sample(atlas,lowerLeft,100,
    CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR,value) &&
    near(value[0],128.f/255.f),"LOD clamps to final level");
  ok &= check(Core::sample(atlas,lowerLeft,-100,
    CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR,value) &&
    near(value[0],1),"negative LOD clamps to base");
  ok &= check(Core::sample(atlas,nearLowerLeft,0,
    CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR,value) &&
    near(value[0],.625f) && near(value[1],.25f) && near(value[2],.25f),
    "mip nearest filter magnifies bilinearly at LOD zero");
  ok &= check(Core::sample(atlas,nearLowerLeft,-.1f,
    CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR,value) &&
    near(value[0],.625f) && near(value[1],.25f) && near(value[2],.25f),
    "mip nearest filter magnifies bilinearly below LOD zero");
  ok &= check(Core::sample(atlas,nearLowerLeft,.01f,
    CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR,value) &&
    near(value[0],.99f+.01f*128.f/255.f) &&
    near(value[1],.01f*128.f/255.f) &&
    near(value[2],.01f*128.f/255.f),
    "mip nearest filter minifies with nearest texel above LOD zero");
  ok &= check(Core::sample(base,nearLowerLeft,1,
    CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR,value) &&
    near(value[0],1) && near(value[1],0) && near(value[2],0),
    "without mips positive LOD still uses nearest minification");
  ok &= check(Core::sample(base,nearLowerLeft,0,
    CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR,value) &&
    near(value[0],.625f) && near(value[1],.25f) && near(value[2],.25f),
    "without mips LOD zero still uses bilinear magnification");
  const SbVec4f marker(-7,-8,-9,-10);
  const float nan=std::numeric_limits<float>::quiet_NaN();
  const float inf=std::numeric_limits<float>::infinity();
  Core::Atlas bad=atlas;bad.width=0;
  for(float lod:{nan,inf,-inf}) {
    value=marker;
    ok &= check(!Core::sample(atlas,center,lod,CoinRenderTextureFilter::NEAREST,value) &&
      same(value,marker),"nonfinite LOD leaves sample output unchanged");
  }
  value=marker;
  ok &= check(!Core::sample(atlas,SbVec3f(0,0,0),0,
    CoinRenderTextureFilter::LINEAR,value) && same(value,marker),
    "zero direction leaves sample output unchanged");
  for(const SbVec3f direction:{SbVec3f(nan,1,0),SbVec3f(1,inf,0)}) {
    value=marker;
    ok &= check(!Core::sample(atlas,direction,0,CoinRenderTextureFilter::LINEAR,value) &&
      same(value,marker),"nonfinite direction leaves sample output unchanged");
  }
  value=marker;
  ok &= check(!Core::sample(atlas,center,0,
    static_cast<CoinRenderTextureFilter>(99),value) && same(value,marker),
    "invalid filter enum leaves sample output unchanged");
  value=marker;
  ok &= check(!Core::sample(bad,center,0,CoinRenderTextureFilter::LINEAR,value) &&
    same(value,marker),"malformed atlas leaves sample output unchanged");
  return ok;
}
bool maximum() {
  // Six 2048² RGBA source faces occupy 96 MiB. The mip atlas is exactly
  // 4096x8192 RGBA (128 MiB), including its zero-filled final padding.
  auto faces=uniformFaces(2048,4);
  Core::Atlas atlas;
  bool ok=check(Core::prepare(faces,true,atlas) && Core::valid(atlas) &&
    atlas.faceSize==2048 && atlas.width==4096 && atlas.height==8192 &&
    atlas.pixelsRgba.size()==size_t(128)*1024*1024,
    "maximum legal cube and 128 MiB physical atlas admitted");
  if(ok) {
    SbVec4f value(-7,-8,-9,-10);
    ok &= check(Core::sample(atlas,SbVec3f(1,0,0),100,
      CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR,value) &&
      near(value[0],15.f/255.f),"maximum atlas final mip remains sampleable");
  }
  return ok;
}
}

int main() {
  bool ok=admissionAndAtomicity();
  ok &= projection();
  ok &= expansionAndLayout();
  ok &= sampling();
  ok &= maximum();
  if(ok) std::cout << "CoinRenderCubeTextureCoreTest passed\n";
  return ok?0:1;
}
