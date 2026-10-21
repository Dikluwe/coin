#ifndef COIN_RENDER_SHADOW_SPATIAL_SAMPLING_ORACLE_H
#define COIN_RENDER_SHADOW_SPATIAL_SAMPLING_ORACLE_H
#include "CoinRenderShadowSpatialFixture.h"
namespace spatial_sampling_oracle {
struct Vertex {SbVec3f point;SbVec4f coordinate;};
struct Sample {
  std::array<double,4> value{},dx{},dy{};
  double minimumWeight=0;
  bool fan=false;
};
// Legacy SoQuadMesh weights quantize a squared-distance ratio by its binary
// exponent. Attribute centres consequently differ from geometric midpoints.
inline double halfWeight(float ratio){
  int exponent=0;
#if defined(HAVE_ILOGB) || defined(HAVE__LOGB)
  std::frexp(double(ratio),&exponent);--exponent;
#else
  (void)ratio; // Legacy builds without either exponent primitive use zero.
#endif
  if(exponent< -16)return 0;if(exponent>=16)return .5;
  const double p=std::sqrt(std::ldexp(.75,exponent));return double(float(p/(1+p))*.5f);
}
inline const char*weightMode(){
#if defined(HAVE_ILOGB)
  return "HAVE_ILOGB";
#elif defined(HAVE__LOGB)
  return "HAVE__LOGB";
#else
  return "legacy-fixed-exponent-zero";
#endif
}
inline bool authoredSample(const SpatialScene&s,int unit,double pixelX,double pixelY,Sample&out){
  const auto*camera=static_cast<const SoOrthographicCamera*>(s.root->getChild(0));
  const double scale=double(side)/camera->height.getValue();const auto origin=camera->position.getValue();
  const char*setting=std::getenv("COIN_QUADMESH_PRECISE_LIGHTING");// This fixture authors 3D points and explicit coordinates, so the
  // remaining legacy eligibility condition is the material binding.
  const int material=s.materialBinding->value.getValue();
  const bool vertexMaterial=material==SoMaterialBinding::PER_VERTEX||material==SoMaterialBinding::PER_VERTEX_INDEXED;
  const bool fan=setting&&std::atoi(setting)>0&&!vertexMaterial;
  auto triangle=[&](const Vertex&a,const Vertex&b,const Vertex&c){
    auto screen=[&](const SbVec3f&p){return std::array<double,2>{{side*.5+(p[0]-origin[0])*scale,side*.5-(p[1]-origin[1])*scale}};};
    const auto pa=screen(a.point),pb=screen(b.point),pc=screen(c.point);
    const double bx=pb[0]-pa[0],by=pb[1]-pa[1],cx=pc[0]-pa[0],cy=pc[1]-pa[1];const double determinant=bx*cy-cx*by;
    if(std::abs(determinant)<1e-12)return false;
    const double u=((pixelX-pa[0])*cy-cx*(pixelY-pa[1]))/determinant;
    const double v=(bx*(pixelY-pa[1])-(pixelX-pa[0])*by)/determinant;
    const double w=1-u-v;
    // Require one strict interior witness, avoiding derivative ambiguity on
    // both the fan diagonals and the strip diagonal.
    if(std::min(w,std::min(u,v))<=1e-7)return false;
    const double ux=cy/determinant,vx=-by/determinant,uy=-cx/determinant,vy=bx/determinant;
    for(unsigned channel=0;channel<4;++channel){out.value[channel]=w*a.coordinate[channel]+u*b.coordinate[channel]+v*c.coordinate[channel];out.dx[channel]=ux*(b.coordinate[channel]-a.coordinate[channel])+vx*(c.coordinate[channel]-a.coordinate[channel]);out.dy[channel]=uy*(b.coordinate[channel]-a.coordinate[channel])+vy*(c.coordinate[channel]-a.coordinate[channel]);}
    out.minimumWeight=std::min(w,std::min(u,v));out.fan=fan;return true;
  };
  for(int row=0;row<2;++row)for(int col=0;col<2;++col){const int index[]={row*3+col,(row+1)*3+col,row*3+col+1,(row+1)*3+col+1};Vertex corners[4];
    for(unsigned k=0;k<4;++k){corners[k].point=s.positions->point[index[k]];const auto raw=s.uvw[unit]->point[index[k]];s.matrices[unit]->matrix.getValue().multVecMatrix(SbVec4f(raw[0],raw[1],raw[2],1),corners[k].coordinate);}
    if(!fan){if(triangle(corners[0],corners[1],corners[2])||triangle(corners[1],corners[2],corners[3]))return true;continue;}
    Vertex centre;centre.point=(corners[0].point+corners[1].point+corners[2].point+corners[3].point)*.25f;
    const double a=halfWeight((corners[0].point-centre.point).sqrLength()/(corners[3].point-centre.point).sqrLength());
    const double b=halfWeight((corners[1].point-centre.point).sqrLength()/(corners[2].point-centre.point).sqrLength());
    centre.coordinate=corners[0].coordinate*float(a)+corners[1].coordinate*float(b)+corners[2].coordinate*float(.5-b)+corners[3].coordinate*float(.5-a);
    const unsigned boundary[]={0,2,3,1};for(unsigned edge=0;edge<4;++edge)if(triangle(corners[boundary[edge]],corners[boundary[(edge+1)%4]],centre))return true;
  }return false;
}
inline double volumeLod(const Sample&s,unsigned w,unsigned h,unsigned d){const unsigned extent[]={w,h,d};double dx=0,dy=0;for(unsigned c=0;c<3;++c){dx+=std::pow(s.dx[c]*extent[c],2);dy+=std::pow(s.dy[c]*extent[c],2);}return std::log2(std::max(1e-30,std::sqrt(std::max(dx,dy))));}
inline bool positiveZ(const Sample&s,unsigned size,double&u,double&v,double&lod){const double z=s.value[2];if(z<=0||z<std::abs(s.value[0])||z<std::abs(s.value[1]))return false;u=.5*(s.value[0]/z+1);v=.5*(1-s.value[1]/z);const double ux=.5*(s.dx[0]*z-s.value[0]*s.dx[2])/(z*z),uy=.5*(s.dy[0]*z-s.value[0]*s.dy[2])/(z*z);const double vx=-.5*(s.dx[1]*z-s.value[1]*s.dx[2])/(z*z),vy=-.5*(s.dy[1]*z-s.value[1]*s.dy[2])/(z*z);lod=std::log2(std::max(1e-30,double(size)*std::sqrt(std::max(ux*ux+vx*vx,uy*uy+vy*vy))));return true;}
}
#endif
