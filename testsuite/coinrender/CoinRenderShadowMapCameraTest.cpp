#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderShadowCore.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoBumpMapCoordinate.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureUnit.h>

#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SbRotation.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoIndexedMarkerSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int side=128;
struct Frame {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial=0;
  std::vector<uint64_t> stereoInputs;
  uint64_t stereoBeforeSerial=0;
  std::vector<double> expectedInputs; // Authored fixture values, never captured IDs.
};
bool check(bool yes,const std::string & what) {
  if(!yes) std::cerr << "shadow_map_camera: " << what << '\n';
  return yes;
}
void plane(SoSeparator * parent) {
  auto * material=new SoMaterial;
  material->diffuseColor.setValue(.72f,.75f,.78f);
  material->ambientColor.setValue(.18f,.18f,.18f);
  parent->addChild(material);
  auto * coordinates=new SoCoordinate3;
  const SbVec3f corners[]={SbVec3f(-1.8f,-1.8f,-1),
    SbVec3f(1.8f,-1.8f,-1),SbVec3f(1.8f,1.8f,-1),
    SbVec3f(-1.8f,1.8f,-1)};
  coordinates->point.setValues(0,4,corners);parent->addChild(coordinates);
  auto * face=new SoFaceSet;face->numVertices.set1Value(0,4);
  parent->addChild(face);
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoShadowGroup * group=new SoShadowGroup;
  SoShadowSpotLight * spot=new SoShadowSpotLight;
  SoShadowDirectionalLight * directional=new SoShadowDirectionalLight;
  SoSeparator * maps[2]={new SoSeparator,new SoSeparator};
  SoPerspectiveCamera * perspective[2]={nullptr,nullptr};
  SoOrthographicCamera * orthographic[2]={nullptr,nullptr};
  SoTranslation * beforeCamera[2]={nullptr,nullptr};
  SoTranslation * afterCamera[2]={nullptr,nullptr};
  Scene() {
    root->ref();
    auto * observer=new SoOrthographicCamera;
    observer->position.setValue(0,0,6);observer->height=4;
    observer->nearDistance=1;observer->farDistance=20;
    root->addChild(observer);
    auto * lighting=new SoLightModel;lighting->model=SoLightModel::PHONG;
    root->addChild(lighting);
    group->quality=.2f;group->precision=.125f;root->addChild(group);
    spot->location.setValue(0,0,5);spot->direction.setValue(0,0,-1);
    spot->cutOffAngle=.6f;spot->intensity=.6f;
    spot->nearDistance=1;spot->farDistance=10;
    directional->direction.setValue(0,0,-1);
    directional->bboxCenter.setValue(0,0,0);
    directional->bboxSize.setValue(4,4,8);
    directional->intensity=.6f;
    group->addChild(spot);group->addChild(directional);
    auto * receiver=new SoSeparator;group->addChild(receiver);
    auto * style=new SoShadowStyle;style->style=SoShadowStyle::SHADOWED;
    receiver->addChild(style);plane(receiver);
    spot->shadowMapScene=maps[0];directional->shadowMapScene=maps[1];
  }
  ~Scene() {root->unref();}
  void configure(int light,int cameraKind,int placement,float cameraX,
                 float casterX,int rasterKind=0) {
    auto * map=maps[light];map->removeAllChildren();
    perspective[light]=nullptr;orthographic[light]=nullptr;
    beforeCamera[light]=nullptr;afterCamera[light]=nullptr;
    if(placement==1) {
      beforeCamera[light]=new SoTranslation;
      beforeCamera[light]->translation.setValue(.28f,0,0);
      map->addChild(beforeCamera[light]);
    }
    if(cameraKind==1) {
      perspective[light]=new SoPerspectiveCamera;
      perspective[light]->position.setValue(cameraX,0,5);
      perspective[light]->nearDistance=1;
      perspective[light]->farDistance=10;
      perspective[light]->heightAngle=1.0f;
      map->addChild(perspective[light]);
    } else if(cameraKind==2) {
      orthographic[light]=new SoOrthographicCamera;
      orthographic[light]->position.setValue(cameraX,0,5);
      orthographic[light]->nearDistance=1;
      orthographic[light]->farDistance=10;
      orthographic[light]->height=4;
      map->addChild(orthographic[light]);
    }
    if(placement==2) {
      afterCamera[light]=new SoTranslation;
      afterCamera[light]->translation.setValue(.28f,0,0);
      map->addChild(afterCamera[light]);
    }
    auto * caster=new SoSeparator;map->addChild(caster);
    auto * style=new SoShadowStyle;style->style=SoShadowStyle::CASTS_SHADOW;
    caster->addChild(style);
    auto * move=new SoTranslation;
    move->translation.setValue(casterX,0,.35f);caster->addChild(move);
    if(rasterKind==0) {
      auto * cube=new SoCube;cube->width=.48f;cube->height=.48f;
      cube->depth=.48f;caster->addChild(cube);
    } else if(rasterKind==1) {
      auto * font=new SoFont;font->name="defaultFont";font->size=8;
      caster->addChild(font);
      auto * text=new SoText2;text->string.setValue("M");
      caster->addChild(text);
    } else if(rasterKind==2) {
      auto * image=new SoImage;
      image->horAlignment=SoImage::CENTER;
      image->vertAlignment=SoImage::HALF;
      std::vector<uint8_t> pixels(size_t(17*17*4),255);
      for(int y=0;y<17;++y) for(int x=0;x<17;++x) {
        const size_t at=size_t(y*17+x)*4;
        pixels[at]=210;pixels[at+1]=50;pixels[at+2]=30;
      }
      image->image.setValue(SbVec2s(17,17),4,pixels.data());
      caster->addChild(image);
    } else {
      auto * coords=new SoCoordinate3;
      coords->point.setValue(0,0,0);caster->addChild(coords);
      if(rasterKind==3) {
        auto * marker=new SoMarkerSet;
        marker->markerIndex=SoMarkerSet::SQUARE_FILLED_9_9;
        caster->addChild(marker);
      } else {
        auto * marker=new SoIndexedMarkerSet;
        marker->coordIndex=0;
        marker->markerIndex=SoMarkerSet::SQUARE_FILLED_9_9;
        caster->addChild(marker);
      }
    }
  }
};
SoSeparator * cubeCaster(float x) {
  auto * branch=new SoSeparator;
  auto * style=new SoShadowStyle;
  style->style=SoShadowStyle::CASTS_SHADOW;
  branch->addChild(style);
  auto * move=new SoTranslation;move->translation.setValue(x,0,.35f);
  branch->addChild(move);
  auto * cube=new SoCube;cube->width=.4f;cube->height=.4f;
  cube->depth=.4f;branch->addChild(cube);
  return branch;
}
bool cameraPortableSampling=false;
struct Capture {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(side,side)};
  SoOffscreenRenderer gl{SbViewportRegion(side,side)};
  explicit Capture(bool canonical, unsigned samples=1) {
    std::string diagnostic;
    auto options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
    if(!diagnostic.empty()) std::cerr << diagnostic << '\n';
    options.sampleCount=samples;
    if(cameraPortableSampling)options.textureSamplingPolicy=COIN_RENDER_SAMPLING_PORTABLE;
    if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    if(target)target->setDepthReadbackEnabled(TRUE);
    action.setRenderTarget(target.get());
    gl.setComponents(SoOffscreenRenderer::RGB);
  }
  ~Capture() {action.setRenderTarget(nullptr);}
  bool gpu(SoNode * scene,Frame & frame) {
    action.apply(scene);
    if(action.getLastStatus()!=CoinRenderAction::SUCCESS) {
      std::cerr << "GPU status=" << action.getLastStatus() << " diagnostic="
                << action.getLastError().getString() << '\n';
      return false;
    }
    target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
    frame.serial=target->getLastSubmissionSerial();
    return frame.rgba.size()==size_t(side*side*4) &&
      frame.depth.size()==size_t(side*side);
  }
  bool native(SoNode * scene,std::vector<uint8_t> & rgb) {
    if(!gl.render(scene) || !gl.getBuffer()) return false;
    const uint8_t * data=gl.getBuffer();
    rgb.resize(size_t(side*side*3));
    // CoinGL offscreen RGB is bottom-up; CoinRender readback is top-down.
    for(int y=0;y<side;++y)
      for(int x=0;x<side;++x)
        for(int c=0;c<3;++c)
          rgb[size_t((y*side+x)*3+c)]=
            data[size_t(((side-1-y)*side+x)*3+c)];
    return true;
  }
};
struct Mask {
  std::vector<uint8_t> bits;
  int count=0;
  double centerX=0;
};
struct RectMask {
  std::vector<uint8_t> bits;
  int count=0;
  double centerX=0,centerY=0;
};
RectMask rectangleShadowMask(const std::vector<uint8_t> & base,
                             const std::vector<uint8_t> & shown,
                             int components,int width,int height) {
  RectMask mask;mask.bits.resize(size_t(width*height));
  for(int y=8;y<height-8;++y) for(int x=8;x<width-8;++x) {
    const size_t p=size_t(y*width+x);
    int loss=0;
    for(int c=0;c<3;++c)
      loss+=int(base[p*components+c])-int(shown[p*components+c]);
    if(loss>18) {
      mask.bits[p]=1;++mask.count;
      mask.centerX+=x;mask.centerY+=y;
    }
  }
  if(mask.count) {
    mask.centerX/=mask.count;mask.centerY/=mask.count;
  }
  return mask;
}
bool compareRectangle(const RectMask & native,const RectMask & gpu,
                      const std::string & label) {
  int different=0;
  for(size_t p=0;p<native.bits.size();++p)
    different+=native.bits[p]!=gpu.bits[p];
  std::cout << "shadow_map_camera_rect " << label << " native/gpu="
            << native.count << '/' << gpu.count << " xy="
            << native.centerX << ',' << native.centerY << '/'
            << gpu.centerX << ',' << gpu.centerY
            << " coverage_mismatch=" << different << '\n';
  return check(native.count>25 && gpu.count>25 && different==0 &&
    std::abs(native.centerX-gpu.centerX)<=.5 &&
    std::abs(native.centerY-gpu.centerY)<=.5,
    label+" rectangular spatial mask and center");
}
struct RectCapture {
  static constexpr int width=160,height=96;
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action{SbViewportRegion(width,height)};
  SoOffscreenRenderer gl{SbViewportRegion(width,height)};
  explicit RectCapture(bool canonical) {
    std::string diagnostic;
    auto options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
    if(!diagnostic.empty()) std::cerr << diagnostic << '\n';
    if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
    target.reset(CoinRenderTarget::createOffscreen(SbVec2i32(width,height),options));
    action.setRenderTarget(target.get());
    gl.setComponents(SoOffscreenRenderer::RGB);
  }
  ~RectCapture() {action.setRenderTarget(nullptr);}
  bool gpu(SoNode * scene,std::vector<uint8_t> & rgba) {
    action.apply(scene);
    if(action.getLastStatus()!=CoinRenderAction::SUCCESS) {
      std::cerr << "rect GPU status=" << action.getLastStatus() << ' '
                << action.getLastError().getString() << '\n';
      return false;
    }
    target->readbackRGBA(rgba);
    return rgba.size()==size_t(width*height*4);
  }
  bool native(SoNode * scene,std::vector<uint8_t> & rgb) {
    if(!gl.render(scene) || !gl.getBuffer()) return false;
    const uint8_t * raw=gl.getBuffer();
    rgb.resize(size_t(width*height*3));
    for(int y=0;y<height;++y) for(int x=0;x<width;++x)
      for(int c=0;c<3;++c)
        rgb[size_t((y*width+x)*3+c)]=
          raw[size_t(((height-1-y)*width+x)*3+c)];
    return true;
  }
};
Mask shadowMask(const std::vector<uint8_t> & base,
                const std::vector<uint8_t> & shadow,int components) {
  Mask result;result.bits.resize(size_t(side*side));
  for(int y=20;y<108;++y) for(int x=20;x<108;++x) {
    const size_t p=size_t(y*side+x);
    int loss=0;
    for(int c=0;c<3;++c)
      loss+=int(base[p*components+c])-int(shadow[p*components+c]);
    if(loss>18) {
      result.bits[p]=1;++result.count;result.centerX+=x;
    }
  }
  if(result.count) result.centerX/=result.count;
  return result;
}
bool compareMask(const Mask & native,const Mask & gpu,
                 const std::string & label) {
  int different=0;
  for(size_t p=0;p<native.bits.size();++p)
    different+=native.bits[p]!=gpu.bits[p];
  std::cout << "shadow_map_camera " << label << " native/gpu="
            << native.count << '/' << gpu.count << " center_x="
            << native.centerX << '/' << gpu.centerX
            << " coverage_mismatch=" << different << '\n';
  return check(native.count>25 && gpu.count>25 && different==0 &&
    std::abs(native.centerX-gpu.centerX)<=.5,
    label+" shadow footprint and spatial position");
}
bool manyMapCameras(Capture & capture,int maps) {
  auto * root=new SoSeparator;root->ref();
  auto * observer=new SoOrthographicCamera;
  observer->position.setValue(0,0,6);observer->height=4;
  observer->nearDistance=1;observer->farDistance=20;
  root->addChild(observer);
  auto * lighting=new SoLightModel;lighting->model=SoLightModel::PHONG;
  root->addChild(lighting);
  auto * group=new SoShadowGroup;group->quality=.2f;
  group->precision=.125f;root->addChild(group);
  std::vector<SoShadowDirectionalLight *> lights;
  std::vector<SoSeparator *> scenes;
  std::vector<SoSeparator *> casters;
  for(int slot=0;slot<maps;++slot) {
    const float cameraX=-.30f+.60f*float(slot)/float(std::max(1,maps-1));
    auto * light=new SoShadowDirectionalLight;
    light->direction.setValue(0,0,-1);
    light->bboxCenter.setValue(0,0,0);
    light->bboxSize.setValue(4,4,8);
    light->intensity=0;
    auto * scene=new SoSeparator;
    auto * camera=new SoOrthographicCamera;
    camera->position.setValue(cameraX,0,5);camera->height=4;
    camera->nearDistance=1;camera->farDistance=10;
    scene->addChild(camera);
    auto * caster=new SoSeparator;
    auto * style=new SoShadowStyle;
    style->style=SoShadowStyle::CASTS_SHADOW;
    caster->addChild(style);
    auto * move=new SoTranslation;move->translation.setValue(0,0,.35f);
    caster->addChild(move);
    auto * cube=new SoCube;cube->width=.48f;cube->height=.48f;
    cube->depth=.48f;caster->addChild(cube);
    scene->addChild(caster);light->shadowMapScene=scene;
    group->addChild(light);
    lights.push_back(light);scenes.push_back(scene);casters.push_back(caster);
  }
  auto * receiver=new SoSeparator;group->addChild(receiver);
  auto * style=new SoShadowStyle;style->style=SoShadowStyle::SHADOWED;
  receiver->addChild(style);plane(receiver);
  bool ok=true;
  std::vector<double> centers;
  for(int slot=0;slot<maps;++slot) {
    for(int i=0;i<maps;++i) lights[i]->intensity=i==slot?.7f:0.f;
    casters[slot]->ref();
    scenes[slot]->removeChild(casters[slot]);
    Frame base,shown;
    const bool baseOk=capture.gpu(root,base);
    scenes[slot]->addChild(casters[slot]);
    casters[slot]->unref();
    const bool shownOk=capture.gpu(root,shown);
    const std::string label="maps="+std::to_string(maps)+
      "/slot="+std::to_string(slot);
    ok &= check(baseOk && shownOk && base.serial<shown.serial,
      label+" published baseline and caster");
    if(baseOk && shownOk) {
      const Mask mask=shadowMask(base.rgba,shown.rgba,4);
      const float cameraX=-.30f+.60f*float(slot)/float(std::max(1,maps-1));
      // CoinGL viewBoundingBox fits the directional light camera to the
      // circumscribed sphere of bbox (4,4,8): lookup width sqrt(96),
      // while the authored map camera still has height 4. A caster at
      // world x=0 therefore appears at receiver x=-cameraX*sqrt(96)/4.
      const double lookupWidth=std::sqrt(4.*4.+4.*4.+8.*8.);
      const double expectedX=64.-32.*cameraX*lookupWidth/4.;
      std::cout << "shadow_map_camera " << label << " footprint="
                << mask.count << " center_x=" << mask.centerX
                << " analytic_x=" << expectedX << '\n';
      ok &= check(mask.count>25 &&
        std::abs(mask.centerX-expectedX)<=2.5,
        label+" independent projected shadow position");
      centers.push_back(mask.centerX);
    }
  }
  for(size_t i=1;i<centers.size();++i)
    ok &= check(centers[i-1]-centers[i]>3,
      "distinct authored map cameras preserve slot order");
  root->unref();
  return ok;
}
bool rasterCameraCore(Capture & capture) {
  bool ok=true;
  const double lookupScale=std::sqrt(4.*4.+4.*4.+8.*8.)/4.;
  for(int rasterKind=1;rasterKind<=4;++rasterKind) {
    Mask masks[2];
    for(int sample=0;sample<2;++sample) {
      Scene scene;
      scene.spot->on=FALSE;scene.directional->on=TRUE;
      if(rasterKind==1) scene.group->precision=.03125f;
      const float cameraX=sample==0?-.1f:.1f;
      // The builtin glyph keeps one-texel strokes at larger font sizes.
      // A 64px map makes one map texel about 4.9 observer pixels; shift
      // its LEFT-aligned origin into the witness ROI without changing the
      // expected camera displacement.
      const float casterX=rasterKind==1?-.35f:0.f;
      scene.configure(1,2,0,cameraX,casterX,rasterKind);
      scene.maps[1]->removeChild(scene.maps[1]->getNumChildren()-1);
      Frame base,shown;
      const bool baseOk=capture.gpu(scene.root,base);
      scene.configure(1,2,0,cameraX,casterX,rasterKind);
      const bool shownOk=capture.gpu(scene.root,shown);
      const std::string label="raster-core/kind="+
        std::to_string(rasterKind)+"/camera_x="+std::to_string(cameraX);
      ok &= check(baseOk && shownOk && base.serial<shown.serial,
        label+" published raster map frames");
      if(baseOk && shownOk) {
        masks[sample]=shadowMask(base.rgba,shown.rgba,4);
        ok &= check(masks[sample].count>25,
          label+" nonempty authored raster caster shadow");
        if(rasterKind!=1) {
          const double expectedX=64.-32.*cameraX*lookupScale;
          ok &= check(std::abs(masks[sample].centerX-expectedX)<=3,
            label+" independent centered image/marker position");
        }
      }
    }
    const double observed=masks[0].centerX-masks[1].centerX;
    const double expected=32.*.2*lookupScale;
    std::cout << "shadow_map_camera_raster_core kind=" << rasterKind
              << " counts=" << masks[0].count << ',' << masks[1].count
              << " camera_shift=" << observed
              << " analytic_shift=" << expected << '\n';
    ok &= check(masks[0].count>25 && masks[1].count>25 &&
      std::abs(observed-expected)<=3,
      "raster caster follows its own map camera in X");
  }
  return ok;
}
bool nearPlaneProfiles(Capture & capture) {
  bool ok=true;
  Mask reference;
  for(float nearPlane:{1.f,0.f,-2.f}) {
    Scene scene;
    scene.spot->on=FALSE;scene.directional->on=TRUE;
    scene.configure(1,2,0,.2f,0);
    scene.orthographic[1]->nearDistance=nearPlane;
    scene.maps[1]->removeChild(scene.maps[1]->getNumChildren()-1);
    Frame gpuBase,gpuShown;
    std::vector<uint8_t> glBase,glShown;
    const bool baseGpu=capture.gpu(scene.root,gpuBase);
    const bool baseGl=capture.native(scene.root,glBase);
    scene.configure(1,2,0,.2f,0);
    scene.orthographic[1]->nearDistance=nearPlane;
    const bool shownGpu=capture.gpu(scene.root,gpuShown);
    const bool shownGl=capture.native(scene.root,glShown);
    const std::string label="orthographic-near="+std::to_string(nearPlane);
    ok &= check(baseGpu && baseGl && shownGpu && shownGl &&
      gpuBase.serial<gpuShown.serial,label+" valid orthographic camera");
    if(baseGpu && baseGl && shownGpu && shownGl) {
      const Mask glMask=shadowMask(glBase,glShown,3);
      const Mask gpuMask=shadowMask(gpuBase.rgba,gpuShown.rgba,4);
      ok &= compareMask(glMask,gpuMask,label);
      if(nearPlane==1.f) reference=glMask;
      else ok &= check(std::abs(glMask.centerX-reference.centerX)<=.5 &&
        glMask.count==reference.count,
        label+" signed near leaves spatial map projection unchanged");
    }
  }
  Scene perspectiveScene;
  perspectiveScene.spot->on=TRUE;
  perspectiveScene.directional->on=FALSE;
  perspectiveScene.configure(0,1,0,.1f,0);
  Frame stable,recovered;
  ok &= check(capture.gpu(perspectiveScene.root,stable),
    "perspective valid frame before invalid near");
  auto * camera=perspectiveScene.perspective[0];
  for(int variant=0;variant<4;++variant) {
    if(variant==0) camera->nearDistance=0;
    if(variant==1) camera->nearDistance=-1;
    if(variant==2) camera->farDistance=1;
    if(variant==3)
      camera->nearDistance=std::numeric_limits<float>::quiet_NaN();
    capture.action.apply(perspectiveScene.root);
    Frame failed;
    capture.target->readbackRGBA(failed.rgba);
    capture.target->readbackDepth(failed.depth);
    failed.serial=capture.target->getLastSubmissionSerial();
    const auto status=capture.action.getLastStatus();
    camera->nearDistance=1;camera->farDistance=10;
    const bool recoveredOk=capture.gpu(perspectiveScene.root,recovered);
    const std::string label="perspective-invalid-near/variant="+
      std::to_string(variant);
    ok &= check(status!=CoinRenderAction::SUCCESS &&
      failed.rgba==stable.rgba && failed.depth==stable.depth &&
      failed.serial==stable.serial && recoveredOk &&
      recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
      recovered.serial>stable.serial,label+" rollback and recovery");
    if(recoveredOk) stable=recovered;
  }
  // A large light bbox compared with the observer/bbox intersection makes
  // CoinGL's fitted light camera lie inside the full bbox. Its near plane
  // is then -zmax*1.01 and legitimately negative, even without an authored
  // camera. Do not clamp it to zero in the executor.
  Scene lightNearScene;
  lightNearScene.spot->on=FALSE;lightNearScene.directional->on=TRUE;
  lightNearScene.directional->bboxSize.setValue(4,4,30);
  lightNearScene.configure(1,0,0,0,0);
  lightNearScene.maps[1]->removeChild(
    lightNearScene.maps[1]->getNumChildren()-1);
  Frame lightBase,lightShown;
  std::vector<uint8_t> lightGlBase,lightGlShown;
  const bool lightBaseOk=capture.gpu(lightNearScene.root,lightBase) &&
    capture.native(lightNearScene.root,lightGlBase);
  lightNearScene.configure(1,0,0,0,0);
  const bool lightShownOk=capture.gpu(lightNearScene.root,lightShown) &&
    capture.native(lightNearScene.root,lightGlShown);
  ok &= check(lightBaseOk && lightShownOk &&
    lightBase.serial<lightShown.serial,
    "fitted directional light camera with signed near publishes");
  if(lightBaseOk && lightShownOk)
    ok &= compareMask(shadowMask(lightGlBase,lightGlShown,3),
      shadowMask(lightBase.rgba,lightShown.rgba,4),
      "fitted directional signed near");
  return ok;
}
bool modelResetProfiles(Capture & capture,bool strictNative) {
  bool ok=true;
  // M before a camera is rotation*nonuniform-scale; P after it is a
  // translation. They do not commute (M*P != P*M), so a path that retains
  // M in the map's caster transform changes the physical footprint.
  const double angle=.31,px=.28,py=.12;
  const double mixedX=1.3*px*std::cos(angle)-.8*py*std::sin(angle);
  const double mixedY=1.3*px*std::sin(angle)+.8*py*std::cos(angle);
  ok &= check(std::hypot(mixedX-px,mixedY-py)>.08,
    "model M and post-camera P are independently noncommuting");
  for(int light=0;light<2;++light)
    for(int rasterKind:{0,2,3}) {
      Frame gpu[2],base[2];
      std::vector<uint8_t> gl[2],glBase[2];
      bool captured[2]={false,false};
      for(int withM=0;withM<2;++withM) {
        Scene scene;
        scene.spot->on=light==0;scene.directional->on=light==1;
        const int cameraKind=light==0?1:2;
        scene.configure(light,cameraKind,2,.1f,0,rasterKind);
        scene.afterCamera[light]->translation.setValue(px,py,0);
        if(withM) {
          auto * rotation=new SoRotation;
          rotation->rotation.setValue(SbVec3f(0,0,1),angle);
          auto * scale=new SoScale;
          scale->scaleFactor.setValue(1.3f,.8f,1.f);
          scene.maps[light]->insertChild(rotation,0);
          scene.maps[light]->insertChild(scale,1);
        }
        auto * map=scene.maps[light];
        auto * caster=map->getChild(map->getNumChildren()-1);
        caster->ref();map->removeChild(caster);
        const bool baseGpu=capture.gpu(scene.root,base[withM]);
        const bool baseGl=capture.native(scene.root,glBase[withM]);
        map->addChild(caster);caster->unref();
        const bool shownGpu=capture.gpu(scene.root,gpu[withM]);
        const bool shownGl=capture.native(scene.root,gl[withM]);
        captured[withM]=baseGpu && baseGl && shownGpu && shownGl;
      }
      const std::string label="model-reset/light="+
        std::to_string(light)+"/caster="+std::to_string(rasterKind);
      ok &= check(captured[0] && captured[1],label+" captures");
      if(captured[0] && captured[1]) {
        const Mask gpuNoM=shadowMask(base[0].rgba,gpu[0].rgba,4);
        const Mask gpuWithM=shadowMask(base[1].rgba,gpu[1].rgba,4);
        const Mask glNoM=shadowMask(glBase[0],gl[0],3);
        const Mask glWithM=shadowMask(glBase[1],gl[1],3);
        int gpuDifference=0,glDifference=0;
        for(size_t p=0;p<gpuNoM.bits.size();++p) {
          gpuDifference+=gpuNoM.bits[p]!=gpuWithM.bits[p];
          glDifference+=glNoM.bits[p]!=glWithM.bits[p];
        }
        std::cout << "shadow_map_camera_model_reset " << label
                  << " GPU0/M=" << gpuNoM.count << '/'
                  << gpuWithM.count << " GL0/M=" << glNoM.count << '/'
                  << glWithM.count << " mask_delta_GPU/GL="
                  << gpuDifference << '/' << glDifference << '\n';
        // The original GL reset is the independent oracle for this M/P
        // relation. Raster B21 may differ from CoinGL in absolute casting,
        // so compare its metamorphic reset separately from native parity.
        const bool nativeQualified=glNoM.count>25 && glWithM.count>25 &&
          glDifference==0 && gl[0]==gl[1];
        ok &= check(((strictNative || rasterKind==0) ? nativeQualified : true) &&
          gpuNoM.count>25 && gpuWithM.count>25 &&
          gpuDifference==0 && gpu[0].rgba==gpu[1].rgba &&
          gpu[0].depth==gpu[1].depth,
          label+" camera resets pre-model M while post-model P survives");
        if(rasterKind==0) {
          ok &= compareMask(glNoM,gpuNoM,label+" no pre-model");
          ok &= compareMask(glWithM,gpuWithM,label+" with pre-model");
        }
      }
    }
  return ok;
}
bool sharedCameraOccurrences(Capture & capture) {
  bool ok=true;
  Frame gpuBase[2][2],gpuShown[2][2];
  std::vector<uint8_t> glBase[2][2],glShown[2][2];
  bool baseGpuOk[2][2]={{false,false},{false,false}};
  bool shownGpuOk[2][2]={{false,false},{false,false}};
  bool baseGlOk[2][2]={{false,false},{false,false}};
  bool shownGlOk[2][2]={{false,false},{false,false}};
  for(int shared=0;shared<2;++shared) for(int preModel=0;preModel<2;++preModel) {
    Scene scene;
    scene.spot->on=FALSE;scene.directional->on=TRUE;
    auto * map=scene.maps[1];
    map->removeAllChildren();
    auto * commonCamera=new SoOrthographicCamera;
    commonCamera->ref();
    commonCamera->position.setValue(0,0,5);
    commonCamera->height=4;
    commonCamera->nearDistance=1;
    commonCamera->farDistance=10;
    SoScale * firstScale=nullptr;
    SoSwitch * switches[2]={nullptr,nullptr};
    for(int occurrence=0;occurrence<2;++occurrence) {
      auto * branch=new SoSeparator;map->addChild(branch);
      auto * rotation=new SoRotation;
      rotation->rotation.setValue(SbVec3f(0,0,1),
        occurrence==0?.31f:-.23f);
      branch->addChild(rotation);
      auto * scale=new SoScale;
      scale->scaleFactor.setValue(occurrence==0 ? 1.3f : .7f,
        occurrence==0 ? .8f : 1.4f,1.f);
      branch->addChild(scale);
      if(occurrence==0) firstScale=scale;
      if(shared) branch->addChild(commonCamera);
      else {
        auto * distinctCamera=new SoOrthographicCamera;
        distinctCamera->position=commonCamera->position.getValue();
        distinctCamera->height=commonCamera->height.getValue();
        distinctCamera->nearDistance=commonCamera->nearDistance.getValue();
        distinctCamera->farDistance=commonCamera->farDistance.getValue();
        branch->addChild(distinctCamera);
      }
      auto * post=new SoTranslation;
      post->translation.setValue(occurrence==0 ? -.48f : .43f,
        occurrence==0 ? -.12f : .16f,0);
      branch->addChild(post);
      switches[occurrence]=new SoSwitch;
      branch->addChild(switches[occurrence]);
      auto * caster=new SoSeparator;
      switches[occurrence]->addChild(caster);
      auto * style=new SoShadowStyle;
      style->style=SoShadowStyle::CASTS_SHADOW;
      caster->addChild(style);
      auto * cube=new SoCube;
      cube->width=.48f;cube->height=.48f;cube->depth=.48f;
      caster->addChild(cube);
    }
    commonCamera->unref();
    // The branch below the two Separators must see the outer map-camera
    // state; neither occurrence may leak its authored camera or model.
    auto * outer=cubeCaster(.8f);
    map->addChild(outer);
    const std::string label="shared-camera/shared="+std::to_string(shared)+
      "/pre-model="+std::to_string(preModel);
    if(!preModel) {
      firstScale->scaleFactor.setValue(1,1,1);
      auto * first=static_cast<SoSeparator *>(map->getChild(0));
      static_cast<SoRotation *>(first->getChild(0))->rotation.setValue(
        SbVec3f(0,0,1),0);
      auto * second=static_cast<SoSeparator *>(map->getChild(1));
      static_cast<SoScale *>(second->getChild(1))->scaleFactor.setValue(1,1,1);
      static_cast<SoRotation *>(second->getChild(0))->rotation.setValue(
        SbVec3f(0,0,1),0);
    }
    for(auto * sw:switches) sw->whichChild=SO_SWITCH_NONE;
    baseGpuOk[shared][preModel]=capture.gpu(scene.root,gpuBase[shared][preModel]);
    baseGlOk[shared][preModel]=capture.native(scene.root,glBase[shared][preModel]);
    ok &= check(baseGpuOk[shared][preModel] && baseGlOk[shared][preModel],
      label+" base");
    for(auto * sw:switches) sw->whichChild=SO_SWITCH_ALL;
    shownGpuOk[shared][preModel]=capture.gpu(scene.root,gpuShown[shared][preModel]);
    shownGlOk[shared][preModel]=capture.native(scene.root,glShown[shared][preModel]);
    ok &= check(shownGpuOk[shared][preModel] && shownGlOk[shared][preModel],
      label+" shown");
    if(shared && preModel) {
      if(shownGpuOk[shared][preModel]) {
        Frame stable=gpuShown[shared][preModel],failed,recovered;
        firstScale->scaleFactor.setValue(0,1,1);
        capture.action.apply(scene.root);
        capture.target->readbackRGBA(failed.rgba);
        capture.target->readbackDepth(failed.depth);
        failed.serial=capture.target->getLastSubmissionSerial();
        const auto status=capture.action.getLastStatus();
        firstScale->scaleFactor.setValue(1.3f,.8f,1.f);
        const bool restored=capture.gpu(scene.root,recovered);
        ok &= check(status!=CoinRenderAction::SUCCESS &&
          failed.rgba==stable.rgba && failed.depth==stable.depth &&
          failed.serial==stable.serial && restored &&
          recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
          recovered.serial>stable.serial,
          label+" singular pre-camera model rollback and recovery");
      } else {
        ok &= check(false,label+" singular pre-camera rollback needs valid frame");
      }
    }
  }
  for(int shared=0;shared<2;++shared) for(int preModel=0;preModel<2;++preModel) {
    const std::string label="shared-camera/shared="+std::to_string(shared)+
      "/pre-model="+std::to_string(preModel);
    const bool complete=baseGpuOk[shared][preModel] &&
      shownGpuOk[shared][preModel] && baseGlOk[shared][preModel] &&
      shownGlOk[shared][preModel] && baseGpuOk[0][0] && shownGpuOk[0][0] &&
      baseGlOk[0][0] && shownGlOk[0][0];
    if(!check(complete,label+" complete captures for mask comparison")) {
      ok=false;
      continue;
    }
    const Mask gpuMask=shadowMask(gpuBase[shared][preModel].rgba,
      gpuShown[shared][preModel].rgba,4);
    const Mask glMask=shadowMask(glBase[shared][preModel],
      glShown[shared][preModel],3);
    ok &= check(gpuMask.count>25 && glMask.count>25 &&
      gpuBase[shared][preModel].rgba==gpuBase[0][0].rgba &&
      gpuShown[shared][preModel].rgba==gpuShown[0][0].rgba &&
      gpuShown[shared][preModel].depth==gpuShown[0][0].depth &&
      glBase[shared][preModel]==glBase[0][0] &&
      glShown[shared][preModel]==glShown[0][0],
      label+" shared camera reset, independent post-model and outer state");
    ok &= compareMask(glMask,gpuMask,label);
  }
  return ok;
}
bool rectangularCameras(bool canonical,bool nativeOnly,bool core=false) {
  RectCapture capture(canonical);
  if(!check(bool(capture.target),"rectangular offscreen target")) return false;
  bool ok=true;
  for(int mapping=0;mapping<2;++mapping)
    for(int placement=0;placement<(core?1:3);++placement) {
      RectMask nativeMasks[3],gpuMasks[3];
      for(int shift=0;shift<3;++shift) {
        Scene scene;
        scene.spot->on=FALSE;scene.directional->on=TRUE;
        auto * observer=static_cast<SoOrthographicCamera *>(scene.root->getChild(0));
        observer->viewportMapping=mapping==0 ?
          SoCamera::ADJUST_CAMERA : SoCamera::LEAVE_ALONE;
        const float dx=shift==1?.30f:0.f;
        const float dy=shift==2?.25f:0.f;
        scene.configure(1,2,placement,dx,.35f);
        auto * camera=scene.orthographic[1];
        camera->position.setValue(dx,dy,5);
        camera->orientation.setValue(SbRotation(SbVec3f(0,0,1),.17f));
        if(scene.beforeCamera[1])
          scene.beforeCamera[1]->translation.setValue(.15f,-.12f,0);
        if(scene.afterCamera[1])
          scene.afterCamera[1]->translation.setValue(.15f,-.12f,0);
        auto * map=scene.maps[1];
        map->removeChild(map->getNumChildren()-1);
        std::vector<uint8_t> glBase,glShown,gpuBase,gpuShown;
        const bool baseGpu=nativeOnly || capture.gpu(scene.root,gpuBase);
        const bool baseGl=core || capture.native(scene.root,glBase);
        scene.configure(1,2,placement,dx,.35f);
        camera=scene.orthographic[1];
        camera->position.setValue(dx,dy,5);
        camera->orientation.setValue(SbRotation(SbVec3f(0,0,1),.17f));
        if(scene.beforeCamera[1])
          scene.beforeCamera[1]->translation.setValue(.15f,-.12f,0);
        if(scene.afterCamera[1])
          scene.afterCamera[1]->translation.setValue(.15f,-.12f,0);
        const bool shownGpu=nativeOnly || capture.gpu(scene.root,gpuShown);
        const bool shownGl=core || capture.native(scene.root,glShown);
        const std::string label="mapping="+std::to_string(mapping)+
          "/placement="+std::to_string(placement)+
          "/shift="+std::to_string(shift);
        ok &= check(baseGpu && baseGl && shownGpu && shownGl,
          label+" rectangular camera frames");
        if(!core && baseGl && shownGl) {
          nativeMasks[shift]=rectangleShadowMask(glBase,glShown,3,
            RectCapture::width,RectCapture::height);
          ok &= check(nativeMasks[shift].count>25,
            label+" native rectangular shadow witness");
        }
        if(!nativeOnly && baseGpu && shownGpu) {
          gpuMasks[shift]=rectangleShadowMask(gpuBase,gpuShown,4,
            RectCapture::width,RectCapture::height);
          ok &= check(gpuMasks[shift].count>25,
            label+" GPU rectangular shadow witness");
          if(!core && baseGl && shownGl)
            ok &= compareRectangle(nativeMasks[shift],gpuMasks[shift],label);
        }
      }
      const double nativeDx=nativeMasks[1].centerX-nativeMasks[0].centerX;
      const double nativeDy=nativeMasks[2].centerY-nativeMasks[0].centerY;
      const double gpuDx=gpuMasks[1].centerX-gpuMasks[0].centerX;
      const double gpuDy=gpuMasks[2].centerY-gpuMasks[0].centerY;
      std::cout << "shadow_map_camera_rect mapping=" << mapping
                << " placement=" << placement << " native_delta="
                << nativeDx << ',' << nativeDy << " gpu_delta="
                << gpuDx << ',' << gpuDy << '\n';
      // The authored map camera moves +X/+Y while its caster stays fixed;
      // the light lookup therefore places its shadow left/down on the
      // observer's rectangular framebuffer (top-down readback has +Y down).
      if(core) {
        // The authored 4-unit square camera projects through the light's
        // circumscribed-box lookup width sqrt(96). The observer maps height
        // 4 to 96 pixels; LEAVE_ALONE retains aspectRatio=1, so its X scale
        // is 160/4 rather than ADJUST_CAMERA's 96/4.
        const double scale=std::sqrt(96.)/4.;
        const double xPixelsPerWorld=mapping==0?24.:40.;
        const double expectedDx=-.30*std::cos(.17)*scale*xPixelsPerWorld;
        const double expectedDy=.25*std::cos(.17)*scale*24.;
        std::cout << "shadow_map_camera_rect_core mapping=" << mapping
                  << " analytic_delta=" << expectedDx << ',' << expectedDy
                  << " observed_delta=" << gpuDx << ',' << gpuDy << '\n';
        ok &= check(gpuMasks[0].count>25 && gpuMasks[1].count>25 &&
          gpuMasks[2].count>25 && std::abs(gpuDx-expectedDx)<=3 &&
          std::abs(gpuDy-expectedDy)<=3,
          "independent two-axis projection under observer viewport mapping");
      } else {
        ok &= check(nativeMasks[0].count>25 &&
          nativeMasks[1].count>25 && nativeMasks[2].count>25 &&
          nativeDx< -3 && nativeDy>3 &&
          (nativeOnly || (gpuDx< -3 && gpuDy>3)),
          "independent X/Y projection witnesses in rectangular viewport");
      }
    }
  return ok;
}
// This is the common CANONICAL map contract, deliberately independent of
// CoinGL's RGBA16F target and of the GPU frame under test. The map pass is a
// 256-square Rgba32Float moment image. Its only caster is the front face of
// the authored .48 cube, at z=.59; every covered texel has the same moments.
// docs/coin-render-b01-canonical-coverage-20261016.md fixes coordinates by
// Q(x)=(floor(128*x)+.5)/128 in Coin bottom-left space; the odd 1/256 phase
// keeps horizontal/vertical polygon edges away from pixel centres.
double canonicalWindow(double coordinate) {
  return (std::floor(128.*coordinate)+.5)/128.;
}
double smoothstep01(double edge0,double edge1,double value) {
  const double t=std::max(0.,std::min(1.,(value-edge0)/(edge1-edge0)));
  return t*t*(3.-2.*t);
}
bool rectangularVsmCore() {
  RectCapture capture(true);
  if(!check(bool(capture.target),"canonical rectangular VSM target")) return false;
  constexpr int mapSide=256;
  constexpr double roll=.17;
  const double cs=std::cos(roll),sn=std::sin(roll);
  const double radius=std::sqrt(24.);
  const double nearValue=(radius-4.)*1.01;
  const double farValue=(radius+4.)*1.01*1.1;
  const double casterDistance=5.-.59;
  const double receiverDistance=radius+1.;
  // Near/far and the map's two moment channels cross float uniforms and
  // Rgba32Float texels. Round each stored input before evaluating m and m².
  const float nearUniform=float(nearValue);
  const float farUniform=float(farValue);
  const float casterMoment=(float(casterDistance)-nearUniform)/
    (farUniform-nearUniform);
  const float casterSecondMoment=casterMoment*casterMoment;
  const float receiverMoment=(float(receiverDistance)-nearUniform)/
    (farUniform-nearUniform);
  bool ok=true;
  for(int mapping=0;mapping<2;++mapping)
    for(int placement=0;placement<3;++placement)
      for(int shift=0;shift<3;++shift) {
        const double cameraX=shift==1?.30:0.;
        const double cameraY=shift==2?.25:0.;
        const double postX=placement==2?.15:0.;
        const double postY=placement==2?-.12:0.;
        const double casterX=.35+postX,casterY=postY;
        Scene scene;
        scene.spot->on=FALSE;scene.directional->on=TRUE;
        auto * observer=static_cast<SoOrthographicCamera *>(scene.root->getChild(0));
        observer->viewportMapping=mapping==0 ?
          SoCamera::ADJUST_CAMERA : SoCamera::LEAVE_ALONE;
        scene.configure(1,2,placement,float(cameraX),.35f);
        auto * mapCamera=scene.orthographic[1];
        mapCamera->position.setValue(float(cameraX),float(cameraY),5);
        mapCamera->orientation.setValue(SbRotation(SbVec3f(0,0,1),float(roll)));
        if(scene.beforeCamera[1])
          scene.beforeCamera[1]->translation.setValue(.15f,-.12f,0);
        if(scene.afterCamera[1])
          scene.afterCamera[1]->translation.setValue(.15f,-.12f,0);
        std::vector<uint8_t> gpu;
        std::vector<float> depth;
        capture.action.apply(scene.root);
        const bool published=capture.action.getLastStatus()==CoinRenderAction::SUCCESS;
        if(published) {
          capture.target->readbackRGBA(gpu);
          capture.target->readbackDepth(depth);
        }
        const std::string label="rectangular-vsm/mapping="+
          std::to_string(mapping)+"/placement="+std::to_string(placement)+
          "/shift="+std::to_string(shift);
        if(!check(published && gpu.size()==size_t(RectCapture::width*
            RectCapture::height*4) && depth.size()==size_t(RectCapture::width*
            RectCapture::height),label+" frame")) {
          std::cerr << label << " status=" << capture.action.getLastStatus()
                    << " diagnostic=" << capture.action.getLastError().getString()
                    << '\n';
          ok=false;continue;
        }
        // Project the four source corners through the authored map camera.
        // Quantization is B01's odd 1/256-pixel grid in bottom-left space.
        double polygon[4][2];
        const double cornerX[4]={-.24,.24,.24,-.24};
        const double cornerY[4]={-.24,-.24,.24,.24};
        for(int i=0;i<4;++i) {
          const double dx=casterX+cornerX[i]-cameraX;
          const double dy=casterY+cornerY[i]-cameraY;
          const double localX=cs*dx+sn*dy;
          const double localY=-sn*dx+cs*dy;
          polygon[i][0]=canonicalWindow((.5+localX/4.)*mapSide);
          polygon[i][1]=canonicalWindow((.5+localY/4.)*mapSide);
        }
        std::vector<uint8_t> coverage(size_t(mapSide*mapSide),0);
        for(int ty=0;ty<mapSide;++ty) for(int tx=0;tx<mapSide;++tx) {
          const double px=tx+.5,py=ty+.5;
          bool inside=true;
          for(int edge=0;edge<4;++edge) {
            const int next=(edge+1)%4;
            const double ex=polygon[next][0]-polygon[edge][0];
            const double ey=polygon[next][1]-polygon[edge][1];
            const double cross=ex*(py-polygon[edge][1])-
              ey*(px-polygon[edge][0]);
            const bool topLeft=ey>0 || (ey==0 && ex<0);
            if(cross<0 || (cross==0 && !topLeft)) {inside=false;break;}
          }
          coverage[size_t(ty*mapSide+tx)]=inside;
        }
        auto momentAt=[&](int tx,int ty,int order) {
          tx=std::max(0,std::min(mapSide-1,tx));
          ty=std::max(0,std::min(mapSide-1,ty));
          const bool cast=coverage[size_t(ty*mapSide+tx)]!=0;
          return cast ? (order==1 ? casterMoment : casterSecondMoment) : 1.f;
        };
        int worst=0,worstX=-1,worstY=-1,bad=0,shadowed=0,transition=0;
        double worstDepth=0;
        // Stay inside the authored receiver, avoiding its own outer edge.
        // All covered interior pixels, including VSM's bilinear penumbra,
        // are compared; no sample is chosen from the GPU result.
        for(int y=0;y<RectCapture::height;++y)
          for(int x=0;x<RectCapture::width;++x) {
            const double worldX=(x+.5-80.)/(mapping==0?24.:40.);
            const double worldY=(48.-(y+.5))/24.;
            if(std::abs(worldX)>=1.7 || std::abs(worldY)>=1.7) continue;
            const double u=.5+worldX/(2.*radius);
            const double v=.5+worldY/(2.*radius);
            const double texX=u*mapSide-.5,texY=v*mapSide-.5;
            const int x0=int(std::floor(texX)),y0=int(std::floor(texY));
            const double fx=texX-x0,fy=texY-y0;
            auto sampledMoment=[&](int order) {
              const double a=momentAt(x0,y0,order);
              const double b=momentAt(x0+1,y0,order);
              const double c=momentAt(x0,y0+1,order);
              const double d=momentAt(x0+1,y0+1,order);
              return (1.-fy)*((1.-fx)*a+fx*b)+
                fy*((1.-fx)*c+fx*d);
            };
            const double mean=sampledMoment(1),second=sampledMoment(2);
            const double variance=std::min(1.,std::max(0.,second-mean*mean)+1e-5);
            const double distance=mean-receiverMoment;
            double probability=variance/(variance+distance*distance);
            probability*=smoothstep01(.1,1.,probability);
            const double shade=std::max(receiverMoment<=mean?1.:0.,probability);
            shadowed+=shade<.9;
            transition+=shade>.02 && shade<.98;
            const double diffuse[3]={.72,.75,.78};
            const size_t pixel=size_t(y*RectCapture::width+x);
            for(int channel=0;channel<3;++channel) {
              const int expected=int(std::lround(255.*(.036+
                diffuse[channel]*.6*shade)));
              const int error=std::abs(int(gpu[pixel*4+channel])-expected);
              bad+=error>3;
              if(error>worst) {worst=error;worstX=x;worstY=y;}
            }
            const int alphaError=std::abs(int(gpu[pixel*4+3])-255);
            bad+=alphaError!=0;
            if(alphaError>worst) {worst=alphaError;worstX=x;worstY=y;}
            worstDepth=std::max(worstDepth,std::abs(double(depth[pixel])-6./19.));
          }
        std::cout << "shadow_map_camera_rect_vsm " << label
                  << " shadowed=" << shadowed << " transition=" << transition
                  << " max_rgba=" << worst << " worst_xy=" << worstX << ','
                  << worstY << " bad=" << bad << " max_depth=" << worstDepth
                  << '\n';
        ok &= check(shadowed>25 && transition>0 && bad==0 && worst<=3 &&
          worstDepth<=1e-5,label+" independent canonical VSM RGB/alpha/depth");
      }
  return ok;
}
#include "CoinRenderShadowMapCropCases.h"
#include "CoinRenderShadowMapNearSlots.h"
#include "CoinRenderShadowMapStereoCases.h"
int stereoMaps=8,stereoSlot=-1,stereoRasterKind=0;
bool stereoDirectional=true,stereoOrthographic=false,stereoOptions=false;
int stereoMapping=SoCamera::ADJUST_CAMERA;
unsigned cameraSamples = 1;
int nearMapCount = 0;
bool nearGeneral = false;
int cropExecutor = 0;
int cropExtent = 0;
bool run(bool canonical,int scope,bool nativeOnly) {
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      &caps,sizeof(caps))!=0 || !caps.gpu_available) return false;
  std::cout << "shadow_map_camera backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " adapter=" << caps.adapter_name
            << " raster=" << (canonical?"CANONICAL_8BIT":"NATIVE") << '\n';
  if(scope==4 || scope==9)
    return rectangularCameras(canonical,nativeOnly,scope==9);
  if(scope==11) return rectangularVsmCore();
  Capture capture(canonical,cameraSamples);
  if(!check(bool(capture.target),"offscreen target")) return false;
  if(scope>=30 && scope<=37) {
    capture.action.setTransparencyType(CoinRenderAction::NONE);
    if(scope<=33) {
      if(!check(capture.target->getOptions().sampleCount==1,"stereo first qualification is actual1x"))return false;
    } else {
      if(!check(nearNativeMsaa(*capture.target) && capture.target->getLastSubmissionSerial()==0 && capture.target->getOptions().sampleCount==cameraSamples &&
          (!cameraPortableSampling || capture.target->getOptions().textureSamplingPolicy==COIN_RENDER_SAMPLING_PORTABLE),
          "requested stereo Native MSAA samples and sampling policy retained"))return false;
      capture.action.setBackgroundColor(SbColor4f(0,0,0,0));
    }
    auto &target=capture.target->getPimpl().get();
    if(!check(target.prepareBackend()==CoinRenderBackendStatus::SUCCESS&&target.backend&&target.backend->isGpuBackend(),"real stereo GPU executor prepared"))return false;
    auto*audit=new NearAudit(std::move(target.backend));target.backend.reset(audit);
    std::cout<<"stereo_environment maps="<<stereoMaps<<" directional="<<stereoDirectional<<" orthographic="<<stereoOrthographic
      <<" mapping="<<stereoMapping<<" general="<<nearGeneral<<" slot="<<stereoSlot<<'\n';
    if(scope==30 || scope==34)return stereoProfiles(capture,*audit,stereoMaps,stereoDirectional,stereoOrthographic,stereoMapping,nearGeneral,0,stereoSlot);
    if(scope==37)return stereoMsaaScissor(capture,*audit,stereoMaps,stereoDirectional,stereoOrthographic,nearGeneral,stereoSlot);
    if(scope==33)return stereoScales(capture,*audit,stereoMaps,stereoDirectional,stereoOrthographic,stereoMapping,nearGeneral,stereoSlot);
    if(scope==32 || scope==36)return stereoState(capture,*audit,stereoMaps,stereoDirectional,stereoOrthographic,stereoMapping,nearGeneral,stereoSlot);
    bool result=true;for(int raster:{1,2})if(!stereoRasterKind||stereoRasterKind==raster)
      result &= stereoProfiles(capture,*audit,stereoMaps,stereoDirectional,stereoOrthographic,stereoMapping,nearGeneral,raster,stereoSlot);
    return result;
  }
  if(scope>=16 && scope<=21) {
    capture.action.setTransparencyType(CoinRenderAction::NONE);
    if(!check(capture.target->getOptions().sampleCount==cameraSamples && (!cameraPortableSampling || capture.target->getOptions().textureSamplingPolicy==COIN_RENDER_SAMPLING_PORTABLE),"requested camera sample count and sampling policy retained")) return false;
    auto &target = capture.target->getPimpl().get();
    if(!check(target.prepareBackend()==CoinRenderBackendStatus::SUCCESS &&
              target.backend && target.backend->isGpuBackend(), "real GPU executor prepared")) return false;
    auto *audit = new NearAudit(std::move(target.backend));
    target.backend.reset(audit);
    bool result = true;
    for(int maps : {4,8}) {
      if(nearMapCount && nearMapCount!=maps) continue;
      std::cout << "near_slots_environment maps=" << maps << " general=" << nearGeneral << '\n';
      if(scope==16) result &= nearSlotProfiles(capture,*audit,maps,nearGeneral,0);
      else if(scope==17) for(int raster : {1,2})
        result &= nearSlotProfiles(capture,*audit,maps,nearGeneral,raster);
      else if(scope==18) result &= nearSlotState(capture,*audit,maps,nearGeneral);
      else if(scope==19) result &= nearCoverage(capture,*audit,maps,nearGeneral,cameraSamples);
      else if(scope==20) result &= nearCropProfiles(capture,*audit,maps,nearGeneral);
      else result &= nearScissorProfiles(capture,*audit,maps,nearGeneral);
    }
    return result;
  }
  if(scope>=12 && scope<=15) {
    capture.action.setTransparencyType(CoinRenderAction::NONE);
    std::cout << "crop_environment executor=" << cropExecutor
              << " requested_map=" << cropExtent << '\n';
    if(scope==12) return cropGeometry(capture,cropExecutor,cropExtent);
    if(scope==13) return cropViewports(capture,cropExecutor,cropExtent);
    if(scope==14) return cropRasters(capture,cropExecutor,cropExtent);
    return cropState(capture,cropExecutor,cropExtent);
  }
  if(scope==5) return rasterCameraCore(capture);
  if(scope==6) return nearPlaneProfiles(capture);
  if(scope==7 || scope==8)
    return modelResetProfiles(capture,scope==7);
  if(scope==10) return sharedCameraOccurrences(capture);
  bool ok=true;
  if((scope==0 || scope==2) && !nativeOnly)
    for(int maps:{1,4,8}) ok &= manyMapCameras(capture,maps);
  // The light's own camera is prepended to shadowMapScene by CoinGL. An
  // authored camera traversed later replaces its projection for that map;
  // the observer camera and sibling light retain their own state.
  if(scope==0 || scope==1) for(int light=0;light<2;++light) {
    for(int kind=0;kind<=2;++kind) for(int placement=0;placement<=2;++placement) {
      if(kind==0 && placement>0) continue;
      Scene scene;
      scene.spot->on=light==0;scene.directional->on=light==1;
      scene.configure(light,kind,placement,.3f,light==0?-.48f:.48f);
      auto * map=scene.maps[light];
      std::vector<uint8_t> glBase,glShown;
      Frame gpuBase,gpuShown;
      map->removeChild(map->getNumChildren()-1);
      const bool baseGpu=nativeOnly || capture.gpu(scene.root,gpuBase);
      const bool baseGl=capture.native(scene.root,glBase);
      scene.configure(light,kind,placement,.3f,light==0?-.48f:.48f);
      const bool shownGpu=nativeOnly || capture.gpu(scene.root,gpuShown);
      const bool shownGl=capture.native(scene.root,glShown);
      const std::string label="light="+std::to_string(light)+
        "/camera="+std::to_string(kind)+"/placement="+
        std::to_string(placement);
      ok &= check(baseGpu && baseGl && shownGpu && shownGl &&
        (nativeOnly || gpuBase.serial<gpuShown.serial),
        label+" successful frames");
      if(baseGl && shownGl) {
        const Mask native=shadowMask(glBase,glShown,3);
        ok &= check(native.count>25,label+" native spatial witness");
        if(baseGpu && shownGpu && !nativeOnly)
          ok &= compareMask(native,
            shadowMask(gpuBase.rgba,gpuShown.rgba,4),label);
      }
    }
  }
  if(scope==0 || scope==1) {
    Scene scene;
    // The inner camera affects only its own Separator. The outer camera
    // resumes for the sibling caster in the very same shadowMapScene.
    scene.spot->on=TRUE;scene.directional->on=FALSE;
    scene.configure(0,2,0,0,0);
    auto * map=scene.maps[0];
    map->removeChild(map->getNumChildren()-1);
    auto * inner=new SoSeparator;inner->ref();
    auto * innerCamera=new SoPerspectiveCamera;
    innerCamera->position.setValue(.4f,0,5);
    innerCamera->heightAngle=1.0f;
    innerCamera->nearDistance=1;innerCamera->farDistance=10;
    inner->addChild(innerCamera);
    inner->addChild(cubeCaster(-.5f));
    auto * sibling=cubeCaster(.5f);sibling->ref();
    Frame gpuBase,gpuInner,gpuSibling,gpuBoth;
    std::vector<uint8_t> glBase,glInner,glSibling,glBoth;
    const bool baseGpu=nativeOnly || capture.gpu(scene.root,gpuBase);
    const bool baseGl=capture.native(scene.root,glBase);
    map->addChild(inner);
    const bool innerGpu=nativeOnly || capture.gpu(scene.root,gpuInner);
    const bool innerGl=capture.native(scene.root,glInner);
    map->removeChild(inner);
    map->addChild(sibling);
    const bool siblingGpu=nativeOnly || capture.gpu(scene.root,gpuSibling);
    const bool siblingGl=capture.native(scene.root,glSibling);
    map->addChild(inner);
    const bool bothGpu=nativeOnly || capture.gpu(scene.root,gpuBoth);
    const bool bothGl=capture.native(scene.root,glBoth);
    ok &= check(baseGpu && baseGl && innerGpu && innerGl &&
      siblingGpu && siblingGl && bothGpu && bothGl,
      "nested and sibling map cameras render");
    if(baseGl && innerGl && siblingGl && bothGl) {
      const Mask first=shadowMask(glBase,glInner,3);
      const Mask second=shadowMask(glBase,glSibling,3);
      const Mask joined=shadowMask(glBase,glBoth,3);
      ok &= check(first.count>25 && second.count>25 &&
        joined.count>first.count && joined.count>second.count &&
        first.centerX+4<second.centerX,
        "native nested camera is spatially scoped to its caster");
      if(!nativeOnly && baseGpu && innerGpu && siblingGpu && bothGpu) {
        ok &= compareMask(first,shadowMask(gpuBase.rgba,gpuInner.rgba,4),
          "nested/inner");
        ok &= compareMask(second,shadowMask(gpuBase.rgba,gpuSibling.rgba,4),
          "nested/sibling");
        ok &= compareMask(joined,shadowMask(gpuBase.rgba,gpuBoth.rgba,4),
          "nested/both");
      }
    }
    inner->unref();sibling->unref();
  }
  // Raster casters use their authored bitmap/marker/text mask in the map
  // camera's pixel space. Both projection kinds are exercised on each light.
  if(scope==0 || scope==3) for(int rasterKind=1;rasterKind<=4;++rasterKind)
    for(int light=0;light<2;++light) {
      Scene scene;
      scene.spot->on=light==0;scene.directional->on=light==1;
      const int kind=light==0?1:2;
      scene.configure(light,kind,0,.2f,0,rasterKind);
      auto * map=scene.maps[light];
      map->removeChild(map->getNumChildren()-1);
      Frame gpuBase,gpuShown;
      std::vector<uint8_t> glBase,glShown;
      const bool baseGpu=nativeOnly || capture.gpu(scene.root,gpuBase);
      const bool baseGl=capture.native(scene.root,glBase);
      scene.configure(light,kind,0,.2f,0,rasterKind);
      const bool shownGpu=nativeOnly || capture.gpu(scene.root,gpuShown);
      const bool shownGl=capture.native(scene.root,glShown);
      const std::string label="raster="+std::to_string(rasterKind)+
        "/light="+std::to_string(light);
      ok &= check(baseGpu && baseGl && shownGpu && shownGl,label+" frames");
      if(baseGl && shownGl) {
        const Mask native=shadowMask(glBase,glShown,3);
        ok &= check(native.count>25,label+" native raster caster witness");
        if(baseGpu && shownGpu && !nativeOnly)
          ok &= compareMask(native,
            shadowMask(gpuBase.rgba,gpuShown.rgba,4),label);
      }
    }
  // Moving only the authored camera in +X projects the same caster toward
  // the left of the light map. This signed spatial check is independent of
  // the CoinGL image comparison and rejects a camera ignored by both paths.
  if(scope==0 || scope==1) for(int light=0;light<2;++light) {
    Scene scene;
    scene.spot->on=light==0;scene.directional->on=light==1;
    const int kind=light==0?1:2;
    Mask gpuMasks[2],glMasks[2];
    for(int sample=0;sample<2;++sample) {
      const float cameraX=sample==0?-.4f:.4f;
      scene.configure(light,kind,0,cameraX,0);
      scene.maps[light]->removeChild(scene.maps[light]->getNumChildren()-1);
      Frame gpuBase,gpuShown;
      std::vector<uint8_t> glBase,glShown;
      const bool baseGpu=nativeOnly || capture.gpu(scene.root,gpuBase);
      const bool baseGl=capture.native(scene.root,glBase);
      scene.configure(light,kind,0,cameraX,0);
      const bool shownGpu=nativeOnly || capture.gpu(scene.root,gpuShown);
      const bool shownGl=capture.native(scene.root,glShown);
      const std::string label="camera-shift/light="+std::to_string(light)+
        "/sample="+std::to_string(sample);
      ok &= check(baseGpu && baseGl && shownGpu && shownGl,label+" frames");
      if(baseGl && shownGl) {
        glMasks[sample]=shadowMask(glBase,glShown,3);
        ok &= check(glMasks[sample].count>25,label+" native shift witness");
        if(baseGpu && shownGpu && !nativeOnly) {
          gpuMasks[sample]=shadowMask(gpuBase.rgba,gpuShown.rgba,4);
          ok &= compareMask(glMasks[sample],gpuMasks[sample],label);
        }
      }
    }
    std::cout << "shadow_map_camera shift/light=" << light
              << " GPU=" << gpuMasks[0].centerX-gpuMasks[1].centerX
              << " CoinGL=" << glMasks[0].centerX-glMasks[1].centerX << '\n';
    ok &= check(glMasks[0].count>25 && glMasks[1].count>25 &&
      glMasks[0].centerX-glMasks[1].centerX>4 &&
      (nativeOnly || (gpuMasks[0].count>25 && gpuMasks[1].count>25 &&
        gpuMasks[0].centerX-gpuMasks[1].centerX>4)),
      "authored camera signed spatial shift");
  }
  // Two distinct scenes and cameras must contribute independently, with
  // their state restored before the receiver and the other light.
  if((scope==0 || scope==1) && !nativeOnly) {
  Scene scene;
  scene.spot->on=TRUE;scene.directional->on=TRUE;
  scene.configure(0,1,1,-.2f,-.55f);
  scene.configure(1,2,2,.25f,.55f);
  Frame both,onlyFirst,onlySecond;
  ok &= check(capture.gpu(scene.root,both),"two-light composition");
  scene.directional->on=FALSE;
  ok &= check(capture.gpu(scene.root,onlyFirst),"first camera only");
  scene.directional->on=TRUE;scene.spot->on=FALSE;
  ok &= check(capture.gpu(scene.root,onlySecond),"second camera only");
  scene.spot->on=TRUE;
  if(!both.rgba.empty() && !onlyFirst.rgba.empty() && !onlySecond.rgba.empty())
    ok &= check(both.rgba!=onlyFirst.rgba && both.rgba!=onlySecond.rgba &&
      onlyFirst.rgba!=onlySecond.rgba,
      "two light cameras remain distinct and both contribute");
  // A non-finite authored camera must fail before publishing color, depth,
  // or serial. Restoring that same node must recover the original frame.
  Frame stable,failed,recovered;
  ok &= check(capture.gpu(scene.root,stable),"stable two-light frame");
  auto * camera=scene.perspective[0];
  camera->position.setValue(std::numeric_limits<float>::quiet_NaN(),0,5);
  capture.action.apply(scene.root);
  capture.target->readbackRGBA(failed.rgba);
  capture.target->readbackDepth(failed.depth);
  failed.serial=capture.target->getLastSubmissionSerial();
  const auto invalidStatus=capture.action.getLastStatus();
  camera->position.setValue(-.2f,0,5);
  const bool recoveredOk=capture.gpu(scene.root,recovered);
  ok &= check(invalidStatus!=CoinRenderAction::SUCCESS &&
    failed.rgba==stable.rgba && failed.depth==stable.depth &&
    failed.serial==stable.serial && recoveredOk &&
    recovered.rgba==stable.rgba && recovered.depth==stable.depth &&
    recovered.serial>stable.serial,
    "invalid camera rollback and exact recovery");
  }
  return ok;
}
} // namespace
int main(int argc,char ** argv) {
  bool canonical=false;
  bool nativeOnly=false;
  int scope=0;
  for(int i=1;i<argc;++i) {
    if(std::string(argv[i])=="--canonical-raster") canonical=true;
    else if(std::string(argv[i])=="--portable-sampling") cameraPortableSampling=true;
    else if(std::string(argv[i])=="--native-oracle-only") nativeOnly=true;
    else if(std::string(argv[i])=="--geometry") scope=1;
    else if(std::string(argv[i])=="--slots") scope=2;
    else if(std::string(argv[i])=="--raster-native") scope=3;
    else if(std::string(argv[i])=="--rectangular") scope=4;
    else if(std::string(argv[i])=="--raster-core") scope=5;
    else if(std::string(argv[i])=="--near-planes") scope=6;
    else if(std::string(argv[i])=="--model-reset") scope=7;
    else if(std::string(argv[i])=="--model-reset-core") scope=8;
    else if(std::string(argv[i])=="--rectangular-core") scope=9;
    else if(std::string(argv[i])=="--shared-camera") scope=10;
    else if(std::string(argv[i])=="--rectangular-vsm-core") scope=11;
    else if(std::string(argv[i])=="--crop-geometry") scope=12;
    else if(std::string(argv[i])=="--crop-viewport") scope=13;
    else if(std::string(argv[i])=="--crop-raster") scope=14;
    else if(std::string(argv[i])=="--crop-state") scope=15;
    else if(std::string(argv[i])=="--stereo") scope=30;
    else if(std::string(argv[i])=="--stereo-raster") scope=31;
    else if(std::string(argv[i])=="--stereo-state") scope=32;
    else if(std::string(argv[i])=="--stereo-scales") scope=33;
    else if(std::string(argv[i])=="--stereo-msaa") scope=34;
    else if(std::string(argv[i])=="--stereo-msaa-raster") scope=35;
    else if(std::string(argv[i])=="--stereo-msaa-state") scope=36;
    else if(std::string(argv[i])=="--stereo-msaa-scissor") scope=37;
    else if(std::string(argv[i])=="--stereo-general") {nearGeneral=true;stereoOptions=true;}
    else if(std::string(argv[i])=="--stereo-maps"||std::string(argv[i])=="--stereo-slot"||
            std::string(argv[i])=="--stereo-light"||std::string(argv[i])=="--stereo-camera"||
            std::string(argv[i])=="--stereo-mapping"||std::string(argv[i])=="--stereo-raster-kind") {
      const std::string option=argv[i];if(++i==argc)return 2;const std::string value=argv[i];stereoOptions=true;
      if(option=="--stereo-maps"){if(value!="4"&&value!="8")return 2;stereoMaps=std::stoi(value);}
      else if(option=="--stereo-slot"){if(value.size()!=1||value[0]<'0'||value[0]>'7')return 2;stereoSlot=value[0]-'0';}
      else if(option=="--stereo-light"){if(value!="directional"&&value!="spot")return 2;stereoDirectional=value=="directional";}
      else if(option=="--stereo-camera"){if(value!="perspective"&&value!="orthographic")return 2;stereoOrthographic=value=="orthographic";}
      else if(option=="--stereo-raster-kind"){if(value!="both"&&value!="image"&&value!="marker")return 2;stereoRasterKind=value=="both"?0:value=="image"?1:2;}
      else if(value=="adjust")stereoMapping=SoCamera::ADJUST_CAMERA;
      else if(value=="leave")stereoMapping=SoCamera::LEAVE_ALONE;
      else if(value=="crop")stereoMapping=SoCamera::CROP_VIEWPORT_NO_FRAME;
      else if(value=="fill")stereoMapping=SoCamera::CROP_VIEWPORT_FILL_FRAME;
      else if(value=="line")stereoMapping=SoCamera::CROP_VIEWPORT_LINE_FRAME;
      else return 2;
    }
    else if(std::string(argv[i])=="--near-slots") scope=16;
    else if(std::string(argv[i])=="--near-slots-raster") scope=17;
    else if(std::string(argv[i])=="--near-slots-state") scope=18;
    else if(std::string(argv[i])=="--near-coverage") scope=19;
    else if(std::string(argv[i])=="--near-crop") scope=20;
    else if(std::string(argv[i])=="--near-scissor") scope=21;
    else if(std::string(argv[i])=="--restore-rules") scope=22;
    else if(std::string(argv[i])=="--samples") {
      if(++i==argc) return 2;
      const std::string value=argv[i];
      if(value=="1") cameraSamples=1;
      else if(value=="2") cameraSamples=2;
      else if(value=="4") cameraSamples=4;
      else return 2;
    }
    else if(std::string(argv[i])=="--near-general") nearGeneral=true;
    else if(std::string(argv[i])=="--near-maps") {
      if(++i==argc) return 2;
      const std::string value=argv[i];
      if(value=="4") nearMapCount=4;
      else if(value=="8") nearMapCount=8;
      else return 2;
    }
    else if(std::string(argv[i])=="--crop-size") {
      if(++i==argc) return 2;
      const std::string value=argv[i];
      if(value=="64") cropExtent=64;
      else if(value=="256") cropExtent=256;
      else if(value=="512") cropExtent=512;
      else return 2;
    } else if(std::string(argv[i])=="--crop-executor") {
      if(++i==argc) return 2;
      const std::string value=argv[i];
      if(value=="fast") cropExecutor=0;
      else if(value=="general") cropExecutor=1;
      else if(value=="bump") cropExecutor=2;
      else return 2;
    } else return 1;
  }
  if(nativeOnly && (scope==2 || scope==5 || scope==6 || scope==7 || scope==8 ||
      scope==9 || scope==10 || scope==11))
    return 1;
  if((nativeOnly && scope>=12) || (cropExecutor==2 && canonical) ||
     ((cropExecutor || cropExtent) && scope<12)) return 2;
  if((cameraSamples!=1 || cameraPortableSampling) && scope<12)return 2;
  if((nearMapCount || nearGeneral) && scope<16) return 2;
  if(scope>=16 && (cropExecutor || cropExtent || nativeOnly)) return 2;
  if(stereoOptions && (scope<30||scope>37))return 2;
  if(scope>=30 && scope<=37){if(nearMapCount)stereoMaps=nearMapCount;
    if(stereoSlot>=stereoMaps)return 2;
    if(scope<=33 && cameraSamples!=1)return 2;
    if(scope>=34 && (cameraSamples==1 || canonical))return 2;}
  SoDB::init();CoinRenderAction::initClass();
  if(scope==22) return nearRestorationRules()?0:1;
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      &caps,sizeof(caps))!=0 || !caps.gpu_available) return 77;
  return run(canonical,scope,nativeOnly)?0:1;
}
