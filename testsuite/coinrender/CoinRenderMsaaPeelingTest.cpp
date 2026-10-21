#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "CoinRenderMsaaShadowFixture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr int side=64;
using Color=std::array<double,4>;
bool canonicalRaster=false;
uint32_t requestedSamples=1;
CoinRenderRenderer requestedRenderer=COIN_RENDER_RENDERER_UNKNOWN;
bool wgpuBackend=false;
CoinRenderOptions testOptions() {
  CoinRenderOptions options{};
  if(options.renderer==COIN_RENDER_RENDERER_UNKNOWN)
    options.renderer=requestedRenderer;
  if(canonicalRaster)
    options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  options.sampleCount=requestedSamples;
  return options;
}
struct Layer {
  double z;
  Color color;
};
bool check(bool value,const std::string & label) {
  if(!value) std::cerr << "peeling_depth_state: " << label << '\n';
  return value;
}
void frameDifference(const std::string &label,int width,
                     const std::vector<uint8_t> &oldRgba,const std::vector<float> &oldDepth,
                     const std::vector<uint8_t> &newRgba,const std::vector<float> &newDepth) {
  const auto oldPrecision=std::cerr.precision(17);
  const size_t n=std::min(oldDepth.size(),newDepth.size());
  const size_t colors=std::min(oldRgba.size(),newRgba.size())/4;
  size_t depthCount=0,rgbaCount=0,firstDepth=n,maxDepth=n,firstRgba=colors,maxRgba=colors;
  double maxAbs=0.;int maxChannel=0;
  size_t categoryDifferent[3]={},categoryOverBound[3]={};double categoryMax[3]={};
  constexpr double diagnosticBound=2./16777216.;
  for(size_t p=0;p<n;++p)if(oldDepth[p]!=newDepth[p]){
    if(!depthCount)firstDepth=p;
    ++depthCount;
    const double delta=std::abs(double(newDepth[p])-oldDepth[p]);
    if(p<colors){
      const size_t category=oldRgba[4*p+3]==0?0:(oldRgba[4*p+3]==255?1:2);
      ++categoryDifferent[category];categoryOverBound[category]+=(delta>diagnosticBound);
      categoryMax[category]=std::max(categoryMax[category],delta);
    }
    if(maxDepth==n||!std::isfinite(delta)||delta>maxAbs){maxDepth=p;maxAbs=delta;}
  }
  for(size_t p=0;p<colors;++p){
    int difference=0;
    for(size_t c=0;c<4;++c)difference=std::max(difference,
      std::abs(int(newRgba[4*p+c])-int(oldRgba[4*p+c])));
    if(difference){
      if(!rgbaCount)firstRgba=p;
      ++rgbaCount;
      if(maxRgba==colors||difference>maxChannel){maxRgba=p;maxChannel=difference;}
    }
  }
  std::cerr<<"msaa_peeling_frame_diff label="<<label
    <<" rgba_equal="<<(oldRgba==newRgba)<<" depth_equal="<<(oldDepth==newDepth)
    <<" rgba_sizes="<<oldRgba.size()<<','<<newRgba.size()
    <<" depth_sizes="<<oldDepth.size()<<','<<newDepth.size()
    <<" rgba_diff_pixels="<<rgbaCount<<" depth_diff_pixels="<<depthCount
    <<" max_channel_delta="<<maxChannel<<" max_depth_abs="<<maxAbs<<'\n';
  const char *categories[]={"alpha_zero","alpha_opaque","alpha_partial"};
  for(size_t category=0;category<3;++category)
    std::cerr<<"msaa_peeling_frame_diff label="<<label<<" category="<<categories[category]
      <<" depth_diff_pixels="<<categoryDifferent[category]
      <<" depth_over_2d24_pixels="<<categoryOverBound[category]
      <<" max_depth_abs="<<categoryMax[category]<<" bound="<<diagnosticBound<<'\n';
  auto point=[&](const char *kind,size_t p){
    std::cerr<<"msaa_peeling_frame_diff label="<<label<<" kind="<<kind
      <<" xy="<<p%width<<','<<p/width;
    if(p<n){
      uint32_t oldBits=0,newBits=0;
      std::memcpy(&oldBits,&oldDepth[p],sizeof(oldBits));
      std::memcpy(&newBits,&newDepth[p],sizeof(newBits));
      std::cerr<<" old_depth="<<oldDepth[p]<<" new_depth="<<newDepth[p]
        <<" old_depth_bits="<<oldBits<<" new_depth_bits="<<newBits;
    }
    if(p<colors){
      std::cerr<<" old_rgba="<<int(oldRgba[4*p])<<','<<int(oldRgba[4*p+1])<<','
        <<int(oldRgba[4*p+2])<<','<<int(oldRgba[4*p+3])
        <<" new_rgba="<<int(newRgba[4*p])<<','<<int(newRgba[4*p+1])<<','
        <<int(newRgba[4*p+2])<<','<<int(newRgba[4*p+3]);
    }
    std::cerr<<'\n';
  };
  if(depthCount){point("first_depth",firstDepth);point("maximum_depth",maxDepth);}
  if(rgbaCount){point("first_rgba",firstRgba);point("maximum_rgba",maxRgba);}
  std::cerr.precision(oldPrecision);
}
Color over(const Color & front,const Color & back) {
  Color out{};
  for(int c=0;c<3;++c) out[c]=front[c]*front[3]+back[c]*(1-front[3]);
  out[3]=front[3]+back[3]*(1-front[3]);
  return out;
}
double rawDepth(double z) { return (9-z)/29; }
bool passes(int fn,double fragment,double opaque) {
  switch(fn) {
  case SoDepthBuffer::NEVER: return false;
  case SoDepthBuffer::ALWAYS: return true;
  case SoDepthBuffer::LESS: return fragment<opaque;
  case SoDepthBuffer::LEQUAL: return fragment<=opaque;
  case SoDepthBuffer::EQUAL: return fragment==opaque;
  case SoDepthBuffer::GEQUAL: return fragment>=opaque;
  case SoDepthBuffer::GREATER: return fragment>opaque;
  case SoDepthBuffer::NOTEQUAL: return fragment!=opaque;
  }
  return false;
}
struct Expected {
  Color color;
  double depth;
  std::vector<int> selected;
};
Expected oracle(const std::vector<Layer> & authored,const std::vector<int> & order,
                int function,bool test,bool write,int passesCount,bool opaqueWrite,
                double nearRange,double farRange,
                Color background={{.12,.20,.30,1}}) {
  const double opaqueDepth=opaqueWrite ? rawDepth(0) : 1.;
  std::vector<int> eligible;
  for(int index:order) {
    const double d=nearRange+(farRange-nearRange)*rawDepth(authored[index].z);
    if(!test || passes(function,d,opaqueDepth)) eligible.push_back(index);
  }
  // At one depth the later fragment wins. Selection is by depth, not by
  // submission order or a fixed epsilon; alpha zero still takes one slot.
  std::stable_sort(eligible.begin(),eligible.end(),[&](int a,int b) {
    return nearRange+(farRange-nearRange)*rawDepth(authored[a].z) <
      nearRange+(farRange-nearRange)*rawDepth(authored[b].z);
  });
  std::vector<int> selected;
  for(int i:eligible) {
    if(!selected.empty() && authored[i].z==authored[selected.back()].z) selected.back()=i;
    else selected.push_back(i);
  }
  if(int(selected.size())>passesCount) selected.resize(size_t(passesCount));
  Color color=background;
  for(auto it=selected.rbegin();it!=selected.rend();++it)
    color=over(authored[*it].color,color);
  double depth=opaqueDepth;
  if(test && write && !selected.empty())
    depth=nearRange+(farRange-nearRange)*rawDepth(authored[selected.front()].z);
  return {color,depth,selected};
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoDepthBuffer * opaqueDepth=nullptr;
  std::vector<SoCoordinate3 *> coordinates;
  std::vector<SoDepthBuffer *> states;
  std::vector<SoDepthBuffer *> stateForLayer;
  std::vector<Layer> layers;
  std::vector<int> order;
  explicit Scene(std::vector<Layer> input,std::vector<int> sequence,
                 bool withOpaque=true):layers(std::move(input)),
      order(std::move(sequence)) {
    root->ref();
    auto * camera=new SoOrthographicCamera;
    camera->height=2;camera->position.setValue(0,0,10);
    camera->nearDistance=1;camera->farDistance=30;root->addChild(camera);
    auto * lighting=new SoLightModel;lighting->model=SoLightModel::BASE_COLOR;
    root->addChild(lighting);
    if(withOpaque) {
      opaqueDepth=new SoDepthBuffer;
      addTriangle(0,{{.12,.20,.30,1}},opaqueDepth);
    }
    coordinates.resize(layers.size(),nullptr);
    stateForLayer.resize(layers.size(),nullptr);
    for(int index:order) {
      auto * state=new SoDepthBuffer;
      states.push_back(state);
      stateForLayer[index]=state;
      coordinates[index]=addTriangle(layers[index].z,layers[index].color,state);
    }
  }
  ~Scene() {root->unref();}
  SoCoordinate3 * addTriangle(double z,const Color & color,SoDepthBuffer * state) {
    auto * branch=new SoSeparator;root->addChild(branch);
    branch->addChild(state);
    auto * material=new SoMaterial;
    material->diffuseColor.setValue(float(color[0]),float(color[1]),float(color[2]));
    material->transparency=float(1-color[3]);branch->addChild(material);
    auto * points=new SoCoordinate3;
    const SbVec3f p[]={SbVec3f(-.9f,-.9f,float(z)),
      SbVec3f(.9f,-.9f,float(z)),SbVec3f(0,.9f,float(z))};
    points->point.setValues(0,3,p);branch->addChild(points);
    auto * face=new SoIndexedFaceSet;
    const int32_t indices[]={0,1,2,-1};
    face->coordIndex.setValues(0,4,indices);branch->addChild(face);
    return points;
  }
  void depthState(bool test,bool write,int function,double nearRange=0,double farRange=1) {
    for(auto * state:states) {
      state->test=test;state->write=write;state->function=function;
      state->range=SbVec2f(float(nearRange),float(farRange));
    }
  }
};
struct Runner {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(side,side)};
  explicit Runner(bool cpu,CoinRenderOptions options={}) {
    if(options.renderer==COIN_RENDER_RENDERER_UNKNOWN)
      options.renderer=requestedRenderer;
    options.sampleCount=cpu?1:requestedSamples;
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    if(target && requestedRenderer!=COIN_RENDER_RENDERER_UNKNOWN &&
       target->getOptions().renderer!=requestedRenderer) target.reset();
    if(cpu && target) target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    if(target) target->setDepthReadbackEnabled(TRUE);
    action.setRenderTarget(target.get());
    action.setTransparencyType(CoinRenderAction::SORTED_LAYERS_BLEND);
  }
  bool capture(Scene & scene,int count,std::vector<uint8_t> & rgba,
               std::vector<float> & depth) {
    if(!target) return false;
    action.setSortedLayersNumPasses(count);action.apply(scene.root);
    target->readbackRGBA(rgba);target->readbackDepth(depth);
    return action.getLastStatus()==CoinRenderAction::SUCCESS &&
      rgba.size()==size_t(side*side*4) && depth.size()==size_t(side*side);
  }
};
bool match(const std::vector<uint8_t> & rgba,const std::vector<float> & depth,
           const Expected & expected,const std::string & label) {
  if(rgba.size()!=size_t(side*side*4) || depth.size()!=size_t(side*side))
    return check(false,label+" image size");
  const size_t p=size_t((side-1-24)*side+32);
  bool good=true;
  for(int c=0;c<4;++c) {
    const int wanted=int(std::lround(expected.color[c]*255));
    const int actual=rgba[p*4+c];
    good &= std::abs(actual-wanted)<=6;
    if(std::abs(actual-wanted)>6)
      std::cerr << label << " channel=" << c << " actual=" << actual
                << " expected=" << wanted << '\n';
  }
  const double delta=std::abs(depth[p]-expected.depth);
  if(delta>.003) std::cerr << label << " depth=" << depth[p]
                           << " expected=" << expected.depth << '\n';
  good &= delta<=.003;
  return check(good,label+" independent color/depth oracle");
}
bool slopedDepth(Runner &runner,const char *backend,float nearRange=0.f,
                 float farRange=1.f,bool equalOffset=false) {
  auto *root=new SoSeparator;root->ref();
  auto *camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,2);camera->height=2;
  camera->nearDistance=1;camera->farDistance=3;root->addChild(camera);
  auto *unlit=new SoLightModel;unlit->model=SoLightModel::BASE_COLOR;
  root->addChild(unlit);
  // Both branches share the exact authored vertices and index order. Their
  // depth is equal at every sample, including across the internal diagonal.
  auto *vertices=new SoCoordinate3;
  const SbVec3f p[]={SbVec3f(-.9f,-.9f,-.5f),SbVec3f(.9f,-.9f,.5f),
    SbVec3f(.9f,.9f,.5f),SbVec3f(-.9f,.9f,-.5f)};
  vertices->point.setValues(0,4,p);
  auto branch=[&](const SbColor &color,float alpha,bool write,
                  SoDepthBuffer **depthState) {
    auto *separator=new SoSeparator;
    auto *state=new SoDepthBuffer;state->test=TRUE;state->write=write;
    state->function=SoDepthBuffer::LEQUAL;
    state->range=SbVec2f(nearRange,farRange);separator->addChild(state);
    if(equalOffset) {
      auto *offset=new SoPolygonOffset;
      offset->styles=SoPolygonOffset::FILLED;
      offset->factor=1.f;offset->units=1.f;offset->on=TRUE;
      separator->addChild(offset);
    }
    auto *material=new SoMaterial;material->diffuseColor=color;
    material->transparency=1-alpha;separator->addChild(material);
    separator->addChild(vertices);
    auto *face=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,3,-1};
    face->coordIndex.setValues(0,5,indices);separator->addChild(face);
    *depthState=state;return separator;
  };
  SoDepthBuffer *opaqueState=nullptr,*transparentState=nullptr;
  root->addChild(branch(SbColor(0,1,0),1.f,true,&opaqueState));
  auto *transparent=branch(SbColor(1,0,0),.5f,false,&transparentState);
  auto capture=[&](std::vector<uint8_t> &rgba,std::vector<float> &depth,
                   uint64_t &serial,const char *phase){
    runner.action.setSortedLayersNumPasses(2);runner.action.apply(root);
    runner.target->readbackRGBA(rgba);runner.target->readbackDepth(depth);
    serial=runner.target->getLastSubmissionSerial();
    const bool published=runner.action.getLastStatus()==CoinRenderAction::SUCCESS&&
      rgba.size()==size_t(side*side*4)&&depth.size()==size_t(side*side)&&serial>0;
    std::cout<<"msaa_peeling sloped_depth backend="<<backend
      <<" samples="<<requestedSamples<<" phase="<<phase
      <<" range="<<nearRange<<','<<farRange<<" offset="<<equalOffset
      <<" status="<<int(runner.action.getLastStatus())
      <<" serial="<<serial<<" error="<<runner.action.getLastError().getString()<<'\n';
    return check(published,"sloped depth frame must publish");
  };
  std::vector<uint8_t> pure,base,frame,restored,pureRestored;
  std::vector<float> pureDepth,baseDepth,frameDepth,restoredDepth,pureRestoredDepth;
  uint64_t pureSerial=0,previous=0,serial=0,restoredSerial=0,pureRestoredSerial=0;
  bool ok=capture(pure,pureDepth,pureSerial,"external opaque control");
  if(!ok){root->unref();return false;}
  root->addChild(transparent);
  transparentState->function=SoDepthBuffer::NEVER;
  ok &= capture(base,baseDepth,previous,"peeling-profile opaque A");
  if(!ok){root->unref();return false;}
  // The external pure-opaque pass can use D24S8, while peeling may use F32.
  // Two depth quantization steps bound that format conversion without
  // weakening the exact write-off comparison within the peeling profile.
  const float formatBound=2.f/16777215.f;
  float formatMax=0.f;size_t formatDiff=0;
  for(size_t p=0;p<pureDepth.size();++p){
    const float delta=std::abs(pureDepth[p]-baseDepth[p]);
    formatMax=std::max(formatMax,delta);
    formatDiff+=(delta!=0.f);
  }
  std::cout<<"msaa_peeling sloped_depth external_opaque backend="<<backend
    <<" samples="<<requestedSamples<<" range="<<nearRange<<','<<farRange
    <<" offset="<<equalOffset<<" differing_depth_pixels="<<formatDiff
    <<" max_abs="<<formatMax<<" bound="<<formatBound<<'\n';
  ok &= check(pure==base&&formatMax<=formatBound&&previous==pureSerial+1,
    "external opaque control matches peeling profile within D24/F32 quantization");
  const std::array<int,8> functions={{SoDepthBuffer::NEVER,SoDepthBuffer::ALWAYS,
    SoDepthBuffer::LESS,SoDepthBuffer::LEQUAL,SoDepthBuffer::EQUAL,
    SoDepthBuffer::GEQUAL,SoDepthBuffer::GREATER,SoDepthBuffer::NOTEQUAL}};
  for(int fn:functions) {
    transparentState->function=fn;
    const bool accepted=fn==SoDepthBuffer::ALWAYS||fn==SoDepthBuffer::LEQUAL||
      fn==SoDepthBuffer::EQUAL||fn==SoDepthBuffer::GEQUAL;
    const std::string phase="function="+std::to_string(fn);
    if(!capture(frame,frameDepth,serial,phase.c_str())){ok=false;continue;}
    ok &= check(serial==previous+1,"one sloped-depth publication per comparator");
    previous=serial;
    if(frameDepth!=baseDepth) {
      size_t count=0,first=frameDepth.size();float maxAbs=0.f;
      for(size_t p=0;p<frameDepth.size();++p)if(frameDepth[p]!=baseDepth[p]) {
        if(first==frameDepth.size())first=p;
        ++count;maxAbs=std::max(maxAbs,std::abs(frameDepth[p]-baseDepth[p]));
      }
      if(first<frameDepth.size()) {
        uint32_t baseBits=0,frameBits=0;
        std::memcpy(&baseBits,&baseDepth[first],sizeof(baseBits));
        std::memcpy(&frameBits,&frameDepth[first],sizeof(frameBits));
        std::cerr<<"msaa_peeling sloped_depth depth_diff backend="<<backend
          <<" samples="<<requestedSamples<<" range="<<nearRange<<','<<farRange
          <<" offset="<<equalOffset<<" function="<<fn
          <<" first_xy="<<first%side<<','<<first/side
          <<" base="<<baseDepth[first]<<" frame="<<frameDepth[first]
          <<" base_bits="<<baseBits<<" frame_bits="<<frameBits
          <<" count="<<count<<" max_abs="<<maxAbs<<'\n';
      }
    }
    ok &= check(frameDepth==baseDepth,
      "transparent depth-write off preserves sloped opaque depth per sample");
    bool pixels=true;
    for(int y=24;y<40;++y)for(int x=24;x<40;++x) {
      const size_t i=size_t(y*side+x)*4;
      const int red=accepted?128:0,green=accepted?128:255;
      const bool good=std::abs(int(frame[i])-red)<=3&&
        std::abs(int(frame[i+1])-green)<=3&&frame[i+2]<=3&&
        std::abs(int(frame[i+3])-255)<=3;
      if(!good&&pixels)std::cerr<<"msaa_peeling sloped_depth first_diff backend="
        <<backend<<" samples="<<requestedSamples<<" function="<<fn
        <<" xy="<<x<<','<<y<<" rgba="<<int(frame[i])<<','<<int(frame[i+1])
        <<','<<int(frame[i+2])<<','<<int(frame[i+3])
        <<" expected="<<red<<','<<green<<",0,255"<<'\n';
      pixels &= good;
    }
    ok &= check(pixels,"eight depth functions compare against the same sloped opaque samples");
  }
  transparentState->function=SoDepthBuffer::NEVER;
  ok &= capture(restored,restoredDepth,restoredSerial,
    "peeling-profile opaque A restored");
  ok &= check(restored==base&&restoredDepth==baseDepth&&
    restoredSerial==previous+1,"sloped-depth peeling-profile A/B/A exact RGBA/depth/serial");
  root->removeChild(transparent);
  ok &= capture(pureRestored,pureRestoredDepth,pureRestoredSerial,
    "external opaque control restored");
  ok &= check(pureRestored==pure&&pureRestoredDepth==pureDepth&&
    pureRestoredSerial==restoredSerial+1,
    "external pure-opaque A/B/A recovers exact RGBA/depth/serial");
  root->unref();return ok;
}
bool opaqueSlopedOracle(Runner &runner,const char *backend,bool withRtt=false) {
  runner.action.setBackgroundColor(SbColor4f(0,0,0,0));
  runner.action.setSortedLayersNumPasses(2);
  auto camera=[](){auto *c=new SoOrthographicCamera;
    c->position.setValue(0,0,2);c->height=2;
    c->nearDistance=1;c->farDistance=3;return c;};
  auto *root=new SoSeparator;root->ref();root->addChild(camera());
  auto *light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;
  root->addChild(light);
  auto *depthState=new SoDepthBuffer;depthState->test=TRUE;
  depthState->write=TRUE;depthState->function=SoDepthBuffer::LEQUAL;
  root->addChild(depthState);
  auto *offset=new SoPolygonOffset;offset->styles=SoPolygonOffset::FILLED;
  offset->factor=1.f;offset->units=1.f;offset->on=FALSE;root->addChild(offset);
  auto *green=new SoMaterial;green->diffuseColor.setValue(0,1,0);root->addChild(green);
  SoSeparator *producer=nullptr;
  if(withRtt) {
    producer=new SoSeparator;producer->ref();producer->addChild(camera());
    auto *producerLight=new SoLightModel;
    producerLight->model=SoLightModel::BASE_COLOR;producer->addChild(producerLight);
    auto *producerMaterial=new SoMaterial;
    producerMaterial->diffuseColor.setValue(1,1,1);producer->addChild(producerMaterial);
    auto *producerPoints=new SoCoordinate3;
    const SbVec3f whiteQuad[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
      SbVec3f(1,1,0),SbVec3f(-1,1,0)};
    producerPoints->point.setValues(0,4,whiteQuad);producer->addChild(producerPoints);
    auto *producerFace=new SoIndexedFaceSet;
    const int32_t whiteIndices[]={0,1,2,3,-1};
    producerFace->coordIndex.setValues(0,5,whiteIndices);
    producer->addChild(producerFace);
    auto *quality=new SoComplexity;quality->textureQuality=.1f;
    root->addChild(quality);
    auto *texture=new SoSceneTexture2;texture->scene=producer;
    texture->size.setValue(side,side);root->addChild(texture);
    auto *uv=new SoTextureCoordinate2;
    const SbVec2f uvPoints[]={SbVec2f(0,0),SbVec2f(1,0),
      SbVec2f(1,1),SbVec2f(0,1)};
    uv->point.setValues(0,4,uvPoints);root->addChild(uv);
  }
  auto *points=new SoCoordinate3;
  const SbVec3f slope[]={SbVec3f(-.9f,-.9f,-.5f),SbVec3f(.9f,-.9f,.5f),
    SbVec3f(.9f,.9f,.5f),SbVec3f(-.9f,.9f,-.5f)};
  points->point.setValues(0,4,slope);root->addChild(points);
  auto *face=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,3,-1};
  face->coordIndex.setValues(0,5,indices);root->addChild(face);
  auto *probe=new SoSeparator;probe->ref();probe->addChild(camera());
  auto *probeLight=new SoLightModel;probeLight->model=SoLightModel::BASE_COLOR;
  probe->addChild(probeLight);
  auto *white=new SoMaterial;white->diffuseColor.setValue(1,1,1);probe->addChild(white);
  auto *edge=new SoCoordinate3;probe->addChild(edge);
  auto *probeFace=new SoIndexedFaceSet;
  probeFace->coordIndex.setValues(0,5,indices);probe->addChild(probeFace);
  uint64_t lastSerial=0;bool ok=true;
  auto capture=[&](SoNode *scene,std::vector<uint8_t> &rgba,
                   std::vector<float> &depth,const char *phase){
    runner.action.apply(scene);runner.target->readbackRGBA(rgba);
    runner.target->readbackDepth(depth);
    const uint64_t serial=runner.target->getLastSubmissionSerial();
    const bool producerFrame=withRtt&&scene==root;
    const uint64_t serialStep=producerFrame&&
      (wgpuBackend || runner.target->getOptions().sceneTexture==
       COIN_RENDER_SCENE_TEXTURE_DIRECT) ? 2:1;
    const bool good=runner.action.getLastStatus()==CoinRenderAction::SUCCESS&&
      rgba.size()==size_t(side*side*4)&&depth.size()==size_t(side*side)&&
      serial>0&&(!lastSerial||serial==lastSerial+serialStep);
    std::cout<<"msaa_opaque_slope backend="<<backend<<" rtt="<<withRtt
      <<" samples="<<requestedSamples<<" phase="<<phase
      <<" status="<<int(runner.action.getLastStatus())<<" serial="<<serial
      <<" error="<<runner.action.getLastError().getString()<<'\n';
    lastSerial=serial;return check(good,"opaque slope control publishes exactly once");
  };
  std::vector<uint8_t> initial,rgba,zeroRgba,offsetRgba,restored;
  std::vector<float> initialDepth,depth,zeroDepth,offsetDepth,restoredDepth;
  depthState->range=SbVec2f(0,1);
  const bool initialPublished=capture(root,initial,initialDepth,"default slope A");
  ok &= initialPublished;
  if(!initialPublished){root->unref();probe->unref();if(producer)producer->unref();return false;}
  struct CoverageProbe {
    int x,y,count;
    std::pair<double,double> first{0.,1.},last{0.,1.};
    CoverageProbe(int px,int py,int n):x(px),y(py),count(n){}
  };
  auto decodeCount=[&](int alpha){
    const int k=int(std::lround(double(alpha)*requestedSamples/255.));
    if(k<0||k>int(requestedSamples)||
      std::abs(alpha-int(std::lround(255.*k/requestedSamples)))>2)return -1;
    return k;
  };
  std::vector<CoverageProbe> probes;
  probes.push_back({32,32,int(requestedSamples)});
  for(int y=0;y<side;++y)for(int x=0;x<side;++x){
    const int alpha=initial[4*size_t(y*side+x)+3];
    const int k=decodeCount(alpha);
    if(k<0)ok &= check(false,"authored quad alpha must quantize to k/N before edge calibration");
    if(k>0&&k<int(requestedSamples))probes.push_back({x,y,k});
  }
  if(!ok){root->unref();probe->unref();if(producer)producer->unref();return false;}
  bool rowOrientationKnown=false,topDown=false;
  auto coverageAt=[&](const std::vector<double> &cuts,const char *phase){
    std::vector<SbVec3f> vertices;std::vector<int32_t> probeIndices;
    auto append=[&](float left,float right,float bottom,float top){
      if(right<=left||top<=bottom)return;
      const int32_t start=int32_t(vertices.size());
      vertices.emplace_back(left,bottom,0);vertices.emplace_back(right,bottom,0);
      vertices.emplace_back(right,top,0);vertices.emplace_back(left,top,0);
      probeIndices.insert(probeIndices.end(),{start,start+1,start+2,start+3,-1});
    };
    // The marker occupies only the originally empty corner and establishes
    // row orientation from color, independently of depth or renderer identity.
    append(-1.f,-.9375f,.9375f,1.f);
    for(size_t i=0;i<probes.size();++i){
      const auto &p=probes[i];
      const double left=2.*p.x/side-1.;
      const double right=2.*(p.x+cuts[i])/side-1.;
      const double bottom=topDown?1.-2.*(p.y+1)/side:2.*p.y/side-1.;
      const double top=topDown?1.-2.*p.y/side:2.*(p.y+1)/side-1.;
      // Each flat white cell probes exactly the authored quad's XY
      // intersection with this pixel. Horizontal-edge and corner samples
      // retain their real Y coverage while their X cutoff is bisected.
      append(std::max(float(left),-.9f),std::min(float(right),.9f),
        std::max(float(bottom),-.9f),std::min(float(top),.9f));
    }
    edge->point.setNum(int(vertices.size()));
    edge->point.setValues(0,int(vertices.size()),vertices.data());
    probeFace->coordIndex.setNum(int(probeIndices.size()));
    probeFace->coordIndex.setValues(0,int(probeIndices.size()),probeIndices.data());
    std::vector<uint8_t> pixels;std::vector<float> values;
    if(!capture(probe,pixels,values,phase)){
      ok=false;return std::vector<int>();
    }
    if(!rowOrientationKnown){
      const bool upper=pixels[3]==255;
      const bool lower=pixels[4*size_t((side-1)*side)+3]==255;
      ok &= check(upper!=lower,"coverage marker must establish one framebuffer row orientation");
      topDown=upper;rowOrientationKnown=upper!=lower;
    }
    std::vector<int> counts;counts.reserve(probes.size());
    for(const auto &p:probes){
      const int count=decodeCount(pixels[4*size_t(p.y*side+p.x)+3]);
      const bool valid=count>=0&&count<=p.count;
      if(!valid)std::cerr<<"msaa_opaque_slope probe_count_diff xy="<<p.x<<','<<p.y
        <<" count="<<count<<" max_authored="<<p.count<<'\n';
      ok &= check(valid,"sample-location coverage must quantize within authored k/N");
      counts.push_back(count);
    }
    return counts;
  };
  const auto uncovered=coverageAt(std::vector<double>(probes.size(),0.),"coverage endpoint zero");
  const auto covered=coverageAt(std::vector<double>(probes.size(),1.),"coverage endpoint full");
  if(uncovered.size()==probes.size()&&covered.size()==probes.size()){
    for(size_t i=0;i<probes.size();++i){
      const bool matches=uncovered[i]==0&&covered[i]==probes[i].count;
      if(!matches)std::cerr<<"msaa_opaque_slope probe_endpoint_diff xy="<<probes[i].x
        <<','<<probes[i].y<<" zero="<<uncovered[i]<<" full="<<covered[i]
        <<" expected="<<probes[i].count<<'\n';
      ok &= check(matches,"each clipped probe endpoint must match authored pixel coverage");
    }
  }
  for(bool firstSample:{true,false})for(int iteration=0;iteration<10&&ok;++iteration){
    std::vector<double> midpoints;midpoints.reserve(probes.size());
    for(const auto &p:probes){const auto bounds=firstSample?p.first:p.last;
      midpoints.push_back((bounds.first+bounds.second)/2.);}
    const auto counts=coverageAt(midpoints,firstSample?"first covered sample sweep":"last covered sample sweep");
    if(counts.size()!=probes.size())break;
    for(size_t i=0;i<probes.size();++i){
      auto &bounds=firstSample?probes[i].first:probes[i].last;
      const bool reached=firstSample?counts[i]>0:counts[i]==probes[i].count;
      if(reached)bounds.second=midpoints[i];else bounds.first=midpoints[i];
    }
  }
  if(!ok){root->unref();probe->unref();if(producer)producer->unref();return false;}
  const auto firstSample=probes[0].first,lastSample=probes[0].last;
  std::cout<<"msaa_opaque_slope edge_calibration samples="<<requestedSamples
    <<" partial_pixels="<<probes.size()-1<<" top_down="<<topDown<<'\n';
  // The edge sweep brackets a quantized vertex/top-left transition, not the
  // sample position itself. The reference raster snaps positions to a 1/256
  // pixel lattice; account for one lattice step on either side of the bracket.
  constexpr double vertexLatticeStep=1./256.;
  auto samplePositionBounds=[&](std::pair<double,double> transition){
    return std::pair<double,double>(
      std::max(0.,transition.first-vertexLatticeStep),
      std::min(1.,transition.second+vertexLatticeStep));
  };
  std::cout<<"msaa_opaque_slope samples="<<requestedSamples
    <<" sample_x_first="<<firstSample.first<<','<<firstSample.second
    <<" sample_x_last="<<lastSample.first<<','<<lastSample.second<<'\n';
  const float formatBound=2.f/16777215.f;
  auto predicted=[&](int x,double sampleX,float nearRange,float farRange){
    const double worldX=2.*(x+sampleX)/side-1.;
    const double z=worldX*(.5/.9);
    return float(nearRange+(farRange-nearRange)*(1.-z)/2.);
  };
  for(const auto range:{SbVec2f(0,1),SbVec2f(.2f,.8f),SbVec2f(.8f,.2f)}){
    depthState->range=range;offset->on=FALSE;
    const bool published=capture(root,rgba,depth,"unoffset sloped depth A");
    ok &= published;
    const float nearRange=range[0],farRange=range[1];
    auto geometric=[&](const std::vector<uint8_t> &pixels,
                       const std::vector<float> &values,const char *phase){
      const auto sample=samplePositionBounds(
        farRange>=nearRange?lastSample:firstSample);
      bool geometry=true;size_t firstBad=0;float maxAbs=0.f;
      for(int y=24;y<40;++y)for(int x=24;x<40;++x){
        const size_t p=size_t(y*side+x);
        const float a=predicted(x,sample.first,nearRange,farRange);
        const float b=predicted(x,sample.second,nearRange,farRange);
        const float low=std::min(a,b)-formatBound,high=std::max(a,b)+formatBound;
        const float actual=values[p];
        if((actual<low||actual>high)&&geometry)firstBad=p;
        geometry &= actual>=low&&actual<=high;
        maxAbs=std::max(maxAbs,std::max(low-actual,actual-high));
        geometry &= pixels[4*p]==0&&pixels[4*p+1]==255&&
          pixels[4*p+2]==0&&pixels[4*p+3]==255;
      }
      size_t edgeFailures=0;float edgeMaxOutside=0.f;
      for(size_t i=1;i<probes.size();++i){
        const auto &p=probes[i];const size_t pixel=size_t(p.y*side+p.x);
        const auto interval=samplePositionBounds(farRange>=nearRange?p.last:p.first);
        const float a=predicted(p.x,interval.first,nearRange,farRange);
        const float b=predicted(p.x,interval.second,nearRange,farRange);
        const float low=std::min(a,b)-formatBound,high=std::max(a,b)+formatBound;
        const int coverage=int(std::lround(255.*p.count/requestedSamples));
        const float actual=values[pixel];
        const bool depthGood=actual>=low&&actual<=high;
        const bool colorGood=pixels[4*pixel]==0&&pixels[4*pixel+2]==0&&
          std::abs(int(pixels[4*pixel+1])-coverage)<=2&&
          std::abs(int(pixels[4*pixel+3])-coverage)<=2;
        if(!depthGood||!colorGood){
          if(edgeFailures<8)std::cerr<<"msaa_opaque_slope edge_geometry_diff backend="<<backend
            <<" samples="<<requestedSamples<<" phase="<<phase
            <<" range="<<nearRange<<','<<farRange<<" xy="<<p.x<<','<<p.y
            <<" covered_samples="<<p.count<<" sample_x="<<interval.first<<','<<interval.second
            <<" actual_depth="<<actual<<" expected_depth="<<low<<','<<high
            <<" rgba="<<int(pixels[4*pixel])<<','<<int(pixels[4*pixel+1])<<','
            <<int(pixels[4*pixel+2])<<','<<int(pixels[4*pixel+3])
            <<" expected_green_alpha="<<coverage<<'\n';
          if(geometry)firstBad=pixel;
          geometry=false;++edgeFailures;
          const float outside=std::max(low-actual,actual-high);
          maxAbs=std::max(maxAbs,outside);edgeMaxOutside=std::max(edgeMaxOutside,outside);
        }
      }
      std::cout<<"msaa_opaque_slope edge_geometry backend="<<backend
        <<" samples="<<requestedSamples<<" phase="<<phase
        <<" range="<<nearRange<<','<<farRange<<" checked="<<probes.size()-1
        <<" failed="<<edgeFailures<<" max_outside_interval="<<edgeMaxOutside<<'\n';
      if(!geometry)std::cerr<<"msaa_opaque_slope geometry_diff backend="<<backend
        <<" samples="<<requestedSamples<<" range="<<nearRange<<','<<farRange
        <<" phase="<<phase<<" first_xy="<<firstBad%side<<','<<firstBad/side
        <<" actual_depth="<<values[firstBad]<<" max_outside_interval="<<maxAbs<<'\n';
      return check(geometry,"opaque sloped depth is minimum of calibrated geometric samples");
    };
    if(published)ok &= geometric(rgba,depth,"hardware A");
    // Exercise the offset depth path while its numerical bias is zero at
    // these normal F32 depths. Compare its geometry independently, then
    // separate this path conversion from the actual factor/units effect.
    offset->on=TRUE;offset->factor=0.f;offset->units=1.e-20f;
    const bool zeroPublished=capture(root,zeroRgba,zeroDepth,"zero-bias depth path C");
    ok &= zeroPublished;
    if(zeroPublished)ok &= geometric(zeroRgba,zeroDepth,"zero bias C");
    if(published&&zeroPublished){
      bool sameGeometry=zeroRgba==rgba;double maxAbs=0.;
      for(size_t p=0;p<depth.size();++p){
        const double delta=std::abs(double(zeroDepth[p])-depth[p]);
        sameGeometry &= std::isfinite(delta)&&delta<=formatBound;
        maxAbs=std::max(maxAbs,delta);
      }
      std::cout<<"msaa_opaque_slope zero_bias samples="<<requestedSamples
        <<" range="<<nearRange<<','<<farRange<<" max_abs="<<maxAbs
        <<" bound="<<formatBound<<'\n';
      if(!sameGeometry)frameDifference(std::string("zero C-A backend=")+backend+
        " samples="+std::to_string(requestedSamples)+" range="+std::to_string(nearRange)+","+
        std::to_string(farRange),side,rgba,depth,zeroRgba,zeroDepth);
      ok &= check(sameGeometry,"zero-bias path preserves hardware geometry within D24/F32 quantization");
    }
    offset->factor=1.f;offset->units=1.f;
    const bool offsetPublished=capture(root,offsetRgba,offsetDepth,"offset sloped depth B");
    ok &= offsetPublished;
    if(zeroPublished&&offsetPublished){
      // B and C use the same depth path. Its offset must match the analytic
      // factor=1 gradient plus units=1; no candidate-derived expected bias.
      // The unchanged two-D24 bound covers subtraction/attachment rounding.
      const double expectedSlope=std::abs(double(farRange)-nearRange)*(.5/.9)/side;
      const double minBias=expectedSlope-formatBound;
      const double maxBias=expectedSlope+1./8388608.+formatBound;
      bool offsetGood=offsetRgba==zeroRgba;
      double observedMin=1.,observedMax=-1.;
      for(int y=24;y<40;++y)for(int x=24;x<40;++x){
        const size_t p=size_t(y*side+x);
        const double delta=double(offsetDepth[p])-zeroDepth[p];
        offsetGood &= delta>=minBias&&delta<=maxBias;
        observedMin=std::min(observedMin,delta);observedMax=std::max(observedMax,delta);
      }
      std::cout<<"msaa_opaque_slope offset samples="<<requestedSamples
        <<" range="<<nearRange<<','<<farRange<<" reference=C expected_bias="<<minBias<<','<<maxBias
        <<" observed_bias="<<observedMin<<','<<observedMax<<'\n';
      ok&=check(offsetGood,"FILLED factor and units match independent geometric bias bounds");
    }
    offset->on=FALSE;
    const bool restoredPublished=capture(root,restored,restoredDepth,"unoffset slope A restored");
    ok &= restoredPublished;
    if(published&&restoredPublished){
      if(restored!=rgba||restoredDepth!=depth)frameDifference(
        std::string("offset A-B-A backend=")+backend+" samples="+std::to_string(requestedSamples)+
        " range="+std::to_string(nearRange)+","+std::to_string(farRange),
        side,rgba,depth,restored,restoredDepth);
      ok&=check(restored==rgba&&restoredDepth==depth,
        "opaque slope offset A/B/A exact RGBA/depth");
    }
  }
  depthState->range=SbVec2f(0,1);offset->on=FALSE;
  const bool restoredPublished=capture(root,restored,restoredDepth,"default slope A restored");
  ok &= restoredPublished;
  if(initialPublished&&restoredPublished){
    if(restored!=initial||restoredDepth!=initialDepth)frameDifference(
      std::string("range A-B-A backend=")+backend+" samples="+std::to_string(requestedSamples),
      side,initial,initialDepth,restored,restoredDepth);
    ok&=check(restored==initial&&restoredDepth==initialDepth,
      "opaque slope range A/B/A exact RGBA/depth");
  }
  root->unref();probe->unref();if(producer)producer->unref();return ok;
}
bool runMatrix(Runner & gpu,Runner & cpu) {
  bool ok=true;
  const std::vector<Layer> layers={
    {2,{{.85,.10,.10,.5}}}, // in front
    {1,{{.10,.85,.10,0}}},  // invisible, but consumes a distinct layer
    {-2,{{.10,.10,.85,.75}}},// behind
    {0,{{.85,.80,.10,.25}}},// equal opaque depth
    {1.999,{{.10,.80,.80,.4}}},// close but distinct from first
    {0,{{.25,.75,.85,.3}}} // equal with 3; last submission wins
  };
  const std::array<int,8> functions={{SoDepthBuffer::NEVER,SoDepthBuffer::ALWAYS,
    SoDepthBuffer::LESS,SoDepthBuffer::LEQUAL,SoDepthBuffer::EQUAL,
    SoDepthBuffer::GEQUAL,SoDepthBuffer::GREATER,SoDepthBuffer::NOTEQUAL}};
  for(bool reverse:{false,true}) {
    std::vector<int> order={0,1,2,3,4,5};
    if(reverse) std::reverse(order.begin(),order.end());
    Scene scene(layers,order);
    for(bool opaqueWrite:{false,true}) {
      scene.opaqueDepth->write=opaqueWrite;
      for(int fn:functions) for(bool test:{false,true}) for(bool write:{false,true})
        for(int count:{1,2,4,8}) {
          scene.depthState(test,write,fn);
          const auto expected=oracle(layers,order,fn,test,write,count,opaqueWrite,0,1);
          const std::string label="function="+std::to_string(fn)+"/test="+
            std::to_string(test)+"/write="+std::to_string(write)+"/passes="+
            std::to_string(count)+"/reverse="+std::to_string(reverse)+
            "/opaque_write="+std::to_string(opaqueWrite);
          std::vector<uint8_t> gp,cp;std::vector<float> gd,cd;
          const bool captured=gpu.capture(scene,count,gp,gd) &&
            cpu.capture(scene,count,cp,cd);
          ok &= check(captured,label+" valid profile must publish");
          if(captured) {
            ok &= match(gp,gd,expected,label+"/GPU");
            ok &= match(cp,cd,expected,label+"/CPU");
          }
        }
    }
  }
  return ok;
}
bool stateTransitions(Runner & gpu,Runner & cpu) {
  bool ok=true;
  const std::vector<Layer> layers={{2,{{.9,.15,.1,.55}}},
    {0,{{.1,.85,.2,.45}}},{-2,{{.15,.2,.95,.65}}}};
  Scene scene(layers,{2,0,1});
  auto compare=[&](const std::string & label,bool test,bool write,int function,
                   int count,bool opaqueWrite,double nearRange,double farRange,
                   std::vector<uint8_t> * savedColor=nullptr,
                   std::vector<float> * savedDepth=nullptr) {
    scene.opaqueDepth->write=opaqueWrite;
    scene.depthState(test,write,function,nearRange,farRange);
    const auto expected=oracle(layers,scene.order,function,test,write,count,
      opaqueWrite,nearRange,farRange);
    std::vector<uint8_t> g,c;std::vector<float> gd,cd;
    const bool captured=gpu.capture(scene,count,g,gd) && cpu.capture(scene,count,c,cd);
    ok &= check(captured,label+" must publish");
    if(captured) {
      ok &= match(g,gd,expected,label+"/GPU");
      ok &= match(c,cd,expected,label+"/CPU");
      if(savedColor) *savedColor=g;
      if(savedDepth) *savedDepth=gd;
    }
    return captured;
  };
  std::vector<uint8_t> first,restored,changed;
  std::vector<float> firstDepth,restoredDepth;
  compare("mutation/initial",true,true,SoDepthBuffer::GREATER,2,true,0,1,
    &first,&firstDepth);
  compare("mutation/changed",true,true,SoDepthBuffer::LESS,2,true,0,1,
    &changed);
  compare("mutation/restored",true,true,SoDepthBuffer::GREATER,2,true,0,1,
    &restored,&restoredDepth);
  ok &= check(first==restored && firstDepth==restoredDepth && first!=changed,
    "same SoDepthBuffer node must invalidate and recover exact published frame");
  compare("range/near-far",true,true,SoDepthBuffer::LEQUAL,4,true,.2,.8);
  compare("range/reversed-test-off",false,true,SoDepthBuffer::NEVER,4,true,.8,.2);
  // Coin's SoDepthBuffer does not inspect SoNode::isOverride. Explicit
  // child fields win; ignored child fields inherit the parent's element.
  auto * parent=new SoDepthBuffer;
  parent->test=TRUE;parent->write=TRUE;
  parent->function=SoDepthBuffer::GREATER;parent->setOverride(TRUE);
  scene.root->insertChild(parent,3);
  scene.depthState(true,false,SoDepthBuffer::LESS);
  const auto localExpected=oracle(layers,scene.order,SoDepthBuffer::LESS,
    true,false,4,true,0,1);
  std::vector<uint8_t> g,c;std::vector<float> gd,cd;
  const bool captured=gpu.capture(scene,4,g,gd) && cpu.capture(scene,4,c,cd);
  ok &= check(captured,"explicit child depth state must publish under parent override");
  if(captured) {
    ok &= match(g,gd,localExpected,"explicit-child/GPU");
    ok &= match(c,cd,localExpected,"explicit-child/CPU");
  }
  for(auto * state:scene.states) {
    state->test.setIgnored(TRUE);
    state->write.setIgnored(TRUE);
    state->function.setIgnored(TRUE);
    state->range.setIgnored(TRUE);
  }
  const auto inheritedExpected=oracle(layers,scene.order,SoDepthBuffer::GREATER,
    true,true,4,true,0,1);
  const bool inherited=gpu.capture(scene,4,g,gd) && cpu.capture(scene,4,c,cd);
  ok &= check(inherited,"ignored child fields must inherit parent depth state");
  if(inherited) {
    ok &= match(g,gd,inheritedExpected,"ignored-child/GPU");
    ok &= match(c,cd,inheritedExpected,"ignored-child/CPU");
  }
  for(auto * state:scene.states) {
    state->test.setIgnored(FALSE);
    state->write.setIgnored(FALSE);
    state->function.setIgnored(FALSE);
    state->range.setIgnored(FALSE);
  }
  parent->setOverride(FALSE);
  const bool restoredLocal=gpu.capture(scene,4,g,gd) && cpu.capture(scene,4,c,cd);
  ok &= check(restoredLocal,"restoring explicit local fields must publish");
  if(restoredLocal) {
    ok &= match(g,gd,localExpected,"separator/GPU");
    ok &= match(c,cd,localExpected,"separator/CPU");
  }
  return ok;
}
bool mixedFragmentStates(Runner & gpu,Runner & cpu) {
  bool ok=true;
  const std::vector<Layer> layers={{2,{{.9,.1,.1,.5}}},
    {1,{{.1,.8,.2,.5}}},{-2,{{.1,.2,.9,.75}}}};
  const Color base={{.12,.20,.30,1}};
  for(bool reverse:{false,true}) {
    std::vector<int> order={0,1,2};
    if(reverse) std::reverse(order.begin(),order.end());
    Scene scene(layers,order);
    auto * near=scene.stateForLayer[0];
    near->test=TRUE;near->write=FALSE;near->function=SoDepthBuffer::LESS;
    auto * middle=scene.stateForLayer[1];
    middle->test=TRUE;middle->write=TRUE;middle->function=SoDepthBuffer::LEQUAL;
    auto * rejected=scene.stateForLayer[2];
    rejected->test=TRUE;rejected->write=TRUE;
    rejected->function=SoDepthBuffer::LESS;
    for(int count:{1,2,4}) {
      const Color expectedColor=count==1 ? over(layers[0].color,base) :
        over(layers[0].color,over(layers[1].color,base));
      const Expected expected={expectedColor,count==1 ? rawDepth(0) :
        rawDepth(layers[1].z),count==1 ? std::vector<int>{0} :
        std::vector<int>{0,1}};
      std::vector<uint8_t> g,c;std::vector<float> gd,cd;
      const bool captured=gpu.capture(scene,count,g,gd) &&
        cpu.capture(scene,count,c,cd);
      const std::string label="mixed/reverse="+std::to_string(reverse)+
        "/passes="+std::to_string(count);
      ok &= check(captured,label+" distinct fragment depth states must publish");
      if(captured) {
        ok &= match(g,gd,expected,label+"/GPU");
        ok &= match(c,cd,expected,label+"/CPU");
      }
    }
  }
  return ok;
}
bool annotationAndReference(Runner & gpu,Runner & cpu) {
  bool ok=true;
  const std::vector<Layer> layers={{2,{{.9,.2,.1,.5}}},
    {1,{{.1,.2,.9,.4}}}};
  Scene scene(layers,{0,1});
  scene.depthState(true,false,SoDepthBuffer::LEQUAL);
  const auto expected=oracle(layers,scene.order,SoDepthBuffer::LEQUAL,
    true,false,2,true,0,1);
  std::vector<uint8_t> g,c;std::vector<float> gd,cd;
  const bool baseline=gpu.capture(scene,2,g,gd) && cpu.capture(scene,2,c,cd);
  ok &= check(baseline,"simple peeling reference must publish");
  if(baseline) {
    ok &= match(g,gd,expected,"reference/GPU");
    ok &= match(c,cd,expected,"reference/CPU");
    SoOffscreenRenderer gl(SbViewportRegion(side,side));
    gl.setComponents(SoOffscreenRenderer::RGB_TRANSPARENCY);
    gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_LAYERS_BLEND);
    // Legacy CoinGL counts the opaque surface in its pass budget. The
    // portable contract's two passes count transparent depths only.
    gl.getGLRenderAction()->setSortedLayersNumPasses(8);
    // SoDepthBuffer::GLRender uses the action's transparent-path write rule.
    // Keep this native reference on Coin's default depth state; the portable
    // scene above exercises explicit depth fields separately.
    Scene nativeScene(layers,{0,1});
    for(int child=3;child<nativeScene.root->getNumChildren();++child)
      static_cast<SoSeparator *>(nativeScene.root->getChild(child))->removeChild(0);
    const bool native=gl.render(nativeScene.root) && gl.getBuffer();
    const bool nativePeeling=native &&
      gl.getGLRenderAction()->getTransparencyType()==
        SoGLRenderAction::SORTED_LAYERS_BLEND;
    std::cout << "peeling_depth_state_native mode="
              << int(gl.getGLRenderAction()->getTransparencyType())
              << " native_peeling=" << nativePeeling << '\n';
    ok &= check(nativePeeling,"CoinGL sorted layers must render without fallback");
    if(nativePeeling) {
      const size_t gIndex=size_t((side-1-24)*side+32)*4;
      const size_t nIndex=size_t(24*side+32)*4;
      for(int channel=0;channel<3;++channel) {
        const int delta=std::abs(int(g[gIndex+channel])-
          int(gl.getBuffer()[nIndex+channel]));
        std::cout << "peeling_depth_state_native channel=" << channel
                  << " delta=" << delta << '\n';
        ok &= check(delta<=6,"CoinRender and CoinGL simple RGB");
      }
    }
  }
  // The annotation is spatially separate from the peeling sample. It must
  // appear in the foreground after composition without changing that sample.
  auto * annotation=new SoAnnotation;scene.root->addChild(annotation);
  auto * fg=new SoMaterial;fg->diffuseColor.setValue(1,1,0);
  fg->transparency=0;annotation->addChild(fg);
  auto * coords=new SoCoordinate3;
  const SbVec3f quad[]={SbVec3f(.4f,-.2f,-4),SbVec3f(.8f,-.2f,-4),
    SbVec3f(.8f,.2f,-4),SbVec3f(.4f,.2f,-4)};
  coords->point.setValues(0,4,quad);annotation->addChild(coords);
  auto * face=new SoIndexedFaceSet;
  const int32_t indices[]={0,1,2,-1,0,2,3,-1};
  face->coordIndex.setValues(0,8,indices);annotation->addChild(face);
  const bool annotated=gpu.capture(scene,2,g,gd) && cpu.capture(scene,2,c,cd);
  ok &= check(annotated,"annotation after peeling must publish");
  if(annotated) {
    ok &= match(g,gd,expected,"annotation center/GPU");
    ok &= match(c,cd,expected,"annotation center/CPU");
    const size_t p=size_t((side-1-32)*side+48)*4;
    const auto yellow=[&](const std::vector<uint8_t> & image) {
      return image[p]>245 && image[p+1]>245 && image[p+2]<10;
    };
    ok &= check(yellow(g) && yellow(c),"foreground annotation remains visible");
  }
  return ok;
}
bool alphaOutput(Runner & gpu,Runner & cpu) {
  bool ok=true;
  const std::vector<Layer> layers={{2,{{.9,.1,.1,0}}},
    {1,{{.1,.8,.2,.25}}},{0,{{.1,.2,.9,.6}}}};
  Scene scene(layers,{2,0,1},false);
  scene.depthState(true,false,SoDepthBuffer::LEQUAL);
  gpu.action.setBackgroundColor(SbColor4f(0,0,0,0));
  cpu.action.setBackgroundColor(SbColor4f(0,0,0,0));
  const Color clear={{0,0,0,0}};
  for(int count:{1,2,4,8}) {
    const auto expected=oracle(layers,scene.order,SoDepthBuffer::LEQUAL,
      true,false,count,false,0,1,clear);
    std::vector<uint8_t> g,c;std::vector<float> gd,cd;
    const bool captured=gpu.capture(scene,count,g,gd) &&
      cpu.capture(scene,count,c,cd);
    ok &= check(captured,"transparent-only alpha profile must publish");
    if(captured) {
      ok &= match(g,gd,expected,"alpha/GPU/passes="+std::to_string(count));
      ok &= match(c,cd,expected,"alpha/CPU/passes="+std::to_string(count));
    }
  }
  gpu.action.setBackgroundColor(SbColor4f(0,0,0,1));
  cpu.action.setBackgroundColor(SbColor4f(0,0,0,1));
  return ok;
}
bool rollback(Runner & runner) {
  const std::vector<Layer> layers={{2,{{.9,.2,.1,.5}}},
    {1,{{.1,.2,.9,.4}}}};
  Scene scene(layers,{0,1});
  std::vector<uint8_t> before,after;std::vector<float> depthBefore,depthAfter;
  if(!runner.capture(scene,2,before,depthBefore)) return check(false,"rollback baseline");
  const auto serial=runner.target->getLastSubmissionSerial();
  runner.action.setSortedLayersNumPasses(9);
  runner.action.apply(scene.root);
  runner.target->readbackRGBA(after);runner.target->readbackDepth(depthAfter);
  bool ok=check(runner.action.getLastStatus()==CoinRenderAction::UNSUPPORTED &&
    before==after && depthBefore==depthAfter &&
    serial==runner.target->getLastSubmissionSerial(),
    "invalid pass budget must preserve RGBA/depth/serial");
  runner.action.setSortedLayersNumPasses(2);
  const auto budget=runner.action.getTransparencyBufferBudget();
  runner.action.setTransparencyBufferBudget(1);
  runner.action.apply(scene.root);
  runner.target->readbackRGBA(after);runner.target->readbackDepth(depthAfter);
  ok &= check(runner.action.getLastStatus()==CoinRenderAction::UNSUPPORTED &&
    before==after && depthBefore==depthAfter &&
    serial==runner.target->getLastSubmissionSerial(),
    "insufficient transparency budget must preserve RGBA/depth/serial");
  runner.action.setTransparencyBufferBudget(budget);
  scene.states.front()->range=SbVec2f(
    std::numeric_limits<float>::quiet_NaN(),1);
  runner.action.apply(scene.root);
  runner.target->readbackRGBA(after);runner.target->readbackDepth(depthAfter);
  ok &= check(runner.action.getLastStatus()!=CoinRenderAction::SUCCESS &&
    before==after && depthBefore==depthAfter &&
    serial==runner.target->getLastSubmissionSerial(),
    "invalid depth range must preserve RGBA/depth/serial");
  scene.states.front()->range=SbVec2f(0,1);
  ok &= check(runner.capture(scene,2,after,depthAfter) &&
    before==after && depthBefore==depthAfter,
    "valid profile must recover exactly after rejection");
  return ok;
}
bool budgetBoundary(Runner &runner) {
  Scene scene({{.5,{{1,0,0,.5}}},{-.5,{{0,1,0,.25}}}}, {0,1});
  scene.depthState(true,false,SoDepthBuffer::LEQUAL);
  const uint64_t required=uint64_t(side)*side*(16*2+8)*requestedSamples;
  const uint64_t old=runner.action.getTransparencyBufferBudget();
  runner.action.setTransparencyBufferBudget(required);
  std::vector<uint8_t> first,after,recovered;
  std::vector<float> firstDepth,afterDepth,recoveredDepth;
  bool ok=check(runner.capture(scene,2,first,firstDepth),
    "WH*(16*layers+8)*N exact budget must admit valid peeling");
  const uint64_t firstSerial=runner.target->getLastSubmissionSerial();
  runner.action.setTransparencyBufferBudget(required-1);
  runner.action.apply(scene.root);
  runner.target->readbackRGBA(after);runner.target->readbackDepth(afterDepth);
  std::cout<<"msaa_peeling budget samples="<<requestedSamples
    <<" exact="<<required<<" status="<<int(runner.action.getLastStatus())
    <<" error="<<runner.action.getLastError().getString()<<'\n';
  ok &= check(runner.action.getLastStatus()==CoinRenderAction::UNSUPPORTED &&
    after==first && afterDepth==firstDepth &&
    runner.target->getLastSubmissionSerial()==firstSerial,
    "one byte below multisample peeling budget rejects transactionally");
  runner.action.setTransparencyBufferBudget(required);
  ok &= check(runner.capture(scene,2,recovered,recoveredDepth) &&
    recovered==first && recoveredDepth==firstDepth &&
    runner.target->getLastSubmissionSerial()==firstSerial+1,
    "exact budget recovers RGBA/depth and one publication");
  runner.action.setTransparencyBufferBudget(old);
  return ok;
}
bool shadowSlots(const CoinRenderCapabilities &caps) {
  bool ok=true;
  for(bool directional:{false,true})for(int count:{1,2,4,8}) {
    msaa_shadow_test::Scene scene(count,directional);
    scene.receiverMaterial->transparency=.5f;
    msaa_shadow_test::Harness run(caps,requestedSamples);
    if(!check(bool(run.target) &&
      run.target->getOptions().renderer==requestedRenderer &&
      run.target->getOptions().sampleCount==requestedSamples,
      "shadow peeling real renderer/sample count"))return false;
    run.action.setSortedLayersNumPasses(2);
    msaa_shadow_test::Frame original,inactive,mutated,restored;
    auto capture=[&](msaa_shadow_test::Frame &f,const char *label) {
      return run.render(scene.root,f,label,CoinRenderAction::SORTED_LAYERS_BLEND);
    };
    ok &= capture(original,"peeling with all maps");
    scene.group->isActive=FALSE;
    ok &= capture(inactive,"peeling with group inactive");
    scene.group->isActive=TRUE;
    if(ok) ok &= check(msaa_shadow_test::differentRgb(original,inactive)>15,
      "active spot/directional VSM changes transparent receiver");
    for(int slot=0;slot<count;++slot) {
      const float home=count==1?0.f:(float(slot)-float(count-1)/2.f)*.34f;
      scene.casterX(slot,home+.65f);
      ok &= capture(mutated,"one peeling shadow slot moved");
      scene.casterX(slot,home);
      ok &= capture(restored,"one peeling shadow slot restored");
      if(ok) ok &= check(msaa_shadow_test::differentRgb(original,mutated)>5 &&
        restored.rgba==original.rgba && restored.depth==original.depth &&
        restored.serial==mutated.serial+1,
        "every 1/2/4/8 map contributes and A/B/A restores exactly");
      original=restored;
    }
  }
  return ok;
}
// The opaque control measures k covered samples independently of peeling.
// A translucent red sheet is entirely behind the slanted green triangle;
// compositing it after an opaque resolve would contaminate green samples.
bool sampleOcclusion(Runner &runner) {
  auto *root=new SoSeparator;root->ref();
  auto *camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,2);camera->height=2;
  camera->nearDistance=1;camera->farDistance=3;root->addChild(camera);
  auto *light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
  auto add=[&](float z,const SbColor &rgb,float alpha,bool triangle) {
    auto *branch=new SoSeparator;root->addChild(branch);
    auto *mat=new SoMaterial;mat->diffuseColor=rgb;mat->transparency=1-alpha;
    branch->addChild(mat);
    auto *coord=new SoCoordinate3;
    const SbVec3f tri[]={SbVec3f(-1,-1,z),SbVec3f(1,-1,z),SbVec3f(-1,.83f,z)};
    const SbVec3f quad[]={SbVec3f(-1,-1,z),SbVec3f(1,-1,z),
      SbVec3f(1,1,z),SbVec3f(-1,1,z)};
    coord->point.setValues(0,triangle?3:4,triangle?tri:quad);branch->addChild(coord);
    auto *face=new SoIndexedFaceSet;
    const int32_t ti[]={0,1,2,-1},qi[]={0,1,2,3,-1};
    face->coordIndex.setValues(0,triangle?4:5,triangle?ti:qi);
    branch->addChild(face);return branch;
  };
  auto *opaque=add(0,SbColor(0,1,0),1,true);
  runner.action.setBackgroundColor(SbColor4f(0,0,1,1));
  std::vector<uint8_t> control,mixed,restored;
  std::vector<float> controlDepth,mixedDepth,restoredDepth;
  runner.action.setSortedLayersNumPasses(1);
  auto take=[&](std::vector<uint8_t>&rgb,std::vector<float>&depth) {
    runner.action.apply(root);runner.target->readbackRGBA(rgb);
    runner.target->readbackDepth(depth);
    return runner.action.getLastStatus()==CoinRenderAction::SUCCESS &&
      rgb.size()==size_t(side*side*4) && depth.size()==size_t(side*side);
  };
  bool ok=check(take(control,controlDepth),"opaque sample control publishes");
  const uint64_t s0=runner.target->getLastSubmissionSerial();
  auto *red=add(-.5f,SbColor(1,0,0),.5f,false);
  ok &= check(take(mixed,mixedDepth),"peeling sample-occlusion publishes");
  const uint64_t s1=runner.target->getLastSubmissionSerial();
  root->removeChild(red);
  ok &= check(take(restored,restoredDepth),"peeling sample-occlusion restores");
  const uint64_t s2=runner.target->getLastSubmissionSerial();
  size_t half=0,quarter=0,three=0,interior=0;
  if(ok) for(int y=3;y<side-3;++y)for(int x=3;x<side-3;++x) {
    const size_t p=size_t(y)*side+x;
    const int green=control[4*p+1];
    const int k=int(std::lround(double(green)*requestedSamples/255.));
    if(k<0 || k>int(requestedSamples) ||
       std::abs(green-int(std::lround(255.*k/requestedSamples)))>2) {
      ok &= check(false,"opaque control has a physical k/N level");continue;
    }
    const double t=double(k)/requestedSamples;
    const int expected[]={int(std::lround(127.5*(1-t))),int(std::lround(255*t)),
      int(std::lround(127.5*(1-t))),255};
    for(int c=0;c<4;++c) if(std::abs(int(mixed[4*p+c])-expected[c])>3) {
      std::cerr<<"msaa_peeling sample="<<requestedSamples<<" xy="<<x<<','<<y
               <<" channel="<<c<<" actual="<<int(mixed[4*p+c])
               <<" expected="<<expected[c]<<'\n';ok=false;
    }
    const float wantDepth=k? .5f:1.f;
    if(std::abs(mixedDepth[p]-wantDepth)>1e-6f) {
      std::cerr<<"msaa_peeling depth sample="<<requestedSamples<<" xy="<<x<<','<<y
               <<" actual="<<mixedDepth[p]<<" expected="<<wantDepth<<'\n';ok=false;
    }
    interior+=(k==0||k==int(requestedSamples));
    half+=(2*k==int(requestedSamples)&&k>0);
    quarter+=(requestedSamples==4&&k==1);
    three+=(requestedSamples==4&&k==3);
  }
  std::cout<<"msaa_peeling samples="<<requestedSamples<<" interior="<<interior
           <<" half="<<half<<" quarter="<<quarter<<" threequarter="<<three<<'\n';
  ok &= check(interior>500 && (requestedSamples==1 ||
    (requestedSamples==2 ? half>0 : quarter>0&&three>0)),
    "real per-sample 2x/4x coverage and opaque occlusion");
  ok &= check(control==restored && controlDepth==restoredDepth &&
    s0+1==s1 && s1+1==s2,"exact A/B/A and one publication per frame");
  // A zero-alpha layer in front of the red sheet must consume the only
  // requested peeled depth on covered samples. Its slanted boundary yields
  // a second, independently measured k/N mask distinct from the opaque one.
  // Isolate transparent-layer coverage from the opaque triangle. The
  // framebuffer's vertical origin must not make an opaque green sample
  // masquerade as the white near-layer control.
  root->removeChild(opaque);
  auto *redAgain=add(-.5f,SbColor(1,0,0),.5f,false);
  auto *redPolicy=new SoDepthBuffer;redPolicy->test=TRUE;
  redPolicy->write=FALSE;redAgain->insertChild(redPolicy,0);
  std::vector<uint8_t> redReference;
  std::vector<float> redReferenceDepth;
  ok &= check(take(redReference,redReferenceDepth),
    "transparent red reference without opaque occluder");
  const uint64_t redReferenceSerial=runner.target->getLastSubmissionSerial();
  auto *zero=add(.5f,SbColor(1,1,1),1,false);
  auto *zeroMaterial=static_cast<SoMaterial*>(zero->getChild(0));
  auto *zeroCoord=static_cast<SoCoordinate3*>(zero->getChild(1));
  zeroCoord->point.set1Value(1,SbVec3f(-.1f,-1,.5f));
  zeroCoord->point.set1Value(2,SbVec3f(.1f,1,.5f));
  auto *zeroPolicy=new SoDepthBuffer;zeroPolicy->test=TRUE;
  zeroPolicy->write=TRUE;zero->insertChild(zeroPolicy,0);
  std::vector<uint8_t> white,alphaZero,redRestored;
  std::vector<float> whiteDepth,zeroDepth,redRestoredDepth;
  ok &= check(take(white,whiteDepth),"opaque zero-layer coverage control");
  const uint64_t s3=runner.target->getLastSubmissionSerial();
  zeroMaterial->transparency=1;
  ok &= check(take(alphaZero,zeroDepth),"alpha-zero layer publishes");
  const uint64_t s4=runner.target->getLastSubmissionSerial();
  root->removeChild(zero);
  ok &= check(take(redRestored,redRestoredDepth),"alpha-zero A/B/A restores");
  const uint64_t s5=runner.target->getLastSubmissionSerial();
  size_t zeroHalf=0,zeroQuarter=0,zeroThree=0;
  if(ok) for(int y=48;y<side-3;++y)for(int x=24;x<40;++x) {
    const size_t p=size_t(y)*side+x;
    // In this ROI the opaque triangle is absent. Green in the white
    // control is precisely the number of samples covered by the slanted
    // near layer, independently of the peeling result.
    const int k=int(std::lround(double(white[4*p+1])*requestedSamples/255.));
    if(k<0||k>int(requestedSamples)||
       std::abs(int(white[4*p+1])-int(std::lround(255.*k/requestedSamples)))>2)
      {ok&=check(false,"alpha-zero independent coverage level");continue;}
    const double remaining=1.-double(k)/requestedSamples;
    const int wantR=int(std::lround(127.5*remaining));
    const int wantB=int(std::lround(255.-127.5*remaining));
    if(std::abs(int(alphaZero[4*p])-wantR)>3 ||
       std::abs(int(alphaZero[4*p+1]))>3 ||
       std::abs(int(alphaZero[4*p+2])-wantB)>3 ||
       std::abs(zeroDepth[p]-(k?.25f:1.f))>1e-6f) {
      std::cerr<<"msaa_peeling alpha0 xy="<<x<<','<<y<<" k="<<k
        <<" rgba="<<int(alphaZero[4*p])<<','<<int(alphaZero[4*p+1])
        <<','<<int(alphaZero[4*p+2])<<" depth="<<zeroDepth[p]
        <<" expected="<<wantR<<",0,"<<wantB<<" depth="<<(k?.25f:1.f)<<'\n';
      ok=false;
    }
    zeroHalf+=(requestedSamples==2&&k==1);
    zeroQuarter+=(requestedSamples==4&&k==1);
    zeroThree+=(requestedSamples==4&&k==3);
  }
  std::cout<<"msaa_peeling alpha0 samples="<<requestedSamples<<" half="<<zeroHalf
    <<" quarter="<<zeroQuarter<<" threequarter="<<zeroThree<<'\n';
  ok &= check(requestedSamples==1 || (requestedSamples==2?zeroHalf>0:
    zeroQuarter>0&&zeroThree>0),"zero-alpha layer occupies depths per sample");
  ok &= check(redRestored==redReference && redRestoredDepth==redReferenceDepth &&
    s2+1==redReferenceSerial && redReferenceSerial+1==s3 &&
    s3+1==s4 && s4+1==s5,
    "zero-alpha selection, depth write and exact recovery publish once");
  runner.action.setBackgroundColor(SbColor4f(0,0,0,1));
  root->unref();return ok;
}
bool rtt(bool direct) {
  CoinRenderOptions options=testOptions();
  options.sceneTexture=direct ? COIN_RENDER_SCENE_TEXTURE_DIRECT :
    COIN_RENDER_SCENE_TEXTURE_STAGED;
  // Exercise an already initialized shared runtime: the first target stays
  // alive while a newly created second target performs direct RTT.
  std::unique_ptr<Runner> peer;
  std::vector<uint8_t> peerColor;
  std::vector<float> peerDepth;
  uint64_t peerSerial=0;
  Scene peerScene({},{});
  if(direct) {
    peer.reset(new Runner(false,options));
    if(!peer->target || !peer->capture(peerScene,1,peerColor,peerDepth))
      return check(false,"opaque peer must initialize shared runtime first");
    peerSerial=peer->target->getLastSubmissionSerial();
  }
  Runner runner(false,options);
  if(!runner.target) return check(false,"RTT target creation");
  std::cout << "peeling_depth_rtt_effective_raster peer="
            << (peer ? int(peer->target->getOptions().rasterizationPolicy) : -1)
            << " consumer=" << int(runner.target->getOptions().rasterizationPolicy)
            << '\n';
  auto peerIntact=[&](const std::string & phase) {
    if(!peer) return true;
    std::vector<uint8_t> color;
    std::vector<float> depth;
    peer->target->readbackRGBA(color);
    peer->target->readbackDepth(depth);
    const bool intact=color==peerColor && depth==peerDepth &&
      peer->target->getLastSubmissionSerial()==peerSerial;
    return check(intact,"opaque peer must survive "+phase);
  };
  runner.action.setSortedLayersNumPasses(4);
  const std::vector<Layer> layers={{2,{{.9,.15,.1,.5}}},
    {-2,{{.1,.2,.9,.7}}}};
  Scene producer(layers,{1,0});
  auto * consumer=new SoSeparator;consumer->ref();
  auto * camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,3);camera->height=2;
  camera->nearDistance=.1f;camera->farDistance=10;consumer->addChild(camera);
  auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;
  consumer->addChild(light);
  auto * quality=new SoComplexity;quality->textureQuality=.3f;
  consumer->addChild(quality);
  auto * white=new SoMaterial;white->diffuseColor.setValue(1,1,1);
  consumer->addChild(white);
  auto * texture=new SoSceneTexture2;
  texture->scene=producer.root;texture->size.setValue(side,side);
  texture->type=SoSceneTexture2::RGBA8;
  texture->model=SoSceneTexture2::REPLACE;
  texture->transparencyFunction=SoSceneTexture2::NONE;
  texture->backgroundColor.setValue(.12f,.20f,.30f,1);
  consumer->addChild(texture);
  auto * uv=new SoTextureCoordinate2;
  const SbVec2f values[]={SbVec2f(0,0),SbVec2f(1,0),
    SbVec2f(1,1),SbVec2f(0,1)};
  uv->point.setValues(0,4,values);consumer->addChild(uv);
  auto * coords=new SoCoordinate3;
  const SbVec3f quad[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
    SbVec3f(1,1,0),SbVec3f(-1,1,0)};
  coords->point.setValues(0,4,quad);consumer->addChild(coords);
  auto * face=new SoIndexedFaceSet;
  const int32_t indices[]={0,1,2,3,-1};
  face->coordIndex.setValues(0,5,indices);consumer->addChild(face);
  auto capture=[&](std::vector<uint8_t> & color,std::vector<float> & depth) {
    runner.action.apply(consumer);
    runner.target->readbackRGBA(color);runner.target->readbackDepth(depth);
    const bool published=runner.action.getLastStatus()==CoinRenderAction::SUCCESS &&
      color.size()==size_t(side*side*4) && depth.size()==size_t(side*side);
    if(!published)
      std::cerr << "peeling_depth_rtt mode=" << (direct ? "direct" : "staged")
                << " status=" << int(runner.action.getLastStatus())
                << " color_bytes=" << color.size() << " depth_pixels=" << depth.size()
                << " diagnostic=" << runner.action.getLastError().getString() << '\n';
    return published;
  };
  bool ok=true;
  std::vector<uint8_t> lastColor,color;std::vector<float> lastDepth,depth;
  uint64_t lastSerial=0;
  for(int profile=0;profile<3;++profile) {
    const bool test=profile!=2;
    const int fn=profile==1 ? SoDepthBuffer::GREATER : SoDepthBuffer::LESS;
    producer.depthState(test,true,fn);
    const auto expected=oracle(layers,producer.order,fn,test,true,4,true,0,1);
    const bool captured=capture(color,depth);
    const std::string label=std::string(direct ? "direct" : "staged")+
      "/profile="+std::to_string(profile);
    ok &= check(captured,label+" RTT producer and consumer must publish");
    ok &= peerIntact(label);
    if(captured) {
      const size_t p=size_t((side-1-32)*side+32)*4;
      for(int c=0;c<3;++c) {
        const int wanted=int(std::lround(expected.color[c]*255));
        const int actual=color[p+c];
        if(std::abs(actual-wanted)>6)
          std::cerr << label << " channel=" << c << " actual=" << actual
                    << " expected=" << wanted << '\n';
        ok &= check(std::abs(actual-wanted)<=6,
          label+" RTT must consume producer peeling color");
      }
      const float consumerDepth=depth[p/4];
      ok &= check(std::isfinite(consumerDepth) && consumerDepth>0 &&
        consumerDepth<1,label+" consumer depth must publish");
      lastColor=color;lastDepth=depth;
      lastSerial=runner.target->getLastSubmissionSerial();
    }
  }
  producer.states.front()->range=SbVec2f(
    std::numeric_limits<float>::quiet_NaN(),1);
  runner.action.apply(consumer);
  runner.target->readbackRGBA(color);runner.target->readbackDepth(depth);
  ok &= check(runner.action.getLastStatus()!=CoinRenderAction::SUCCESS &&
    color==lastColor && depth==lastDepth &&
    lastSerial==runner.target->getLastSubmissionSerial(),
    "invalid RTT producer must preserve consumer RGBA/depth/serial");
  ok &= peerIntact("RTT rejection");
  producer.states.front()->range=SbVec2f(0,1);
  ok &= check(capture(color,depth) && color==lastColor && depth==lastDepth,
    "RTT producer must recover exact consumer frame");
  ok &= peerIntact("RTT recovery");
  consumer->unref();
  return ok;
}
} // namespace
int main(int argc,char ** argv) {
  SoDB::init();CoinRenderAction::initClass();
  std::string mode="--offscreen";
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--canonical-raster") canonicalRaster=true;
    else if(arg=="--offscreen" || arg=="--coverage" || arg=="--cpu" ||
            arg=="--sloped-depth" || arg=="--sloped-depth-state" ||
            arg=="--opaque-sloped-oracle" ||
            arg=="--opaque-slope-rtt-staged" ||
            arg=="--opaque-slope-rtt-direct" ||
            arg=="--shadows" || arg=="--rejections" ||
            arg=="--rtt-staged" || arg=="--rtt-direct") mode=arg;
    else if(arg=="--samples=1" || arg=="--samples=2" || arg=="--samples=4")
      requestedSamples=uint32_t(arg.back()-'0');
    else return 1;
  }
  CoinRenderCapabilities caps{};
  const bool available=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0 && caps.gpu_available;
  std::cout << "peeling_depth_state backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " device=" << caps.device_id << " adapter=" << caps.adapter_name
            << " gpu_available=" << caps.gpu_available << '\n';
  if(!available) return 77;
  if(caps.renderer==COIN_RENDER_RENDERER_UNKNOWN) return 77;
  requestedRenderer=static_cast<CoinRenderRenderer>(caps.renderer);
  wgpuBackend=caps.backend==COIN_RENDER_EXPERIMENTAL_RUST;
  if(mode=="--rtt-staged")
    return rtt(false) ? 0 : 1;
  if(mode=="--rtt-direct")
    return rtt(true) ? 0 : 1;
  if(mode=="--shadows") return shadowSlots(caps)?0:1;
  if(mode=="--opaque-slope-rtt-staged" || mode=="--opaque-slope-rtt-direct") {
    CoinRenderOptions options=testOptions();
    options.sceneTexture=mode=="--opaque-slope-rtt-direct" ?
      COIN_RENDER_SCENE_TEXTURE_DIRECT:COIN_RENDER_SCENE_TEXTURE_STAGED;
    Runner rtt(false,options);
    if(!rtt.target || rtt.target->getOptions().renderer!=requestedRenderer ||
       rtt.target->getOptions().sampleCount!=requestedSamples ||
       rtt.target->getOptions().sceneTexture!=options.sceneTexture)
      return check(false,"opaque slope RTT effective API/route/count")?0:1;
    return opaqueSlopedOracle(rtt,
      mode=="--opaque-slope-rtt-direct"?"RTT direct":"RTT staged",true)?0:1;
  }
  Runner gpu(mode!="--cpu"?false:true,testOptions()),cpu(true,testOptions());
  if(!gpu.target || !cpu.target) return 1;
  if(mode!="--cpu" && (gpu.target->getOptions().sampleCount!=requestedSamples ||
      gpu.target->getOptions().renderer!=requestedRenderer))
    return check(false,"requested GPU API and sample count must be effective") ? 0:1;
  std::cout << "peeling_depth_effective_raster GPU="
            << int(gpu.target->getOptions().rasterizationPolicy)
            << " CPU=" << int(cpu.target->getOptions().rasterizationPolicy) << '\n';
  if(mode=="--coverage") return sampleOcclusion(gpu)?0:1;
  if(mode=="--sloped-depth") {
    bool ok=slopedDepth(gpu,"GPU");
    if(requestedSamples==1)ok &= slopedDepth(cpu,"CPU");
    return ok?0:1;
  }
  if(mode=="--sloped-depth-state") {
    bool ok=slopedDepth(gpu,"GPU",.2f,.8f,true);
    ok &= slopedDepth(gpu,"GPU",.8f,.2f,true);
    if(requestedSamples==1) {
      ok &= slopedDepth(cpu,"CPU",.2f,.8f,true);
      ok &= slopedDepth(cpu,"CPU",.8f,.2f,true);
    }
    return ok?0:1;
  }
  if(mode=="--opaque-sloped-oracle") {
    bool ok=opaqueSlopedOracle(gpu,"GPU");
    if(requestedSamples==1)ok &= opaqueSlopedOracle(cpu,"CPU");
    return ok?0:1;
  }
  if(mode=="--rejections") return (budgetBoundary(gpu) && rollback(gpu))?0:1;
  bool ok=runMatrix(gpu,cpu);
  ok &= stateTransitions(gpu,cpu);
  ok &= mixedFragmentStates(gpu,cpu);
  ok &= annotationAndReference(gpu,cpu);
  ok &= alphaOutput(gpu,cpu);
  ok &= rollback(gpu);
  ok &= rollback(cpu);
  if(mode=="--offscreen") ok &= sampleOcclusion(gpu);
  return ok ? 0 : 1;
}
