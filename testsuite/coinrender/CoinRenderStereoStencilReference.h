#ifndef COIN_RENDER_STEREO_STENCIL_REFERENCE_H
#define COIN_RENDER_STEREO_STENCIL_REFERENCE_H
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace stereo_stencil_reference {
struct Layout {
  int rowLength=0,alignment=4,skipPixels=0,skipRows=0;
  bool lsbFirst=false;
};
inline bool expand(int width,int height,const Layout &layout,const uint8_t *source,
                   size_t sourceBytes,std::vector<uint8_t> &out,std::string &error){
  out.clear();error.clear();
  if(width<0||height<0||layout.rowLength<0||layout.skipPixels<0||layout.skipRows<0||
     (layout.alignment!=1&&layout.alignment!=2&&layout.alignment!=4&&layout.alignment!=8)){
    error="invalid bitmap dimensions or unpack layout";return false;
  }
  if(!width||!height)return true;
  const size_t limit=std::numeric_limits<size_t>::max();
  const size_t rowPixels=layout.rowLength?size_t(layout.rowLength):size_t(width);
  const size_t alignment=size_t(layout.alignment),rowBytes=(rowPixels+7)/8;
  if(rowBytes>limit-(alignment-1)){error="bitmap row stride overflows";return false;}
  const size_t stride=((rowBytes+alignment-1)/alignment)*alignment;
  const size_t lastRow=size_t(layout.skipRows)+size_t(height)-1;
  const size_t lastBit=size_t(layout.skipPixels)+size_t(width)-1;
  if(lastRow>limit/stride||lastRow*stride>limit-(lastBit/8+1)){
    error="bitmap source extent overflows";return false;
  }
  const size_t needed=lastRow*stride+lastBit/8+1;
  if(!source||needed>sourceBytes){error="truncated bitmap source";return false;}
  if(size_t(width)>limit/size_t(height)){error="expanded bitmap extent overflows";return false;}
  try{out.resize(size_t(width)*size_t(height));}catch(const std::bad_alloc&){error="expanded bitmap allocation failed";return false;}
  for(int y=0;y<height;++y){
    const size_t row=(size_t(layout.skipRows)+size_t(y))*stride;
    for(int x=0;x<width;++x){const size_t bit=size_t(layout.skipPixels)+size_t(x);
      const unsigned shift=layout.lsbFirst?unsigned(bit%8):7u-unsigned(bit%8);
      out[size_t(y)*size_t(width)+size_t(x)]=uint8_t((source[row+bit/8]>>shift)&1u);
    }
  }
  return true;
}
// Remap only a GL polygon-stipple upload for an upper-origin oracle.
// GL stipple addresses window rows independently of clip origin. Output
// row r must consume logical bottom row(H-1-r) modulo the32-row period.
inline bool reflectStipple(int framebufferHeight,const Layout &layout,const uint8_t *source,size_t sourceBytes,
                          std::vector<uint8_t> &packed,std::string &error){
  packed.clear();if(framebufferHeight<=0){error="stipple reference needs a positive framebuffer height";return false;}
  std::vector<uint8_t> bits;if(!expand(32,32,layout,source,sourceBytes,bits,error))return false;
  packed.assign(128,0);
  for(unsigned row=0;row<32;++row){const unsigned nativeRow=unsigned((framebufferHeight-1-int(row))%32+32)%32;
    for(unsigned x=0;x<32;++x)if(bits[nativeRow*32+x])packed[row*4+x/8]|=uint8_t(0x80u>>(x%8));
  }return true;
}
// Native Coin's public Bayer32 screen-door construction, independently
// evaluated from authored alpha and integer window coordinates for diagnostics.
inline bool bayerPass(unsigned x,unsigned y,float transparency){
  const int level=std::max(0,std::min(64,int(transparency*64)));
  if(!level)return true;const unsigned seed[4]={0,2,3,1};unsigned rank=0;
  for(unsigned bit=0;bit<5;++bit)rank|=seed[(((y>>bit)&1)*2)+((x>>bit)&1)]<<(2*(4-bit));
  return rank>unsigned(level*16-1);
}
struct RasterPlacement {float x=0,y=0,zoomX=1,zoomY=1;};
inline RasterPlacement upperLeftRaster(int framebufferHeight,int originX,int bottomOriginY,float zoomX,float zoomY){
  RasterPlacement result;result.x=float(originX);result.y=float(framebufferHeight)-float(bottomOriginY);
  result.zoomX=zoomX;result.zoomY=-zoomY;return result;
}
inline RasterPlacement upperLeftImageRaster(float x,float y,float zoomX,float zoomY){
  RasterPlacement result;result.x=x;result.y=y;result.zoomX=zoomX;result.zoomY=-zoomY;return result;
}
inline float upperLeftBitmapYMove(float lowerMove){return -lowerMove;}
// The interposer changes a legal legacy upload only inside this native-oracle
// scope. Every other GL call follows its original entry point unchanged.
struct Scope {
  Scope *previous=nullptr;
  unsigned uploads=0;
  bool clipControlAvailable=false,imageUploads=false;
  unsigned imageUploadCount=0,bitmapMoveCount=0,stippleUploadCount=0;
  int framebufferHeight=0;
  std::string error;
  Scope();
  ~Scope();
  bool ok()const{return error.empty();}
  Scope(const Scope&)=delete;
  Scope&operator=(const Scope&)=delete;
};
}
#endif
