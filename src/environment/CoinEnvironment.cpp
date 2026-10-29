#include <Inventor/environment/CoinEnvironment.h>
#include <Inventor/environment/CoinEnvironmentHalf.h>
#include <Inventor/C/base/time.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <vector>
#include <string>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif
namespace {
const float pi=3.14159265358979323846f;
const uint64_t headerBytes=72, sectionBytes=40;
uint32_t get32(const uint8_t * p) { return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; }
void put32(uint8_t * p,uint32_t v) { for(int i=0;i<4;++i)p[i]=uint8_t(v>>(8*i)); }
uint64_t get64(const uint8_t * p) { return get32(p)|uint64_t(get32(p+4))<<32; }
void put64(uint8_t * p,uint64_t v) {put32(p,uint32_t(v));put32(p+4,uint32_t(v>>32));}
float getFloat(const uint8_t * p) {uint32_t bits=get32(p);float f;std::memcpy(&f,&bits,4);return f;}
void putFloat(uint8_t * p,float f) {uint32_t bits;std::memcpy(&bits,&f,4);put32(p,bits);}
uint64_t hash(const SbByteBuffer & buffer) {
  uint64_t value=UINT64_C(14695981039346656037);
  for(size_t i=0;i<buffer.size();++i) {value^=uint8_t(buffer[i]);value*=UINT64_C(1099511628211);}
  return value;
}
bool power(uint32_t n,uint32_t maximum) {return n>=8&&n<=maximum&&(n&(n-1))==0;}
float reverse(unsigned bits) {
  bits=(bits<<16)|(bits>>16);
  bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
  bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);
  bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
  bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
  return float(double(bits)/4294967296.0);
}
SbVec3f unit(SbVec3f v) {v.normalize();return v;}
// Heitz 2018 GGX visible-normal sampling, expressed with Coin vector operations.
SbVec3f visibleNormal(const SbVec3f & v,float alpha,float u,float q) {
  const SbVec3f vh=unit(SbVec3f(alpha*v[0],alpha*v[1],v[2]));
  const float len=vh[0]*vh[0]+vh[1]*vh[1];
  const SbVec3f t1=len>0 ? SbVec3f(-vh[1],vh[0],0)/std::sqrt(len) : SbVec3f(1,0,0);
  const SbVec3f t2=vh.cross(t1);
  const float radius=std::sqrt(u),phi=2*pi*q,x=radius*std::cos(phi);
  const float s=.5f*(1+vh[2]),y=(1-s)*std::sqrt(std::max(0.f,1-x*x))+s*radius*std::sin(phi);
  const SbVec3f normal=t1*x+t2*y+vh*std::sqrt(std::max(0.f,1-x*x-y*y));
  return unit(SbVec3f(alpha*normal[0],alpha*normal[1],std::max(0.f,normal[2])));
}
uint64_t layout(CoinEnvironment & e,bool allocate=true) {
  e.sections.truncate(0);e.levels=1;for(uint32_t n=e.size;n>1;n>>=1)++e.levels;
  uint64_t offset=0;
  const auto add=[&](uint32_t kind,uint32_t face,uint32_t level,uint32_t w,uint32_t h,float r,uint64_t bytes) {
    CoinEnvironmentSection s;s.kind=kind;s.face=face;s.level=level;s.width=w;s.height=h;
    s.roughness=r;s.offset=offset;s.bytes=bytes;e.sections.append(s);offset+=bytes;
  };
  for(unsigned face=0;face<6;++face)for(unsigned level=0;level<e.sourceLevels;++level) {
    const unsigned n=std::max(1u,e.size>>level);
    add(e.SOURCE,face,level,n,n,0,uint64_t(n)*n*8);
  }
  for(unsigned face=0;face<6;++face)for(unsigned level=0;level<e.levels;++level) {
    const unsigned n=std::max(1u,e.size>>level);
    add(e.SPECULAR,face,level,n,n,float(level)/float(e.levels-1),uint64_t(n)*n*8);
  }
  add(e.LUT,0,0,e.lutSize,e.lutSize,0,uint64_t(e.lutSize)*e.lutSize*8);
  add(e.IRRADIANCE,0,0,9,1,0,108);
  if(allocate)e.payload=SbByteBuffer(size_t(offset));
  return offset;
}
void color(uint8_t * p,const SbVec3f & c) {
  for(int j=0;j<3;++j)CoinEnvironmentHalf::write16(p+j*2,CoinEnvironmentHalf::toHalf(c[j]));
  CoinEnvironmentHalf::write16(p+6,CoinEnvironmentHalf::toHalf(1));
}
double cubeArea(double u0,double v0,double u1,double v1) {
  const auto area=[](double u,double v) {return std::atan2(u*v,std::sqrt(1+u*u+v*v));};
  return area(u1,v1)-area(u0,v1)-area(u1,v0)+area(u0,v0);
}
void cubeCoordinate(const SbVec3f & n,unsigned & face,double & u,double & v) {
  const double x=std::abs(n[0]),y=std::abs(n[1]),z=std::abs(n[2]);
  if(x>=y&&x>=z) {face=n[0]>=0?0:1;u=(face==0?-n[2]:n[2])/x;v=-n[1]/x;}
  else if(y>=z) {face=n[1]>=0?2:3;u=n[0]/y;v=(face==2?n[2]:-n[2])/y;}
  else {face=n[2]>=0?4:5;u=(face==4?n[0]:-n[0])/z;v=-n[1]/z;}
}
SbVec3f imagePixel(const CoinEnvironmentRadiance & r,unsigned face,unsigned x,unsigned y) {
  float rgba[4];std::memcpy(rgba,r.pixels[face].constData()+(uint64_t(y)*r.dimensions[0]+x)*16,16);
  return SbVec3f(rgba[0],rgba[1],rgba[2]);
}
SbVec3f imageDirection(const CoinEnvironmentRadiance & r,unsigned face,double x,double y) {
  SbVec3f n;
  if(r.projection==CoinEnvironmentRadiance::CUBE)
    n=CoinEnvironment::direction(face,float(2*x/r.dimensions[0]-1),float(2*y/r.dimensions[1]-1));
  else {
    const double phi=(x/r.dimensions[0]-.5)*2*double(pi),theta=y/r.dimensions[1]*double(pi);
    n.setValue(float(std::sin(theta)*std::cos(phi)),float(std::sin(theta)*std::sin(phi)),float(std::cos(theta)));
  }
  SbVec3f world;r.sourceToWorld.multVec(n,world);return world;
}
double imageArea(const CoinEnvironmentRadiance & r,double x0,double y0,double x1,double y1) {
  if(r.projection==CoinEnvironmentRadiance::CUBE)
    return cubeArea(2*x0/r.dimensions[0]-1,2*y0/r.dimensions[1]-1,2*x1/r.dimensions[0]-1,2*y1/r.dimensions[1]-1);
  return 2*double(pi)*(x1-x0)/r.dimensions[0]*(std::cos(y0/r.dimensions[1]*double(pi))-std::cos(y1/r.dimensions[1]*double(pi)));
}
bool checkedColor(uint8_t * p,const SbVec3f & rgb,SbString & error) {
  for(int c=0;c<3;++c)if(!std::isfinite(rgb[c])||rgb[c]<0||rgb[c]>65504) {
    error="Prepared radiance exceeds finite RGBA16F; use an explicit import scale";return false;
  }
  color(p,rgb);return true;
}
void storeFloat(SbByteBuffer & p,uint64_t offset,const SbVec3f & value) {
  const float rgba[]={value[0],value[1],value[2],1};std::memcpy(p.data()+offset,rgba,16);
}
}
constexpr float CoinEnvironment::roughnessFloor;
const uint8_t * CoinEnvironment::data(const CoinEnvironmentSection & s) const {
  return reinterpret_cast<const uint8_t *>(payload.constData())+s.offset;
}
SbVec3f CoinEnvironment::direction(uint32_t face,float u,float v) {
  SbVec3f n;
  switch(face) {
  case 0:n.setValue(1,-v,-u);break;case 1:n.setValue(-1,-v,u);break;
  case 2:n.setValue(u,1,v);break;case 3:n.setValue(u,-1,-v);break;
  case 4:n.setValue(u,-v,1);break;default:n.setValue(-u,-v,-1);break;
  }
  return unit(n);
}
SbVec3f CoinEnvironment::radiance(Source s,const SbVec3f & n) {
  if(s==WHITE)return SbVec3f(1,1,1);
  if(s==AXIS) {
    SbVec3f result=(n+SbVec3f(1,1,1))*.5f;
    for(int c=0;c<3;++c)result[c]=std::max(0.f,std::min(1.f,result[c]));
    return result;
  }
  SbVec3f result(.15f,.15f,.15f);
  static const SbVec3f axes[]={unit(SbVec3f(.65f,-.2f,.74f)),unit(SbVec3f(-.7f,.3f,.64f)),unit(SbVec3f(.1f,.85f,.5f))};
  const SbVec3f colors[]={s==STUDIO?SbVec3f(12,12,12):SbVec3f(18,7,2),
    s==STUDIO?SbVec3f(8,8,8):SbVec3f(2,7,14),s==STUDIO?SbVec3f(5,5,5):SbVec3f(6,11,3)};
  for(int i=0;i<3;++i)result+=colors[i]*std::exp2((n.dot(axes[i])-1)*(i==2?8.f:22.f));
  return result;
}
void CoinEnvironment::basis(const SbVec3f & n,float b[9]) {
  const float x=n[0],y=n[1],z=n[2];
  b[0]=.2820947918f;b[1]=.4886025119f*y;b[2]=.4886025119f*z;b[3]=.4886025119f*x;
  b[4]=1.092548431f*x*y;b[5]=1.092548431f*y*z;b[6]=.3153915653f*(3*z*z-1);
  b[7]=1.092548431f*x*z;b[8]=.5462742153f*(x*x-y*y);
}
SbVec2f CoinEnvironment::dfg(float nv,float roughness,uint32_t samples) {
  const float r=std::max(roughness,roughnessFloor),a=r*r,a2=a*a;
  const SbVec3f v(std::sqrt(std::max(0.f,1-nv*nv)),0,nv);
  const float sv=std::sqrt(a2+(1-a2)*nv*nv),g1=2*nv/(nv+sv);
  double sumA=0,sumB=0;
  for(unsigned i=0;i<samples;++i) {
    const auto h=visibleNormal(v,a,(i+.5f)/samples,reverse(i));
    const float vh=std::max(0.f,v.dot(h));const auto l=h*(2*vh)-v;const float nl=l[2];
    if(nl<=0)continue;
    const float sl=std::sqrt(a2+(1-a2)*nl*nl),g2=2*nv*nl/(nv*sl+nl*sv);
    const float weight=std::min(1.f,g2/g1),fc=std::pow(std::max(0.f,1-vh),5);
    sumA+=weight*(1-fc);sumB+=weight*fc;
  }
  return SbVec2f(float(sumA/samples),float(sumB/samples));
}
bool CoinEnvironment::anisotropicDfg(const SbVec3f & view,float roughness,
  float strength,uint32_t samples,SbVec2f & result,SbString & diagnostic) {
  const float scale=std::max(std::abs(view[0]),std::max(std::abs(view[1]),std::abs(view[2])));
  if(!std::isfinite(view[0])||!std::isfinite(view[1])||!std::isfinite(view[2])||
     !(scale>0)||!(view[2]>0)||!std::isfinite(roughness)||roughness<0||roughness>1||
     !std::isfinite(strength)||strength<0||strength>1||samples<128||samples>1048576) {
    diagnostic="Invalid anisotropic GGX view, roughness, strength or sample count";return false;
  }
  const auto v=unit(view/scale);
  const float alpha=std::pow(std::max(roughness,roughnessFloor),2);
  const float ax=alpha+(1-alpha)*strength*strength,ay=alpha;
  const auto vh=unit(SbVec3f(ax*v[0],ay*v[1],v[2]));
  const float len=vh[0]*vh[0]+vh[1]*vh[1];
  const SbVec3f t1=len>0?SbVec3f(-vh[1],vh[0],0)/std::sqrt(len):SbVec3f(1,0,0);
  const SbVec3f t2=vh.cross(t1);
  const float viewLength=std::sqrt(ax*ax*v[0]*v[0]+ay*ay*v[1]*v[1]+v[2]*v[2]);
  double sumA=0,sumB=0;
  // Heitz 2018 projected-ellipsoid VNDF. Rejected below-horizon directions
  // contribute zero; normalizing by accepted samples would increase energy.
  for(unsigned i=0;i<samples;++i) {
    const float radius=std::sqrt((i+.5f)/samples),phi=2*pi*reverse(i),x=radius*std::cos(phi);
    const float s=.5f*(1+vh[2]),y=(1-s)*std::sqrt(std::max(0.f,1-x*x))+s*radius*std::sin(phi);
    const auto projected=t1*x+t2*y+vh*std::sqrt(std::max(0.f,1-x*x-y*y));
    const auto h=unit(SbVec3f(ax*projected[0],ay*projected[1],std::max(0.f,projected[2])));
    const float voh=std::max(0.f,v.dot(h));const auto l=h*(2*voh)-v;
    if(l[2]<=0)continue;
    const float lightLength=std::sqrt(ax*ax*l[0]*l[0]+ay*ay*l[1]*l[1]+l[2]*l[2]);
    // G2/G1(V), expressed without dividing by small NdotV or NdotL.
    const float weight=l[2]*(v[2]+viewLength)/(v[2]*lightLength+l[2]*viewLength);
    const float fc=std::pow(std::max(0.f,1-voh),5);
    sumA+=weight*(1-fc);sumB+=weight*fc;
  }
  const SbVec2f candidate(float(sumA/samples),float(sumB/samples));
  if(!std::isfinite(candidate[0])||!std::isfinite(candidate[1])||
     candidate[0]<0||candidate[1]<0||candidate[0]+candidate[1]>1.00001f) {
    diagnostic="Anisotropic GGX directional albedo is outside its energy domain";return false;
  }
  result=candidate;diagnostic="";return true;
}
float CoinEnvironment::diffuseFresnel(float nv,uint32_t samples) {
  const SbVec3f v(std::sqrt(std::max(0.f,1-nv*nv)),0,nv);double sum=0;
  for(unsigned i=0;i<samples;++i) {
    const float u=(i+.5f)/samples,phi=2*pi*reverse(i),r=std::sqrt(u);
    const SbVec3f l(r*std::cos(phi),r*std::sin(phi),std::sqrt(1-u));
    sum+=std::pow(std::max(0.f,1-v.dot(unit(v+l))),5);
  }
  return float(sum/samples);
}
bool CoinEnvironment::bake(Source source,uint32_t cube,uint32_t lut,uint32_t filterCount,
                                   uint32_t lutCount,CoinEnvironment & result,SbString & error) {
  if(source>AXIS||!power(cube,512)||!power(lut,512)||filterCount<32||filterCount>4096||lutCount<128||lutCount>16384) {
    error="Invalid PBR bake dimensions, source or sample counts";return false;
  }
  try {
    CoinEnvironment e;e.size=cube;e.lutSize=lut;e.source=source;
    e.filterSamples=filterCount;e.lutSamples=lutCount;e.shSamples=65536;layout(e);
    if(e.payload.size()>budget) {error="PBR environment exceeds 64 MiB payload budget";return false;}
    uint8_t * bytes=reinterpret_cast<uint8_t *>(e.payload.data());
    for(int sectionIndex=0;sectionIndex<e.sections.getLength();++sectionIndex) {
      const auto & section=e.sections[sectionIndex];
      if(section.kind!=SOURCE&&section.kind!=SPECULAR)continue;
      SbList<SbVec3f> halfVectors;
      if(section.kind==SPECULAR) {
        const unsigned count=section.level==0?std::min(64u,filterCount):filterCount;
        const float r=std::max(section.roughness,roughnessFloor),a2=r*r*r*r;
        for(unsigned i=0;i<count;++i) {
          const float u=(i+.5f)/count,phi=2*pi*reverse(i),z=std::sqrt((1-u)/(1+(a2-1)*u)),s=std::sqrt(std::max(0.f,1-z*z));
          halfVectors.append(SbVec3f(s*std::cos(phi),s*std::sin(phi),z));
        }
      }
      for(unsigned y=0;y<section.height;++y)for(unsigned x=0;x<section.width;++x) {
        const auto n=direction(section.face,2*(x+.5f)/section.width-1,2*(y+.5f)/section.height-1);
        SbVec3f value=radiance(source,n);
        if(section.kind==SPECULAR) {
          const auto tangent=unit((std::abs(n[2])<.99f?SbVec3f(0,0,1):SbVec3f(0,1,0)).cross(n));
          const auto bitangent=n.cross(tangent);value.setValue(0,0,0);double weight=0;
          for(int sampleIndex=0;sampleIndex<halfVectors.getLength();++sampleIndex) {
            const auto & sample=halfVectors[sampleIndex];
            const auto h=tangent*sample[0]+bitangent*sample[1]+n*sample[2];
            const auto l=h*(2*n.dot(h))-n;const float nl=n.dot(l);
            if(nl>0) {value+=radiance(source,l)*nl;weight+=nl;}
          }
          value/=float(weight);
        }
        color(bytes+section.offset+(uint64_t(y)*section.width+x)*8,value);
      }
    }
    SbList<float> diffuse;
    for(unsigned x=0;x<lut;++x)diffuse.append(diffuseFresnel((x+.5f)/lut,lutCount));
    const auto & table=e.sections[e.sections.getLength()-2];
    for(unsigned y=0;y<lut;++y) {
      const float roughness=(y+.5f)/lut;SbList<SbVec2f> row;double average=0;
      for(unsigned x=0;x<lut;++x) {
        const float nv=(x+.5f)/lut;const auto value=dfg(nv,roughness,lutCount);row.append(value);
        average+=2*nv*(value[0]+value[1])/lut;
      }
      for(unsigned x=0;x<lut;++x) {
        const float values[]={row[x][0],row[x][1],diffuse[x],float(average)};
        for(int c=0;c<4;++c)CoinEnvironmentHalf::write16(bytes+table.offset+(uint64_t(y)*lut+x)*8+c*2,
          CoinEnvironmentHalf::toHalf(values[c]));
      }
    }
    double coefficients[9][3]={};
    for(unsigned i=0;i<e.shSamples;++i) {
      const float z=1-2*(i+.5f)/e.shSamples,phi=2*pi*reverse(i),s=std::sqrt(std::max(0.f,1-z*z));
      const SbVec3f n(s*std::cos(phi),s*std::sin(phi),z);float b[9];basis(n,b);const auto rgb=radiance(source,n);
      for(int j=0;j<9;++j)for(int c=0;c<3;++c)coefficients[j][c]+=double(rgb[c])*b[j];
    }
    const auto & sh=e.sections[e.sections.getLength()-1];
    for(int j=0;j<9;++j)for(int c=0;c<3;++c) {
      const float convolution=j==0?pi:j<4?2*pi/3:pi/4;
      e.irradiance[j][c]=float(coefficients[j][c]*(4*double(pi)/e.shSamples)*convolution);
      putFloat(bytes+sh.offset+(j*3+c)*4,e.irradiance[j][c]);
    }
    e.identity=hash(e.payload);result=e;error.makeEmpty();return true;
  }catch(const std::bad_alloc&) {error="PBR bake allocation failed";return false;}
}
bool CoinEnvironment::build(const CoinEnvironmentRadiance & input,
  const CoinEnvironmentBuildOptions & options,CoinEnvironment & result,SbString & error,Source provenance) {
  return buildPrepared(input,options,result,error,provenance,nullptr);
}
bool CoinEnvironment::buildPrepared(const CoinEnvironmentRadiance & input,
  const CoinEnvironmentBuildOptions & options,CoinEnvironment & result,SbString & error,Source provenance,
  const CoinEnvironment * common) {
  if((provenance!=IMPORTED&&provenance!=CAPTURED)||!power(options.faceSize,512)||
     !power(options.lutSize,512)||options.filterSamples<32||options.filterSamples>4096||
     options.lutSamples<128||options.lutSamples>16384||options.shSamples<256||options.shSamples>1048576) {
    error="Invalid environment build provenance, resolution or sample counts";return false;
  }
  if(!input.validate(error,options.workingBudgetBytes))return false;
  try {
    CoinEnvironment e;e.bundleVersion=2;e.source=provenance;e.size=options.faceSize;e.lutSize=options.lutSize;
    e.filterSamples=options.filterSamples;e.lutSamples=options.lutSamples;
    e.sourceLevels=1;for(uint32_t n=e.size;n>1;n>>=1)++e.sourceLevels;
    const uint64_t payloadBytes=layout(e,false),pixels=uint64_t(e.size)*e.size*6;
    // Peak: input, final payload, base float cube, conservative splat accumulator
    // and bounded LUT/descriptor scratch. Later float mip chains fit below this.
    const uint64_t work=payloadBytes+pixels*(16+32)+65536+uint64_t(e.lutSize)*32;
    if(payloadBytes>budget||payloadBytes>options.payloadBudgetBytes||work>options.workingBudgetBytes||
       input.memoryBytes()>options.workingBudgetBytes-work) {
      error="Environment preparation exceeds its payload or working budget";return false;
    }
    layout(e);auto * bytes=reinterpret_cast<uint8_t *>(e.payload.data());
    std::vector<CoinEnvironmentRadiance> mips(e.sourceLevels);
    auto & base=mips[0];base.projection=CoinEnvironmentRadiance::CUBE;base.dimensions=SbVec2i32(e.size,e.size);
    for(auto & face:base.pixels)face=SbByteBuffer(size_t(e.size)*e.size*16);
    struct Accum {double rgb[3]={};double weight=0;};
    std::vector<Accum> accumulator(static_cast<size_t>(pixels));double total[3]={};
    const unsigned faces=input.projection==CoinEnvironmentRadiance::CUBE?6:1;
    const unsigned width=input.dimensions[0],height=input.dimensions[1];
    for(unsigned face=0;face<faces;++face)for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
      const auto rgb=imagePixel(input,face,x,y);const double weight=imageArea(input,x,y,x+1,y+1);
      unsigned destination;double u,v;cubeCoordinate(imageDirection(input,face,x+.5,y+.5),destination,u,v);
      const unsigned dx=std::min(e.size-1,unsigned(std::max(0.,(u+1)*.5*e.size)));
      const unsigned dy=std::min(e.size-1,unsigned(std::max(0.,(v+1)*.5*e.size)));
      auto & a=accumulator[(uint64_t(destination)*e.size+dy)*e.size+dx];a.weight+=weight;
      for(int c=0;c<3;++c) {a.rgb[c]+=rgb[c]*weight;total[c]+=rgb[c]*weight;}
    }
    // Conservative splatting keeps even a sub-texel bright source. Interpolation
    // fills upsampling holes. Normalize the spherical integral, not display RGB.
    for(unsigned face=0;face<6;++face)for(unsigned y=0;y<e.size;++y)for(unsigned x=0;x<e.size;++x) {
      const auto & a=accumulator[(uint64_t(face)*e.size+y)*e.size+x];SbVec3f rgb;
      if(a.weight>0)for(int c=0;c<3;++c)rgb[c]=float(a.rgb[c]/a.weight);
      else rgb=input.sample(direction(face,2*(x+.5f)/e.size-1,2*(y+.5f)/e.size-1));
      storeFloat(base.pixels[face],(uint64_t(y)*e.size+x)*16,rgb);
    }
    std::vector<Accum>().swap(accumulator);
    const auto conserve=[&](CoinEnvironmentRadiance & image) {
      const unsigned size=image.dimensions[0];double integral[3]={};
      for(unsigned face=0;face<6;++face)for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x) {
        const auto rgb=imagePixel(image,face,x,y);const double weight=imageArea(image,x,y,x+1,y+1);
        for(int c=0;c<3;++c)integral[c]+=rgb[c]*weight;
      }
      for(unsigned face=0;face<6;++face)for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x) {
        auto rgb=imagePixel(image,face,x,y);
        for(int c=0;c<3;++c)rgb[c]=float(integral[c]>0?rgb[c]*total[c]/integral[c]:0);
        storeFloat(image.pixels[face],(uint64_t(y)*size+x)*16,rgb);
      }
    };
    conserve(base);
    for(unsigned level=1;level<e.sourceLevels;++level) {
      auto & mip=mips[level];const auto & previous=mips[level-1];const unsigned size=std::max(1u,e.size>>level);
      mip.projection=CoinEnvironmentRadiance::CUBE;mip.dimensions=SbVec2i32(size,size);
      for(auto & face:mip.pixels)face=SbByteBuffer(size_t(size)*size*16);
      for(unsigned face=0;face<6;++face)for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x) {
        double sum[3]={},weightSum=0;
        for(unsigned sy=0;sy<2;++sy)for(unsigned sx=0;sx<2;++sx) {
          const double x0=x+sx*.5,y0=y+sy*.5,weight=imageArea(mip,x0,y0,x0+.5,y0+.5);
          const auto rgb=previous.sample(direction(face,float(2*(x0+.25)/size-1),float(2*(y0+.25)/size-1)));
          for(int c=0;c<3;++c)sum[c]+=rgb[c]*weight;weightSum+=weight;
        }
        storeFloat(mip.pixels[face],(uint64_t(y)*size+x)*16,SbVec3f(float(sum[0]/weightSum),float(sum[1]/weightSum),float(sum[2]/weightSum)));
      }
      conserve(mip);
    }
    for(int index=0;index<e.sections.getLength();++index) {
      const auto & section=e.sections[index];if(section.kind!=SOURCE&&section.kind!=SPECULAR)continue;
      SbList<SbVec3f> halfVectors;
      const unsigned count=section.level==0?std::min(64u,e.filterSamples):e.filterSamples;
      const float r=std::max(section.roughness,roughnessFloor),a2=r*r*r*r;
      if(section.kind==SPECULAR)for(unsigned i=0;i<count;++i) {
        const float u=(i+.5f)/count,phi=2*pi*reverse(i),z=std::sqrt((1-u)/(1+(a2-1)*u)),s=std::sqrt(std::max(0.f,1-z*z));
        halfVectors.append(SbVec3f(s*std::cos(phi),s*std::sin(phi),z));
      }
      for(unsigned y=0;y<section.height;++y)for(unsigned x=0;x<section.width;++x) {
        SbVec3f value;
        if(section.kind==SOURCE)value=imagePixel(mips[section.level],section.face,x,y);
        else {
          const auto n=direction(section.face,2*(x+.5f)/section.width-1,2*(y+.5f)/section.height-1);
          const auto tangent=unit((std::abs(n[2])<.99f?SbVec3f(0,0,1):SbVec3f(0,1,0)).cross(n));
          const auto bitangent=n.cross(tangent);double sum[3]={},weight=0;
          for(int i=0;i<halfVectors.getLength();++i) {
            const auto & sample=halfVectors[i];const auto h=tangent*sample[0]+bitangent*sample[1]+n*sample[2];
            const auto l=h*(2*n.dot(h))-n;const float nl=n.dot(l);if(nl<=0)continue;
            const double denominator=sample[2]*sample[2]*(a2-1)+1;
            const double pdf=a2/(4*double(pi)*denominator*denominator);
            unsigned face;double u,v;cubeCoordinate(l,face,u,v);
            const unsigned ix=std::min(e.size-1,unsigned(std::max(0.,(u+1)*.5*e.size)));
            const unsigned iy=std::min(e.size-1,unsigned(std::max(0.,(v+1)*.5*e.size)));
            const double texelArea=imageArea(base,ix,iy,ix+1,iy+1);
            const float lod=float(std::max(0.,std::min(double(e.sourceLevels-1),.5*std::log2(1/(count*pdf*texelArea)))));
            const unsigned lo=unsigned(lod),hi=std::min(e.sourceLevels-1,lo+1);const float blend=lod-lo;
            const auto rgb=mips[lo].sample(l)*(1-blend)+mips[hi].sample(l)*blend;
            for(int c=0;c<3;++c)sum[c]+=rgb[c]*nl;weight+=nl;
          }
          value=SbVec3f(float(sum[0]/weight),float(sum[1]/weight),float(sum[2]/weight));
        }
        if(!checkedColor(bytes+section.offset+(uint64_t(y)*section.width+x)*8,value,error))return false;
      }
    }
    if(common) {
      const auto & table=e.sections[e.sections.getLength()-2];
      const auto & oldTable=common->sections[common->sections.getLength()-2];
      const auto & sh=e.sections[e.sections.getLength()-1];
      const auto & oldSh=common->sections[common->sections.getLength()-1];
      std::memcpy(bytes+table.offset,common->data(oldTable),size_t(table.bytes));
      std::memcpy(bytes+sh.offset,common->data(oldSh),size_t(sh.bytes));
      std::memcpy(e.irradiance,common->irradiance,sizeof(e.irradiance));e.shSamples=common->shSamples;
    } else {
    const auto & table=e.sections[e.sections.getLength()-2];SbList<float> diffuse;
    for(unsigned x=0;x<e.lutSize;++x)diffuse.append(diffuseFresnel((x+.5f)/e.lutSize,e.lutSamples));
    for(unsigned y=0;y<e.lutSize;++y) {
      const float roughness=(y+.5f)/e.lutSize;SbList<SbVec2f> row;double average=0;
      for(unsigned x=0;x<e.lutSize;++x) {
        const float nv=(x+.5f)/e.lutSize;const auto value=dfg(nv,roughness,e.lutSamples);row.append(value);
        average+=2*nv*(value[0]+value[1])/e.lutSize;
      }
      for(unsigned x=0;x<e.lutSize;++x) {
        const float values[]={row[x][0],row[x][1],diffuse[x],float(average)};
        for(int c=0;c<4;++c)CoinEnvironmentHalf::write16(bytes+table.offset+(uint64_t(y)*e.lutSize+x)*8+c*2,CoinEnvironmentHalf::toHalf(values[c]));
      }
    }
    // Integrate every source pixel with its exact solid angle, so a bright
    // source is not missed by uniform random directions. A constant control
    // variate makes white-furnace SH exact even on coarse panorama grids.
    const uint64_t inputPixels=uint64_t(width)*height*faces;
    const unsigned subdivision=std::max(1u,unsigned(std::ceil(std::sqrt(double(options.shSamples)/inputPixels))));
    const uint64_t shCount=inputPixels*subdivision*subdivision;
    if(shCount>67108864) {error="SH quadrature exceeds its bounded work profile";return false;}
    e.shSamples=uint32_t(shCount);double coefficients[9][3]={};
    const double sphere=4*double(pi);double mean[3];for(int c=0;c<3;++c)mean[c]=total[c]/sphere;
    for(unsigned face=0;face<faces;++face)for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
      const auto rgb=imagePixel(input,face,x,y);
      for(unsigned sy=0;sy<subdivision;++sy)for(unsigned sx=0;sx<subdivision;++sx) {
        const double x0=x+double(sx)/subdivision,y0=y+double(sy)/subdivision;
        const double x1=x+double(sx+1)/subdivision,y1=y+double(sy+1)/subdivision;
        float b[9];basis(imageDirection(input,face,(x0+x1)*.5,(y0+y1)*.5),b);
        const double weight=imageArea(input,x0,y0,x1,y1);
        for(int j=0;j<9;++j)for(int c=0;c<3;++c)coefficients[j][c]+=(rgb[c]-mean[c])*b[j]*weight;
      }
    }
    for(int c=0;c<3;++c)coefficients[0][c]+=mean[c]*sphere*.2820947918;
    const auto & sh=e.sections[e.sections.getLength()-1];
    for(int j=0;j<9;++j)for(int c=0;c<3;++c) {
      const double convolution=j==0?double(pi):j<4?2*double(pi)/3:double(pi)/4;
      e.irradiance[j][c]=float(coefficients[j][c]*convolution);
      putFloat(bytes+sh.offset+(j*3+c)*4,e.irradiance[j][c]);
    }
    }
    e.identity=hash(e.payload);if(!e.validate(error))return false;
    result=e;error.makeEmpty();return true;
  } catch(const std::bad_alloc&) {error="Environment preparation allocation failed";return false;}
}
bool CoinEnvironment::prepareVariants(const CoinEnvironmentRadiance & input,
  const CoinEnvironmentBuildOptions & options,uint32_t minimum,SbList<CoinEnvironment> & result,
  SbString & error,Source provenance) {
  if(!power(minimum,512)||!power(options.faceSize,512)||minimum>options.faceSize||!power(options.lutSize,512)) {
    error="Environment variants require power-of-two resolutions in 8..512";return false;
  }
  uint64_t total=0,retained=0;
  for(int i=0;i<result.getLength();++i)retained+=result[i].payload.size();
  for(uint32_t size=minimum;size<=options.faceSize;size*=2) {
    CoinEnvironment description;description.size=size;description.lutSize=options.lutSize;
    description.sourceLevels=1;for(uint32_t n=size;n>1;n>>=1)++description.sourceLevels;
    total+=layout(description,false);
  }
  if(total>options.payloadBudgetBytes||total>budget||retained>=options.workingBudgetBytes) {
    error="Complete environment variant set exceeds its payload or working budget";return false;
  }
  try {
    SbList<CoinEnvironment> candidate;uint64_t prepared=0;
    for(uint32_t size=minimum;size<=options.faceSize;size*=2) {
      if(retained+prepared>=options.workingBudgetBytes) {error="Variant candidates exceed the working budget";return false;}
      auto variantOptions=options;variantOptions.faceSize=size;
      variantOptions.workingBudgetBytes-=retained+prepared;
      CoinEnvironment variant;
      const auto * common=candidate.getLength()?&candidate[0]:nullptr;
      if(!buildPrepared(input,variantOptions,variant,error,provenance,common))return false;
      prepared+=variant.payload.size();candidate.append(variant);
    }
    result=candidate;error.makeEmpty();return true;
  }catch(const std::bad_alloc&) {error="Cannot allocate prepared environment variants";return false;}
}

