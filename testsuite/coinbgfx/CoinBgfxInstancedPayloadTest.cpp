#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include <Inventor/SoDB.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace {
bool check(bool value, const std::string & label)
{
  if (!value) std::cerr << "CoinBgfxInstancedPayloadTest: " << label << '\n';
  return value;
}
template<class T> bool bytes(const std::vector<T> & a, const std::vector<T> & b)
{
  return a.size() == b.size() && (a.empty() || !std::memcmp(a.data(), b.data(), a.size()*sizeof(T)));
}
bool near(float a, float b)
{
  return std::isfinite(a) && std::isfinite(b) &&
    std::abs(a-b) <= 2e-5f * std::max(1.f, std::abs(b));
}

// Three separately stored, positively scaled tetrahedra. Indices begin with
// repeats and visit vertices in a non-storage order. Padding is intentionally
// inside the buffers but outside the consumed ranges.
CoinRenderFramePlan fixture()
{
  CoinRenderFramePlan f;
  f.revision = 101;
  f.clearColor = SbColor4f(.1f,.2f,.3f,1);
  f.materials.resize(3);
  f.lightingStates.emplace_back();
  f.cameras.emplace_back();
  CoinRenderViewportSnapshot vp; vp.width=32; vp.height=24;
  f.viewports.push_back(vp);
  f.vertices.resize(18);
  f.indices.resize(39, 0);
  const float corners[4][3]={{-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}};
  const uint32_t order[12]={2,2,2,2,0,1,0,3,1,1,3,2};
  for (unsigned span=0; span<3; ++span) {
    f.materials[span].diffuse[0]=.2f+.2f*span;
    f.materials[span].ambient[1]=.1f+.1f*span;
    f.materials[span].specular[2]=.3f+.1f*span;
    f.materials[span].emission[0]=.03f*span;
    f.materials[span].shininess=.15f+.1f*span;
    for (unsigned v=0; v<4; ++v) {
      auto & dst=f.vertices[2+span*5+v];
      dst.materialSlot=span;
      for (unsigned axis=0; axis<3; ++axis) {
        dst.position[axis]=corners[v][axis]*float((span+1)*(axis+1));
        dst.normal[axis]=float(axis+1)*.125f;
      }
    }
    for (unsigned j=0; j<12; ++j) f.indices[3+span*12+j]=2+span*5+order[j];
  }
  f.renderStates.resize(300);
  f.draws.resize(300);
  for (unsigned i=0; i<300; ++i) {
    auto & s=f.renderStates[i];
    s.lightModel=CoinRenderLightModel::PHONG;
    s.cullMode=CoinRenderCullMode::BACK;
    s.frontFace=CoinRenderFrontFace::CCW;
    s.materialSlot=i%3;
    s.model.setScale(SbVec3f(i%2 ? -2.f : 2.f, 3.f, .5f));
    s.model[3][0]=float(i)*.03125f;
    s.model[3][1]=-.25f;
    s.model[3][2]=.125f;
    s.view.setTranslate(SbVec3f(-.5f,.75f,-3.f));
    auto & d=f.draws[i];
    d.renderStateSlot=i; d.sourceNodeId=1000+i; d.sourceRevision=77;
    d.geometry.firstVertex=2+(i%3)*5; d.geometry.vertexCount=4;
    d.geometry.firstIndex=3+(i%3)*12; d.geometry.indexCount=12;
  }
  return f;
}

