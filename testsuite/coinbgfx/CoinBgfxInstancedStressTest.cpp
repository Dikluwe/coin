#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include <Inventor/SoDB.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace {
constexpr unsigned spanCount=40000;
constexpr unsigned changedSpan=39998;
struct Options { bool measure=false; unsigned iterations=1; std::string layout="both"; };
bool check(bool value,const std::string & label)
{
  if (!value) std::cerr << "CoinBgfxInstancedStressTest: " << label << '\n';
  return value;
}
bool near(float a,float b)
{
  return std::isfinite(a) && std::isfinite(b) &&
    std::abs(a-b)<=2e-5f*std::max(1.f,std::abs(b));
}

CoinRenderFramePlan fixture(bool alternating)
{
  CoinRenderFramePlan f;
  f.revision=202;
  f.clearColor=SbColor4f(.125f,.25f,.375f,1);
  f.materials.resize(3);
  f.renderStates.resize(3);
  f.lightingStates.emplace_back();
  f.cameras.emplace_back();
  CoinRenderViewportSnapshot vp; vp.width=32; vp.height=24;
  f.viewports.push_back(vp);
  const float scales[3][3]={{-2,3,.5f},{2,.25f,4},{-1,2,3}};
  for (unsigned i=0;i<3;++i) {
    auto & s=f.renderStates[i];
    s.lightModel=CoinRenderLightModel::PHONG;
    s.materialSlot=i;
    s.cullMode=CoinRenderCullMode::BACK;
    s.frontFace=CoinRenderFrontFace::CCW;
    s.model.setScale(SbVec3f(scales[i]));
    s.model[3][0]=float(i)*.125f;
    s.model[3][1]=-.25f;
    s.model[3][2]=.5f;
    s.view.setTranslate(SbVec3f(-.5f,.75f,-3));
    f.materials[i].diffuse[0]=.25f+float(i)*.125f;
    f.materials[i].ambient[1]=.125f+float(i)*.0625f;
    f.materials[i].specular[2]=.5f+float(i)*.125f;
    f.materials[i].emission[0]=float(i)*.03125f;
    f.materials[i].shininess=.25f+float(i)*.125f;
  }
  // Every key and physical source range is different. Padding before and
  // between ranges prevents accidental interpretation as packed zero offsets.
  f.vertices.resize(3+spanCount*5);
  f.indices.resize(5+spanCount*15,0);
  f.draws.resize(spanCount);
  const float corners[4][3]={{-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}};
  const unsigned order[12]={2,2,2,2,0,1,0,3,1,1,3,2};
  for (unsigned span=0;span<spanCount;++span) {
    auto & d=f.draws[span];
    d.renderStateSlot=span%3;
    d.sourceNodeId=901; d.sourceRevision=77;
    d.geometry.firstVertex=3+span*5;
    d.geometry.vertexCount=4;
    d.geometry.firstIndex=5+span*15;
    d.geometry.indexCount=12;
    const float extent[3]={.5f+float(span%257)/256.f,
                           .75f+float(span%131)/128.f,
                           .25f+float(span%97)/64.f};
    for (unsigned j=0;j<4;++j) {
      auto & v=f.vertices[d.geometry.firstVertex+j];
      v.materialSlot=span%3;
      for (unsigned axis=0;axis<3;++axis) {
        v.position[axis]=corners[j][axis]*extent[axis];
        v.normal[axis]=float(axis+1)*.125f;
      }
    }
    for (unsigned j=0;j<12;++j) {
      const unsigned lane=alternating && span%2 ? (j/3)*3+2-j%3 : j;
      f.indices[d.geometry.firstIndex+j]=d.geometry.firstVertex+order[lane];
    }
  }
  return f;
}

