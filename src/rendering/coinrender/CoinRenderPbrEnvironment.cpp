#include "CoinRenderPbrEnvironment.h"
#include "CoinRenderTextureFormatCore.h"
#include <Inventor/C/base/time.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
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
void layout(CoinRenderPbrEnvironment & e) {
  e.sections.truncate(0);e.levels=1;for(uint32_t n=e.size;n>1;n>>=1)++e.levels;
  uint64_t offset=0;
  const auto add=[&](uint32_t kind,uint32_t face,uint32_t level,uint32_t w,uint32_t h,float r,uint64_t bytes) {
    CoinRenderPbrEnvironmentSection s;s.kind=kind;s.face=face;s.level=level;s.width=w;s.height=h;
    s.roughness=r;s.offset=offset;s.bytes=bytes;e.sections.append(s);offset+=bytes;
  };
  for(unsigned face=0;face<6;++face)add(e.SOURCE,face,0,e.size,e.size,0,uint64_t(e.size)*e.size*8);
  for(unsigned face=0;face<6;++face)for(unsigned level=0;level<e.levels;++level) {
    const unsigned n=std::max(1u,e.size>>level);
    add(e.SPECULAR,face,level,n,n,float(level)/float(e.levels-1),uint64_t(n)*n*8);
  }
  add(e.LUT,0,0,e.lutSize,e.lutSize,0,uint64_t(e.lutSize)*e.lutSize*8);
  add(e.IRRADIANCE,0,0,9,1,0,108);
  e.payload=SbByteBuffer(size_t(offset));
}
void color(uint8_t * p,const SbVec3f & c) {
  for(int j=0;j<3;++j)CoinRenderTextureFormatCore::write16(p+j*2,CoinRenderTextureFormatCore::toHalf(c[j]));
  CoinRenderTextureFormatCore::write16(p+6,CoinRenderTextureFormatCore::toHalf(1));
}
}
constexpr float CoinRenderPbrEnvironment::roughnessFloor;
const uint8_t * CoinRenderPbrEnvironment::data(const CoinRenderPbrEnvironmentSection & s) const {
  return reinterpret_cast<const uint8_t *>(payload.constData())+s.offset;
}
SbVec3f CoinRenderPbrEnvironment::direction(uint32_t face,float u,float v) {
  SbVec3f n;
  switch(face) {
  case 0:n.setValue(1,-v,-u);break;case 1:n.setValue(-1,-v,u);break;
  case 2:n.setValue(u,1,v);break;case 3:n.setValue(u,-1,-v);break;
  case 4:n.setValue(u,-v,1);break;default:n.setValue(-u,-v,-1);break;
  }
  return unit(n);
}
SbVec3f CoinRenderPbrEnvironment::radiance(Source s,const SbVec3f & n) {
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
void CoinRenderPbrEnvironment::basis(const SbVec3f & n,float b[9]) {
  const float x=n[0],y=n[1],z=n[2];
  b[0]=.2820947918f;b[1]=.4886025119f*y;b[2]=.4886025119f*z;b[3]=.4886025119f*x;
  b[4]=1.092548431f*x*y;b[5]=1.092548431f*y*z;b[6]=.3153915653f*(3*z*z-1);
  b[7]=1.092548431f*x*z;b[8]=.5462742153f*(x*x-y*y);
}
SbVec2f CoinRenderPbrEnvironment::dfg(float nv,float roughness,uint32_t samples) {
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
float CoinRenderPbrEnvironment::diffuseFresnel(float nv,uint32_t samples) {
  const SbVec3f v(std::sqrt(std::max(0.f,1-nv*nv)),0,nv);double sum=0;
  for(unsigned i=0;i<samples;++i) {
    const float u=(i+.5f)/samples,phi=2*pi*reverse(i),r=std::sqrt(u);
    const SbVec3f l(r*std::cos(phi),r*std::sin(phi),std::sqrt(1-u));
    sum+=std::pow(std::max(0.f,1-v.dot(unit(v+l))),5);
  }
  return float(sum/samples);
}
bool CoinRenderPbrEnvironment::bake(Source source,uint32_t cube,uint32_t lut,uint32_t filterCount,
                                   uint32_t lutCount,CoinRenderPbrEnvironment & result,SbString & error) {
  if(source>AXIS||!power(cube,512)||!power(lut,512)||filterCount<32||filterCount>4096||lutCount<128||lutCount>16384) {
    error="Invalid PBR bake dimensions, source or sample counts";return false;
  }
  try {
    CoinRenderPbrEnvironment e;e.size=cube;e.lutSize=lut;e.source=source;
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
        for(int c=0;c<4;++c)CoinRenderTextureFormatCore::write16(bytes+table.offset+(uint64_t(y)*lut+x)*8+c*2,
          CoinRenderTextureFormatCore::toHalf(values[c]));
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
bool CoinRenderPbrEnvironment::write(const SbString & file,SbString & error) const {
  if(payload.empty()||payload.size()>budget||!identity||identity!=hash(payload)) {error="Invalid PBR environment for serialization";return false;}
  FILE * output=std::fopen(file.getString(),"wb");if(!output) {error="Could not open PBR bundle for writing";return false;}
  uint8_t header[72]={};std::memcpy(header,"COINIBL1",8);
  const uint32_t fields[]={version,model,orientation,uint32_t(sections.getLength()),source,size,levels,lutSize,filterSamples,lutSamples,shSamples};
  for(int i=0;i<11;++i)put32(header+8+i*4,fields[i]);
  putFloat(header+52,roughnessFloor);put64(header+56,payload.size());put64(header+64,identity);
  bool ok=std::fwrite(header,1,sizeof(header),output)==sizeof(header);
  for(int sectionIndex=0;sectionIndex<sections.getLength();++sectionIndex) {
    const auto & s=sections[sectionIndex];
    uint8_t descriptor[40]={};const uint32_t f[]={s.kind,s.face,s.level,s.width,s.height};
    for(int i=0;i<5;++i)put32(descriptor+i*4,f[i]);putFloat(descriptor+20,s.roughness);
    put64(descriptor+24,s.offset);put64(descriptor+32,s.bytes);
    ok=ok&&std::fwrite(descriptor,1,sizeof(descriptor),output)==sizeof(descriptor);
  }
  ok=ok&&std::fwrite(payload.constData(),1,payload.size(),output)==payload.size();
  ok=std::fclose(output)==0&&ok;if(!ok)error="Could not complete PBR bundle write";else error.makeEmpty();return ok;
}
bool CoinRenderPbrEnvironment::read(const SbString & file,SbString & error) {
  FILE * input=std::fopen(file.getString(),"rb");if(!input) {error="Could not open PBR environment bundle";return false;}
  struct Close {FILE * f;~Close(){std::fclose(f);}} close{input};
  uint8_t h[72];if(std::fread(h,1,72,input)!=72||std::memcmp(h,"COINIBL1",8)||
    get32(h+8)!=version||get32(h+12)!=model||get32(h+16)!=orientation||getFloat(h+52)!=roughnessFloor) {
    error="Unsupported PBR bundle header, model or orientation";return false;
  }
  try {
    CoinRenderPbrEnvironment e;e.source=get32(h+24);e.size=get32(h+28);e.lutSize=get32(h+36);
    e.filterSamples=get32(h+40);e.lutSamples=get32(h+44);e.shSamples=get32(h+48);
    if(e.source>AXIS||!power(e.size,512)||!power(e.lutSize,512)||get64(h+56)>budget||
      e.filterSamples<32||e.filterSamples>4096||e.lutSamples<128||e.lutSamples>16384||e.shSamples!=65536) {
      error="PBR bundle dimensions, samples or budget are invalid";return false;
    }
    // Expected layout is computed before reading descriptors or GPU allocation.
    layout(e);
    if(get32(h+20)!=uint32_t(e.sections.getLength())||get32(h+32)!=e.levels||get64(h+56)!=e.payload.size()) {
      error="PBR bundle face/level counts or payload size mismatch";return false;
    }
    for(int sectionIndex=0;sectionIndex<e.sections.getLength();++sectionIndex) {
      const auto & s=e.sections[sectionIndex];
      uint8_t p[40];if(std::fread(p,1,40,input)!=40||get32(p)!=s.kind||get32(p+4)!=s.face||get32(p+8)!=s.level||
        get32(p+12)!=s.width||get32(p+16)!=s.height||getFloat(p+20)!=s.roughness||get64(p+24)!=s.offset||get64(p+32)!=s.bytes) {
        error="PBR bundle has missing, reordered or invalid face/level descriptors";return false;
      }
    }
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
        float values[4];for(int c=0;c<4;++c)values[c]=CoinRenderTextureFormatCore::fromHalf(CoinRenderTextureFormatCore::read16(p+pixel*8+c*2));
        for(float value:values)if(!std::isfinite(value)||value<0) {error="Invalid PBR half-float payload";return false;}
        if(s.kind==LUT) {
          if(values[0]+values[1]>1.002f||values[2]>1||values[3]>1) {error="PBR LUT violates its energy domain";return false;}
        }else if(values[3]!=1) {error="PBR cubemap must be opaque";return false;}
      }
    }
    *this=e;error.makeEmpty();return true;
  }catch(const std::bad_alloc&) {error="PBR environment allocation failed";return false;}
}
