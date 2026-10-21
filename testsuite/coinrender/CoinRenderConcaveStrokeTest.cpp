#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderSamplingTestOptions.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoNormalElement.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/details/SoFaceDetail.h>
#include <Inventor/details/SoPointDetail.h>
#include <Inventor/system/gl.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>
#include <array>

class DerivedConcaveFaceSet : public SoFaceSet {
  SO_NODE_HEADER(DerivedConcaveFaceSet);
public:
  DerivedConcaveFaceSet() { SO_NODE_CONSTRUCTOR(DerivedConcaveFaceSet); }
  static void initClass() { SO_NODE_INIT_CLASS(DerivedConcaveFaceSet,SoFaceSet,"FaceSet"); }
};
SO_NODE_SOURCE(DerivedConcaveFaceSet);

namespace {
constexpr int side=96;
bool canonicalRaster=false;
CoinRenderOptions concaveOptions() {
  CoinRenderOptions options=samplingTestOptions();
  options.rasterizationPolicy=canonicalRaster ?
    COIN_RENDER_RASTER_CANONICAL_8BIT : COIN_RENDER_RASTER_NATIVE;
  return options;
}
bool check(bool good,const std::string & why) {
  if (!good) std::cerr << "concave stroke: " << why << '\n';
  return good;
}
const SbVec3f ring[6]={SbVec3f(-.8f,-.8f,0),SbVec3f(.8f,-.8f,.09f),
  SbVec3f(.8f,-.25f,-.07f),SbVec3f(-.25f,-.25f,.08f),
  SbVec3f(-.25f,.8f,-.04f),SbVec3f(-.8f,.8f,.03f)};
const SbVec3f uRing[8]={SbVec3f(-.8f,.8f,0),SbVec3f(-.45f,.8f,0),
  SbVec3f(-.45f,-.7f,0),SbVec3f(.45f,-.7f,0),
  SbVec3f(.45f,.8f,0),SbVec3f(.8f,.8f,0),
  SbVec3f(.8f,-.9f,0),SbVec3f(-.8f,-.9f,0)};
struct Fixture {
  SoSeparator * root=new SoSeparator;
  SoDrawStyle * style=new SoDrawStyle;
  SoLightModel * light=new SoLightModel;
  SoEnvironment * fog=new SoEnvironment;
  SoComplexity * complexity=new SoComplexity;
  SoPolygonOffset * offset=new SoPolygonOffset;
  SoClipPlane * clip=new SoClipPlane;
  SoShapeHints * hints=new SoShapeHints;
  SoMaterialBinding * materialBinding=new SoMaterialBinding;
  SoNormalBinding * normalBinding=new SoNormalBinding;
  SoMaterial * material=new SoMaterial;
  SoTextureUnit * unit3=new SoTextureUnit;
  SoTexture2 * texture0=new SoTexture2;
  SoTexture2 * texture3=new SoTexture2;
  SoTextureCoordinate2 * uv0=new SoTextureCoordinate2;
  SoTextureCoordinate2 * uv3=new SoTextureCoordinate2;
  SoCoordinate3 * positions=new SoCoordinate3;
  int shapeKind;
  explicit Fixture(int kind):shapeKind(kind) {
    root->ref();
    auto * camera=new SoOrthographicCamera;
    camera->height=2;camera->position.setValue(0,0,3);
    camera->nearDistance=.1f;camera->farDistance=10;
    root->addChild(camera);
    auto * directional=new SoDirectionalLight;
    directional->direction.setValue(.2f,-.3f,-1);
    root->addChild(directional);
    root->addChild(light);
    fog->fogType=SoEnvironment::NONE;
    fog->fogColor.setValue(.16f,.2f,.27f);
    fog->fogVisibility=8;
    root->addChild(fog);
    complexity->textureQuality=.8f;
    root->addChild(complexity);
    material->ambientColor.setValue(.25f,.25f,.25f);
    material->specularColor.setValue(0,0,0);
    const SbColor color[]={SbColor(.94f,.3f,.26f),SbColor(.3f,.92f,.34f),
      SbColor(.32f,.45f,.96f),SbColor(.92f,.8f,.3f),
      SbColor(.84f,.35f,.83f),SbColor(.32f,.87f,.83f)};
    material->diffuseColor.setValues(0,6,color);
    root->addChild(material);
    root->addChild(materialBinding);
    auto * normals=new SoNormal;
    const SbVec3f n[]={SbVec3f(.1f,.02f,1),SbVec3f(-.13f,.05f,1),
      SbVec3f(.06f,-.17f,1),SbVec3f(-.11f,-.05f,1),
      SbVec3f(.16f,.09f,1),SbVec3f(-.07f,.15f,1)};
    normals->vector.setValues(0,6,n);
    root->addChild(normals);
    root->addChild(normalBinding);
    style->lineWidth=3;style->pointSize=5;
    root->addChild(style);
    offset->styles=SoPolygonOffset::LINES|SoPolygonOffset::POINTS;
    offset->on=FALSE;
    root->addChild(offset);
    clip->on=FALSE;
    clip->plane=SbPlane(SbVec3f(1,0,0),0);
    root->addChild(clip);
    hints->shapeType=SoShapeHints::UNKNOWN_SHAPE_TYPE;
    hints->vertexOrdering=SoShapeHints::UNKNOWN_ORDERING;
    hints->faceType=SoShapeHints::UNKNOWN_FACE_TYPE;
    root->addChild(hints);
    auto * unit0=new SoTextureUnit;unit0->unit=0;root->addChild(unit0);
    texture0->model=SoTexture2::MODULATE;
    const unsigned char a[]={240,210,170, 190,245,205, 205,185,245, 245,235,180};
    texture0->image.setValue(SbVec2s(2,2),3,a);root->addChild(texture0);
    const SbVec2f first[]={SbVec2f(.05f,.05f),SbVec2f(.95f,.05f),
      SbVec2f(.95f,.38f),SbVec2f(.38f,.38f),SbVec2f(.38f,.95f),SbVec2f(.05f,.95f)};
    uv0->point.setValues(0,6,first);root->addChild(uv0);
    unit3->unit=3;root->addChild(unit3);
    texture3->model=SoTexture2::MODULATE;
    const unsigned char b[]={195,235,240, 245,200,225, 220,245,180, 240,225,245};
    texture3->image.setValue(SbVec2s(2,2),3,b);root->addChild(texture3);
    const SbVec2f second[]={SbVec2f(.1f,.9f),SbVec2f(.9f,.9f),
      SbVec2f(.9f,.62f),SbVec2f(.62f,.62f),SbVec2f(.62f,.1f),SbVec2f(.1f,.1f)};
    uv3->point.setValues(0,6,second);root->addChild(uv3);
    positions->point.setValues(0,6,ring);root->addChild(positions);
    if (kind==1) {
      auto * face=new SoIndexedFaceSet;
      const int32_t indices[]={0,1,2,3,4,5,-1};
      face->coordIndex.setValues(0,7,indices);
      root->addChild(face);
    } else if (kind==2) {
      auto * face=new DerivedConcaveFaceSet;
      face->numVertices=6;root->addChild(face);
    } else {
      auto * face=new SoFaceSet;
      face->numVertices=6;root->addChild(face);
    }
  }
  ~Fixture() { root->unref(); }
};
double distanceToSegment(double x,double y,const SbVec3f & a,const SbVec3f & b) {
  const double ax=(a[0]+1)*side/2,ay=(a[1]+1)*side/2;
  const double bx=(b[0]+1)*side/2,by=(b[1]+1)*side/2;
  const double dx=bx-ax,dy=by-ay;
  const double t=std::max(0.,std::min(1.,((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy)));
  return std::hypot(x-(ax+t*dx),y-(ay+t*dy));
}
bool contourOracle(const std::vector<unsigned char> & rgba,int style,bool clipped,
                   const std::string & name) {
  if (rgba.size()!=size_t(side*side*4)) return check(false,name+" image size");
  size_t outside=0,covered=0;
  for(int y=4;y<side-4;++y) for(int x=4;x<side-4;++x) {
    const size_t at=size_t((side-1-y)*side+x)*4;
    const bool drawn=rgba[at]+rgba[at+1]+rgba[at+2]>12;
    if (!drawn) continue;
    ++covered;
    double nearest=1e9;
    if (style==SoDrawStyle::LINES) {
      for(int i=0;i<6;++i)
        nearest=std::min(nearest,distanceToSegment(x+.5,y+.5,ring[i],ring[(i+1)%6]));
      if (clipped) {
        const SbVec3f a(0,-.8f,0),b(0,-.25f,0);
        nearest=std::min(nearest,distanceToSegment(x+.5,y+.5,a,b));
      }
    } else {
      for(const auto & p : ring)
        nearest=std::min(nearest,std::hypot(x+.5-(p[0]+1)*side/2,
                                             y+.5-(p[1]+1)*side/2));
      if (clipped) for(const SbVec3f & p : {SbVec3f(0,-.8f,0),SbVec3f(0,-.25f,0)})
        nearest=std::min(nearest,std::hypot(x+.5-(p[0]+1)*side/2,
                                             y+.5-(p[1]+1)*side/2));
    }
    outside+=nearest>5;
  }
  size_t missingWitnesses=0;
  if (!clipped) for(int i=0;i<6;++i) {
    const SbVec3f witness=style==SoDrawStyle::LINES ?
      (ring[i]+ring[(i+1)%6])*.5f : ring[i];
    const int wx=int(std::lround((witness[0]+1)*side/2));
    const int wy=int(std::lround((witness[1]+1)*side/2));
    bool visible=false;
    for(int dy=-4;dy<=4;++dy) for(int dx=-4;dx<=4;++dx) {
      const int x=wx+dx,y=wy+dy;
      if (x<0 || y<0 || x>=side || y>=side) continue;
      const size_t at=size_t((side-1-y)*side+x)*4;
      visible |= rgba[at]+rgba[at+1]+rgba[at+2]>12;
    }
    missingWitnesses+=!visible;
  }
  std::cout << "concave_stroke_contour " << name << " covered=" << covered
            << " outside_original=" << outside
            << " missing_edge_or_point_witnesses=" << missingWitnesses << '\n';
  return check(covered>(style==SoDrawStyle::LINES ? 180u : 30u) && outside==0 &&
               missingWitnesses==0,
               name+" must contain only original contour edges or vertices");
}
bool compareGl(const std::vector<unsigned char> & gpu,const unsigned char * gl,
               const std::string & name) {
  if (gpu.size()!=size_t(side*side*4) || !gl) return check(false,name+" readback");
  static bool pointTransitionReported=false;
  static bool bindingReported=false,stateReported=false;
  const bool pointProbe=!pointTransitionReported &&
    name.find("transition/step=2/GPU")!=std::string::npos;
  const bool bindingProbe=!bindingReported &&
    name=="binding/shape=0/stroke=1/material=overall/normal=overall";
  const bool stateProbe=!stateReported &&
    name=="state/shape=0/clip=0/offset=0/cull=0";
  struct ColorMismatch {
    int x,y,delta;bool gpuCovered,glCovered;
    int gpu[3],reference[3];
  };
  std::vector<ColorMismatch> colorSamples;
  size_t gpuOnlyCount=0,glOnlyCount=0;
  std::vector<std::string> exclusivePixels;
  int maximum=0,worstX=0,worstY=0;
  size_t coveredMismatch=0,bad=0;
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    const size_t ga=size_t((side-1-y)*side+x)*4,na=size_t(y*side+x)*3;
    const bool a=gpu[ga]+gpu[ga+1]+gpu[ga+2]>12;
    const bool b=gl[na]+gl[na+1]+gl[na+2]>12;
    coveredMismatch+=a!=b;
    if(pointProbe && a!=b) {
      if(a) ++gpuOnlyCount; else ++glOnlyCount;
      if(exclusivePixels.size()<40)
        exclusivePixels.push_back(std::string(a ? "GPU-only(" : "CoinGL-only(")+
          std::to_string(x)+","+std::to_string(y)+")");
    }
    int pixelMaximum=0;
    for(int c=0;c<3;++c) {
      const int delta=std::abs(int(gpu[ga+c])-int(gl[na+c]));
      bad+=delta>4;
      pixelMaximum=std::max(pixelMaximum,delta);
      if(delta>maximum) {maximum=delta;worstX=x;worstY=y;}
    }
    if(bindingProbe || stateProbe)
      colorSamples.push_back({x,y,pixelMaximum,a,b,
        {int(gpu[ga]),int(gpu[ga+1]),int(gpu[ga+2])},
        {int(gl[na]),int(gl[na+1]),int(gl[na+2])}});
  }
  const bool good=coveredMismatch==0 && bad==0;
  if(bindingProbe || stateProbe) {
    if(bindingProbe) bindingReported=true; else stateReported=true;
    std::sort(colorSamples.begin(),colorSamples.end(),
      [](const ColorMismatch & a,const ColorMismatch & b) {
        if(a.delta!=b.delta) return a.delta>b.delta;
        return a.y==b.y ? a.x<b.x : a.y<b.y;
      });
    std::cout << "concave_stroke_rgb_worst " << name << '\n';
    for(size_t i=0;i<std::min(size_t(5),colorSamples.size());++i) {
      const auto & p=colorSamples[i];
      const char * coverage=p.gpuCovered ? (p.glCovered ? "both" : "GPU-only") :
        (p.glCovered ? "CoinGL-only" : "neither");
      std::cout << "  x=" << p.x << " y=" << p.y << " delta=" << p.delta
                << " GPU=" << p.gpu[0] << ',' << p.gpu[1] << ',' << p.gpu[2]
                << " CoinGL=" << p.reference[0] << ',' << p.reference[1] << ','
                << p.reference[2] << " coverage=" << coverage << '\n';
    }
  }
  if(pointProbe) {
    pointTransitionReported=true;
    std::cout << "concave_stroke_point_transition_coverage GPU_only=" << gpuOnlyCount
              << " CoinGL_only=" << glOnlyCount << " listed=" << exclusivePixels.size() << '\n';
    for(const auto & entry:exclusivePixels) std::cout << "  " << entry << '\n';
    for(int i=0;i<6;++i)
      std::cout << "  ring_vertex=" << i << " window=("
                << (ring[i][0]+1)*side/2 << ',' << (ring[i][1]+1)*side/2
                << ") rounded=(" << std::lround((ring[i][0]+1)*side/2) << ','
                << std::lround((ring[i][1]+1)*side/2) << ")\n";
  }
  std::cout << "concave_stroke_gl " << name << " max=" << maximum
            << " bad4=" << bad << " coverage_mismatch=" << coveredMismatch
            << " worst=" << worstX << ',' << worstY << " pass=" << good << '\n';
  return check(good,name+" CoinRender must match CoinGL for color and coverage");
}
bool compareCpuGpu(const std::vector<unsigned char> & gpu,
                   const std::vector<unsigned char> & cpu,const std::string & name) {
  if(gpu.size()!=size_t(side*side*4) || cpu.size()!=gpu.size())
    return check(false,name+" CPU/GPU RGBA readback size");
  int maximum=0;size_t bad4=0,coverageMismatch=0,exactDifference=0;
  for(size_t p=0;p<gpu.size();p+=4) {
    const bool a=gpu[p]+gpu[p+1]+gpu[p+2]>12;
    const bool b=cpu[p]+cpu[p+1]+cpu[p+2]>12;
    coverageMismatch+=a!=b;
    for(int c=0;c<4;++c) {
      const int delta=std::abs(int(gpu[p+c])-int(cpu[p+c]));
      maximum=std::max(maximum,delta);bad4+=delta>4;exactDifference+=delta!=0;
    }
  }
  std::cout << "concave_stroke_cpu_gpu " << name << " policy="
            << (canonicalRaster ? "CANONICAL_8BIT" : "NATIVE")
            << " max_rgba=" << maximum << " channels_over4=" << bad4
            << " channels_nonzero=" << exactDifference
            << " coverage_mismatch=" << coverageMismatch << '\n';
  return check(coverageMismatch==0 && bad4==0,
    name+" CPU and GPU must agree on RGBA and coverage");
}
// Coin's clipped U has two disconnected pieces above y=0. The empty gap
// between the legs must not acquire a tessellation edge or a clip bridge.
bool splitUOracle(const std::vector<unsigned char> & rgba,const std::string & name) {
  if (rgba.size()!=size_t(side*side*4)) return check(false,name+" image size");
  auto drawn=[&](int x,int y) {
    const size_t p=size_t((side-1-y)*side+x)*4;
    return rgba[p]+rgba[p+1]+rgba[p+2]>12;
  };
  size_t left=0,right=0,bridge=0,bridgeAtCut=0;
  for(int y=52;y<84;++y) for(int x=10;x<86;++x) if (drawn(x,y)) {
    if (x<36) ++left;
    else if (x>60) ++right;
  }
  // The two clipped legs cannot be joined anywhere in the central gap.
  // Include the clip boundary itself: a fabricated closing edge lies at y=48
  // and would evade a witness restricted to the interior of the upper legs.
  for(int y=0;y<side;++y) for(int x=36;x<=60;++x) if(drawn(x,y)) {
    ++bridge;
    bridgeAtCut+=y>=45 && y<=51;
  }
  std::cout << "concave_stroke_split " << name << " left=" << left
            << " right=" << right << " bridge=" << bridge
            << " bridge_at_cut=" << bridgeAtCut << '\n';
  return check(left>40 && right>40 && bridge==0,
               name+" must show both clipped components without a bridge");
}
using NativeTriangle=std::array<SbVec3f,3>;
void nativeTriangle(void * user,SoCallbackAction *,const SoPrimitiveVertex * a,
                    const SoPrimitiveVertex * b,const SoPrimitiveVertex * c) {
  static_cast<std::vector<NativeTriangle> *>(user)->push_back({{a->getPoint(),b->getPoint(),c->getPoint()}});
}
bool samePosition(const SbVec3f & a,const SbVec3f & b) {
  return (a-b).length()<1e-5f;
}
double nativeTriangleSlope(const NativeTriangle & t) {
  const auto sx=[](const SbVec3f & p) {return (double(p[0])+1)*side/2;};
  const auto sy=[](const SbVec3f & p) {return (double(p[1])+1)*side/2;};
  const auto dz=[](const SbVec3f & p) {return (3.-double(p[2])-.1)/9.9;};
  const double x1=sx(t[1])-sx(t[0]),y1=sy(t[1])-sy(t[0]),z1=dz(t[1])-dz(t[0]);
  const double x2=sx(t[2])-sx(t[0]),y2=sy(t[2])-sy(t[0]),z2=dz(t[2])-dz(t[0]);
  const double det=x1*y2-x2*y1;
  if(std::abs(det)<1e-10) return -1;
  return std::max(std::abs((z1*y2-z2*y1)/det),std::abs((x1*z2-x2*z1)/det));
}
struct GlDepthCapture {std::vector<float> depth;GLint bits=0;GLenum error=GL_NO_ERROR;bool seen=false;};
void captureGlDepth(void * user,SoAction * current) {
  if(!current->isOfType(SoGLRenderAction::getClassTypeId())) return;
  auto & result=*static_cast<GlDepthCapture *>(user);
  result.depth.resize(size_t(side*side));
  glGetIntegerv(GL_DEPTH_BITS,&result.bits);
  glReadPixels(0,0,side,side,GL_DEPTH_COMPONENT,GL_FLOAT,result.depth.data());
  result.error=glGetError();result.seen=true;
}
bool slopeOracle(Fixture & scene,CoinRenderAction & action,CoinRenderTarget & target,
                 SoOffscreenRenderer & gl) {
  scene.style->style=SoDrawStyle::LINES;
  scene.light->model=SoLightModel::BASE_COLOR;
  scene.fog->fogType=SoEnvironment::NONE;
  scene.complexity->textureQuality=0;
  scene.offset->on=TRUE;scene.offset->styles=SoPolygonOffset::LINES;
  scene.offset->units=0;
  std::vector<NativeTriangle> nativeTriangles;
  SoCallbackAction native;
  native.addTriangleCallback(SoFaceSet::getClassTypeId(),nativeTriangle,&nativeTriangles);
  native.apply(scene.root);
  GlDepthCapture glCapture;
  auto * callback=new SoCallback;
  callback->setCallback(captureGlDepth,&glCapture);
  scene.root->addChild(callback);
  bool ok=check(nativeTriangles.size()>=4,"native callback must expose nonplanar tessellation");
  const float factors[]={0.f,12.f,-12.f};
  std::vector<float> depths[3];
  std::vector<float> nativeDepths[3];
  GLint nativeBits[3]={0,0,0};
  std::vector<unsigned char> colors[3];
  for(int i=0;i<3;++i) {
    scene.offset->factor=factors[i];action.apply(scene.root);
    target.readbackDepth(depths[i]);target.readbackRGBA(colors[i]);
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
      depths[i].size()==size_t(side*side) && colors[i].size()==size_t(side*side*4),
      "nonplanar stroke factor must publish depth and color");
    glCapture=GlDepthCapture{};
    const bool nativeRendered=gl.render(scene.root);
    nativeDepths[i]=glCapture.depth;nativeBits[i]=glCapture.bits;
    ok &= check(nativeRendered && glCapture.seen && glCapture.error==GL_NO_ERROR &&
      glCapture.bits>0 && glCapture.depth.size()==size_t(side*side),
      "CoinGL callback must read a valid depth buffer after the stroke");
  }
  size_t witnesses=0;
  for(int e=0;e<6;++e) {
    std::vector<double> owners;
    for(const auto & t:nativeTriangles) {
      bool first=false,second=false;
      for(const auto & p:t) {
        first|=samePosition(p,ring[e]);second|=samePosition(p,ring[(e+1)%6]);
      }
      if(first && second) owners.push_back(nativeTriangleSlope(t));
    }
    if(owners.size()!=1 || owners[0]<=0) continue;
    const SbVec3f midpoint=(ring[e]+ring[(e+1)%6])*.5f;
    const int mx=int(std::lround((midpoint[0]+1)*side/2));
    const int my=int(std::lround((midpoint[1]+1)*side/2));
    double best=1e9;size_t index=0;
    for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
      const int x=mx+dx,y=my+dy;
      if(x<0 || y<0 || x>=side || y>=side) continue;
      const size_t p=size_t(side-1-y)*side+x;
      const size_t c=p*4;
      if(colors[0].size()!=size_t(side*side*4) ||
         colors[0][c]+colors[0][c+1]+colors[0][c+2]<12) continue;
      const double distance=std::hypot(double(dx),double(dy));
      if(distance<best) {best=distance;index=p;}
    }
    if(best==1e9 || depths[0].size()!=size_t(side*side)) continue;
    const double expected=12*owners[0];
    const double positive=double(depths[1][index])-double(depths[0][index]);
    const double negative=double(depths[2][index])-double(depths[0][index]);
    std::cout << "concave_stroke_slope edge=" << e << " owner_m=" << owners[0]
              << " expected_delta=" << expected << " positive=" << positive
              << " negative=" << negative << " pixel=" << index << '\n';
    if(nativeDepths[0].size()==size_t(side*side) &&
       nativeDepths[1].size()==size_t(side*side) &&
       nativeDepths[2].size()==size_t(side*side)) {
      const size_t glIndex=size_t(side-1-int(index/side))*side+index%side;
      const double nativePositive=double(nativeDepths[1][glIndex])-nativeDepths[0][glIndex];
      const double nativeNegative=double(nativeDepths[2][glIndex])-nativeDepths[0][glIndex];
      std::cout << "concave_stroke_native_slope edge=" << e << " depth_bits=" << nativeBits[0]
                << " positive=" << nativePositive << " negative=" << nativeNegative
                << " expected=" << expected << " positive_difference=" << nativePositive-expected
                << " negative_difference=" << nativeNegative+expected << '\n';
    }
    ok &= check(std::abs(positive-expected)<2e-4 &&
      std::abs(negative+expected)<2e-4,
      "stroke depth delta must equal factor times native owner triangle slope");
    ++witnesses;
  }
  ok &= check(witnesses>=3,"at least three native-owned original edges need depth witnesses");
  scene.style->style=SoDrawStyle::POINTS;
  scene.offset->styles=SoPolygonOffset::POINTS;
  for(int i=0;i<3;++i) {
    scene.offset->factor=factors[i];action.apply(scene.root);
    depths[i].clear();colors[i].clear();
    target.readbackDepth(depths[i]);target.readbackRGBA(colors[i]);
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
      depths[i].size()==size_t(side*side) && colors[i].size()==size_t(side*side*4),
      "nonplanar point factor must publish depth and color");
    glCapture=GlDepthCapture{};
    const bool nativeRendered=gl.render(scene.root);
    nativeDepths[i]=glCapture.depth;nativeBits[i]=glCapture.bits;
    ok &= check(nativeRendered && glCapture.seen && glCapture.error==GL_NO_ERROR &&
      glCapture.bits>0 && glCapture.depth.size()==size_t(side*side),
      "CoinGL callback must read valid point depth after the stroke");
  }
  size_t pointWitnesses=0;
  for(int v=0;v<6;++v) {
    auto edgeSlope=[&](int e) {
      double slope=-1;size_t owners=0;
      for(const auto & t:nativeTriangles) {
        bool first=false,second=false;
        for(const auto & p:t) {
          first|=samePosition(p,ring[e]);second|=samePosition(p,ring[(e+1)%6]);
        }
        if(first && second) {slope=nativeTriangleSlope(t);++owners;}
      }
      return owners==1 ? slope : -1.;
    };
    const double incoming=edgeSlope((v+5)%6),outgoing=edgeSlope(v);
    std::vector<double> incidentSlopes;
    for(const auto & t:nativeTriangles) {
      bool ownsVertex=false;
      for(const auto & p:t) ownsVertex|=samePosition(p,ring[v]);
      if(ownsVertex) incidentSlopes.push_back(nativeTriangleSlope(t));
    }
    ok &= check(!incidentSlopes.empty() &&
      std::all_of(incidentSlopes.begin(),incidentSlopes.end(),
        [](double slope) {return std::isfinite(slope) && slope>0;}),
      "native callback must identify positive slopes for each incident point triangle");
    if(incidentSlopes.empty()) continue;
    const double least=*std::min_element(incidentSlopes.begin(),incidentSlopes.end());
    const double greatest=*std::max_element(incidentSlopes.begin(),incidentSlopes.end());
    const int mx=int(std::lround((ring[v][0]+1)*side/2));
    const int my=int(std::lround((ring[v][1]+1)*side/2));
    double best=1e9;size_t index=0;
    for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
      const int x=mx+dx,y=my+dy;
      if(x<0 || y<0 || x>=side || y>=side) continue;
      const size_t p=size_t(side-1-y)*side+x,c=p*4;
      if(colors[0].size()!=size_t(side*side*4) ||
         colors[0][c]+colors[0][c+1]+colors[0][c+2]<12) continue;
      const double distance=std::hypot(double(dx),double(dy));
      if(distance<best) {best=distance;index=p;}
    }
    if(best==1e9 || depths[0].size()!=size_t(side*side) ||
       nativeDepths[0].size()!=size_t(side*side) ||
       nativeDepths[1].size()!=size_t(side*side) ||
       nativeDepths[2].size()!=size_t(side*side)) continue;
    const size_t glIndex=size_t(side-1-int(index/side))*side+index%side;
    const double positive=double(depths[1][index])-depths[0][index];
    const double negative=double(depths[2][index])-depths[0][index];
    const double nativePositive=double(nativeDepths[1][glIndex])-nativeDepths[0][glIndex];
    const double nativeNegative=double(nativeDepths[2][glIndex])-nativeDepths[0][glIndex];
    // All incident point fragments share the original vertex depth. Under
    // LESS the positive factor selects the smallest m; the negative factor
    // selects the largest m. This follows from depth ordering, independent
    // of the driver's triangle submission order.
    const double expectedPositive=12*least,expectedNegative=-12*greatest;
    std::cout << "concave_stroke_point_slope vertex=" << v << " incoming_m=" << incoming
              << " outgoing_m=" << outgoing << " incident_count=" << incidentSlopes.size()
              << " incident_min=" << least << " incident_max=" << greatest
              << " expected_positive=" << expectedPositive
              << " expected_negative=" << expectedNegative << " gpu_positive=" << positive
              << " gpu_negative=" << negative << " gl_positive=" << nativePositive
              << " gl_negative=" << nativeNegative << " gl_depth_bits=" << nativeBits[0]
              << " pixel=" << index << '\n';
    ok &= check(std::abs(positive-expectedPositive)<2e-4 &&
      std::abs(negative-expectedNegative)<2e-4,
      "point depth must select the winning incident triangle for each factor sign");
    ok &= check(std::abs(nativePositive-expectedPositive)<2e-4 &&
      std::abs(nativeNegative-expectedNegative)<2e-4,
      "CoinGL point depth must select the expected incident triangle");
    ++pointWitnesses;
  }
  ok &= check(pointWitnesses>=4,"at least four original point interiors need depth witnesses");
  scene.root->removeChild(callback);
  scene.offset->on=FALSE;
  return ok;
}
SoSeparator * makeSplitU() {
  auto * root=new SoSeparator;root->ref();
  auto * camera=new SoOrthographicCamera;camera->height=2;
  camera->position.setValue(0,0,3);camera->nearDistance=.1f;camera->farDistance=10;
  root->addChild(camera);
  auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
  auto * color=new SoMaterial;color->diffuseColor.setValue(.85f,.25f,.2f);root->addChild(color);
  auto * stroke=new SoDrawStyle;stroke->style=SoDrawStyle::LINES;stroke->lineWidth=3;
  root->addChild(stroke);
  auto * hints=new SoShapeHints;hints->faceType=SoShapeHints::UNKNOWN_FACE_TYPE;
  root->addChild(hints);
  auto * clip=new SoClipPlane;clip->plane=SbPlane(SbVec3f(0,1,0),0);
  root->addChild(clip);
  auto * coords=new SoCoordinate3;coords->point.setValues(0,8,uRing);root->addChild(coords);
  auto * face=new SoFaceSet;face->numVertices=8;root->addChild(face);
  return root;
}
struct Occurrences {
  SoSeparator * root=new SoSeparator;
  SoTextureCoordinate2 * uv=new SoTextureCoordinate2;
  Occurrences() {
    root->ref();
    auto * camera=new SoOrthographicCamera;camera->height=2;
    camera->position.setValue(0,0,3);camera->nearDistance=.1f;camera->farDistance=10;
    root->addChild(camera);
    auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
    auto * material=new SoMaterial;
    const SbColor colors[]={SbColor(.9f,.2f,.2f),SbColor(.9f,.2f,.2f),
      SbColor(.9f,.2f,.2f),SbColor(.2f,.3f,.9f),SbColor(.2f,.3f,.9f),
      SbColor(.2f,.3f,.9f)};
    material->diffuseColor.setValues(0,6,colors);root->addChild(material);
    auto * binding=new SoMaterialBinding;binding->value=SoMaterialBinding::PER_VERTEX;
    root->addChild(binding);
    auto * style=new SoDrawStyle;style->style=SoDrawStyle::LINES;style->lineWidth=3;
    root->addChild(style);
    auto * texture=new SoTexture2;texture->model=SoTexture2::MODULATE;
    const unsigned char tex[]={255,255,255, 255,100,100, 100,100,255, 255,255,100};
    texture->image.setValue(SbVec2s(2,2),3,tex);root->addChild(texture);
    const SbVec2f coords[]={SbVec2f(.25f,.25f),SbVec2f(.25f,.25f),SbVec2f(.25f,.25f),
      SbVec2f(.75f,.75f),SbVec2f(.75f,.75f),SbVec2f(.75f,.75f)};
    uv->point.setValues(0,6,coords);root->addChild(uv);
    const SbVec3f p[]={SbVec3f(-.8f,-.6f,0),SbVec3f(0,0,0),
      SbVec3f(-.8f,.6f,0),SbVec3f(0,0,0),SbVec3f(.8f,.6f,0),
      SbVec3f(.8f,-.6f,0)};
    auto * positions=new SoCoordinate3;positions->point.setValues(0,6,p);
    root->addChild(positions);
    auto * faces=new SoFaceSet;const int32_t counts[]={3,3};
    faces->numVertices.setValues(0,2,counts);root->addChild(faces);
  }
  ~Occurrences() {root->unref();}
};
bool occurrenceOracle(const std::vector<unsigned char> & image,const std::string & name) {
  if(image.size()!=size_t(side*side*4)) return check(false,name+" image size");
  auto sample=[&](int x,int y,int c) {return image[(size_t(side-1-y)*side+x)*4+c];};
  // Distinct occurrences at (0,0) lead to independently authored left/red
  // and right/blue edges. Sample away from the ambiguous shared pixel.
  int leftRed=0,rightBlue=0;
  const int witnesses[][2]={{29,34},{29,62},{67,62},{67,34}};
  for(int i=0;i<4;++i) for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx) {
    const int x=witnesses[i][0]+dx,y=witnesses[i][1]+dy;
    if(i<2) leftRed+=sample(x,y,0)>sample(x,y,2)+25;
    else rightBlue+=sample(x,y,2)>sample(x,y,0)+25;
  }
  std::cout << "concave_stroke_occurrences " << name << " left_red=" << leftRed
            << " right_blue=" << rightBlue << '\n';
  return check(leftRed>8 && rightBlue>8,
               name+" must retain distinct materials on coincident occurrences");
}
std::vector<unsigned char> normalizeGl(const unsigned char * rgb) {
  std::vector<unsigned char> rgba(size_t(side*side*4));
  if(!rgb) return {};
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    const size_t source=size_t(y*side+x)*3;
    const size_t dest=size_t((side-1-y)*side+x)*4;
    for(int c=0;c<3;++c) rgba[dest+c]=rgb[source+c];
    rgba[dest+3]=255;
  }
  return rgba;
}
bool textureLineProbe(CoinRenderAction & action,CoinRenderTarget & target,
                      CoinRenderAction & cpuAction,CoinRenderTarget & cpu,
                      SoOffscreenRenderer & gl) {
  bool ok=true;
  const unsigned char firstImage[]={240,210,170,190,245,205,205,185,245,245,235,180};
  const unsigned char thirdImage[]={195,235,240,245,200,225,220,245,180,240,225,245};
  for(int lighting : {SoLightModel::BASE_COLOR,SoLightModel::PHONG})
    for(int textureMask=0;textureMask<4;++textureMask)
      for(bool constantUV : {false,true})
        for(float quality : {.1f,.3f,.8f}) {
          // Fresh image nodes prevent CoinGL's texture-object quality cache
          // from making the next quality band inherit the previous filter.
          Fixture scene(0);
          scene.style->style=SoDrawStyle::LINES;
          scene.light->model=lighting;
          scene.fog->fogType=SoEnvironment::NONE;
          scene.complexity->textureQuality=quality;
          scene.materialBinding->value=SoMaterialBinding::OVERALL;
          scene.normalBinding->value=SoNormalBinding::OVERALL;
          if(!(textureMask&1)) scene.texture0->image.setValue(SbVec2s(0,0),0,nullptr);
          else scene.texture0->image.setValue(SbVec2s(2,2),3,firstImage);
          if(!(textureMask&2)) scene.texture3->image.setValue(SbVec2s(0,0),0,nullptr);
          else scene.texture3->image.setValue(SbVec2s(2,2),3,thirdImage);
          if(constantUV) for(int i=0;i<6;++i) {
            scene.uv0->point.set1Value(i,SbVec2f(.25f,.25f));
            scene.uv3->point.set1Value(i,SbVec2f(.75f,.75f));
          }
          const std::string name="texture_line/light="+std::to_string(lighting)+
            "/mask="+std::to_string(textureMask)+"/constant_uv="+
            std::to_string(constantUV)+"/quality="+std::to_string(quality);
          action.apply(scene.root);cpuAction.apply(scene.root);
          std::vector<unsigned char> image,cpuImage;
          target.readbackRGBA(image);cpu.readbackRGBA(cpuImage);
          const bool native=gl.render(scene.root) && gl.getBuffer();
          const bool captured=native && action.getLastStatus()==CoinRenderAction::SUCCESS &&
            cpuAction.getLastStatus()==CoinRenderAction::SUCCESS &&
            image.size()==size_t(side*side*4) && cpuImage.size()==size_t(side*side*4);
          ok &= check(captured,name+" must render on CPU, GPU and CoinGL");
          if(!captured) continue;
          int bothMaximum=0;
          size_t bothCovered=0;
          for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
            const size_t p=(size_t(side-1-y)*side+x)*4,g=(size_t(y)*side+x)*3;
            const bool core=image[p]+image[p+1]+image[p+2]>12;
            const bool reference=gl.getBuffer()[g]+gl.getBuffer()[g+1]+gl.getBuffer()[g+2]>12;
            if(!core || !reference) continue;
            ++bothCovered;
            for(int c=0;c<3;++c)
              bothMaximum=std::max(bothMaximum,
                std::abs(int(image[p+c])-int(gl.getBuffer()[g+c])));
          }
          const size_t center=(size_t(side-1-8)*side+13)*4;
          const size_t glCenter=(size_t(8)*side+13)*3;
          std::cout << "concave_stroke_texture_line " << name
                    << " both_covered=" << bothCovered << " both_max=" << bothMaximum
                    << " sample13_8_GPU=" << int(image[center]) << ','
                    << int(image[center+1]) << ',' << int(image[center+2])
                    << " CoinGL=" << int(gl.getBuffer()[glCenter]) << ','
                    << int(gl.getBuffer()[glCenter+1]) << ','
                    << int(gl.getBuffer()[glCenter+2]) << '\n';
          ok &= compareGl(image,gl.getBuffer(),name+"/GPU");
          ok &= compareGl(cpuImage,gl.getBuffer(),name+"/CPU");
          ok &= compareCpuGpu(image,cpuImage,name);
        }
  return ok;
}
struct NormalProbe {
  GLfloat gl[3]={0,0,0};
  SbVec3f authored;
  bool gotGL=false,gotAuthored=false;
};
void captureCurrentNormal(void * user,SoAction * current) {
  auto & result=*static_cast<NormalProbe *>(user);
  if(current->isOfType(SoGLRenderAction::getClassTypeId())) {
    glGetFloatv(GL_CURRENT_NORMAL,result.gl);result.gotGL=true;
  } else if(current->getTypeId()==SoCallbackAction::getClassTypeId()) {
    const auto * normals=SoNormalElement::getInstance(current->getState());
    if(normals && normals->getNum()>0) {
      result.authored=normals->get(0);result.gotAuthored=true;
    }
  }
}
bool overallNormalPrecursorProbe(CoinRenderAction & action,CoinRenderTarget & target,
                                SoOffscreenRenderer & gl) {
  bool ok=true;
  std::vector<unsigned char> samples[3];
  for(int pass=0;pass<3;++pass) {
    Fixture scene(0);
    scene.style->style=SoDrawStyle::LINES;
    scene.light->model=SoLightModel::PHONG;
    scene.fog->fogType=SoEnvironment::NONE;
    scene.complexity->textureQuality=0;
    scene.materialBinding->value=SoMaterialBinding::OVERALL;
    scene.normalBinding->value=SoNormalBinding::OVERALL;
    if(pass==1) {
      auto * precursor=new SoSeparator;
      auto * normals=new SoNormal;
      const SbVec3f foreign[]={SbVec3f(.7f,-.6f,.1f),
        SbVec3f(.7f,-.6f,.1f),SbVec3f(.7f,-.6f,.1f)};
      normals->vector.setValues(0,3,foreign);precursor->addChild(normals);
      auto * binding=new SoNormalBinding;
      binding->value=SoNormalBinding::PER_VERTEX;precursor->addChild(binding);
      auto * coords=new SoCoordinate3;
      const SbVec3f points[]={SbVec3f(-.98f,.86f,0),SbVec3f(-.86f,.86f,0),
        SbVec3f(-.92f,.98f,0)};
      coords->point.setValues(0,3,points);precursor->addChild(coords);
      auto * shape=new SoFaceSet;shape->numVertices=3;precursor->addChild(shape);
      scene.root->insertChild(precursor,scene.root->getNumChildren()-2);
    }
    NormalProbe normal;
    auto * witness=new SoCallback;
    witness->setCallback(captureCurrentNormal,&normal);
    scene.root->insertChild(witness,scene.root->getNumChildren()-2);
    SoCallbackAction nativeState;nativeState.apply(scene.root);
    action.apply(scene.root);
    std::vector<unsigned char> image;target.readbackRGBA(image);
    const bool native=gl.render(scene.root) && gl.getBuffer();
    const std::string name="overall_normal/precursor="+std::to_string(pass==1)+
      "/pass="+std::to_string(pass);
    const bool captured=native && normal.gotGL && normal.gotAuthored &&
      action.getLastStatus()==CoinRenderAction::SUCCESS &&
      image.size()==size_t(side*side*4);
    ok &= check(captured,name+" must expose both normal states and RGB");
    if(!captured) continue;
    const size_t gpuAt=(size_t(side-1-8)*side+13)*4;
    const size_t glAt=(size_t(8)*side+13)*3;
    const int gpuRGB[]={image[gpuAt],image[gpuAt+1],image[gpuAt+2]};
    const int glRGB[]={gl.getBuffer()[glAt],gl.getBuffer()[glAt+1],
      gl.getBuffer()[glAt+2]};
    samples[pass].assign(image.begin()+gpuAt,image.begin()+gpuAt+3);
    std::cout << "concave_stroke_overall_normal " << name
              << " authored=" << normal.authored[0] << ',' << normal.authored[1]
              << ',' << normal.authored[2] << " GL_CURRENT_NORMAL="
              << normal.gl[0] << ',' << normal.gl[1] << ',' << normal.gl[2]
              << " GPU_RGB=" << gpuRGB[0] << ',' << gpuRGB[1] << ',' << gpuRGB[2]
              << " CoinGL_RGB=" << glRGB[0] << ',' << glRGB[1] << ',' << glRGB[2]
              << '\n';
    ok &= check(std::abs(normal.authored[0]-.1f)<1e-6f &&
      std::abs(normal.authored[1]-.02f)<1e-6f &&
      std::abs(normal.authored[2]-1.f)<1e-6f,
      "target authored OVERALL normal remains normal0 after the precursor");
    const int expected[]={234,83,74};
    for(int channel=0;channel<3;++channel)
      ok &= check(std::abs(gpuRGB[channel]-expected[channel])<=4 &&
        std::abs(glRGB[channel]-expected[channel])<=4,
        "OVERALL must light the target from authored normal0 independent of precursor") ;
  }
  ok &= check(samples[0].size()==3 && samples[1]==samples[0] && samples[2]==samples[0],
    "target GPU RGB must not inherit the precursor's normal");
  return ok;
}
struct PointMatrixProbe {
  GLdouble glModel[16]={0},glProjection[16]={0};
  GLint viewport[4]={0};
  SbMatrix coinModel,coinView,coinProjection;
  bool gotGL=false,gotCoin=false;
};
void capturePointMatrices(void * user,SoAction * current) {
  auto & result=*static_cast<PointMatrixProbe *>(user);
  if(current->isOfType(SoGLRenderAction::getClassTypeId())) {
    glGetDoublev(GL_MODELVIEW_MATRIX,result.glModel);
    glGetDoublev(GL_PROJECTION_MATRIX,result.glProjection);
    glGetIntegerv(GL_VIEWPORT,result.viewport);
    result.gotGL=true;
  } else if(current->getTypeId()==SoCallbackAction::getClassTypeId()) {
    result.coinModel=SoModelMatrixElement::get(current->getState());
    result.coinView=SoViewingMatrixElement::get(current->getState());
    result.coinProjection=SoProjectionMatrixElement::get(current->getState());
    result.gotCoin=true;
  }
}
struct PointBounds {int minX=side,minY=side,maxX=-1,maxY=-1;size_t count=0;};
PointBounds localPointBounds(const std::vector<unsigned char> & image,int cx,int cy) {
  PointBounds result;
  if(image.size()!=size_t(side*side*4)) return result;
  for(int y=std::max(0,cy-10);y<=std::min(side-1,cy+10);++y)
    for(int x=std::max(0,cx-6);x<=std::min(side-1,cx+6);++x) {
      const size_t p=(size_t(side-1-y)*side+x)*4;
      if(image[p]+image[p+1]+image[p+2]<=12) continue;
      result.minX=std::min(result.minX,x);result.maxX=std::max(result.maxX,x);
      result.minY=std::min(result.minY,y);result.maxY=std::max(result.maxY,y);
      ++result.count;
    }
  return result;
}
bool canonicalPointMask(const std::vector<unsigned char> & image,const Fixture & scene,
                        int pointSize,const std::string & name) {
  if(image.size()!=size_t(side*side*4)) return check(false,name+" canonical image size");
  // Public CANONICAL_8BIT contract: window centers snap to odd 1/256 values.
  // A point is the square of width pointSize around that center, tested at
  // pixel centers. This computes coverage from authored coordinates alone.
  int64_t centerX[6],centerY[6];
  for(int i=0;i<6;++i) {
    const SbVec3f p=scene.positions->point[i];
    const double x=(double(p[0])+1)*side/2,y=(double(p[1])+1)*side/2;
    centerX[i]=2*int64_t(std::floor(x*128))+1;
    centerY[i]=2*int64_t(std::floor(y*128))+1;
  }
  size_t expectedCount=0,mismatch=0;
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    const int64_t px=int64_t(x)*256+128,py=int64_t(y)*256+128;
    bool expected=false;
    for(int i=0;i<6;++i)
      expected|=px>=centerX[i]-int64_t(pointSize)*128 &&
        px<centerX[i]+int64_t(pointSize)*128 &&
        py>=centerY[i]-int64_t(pointSize)*128 &&
        py<centerY[i]+int64_t(pointSize)*128;
    const size_t p=(size_t(side-1-y)*side+x)*4;
    const bool actual=image[p]+image[p+1]+image[p+2]>12;
    expectedCount+=expected;mismatch+=expected!=actual;
  }
  std::cout << "concave_stroke_canonical_point_mask " << name
            << " expected_covered=" << expectedCount << " mismatched=" << mismatch << '\n';
  return check(expectedCount>0 && mismatch==0,
    name+" must match the independent snapped POINTS mask");
}
struct RepeatedDetailProof {
  bool seen=false,valid=false;
  int coordinate[6]={0},material[6]={0},texture[6]={0};
};
void repeatedFaceDetails(void * user,SoCallbackAction *,const SoPrimitiveVertex * a,
                         const SoPrimitiveVertex *,const SoPrimitiveVertex *) {
  auto & proof=*static_cast<RepeatedDetailProof *>(user);
  if(proof.seen) return;
  proof.seen=true;
  const SoDetail * detail=a->getDetail();
  if(!detail || !detail->isOfType(SoFaceDetail::getClassTypeId())) return;
  const auto * face=static_cast<const SoFaceDetail *>(detail);
  if(face->getNumPoints()!=6) return;
  for(int i=0;i<6;++i) {
    const auto * point=face->getPoint(i);
    proof.coordinate[i]=point->getCoordinateIndex();
    proof.material[i]=point->getMaterialIndex();
    proof.texture[i]=point->getTextureCoordIndex();
  }
  proof.valid=true;
}
bool pointRasterProbe(CoinRenderAction & action,CoinRenderTarget & target,
                      CoinRenderAction & cpuAction,CoinRenderTarget & cpu,
                      SoOffscreenRenderer & gl) {
  Fixture scene(0);
  scene.style->style=SoDrawStyle::POINTS;
  scene.light->model=SoLightModel::BASE_COLOR;
  scene.fog->fogType=SoEnvironment::NONE;
  scene.complexity->textureQuality=0;
  scene.offset->on=FALSE;
  PointMatrixProbe matrices;
  auto * callback=new SoCallback;
  callback->setCallback(capturePointMatrices,&matrices);
  scene.root->addChild(callback);
  bool ok=true;
  const float shifts[]={0.f,-1e-7f,1e-7f,-1e-5f,1e-5f};
  for(int size : {1,2,5,6}) for(int shiftIndex=0;shiftIndex<5;++shiftIndex) {
    const float shift=shifts[shiftIndex];
    scene.style->pointSize=float(size);
    for(int vertex : {2,3}) {
      SbVec3f position=ring[vertex];position[1]=-.25f+shift;
      scene.positions->point.set1Value(vertex,position);
    }
    matrices=PointMatrixProbe{};
    SoCallbackAction nativeState;nativeState.apply(scene.root);
    action.apply(scene.root);cpuAction.apply(scene.root);
    std::vector<unsigned char> image,cpuImage;
    target.readbackRGBA(image);cpu.readbackRGBA(cpuImage);
    const bool native=gl.render(scene.root) && gl.getBuffer();
    const std::vector<unsigned char> nativeImage=native ? normalizeGl(gl.getBuffer()) :
      std::vector<unsigned char>{};
    const std::string name="point_raster/size="+std::to_string(size)+
      "/shift_index="+std::to_string(shiftIndex);
    const bool captured=native && action.getLastStatus()==CoinRenderAction::SUCCESS &&
      cpuAction.getLastStatus()==CoinRenderAction::SUCCESS &&
      matrices.gotGL && matrices.gotCoin;
    ok &= check(captured,name+" matrix and image captures must succeed");
    if(!captured) continue;
    for(int vertex : {2,3}) {
      const SbVec3f point=scene.positions->point[vertex];
      const int cx=int(std::lround((point[0]+1)*side/2));
      const int cy=int(std::lround((point[1]+1)*side/2));
      const PointBounds gpuBounds=localPointBounds(image,cx,cy);
      const PointBounds cpuBounds=localPointBounds(cpuImage,cx,cy);
      const PointBounds glBounds=localPointBounds(nativeImage,cx,cy);
      const std::streamsize previousPrecision=std::cout.precision();
      std::cout << std::setprecision(17);
      auto print=[](const char * label,const PointBounds & bounds) {
        std::cout << ' ' << label << "=[" << bounds.minX << ',' << bounds.minY << ".."
                  << bounds.maxX << ',' << bounds.maxY << ";n=" << bounds.count << ']';
      };
      std::cout << "concave_stroke_point_raster " << name << " vertex=" << vertex
                << " shift=" << shift << " authored_y=" << point[1]
                << " projected=" << (double(point[0])+1)*side/2 << ','
                << (double(point[1])+1)*side/2 << " rounded=" << cx << ',' << cy;
      print("GPU",gpuBounds);print("CPU",cpuBounds);print("CoinGL",glBounds);
      std::cout << '\n';
      std::cout.precision(previousPrecision);
    }
    if(size==1 && shift==0) {
      const std::streamsize previousPrecision=std::cout.precision();
      std::cout << std::setprecision(17);
      std::cout << "concave_stroke_point_matrices viewport=";
      for(int i=0;i<4;++i) std::cout << matrices.viewport[i] << (i==3 ? ' ' : ',');
      std::cout << "gl_model=";
      for(double value:matrices.glModel) std::cout << value << ',';
      std::cout << " gl_projection=";
      for(double value:matrices.glProjection) std::cout << value << ',';
      std::cout << " coin_model=";
      for(int r=0;r<4;++r) for(int c=0;c<4;++c)
        std::cout << matrices.coinModel[r][c] << ',';
      std::cout << " coin_view=";
      for(int r=0;r<4;++r) for(int c=0;c<4;++c)
        std::cout << matrices.coinView[r][c] << ',';
      std::cout << " coin_projection=";
      for(int r=0;r<4;++r) for(int c=0;c<4;++c)
        std::cout << matrices.coinProjection[r][c] << ',';
      std::cout << '\n';
      std::cout.precision(previousPrecision);
    }
    ok &= compareGl(image,gl.getBuffer(),name+"/GPU");
    ok &= compareGl(cpuImage,gl.getBuffer(),name+"/CPU");
    ok &= compareCpuGpu(image,cpuImage,name);
    if(canonicalRaster) {
      ok &= canonicalPointMask(image,scene,size,name+"/GPU");
      ok &= canonicalPointMask(cpuImage,scene,size,name+"/CPU");
    }
  }
  scene.root->removeChild(callback);
  return ok;
}
void diagnoseSplitUTriangulation(SoSeparator * polygon,SoOffscreenRenderer & gl,
                                  const std::vector<unsigned char> & polygonImage) {
  std::vector<NativeTriangle> triangles;
  SoCallbackAction collector;
  collector.addTriangleCallback(SoFaceSet::getClassTypeId(),nativeTriangle,&triangles);
  collector.apply(polygon);
  if(triangles.empty()) {
    std::cout << "concave_stroke_native_diagonals inconclusive=no_native_triangles\n";
    return;
  }
  SoSeparator * explicitTriangles=makeSplitU();
  explicitTriangles->removeChild(explicitTriangles->getNumChildren()-1);
  explicitTriangles->removeChild(explicitTriangles->getNumChildren()-1);
  std::vector<SbVec3f> points;std::vector<int32_t> counts;
  for(const auto & t:triangles) {
    for(const auto & p:t) points.push_back(p);
    counts.push_back(3);
  }
  auto * coords=new SoCoordinate3;
  coords->point.setValues(0,int(points.size()),points.data());
  explicitTriangles->addChild(coords);
  auto * faces=new SoFaceSet;
  faces->numVertices.setValues(0,int(counts.size()),counts.data());
  explicitTriangles->addChild(faces);
  const bool rendered=gl.render(explicitTriangles) && gl.getBuffer();
  const std::vector<unsigned char> explicitImage=rendered ? normalizeGl(gl.getBuffer()) :
    std::vector<unsigned char>{};
  explicitTriangles->unref();
  if(polygonImage.size()!=size_t(side*side*4) ||
     explicitImage.size()!=size_t(side*side*4)) {
    std::cout << "concave_stroke_native_diagonals inconclusive=readback_failed\n";
    return;
  }
  auto nearEdge=[](double x,double y,const SbVec3f & a,const SbVec3f & b) {
    const double ax=(a[0]+1)*side/2,ay=(a[1]+1)*side/2;
    const double bx=(b[0]+1)*side/2,by=(b[1]+1)*side/2;
    const double dx=bx-ax,dy=by-ay;
    const double t=std::max(0.,std::min(1.,((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy)));
    return std::hypot(x-ax-t*dx,y-ay-t*dy);
  };
  struct Edge {SbVec3f a,b;};
  std::vector<Edge> diagonals;
  for(const auto & t:triangles) for(int e=0;e<3;++e) {
    const SbVec3f & a=t[e],& b=t[(e+1)%3];
    bool original=false;
    for(int i=0;i<8;++i)
      original|=(samePosition(a,uRing[i]) && samePosition(b,uRing[(i+1)%8])) ||
        (samePosition(b,uRing[i]) && samePosition(a,uRing[(i+1)%8]));
    if(!original) diagonals.push_back({a,b});
  }
  size_t candidates=0,polygonOnly=0,trianglesOnly=0,both=0,neither=0;
  size_t globalCoverageDifference=0;
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    const size_t p=(size_t(side-1-y)*side+x)*4;
    const bool originalDrawn=polygonImage[p]+polygonImage[p+1]+polygonImage[p+2]>12;
    const bool triangleDrawn=explicitImage[p]+explicitImage[p+1]+explicitImage[p+2]>12;
    globalCoverageDifference+=originalDrawn!=triangleDrawn;
    if(y<=48) continue; // Only the two clipped upper components.
    double boundary=1e9,diagonal=1e9;
    for(int i=0;i<8;++i)
      boundary=std::min(boundary,nearEdge(x+.5,y+.5,uRing[i],uRing[(i+1)%8]));
    for(const auto & edge:diagonals)
      diagonal=std::min(diagonal,nearEdge(x+.5,y+.5,edge.a,edge.b));
    if(diagonal>2 || boundary<5) continue;
    ++candidates;
    if(originalDrawn && triangleDrawn) ++both;
    else if(originalDrawn) ++polygonOnly;
    else if(triangleDrawn) ++trianglesOnly;
    else ++neither;
  }
  std::cout << "concave_stroke_native_diagonals triangles=" << triangles.size()
            << " interior_candidates=" << candidates << " polygon_only=" << polygonOnly
            << " explicit_triangles_only=" << trianglesOnly << " both=" << both
            << " neither=" << neither << " global_coverage_difference="
            << globalCoverageDifference << '\n';
}
}
int main(int argc,char ** argv) {
  configureSamplingTest(argc,argv);
  for(int i=1;i<argc;++i)
    if(std::string(argv[i])=="--canonical-raster") canonicalRaster=true;
  std::cout << "concave_stroke_raster_policy="
            << (canonicalRaster ? "CANONICAL_8BIT" : "NATIVE") << '\n';
  SoDB::init();CoinRenderAction::initClass();DerivedConcaveFaceSet::initClass();
  CoinRenderCapabilities caps{};
  const bool gpu=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0 && caps.gpu_available;
  std::cout << "concave_stroke_runtime backend=" << caps.backend << " renderer=" << caps.renderer
            << " vendor=" << caps.vendor_id << " device=" << caps.device_id
            << " adapter=" << caps.adapter_name << " gpu_available=" << caps.gpu_available << '\n';
  if (!gpu) return 77;
  std::unique_ptr<CoinRenderTarget> target(samplingTestOffscreen(
    SbVec2i32(side,side),concaveOptions()));
  if (!target || !target->setDepthReadbackEnabled(TRUE)) return 1;
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target.get());
  std::unique_ptr<CoinRenderTarget> cpu(samplingTestOffscreen(
    SbVec2i32(side,side),concaveOptions()));
  if (!cpu || !cpu->setDepthReadbackEnabled(TRUE)) return 1;
  std::cout << "concave_stroke_effective_raster_gpu="
            << target->getOptions().rasterizationPolicy
            << " cpu=" << cpu->getOptions().rasterizationPolicy << '\n';
  if(target->getOptions().rasterizationPolicy!=concaveOptions().rasterizationPolicy ||
     cpu->getOptions().rasterizationPolicy!=concaveOptions().rasterizationPolicy) return 1;
  cpu->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  CoinRenderAction cpuAction(SbViewportRegion(side,side));cpuAction.setRenderTarget(cpu.get());
  SoOffscreenRenderer gl(SbViewportRegion(side,side));gl.setComponents(SoOffscreenRenderer::RGB);
  gl.setBackgroundColor(SbColor(0,0,0));
  bool ok=true;
  {
    SoSeparator * split=makeSplitU();
    action.apply(split);cpuAction.apply(split);
    std::vector<unsigned char> gpuImage,cpuImage;
    target->readbackRGBA(gpuImage);cpu->readbackRGBA(cpuImage);
    const bool native=gl.render(split) && gl.getBuffer();
    const std::vector<unsigned char> polygonGl=native ? normalizeGl(gl.getBuffer()) :
      std::vector<unsigned char>{};
    const bool captured=native && action.getLastStatus()==CoinRenderAction::SUCCESS &&
      cpuAction.getLastStatus()==CoinRenderAction::SUCCESS;
    ok &= check(captured,"clipped concave U must render two components");
    if(captured) {
      ok &= compareGl(gpuImage,gl.getBuffer(),"split U/GPU");
      ok &= compareGl(cpuImage,gl.getBuffer(),"split U/CPU");
      ok &= compareCpuGpu(gpuImage,cpuImage,"split U");
      ok &= splitUOracle(gpuImage,"GPU");
      ok &= splitUOracle(cpuImage,"CPU");
      ok &= splitUOracle(polygonGl,"CoinGL");
      diagnoseSplitUTriangulation(split,gl,polygonGl);
    }
    split->unref();
  }
  {
    Occurrences scene;
    action.apply(scene.root);cpuAction.apply(scene.root);
    std::vector<unsigned char> image,cpuImage;
    target->readbackRGBA(image);cpu->readbackRGBA(cpuImage);
    const bool native=gl.render(scene.root) && gl.getBuffer();
    const std::vector<unsigned char> glBefore=native ? normalizeGl(gl.getBuffer()) :
      std::vector<unsigned char>{};
    const bool captured=native && action.getLastStatus()==CoinRenderAction::SUCCESS &&
      cpuAction.getLastStatus()==CoinRenderAction::SUCCESS;
    ok &= check(captured,"coincident positions with independent occurrences must render");
    if(captured) {
      ok &= compareGl(image,gl.getBuffer(),"occurrences/GPU");
      ok &= compareGl(cpuImage,gl.getBuffer(),"occurrences/CPU");
      ok &= compareCpuGpu(image,cpuImage,"occurrences");
      ok &= occurrenceOracle(image,"GPU");
      ok &= occurrenceOracle(cpuImage,"CPU");
      ok &= occurrenceOracle(glBefore,"CoinGL");
    }
    // Changing only the UV of the second occurrence must change only the
    // right-hand triangle; coordinate deduplication would contaminate left.
    std::vector<unsigned char> before=image;
    for(int i=3;i<6;++i) scene.uv->point.set1Value(i,SbVec2f(.25f,.25f));
    action.apply(scene.root);cpuAction.apply(scene.root);
    std::vector<unsigned char> changed;target->readbackRGBA(changed);
    std::vector<unsigned char> cpuChanged;cpu->readbackRGBA(cpuChanged);
    const bool nativeChanged=gl.render(scene.root) && gl.getBuffer();
    const std::vector<unsigned char> glChanged=nativeChanged ? normalizeGl(gl.getBuffer()) :
      std::vector<unsigned char>{};
    ok &= check(nativeChanged && action.getLastStatus()==CoinRenderAction::SUCCESS &&
      cpuAction.getLastStatus()==CoinRenderAction::SUCCESS,
      "occurrence UV mutation must render");
    if(nativeChanged) {
      ok &= compareGl(changed,gl.getBuffer(),"occurrence UV mutation/GPU");
      ok &= compareGl(cpuChanged,gl.getBuffer(),"occurrence UV mutation/CPU");
      ok &= compareCpuGpu(changed,cpuChanged,"occurrence UV mutation");
    }
    if(before.size()==changed.size() && glBefore.size()==glChanged.size()) {
      size_t leftChanges=0,rightChanges=0,glLeftChanges=0,glRightChanges=0;
      for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
        const size_t p=(size_t(side-1-y)*side+x)*4;
        const bool altered=before[p]!=changed[p] || before[p+1]!=changed[p+1] ||
          before[p+2]!=changed[p+2];
        const bool glAltered=glBefore[p]!=glChanged[p] ||
          glBefore[p+1]!=glChanged[p+1] || glBefore[p+2]!=glChanged[p+2];
        if(x<44) leftChanges+=altered;
        if(x>52) rightChanges+=altered;
        if(x<44) glLeftChanges+=glAltered;
        if(x>52) glRightChanges+=glAltered;
      }
      std::cout << "concave_stroke_occurrence_uv left_changed=" << leftChanges
                << " right_changed=" << rightChanges << " gl_left_changed="
                << glLeftChanges << " gl_right_changed=" << glRightChanges << '\n';
      ok &= check(leftChanges==0 && rightChanges>15 &&
        glLeftChanges==0 && glRightChanges>15,
        "changing right occurrence UV must leave left side unchanged and alter right side in both renderers");
    }
  }
  {
    Fixture scene(0);
    scene.light->model=SoLightModel::BASE_COLOR;
    scene.fog->fogType=SoEnvironment::NONE;
    scene.complexity->textureQuality=0;
    scene.offset->on=TRUE;
    scene.offset->styles=SoPolygonOffset::LINES|SoPolygonOffset::POINTS;
    scene.offset->factor=-12;scene.offset->units=0;
    std::vector<unsigned char> firstFill,lastFill,lines,points,cpuFirstFill,cpuLastFill,
      cpuLines,cpuPoints;
    std::vector<float> firstDepth,lastDepth,pointDepth,cpuFirstDepth,cpuLastDepth,
      cpuPointDepth;
    uint64_t priorSerial=target->getLastSubmissionSerial();
    for(int step=0;step<4;++step) {
      const int mode=step==0 || step==3 ? SoDrawStyle::FILLED :
        step==1 ? SoDrawStyle::LINES : SoDrawStyle::POINTS;
      scene.style->style=mode;
      action.apply(scene.root);cpuAction.apply(scene.root);
      std::vector<unsigned char> image,cpuImage;
      std::vector<float> depth,cpuDepth;
      target->readbackRGBA(image);target->readbackDepth(depth);
      cpu->readbackRGBA(cpuImage);cpu->readbackDepth(cpuDepth);
      const bool native=gl.render(scene.root) && gl.getBuffer();
      const std::string name="transition/step="+std::to_string(step);
      const bool valid=action.getLastStatus()==CoinRenderAction::SUCCESS &&
        cpuAction.getLastStatus()==CoinRenderAction::SUCCESS && native &&
        target->getLastSubmissionSerial()>priorSerial &&
        image.size()==size_t(side*side*4) && depth.size()==size_t(side*side) &&
        cpuImage.size()==size_t(side*side*4) && cpuDepth.size()==size_t(side*side);
      ok &= check(valid,name+" must publish a new complete frame");
      priorSerial=target->getLastSubmissionSerial();
      if(step==0) {
        firstFill=image;firstDepth=depth;cpuFirstFill=cpuImage;cpuFirstDepth=cpuDepth;
      } else if(step==1) {
        lines=image;cpuLines=cpuImage;
      } else if(step==2) {
        points=image;pointDepth=depth;cpuPoints=cpuImage;cpuPointDepth=cpuDepth;
      } else {
        lastFill=image;lastDepth=depth;cpuLastFill=cpuImage;cpuLastDepth=cpuDepth;
      }
      if(valid) ok &= compareCpuGpu(image,cpuImage,name);
      if(valid && (step==1 || step==2)) {
        ok &= compareGl(image,gl.getBuffer(),name+"/GPU");
        ok &= compareGl(cpuImage,gl.getBuffer(),name+"/CPU");
        ok &= contourOracle(image,mode,false,name+"/GPU");
        ok &= contourOracle(cpuImage,mode,false,name+"/CPU");
        if(canonicalRaster && step==2) {
          ok &= canonicalPointMask(image,scene,5,name+"/GPU");
          ok &= canonicalPointMask(cpuImage,scene,5,name+"/CPU");
        }
      }
    }
    ok &= check(firstFill==lastFill && firstDepth==lastDepth &&
      cpuFirstFill==cpuLastFill && cpuFirstDepth==cpuLastDepth,
      "FILL after LINES and POINTS must recover the initial color and depth exactly");
    Fixture fresh(0);
    fresh.style->style=SoDrawStyle::POINTS;
    fresh.light->model=SoLightModel::BASE_COLOR;
    fresh.fog->fogType=SoEnvironment::NONE;
    fresh.complexity->textureQuality=0;
    fresh.offset->on=TRUE;
    fresh.offset->styles=SoPolygonOffset::LINES|SoPolygonOffset::POINTS;
    fresh.offset->factor=-12;fresh.offset->units=0;
    std::unique_ptr<CoinRenderTarget> freshGpu(samplingTestOffscreen(
      SbVec2i32(side,side),concaveOptions()));
    std::unique_ptr<CoinRenderTarget> freshCpu(samplingTestOffscreen(
      SbVec2i32(side,side),concaveOptions()));
    const bool freshTargets=freshGpu && freshCpu &&
      freshGpu->setDepthReadbackEnabled(TRUE) && freshCpu->setDepthReadbackEnabled(TRUE);
    ok &= check(freshTargets,"fresh POINTS targets must support depth readback");
    if(freshTargets) {
      freshCpu->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
      CoinRenderAction freshGpuAction(SbViewportRegion(side,side));
      CoinRenderAction freshCpuAction(SbViewportRegion(side,side));
      freshGpuAction.setRenderTarget(freshGpu.get());
      freshCpuAction.setRenderTarget(freshCpu.get());
      freshGpuAction.apply(fresh.root);freshCpuAction.apply(fresh.root);
      std::vector<unsigned char> freshImage,freshCpuImage;
      std::vector<float> freshDepth,freshCpuDepth;
      freshGpu->readbackRGBA(freshImage);freshGpu->readbackDepth(freshDepth);
      freshCpu->readbackRGBA(freshCpuImage);freshCpu->readbackDepth(freshCpuDepth);
      ok &= check(freshGpuAction.getLastStatus()==CoinRenderAction::SUCCESS &&
        freshCpuAction.getLastStatus()==CoinRenderAction::SUCCESS &&
        points==freshImage && pointDepth==freshDepth &&
        cpuPoints==freshCpuImage && cpuPointDepth==freshCpuDepth,
        "POINTS after FILL and LINES must retain fresh incident-triangle color and depth");
      freshGpuAction.setRenderTarget(nullptr);freshCpuAction.setRenderTarget(nullptr);
    }
  }
  ok &= pointRasterProbe(action,*target,cpuAction,*cpu,gl);
  ok &= textureLineProbe(action,*target,cpuAction,*cpu,gl);
  ok &= overallNormalPrecursorProbe(action,*target,gl);
  {
    // Coin's SoFaceSet accepts all six authored occurrences through
    // shapeVertex(), including a collinear bottom vertex and its consecutive
    // duplicate. Tessellation may remove degenerate triangles, but the
    // original exterior and point occurrences remain meaningful.
    Fixture scene(0);
    const SbVec3f authored[]={SbVec3f(-.8f,-.7f,0),SbVec3f(0,-.7f,0),
      SbVec3f(0,-.7f,0),SbVec3f(.8f,-.7f,0),
      SbVec3f(.8f,.7f,0),SbVec3f(-.8f,.7f,0)};
    scene.positions->point.setValues(0,6,authored);
    scene.light->model=SoLightModel::BASE_COLOR;
    scene.fog->fogType=SoEnvironment::NONE;
    scene.materialBinding->value=SoMaterialBinding::PER_VERTEX;
    scene.normalBinding->value=SoNormalBinding::PER_VERTEX;
    scene.complexity->textureQuality=.8f;
    RepeatedDetailProof proof;
    SoCallbackAction inspector;
    inspector.addTriangleCallback(SoFaceSet::getClassTypeId(),repeatedFaceDetails,&proof);
    inspector.apply(scene.root);
    bool sourceValid=proof.seen && proof.valid &&
      proof.material[1]!=proof.material[2] && proof.texture[1]!=proof.texture[2];
    for(int i=0;i<6;++i) sourceValid &= proof.coordinate[i]==i;
    std::cout << "concave_stroke_repeated_source callback=" << proof.seen
              << " detail=" << proof.valid << " material1/2=" << proof.material[1]
              << '/' << proof.material[2] << " texture1/2=" << proof.texture[1]
              << '/' << proof.texture[2] << '\n';
    ok &= check(sourceValid,
      "Coin original must retain both consecutive authored occurrences and distinct attributes");
    auto geometryOracle=[&](const std::vector<unsigned char> & image,int mode,
                            const std::string & name) {
      if(image.size()!=size_t(side*side*4)) return check(false,name+" image size");
      auto drawn=[&](int x,int y) {
        const size_t p=(size_t(side-1-y)*side+x)*4;
        return image[p]+image[p+1]+image[p+2]>12;
      };
      size_t outside=0,bottomGaps=0,pointWitnesses=0;
      for(int y=3;y<side-3;++y) for(int x=3;x<side-3;++x) if(drawn(x,y)) {
        double nearest=1e9;
        if(mode==SoDrawStyle::LINES) {
          nearest=distanceToSegment(x+.5,y+.5,authored[0],authored[3]);
          nearest=std::min(nearest,distanceToSegment(x+.5,y+.5,authored[3],authored[4]));
          nearest=std::min(nearest,distanceToSegment(x+.5,y+.5,authored[4],authored[5]));
          nearest=std::min(nearest,distanceToSegment(x+.5,y+.5,authored[5],authored[0]));
        } else for(const auto & point:authored)
          nearest=std::min(nearest,std::hypot(x+.5-(point[0]+1)*side/2,
                                               y+.5-(point[1]+1)*side/2));
        outside+=nearest>5;
      }
      if(mode==SoDrawStyle::LINES) {
        for(int x=12;x<=84;++x) {
          bool hit=false;
          for(int y=11;y<=18;++y) hit|=drawn(x,y);
          bottomGaps+=!hit;
        }
      } else for(int index : {0,1,3,4,5}) {
        const int cx=int(std::lround((authored[index][0]+1)*side/2));
        const int cy=int(std::lround((authored[index][1]+1)*side/2));
        bool hit=false;
        for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx)
          hit|=drawn(cx+dx,cy+dy);
        pointWitnesses+=hit;
      }
      std::cout << "concave_stroke_repeated_geometry " << name << " outside=" << outside
                << " bottom_gaps=" << bottomGaps << " point_witnesses="
                << pointWitnesses << '\n';
      return check(outside==0 && (mode==SoDrawStyle::LINES ? bottomGaps==0 :
        pointWitnesses==5),
        name+" must preserve exterior, collinear segment and original point locations");
    };
    for(int mode : {SoDrawStyle::LINES,SoDrawStyle::POINTS}) {
      scene.style->style=mode;
      action.apply(scene.root);cpuAction.apply(scene.root);
      std::vector<unsigned char> image,cpuImage;
      target->readbackRGBA(image);cpu->readbackRGBA(cpuImage);
      const bool native=gl.render(scene.root) && gl.getBuffer();
      const std::string name="collinear_repeated/style="+std::to_string(mode);
      const bool captured=native && action.getLastStatus()==CoinRenderAction::SUCCESS &&
        cpuAction.getLastStatus()==CoinRenderAction::SUCCESS;
      ok &= check(captured,name+" valid authored polygon must render");
      if(captured) {
        const auto reference=normalizeGl(gl.getBuffer());
        ok &= compareGl(image,gl.getBuffer(),name+"/GPU");
        ok &= compareGl(cpuImage,gl.getBuffer(),name+"/CPU");
        ok &= compareCpuGpu(image,cpuImage,name);
        ok &= geometryOracle(image,mode,name+"/GPU");
        ok &= geometryOracle(cpuImage,mode,name+"/CPU");
        ok &= geometryOracle(reference,mode,name+"/CoinGL");
        if(canonicalRaster && mode==SoDrawStyle::POINTS) {
          ok &= canonicalPointMask(image,scene,5,name+"/GPU");
          ok &= canonicalPointMask(cpuImage,scene,5,name+"/CPU");
        }
      }
    }
    auto captureFace=[&](Fixture & fixture,const std::string & name,
                         std::vector<unsigned char> & gpuImage,
                         std::vector<unsigned char> & cpuImage,
                         std::vector<unsigned char> & glImage) {
      action.apply(fixture.root);cpuAction.apply(fixture.root);
      target->readbackRGBA(gpuImage);cpu->readbackRGBA(cpuImage);
      const bool native=gl.render(fixture.root) && gl.getBuffer();
      glImage=native ? normalizeGl(gl.getBuffer()) : std::vector<unsigned char>{};
      const bool captured=native && action.getLastStatus()==CoinRenderAction::SUCCESS &&
        cpuAction.getLastStatus()==CoinRenderAction::SUCCESS &&
        gpuImage.size()==size_t(side*side*4) && cpuImage.size()==gpuImage.size();
      ok &= check(captured,name+" repeated occurrence frame must publish");
      if(captured) {
        ok &= compareGl(gpuImage,gl.getBuffer(),name+"/GPU");
        ok &= compareGl(cpuImage,gl.getBuffer(),name+"/CPU");
        ok &= compareCpuGpu(gpuImage,cpuImage,name);
      }
      return captured;
    };
    auto channel=[&](const std::vector<unsigned char> & image,int x,int y,int c) {
      return int(image[(size_t(side-1-y)*side+x)*4+c]);
    };
    auto changedRegion=[&](const std::vector<unsigned char> & before,
                           const std::vector<unsigned char> & after,int left,int right) {
      size_t changed=0;
      for(int y=11;y<=18;++y) for(int x=left;x<=right;++x) {
        const size_t p=(size_t(side-1-y)*side+x)*4;
        changed+=before[p]!=after[p] || before[p+1]!=after[p+1] ||
          before[p+2]!=after[p+2];
      }
      return changed;
    };
    auto changedSides=[&](const std::vector<unsigned char> & before,
                          const std::vector<unsigned char> & after,
                          const std::string & name,bool required=true) {
      if(before.size()!=size_t(side*side*4) || after.size()!=before.size())
        return check(false,name+" image size");
      const size_t left=changedRegion(before,after,12,42);
      const size_t right=changedRegion(before,after,54,82);
      std::cout << "concave_stroke_repeated_occurrence " << name
                << " left_changed=" << left << " right_changed=" << right << '\n';
      return required ? check(left==0 && right>10,
        name+" occurrence2 must affect outgoing edge only") : true;
    };
    scene.style->style=SoDrawStyle::LINES;
    scene.complexity->textureQuality=0;
    std::vector<unsigned char> materialBefore,materialAfter,materialCpuBefore,
      materialCpuAfter,materialGlBefore,materialGlAfter;
    const bool baseMaterial=captureFace(scene,"repeated_material/before",
      materialBefore,materialCpuBefore,materialGlBefore);
    scene.material->diffuseColor.set1Value(2,SbColor(1,0,0));
    const bool changedMaterial=captureFace(scene,"repeated_material/after",
      materialAfter,materialCpuAfter,materialGlAfter);
    if(baseMaterial && changedMaterial) {
      ok &= changedSides(materialBefore,materialAfter,"material/GPU");
      ok &= changedSides(materialCpuBefore,materialCpuAfter,"material/CPU");
      ok &= changedSides(materialGlBefore,materialGlAfter,
        "material/CoinGL control",false);
      const double t=(67.5-48.)/(86.4-48.);
      const double source[][3]={{.32,.45,.96},{1,0,0}};
      const double destination[]={.92,.8,.3};
      for(int profile=0;profile<2;++profile) {
        const auto & image=profile ? materialAfter : materialBefore;
        const auto & cpuImage=profile ? materialCpuAfter : materialCpuBefore;
        for(int c=0;c<3;++c) {
          const int expected=int(std::lround(255*((1-t)*source[profile][c]+
            t*destination[c])));
          const int actual=channel(image,67,14,c),cpuActual=channel(cpuImage,67,14,c);
          std::cout << "concave_stroke_repeated_material_linear profile=" << profile
                    << " channel=" << c << " expected=" << expected
                    << " GPU=" << actual << " CPU=" << cpuActual << '\n';
          ok &= check(std::abs(actual-expected)<=4 &&
            std::abs(cpuActual-expected)<=4,
            "outgoing edge material must interpolate from occurrence2, not coincident occurrence1");
        }
      }
    }
    Fixture uvScene(0);
    uvScene.positions->point.setValues(0,6,authored);
    uvScene.style->style=SoDrawStyle::LINES;
    uvScene.light->model=SoLightModel::BASE_COLOR;
    uvScene.fog->fogType=SoEnvironment::NONE;
    uvScene.materialBinding->value=SoMaterialBinding::PER_VERTEX;
    for(int i=0;i<6;++i) uvScene.material->diffuseColor.set1Value(i,SbColor(1,1,1));
    uvScene.complexity->textureQuality=.1f; // Coin nearest band (< 0.2).
    const unsigned char asymmetric[]={255,255,255,50,180,240};
    uvScene.texture0->image.setValue(SbVec2s(2,1),3,asymmetric);
    uvScene.texture0->wrapS=SoTexture2::CLAMP;
    uvScene.texture0->wrapT=SoTexture2::CLAMP;
    uvScene.texture3->image.setValue(SbVec2s(0,0),0,nullptr);
    for(int i=0;i<6;++i) uvScene.uv0->point.set1Value(i,SbVec2f(.25f,.5f));
    std::vector<unsigned char> uvBefore,uvAfter,uvCpuBefore,uvCpuAfter,
      uvGlBefore,uvGlAfter;
    const bool baseUv=captureFace(uvScene,"repeated_uv/before",uvBefore,
      uvCpuBefore,uvGlBefore);
    uvScene.uv0->point.set1Value(2,SbVec2f(.75f,.5f));
    const bool changedUv=captureFace(uvScene,"repeated_uv/after",uvAfter,
      uvCpuAfter,uvGlAfter);
    if(baseUv && changedUv) {
      ok &= changedSides(uvBefore,uvAfter,"UV/GPU");
      ok &= changedSides(uvCpuBefore,uvCpuAfter,"UV/CPU");
      ok &= changedSides(uvGlBefore,uvGlAfter,"UV/CoinGL control",false);
      const int expected[][3]={{255,255,255},{50,180,240}};
      for(int profile=0;profile<2;++profile) {
        const auto & image=profile ? uvAfter : uvBefore;
        const auto & cpuImage=profile ? uvCpuAfter : uvCpuBefore;
        for(int c=0;c<3;++c) {
          const int actual=channel(image,60,14,c),cpuActual=channel(cpuImage,60,14,c);
          std::cout << "concave_stroke_repeated_uv_nearest profile=" << profile
                    << " channel=" << c << " expected=" << expected[profile][c]
                    << " GPU=" << actual << " CPU=" << cpuActual << '\n';
          ok &= check(std::abs(actual-expected[profile][c])<=4 &&
            std::abs(cpuActual-expected[profile][c])<=4,
            "outgoing edge UV must sample occurrence2's authored texel");
        }
      }
    }
  }
  for(int shape=0;shape<3;++shape) {
    Fixture scene(shape);
    if(shape==0) ok &= slopeOracle(scene,action,*target,gl);
    for(int style : {SoDrawStyle::LINES,SoDrawStyle::POINTS})
      for(int light : {SoLightModel::BASE_COLOR,SoLightModel::PHONG})
        for(int fog : {SoEnvironment::NONE,SoEnvironment::HAZE})
          for(bool textured : {false,true}) {
            scene.style->style=style;
            scene.light->model=light;
            scene.fog->fogType=fog;
            scene.materialBinding->value=textured ? SoMaterialBinding::PER_VERTEX : SoMaterialBinding::PER_FACE;
            scene.normalBinding->value=light==SoLightModel::PHONG ? SoNormalBinding::PER_VERTEX : SoNormalBinding::OVERALL;
            scene.complexity->textureQuality=textured ? .8f : 0.f;
            const std::string name="shape="+std::to_string(shape)+"/style="+std::to_string(style)+
              "/light="+std::to_string(light)+"/fog="+std::to_string(fog)+
              "/textures="+std::to_string(textured);
            action.apply(scene.root);
            std::vector<unsigned char> image;target->readbackRGBA(image);
            std::vector<float> depth;target->readbackDepth(depth);
            cpuAction.apply(scene.root);
            std::vector<unsigned char> cpuImage;cpu->readbackRGBA(cpuImage);
            const bool native=gl.render(scene.root) && gl.getBuffer();
            const bool captured=action.getLastStatus()==CoinRenderAction::SUCCESS && native &&
              cpuAction.getLastStatus()==CoinRenderAction::SUCCESS &&
              image.size()==size_t(side*side*4) && depth.size()==size_t(side*side) &&
              cpuImage.size()==size_t(side*side*4);
            ok &= check(captured,name+" valid concave nonplanar stroke must publish");
            if (captured) {
              ok &= compareGl(image,gl.getBuffer(),name);
              ok &= contourOracle(image,style,false,name);
              ok &= compareGl(cpuImage,gl.getBuffer(),name+"/CPU");
              ok &= compareCpuGpu(image,cpuImage,name);
              ok &= contourOracle(cpuImage,style,false,name+"/CPU");
              if(canonicalRaster && style==SoDrawStyle::POINTS) {
                ok &= canonicalPointMask(image,scene,5,name+"/GPU");
                ok &= canonicalPointMask(cpuImage,scene,5,name+"/CPU");
              }
            }
          }
    // State variants combine clipping, original-face offset and culling with
    // the same nonplanar contour. The un-clipped contour remains the control.
    scene.style->style=SoDrawStyle::LINES;
    scene.light->model=SoLightModel::PHONG;
    scene.fog->fogType=SoEnvironment::HAZE;
    scene.complexity->textureQuality=.8f;
    for(bool clip : {false,true}) for(bool offset : {false,true})
      for(bool cull : {false,true}) {
        scene.clip->on=clip;scene.offset->on=offset;
        scene.offset->factor=-2;scene.offset->units=1;
        scene.hints->shapeType=cull ? SoShapeHints::SOLID : SoShapeHints::UNKNOWN_SHAPE_TYPE;
        scene.hints->vertexOrdering=cull ? SoShapeHints::CLOCKWISE : SoShapeHints::UNKNOWN_ORDERING;
        const std::string name="state/shape="+std::to_string(shape)+"/clip="+std::to_string(clip)+
          "/offset="+std::to_string(offset)+"/cull="+std::to_string(cull);
        action.apply(scene.root);
        std::vector<unsigned char> image;target->readbackRGBA(image);
        cpuAction.apply(scene.root);
        std::vector<unsigned char> cpuImage;cpu->readbackRGBA(cpuImage);
        const bool native=gl.render(scene.root) && gl.getBuffer();
        const bool captured=action.getLastStatus()==CoinRenderAction::SUCCESS &&
          cpuAction.getLastStatus()==CoinRenderAction::SUCCESS && native;
        ok &= check(captured,name+" valid state combination must publish");
        if (captured) {
          ok &= compareGl(image,gl.getBuffer(),name);
          if (!cull) ok &= contourOracle(image,SoDrawStyle::LINES,clip,name);
          ok &= compareGl(cpuImage,gl.getBuffer(),name+"/CPU");
          ok &= compareCpuGpu(image,cpuImage,name);
          if (!cull) ok &= contourOracle(cpuImage,SoDrawStyle::LINES,clip,name+"/CPU");
        }
      }
    scene.clip->on=FALSE;scene.offset->on=FALSE;
    scene.hints->shapeType=SoShapeHints::UNKNOWN_SHAPE_TYPE;
    scene.hints->vertexOrdering=SoShapeHints::UNKNOWN_ORDERING;
    const int bindings[]={SoMaterialBinding::OVERALL,SoMaterialBinding::PER_PART,
      SoMaterialBinding::PER_PART_INDEXED,SoMaterialBinding::PER_FACE,
      SoMaterialBinding::PER_FACE_INDEXED,SoMaterialBinding::PER_VERTEX,
      SoMaterialBinding::PER_VERTEX_INDEXED};
    const char * bindingNames[]={"overall","part","part_indexed","face",
      "face_indexed","vertex","vertex_indexed"};
    scene.light->model=SoLightModel::PHONG;
    scene.fog->fogType=SoEnvironment::HAZE;
    scene.complexity->textureQuality=.8f;
    for(int stroke : {SoDrawStyle::LINES,SoDrawStyle::POINTS})
      for(int m=0;m<7;++m) for(int n=0;n<7;++n) {
        scene.style->style=stroke;
        scene.materialBinding->value=bindings[m];
        scene.normalBinding->value=bindings[n];
        const std::string name="binding/shape="+std::to_string(shape)+
          "/stroke="+std::to_string(stroke)+"/material="+bindingNames[m]+
          "/normal="+bindingNames[n];
        action.apply(scene.root);
        cpuAction.apply(scene.root);
        std::vector<unsigned char> gpuImage,cpuImage;
        target->readbackRGBA(gpuImage);cpu->readbackRGBA(cpuImage);
        const bool native=gl.render(scene.root) && gl.getBuffer();
        const bool captured=action.getLastStatus()==CoinRenderAction::SUCCESS &&
          cpuAction.getLastStatus()==CoinRenderAction::SUCCESS && native;
        ok &= check(captured,name+" valid binding pair must render");
        if (captured) {
          ok &= compareGl(gpuImage,gl.getBuffer(),name);
          ok &= compareGl(cpuImage,gl.getBuffer(),name+"/CPU");
          ok &= compareCpuGpu(gpuImage,cpuImage,name);
          ok &= contourOracle(gpuImage,stroke,false,name);
          ok &= contourOracle(cpuImage,stroke,false,name+"/CPU");
          if(canonicalRaster && stroke==SoDrawStyle::POINTS) {
            ok &= canonicalPointMask(gpuImage,scene,5,name+"/GPU");
            ok &= canonicalPointMask(cpuImage,scene,5,name+"/CPU");
          }
        }
      }
    scene.clip->on=FALSE;scene.offset->on=FALSE;
    scene.hints->shapeType=SoShapeHints::UNKNOWN_SHAPE_TYPE;
    scene.hints->vertexOrdering=SoShapeHints::UNKNOWN_ORDERING;
    action.apply(scene.root);
    std::vector<unsigned char> stable,after;
    std::vector<float> stableDepth,afterDepth;
    target->readbackRGBA(stable);target->readbackDepth(stableDepth);
    const uint64_t serial=target->getLastSubmissionSerial();
    cpuAction.apply(scene.root);
    std::vector<unsigned char> cpuStable,cpuAfter;
    std::vector<float> cpuStableDepth,cpuAfterDepth;
    cpu->readbackRGBA(cpuStable);cpu->readbackDepth(cpuStableDepth);
    const uint64_t cpuSerial=cpu->getLastSubmissionSerial();
    std::vector<SoClipPlane *> overflow;
    for(int i=0;i<9;++i) {
      auto * plane=new SoClipPlane;
      plane->plane=SbPlane(SbVec3f(1,0,0),float(i)*.01f);
      scene.root->insertChild(plane,scene.root->getNumChildren()-1);
      overflow.push_back(plane);
    }
    action.apply(scene.root);
    target->readbackRGBA(after);target->readbackDepth(afterDepth);
    cpuAction.apply(scene.root);
    cpu->readbackRGBA(cpuAfter);cpu->readbackDepth(cpuAfterDepth);
    ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
      after==stable && afterDepth==stableDepth && target->getLastSubmissionSerial()==serial,
      "invalid ninth clipping plane must preserve RGBA, depth and serial");
    ok &= check(cpuAction.getLastStatus()!=CoinRenderAction::SUCCESS &&
      cpuAfter==cpuStable && cpuAfterDepth==cpuStableDepth &&
      cpu->getLastSubmissionSerial()==cpuSerial,
      "CPU invalid ninth clipping plane must preserve RGBA, depth and serial");
    for(auto * plane : overflow) scene.root->removeChild(plane);
    action.apply(scene.root);
    target->readbackRGBA(after);target->readbackDepth(afterDepth);
    cpuAction.apply(scene.root);
    cpu->readbackRGBA(cpuAfter);cpu->readbackDepth(cpuAfterDepth);
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
      after==stable && afterDepth==stableDepth && target->getLastSubmissionSerial()>serial,
      "restored clipping profile must recover exactly");
    ok &= check(cpuAction.getLastStatus()==CoinRenderAction::SUCCESS &&
      cpuAfter==cpuStable && cpuAfterDepth==cpuStableDepth &&
      cpu->getLastSubmissionSerial()>cpuSerial,
      "CPU restored clipping profile must recover exactly");
  }
  action.setRenderTarget(nullptr);
  cpuAction.setRenderTarget(nullptr);
  return ok ? 0 : 1;
}
