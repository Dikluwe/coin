#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif
#include "rendering/coinrender/CoinRenderBumpMapCore.h"
#include "rendering/coinrender/CoinRenderDepthCore.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"

#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoCube.h>
#include "actions/CoinRenderActionP.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool check(bool value, const char *message) {
  if (!value) std::cerr << "CoinRenderHotPathContractTest: " << message << '\n';
  return value;
}
CoinRenderVertexSnapshot vertex(float x,float y,float z,uint32_t material,float u) {
  CoinRenderVertexSnapshot v;v.position[0]=x;v.position[1]=y;v.position[2]=z;
  v.normal[2]=1;v.materialSlot=material;v.texcoord[0]=u;v.texcoord[1]=u*.5f;
  v.extraTexcoords[2][0]=u+3;v.textureR[3]=u+7;v.textureQ[3]=u+11;return v;
}
bool near(const CoinRenderVertexSnapshot&a,const CoinRenderVertexSnapshot&b) {
  const SbVec3f pa(a.position),pb(b.position);const float scale=std::max(1.f,std::max(pa.length(),pb.length()));
  return (pa-pb).length()<=2e-6f*scale;
}

bool polygonContract() {
  std::vector<CoinRenderMaterialSnapshot> materials(4);
  for(size_t i=0;i<materials.size();++i){materials[i].diffuse[0]=float(i+1)/5;materials[i].shininess=float(i)/7;}
  const auto a=vertex(-.7f,-.6f,-.03f,0,.1f), b=vertex(.7f,-.6f,.03f,1,.2f),
             c=vertex(.7f,.6f,-.03f,2,.3f), d=vertex(-.7f,.6f,.03f,3,.4f);
  std::vector<CoinRenderVertexSnapshot> ring{a,b,c,d};
  auto a2=a,c2=c;
  // Internal diagonal endpoints differ but remain within the documented
  // relative coincidence tolerance.  They must cancel without aliasing the
  // authored attributes carried by the four outer directed edges.
  a2.position[0]+=5e-7f;c2.position[1]-=5e-7f;
  std::vector<CoinRenderVertexSnapshot> triangles{a,b,c,a2,c2,d};
  CoinRenderRenderStateSnapshot state;state.lightModel=CoinRenderLightModel::BASE_COLOR;
  state.cullMode=CoinRenderCullMode::NONE;state.polygonOffsetEnabled=true;
  state.polygonOffsetStyles=2;state.polygonOffsetFactor=1;
  CoinRenderViewportSnapshot viewport;viewport.width=640;viewport.height=480;
  CoinRenderPolygonStyleResult out;CoinRenderLightingSnapshot lighting;std::string diagnostic;
  if(!check(coin_render_prepare_polygon_style(ring,state,materials,lighting,
      CoinRenderPolygonStyle::LINES,out,diagnostic,viewport,false,&triangles),diagnostic.c_str()))return false;
  bool ok=check(out.vertices.size()==8&&out.indices.size()==8,"four boundary edges must survive exactly once");
  const std::vector<CoinRenderVertexSnapshot> authored{a,b,c,d};
  bool seen[4]={false,false,false,false};
  for(size_t edge=0;edge<out.vertices.size();edge+=2){
    const auto &first=out.vertices[edge].vertex,&second=out.vertices[edge+1].vertex;
    size_t slot=4;for(size_t i=0;i<4;++i)if(near(first,authored[i])&&near(second,authored[(i+1)%4]))slot=i;
    ok&=check(slot<4,"boundary direction/order changed or diagonal leaked");if(slot>=4)continue;
    seen[slot]=true;
    ok&=check(first.materialSlot==authored[slot].materialSlot &&
      first.texcoord[0]==authored[slot].texcoord[0] &&
      first.extraTexcoords[2][0]==authored[slot].extraTexcoords[2][0] &&
      first.textureR[3]==authored[slot].textureR[3] && first.textureQ[3]==authored[slot].textureQ[3],
      "boundary reconstruction changed per-occurrence attributes");
  }
  for(bool value:seen)ok&=check(value,"authored boundary edge missing");
  return ok;
}

