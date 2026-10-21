#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoIndexedMarkerSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#ifdef COIN_ALPHA_X11
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>
#include <thread>
#endif
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int frameSide=96;
constexpr int mapSide=256;
struct Frame {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial=0;
};
bool require(bool valid,const std::string & reason) {
  if(!valid) std::cerr << "shadow_alpha: " << reason << '\n';
  return valid;
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoShadowGroup * group=new SoShadowGroup;
  SoShadowDirectionalLight * light=new SoShadowDirectionalLight;
  SoSeparator * map=new SoSeparator;
  SoAlphaTest * alpha=new SoAlphaTest;
  Scene() {
    root->ref();
    auto * observer=new SoOrthographicCamera;
    observer->position.setValue(0,0,6);
    observer->height=4;
    observer->nearDistance=1;
    observer->farDistance=20;
    root->addChild(observer);
    auto * lighting=new SoLightModel;
    lighting->model=SoLightModel::PHONG;
    root->addChild(lighting);
    group->quality=.2f;
    group->precision=.125f;
    root->addChild(group);
    light->direction.setValue(0,0,-1);
    light->intensity=.7f;
    light->bboxCenter.setValue(0,0,0);
    light->bboxSize.setValue(4,4,8);
    light->shadowMapScene=map;
    group->addChild(light);
    auto * receiver=new SoSeparator;
    group->addChild(receiver);
    auto * receiveStyle=new SoShadowStyle;
    receiveStyle->style=SoShadowStyle::SHADOWED;
    receiver->addChild(receiveStyle);
    auto * material=new SoMaterial;
    material->diffuseColor.setValue(.72f,.75f,.78f);
    material->ambientColor.setValue(.18f,.18f,.18f);
    material->specularColor.setValue(0,0,0);
    receiver->addChild(material);
    auto * receiverCoords=new SoCoordinate3;
    const SbVec3f receiverPoints[]={SbVec3f(-1.8f,-1.8f,-1),
      SbVec3f(1.8f,-1.8f,-1),SbVec3f(1.8f,1.8f,-1),
      SbVec3f(-1.8f,1.8f,-1)};
    receiverCoords->point.setValues(0,4,receiverPoints);
    receiver->addChild(receiverCoords);
    auto * receiverFace=new SoFaceSet;
    receiverFace->numVertices.set1Value(0,4);
    receiver->addChild(receiverFace);
    auto * casterStyle=new SoShadowStyle;
    casterStyle->style=SoShadowStyle::CASTS_SHADOW;
    map->addChild(casterStyle);
    map->addChild(alpha);
    auto * casterMaterial=new SoMaterial;
    casterMaterial->diffuseColor.setValue(1,1,1);
    casterMaterial->transparency=0;
    map->addChild(casterMaterial);
    auto * casterCoords=new SoCoordinate3;
    // One tilted plane: z=.6+.625*x. Alpha from the VSM shader changes
    // across this one surface, rather than per-object or per-material.
    const SbVec3f casterPoints[]={SbVec3f(-.8f,-.5f,.1f),
      SbVec3f(.8f,-.5f,1.1f),SbVec3f(.8f,.5f,1.1f),
      SbVec3f(-.8f,.5f,.1f)};
    casterCoords->point.setValues(0,4,casterPoints);
    map->addChild(casterCoords);
    auto * casterFace=new SoFaceSet;
    casterFace->numVertices.set1Value(0,4);
    map->addChild(casterFace);
  }
  ~Scene() {root->unref();}
};
struct MultiScene {
  Scene base;
  std::vector<SoAlphaTest *> tests;
  std::vector<float> offsets;
  std::vector<SoSeparator *> mapScenes;
  explicit MultiScene(int count) {
    base.light->intensity=.7f/count;
    tests.push_back(base.alpha);
    offsets.push_back(0.f);
    mapScenes.push_back(base.map);
    for(int slot=1;slot<count;++slot) {
      const float offset=.13f*slot;
      auto * light=new SoShadowDirectionalLight;
      light->direction.setValue(0,0,-1);
      light->intensity=.7f/count;
      light->bboxCenter.setValue(0,0,0);
      light->bboxSize.setValue(4,4,8);
      auto * map=new SoSeparator;
      auto * style=new SoShadowStyle;
      style->style=SoShadowStyle::CASTS_SHADOW;
      map->addChild(style);
      auto * alpha=new SoAlphaTest;
      map->addChild(alpha);
      auto * material=new SoMaterial;
      material->diffuseColor.setValue(1,1,1);
      material->transparency=0;
      map->addChild(material);
      auto * coordinates=new SoCoordinate3;
      const SbVec3f points[]={SbVec3f(offset-.8f,-.5f,.1f),
        SbVec3f(offset+.8f,-.5f,1.1f),
        SbVec3f(offset+.8f,.5f,1.1f),
        SbVec3f(offset-.8f,.5f,.1f)};
      coordinates->point.setValues(0,4,points);
      map->addChild(coordinates);
      auto * face=new SoFaceSet;
      face->numVertices.set1Value(0,4);
      map->addChild(face);
      light->shadowMapScene=map;
      base.group->insertChild(light,slot);
      tests.push_back(alpha);
      offsets.push_back(offset);
      mapScenes.push_back(map);
    }
  }
};
bool alphaPass(int function,float raw,float reference) {
  // SoAlphaTest references are clamped by GL; the incoming RGBA32F map
  // shader alpha is signed and is deliberately NOT clamped.
  const float threshold=std::max(0.f,std::min(1.f,reference));
  switch(function) {
  case SoAlphaTest::NONE:case SoAlphaTest::ALWAYS:return true;
  case SoAlphaTest::NEVER:return false;
  case SoAlphaTest::LESS:return raw<threshold;
  case SoAlphaTest::LEQUAL:return raw<=threshold;
  case SoAlphaTest::EQUAL:return raw==threshold;
  case SoAlphaTest::GEQUAL:return raw>=threshold;
  case SoAlphaTest::GREATER:return raw>threshold;
  case SoAlphaTest::NOTEQUAL:return raw!=threshold;
  }
  return false;
}
float vsmFactor(float mean,float second,float receiverDistance) {
  if(receiverDistance<=mean) return 1.f;
  const float variance=std::min(1.f,std::max(0.f,second-mean*mean)+1e-5f);
  const float gap=mean-receiverDistance;
  float p=variance/(variance+gap*gap);
  const float t=std::max(0.f,std::min(1.f,(p-.1f)/.9f));
  p*=t*t*(3.f-2.f*t);
  return p;
}
struct Capture {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(frameSide,frameSide)};
  explicit Capture(bool canonical) {
    std::string diagnostic;
    auto options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
    if(!diagnostic.empty()) std::cerr << diagnostic << '\n';
    if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
    target.reset(CoinRenderTarget::createOffscreen(
      SbVec2i32(frameSide,frameSide),options));
    action.setRenderTarget(target.get());
  }
  ~Capture() {action.setRenderTarget(nullptr);}
  bool render(SoNode * scene,Frame & frame) {
    action.apply(scene);
    if(action.getLastStatus()!=CoinRenderAction::SUCCESS) {
      std::cerr << "shadow_alpha status=" << action.getLastStatus()
                << " diagnostic=" << action.getLastError().getString() << '\n';
      return false;
    }
    target->readbackRGBA(frame.rgba);
    target->readbackDepth(frame.depth);
    frame.serial=target->getLastSubmissionSerial();
    return frame.rgba.size()==size_t(frameSide*frameSide*4) &&
      frame.depth.size()==size_t(frameSide*frameSide);
  }
};
struct CpuCapture {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(frameSide,frameSide)};
  explicit CpuCapture(bool canonical) {
    std::string diagnostic;
    auto options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
    if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
    target.reset(CoinRenderTarget::createOffscreen(
      SbVec2i32(frameSide,frameSide),options));
    if(!target) return;
    target->setDepthReadbackEnabled(TRUE);
    target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    action.setRenderTarget(target.get());
  }
  ~CpuCapture() {action.setRenderTarget(nullptr);}
  bool render(SoNode * scene,Frame & frame) {
    action.apply(scene);
    if(action.getLastStatus()!=CoinRenderAction::SUCCESS) {
      std::cerr << "shadow_alpha CPU status=" << action.getLastStatus()
                << " diagnostic=" << action.getLastError().getString() << '\n';
      return false;
    }
    target->readbackRGBA(frame.rgba);
    target->readbackDepth(frame.depth);
    frame.serial=target->getLastSubmissionSerial();
    return frame.rgba.size()==size_t(frameSide*frameSide*4) &&
      frame.depth.size()==size_t(frameSide*frameSide);
  }
};
bool oneFunction(Capture & capture,int function,float reference,
                 bool canonical) {
  Scene scene;
  scene.alpha->function=function;
  scene.alpha->value=reference;
  Frame frame;
  const std::string label="function="+std::to_string(function)+
    "/reference="+std::to_string(reference);
  if(!require(capture.render(scene.root,frame),label+" publishes")) return false;
  const float radius=std::sqrt(24.f);
  const float nearValue=(radius-4.f)*1.01f;
  const float farValue=(radius+4.f)*1.01f*1.1f;
  const float receiverMoment=(radius+1.f-nearValue)/(farValue-nearValue);
  std::vector<float> first(size_t(mapSide*mapSide),1.f);
  std::vector<float> second(size_t(mapSide*mapSide),1.f);
  int negative=0,positive=0,passed=0,rejected=0;
  for(int ty=0;ty<mapSide;++ty) for(int tx=0;tx<mapSide;++tx) {
    const float wx=(tx+.5f-mapSide/2.f)*2.f*radius/mapSide;
    const float wy=(ty+.5f-mapSide/2.f)*2.f*radius/mapSide;
    if(wx<=-.8f || wx>=.8f || wy<=-.5f || wy>=.5f) continue;
    const float z=.6f+.625f*wx;
    const float l=(radius-z-nearValue)/(farValue-nearValue);
    const float m2=l*l;
    const float phase=64.f*m2;
    const float raw=2.f*(phase-std::floor(phase))-1.f;
    negative+=raw<0;positive+=raw>0;
    if(alphaPass(function,raw,reference)) {
      first[size_t(ty*mapSide+tx)]=l;
      second[size_t(ty*mapSide+tx)]=m2;
      ++passed;
    } else ++rejected;
  }
  auto texel=[&](const std::vector<float> & values,int x,int y) {
    x=std::max(0,std::min(mapSide-1,x));
    y=std::max(0,std::min(mapSide-1,y));
    return values[size_t(y*mapSide+x)];
  };
  int maxRgb=0,bad=0,shadowed=0,maxAlpha=0;
  double maxDepth=0;
  for(int y=0;y<frameSide;++y) for(int x=0;x<frameSide;++x) {
    const float wx=(x+.5f-frameSide/2.f)*4.f/frameSide;
    const float wy=(frameSide/2.f-(y+.5f))*4.f/frameSide;
    if(std::abs(wx)>=1.7f || std::abs(wy)>=1.7f) continue;
    const float texX=(.5f+wx/(2.f*radius))*mapSide-.5f;
    const float texY=(.5f+wy/(2.f*radius))*mapSide-.5f;
    const int x0=int(std::floor(texX)),y0=int(std::floor(texY));
    const float fx=texX-x0,fy=texY-y0;
    auto sample=[&](const std::vector<float> & values) {
      const float low=texel(values,x0,y0)*(1.f-fx)+
        texel(values,x0+1,y0)*fx;
      const float high=texel(values,x0,y0+1)*(1.f-fx)+
        texel(values,x0+1,y0+1)*fx;
      return low*(1.f-fy)+high*fy;
    };
    const float factor=vsmFactor(sample(first),sample(second),receiverMoment);
    shadowed+=factor<.9f;
    const size_t p=size_t(y*frameSide+x);
    const float diffuse[]={.72f,.75f,.78f};
    for(int channel=0;channel<3;++channel) {
      const int expected=int(std::lround(255.f*(.036f+
        .7f*diffuse[channel]*factor)));
      const int delta=std::abs(int(frame.rgba[p*4+channel])-expected);
      maxRgb=std::max(maxRgb,delta);
      bad+=delta>3;
    }
    maxAlpha=std::max(maxAlpha,std::abs(int(frame.rgba[p*4+3])-255));
    maxDepth=std::max(maxDepth,std::abs(double(frame.depth[p])-6./19.));
  }
  std::cout << "shadow_alpha_map " << label << " canonical=" << canonical
            << " negative/positive=" << negative << '/' << positive
            << " passed/rejected=" << passed << '/' << rejected
            << " shadowed=" << shadowed << " max_rgb=" << maxRgb
            << " bad=" << bad << " max_alpha=" << maxAlpha
            << " max_depth=" << maxDepth << '\n';
  return require(negative>0 && positive>0 && passed+rejected>100 &&
    (passed==0 ? shadowed==0 : shadowed>25) &&
    bad==0 && maxRgb<=3 && maxAlpha==0 && maxDepth<=1e-5,
    label+" signed VSM alpha, depth and receiver color");
}
bool multiFrame(Capture & capture,MultiScene & scene,
                const std::vector<int> & functions,
                const std::vector<float> & references,
                const std::string & label,Frame & frame) {
  const int count=int(scene.tests.size());
  if(!require(int(functions.size())==count && int(references.size())==count,
      label+" slot configuration")) return false;
  for(int slot=0;slot<count;++slot) {
    scene.tests[slot]->function=functions[slot];
    scene.tests[slot]->value=references[slot];
  }
  if(!require(capture.render(scene.base.root,frame),label+" publishes")) return false;
  const float radius=std::sqrt(24.f);
  const float nearValue=(radius-4.f)*1.01f;
  const float farValue=(radius+4.f)*1.01f*1.1f;
  const float receiverMoment=(radius+1.f-nearValue)/(farValue-nearValue);
  struct Moments { std::vector<float> first,second;int passed=0,rejected=0; };
  std::vector<Moments> maps;
  maps.resize(size_t(count));
  for(int slot=0;slot<count;++slot) {
    auto & moments=maps[size_t(slot)];
    moments.first.assign(size_t(mapSide*mapSide),1.f);
    moments.second.assign(size_t(mapSide*mapSide),1.f);
    for(int ty=0;ty<mapSide;++ty) for(int tx=0;tx<mapSide;++tx) {
      const float wx=(tx+.5f-mapSide/2.f)*2.f*radius/mapSide;
      const float wy=(ty+.5f-mapSide/2.f)*2.f*radius/mapSide;
      const float localX=wx-scene.offsets[size_t(slot)];
      if(localX<=-.8f || localX>=.8f || wy<=-.5f || wy>=.5f) continue;
      const float z=.6f+.625f*localX;
      const float l=(radius-z-nearValue)/(farValue-nearValue);
      const float second=l*l;
      const float phase=64.f*second;
      const float raw=2.f*(phase-std::floor(phase))-1.f;
      if(alphaPass(functions[size_t(slot)],raw,references[size_t(slot)])) {
        moments.first[size_t(ty*mapSide+tx)]=l;
        moments.second[size_t(ty*mapSide+tx)]=second;
        ++moments.passed;
      } else ++moments.rejected;
    }
  }
  auto texel=[&](const std::vector<float> & image,int x,int y) {
    x=std::max(0,std::min(mapSide-1,x));
    y=std::max(0,std::min(mapSide-1,y));
    return image[size_t(y*mapSide+x)];
  };
  int maxRgb=0,bad=0,maxAlpha=0;
  double maxDepth=0;
  std::vector<int> contribution(size_t(count),0);
  for(int y=0;y<frameSide;++y) for(int x=0;x<frameSide;++x) {
    const float wx=(x+.5f-frameSide/2.f)*4.f/frameSide;
    const float wy=(frameSide/2.f-(y+.5f))*4.f/frameSide;
    if(std::abs(wx)>=1.7f || std::abs(wy)>=1.7f) continue;
    const float texX=(.5f+wx/(2.f*radius))*mapSide-.5f;
    const float texY=(.5f+wy/(2.f*radius))*mapSide-.5f;
    const int x0=int(std::floor(texX)),y0=int(std::floor(texY));
    const float fx=texX-x0,fy=texY-y0;
    auto sample=[&](const std::vector<float> & image) {
      const float lo=texel(image,x0,y0)*(1.f-fx)+
        texel(image,x0+1,y0)*fx;
      const float hi=texel(image,x0,y0+1)*(1.f-fx)+
        texel(image,x0+1,y0+1)*fx;
      return lo*(1.f-fy)+hi*fy;
    };
    float lightSum=0;
    for(int slot=0;slot<count;++slot) {
      const auto & moments=maps[size_t(slot)];
      const float factor=vsmFactor(sample(moments.first),
        sample(moments.second),receiverMoment);
      contribution[size_t(slot)]+=factor<.9f;
      lightSum+=factor;
    }
    const size_t pixel=size_t(y*frameSide+x);
    const float diffuse[]={.72f,.75f,.78f};
    for(int channel=0;channel<3;++channel) {
      const int expected=int(std::lround(255.f*(.036f+
        diffuse[channel]*(.7f/count)*lightSum)));
      const int delta=std::abs(int(frame.rgba[pixel*4+channel])-expected);
      maxRgb=std::max(maxRgb,delta);
      bad+=delta>3;
    }
    maxAlpha=std::max(maxAlpha,std::abs(int(frame.rgba[pixel*4+3])-255));
    maxDepth=std::max(maxDepth,std::abs(double(frame.depth[pixel])-6./19.));
  }
  std::cout << "shadow_alpha_multi " << label << " maps=" << count
            << " max_rgb=" << maxRgb << " bad=" << bad
            << " max_alpha=" << maxAlpha << " max_depth=" << maxDepth;
  for(int slot=0;slot<count;++slot)
    std::cout << " slot" << slot << "(function/ref/pass/reject/shadow)="
              << functions[size_t(slot)] << '/'
              << references[size_t(slot)] << '/'
              << maps[size_t(slot)].passed << '/'
              << maps[size_t(slot)].rejected << '/'
              << contribution[size_t(slot)];
  std::cout << '\n';
  return require(bad==0 && maxRgb<=3 && maxAlpha==0 && maxDepth<=1e-5,
    label+" independent multi-map signed VSM composition");
}
bool multiMaps(Capture & capture,int count) {
  MultiScene scene(count);
  bool ok=true;
  std::vector<float> references;
  references.resize(size_t(count));
  for(int slot=0;slot<count;++slot)
    references[size_t(slot)]=.125f+.0625f*slot;
  std::vector<int> functions(size_t(count),SoAlphaTest::ALWAYS);
  Frame allAlways,allNever;
  ok &= multiFrame(capture,scene,functions,references,
    "all-always/"+std::to_string(count),allAlways);
  std::fill(functions.begin(),functions.end(),SoAlphaTest::NEVER);
  ok &= multiFrame(capture,scene,functions,references,
    "all-never/"+std::to_string(count),allNever);
  for(int slot=0;slot<count;++slot) {
    functions.assign(size_t(count),SoAlphaTest::ALWAYS);
    functions[size_t(slot)]=SoAlphaTest::NEVER;
    Frame oneNever;
    const bool neverOk=multiFrame(capture,scene,functions,references,
      "one-never/"+std::to_string(count)+"/slot="+
      std::to_string(slot),oneNever);
    ok &= neverOk;
    if(neverOk && !allAlways.rgba.empty())
      ok &= require(oneNever.rgba!=allAlways.rgba,
        "slot "+std::to_string(slot)+" must alter all-map shadow");
    functions.assign(size_t(count),SoAlphaTest::NEVER);
    for(int function:{SoAlphaTest::ALWAYS,SoAlphaTest::LESS,
                      SoAlphaTest::GREATER}) {
      functions[size_t(slot)]=function;
      Frame isolated;
      const bool isolatedOk=multiFrame(capture,scene,functions,references,
        "isolated/"+std::to_string(count)+"/slot="+
        std::to_string(slot)+"/function="+std::to_string(function),isolated);
      ok &= isolatedOk;
      if(function==SoAlphaTest::ALWAYS && isolatedOk &&
          !allNever.rgba.empty())
        ok &= require(isolated.rgba!=allNever.rgba,
          "slot "+std::to_string(slot)+" must cast when isolated");
    }
  }
  functions.assign(size_t(count),SoAlphaTest::ALWAYS);
  functions.back()=SoAlphaTest::GREATER;
  std::vector<float> stableRefs(size_t(count),0.f);
  stableRefs.back()=.25f;
  Frame stable,failed,recovered;
  if(!multiFrame(capture,scene,functions,stableRefs,
      "multi-rollback/stable/"+std::to_string(count),stable)) return false;
  scene.tests.back()->value=std::numeric_limits<float>::quiet_NaN();
  capture.action.apply(scene.base.root);
  const auto status=capture.action.getLastStatus();
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial=capture.target->getLastSubmissionSerial();
  const bool recoveredOk=multiFrame(capture,scene,functions,stableRefs,
    "multi-rollback/recovered/"+std::to_string(count),recovered);
  ok &= require(status!=CoinRenderAction::SUCCESS &&
    failed.rgba==stable.rgba && failed.depth==stable.depth &&
    failed.serial==stable.serial && recoveredOk &&
    recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
    recovered.serial>stable.serial,
    "multi-map invalid reference rollback and recovery");
  return ok;
}
int rasterDifference(const Frame & left,const Frame & right) {
  if(left.rgba.size()!=right.rgba.size() || left.rgba.empty()) return -1;
  int changed=0;
  for(size_t p=0;p<left.rgba.size()/4;++p) {
    bool pixel=false;
    for(int channel=0;channel<3;++channel)
      pixel|=std::abs(int(left.rgba[p*4+channel])-
        int(right.rgba[p*4+channel]))>3;
    changed+=pixel;
  }
  return changed;
}
bool rasterCaster(Capture & capture,int kind,bool spot,bool positivePhase) {
  Scene scene;
  if(spot) {
    auto * light=new SoShadowSpotLight;
    light->location.setValue(0,0,5);
    light->direction.setValue(0,0,-1);
    light->cutOffAngle=.6f;
    light->intensity=.7f;
    light->nearDistance=1;
    light->farDistance=10;
    light->shadowMapScene=scene.map;
    scene.group->replaceChild(0,light);
  }
  // Remove only the geometric caster; keep its authored shadow style,
  // opaque material and active AlphaTest ahead of the raster node.
  scene.map->removeChild(4);
  scene.map->removeChild(3);
  const float radius=std::sqrt(24.f);
  const float nearValue=(radius-4.f)*1.01f;
  const float farValue=(radius+4.f)*1.01f*1.1f;
  const float phase=positivePhase?.75f:.25f;
  const float l=std::sqrt((14.f+phase)/64.f);
  const float z=radius-nearValue-l*(farValue-nearValue);
  const float anchorRaw=2.f*(64.f*l*l-std::floor(64.f*l*l))-1.f;
  auto * raster=new SoSeparator;
  scene.map->addChild(raster);
  auto * move=new SoTranslation;
  move->translation.setValue(0,0,z);
  raster->addChild(move);
  if(kind==0) {
    auto * font=new SoFont;
    font->name="defaultFont";
    // Coin's built-in glyph keeps one-texel strokes at size 48 in a 256
    // map. For the spot witness, a 64 map makes each authored run cover
    // several observer pixels, as in the qualified C21 text caster fixture.
    if(spot) scene.group->precision=.03125f;
    font->size=spot ? 8 : 48;
    raster->addChild(font);
    auto * text=new SoText2;
    text->string.setValue("MW");
    raster->addChild(text);
  } else if(kind==1) {
    auto * image=new SoImage;
    image->horAlignment=SoImage::CENTER;
    image->vertAlignment=SoImage::HALF;
    std::vector<uint8_t> pixels;
    pixels.resize(size_t(17*17*3));
    for(int p=0;p<17*17;++p) {
      pixels[size_t(3*p)]=220;
      pixels[size_t(3*p+1)]=45;
      pixels[size_t(3*p+2)]=30;
    }
    image->image.setValue(SbVec2s(17,17),3,pixels.data());
    raster->addChild(image);
  } else {
    auto * coordinates=new SoCoordinate3;
    coordinates->point.setValue(0,0,0);
    raster->addChild(coordinates);
    if(kind==2) {
      auto * marker=new SoMarkerSet;
      marker->markerIndex=SoMarkerSet::SQUARE_FILLED_9_9;
      raster->addChild(marker);
    } else {
      auto * marker=new SoIndexedMarkerSet;
      marker->coordIndex=0;
      marker->markerIndex=SoMarkerSet::SQUARE_FILLED_9_9;
      raster->addChild(marker);
    }
  }
  const std::string label="raster/kind="+std::to_string(kind)+
    "/spot="+std::to_string(spot)+
    "/positive="+std::to_string(positivePhase);
  scene.alpha->value=0;
  Frame always,never,greater,less;
  scene.alpha->function=SoAlphaTest::ALWAYS;
  if(!require(capture.render(scene.root,always),label+" always")) return false;
  scene.alpha->function=SoAlphaTest::NEVER;
  if(!require(capture.render(scene.root,never),label+" never")) return false;
  scene.alpha->function=SoAlphaTest::GREATER;
  if(!require(capture.render(scene.root,greater),label+" greater")) return false;
  scene.alpha->function=SoAlphaTest::LESS;
  if(!require(capture.render(scene.root,less),label+" less")) return false;
  const int changed=rasterDifference(always,never);
  std::cout << "shadow_alpha_raster " << label << " anchor_raw=" << anchorRaw
            << " always_never_changed=" << changed << " serials="
            << always.serial << ',' << never.serial << ',' << greater.serial
            << ',' << less.serial << '\n';
  bool ok=require(changed>0 && always.serial<never.serial &&
    never.serial<greater.serial && greater.serial<less.serial,
    label+" nonempty authored bitmap caster and publication order");
  if(!spot) {
    const Frame & greaterExpected=positivePhase?always:never;
    const Frame & lessExpected=positivePhase?never:always;
    // The raster has one depth/anchor, so the VSM alpha is constant over
    // its bits/runs. ±.5 is far from the zero reference; this comparison
    // tests signed VSM alpha without treating the native B21 mask as gold.
    ok &= require(std::abs(anchorRaw-(positivePhase?.5f:-.5f))<.02f &&
      greater.rgba==greaterExpected.rgba &&
      greater.depth==greaterExpected.depth &&
      less.rgba==lessExpected.rgba && less.depth==lessExpected.depth,
      label+" independent signed anchor phase over authored raster mask");
  }
  Frame stable,failed,recovered;
  scene.alpha->function=SoAlphaTest::GREATER;
  if(!require(capture.render(scene.root,stable),
      label+" GREATER stable frame before invalid reference")) return false;
  scene.alpha->value=std::numeric_limits<float>::quiet_NaN();
  capture.action.apply(scene.root);
  const auto status=capture.action.getLastStatus();
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial=capture.target->getLastSubmissionSerial();
  scene.alpha->value=0;
  const bool repaired=capture.render(scene.root,recovered);
  ok &= require(status!=CoinRenderAction::SUCCESS &&
    failed.rgba==stable.rgba && failed.depth==stable.depth &&
    failed.serial==stable.serial && repaired &&
    recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
    recovered.serial>stable.serial,label+" raster NaN rollback and recovery");
  return ok;
}
bool rasterMultiMaps(Capture & capture,int count) {
  MultiScene scene(count);
  scene.base.group->precision=.03125f;
  const float radius=std::sqrt(24.f);
  const float nearValue=(radius-4.f)*1.01f;
  const float farValue=(radius+4.f)*1.01f*1.1f;
  for(int slot=0;slot<count;++slot) {
    auto * map=scene.mapScenes[size_t(slot)];
    map->removeChild(4);
    map->removeChild(3);
    const float phase=(slot%2)==0?.25f:.75f;
    const float l=std::sqrt((14.f+phase)/64.f);
    const float z=radius-nearValue-l*(farValue-nearValue);
    auto * branch=new SoSeparator;
    map->addChild(branch);
    auto * move=new SoTranslation;
    move->translation.setValue(scene.offsets[size_t(slot)],0,z);
    branch->addChild(move);
    const int kind=slot%4;
    if(kind==0) {
      auto * font=new SoFont;
      font->name="defaultFont";font->size=8;
      branch->addChild(font);
      auto * text=new SoText2;
      text->string.setValue("MW");
      branch->addChild(text);
    } else if(kind==1) {
      auto * image=new SoImage;
      image->horAlignment=SoImage::CENTER;
      image->vertAlignment=SoImage::HALF;
      std::vector<uint8_t> pixels(size_t(17*17*3),255);
      image->image.setValue(SbVec2s(17,17),3,pixels.data());
      branch->addChild(image);
    } else {
      auto * coords=new SoCoordinate3;
      coords->point.setValue(0,0,0);
      branch->addChild(coords);
      if(kind==2) {
        auto * marker=new SoMarkerSet;
        marker->markerIndex=SoMarkerSet::SQUARE_LINE_9_9;
        branch->addChild(marker);
      } else {
        auto * marker=new SoIndexedMarkerSet;
        marker->coordIndex=0;
        marker->markerIndex=SoMarkerSet::SQUARE_LINE_9_9;
        branch->addChild(marker);
      }
    }
  }
  auto apply=[&](const std::vector<int> & functions,
                 const std::string & label,Frame & frame) {
    for(int slot=0;slot<count;++slot) {
      scene.tests[size_t(slot)]->function=functions[size_t(slot)];
      scene.tests[size_t(slot)]->value=0;
    }
    return require(capture.render(scene.base.root,frame),label);
  };
  std::vector<int> functions(size_t(count),SoAlphaTest::ALWAYS);
  Frame allAlways,allNever;
  if(!apply(functions,"raster-maps all ALWAYS",allAlways)) return false;
  functions.assign(size_t(count),SoAlphaTest::NEVER);
  if(!apply(functions,"raster-maps all NEVER",allNever)) return false;
  bool ok=require(rasterDifference(allAlways,allNever)>25,
    "raster multi-map authored casters change receiver");
  for(int slot=0;slot<count;++slot) {
    const std::string label="raster-maps/count="+std::to_string(count)+
      "/slot="+std::to_string(slot);
    functions.assign(size_t(count),SoAlphaTest::ALWAYS);
    functions[size_t(slot)]=SoAlphaTest::NEVER;
    Frame oneNever;
    const bool oneNeverOk=apply(functions,label+" one NEVER",oneNever);
    functions.assign(size_t(count),SoAlphaTest::NEVER);
    functions[size_t(slot)]=SoAlphaTest::ALWAYS;
    Frame isolated,greater,less;
    const bool isolatedOk=apply(functions,label+" isolated ALWAYS",isolated);
    functions[size_t(slot)]=SoAlphaTest::GREATER;
    const bool greaterOk=apply(functions,label+" isolated GREATER",greater);
    functions[size_t(slot)]=SoAlphaTest::LESS;
    const bool lessOk=apply(functions,label+" isolated LESS",less);
    if(!require(oneNeverOk && isolatedOk && greaterOk && lessOk,
        label+" complete frames")) {ok=false;continue;}
    const int removed=rasterDifference(allAlways,oneNever);
    const int isolatedPixels=rasterDifference(isolated,allNever);
    const bool positivePhase=(slot%2)!=0;
    const Frame & greaterExpected=positivePhase?isolated:allNever;
    const Frame & lessExpected=positivePhase?allNever:isolated;
    const int kind=slot%4;
    int holePixels=0;
    if(kind>=2) {
      const int centerX=int(std::lround(frameSide/2.f+
        scene.offsets[size_t(slot)]*frameSide/4.f));
      for(int y=frameSide/2-2;y<=frameSide/2+2;++y)
        for(int x=centerX-2;x<=centerX+2;++x) {
          const size_t p=size_t(y*frameSide+x);
          bool differs=false;
          for(int channel=0;channel<3;++channel)
            differs|=std::abs(int(isolated.rgba[p*4+channel])-
              int(allNever.rgba[p*4+channel]))>3;
          holePixels+=differs;
        }
    }
    std::cout << "shadow_alpha_raster_maps " << label
              << " removed=" << removed << " isolated=" << isolatedPixels
              << " hole_pixels=" << holePixels << " phase="
              << (positivePhase?"positive":"negative") << '\n';
    ok &= require(removed>0 && isolatedPixels>0 &&
      (kind<2 || holePixels==0) &&
      greater.rgba==greaterExpected.rgba &&
      greater.depth==greaterExpected.depth &&
      less.rgba==lessExpected.rgba && less.depth==lessExpected.depth &&
      allAlways.serial<allNever.serial &&
      oneNever.serial<isolated.serial && isolated.serial<greater.serial &&
      greater.serial<less.serial,
      label+" per-slot signed raster caster, bitmap hole and publication");
  }
  functions.assign(size_t(count),SoAlphaTest::ALWAYS);
  functions.back()=SoAlphaTest::GREATER;
  Frame stable,failed,recovered;
  if(!apply(functions,"raster-maps stable GREATER",stable)) return false;
  scene.tests.back()->value=std::numeric_limits<float>::quiet_NaN();
  capture.action.apply(scene.base.root);
  const auto status=capture.action.getLastStatus();
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial=capture.target->getLastSubmissionSerial();
  const bool repaired=apply(functions,"raster-maps recovered GREATER",recovered);
  ok &= require(status!=CoinRenderAction::SUCCESS &&
    failed.rgba==stable.rgba && failed.depth==stable.depth &&
    failed.serial==stable.serial && repaired &&
    recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
    recovered.serial>stable.serial,
    "raster 4/8 maps NaN rollback and recovery");
  return ok;
}
bool receiverAlpha(Capture & capture,bool blend) {
  capture.action.setTransparencyType(blend ? CoinRenderAction::BLEND :
    CoinRenderAction::SCREEN_DOOR);
  Scene scene;
  auto * receiver=static_cast<SoSeparator *>(scene.group->getChild(1));
  auto * material=static_cast<SoMaterial *>(receiver->getChild(1));
  material->transparency=.25f;
  auto * alpha=new SoAlphaTest;
  receiver->insertChild(alpha,1);
  scene.alpha->function=SoAlphaTest::GREATER;
  scene.alpha->value=.25f;
  const auto render=[&](int function,float reference,Frame & frame) {
    alpha->function=function;
    alpha->value=reference;
    return capture.render(scene.root,frame);
  };
  const float equalReference=blend ? 191.f/255.f : 1.f;
  const float unequalReference=blend ? .75f : 191.f/255.f;
  Frame pass,fail,greaterPass,greaterHigh,lessHigh,lessFail,equalPass,equalFail;
  if(!require(render(SoAlphaTest::NONE,.5f,pass) &&
      render(SoAlphaTest::NEVER,.5f,fail) &&
      render(SoAlphaTest::GREATER,.5f,greaterPass) &&
      render(SoAlphaTest::GREATER,.9f,greaterHigh) &&
      render(SoAlphaTest::LESS,.9f,lessHigh) &&
      render(SoAlphaTest::LESS,.5f,lessFail) &&
      render(SoAlphaTest::EQUAL,equalReference,equalPass) &&
      render(SoAlphaTest::EQUAL,unequalReference,equalFail),
      "receiver final-alpha captures")) return false;
  const int witness=rasterDifference(pass,fail);
  auto describe=[&](const char * name,const Frame & actual,
                    const Frame & expected) {
    int rgb=0,alphaError=0,different=0,nonBackground=0;
    double depthError=0;
    if(actual.rgba.size()!=expected.rgba.size() ||
       actual.depth.size()!=expected.depth.size() || actual.rgba.empty()) {
      std::cout << "shadow_alpha_receiver " << name << " incomplete frame\n";
      return;
    }
    for(size_t p=0;p<actual.rgba.size()/4;++p) {
      bool changed=false,visible=false;
      for(int c=0;c<3;++c) {
        const int delta=std::abs(int(actual.rgba[p*4+c])-
          int(expected.rgba[p*4+c]));
        rgb=std::max(rgb,delta);
        changed|=delta!=0;
        visible|=actual.rgba[p*4+c]!=actual.rgba[c];
      }
      different+=changed;
      nonBackground+=visible;
      alphaError=std::max(alphaError,
        std::abs(int(actual.rgba[p*4+3])-int(expected.rgba[p*4+3])));
      depthError=std::max(depthError,
        std::abs(double(actual.depth[p])-double(expected.depth[p])));
    }
    const size_t center=size_t((frameSide/2)*frameSide+frameSide/2);
    std::cout << "shadow_alpha_receiver " << name << " RGBmax=" << rgb
              << " alpha_max=" << alphaError << " depth_max=" << depthError
              << " pixels_different=" << different
              << " non_background=" << nonBackground
              << " center_RGBA=" << int(actual.rgba[center*4]) << ','
              << int(actual.rgba[center*4+1]) << ','
              << int(actual.rgba[center*4+2]) << ','
              << int(actual.rgba[center*4+3])
              << " center_depth=" << actual.depth[center] << '\n';
  };
  describe("GREATER.5/NONE quantization control",greaterPass,pass);
  describe("GREATER.9",greaterHigh,blend?fail:pass);
  describe("LESS.9",lessHigh,blend?greaterPass:fail);
  describe("LESS.5/fail",lessFail,fail);
  describe("EQUAL/passed",equalPass,greaterPass);
  describe("EQUAL/failed",equalFail,fail);
  std::cout << "shadow_alpha_receiver witness=" << witness
            << " mode=" << (blend?"BLEND":"SCREEN_DOOR")
            << " packed_reference=" << equalReference
            << " pass_alpha=" << int(pass.rgba[4*(frameSide/2*frameSide+frameSide/2)+3])
            << " serial=" << pass.serial << "->" << equalPass.serial << '\n';
  bool ok=require(witness>100 &&
    greaterHigh.rgba==(blend?fail.rgba:greaterPass.rgba) &&
    greaterHigh.depth==(blend?fail.depth:pass.depth) &&
    lessHigh.rgba==(blend?greaterPass.rgba:fail.rgba) &&
    lessHigh.depth==(blend?greaterPass.depth:fail.depth) &&
    equalPass.rgba==greaterPass.rgba &&
    equalPass.depth==greaterPass.depth && equalFail.rgba==fail.rgba &&
    equalFail.depth==fail.depth && lessFail.rgba==fail.rgba &&
    lessFail.depth==fail.depth,
    "receiver compares final packed alpha for BLEND/SCREEN_DOOR, not VSM alpha");
  Frame stable=greaterPass,failed,recovered;
  alpha->function=SoAlphaTest::GREATER;
  alpha->value=.5f;
  if(!require(capture.render(scene.root,stable),
      "receiver stable frame before invalid reference")) return false;
  alpha->value=std::numeric_limits<float>::quiet_NaN();
  capture.action.apply(scene.root);
  const auto status=capture.action.getLastStatus();
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial=capture.target->getLastSubmissionSerial();
  alpha->value=.5f;
  const bool repaired=capture.render(scene.root,recovered);
  ok &= require(status!=CoinRenderAction::SUCCESS &&
    failed.rgba==stable.rgba && failed.depth==stable.depth &&
    failed.serial==stable.serial && repaired &&
    recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
    recovered.serial>stable.serial,"receiver invalid alpha rollback/recovery");
  return ok;
}
template <typename Renderer>
bool noMapWith(Renderer & capture,const std::string & route) {
  Scene scene;
  scene.light->on=FALSE;
  auto * receiver=static_cast<SoSeparator *>(scene.group->getChild(1));
  auto * material=static_cast<SoMaterial *>(receiver->getChild(1));
  material->transparency=.25f;
  auto * alpha=new SoAlphaTest;
  receiver->insertChild(alpha,1);
  scene.alpha->function=SoAlphaTest::GREATER;
  scene.alpha->value=.25f;
  bool ok=true;
  for(int function:{SoAlphaTest::GREATER,SoAlphaTest::NEVER}) {
    alpha->function=function;
    alpha->value=.5f;
    Frame active,inactive;
    scene.group->isActive=TRUE;
    const bool activeOk=capture.render(scene.root,active);
    scene.group->isActive=FALSE;
    const bool inactiveOk=capture.render(scene.root,inactive);
    const std::string label="no-maps/"+route+"/function="+
      std::to_string(function);
    ok &= require(activeOk && inactiveOk &&
      active.rgba==inactive.rgba && active.depth==inactive.depth &&
      active.serial<inactive.serial,
      label+" active empty group equals ordinary traversal");
  }
  scene.group->isActive=TRUE;
  alpha->function=SoAlphaTest::GREATER;
  alpha->value=.5f;
  Frame stable,failed,recovered;
  if(!require(capture.render(scene.root,stable),route+" no-map stable")) return false;
  alpha->value=std::numeric_limits<float>::quiet_NaN();
  capture.action.apply(scene.root);
  const auto status=capture.action.getLastStatus();
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial=capture.target->getLastSubmissionSerial();
  alpha->value=.5f;
  const bool repaired=capture.render(scene.root,recovered);
  ok &= require(status!=CoinRenderAction::SUCCESS &&
    failed.rgba==stable.rgba && failed.depth==stable.depth &&
    failed.serial==stable.serial && repaired &&
    recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
    recovered.serial>stable.serial,route+" no-map rollback/recovery");
  return ok;
}
void addMapQuad(SoSeparator * parent,float left,float right) {
  auto * coordinates=new SoCoordinate3;
  const SbVec3f points[]={SbVec3f(left,-.5f,.8f),
    SbVec3f(right,-.5f,.8f),SbVec3f(right,.5f,.8f),
    SbVec3f(left,.5f,.8f)};
  coordinates->point.setValues(0,4,points);
  parent->addChild(coordinates);
  auto * face=new SoFaceSet;
  face->numVertices.set1Value(0,4);
  parent->addChild(face);
}
SoSeparator * mapPrelude() {
  auto * map=new SoSeparator;
  auto * style=new SoShadowStyle;
  style->style=SoShadowStyle::CASTS_SHADOW;
  map->addChild(style);
  auto * material=new SoMaterial;
  material->diffuseColor.setValue(1,1,1);
  material->transparency=0;
  map->addChild(material);
  return map;
}
bool separatorOverride(Capture & capture) {
  Scene scene;
  auto * composed=mapPrelude();
  composed->ref();
  auto * outer=new SoAlphaTest;
  composed->addChild(outer);
  auto * nested=new SoSeparator;
  auto * inner=new SoAlphaTest;
  nested->addChild(inner);
  addMapQuad(nested,-.9f,-.1f);
  composed->addChild(nested);
  addMapQuad(composed,.1f,.9f);
  auto * left=mapPrelude();
  left->ref();
  auto * leftAlways=new SoAlphaTest;
  leftAlways->function=SoAlphaTest::ALWAYS;
  left->addChild(leftAlways);addMapQuad(left,-.9f,-.1f);
  auto * right=mapPrelude();
  right->ref();
  auto * rightAlways=new SoAlphaTest;
  rightAlways->function=SoAlphaTest::ALWAYS;
  right->addChild(rightAlways);addMapQuad(right,.1f,.9f);
  auto * absent=mapPrelude();
  absent->ref();
  auto * never=new SoAlphaTest;
  never->function=SoAlphaTest::NEVER;
  absent->addChild(never);
  addMapQuad(absent,-.9f,.9f);
  auto captureMap=[&](SoSeparator * map,Frame & frame) {
    scene.light->shadowMapScene=map;
    return capture.render(scene.root,frame);
  };
  Frame leftReference,rightReference,empty,leftScoped,rightScoped,
    leftOverridden,recovered;
  if(!require(captureMap(absent,empty)&&
      captureMap(left,leftReference)&&captureMap(right,rightReference),
      "separator independent left/right/empty references")) return false;
  outer->function=SoAlphaTest::NEVER;
  inner->function=SoAlphaTest::ALWAYS;
  bool ok=require(captureMap(composed,leftScoped),
    "separator inner ALWAYS under outer NEVER");
  outer->function=SoAlphaTest::ALWAYS;
  inner->function=SoAlphaTest::NEVER;
  ok &= require(captureMap(composed,rightScoped),
    "separator outer ALWAYS restored after inner NEVER");
  outer->function=SoAlphaTest::NEVER;
  inner->function=SoAlphaTest::ALWAYS;
  outer->setOverride(TRUE);
  ok &= require(captureMap(composed,leftOverridden),
    "AlphaTest inherited setOverride traversal");
  std::cout << "shadow_alpha_separator left="
            << rasterDifference(leftReference,empty) << " right="
            << rasterDifference(rightReference,empty) << '\n';
  ok &= require(rasterDifference(leftReference,empty)>10 &&
    rasterDifference(rightReference,empty)>10 &&
    leftScoped.rgba==leftReference.rgba &&
    leftScoped.depth==leftReference.depth &&
    rightScoped.rgba==rightReference.rgba &&
    rightScoped.depth==rightReference.depth &&
    leftOverridden.rgba==leftReference.rgba &&
    leftOverridden.depth==leftReference.depth &&
    leftScoped.serial<rightScoped.serial &&
    rightScoped.serial<leftOverridden.serial,
    "Separator restores AlphaTest per caster; CoinGL ignores override flag");
  inner->value=std::numeric_limits<float>::quiet_NaN();
  capture.action.apply(scene.root);
  Frame failed;
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial=capture.target->getLastSubmissionSerial();
  const auto status=capture.action.getLastStatus();
  inner->value=.5f;
  ok &= require(captureMap(composed,recovered)&&
    status!=CoinRenderAction::SUCCESS &&
    failed.rgba==leftOverridden.rgba &&
    failed.depth==leftOverridden.depth &&
    failed.serial==leftOverridden.serial &&
    recovered.rgba==leftOverridden.rgba &&
    recovered.depth==leftOverridden.depth &&
    recovered.serial>leftOverridden.serial,
    "Separator invalid AlphaTest rollback and recovery");
  composed->unref();left->unref();right->unref();absent->unref();
  return ok;
}
bool mixedMapScene(Capture & capture) {
  Scene scene;
  scene.light->intensity=.35f;
  auto * spot=new SoShadowSpotLight;
  spot->location.setValue(0,0,5);
  spot->direction.setValue(0,0,-1);
  spot->cutOffAngle=.6f;
  spot->nearDistance=1;spot->farDistance=10;
  spot->intensity=.35f;
  auto * map=mapPrelude();
  auto * camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,5);
  camera->height=4;camera->nearDistance=1;camera->farDistance=10;
  map->addChild(camera);
  auto * test=new SoAlphaTest;
  test->function=SoAlphaTest::ALWAYS;
  map->addChild(test);
  addMapQuad(map,.1f,.9f);
  spot->shadowMapScene=map;
  scene.group->insertChild(spot,1);
  auto render=[&](int directional,int spotFunction,Frame & frame) {
    scene.alpha->function=directional;
    test->function=spotFunction;
    return capture.render(scene.root,frame);
  };
  Frame none,directionalOnly,spotOnly,both,failed,recovered;
  if(!require(render(SoAlphaTest::NEVER,SoAlphaTest::NEVER,none)&&
      render(SoAlphaTest::ALWAYS,SoAlphaTest::NEVER,directionalOnly)&&
      render(SoAlphaTest::NEVER,SoAlphaTest::ALWAYS,spotOnly)&&
      render(SoAlphaTest::ALWAYS,SoAlphaTest::ALWAYS,both),
      "mixed directional/spot authored-camera captures")) return false;
  std::cout << "shadow_alpha_mixed directional="
            << rasterDifference(directionalOnly,none) << " spot="
            << rasterDifference(spotOnly,none) << " both="
            << rasterDifference(both,none) << '\n';
  bool ok=require(rasterDifference(directionalOnly,none)>10 &&
    rasterDifference(spotOnly,none)>10 &&
    rasterDifference(both,directionalOnly)>10 &&
    rasterDifference(both,spotOnly)>10 &&
    directionalOnly.serial<spotOnly.serial && spotOnly.serial<both.serial,
    "two mapScene cameras/lights each contribute with own AlphaTest");
  test->value=std::numeric_limits<float>::quiet_NaN();
  capture.action.apply(scene.root);
  const auto status=capture.action.getLastStatus();
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial=capture.target->getLastSubmissionSerial();
  test->value=.5f;
  const bool repaired=render(SoAlphaTest::ALWAYS,SoAlphaTest::ALWAYS,recovered);
  ok &= require(status!=CoinRenderAction::SUCCESS &&
    failed.rgba==both.rgba && failed.depth==both.depth &&
    failed.serial==both.serial && repaired &&
    recovered.rgba==both.rgba && recovered.depth==both.depth &&
    recovered.serial>both.serial,
    "mixed authored-camera invalid slot preserves frame and recovers");
  return ok;
}
bool rttCase(bool canonical,bool direct) {
  std::string diagnostic;
  auto options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if(!require(diagnostic.empty(),diagnostic)) return false;
  if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  options.sceneTexture=direct ? COIN_RENDER_SCENE_TEXTURE_DIRECT :
    COIN_RENDER_SCENE_TEXTURE_STAGED;
  auto makeTarget=[&]() {
    return std::unique_ptr<CoinRenderTarget>(CoinRenderTarget::createOffscreen(
      SbVec2i32(frameSide,frameSide),options));
  };
  auto peer=makeTarget();
  auto reference=makeTarget();
  auto consumerTarget=makeTarget();
  if(!require(bool(peer) && bool(reference) && bool(consumerTarget),
      "RTT staged/direct targets")) return false;
  reference->setDepthReadbackEnabled(TRUE);
  consumerTarget->setDepthReadbackEnabled(TRUE);
  Scene peerScene;
  peerScene.light->on=FALSE;
  CoinRenderAction peerAction(SbViewportRegion(frameSide,frameSide));
  peerAction.setRenderTarget(peer.get());
  peerAction.apply(peerScene.root);
  std::vector<uint8_t> peerColor;
  peer->readbackRGBA(peerColor);
  const uint64_t peerSerial=peer->getLastSubmissionSerial();
  if(!require(peerAction.getLastStatus()==CoinRenderAction::SUCCESS &&
      peerColor.size()==size_t(frameSide*frameSide*4),
      "RTT peer initializes shared runtime")) return false;
  auto peerIntact=[&]() {
    std::vector<uint8_t> now;
    peer->readbackRGBA(now);
    return now==peerColor && peer->getLastSubmissionSerial()==peerSerial;
  };
  Scene producer;
  producer.alpha->function=SoAlphaTest::GREATER;
  producer.alpha->value=0;
  auto * consumer=new SoSeparator;
  consumer->ref();
  auto * camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,3);
  camera->height=2;camera->nearDistance=.1f;camera->farDistance=10;
  consumer->addChild(camera);
  auto * model=new SoLightModel;
  model->model=SoLightModel::BASE_COLOR;
  consumer->addChild(model);
  auto * quality=new SoComplexity;
  quality->textureQuality=.3f;
  consumer->addChild(quality);
  auto * white=new SoMaterial;
  white->diffuseColor.setValue(1,1,1);
  consumer->addChild(white);
  auto * texture=new SoSceneTexture2;
  texture->scene=producer.root;
  texture->size.setValue(frameSide,frameSide);
  texture->type=SoSceneTexture2::RGBA8;
  texture->model=SoSceneTexture2::REPLACE;
  texture->transparencyFunction=SoSceneTexture2::NONE;
  texture->backgroundColor.setValue(0,0,0,1);
  consumer->addChild(texture);
  auto * uv=new SoTextureCoordinate2;
  const SbVec2f texcoords[]={SbVec2f(0,0),SbVec2f(1,0),
    SbVec2f(1,1),SbVec2f(0,1)};
  uv->point.setValues(0,4,texcoords);
  consumer->addChild(uv);
  auto * coords=new SoCoordinate3;
  const SbVec3f corners[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
    SbVec3f(1,1,0),SbVec3f(-1,1,0)};
  coords->point.setValues(0,4,corners);
  consumer->addChild(coords);
  auto * quad=new SoFaceSet;
  quad->numVertices.set1Value(0,4);
  consumer->addChild(quad);
  CoinRenderAction producerAction(SbViewportRegion(frameSide,frameSide));
  CoinRenderAction consumerAction(SbViewportRegion(frameSide,frameSide));
  producerAction.setRenderTarget(reference.get());
  consumerAction.setRenderTarget(consumerTarget.get());
  auto capture=[&](Frame & source,Frame & consumed,const std::string & label) {
    producerAction.apply(producer.root);
    consumerAction.apply(consumer);
    if(producerAction.getLastStatus()!=CoinRenderAction::SUCCESS ||
       consumerAction.getLastStatus()!=CoinRenderAction::SUCCESS) {
      std::cerr << "shadow_alpha RTT " << label << " source_status="
                << producerAction.getLastStatus() << " consumer_status="
                << consumerAction.getLastStatus() << " diagnostic="
                << consumerAction.getLastError().getString() << '\n';
      return false;
    }
    reference->readbackRGBA(source.rgba);
    reference->readbackDepth(source.depth);
    source.serial=reference->getLastSubmissionSerial();
    consumerTarget->readbackRGBA(consumed.rgba);
    consumerTarget->readbackDepth(consumed.depth);
    consumed.serial=consumerTarget->getLastSubmissionSerial();
    return source.rgba.size()==size_t(frameSide*frameSide*4) &&
      consumed.rgba.size()==source.rgba.size() &&
      source.depth.size()==size_t(frameSide*frameSide) &&
      consumed.depth.size()==source.depth.size();
  };
  Frame sourcePass,consumerPass,sourceFail,consumerFail;
  bool ok=require(capture(sourcePass,consumerPass,"GREATER"),
    "RTT AlphaTest producer/consumer pass");
  producer.alpha->function=SoAlphaTest::NEVER;
  ok &= require(capture(sourceFail,consumerFail,"NEVER"),
    "RTT AlphaTest producer/consumer fail");
  auto equivalent=[&](const Frame & source,const Frame & consumed,
                      const std::string & label) {
    if(source.rgba.size()!=consumed.rgba.size() || source.rgba.empty())
      return require(false,label+" complete frames");
    int maxRgb=0,maxAlpha=0;
    double maxDepth=0;
    for(size_t p=0;p<source.rgba.size()/4;++p) {
      for(int c=0;c<3;++c)
        maxRgb=std::max(maxRgb,std::abs(int(source.rgba[p*4+c])-
          int(consumed.rgba[p*4+c])));
      maxAlpha=std::max(maxAlpha,std::abs(int(source.rgba[p*4+3])-
        int(consumed.rgba[p*4+3])));
      const int x=int(p%frameSide),y=int(p/frameSide);
      if(x>=8 && x<frameSide-8 && y>=8 && y<frameSide-8)
        maxDepth=std::max(maxDepth,std::abs(double(consumed.depth[p])-
          (3.-.1)/9.9));
    }
    std::cout << "shadow_alpha_rtt " << label << " direct=" << direct
              << " RGB_max=" << maxRgb << " alpha_max=" << maxAlpha
              << " depth_max=" << maxDepth << '\n';
    return require(maxRgb<=3 && maxAlpha==0 && maxDepth<=1e-5,
      label+" RTT consumes AlphaTest map frame and preserves RGBA/depth");
  };
  if(ok) ok &= equivalent(sourcePass,consumerPass,"GREATER") &&
    equivalent(sourceFail,consumerFail,"NEVER") &&
    rasterDifference(consumerPass,consumerFail)>25 &&
    sourcePass.serial<sourceFail.serial &&
    consumerPass.serial<consumerFail.serial && peerIntact();
  producer.alpha->function=SoAlphaTest::GREATER;
  producer.alpha->value=0;
  Frame sourceStable,consumerStable,sourceRecovered,consumerRecovered,failed;
  ok &= require(capture(sourceStable,consumerStable,"stable"),
    "RTT stable frame before invalid producer");
  producer.alpha->value=std::numeric_limits<float>::quiet_NaN();
  consumerAction.apply(consumer);
  const auto invalidStatus=consumerAction.getLastStatus();
  consumerTarget->readbackRGBA(failed.rgba);
  consumerTarget->readbackDepth(failed.depth);
  failed.serial=consumerTarget->getLastSubmissionSerial();
  producer.alpha->value=0;
  const bool recovered=capture(sourceRecovered,consumerRecovered,"recovered");
  ok &= require(invalidStatus!=CoinRenderAction::SUCCESS &&
    failed.rgba==consumerStable.rgba &&
    failed.depth==consumerStable.depth &&
    failed.serial==consumerStable.serial && recovered &&
    consumerRecovered.rgba==consumerStable.rgba &&
    consumerRecovered.depth==consumerStable.depth &&
    consumerRecovered.serial>consumerStable.serial && peerIntact(),
    "RTT AlphaTest invalid producer rollback, peer and recovery");
  producerAction.setRenderTarget(nullptr);
  consumerAction.setRenderTarget(nullptr);
  peerAction.setRenderTarget(nullptr);
  consumer->unref();
  return ok;
}
#ifdef COIN_ALPHA_X11
uint8_t physicalChannel(unsigned long pixel,unsigned long mask) {
  if(!mask) return 0;
  while(!(mask&1UL)) {pixel>>=1;mask>>=1;}
  return uint8_t(((pixel&mask)*255UL+mask/2UL)/mask);
}
std::vector<uint8_t> physicalRGB(Display * display,Window window,int side) {
  XImage * image=XGetImage(display,window,0,0,side,side,AllPlanes,ZPixmap);
  if(!image) return {};
  std::vector<uint8_t> rgb(size_t(side)*side*3);
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    const unsigned long pixel=XGetPixel(image,x,y);
    const size_t p=size_t(y*side+x)*3;
    rgb[p]=physicalChannel(pixel,image->red_mask);
    rgb[p+1]=physicalChannel(pixel,image->green_mask);
    rgb[p+2]=physicalChannel(pixel,image->blue_mask);
  }
  XDestroyImage(image);
  return rgb;
}
bool windowCase(bool canonical) {
  std::string diagnostic;
  auto options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if(!require(diagnostic.empty(),diagnostic)) return false;
  if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
      options.renderer,&caps,sizeof(caps))!=0 || !caps.gpu_available) return false;
  Display * display=XOpenDisplay(nullptr);
  if(!require(display!=nullptr,"AlphaTest X11 display")) return false;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),
    20,20,64,64,0,0,0);
  XSetWindowAttributes attributes{};attributes.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor surface{};
  surface.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  surface.structSize=sizeof(surface);surface.type=COIN_RENDER_SURFACE_XLIB;
  surface.native.xlib.display=display;surface.native.xlib.window=window;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createWindow(
    surface,SbVec2i32(64,64),options));
  std::unique_ptr<CoinRenderTarget> reference(CoinRenderTarget::createOffscreen(
    SbVec2i32(64,64),options));
  if(!require(bool(target)&&bool(reference),"AlphaTest window/reference targets")) {
    target.reset();reference.reset();XDestroyWindow(display,window);
    XCloseDisplay(display);return false;
  }
  reference->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(64,64));
  CoinRenderAction refAction(SbViewportRegion(64,64));
  action.setRenderTarget(target.get());refAction.setRenderTarget(reference.get());
  int current=64;
  uint64_t lastSerial=target->getLastSubmissionSerial();
  auto capture=[&](SoNode * root,int size,Frame & off,
                   std::vector<uint8_t> & visible,const std::string & label) {
    if(size!=current) {
      XResizeWindow(display,window,size,size);XSync(display,False);
      XWindowAttributes observed{};
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      do {
        XGetWindowAttributes(display,window,&observed);
        if(observed.width==size&&observed.height==size) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      } while(std::chrono::steady_clock::now()<deadline);
      if(observed.width!=size||observed.height!=size||
         !target->resize(SbVec2i32(size,size))||
         !reference->resize(SbVec2i32(size,size)))
        return require(false,label+" resize");
      current=size;
    }
    action.setViewportRegion(SbViewportRegion(size,size));
    refAction.setViewportRegion(SbViewportRegion(size,size));
    refAction.apply(root);action.apply(root);
    if(refAction.getLastStatus()!=CoinRenderAction::SUCCESS||
       action.getLastStatus()!=CoinRenderAction::SUCCESS) {
      std::cerr << label << " offscreen=" << refAction.getLastStatus()
                << " window=" << action.getLastStatus() << " diagnostic="
                << action.getLastError().getString() << '\n';return false;
    }
    reference->readbackRGBA(off.rgba);reference->readbackDepth(off.depth);
    off.serial=reference->getLastSubmissionSerial();
    const uint64_t serial=target->getLastSubmissionSerial();
    if(off.rgba.size()!=size_t(size*size*4)||
       off.depth.size()!=size_t(size*size)||serial<=lastSerial)
      return require(false,label+" complete fresh frame");
    lastSerial=serial;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    int maxRgb=255;
    do {
      visible=physicalRGB(display,window,size);
      if(visible.size()==size_t(size*size*3)) {
        maxRgb=0;
        for(size_t p=0;p<size_t(size*size);++p)
          for(int c=0;c<3;++c)
            maxRgb=std::max(maxRgb,std::abs(int(visible[p*3+c])-
              int(off.rgba[p*4+c])));
        if(maxRgb<=3) break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while(std::chrono::steady_clock::now()<deadline);
    std::cout << "shadow_alpha_window " << label << " size=" << size
              << " serial=" << serial << " RGBmax=" << maxRgb << '\n';
    return require(maxRgb<=3,label+" physical X11 matches offscreen");
  };
  bool ok=true;
  int index=0;
  for(int maps:{1,4,8,1}) {
    MultiScene scene(maps);
    auto setFunctions=[&](int function) {
      for(int i=0;i<maps;++i) {
        scene.tests[size_t(i)]->function=function;
        scene.tests[size_t(i)]->value=0;
      }
    };
    const int sizes[]={64,96,65,64};
    const int size=sizes[index++%4];
    Frame pass,fail,recovered;
    std::vector<uint8_t> passVisible,failVisible,recoveredVisible;
    setFunctions(SoAlphaTest::ALWAYS);
    bool cell=capture(scene.base.root,size,pass,passVisible,
      "maps="+std::to_string(maps)+" ALWAYS");
    setFunctions(SoAlphaTest::NEVER);
    cell &= capture(scene.base.root,size,fail,failVisible,
      "maps="+std::to_string(maps)+" NEVER");
    if(!cell) {ok=false;continue;}
    cell &= require(rasterDifference(pass,fail)>25 &&
      pass.serial<fail.serial && passVisible!=failVisible,
      "window AlphaTest changes nonempty shadow and physical pixels");
    scene.tests[0]->value=std::numeric_limits<float>::quiet_NaN();
    action.apply(scene.base.root);
    const auto rejected=physicalRGB(display,window,size);
    const uint64_t rejectedSerial=target->getLastSubmissionSerial();
    refAction.apply(scene.base.root);
    Frame rejectedOff;
    reference->readbackRGBA(rejectedOff.rgba);
    reference->readbackDepth(rejectedOff.depth);
    rejectedOff.serial=reference->getLastSubmissionSerial();
    cell &= require(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
      refAction.getLastStatus()!=CoinRenderAction::SUCCESS &&
      rejected==failVisible && rejectedSerial==lastSerial &&
      rejectedOff.rgba==fail.rgba && rejectedOff.depth==fail.depth &&
      rejectedOff.serial==fail.serial,
      "window invalid AlphaTest preserves physical/offscreen/depth/serial");
    scene.tests[0]->value=0;
    cell &= capture(scene.base.root,size,recovered,recoveredVisible,
      "maps="+std::to_string(maps)+" recovered");
    cell &= require(recovered.rgba==fail.rgba &&
      recovered.depth==fail.depth && recovered.serial>fail.serial &&
      recoveredVisible==failVisible,"window exact recovery");
    ok &= cell;
  }
  action.setRenderTarget(nullptr);refAction.setRenderTarget(nullptr);
  target.reset();reference.reset();XDestroyWindow(display,window);
  XCloseDisplay(display);
  return ok;
}
#endif
bool rollback(Capture & capture) {
  Scene scene;
  scene.alpha->function=SoAlphaTest::GREATER;
  scene.alpha->value=.25f;
  Frame stable,failed,recovered;
  if(!require(capture.render(scene.root,stable),"rollback stable frame")) return false;
  scene.alpha->value=std::numeric_limits<float>::quiet_NaN();
  capture.action.apply(scene.root);
  const auto status=capture.action.getLastStatus();
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial=capture.target->getLastSubmissionSerial();
  scene.alpha->value=.25f;
  const bool recoveredOk=capture.render(scene.root,recovered);
  return require(status!=CoinRenderAction::SUCCESS &&
    failed.rgba==stable.rgba && failed.depth==stable.depth &&
    failed.serial==stable.serial && recoveredOk &&
    recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
    recovered.serial>stable.serial,"invalid reference rollback/recovery");
}
} // namespace

