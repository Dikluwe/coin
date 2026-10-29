#include <Inventor/environment/CoinEnvironment.h>
#include <Inventor/environment/CoinEnvironmentAnisotropy.h>
#include <Inventor/C/base/time.h>
#include <Inventor/SbVec3d.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#define CHECK(x,m) do { if(!(x)) {std::fprintf(stderr,"FAIL %s:%d\n",m,__LINE__);return 1;} } while(0)
namespace {
const double pi=3.14159265358979323846;
double radical(unsigned i) {
  double result=0,step=.5;
  while(i) {result+=(i&1)*step;i>>=1;step*=.5;}
  return result;
}
// Independent double-precision NDF importance quadrature. Neither the
// projected-disk VNDF sampler nor its G2/G1 estimator is used here.
SbVec2f integrate(const SbVec3d & v,double roughness,double strength,unsigned count) {
  const double ay=std::pow(std::max(.045,roughness),2),ax=ay+(1-ay)*strength*strength;
  const auto lambda=[=](const SbVec3d & w) {
    return .5*(std::sqrt(1+(ax*ax*w[0]*w[0]+ay*ay*w[1]*w[1])/(w[2]*w[2]))-1);
  };
  double sum[2]={};
  for(unsigned i=0;i<count;++i) {
    const double u=(i+.5)/count,radius=std::sqrt(u/(1-u)),phi=2*pi*radical(i);
    SbVec3d h(ax*radius*std::cos(phi),ay*radius*std::sin(phi),1);h.normalize();
    const double vh=v.dot(h);const auto l=h*(2*vh)-v;
    if(vh<=0||l[2]<=0)continue;
    const double g=1/(1+lambda(v)+lambda(l));
    const double weight=g*vh/(v[2]*h[2]),fc=std::pow(1-vh,5);
    sum[0]+=weight*(1-fc);sum[1]+=weight*fc;
  }
  return SbVec2f(float(sum[0]/count),float(sum[1]/count));
}
}
int main(int argc,char ** argv) {
  SbString error;SbVec2f actual;
  CoinEnvironmentAnisotropy product;CoinEnvironmentAnisotropyOptions options;
  const double begin=cc_time_gettimeofday();
  CHECK(product.prepare(options,product,error),error.getString());
  const double preparationMs=(cc_time_gettimeofday()-begin)*1000;
  CHECK(product.validate(error),"prepared product validates");
  FILE * report=std::fopen(argc>1?argv[1]:"/tmp/coin-environment-anisotropy.csv","w");
  CHECK(report,"open numerical report");
  std::fprintf(report,"roughness,strength,nv,azimuth,reference_a,reference_b,vndf_a,vndf_b,max_error,effective_isotropic_error,product_a,product_b,product_error\n");
  double worst=0,worstApproximation=0,worstProduct=0;unsigned cases=0;
  for(float roughness:{.15f,.4f,.8f})for(float strength:{0.f,.6f,1.f})
    for(float nv:{.2f,.4f,1.f})for(float azimuth:{0.f,float(pi/4),float(pi/2)}) {
      const double tangent=std::sqrt(1-double(nv)*nv);
      const SbVec3d v(tangent*std::cos(azimuth),tangent*std::sin(azimuth),nv);
      const SbVec3f vf{float(v[0]),float(v[1]),float(v[2])};
      const auto expected=integrate(v,roughness,strength,131072);
      CHECK(CoinEnvironment::anisotropicDfg(vf,roughness,strength,32768,actual,error),"anisotropic directional albedo preparation");
      const double delta=std::max(std::abs(actual[0]-expected[0]),std::abs(actual[1]-expected[1]));
      worst=std::max(worst,delta);
      CHECK(delta<.003,"VNDF A/B agree with independent NDF quadrature");
      CHECK(actual[0]>=0&&actual[1]>=0&&actual[0]+actual[1]<=1.00001f,"unit-Fresnel energy bounded");
      const float a=std::pow(roughness,2),effective=std::pow((a+(1-a)*strength*strength)*a,.25f);
      const auto previous=CoinEnvironment::dfg(nv,effective,32768);
      SbVec2f prepared;
      CHECK(product.evaluate(vf,roughness,strength,prepared,error),"prepared four-dimensional albedo query");
      const double productError=std::max(std::abs(prepared[0]-expected[0]),std::abs(prepared[1]-expected[1]));
      worstProduct=std::max(worstProduct,productError);
      if(productError>=.02)std::fprintf(stderr,"LUT error rough=%g strength=%g nv=%g phi=%g actual=%g,%g expected=%g,%g\n",roughness,strength,nv,azimuth,prepared[0],prepared[1],expected[0],expected[1]);
      CHECK(productError<.02,"prepared albedo agrees with independent reference within discretization budget");
      const double approximation=std::abs(previous[0]+previous[1]-expected[0]-expected[1]);
      worstApproximation=std::max(worstApproximation,approximation);
      std::fprintf(report,"%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",roughness,strength,nv,azimuth,expected[0],expected[1],actual[0],actual[1],delta,approximation,prepared[0],prepared[1],productError);
      if(strength==0&&azimuth==0) {
        const auto isotropic=CoinEnvironment::dfg(nv,roughness,32768);
        CHECK((actual-isotropic).length()<.00001f,"zero strength matches existing isotropic integral");
      }
      SbVec2f scaled;
      CHECK(CoinEnvironment::anisotropicDfg(vf*1e-20f,roughness,strength,32768,scaled,error)&&
        (scaled-actual).length()<.00001f,"view magnitude does not alter directional albedo");
      ++cases;
    }
  std::fclose(report);
  // An independent cosine-weighted hemisphere quadrature checks the average
  // product used by energy compensation, including azimuthal variation.
  for(float rough:{.4f,.8f})for(float strength:{.6f,1.f}) {
    SbVec2f mean;CHECK(product.evaluateAverage(rough,strength,mean,error),"hemispherical average query");
    SbVec2f refMean(0,0);
    for(int u=0;u<16;++u)for(int a=0;a<16;++a) {
      const double nv=std::sqrt((u+.5)/16),phi=(a+.5)*pi/32,xy=std::sqrt(1-nv*nv);
      refMean+=integrate(SbVec3d(xy*std::cos(phi),xy*std::sin(phi),nv),rough,strength,8192)/256.f;
    }
    CHECK((mean-refMean).length()<.01f,"average A/B matches independent cosine-weighted integration");
  }
  const auto id=product.identity;
  auto refused=options;refused.payloadBudgetBytes=product.bytes()-1;
  CHECK(!product.prepare(refused,product,error)&&product.identity==id,"payload refusal preserves publication");
  refused=options;refused.workingBudgetBytes=product.bytes()*2-1;
  CHECK(!product.prepare(refused,product,error)&&product.identity==id,"concurrent candidate refusal preserves publication");
  refused=options;refused.azimuthSize=1;
  CHECK(!product.prepare(refused,product,error)&&product.identity==id,"dimension refusal preserves publication");
  const SbString file=argc>1?SbString(argv[1])+".cani":SbString("/tmp/coin-environment-anisotropy-product.cani");
  CHECK(product.write(file,error),"atomic product file write");
  CoinEnvironmentAnisotropy decoded;
  CHECK(decoded.read(file,error)&&decoded.identity==id&&decoded.directional==product.directional&&decoded.average==product.average,"prepared product byte roundtrip");
  CHECK(!decoded.read(file,error,decoded.bytes()*2-1)&&decoded.identity==id,"bounded read preserves prior product");
  FILE * mutated=std::fopen(file.getString(),"r+b");CHECK(mutated,"open mutation fixture");
  std::fseek(mutated,48,SEEK_SET);const int byte=std::fgetc(mutated);std::fseek(mutated,48,SEEK_SET);std::fputc(byte^1,mutated);std::fclose(mutated);
  CHECK(!decoded.read(file,error)&&decoded.identity==id,"invalid content identity preserves publication");
  std::remove(file.getString());
  auto inconsistent=product;inconsistent.directional.data()[0]^=1;
  CHECK(!inconsistent.validate(error),"mutated in-memory product rejected");
  SbVec2f retainedQuery(.3f,.04f),query=retainedQuery;
  CHECK(!product.evaluate(SbVec3f(0,0,-1),.4f,.6f,query,error)&&query==retainedQuery,"invalid prepared query preserves output");
  std::printf("Prepared anisotropic product: bytes=%llu build_ms=%.6f max_ab_error=%.9g roundtrip budgets rollback PASS\n",(unsigned long long)product.bytes(),preparationMs,worstProduct);
  const SbVec2f retained(.3f,.04f);actual=retained;
  const SbVec3f invalid[]={SbVec3f(0,0,0),SbVec3f(0,0,-1),SbVec3f(1,0,0),
    SbVec3f(std::numeric_limits<float>::quiet_NaN(),0,1),SbVec3f(0,0,std::numeric_limits<float>::infinity())};
  for(const auto & v:invalid)CHECK(!CoinEnvironment::anisotropicDfg(v,.4f,.6f,1024,actual,error)&&actual==retained&&error.getLength(),"invalid view preserves output");
  for(float p:{-1.f,1.1f,std::numeric_limits<float>::quiet_NaN()}) {
    CHECK(!CoinEnvironment::anisotropicDfg(SbVec3f(0,0,1),p,.6f,1024,actual,error)&&actual==retained,"invalid roughness preserves output");
    CHECK(!CoinEnvironment::anisotropicDfg(SbVec3f(0,0,1),.4f,p,1024,actual,error)&&actual==retained,"invalid anisotropy preserves output");
  }
  for(unsigned n:{0u,127u,1048577u})CHECK(!CoinEnvironment::anisotropicDfg(SbVec3f(0,0,1),.4f,.6f,n,actual,error)&&actual==retained,"sample admission preserves output");
  CHECK(worstApproximation>10*worst,"directional integration improves on effective isotropic energy approximation");
  std::printf("Anisotropic CPU albedo: cases=%u max_ab_error=%.9g previous_max_energy_error=%.9g invalid rollback PASS\n",cases,worst,worstApproximation);
  return 0;
}
