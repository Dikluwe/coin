#include <Inventor/environment/CoinEnvironment.h>
#include <Inventor/environment/CoinEnvironmentAnisotropy.h>
#include <cstdio>
#include <cstring>
int main(int argc,char **argv) {
  if(argc!=3){std::fprintf(stderr,"Usage: coin_render_ibl_baker studio|asymmetric|white OUTPUT.cibl | anisotropy OUTPUT.cani\n");return 1;}
  if(!std::strcmp(argv[1],"anisotropy")) {
    CoinEnvironmentAnisotropy product;CoinEnvironmentAnisotropyOptions options;SbString error;
    if(!product.prepare(options,product,error)||!product.write(argv[2],error)) {std::fprintf(stderr,"%s\n",error.getString());return 2;}
    std::printf("Anisotropic albedo prepared: bytes=%llu identity=%llu\n",(unsigned long long)product.bytes(),(unsigned long long)product.identity);return 0;
  }
  CoinEnvironment::Source source;
  if(!std::strcmp(argv[1],"studio"))source=CoinEnvironment::STUDIO;
  else if(!std::strcmp(argv[1],"asymmetric"))source=CoinEnvironment::ASYMMETRIC;
  else if(!std::strcmp(argv[1],"white"))source=CoinEnvironment::WHITE;
  else return 1;
  CoinEnvironment environment;SbString diagnostic;
  if(!CoinEnvironment::bake(source,128,128,128,512,environment,diagnostic)||!environment.write(argv[2],diagnostic)) {
    std::fprintf(stderr,"%s\n",diagnostic.getString());return 2;
  }
  std::printf("IBL baked: face=%u lut=%u levels=%u bytes=%llu identity=%llu\n",environment.size,environment.lutSize,
    environment.levels,(unsigned long long)environment.payload.size(),(unsigned long long)environment.identity);
  return 0;
}
