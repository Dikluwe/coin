// Compile this consumer twice: current retained mapping and the pinned PBR
// source's original mapping. The source build must use its own headers/libs.
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderRoughPlanarCore.h"
#include <Inventor/SbViewVolume.h>
#include <Inventor/SbRotation.h>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <limits>
static void need(bool v,const std::string & message){if(!v)throw std::runtime_error(message);}
int main(){try {
  std::cout<<std::setprecision(17);
  for(int mode=0;mode<12;++mode) {
    const float unit=mode%3==0?1.e-5f:mode%3==1?1.f:1.e5f;
    CoinRenderFramePlan frame;CoinRenderRenderStateSnapshot state;
    state.model=SbMatrix::identity();
    SbViewVolume volume;if(mode<6)volume.perspective(.7f,1.3f,.1f*unit,50*unit);
    else volume.ortho(-5*unit,5*unit,-4*unit,4*unit,-2*unit,40*unit);
    state.view=SbMatrix::identity();state.view[3][1]=-7*unit;state.view[3][2]=-5*unit;
    if(mode%2){SbMatrix rotation;rotation.setRotate(SbRotation(SbVec3f(1,0,0),.3f));state.view=rotation*state.view;}
    state.projectionCoin=volume.getMatrix();frame.renderStates.push_back(state);
    const SbVec3f normal=mode%2?SbVec3f(.6f,0,.8f):SbVec3f(0,0,1);
    const SbVec3f point=normal*(-2*unit);const SbPlane plane(normal,point);
    for(unsigned i=0;i<8;++i){CoinRenderVertexSnapshot v;for(int c=0;c<3;++c)v.position[c]=(i&(1<<c)?2.f:-1.f)*unit;frame.vertices.push_back(v);}
    const uint32_t boxIndices[]={0,1,3,0,3,2,4,6,7,4,7,5,0,4,5,0,5,1,2,3,7,2,7,6,0,2,6,0,6,4,1,5,7,1,7,3};
    frame.indices.assign(boxIndices,boxIndices+36);
    CoinRenderDrawPacket object;object.geometry.firstIndex=0;object.geometry.indexCount=36;object.geometry.vertexCount=8;frame.draws.push_back(object);
    CoinRenderVertexSnapshot vertex;for(int c=0;c<3;++c)vertex.position[c]=point[c];frame.vertices.push_back(vertex);
    SbVec3f tangent=normal.cross(SbVec3f(0,1,0));tangent.normalize();const auto bitangent=normal.cross(tangent);
    for(const auto & offset:{tangent*unit,bitangent*unit}) {auto v=vertex;for(int c=0;c<3;++c)v.position[c]+=offset[c];frame.vertices.push_back(v);}
    CoinRenderDrawPacket floor;floor.geometry.firstVertex=8;floor.geometry.firstIndex=36;floor.geometry.indexCount=3;floor.geometry.vertexCount=3;frame.indices.insert(frame.indices.end(),{8,9,10});frame.draws.push_back(floor);
    CoinRenderRoughPlanar::Mapping mapping;std::string error;
#ifdef COIN_ROUGH_PINNED_REFERENCE
    frame.draws.back().presentationPlanarReflection=true;
    need(CoinRenderRoughPlanar::prepare(frame,floor,normal,mapping,error),error);
#else
    const std::vector<uint8_t> receivers={0,1};
    need(CoinRenderRoughPlanar::prepare(frame,plane,receivers,state,mapping,error),error);
#endif
    need(mapping.enabled&&std::isfinite(mapping.proxyHeight)&&mapping.scale>0,"invalid mapping publication");
    std::cout<<mode<<' '<<mapping.proxyHeight<<' '<<mapping.scale;
    for(float x:mapping.viewToCanonical)std::cout<<' '<<x;
    for(float x:mapping.canonicalToClip)std::cout<<' '<<x;
    for(float x:mapping.normalHeight)std::cout<<' '<<x;
    std::cout<<'\n';
#ifndef COIN_ROUGH_PINNED_REFERENCE
    // Rejection must preserve the prior mapping, including its enable flag.
    auto bad=state;bad.view[0][0]*=2;const auto old=mapping.proxyHeight;
    need(!CoinRenderRoughPlanar::prepare(frame,plane,receivers,bad,mapping,error)&&!error.empty()&&mapping.proxyHeight==old&&mapping.enabled,"non-similarity camera accepted or rejection mutated mapping");
    frame.vertices[0].position[0]=std::numeric_limits<float>::infinity();
    need(!CoinRenderRoughPlanar::prepare(frame,plane,receivers,state,mapping,error)&&!error.empty()&&mapping.proxyHeight==old,"nonfinite bounds accepted or published");
#endif
  }
  return 0;
}catch(const std::exception & e){std::cerr<<e.what()<<'\n';return 1;}}