bool CoinEnvironment::bakeFiltered(Source source,const CoinEnvironmentBuildOptions & options,
  CoinEnvironment & result,SbString & error) {
  if(source>AXIS||!power(options.faceSize,512)) {error="Invalid procedural filtered source";return false;}
  try {
    CoinEnvironmentRadiance r;r.projection=CoinEnvironmentRadiance::CUBE;
    r.dimensions=SbVec2i32(options.faceSize,options.faceSize);
    const uint64_t bytes=uint64_t(options.faceSize)*options.faceSize*6*16;
    if(bytes>options.workingBudgetBytes) {error="Procedural source exceeds the working budget";return false;}
    for(unsigned face=0;face<6;++face) {
      r.pixels[face]=SbByteBuffer(size_t(options.faceSize)*options.faceSize*16);
      for(unsigned y=0;y<options.faceSize;++y)for(unsigned x=0;x<options.faceSize;++x)
        storeFloat(r.pixels[face],(uint64_t(y)*options.faceSize+x)*16,
          radiance(source,direction(face,2*(x+.5f)/options.faceSize-1,2*(y+.5f)/options.faceSize-1)));
    }
    CoinEnvironment candidate;if(!build(r,options,candidate,error))return false;
    candidate.source=source;result=candidate;return true;
  } catch(const std::bad_alloc&) {error="Procedural source allocation failed";return false;}
}
uint64_t CoinEnvironment::contentIdentity() const {
  // Canonical little-endian interpretation followed by the immutable bytes.
  uint64_t value=UINT64_C(14695981039346656037);
  const auto append=[&](const uint8_t * p,size_t n) {
    for(size_t i=0;i<n;++i) {value^=p[i];value*=UINT64_C(1099511628211);}
  };
  const uint32_t fields[]={bundleVersion,model,orientation,source,size,levels,lutSize,
    filterSamples,lutSamples,shSamples,uint32_t(sections.getLength()),sourceLevels};
  uint8_t bytes[40]={};
  for(uint32_t field:fields) {put32(bytes,field);append(bytes,4);}
  putFloat(bytes,roughnessFloor);append(bytes,4);
  for(int i=0;i<sections.getLength();++i) {
    const auto & s=sections[i];const uint32_t fields[]={s.kind,s.face,s.level,s.width,s.height};
    for(int j=0;j<5;++j)put32(bytes+j*4,fields[j]);
    putFloat(bytes+20,s.roughness);put64(bytes+24,s.offset);put64(bytes+32,s.bytes);
    append(bytes,40);
  }
  append(reinterpret_cast<const uint8_t *>(payload.constData()),payload.size());
  return value;
}
bool CoinEnvironment::validate(SbString & error) const {
  if((bundleVersion!=1&&bundleVersion!=2)||source>(bundleVersion==1?AXIS:CAPTURED)||
     sourceLevels!=(bundleVersion==1?1:levels)||!power(size,512)||!power(lutSize,512)||filterSamples<32||
     filterSamples>4096||lutSamples<128||lutSamples>16384||
     (bundleVersion==1?shSamples!=65536:(shSamples<256||shSamples>67108864))||
     payload.empty()||payload.size()>budget||!identity||identity!=hash(payload)) {
    error="Invalid environment dimensions, samples, budget or payload checksum";return false;
  }
  try {
    CoinEnvironment expected;expected.size=size;expected.lutSize=lutSize;expected.sourceLevels=sourceLevels;
    const uint64_t expectedBytes=layout(expected,false);
    if(levels!=expected.levels||sections.getLength()!=expected.sections.getLength()||
       payload.size()!=expectedBytes) {
      error="Invalid environment face/level counts or payload size";return false;
    }
    for(int i=0;i<sections.getLength();++i) {
      const auto & s=sections[i];const auto & t=expected.sections[i];
      if(s.kind!=t.kind||s.face!=t.face||s.level!=t.level||s.width!=t.width||
         s.height!=t.height||s.roughness!=t.roughness||s.offset!=t.offset||s.bytes!=t.bytes) {
        error="Invalid environment section layout";return false;
      }
      const uint8_t * p=data(s);
      if(s.kind==IRRADIANCE) {
        for(int j=0;j<9;++j)for(int c=0;c<3;++c) {
          const float value=getFloat(p+(j*3+c)*4);
          if(!std::isfinite(value)||irradiance[j][c]!=value) {
            error="Invalid or inconsistent environment irradiance";return false;
          }
        }
      } else for(uint64_t pixel=0;pixel<s.bytes/8;++pixel) {
        float v[4];for(int c=0;c<4;++c)v[c]=CoinEnvironmentHalf::fromHalf(CoinEnvironmentHalf::read16(p+pixel*8+c*2));
        for(float x:v)if(!std::isfinite(x)||x<0) {error="Invalid environment radiance or LUT domain";return false;}
        if(s.kind==LUT) {
          if(v[0]+v[1]>1.002f||v[2]>1||v[3]>1) {error="Environment LUT violates its energy domain";return false;}
        } else if(v[3]!=1) {error="Environment source and prefilter must be opaque";return false;}
      }
    }
    error.makeEmpty();return true;
  } catch(const std::bad_alloc&) {error="Environment validation allocation failed";return false;}
}
bool CoinEnvironment::write(const SbString & file,SbString & error) const {
  if(!validate(error))return false;
  std::string pattern=std::string(file.getString())+".tmp-XXXXXX";
  std::vector<char> temporary(pattern.begin(),pattern.end());temporary.push_back(0);
#ifdef _WIN32
  int fd=-1;
  if(_mktemp_s(temporary.data(),temporary.size())==0)
    fd=_open(temporary.data(),_O_CREAT|_O_EXCL|_O_BINARY|_O_WRONLY,_S_IREAD|_S_IWRITE);
  FILE * output=fd<0?nullptr:_fdopen(fd,"wb");
#else
  const int fd=mkstemp(temporary.data());FILE * output=fd<0?nullptr:fdopen(fd,"wb");
#endif
  if(!output) {
    if(fd>=0) {
#ifdef _WIN32
      _close(fd);
#else
      close(fd);
#endif
      std::remove(temporary.data());
    }
    error="Could not create a candidate PBR bundle";return false;
  }
  uint8_t header[96]={};std::memcpy(header,bundleVersion==1?"COINIBL1":"COINIBL2",8);
  const uint32_t fields[]={bundleVersion,model,orientation,uint32_t(sections.getLength()),source,size,levels,lutSize,filterSamples,lutSamples,shSamples};
  for(int i=0;i<11;++i)put32(header+8+i*4,fields[i]);
  putFloat(header+52,roughnessFloor);put64(header+56,payload.size());put64(header+64,identity);
  if(bundleVersion==2) {
    put32(header+72,sourceLevels);put32(header+76,CoinEnvironmentRadiance::LINEAR_SRGB);
    put64(header+80,contentIdentity());put32(header+92,96);
  }
  const size_t headerSize=bundleVersion==1?72:96;
  bool ok=std::fwrite(header,1,headerSize,output)==headerSize;
  for(int sectionIndex=0;sectionIndex<sections.getLength();++sectionIndex) {
    const auto & s=sections[sectionIndex];
    uint8_t descriptor[40]={};const uint32_t f[]={s.kind,s.face,s.level,s.width,s.height};
    for(int i=0;i<5;++i)put32(descriptor+i*4,f[i]);putFloat(descriptor+20,s.roughness);
    put64(descriptor+24,s.offset);put64(descriptor+32,s.bytes);
    ok=ok&&std::fwrite(descriptor,1,sizeof(descriptor),output)==sizeof(descriptor);
  }
  ok=ok&&std::fwrite(payload.constData(),1,payload.size(),output)==payload.size();
  ok=std::fclose(output)==0&&ok;
  CoinEnvironment verified;
  if(ok)ok=verified.read(temporary.data(),error)&&verified.contentIdentity()==contentIdentity();
  if(ok) {
#ifdef _WIN32
    ok=MoveFileExA(temporary.data(),file.getString(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    ok=std::rename(temporary.data(),file.getString())==0;
#endif
  }
  if(!ok) {std::remove(temporary.data());if(!error.getLength())error="Could not verify or publish PBR bundle";}
  else error.makeEmpty();return ok;
}
bool CoinEnvironment::read(const SbString & file,SbString & error) {
  return this->read(file,error,UINT64_MAX);
}
bool CoinEnvironment::read(const SbString & file,SbString & error,uint64_t candidateBudgetBytes) {
  FILE * input=std::fopen(file.getString(),"rb");if(!input) {error="Could not open PBR environment bundle";return false;}
  struct Close {FILE * f;~Close(){std::fclose(f);}} close{input};
  uint8_t h[96]={};if(std::fread(h,1,72,input)!=72||
    (std::memcmp(h,"COINIBL1",8)&&std::memcmp(h,"COINIBL2",8))||
    (get32(h+8)!=1&&get32(h+8)!=2)||h[7]!=char('0'+get32(h+8))||
    get32(h+12)!=model||get32(h+16)!=orientation||getFloat(h+52)!=roughnessFloor) {
    error="Unsupported PBR bundle header, model or orientation";return false;
  }
  try {
    CoinEnvironment e;e.bundleVersion=get32(h+8);e.source=get32(h+24);e.size=get32(h+28);e.lutSize=get32(h+36);
    if(e.bundleVersion==2) {
      if(std::fread(h+72,1,24,input)!=24||get32(h+76)!=CoinEnvironmentRadiance::LINEAR_SRGB||
         get32(h+88)!=0||get32(h+92)!=96) {error="Unsupported or truncated v2 environment metadata";return false;}
      e.sourceLevels=get32(h+72);
    }
    e.filterSamples=get32(h+40);e.lutSamples=get32(h+44);e.shSamples=get32(h+48);
    uint32_t expectedLevels=1;for(uint32_t n=e.size;n>1;n>>=1)++expectedLevels;
    if(e.source>(e.bundleVersion==1?AXIS:CAPTURED)||e.sourceLevels!=(e.bundleVersion==1?1:expectedLevels)||
      !power(e.size,512)||!power(e.lutSize,512)||get64(h+56)>budget||
      e.filterSamples<32||e.filterSamples>4096||e.lutSamples<128||e.lutSamples>16384||
      (e.bundleVersion==1?e.shSamples!=65536:(e.shSamples<256||e.shSamples>67108864))) {
      error="PBR bundle dimensions, samples or budget are invalid";return false;
    }
    // Expected layout is computed before reading descriptors or GPU allocation.
    const uint64_t payloadBytes=layout(e,false);
    if(payloadBytes+uint64_t(e.sections.getLength())*sizeof(CoinEnvironmentSection)+sizeof(CoinEnvironment)>candidateBudgetBytes) {
      error="PBR bundle candidate exceeds the CPU snapshot budget";return false;
    }
    if(get32(h+20)!=uint32_t(e.sections.getLength())||get32(h+32)!=e.levels||get64(h+56)!=payloadBytes) {
      error="PBR bundle face/level counts or payload size mismatch";return false;
    }
    for(int sectionIndex=0;sectionIndex<e.sections.getLength();++sectionIndex) {
      const auto & s=e.sections[sectionIndex];
      uint8_t p[40];if(std::fread(p,1,40,input)!=40||get32(p)!=s.kind||get32(p+4)!=s.face||get32(p+8)!=s.level||
        get32(p+12)!=s.width||get32(p+16)!=s.height||getFloat(p+20)!=s.roughness||get64(p+24)!=s.offset||get64(p+32)!=s.bytes) {
        error="PBR bundle has missing, reordered or invalid face/level descriptors";return false;
      }
    }
    e.payload=SbByteBuffer(size_t(payloadBytes));
    if(std::fread(e.payload.data(),1,e.payload.size(),input)!=e.payload.size()||std::fgetc(input)!=EOF) {
      error="PBR bundle is truncated or has trailing bytes";return false;
    }
    e.identity=hash(e.payload);if(e.identity!=get64(h+64)) {error="PBR bundle content identity mismatch";return false;}
    for(int sectionIndex=0;sectionIndex<e.sections.getLength();++sectionIndex) {
      const auto & s=e.sections[sectionIndex];
      const auto * p=e.data(s);
      if(s.kind==IRRADIANCE) {
        for(int j=0;j<9;++j)for(int c=0;c<3;++c) {
          const float value=getFloat(p+(j*3+c)*4);if(!std::isfinite(value)) {error="Nonfinite PBR SH coefficient";return false;}
          e.irradiance[j][c]=value;
        }
      }else for(uint64_t pixel=0;pixel<s.bytes/8;++pixel) {
        float values[4];for(int c=0;c<4;++c)values[c]=CoinEnvironmentHalf::fromHalf(CoinEnvironmentHalf::read16(p+pixel*8+c*2));
        for(float value:values)if(!std::isfinite(value)||value<0) {error="Invalid PBR half-float payload";return false;}
        if(s.kind==LUT) {
          if(values[0]+values[1]>1.002f||values[2]>1||values[3]>1) {error="PBR LUT violates its energy domain";return false;}
        }else if(values[3]!=1) {error="PBR cubemap must be opaque";return false;}
      }
    }
    if(e.bundleVersion==2&&e.contentIdentity()!=get64(h+80)) {error="Environment metadata/content identity mismatch";return false;}
    *this=e;error.makeEmpty();return true;
  }catch(const std::bad_alloc&) {error="PBR environment allocation failed";return false;}
}

SbByteBuffer CoinEnvironment::albedoAverage() const {
  SbByteBuffer result(size_t(lutSize)*8);
  CoinEnvironmentSection table;
  for(int i=0;i<sections.getLength();++i)if(sections[i].kind==LUT)table=sections[i];
  if(table.kind!=LUT)return SbByteBuffer();
  auto * dst=reinterpret_cast<uint8_t *>(result.data());
  const auto * src=data(table);
  for(uint32_t y=0;y<lutSize;++y) {
    double a=0,b=0;
    for(uint32_t x=0;x<lutSize;++x) {
      const auto * pixel=src+(uint64_t(y)*lutSize+x)*8;
      const double weight=2*(x+.5)/double(lutSize*lutSize);
      a+=CoinEnvironmentHalf::fromHalf(CoinEnvironmentHalf::read16(pixel))*weight;
      b+=CoinEnvironmentHalf::fromHalf(CoinEnvironmentHalf::read16(pixel+2))*weight;
    }
    const float values[]={float(a),float(b),float(a+b),1};
    for(int c=0;c<4;++c)CoinEnvironmentHalf::write16(dst+y*8+c*2,CoinEnvironmentHalf::toHalf(values[c]));
  }
  return result;
}
