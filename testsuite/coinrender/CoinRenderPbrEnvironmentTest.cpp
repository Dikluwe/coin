#include "rendering/coinrender/CoinRenderPbrEnvironment.h"
#include "rendering/coinrender/CoinRenderTextureFormatCore.h"
#include <Inventor/SbVec3d.h>
#include <Inventor/C/base/time.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <limits>
#define CHECK(x,m) do{if(!(x)){std::fprintf(stderr,"FAIL: %s (%d)\n",m,__LINE__);return 1;}}while(0)
// Independent NDF importance integration, explicit double Lambda. The baker
// uses f32 visible-normal sampling and correlated G2/G1 weights instead.
static SbVec2f integral(double nv,double roughness) {
  const double pi=3.14159265358979323846,a2=std::pow(std::max(.045,roughness),4);
  const SbVec3d v(std::sqrt(1-nv*nv),0,nv);double sum[2]={};
  const unsigned count=65536;
  for(unsigned i=0;i<count;++i) {
    unsigned bits=i;bits=(bits<<16)|(bits>>16);
    bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
    bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);
    bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
    bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
    const double u=(i+.5)/count,phi=2*pi*bits/4294967296.0,nh=std::sqrt((1-u)/(1+(a2-1)*u));
    const double s=std::sqrt(1-nh*nh);const SbVec3d h(s*std::cos(phi),s*std::sin(phi),nh);
    const double vh=v.dot(h);const auto l=h*(2*vh)-v;if(vh<=0||l[2]<=0)continue;
    const auto lambda=[a2](double cosine){return .5*(std::sqrt(1+a2*(1-cosine*cosine)/(cosine*cosine))-1);};
    const double g=1/(1+lambda(nv)+lambda(l[2])),w=g*vh/(nv*nh),fc=std::pow(1-vh,5);
    sum[0]+=w*(1-fc);sum[1]+=w*fc;
  }
  return SbVec2f(float(sum[0]/count),float(sum[1]/count));
}
static bool save(const SbString & path,const SbByteBuffer & buffer) {
  FILE * f=std::fopen(path.getString(),"wb");if(!f)return false;
  bool ok=std::fwrite(buffer.constData(),1,buffer.size(),f)==buffer.size();return std::fclose(f)==0&&ok;
}
int main(int argc,char ** argv) {
  SbString path=argc>1?argv[1]:"/tmp/coin-pbr-environment-test.cibl",error;
  CoinRenderPbrEnvironment white,decoded;
  CHECK(white.bake(white.WHITE,16,32,32,512,white,error),"small deterministic bake");
  CHECK(white.write(path,error)&&decoded.read(path,error),"versioned binary bundle roundtrip");
  CHECK(decoded.identity==white.identity&&decoded.payload==white.payload,"content identity and bytes preserved");
  CHECK(decoded.size==16&&decoded.levels==5&&decoded.sections.getLength()==38,"all six faces and complete explicit levels");
  const float pi=3.14159265358979323846f;
  for(int face=0;face<6;++face) {
    const auto center=white.direction(face,0,0);
    CHECK(center[face/2]==(face%2?-1.f:1.f),"cube face axis convention");
    float basis[9];white.basis(center,basis);SbVec3f irradiance(0,0,0);
    for(int i=0;i<9;++i)irradiance+=decoded.irradiance[i]*basis[i];
    CHECK((irradiance-SbVec3f(pi,pi,pi)).length()<.0002,"constant furnace SH equals pi in every direction");
  }
  for(int i=0;i<decoded.sections.getLength();++i) {
    const auto & section=decoded.sections[i];
    if(section.kind==white.SOURCE||section.kind==white.SPECULAR) {
      for(uint64_t n=0;n<section.bytes/2;++n)
        CHECK(CoinRenderTextureFormatCore::fromHalf(CoinRenderTextureFormatCore::read16(decoded.data(section)+n*2))==1,
          "constant radiance preserved by every source/prefilter face and level");
    }
  }
  for(float nv:{.1f,.3f,.7f,1.f}) for(float roughness:{.045f,.25f,.6f,1.f}) {
    const auto expected=integral(nv,roughness),actual=white.dfg(nv,roughness,8192);
    CHECK(std::abs(actual[0]-expected[0])<.006f&&std::abs(actual[1]-expected[1])<.003f,
      "VNDF DFG agrees with independent double-precision NDF integration");
    CHECK(actual[0]>=0&&actual[1]>=0&&actual[0]+actual[1]<=1.000001f,"DFG single scattering energy bounded");
  }
  // A second bake must be byte-identical; timing is outside the bundle.
  CoinRenderPbrEnvironment replay;
  CHECK(replay.bake(white.WHITE,16,32,32,512,replay,error)&&replay.payload==white.payload,"bake is reproducible");
  FILE * f=std::fopen(path.getString(),"rb");CHECK(f,"read roundtrip fixture");
  std::fseek(f,0,SEEK_END);const auto size=std::ftell(f);std::rewind(f);SbByteBuffer raw(size);
  CHECK(std::fread(raw.data(),1,raw.size(),f)==raw.size(),"read fixture bytes");std::fclose(f);
  for(unsigned offset:{8u,12u,16u,20u,28u,32u,36u,52u,56u,64u,76u,80u,96u,104u}) {
    SbByteBuffer mutated=raw;mutated.data()[offset]^=1;
    CHECK(save(path,mutated)&&!decoded.read(path,error)&&decoded.identity==white.identity,
      "malformed version/orientation/layout/count/hash preserves the prior environment");
  }
  SbByteBuffer truncated(raw.size()-1,raw.constData());
  CHECK(save(path,truncated)&&!decoded.read(path,error),"truncated payload rejected");
  SbByteBuffer trailing=raw;trailing.push(SbByteBuffer("x"));
  CHECK(save(path,trailing)&&!decoded.read(path,error),"trailing payload rejected");
  // Recompute the content ID so domain validation is tested independently of
  // the checksum. These candidates have structurally valid descriptors.
  const size_t base=72+white.sections.getLength()*40;
  for(int invalid=0;invalid<5;++invalid) {
    SbByteBuffer mutated=raw;auto * bytes=reinterpret_cast<uint8_t *>(mutated.data());
    if(invalid<2) CoinRenderTextureFormatCore::write16(bytes+base,invalid==0?0x7e00:0xbc00);
    if(invalid==2) CoinRenderTextureFormatCore::write16(bytes+base+6,0);
    if(invalid==3) {
      const auto & lut=white.sections[white.sections.getLength()-2];
      CoinRenderTextureFormatCore::write16(bytes+base+lut.offset,0x3c00);
      CoinRenderTextureFormatCore::write16(bytes+base+lut.offset+2,0x3c00);
    }
    if(invalid==4) {
      const auto & sh=white.sections[white.sections.getLength()-1];
      bytes[base+sh.offset]=0;bytes[base+sh.offset+1]=0;bytes[base+sh.offset+2]=0xc0;bytes[base+sh.offset+3]=0x7f;
    }
    uint64_t id=UINT64_C(14695981039346656037);
    for(size_t i=base;i<mutated.size();++i) {id^=bytes[i];id*=UINT64_C(1099511628211);}
    for(int i=0;i<8;++i)bytes[64+i]=uint8_t(id>>(8*i));
    CHECK(save(path,mutated)&&!decoded.read(path,error)&&decoded.identity==white.identity,
      "nonfinite/negative color, alpha, invalid LUT energy and SH rejected with a valid content ID");
  }
  CHECK(!replay.bake(white.WHITE,17,32,32,512,replay,error),"non-power-of-two cube rejected");
  CHECK(save(path,raw)&&decoded.read(path,error),"valid bundle recovers after rejected candidates");
  std::remove(path.getString());
  std::printf("PBR environment: deterministic package, SH, six faces/all levels, independent DFG, malformed input and rollback PASS\n");
  return 0;
}
