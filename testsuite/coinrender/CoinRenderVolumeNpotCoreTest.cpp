#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include "CoinRenderVolumeOracle.h"
#include <iostream>
#include <string>
using namespace volume_oracle;
static bool verify(unsigned w,unsigned h,unsigned d,const std::vector<uint8_t>& source,bool mips) {
  CoinRenderTextureImageSnapshot image;
  if(!CoinRenderTextureSamplingCore::packVolumeAtlas(source,w,h,d,4,mips,image)){std::cerr<<"admission "<<w<<','<<h<<','<<d<<'\n';return false;}
  unsigned columns=1;while(columns*columns<d)columns*=2;
  auto expected=chain(w,h,d,source,mips);unsigned rows=0;for(auto& l:expected)rows+=l.h*((l.d+columns-1)/columns);
  bool ok=image.width==w*columns&&image.height==rows&&image.volumeWidth==w&&image.volumeHeight==h&&image.volumeDepth==d&&image.volumeColumns==columns&&image.volumeMipmapped==mips&&!image.mipmapped&&CoinRenderTextureSamplingCore::validMipImage(image);
  std::vector<uint8_t> atlas(size_t(w*columns)*rows*4,0);unsigned offset=0;
  for(auto& l:expected){for(unsigned z=0;z<l.d;++z)for(unsigned y=0;y<l.h;++y)for(unsigned x=0;x<l.w;++x)for(unsigned c=0;c<4;++c)
    atlas[((size_t(offset+(z/columns)*l.h+y)*image.width)+(z%columns)*l.w+x)*4+c]=l.bytes[((size_t(z)*l.h+y)*l.w+x)*4+c];offset+=l.h*((l.d+columns-1)/columns);}
  if(image.pixelsRgba!=atlas){std::cerr<<"oracle bytes/padding "<<w<<','<<h<<','<<d<<'\n';ok=false;}
  auto bad=image;bad.width++;ok &= !CoinRenderTextureSamplingCore::validMipImage(bad);bad=image;bad.height++;ok &= !CoinRenderTextureSamplingCore::validMipImage(bad);bad=image;bad.volumeColumns*=2;ok &= !CoinRenderTextureSamplingCore::validMipImage(bad);bad=image;bad.pixelsRgba.pop_back();ok &= !CoinRenderTextureSamplingCore::validMipImage(bad);
  return ok;
}
int main(int argc,char**argv){bool ok=true;bool limit=argc>1&&std::string(argv[1])=="--limits";
  for(auto dims:{std::array<unsigned,3>{3,5,7},{7,3,5},{5,7,3},{1,255,1},{255,1,1},{1,1,255},{1,1,1},{2,4,8},{8,8,8},{9,11,13},{15,2,3}})
    for(bool mips:{false,true})ok &= verify(dims[0],dims[1],dims[2],authored(dims[0],dims[1],dims[2]),mips);
  // Each last cell on each odd axis must contribute, including alpha.
  for(unsigned axis=0;axis<3;++axis){unsigned dims[]={5,5,5};auto source=std::vector<uint8_t>(5*5*5*4,0);for(unsigned z=0;z<5;++z)for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x){unsigned p[]={x,y,z};if(p[axis]==4)for(unsigned c=0;c<4;++c)source[((z*5+y)*5+x)*4+c]=255;}ok &=verify(dims[0],dims[1],dims[2],source,true);}
  // Explicit half ties and immediately-preceding-level rounding.
  ok &= verify(2,1,1,{0,2,4,6,1,3,5,7},true);
  for(auto dims:{std::array<unsigned,3>{0,1,1},{1,0,1},{1,1,0},{257,1,1},{1,257,1},{1,1,257}}){CoinRenderTextureImageSnapshot image;ok &= !CoinRenderTextureSamplingCore::packVolumeAtlas({},dims[0],dims[1],dims[2],4,true,image);}
  for(unsigned channels:{0u,5u}){CoinRenderTextureImageSnapshot image;ok &= !CoinRenderTextureSamplingCore::packVolumeAtlas(std::vector<uint8_t>(4),1,1,1,channels,true,image);}
  for(unsigned size:{3u,5u}){CoinRenderTextureImageSnapshot image;ok &= !CoinRenderTextureSamplingCore::packVolumeAtlas(std::vector<uint8_t>(size),1,1,1,4,true,image);}
  if(limit)ok &= verify(255,253,251,authored(255,253,251),true);
  std::cout<<"NPOT core area oracle "<<(ok?"PASS":"FAIL")<<'\n';return ok?0:1;}
