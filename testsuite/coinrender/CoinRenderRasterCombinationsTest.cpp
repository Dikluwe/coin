#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoIndexedMarkerSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoQuadMesh.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#ifdef COIN_RASTER_X11
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>
#include <thread>
#endif
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int side=128;
bool check(bool good,const std::string & message) {
  if(!good) std::cerr << "raster_combinations: " << message << '\n';
  return good;
}
enum Kind { TEXT=0,IMAGE=1,MARKER=2,INDEXED_MARKER=3 };
const char * kindName(Kind kind) {
  switch(kind) {
  case TEXT:return "Text2";case IMAGE:return "Image";
  case MARKER:return "MarkerSet";case INDEXED_MARKER:return "IndexedMarkerSet";
  }
  return "unknown";
}
void face(SoGroup * parent,float left,float bottom,float right,float top,
          float z,const SbColor & color) {
  auto * branch=new SoSeparator;parent->addChild(branch);
  auto * material=new SoMaterial;material->diffuseColor.setValue(color);
  branch->addChild(material);
  auto * points=new SoCoordinate3;
  const SbVec3f p[]={SbVec3f(left,bottom,z),SbVec3f(right,bottom,z),
    SbVec3f(right,top,z),SbVec3f(left,top,z)};
  points->point.setValues(0,4,p);branch->addChild(points);
  auto * shape=new SoFaceSet;shape->numVertices.set1Value(0,4);
  branch->addChild(shape);
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoGroup * group=nullptr;
  SoGroup * receiver=nullptr;
  SoShadowGroup * shadowGroup=nullptr;
  SoOrthographicCamera * camera=nullptr;
  SoSwitch * rasterSwitch=new SoSwitch;
  SoShadowStyle * rasterStyle=nullptr;
  SoDepthBuffer * rasterDepth=nullptr;
  SoMaterial * rasterMaterial=nullptr;
  SoImage * image=nullptr;
  SoMarkerSet * marker=nullptr;
  SoIndexedMarkerSet * indexedMarker=nullptr;
  SoText2 * text=nullptr;
  SoFont * font=nullptr;
  SoCoordinate3 * markerCoordinates=nullptr;
  Kind kind;
  bool shadow;
  Scene(Kind kind,bool shadow,bool directional,int maps,int style,bool phong,
        bool annotation=false,float precision=.125f,
        bool rasterAnnotation=false,bool lightOn=true):kind(kind),shadow(shadow) {
    root->ref();
    camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,10);camera->height=4;
    camera->nearDistance=1;camera->farDistance=30;root->addChild(camera);
    auto * lighting=new SoLightModel;
    // Shadow receivers and control geometry always use a valid PHONG profile.
    // The raster node exercises its own BASE_COLOR/PHONG state locally.
    lighting->model=SoLightModel::PHONG;
    root->addChild(lighting);
    if(shadow) {
      auto * g=new SoShadowGroup;g->quality=.2f;g->precision=precision;
      shadowGroup=g;
      group=g;root->addChild(g);
      for(int i=0;i<maps;++i) {
        if(directional) {
          auto * light=new SoShadowDirectionalLight;
          light->direction.setValue(-.2f,-.3f,-1);
          light->intensity=.7f/maps;group->addChild(light);
          light->on=lightOn;
        } else {
          auto * light=new SoShadowSpotLight;
          light->location.setValue(1.5f,2,6);
          light->direction.setValue(-.2f,-.3f,-1);
          light->cutOffAngle=1.1f;
          light->intensity=.7f/maps;group->addChild(light);
          light->on=lightOn;
        }
      }
    } else {
      group=new SoSeparator;root->addChild(group);
    }
    receiver=new SoSeparator;group->addChild(receiver);
    if(shadow) {
      auto * s=new SoShadowStyle;s->style=SoShadowStyle::SHADOWED;
      receiver->addChild(s);
    }
    face(receiver,-1.9f,-1.9f,1.9f,1.9f,-1,SbColor(.22f,.24f,.27f));
    auto * caster=new SoSeparator;group->addChild(caster);
    if(shadow) {
      auto * s=new SoShadowStyle;s->style=SoShadowStyle::CASTS_SHADOW;
      caster->addChild(s);
    }
    face(caster,-1.5f,-1.1f,-.8f,1.1f,.4f,SbColor(.8f,.6f,.2f));
    if(rasterAnnotation) {
      auto * rasterLayer=new SoAnnotation;
      group->addChild(rasterLayer);
      rasterLayer->addChild(rasterSwitch);
    } else group->addChild(rasterSwitch);
    rasterSwitch->whichChild=SO_SWITCH_ALL;
    auto * raster=new SoSeparator;rasterSwitch->addChild(raster);
    auto * rasterLighting=new SoLightModel;
    rasterLighting->model=phong ? SoLightModel::PHONG : SoLightModel::BASE_COLOR;
    raster->addChild(rasterLighting);
    if(shadow) {
      rasterStyle=new SoShadowStyle;
      rasterStyle->style=style;raster->addChild(rasterStyle);
    }
    rasterDepth=new SoDepthBuffer;
    rasterDepth->function=SoDepthBuffer::LEQUAL;
    raster->addChild(rasterDepth);
    rasterMaterial=new SoMaterial;
    rasterMaterial->diffuseColor.setValue(.9f,.18f,.12f);
    rasterMaterial->transparency=.4f;raster->addChild(rasterMaterial);
    if(kind==TEXT || kind==IMAGE) {
      auto * move=new SoTranslation;move->translation.setValue(0,0,1.2f);
      raster->addChild(move);
    }
    switch(kind) {
    case TEXT: {
      font=new SoFont;font->name="defaultFont";font->size=22;
      raster->addChild(font);
      text=new SoText2;text->string.setValue("I");raster->addChild(text);
      break;
    }
    case IMAGE: {
      image=new SoImage;
      image->horAlignment=SoImage::CENTER;
      image->vertAlignment=SoImage::HALF;
      const unsigned char pixels[]={
        255,20,10,255, 255,20,10,0, 255,20,10,255,
        255,20,10,0,   255,20,10,255,255,20,10,0,
        255,20,10,255, 255,20,10,0, 255,20,10,255};
      image->image.setValue(SbVec2s(3,3),4,pixels);
      raster->addChild(image);break;
    }
    case MARKER:
    case INDEXED_MARKER: {
      markerCoordinates=new SoCoordinate3;
      markerCoordinates->point.setValue(0,0,1.2f);
      raster->addChild(markerCoordinates);
      if(kind==MARKER) {
        marker=new SoMarkerSet;
        marker->markerIndex=SoMarkerSet::CROSS_9_9;
        raster->addChild(marker);
      } else {
        indexedMarker=new SoIndexedMarkerSet;
        indexedMarker->coordIndex=0;
        indexedMarker->markerIndex=SoMarkerSet::CROSS_9_9;
        raster->addChild(indexedMarker);
      }
      break;
    }
    }
    // The sibling tests whether raster-local material/depth/shadow state
    // leaks after the active group. It lies away from the central bitmap.
    auto * sibling=annotation ? static_cast<SoGroup *>(new SoAnnotation) :
      static_cast<SoGroup *>(new SoSeparator);
    root->addChild(sibling);
    face(sibling,1.2f,-.2f,1.6f,.2f,.8f,SbColor(.08f,.85f,.18f));
  }
  ~Scene() {root->unref();}
};
void quadReceiver(Scene & scene) {
  // A two-by-two face mesh, with nine authored positions and enough
  // face-bound values for CoinGL's terminal provoking access. This exercises
  // the precise QuadMesh fan and flat face attribution behind the bitmap.
  scene.receiver->removeChild(scene.receiver->getNumChildren()-1);
  auto * branch=new SoSeparator;scene.receiver->addChild(branch);
  auto * material=new SoMaterial;
  const SbColor colors[]={
    SbColor(.31f,.22f,.35f),SbColor(.18f,.42f,.27f),
    SbColor(.42f,.19f,.22f),SbColor(.25f,.31f,.49f),
    SbColor(.34f,.27f,.19f),SbColor(.20f,.38f,.41f),
    SbColor(.39f,.24f,.32f),SbColor(.27f,.36f,.23f),
    SbColor(.23f,.29f,.44f)};
  material->diffuseColor.setValues(0,9,colors);branch->addChild(material);
  auto * materialBinding=new SoMaterialBinding;
  materialBinding->value=SoMaterialBinding::PER_FACE;
  branch->addChild(materialBinding);
  auto * normals=new SoNormal;
  const SbVec3f faceNormals[]={
    SbVec3f(.08f,.12f,1),SbVec3f(-.12f,.05f,1),
    SbVec3f(.15f,-.08f,1),SbVec3f(-.07f,-.14f,1),
    SbVec3f(.04f,.09f,1),SbVec3f(-.11f,.13f,1),
    SbVec3f(.10f,-.13f,1),SbVec3f(-.06f,-.04f,1),
    SbVec3f(.13f,.07f,1)};
  normals->vector.setValues(0,9,faceNormals);branch->addChild(normals);
  auto * normalBinding=new SoNormalBinding;
  normalBinding->value=SoNormalBinding::PER_FACE;
  branch->addChild(normalBinding);
  auto * coordinates=new SoCoordinate3;
  for(int y=0;y<3;++y) for(int x=0;x<3;++x)
    coordinates->point.set1Value(y*3+x,
      SbVec3f(1.9f*(x-1),1.9f*(y-1),
        -1.f+.04f*x-.07f*y+(x==1 && y==1 ? .12f : 0)));
  branch->addChild(coordinates);
  auto * mesh=new SoQuadMesh;
  mesh->verticesPerRow=3;mesh->verticesPerColumn=3;
  branch->addChild(mesh);
}
struct Frame {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial=0;
};
struct Harness {
  std::unique_ptr<CoinRenderTarget> gpu,cpu;
  CoinRenderAction action{SbViewportRegion(side,side)};
  CoinRenderAction cpuAction{SbViewportRegion(side,side)};
  SoOffscreenRenderer gl{SbViewportRegion(side,side)};
  bool gpuAvailable=false;
  explicit Harness(bool canonical) {
    std::string diagnostic;
    CoinRenderOptions options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
    if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
    gpu.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    cpu.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    if(!gpu || !cpu) return;
    gpuAvailable=gpu->setDepthReadbackEnabled(TRUE);
    cpu->setDepthReadbackEnabled(TRUE);
    cpu->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    action.setRenderTarget(gpu.get());cpuAction.setRenderTarget(cpu.get());
    action.setBackgroundColor(SbColor4f(0,0,0,1));
    cpuAction.setBackgroundColor(SbColor4f(0,0,0,1));
    gl.setComponents(SoOffscreenRenderer::RGB);
    gl.setBackgroundColor(SbColor(0,0,0));
    std::cout << "raster_combinations_effective_raster GPU="
              << int(gpu->getOptions().rasterizationPolicy)
              << " CPU=" << int(cpu->getOptions().rasterizationPolicy)
              << " diagnostic=" << diagnostic << '\n';
  }
  void mode(int value) {
    action.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(value));
    cpuAction.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(value));
    gl.getGLRenderAction()->setTransparencyType(
      static_cast<SoGLRenderAction::TransparencyType>(value));
  }
  bool render(SoNode * node,Frame & out) {
    action.apply(node);gpu->readbackRGBA(out.rgba);
    gpu->readbackDepth(out.depth);
    out.serial=gpu->getLastSubmissionSerial();
    return action.getLastStatus()==CoinRenderAction::SUCCESS &&
      out.rgba.size()==size_t(side*side*4) &&
      out.depth.size()==size_t(side*side);
  }
  bool renderCpu(SoNode * node,Frame & out) {
    cpuAction.apply(node);cpu->readbackRGBA(out.rgba);
    cpu->readbackDepth(out.depth);
    out.serial=cpu->getLastSubmissionSerial();
    return cpuAction.getLastStatus()==CoinRenderAction::SUCCESS &&
      out.rgba.size()==size_t(side*side*4) &&
      out.depth.size()==size_t(side*side);
  }
  bool reference(SoNode * node,std::vector<uint8_t> & rgb) {
    if(!gl.render(node) || !gl.getBuffer()) return false;
    rgb.resize(size_t(side*side*3));
    for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
      const size_t from=size_t((side-1-y)*side+x)*3;
      const size_t to=size_t(y*side+x)*3;
      for(int c=0;c<3;++c) rgb[to+c]=gl.getBuffer()[from+c];
    }
    return true;
  }
};
struct Effect {
  size_t pixels=0,centerPixels=0,holes=0,outside=0;
  int maxError=0;
  double meanError=0;
};
bool exactFrame(const Frame & left,const Frame & right) {
  return left.rgba==right.rgba && left.depth==right.depth &&
    left.serial==right.serial;
}
int rgbMax(const Frame & actual,const std::vector<uint8_t> & native) {
  if(actual.rgba.size()!=size_t(side*side*4) ||
     native.size()!=size_t(side*side*3)) return 255;
  int result=0;
  for(size_t p=0;p<size_t(side*side);++p)
    for(int c=0;c<3;++c)
      result=std::max(result,
        std::abs(int(actual.rgba[p*4+c])-int(native[p*3+c])));
  return result;
}
bool imageMask(const Frame & shown,const Frame & bare,
               const std::vector<uint8_t> & native,
               const std::vector<uint8_t> & nativeBare,
               const std::string & label) {
  std::array<unsigned,9> gpuMask{},glMask{};
  // The authored 3x3 image uses a five-texel checker. Search only the
  // central raster footprint, then compare its dimensions and bitmap mask;
  // this is independent of CoinRender and of the CoinGL pixel colors.
  int minx=side,miny=side,maxx=-1,maxy=-1;
  for(int y=48;y<=80;++y) for(int x=48;x<=80;++x) {
    const size_t p=size_t(y*side+x);
    bool nativeChanged=false;
    for(int c=0;c<3;++c)
      nativeChanged|=std::abs(int(native[p*3+c])-int(nativeBare[p*3+c]))>3;
    if(nativeChanged) {
      minx=std::min(minx,x);maxx=std::max(maxx,x);
      miny=std::min(miny,y);maxy=std::max(maxy,y);
    }
  }
  if(maxx<minx || maxy<miny) return check(false,label+" native image empty");
  if(maxx-minx!=2 || maxy-miny!=2)
    return check(false,label+" native image is not 3x3 bitmap");
  for(int y=0;y<3;++y) for(int x=0;x<3;++x) {
    const size_t p=size_t(miny+y)*side+minx+x;
    bool gpuChanged=false,glChanged=false;
    for(int c=0;c<3;++c) {
      gpuChanged|=std::abs(int(shown.rgba[p*4+c])-int(bare.rgba[p*4+c]))>3;
      glChanged|=std::abs(int(native[p*3+c])-int(nativeBare[p*3+c]))>3;
    }
    gpuMask[size_t(y*3+x)]=gpuChanged;
    glMask[size_t(y*3+x)]=glChanged;
  }
  const std::array<unsigned,9> checker{{1,0,1,0,1,0,1,0,1}};
  std::cout << "raster_combinations_image_mask " << label << " GPU=";
  for(unsigned value:gpuMask) std::cout << value;
  std::cout << " CoinGL=";
  for(unsigned value:glMask) std::cout << value;
  std::cout << '\n';
  return check(gpuMask==checker && glMask==checker,
    label+" must preserve all transparent checker holes");
}
Effect compare(const Frame & actual,const Frame & bare,
               const std::vector<uint8_t> & native,
               const std::vector<uint8_t> & nativeBare) {
  Effect result;
  uint64_t error=0;
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    const size_t p=size_t(y*side+x),a=p*4,n=p*3;
    bool effect=false,referenceEffect=false;
    for(int c=0;c<3;++c) {
      effect|=std::abs(int(actual.rgba[a+c])-int(bare.rgba[a+c]))>3;
      referenceEffect|=std::abs(int(native[n+c])-int(nativeBare[n+c]))>3;
    }
    result.pixels+=effect;
    if(x>=48 && x<=80 && y>=48 && y<=80) result.centerPixels+=effect;
    if(x>=56 && x<=72 && y>=56 && y<=72) result.holes+=!effect;
    if(x<32 || x>96 || y<32 || y>96) result.outside+=effect;
    if(effect || referenceEffect) {
      for(int c=0;c<3;++c) {
        const int delta=std::abs(int(actual.rgba[a+c])-int(native[n+c]));
        result.maxError=std::max(result.maxError,delta);error+=delta;
      }
    }
  }
  result.meanError=result.pixels ? double(error)/(3*result.pixels) : 0;
  return result;
}
bool exercise(Harness & harness,Kind kind,bool shadow,bool directional,int maps,
              int style,bool phong,int mode,bool annotation,
              bool depthTest=true,bool depthWrite=true,
              bool rasterAnnotation=false,bool lightOn=true,
              bool ordinaryNativeReference=false,bool useQuadReceiver=false) {
  const bool realMaps=shadow && maps>0 && lightOn;
  Scene scene(kind,shadow,directional,maps,style,phong,annotation,
    .125f,rasterAnnotation,lightOn);
  if(useQuadReceiver) quadReceiver(scene);
  scene.rasterDepth->test=depthTest;
  scene.rasterDepth->write=depthWrite;
  harness.mode(mode);
  const std::string label=std::string(kindName(kind))+"/shadow="+
    std::to_string(shadow)+"/directional="+std::to_string(directional)+
    "/maps="+std::to_string(maps)+"/style="+std::to_string(style)+
    "/light_on="+std::to_string(lightOn)+
    "/ordinary_native_reference="+std::to_string(ordinaryNativeReference)+
    "/quad_receiver="+std::to_string(useQuadReceiver)+
    "/phong="+std::to_string(phong)+"/mode="+std::to_string(mode)+
    "/annotation="+std::to_string(annotation)+
    "/raster_annotation="+std::to_string(rasterAnnotation)+
    "/depth_test="+std::to_string(depthTest)+
    "/depth_write="+std::to_string(depthWrite);
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  Frame bare;
  std::vector<uint8_t> nativeBare;
  auto reference=[&](std::vector<uint8_t> & output) {
    if(ordinaryNativeReference) scene.shadowGroup->isActive=FALSE;
    const bool rendered=harness.reference(scene.root,output);
    if(ordinaryNativeReference) scene.shadowGroup->isActive=TRUE;
    return rendered;
  };
  const bool barePublished=harness.render(scene.root,bare);
  if(!barePublished)
    std::cerr << "raster_combinations_bare " << label
              << " status=" << int(harness.action.getLastStatus())
              << " diagnostic=" << harness.action.getLastError().getString() << '\n';
  bool ok=check(barePublished,label+" bare GPU render");
  ok &= check(reference(nativeBare),label+" bare CoinGL render");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  Frame shown;
  std::vector<uint8_t> native;
  ok &= check(harness.render(scene.root,shown),label+" raster GPU render");
  ok &= check(reference(native),label+" raster CoinGL render");
  if(!ok) {
    std::cerr << label << " GPU diagnostic=" << harness.action.getLastError().getString() << '\n';
    return false;
  }
  const Effect effect=compare(shown,bare,native,nativeBare);
  size_t nativeEffect=0,nativeCenter=0;
  for(int y=0;y<side;++y) for(int x=0;x<side;++x)
    for(int c=0;c<3;++c) {
      const size_t p=(size_t(y)*side+x)*3+c;
      const bool changed=std::abs(int(native[p])-int(nativeBare[p]))>3;
      nativeEffect+=changed;
      if(x>=48 && x<=80 && y>=48 && y<=80) nativeCenter+=changed;
    }
  std::cout << "raster_combinations " << label
            << " GPU_effect=" << effect.pixels << " GPU_center=" << effect.centerPixels
            << " CoinGL_channels_changed=" << nativeEffect
            << " CoinGL_center=" << nativeCenter << " holes=" << effect.holes
            << " outside=" << effect.outside
            << " RGB_max=" << effect.maxError
            << " RGB_mean=" << effect.meanError << '\n';
  // The bitmap must remain sparse and bounded even when CoinGL accidentally
  // drops it in sorted-triangle mode. In that case the native defect remains
  // a failing gate; an empty render cannot count as parity.
  ok &= check(effect.centerPixels>=3 && nativeCenter>=9,
    label+" raster coverage must be nonempty in both executors");
  ok &= check(effect.holes>0 && (realMaps || effect.outside==0),
    label+" raster must retain holes and remain bounded without shadow-map effects");
  ok &= check(effect.maxError<=3,label+" CoinGL RGB and coverage");
  ok &= check(shown.serial>bare.serial,label+" raster publication serial");
  if(kind==IMAGE && !realMaps)
    ok &= imageMask(shown,bare,native,nativeBare,label);
  // A malformed depth range is rejected before the previously published
  // image, depth and ticket can change. Restoring it must reproduce the
  // accepted frame exactly, including all raster mask holes.
  Frame rejected,recovered;
  scene.rasterDepth->range.setValue(0.0f,
    std::numeric_limits<float>::quiet_NaN());
  harness.action.apply(scene.root);
  harness.gpu->readbackRGBA(rejected.rgba);
  harness.gpu->readbackDepth(rejected.depth);
  rejected.serial=harness.gpu->getLastSubmissionSerial();
  const CoinRenderAction::Status rejectedStatus=harness.action.getLastStatus();
  scene.rasterDepth->range.setValue(0.0f,1.0f);
  const bool recoveredOk=harness.render(scene.root,recovered);
  std::cout << "raster_combinations_transaction " << label
            << " rejected_status=" << int(rejectedStatus)
            << " rollback=" << exactFrame(rejected,shown)
            << " recovered=" << recoveredOk << '\n';
  ok &= check(rejectedStatus!=CoinRenderAction::SUCCESS &&
    exactFrame(rejected,shown),label+" invalid depth rollback");
  ok &= check(recoveredOk && recovered.rgba==shown.rgba &&
    recovered.depth==shown.depth && recovered.serial>shown.serial,
    label+" recovery after invalid depth");
  if(!realMaps) {
    scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
    Frame cpuBare,cpuShown,cpuRejected,cpuRecovered;
    const bool cpuBareOk=harness.renderCpu(scene.root,cpuBare);
    scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
    const bool cpuShownOk=harness.renderCpu(scene.root,cpuShown);
    ok &= check(cpuBareOk && cpuShownOk,
      label+" CPU sorted mode must publish bare and raster frames");
    if(cpuBareOk && cpuShownOk) {
      const Effect cpuEffect=compare(cpuShown,cpuBare,native,nativeBare);
      std::cout << "raster_combinations_cpu " << label
                << " effect=" << cpuEffect.pixels
                << " center=" << cpuEffect.centerPixels
                << " RGB_max=" << cpuEffect.maxError
                << " bare_RGB_max=" << rgbMax(cpuBare,nativeBare)
                << " shown_RGB_max=" << rgbMax(cpuShown,native)
                << " bare_serial=" << cpuBare.serial
                << " shown_serial=" << cpuShown.serial << '\n';
      ok &= check(cpuEffect.centerPixels>=3 && cpuEffect.maxError<=3 &&
        rgbMax(cpuBare,nativeBare)<=3 && rgbMax(cpuShown,native)<=3 &&
        cpuShown.serial>cpuBare.serial,
        label+" CPU sorted raster must match CoinGL and advance serial");
      if(kind==IMAGE)
        ok &= imageMask(cpuShown,cpuBare,native,nativeBare,
          label+"/CPU");
      scene.rasterDepth->range.setValue(
        std::numeric_limits<float>::quiet_NaN(),1);
      harness.cpuAction.apply(scene.root);
      harness.cpu->readbackRGBA(cpuRejected.rgba);
      harness.cpu->readbackDepth(cpuRejected.depth);
      cpuRejected.serial=harness.cpu->getLastSubmissionSerial();
      const CoinRenderAction::Status cpuReject=harness.cpuAction.getLastStatus();
      scene.rasterDepth->range.setValue(0,1);
      const bool cpuRecovery=harness.renderCpu(scene.root,cpuRecovered);
      std::cout << "raster_combinations_cpu_transaction " << label
                << " rejected_status=" << int(cpuReject)
                << " rollback=" << exactFrame(cpuRejected,cpuShown)
                << " recovered=" << cpuRecovery << '\n';
      ok &= check(cpuReject!=CoinRenderAction::SUCCESS &&
        exactFrame(cpuRejected,cpuShown),
        label+" CPU invalid profile must preserve RGBA/depth/serial");
      ok &= check(cpuRecovery && cpuRecovered.rgba==cpuShown.rgba &&
        cpuRecovered.depth==cpuShown.depth &&
        cpuRecovered.serial>cpuShown.serial,
        label+" CPU recovery after invalid profile");
    }
  }
  if(realMaps) {
    Frame cpuStable,cpuRejected;
    scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
    // A plain peer frame is published before the unsupported real map.
    Scene peer(kind,false,false,0,0,false);
    harness.mode(CoinRenderAction::BLEND);
    const bool peerReady=harness.renderCpu(peer.root,cpuStable);
    harness.mode(mode);
    scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
    harness.cpuAction.apply(scene.root);
    harness.cpu->readbackRGBA(cpuRejected.rgba);
    harness.cpu->readbackDepth(cpuRejected.depth);
    cpuRejected.serial=harness.cpu->getLastSubmissionSerial();
    ok &= check(peerReady &&
      harness.cpuAction.getLastStatus()==CoinRenderAction::UNSUPPORTED &&
      cpuRejected.rgba==cpuStable.rgba &&
      cpuRejected.depth==cpuStable.depth &&
      cpuRejected.serial==cpuStable.serial,
      label+" CPU real shadow maps must reject atomically");
  }
  return ok;
}
bool shadowStyleContrast(Harness & harness,Kind kind,bool directional,int mode) {
  // CoinGL supplies the map-camera behavior, while a separate change mask
  // proves that CASTS_SHADOW is not silently treated as NO_SHADOWING.
  Scene no(kind,true,directional,1,SoShadowStyle::NO_SHADOWING,false,false,.05f);
  Scene cast(kind,true,directional,1,SoShadowStyle::CASTS_SHADOW,false,false,.05f);
  harness.mode(mode);
  Frame gpuNo,gpuCast;
  std::vector<uint8_t> glNo,glCast;
  const std::string label=std::string(kindName(kind))+
    "/style-contrast/directional="+std::to_string(directional)+
    "/mode="+std::to_string(mode);
  bool ok=check(harness.render(no.root,gpuNo),label+" no-shadow GPU");
  ok &= check(harness.reference(no.root,glNo),label+" no-shadow CoinGL");
  ok &= check(harness.render(cast.root,gpuCast),label+" cast GPU");
  ok &= check(harness.reference(cast.root,glCast),label+" cast CoinGL");
  if(!ok) return false;
  size_t gpuOutside=0,nativeOutside=0,maskDisagreement=0;
  int maxDelta=0;
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    if(x>=48 && x<=80 && y>=48 && y<=80) continue;
    const size_t p=size_t(y*side+x);
    bool gpuChanged=false,nativeChanged=false;
    for(int c=0;c<3;++c) {
      const int gpu=std::abs(int(gpuNo.rgba[p*4+c])-int(gpuCast.rgba[p*4+c]));
      const int native=std::abs(int(glNo[p*3+c])-int(glCast[p*3+c]));
      gpuChanged|=gpu>3;nativeChanged|=native>3;
      maxDelta=std::max(maxDelta,std::abs(gpu-native));
    }
    gpuOutside+=gpuChanged;nativeOutside+=nativeChanged;
    maskDisagreement+=gpuChanged!=nativeChanged;
  }
  std::cout << "raster_combinations_style_contrast " << label
            << " CoinGL_caster_outside=" << nativeOutside
            << " GPU_caster_outside=" << gpuOutside
            << " mask_disagreement=" << maskDisagreement
            << " max_effect_delta=" << maxDelta << '\n';
  // On a driver where this fixture has no native caster effect, record the
  // limitation rather than manufacturing a positive shadow or a PASS claim.
  if(nativeOutside==0) {
    std::cout << "raster_combinations_native_caster_not_discriminating "
              << label << '\n';
  }
  return check(maskDisagreement==0 && maxDelta<=3,
    label+" caster effect must match CoinGL map-camera mask and color");
}
float cameraDepth(float worldZ,float nearRange=0,float farRange=1) {
  // Orthographic camera at z=10, near=1, far=30: the near and far world
  // planes are z=9 and z=-20. glDepthRange maps normalized eye depth.
  return nearRange+(farRange-nearRange)*(9.f-worldZ)/29.f;
}
bool depthWitness(Harness & harness,int mode,bool front,bool test,bool write,
                  bool annotation,float rangeNear,float rangeFar) {
  Scene scene(IMAGE,false,false,0,0,false,false,.125f,annotation);
  scene.rasterMaterial->transparency=0;
  scene.rasterDepth->test=test;
  scene.rasterDepth->write=write;
  scene.rasterDepth->range.setValue(rangeNear,rangeFar);
  const float occluderZ=front ? 2.f : 0.f;
  auto * occluder=new SoSeparator;
  face(occluder,-.3f,-.3f,.3f,.3f,occluderZ,
    SbColor(.05f,.3f,.85f));
  scene.group->insertChild(occluder,2); // after receiver/caster, before raster
  harness.mode(mode);
  const std::string label=std::string("depth/front=")+std::to_string(front)+
    "/test="+std::to_string(test)+"/write="+std::to_string(write)+
    "/annotation="+std::to_string(annotation)+
    "/range="+std::to_string(rangeNear)+","+std::to_string(rangeFar)+
    "/mode="+std::to_string(mode);
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  Frame bare,cpuBare,shown,cpuShown;
  std::vector<uint8_t> glBare,glShown;
  bool ok=check(harness.render(scene.root,bare),label+" bare GPU");
  ok &= check(harness.renderCpu(scene.root,cpuBare),label+" bare CPU");
  ok &= check(harness.reference(scene.root,glBare),label+" bare CoinGL");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  ok &= check(harness.render(scene.root,shown),label+" shown GPU");
  ok &= check(harness.renderCpu(scene.root,cpuShown),label+" shown CPU");
  ok &= check(harness.reference(scene.root,glShown),label+" shown CoinGL");
  scene.rasterDepth->test=FALSE;
  std::vector<uint8_t> glCalibration;
  ok &= check(harness.reference(scene.root,glCalibration),
    label+" native bitmap center calibration");
  scene.rasterDepth->test=test;
  if(!ok) {
    std::cerr << label << " GPU=" << int(harness.action.getLastStatus())
              << ":" << harness.action.getLastError().getString()
              << " CPU=" << int(harness.cpuAction.getLastStatus())
              << ":" << harness.cpuAction.getLastError().getString() << '\n';
    return false;
  }
  // The image center is the central authored opaque texel. Calibrate bitmap
  // alignment with native depth testing temporarily disabled so the front
  // oclusor cannot hide the footprint.
  int minx=side,miny=side,maxx=-1,maxy=-1;
  for(int y=48;y<=80;++y) for(int x=48;x<=80;++x) {
    const size_t p=(size_t(y)*side+x)*3;
    bool imagePixel=false;
    for(int c=0;c<3;++c)
      imagePixel|=std::abs(int(glCalibration[p+c])-int(glBare[p+c]))>3;
    if(imagePixel) {
      minx=std::min(minx,x);maxx=std::max(maxx,x);
      miny=std::min(miny,y);maxy=std::max(maxy,y);
    }
  }
  ok &= check(maxx-minx==2 && maxy-miny==2,
    label+" calibrated native raster bitmap must remain 3x3");
  if(!ok) return false;
  const int witnessX=minx+1;
  const int witnessY=miny+1;
  const size_t pixel=size_t(witnessY)*side+witnessX;
  bool gpuChanged=false,cpuChanged=false,glChanged=false;
  for(int c=0;c<3;++c) {
    gpuChanged|=std::abs(int(shown.rgba[pixel*4+c])-int(bare.rgba[pixel*4+c]))>3;
    cpuChanged|=std::abs(int(cpuShown.rgba[pixel*4+c])-int(cpuBare.rgba[pixel*4+c]))>3;
    glChanged|=std::abs(int(glShown[pixel*3+c])-int(glBare[pixel*3+c]))>3;
  }
  const bool shouldShow=!front || !test;
  // The CoinRender composition contract gives the explicit child depth state
  // precedence even inside Annotation. Legacy CoinGL may leave depth testing
  // disabled because Annotation changes raw GL state without updating the
  // depth element; RGB parity is still checked separately above.
  const float expectedDepth=(!front && test && write) ?
    cameraDepth(1.2f,rangeNear,rangeFar) : cameraDepth(occluderZ);
  const float gpuDepth=shown.depth[pixel],cpuDepth=cpuShown.depth[pixel];
  std::cout << "raster_combinations_depth " << label
            << " xy=" << witnessX << ',' << witnessY
            << " GPU_changed=" << gpuChanged << " CPU_changed=" << cpuChanged
            << " CoinGL_changed=" << glChanged
            << " GPU_depth=" << gpuDepth << " CPU_depth=" << cpuDepth
            << " expected_depth=" << expectedDepth
            << " GPU_RGB_max=" << rgbMax(shown,glShown)
            << " CPU_RGB_max=" << rgbMax(cpuShown,glShown) << '\n';
  ok &= check(gpuChanged==shouldShow && cpuChanged==shouldShow &&
    glChanged==shouldShow,label+" image visibility obeys depth test");
  ok &= check(std::isfinite(gpuDepth) && std::isfinite(cpuDepth) &&
    std::abs(gpuDepth-expectedDepth)<=2e-5f &&
    std::abs(cpuDepth-expectedDepth)<=2e-5f,
    label+" numeric depth obeys test/write/range");
  ok &= check(rgbMax(shown,glShown)<=3 && rgbMax(cpuShown,glShown)<=3,
    label+" full RGB agrees with CoinGL");
  ok &= check(shown.serial>bare.serial && cpuShown.serial>cpuBare.serial,
    label+" publication serial");
  return ok;
}
enum ImageCasterProfile { IMAGE_RGB_OPAQUE, IMAGE_RGBA_OPAQUE,
  IMAGE_RGBA_VARIABLE };
