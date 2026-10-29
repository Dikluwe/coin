#ifndef COIN_RENDER_ROUGH_PLANAR_CORE_H
#define COIN_RENDER_ROUGH_PLANAR_CORE_H
#include "rendering/coinrender/CoinRenderPlanarReflectionCore.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"
#include <limits>
#include <cstdint>
#include <cstring>

// Adapted from codex/pbr-clean ce72805f0536f8f0ef07dc5575d301b3ddffa507.
// Finite GPU proxy model over captured geometry, not geometric ray tracing.
// Receiver slots replace presentation flags; no additional capture or planning.
namespace CoinRenderRoughPlanar {
struct Mapping {
  bool enabled=false;
  double proxyHeight=0,scale=1;
  float viewToCanonical[16]={},canonicalToClip[16]={},normalHeight[4]={};
};
// Both published/candidate CPU mappings and native uniform storage are charged.
static const uint64_t workingBytes=2*sizeof(Mapping)+256;
static const uint64_t uniformBytes=256;
inline bool fail(std::string & d,const char * text){d=text;return false;}
inline bool scalar(long double v,float & out) {
  if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<float>::max())return false;
  out=float(v);uint32_t bits=0;std::memcpy(&bits,&out,4);bits&=0x7fffffffu;
  return bits==0?v==0:bits>=0x00800000u&&bits<0x7f800000u;
}
// Similarity cameras preserve angles and ray directions while allowing an
// authored unit conversion. Normalize the Gram test before squaring: neither
// admission nor its tolerance depends on the absolute camera scale.
inline bool uniformViewMatrix(const SbMatrix & matrix) {
  if(!CoinRenderTransformCore::finiteMatrix(matrix)||matrix[0][3]!=0||matrix[1][3]!=0||matrix[2][3]!=0||matrix[3][3]!=1)return false;
  double magnitude=0;
  for(int r=0;r<3;++r)for(int c=0;c<3;++c)magnitude=std::max(magnitude,std::abs(double(matrix[r][c])));
  if(!(magnitude>0))return false;
  double normalized[3][3],gram[3][3]={};
  for(int r=0;r<3;++r)for(int c=0;c<3;++c)normalized[r][c]=double(matrix[r][c])/magnitude;
  for(int r=0;r<3;++r)for(int c=0;c<3;++c)for(int k=0;k<3;++k)gram[r][c]+=normalized[r][k]*normalized[c][k];
  const double squaredScale=(gram[0][0]+gram[1][1]+gram[2][2])/3;
  if(!(squaredScale>0)||!std::isfinite(squaredScale))return false;
  const double tolerance=2.e-5*squaredScale;
  for(int r=0;r<3;++r)for(int c=0;c<3;++c)if(std::abs(gram[r][c]-(r==c?squaredScale:0))>tolerance)return false;
  return true;
}
inline bool prepare(const CoinRenderFramePlan & frame,const SbPlane & plane,
                    const std::vector<uint8_t> & receivers,
                    const CoinRenderRenderStateSnapshot & basis,Mapping & out,std::string & d) {
  Mapping next;
  if(receivers.size()!=frame.draws.size())return fail(d,"Rough planar receiver slots are invalid");
  if(!uniformViewMatrix(basis.view))
    return fail(d,"Rough planar projection requires a finite orthogonal uniformly scaled affine camera");
  const SbVec3f nf=plane.getNormal();
  const SbVec3f groundPoint=nf*plane.getDistanceFromOrigin();
  long double low[3]={},high[3]={};bool any=false;
  for(size_t slot=0;slot<frame.draws.size();++slot) {
    if(receivers[slot])continue;
    const auto & draw=frame.draws[slot];
    if(draw.topology!=CoinRenderPrimitiveTopology::TRIANGLE_LIST||draw.renderStateSlot>=frame.renderStates.size())
      return fail(d,"Rough planar proxy requires captured opaque CAD triangles");
    const auto & state=frame.renderStates[draw.renderStateSlot];
    if(state.transparentMaterial||state.transparentTexture||state.model[0][3]!=0||state.model[1][3]!=0||state.model[2][3]!=0||state.model[3][3]!=1)
      return fail(d,"Rough planar proxy requires finite opaque affine geometry");
    if(draw.geometry.firstIndex>frame.indices.size()||draw.geometry.indexCount>frame.indices.size()-draw.geometry.firstIndex)
      return fail(d,"Rough planar proxy index range is invalid");
    for(uint32_t i=0;i<draw.geometry.indexCount;++i) {
      const auto index=frame.indices[size_t(draw.geometry.firstIndex)+i];
      if(index>=frame.vertices.size())return fail(d,"Rough planar proxy vertex is invalid");
      const auto & p=frame.vertices[index];long double world[3];
      for(int c=0;c<3;++c) {
        world[c]=state.model[3][c];
        for(int a=0;a<3;++a)world[c]+=static_cast<long double>(p.position[a])*state.model[a][c];
        if(!std::isfinite(world[c]))return fail(d,"Rough planar proxy world bounds are nonfinite");
        if(!any)low[c]=high[c]=world[c];else {low[c]=std::min(low[c],world[c]);high[c]=std::max(high[c],world[c]);}
      }
      any=true;
    }
  }
  if(!any)return fail(d,"Rough planar proxy has no captured CAD bounds");
  long double center[3],extent=0,height=0;
  for(int a=0;a<3;++a){center[a]=low[a]+(high[a]-low[a])/2;extent=std::max(extent,high[a]-low[a]);height+=(center[a]-groundPoint[a])*nf[a];}
  if(!std::isfinite(extent)||extent<=0||!std::isfinite(height)||std::abs(height)>std::numeric_limits<double>::max())
    return fail(d,"Rough planar proxy extent or height is not representable");
  int exponent=0;std::frexp(extent,&exponent);next.scale=std::ldexp(1.,exponent-1);
  if(!std::isfinite(next.scale)||next.scale<=0)return fail(d,"Rough planar canonical scale is not representable");
  next.proxyHeight=double(height);
  if(!scalar(height/next.scale,next.normalHeight[3]))return fail(d,"Rough planar proxy height is outside normal binary32 range");
  for(int a=0;a<3;++a)if(!scalar(nf[a],next.normalHeight[a]))return fail(d,"Rough planar normal is outside normal binary32 range");
  const SbMatrix inverse=basis.view.inverse();
  // SbMatrix's float inverse can return its input after determinant range
  // failure. Refuse that unrepresentable case rather than use a false inverse.
  if(!CoinRenderTransformCore::finiteMatrix(inverse))return fail(d,"Rough planar camera inverse is nonfinite");
  for(int r=0;r<3;++r)for(int c=0;c<3;++c) {
    long double product=0;for(int k=0;k<3;++k)product+=static_cast<long double>(basis.view[r][k])*inverse[k][c];
    if(!std::isfinite(product)||std::abs(product-(r==c?1.L:0.L))>4.e-5L)
      return fail(d,"Rough planar camera inverse cannot certify the finite similarity transform");
  }
  for(int r=0;r<4;++r)for(int c=0;c<4;++c) {
    const long double v=c<3?(static_cast<long double>(inverse[r][c])-(r==3?center[c]:0))/next.scale:inverse[r][c];
    if(!scalar(v,next.viewToCanonical[4*r+c]))return fail(d,"Rough planar view transform exceeds the finite binary32 profile");
  }
  // The producer's far extension changes only clip Z. XY/W below are exactly
  // those of its actual reflected camera, at every reflectionScale.
  const SbMatrix reflected=coin_render_plane_reflection(plane)*basis.view;
  long double view[4][4]={};
  for(int r=0;r<4;++r)for(int c=0;c<4;++c) {
    if(r<3)view[r][c]=static_cast<long double>(next.scale)*reflected[r][c];
    else {view[r][c]=reflected[3][c];for(int a=0;a<3;++a)view[r][c]+=center[a]*reflected[a][c];}
  }
  for(int r=0;r<4;++r)for(int c=0;c<4;++c) {
    // Z is unused for UV or positive-W admission; do not claim the old far.
    long double v=0;if(c!=2)for(int a=0;a<4;++a)v+=view[r][a]*basis.projectionCoin[a][c];
    if(!scalar(v,next.canonicalToClip[4*r+c]))return fail(d,"Rough planar source projection exceeds the finite binary32 profile");
  }
  next.enabled=true;out=next;d.clear();return true;
}
}
#endif