CoinRenderFramePlan depthPlan(const std::vector<float>&slopes) {
  CoinRenderFramePlan p;p.revision=1;p.materials.push_back({});p.lightingStates.push_back({});p.cameras.push_back({});
  CoinRenderViewportSnapshot vp;vp.width=vp.height=1024;p.viewports.push_back(vp);
  CoinRenderRenderStateSnapshot s;s.lightModel=CoinRenderLightModel::BASE_COLOR;s.cullMode=CoinRenderCullMode::NONE;
  s.polygonOffsetEnabled=true;s.polygonOffsetStyles=1;s.polygonOffsetPrimitiveStyle=1;s.polygonOffsetFactor=1;s.polygonOffsetMaxDepth=-1;
  p.renderStates.push_back(s);
  // Reuse exactly the same projected triangle so equal slope inputs produce
  // bit-identical resolved states.  Translated floating-point coordinates can
  // introduce harmless rounding differences and are intentionally not merged.
  for(size_t i=0;i<slopes.size();++i){float x=0;uint32_t base=uint32_t(p.vertices.size());
    p.vertices.push_back(vertex(x,-.2f,0,0,0));p.vertices.push_back(vertex(x+.1f,-.2f,.1f*slopes[i],0,0));p.vertices.push_back(vertex(x,.2f,0,0,0));
    p.indices.insert(p.indices.end(),{base,base+1,base+2});}
  CoinRenderDrawPacket d;d.topology=CoinRenderPrimitiveTopology::TRIANGLE_LIST;d.geometry.vertexCount=p.vertices.size();d.geometry.indexCount=p.indices.size();
  d.clearDepthBefore=true;d.stableNodeId=91;d.sourceRevision=92;d.drawOrdinal=93;p.draws.push_back(d);return p;
}
bool depthContract(){bool ok=true;std::string why;
  auto equal=depthPlan({1,1,1,1});ok&=check(coin_render_resolve_triangle_depth(equal,why),why.c_str());
  ok&=check(equal.draws.size()==1&&equal.draws[0].geometry.firstIndex==0&&equal.draws[0].geometry.indexCount==12,
            "contiguous triangles with one resolved state must stay one range");
  ok&=check(equal.draws[0].clearDepthBefore,"merged range lost the source depth clear");
  auto split=depthPlan({1,1,2,1});why.clear();ok&=check(coin_render_resolve_triangle_depth(split,why),why.c_str());
  ok&=check(split.draws.size()==3&&split.draws[0].geometry.indexCount==6&&split.draws[1].geometry.indexCount==3&&split.draws[2].geometry.indexCount==3,
            "only adjacent triangles with bit-identical resolved state may merge");
  ok&=check(split.draws[0].clearDepthBefore&&!split.draws[1].clearDepthBefore&&!split.draws[2].clearDepthBefore,
            "split ranges changed clearDepth ordering");return ok;}

CoinRenderFramePlan reusePlan(uint64_t revision){auto p=depthPlan({0});p.revision=revision;p.renderStates[0].polygonOffsetEnabled=false;p.renderStates[0].polygonOffsetFactor=0;p.renderStates[0].polygonOffsetMaxDepth=0;return p;}
bool reuseContract(){bool ok=true;auto a=reusePlan(10),b=a;b.revision=11;
 ok&=check(CoinRenderFrameReuseCore::classify(a,b).kind==CoinRenderFrameReuseKind::REUSE,"identical payload is REUSE");
 SbMatrix view=SbMatrix::identity();view[3][0]=.125f;b.cameras[0].viewMatrix=view;b.renderStates[0].view=view;
 ok&=check(CoinRenderFrameReuseCore::classify(a,b).kind==CoinRenderFrameReuseKind::CAMERA_PATCH,"camera-only payload is CAMERA_PATCH");
 b=a;b.revision=12;b.vertices[0].position[0]+=.01f;
 ok&=check(CoinRenderFrameReuseCore::classify(a,b).kind==CoinRenderFrameReuseKind::RESOURCE_REBUILD,"same structure changed geometry is RESOURCE_REBUILD");
 b=a;b.revision=13;b.draws[0].geometry.indexCount=0;
 ok&=check(CoinRenderFrameReuseCore::classify(a,b).kind==CoinRenderFrameReuseKind::FULL_REBUILD,"changed draw structure is FULL_REBUILD");return ok;}

