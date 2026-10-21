#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include "CoinRenderVolumeSrgbOracle.h"
#include <iostream>
using Format=CoinRenderTextureFormat;
static bool check(bool value,const char* text){if(!value)std::cerr<<"volume_srgb_core: "<<text<<'\n';return value;}
static bool same(const CoinRenderTextureImageSnapshot&a,const CoinRenderTextureImageSnapshot&b){CoinRenderFramePlan x,y;x.textures.push_back(a);y.textures.push_back(b);return x.hasSamePayload(y);}
static bool verify(unsigned w,unsigned h,unsigned d,const std::vector<uint8_t>& raw,unsigned components,bool mips,Format format) {
  CoinRenderTextureImageSnapshot image;
  if(!check(CoinRenderTextureSamplingCore::packVolumeAtlas(raw,w,h,d,components,mips,image,format),"valid stored volume accepted"))return false;
  const auto levels=format==Format::RGBA8_SRGB?volume_srgb_oracle::chain(w,h,d,raw,mips):volume_oracle::chain(w,h,d,raw,mips);
  unsigned aw=0,ah=0;const auto expected=volume_srgb_oracle::atlas(levels,aw,ah);
  bool ok=check(image.format==format&&image.volumeWidth==w&&image.volumeHeight==h&&image.volumeDepth==d&&image.volumeSourceComponents==components&&image.volumeMipmapped==mips&&!image.mipmapped&&image.width==aw&&image.height==ah&&image.pixelsRgba==expected&&image.mipmapsRgba.empty()&&CoinRenderTextureSamplingCore::validMipImage(image),"independent exact atlas RGB/alpha/odd borders/padding and metadata");
  if(!ok)std::cerr<<"extent="<<w<<'x'<<h<<'x'<<d<<" mips="<<mips<<" format="<<unsigned(format)<<'\n';
  return ok;
}
static bool packing(){bool ok=true;
  for(auto dims:{std::array<unsigned,3>{3,5,7},{7,3,5},{5,7,3},{2,4,8},{1,255,1},{255,1,1},{1,1,255},{9,11,1},{1,1,1}})
    for(bool mips:{false,true})for(unsigned components:{1u,2u,3u,4u})for(auto format:{Format::RGBA8_LINEAR,Format::RGBA8_SRGB})ok &=verify(dims[0],dims[1],dims[2],volume_oracle::authored(dims[0],dims[1],dims[2]),components,mips,format);
  ok &=verify(2,1,1,{0,2,10,0,255,32,11,1},4,true,Format::RGBA8_SRGB);
  for(unsigned axis=0;axis<3;++axis){auto data=std::vector<uint8_t>(5*5*5*4,0);for(unsigned z=0;z<5;++z)for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x){const unsigned p[]={x,y,z};if(p[axis]==4)for(unsigned c=0;c<4;++c)data[((z*5+y)*5+x)*4+c]=255;}ok &=verify(5,5,5,data,4,true,Format::RGBA8_SRGB);}
  CoinRenderTextureImageSnapshot old;CoinRenderTextureSamplingCore::packVolumeAtlas({40,90,180,255},1,1,1,4,false,old);
  auto image=old;ok &=check(CoinRenderTextureSamplingCore::packVolumeAtlas({40,90,180,255},1,1,1,4,false,image)&&same(image,old),"default helper preserves linear representation");
  for(unsigned fault=0;fault<15;++fault){image=old;unsigned w=1,h=1,d=1,c=4;auto bytes=std::vector<uint8_t>{1,2,3,4};Format f=Format::RGBA8_SRGB;
    if(fault<6){unsigned*dims[]={&w,&h,&d};*dims[fault%3]=fault<3?0:257;}else if(fault==6)c=0;else if(fault==7)c=5;else if(fault==8)bytes.pop_back();else if(fault==9)bytes.push_back(5);else f=static_cast<Format>(fault-8);
    ok &=check(!CoinRenderTextureSamplingCore::packVolumeAtlas(bytes,w,h,d,c,true,image,f)&&same(image,old),"malformed format/dims/components/payload leaves previous image intact");}
  return ok;
}
static bool transport(){bool ok=true;CoinRenderFramePlan frame;frame.revision=23123;CoinRenderTextureImageSnapshot image;auto raw=volume_oracle::authored(3,5,7);
  if(!check(CoinRenderTextureSamplingCore::packVolumeAtlas(raw,3,5,7,4,true,image,Format::RGBA8_SRGB),"transport source packs"))return false;
  frame.textures.push_back(image);CoinWgpuFfiFrame ffi;std::string error;
  auto packet=[&](const CoinRenderFramePlan& input){if(!check(ffi.prepare(input,64,64,error),"same-revision valid FFI packet"))return false;const auto&v=ffi.getView();const auto&t=v.textures[0];const auto&i=input.textures[0];const unsigned flags=(i.volumeWidth-1)|((i.volumeHeight-1)<<8)|((i.volumeDepth-1)<<16)|(i.volumeMipmapped?1u<<24:0)|(i.format==Format::RGBA8_SRGB?1u<<25:0);return check(v.abi_version==66&&sizeof(CoinWgpuFrameView)==544&&v.texture_count==1&&t.format==11&&t.reserved==flags&&t.pixel_bytes_len==i.pixelsRgba.size()&&std::equal(i.pixelsRgba.begin(),i.pixelsRgba.end(),t.pixels),"ABI66 format11 bit25 and exact authored/mip bytes");};
  ok &=packet(frame);auto linear=frame;linear.textures[0].format=Format::RGBA8_LINEAR;ok &=check(!frame.hasSamePayload(linear),"format is part of payload identity")&&packet(linear)&&packet(frame);
  auto changed=frame;changed.textures[0].pixelsRgba[0]^=127;ok &=packet(changed)&&packet(frame);
  for(unsigned fault=0;fault<11;++fault){auto bad=frame;auto&i=bad.textures[0];if(fault==0)i.volumeWidth=257;if(fault==1)i.volumeHeight=0;if(fault==2)i.volumeColumns*=2;if(fault==3)i.width++;if(fault==4)i.height++;if(fault==5)i.pixelsRgba.pop_back();if(fault==6)i.pixelsRgba.push_back(0);if(fault==7)i.format=Format::RGBA16_FLOAT;if(fault==8)i.mipmapped=true;if(fault==9)i.volumeSourceComponents=5;if(fault==10)i.pixelsRgba.back()=1;
    ok &=check(!CoinRenderTextureSamplingCore::validMipImage(i)&&!ffi.prepare(bad,64,64,error),"same revision cannot bypass malformed volume validation")&&packet(frame);}
  return ok;
}
int main(int argc,char**argv){std::string mode=argc>1?argv[1]:"--core";bool ok=true;if(mode=="--core")ok=packing();else if(mode=="--transport")ok=transport();else if(mode=="--limits"){ok=verify(256,256,256,std::vector<uint8_t>(size_t(256)*256*256*4,128),4,true,Format::RGBA8_SRGB);for(auto dims:{std::array<unsigned,3>{256,1,1},{1,256,1},{1,1,256}})ok &=verify(dims[0],dims[1],dims[2],volume_oracle::authored(dims[0],dims[1],dims[2]),4,true,Format::RGBA8_SRGB);}else return 2;return ok?0:1;}
