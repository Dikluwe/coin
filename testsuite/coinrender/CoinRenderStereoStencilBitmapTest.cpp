#include "CoinRenderStereoStencilReference.h"
#include <iostream>
#include <cmath>
using namespace stereo_stencil_reference;
static bool check(bool value,const char *label){if(!value)std::cerr<<"stereo_bitmap FAIL "<<label<<std::endl;return value;}
static bool expected(int width,int height,const Layout &layout,const std::vector<uint8_t> &source,
                     const std::vector<uint8_t> &oracle,const char *label){
  std::vector<uint8_t> out;std::string error;
  return check(expand(width,height,layout,source.data(),source.size(),out,error)&&out==oracle,label);
}
int main(){
  bool ok=true;Layout layout;layout.alignment=1;
  const std::vector<uint8_t> msb={1,0,0,1,0,1,1,0,0,1,1,0,1, 0,1,0,1,0,0,1,1,1,0,1,0,0};
  const std::vector<uint8_t> lsb={0,1,1,0,1,0,0,1,0,0,0,1,0, 1,1,0,0,1,0,1,0,0,0,0,0,0};
  ok &= expected(13,2,layout,{0x96,0x68,0x53,0xa0},msb,"MSB-first odd-width bits and next row");
  layout.lsbFirst=true;
  ok &= expected(13,2,layout,{0x96,0x68,0x53,0xa0},lsb,"LSB-first reverses bits, not rows or bytes");
  for(int alignment:{1,2,4,8}){
    layout=Layout{};layout.alignment=alignment;
    const unsigned stride=alignment>2?unsigned(alignment):2u;std::vector<uint8_t> source(stride*2,0xff);
    source[0]=0x96;source[1]=0x68;source[stride]=0x53;source[stride+1]=0xa0;
    ok &= expected(13,2,layout,source,msb,"padding for every legal unpack alignment");
  }
  layout=Layout{};layout.rowLength=19;layout.alignment=2;layout.skipRows=1;layout.skipPixels=5;
  const std::vector<uint8_t> padded={0xff,0xff,0xff,0xff,0xd6,0x6c,0xa8,0xfe,0x39,0xb2,0x50,0xed};
  ok &= expected(10,2,layout,padded,{1,1,0,0,1,1,0,1,1,0, 0,0,1,1,0,1,1,0,0,1},"row length, row skip and partial-byte pixel skip");
  layout.lsbFirst=true;
  ok &= expected(10,2,layout,padded,{0,1,1,0,0,1,1,0,1,1, 1,0,0,0,1,0,0,1,1,0},"LSB-first with row padding and both skips");
  layout=Layout{};layout.alignment=1;layout.rowLength=19;layout.skipPixels=9;
  ok &= expected(9,1,layout,{0x00,0xb2,0x50},{0,1,1,0,0,1,0,0,1},"whole-byte plus partial-byte skip");
  layout.lsbFirst=true;
  ok &= expected(9,1,layout,{0x00,0xb2,0x50},{1,0,0,1,1,0,1,0,0},"LSB-first whole-byte skip");
  // Native columns use 0x55 with MSB-first default: left is local odd X.
  layout=Layout{};layout.alignment=1;
  ok &= expected(9,2,layout,{0x55,0x55,0x55,0x55},{0,1,0,1,0,1,0,1,0, 0,1,0,1,0,1,0,1,0},"native columns retain exact odd-X phase");
  ok &= expected(9,3,layout,{0,0,0xff,0xff,0,0},{0,0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1,1, 0,0,0,0,0,0,0,0,0},"native rows retain exact odd-Y phase");
  // Independent geometric oracles: the logical bottom origin is5 in a
  // 17-row framebuffer. Source row0 writes output row11, never row9 (the
  // unconverted GL viewport top). Odd/even extents retain the same phase.
  for(int height:{4,5})for(bool columns:{false,true}){
    layout=Layout{};layout.alignment=1;std::vector<uint8_t> source(size_t(height)*2);
    for(int row=0;row<height;++row){source[size_t(row)*2]=columns?0x55:row%2?0xff:0;
      source[size_t(row)*2+1]=source[size_t(row)*2];}
    std::vector<uint8_t> bits;std::string problem;
    ok &= check(expand(9,height,layout,source.data(),source.size(),bits,problem),"geometry source bitmap expands");
    const auto placement=upperLeftRaster(17,3,5,1,1);std::vector<uint8_t> screen(19*17,0x7f);
    for(int row=0;row<height;++row)for(int column=0;column<9;++column){
      const int x=int(std::floor(placement.x+(column+.5f)*placement.zoomX));
      const int y=int(std::floor(placement.y+(row+.5f)*placement.zoomY));
      if(x<0||x>=19||y<0||y>=17){ok &= check(false,"canonical stencil write stays in authored viewport");continue;}
      screen[size_t(y)*19+unsigned(x)]=bits[size_t(row)*9+unsigned(column)];
    }
    for(int y=0;y<17;++y)for(int x=0;x<19;++x){const bool inside=x>=3&&x<=11&&y>=(height==4?8:7)&&y<=11;
      const uint8_t value=inside?uint8_t(columns?(x==4||x==6||x==8||x==10):(y==8||y==10)):uint8_t(0x7f);
      ok &= check(screen[size_t(y)*19+unsigned(x)]==value,"upper-left row/column odd-origin even/odd-height exact writes and native phase");}
  }
  for(int height:{96,97}){
    const auto placement=upperLeftRaster(height,0,0,1,1);
    const int bottom=int(std::floor(placement.y+.5f*placement.zoomY));
    const int top=int(std::floor(placement.y+(height-.5f)*placement.zoomY));
    ok &= check(bottom==height-1&&top==0&&placement.zoomX==1&&placement.zoomY==-1,
      "full odd/even framebuffer maps native bottom row to last upper-left output row");
  }
  // An odd 17x13 CENTER/HALF image around native anchor(20,24)
  // occupies x12..28 and bottom-origin y18..30 in framebuffer53. The
  // reflected raster anchor is(12,35); source row0 must land at output34,
  // source row12 at22, with every source byte retaining its original row.
  const auto image=upperLeftImageRaster(12,35,1,1);
  for(int row=0;row<13;++row)for(int column=0;column<17;++column){
    const int x=int(std::floor(image.x+(column+.5f)*image.zoomX));
    const int y=int(std::floor(image.y+(row+.5f)*image.zoomY));
    ok &= check(x==12+column&&y==34-row,"odd CENTER/HALF image retains anchor, X order and native bottom rows");
  }
  const auto scaled=upperLeftImageRaster(7,41,2,3);
  for(int row=0;row<5;++row)for(int column=0;column<9;++column){
    // GL's per-source-pixel rectangle, not just its center: clipped source
    // skips are still owned by UNPACK, while zoom advances the written cells.
    const int x0=int(scaled.x+column*scaled.zoomX),x1=int(scaled.x+(column+1)*scaled.zoomX);
    const int y0=int(scaled.y+row*scaled.zoomY),y1=int(scaled.y+(row+1)*scaled.zoomY);
    ok &= check(x0==7+2*column&&x1==9+2*column&&y0==41-3*row&&y1==38-3*row,
      "scaled image footprint preserves rows and the exact zoomed pixel rectangle");
  }
  ok &= check(upperLeftBitmapYMove(-3)==3&&upperLeftBitmapYMove(4)==-4&&upperLeftBitmapYMove(0)==0,
    "zero-area bitmap alignment move reflects only Y, including clipped negative anchors");
  // The exact GL polygon-stipple row map includes framebuffer phase.
  // H96 reverses31..0; H97 is0,31..1, not a blind array reversal.
  std::vector<uint8_t> stipple(128);
  for(unsigned row=0;row<32;++row){stipple[row*4]=uint8_t(row);stipple[row*4+1]=uint8_t(row^0x55);
    stipple[row*4+2]=0x96;stipple[row*4+3]=uint8_t(~row);}
  for(int framebufferHeight:{96,97})for(int alignment:{1,2,4,8})for(bool lsb:{false,true}){
    Layout input;input.rowLength=45;input.alignment=alignment;input.skipPixels=7;input.skipRows=3;input.lsbFirst=lsb;
    const unsigned rowBytes=6,stride=((rowBytes+unsigned(alignment)-1)/unsigned(alignment))*unsigned(alignment);
    std::vector<uint8_t> padded((32+3)*stride,0xa5);
    for(unsigned row=0;row<32;++row)for(unsigned x=0;x<32;++x){const bool bit=(stipple[row*4+x/8]>>(7-x%8))&1;
      const unsigned targetBit=x+7,shift=lsb?targetBit%8:7-targetBit%8;
      auto &value=padded[(row+3)*stride+targetBit/8];value=uint8_t((value&~(1u<<shift))|(unsigned(bit)<<shift));}
    std::vector<uint8_t> normalized;std::string problem;
    ok &= check(reflectStipple(framebufferHeight,input,padded.data(),padded.size(),normalized,problem),"stipple expands padded/LSB/skip input");
    for(unsigned row=0;row<32;++row){const unsigned nativeRow=framebufferHeight==96?31-row:row==0?0:32-row;
      for(unsigned byte=0;byte<4;++byte)ok &= check(normalized[row*4+byte]==stipple[nativeRow*4+byte],
        "stipple preserves exact native bits with H96/H97 phase and every unpack layout");}
    ok &= check(normalized[0]==(framebufferHeight==96?31:0)&&normalized[4]==(framebufferHeight==96?30:31)&&
      normalized[124]==(framebufferHeight==96?0:1),"stipple top/next/bottom rows distinguish odd-height phase from naive reversal");
  }
  std::vector<uint8_t> normalized;std::string normalizationError;
  ok &= check(!reflectStipple(0,Layout{},nullptr,0,normalized,normalizationError)&&normalized.empty(),"stipple refuses missing framebuffer phase");
  ok &= check(bayerPass(0,0,0)&&!bayerPass(0,0,.5f)&&bayerPass(1,0,.5f)&&bayerPass(0,1,.5f)&&!bayerPass(1,1,.5f),
    "Bayer diagnostics independently preserve the native2x2 seed at half transparency");
  std::vector<uint8_t> out={9};std::string error;const uint8_t byte=0;
  ok &= check(!expand(13,2,layout,&byte,1,out,error)&&out.empty()&&!error.empty(),"truncated source refuses atomically");
  for(int invalid:{0,3,16}){layout.alignment=invalid;
    ok &= check(!expand(1,1,layout,&byte,1,out,error)&&out.empty()&&!error.empty(),"invalid unpack alignment refuses");}
  layout=Layout{};layout.skipRows=-1;
  ok &= check(!expand(1,1,layout,&byte,1,out,error),"negative row skip refuses");
  layout=Layout{};layout.skipPixels=-1;
  ok &= check(!expand(1,1,layout,&byte,1,out,error),"negative pixel skip refuses");
  layout=Layout{};layout.rowLength=-1;
  ok &= check(!expand(1,1,layout,&byte,1,out,error),"negative row length refuses");
  layout=Layout{};
  ok &= check(!expand(-1,1,layout,&byte,1,out,error),"negative dimensions refuse");
  ok &= check(expand(0,9,layout,nullptr,0,out,error)&&out.empty()&&error.empty(),"empty upload needs no client data");
  std::cout<<"stereo_bitmap_cpu result="<<(ok?"PASS":"FAIL")<<std::endl;return ok?0:1;
}