// Decode every consumed index for every instance and compare directly with
// authored positions/normals/materials. No general lowering or baseline image
// participates in this oracle, and hashes are never evidence of correctness.
bool oracle(const CoinRenderFramePlan & f,const CoinBgfxPlan & p,const std::string & label)
{
  if (!check(p.usesInstancing && p.instances.size()==spanCount,label+": instance count")) return false;
  size_t ordinal=0;
  for (const auto & batch:p.draws) {
    if (!check(batch.firstInstance==ordinal && batch.sourceDrawSlot==ordinal &&
      batch.instanceCount && ordinal+batch.instanceCount<=f.draws.size(),label+": ordered batch extent")) return false;
    for (unsigned k=0;k<batch.instanceCount;++k,++ordinal) {
      const auto & d=f.draws[ordinal];
      const auto & s=f.renderStates[d.renderStateSlot];
      const auto & instance=p.instances[ordinal];
      const auto & material=f.materials[f.vertices[f.indices[d.geometry.firstIndex]].materialSlot];
      if (!check(batch.indexCount==d.geometry.indexCount &&
        uint64_t(batch.firstIndex)+batch.indexCount<=p.indices.size() &&
        batch.cullMode==s.cullMode && batch.frontFace==s.frontFace &&
        batch.depthFunction==s.depthFunction && batch.depthTest && batch.depthWrite && !batch.blend,
        label+": draw state")) return false;
      const float * attrs[4]={material.diffuse,material.ambient,material.specular,material.emission};
      for (unsigned attr=0;attr<4;++attr)
        if (!check(!std::memcmp(instance.data[6+attr],attrs[attr],4*sizeof(float)),label+": material/order")) return false;
      if (!check(instance.data[3][3]==material.shininess && instance.data[4][3]==1,label+": material scalars")) return false;
      for (unsigned j=0;j<batch.indexCount;++j) {
        const auto & src=f.vertices[f.indices[d.geometry.firstIndex+j]];
        const uint32_t index=p.indices[batch.firstIndex+j];
        if (!check(index>=batch.firstVertex && index<uint64_t(batch.firstVertex)+batch.vertexCount &&
          index<p.instancedVertices.size(),label+": remapped range")) return false;
        const auto & v=p.instancedVertices[index];
        if (!check(!std::memcmp(v.normal,src.normal,sizeof(v.normal)),label+": authored normal")) return false;
        for (unsigned axis=0;axis<3;++axis) {
          float position=instance.data[axis][3], normal=0;
          for (unsigned c=0;c<3;++c) {
            position+=v.position[c]*instance.data[axis][c];
            normal+=v.normal[c]*instance.data[axis+3][c];
          }
          if (p.instancedWorldSpace) position+=s.view[3][axis];
          const float expected=src.position[axis]*s.model[axis][axis]+s.model[3][axis]+s.view[3][axis];
          if (!check(near(position,expected) && near(normal,src.normal[axis]/s.model[axis][axis]),
                     label+": authored transformed position/normal")) return false;
        }
      }
    }
  }
  return check(ordinal==spanCount,label+": complete ordered coverage");
}

bool accepted(const CoinRenderFramePlan & f,const Options & options,const std::string & layout,
              const std::string & phase,unsigned iteration,bool original)
{
  CoinBgfxPlan p;
  std::string diagnostic;
  // Fresh output on every call. Setup, validation oracle and destruction are
  // deliberately outside the measured interval; no warm-up calls are made.
  const auto begin=std::chrono::steady_clock::now();
  const bool result=CoinBgfxLowering::lowerInstanced(f,32,24,false,p,diagnostic);
  const auto end=std::chrono::steady_clock::now();
  if (options.measure)
    std::cout << "COIN_INSTANCED_CPU layout=" << layout << " phase=" << phase
      << " iteration=" << iteration << " spans=" << spanCount
      << " lowering_ms=" << std::chrono::duration<double,std::milli>(end-begin).count()
      << " accepted=" << result << '\n';
  if (!check(result,layout+" "+phase+": acceptance: "+diagnostic)) return false;
  if (original && !check(p.instancedVertices.size()==(layout=="alternating" ? 8u : 4u) &&
      p.indices.size()==(layout=="alternating" ? 24u : 12u) &&
      p.draws.size()==(layout=="alternating" ? spanCount : 1u),layout+": equivalent mesh sharing and runs")) return false;
  return oracle(f,p,layout+" "+phase);
}

bool declined(const CoinRenderFramePlan & f,const std::string & label)
{
  CoinBgfxPlan out;
  // Small published sentinel avoids retaining an unrelated 40000-draw plan.
  out.usesInstancing=true;
  out.indices={73,19,41};
  out.instancedVertices.resize(1);
  out.instancedVertices[0].position[0]=.375f;
  out.instances.resize(1);
  out.instances[0].data[6][0]=.8125f;
  out.clearColor[0]=.625f;
  std::string diagnostic;
  return check(!CoinBgfxLowering::lowerInstanced(f,32,24,false,out,diagnostic) &&
    out.usesInstancing && out.indices==std::vector<uint32_t>({73,19,41}) &&
    out.instancedVertices.size()==1 && out.instancedVertices[0].position[0]==.375f &&
    out.instances.size()==1 && out.instances[0].data[6][0]==.8125f &&
    out.draws.empty() && out.clearColor[0]==.625f,label+": refusal without publication");
}

