#ifndef COIN_RENDER_VOLUME_SRGB_ORACLE_H
#define COIN_RENDER_VOLUME_SRGB_ORACLE_H
#include "CoinRenderVolumeOracle.h"
namespace volume_srgb_oracle {
using volume_oracle::Level;
inline double decode(double byte) { const double x=byte/255.; return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4); }
inline uint8_t encode(double value) { const double x=value<=.0031308?12.92*value:1.055*std::pow(value,1/2.4)-.055; return uint8_t(std::max(0.,std::min(255.,std::floor(x*255+.5)))); }
inline std::vector<Level> chain(unsigned w,unsigned h,unsigned d,const std::vector<uint8_t>& bytes,bool mips=true) {
  std::vector<Level> out{{w,h,d,bytes}};
  while(mips&&(w>1||h>1||d>1)) {
    const unsigned nw=std::max(1u,w/2),nh=std::max(1u,h/2),nd=std::max(1u,d/2);
    Level next{nw,nh,nd,std::vector<uint8_t>(size_t(nw)*nh*nd*4)};
    const auto& old=out.back().bytes; const uint64_t area=uint64_t(w)*h*d;
    for(unsigned z=0;z<nd;++z)for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x) {
      double rgb[3]={};uint64_t alpha=0;
      for(unsigned iz=z*d/nd;iz<std::min(d,((z+1)*d+nd-1)/nd);++iz)
      for(unsigned iy=y*h/nh;iy<std::min(h,((y+1)*h+nh-1)/nh);++iy)
      for(unsigned ix=x*w/nw;ix<std::min(w,((x+1)*w+nw-1)/nw);++ix) {
        const uint64_t weight=volume_oracle::overlap(x,ix,w,nw)*volume_oracle::overlap(y,iy,h,nh)*volume_oracle::overlap(z,iz,d,nd);
        const size_t at=((size_t(iz)*h+iy)*w+ix)*4;
        for(unsigned c=0;c<3;++c)rgb[c]+=decode(old[at+c])*double(weight);
        alpha+=old[at+3]*weight;
      }
      const size_t at=((size_t(z)*nh+y)*nw+x)*4;
      for(unsigned c=0;c<3;++c)next.bytes[at+c]=encode(rgb[c]/area);
      next.bytes[at+3]=uint8_t((alpha+area/2)/area);
    }
    out.push_back(std::move(next));w=nw;h=nh;d=nd;
  }
  return out;
}
inline std::vector<uint8_t> atlas(const std::vector<Level>& levels,unsigned& width,unsigned& height) {
  const auto& first=levels.front();unsigned columns=1;while(columns*columns<first.d)columns*=2;
  width=first.w*columns;height=0;for(const auto& l:levels)height+=l.h*((l.d+columns-1)/columns);
  std::vector<uint8_t> bytes(size_t(width)*height*4,0);unsigned row=0;
  for(const auto& l:levels){for(unsigned z=0;z<l.d;++z)for(unsigned y=0;y<l.h;++y)for(unsigned x=0;x<l.w;++x)for(unsigned c=0;c<4;++c)
    bytes[(size_t(row+(z/columns)*l.h+y)*width+(z%columns)*l.w+x)*4+c]=l.bytes[((size_t(z)*l.h+y)*l.w+x)*4+c];row+=l.h*((l.d+columns-1)/columns);}
  return bytes;
}
inline double sample(const Level& l,double s,double t,double r,unsigned c,bool linear,bool repeat) {
  unsigned dims[]={l.w,l.h,l.d};double coords[]={s,t,r};int lo[3];double f[3];
  for(unsigned a=0;a<3;++a){const double v=coords[a]*dims[a]-(linear?.5:0);lo[a]=int(std::floor(v));f[a]=linear?v-std::floor(v):0;}
  auto bound=[&](int v,unsigned a){return repeat?(v%int(dims[a])+int(dims[a]))%int(dims[a]):std::max(0,std::min(int(dims[a])-1,v));};
  double out=0;
  for(unsigned z=0;z<2;++z)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){const auto b=l.bytes[((size_t(bound(lo[2]+z,2))*l.h+bound(lo[1]+y,1))*l.w+bound(lo[0]+x,0))*4+c];out+=(x?f[0]:1-f[0])*(y?f[1]:1-f[1])*(z?f[2]:1-f[2])*(c==3?b/255.:decode(b));}
  return out;
}
inline double filtered(const std::vector<Level>& levels,double s,double t,double r,unsigned c,int quality,double lod,bool repeat=false) {
  if(quality<2||lod<=0)return volume_srgb_oracle::sample(levels[0],s,t,r,c,quality!=0,repeat);
  lod=std::min(lod,double(levels.size()-1));const unsigned a=unsigned(std::floor(lod)),b=std::min(a+1,unsigned(levels.size()-1));
  return (1-(lod-a))*volume_srgb_oracle::sample(levels[a],s,t,r,c,quality==3,repeat)+(lod-a)*volume_srgb_oracle::sample(levels[b],s,t,r,c,quality==3,repeat);
}
}
#endif