// CPU oracle from authored occurrences, independent of the ordinary lowerer,
// remap layout, hash, diagonal normalization and batching implementation.
bool oracle(const CoinRenderFramePlan & f, const CoinBgfxPlan & p, const std::string & label)
{
  if (!check(p.usesInstancing && p.instances.size()==f.draws.size(), label+": instance count")) return false;
  size_t ordinal=0;
  for (const auto & batch:p.draws) {
    if (!check(batch.firstInstance==ordinal && batch.instanceCount>0 &&
               batch.sourceDrawSlot==ordinal && ordinal+batch.instanceCount<=f.draws.size(), label+": ordered batches")) return false;
    for (unsigned k=0;k<batch.instanceCount;++k,++ordinal) {
      const auto & d=f.draws[ordinal];
      const auto & s=f.renderStates[d.renderStateSlot];
      const auto & instance=p.instances[ordinal];
      const auto & material=f.materials[f.vertices[f.indices[d.geometry.firstIndex]].materialSlot];
      if (!check(batch.indexCount==d.geometry.indexCount &&
                 uint64_t(batch.firstIndex)+batch.indexCount<=p.indices.size() &&
                 batch.cullMode==s.cullMode && batch.frontFace==s.frontFace &&
                 batch.depthFunction==s.depthFunction && batch.depthTest && batch.depthWrite && !batch.blend,
                 label+": draw metadata")) return false;
      const float * attrs[4]={material.diffuse,material.ambient,material.specular,material.emission};
      for (unsigned a=0;a<4;++a)
        if (!check(!std::memcmp(instance.data[6+a],attrs[a],4*sizeof(float)), label+": material order/value")) return false;
      if (!check(instance.data[3][3]==material.shininess && instance.data[4][3]==1, label+": shininess/PHONG")) return false;
      const double determinant=double(s.model[0][0])*s.model[1][1]*s.model[2][2];
      for (unsigned j=0;j<batch.indexCount;++j) {
        const auto & src=f.vertices[f.indices[d.geometry.firstIndex+j]];
        const uint32_t index=p.indices[batch.firstIndex+j];
        if (!check(index>=batch.firstVertex && index<uint64_t(batch.firstVertex)+batch.vertexCount && index<p.instancedVertices.size(), label+": remapped index bounds")) return false;
        const auto & v=p.instancedVertices[index];
        if (!check(!std::memcmp(v.normal,src.normal,sizeof(v.normal)), label+": authored normal attribute")) return false;
        for (unsigned axis=0;axis<3;++axis) {
          float actual=instance.data[axis][3];
          float normal=0;
          for (unsigned c=0;c<3;++c) {
            actual+=v.position[c]*instance.data[axis][c];
            normal+=v.normal[c]*instance.data[axis+3][c];
          }
          if (p.instancedWorldSpace) actual+=s.view[3][axis];
          const float expected=src.position[axis]*s.model[axis][axis]+s.model[3][axis]+s.view[3][axis];
          const float expectedNormal=std::abs(determinant)<=1e-12 ? src.normal[axis] : src.normal[axis]/s.model[axis][axis];
          if (!check(near(actual,expected) && near(normal,expectedNormal),label+": authored position/normal reconstruction at occurrence "+std::to_string(ordinal))) return false;
        }
      }
    }
  }
  return check(ordinal==f.draws.size(),label+": complete ordered coverage");
}
bool accepted(const CoinRenderFramePlan & f,const std::string & label, bool world=true)
{
  CoinBgfxPlan p; std::string diagnostic;
  if (!check(CoinBgfxLowering::lowerInstanced(f,32,24,false,p,diagnostic,nullptr,world),label+": accepted: "+diagnostic)) return false;
  return oracle(f,p,label);
}
bool exactFirstMesh(const CoinRenderFramePlan & f,const std::string & label)
{
  CoinBgfxPlan p; std::string diagnostic;
  if (!check(CoinBgfxLowering::lowerInstanced(f,32,24,false,p,diagnostic),label+": exact fallback accepted")) return false;
  const auto & source=f.draws.front().geometry;
  const auto & output=p.draws.front();
  for (unsigned j=0;j<source.indexCount;++j) {
    const auto & a=f.vertices[f.indices[source.firstIndex+j]];
    const auto & b=p.instancedVertices[p.indices[output.firstIndex+j]];
    if (!check(!std::memcmp(a.position,b.position,sizeof(a.position)),label+": exact position bytes")) return false;
  }
  return oracle(f,p,label);
}
bool declined(const CoinRenderFramePlan & f,const CoinBgfxPlan & seed,const std::string & label)
{
  CoinBgfxPlan out=seed; std::string diagnostic;
  const bool result=CoinBgfxLowering::lowerInstanced(f,32,24,false,out,diagnostic);
  return check(!result && out.usesInstancing==seed.usesInstancing &&
    out.instancedWorldSpace==seed.instancedWorldSpace &&
    out.instancedCameraPatchable==seed.instancedCameraPatchable &&
    bytes(out.instancedVertices,seed.instancedVertices) && bytes(out.instances,seed.instances) &&
    bytes(out.instancePositionScales,seed.instancePositionScales) &&
    bytes(out.vertices,seed.vertices) && bytes(out.packedVertices,seed.packedVertices) &&
    bytes(out.indices,seed.indices) && bytes(out.draws,seed.draws) &&
    !std::memcmp(out.clearColor,seed.clearColor,sizeof(out.clearColor)),label+": decline leaves published output intact");
}

