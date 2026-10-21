#ifndef COIN_RENDER_VOLUME_ORACLE_H
#define COIN_RENDER_VOLUME_ORACLE_H
#include <vector>
#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>
namespace volume_oracle {
struct Level { unsigned w,h,d; std::vector<uint8_t> bytes; };
// Integer common-denominator interval intersections avoid floating point ties.
inline uint64_t overlap(unsigned destination,unsigned source,unsigned old,unsigned next) {
  const uint64_t lo=std::max(uint64_t(destination)*old,uint64_t(source)*next);
  const uint64_t hi=std::min(uint64_t(destination+1)*old,uint64_t(source+1)*next);
  return hi>lo ? hi-lo : 0;
}
inline std::vector<Level> chain(unsigned w,unsigned h,unsigned d,const std::vector<uint8_t>& bytes,bool mips=true) {
  std::vector<Level> levels{{w,h,d,bytes}};
  while(mips&&(w>1||h>1||d>1)) {
    unsigned nw=std::max(1u,w/2),nh=std::max(1u,h/2),nd=std::max(1u,d/2);
    Level next{nw,nh,nd,std::vector<uint8_t>(size_t(nw)*nh*nd*4)};
    const auto& old=levels.back().bytes;
    const uint64_t denominator=uint64_t(w)*h*d;
    for(unsigned z=0;z<nd;++z)for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x)for(unsigned c=0;c<4;++c) {
      uint64_t sum=0;
      for(unsigned iz=z*d/nd;iz<std::min(d,((z+1)*d+nd-1)/nd);++iz)
      for(unsigned iy=y*h/nh;iy<std::min(h,((y+1)*h+nh-1)/nh);++iy)
      for(unsigned ix=x*w/nw;ix<std::min(w,((x+1)*w+nw-1)/nw);++ix)
        sum+=uint64_t(old[((size_t(iz)*h+iy)*w+ix)*4+c])*overlap(x,ix,w,nw)*overlap(y,iy,h,nh)*overlap(z,iz,d,nd);
      next.bytes[((size_t(z)*nh+y)*nw+x)*4+c]=uint8_t((sum+denominator/2)/denominator);
    }
    levels.push_back(std::move(next));w=nw;h=nh;d=nd;
  }
  return levels;
}
inline std::vector<uint8_t> authored(unsigned w,unsigned h,unsigned d) {
  std::vector<uint8_t> out(size_t(w)*h*d*4);
  for(unsigned z=0;z<d;++z)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<4;++c)
    out[((size_t(z)*h+y)*w+x)*4+c]=uint8_t(13+((x*x*(11+c*5)+y*y*(17+c*3)+z*z*(23+c*7)+c*41)%229));
  return out;
}
inline double sample(const Level& l,double s,double t,double r,unsigned c,bool linear,bool repeat) {
  unsigned dims[]={l.w,l.h,l.d};double coords[]={s,t,r};int lo[3],hi[3];double f[3];
  for(unsigned a=0;a<3;++a){double v=coords[a]*dims[a]-(linear?.5:0.0);lo[a]=int(std::floor(v));hi[a]=lo[a]+1;f[a]=linear?v-std::floor(v):0;}
  auto bound=[&](int v,unsigned a){return repeat?(v%int(dims[a])+int(dims[a]))%int(dims[a]):std::max(0,std::min(int(dims[a])-1,v));};
  double out=0;
  for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x)
    out+=(x?f[0]:1-f[0])*(y?f[1]:1-f[1])*(z?f[2]:1-f[2])*l.bytes[((size_t(bound(z?hi[2]:lo[2],2))*l.h+bound(y?hi[1]:lo[1],1))*l.w+bound(x?hi[0]:lo[0],0))*4+c];
  return out;
}
inline double filtered(const std::vector<Level>& levels,double s,double t,double r,unsigned c,int quality,double lod,bool repeat=false){
  if(quality<2||lod<=0)return sample(levels[0],s,t,r,c,quality!=0,repeat);
  lod=std::min(lod,double(levels.size()-1));unsigned a=unsigned(std::floor(lod)),b=std::min(a+1,unsigned(levels.size()-1));
  return (1-(lod-a))*sample(levels[a],s,t,r,c,quality==3,repeat)+(lod-a)*sample(levels[b],s,t,r,c,quality==3,repeat);
}
}
#endif
