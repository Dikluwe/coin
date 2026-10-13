#include "rendering/coinrender/CoinRenderPbrEnvironment.h"
#include <Inventor/C/base/time.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <algorithm>
static bool number(const char * value,uint32_t & result) {
  char * end=nullptr;const unsigned long parsed=std::strtoul(value,&end,10);
  if(!*value||*end||parsed>std::numeric_limits<uint32_t>::max())return false;
  result=uint32_t(parsed);return true;
}
int main(int argc,char ** argv) {
  CoinRenderPbrEnvironment::Source source=CoinRenderPbrEnvironment::STUDIO;
  uint32_t size=512,lut=256,filter=256,samples=2048;SbString output;
  for(int i=1;i<argc;++i) {
    if(i+1>=argc)return 2;
    const char * option=argv[i],*value=argv[++i];
    if(!std::strcmp(option,"--source")) {
      if(!std::strcmp(value,"studio"))source=CoinRenderPbrEnvironment::STUDIO;
      else if(!std::strcmp(value,"asymmetric"))source=CoinRenderPbrEnvironment::ASYMMETRIC;
      else if(!std::strcmp(value,"white"))source=CoinRenderPbrEnvironment::WHITE;
      else if(!std::strcmp(value,"axis"))source=CoinRenderPbrEnvironment::AXIS;
      else return 2;
    }else if(!std::strcmp(option,"--output"))output=value;
    else if(!std::strcmp(option,"--size")) {if(!number(value,size))return 2;}
    else if(!std::strcmp(option,"--lut-size")) {if(!number(value,lut))return 2;}
    else if(!std::strcmp(option,"--filter-samples")) {if(!number(value,filter))return 2;}
    else if(!std::strcmp(option,"--lut-samples")) {if(!number(value,samples))return 2;}
    else return 2;
  }
  if(!output.getLength()) {
    std::fprintf(stderr,"coin_render_pbr_baker --source studio|asymmetric|white|axis --output path.cibl [--size 512 --lut-size 256 --filter-samples 256 --lut-samples 2048]\n");return 2;
  }
  const double start=cc_time_gettimeofday();
  std::printf("Baking Coin PBR environment: cube=%u, LUT=%u, source=%u\n",size,lut,unsigned(source));std::fflush(stdout);
  CoinRenderPbrEnvironment environment;SbString error;
  if(!environment.bake(source,size,lut,filter,samples,environment,error)||!environment.write(output,error)) {
    std::fprintf(stderr,"PBR bake: %s\n",error.getString());return 1;
  }
  SbString manifest=output+".json";FILE * file=std::fopen(manifest.getString(),"w");if(!file)return 1;
  std::fprintf(file,"{\n  \"format\":\"CoinRenderIBL\",\n  \"version\":1,\n  \"model\":\"GGX-Smith-correlated-Schlick-Lambert-v1\",\n"
    "  \"source\":%u,\n  \"cube_size\":%u,\n  \"levels\":%u,\n  \"lut_size\":%u,\n"
    "  \"roughness_floor\":0.045,\n  \"filter_samples\":%u,\n  \"level_zero_filter_samples\":%u,\n"
    "  \"lut_samples\":%u,\n  \"sh_samples\":%u,\n  \"lut_channels\":[\"DFG_A\",\"DFG_B\",\"diffuse_Fc\",\"Eavg\"],\n"
    "  \"format_pixels\":\"RGBA16F-little-endian\",\n  \"format_SH\":\"9-RGB-f32-little-endian-irradiance\",\n"
    "  \"faces\":[\"+X\",\"-X\",\"+Y\",\"-Y\",\"+Z\",\"-Z\"],\n"
    "  \"payload_bytes\":%llu,\n  \"content_id_fnv1a64\":\"%016llx\",\n  \"bake_seconds\":%.6f,\n  \"sections\":[\n",
    unsigned(source),environment.size,environment.levels,environment.lutSize,filter,std::min(64u,filter),samples,
    environment.shSamples,(unsigned long long)environment.payload.size(),(unsigned long long)environment.identity,cc_time_gettimeofday()-start);
  for(int i=0;i<environment.sections.getLength();++i) {
    const auto & s=environment.sections[i];
    std::fprintf(file,"    %s{\"kind\":%u,\"face\":%u,\"level\":%u,\"width\":%u,\"height\":%u,\"roughness\":%.9g,\"offset\":%llu,\"bytes\":%llu}\n",
      i?",":"",s.kind,s.face,s.level,s.width,s.height,double(s.roughness),(unsigned long long)s.offset,(unsigned long long)s.bytes);
  }
  std::fprintf(file,"  ]\n}\n");if(std::fclose(file))return 1;
  CoinRenderPbrEnvironment decoded;
  if(!decoded.read(output,error)||decoded.identity!=environment.identity) {
    std::fprintf(stderr,"PBR bundle roundtrip: %s\n",error.getString());return 1;
  }
  std::printf("PBR bundle verified: %.2f MiB, ID=%016llx, seconds=%.3f\n",environment.payload.size()/1048576.0,
    (unsigned long long)environment.identity,cc_time_gettimeofday()-start);return 0;
}
