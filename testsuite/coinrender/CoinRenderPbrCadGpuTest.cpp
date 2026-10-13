#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/lists/SbList.h>
#include <cmath>
#include <cstdio>
#define CHECK(x,m) do{if(!(x)){std::fprintf(stderr,"FAIL: %s (%d)\n",m,__LINE__);return 1;}}while(0)
static void quad(CoinRenderFramePlan & f,float z,const SbColor & color,uint32_t layer=0) {
  const unsigned material=f.materials.size(),state=f.renderStates.size(),base=f.vertices.size(),first=f.indices.size();
  f.materials.emplace_back();auto & m=f.materials.back();m.model=layer?CoinRenderMaterialModel::LEGACY:CoinRenderMaterialModel::PBR;
  m.pbr.emissive=color;for(int c=0;c<3;++c)m.diffuse[c]=color[c];
  f.renderStates.emplace_back();auto & s=f.renderStates.back();s.materialSlot=material;s.cullMode=CoinRenderCullMode::NONE;
  if(layer)s.lightModel=CoinRenderLightModel::BASE_COLOR;
  const float xy[][2]={{-.95f,-.95f},{.95f,-.95f},{.95f,.95f},{-.95f,.95f}};
  for(int i=0;i<4;++i) {
    CoinRenderVertexSnapshot v;v.position[0]=xy[i][0];v.position[1]=xy[i][1];v.position[2]=z;v.normal[2]=1;v.materialSlot=material;
    f.vertices.push_back(v);
  }
  for(unsigned i:{0u,1u,2u,0u,2u,3u})f.indices.push_back(base+i);
  f.draws.emplace_back();auto & d=f.draws.back();d.geometry.firstVertex=base;d.geometry.vertexCount=4;d.geometry.firstIndex=first;d.geometry.indexCount=6;d.renderStateSlot=state;d.renderLayer=layer;
}
static CoinRenderFramePlan plane() {
  CoinRenderFramePlan f;f.lightingStates.emplace_back();f.cameras.emplace_back();f.cameras[0].isPerspective=false;
  f.viewports.emplace_back();f.viewports[0].width=f.viewports[0].height=32;quad(f,-.5f,SbColor(2,1,.1f));return f;
}
static bool execute(CoinRenderTargetP & t,CoinRenderFramePlan & f) {
  ++f.revision;auto result=t.executeFrame(f);if(result.status!=CoinRenderBackendStatus::SUCCESS)std::fprintf(stderr,"CAD submit: %s\n",result.diagnostic.c_str());return result.status==CoinRenderBackendStatus::SUCCESS;
}
static bool color(const CoinRenderTargetP & t,int x,int y,int r,int g,int b) {
  const auto i=(y*32+x)*4;return std::abs(int(t.colorBuffer[i])-r)<=1&&std::abs(int(t.colorBuffer[i+1])-g)<=1&&std::abs(int(t.colorBuffer[i+2])-b)<=1;
}
int main() {
  SoDB::init();CoinRenderTargetP target(SbVec2i32(32,32));target.options.renderer=COIN_RENDER_RENDERER_VULKAN;
  auto frame=plane();CHECK(execute(target,frame),"PBR base");const auto base=target.colorBuffer;
  quad(frame,-.25f,SbColor(.2f,.7f,.4f),1);
  CHECK(execute(target,frame)&&target.colorBuffer==base,"HDR depth transfer occludes a technical overlay behind the surface");
  for(unsigned i=4;i<8;++i)frame.vertices[i].position[2]=-.75f;
  CHECK(execute(target,frame),"front technical overlay submits");
  std::printf("front overlay RGB %u %u %u depth %g\n",target.colorBuffer[(16*32+16)*4],target.colorBuffer[(16*32+16)*4+1],target.colorBuffer[(16*32+16)*4+2],target.depthBuffer[16*32+16]);
  CHECK(color(target,16,16,51,179,102),"front technical color is emitted once after scene transform");
  CHECK(std::abs(target.depthBuffer[16*32+16]-.125f)<1e-5,"display output publishes overlay depth");
  SbList<SbVec3f> hdr;CHECK(target.backend->readSceneLinear(hdr)&&hdr[16*32+16][0]==2,"technical display color does not contaminate scene-linear radiance");
  CoinRenderTargetP exposed(SbVec2i32(32,32));exposed.options.renderer=COIN_RENDER_RENDERER_VULKAN;exposed.options.pbrExposureStops=2;
  CHECK(execute(exposed,frame)&&color(exposed,16,16,51,179,102),"technical color remains independent of exposure");
  const auto first=target.colorBuffer;CHECK(execute(target,frame)&&target.colorBuffer==first,"overlay replay exact");
  auto annotation=plane();quad(annotation,-.25f,SbColor(0,0,1),1);annotation.draws.back().clearDepthBefore=true;
  CoinRenderViewportSnapshot right=annotation.viewports[0];right.x=16;right.width=16;annotation.viewports.push_back(right);annotation.renderStates.back().viewportSlot=1;
  quad(annotation,-.35f,SbColor(0,1,0),2);
  CHECK(execute(target,annotation),"viewport depth barrier");
  CHECK(target.colorBuffer[(16*32+8)*4]==base[(16*32+8)*4]&&color(target,24,16,0,255,0),"annotation depth clear preserves the other half of the CAD surface");
  auto invalid=frame;invalid.draws.back().renderLayer=0;const auto prior=target.colorBuffer;const auto serial=target.lastSubmissionSerial;
  CHECK(target.executeFrame(invalid).status==CoinRenderBackendStatus::UNSUPPORTED&&target.colorBuffer==prior&&target.lastSubmissionSerial==serial,"mixed base-layer legacy opaque is explicitly refused without publication");
  invalid=frame;invalid.materials.back().model=CoinRenderMaterialModel::PBR;
  CHECK(target.executeFrame(invalid).status==CoinRenderBackendStatus::UNSUPPORTED,"PBR technical layer is refused rather than emitted through a legacy shader");
  invalid=frame;invalid.renderStates.back().lightModel=CoinRenderLightModel::PHONG;
  CHECK(target.executeFrame(invalid).status==CoinRenderBackendStatus::UNSUPPORTED,"lit legacy overlay requires an explicit composition policy");
  invalid=plane();for(unsigned i=1;i<=12;++i)quad(invalid,-.75f,SbColor(0,1,0),i);
  CHECK(target.executeFrame(invalid).status==CoinRenderBackendStatus::UNSUPPORTED&&target.colorBuffer==prior&&
    target.lastSubmissionSerial==serial,"excessive technical layers are refused before encoding or publication");
  std::printf("PBR CAD GPU: depth handoff, occlusion, semantic color, exposure, replay, viewport barrier and explicit composition boundaries PASS\n");return 0;
}