bool textureCaptureContract(){
  auto *root=new SoSeparator;root->ref();root->addChild(new SoOrthographicCamera);
  auto *base=new SoLightModel;base->model=SoLightModel::BASE_COLOR;root->addChild(base);
  auto *texture=new SoTexture2;std::vector<unsigned char> bytes(64*64*4);
  for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>((i*29+17)&255);
  texture->image.setValue(SbVec2s(64,64),4,bytes.data());root->addChild(texture);
  auto *cube=new SoCube;for(unsigned i=0;i<32;++i)root->addChild(cube);
  CoinRenderAction action(SbViewportRegion(128,128));action.getPimpl()->planOnly=true;
  action.setTransparencyType(CoinRenderAction::NONE);action.apply(root);
  bool ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS,"revisioned repeated texture capture failed");
  const auto first=action.getPimpl()->lastValidPlan;
  ok&=check(first.textures.size()==1&&first.textures[0].pixelsRgba.size()==bytes.size(),
            "32 occurrences of one revisioned SoTexture2 must publish one exact image");
  SbVec2s extent;int components=0;const unsigned char *stored=texture->image.getValue(extent,components);
  const SbUniqueId revision=texture->getNodeId();const unsigned char old=stored[37];
  const_cast<unsigned char *>(stored)[37]^=0x5a; // Deliberately bypass notification.
  action.apply(root);const auto second=action.getPimpl()->lastValidPlan;
  ok&=check(texture->getNodeId()==revision,"silent byte mutation unexpectedly changed source revision");
  ok&=check(action.getLastStatus()==CoinRenderAction::SUCCESS&&second.textures.size()==1&&
            second.textures[0].pixelsRgba[37]==static_cast<unsigned char>(old^0x5a)&&
            second.textures[0].contentDigest!=first.textures[0].contentDigest,
            "unnotified texture bytes must not be hidden by the revision cache");
  const_cast<unsigned char *>(stored)[37]=old;root->unref();return ok;
}

bool bumpSafety(){auto p=reusePlan(50);CoinRenderBumpImage image;image.width=image.height=1;image.rgb={128,128,255};p.bumpImages.push_back(image);
 CoinRenderBumpCoordOverride coord;coord.vertexIndex=0;coord.imageSlot=1;p.bumpCoords.push_back(coord);std::string why;
 auto invalid=p;invalid.bumpCoords[0].vertexIndex=uint32_t(invalid.vertices.size());
 bool ok=check(!coin_render_validate_extended_bump(invalid,why)&&!why.empty(),"standalone extended bump validator must reject invalid public input");
 auto copied=p;copied.bumpTransforms.reserve(4096);for(unsigned i=0;i<4096;++i){SbMatrix m=SbMatrix::identity();m[3][0]=float(i+1);copied.bumpTransforms.push_back(m);}copied.bumpCoords[0].transformSlot=4096;
 ok&=check(copied.bumpTransforms.size()==4096&&copied.bumpCoords[0].transformSlot==4096,"1-based bump transform slots survive plan copy");return ok;}
}
int main(){
  SoDB::init();
  CoinRenderAction::initClass();
  if (!polygonContract()) return 1;
  if (!depthContract()) return 1;
  if (!reuseContract()) return 1;
  if (!textureCaptureContract()) return 1;
  if (!bumpSafety()) return 1;
  return 0;
}