int sparseCornerMarker();
void setCasterImage(SoImage * image,ImageCasterProfile profile) {
  const unsigned char rgb[]={
    240,20,10, 240,20,10, 240,20,10,
    240,20,10, 240,20,10, 240,20,10,
    240,20,10, 240,20,10, 240,20,10};
  const unsigned char rgbaOpaque[]={
    240,20,10,255, 240,20,10,255, 240,20,10,255,
    240,20,10,255, 240,20,10,255, 240,20,10,255,
    240,20,10,255, 240,20,10,255, 240,20,10,255};
  const unsigned char rgbaVariable[]={
    240,20,10,255, 240,20,10,0,   240,20,10,255,
    240,20,10,0,   240,20,10,255, 240,20,10,0,
    240,20,10,255, 240,20,10,0,   240,20,10,255};
  if(profile==IMAGE_RGB_OPAQUE)
    image->image.setValue(SbVec2s(3,3),3,rgb);
  else
    image->image.setValue(SbVec2s(3,3),4,
      profile==IMAGE_RGBA_OPAQUE ? rgbaOpaque : rgbaVariable);
}
bool shadowCoreCase(Harness & harness,Kind kind,bool directional,
                    bool hiddenObserver,bool materialTransparent,
                    ImageCasterProfile imageProfile) {
  Scene scene(kind,true,directional,1,SoShadowStyle::CASTS_SHADOW,
    false,false,.125f);
  // The pre-existing geometric caster would contaminate the analytical
  // witness. Keep the receiver and the raster, remove only that control.
  scene.group->removeChild(2);
  if(kind==TEXT) {
    // The default-font "I" has only a handful of thin raster runs in the
    // light map; at this spot cone it can miss every receiver sample. Use
    // multiple broad strokes while retaining an actual glyph bitmap.
    scene.font->size=48;
    scene.text->string.setValue("MW");
  }
  if(kind==MARKER) scene.marker->markerIndex=sparseCornerMarker();
  if(kind==INDEXED_MARKER)
    scene.indexedMarker->markerIndex=sparseCornerMarker();
  if(kind==IMAGE) setCasterImage(scene.image,imageProfile);
  scene.rasterMaterial->transparency=materialTransparent ? .4f : 0.f;
  const float predictedX=directional ? -.44f : -.6875f;
  const float predictedY=directional ? -.66f : -.9166667f;
  if(hiddenObserver) {
    // Clip the raster at z=1.2 at the observer near plane. A narrow camera
    // centred on the shadow ray can see only the hole in a sparse marker.
    // Keep the original image scale so its whole projected shadow is visible.
    scene.camera->position.setValue(0,0,3);
    scene.camera->nearDistance=2.5f;
    scene.camera->farDistance=10.f;
  }
  harness.mode(CoinRenderAction::BLEND);
  const std::string label=std::string("shadow-core/")+kindName(kind)+
    "/directional="+std::to_string(directional)+
    "/hidden="+std::to_string(hiddenObserver)+
    "/material_transparent="+std::to_string(materialTransparent)+
    "/image_profile="+std::to_string(int(imageProfile));
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  Frame bare;
  bool ok=check(harness.render(scene.root,bare),label+" bare receiver");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  Frame shown;
  ok &= check(harness.render(scene.root,shown),label+" raster caster");
  // Only the style changes: this frame contains exactly the same observer
  // bitmap, lighting and geometry, but the raster is absent from the maps.
  scene.rasterStyle->style=SoShadowStyle::NO_SHADOWING;
  Frame noCast;
  ok &= check(harness.render(scene.root,noCast),label+" no-cast control");
  if(!ok) {
    std::cerr << label << " status=" << int(harness.action.getLastStatus())
              << " diagnostic=" << harness.action.getLastError().getString() << '\n';
    return false;
  }
  const float height=4.f;
  const float cameraX=0.f;
  const float cameraY=0.f;
  const int centerX=int(std::floor((predictedX-cameraX)/height*side+side/2.f));
  const int centerY=int(std::floor((cameraY-predictedY)/height*side+side/2.f));
  // Coin's map camera clamps this spotlight's cutOffAngle from 1.1 to .78
  // and uses 2*cutoff as its perspective heightAngle. A map bitmap of N
  // pixels at distance d covers N*d*tan(cutoff)/mapSize around its anchor;
  // the receiver-plane projection adds the light-to-plane/raster ratio.
  // Text2 is origin-aligned, so include its entire authored font advance.
  const float bitmapPixels=kind==TEXT ? 2.f*float(scene.font->size.getValue()) :
    (kind==IMAGE ? 3.f : 33.f);
  const float lightToRaster=std::sqrt(1.5f*1.5f+2.f*2.f+4.8f*4.8f);
  const float lightProjection=(6.f-(-1.f))/(6.f-1.2f);
  const float mapWorldPerPixel=directional ? 4.f/256.f :
    2.f*std::tan(.78f)*lightToRaster/256.f;
  const int radius=int(std::ceil(bitmapPixels*mapWorldPerPixel*
    lightProjection*side/height))+3;
  size_t shadowPixels=0,observerPixels=0,farPixels=0;
  size_t authoredObserverPixels=0;
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    const size_t p=size_t(y*side+x);
    bool changed=false;
    bool authored=false;
    for(int c=0;c<3;++c)
      changed|=std::abs(int(shown.rgba[p*4+c])-int(noCast.rgba[p*4+c]))>3;
    for(int c=0;c<3;++c)
      authored|=std::abs(int(noCast.rgba[p*4+c])-int(bare.rgba[p*4+c]))>3;
    authoredObserverPixels+=authored;
    if(!changed) continue;
    const bool shadowRoi=std::abs(x-centerX)<=radius &&
      std::abs(y-centerY)<=radius;
    const bool observerRoi=std::abs(x-side/2)<=20 &&
      std::abs(y-side/2)<=20 && !hiddenObserver;
    shadowPixels+=shadowRoi;
    observerPixels+=observerRoi;
    farPixels+=!shadowRoi;
  }
  const bool casts=!materialTransparent &&
    (kind!=IMAGE || imageProfile!=IMAGE_RGBA_VARIABLE);
  std::cout << "raster_combinations_shadow_core " << label
            << " analytic_shadow_xy=" << centerX << ',' << centerY
            << " radius=" << radius << " shadow_pixels=" << shadowPixels
            << " observer_pixels=" << observerPixels
            << " far_pixels=" << farPixels
            << " authored_observer_pixels=" << authoredObserverPixels
            << " expected_cast=" << casts << '\n';
  // The target point comes from the ray through the raster anchor at z=1.2
  // to the receiver plane z=-1. Transparent objects must cast no shadow;
  // RGB and fully opaque RGBA images, text and marker bits must cast.
  ok &= check(casts ? shadowPixels>0 : shadowPixels==0,
    label+" analytical caster presence/exclusion at receiver");
  ok &= check(farPixels==0,
    label+" raster must not become a solid screen-camera caster quad");
  if(hiddenObserver)
    ok &= check(noCast.rgba==bare.rgba && noCast.depth==bare.depth,
      label+" near-clipped raster absent from observer");
  else
    ok &= check(authoredObserverPixels>0,
      label+" authored raster bitmap visible at observer");
  ok &= check(shown.serial>bare.serial,label+" caster publication serial");
  return ok;
}
int sparseCornerMarker() {
  static int markerId=-1;
  if(markerId>=0) return markerId;
  // Four isolated 7x7 blocks in a 33x33 bitmap. The 19x19 central hole
  // separates a true bitmap caster from a solid 33x33 billboard caster.
  std::vector<unsigned char> bits(size_t(33*5),0);
  for(int y=0;y<33;++y) for(int x=0;x<33;++x) {
    if((x<=6 || x>=26) && (y<=6 || y>=26))
      bits[size_t(y*5+x/8)]|=static_cast<unsigned char>(0x80U>>(x%8));
  }
  markerId=SoMarkerSet::getNumDefinedMarkers()+50;
  SoMarkerSet::addMarker(markerId,SbVec2s(33,33),bits.data(),FALSE,TRUE);
  return markerId;
}
bool shadowSparseMask(Harness & harness) {
  Scene scene(MARKER,true,true,1,SoShadowStyle::CASTS_SHADOW,
    false,false,.125f);
  scene.group->removeChild(2);
  auto * light=static_cast<SoShadowDirectionalLight *>(scene.group->getChild(0));
  light->direction.setValue(0,0,-1);
  light->intensity=1;
  light->bboxCenter.setValue(0,0,0);
  light->bboxSize.setValue(4,4,8);
  scene.marker->markerIndex=sparseCornerMarker();
  scene.rasterMaterial->transparency=0;
  // Camera near plane at world z=.5 clips the marker at z=1.2, while the
  // receiver at z=-1 and the light's map camera still contain it.
  scene.camera->position.setValue(0,0,3);
  scene.camera->nearDistance=2.5f;
  scene.camera->farDistance=10;
  harness.mode(CoinRenderAction::BLEND);
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  Frame bare,shown;
  bool ok=check(harness.render(scene.root,bare),"sparse-map bare receiver");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  ok &= check(harness.render(scene.root,shown),"sparse-map caster");
  if(!ok) return false;
  auto changed=[&](int cx,int cy,int radius) {
    size_t count=0;
    for(int y=cy-radius;y<=cy+radius;++y)
      for(int x=cx-radius;x<=cx+radius;++x) {
        const size_t p=size_t(y*side+x)*4;
        bool visible=false;
        for(int c=0;c<3;++c)
          visible|=std::abs(int(shown.rgba[p+c])-int(bare.rgba[p+c]))>3;
        count+=visible;
      }
    return count;
  };
  std::array<size_t,4> corner{};
  // This fixture overrides the directional light to (0,0,-1), hence the
  // ray through the marker stays at the framebuffer centre. The map camera
  // fits the observer/bbox intersection [-2,2]x[-2,2]x[-4,.5] with a
  // circumscribed sphere, so
  // its world-space width is sqrt(4²+4²+4.5²). The four 7x7 islands of the
  // 33x33 bitmap are centred 13 map pixels away from the raster origin.
  const float mapWidth=std::sqrt(4.f*4.f+4.f*4.f+4.5f*4.5f);
  const int shadowX=side/2;
  const int shadowY=side/2;
  const int islandOffset=int(std::lround(13.f*mapWidth/256.f*side/4.f));
  const int offsets[][2]={{-1,-1},{1,-1},{-1,1},{1,1}};
  for(size_t i=0;i<corner.size();++i)
    corner[i]=changed(shadowX+islandOffset*offsets[i][0],
      shadowY+islandOffset*offsets[i][1],2);
  const size_t hole=changed(shadowX,shadowY,2);
  size_t total=0;
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    const size_t p=size_t(y*side+x)*4;
    bool visible=false;
    for(int c=0;c<3;++c)
      visible|=std::abs(int(shown.rgba[p+c])-int(bare.rgba[p+c]))>3;
    total+=visible;
  }
  std::cout << "raster_combinations_sparse_map marker=33x33"
            << " projected_center=" << shadowX << ',' << shadowY
            << " projected_corner_offsets=+-" << islandOffset
            << " corner=" << corner[0] << ',' << corner[1] << ','
            << corner[2] << ',' << corner[3]
            << " center_hole=" << hole << " total=" << total
            << " serial=" << bare.serial << "->" << shown.serial << '\n';
  for(size_t count:corner)
    ok &= check(count>0,"each authored sparse marker corner casts a shadow");
  ok &= check(hole==0,"authored central marker hole must not cast a solid quad");
  ok &= check(total>0 && total<400,
    "sparse marker shadow remains bounded to authored bitmap");
  ok &= check(shown.serial>bare.serial,"sparse marker shadow publication");
  return ok;
}
bool shadowReceiverAnchor(Harness & harness,Kind kind,bool directional,
                          bool imageAlphaHoles) {
  const bool phongReceiver=kind==IMAGE;
  Scene scene(kind,true,directional,1,SoShadowStyle::NO_SHADOWING,
    phongReceiver,false,.125f);
  scene.group->removeChild(2);
  scene.rasterMaterial->transparency=0;
  scene.rasterMaterial->specularColor.setValue(0,0,0);
  if(kind==IMAGE) {
    std::vector<unsigned char> pixels(size_t(17*17*(imageAlphaHoles ? 4 : 3)));
    for(int y=0;y<17;++y) for(int x=0;x<17;++x) {
      const int n=imageAlphaHoles ? 4 : 3;
      const size_t at=size_t(y*17+x)*n;
      pixels[at]=235;pixels[at+1]=42;pixels[at+2]=25;
      if(imageAlphaHoles) pixels[at+3]=((x/3+y/3)%2) ? 0 : 255;
    }
    scene.image->image.setValue(SbVec2s(17,17),
      imageAlphaHoles ? 4 : 3,pixels.data());
  } else if(kind==MARKER) {
    scene.marker->markerIndex=sparseCornerMarker();
  } else if(kind==INDEXED_MARKER) {
    scene.indexedMarker->markerIndex=sparseCornerMarker();
  }
  // For z=4, the light-to-raster ray intersects at (.875,1.1667) for the
  // spot and (.56,.84) for the directional light. These small casters lie
  // outside the authored screen bitmap, while their shadow ray hits its
  // anchor (0,0,1.2) exactly.
  const float casterX=directional ? .56f : .875f;
  const float casterY=directional ? .84f : 1.1666667f;
  auto * caster=new SoSeparator;
  auto * casterStyle=new SoShadowStyle;
  casterStyle->style=SoShadowStyle::CASTS_SHADOW;
  caster->addChild(casterStyle);
  auto * move=new SoTranslation;
  move->translation.setValue(casterX,casterY,0);
  caster->addChild(move);
  face(caster,-.12f,-.12f,.12f,.12f,4.f,SbColor(.2f,.2f,.2f));
  scene.group->insertChild(caster,2);
  harness.mode(CoinRenderAction::BLEND);
  const std::string label=std::string("shadow-receiver/")+kindName(kind)+
    "/directional="+std::to_string(directional)+
    "/alpha_holes="+std::to_string(imageAlphaHoles);
  // Validate the ray/occluder fixture with an ordinary PHONG face at the
  // raster anchor. If even this face is not shadowed, the map cannot prove
  // anything about the raster receiver at that anchor.
  auto * anchorPatch=new SoSeparator;
  auto * patchStyle=new SoShadowStyle;
  patchStyle->style=SoShadowStyle::SHADOWED;
  anchorPatch->addChild(patchStyle);
  auto * patchLightModel=new SoLightModel;
  patchLightModel->model=SoLightModel::PHONG;
  anchorPatch->addChild(patchLightModel);
  face(anchorPatch,-.11f,-.11f,.11f,.11f,1.2f,
    SbColor(.9f,.18f,.12f));
  scene.group->insertChild(anchorPatch,3);
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  Frame patchBlocked,patchClear;
  bool ok=check(harness.render(scene.root,patchBlocked),
    label+" geometric receiver blocked");
  move->translation.setValue(casterX+1.5f,casterY,0);
  ok &= check(harness.render(scene.root,patchClear),
    label+" geometric receiver clear");
  scene.group->removeChild(anchorPatch);
  move->translation.setValue(casterX,casterY,0);
  size_t patchDarkened=0;
  if(ok) for(int y=61;y<=67;++y) for(int x=61;x<=67;++x) {
    const size_t at=(size_t(y)*side+x)*4;
    int loss=0;
    for(int c=0;c<3;++c)
      loss+=int(patchClear.rgba[at+c])-int(patchBlocked.rgba[at+c]);
    patchDarkened+=loss>3;
  }
  std::cout << "raster_combinations_geometric_receiver " << label
            << " caster_xy=" << casterX << ',' << casterY
            << " anchor_patch_darkened=" << patchDarkened << '\n';
  ok &= check(patchDarkened>0,
    label+" geometric PHONG patch proves caster blocks raster anchor");
  if(!ok) return false;
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  Frame bare,unshadowed,shadowed,both,clearBare,clear,noCaster;
  ok &= check(harness.render(scene.root,bare),label+" bare");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  ok &= check(harness.render(scene.root,unshadowed),label+" NO_SHADOWING");
  scene.rasterStyle->style=SoShadowStyle::SHADOWED;
  ok &= check(harness.render(scene.root,shadowed),label+" SHADOWED");
  scene.rasterStyle->style=SoShadowStyle::CASTS_SHADOW_AND_SHADOWED;
  ok &= check(harness.render(scene.root,both),label+" BOTH");
  scene.rasterStyle->style=SoShadowStyle::SHADOWED;
  move->translation.setValue(casterX+1.5f,casterY,0);
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  ok &= check(harness.render(scene.root,clearBare),label+" caster-away bare");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  ok &= check(harness.render(scene.root,clear),label+" caster-away");
  casterStyle->style=SoShadowStyle::NO_SHADOWING;
  ok &= check(harness.render(scene.root,noCaster),
    label+" caster disabled with same PHONG receiver");
  casterStyle->style=SoShadowStyle::CASTS_SHADOW;
  if(!ok) {
    std::cerr << label << " status=" << int(harness.action.getLastStatus())
              << " diagnostic=" << harness.action.getLastError().getString() << '\n';
    return false;
  }
  // Find the largest exact RGB class among fully covered authored pixels.
  // Text edges may be antialiased; comparing the dominant class avoids
  // confusing fractional glyph coverage with a nonuniform shadow factor.
  std::map<uint32_t,std::vector<size_t>> classes;
  const Frame & litReference=phongReceiver ? clear : unshadowed;
  const Frame & litBare=phongReceiver ? clearBare : bare;
  size_t authored=0,holes=0;
  size_t changedMask=0,alphaMismatch=0;
  for(int y=32;y<96;++y) for(int x=32;x<96;++x) {
    const size_t p=size_t(y*side+x),at=p*4;
    bool covered=false;
    for(int c=0;c<3;++c)
      covered|=std::abs(int(litReference.rgba[at+c])-int(litBare.rgba[at+c]))>3;
    bool shadedCovered=false;
    for(int c=0;c<3;++c)
      shadedCovered|=std::abs(int(shadowed.rgba[at+c])-int(bare.rgba[at+c]))>3;
    changedMask+=covered!=shadedCovered;
    alphaMismatch+=litReference.rgba[at+3]!=shadowed.rgba[at+3];
    if(!covered) {++holes;continue;}
    ++authored;
    const uint32_t key=(uint32_t(litReference.rgba[at])<<16) |
      (uint32_t(litReference.rgba[at+1])<<8) |
      uint32_t(litReference.rgba[at+2]);
    classes[key].push_back(p);
  }
  std::vector<size_t> dominant;
  for(const auto & entry:classes)
    if(entry.second.size()>dominant.size()) dominant=entry.second;
  size_t darkened=0,bothDarkened=0,cleared=0;
  std::array<int,3> minDelta{{255,255,255}},maxDelta{{-255,-255,-255}};
  for(size_t p:dominant) {
    const size_t at=p*4;
    int loss=0,bothLoss=0,clearError=0;
    for(int c=0;c<3;++c) {
      const int delta=int(litReference.rgba[at+c])-int(shadowed.rgba[at+c]);
      minDelta[size_t(c)]=std::min(minDelta[size_t(c)],delta);
      maxDelta[size_t(c)]=std::max(maxDelta[size_t(c)],delta);
      loss+=delta;
      bothLoss+=int(litReference.rgba[at+c])-int(both.rgba[at+c]);
      clearError=std::max(clearError,
        std::abs(int(clear.rgba[at+c])-int(noCaster.rgba[at+c])));
    }
    darkened+=loss>3;bothDarkened+=bothLoss>3;cleared+=clearError<=3;
  }
  int spread=0;
  for(int c=0;c<3;++c)
    spread=std::max(spread,maxDelta[size_t(c)]-minDelta[size_t(c)]);
  std::cout << "raster_combinations_receiver_anchor " << label
            << " ray_caster_xy=" << casterX << ',' << casterY
            << " authored=" << authored << " holes=" << holes
            << " mask_mismatch=" << changedMask
            << " final_alpha_mismatch=" << alphaMismatch
            << " dominant=" << dominant.size()
            << " darkened=" << darkened
            << " both_darkened=" << bothDarkened
            << " caster_away_restored=" << cleared
            << " phong_receiver=" << phongReceiver
            << " uniform_delta_spread=" << spread << '\n';
  ok &= check(authored>=8 && holes>0 && dominant.size()>=4,
    label+" authored raster mask and full-coverage pixels are nonempty");
  ok &= check(changedMask==0 && alphaMismatch==0,
    label+" shadow receiver preserves authored mask and final alpha");
  if(phongReceiver) {
    ok &= check(darkened==dominant.size() &&
      bothDarkened==dominant.size(),
      label+" every PHONG receiver pixel samples its common shadow anchor");
    ok &= check(spread<=3,
      label+" PHONG anchor shadow delta uniform by authored color");
  } else {
    // Native Text2 and both marker nodes force BASE_COLOR in GLRender. The
    // original SoShadowGroup fragment shader discards shadeFactor entirely
    // for BASE_COLOR, even when a parent authored PHONG or style SHADOWED.
    size_t unchanged=0,bothUnchanged=0;
    for(size_t p:dominant) {
      const size_t at=p*4;
      bool equal=true,bothEqual=true;
      for(int c=0;c<3;++c) {
        equal &= std::abs(int(unshadowed.rgba[at+c])-
          int(shadowed.rgba[at+c]))<=3;
        bothEqual &= std::abs(int(unshadowed.rgba[at+c])-
          int(both.rgba[at+c]))<=3;
      }
      unchanged+=equal;bothUnchanged+=bothEqual;
    }
    ok &= check(unchanged==dominant.size() &&
      bothUnchanged==dominant.size(),
      label+" BASE_COLOR raster ignores shadow factor without losing content");
  }
  ok &= check(cleared==dominant.size(),
    label+" off-ray caster matches same-shader disabled-caster control");
  if(phongReceiver) {
    // Hold world geometry and lighting fixed while moving only the observer
    // along Z. The near plane now clips the small caster at z=4, but keeps
    // the SoImage receiver at z=1.2. With specular disabled, its authored
    // color, coverage and shadow lookup must be view-distance invariant.
    move->translation.setValue(casterX,casterY,0);
    scene.rasterStyle->style=SoShadowStyle::SHADOWED;
    scene.camera->position.setValue(0,0,3);
    scene.camera->nearDistance=1;
    scene.camera->farDistance=30;
    scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
    Frame movedBare,movedShown;
    const bool movedBareOk=harness.render(scene.root,movedBare);
    scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
    const bool movedShownOk=harness.render(scene.root,movedShown);
    ok &= check(movedBareOk && movedShownOk,
      label+" camera-only Z translation must publish");
    if(movedBareOk && movedShownOk) {
      size_t maskMismatch=0,alphaDifference=0,rgbDifference=0;
      int maxRgb=0;
      for(int y=32;y<96;++y) for(int x=32;x<96;++x) {
        const size_t at=(size_t(y)*side+x)*4;
        bool originalCovered=false,movedCovered=false;
        for(int c=0;c<3;++c) {
          originalCovered|=std::abs(int(unshadowed.rgba[at+c])-
            int(bare.rgba[at+c]))>3;
          movedCovered|=std::abs(int(movedShown.rgba[at+c])-
            int(movedBare.rgba[at+c]))>3;
        }
        maskMismatch+=originalCovered!=movedCovered;
        if(!originalCovered) continue;
        alphaDifference+=shadowed.rgba[at+3]!=movedShown.rgba[at+3];
        for(int c=0;c<3;++c) {
          const int delta=std::abs(int(shadowed.rgba[at+c])-
            int(movedShown.rgba[at+c]));
          maxRgb=std::max(maxRgb,delta);
          rgbDifference+=delta>3;
        }
      }
      std::cout << "raster_combinations_receiver_camera_z " << label
                << " z=10->3 near=1"
                << " mask_mismatch=" << maskMismatch
                << " alpha_mismatch=" << alphaDifference
                << " RGB_max=" << maxRgb
                << " RGB_channels_over3=" << rgbDifference << '\n';
      ok &= check(maskMismatch==0 && alphaDifference==0 && maxRgb<=3,
        label+" PHONG raster shadow invariant to observer Z/clipped caster");
      ok &= check(movedBare.serial>noCaster.serial &&
        movedShown.serial>movedBare.serial,
        label+" camera movement submits fresh frames");
    }
  }
  ok &= check(bare.serial<unshadowed.serial &&
    unshadowed.serial<shadowed.serial &&
    shadowed.serial<both.serial && both.serial<clearBare.serial &&
    clearBare.serial<clear.serial && clear.serial<noCaster.serial,
    label+" receiver state transitions publish new frames");
  return ok;
}
bool shadowReceiverFog(Harness & harness,bool directional,bool alphaHoles,
                       float cameraZ) {
  Scene scene(IMAGE,true,directional,1,SoShadowStyle::SHADOWED,
    true,false,.125f);
  scene.group->removeChild(2);
  scene.camera->position.setValue(0,0,cameraZ);
  scene.camera->nearDistance=1;
  scene.camera->farDistance=30;
  auto * environment=new SoEnvironment;
  environment->fogType=SoEnvironment::NONE;
  environment->fogColor.setValue(.12f,.61f,.84f);
  environment->fogVisibility=1.f;
  scene.root->insertChild(environment,1);
  scene.rasterMaterial->transparency=0;
  scene.rasterMaterial->specularColor.setValue(0,0,0);
  std::vector<unsigned char> pixels(size_t(17*17*(alphaHoles ? 4 : 3)));
  for(int y=0;y<17;++y) for(int x=0;x<17;++x) {
    const int n=alphaHoles ? 4 : 3;
    const size_t at=size_t(y*17+x)*n;
    pixels[at]=235;pixels[at+1]=42;pixels[at+2]=25;
    if(alphaHoles) pixels[at+3]=((x/3+y/3)%2) ? 0 : 255;
  }
  scene.image->image.setValue(SbVec2s(17,17),alphaHoles ? 4 : 3,
    pixels.data());
  const float casterX=directional ? .56f : .875f;
  const float casterY=directional ? .84f : 1.1666667f;
  auto * caster=new SoSeparator;
  auto * casterStyle=new SoShadowStyle;
  casterStyle->style=SoShadowStyle::CASTS_SHADOW;
  caster->addChild(casterStyle);
  auto * move=new SoTranslation;
  move->translation.setValue(casterX,casterY,0);
  caster->addChild(move);
  face(caster,-.12f,-.12f,.12f,.12f,4.f,SbColor(.2f,.2f,.2f));
  scene.group->insertChild(caster,2);
  harness.mode(CoinRenderAction::BLEND);
  const std::string label=std::string("shadow-receiver-fog/Image/")+
    "directional="+std::to_string(directional)+
    "/alpha_holes="+std::to_string(alphaHoles)+
    "/camera_z="+std::to_string(cameraZ);
  Frame bareBlocked,bareClear,blocked,clear;
  Frame fogBareBlocked,fogBareClear,fogBlocked,fogClear;
  bool ok=true;
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  ok &= check(harness.render(scene.root,bareBlocked),
    label+" no-fog blocked bare");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  ok &= check(harness.render(scene.root,blocked),label+" no-fog blocked");
  move->translation.setValue(casterX+1.5f,casterY,0);
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  ok &= check(harness.render(scene.root,bareClear),
    label+" no-fog clear bare");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  ok &= check(harness.render(scene.root,clear),label+" no-fog clear");
  environment->fogType=SoEnvironment::HAZE;
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  ok &= check(harness.render(scene.root,fogBareClear),
    label+" fog clear bare");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  move->translation.setValue(casterX,casterY,0);
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  ok &= check(harness.render(scene.root,fogBareBlocked),
    label+" fog blocked bare");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  ok &= check(harness.render(scene.root,fogBlocked),label+" fog blocked");
  move->translation.setValue(casterX+1.5f,casterY,0);
  ok &= check(harness.render(scene.root,fogClear),label+" fog clear");
  if(!ok) {
    std::cerr << label << " status=" << int(harness.action.getLastStatus())
              << " diagnostic=" << harness.action.getLastError().getString()
              << '\n';
    return false;
  }
  // OpenGL orthographic projection maps eye distance d to
  // z_ndc=(2*d-near-far)/(far-near). The portable planar fog contract uses
  // abs(z_ndc), while the shadow lookup still uses the anchor's world/eye
  // position; moving only the observer must change this fog weight.
  const float eyeDistance=cameraZ-1.2f;
  const float ndc=(2.f*eyeDistance-31.f)/29.f;
  const float weight=std::min(1.f,std::max(0.f,std::abs(ndc)));
  const int fogColor[3]={31,156,214};
  size_t covered=0,holes=0,darkened=0,maskMismatch=0,alphaMismatch=0;
  int maxFogError=0;
  for(int y=32;y<96;++y) for(int x=32;x<96;++x) {
    const size_t at=(size_t(y)*side+x)*4;
    bool authored=false,blockedAuthored=false;
    for(int c=0;c<3;++c) {
      authored|=std::abs(int(clear.rgba[at+c])-int(bareClear.rgba[at+c]))>3;
      blockedAuthored|=std::abs(int(blocked.rgba[at+c])-
        int(bareBlocked.rgba[at+c]))>3;
    }
    maskMismatch+=authored!=blockedAuthored;
    if(!authored) {
      ++holes;
      for(int c=0;c<4;++c)
        maskMismatch+=fogClear.rgba[at+c]!=fogBareClear.rgba[at+c];
      for(int c=0;c<4;++c)
        maskMismatch+=fogBlocked.rgba[at+c]!=fogBareBlocked.rgba[at+c];
      continue;
    }
    ++covered;
    alphaMismatch+=fogClear.rgba[at+3]!=clear.rgba[at+3];
    alphaMismatch+=fogBlocked.rgba[at+3]!=blocked.rgba[at+3];
    int loss=0;
    for(int c=0;c<3;++c) {
      loss+=int(clear.rgba[at+c])-int(blocked.rgba[at+c]);
      const int expectedClear=int(std::lround(
        float(clear.rgba[at+c])*(1.f-weight)+float(fogColor[c])*weight));
      const int expectedBlocked=int(std::lround(
        float(blocked.rgba[at+c])*(1.f-weight)+float(fogColor[c])*weight));
      maxFogError=std::max(maxFogError,
        std::abs(int(fogClear.rgba[at+c])-expectedClear));
      maxFogError=std::max(maxFogError,
        std::abs(int(fogBlocked.rgba[at+c])-expectedBlocked));
    }
    darkened+=loss>3;
  }
  std::cout << "raster_combinations_receiver_fog " << label
            << " ndc=" << ndc << " weight=" << weight
            << " covered=" << covered << " holes=" << holes
            << " no_fog_shadowed=" << darkened
            << " mask_mismatch=" << maskMismatch
            << " alpha_mismatch=" << alphaMismatch
            << " RGB_max_fog_error=" << maxFogError << '\n';
  ok &= check(covered>=100 && holes>0 && darkened>0,
    label+" PHONG image, holes and blocked anchor are nonvacuous");
  ok &= check(maskMismatch==0 && alphaMismatch==0,
    label+" fog preserves bitmap mask and final alpha");
  ok &= check(maxFogError<=3,
    label+" planar abs(NDCz) fog after PHONG shadow composition");
  ok &= check(bareBlocked.serial<blocked.serial &&
    blocked.serial<bareClear.serial && bareClear.serial<clear.serial &&
    clear.serial<fogBareClear.serial &&
    fogBareClear.serial<fogBareBlocked.serial &&
    fogBareBlocked.serial<fogBlocked.serial &&
    fogBlocked.serial<fogClear.serial,
    label+" fog and caster changes publish new frames");
  return ok;
}
bool shadowCombinationsCore(Harness & harness,Kind kind,bool directional,
                            int maps,int mode) {
  Scene scene(kind,true,directional,maps,SoShadowStyle::SHADOWED,
    kind==IMAGE,false,.125f);
  // Distinct shadow cameras must survive map allocation, traversal and
  // sorted raster submission. All directions still cover the central raster.
  for(int i=0;i<maps;++i) {
    if(directional) {
      auto * light=static_cast<SoShadowDirectionalLight *>(scene.group->getChild(i));
      light->direction.setValue(-.18f-.025f*i,-.27f+.015f*i,-1.f);
    } else {
      auto * light=static_cast<SoShadowSpotLight *>(scene.group->getChild(i));
      const float x=1.35f+.10f*i,y=1.8f-.06f*i,z=6.f+.04f*i;
      light->location.setValue(x,y,z);
      light->direction.setValue(-x,-y,1.2f-z);
    }
  }
  harness.mode(mode);
  const std::string label=std::string("shadow-combinations/")+kindName(kind)+
    "/directional="+std::to_string(directional)+
    "/maps="+std::to_string(maps)+"/mode="+std::to_string(mode);
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  Frame bare;
  bool ok=check(harness.render(scene.root,bare),label+" bare maps");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  Frame shown;
  ok &= check(harness.render(scene.root,shown),label+" sorted raster maps");
  if(!ok) {
    std::cerr << label << " status=" << int(harness.action.getLastStatus())
              << " diagnostic=" << harness.action.getLastError().getString()
              << '\n';
    return false;
  }
  size_t center=0,holes=0,alphaMismatch=0;
  int minX=side,minY=side,maxX=-1,maxY=-1;
  for(int y=48;y<=80;++y) for(int x=48;x<=80;++x) {
    const size_t at=(size_t(y)*side+x)*4;
    bool changed=false;
    for(int c=0;c<3;++c)
      changed|=std::abs(int(shown.rgba[at+c])-int(bare.rgba[at+c]))>3;
    if(changed) {
      ++center;
      minX=std::min(minX,x);maxX=std::max(maxX,x);
      minY=std::min(minY,y);maxY=std::max(maxY,y);
    } else if(x>=56 && x<=72 && y>=56 && y<=72) ++holes;
    alphaMismatch+=shown.rgba[at+3]!=bare.rgba[at+3];
  }
  std::cout << "raster_combinations_shadow_maps " << label
            << " center=" << center << " holes=" << holes
            << " bounds=" << minX << ',' << minY << ':' << maxX << ',' << maxY
            << " alpha_mismatch=" << alphaMismatch
            << " serial=" << bare.serial << "->" << shown.serial << '\n';
  ok &= check(center>0 && holes>0 && alphaMismatch==0,
    label+" sorted raster mask/alpha nonempty with distinct map cameras");
  if(kind==IMAGE) {
    std::array<unsigned,9> mask{{}};
    if(maxX-minX==2 && maxY-minY==2) {
      for(int y=0;y<3;++y) for(int x=0;x<3;++x) {
        const size_t at=(size_t(minY+y)*side+minX+x)*4;
        bool changed=false;
        for(int c=0;c<3;++c)
          changed|=std::abs(int(shown.rgba[at+c])-int(bare.rgba[at+c]))>3;
        mask[size_t(y*3+x)]=changed;
      }
    }
    const std::array<unsigned,9> checker{{1,0,1,0,1,0,1,0,1}};
    ok &= check(mask==checker,
      label+" sorted image preserves five authored texels and four holes");
  }
  ok &= check(shown.serial>bare.serial,label+" sorted maps publication");
  Frame rejected,recovered;
  scene.rasterDepth->range.setValue(0,
    std::numeric_limits<float>::quiet_NaN());
  harness.action.apply(scene.root);
  harness.gpu->readbackRGBA(rejected.rgba);
  harness.gpu->readbackDepth(rejected.depth);
  rejected.serial=harness.gpu->getLastSubmissionSerial();
  const CoinRenderAction::Status rejection=harness.action.getLastStatus();
  scene.rasterDepth->range.setValue(0,1);
  const bool recovery=harness.render(scene.root,recovered);
  ok &= check(rejection!=CoinRenderAction::SUCCESS &&
    exactFrame(rejected,shown),label+" invalid range atomic rollback");
  ok &= check(recovery && recovered.rgba==shown.rgba &&
    recovered.depth==shown.depth && recovered.serial>shown.serial,
    label+" sorted maps exact recovery");
  return ok;
}
bool shadowMapSlotCaster(Harness & harness,bool directional,int maps,int slot) {
  // Rotate the raster kind across slots: every map index is tested with an
  // authored caster, and the four bitmap classes all occur in each bank.
  const Kind kind=static_cast<Kind>(slot%4);
  Scene scene(kind,true,directional,maps,SoShadowStyle::CASTS_SHADOW,
    false,false,.125f);
  scene.group->removeChild(maps+1); // the ordinary caster is not this witness
  scene.rasterMaterial->transparency=0;
  if(kind==TEXT) {
    scene.font->size=48;
    scene.text->string.setValue("MW");
  } else if(kind==IMAGE) {
    // A 3x3 image projects to only two receiver pixels at some map angles;
    // that footprint cannot distinguish a borrowed first-map camera. This
    // fully opaque 17x17 RGB payload supplies interior map texels while the
    // separate sparse-marker fixture still proves bitmap holes.
    std::vector<unsigned char> pixels(size_t(17*17*3));
    for(int y=0;y<17;++y) for(int x=0;x<17;++x) {
      const size_t at=size_t(y*17+x)*3;
      pixels[at]=uint8_t(80+6*x);
      pixels[at+1]=uint8_t(30+8*y);
      pixels[at+2]=uint8_t(45+3*(x+y));
    }
    scene.image->image.setValue(SbVec2s(17,17),3,pixels.data());
  }
  else if(kind==MARKER) scene.marker->markerIndex=sparseCornerMarker();
  else scene.indexedMarker->markerIndex=sparseCornerMarker();
  for(int i=0;i<maps;++i) {
    if(directional) {
      auto * light=static_cast<SoShadowDirectionalLight *>(scene.group->getChild(i));
      light->direction.setValue(-.18f-.025f*i,-.27f+.015f*i,-1.f);
      light->intensity=i==slot ? .8f : 0.f;
    } else {
      auto * light=static_cast<SoShadowSpotLight *>(scene.group->getChild(i));
      const float x=1.35f+.10f*i,y=1.8f-.06f*i,z=6.f+.04f*i;
      light->location.setValue(x,y,z);
      light->direction.setValue(-x,-y,1.2f-z);
      light->cutOffAngle=.35f;
      light->intensity=i==slot ? .8f : 0.f;
    }
  }
  // The observer sees the receiver but its near plane clips the raster.
  // Any CAST/NO difference therefore comes from this light's map, not from
  // the screen-space bitmap or an observer-camera-expanded fake caster.
  scene.camera->position.setValue(0,0,3);
  scene.camera->nearDistance=2.5f;
  scene.camera->farDistance=10.f;
  harness.mode(CoinRenderAction::BLEND);
  const std::string label=std::string("shadow-map-slot/")+kindName(kind)+
    "/directional="+std::to_string(directional)+
    "/maps="+std::to_string(maps)+"/slot="+std::to_string(slot);
  scene.rasterStyle->style=SoShadowStyle::NO_SHADOWING;
  Frame noCast;
  bool ok=check(harness.render(scene.root,noCast),label+" no-cast");
  scene.rasterStyle->style=SoShadowStyle::CASTS_SHADOW;
  Frame cast;
  ok &= check(harness.render(scene.root,cast),label+" cast");
  // Turning off zero-intensity peers changes only the selected light's map
  // index, not its camera, authored bitmap, receiver or lighting. A renderer
  // that borrows the first map's camera for later slots must fail this image
  // equivalence even if both runs happen to cast a nonempty shadow.
  for(int i=0;i<maps;++i) {
    if(directional)
      static_cast<SoShadowDirectionalLight *>(scene.group->getChild(i))->on=(i==slot);
    else
      static_cast<SoShadowSpotLight *>(scene.group->getChild(i))->on=(i==slot);
  }
  scene.rasterStyle->style=SoShadowStyle::NO_SHADOWING;
  Frame soloNoCast;
  ok &= check(harness.render(scene.root,soloNoCast),label+" solo no-cast");
  scene.rasterStyle->style=SoShadowStyle::CASTS_SHADOW;
  Frame soloCast;
  ok &= check(harness.render(scene.root,soloCast),label+" solo cast");
  if(!ok) {
    std::cerr << label << " status=" << int(harness.action.getLastStatus())
              << " diagnostic=" << harness.action.getLastError().getString()
              << '\n';
    return false;
  }
  size_t changed=0,soloChanged=0,coverageMismatch=0,alphaMismatch=0;
  int maxNoCastRgb=0,maxCastRgb=0;
  float maxDepth=0.f;
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    const size_t p=size_t(y)*side+x,at=p*4;
    bool rgb=false,soloRgb=false;
    for(int c=0;c<3;++c) {
      rgb|=std::abs(int(cast.rgba[at+c])-int(noCast.rgba[at+c]))>3;
      soloRgb|=std::abs(int(soloCast.rgba[at+c])-
        int(soloNoCast.rgba[at+c]))>3;
      maxNoCastRgb=std::max(maxNoCastRgb,
        std::abs(int(noCast.rgba[at+c])-int(soloNoCast.rgba[at+c])));
      maxCastRgb=std::max(maxCastRgb,
        std::abs(int(cast.rgba[at+c])-int(soloCast.rgba[at+c])));
    }
    changed+=rgb;soloChanged+=soloRgb;
    coverageMismatch+=rgb!=soloRgb;
    alphaMismatch+=noCast.rgba[at+3]!=soloNoCast.rgba[at+3];
    alphaMismatch+=cast.rgba[at+3]!=soloCast.rgba[at+3];
    maxDepth=std::max(maxDepth,
      std::abs(noCast.depth[p]-soloNoCast.depth[p]));
    maxDepth=std::max(maxDepth,
      std::abs(cast.depth[p]-soloCast.depth[p]));
  }
  std::cout << "raster_combinations_map_slot " << label
            << " active_lights=" << maps << " contributing_slot=" << slot
            << " receiver_changed=" << changed
            << " solo_changed=" << soloChanged
            << " coverage_mismatch=" << coverageMismatch
            << " no_cast_RGB_max=" << maxNoCastRgb
            << " cast_RGB_max=" << maxCastRgb
            << " alpha_mismatch=" << alphaMismatch
            << " depth_max=" << maxDepth
            << " serial=" << noCast.serial << "->" << cast.serial
            << "->" << soloNoCast.serial << "->" << soloCast.serial << '\n';
  ok &= check(changed>0 && soloChanged>0 &&
    noCast.serial<cast.serial && cast.serial<soloNoCast.serial &&
    soloNoCast.serial<soloCast.serial,
    label+" selected slot casts authored raster shadow on receiver");
  ok &= check(coverageMismatch==0 && maxNoCastRgb<=3 && maxCastRgb<=3 &&
    alphaMismatch==0 && maxDepth==0.f,
    label+" each allocated slot uses its own map camera and footprint");
  return ok;
}
struct ShadowDepthProfile {
  Kind kind;
  bool front,test,write,annotation;
  float rangeNear,rangeFar;
};
bool shadowDepthCoreCase(Harness & harness,int maps,int mode,
                         const ShadowDepthProfile & profile) {
  Scene scene(profile.kind,true,false,maps,SoShadowStyle::SHADOWED,
    profile.kind==IMAGE,false,.125f,profile.annotation);
  scene.rasterMaterial->transparency=0;
  scene.rasterDepth->test=profile.test;
  scene.rasterDepth->write=profile.write;
  scene.rasterDepth->range.setValue(profile.rangeNear,profile.rangeFar);
  const float occluderZ=profile.front ? 2.f : 0.f;
  auto * occluder=new SoSeparator;
  face(occluder,-.3f,-.3f,.3f,.3f,occluderZ,SbColor(.05f,.3f,.85f));
  scene.group->insertChild(occluder,maps+2);
  for(int i=0;i<maps;++i) {
    auto * light=static_cast<SoShadowSpotLight *>(scene.group->getChild(i));
    const float x=1.35f+.1f*i,y=1.8f-.06f*i,z=6.f+.04f*i;
    light->location.setValue(x,y,z);
    light->direction.setValue(-x,-y,1.2f-z);
  }
  harness.mode(mode);
  const std::string label=std::string("shadow-depth/")+kindName(profile.kind)+
    "/maps="+std::to_string(maps)+"/mode="+std::to_string(mode)+
    "/front="+std::to_string(profile.front)+
    "/test="+std::to_string(profile.test)+
    "/write="+std::to_string(profile.write)+
    "/annotation="+std::to_string(profile.annotation)+
    "/range="+std::to_string(profile.rangeNear)+","+
    std::to_string(profile.rangeFar);
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  Frame bare;
  bool ok=check(harness.render(scene.root,bare),label+" bare");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  scene.rasterDepth->test=FALSE;
  Frame calibration;
  ok &= check(harness.render(scene.root,calibration),label+" calibration");
  scene.rasterDepth->test=profile.test;
  Frame shown;
  ok &= check(harness.render(scene.root,shown),label+" state");
  if(!ok) {
    std::cerr << label << " status=" << int(harness.action.getLastStatus())
              << " diagnostic=" << harness.action.getLastError().getString()
              << '\n';
    return false;
  }
  // Find an interior authored fragment directly over the oclusor. The
  // calibration changes only the explicit depth test and never consults
  // CoinGL or a candidate rasterizer's primitive representation.
  size_t witness=size_t(side*side);
  int witnessContrast=0;
  const float occluderDepth=cameraDepth(occluderZ);
  for(int y=59;y<=69;++y) for(int x=59;x<=69;++x) {
    const size_t p=size_t(y)*side+x,at=p*4;
    if(std::abs(bare.depth[p]-occluderDepth)>2e-5f) continue;
    int contrast=0;
    for(int c=0;c<3;++c)
      contrast+=std::abs(int(calibration.rgba[at+c])-int(bare.rgba[at+c]));
    if(contrast>witnessContrast) {
      witnessContrast=contrast;witness=p;
    }
  }
  ok &= check(witness<size_t(side*side) && witnessContrast>9,
    label+" authored raster/occlusor overlap is nonempty");
  if(!ok) return false;
  const float rasterDepth=cameraDepth(1.2f,
    profile.rangeNear,profile.rangeFar);
  const bool shouldShow=!profile.test || rasterDepth<=occluderDepth+1e-6f;
  const float expectedDepth=(profile.test && profile.write && shouldShow) ?
    rasterDepth : occluderDepth;
  const size_t at=witness*4;
  int observedContrast=0;
  for(int c=0;c<3;++c)
    observedContrast+=std::abs(int(shown.rgba[at+c])-int(bare.rgba[at+c]));
  const bool visible=observedContrast>9;
  const float depth=shown.depth[witness];
  std::cout << "raster_combinations_shadow_depth " << label
            << " xy=" << witness%side << ',' << witness/side
            << " calibration_contrast=" << witnessContrast
            << " shown_contrast=" << observedContrast
            << " visible=" << visible << " expected_visible=" << shouldShow
            << " depth=" << depth << " expected_depth=" << expectedDepth
            << " alpha=" << int(shown.rgba[at+3])
            << " serial=" << bare.serial << "->" << calibration.serial
            << "->" << shown.serial << '\n';
  ok &= check(visible==shouldShow &&
    std::abs(depth-expectedDepth)<=2e-5f &&
    shown.rgba[at+3]==bare.rgba[at+3] &&
    bare.serial<calibration.serial && calibration.serial<shown.serial,
    label+" numeric test/write/range/Annotation depth and authored alpha");
  return ok;
}
bool shadowFogModesCore(Harness & harness,Kind kind,int maps,float cameraZ) {
  Scene scene(kind,true,false,maps,SoShadowStyle::SHADOWED,
    kind==IMAGE,false,.125f);
  scene.camera->position.setValue(0,0,cameraZ);
  scene.camera->nearDistance=1;scene.camera->farDistance=30;
  scene.rasterMaterial->transparency=0;
  scene.rasterMaterial->specularColor.setValue(0,0,0);
  if(kind==IMAGE) {
    std::vector<unsigned char> pixels(size_t(17*17*4));
    for(int y=0;y<17;++y) for(int x=0;x<17;++x) {
      const size_t at=size_t(y*17+x)*4;
      pixels[at]=235;pixels[at+1]=42;pixels[at+2]=25;
      pixels[at+3]=((x/3+y/3)%2) ? 0 : 255;
    }
    scene.image->image.setValue(SbVec2s(17,17),4,pixels.data());
  } else if(kind==MARKER) scene.marker->markerIndex=sparseCornerMarker();
  else if(kind==INDEXED_MARKER)
    scene.indexedMarker->markerIndex=sparseCornerMarker();
  for(int i=0;i<maps;++i) {
    auto * light=static_cast<SoShadowSpotLight *>(scene.group->getChild(i));
    const float x=1.35f+.1f*i,y=1.8f-.06f*i,z=6.f+.04f*i;
    light->location.setValue(x,y,z);
    light->direction.setValue(-x,-y,1.2f-z);
  }
  auto * environment=new SoEnvironment;
  environment->fogType=SoEnvironment::NONE;
  environment->fogColor.setValue(.12f,.61f,.84f);
  environment->fogVisibility=1.7f;
  scene.root->insertChild(environment,1);
  harness.mode(CoinRenderAction::BLEND);
  const std::string label=std::string("shadow-fog-modes/")+kindName(kind)+
    "/maps="+std::to_string(maps)+"/camera_z="+std::to_string(cameraZ);
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  Frame noFogBare,noFogShown;
  bool ok=check(harness.render(scene.root,noFogBare),label+" no-fog bare");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  ok &= check(harness.render(scene.root,noFogShown),label+" no-fog shown");
  if(!ok) return false;
  std::map<uint32_t,std::vector<size_t>> classes;
  size_t authored=0,holes=0;
  for(int y=32;y<96;++y) for(int x=32;x<96;++x) {
    const size_t p=size_t(y)*side+x,at=p*4;
    bool covered=false;
    for(int c=0;c<3;++c)
      covered|=std::abs(int(noFogShown.rgba[at+c])-
        int(noFogBare.rgba[at+c]))>3;
    if(!covered) {++holes;continue;}
    ++authored;
    const uint32_t rgb=(uint32_t(noFogShown.rgba[at])<<16) |
      (uint32_t(noFogShown.rgba[at+1])<<8) | noFogShown.rgba[at+2];
    classes[rgb].push_back(p);
  }
  std::vector<size_t> fullPixels;
  for(const auto & entry:classes)
    if(entry.second.size()>fullPixels.size()) fullPixels=entry.second;
  ok &= check(authored>0 && holes>0 && fullPixels.size()>=4,
    label+" authored bitmap, holes and full-coverage class");
  if(!ok) return false;
  const float eyeDistance=cameraZ-1.2f;
  const float ndc=(2.f*eyeDistance-31.f)/29.f;
  const float distance=std::abs(ndc);
  const float ratio=distance/1.7f;
  const int fogColor[3]={31,156,214};
  const SoEnvironment::FogType modes[]={
    SoEnvironment::HAZE,SoEnvironment::FOG,SoEnvironment::SMOKE};
  std::array<float,3> factors{{}};
  uint64_t previous=noFogShown.serial;
  for(size_t mode=0;mode<3;++mode) {
    environment->fogType=modes[mode];
    scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
    Frame fogBare,fogShown;
    ok &= check(harness.render(scene.root,fogBare),label+" fog bare");
    scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
    ok &= check(harness.render(scene.root,fogShown),label+" fog shown");
    if(!ok) return false;
    // Original SoGLEnvironmentElement uses density 5.545/visibility for
    // GL_EXP and 2.35/visibility for GL_EXP2. The portable raster contract
    // feeds planar abs(NDCz) into that density, not shadow lookup eye depth.
    const float transmittance=mode==0 ?
      std::max(0.f,1.f-ratio) :
      (mode==1 ? std::exp(-5.545f*ratio) :
        std::exp(-std::pow(2.35f*ratio,2.f)));
    factors[mode]=transmittance;
    int maxRgb=0;
    size_t alphaMismatch=0,holeMismatch=0,visible=0;
    for(size_t p:fullPixels) {
      const size_t at=p*4;
      bool effect=false;
      for(int c=0;c<3;++c) {
        const int expected=int(std::lround(float(fogColor[c])+
          (float(noFogShown.rgba[at+c])-float(fogColor[c]))*transmittance));
        maxRgb=std::max(maxRgb,
          std::abs(int(fogShown.rgba[at+c])-expected));
        effect|=std::abs(int(fogShown.rgba[at+c])-
          int(fogBare.rgba[at+c]))>3;
      }
      visible+=effect;
      alphaMismatch+=fogShown.rgba[at+3]!=noFogShown.rgba[at+3];
    }
    for(int y=32;y<96;++y) for(int x=32;x<96;++x) {
      const size_t p=size_t(y)*side+x,at=p*4;
      bool covered=false;
      for(int c=0;c<3;++c)
        covered|=std::abs(int(noFogShown.rgba[at+c])-
          int(noFogBare.rgba[at+c]))>3;
      if(covered) continue;
      for(int c=0;c<4;++c)
        holeMismatch+=fogShown.rgba[at+c]!=fogBare.rgba[at+c];
    }
    std::cout << "raster_combinations_shadow_fog " << label
              << " mode=" << int(modes[mode]) << " abs_ndc=" << distance
              << " transmittance=" << transmittance
              << " authored=" << authored << " holes=" << holes
              << " full_class=" << fullPixels.size()
              << " fog_visible=" << visible
              << " RGB_max=" << maxRgb
              << " alpha_mismatch=" << alphaMismatch
              << " hole_mismatch=" << holeMismatch
              << " serial=" << previous << "->" << fogBare.serial
              << "->" << fogShown.serial << '\n';
    ok &= check(maxRgb<=3 && alphaMismatch==0 && holeMismatch==0 &&
      visible==fullPixels.size() &&
      previous<fogBare.serial && fogBare.serial<fogShown.serial,
      label+" analytic planar fog preserves raster mask, alpha and publication");
    previous=fogShown.serial;
    if(mode==1) {
      Frame rejected,recovered;
      scene.rasterDepth->range.setValue(0,
        std::numeric_limits<float>::quiet_NaN());
      harness.action.apply(scene.root);
      harness.gpu->readbackRGBA(rejected.rgba);
      harness.gpu->readbackDepth(rejected.depth);
      rejected.serial=harness.gpu->getLastSubmissionSerial();
      const auto status=harness.action.getLastStatus();
      scene.rasterDepth->range.setValue(0,1);
      const bool recoveredOk=harness.render(scene.root,recovered);
      ok &= check(status!=CoinRenderAction::SUCCESS &&
        exactFrame(rejected,fogShown) && recoveredOk &&
        recovered.rgba==fogShown.rgba &&
        recovered.depth==fogShown.depth &&
        recovered.serial>fogShown.serial,
        label+" exponential fog rollback and recovery");
      if(recoveredOk) previous=recovered.serial;
    }
  }
  const bool distinct=std::abs(factors[0]-factors[1])>.05f &&
    std::abs(factors[1]-factors[2])>.05f &&
    std::abs(factors[0]-factors[2])>.05f;
  ok &= check(distinct,label+" three fog functions numerically distinguishable");
  return ok;
}
bool shadowMarkerAnchorFogCore(Harness & harness,Kind kind,int maps,
                               float cameraZ) {
  Scene scene(kind,true,false,maps,SoShadowStyle::SHADOWED,
    false,false,.125f);
  scene.camera->position.setValue(0,0,cameraZ);
  scene.rasterMaterial->transparency=0;
  const SbVec3f anchors[2]={{-.5f,0,1.5f},{.5f,0,-.8f}};
  scene.markerCoordinates->point.setValues(0,2,anchors);
  if(kind==MARKER) {
    scene.marker->numPoints=2;
    const int32_t glyphs[2]={SoMarkerSet::CROSS_9_9,
      SoMarkerSet::CROSS_9_9};
    scene.marker->markerIndex.setValues(0,2,glyphs);
  } else {
    const int32_t indices[2]={0,1};
    const int32_t glyphs[2]={SoMarkerSet::CROSS_9_9,
      SoMarkerSet::CROSS_9_9};
    scene.indexedMarker->coordIndex.setValues(0,2,indices);
    scene.indexedMarker->markerIndex.setValues(0,2,glyphs);
  }
  auto * environment=new SoEnvironment;
  environment->fogType=SoEnvironment::NONE;
  // At camera z=3 the two authored depths yield FOG transmittances only
  // 0.029 apart with visibility 1.7. Visibility 2.2 separates them by
  // 0.043 while keeping both modes and cameras away from saturation.
  environment->fogVisibility=2.2f;
  environment->fogColor.setValue(.12f,.61f,.84f);
  scene.root->insertChild(environment,1);
  harness.mode(CoinRenderAction::BLEND);
  const std::string label=std::string("shadow-marker-anchor-fog/")+
    kindName(kind)+"/maps="+std::to_string(maps)+
    "/camera_z="+std::to_string(cameraZ);
  scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
  Frame bare,clear;
  bool ok=check(harness.render(scene.root,bare),label+" bare");
  scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
  ok &= check(harness.render(scene.root,clear),label+" clear");
  if(!ok) return false;
  // Ortho height 4 at 128 pixels maps the two authored x coordinates to
  // pixel columns 48 and 80. Each ROI contains only its own 9x9 glyph.
  const int centers[2]={48,80};
  std::vector<size_t> pixels[2];
  for(int anchor=0;anchor<2;++anchor)
    for(int y=60;y<=68;++y) for(int x=centers[anchor]-4;
        x<=centers[anchor]+4;++x) {
      const size_t p=size_t(y)*side+x,at=p*4;
      bool covered=false;
      for(int c=0;c<3;++c)
        covered|=std::abs(int(clear.rgba[at+c])-int(bare.rgba[at+c]))>3;
      if(covered) pixels[anchor].push_back(p);
    }
  ok &= check(pixels[0].size()>=5 && pixels[1].size()>=5,
    label+" two nonempty masks in one marker draw");
  if(!ok) return false;
  const int fogColor[3]={31,156,214};
  const SoEnvironment::FogType modes[]={SoEnvironment::HAZE,
    SoEnvironment::FOG,SoEnvironment::SMOKE};
  for(auto mode:modes) {
    environment->fogType=mode;
    scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
    Frame fogBare,fogShown;
    ok &= check(harness.render(scene.root,fogBare),label+" fog bare");
    scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
    ok &= check(harness.render(scene.root,fogShown),label+" fog shown");
    if(!ok) return false;
    int maxRgb=0,wrongNodeDepth=0;
    size_t holes=0,holeMismatch=0,alphaMismatch=0;
    float factors[2];
    for(int anchor=0;anchor<2;++anchor) {
      const float eyeDistance=cameraZ-anchors[anchor][2];
      const float ratio=std::abs((2.f*eyeDistance-31.f)/29.f)/2.2f;
      factors[anchor]=mode==SoEnvironment::HAZE ?
        std::max(0.f,1.f-ratio) :
        (mode==SoEnvironment::FOG ? std::exp(-5.545f*ratio) :
          std::exp(-std::pow(2.35f*ratio,2.f)));
      const float otherEye=cameraZ-anchors[1-anchor][2];
      const float otherRatio=std::abs((2.f*otherEye-31.f)/29.f)/2.2f;
      const float wrongFactor=mode==SoEnvironment::HAZE ?
        std::max(0.f,1.f-otherRatio) :
        (mode==SoEnvironment::FOG ? std::exp(-5.545f*otherRatio) :
          std::exp(-std::pow(2.35f*otherRatio,2.f)));
      for(size_t p:pixels[anchor]) {
        const size_t at=p*4;
        for(int c=0;c<3;++c) {
          const int expected=int(std::lround(float(fogColor[c])+
            (float(clear.rgba[at+c])-float(fogColor[c]))*factors[anchor]));
          const int wrong=int(std::lround(float(fogColor[c])+
            (float(clear.rgba[at+c])-float(fogColor[c]))*wrongFactor));
          maxRgb=std::max(maxRgb,
            std::abs(int(fogShown.rgba[at+c])-expected));
          wrongNodeDepth=std::max(wrongNodeDepth,std::abs(expected-wrong));
        }
        alphaMismatch+=fogShown.rgba[at+3]!=clear.rgba[at+3];
      }
      for(int y=60;y<=68;++y) for(int x=centers[anchor]-4;
          x<=centers[anchor]+4;++x) {
        const size_t p=size_t(y)*side+x,at=p*4;
        if(std::find(pixels[anchor].begin(),pixels[anchor].end(),p)!=
            pixels[anchor].end()) continue;
        ++holes;
        for(int c=0;c<4;++c)
          holeMismatch+=fogShown.rgba[at+c]!=fogBare.rgba[at+c];
      }
    }
    std::cout << "raster_combinations_marker_anchor_fog " << label
              << " mode=" << int(mode) << " factors=" << factors[0]
              << ',' << factors[1] << " covered=" << pixels[0].size()
              << ',' << pixels[1].size() << " holes=" << holes
              << " RGB_max=" << maxRgb
              << " wrong_node_depth_separation=" << wrongNodeDepth
              << " alpha_mismatch=" << alphaMismatch
              << " hole_mismatch=" << holeMismatch << '\n';
    ok &= check(maxRgb<=3 && wrongNodeDepth>5 && holes>0 &&
      holeMismatch==0 && alphaMismatch==0 && clear.serial<fogBare.serial &&
      fogBare.serial<fogShown.serial,
      label+" distinct per-anchor planar fog, holes, alpha and serial");
  }
  return ok;
}
bool rttCampaign(bool canonical,bool direct) {
  std::string diagnostic;
  CoinRenderOptions options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if(!check(diagnostic.empty(),diagnostic)) return false;
  if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  options.sceneTexture=direct ? COIN_RENDER_SCENE_TEXTURE_DIRECT :
    COIN_RENDER_SCENE_TEXTURE_STAGED;
  std::unique_ptr<CoinRenderTarget> peer;
  std::vector<uint8_t> peerColor;
  uint64_t peerSerial=0;
  if(direct) {
    peer.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    if(!check(bool(peer),"RTT peer target")) return false;
    Scene peerScene(MARKER,false,false,0,0,false);
    CoinRenderAction peerAction(SbViewportRegion(side,side));
    peerAction.setRenderTarget(peer.get());peerAction.apply(peerScene.root);
    peer->readbackRGBA(peerColor);
    peerSerial=peer->getLastSubmissionSerial();
    if(!check(peerAction.getLastStatus()==CoinRenderAction::SUCCESS &&
      peerColor.size()==size_t(side*side*4),"RTT peer initializes runtime")) return false;
  }
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  if(!check(bool(target),"RTT consumer target")) return false;
  target->setDepthReadbackEnabled(TRUE);
  std::cout << "raster_combinations_rtt_runtime direct=" << direct
            << " renderer=" << int(target->getOptions().renderer)
            << " raster=" << int(target->getOptions().rasterizationPolicy) << '\n';
  Scene producer(IMAGE,true,false,1,SoShadowStyle::CASTS_SHADOW,false,true,.05f);
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
  texture->backgroundColor.setValue(0,0,0,1);
  consumer->addChild(texture);
  auto * uv=new SoTextureCoordinate2;
  const SbVec2f texcoords[]={SbVec2f(0,0),SbVec2f(1,0),
    SbVec2f(1,1),SbVec2f(0,1)};
  uv->point.setValues(0,4,texcoords);consumer->addChild(uv);
  auto * coords=new SoCoordinate3;
  const SbVec3f corners[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
    SbVec3f(1,1,0),SbVec3f(-1,1,0)};
  coords->point.setValues(0,4,corners);consumer->addChild(coords);
  auto * quad=new SoFaceSet;quad->numVertices.set1Value(0,4);
  consumer->addChild(quad);
  CoinRenderAction action(SbViewportRegion(side,side));
  action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  SoOffscreenRenderer native{SbViewportRegion(side,side)};
  native.setComponents(SoOffscreenRenderer::RGB);
  native.getGLRenderAction()->setTransparencyType(
    SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  auto peerIntact=[&]() {
    if(!peer) return true;
    std::vector<uint8_t> now;peer->readbackRGBA(now);
    return now==peerColor && peer->getLastSubmissionSerial()==peerSerial;
  };
  auto capture=[&](Frame & frame,std::vector<uint8_t> & gl,
                   const std::string & label) {
    action.apply(consumer);
    target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
    frame.serial=target->getLastSubmissionSerial();
    bool published=action.getLastStatus()==CoinRenderAction::SUCCESS &&
      frame.rgba.size()==size_t(side*side*4) &&
      frame.depth.size()==size_t(side*side);
    if(!published)
      std::cerr << "raster_combinations_rtt " << label << " status="
                << int(action.getLastStatus()) << " diagnostic="
                << action.getLastError().getString() << '\n';
    if(!published || !native.render(consumer) || !native.getBuffer()) return false;
    gl.resize(size_t(side*side*3));
    for(int y=0;y<side;++y) for(int x=0;x<side;++x)
      for(int c=0;c<3;++c)
        gl[(size_t(y)*side+x)*3+c]=
          native.getBuffer()[(size_t(side-1-y)*side+x)*3+c];
    return true;
  };
  producer.rasterSwitch->whichChild=SO_SWITCH_NONE;
  Frame bare,shown,rejected,recovered;
  std::vector<uint8_t> glBare,glShown,glRecovered;
  bool ok=check(capture(bare,glBare,"bare"),"RTT bare capture");
  producer.rasterSwitch->whichChild=SO_SWITCH_ALL;
  ok &= check(capture(shown,glShown,"shown"),"RTT raster capture");
  if(ok) {
    const Effect effect=compare(shown,bare,glShown,glBare);
    std::cout << "raster_combinations_rtt direct=" << direct
              << " GPU_effect=" << effect.pixels
              << " center=" << effect.centerPixels
              << " RGB_max=" << effect.maxError << '\n';
    ok &= check(effect.centerPixels>=3 && effect.maxError<=3,
      "RTT must consume raster mask and agree with CoinGL");
    ok &= check(shown.serial>bare.serial && peerIntact(),
      "RTT serial and initialized peer");
  }
  producer.rasterDepth->range.setValue(
    std::numeric_limits<float>::quiet_NaN(),1);
  action.apply(consumer);
  target->readbackRGBA(rejected.rgba);target->readbackDepth(rejected.depth);
  rejected.serial=target->getLastSubmissionSerial();
  ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
    exactFrame(rejected,shown) && peerIntact(),
    "RTT invalid producer must preserve consumer and peer");
  producer.rasterDepth->range.setValue(0,1);
  ok &= check(capture(recovered,glRecovered,"recovered") &&
    recovered.rgba==shown.rgba && recovered.depth==shown.depth &&
    recovered.serial>shown.serial && peerIntact(),
    "RTT producer recovery");
  consumer->unref();
  return ok;
}
bool rttShadowCore(bool canonical,bool direct) {
  std::string diagnostic;
  CoinRenderOptions options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if(!check(diagnostic.empty(),diagnostic)) return false;
  if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  options.sceneTexture=direct ? COIN_RENDER_SCENE_TEXTURE_DIRECT :
    COIN_RENDER_SCENE_TEXTURE_STAGED;
  std::unique_ptr<CoinRenderTarget> peer(
    CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  if(!check(bool(peer),"RTT shadow core peer target")) return false;
  Scene peerScene(MARKER,false,false,0,0,false);
  CoinRenderAction peerAction(SbViewportRegion(side,side));
  peerAction.setRenderTarget(peer.get());peerAction.apply(peerScene.root);
  std::vector<uint8_t> peerColor;
  peer->readbackRGBA(peerColor);
  const uint64_t peerSerial=peer->getLastSubmissionSerial();
  if(!check(peerAction.getLastStatus()==CoinRenderAction::SUCCESS &&
    peerColor.size()==size_t(side*side*4),"RTT shadow peer initial frame"))
    return false;
  auto peerIntact=[&]() {
    std::vector<uint8_t> color;peer->readbackRGBA(color);
    return color==peerColor && peer->getLastSubmissionSerial()==peerSerial;
  };
  bool all=true;
  for(int maps:{1,4,8}) for(Kind kind:{TEXT,IMAGE,MARKER,INDEXED_MARKER}) {
    Scene producer(kind,true,false,maps,SoShadowStyle::SHADOWED,
      kind==IMAGE,false,.125f);
    for(int i=0;i<maps;++i) {
      auto * light=static_cast<SoShadowSpotLight *>(producer.group->getChild(i));
      const float x=1.35f+.1f*i,y=1.8f-.06f*i,z=6.f+.04f*i;
      light->location.setValue(x,y,z);
      light->direction.setValue(-x,-y,1.2f-z);
    }
    std::unique_ptr<CoinRenderTarget> reference(
      CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    std::unique_ptr<CoinRenderTarget> consumerTarget(
      CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    const std::string label=std::string("RTT-shadow-core/")+kindName(kind)+
      "/maps="+std::to_string(maps)+"/direct="+std::to_string(direct);
    if(!check(bool(reference) && bool(consumerTarget),label+" targets"))
      return false;
    reference->setDepthReadbackEnabled(TRUE);
    consumerTarget->setDepthReadbackEnabled(TRUE);
    CoinRenderAction producerAction(SbViewportRegion(side,side));
    producerAction.setRenderTarget(reference.get());
    producerAction.setTransparencyType(
      CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
    producerAction.setBackgroundColor(SbColor4f(0,0,0,1));
    auto * consumer=new SoSeparator;consumer->ref();
    auto * camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=.1f;camera->farDistance=10;
    consumer->addChild(camera);
    auto * model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;
    consumer->addChild(model);
    auto * quality=new SoComplexity;quality->textureQuality=.3f;
    consumer->addChild(quality);
    auto * white=new SoMaterial;white->diffuseColor.setValue(1,1,1);
    consumer->addChild(white);
    auto * texture=new SoSceneTexture2;
    texture->scene=producer.root;texture->size.setValue(side,side);
    texture->type=SoSceneTexture2::RGBA8;
    texture->model=SoSceneTexture2::REPLACE;
    texture->transparencyFunction=SoSceneTexture2::NONE;
    texture->backgroundColor.setValue(0,0,0,1);
    consumer->addChild(texture);
    auto * uv=new SoTextureCoordinate2;
    const SbVec2f texcoords[]={SbVec2f(0,0),SbVec2f(1,0),
      SbVec2f(1,1),SbVec2f(0,1)};
    uv->point.setValues(0,4,texcoords);consumer->addChild(uv);
    auto * coords=new SoCoordinate3;
    const SbVec3f corners[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),
      SbVec3f(1,1,0),SbVec3f(-1,1,0)};
    coords->point.setValues(0,4,corners);consumer->addChild(coords);
    auto * quad=new SoFaceSet;quad->numVertices.set1Value(0,4);
    consumer->addChild(quad);
    CoinRenderAction action(SbViewportRegion(side,side));
    action.setRenderTarget(consumerTarget.get());
    action.setTransparencyType(
      CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
    action.setBackgroundColor(SbColor4f(0,0,0,1));
    auto refFrame=[&](Frame & frame) {
      producerAction.apply(producer.root);
      reference->readbackRGBA(frame.rgba);
      reference->readbackDepth(frame.depth);
      frame.serial=reference->getLastSubmissionSerial();
      return producerAction.getLastStatus()==CoinRenderAction::SUCCESS &&
        frame.rgba.size()==size_t(side*side*4) &&
        frame.depth.size()==size_t(side*side);
    };
    auto targetFrame=[&](Frame & frame) {
      action.apply(consumer);
      consumerTarget->readbackRGBA(frame.rgba);
      consumerTarget->readbackDepth(frame.depth);
      frame.serial=consumerTarget->getLastSubmissionSerial();
      return action.getLastStatus()==CoinRenderAction::SUCCESS &&
        frame.rgba.size()==size_t(side*side*4) &&
        frame.depth.size()==size_t(side*side);
    };
    producer.rasterSwitch->whichChild=SO_SWITCH_NONE;
    Frame refBare,bare,refShown,shown,rejected,recovered;
    bool ok=check(refFrame(refBare),label+" direct bare reference");
    ok &= check(targetFrame(bare),label+" RTT bare");
    producer.rasterSwitch->whichChild=SO_SWITCH_ALL;
    ok &= check(refFrame(refShown),label+" direct shown reference");
    ok &= check(targetFrame(shown),label+" RTT shown");
    if(!ok) {
      std::cerr << label << " producer_status=" << int(producerAction.getLastStatus())
                << " RTT_status=" << int(action.getLastStatus())
                << " diagnostic=" << action.getLastError().getString() << '\n';
      consumer->unref();all=false;continue;
    }
    size_t effect=0,maskMismatch=0,alphaMismatch=0;
    size_t alphaIdentityDifference=0;
    int firstAlphaX=-1,firstAlphaY=-1;
    int firstSourceAlpha=0,firstConsumerAlpha=0;
    int firstBareSourceAlpha=0,firstBareConsumerAlpha=0;
    std::array<int,3> firstSourceRgb{{0,0,0}},firstConsumerRgb{{0,0,0}};
    int maxRgb=0;
    float maxDepthChange=0.f;
    for(size_t p=0;p<size_t(side*side);++p) {
      const size_t at=p*4;
      bool sourceEffect=false,targetEffect=false;
      for(int c=0;c<3;++c) {
        sourceEffect|=std::abs(int(refShown.rgba[at+c])-
          int(refBare.rgba[at+c]))>3;
        targetEffect|=std::abs(int(shown.rgba[at+c])-
          int(bare.rgba[at+c]))>3;
        maxRgb=std::max(maxRgb,
          std::abs(int(refShown.rgba[at+c])-int(shown.rgba[at+c])));
      }
      effect+=sourceEffect;
      maskMismatch+=sourceEffect!=targetEffect;
      // The scene texture producer uses Coin's legacy blend alpha. A covered
      // monochrome text/marker fragment has As=1-material.transparency=.6
      // over Ad=1, hence RTT alpha=As*As+Ad*(1-As)=.76. The direct offscreen
      // reference keeps Ad=1. SoImage uses its own binary payload alpha,
      // giving 1 at both opaque texels and transparent holes over Ad=1.
      const float sourceAlpha=1.f-.4f; // Scene's authored material transparency
      const int legacyMonoAlpha=int(std::lround(255.f*(sourceAlpha*sourceAlpha+
        (1.f-sourceAlpha))));
      const int expectedAlpha=(kind==IMAGE || !sourceEffect) ?
        int(refShown.rgba[at+3]) : legacyMonoAlpha;
      alphaIdentityDifference+=refShown.rgba[at+3]!=shown.rgba[at+3];
      if(int(shown.rgba[at+3])!=expectedAlpha) {
        ++alphaMismatch;
        if(firstAlphaX<0) {
          firstAlphaX=int(p%side);firstAlphaY=int(p/side);
          firstSourceAlpha=int(refShown.rgba[at+3]);
          firstConsumerAlpha=int(shown.rgba[at+3]);
          firstBareSourceAlpha=int(refBare.rgba[at+3]);
          firstBareConsumerAlpha=int(bare.rgba[at+3]);
          for(int c=0;c<3;++c) {
            firstSourceRgb[size_t(c)]=int(refShown.rgba[at+c]);
            firstConsumerRgb[size_t(c)]=int(shown.rgba[at+c]);
          }
        }
      }
      maxDepthChange=std::max(maxDepthChange,
        std::abs(shown.depth[p]-bare.depth[p]));
    }
    std::cout << "raster_combinations_rtt_core " << label
              << " effect=" << effect << " mask_mismatch=" << maskMismatch
              << " RGB_max=" << maxRgb << " alpha_mismatch=" << alphaMismatch
              << " alpha_identity_difference=" << alphaIdentityDifference
              << " first_alpha_xy=" << firstAlphaX << ',' << firstAlphaY
              << " sourceA=" << firstSourceAlpha
              << " consumerA=" << firstConsumerAlpha
              << " bare_sourceA=" << firstBareSourceAlpha
              << " bare_consumerA=" << firstBareConsumerAlpha
              << " sourceRGB=" << firstSourceRgb[0] << ','
              << firstSourceRgb[1] << ',' << firstSourceRgb[2]
              << " consumerRGB=" << firstConsumerRgb[0] << ','
              << firstConsumerRgb[1] << ',' << firstConsumerRgb[2]
              << " consumer_depth_change=" << maxDepthChange
              << " source_serial=" << refBare.serial << "->" << refShown.serial
              << " consumer_serial=" << bare.serial << "->" << shown.serial
              << '\n';
    ok &= check(effect>0 && maskMismatch==0 && maxRgb<=3 &&
      alphaMismatch==0 && maxDepthChange==0.f &&
      refShown.serial>refBare.serial && shown.serial>bare.serial &&
      peerIntact(),label+" RTT sampled shadow raster equals offscreen source");
    producer.rasterDepth->range.setValue(0,
      std::numeric_limits<float>::quiet_NaN());
    action.apply(consumer);
    consumerTarget->readbackRGBA(rejected.rgba);
    consumerTarget->readbackDepth(rejected.depth);
    rejected.serial=consumerTarget->getLastSubmissionSerial();
    const auto status=action.getLastStatus();
    producer.rasterDepth->range.setValue(0,1);
    const bool recovery=targetFrame(recovered);
    ok &= check(status!=CoinRenderAction::SUCCESS &&
      exactFrame(rejected,shown) && peerIntact(),
      label+" invalid producer preserves consumer and peer");
    ok &= check(recovery && recovered.rgba==shown.rgba &&
      recovered.depth==shown.depth && recovered.serial>shown.serial &&
      peerIntact(),label+" RTT recovery");
    consumer->unref();all &= ok;
  }
  return all;
}
#ifdef COIN_RASTER_X11
uint8_t x11channel(unsigned long value,unsigned long mask) {
  if(!mask) return 0;
  while(!(mask&1UL)) {value>>=1;mask>>=1;}
  return uint8_t(((value&mask)*255UL+mask/2UL)/mask);
}
std::vector<uint8_t> physical(Display * display,Window window,int size) {
  XImage * image=XGetImage(display,window,0,0,size,size,AllPlanes,ZPixmap);
  if(!image) return {};
  std::vector<uint8_t> rgb(size_t(size)*size*3);
  for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
    const unsigned long pixel=XGetPixel(image,x,y);
    const size_t at=(size_t(y)*size+x)*3;
    rgb[at]=x11channel(pixel,image->red_mask);
    rgb[at+1]=x11channel(pixel,image->green_mask);
    rgb[at+2]=x11channel(pixel,image->blue_mask);
  }
  XDestroyImage(image);
  return rgb;
}
bool windowCampaign(bool canonical) {
  std::string diagnostic;
  CoinRenderOptions options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if(!diagnostic.empty()) return check(false,diagnostic);
  if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
    options.renderer,&caps,sizeof(caps))!=0 || !caps.gpu_available) return false;
  std::cout << "raster_combinations_window_runtime backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " adapter=" << caps.adapter_name << '\n';
  Display * display=XOpenDisplay(nullptr);
  if(!display) return false;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),
    20,20,side,side,0,0,0);
  XSetWindowAttributes attributes{};attributes.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor surface{};
  surface.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  surface.structSize=sizeof(surface);
  surface.type=COIN_RENDER_SURFACE_XLIB;
  surface.native.xlib.display=display;surface.native.xlib.window=window;
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createWindow(surface,SbVec2i32(side,side),options));
  bool ok=check(bool(target),"window target creation");
  if(ok) ok &= check(target->getOptions().rasterizationPolicy==
    options.rasterizationPolicy,"window raster policy");
  Scene scene(IMAGE,true,false,1,SoShadowStyle::CASTS_SHADOW,false,true,.05f);
  CoinRenderAction action(SbViewportRegion(side,side));
  action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  uint64_t previous=target ? target->getLastSubmissionSerial() : 0;
  std::vector<uint8_t> lastImage;
  int current=side;
  auto frame=[&](int size,uint8_t stamp,bool shown,const char * label) {
    scene.rasterSwitch->whichChild=shown ? SO_SWITCH_ALL : SO_SWITCH_NONE;
    if(size!=current) {
      XResizeWindow(display,window,size,size);XSync(display,False);
      XWindowAttributes observed{};
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      do {
        XGetWindowAttributes(display,window,&observed);
        if(observed.width==size && observed.height==size) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      } while(std::chrono::steady_clock::now()<deadline);
      if(!check(observed.width==size && observed.height==size,
        std::string(label)+" X11 resize")) return false;
      if(!check(target->resize(SbVec2i32(size,size)),
        std::string(label)+" target resize")) return false;
      current=size;
    }
    action.setViewportRegion(SbViewportRegion(size,size));
    action.setBackgroundColor(SbColor4f(float(stamp)/255.f,17.f/255.f,29.f/255.f,1));
    action.apply(scene.root);
    if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS,
      std::string(label)+" window submission")) {
      std::cerr << action.getLastError().getString() << '\n';return false;
    }
    const uint64_t serial=target->getLastSubmissionSerial();
    if(!check(serial>previous,std::string(label)+" serial")) return false;
    previous=serial;
    SoOffscreenRenderer native{SbViewportRegion(size,size)};
    native.setComponents(SoOffscreenRenderer::RGB);
    native.setBackgroundColor(SbColor(float(stamp)/255.f,17.f/255.f,29.f/255.f));
    native.getGLRenderAction()->setTransparencyType(
      SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
    if(!check(native.render(scene.root) && native.getBuffer(),
      std::string(label)+" CoinGL")) return false;
    std::vector<uint8_t> expected(size_t(size)*size*3);
    for(int y=0;y<size;++y) for(int x=0;x<size;++x)
      for(int c=0;c<3;++c)
        expected[(size_t(y)*size+x)*3+c]=
          native.getBuffer()[(size_t(size-1-y)*size+x)*3+c];
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    std::vector<uint8_t> actual;
    do {
      actual=physical(display,window,size);
      if(actual.size()==expected.size() &&
        std::abs(int(actual[0])-int(stamp))<=1) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while(std::chrono::steady_clock::now()<deadline);
    if(!check(actual.size()==expected.size(),
      std::string(label)+" physical readback")) return false;
    int maxError=0;size_t changed=0;
    for(size_t i=0;i<expected.size();++i) {
      maxError=std::max(maxError,std::abs(int(actual[i])-int(expected[i])));
      if(actual[i]!=expected[i]) ++changed;
    }
    std::cout << "raster_combinations_window " << label << " size=" << size
              << " serial=" << serial << " changed_channels=" << changed
              << " RGB_max=" << maxError << '\n';
    lastImage=actual;
    return check(maxError<=3,std::string(label)+" physical X11 vs CoinGL");
  };
  if(ok) ok &= frame(128,31,false,"bare128");
  if(ok) ok &= frame(128,47,true,"raster128");
  if(ok) ok &= frame(96,63,true,"raster96");
  if(ok) ok &= frame(65,79,true,"raster65");
  if(ok) ok &= frame(128,95,false,"bare-return128");
  if(ok) {
    scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
    scene.rasterDepth->range.setValue(
      std::numeric_limits<float>::quiet_NaN(),1);
    action.apply(scene.root);
    const auto observed=physical(display,window,128);
    ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
      target->getLastSubmissionSerial()==previous && observed==lastImage,
      "window invalid profile must preserve physical image and serial");
    scene.rasterDepth->range.setValue(0,1);
    if(ok) ok &= frame(128,111,true,"raster-recovery128");
  }
  action.setRenderTarget(nullptr);
  target.reset();XDestroyWindow(display,window);XCloseDisplay(display);
  return ok;
}
bool windowShadowCore(bool canonical) {
  std::string diagnostic;
  CoinRenderOptions options=CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if(!check(diagnostic.empty(),diagnostic)) return false;
  if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
    options.renderer,&caps,sizeof(caps))!=0 || !caps.gpu_available) return false;
  std::cout << "raster_combinations_window_shadow_runtime backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " adapter=" << caps.adapter_name
            << " raster=" << int(options.rasterizationPolicy) << '\n';
  Display * display=XOpenDisplay(nullptr);
  if(!display) return false;
  Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),
    20,20,side,side,0,0,0);
  XSetWindowAttributes attributes{};attributes.override_redirect=True;
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  CoinRenderNativeSurfaceDescriptor surface{};
  surface.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  surface.structSize=sizeof(surface);surface.type=COIN_RENDER_SURFACE_XLIB;
  surface.native.xlib.display=display;surface.native.xlib.window=window;
  std::unique_ptr<CoinRenderTarget> target(
    CoinRenderTarget::createWindow(surface,SbVec2i32(side,side),options));
  std::unique_ptr<CoinRenderTarget> reference(
    CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  std::unique_ptr<CoinRenderTarget> peer(
    CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  bool ok=check(bool(target) && bool(reference) && bool(peer),
    "window shadow targets and peer");
  if(!ok) {
    target.reset();XDestroyWindow(display,window);XCloseDisplay(display);
    return false;
  }
  reference->setDepthReadbackEnabled(TRUE);
  Scene peerScene(MARKER,false,false,0,0,false);
  CoinRenderAction peerAction(SbViewportRegion(side,side));
  peerAction.setRenderTarget(peer.get());peerAction.apply(peerScene.root);
  std::vector<uint8_t> peerColor;peer->readbackRGBA(peerColor);
  const uint64_t peerSerial=peer->getLastSubmissionSerial();
  ok &= check(peerAction.getLastStatus()==CoinRenderAction::SUCCESS &&
    peerColor.size()==size_t(side*side*4),"window shadow peer frame");
  auto peerIntact=[&]() {
    std::vector<uint8_t> now;peer->readbackRGBA(now);
    return now==peerColor && peer->getLastSubmissionSerial()==peerSerial;
  };
  CoinRenderAction action(SbViewportRegion(side,side));
  CoinRenderAction refAction(SbViewportRegion(side,side));
  action.setRenderTarget(target.get());refAction.setRenderTarget(reference.get());
  action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  refAction.setTransparencyType(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  int current=side,index=0;
  uint64_t lastSerial=target->getLastSubmissionSerial();
  std::vector<uint8_t> lastPhysical;
  auto capture=[&](SoNode * root,int size,uint8_t stamp,Frame & off,
                   std::vector<uint8_t> & visible,const std::string & label) {
    if(size!=current) {
      XResizeWindow(display,window,size,size);XSync(display,False);
      XWindowAttributes observed{};
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      do {
        XGetWindowAttributes(display,window,&observed);
        if(observed.width==size && observed.height==size) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      } while(std::chrono::steady_clock::now()<deadline);
      if(observed.width!=size || observed.height!=size ||
         !target->resize(SbVec2i32(size,size)) ||
         !reference->resize(SbVec2i32(size,size)))
        return check(false,label+" X11/target resize");
      current=size;
    }
    action.setViewportRegion(SbViewportRegion(size,size));
    refAction.setViewportRegion(SbViewportRegion(size,size));
    const SbColor4f background(float(stamp)/255.f,17.f/255.f,29.f/255.f,1);
    action.setBackgroundColor(background);refAction.setBackgroundColor(background);
    refAction.apply(root);reference->readbackRGBA(off.rgba);
    reference->readbackDepth(off.depth);
    off.serial=reference->getLastSubmissionSerial();
    action.apply(root);
    const uint64_t serial=target->getLastSubmissionSerial();
    if(refAction.getLastStatus()!=CoinRenderAction::SUCCESS ||
       action.getLastStatus()!=CoinRenderAction::SUCCESS ||
       off.rgba.size()!=size_t(size*size*4) ||
       off.depth.size()!=size_t(size*size) || serial<=lastSerial) {
      std::cerr << label << " offscreen_status=" << int(refAction.getLastStatus())
                << " window_status=" << int(action.getLastStatus())
                << " diagnostic=" << action.getLastError().getString() << '\n';
      return false;
    }
    lastSerial=serial;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    int maxRgb=255;
    do {
      visible=physical(display,window,size);
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
    std::cout << "raster_combinations_window_shadow " << label
              << " size=" << size << " serial=" << serial
              << " RGB_max=" << maxRgb << '\n';
    lastPhysical=visible;
    return check(maxRgb<=3 && peerIntact(),
      label+" physical X11 agrees with same-scene offscreen and peer intact");
  };
  for(int maps:{1,4,8}) for(Kind kind:{TEXT,IMAGE,MARKER,INDEXED_MARKER}) {
    const int sizes[]={64,96,65,128};
    const int size=sizes[index%4];
    const uint8_t stamp=uint8_t(31+index*11);
    ++index;
    Scene scene(kind,true,false,maps,SoShadowStyle::SHADOWED,
      kind==IMAGE,false,.125f);
    for(int i=0;i<maps;++i) {
      auto * light=static_cast<SoShadowSpotLight *>(scene.group->getChild(i));
      const float x=1.35f+.1f*i,y=1.8f-.06f*i,z=6.f+.04f*i;
      light->location.setValue(x,y,z);
      light->direction.setValue(-x,-y,1.2f-z);
    }
    const std::string label=std::string("window-shadow/")+kindName(kind)+
      "/maps="+std::to_string(maps);
    scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
    Frame bare,shown,rejected,recovered;
    std::vector<uint8_t> physicalBare,physicalShown,physicalRecovered;
    bool cell=capture(scene.root,size,stamp,bare,physicalBare,label+" bare");
    scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
    cell &= capture(scene.root,size,stamp,shown,physicalShown,label+" shown");
    if(!cell) {ok=false;continue;}
    size_t sourceEffect=0,physicalEffect=0,maskMismatch=0;
    for(size_t p=0;p<size_t(size*size);++p) {
      bool source=false,presented=false;
      for(int c=0;c<3;++c) {
        source|=std::abs(int(shown.rgba[p*4+c])-
          int(bare.rgba[p*4+c]))>3;
        presented|=std::abs(int(physicalShown[p*3+c])-
          int(physicalBare[p*3+c]))>3;
      }
      sourceEffect+=source;physicalEffect+=presented;
      maskMismatch+=source!=presented;
    }
    std::cout << "raster_combinations_window_shadow_mask " << label
              << " size=" << size << " offscreen=" << sourceEffect
              << " physical=" << physicalEffect
              << " mismatch=" << maskMismatch << '\n';
    cell &= check(sourceEffect>0 && maskMismatch==0 &&
      shown.serial>bare.serial && shown.depth.size()==size_t(size*size),
      label+" raster mask, offscreen depth and window publication");
    scene.rasterDepth->range.setValue(0,
      std::numeric_limits<float>::quiet_NaN());
    action.apply(scene.root);
    const auto invalidVisible=physical(display,window,size);
    const uint64_t invalidSerial=target->getLastSubmissionSerial();
    refAction.apply(scene.root);
    reference->readbackRGBA(rejected.rgba);
    reference->readbackDepth(rejected.depth);
    rejected.serial=reference->getLastSubmissionSerial();
    cell &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
      refAction.getLastStatus()!=CoinRenderAction::SUCCESS &&
      invalidSerial==lastSerial && invalidVisible==lastPhysical &&
      exactFrame(rejected,shown) && peerIntact(),
      label+" invalid profile preserves window/offscreen/depth/serial/peer");
    scene.rasterDepth->range.setValue(0,1);
    cell &= capture(scene.root,size,stamp,recovered,physicalRecovered,
      label+" recovery");
    cell &= check(recovered.rgba==shown.rgba &&
      recovered.depth==shown.depth && recovered.serial>shown.serial &&
      physicalRecovered==physicalShown && peerIntact(),
      label+" window and offscreen exact recovery");
    ok &= cell;
  }
  // Exercise target teardown and a fresh Xlib surface while the peer and
  // offscreen reference remain alive. No old frame may be borrowed by the
  // newly created window target.
  action.setRenderTarget(nullptr);
  target.reset();
  XDestroyWindow(display,window);XSync(display,False);
  window=XCreateSimpleWindow(display,DefaultRootWindow(display),
    24,24,side,side,0,0,0);
  XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
  XMapRaised(display,window);XSync(display,False);
  surface.native.xlib.window=window;
  target.reset(CoinRenderTarget::createWindow(surface,SbVec2i32(side,side),options));
  ok &= check(bool(target) && peerIntact(),
    "window shadow close/recreate target with live peer");
  if(target) {
    action.setRenderTarget(target.get());
    current=side;lastSerial=target->getLastSubmissionSerial();
    Scene scene(IMAGE,true,false,8,SoShadowStyle::SHADOWED,true,false,.125f);
    scene.rasterSwitch->whichChild=SO_SWITCH_NONE;
    Frame bare,shown;
    std::vector<uint8_t> visibleBare,visibleShown;
    bool recreated=capture(scene.root,side,211,bare,visibleBare,
      "window-shadow/recreated/bare");
    scene.rasterSwitch->whichChild=SO_SWITCH_ALL;
    recreated &= capture(scene.root,side,211,shown,visibleShown,
      "window-shadow/recreated/shown");
    size_t effect=0;
    if(recreated) for(size_t p=0;p<size_t(side*side);++p) {
      bool changed=false;
      for(int c=0;c<3;++c)
        changed|=std::abs(int(visibleShown[p*3+c])-
          int(visibleBare[p*3+c]))>3;
      effect+=changed;
    }
    ok &= check(recreated && effect>0 && peerIntact(),
      "recreated X11 target presents fresh raster and preserves peer");
  }
  action.setRenderTarget(nullptr);
  refAction.setRenderTarget(nullptr);
  peerAction.setRenderTarget(nullptr);
  target.reset();reference.reset();peer.reset();
  XDestroyWindow(display,window);XCloseDisplay(display);
  return ok;
}
#endif
} // namespace
int main(int argc,char ** argv) {
  bool canonical=false;
  bool window=false;
  bool windowShadow=false;
  bool rtt=false,direct=false;
  bool rttShadow=false;
  bool sortingOnly=false;
  bool noMapsOnly=false;
  bool noMapsCoreOnly=false;
  bool shadowCoreOnly=false;
  bool shadowReceiverCoreOnly=false;
  bool shadowReceiverFogCoreOnly=false;
  bool shadowCombinationsCoreOnly=false;
  bool shadowDepthCoreOnly=false;
  bool shadowFogModesCoreOnly=false;
  bool shadowMarkerAnchorFogCoreOnly=false;
  for(int i=1;i<argc;++i) {
    if(std::string(argv[i])=="--canonical-raster") canonical=true;
    else if(std::string(argv[i])=="--window") window=true;
    else if(std::string(argv[i])=="--window-shadow-core") windowShadow=true;
    else if(std::string(argv[i])=="--rtt-staged") rtt=true;
    else if(std::string(argv[i])=="--rtt-direct") {rtt=true;direct=true;}
    else if(std::string(argv[i])=="--rtt-shadow-staged") rttShadow=true;
    else if(std::string(argv[i])=="--rtt-shadow-direct") {
      rttShadow=true;direct=true;
    }
    else if(std::string(argv[i])=="--sorting-only") sortingOnly=true;
    else if(std::string(argv[i])=="--no-maps-only") noMapsOnly=true;
    else if(std::string(argv[i])=="--no-maps-core-only") noMapsCoreOnly=true;
    else if(std::string(argv[i])=="--shadow-core-only") shadowCoreOnly=true;
    else if(std::string(argv[i])=="--shadow-receiver-core-only")
      shadowReceiverCoreOnly=true;
    else if(std::string(argv[i])=="--shadow-receiver-fog-core-only")
      shadowReceiverFogCoreOnly=true;
    else if(std::string(argv[i])=="--shadow-combinations-core-only")
      shadowCombinationsCoreOnly=true;
    else if(std::string(argv[i])=="--shadow-depth-core-only")
      shadowDepthCoreOnly=true;
    else if(std::string(argv[i])=="--shadow-fog-modes-core-only")
      shadowFogModesCoreOnly=true;
    else if(std::string(argv[i])=="--shadow-marker-anchor-fog-core-only")
      shadowMarkerAnchorFogCoreOnly=true;
    else return 1;
  }
#if !defined(_WIN32)
  if(noMapsOnly || noMapsCoreOnly)
    setenv("COIN_QUADMESH_PRECISE_LIGHTING","1",1);
#endif
  SoDB::init();CoinRenderAction::initClass();
  if(rttShadow) return rttShadowCore(canonical,direct) ? 0 : 1;
  if(rtt) return rttCampaign(canonical,direct) ? 0 : 1;
#ifdef COIN_RASTER_X11
  if(windowShadow) return windowShadowCore(canonical) ? 0 : 1;
  if(window) return windowCampaign(canonical) ? 0 : 1;
#else
  if(window || windowShadow) return 77;
#endif
  CoinRenderCapabilities caps{};
  const bool available=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0 && caps.gpu_available;
  std::cout << "raster_combinations_runtime backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " device=" << caps.device_id << " adapter=" << caps.adapter_name
            << " gpu_available=" << caps.gpu_available << '\n';
  if(!available) return 77;
  Harness harness(canonical);
  if(!harness.gpuAvailable) return 1;
  bool ok=true;
  if(shadowMarkerAnchorFogCoreOnly) {
    for(int maps:{1,4,8})
      for(float cameraZ:{10.f,3.f})
        for(Kind kind:{MARKER,INDEXED_MARKER})
          ok &= shadowMarkerAnchorFogCore(harness,kind,maps,cameraZ);
    return ok ? 0 : 1;
  }
  if(shadowFogModesCoreOnly) {
    for(int maps:{1,4,8})
      for(float cameraZ:{10.f,3.f})
        for(Kind kind:{TEXT,IMAGE,MARKER,INDEXED_MARKER})
          ok &= shadowFogModesCore(harness,kind,maps,cameraZ);
    return ok ? 0 : 1;
  }
  if(shadowDepthCoreOnly) {
    const ShadowDepthProfile profiles[]={
      {TEXT,true,true,true,false,0,1},
      {IMAGE,false,true,true,false,0,1},
      {MARKER,true,false,true,false,0,1},
      {INDEXED_MARKER,false,true,false,false,0,1},
      {TEXT,false,true,true,false,1,0},
      {IMAGE,true,true,true,false,.4f,.4f},
      {MARKER,false,true,true,true,0,1},
      {INDEXED_MARKER,true,true,true,false,.1f,.7f},
    };
    for(int maps:{1,4,8})
      for(int mode:{int(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND),
                    int(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD)})
        for(const auto & profile:profiles)
          ok &= shadowDepthCoreCase(harness,maps,mode,profile);
    return ok ? 0 : 1;
  }
  if(shadowCombinationsCoreOnly) {
    for(int maps:{4,8})
      for(bool directional:{false,true})
        for(int mode:{int(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND),
                      int(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD)})
          for(Kind kind:{TEXT,IMAGE,MARKER,INDEXED_MARKER})
            ok &= shadowCombinationsCore(harness,kind,directional,maps,mode);
    for(int maps:{4,8})
      for(bool directional:{false,true})
        for(int slot=0;slot<maps;++slot)
          ok &= shadowMapSlotCaster(harness,directional,maps,slot);
    return ok ? 0 : 1;
  }
  if(shadowReceiverFogCoreOnly) {
    for(bool directional:{false,true})
      for(bool alphaHoles:{false,true})
        for(float cameraZ:{10.f,3.f})
          ok &= shadowReceiverFog(harness,directional,alphaHoles,cameraZ);
    return ok ? 0 : 1;
  }
  if(shadowReceiverCoreOnly) {
    for(bool directional:{false,true})
      for(Kind kind:{TEXT,IMAGE,MARKER,INDEXED_MARKER}) {
        ok &= shadowReceiverAnchor(harness,kind,directional,false);
        if(kind==IMAGE) ok &= shadowReceiverAnchor(harness,kind,directional,true);
      }
    return ok ? 0 : 1;
  }
  if(shadowCoreOnly) {
    ok &= shadowSparseMask(harness);
    for(bool directional:{false,true})
      for(Kind kind:{TEXT,IMAGE,MARKER,INDEXED_MARKER}) {
        ok &= shadowCoreCase(harness,kind,directional,false,false,
          IMAGE_RGB_OPAQUE);
        ok &= shadowCoreCase(harness,kind,directional,true,false,
          IMAGE_RGB_OPAQUE);
        ok &= shadowCoreCase(harness,kind,directional,true,true,
          IMAGE_RGB_OPAQUE);
        if(kind==IMAGE) {
          ok &= shadowCoreCase(harness,kind,directional,true,false,
            IMAGE_RGBA_OPAQUE);
          ok &= shadowCoreCase(harness,kind,directional,true,false,
            IMAGE_RGBA_VARIABLE);
        }
      }
    return ok ? 0 : 1;
  }
  if(noMapsOnly || noMapsCoreOnly) {
    for(int mode:{int(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND),
                  int(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD)})
      for(Kind kind:{TEXT,IMAGE,MARKER,INDEXED_MARKER})
        for(bool phong:{false,true}) {
          ok &= exercise(harness,kind,true,false,0,
            SoShadowStyle::SHADOWED,phong,mode,false,
            true,true,false,true,noMapsCoreOnly);
          ok &= exercise(harness,kind,true,false,1,
            SoShadowStyle::SHADOWED,phong,mode,false,
            false,false,true,false,noMapsCoreOnly);
          ok &= exercise(harness,kind,true,true,1,
            SoShadowStyle::SHADOWED,phong,mode,true,
            true,false,false,false,noMapsCoreOnly);
        }
    for(int mode:{int(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND),
                  int(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD)}) {
      ok &= exercise(harness,IMAGE,true,false,0,
        SoShadowStyle::SHADOWED,true,mode,false,
        true,true,false,true,noMapsCoreOnly,true);
      ok &= exercise(harness,IMAGE,true,false,1,
        SoShadowStyle::SHADOWED,true,mode,false,
        true,true,false,false,noMapsCoreOnly,true);
      ok &= exercise(harness,IMAGE,true,true,1,
        SoShadowStyle::SHADOWED,true,mode,false,
        true,true,false,false,noMapsCoreOnly,true);
    }
    return ok ? 0 : 1;
  }
  for(int mode:{int(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND),
                int(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD)}) {
    for(Kind kind:{TEXT,IMAGE,MARKER,INDEXED_MARKER})
      ok &= exercise(harness,kind,false,false,0,0,false,mode,false);
    for(Kind kind:{TEXT,IMAGE,MARKER,INDEXED_MARKER}) {
      ok &= exercise(harness,kind,false,false,0,0,true,mode,true,false,false);
      ok &= exercise(harness,kind,false,false,0,0,false,mode,false,true,false);
      ok &= exercise(harness,kind,false,false,0,0,false,mode,false,
        true,true,true);
    }
    for(bool front:{false,true}) for(bool test:{false,true})
      for(bool write:{false,true})
        ok &= depthWitness(harness,mode,front,test,write,false,0,1);
    ok &= depthWitness(harness,mode,false,true,true,false,.1f,.7f);
    ok &= depthWitness(harness,mode,false,true,true,true,0,1);
    if(sortingOnly) continue;
    for(bool directional:{false,true}) for(int maps:{1,4,8})
      for(Kind kind:{TEXT,IMAGE,MARKER,INDEXED_MARKER})
        ok &= exercise(harness,kind,true,directional,maps,
          SoShadowStyle::SHADOWED,true,mode,false);
    for(bool directional:{false,true})
      for(Kind kind:{TEXT,IMAGE,MARKER,INDEXED_MARKER}) {
        ok &= exercise(harness,kind,true,directional,1,
          SoShadowStyle::CASTS_SHADOW,false,mode,true);
        ok &= exercise(harness,kind,true,directional,1,
          SoShadowStyle::NO_SHADOWING,true,mode,false);
        ok &= exercise(harness,kind,true,directional,1,
          SoShadowStyle::CASTS_SHADOW_AND_SHADOWED,true,mode,false);
        ok &= shadowStyleContrast(harness,kind,directional,mode);
      }
  }
  return ok ? 0 : 1;
}
