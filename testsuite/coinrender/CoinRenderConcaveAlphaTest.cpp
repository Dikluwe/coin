#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderSamplingTestOptions.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int side=96;
const SbVec3f outline[8]={
  SbVec3f(-.8f,.8f,0),SbVec3f(-.45f,.8f,0),
  SbVec3f(-.45f,-.7f,0),SbVec3f(.45f,-.7f,0),
  SbVec3f(.45f,.8f,0),SbVec3f(.8f,.8f,0),
  SbVec3f(.8f,-.9f,0),SbVec3f(-.8f,-.9f,0)};
bool canonical=false, strictGL=false;
bool check(bool value,const std::string & label) {
  if(!value) std::cerr << "concave_alpha FAIL " << label << '\n';
  return value;
}
size_t pixel(int x,int y) {return size_t((side-1-y)*side+x)*4;}
struct Image {std::vector<uint8_t> rgba;std::vector<float> depth;uint64_t serial=0;};
bool equalImage(const Image & a,const Image & b) {
  return a.rgba==b.rgba && a.depth==b.depth;
}
bool equalColor(const Image & a,const Image & b,const std::string & label) {
  int maxColor=0;size_t colorPixels=0,depthPixels=0;
  if(a.rgba.size()!=b.rgba.size() || a.depth.size()!=b.depth.size())
    return check(false,label+" image sizes");
  for(size_t p=0;p<a.rgba.size();p+=4) {
    bool changed=false;
    for(int c=0;c<4;++c) {
      const int delta=std::abs(int(a.rgba[p+c])-int(b.rgba[p+c]));
      maxColor=std::max(maxColor,delta);changed|=delta!=0;
    }
    colorPixels+=changed;
  }
  for(size_t p=0;p<a.depth.size();++p) depthPixels+=a.depth[p]!=b.depth[p];
  std::cout << "concave_alpha " << label << " max_rgba=" << maxColor
            << " rgba_pixels=" << colorPixels << " depth_pixels=" << depthPixels << '\n';
  return check(maxColor==0,label+" composited RGBA");
}
size_t differing(const Image & a,const Image & b) {
  if(a.rgba.size()!=b.rgba.size()) return 0;
  size_t pixels=0;
  for(size_t p=0;p<a.rgba.size();p+=4)
    pixels+=a.rgba[p]!=b.rgba[p] || a.rgba[p+1]!=b.rgba[p+1] ||
      a.rgba[p+2]!=b.rgba[p+2] || a.rgba[p+3]!=b.rgba[p+3];
  return pixels;
}
bool closeImages(const Image & a,const Image & b,const std::string & label) {
  if(a.rgba.size()!=size_t(side*side*4) || b.rgba.size()!=a.rgba.size() ||
     a.depth.size()!=size_t(side*side) || b.depth.size()!=a.depth.size())
    return check(false,label+" capture sizes");
  int maxRgb=0,maxAlpha=0;float maxDepth=0;size_t differentCoverage=0;
  for(size_t p=0;p<size_t(side*side);++p) {
    for(int c=0;c<3;++c) maxRgb=std::max(maxRgb,std::abs(int(a.rgba[4*p+c])-int(b.rgba[4*p+c])));
    maxAlpha=std::max(maxAlpha,std::abs(int(a.rgba[4*p+3])-int(b.rgba[4*p+3])));
    maxDepth=std::max(maxDepth,std::abs(a.depth[p]-b.depth[p]));
    differentCoverage+=(a.depth[p]<.999f)!=(b.depth[p]<.999f);
  }
  std::cout << "concave_alpha " << label << " rgb=" << maxRgb
            << " alpha=" << maxAlpha << " depth=" << maxDepth
            << " coverage=" << differentCoverage << '\n';
  return check(maxRgb<=3 && maxAlpha<=2 && maxDepth<=2e-5f && differentCoverage==0,
               label+" CPU/GPU pixels");
}
struct Scene {
  SoSeparator *root=new SoSeparator;
  SoSeparator *stroke=new SoSeparator;
  SoCoordinate3 *points=new SoCoordinate3;
  SoDrawStyle *style=new SoDrawStyle;
  SoMaterial *material=new SoMaterial;
  SoMaterialBinding *binding=new SoMaterialBinding;
  SoTransparencyType *transparency=new SoTransparencyType;
  SoLightModel *lightModel=new SoLightModel;
  SoEnvironment *fog=new SoEnvironment;
  SoClipPlane *clip=new SoClipPlane;
  SoShapeHints *hints=new SoShapeHints;
  SoPolygonOffset *offset=new SoPolygonOffset;
  SoTexture2 *texture=new SoTexture2;
  SoTextureCoordinate2 *uv=new SoTextureCoordinate2;
  SoTextureMatrixTransform *matrix=new SoTextureMatrixTransform;
  SoTextureUnit *unit=new SoTextureUnit;
  bool textured=false;
  std::array<uint8_t,16> texels={{220,200,180,128, 110,230,90,255,
                                  240,85,160,0, 170,200,240,192}};
  Scene() {
    root->ref();
    auto *camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=.1f;camera->farDistance=10;
    root->addChild(camera);
    auto *light=new SoDirectionalLight;light->direction.setValue(.2f,-.1f,-1);
    root->addChild(light);
    auto *base=new SoSeparator;
    auto *baseLight=new SoLightModel;baseLight->model=SoLightModel::BASE_COLOR;
    base->addChild(baseLight);
    auto *blue=new SoMaterial;blue->diffuseColor.setValue(.08f,.16f,.4f);
    base->addChild(blue);
    auto *baseCoords=new SoCoordinate3;
    const SbVec3f rect[]={SbVec3f(-1,-1,-.3f),SbVec3f(1,-1,-.3f),
      SbVec3f(1,1,-.3f),SbVec3f(-1,1,-.3f)};
    baseCoords->point.setValues(0,4,rect);base->addChild(baseCoords);
    auto *baseFace=new SoFaceSet;baseFace->numVertices=4;base->addChild(baseFace);
    root->addChild(base);
    root->addChild(transparency);
    auto *complexity=new SoComplexity;complexity->textureQuality=.1f;
    stroke->addChild(complexity);
    stroke->addChild(lightModel);
    fog->fogType=SoEnvironment::NONE;fog->fogColor.setValue(.3f,.1f,.2f);
    fog->fogVisibility=5;stroke->addChild(fog);
    hints->faceType=SoShapeHints::UNKNOWN_FACE_TYPE;
    hints->vertexOrdering=SoShapeHints::UNKNOWN_ORDERING;
    stroke->addChild(hints);
    const SbColor red[]={SbColor(.9f,.2f,.12f),SbColor(.9f,.2f,.12f),
      SbColor(.9f,.2f,.12f),SbColor(.9f,.2f,.12f),
      SbColor(.9f,.2f,.12f),SbColor(.9f,.2f,.12f),
      SbColor(.9f,.2f,.12f),SbColor(.9f,.2f,.12f)};
    material->diffuseColor.setValues(0,8,red);
    material->specularColor.setValue(0,0,0);
    material->transparency.setValue(.5f);
    stroke->addChild(material);stroke->addChild(binding);
    style->lineWidth=3;style->pointSize=5;stroke->addChild(style);
    offset->on=FALSE;
    offset->styles=SoPolygonOffset::LINES|SoPolygonOffset::POINTS;
    offset->units=0;
    stroke->addChild(offset);
    clip->on=FALSE;clip->plane=SbPlane(SbVec3f(0,1,0),0);
    stroke->addChild(clip);
    unit->unit=0;stroke->addChild(unit);
    texture->model=SoTexture2::MODULATE;texture->image.setValue(SbVec2s(2,2),4,texels.data());
    stroke->addChild(texture);
    matrix->matrix=SbMatrix::identity();stroke->addChild(matrix);
    const SbVec2f tc[]={SbVec2f(.25f,.25f),SbVec2f(.25f,.25f),
      SbVec2f(.25f,.25f),SbVec2f(.25f,.25f),SbVec2f(.25f,.25f),
      SbVec2f(.25f,.25f),SbVec2f(.25f,.25f),SbVec2f(.25f,.25f)};
    uv->point.setValues(0,8,tc);stroke->addChild(uv);
    points->point.setValues(0,8,outline);stroke->addChild(points);
    auto *face=new SoFaceSet;face->numVertices=8;stroke->addChild(face);
    root->addChild(stroke);
  }
  ~Scene(){root->unref();}
  void useTexture(bool active,int textureUnit=0) {
    textured=active;unit->unit=textureUnit;
    texture->image.setValue(active?SbVec2s(2,2):SbVec2s(0,0),4,
                            active?texels.data():nullptr);
  }
  void setZ(float z) {
    SbVec3f shifted[8];for(int i=0;i<8;++i) {shifted[i]=outline[i];shifted[i][2]=z;}
    points->point.setValues(0,8,shifted);
  }
};
bool capture(CoinRenderAction & action,CoinRenderTarget & target,Scene & scene,
             Image & image,const std::string & label) {
  const uint64_t before=target.getLastSubmissionSerial();
  action.apply(scene.root);
  image.rgba.clear();image.depth.clear();
  target.readbackRGBA(image.rgba);target.readbackDepth(image.depth);
  image.serial=target.getLastSubmissionSerial();
  const bool good=action.getLastStatus()==CoinRenderAction::SUCCESS &&
    image.serial>before && image.rgba.size()==size_t(side*side*4) &&
    image.depth.size()==size_t(side*side);
  if(!good) std::cerr << "concave_alpha " << label << " status=" << action.getLastStatus()
    << " error=" << action.getLastError().getString() << " serial=" << before
    << "->" << image.serial << " bytes=" << image.rgba.size() << '\n';
  return check(good,label+" published");
}
bool geometry(const Image & stroke,const Image & base,int mode,bool clipped,
              const std::string & label) {
  size_t bridge=0,left=0,right=0,edge=0,anyChanged=0;
  int maxRgbDelta=0;
  for(int y=2;y<side-2;++y) for(int x=2;x<side-2;++x) {
    const size_t p=pixel(x,y);
    bool changed=false;
    for(int c=0;c<3;++c) {
      const int delta=std::abs(int(stroke.rgba[p+c])-int(base.rgba[p+c]));
      maxRgbDelta=std::max(maxRgbDelta,delta);changed|=delta>4;
    }
    if(!changed) continue;
    ++anyChanged;
    if(x>=41 && x<=55 && y>=47 && y<=55) ++bridge;
    if(x<39 && y>50) ++left;
    if(x>57 && y>50) ++right;
    if(y<18) ++edge;
  }
  std::cout << "concave_alpha " << label << " left=" << left << " right=" << right
            << " bridge=" << bridge << " lower_edge=" << edge
            << " changed=" << anyChanged << " max_rgb_delta=" << maxRgbDelta << '\n';
  return check(left>5 && right>5 && bridge==0 && (!clipped || edge==0),
               label+" two components and no internal bridge");
}
bool blendWitness(const Image & image,const std::string & label,int y=86) {
  if(image.rgba.size()!=size_t(side*side*4)) return check(false,label+" image size");
  // Interior of the two authored horizontal top edges. BASE_COLOR and
  // material transparency .5 over the opaque blue quad give this equation.
  const float base[3]={.08f,.16f,.4f},paint[3]={.9f,.2f,.12f};
  bool ok=true;
  for(const int x:{18,78}) {
    const size_t p=pixel(x,y);
    for(int c=0;c<3;++c) {
      const int expected=int(std::lround(255.f*(.5f*paint[c]+.5f*base[c])));
      ok &= check(std::abs(int(image.rgba[p+c])-expected)<=5,
        label+" analytic blend x="+std::to_string(x)+" y="+std::to_string(y)+
        " channel="+std::to_string(c)+
        " actual="+std::to_string(image.rgba[p+c])+" expected="+std::to_string(expected));
    }
    ok &= check(image.rgba[p+3]==255,label+" opaque destination alpha");
  }
  return ok;
}
bool clippedAlphaWitness(const Image & image,int mode,const std::string & label) {
  if(image.rgba.size()!=size_t(side*side*4)) return check(false,label+" image size");
  const float base[3]={.08f,.16f,.4f},paint[3]={.9f,.2f,.12f};
  // Authored vertical edges span y=+.8 to -.7. Clipping at y=0 creates
  // endpoints with alpha 7/15, while the center of scanline 48 is just above
  // the cut (0.5/48 world units). Either value differs sharply from alpha 1
  // or 0 copied from an original endpoint.
  const float alpha=mode==SoDrawStyle::POINTS ? .7f/1.5f :
    (.7f+.5f/48.f)/1.5f;
  bool ok=true;
  for(const int x:{26,69}) {
    const size_t p=pixel(x,48);
    for(int c=0;c<3;++c) {
      const int expected=int(std::lround(255.f*(alpha*paint[c]+(1.f-alpha)*base[c])));
      const int actual=image.rgba[p+c];
      std::cout << "concave_alpha " << label << " clipped_xy=" << x << ",48"
                << " alpha_expected=" << alpha << " channel=" << c
                << " actual=" << actual << " expected=" << expected << '\n';
      ok &= check(std::abs(actual-expected)<=6,
        label+" clipped alpha interpolation x="+std::to_string(x)+
        " channel="+std::to_string(c));
    }
    ok &= check(image.rgba[p+3]==255,label+" clipped opaque destination alpha");
  }
  return ok;
}
size_t changedBand(const Image & a,const Image & b,int x0,int x1) {
  if(a.rgba.size()!=size_t(side*side*4) || b.rgba.size()!=a.rgba.size()) return 0;
  size_t changed=0;
  for(int y=11;y<=18;++y) for(int x=x0;x<=x1;++x) {
    const size_t p=pixel(x,y);
    changed+=a.rgba[p]!=b.rgba[p] || a.rgba[p+1]!=b.rgba[p+1] ||
      a.rgba[p+2]!=b.rgba[p+2] || a.rgba[p+3]!=b.rgba[p+3];
  }
  return changed;
}
bool outgoingOnly(const Image & before,const Image & after,const std::string & label) {
  const size_t left=changedBand(before,after,12,42);
  const size_t right=changedBand(before,after,54,82);
  std::cout << "concave_alpha " << label << " left_changed=" << left
            << " right_changed=" << right << '\n';
  return check(left==0 && right>5,label+" occurrence2 outgoing edge only");
}
float depthDelta(const Image & a,const Image & b) {
  if(a.depth.size()!=size_t(side*side) || b.depth.size()!=a.depth.size()) return 0;
  float maximum=0;
  for(size_t p=0;p<a.depth.size();++p)
    if(a.depth[p]<.999f && b.depth[p]<.999f)
      maximum=std::max(maximum,std::abs(a.depth[p]-b.depth[p]));
  return maximum;
}
}
int main(int argc,char **argv) {
  configureSamplingTest(argc,argv);
  bool gpuRequested=false;
  for(int i=1;i<argc;++i) {
    const std::string arg(argv[i]);
    gpuRequested|=arg=="--gpu";
    canonical|=arg=="--canonical-raster";
    strictGL|=arg=="--gl-reference";
  }
  SoDB::init();CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  const bool available=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0 && caps.gpu_available;
  std::cout << "concave_alpha backend=" << caps.backend << " renderer=" << caps.renderer
    << " vendor=" << caps.vendor_id << " device=" << caps.device_id
    << " adapter=" << caps.adapter_name << " gpu_available=" << caps.gpu_available
    << " max_units=" << caps.max_texture_units
    << " raster=" << (canonical?"CANONICAL_8BIT":"NATIVE") << '\n';
  if(gpuRequested && !available) return 77;
  CoinRenderOptions options=samplingTestOptions();
  options.rasterizationPolicy=canonical?COIN_RENDER_RASTER_CANONICAL_8BIT:COIN_RENDER_RASTER_NATIVE;
  std::unique_ptr<CoinRenderTarget> gpu(samplingTestOffscreen(SbVec2i32(side,side),options));
  std::unique_ptr<CoinRenderTarget> cpu(samplingTestOffscreen(SbVec2i32(side,side),options));
  if(!gpu || !cpu || !gpu->setDepthReadbackEnabled(TRUE) ||
     !cpu->setDepthReadbackEnabled(TRUE)) return 1;
  if(!gpuRequested) gpu->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  cpu->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  CoinRenderAction gpuAction(SbViewportRegion(side,side)),cpuAction(SbViewportRegion(side,side));
  gpuAction.setRenderTarget(gpu.get());cpuAction.setRenderTarget(cpu.get());
  SoOffscreenRenderer gl(SbViewportRegion(side,side));gl.setComponents(SoOffscreenRenderer::RGB);
  Scene scene;
  bool ok=true;
  auto both=[&](const std::string & label,Image & out) {
    Image reference;
    const bool gotGpu=capture(gpuAction,*gpu,scene,out,label+"/GPU");
    const bool gotCpu=capture(cpuAction,*cpu,scene,reference,label+"/CPU");
    bool passed=check(gotGpu && gotCpu,label+" both executors");
    if(gotGpu && gotCpu) passed &= closeImages(out,reference,label);
    if(strictGL) {
      const bool native=gl.render(scene.root) && gl.getBuffer();
      passed &= check(native,label+" CoinGL render");
      if(native && gotGpu) {
        int maxRgb=0;
        const auto *pixels=gl.getBuffer();
        for(int y=0;y<side;++y) for(int x=0;x<side;++x)
          for(int c=0;c<3;++c)
            maxRgb=std::max(maxRgb,std::abs(int(out.rgba[pixel(x,y)+c])-
              int(pixels[(size_t(y)*side+x)*3+c])));
        std::cout << "concave_alpha " << label << " CoinGL max_rgb=" << maxRgb << '\n';
        passed &= check(maxRgb<=3,label+" strict CoinGL RGB");
      }
    }
    return passed;
  };
  Image base,front,rear,changed,recovered;
  scene.stroke->renderCaching=SoSeparator::OFF;
  scene.stroke->ref();
  scene.root->removeChild(scene.stroke);
  ok &= both("opaque_base",base);
  scene.root->addChild(scene.stroke);
  scene.stroke->unref();
  for(int mode:{SoDrawStyle::LINES,SoDrawStyle::POINTS}) {
    scene.style->style=static_cast<SoDrawStyle::Style>(mode);
    scene.setZ(0);scene.clip->on=TRUE;
    scene.lightModel->model=SoLightModel::BASE_COLOR;
    scene.transparency->value=SoTransparencyType::BLEND;
    scene.useTexture(false);
    if(mode==SoDrawStyle::POINTS) {
      scene.clip->on=FALSE;
      Image unclipped;
      ok &= both("points_unclipped_control",unclipped);
      std::cout << "concave_alpha points_unclipped_changed="
                << differing(unclipped,base) << '\n';
      ok &= check(differing(unclipped,base)>20,"unclipped points visible");
      scene.clip->on=TRUE;
    }
    ok &= both(std::string("blend_")+(mode==SoDrawStyle::LINES?"lines":"points"),front);
    if(front.rgba.size()==base.rgba.size())
      ok &= geometry(front,base,mode,true,"blend_geometry");
    if(mode==SoDrawStyle::LINES) ok &= blendWitness(front,"blend_lines");
    scene.setZ(-.4f);
    ok &= both("behind_opaque",rear);
    ok &= check(equalImage(rear,base),"behind opaque leaves background unchanged");
    scene.setZ(0);
    for(int unit:{0,3,7}) {
      if(unit>=int(caps.max_texture_units)) {
        std::cout << "concave_alpha unit=" << unit << " unavailable on adapter\n";
        continue;
      }
      scene.useTexture(true,unit);
      ok &= both("texture_unit_"+std::to_string(unit),front);
      if(front.rgba.size()==base.rgba.size())
        ok &= geometry(front,base,mode,true,"texture_geometry");
      const Image stable=front;
      scene.texels[0]=85;scene.texture->image.setValue(SbVec2s(2,2),4,scene.texels.data());
      ok &= both("texture_mutated_"+std::to_string(unit),changed);
      ok &= check(differing(stable,changed)>5,"texture mutation visible");
      scene.texels[0]=220;scene.texture->image.setValue(SbVec2s(2,2),4,scene.texels.data());
      ok &= both("texture_recovered_"+std::to_string(unit),recovered);
      ok &= check(equalImage(stable,recovered),"texture recovery exact");
      SbMatrix shifted=SbMatrix::identity();
      shifted.setTranslate(SbVec3f(.5f,0,0));
      scene.matrix->matrix=shifted;
      ok &= both("texture_matrix_mutated_"+std::to_string(unit),changed);
      ok &= check(differing(stable,changed)>5,"texture matrix mutation visible");
      scene.matrix->matrix=SbMatrix::identity();
      ok &= both("texture_matrix_recovered_"+std::to_string(unit),recovered);
      ok &= check(equalImage(stable,recovered),"texture matrix recovery exact");
      scene.useTexture(false);
    }
    Image alphaBase;
    ok &= both("alpha_baseline",alphaBase);
    scene.material->transparency.setValue(.75f);
    ok &= both("alpha_mutated",changed);
    ok &= check(differing(alphaBase,changed)>5,"material alpha mutation visible");
    scene.material->transparency.setValue(.5f);
    ok &= both("alpha_recovered",recovered);
    ok &= check(equalImage(alphaBase,recovered),"material alpha recovery exact");
    const float variableAlpha[]={0.f,.25f,.5f,.75f,.2f,.6f,.4f,.8f};
    scene.material->transparency.setValues(0,8,variableAlpha);
    scene.binding->value=SoMaterialBinding::PER_VERTEX;
    ok &= both("alpha_per_vertex",changed);
    ok &= check(differing(alphaBase,changed)>5,"per-vertex alpha changes contour");
    scene.binding->value=SoMaterialBinding::OVERALL;
    scene.material->transparency.setValue(.5f);
    ok &= both("alpha_per_vertex_recovered",recovered);
    ok &= check(equalImage(alphaBase,recovered),"per-vertex alpha recovery exact");
    scene.transparency->value=SoTransparencyType::SORTED_OBJECT_BLEND;
    ok &= both("sorted_object_blend",front);
    ok &= equalColor(alphaBase,front,"single sorted object equals ordered blend");
    scene.transparency->value=SoTransparencyType::SCREEN_DOOR;
    ok &= both("screen_door_polygon_stroke",front);
    scene.transparency->value=SoTransparencyType::BLEND;
    scene.clip->on=FALSE;
    scene.lightModel->model=SoLightModel::PHONG;
    scene.fog->fogType=SoEnvironment::HAZE;
    ok &= both("phong_fog",front);
    scene.fog->fogType=SoEnvironment::NONE;
    scene.lightModel->model=SoLightModel::BASE_COLOR;
    scene.clip->on=TRUE;
  }
  // An inserted clipping endpoint must interpolate material alpha from both
  // authored corners. The two U legs cross the same y=0 plane in opposite
  // edge directions, which also checks direction-independent interpolation.
  const float clippedTransparency[]={.5f,0.f,1.f,1.f,0.f,.5f,.5f,.5f};
  scene.material->transparency.setValues(0,8,clippedTransparency);
  scene.binding->value=SoMaterialBinding::PER_VERTEX;
  scene.useTexture(false);
  scene.setZ(0);
  scene.clip->on=TRUE;
  scene.transparency->value=SoTransparencyType::BLEND;
  for(int mode:{SoDrawStyle::LINES,SoDrawStyle::POINTS}) {
    scene.style->style=static_cast<SoDrawStyle::Style>(mode);
    Image clipped;
    const std::string name=mode==SoDrawStyle::LINES?"clipped_alpha_lines":"clipped_alpha_points";
    const bool captured=both(name,clipped);
    ok &= captured;
    if(captured) ok &= clippedAlphaWitness(clipped,mode,name);
  }
  scene.binding->value=SoMaterialBinding::OVERALL;
  scene.material->transparency.setValue(.5f);
  scene.style->style=SoDrawStyle::FILLED;
  scene.clip->on=FALSE;
  Image filled;
  const bool filledCaptured=both("filled_transparent_control",filled);
  ok &= filledCaptured;
  if(filledCaptured) {
    ok &= blendWitness(filled,"filled_transparent_control",72);
    const size_t hole=pixel(48,72);
    bool unchanged=true;
    for(int c=0;c<4;++c) unchanged &= filled.rgba[hole+c]==base.rgba[hole+c];
    unchanged &= filled.depth[hole/4]==base.depth[hole/4];
    ok &= check(unchanged,"filled concave center remains the opaque background");
  }
  // Two consecutive occurrences share position A, but occurrence 1 ends the
  // incoming edge and occurrence 2 starts the outgoing edge. Editing only 2
  // must leave the left band intact, even though both have the same XYZ.
  const SbVec3f repeated[]={SbVec3f(-.8f,-.7f,0),SbVec3f(0,-.7f,0),
    SbVec3f(0,-.7f,0),SbVec3f(.8f,-.7f,0),
    SbVec3f(.8f,.7f,0),SbVec3f(-.8f,.7f,0),
    SbVec3f(-.8f,0,0),SbVec3f(-.8f,-.35f,0)};
  scene.points->point.setValues(0,8,repeated);
  scene.style->style=SoDrawStyle::LINES;
  scene.clip->on=FALSE;
  scene.binding->value=SoMaterialBinding::PER_VERTEX;
  const float uniformAlpha[]={.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f};
  scene.material->transparency.setValues(0,8,uniformAlpha);
  scene.useTexture(false);
  Image occurrenceBase;
  ok &= both("occurrence_alpha_A_B_A",occurrenceBase);
  scene.material->transparency.set1Value(2,.05f);
  ok &= both("occurrence_alpha2_mutated",changed);
  ok &= outgoingOnly(occurrenceBase,changed,"material_alpha");
  scene.material->transparency.set1Value(2,.5f);
  ok &= both("occurrence_alpha2_recovered",recovered);
  ok &= check(equalImage(occurrenceBase,recovered),"occurrence material recovery exact");
  scene.useTexture(true,0);
  ok &= both("occurrence_texture_A_B_A",occurrenceBase);
  scene.uv->point.set1Value(2,SbVec2f(.75f,.25f));
  ok &= both("occurrence_uv2_mutated",changed);
  ok &= outgoingOnly(occurrenceBase,changed,"texture_uv");
  scene.uv->point.set1Value(2,SbVec2f(.25f,.25f));
  ok &= both("occurrence_uv2_recovered",recovered);
  ok &= check(equalImage(occurrenceBase,recovered),"occurrence UV recovery exact");
  scene.useTexture(false);
  scene.binding->value=SoMaterialBinding::OVERALL;
  scene.material->transparency.setValue(.5f);

  // Nonplanar original contours must apply the authored stroke offset, with
  // alpha active, and changing cull winding must affect the visible stroke.
  SbVec3f sloped[8];
  for(int i=0;i<8;++i) {sloped[i]=outline[i];sloped[i][2]=(i&1)?.09f:-.07f;}
  scene.points->point.setValues(0,8,sloped);
  scene.useTexture(true,0);
  scene.offset->on=TRUE;
  scene.offset->units=0;
  for(int mode:{SoDrawStyle::LINES,SoDrawStyle::POINTS}) {
    scene.style->style=static_cast<SoDrawStyle::Style>(mode);
    scene.offset->factor=0;
    Image unoffset,positive,negative;
    ok &= both("alpha_offset_zero",unoffset);
    scene.offset->factor=12;
    ok &= both("alpha_offset_positive",positive);
    scene.offset->factor=-12;
    ok &= both("alpha_offset_negative",negative);
    const float plus=depthDelta(unoffset,positive),minus=depthDelta(unoffset,negative);
    std::cout << "concave_alpha offset style=" << mode << " plus=" << plus
              << " minus=" << minus << '\n';
    ok &= check(plus>1e-4f && minus>1e-4f,"alpha stroke offset changes depth");
    scene.offset->factor=0;
    ok &= both("alpha_offset_recovered",recovered);
    ok &= check(equalImage(unoffset,recovered),"alpha offset recovery exact");
  }
  scene.offset->on=FALSE;
  scene.style->style=SoDrawStyle::LINES;
  scene.hints->shapeType=SoShapeHints::SOLID;
  scene.hints->vertexOrdering=SoShapeHints::COUNTERCLOCKWISE;
  Image ccw,cw;
  ok &= both("alpha_cull_ccw",ccw);
  scene.hints->vertexOrdering=SoShapeHints::CLOCKWISE;
  ok &= both("alpha_cull_cw",cw);
  ok &= check(differing(ccw,cw)>5,"alpha culling changes visible contour");
  scene.hints->shapeType=SoShapeHints::UNKNOWN_SHAPE_TYPE;
  scene.hints->vertexOrdering=SoShapeHints::UNKNOWN_ORDERING;
  scene.useTexture(false);
  scene.points->point.setValues(0,8,outline);
  scene.style->style=SoDrawStyle::LINES;
  scene.transparency->value=SoTransparencyType::BLEND;
  scene.setZ(0);scene.clip->on=FALSE;
  ok &= both("rollback_baseline",front);
  const SbVec3f good=scene.points->point[2];
  scene.points->point.set1Value(2,SbVec3f(std::numeric_limits<float>::quiet_NaN(),good[1],good[2]));
  const Image before=front;
  const uint64_t serial=gpu->getLastSubmissionSerial();
  gpuAction.apply(scene.root);
  Image after;gpu->readbackRGBA(after.rgba);gpu->readbackDepth(after.depth);
  after.serial=gpu->getLastSubmissionSerial();
  ok &= check(gpuAction.getLastStatus()!=CoinRenderAction::SUCCESS &&
    after.serial==serial && equalImage(before,after),"invalid vertex atomic rollback");
  scene.points->point.set1Value(2,good);
  ok &= both("rollback_recovered",recovered);
  ok &= check(equalImage(before,recovered),"invalid vertex recovery exact");
  gpuAction.setRenderTarget(nullptr);cpuAction.setRenderTarget(nullptr);
  return ok?0:1;
}