bool mutablePayload()
{
  auto f=fixture(); CoinBgfxPlan p; std::string diagnostic;
  bool ok=check(CoinBgfxLowering::lowerInstanced(f,32,24,false,p,diagnostic),"initial lowering");
  if (!ok) return false;
  ok &= oracle(f,p,"initial");
  ok &= check(p.instancedVertices.size()==4 && p.indices.size()==12 && p.draws.size()==1,"equivalent distinct ranges share normalized mesh");
  const auto * vertexAddress=f.vertices.data(); const auto * indexAddress=f.indices.data();
  const auto * stateAddress=f.renderStates.data(); const auto * materialAddress=f.materials.data();
  const auto original=f.vertices[7];
  const CoinBgfxPlan initial=p;
  // Same address, revision and source identity across A/B/A, on a reused output.
  for (unsigned cycle=0;cycle<3;++cycle) {
    if (cycle==1) { f.vertices[7].position[0]*=.75f; f.vertices[7].normal[1]=.625f; }
    else f.vertices[7]=original;
    ok &= check(CoinBgfxLowering::lowerInstanced(f,32,24,false,p,diagnostic),"mutable A/B/A accepted");
    ok &= oracle(f,p,"mutable A/B/A");
    if (cycle!=1) ok &= check(bytes(p.instancedVertices,initial.instancedVertices) &&
      bytes(p.indices,initial.indices) && bytes(p.instances,initial.instances),"A/B/A restores exact payload");
    ok &= check(cycle==1 ? p.instancedVertices.size()==8 && p.draws.size()==201 : p.instancedVertices.size()==4 && p.draws.size()==1,"mutable A/B/A mesh identity and ordered runs");
  }
  const auto originalIndex=f.indices[26];
  f.indices[26]=f.indices[25];
  ok &= accepted(f,"same-address valid index mutation");
  f.indices[26]=originalIndex;
  f.materials[1].diffuse[2]=.95f; f.materials[1].shininess=.8f;
  f.renderStates[25].model[3][0]=13.f;
  ok &= accepted(f,"same-address material and transform mutation");
  ok &= check(vertexAddress==f.vertices.data() && indexAddress==f.indices.data() &&
    stateAddress==f.renderStates.data() && materialAddress==f.materials.data() && f.revision==101,"mutation fixture retained addresses/revision");
  return ok;
}