int main(int argc,char ** argv) {
  bool canonical=false;
  int multiCount=0;
  bool raster=false;
  bool receiver=false,noMaps=false,separator=false,mixed=false;
  int rtt=0;
  bool window=false;
  int rasterMapCount=0;
  for(int i=1;i<argc;++i) {
    if(std::string(argv[i])=="--canonical-raster") canonical=true;
    else if(std::string(argv[i])=="--maps4") multiCount=4;
    else if(std::string(argv[i])=="--maps8") multiCount=8;
    else if(std::string(argv[i])=="--raster") raster=true;
    else if(std::string(argv[i])=="--receiver") receiver=true;
    else if(std::string(argv[i])=="--no-maps") noMaps=true;
    else if(std::string(argv[i])=="--separator") separator=true;
    else if(std::string(argv[i])=="--mixed-map-scene") mixed=true;
    else if(std::string(argv[i])=="--rtt-staged") rtt=1;
    else if(std::string(argv[i])=="--rtt-direct") rtt=2;
    else if(std::string(argv[i])=="--window") window=true;
    else if(std::string(argv[i])=="--raster-maps4") rasterMapCount=4;
    else if(std::string(argv[i])=="--raster-maps8") rasterMapCount=8;
    else return 1;
  }
  SoDB::init();CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      &caps,sizeof(caps))!=0 || !caps.gpu_available) return 77;
  std::cout << "shadow_alpha backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " adapter=" << caps.adapter_name
            << " raster=" << (canonical?"CANONICAL_8BIT":"NATIVE") << '\n';
  if(rtt) return rttCase(canonical,rtt==2)?0:1;