bool invalidLateSpans(CoinRenderFramePlan & f,const std::string & layout)
{
  bool ok=true;
  for (unsigned span:{1u,20000u,39999u}) {
    const auto & range=f.draws[span].geometry;
    auto & v=f.vertices[range.firstVertex+3];
    const auto saved=v;
    for (float bad:{std::nextafter(1e8f,std::numeric_limits<float>::infinity()),
                   std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
      v.position[0]=bad; ok &= declined(f,layout+": late position span "+std::to_string(span)); v=saved;
      v.normal[1]=bad; ok &= declined(f,layout+": late normal span "+std::to_string(span)); v=saved;
    }
    v.materialSlot=(saved.materialSlot+1)%3;
    ok &= declined(f,layout+": late nonuniform vertex material"); v=saved;
    const unsigned indexOffset=range.firstIndex+11;
    const uint32_t savedIndex=f.indices[indexOffset];
    for (uint32_t index:{range.firstVertex-1,range.firstVertex+range.vertexCount,UINT32_MAX}) {
      f.indices[indexOffset]=index;
      ok &= declined(f,layout+": late invalid index after repeated references");
      f.indices[indexOffset]=savedIndex;
    }
  }
  // This material is consumed only by the very last occurrence. A finite
  // geometry prefix and unrelated shared materials cannot authorize its data.
  const auto & last=f.draws.back().geometry;
  uint32_t savedSlots[4];
  for (unsigned i=0;i<4;++i) {
    auto & v=f.vertices[last.firstVertex+i]; savedSlots[i]=v.materialSlot; v.materialSlot=3;
  }
  for (unsigned attribute=0;attribute<5;++attribute) {
    auto & material=f.materials[3];
    const auto saved=material;
    float * attrs[]={material.diffuse,material.ambient,material.specular,material.emission,&material.shininess};
    attrs[attribute][0]=std::numeric_limits<float>::quiet_NaN();
    ok &= declined(f,layout+": last-span nonfinite material");
    material=saved;
  }
  for (unsigned i=0;i<4;++i) f.vertices[last.firstVertex+i].materialSlot=savedSlots[i];
  return ok;
}

bool run(const Options & options,bool alternating)
{
  const std::string layout=alternating ? "alternating" : "same";
  const auto begin=std::chrono::steady_clock::now();
  auto f=fixture(alternating);
  // Reserve all materials before taking addresses; slot 3 belongs to the
  // directed late-span material check and does not change vector ownership.
  f.materials.push_back(CoinRenderMaterialSnapshot());
  const auto end=std::chrono::steady_clock::now();
  if (options.measure)
    std::cout << "COIN_INSTANCED_SETUP layout=" << layout << " setup_ms="
      << std::chrono::duration<double,std::milli>(end-begin).count() << '\n';
  const auto * vertices=f.vertices.data(); const auto * indices=f.indices.data();
  const auto * states=f.renderStates.data(); const auto * materials=f.materials.data();
  const auto & range=f.draws[changedSpan].geometry;
  auto & vertex=f.vertices[range.firstVertex]; const auto savedVertex=vertex;
  const unsigned indexOffset=range.firstIndex+10; const uint32_t savedIndex=f.indices[indexOffset];
  const auto savedMaterial=f.materials[2]; const auto savedModel=f.renderStates[2].model;
  bool ok=true;
  for (unsigned iteration=0;iteration<options.iterations;++iteration) {
    ok &= accepted(f,options,layout,"A",iteration,true);
    vertex.position[0]*=.75f; vertex.normal[1]=.625f;
    f.indices[indexOffset]=range.firstVertex+2;
    f.materials[2].diffuse[2]=.9375f; f.materials[2].shininess=.8125f;
    f.renderStates[2].model[3][0]=1.75f;
    ok &= accepted(f,options,layout,"B",iteration,false);
    vertex=savedVertex; f.indices[indexOffset]=savedIndex;
    f.materials[2]=savedMaterial; f.renderStates[2].model=savedModel;
    ok &= accepted(f,options,layout,"A_restored",iteration,true);
  }
  ok &= invalidLateSpans(f,layout);
  ok &= accepted(f,options,layout,"after_invalid",options.iterations,true);
  ok &= check(f.revision==202 && vertices==f.vertices.data() && indices==f.indices.data() &&
    states==f.renderStates.data() && materials==f.materials.data(),layout+": mutation retained addresses and revision");
  return ok;
}
}
int main(int argc,char ** argv)
{
  Options options;
  for (int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if (arg=="--measure") options.measure=true;
    else if (arg=="--layout" && i+1<argc) options.layout=argv[++i];
    else if (arg=="--iterations" && i+1<argc) {
      char * end=nullptr;
      const unsigned long value=std::strtoul(argv[++i],&end,10);
      if (!end || *end || value==0 || value>20) return 2;
      options.iterations=static_cast<unsigned>(value);
    }
    else return 2;
  }
  if (options.layout!="same" && options.layout!="alternating" && options.layout!="both") return 2;
  SoDB::init();
  bool ok=true;
  if (options.layout!="alternating") ok &= run(options,false);
  if (options.layout!="same") ok &= run(options,true);
  return ok ? 0 : 1;
}
