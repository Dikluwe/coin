#include <Inventor/environment/CoinEnvironmentAnisotropy.h>
#include <Inventor/environment/CoinEnvironment.h>
#include <Inventor/environment/CoinEnvironmentHalf.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <vector>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif
namespace {
const double pi=3.14159265358979323846;
void put32(uint8_t * p,uint32_t v) {for(int i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
void put64(uint8_t * p,uint64_t v) {for(int i=0;i<8;++i)p[i]=uint8_t(v>>(8*i));}
uint32_t get32(const uint8_t * p) {uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(p[i])<<(8*i);return v;}
uint64_t get64(const uint8_t * p) {uint64_t v=0;for(int i=0;i<8;++i)v|=uint64_t(p[i])<<(8*i);return v;}
bool dimensions(const CoinEnvironmentAnisotropy & p) {
  return p.viewSize>=4&&p.viewSize<=128&&p.roughnessSize>=4&&p.roughnessSize<=128&&
    p.strengthSize>=2&&p.strengthSize<=32&&p.azimuthSize>=2&&p.azimuthSize<=32&&
    p.samples>=128&&p.samples<=16384;
}
bool layout(const CoinEnvironmentAnisotropy & p) {
  return dimensions(p)&&p.directional.size()==uint64_t(p.atlasWidth())*p.atlasHeight()*8&&
    p.average.size()==uint64_t(p.strengthSize)*p.roughnessSize*8;
}
float clamp(float x,float low,float high) {return std::max(low,std::min(high,x));}
SbVec2f texel(const SbByteBuffer & b,uint64_t index) {
  const auto * p=reinterpret_cast<const uint8_t *>(b.constData())+index*8;
  return SbVec2f(CoinEnvironmentHalf::fromHalf(CoinEnvironmentHalf::read16(p)),
    CoinEnvironmentHalf::fromHalf(CoinEnvironmentHalf::read16(p+2)));
}
void store(SbByteBuffer & b,uint64_t index,const SbVec2f & value) {
  auto * p=reinterpret_cast<uint8_t *>(b.data())+index*8;
  CoinEnvironmentHalf::write16(p,CoinEnvironmentHalf::toHalf(value[0]));
  CoinEnvironmentHalf::write16(p+2,CoinEnvironmentHalf::toHalf(value[1]));
  CoinEnvironmentHalf::write16(p+4,CoinEnvironmentHalf::toHalf(value[0]+value[1]));
  CoinEnvironmentHalf::write16(p+6,CoinEnvironmentHalf::toHalf(1));
}
struct Grid {
  unsigned low,high;float weight;
  Grid(float normalized,unsigned count) {
    const float x=clamp(normalized,0,1)*(count-1);
    low=unsigned(x);high=std::min(low+1,count-1);weight=x-low;
  }
};
SbVec2f tile(const CoinEnvironmentAnisotropy & p,const Grid & v,const Grid & r,unsigned s,unsigned a) {
  const uint64_t row=(uint64_t(a)*p.roughnessSize+r.low)*p.atlasWidth(),
    next=(uint64_t(a)*p.roughnessSize+r.high)*p.atlasWidth(),column=s*p.viewSize;
  const auto first=texel(p.directional,row+column+v.low)*(1-v.weight)+texel(p.directional,row+column+v.high)*v.weight;
  const auto last=texel(p.directional,next+column+v.low)*(1-v.weight)+texel(p.directional,next+column+v.high)*v.weight;
  return first*(1-r.weight)+last*r.weight;
}
}
uint64_t CoinEnvironmentAnisotropy::contentIdentity() const {
  uint64_t h=UINT64_C(14695981039346656037);
  const auto add=[&](uint8_t v) {h^=v;h*=UINT64_C(1099511628211);};
  uint8_t description[24];const uint32_t values[]={1,viewSize,roughnessSize,strengthSize,azimuthSize,samples};
  for(int i=0;i<6;++i)put32(description+4*i,values[i]);
  for(auto v:description)add(v);
  for(const auto * b:{&directional,&average})for(size_t i=0;i<b->size();++i)add(uint8_t(b->constData()[i]));
  return h;
}
bool CoinEnvironmentAnisotropy::validate(SbString & error) const {
  if(!layout(*this)) {error="Invalid anisotropic albedo dimensions or layout";return false;}
  for(const auto * b:{&directional,&average})for(size_t i=0;i<b->size();i+=8) {
    const auto * p=reinterpret_cast<const uint8_t *>(b->constData())+i;float v[4];
    for(int c=0;c<4;++c)v[c]=CoinEnvironmentHalf::fromHalf(CoinEnvironmentHalf::read16(p+2*c));
    if(!std::isfinite(v[0])||!std::isfinite(v[1])||!std::isfinite(v[2])||v[0]<0||v[1]<0||
       v[0]+v[1]>1.001f||v[2]<0||v[2]>1.001f||std::abs(v[2]-v[0]-v[1])>.001f||v[3]!=1) {
      error="Anisotropic albedo violates its finite energy domain";return false;
    }
  }
  if(identity!=contentIdentity()) {error="Anisotropic albedo content identity is invalid";return false;}
  error.makeEmpty();return true;
}
bool CoinEnvironmentAnisotropy::prepare(const CoinEnvironmentAnisotropyOptions & o,
  CoinEnvironmentAnisotropy & result,SbString & error) {
  CoinEnvironmentAnisotropy p;p.viewSize=o.viewSize;p.roughnessSize=o.roughnessSize;
  p.strengthSize=o.strengthSize;p.azimuthSize=o.azimuthSize;p.samples=o.samples;
  if(!dimensions(p)) {error="Invalid anisotropic albedo preparation dimensions or samples";return false;}
  const uint64_t pixels=uint64_t(p.atlasWidth())*p.atlasHeight(),bytes=(pixels+uint64_t(p.strengthSize)*p.roughnessSize)*8;
  if(pixels*p.samples>UINT64_C(536870912)) {error="Anisotropic albedo preparation exceeds its integration-work bound";return false;}
  if(bytes>o.payloadBudgetBytes||result.bytes()>o.workingBudgetBytes||bytes>o.workingBudgetBytes-result.bytes()) {
    error="Anisotropic albedo exceeds its payload or concurrent CPU budget";return false;
  }
  try {
    p.directional=SbByteBuffer(size_t(pixels*8));p.average=SbByteBuffer(size_t(p.strengthSize)*p.roughnessSize*8);
    for(unsigned a=0;a<p.azimuthSize;++a)for(unsigned s=0;s<p.strengthSize;++s)
      for(unsigned r=0;r<p.roughnessSize;++r)for(unsigned v=0;v<p.viewSize;++v) {
        const float nv=std::max(.0001f,float(v)/(p.viewSize-1)),phi=float(pi*.5*a/(p.azimuthSize-1));
        const float lateral=std::sqrt(std::max(0.f,1-nv*nv));SbVec2f dfg;
        const float rough=CoinEnvironment::roughnessFloor+(1-CoinEnvironment::roughnessFloor)*r/(p.roughnessSize-1);
        if(!CoinEnvironment::anisotropicDfg(SbVec3f(lateral*std::cos(phi),lateral*std::sin(phi),nv),
          rough,float(s)/(p.strengthSize-1),p.samples,dfg,error))return false;
        const uint64_t index=(uint64_t(a)*p.roughnessSize+r)*p.atlasWidth()+s*p.viewSize+v;
        store(p.directional,index,dfg);
      }
    // Cosine-weighted hemispherical A/B average. Quarter-azimuth symmetry is
    // integrated explicitly; endpoints use trapezoid weights in both axes.
    for(unsigned s=0;s<p.strengthSize;++s)for(unsigned r=0;r<p.roughnessSize;++r) {
      double sum[2]={},norm=0;
      for(unsigned a=0;a<p.azimuthSize;++a)for(unsigned v=0;v<p.viewSize;++v) {
        const double mu=double(v)/(p.viewSize-1);
        const double weight=mu*(a==0||a+1==p.azimuthSize?.5:1)*(v==0||v+1==p.viewSize?.5:1);
        const auto ab=texel(p.directional,(uint64_t(a)*p.roughnessSize+r)*p.atlasWidth()+s*p.viewSize+v);
        sum[0]+=weight*ab[0];sum[1]+=weight*ab[1];norm+=weight;
      }
      store(p.average,uint64_t(r)*p.strengthSize+s,SbVec2f(float(sum[0]/norm),float(sum[1]/norm)));
    }
    p.identity=p.contentIdentity();if(!p.validate(error))return false;
    result=p;error.makeEmpty();return true;
  }catch(const std::bad_alloc &) {error="Cannot allocate anisotropic albedo candidate";return false;}
}
bool CoinEnvironmentAnisotropy::evaluate(const SbVec3f & view,float rough,float strength,SbVec2f & result,SbString & error) const {
  const float scale=std::max(std::abs(view[0]),std::max(std::abs(view[1]),std::abs(view[2])));
  if(!layout(*this)||!std::isfinite(view[0])||!std::isfinite(view[1])||!std::isfinite(view[2])||
     !(scale>0)||view[2]<=0||!std::isfinite(rough)||rough<0||rough>1||!std::isfinite(strength)||strength<0||strength>1) {
    error="Invalid anisotropic albedo query";return false;
  }
  const auto v=view/scale;const float nv=v[2]/v.length();
  const float phi=std::atan2(std::abs(v[1]),std::abs(v[0]));
  const Grid vg(nv,viewSize),rg((rough-CoinEnvironment::roughnessFloor)/(1-CoinEnvironment::roughnessFloor),roughnessSize),sg(strength,strengthSize),ag(float(phi/(pi*.5)),azimuthSize);
  const auto first=tile(*this,vg,rg,sg.low,ag.low)*(1-sg.weight)+tile(*this,vg,rg,sg.high,ag.low)*sg.weight;
  const auto last=tile(*this,vg,rg,sg.low,ag.high)*(1-sg.weight)+tile(*this,vg,rg,sg.high,ag.high)*sg.weight;
  result=first*(1-ag.weight)+last*ag.weight;error.makeEmpty();return true;
}
bool CoinEnvironmentAnisotropy::evaluateAverage(float rough,float strength,SbVec2f & result,SbString & error) const {
  if(!layout(*this)||!std::isfinite(rough)||rough<0||rough>1||!std::isfinite(strength)||strength<0||strength>1) {
    error="Invalid anisotropic average-albedo query";return false;
  }
  const Grid r((rough-CoinEnvironment::roughnessFloor)/(1-CoinEnvironment::roughnessFloor),roughnessSize),s(strength,strengthSize);
  const auto first=texel(average,uint64_t(r.low)*strengthSize+s.low)*(1-s.weight)+texel(average,uint64_t(r.low)*strengthSize+s.high)*s.weight;
  const auto last=texel(average,uint64_t(r.high)*strengthSize+s.low)*(1-s.weight)+texel(average,uint64_t(r.high)*strengthSize+s.high)*s.weight;
  result=first*(1-r.weight)+last*r.weight;error.makeEmpty();return true;
}
bool CoinEnvironmentAnisotropy::write(const SbString & file,SbString & error) const {
  if(!validate(error))return false;
  std::string pattern=std::string(file.getString())+".tmp-XXXXXX";std::vector<char> tmp(pattern.begin(),pattern.end());tmp.push_back(0);
#ifdef _WIN32
  int fd=-1;if(_mktemp_s(tmp.data(),tmp.size())==0)fd=_open(tmp.data(),_O_CREAT|_O_EXCL|_O_BINARY|_O_WRONLY,_S_IREAD|_S_IWRITE);
  FILE * f=fd<0?nullptr:_fdopen(fd,"wb");
#else
  const int fd=mkstemp(tmp.data());FILE * f=fd<0?nullptr:fdopen(fd,"wb");
#endif
  if(!f) {
    if(fd>=0) {
#ifdef _WIN32
      _close(fd);
#else
      close(fd);
#endif
      std::remove(tmp.data());
    }
    error="Cannot create anisotropic albedo candidate file";return false;
  }
  uint8_t h[56]={};std::memcpy(h,"COINANI1",8);
  const uint32_t values[]={1,viewSize,roughnessSize,strengthSize,azimuthSize,samples};
  for(int i=0;i<6;++i)put32(h+8+i*4,values[i]);
  put64(h+32,directional.size());put64(h+40,average.size());put64(h+48,identity);
  bool ok=std::fwrite(h,1,sizeof(h),f)==sizeof(h)&&std::fwrite(directional.constData(),1,directional.size(),f)==directional.size()&&
    std::fwrite(average.constData(),1,average.size(),f)==average.size();
  if(std::fclose(f)!=0)ok=false;
  if(ok) {
#ifdef _WIN32
    // Existing destinations are preserved if the platform refuses replacement.
#endif
    ok=std::rename(tmp.data(),file.getString())==0;
  }
  if(!ok) {std::remove(tmp.data());error="Cannot publish anisotropic albedo candidate file";return false;}
  error.makeEmpty();return true;
}
bool CoinEnvironmentAnisotropy::read(const SbString & file,SbString & error,uint64_t working) {
  FILE * f=std::fopen(file.getString(),"rb");if(!f) {error="Cannot open anisotropic albedo file";return false;}
  uint8_t h[56];const bool header=std::fread(h,1,sizeof(h),f)==sizeof(h);
  CoinEnvironmentAnisotropy p;
  if(!header||std::memcmp(h,"COINANI1",8)||get32(h+8)!=1) {std::fclose(f);error="Invalid anisotropic albedo file header";return false;}
  p.viewSize=get32(h+12);p.roughnessSize=get32(h+16);p.strengthSize=get32(h+20);p.azimuthSize=get32(h+24);p.samples=get32(h+28);
  const uint64_t d=get64(h+32),a=get64(h+40);p.identity=get64(h+48);
  if(!dimensions(p)||d!=uint64_t(p.atlasWidth())*p.atlasHeight()*8||a!=uint64_t(p.strengthSize)*p.roughnessSize*8||
    bytes()>working||d>working-bytes()||a>working-bytes()-d) {
    std::fclose(f);error="Anisotropic albedo file exceeds its layout or concurrent CPU budget";return false;
  }
  try {
    p.directional=SbByteBuffer(size_t(d));p.average=SbByteBuffer(size_t(a));
    const bool ok=std::fread(p.directional.data(),1,size_t(d),f)==d&&std::fread(p.average.data(),1,size_t(a),f)==a&&std::fgetc(f)==EOF&&!std::ferror(f);
    const bool closed=std::fclose(f)==0;f=nullptr;
    if(!ok||!closed) {error="Truncated or trailing anisotropic albedo file";return false;}
    if(!p.validate(error))return false;
    *this=p;error.makeEmpty();return true;
  }catch(const std::bad_alloc &) {if(f)std::fclose(f);error="Cannot allocate anisotropic albedo file candidate";return false;}
}