bool spansAndFactors()
{
  bool ok=true;
  auto f=fixture();
  // Same first index, different count; overlapping index and vertex ranges.
  for (unsigned i=0;i<f.draws.size();++i) {
    auto & g=f.draws[i].geometry;
    g.firstVertex=1; g.vertexCount=6;
    g.firstIndex=3+(i%3)*3; g.indexCount=i%3==0 ? 12 : 6;
  }
  ok &= accepted(f,"partial overlap/start/count");
  f=fixture();
  for (float extent:{0.f,std::numeric_limits<float>::denorm_min(),.25f,-.25f}) {
    for (unsigned v=2;v<6;++v) f.vertices[v].position[0]=(v==2 || v==5 ? -extent : extent);
    ok &= accepted(f,"zero/subnormal/asymmetric/reflected extent");
    if (extent==0 || extent==std::numeric_limits<float>::denorm_min())
      ok &= exactFirstMesh(f,"degenerate extent exact fallback");
  }
  f=fixture();
  for (unsigned v=2;v<6;++v) f.vertices[v].position[0]=0.f;
  f.vertices[2].position[0]=-0.f;
  ok &= exactFirstMesh(f,"signed zero exact mesh");
  f=fixture();
  f.vertices[2].position[0]=-.75f; // Interior value forces exact geometry fallback.
  ok &= exactFirstMesh(f,"non-endpoint exact fallback");
  for (auto & state:f.renderStates) state.model.setScale(SbVec3f(0,3,.5f));
  ok &= accepted(f,"singular normal identity fallback");
  f=fixture();
  for (auto & state:f.renderStates) state.depthFunction=CoinRenderDepthFunction::LEQUAL;
  ok &= accepted(f,"LEQUAL eye-space");
  f=fixture(); ok &= accepted(f,"explicit eye-space",false);
  return ok;
}

bool invalidPayload()
{
  auto f=fixture(); CoinBgfxPlan seed; std::string diagnostic;
  if (!check(CoinBgfxLowering::lowerInstanced(f,32,24,false,seed,diagnostic),"rejection seed")) return false;
  bool ok=true;
  const uint32_t saved=f.indices[14];
  for (uint32_t value:{uint32_t(1),uint32_t(6),uint32_t(18),UINT32_MAX}) {
    f.indices[14]=value; // After several repeats; global-valid escapes included.
    ok &= declined(f,seed,"late invalid index");
    f.indices[14]=saved;
    ok &= accepted(f,"index recovery with same revision");
  }
  for (float value:{std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::max(),1.000001e8f}) {
    for (unsigned axis=0;axis<3;++axis) {
      float old=f.vertices[2].position[axis]; f.vertices[2].position[axis]=value;
      ok &= declined(f,seed,"consumed bad position after repeats"); f.vertices[2].position[axis]=old;
      old=f.vertices[2].normal[axis]; f.vertices[2].normal[axis]=value;
      ok &= declined(f,seed,"consumed bad normal after repeats"); f.vertices[2].normal[axis]=old;
    }
  }
  for (unsigned attr=0;attr<4;++attr) {
    float * arrays[]={f.materials[1].diffuse,f.materials[1].ambient,f.materials[1].specular,f.materials[1].emission};
    const float savedValue=arrays[attr][2]; arrays[attr][2]=std::numeric_limits<float>::quiet_NaN();
    ok &= declined(f,seed,"nonfinite consumed material"); arrays[attr][2]=savedValue;
  }
  const auto vertex=f.vertices[2];
  f.vertices[2].materialSlot=1; ok &= declined(f,seed,"mixed consumed material"); f.vertices[2]=vertex;
  f.vertices[2].screenSpaceW=.5f; ok &= declined(f,seed,"expanded screen W"); f.vertices[2]=vertex;
  f.vertices[2].fogEyeDepth=1; ok &= declined(f,seed,"explicit fog depth"); f.vertices[2]=vertex;
  for (unsigned mode=0;mode<12;++mode) {
    auto g=f;
    auto & state=g.renderStates.back();
    switch(mode) {
      case 0: state.fogMode=CoinRenderFogMode::HAZE; break;
      case 1: state.clipPlanesWorld.emplace_back(SbVec3f(1,0,0),0); break;
      case 2: state.hasTexture=true; break;
      case 3: state.depthFunction=CoinRenderDepthFunction::GEQUAL; break;
      case 4: state.cullMode=CoinRenderCullMode::FRONT; break;
      case 5: state.frontFace=CoinRenderFrontFace::CW; break;
      case 6: state.model[0][3]=.25f; break;
      case 7: state.model.setScale(SbVec3f(1e9f,1,1)); break;
      case 8: state.model.setScale(SbVec3f(1e-9f,1,1)); break;
      case 9: g.materials[1].diffuse[3]=.5f; g.materials[1].transparency=.5f; state.transparentMaterial=true; break;
      case 10: g.draws.back().clearDepthBefore=true; break;
      case 11: g.draws.back().renderLayer=1; break;
    }
    ok &= declined(g,seed,"profile refusal "+std::to_string(mode));
  }
  // Valid finite extreme within the source limit, with a safe model product.
  auto extreme=f;
  for (auto & state:extreme.renderStates) state.model=SbMatrix::identity();
  extreme.vertices[2].normal[0]=1e8f;
  extreme.vertices[2].position[0]=1e8f;
  ok &= accepted(extreme,"inclusive magnitude boundary");
  return ok;
}

