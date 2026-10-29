#include <Inventor/environment/CoinEnvironmentRadiance.h>
#include <Inventor/environment/CoinEnvironment.h>
#include "CoinEnvironmentExrP.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace {
const float pi=3.14159265358979323846f;
SbVec3f pixel(const SbByteBuffer & bytes,uint32_t width,uint32_t x,uint32_t y) {
  float rgba[4];std::memcpy(rgba,bytes.constData()+(uint64_t(y)*width+x)*16,16);
  return SbVec3f(rgba[0],rgba[1],rgba[2]);
}
void cubeCoordinates(const SbVec3f & n,uint32_t & face,float & u,float & v) {
  const float x=std::abs(n[0]),y=std::abs(n[1]),z=std::abs(n[2]);
  if(x>=y&&x>=z) {
    face=n[0]>=0?0:1;u=(face==0?-n[2]:n[2])/x;v=-n[1]/x;
  } else if(y>=z) {
    face=n[1]>=0?2:3;u=n[0]/y;v=(face==2?n[2]:-n[2])/y;
  } else {
    face=n[2]>=0?4:5;u=(face==4?n[0]:-n[0])/z;v=-n[1]/z;
  }
}
SbVec3f cubeTap(const CoinEnvironmentRadiance & r,uint32_t face,int x,int y) {
  const int size=r.dimensions[0];
  if(x>=0&&y>=0&&x<size&&y<size)return pixel(r.pixels[face],size,x,y);
  if((x<0||x>=size)&&(y<0||y>=size)) {
    // A cube corner belongs to three faces. Averaging their corner texels
    // makes the four bilinear taps give one third to each adjacent face.
    const auto corner=CoinEnvironment::direction(face,x<0?-1.f:1.f,y<0?-1.f:1.f);
    SbVec3f sum(0,0,0);
    for(unsigned axis=0;axis<3;++axis) {
      const unsigned adjacent=axis*2+(corner[axis]<0?1:0);float u,v;
      switch(adjacent) {
      case 0:u=-corner[2]/corner[0];v=-corner[1]/corner[0];break;
      case 1:u=corner[2]/(-corner[0]);v=-corner[1]/(-corner[0]);break;
      case 2:u=corner[0]/corner[1];v=corner[2]/corner[1];break;
      case 3:u=corner[0]/(-corner[1]);v=-corner[2]/(-corner[1]);break;
      case 4:u=corner[0]/corner[2];v=-corner[1]/corner[2];break;
      default:u=-corner[0]/(-corner[2]);v=-corner[1]/(-corner[2]);break;
      }
      sum+=pixel(r.pixels[adjacent],size,u<0?0:size-1,v<0?0:size-1);
    }
    return sum/3;
  }
  float u,v;uint32_t neighbor;
  cubeCoordinates(CoinEnvironment::direction(face,2*(x+.5f)/size-1,2*(y+.5f)/size-1),neighbor,u,v);
  const int nx=std::max(0,std::min(size-1,int((u+1)*.5f*size)));
  const int ny=std::max(0,std::min(size-1,int((v+1)*.5f*size)));
  return pixel(r.pixels[neighbor],size,nx,ny);
}
bool readBytes(FILE * f,uint8_t * p,size_t count) {return std::fread(p,1,count,f)==count;}
bool hdr(const SbString & name,SbVec2i32 & dimensions,SbByteBuffer & pixels,
         float scale,uint64_t memoryBudget,SbString & error) {
  if(!std::isfinite(scale)||scale<0) {error="HDR import scale must be finite and nonnegative";return false;}
  FILE * f=std::fopen(name.getString(),"rb");
  if(!f) {error="Could not open HDR radiance image";return false;}
  struct Close {FILE * f;~Close(){std::fclose(f);}} close{f};
  char line[4096];
  if(!std::fgets(line,sizeof(line),f)||std::strncmp(line,"#?",2)) {error="Invalid Radiance HDR signature";return false;}
  bool format=false,finished=false;double exposure=1,correction[3]={1,1,1};
  unsigned headerBytes=unsigned(std::strlen(line));
  for(unsigned count=0;count<256;++count) {
    if(!std::fgets(line,sizeof(line),f))break;
    const size_t length=std::strlen(line);headerBytes+=unsigned(length);
    if(!length||line[length-1]!='\n'||headerBytes>65536) {error="HDR header exceeds its bounded profile";return false;}
    if(!std::strcmp(line,"\n")||!std::strcmp(line,"\r\n")) {finished=true;break;}
    std::string text(line);while(!text.empty()&&(text.back()=='\n'||text.back()=='\r'))text.pop_back();
    if(text.compare(0,7,"FORMAT=")==0) {
      if(text.substr(7)!="32-bit_rle_rgbe") {error="HDR profile supports RGBE, not XYZE or another format";return false;}
      format=true;
    } else if(text.compare(0,9,"EXPOSURE=")==0) {
      double factor;std::istringstream in(text.substr(9));
      if(!(in>>factor)||!std::isfinite(factor)||factor<=0) {error="Invalid HDR exposure metadata";return false;}
      exposure*=factor;
    } else if(text.compare(0,10,"COLORCORR=")==0) {
      double values[3];std::istringstream in(text.substr(10));
      if(!(in>>values[0]>>values[1]>>values[2])) {error="Invalid HDR color correction metadata";return false;}
      for(int c=0;c<3;++c) {
        if(!std::isfinite(values[c])||values[c]<=0) {error="Invalid HDR color correction factor";return false;}
        correction[c]*=values[c];
      }
    } else if(text.compare(0,10,"PRIMARIES=")==0) {
      const double accepted[]={.64,.33,.30,.60,.15,.06,.3127,.3290};
      std::istringstream in(text.substr(10));
      for(double reference:accepted) {
        double value;
        if(!(in>>value)||!std::isfinite(value)||std::abs(value-reference)>.0001) {
          error="HDR primaries require an explicit conversion to linear sRGB";return false;
        }
      }
    }
  }
  if(!format||!finished||!std::isfinite(exposure)||exposure<=0) {error="Missing or invalid HDR format/header";return false;}
  for(double value:correction)if(!std::isfinite(value)||value<=0) {error="Invalid accumulated HDR color correction";return false;}
  if(!std::fgets(line,sizeof(line),f)) {error="Missing HDR resolution";return false;}
  std::string axis1,axis2,trailing;uint32_t count1,count2;std::istringstream resolution(line);
  if(!(resolution>>axis1>>count1>>axis2>>count2)||(resolution>>trailing)||
     axis1.size()!=2||axis2.size()!=2||(axis1[0]!='+'&&axis1[0]!='-')||
     (axis2[0]!='+'&&axis2[0]!='-')||(axis1[1]!='X'&&axis1[1]!='Y')||
     (axis2[1]!='X'&&axis2[1]!='Y')||axis1[1]==axis2[1]||
     !count1||!count2||count1>32768||count2>32768) {error="Invalid HDR dimensions or orientation";return false;}
  const uint32_t width=axis1[1]=='X'?count1:count2,height=axis1[1]=='Y'?count1:count2;
  const uint64_t bytes=uint64_t(width)*height*16,scanlineBytes=uint64_t(count2)*4;
  if(bytes>memoryBudget||scanlineBytes>memoryBudget-bytes) {error="HDR decoded pixels exceed the working budget";return false;}
  SbByteBuffer output(static_cast<size_t>(bytes));
  std::vector<uint8_t> row(static_cast<size_t>(scanlineBytes));
  uint8_t previous[4]={};bool havePrevious=false;
  for(uint32_t a=0;a<count1;++a) {
    uint8_t first[4];if(!readBytes(f,first,4)) {error="Truncated HDR pixels";return false;}
    if(count2>=8&&count2<=32767&&first[0]==2&&first[1]==2&&!(first[2]&128)) {
      if((uint32_t(first[2])<<8|first[3])!=count2) {error="HDR RLE scanline width mismatch";return false;}
      for(int channel=0;channel<4;++channel) {
        uint32_t at=0;
        while(at<count2) {
          const int code=std::fgetc(f);
          if(code<=0) {error="Truncated or zero-length HDR RLE packet";return false;}
          const uint32_t length=code>128?code-128:code;
          if(length>count2-at) {error="HDR RLE packet exceeds its scanline";return false;}
          if(code>128) {
            const int value=std::fgetc(f);if(value<0) {error="Truncated HDR RLE run";return false;}
            for(uint32_t i=0;i<length;++i)row[(at+i)*4+channel]=uint8_t(value);
          } else for(uint32_t i=0;i<length;++i) {
            const int value=std::fgetc(f);if(value<0) {error="Truncated HDR RLE literal";return false;}
            row[(at+i)*4+channel]=uint8_t(value);
          }
          at+=length;
        }
      }
    } else {
      uint32_t at=0,shift=0;uint8_t value[4];std::memcpy(value,first,4);
      while(at<count2) {
        if(value[0]==1&&value[1]==1&&value[2]==1) {
          if(!havePrevious||shift>=32) {error="Invalid legacy HDR run";return false;}
          const uint64_t length=uint64_t(value[3])<<shift;
          if(length>count2-at) {error="Legacy HDR run exceeds its scanline";return false;}
          for(uint64_t i=0;i<length;++i)std::memcpy(&row[(at+i)*4],previous,4);
          at+=uint32_t(length);shift+=8;
        } else {
          std::memcpy(&row[at++*4],value,4);std::memcpy(previous,value,4);havePrevious=true;shift=0;
        }
        if(at<count2&&!readBytes(f,value,4)) {error="Truncated flat HDR pixels";return false;}
      }
    }
    std::memcpy(previous,&row[(count2-1)*4],4);havePrevious=true;
    for(uint32_t b=0;b<count2;++b) {
      const uint32_t xi=axis1[1]=='X'?a:b,yi=axis1[1]=='Y'?a:b;
      const char xsign=axis1[1]=='X'?axis1[0]:axis2[0],ysign=axis1[1]=='Y'?axis1[0]:axis2[0];
      const uint32_t x=xsign=='+'?xi:width-1-xi,y=ysign=='-'?yi:height-1-yi;
      const auto * rgbe=&row[b*4];float rgba[4]={0,0,0,1};
      for(int c=0;c<3;++c) {
        const double value=rgbe[3]?std::ldexp(double(rgbe[c])+.5,int(rgbe[3])-136):0;
        const double converted=value*scale/(exposure*correction[c]);
        if(!std::isfinite(converted)||converted<0||converted>std::numeric_limits<float>::max()) {
          error="HDR radiance or metadata exceeds the finite float domain";return false;
        }
        rgba[c]=float(converted);
      }
      std::memcpy(output.data()+(uint64_t(y)*width+x)*16,rgba,16);
    }
  }
  if(std::fgetc(f)!=EOF||std::ferror(f)) {error="HDR image contains trailing data or a read error";return false;}
  dimensions=SbVec2i32(width,height);pixels=output;return true;
}
bool readImage(const SbString & name,SbVec2i32 & dimensions,SbByteBuffer & pixels,
  float scale,uint64_t budget,SbString & error) {
  FILE * f=std::fopen(name.getString(),"rb");
  if(!f) {error="Could not open environment image";return false;}
  uint8_t magic[4]={};const size_t count=std::fread(magic,1,4,f);std::fclose(f);
  if(count>=2&&magic[0]=='#'&&magic[1]=='?')return hdr(name,dimensions,pixels,scale,budget,error);
  const uint8_t exrMagic[]={0x76,0x2f,0x31,0x01};
  if(count==4&&!std::memcmp(magic,exrMagic,4))return coin_environment_read_exr(name,dimensions,pixels,scale,budget,error);
  error="Unsupported environment image signature; use RGBE, optional OpenEXR or native float pixels";return false;
}
}