#ifdef COIN_ALPHA_X11
  if(window) return windowCase(canonical)?0:1;
#else
  if(window) return 77;
#endif
  Capture capture(canonical);
  if(!require(bool(capture.target),"offscreen target")) return 1;
  if(multiCount) return multiMaps(capture,multiCount)?0:1;
  if(rasterMapCount) return rasterMultiMaps(capture,rasterMapCount)?0:1;
  if(separator) return separatorOverride(capture)?0:1;
  if(mixed) return mixedMapScene(capture)?0:1;
  if(raster) {
    bool ok=true;
    for(int kind=0;kind<4;++kind)
      for(int spot=0;spot<2;++spot)
        for(int phase=0;phase<2;++phase)
          ok &= rasterCaster(capture,kind,spot!=0,phase!=0);
    return ok?0:1;
  }
  if(receiver) {
    const bool blendOk=receiverAlpha(capture,true);
    const bool screenDoorOk=receiverAlpha(capture,false);
    return blendOk && screenDoorOk?0:1;
  }
  if(noMaps) {
    capture.action.setTransparencyType(CoinRenderAction::BLEND);
    CpuCapture cpu(canonical);
    if(!require(bool(cpu.target),"no-map CPU target")) return 1;
    cpu.action.setTransparencyType(CoinRenderAction::BLEND);
    return noMapWith(capture,"GPU") && noMapWith(cpu,"CPU")?0:1;
  }
  bool ok=true;
  const int functions[]={SoAlphaTest::NEVER,SoAlphaTest::ALWAYS,
    SoAlphaTest::LESS,SoAlphaTest::LEQUAL,SoAlphaTest::EQUAL,
    SoAlphaTest::GEQUAL,SoAlphaTest::GREATER,SoAlphaTest::NOTEQUAL};
  for(int function:functions)
    for(float reference:{0.f,.25f,1.f,-.25f,1.25f})
      ok &= oneFunction(capture,function,reference,canonical);
  ok &= rollback(capture);
  return ok?0:1;
}
