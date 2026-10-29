#include "CoinEnvironmentExrP.h"
#include <cmath>
#include <cstring>
#include <exception>
#include <cstdio>
#ifdef COIN_ENVIRONMENT_HAVE_OPENEXR
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfTestFile.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfStringAttribute.h>
namespace {
// Bound the binary header and data window before InputFile can allocate its
// header/chunk index. No process-global OpenEXR limits are modified.
bool preflightExr(const SbString & file,uint64_t budget) {
  FILE * f=std::fopen(file.getString(),"rb");if(!f)return false;
  struct Close {FILE * f;~Close(){std::fclose(f);}} close{f};
  uint8_t word[16];if(std::fread(word,1,8,f)!=8)return false;
  unsigned bytes=8;bool window=false;
  const auto readName=[&](char * output) {
    for(unsigned n=0;n<256;++n) {
      const int c=std::fgetc(f);if(c<0||++bytes>65536)return false;
      output[n]=char(c);if(!c)return true;
    }
    return false;
  };
  const auto little=[](const uint8_t * p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;};
  while(bytes<65536) {
    char name[256],type[256];if(!readName(name))return false;if(!name[0])return window;
    if(!readName(type)||std::fread(word,1,4,f)!=4)return false;
    bytes+=4;const uint32_t size=little(word);if(bytes>65536||size>65536-bytes)return false;
    if(!std::strcmp(name,"dataWindow")) {
      if(window||std::strcmp(type,"box2i")||size!=16||std::fread(word,1,16,f)!=16)return false;
      const int64_t w=int64_t(int32_t(little(word+8)))-int32_t(little(word))+1;
      const int64_t h=int64_t(int32_t(little(word+12)))-int32_t(little(word+4))+1;
      if(w<=0||h<=0||w>32768||h>32768||uint64_t(w)*h*16>budget)return false;
      window=true;
    } else if(std::fseek(f,size,SEEK_CUR)!=0)return false;
    bytes+=size;
  }
  return false;
}
}
#endif

bool CoinEnvironmentRadiance::supportsOpenExr() {
#ifdef COIN_ENVIRONMENT_HAVE_OPENEXR
  return true;
#else
  return false;
#endif
}
bool coin_environment_read_exr(const SbString & file,SbVec2i32 & dimensions,
  SbByteBuffer & pixels,float scale,uint64_t budget,SbString & error) {
#ifdef COIN_ENVIRONMENT_HAVE_OPENEXR
  namespace exr=OPENEXR_IMF_NAMESPACE;
  try {
    if(!std::isfinite(scale)||scale<0) {error="OpenEXR scale must be finite and nonnegative";return false;}
    bool tiled=false,deep=false,multipart=false;
    if(!exr::isOpenExrFile(file.getString(),tiled,deep,multipart)||tiled||deep||multipart) {
      error="OpenEXR profile requires a single scanline RGB(A) image; tiled/deep/multipart are unsupported";return false;
    }
    if(!preflightExr(file,budget)) {error="OpenEXR header or decoded dimensions exceed the bounded import budget";return false;}
    exr::InputFile input(file.getString());const auto & header=input.header();
    const auto window=header.dataWindow();
    const int64_t width=int64_t(window.max.x)-window.min.x+1,height=int64_t(window.max.y)-window.min.y+1;
    if(width<=0||height<=0||width>32768||height>32768||window!=header.displayWindow()||
       uint64_t(width)*height*16>budget) {
      error="OpenEXR dimensions, window or decoded memory exceed the supported profile";return false;
    }
    const auto * chroma=header.findTypedAttribute<exr::ChromaticitiesAttribute>("chromaticities");
    if(chroma) {
      const exr::Chromaticities accepted;
      const auto & actual=chroma->value();
      if((actual.red-accepted.red).length()>.0001f||(actual.green-accepted.green).length()>.0001f||
         (actual.blue-accepted.blue).length()>.0001f||(actual.white-accepted.white).length()>.0001f) {
        error="OpenEXR primaries require explicit conversion to linear sRGB";return false;
      }
    }
    for(const char * name:{"colorSpace","oiio:ColorSpace"}) {
      const auto * attribute=header.findTypedAttribute<exr::StringAttribute>(name);
      if(attribute&&attribute->value()!="linear"&&attribute->value()!="Linear"&&
         attribute->value()!="Linear sRGB"&&attribute->value()!="linear_srgb"&&
         attribute->value()!="Linear Rec.709") {
        error="OpenEXR transfer/color space requires an explicit linear-sRGB conversion";return false;
      }
    }
    const auto & channels=header.channels();
    if(!channels.findChannel("R")||!channels.findChannel("G")||!channels.findChannel("B")) {
      error="OpenEXR profile requires R, G and B channels";return false;
    }
    for(auto it=channels.begin();it!=channels.end();++it) {
      const auto & channel=it.channel();
      if((std::strcmp(it.name(),"R")&&std::strcmp(it.name(),"G")&&std::strcmp(it.name(),"B")&&std::strcmp(it.name(),"A"))||
         (channel.type!=exr::HALF&&channel.type!=exr::FLOAT)||channel.xSampling!=1||channel.ySampling!=1) {
        error="OpenEXR profile accepts only unsampled RGB(A) half/float channels";return false;
      }
    }
    SbByteBuffer output(size_t(width*height)*16);char * base=output.data();
    exr::FrameBuffer frame;
    const char * names[]={"R","G","B","A"};
    for(int c=0;c<4;++c)frame.insert(names[c],exr::Slice::Make(exr::FLOAT,base+c*4,window,16,size_t(width)*16,1,1,c==3?1:0));
    input.setFrameBuffer(frame);input.readPixels(window.min.y,window.max.y);
    for(uint64_t offset=0;offset<output.size();offset+=16) {
      float rgba[4];std::memcpy(rgba,base+offset,16);
      for(int c=0;c<3;++c) {
        if(!std::isfinite(rgba[c])||rgba[c]<0||!std::isfinite(rgba[c]*scale)) {
          error="OpenEXR radiance must be finite and nonnegative before and after import scale";return false;
        }
        rgba[c]*=scale;
      }
      if(rgba[3]!=1) {error="OpenEXR environment alpha must be opaque; output transparency is independent";return false;}
      std::memcpy(base+offset,rgba,16);
    }
    dimensions=SbVec2i32(int32_t(width),int32_t(height));pixels=output;error.makeEmpty();return true;
  } catch(const std::exception & exception) {error="OpenEXR import failed: ";error+=exception.what();return false;}
#else
  (void)file;(void)dimensions;(void)pixels;(void)scale;(void)budget;
  error="OpenEXR codec is disabled; enable COIN_ENVIRONMENT_OPENEXR or provide native float pixels";return false;
#endif
}