uint64_t CoinEnvironmentRadiance::memoryBytes() const {
  uint64_t bytes=0;for(const auto & image:pixels)bytes+=image.size();return bytes;
}
bool CoinEnvironmentRadiance::validate(SbString & error,uint64_t budget) const {
  const int w=dimensions[0],h=dimensions[1];
  if((projection!=CUBE&&projection!=EQUIRECTANGULAR)||colorSpace!=LINEAR_SRGB||
     w<=0||h<=0||w>32768||h>32768||(projection==CUBE?w!=h:w!=2*h)||memoryBytes()>budget) {
    error="Invalid radiance projection, dimensions, color space or memory budget";return false;
  }
  const float * q=sourceToWorld.getValue();double norm=0;
  for(int i=0;i<4;++i) {if(!std::isfinite(q[i])) {error="Nonfinite source orientation";return false;}norm+=double(q[i])*q[i];}
  if(std::abs(norm-1)>.0001) {error="Source orientation must be a unit rotation";return false;}
  const int faces=projection==CUBE?6:1;
  for(int face=0;face<6;++face) {
    if(pixels[face].size()!=(face<faces?uint64_t(w)*h*16:0)) {error="Missing, extra or incompatible radiance face";return false;}
    for(size_t offset=0;offset<pixels[face].size();offset+=16) {
      float rgba[4];std::memcpy(rgba,pixels[face].constData()+offset,16);
      for(int c=0;c<3;++c)if(!std::isfinite(rgba[c])||rgba[c]<0) {error="Radiance must be finite and nonnegative";return false;}
      if(rgba[3]!=1) {error="Radiance faces must be opaque; presentation alpha is independent";return false;}
    }
  }
  error.makeEmpty();return true;
}
SbVec3f CoinEnvironmentRadiance::sample(const SbVec3f & direction) const {
  if(!std::isfinite(direction[0])||!std::isfinite(direction[1])||
     !std::isfinite(direction[2]))return SbVec3f(0,0,0);
  const float largest=std::max(std::abs(direction[0]),std::max(std::abs(direction[1]),std::abs(direction[2])));
  if(largest==0)return SbVec3f(0,0,0);
  SbVec3f n;sourceToWorld.inverse().multVec(direction/largest,n);n.normalize();
  const int w=dimensions[0],h=dimensions[1];if(w<=0||h<=0)return SbVec3f(0,0,0);
  if(projection==CUBE) {
    uint32_t face;float u,v;cubeCoordinates(n,face,u,v);
    const float x=(u+1)*.5f*w-.5f,y=(v+1)*.5f*h-.5f;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));const float dx=x-ix,dy=y-iy;
    return cubeTap(*this,face,ix,iy)*((1-dx)*(1-dy))+cubeTap(*this,face,ix+1,iy)*(dx*(1-dy))+
      cubeTap(*this,face,ix,iy+1)*((1-dx)*dy)+cubeTap(*this,face,ix+1,iy+1)*(dx*dy);
  }
  const float x=(std::atan2(n[1],n[0])/(2*pi)+.5f)*w-.5f;
  const float y=std::acos(std::max(-1.f,std::min(1.f,n[2])))/pi*h-.5f;
  const int ix=int(std::floor(x)),iy=int(std::floor(y));const float dx=x-ix,dy=y-iy;
  const auto tap=[&](int tx,int ty) {return pixel(pixels[0],w,(tx%w+w)%w,std::max(0,std::min(h-1,ty)));};
  return tap(ix,iy)*((1-dx)*(1-dy))+tap(ix+1,iy)*(dx*(1-dy))+
    tap(ix,iy+1)*((1-dx)*dy)+tap(ix+1,iy+1)*(dx*dy);
}
bool CoinEnvironmentRadiance::readHdrPanorama(const SbString & file,SbString & error,float scale,uint64_t budget) {
  try {
    CoinEnvironmentRadiance candidate;
    if(!readImage(file,candidate.dimensions,candidate.pixels[0],scale,budget,error)||!candidate.validate(error,budget))return false;
    *this=candidate;error.makeEmpty();return true;
  } catch(const std::bad_alloc&) {error="HDR panorama allocation failed";return false;}
}
bool CoinEnvironmentRadiance::readHdrFaces(const SbString files[6],SbString & error,float scale,uint64_t budget) {
  try {
    CoinEnvironmentRadiance candidate;candidate.projection=CUBE;
    for(int face=0;face<6;++face) {
      SbVec2i32 size;
      if(!readImage(files[face],size,candidate.pixels[face],scale,budget-candidate.memoryBytes(),error))return false;
      if(face==0)candidate.dimensions=size;
      if(size!=candidate.dimensions||size[0]!=size[1]) {error="HDR cube faces must have matching square dimensions";return false;}
    }
    if(!candidate.validate(error,budget))return false;
    *this=candidate;error.makeEmpty();return true;
  } catch(const std::bad_alloc&) {error="HDR cube allocation failed";return false;}
}