bool capacities()
{
  auto f=fixture(); CoinBgfxPlan seed; std::string diagnostic;
  if (!CoinBgfxLowering::lowerInstanced(f,32,24,false,seed,diagnostic)) return false;
  bool ok=true;
  f.draws.resize(255); ok &= declined(f,seed,"255 draws");
  f.draws.resize(256,f.draws.front()); ok &= accepted(f,"256 draws");
  f=fixture(); f.vertices.resize(4097);
  for (auto & d:f.draws) { d.geometry.firstVertex=0; d.geometry.vertexCount=4096; d.geometry.firstIndex=3; }
  ok &= accepted(f,"4096 declared vertices");
  for (auto & d:f.draws) d.geometry.vertexCount=4097;
  ok &= declined(f,seed,"4097 declared vertices");
  f=fixture(); f.indices.assign(65538,2);
  for (auto & d:f.draws) { d.geometry.firstVertex=2; d.geometry.vertexCount=4; d.geometry.firstIndex=0; d.geometry.indexCount=65535; }
  CoinBgfxPlan limit;
  ok &= check(CoinBgfxLowering::lowerInstanced(f,32,24,false,limit,diagnostic) &&
    limit.instances.size()==f.draws.size() && limit.indices.size()==65535 &&
    limit.instancedVertices.size()==1 && limit.draws.size()==1 &&
    limit.draws[0].indexCount==65535 &&
    std::all_of(limit.indices.begin(),limit.indices.end(),[](uint32_t index){return index==0;}),
    "largest triangular index count below cap retains all repeated indices");
  for (auto & d:f.draws) d.geometry.indexCount=65538;
  ok &= declined(f,seed,"index count above cap");
  auto distinct=fixture();
  distinct.vertices.resize(257*4);
  distinct.indices.resize(257*12);
  const auto source=fixture();
  for (unsigned mesh=0;mesh<257;++mesh) {
    for (unsigned vertex=0;vertex<4;++vertex) {
      auto & value=distinct.vertices[mesh*4+vertex];
      value=source.vertices[2+vertex];
      value.normal[0]=float(mesh+1)*.001f;
    }
    for (unsigned j=0;j<12;++j) distinct.indices[mesh*12+j]=mesh*4+source.indices[3+j]-2;
  }
  for (unsigned i=0;i<distinct.draws.size();++i) {
    auto & g=distinct.draws[i].geometry;
    g.firstVertex=(i%256)*4; g.vertexCount=4;
    g.firstIndex=(i%256)*12; g.indexCount=12;
  }
  ok &= accepted(distinct,"256 distinct meshes");
  distinct.draws.back().geometry.firstVertex=256*4;
  distinct.draws.back().geometry.firstIndex=256*12;
  ok &= declined(distinct,seed,"257 distinct meshes");
  for (unsigned field=0;field<4;++field) {
    auto g=fixture(); auto & range=g.draws.back().geometry;
    if (field==0) range.firstVertex=UINT32_MAX;
    if (field==1) range.vertexCount=UINT32_MAX;
    if (field==2) range.firstIndex=UINT32_MAX;
    if (field==3) range.indexCount=UINT32_MAX;
    ok &= declined(g,seed,"overflow range "+std::to_string(field));
  }
  return ok;
}
}
int main()
{
  SoDB::init();
  bool ok=mutablePayload();
  ok &= spansAndFactors();
  ok &= invalidPayload();
  ok &= capacities();
  return ok ? 0 : 1;
}
