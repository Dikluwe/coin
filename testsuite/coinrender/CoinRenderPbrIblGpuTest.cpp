#include "rendering/coinrender/CoinRenderPbrEnvironment.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/SbVec3d.h>
#include <Inventor/SbVec2d.h>
#include <Inventor/SbRotation.h>
#include <cmath>
#include <cstdio>
#define CHECK(x,m) do{if(!(x)){std::fprintf(stderr,"FAIL: %s (%d)\n",m,__LINE__);return 1;}}while(0)
static const double pi=3.14159265358979323846;
static double reverse(unsigned bits) {
  bits=(bits<<16)|(bits>>16);bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
  bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);
  bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
  bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);return bits/4294967296.0;
}
// Double NDF integration and cosine diffuse integration, independent of the
// baker's f32 VNDF estimator and of the GPU split-sum/SH implementation.
static SbVec3d whiteReference(double nv,const CoinRenderPbrMaterialSnapshot & p) {
  const double a2=std::pow(std::max(.045,double(p.roughness)),4);
  const SbVec3d v(std::sqrt(1-nv*nv),0,nv);SbVec3d result(0,0,0);
  const unsigned count=65536;
  for(unsigned i=0;i<count;++i) {
    const double u=(i+.5)/count,phi=2*pi*reverse(i),nh=std::sqrt((1-u)/(1+(a2-1)*u));
    const double s=std::sqrt(1-nh*nh);const SbVec3d h(s*std::cos(phi),s*std::sin(phi),nh);
    const double vh=v.dot(h);const auto l=h*(2*vh)-v;
    if(vh>0&&l[2]>0) {
      const auto lambda=[a2](double cosine){return .5*(std::sqrt(1+a2*(1-cosine*cosine)/(cosine*cosine))-1);};
      const double g=1/(1+lambda(nv)+lambda(l[2])),weight=g*vh/(nv*nh),fc=std::pow(1-vh,5);
      for(int c=0;c<3;++c) {
        const double f0=.04*(1-p.metallic)+p.baseColor[c]*p.metallic;
        result[c]+=weight*(f0+(1-f0)*fc)/count;
      }
    }
    const double radius=std::sqrt(u);const SbVec3d light(radius*std::cos(phi),radius*std::sin(phi),std::sqrt(1-u));
    SbVec3d half=v+light;half.normalize();const double fc=std::pow(1-v.dot(half),5);
    for(int c=0;c<3;++c) {
      const double f0=.04*(1-p.metallic)+p.baseColor[c]*p.metallic;
      result[c]+=(1-p.metallic)*p.baseColor[c]*(1-f0)*(1-fc)/count;
    }
  }
  return result;
}
static SbVec2d integratedDfg(double nv,double r,unsigned count=65536) {
  const double a2=std::pow(std::max(.045,r),4);
  const SbVec3d v(std::sqrt(1-nv*nv),0,nv);SbVec2d result(0,0);
  for(unsigned i=0;i<count;++i) {
    const double u=(i+.5)/count,phi=2*pi*reverse(i),nh=std::sqrt((1-u)/(1+(a2-1)*u));
    const double s=std::sqrt(1-nh*nh);const SbVec3d h(s*std::cos(phi),s*std::sin(phi),nh);
    const double vh=v.dot(h);const auto l=h*(2*vh)-v;
    if(vh<=0||l[2]<=0)continue;
    const auto lambda=[a2](double cosine){return .5*(std::sqrt(1+a2*(1-cosine*cosine)/(cosine*cosine))-1);};
    const double g=1/(1+lambda(nv)+lambda(l[2])),w=g*vh/(nv*nh)/count,fc=std::pow(1-vh,5);
    result[0]+=w*(1-fc);result[1]+=w*fc;
  }
  return result;
}
static SbVec2d integratedAverage(double r) {
  SbVec2d result(0,0);
  for(unsigned i=0;i<128;++i) {const double nv=(i+.5)/128;
    result+=integratedDfg(nv,r,4096)*(2*nv/128);}
  return result;
}
static double multipleWeight(double f0,double average) {
  const double favg=(1+20*f0)/21;
  return favg*favg*average/(1-favg*(1-average));
}
static SbVec3d compensatedWhite(double nv,const CoinRenderPbrMaterialSnapshot & p,const SbVec2d & avg) {
  const auto dfg=integratedDfg(nv,p.roughness);const double eavg=avg[0]+avg[1];SbVec3d result;
  for(int c=0;c<3;++c) {
    const double f0=.04*(1-p.metallic)+p.baseColor[c]*p.metallic;
    const double s=f0*dfg[0]+dfg[1]+(1-dfg[0]-dfg[1])*multipleWeight(f0,eavg);
    result[c]=s+(1-s)*(1-p.metallic)*p.baseColor[c];
  }
  return result;
}
static SbVec3d compensatedDirect(const SbVec3d & n,const SbVec3d & v,const SbVec3d & l,
  const CoinRenderPbrMaterialSnapshot & p,const SbVec2d & avg) {
  const double nv=n.dot(v),nl=n.dot(l),a2=std::pow(std::max(.045,double(p.roughness)),4);
  SbVec3d h=v+l;h.normalize();const double nh=n.dot(h),vh=v.dot(h),den=1+(a2-1)*nh*nh;
  const auto lambda=[a2](double cosine){return .5*(std::sqrt(1+a2*(1-cosine*cosine)/(cosine*cosine))-1);};
  const double factor=a2/(pi*den*den)/(4*nv*nl*(1+lambda(nv)+lambda(nl)));
  const auto dv=integratedDfg(nv,p.roughness),dl=integratedDfg(nl,p.roughness);const double eavg=avg[0]+avg[1];SbVec3d result;
  for(int c=0;c<3;++c) {
    const double f0=.04*(1-p.metallic)+p.baseColor[c]*p.metallic,multi=multipleWeight(f0,eavg);
    const double sv=f0*dv[0]+dv[1]+(1-dv[0]-dv[1])*multi;
    const double sl=f0*dl[0]+dl[1]+(1-dl[0]-dl[1])*multi;
    const double sa=f0*avg[0]+avg[1]+(1-eavg)*multi;
    const double spec=factor*(f0+(1-f0)*std::pow(1-vh,5))+
      multi*(1-dv[0]-dv[1])*(1-dl[0]-dl[1])/(pi*std::max(1-eavg,1e-6));
    result[c]=spec+(1-p.metallic)*p.baseColor[c]*(1-sv)*(1-sl)/(pi*std::max(1-sa,1e-6));
  }
  return result;
}
static CoinRenderFramePlan plane() {
  CoinRenderFramePlan f;f.materials.emplace_back();f.materials[0].model=CoinRenderMaterialModel::PBR;
  f.lightingStates.emplace_back();f.cameras.emplace_back();f.cameras[0].isPerspective=false;
  f.viewports.emplace_back();f.viewports[0].width=f.viewports[0].height=32;
  f.renderStates.emplace_back();f.renderStates[0].cullMode=CoinRenderCullMode::NONE;
  f.vertices.resize(4);const float xy[][2]={{-.95f,-.95f},{.95f,-.95f},{.95f,.95f},{-.95f,.95f}};
  for(int i=0;i<4;++i) {f.vertices[i].position[0]=xy[i][0];f.vertices[i].position[1]=xy[i][1];f.vertices[i].position[2]=-.5f;f.vertices[i].normal[2]=1;}
  f.indices={0,1,2,0,2,3};f.draws.emplace_back();f.draws[0].geometry.vertexCount=4;f.draws[0].geometry.indexCount=6;return f;
}
static bool render(CoinRenderTargetP & target,CoinRenderFramePlan & frame,SbVec3f & pixel) {
  static uint64_t revision=0;frame.revision=++revision;
  const auto result=target.executeFrame(frame);
  if(result.status!=CoinRenderBackendStatus::SUCCESS) {std::fprintf(stderr,"IBL submission: %s\n",result.diagnostic.c_str());return false;}
  SbList<SbVec3f> hdr;if(!target.backend->readSceneLinear(hdr)||hdr.getLength()!=target.size[0]*target.size[1])return false;
  pixel=hdr[(target.size[1]/2)*target.size[0]+target.size[0]/2];return true;
}
int main(int argc,char ** argv) {
  SoDB::init();SbString prefix=argc>1?argv[1]:"/tmp/coin-ibl-gpu",error;
  const SbString whitePath=prefix+"-white.cibl",axisPath=prefix+"-axis.cibl";
  CoinRenderPbrEnvironment white,axis;
  CHECK(white.bake(white.WHITE,16,128,64,2048,white,error)&&white.write(whitePath,error),"white control bundle");
  CHECK(axis.bake(axis.AXIS,64,128,256,2048,axis,error)&&axis.write(axisPath,error),"axis control bundle");
  CoinRenderTargetP target(SbVec2i32(32,32));target.options.renderer=COIN_RENDER_RENDERER_VULKAN;
  target.options.pbrEnergyCompensation=false;target.options.pbrSpecularFilterThreshold=0;
  target.options.pbrEnvironmentBundle=whitePath;
  auto frame=plane();SbVec3f pixel;
  int cases=0;
  for(float metal:{0.f,1.f})for(float roughness:{.045f,.5f,1.f})for(float nv:{.2f,.6f,1.f}) {
    auto & material=frame.materials[0].pbr;material.baseColor=SbColor(.7f,.3f,.1f);material.metallic=metal;material.roughness=roughness;
    for(auto & vertex:frame.vertices) {vertex.normal[0]=std::sqrt(1-nv*nv);vertex.normal[2]=nv;}
    CHECK(render(target,frame,pixel),"white furnace GPU radiance");const auto expected=whiteReference(nv,material);
    for(int c=0;c<3;++c)if(std::abs(pixel[c]-expected[c])>.012) {
      std::fprintf(stderr,"white nv=%g m=%g r=%g c=%d actual=%g expected=%g\n",nv,metal,roughness,c,pixel[c],expected[c]);return 1;
    }
    ++cases;
  }
  CoinRenderTargetP quality(SbVec2i32(32,32));quality.options.renderer=COIN_RENDER_RENDERER_VULKAN;
  quality.options.pbrEnvironmentBundle=whitePath;quality.options.pbrSpecularFilterThreshold=0;
  auto qframe=plane();int qualityCases=0;
  for(float roughness:{.045f,.3f,.6f,1.f}) {
    const auto avg=integratedAverage(roughness);
    for(float metal:{0.f,.5f,1.f})for(float nv:{.15f,.6f,1.f})for(bool whiteColor:{false,true}) {
      auto & p=qframe.materials[0].pbr;p.baseColor=whiteColor?SbColor(1,1,1):SbColor(.7f,.3f,.1f);p.metallic=metal;p.roughness=roughness;
      for(auto & vertex:qframe.vertices) {vertex.normal[0]=std::sqrt(1-nv*nv);vertex.normal[2]=nv;}
      CHECK(render(quality,qframe,pixel),"compensated furnace renders");const auto expected=compensatedWhite(nv,p,avg);
      for(int c=0;c<3;++c) {
        if(std::abs(pixel[c]-expected[c])>.013) {
          std::fprintf(stderr,"compensated nv=%g m=%g r=%g c=%d actual=%g expected=%g\n",nv,metal,roughness,c,pixel[c],expected[c]);return 1;
        }
        CHECK(pixel[c]<=1.003f,"compensated furnace never gains energy");
        if(whiteColor&&(metal==0||metal==1))CHECK(std::abs(pixel[c]-1)<.003,"white conductor and white dielectric preserve furnace energy");
      }
      ++qualityCases;
    }
  }
  CoinRenderTargetP directQuality(SbVec2i32(32,32));directQuality.options.renderer=COIN_RENDER_RENDERER_VULKAN;
  directQuality.options.pbrEnvironmentBundle=whitePath;directQuality.options.pbrEnvironmentIntensity=0;
  directQuality.options.pbrSpecularFilterThreshold=0;
  auto directFrame=plane();CoinRenderLightSourceSnapshot light;light.intensity=1;directFrame.lightingStates[0].lights.push_back(light);
  for(float roughness:{.3f,.6f,1.f}) {
    const auto avg=integratedAverage(roughness);
    for(float metal:{0.f,.5f,1.f}) {
      auto & p=directFrame.materials[0].pbr;p.baseColor=SbColor(.7f,.3f,.1f);p.metallic=metal;p.roughness=roughness;
      const SbVec3d n(0,0,1),v(.8,0,.6),l(-.6,0,.8);
      // Rotate view so authored V becomes camera +Z, preserving N/L geometry.
      const SbRotation rot(SbVec3f(.8f,0,.6f),SbVec3f(0,0,1));
      SbVec3f normal,ldir;rot.multVec(SbVec3f(0,0,1),normal);rot.multVec(SbVec3f(-.6f,0,.8f),ldir);
      for(auto & vertex:directFrame.vertices)for(int c=0;c<3;++c)vertex.normal[c]=normal[c];
      for(int c=0;c<3;++c)directFrame.lightingStates[0].lights[0].direction[c]=-ldir[c];
      CHECK(render(directQuality,directFrame,pixel),"direct compensated BRDF renders");const auto expected=compensatedDirect(n,v,l,p,avg)*.8;
      for(int c=0;c<3;++c)CHECK(std::abs(pixel[c]-expected[c])<.005+.01*expected[c],"direct reciprocal compensation and diffuse residual match independent integration");
      CHECK((compensatedDirect(n,v,l,p,avg)-compensatedDirect(n,l,v,p,avg)).length()<1e-10,"quality reference is reciprocal");
    }
  }
  // Normal variation is geometric and smooth. Flat faces must stay unchanged,
  // while a subpixel narrow highlight is widened with a bounded NDF variance.
  CoinRenderTargetP filtered(SbVec2i32(32,32));filtered.options.renderer=COIN_RENDER_RENDERER_VULKAN;
  filtered.options.pbrEnvironmentBundle=whitePath;filtered.options.pbrEnvironmentIntensity=0;
  auto varying=plane();varying.materials[0].pbr.baseColor=SbColor(.7f,.3f,.1f);varying.materials[0].pbr.metallic=1;varying.materials[0].pbr.roughness=.045f;
  light.direction[0]=light.direction[1]=0;light.direction[2]=-1;
  varying.lightingStates[0].lights.push_back(light);
  for(auto & vertex:varying.vertices) {
    vertex.normal[0]=.4f*(vertex.position[0]-.03125f)/.95f;
    vertex.normal[2]=std::sqrt(1-vertex.normal[0]*vertex.normal[0]);
  }
  SbVec3f unfiltered,smoothed;
  CHECK(render(directQuality,varying,unfiltered)&&render(filtered,varying,smoothed),"geometric specular filter control renders");
  std::printf("Specular filter peak: unfiltered=%g filtered=%g\n",unfiltered[0],smoothed[0]);
  CHECK(smoothed[0]<unfiltered[0]*.1f&&smoothed[0]>0,"filter suppresses subpixel specular peak without removing the lobe");
  auto flat=plane();flat.materials[0].pbr.baseColor=SbColor(.7f,.3f,.1f);flat.materials[0].pbr.metallic=1;flat.materials[0].pbr.roughness=.25f;
  flat.lightingStates[0].lights.push_back(light);
  CHECK(render(directQuality,flat,unfiltered)&&render(filtered,flat,smoothed)&&
    directQuality.colorBuffer==filtered.colorBuffer&&(unfiltered-smoothed).length()<.001f,
    "normal-variance filtering preserves a flat CAD face exactly");
  std::printf("M4 quality: %d furnaces, 9 independent direct BRDF controls, reciprocity and normal-variance filter PASS\n",qualityCases);
  CHECK(render(target,frame,pixel),"restore single-scattering oracle publication");
  const auto first=target.colorBuffer;CHECK(render(target,frame,pixel)&&target.colorBuffer==first,"first/warm IBL pixels stable");
  CoinRenderTargetP doubled(SbVec2i32(32,32));doubled.options.renderer=COIN_RENDER_RENDERER_VULKAN;
  doubled.options.pbrEnergyCompensation=false;doubled.options.pbrSpecularFilterThreshold=0;
  doubled.options.pbrEnvironmentBundle=whitePath;doubled.options.pbrEnvironmentIntensity=2;
  SbVec3f bright;CHECK(render(doubled,frame,bright)&&(bright-pixel*2).length()<.001,"environment intensity doubles scene-linear radiance");
  frame.materials[0].pbr.baseColor=SbColor(1,1,1);frame.materials[0].pbr.metallic=1;frame.materials[0].pbr.roughness=.045f;
  CHECK(render(doubled,frame,bright)&&bright[0]>1.9f,"indirect HDR radiance is preserved above display white");

  CoinRenderTargetP directional(SbVec2i32(32,32));directional.options.renderer=COIN_RENDER_RENDERER_VULKAN;
  directional.options.pbrEnergyCompensation=false;directional.options.pbrSpecularFilterThreshold=0;
  directional.options.pbrEnvironmentBundle=axisPath;
  frame=plane();frame.materials[0].pbr.baseColor=SbColor(1,1,1);frame.materials[0].pbr.metallic=1;frame.materials[0].pbr.roughness=0;
  const SbVec3f directions[]={SbVec3f(1,0,0),SbVec3f(-1,0,0),SbVec3f(0,1,0),SbVec3f(0,-1,0),SbVec3f(0,0,1),SbVec3f(0,0,-1),
    SbVec3f(1,.4f,.3f),SbVec3f(-1,.4f,-.3f),SbVec3f(.4f,1,.3f),SbVec3f(.4f,-1,-.3f),
    SbVec3f(.4f,.3f,1),SbVec3f(-.4f,.3f,-1),SbVec3f(1,1.01f,.2f),SbVec3f(1.01f,1,.2f),
    SbVec3f(1,1,1),SbVec3f(-1,-1,-1)};
  for(const auto & authored:directions) {
    SbVec3f normal=authored;normal.normalize();
    SbMatrix rotation;rotation.setRotate(SbRotation(SbVec3f(0,0,1),normal));
    // Keep the surface and camera's relative geometry fixed while changing
    // their orientation in world space. The environment must remain fixed.
    frame.renderStates[0].model=rotation;frame.renderStates[0].view=rotation.inverse();
    frame.cameras[0].viewMatrix=frame.renderStates[0].view;
    CHECK(render(directional,frame,pixel),"cube face probe renders");const auto expected=(normal+SbVec3f(1,1,1))*.5f;
    CHECK((pixel-expected).length()<.015,"native cube faces and camera/world direction agree");
  }
  CoinRenderTargetP rotated(SbVec2i32(32,32));rotated.options.renderer=COIN_RENDER_RENDERER_VULKAN;
  rotated.options.pbrEnergyCompensation=false;rotated.options.pbrSpecularFilterThreshold=0;
  rotated.options.pbrEnvironmentBundle=axisPath;rotated.options.pbrEnvironmentRotation=float(pi/2);
  SbMatrix rotation;rotation.setRotate(SbRotation(SbVec3f(0,0,1),SbVec3f(1,0,0)));
  frame.renderStates[0].model=rotation;frame.renderStates[0].view=rotation.inverse();frame.cameras[0].viewMatrix=rotation.inverse();
  CHECK(render(rotated,frame,pixel)&&(pixel-SbVec3f(.5f,0,.5f)).length()<.015,"environment rotates independently around world Z");
  frame.renderStates[0].model=frame.renderStates[0].view=SbMatrix::identity();frame.cameras[0].viewMatrix=SbMatrix::identity();
  frame.materials[0].pbr.roughness=1.f/6-.0001f;
  CHECK(render(directional,frame,pixel),"roughness below first mip boundary");
  const auto below=pixel;frame.materials[0].pbr.roughness=1.f/6+.0001f;
  CHECK(render(directional,frame,pixel)&&(pixel-below).length()<.003,"prefilter LOD is continuous across its first transition");
  frame.materials[0].pbr.roughness=1;
  CHECK(render(directional,frame,pixel),"last prefiltered mip probe renders");
  CoinRenderPbrMaterialSnapshot whiteMetal;whiteMetal.baseColor=SbColor(1,1,1);whiteMetal.metallic=1;whiteMetal.roughness=1;
  const float energy=float(whiteReference(1,whiteMetal)[0]);const SbVec3f lastExpected=SbVec3f(.5f,.5f,5.f/6)*energy;
  CHECK((pixel-lastExpected).length()<.012,"last mip is the GGX integral, not an ordinary source mip");
  CHECK(target.pbrEnvironmentSnapshot&&target.pbrEnvironmentSnapshot->identity==white.identity,
    "target owns immutable CPU content independently of executor resources");
  std::remove(whitePath.getString());
  CHECK(target.resize(SbVec2i32(48,48)),"IBL target resize");
  frame=plane();frame.viewports[0].width=frame.viewports[0].height=48;
  frame.materials[0].pbr.baseColor=SbColor(1,1,1);frame.materials[0].pbr.metallic=1;frame.materials[0].pbr.roughness=.045f;
  CHECK(render(target,frame,pixel)&&pixel[0]>.98f,"HDR resize retains immutable environment after the source file is removed");
  auto legacy=frame;legacy.materials[0].model=CoinRenderMaterialModel::LEGACY;
  legacy.renderStates[0].lightModel=CoinRenderLightModel::BASE_COLOR;
  CHECK(target.executeFrame(legacy).status==CoinRenderBackendStatus::SUCCESS,"legacy frame after IBL");
  SbList<SbVec3f> stale;
  CHECK(!target.backend->readSceneLinear(stale)&&stale.getLength()==0,"legacy publication exposes no stale HDR radiance");
  CHECK(render(target,frame,pixel)&&pixel[0]>.98f,"PBR publication restores HDR on the same target");
  const auto frozenIdentity=target.pbrEnvironmentSnapshot->identity;
  const auto oldGeneration=target.resourceGeneration;
  target.deviceLost();
  CHECK(!target.backend&&target.resourceGeneration>oldGeneration&&
    target.pbrEnvironmentSnapshot->identity==frozenIdentity,"device retirement retains immutable CPU environment");
  CHECK(render(target,frame,pixel)&&pixel[0]>.98f,
    "executor reconstruction uploads frozen environment after source removal");
  CoinRenderTargetP broken(SbVec2i32(32,32));broken.options.renderer=COIN_RENDER_RENDERER_VULKAN;
  broken.options.pbrEnergyCompensation=false;broken.options.pbrSpecularFilterThreshold=0;
  broken.options.pbrEnvironmentBundle=prefix+"-missing.cibl";
  auto control=plane();control.materials[0].model=CoinRenderMaterialModel::LEGACY;
  control.materials[0].diffuse[0]=1;control.materials[0].diffuse[1]=control.materials[0].diffuse[2]=0;
  control.renderStates[0].lightModel=CoinRenderLightModel::BASE_COLOR;
  CHECK(broken.executeFrame(control).status==CoinRenderBackendStatus::SUCCESS,"legacy publication before rejected environment");
  const auto previous=broken.colorBuffer;const auto previousSerial=broken.lastSubmissionSerial;
  auto candidate=plane();
  CHECK(broken.executeFrame(candidate).status==CoinRenderBackendStatus::UNSUPPORTED&&broken.colorBuffer==previous&&
    broken.lastSubmissionSerial==previousSerial&&!broken.pbrEnvironmentSnapshot,"failed environment load preserves publication and captures no content");
  std::remove(whitePath.getString());std::remove(axisPath.getString());
  std::printf("PBR IBL GPU: %d independent furnace cases, six native faces, world/camera/rotation, intensity, last physical mip and replay PASS\n",cases);
  return 0;
}
