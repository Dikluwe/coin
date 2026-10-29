#ifndef COIN_RENDER_PLANAR_REFLECTION_CORE_H
#define COIN_RENDER_PLANAR_REFLECTION_CORE_H
// Adapted from codex/pbr-clean ce72805f: camera reflection and far-plane fit.
// Mechanical pass lowering of captured values; no scene action or planning.
#include "CoinRenderFramePlan.h"
#include <algorithm>
#include <cmath>
inline SbMatrix coin_render_plane_reflection(const SbPlane & plane) {
  const SbVec3f n=plane.getNormal();SbMatrix matrix=SbMatrix::identity();
  for(int r=0;r<3;++r)for(int c=0;c<3;++c)matrix[r][c]-=2*n[r]*n[c];
  for(int c=0;c<3;++c)matrix[3][c]=2*plane.getDistanceFromOrigin()*n[c];
  return matrix;
}
inline bool coin_render_reflection_projection(SbMatrix & projection,double farDepth,std::string & error) {
  const double a=projection[2][2],b=projection[3][2];
  const bool perspective=projection[2][3]==-1&&projection[3][3]==0;
  const bool ortho=projection[2][3]==0&&projection[3][3]==1;
  if((!perspective&&!ortho)||a==0) {error="Planar reflection requires a standard perspective/orthographic projection";return false;}
  const double near=perspective?b/(a-1):(b+1)/a;
  const double priorFar=perspective?b/(a+1):(b-1)/a;
  const double far=std::max(priorFar,farDepth*1.01);
  if(!std::isfinite(far)||!std::isfinite(near)||(perspective&&near<=0)||far<=near) {error="Invalid reflected camera depth extent";return false;}
  if(far>priorFar) {
    projection[2][2]=float(perspective?-(far+near)/(far-near):-2/(far-near));
    projection[3][2]=float(perspective?-2*far*near/(far-near):-(far+near)/(far-near));
  }
  return true;
}
#endif
