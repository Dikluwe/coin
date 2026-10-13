#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/SbVec3d.h>
#include <Inventor/lists/SbList.h>
#include <cmath>
#include <cstdio>
#include <algorithm>
#define CHECK(x, m) do { if (!(x)) { std::fprintf(stderr, "FAIL: %s (line %d)\n", m, __LINE__); return 1; } } while (0)
static const double pi = 3.14159265358979323846;
static SbVec3d unit(SbVec3d v) { v.normalize(); return v; }

// Double-precision oracle uses explicit Smith Lambda/G and the Cook-Torrance
// denominator. It does not import the GPU BRDF or any renderer math helper.
static SbVec3d reference(const SbVec3d & n, const SbVec3d & v, const SbVec3d & l,
                        const CoinRenderPbrMaterialSnapshot & p) {
  const double nv = n.dot(v), nl = n.dot(l);
  if (nv <= 0 || nl <= 0) return SbVec3d(0,0,0);
  const SbVec3d h = unit(v+l);
  const double nh = std::max(0.0, n.dot(h)), vh = std::max(0.0, v.dot(h));
  const double a = std::pow(std::max(double(p.roughness), .045), 2);
  const double a2 = a*a, d = 1.0 + (a2-1.0)*nh*nh;
  const double distribution = a2 / (pi*d*d);
  const auto lambda = [a2](double cosine) {
    return .5 * (std::sqrt(1+a2*(1-cosine*cosine)/(cosine*cosine))-1);
  };
  const double g = 1.0/(1+lambda(nv)+lambda(nl));
  SbVec3d result;
  for (int c=0;c<3;++c) {
    const double f0 = .04*(1-p.metallic)+p.baseColor[c]*p.metallic;
    const double f = f0+(1-f0)*std::pow(1-vh,5);
    result[c] = distribution*g*f/(4*nv*nl) + (1-p.metallic)*(1-f)*p.baseColor[c]/pi;
  }
  return result;
}
static double output(double radiance, double stops) {
  // Neutral monochrome reduces to pedestal removal then peak compression.
  const double input = radiance*std::exp2(stops);
  const double offset = input <= .08 ? input-input*input/.16 : .04;
  const double peak = input-offset;
  const double mapped = peak <= .76 ? peak : 1-.24*.24/(peak-.52);
  return mapped <= .0031308 ? mapped*12.92 : 1.055*std::pow(mapped,1/2.4)-.055;
}
static double furnace(double nv, double roughness) {
  CoinRenderPbrMaterialSnapshot p;p.baseColor=SbColor(1,1,1);p.metallic=1;p.roughness=float(roughness);
  const SbVec3d n(0,0,1),v(std::sqrt(1-nv*nv),0,nv);
  const double a2=std::pow(roughness,4);double total=0;
  const unsigned samples=16384;
  for(unsigned i=0;i<samples;++i) {
    unsigned bits=i;bits=(bits<<16)|(bits>>16);
    bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
    bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);
    bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
    bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
    const double u=(i+.5)/samples,phi=2*pi*bits/4294967296.0;
    const double nh=std::sqrt((1-u)/(1+(a2-1)*u)),s=std::sqrt(1-nh*nh);
    const SbVec3d h(s*std::cos(phi),s*std::sin(phi),nh);
    const double vh=v.dot(h);if(vh<=0)continue;
    const auto l=h*(2*vh)-v;if(l[2]<=0)continue;
    const double d=1+(a2-1)*nh*nh,pdf=a2*nh/(pi*d*d*4*vh);
    total+=reference(n,v,l,p)[0]*l[2]/pdf;
  }
  return total/samples;
}
static CoinRenderFramePlan plane() {
  CoinRenderFramePlan f;
  f.materials.emplace_back(); f.materials[0].model = CoinRenderMaterialModel::PBR;
  f.lightingStates.emplace_back(); f.cameras.emplace_back(); f.cameras[0].isPerspective = false;
  f.viewports.emplace_back(); f.viewports[0].width=64; f.viewports[0].height=64;
  f.renderStates.emplace_back(); f.renderStates[0].cullMode = CoinRenderCullMode::NONE;
  f.vertices.resize(4);
  const float xy[][2] = {{-.95f,-.95f},{.95f,-.95f},{.95f,.95f},{-.95f,.95f}};
  for (int i=0;i<4;++i) {
    f.vertices[i].position[0]=xy[i][0]; f.vertices[i].position[1]=xy[i][1]; f.vertices[i].position[2]=-.5f;
    f.vertices[i].normal[2]=1;
  }
  f.indices={0,1,2,0,2,3}; f.draws.emplace_back();
  f.draws[0].geometry.vertexCount=4; f.draws[0].geometry.indexCount=6;
  f.revision=1; return f;
}
static bool read(CoinRenderTargetP & t, CoinRenderFramePlan & f, SbList<SbVec3f> & hdr) {
  ++f.revision;
  auto result=t.executeFrame(f);
  if (result.status!=CoinRenderBackendStatus::SUCCESS) {
    std::fprintf(stderr,"submission: %s\n", result.diagnostic.c_str()); return false;
  }
  return t.backend->readSceneLinear(hdr) && hdr.getLength()==t.size[0]*t.size[1];
}
int main() {
  SoDB::init();
  for(double nv:{.2,.5,1.0}) for(double roughness:{.25,.5,1.0}) {
    const double energy=furnace(nv,roughness);
    CHECK(std::isfinite(energy)&&energy>0&&energy<1.005,"single scattering white furnace is finite and energy bounded");
    if(nv==1&&roughness==1) CHECK(energy>.30&&energy<.32,"rough white metal retains expected single scattering loss");
  }
  CoinRenderPbrMaterialSnapshot reciprocal;reciprocal.metallic=.5f;
  const auto n=SbVec3d(0,0,1),v=unit(SbVec3d(.2,.1,1)),l=unit(SbVec3d(-.4,.3,1));
  CHECK((reference(n,v,l,reciprocal)-reference(n,l,v,reciprocal)).length()<1e-12,"reflection reference is reciprocal");
  CoinRenderTargetP target(SbVec2i32(64,64));
  target.options.renderer=COIN_RENDER_RENDERER_VULKAN;
  CoinRenderFramePlan f=plane(); SbList<SbVec3f> hdr;
  for (float intensity : {0.01f, .05f, .5f, 2.f, 4.f, 8.f}) {
    f.materials[0].pbr.emissive=SbColor(intensity,intensity,intensity);
    CHECK(read(target,f,hdr),"HDR framebuffer readable");
    const int i=32*64+32;
    CHECK(std::abs(hdr[i][0]-intensity)<.001*std::max(1.f,intensity),"binary16 preserves authored HDR emission");
    CHECK(std::abs(target.colorBuffer[i*4]/255.0-output(intensity,0))<2.0/255,"Neutral plus exactly one sRGB encoding");
    CHECK(std::abs(target.depthBuffer[i]-.25f)<1e-5,"PBR depth comes from the HDR scene, not fullscreen output");
  }
  // Exposure changes the output transform; scene-linear radiance stays 2.
  CoinRenderTargetP exposed(SbVec2i32(64,64)); exposed.options.renderer=COIN_RENDER_RENDERER_VULKAN;
  exposed.options.pbrExposureStops=1;
  f.materials[0].pbr.emissive=SbColor(2,2,2);
  CHECK(read(exposed,f,hdr),"exposed frame readable");
  CHECK(hdr[32*64+32][0]==2 && std::abs(exposed.colorBuffer[(32*64+32)*4]/255.0-output(2,1))<2.0/255,
    "one stop doubles tone mapper input and preserves radiance");

  f.materials[0].pbr.emissive=SbColor(0,0,0);
  CoinRenderLightSourceSnapshot light; light.direction[0]=.6f; light.direction[2]=-.8f;
  light.intensity=3; f.lightingStates[0].lights.push_back(light);
  const SbVec3d directLight(-.6,0,.8);
  int cases=0;
  for (bool perspective : {false,true}) for (float metal : {0.f,1.f})
    for (float roughness : {0.f,.045f,.25f,.6f,1.f}) {
      f.cameras[0].isPerspective=perspective;
      f.materials[0].pbr.baseColor=SbColor(.7f,.3f,.1f);
      f.materials[0].pbr.metallic=metal; f.materials[0].pbr.roughness=roughness;
      CHECK(read(target,f,hdr),"GGX candidate renders");
      for (int x : {20,32,44}) {
        const SbVec3d position((x+.5)/64*2-1,(32+.5)/64*2-1,-.5);
        const SbVec3d v=perspective ? unit(-position) : SbVec3d(0,0,1);
        const auto expected=reference(SbVec3d(0,0,1),v,directLight,f.materials[0].pbr)*(.8*3);
        for (int c=0;c<3;++c) {
          const double actual=hdr[32*64+x][c];
          if (std::abs(actual-expected[c])>.003*std::max(1.0,expected[c])) {
            std::fprintf(stderr,"p=%d m=%.1f r=%.3f x=%d channel=%d actual=%.8g expected=%.8g\n",
              perspective,metal,roughness,x,c,actual,expected[c]); return 1;
          }
        }
      }
      ++cases;
    }
  // The camera-dependent highlight must vary across a single planar draw.
  CHECK(std::abs(hdr[32*64+20][0]-hdr[32*64+44][0])>.0005,"PBR illumination is evaluated per fragment");
  f.cameras[0].isPerspective=false;f.materials[0].pbr.roughness=0;
  f.lightingStates[0].lights[0].direction[0]=0;
  f.lightingStates[0].lights[0].direction[2]=-1;
  CHECK(read(target,f,hdr),"minimum roughness highlight renders");
  const auto sharp=reference(SbVec3d(0,0,1),SbVec3d(0,0,1),SbVec3d(0,0,1),f.materials[0].pbr)*3;
  for(int c=0;c<3;++c) CHECK(std::isfinite(hdr[32*64+32][c]) &&
    std::abs(hdr[32*64+32][c]-sharp[c])<.003*sharp[c],"sharp highlight is finite and retains HDR radiance");
  f.lightingStates[0].lights[0].direction[0]=.6f;
  f.lightingStates[0].lights[0].direction[2]=-.8f;
  f.materials[0].pbr.roughness=1;
  // Nonuniform and reflected transforms use inverse-transpose normals.
  for (float sx : {1.5f,-1.5f}) {
    f.renderStates[0].model.setScale(SbVec3f(sx,.8f,1.2f));
    for (auto & vertex:f.vertices) {vertex.normal[0]=.3f;vertex.normal[1]=.2f;vertex.normal[2]=1;}
    f.cameras[0].isPerspective=false;
    CHECK(read(target,f,hdr),"scaled/reflected surface renders");
    const SbVec3d n=unit(SbVec3d(.3/sx,.2/.8,1/1.2));
    const auto expected=reference(n,SbVec3d(0,0,1),directLight,f.materials[0].pbr)*(std::max(0.0,n.dot(directLight))*3);
    for(int c=0;c<3;++c) CHECK(std::abs(hdr[32*64+32][c]-expected[c])<.003*std::max(1.0,expected[c]),
      "normal transform matches independent scaled/reflected reference");
  }
  f.renderStates[0].model=SbMatrix::identity();
  for(auto & vertex:f.vertices) {vertex.normal[0]=vertex.normal[1]=0;vertex.normal[2]=1;}
  f.materials[0].pbr.metallic=0;f.materials[0].pbr.roughness=.4f;
  auto & punctual=f.lightingStates[0].lights[0];
  punctual.position[2]=.5f;punctual.intensity=4;
  punctual.color[0]=.5f;punctual.color[2]=.25f;
  punctual.attenuation[0]=.2f;punctual.attenuation[1]=.3f;
  for(bool spot:{false,true}) {
    punctual.type=spot?CoinRenderLightType::SPOT:CoinRenderLightType::POINT;
    punctual.direction[0]=0;punctual.direction[2]=-1;
    punctual.cutOffAngle=.3f;punctual.dropOffRate=.05f;
    CHECK(read(target,f,hdr),"point/spot light renders");
    for(int x:{20,32,44}) {
      const SbVec3d delta(-((x+.5)/64*2-1),-((32+.5)/64*2-1),1);
      const double distance=delta.length();const auto direction=delta/distance;
      double strength=4/(1+.3*distance+.2*distance*distance);
      if(spot) strength=direction[2]<std::cos(.3)?0:strength*std::pow(direction[2],.05*128);
      const auto brdf=reference(SbVec3d(0,0,1),SbVec3d(0,0,1),direction,f.materials[0].pbr);
      for(int c=0;c<3;++c) CHECK(std::abs(hdr[32*64+x][c]-brdf[c]*direction[2]*strength*punctual.color[c])<.003,
        "point attenuation and spot cone match independent reference");
    }
  }
  f.lightingStates[0].lights.clear();f.materials[0].pbr.emissive=SbColor(2,2,2);
  f.renderStates[0].clipPlanesWorld.emplace_back(SbVec3f(1,0,0),0);
  CHECK(read(target,f,hdr),"PBR clip plane renders");
  CHECK(hdr[32*64+20][0]==0 && target.depthBuffer[32*64+20]==1 &&
    hdr[32*64+44][0]==2 && std::abs(target.depthBuffer[32*64+44]-.25)<1e-5,
    "clipping agrees in HDR color and depth");
  f.renderStates[0].clipPlanesWorld.clear();
  const auto published=target.colorBuffer; const auto serial=target.lastSubmissionSerial;
  f.renderStates[0].fogMode=CoinRenderFogMode::HAZE;
  CHECK(target.executeFrame(f).status==CoinRenderBackendStatus::UNSUPPORTED && target.colorBuffer==published &&
    target.lastSubmissionSerial==serial,"rejected PBR combination preserves publication");
  f.renderStates[0].fogMode=CoinRenderFogMode::NONE;
  f.renderStates[0].model.setScale(SbVec3f(0,1,1));
  CHECK(target.executeFrame(f).status==CoinRenderBackendStatus::UNSUPPORTED && target.colorBuffer==published,
    "singular PBR transform is rejected without a normal fallback");
  f.renderStates[0].model=SbMatrix::identity();f.vertices[0].normal[2]=0;
  CHECK(target.executeFrame(f).status==CoinRenderBackendStatus::UNSUPPORTED && target.colorBuffer==published,
    "zero PBR normal is rejected without a fabricated orientation");
  f.vertices[0].normal[2]=1;f.materials.push_back(f.materials[0]);f.vertices[0].materialSlot=1;
  CHECK(target.executeFrame(f).status==CoinRenderBackendStatus::UNSUPPORTED && target.colorBuffer==published,
    "per-vertex material variation cannot bypass the OVERALL PBR profile");
  std::printf("PBR GPU: HDR/depth/output/exposure, %d GGX cases, cameras/normals, point/spot and clipping PASS\n",cases);
  return 0;
}
