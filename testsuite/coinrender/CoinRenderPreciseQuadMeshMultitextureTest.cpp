#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderSamplingTestOptions.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/system/gl.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoQuadMesh.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr int side=96;
bool check(bool good,const char * why) {
  if (!good) std::cerr << "precise multitexture: " << why << '\n';
  return good;
}
const int bindingModes[]={SoMaterialBinding::OVERALL,SoMaterialBinding::PER_PART,
  SoMaterialBinding::PER_PART_INDEXED,SoMaterialBinding::PER_FACE,
  SoMaterialBinding::PER_FACE_INDEXED,SoMaterialBinding::PER_VERTEX,
  SoMaterialBinding::PER_VERTEX_INDEXED};
const char * bindingNames[]={"overall","part","part_indexed","face",
                             "face_indexed","vertex","vertex_indexed"};
const char * bindingName(int value) {
  for(int i=0;i<7;++i) if (bindingModes[i]==value)
    return bindingNames[i];
  return "unknown";
}
bool hasNativeFan(int materialMode) {
  return materialMode!=SoMaterialBinding::PER_VERTEX &&
         materialMode!=SoMaterialBinding::PER_VERTEX_INDEXED;
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoLightModel * lightModel=new SoLightModel;
  SoMaterialBinding * materialBinding=new SoMaterialBinding;
  SoNormalBinding * normalBinding=new SoNormalBinding;
  SoTextureUnit * secondUnit=new SoTextureUnit;
  SoCoordinate3 * positions=new SoCoordinate3;
  SoTextureCoordinate2 * firstCoordinates=new SoTextureCoordinate2;
  SoTexture2 * secondTexture=new SoTexture2;
  SoTextureCoordinate2 * secondCoordinates=new SoTextureCoordinate2;
  SoQuadMesh * mesh=new SoQuadMesh;
  SoTexture2 * lastTexture=nullptr;
  std::array<SoTextureCoordinate2 *,8> coordinates{{}};
  GLint nativeTextureUnits=0;
  bool eightUnits=false;
  int meshStartIndex=0;
  explicit Scene(bool eight=false,int start=0):eightUnits(eight),meshStartIndex(start) {
    root->ref();
    auto * witness=new SoCallback;
    witness->setCallback([](void * data,SoAction * current) {
      if (current->isOfType(SoGLRenderAction::getClassTypeId()))
        glGetIntegerv(GL_MAX_TEXTURE_UNITS,static_cast<GLint *>(data));
    },&nativeTextureUnits);
    root->addChild(witness);
    auto * camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,5);
    camera->height=2.4f;
    camera->nearDistance=1;
    camera->farDistance=20;
    root->addChild(camera);
    lightModel->model=SoLightModel::PHONG;
    root->addChild(lightModel);
    auto * light=new SoDirectionalLight;
    light->direction.setValue(.35f,-.22f,-1);
    root->addChild(light);
    auto * material=new SoMaterial;
    material->ambientColor.setValue(.17f,.17f,.17f);
    material->specularColor.setValue(0,0,0);
    const SbColor colors[]={SbColor(.9f,.44f,.4f),SbColor(.4f,.8f,.53f),
      SbColor(.44f,.55f,.9f),SbColor(.86f,.7f,.4f),SbColor(.75f,.48f,.83f),
      SbColor(.43f,.8f,.78f),SbColor(.9f,.5f,.55f),SbColor(.55f,.83f,.45f),
      SbColor(.7f,.68f,.9f)};
    material->diffuseColor.setValues(0,9,colors);
    root->addChild(material);
    root->addChild(materialBinding);
    auto * normals=new SoNormal;
    const SbVec3f n[]={SbVec3f(.06f,.04f,1),SbVec3f(-.12f,.03f,1),
      SbVec3f(.13f,-.08f,1),SbVec3f(-.08f,-.13f,1),SbVec3f(.16f,.05f,1),
      SbVec3f(-.1f,.11f,1),SbVec3f(.07f,-.13f,1),SbVec3f(-.15f,-.04f,1),
      SbVec3f(.14f,.12f,1)};
    normals->vector.setValues(0,9,n);
    root->addChild(normals);
    root->addChild(normalBinding);
    auto * complexity=new SoComplexity;
    complexity->textureQuality=.8f;
    root->addChild(complexity);
    const SbVec3f p[]={SbVec3f(-.87f,-.81f,0),SbVec3f(0,-.81f,.09f),SbVec3f(.87f,-.81f,.02f),
      SbVec3f(-.87f,0,.04f),SbVec3f(0,0,-.07f),SbVec3f(.87f,0,.06f),
      SbVec3f(-.87f,.81f,-.02f),SbVec3f(0,.81f,.07f),SbVec3f(.87f,.81f,0)};
    for(int i=0;i<start;++i)
      positions->point.set1Value(i,SbVec3f(10+i,10+i,10+i));
    positions->point.setValues(start,9,p);
    root->addChild(positions);
    auto * firstUnit=new SoTextureUnit;
    firstUnit->unit=0;
    root->addChild(firstUnit);
    auto * firstTexture=new SoTexture2;
    firstTexture->model=SoTexture2::MODULATE;
    const unsigned char a[]={250,110,80, 80,245,110, 110,100,250, 245,230,80};
    firstTexture->image.setValue(SbVec2s(2,2),3,a);
    root->addChild(firstTexture);
    for(int y=0;y<3;++y) for(int x=0;x<3;++x)
      firstCoordinates->point.set1Value(y*3+x,SbVec2f(float(x)/2,float(y)/2));
    coordinates[0]=firstCoordinates;
    root->addChild(firstCoordinates);
    auto * firstProjective=new SoTextureMatrixTransform;
    SbMatrix m=SbMatrix::identity();
    m[0][3]=.24f;
    firstProjective->matrix=m;
    root->addChild(firstProjective);
    secondUnit->unit=3;
    root->addChild(secondUnit);
    secondTexture->model=SoTexture2::MODULATE;
    const unsigned char b[]={100,245,220, 240,90,220, 230,210,90, 80,190,245};
    secondTexture->image.setValue(SbVec2s(2,2),3,b);
    root->addChild(secondTexture);
    for(int y=0;y<3;++y) for(int x=0;x<3;++x)
      secondCoordinates->point.set1Value(y*3+x,SbVec2f(1-float(y)/2,float(x)/2));
    coordinates[3]=secondCoordinates;
    root->addChild(secondCoordinates);
    auto * secondProjective=new SoTextureMatrixTransform;
    m=SbMatrix::identity();
    m[1][3]=-.19f;
    secondProjective->matrix=m;
    root->addChild(secondProjective);
    if (eightUnits) for(int unit : {1,2,4,5,6,7}) {
      auto * selected=new SoTextureUnit;
      selected->unit=unit;
      root->addChild(selected);
      auto * image=new SoTexture2;
      image->model=SoTexture2::MODULATE;
      const unsigned char bright[]={247,249,245};
      image->image.setValue(SbVec2s(1,1),3,bright);
      root->addChild(image);
      if (unit==7) lastTexture=image;
      auto * uv=new SoTextureCoordinate2;
      for(int y=0;y<3;++y) for(int x=0;x<3;++x)
        uv->point.set1Value(y*3+x,SbVec2f(.07f*unit+.35f*x/2,
                                           .05f*unit+.38f*y/2));
      coordinates[unit]=uv;
      root->addChild(uv);
      auto * projective=new SoTextureMatrixTransform;
      m=SbMatrix::identity();
      m[0][3]=.04f*(unit+1);
      projective->matrix=m;
      root->addChild(projective);
    }
    mesh->verticesPerRow=3;
    mesh->verticesPerColumn=3;
    mesh->startIndex=start;
    root->addChild(mesh);
  }
  ~Scene() { root->unref(); }
};
struct Frame {
  std::vector<unsigned char> gpu,gl;
  std::vector<float> depth;
  uint64_t serial=0;
};
class RecordingBackend : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderFramePlan plan;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame,
                                CoinRenderTargetP & target) override {
    plan=frame;
    return CoinRenderCpuReferenceBackend::submit(frame,target);
  }
};
float nativeFanHalfWeight(float ratio) {
  // Coin's original weight table uses a fixed midpoint when the build has
  // neither ilogb nor _logb. Follow the configured native branch exactly.
#if defined(HAVE_ILOGB)
  const int exponent=std::ilogb(ratio);
#elif defined(HAVE__LOGB)
  const int exponent=int(std::logb(ratio));
#else
  const int exponent=0;
  (void)ratio;
#endif
  if (exponent< -16) return 0;
  if (exponent>=16) return .5f;
  const double p=std::sqrt(std::ldexp(.75,exponent));
  return float(.5*p/(1+p));
}
bool checkFanCenters(const CoinRenderFramePlan & frame,const Scene & scene) {
  if (frame.draws.empty() || frame.renderStates.empty())
    return check(false,"captured fan and render state must exist");
  const auto & state=frame.renderStates[frame.draws.front().renderStateSlot];
  bool good=true;
  for(int unit=0;unit<8;++unit)
    if (scene.coordinates[unit]) good &= coin_render_texture_unit(state,unit).enabled;
  for(int row=0;row<2;++row) for(int col=0;col<2;++col) {
    const int idx[]={row*3+col,(row+1)*3+col,row*3+col+1,(row+1)*3+col+1};
    SbVec3f center(0,0,0);
    for(int k=0;k<4;++k) center+=scene.positions->point[scene.meshStartIndex+idx[k]];
    center*=.25f;
    const float d0=(scene.positions->point[scene.meshStartIndex+idx[0]]-center).sqrLength();
    const float d1=(scene.positions->point[scene.meshStartIndex+idx[1]]-center).sqrLength();
    const float d2=(scene.positions->point[scene.meshStartIndex+idx[2]]-center).sqrLength();
    const float d3=(scene.positions->point[scene.meshStartIndex+idx[3]]-center).sqrLength();
    const float w0=nativeFanHalfWeight(d0/d3),w1=nativeFanHalfWeight(d1/d2);
    const float weight[]={w0,w1,.5f-w1,.5f-w0};
    SbVec4f expected[8];
    for(int unit=0;unit<8;++unit) if (scene.coordinates[unit]) {
      SbVec2f uv(0,0);
      for(int k=0;k<4;++k) uv+=scene.coordinates[unit]->point[idx[k]]*weight[k];
      const float q=unit==0 ? 1+.24f*uv[0] :
                    unit==3 ? 1-.19f*uv[1] : 1+.04f*(unit+1)*uv[0];
      expected[unit].setValue(uv[0],uv[1],0,q);
    }
    size_t matches=0;
    for(const auto & vertex : frame.vertices) {
      if (std::abs(vertex.position[0]-center[0])>1e-4f ||
          std::abs(vertex.position[1]-center[1])>1e-4f ||
          std::abs(vertex.position[2]-center[2])>1e-4f) continue;
      ++matches;
      for(int unit=0;unit<8;++unit) if (scene.coordinates[unit]) {
        const float * st=unit==0 ? vertex.texcoord : vertex.extraTexcoords[unit-1];
        const SbVec4f raw(st[0],st[1],vertex.textureR[unit],vertex.textureQ[unit]);
        SbVec4f actual;
        coin_render_texture_unit(state,unit).matrix.multVecMatrix(raw,actual);
        for(int c=0;c<4;++c)
          good &= std::abs(actual[c]-expected[unit][c])<=.002f;
        if (matches==1) std::cout << "precise_multitexture_fan face=" << row << ',' << col
          << " unit=" << unit << " actual=" << actual[0] << ',' << actual[1] << ',' << actual[3]
          << " expected=" << expected[unit][0] << ',' << expected[unit][1]
          << ',' << expected[unit][3] << '\n';
      }
    }
    good &= matches>0;
  }
  return check(good,"fan centers must preserve independently weighted homogeneous coordinates for every active unit");
}
size_t changed(const std::vector<unsigned char> & a,const std::vector<unsigned char> & b) {
  if (a.size()!=b.size()) return 0;
  size_t count=0;
  for(size_t i=0;i<a.size();i+=4)
    count += std::abs(int(a[i])-int(b[i]))+
             std::abs(int(a[i+1])-int(b[i+1]))+
             std::abs(int(a[i+2])-int(b[i+2]))>20;
  return count;
}
bool compare(const Frame & f,const char * label,size_t requiredCoverage=2000) {
  if (f.gpu.size()!=size_t(side*side*4) || f.gl.size()!=size_t(side*side*3))
    return check(false,"readback dimensions");
  double sum=0;
  int maximum=0,worstX=0,worstY=0;
  size_t covered=0,samples=0;
  // Both meshes occupy the central viewport. This inset avoids their outer
  // raster boundaries. The five-pixel cross avoids QuadMesh face boundaries,
  // while the remaining pixels still sample all four faces and fan sectors.
  for(int y=20;y<76;++y) for(int x=20;x<76;++x) {
    if (std::abs(x-48)<=2 || std::abs(y-48)<=2) continue;
    const size_t gi=size_t((side-1-y)*side+x)*4,ni=size_t(y*side+x)*3;
    const bool inside=f.gl[ni]+f.gl[ni+1]+f.gl[ni+2]>0;
    covered+=inside;
    for(int c=0;c<3;++c) {
      const int delta=std::abs(int(f.gpu[gi+c])-int(f.gl[ni+c]));
      sum+=delta;
      ++samples;
      if (delta>maximum) {maximum=delta;worstX=x;worstY=y;}
    }
  }
  const double mean=sum/samples;
  const bool good=covered>requiredCoverage && mean<=2 && maximum<=8;
  std::cout << "precise_multitexture " << label << " covered=" << covered
            << " mean=" << mean << " max=" << maximum
            << " worst=" << worstX << ',' << worstY << " pass=" << good << '\n';
  return check(good,"CoinRender RGB must match CoinGL with both texture units");
}
bool compareExecutor(const std::vector<unsigned char> & gpu,
                     const std::vector<unsigned char> & cpu,const char * label) {
  if (gpu.size()!=size_t(side*side*4) || cpu.size()!=gpu.size())
    return check(false,"eight-unit CPU/GPU readback dimensions");
  double sum=0;
  int maximum=0;
  size_t samples=0,covered=0;
  for(int y=20;y<76;++y) for(int x=20;x<76;++x) {
    if (std::abs(x-48)<=2 || std::abs(y-48)<=2) continue;
    const size_t at=size_t(y*side+x)*4;
    covered+=cpu[at]+cpu[at+1]+cpu[at+2]>0;
    for(int c=0;c<3;++c) {
      const int delta=std::abs(int(gpu[at+c])-int(cpu[at+c]));
      sum+=delta;
      maximum=std::max(maximum,delta);
      ++samples;
    }
  }
  const double mean=sum/samples;
  const bool good=covered>2000 && mean<=2 && maximum<=8;
  std::cout << "precise_multitexture_executor " << label << " covered=" << covered
            << " mean=" << mean << " max=" << maximum << " pass=" << good << '\n';
  return check(good,"eight-unit GPU RGB must match the independent CPU executor");
}
std::vector<int32_t> explicitTriangles(bool diagonal03) {
  std::vector<int32_t> result;
  for(int row=0;row<2;++row) for(int col=0;col<2;++col) {
    const int32_t corner[]={int32_t(row*3+col),int32_t((row+1)*3+col),
                            int32_t(row*3+col+1),int32_t((row+1)*3+col+1)};
    const int local03[]={0,1,3,-1,0,3,2,-1};
    const int local12[]={0,1,2,-1,2,1,3,-1};
    const int * local=diagonal03 ? local03 : local12;
    for(int i=0;i<8;++i) result.push_back(local[i]<0 ? -1 : corner[local[i]]);
  }
  return result;
}
void reportDiagonal(const std::vector<unsigned char> & candidate,int channels,
                    bool candidateTopDown,const std::vector<unsigned char> & reference,
                    const std::string & label) {
  if (candidate.size()!=size_t(side*side*channels) ||
      reference.size()!=size_t(side*side*3)) {
    std::cout << "precise_multitexture_diagonal " << label << " invalid_readback=1\n";
    return;
  }
  double sum=0;
  int maximum=0,worstX=0,worstY=0;
  size_t samples=0,bad8=0,bad16=0,coverage=0;
  for(int y=16;y<80;++y) for(int x=16;x<80;++x) {
    const size_t ci=size_t((candidateTopDown ? side-1-y : y)*side+x)*channels;
    const size_t ri=size_t(y*side+x)*3;
    const bool coveredCandidate=candidate[ci]+candidate[ci+1]+candidate[ci+2]>0;
    const bool coveredReference=reference[ri]+reference[ri+1]+reference[ri+2]>0;
    coverage+=coveredCandidate!=coveredReference;
    for(int c=0;c<3;++c) {
      const int delta=std::abs(int(candidate[ci+c])-int(reference[ri+c]));
      sum+=delta; ++samples; bad8+=delta>8; bad16+=delta>16;
      if (delta>maximum) {maximum=delta;worstX=x;worstY=y;}
    }
  }
  std::cout << "precise_multitexture_diagonal " << label << " mean=" << sum/samples
            << " max=" << maximum << " bad8=" << bad8 << " bad16=" << bad16
            << " coverage_mismatch=" << coverage << " worst=" << worstX << ',' << worstY
            << " diagnostic_only=1\n";
}
bool diagonalStudy() {
  bool rendered=true;
  for(int normalMode : {int(SoNormalBinding::OVERALL),int(SoNormalBinding::PER_VERTEX)}) {
    Scene quad;
    quad.materialBinding->value=SoMaterialBinding::PER_VERTEX;
    quad.normalBinding->value=normalMode;
    std::unique_ptr<CoinRenderTarget> target(samplingTestOffscreen(SbVec2i32(side,side)));
    if (!target) return check(false,"diagonal study target");
    CoinRenderAction action(SbViewportRegion(side,side));
    action.setRenderTarget(target.get());
    action.apply(quad.root);
    std::vector<unsigned char> gpu;
    target->readbackRGBA(gpu);
    rendered &= action.getLastStatus()==CoinRenderAction::SUCCESS &&
                gpu.size()==size_t(side*side*4);
    action.setRenderTarget(nullptr);
    SoOffscreenRenderer quadRenderer(SbViewportRegion(side,side));
    quadRenderer.setComponents(SoOffscreenRenderer::RGB);
    quadRenderer.setBackgroundColor(SbColor(0,0,0));
    const bool quadGl=quadRenderer.render(quad.root) && quadRenderer.getBuffer();
    rendered &= quadGl;
    std::vector<unsigned char> nativeQuad;
    if (quadGl) nativeQuad.assign(quadRenderer.getBuffer(),
                                  quadRenderer.getBuffer()+side*side*3);
    for(bool diagonal03 : {true,false}) {
      Scene reference;
      // Indexed bindings retain the same authored per-coordinate material
      // and normal values when each vertex appears in multiple triangles.
      reference.materialBinding->value=SoMaterialBinding::PER_VERTEX_INDEXED;
      reference.normalBinding->value=normalMode==SoNormalBinding::PER_VERTEX ?
        SoNormalBinding::PER_VERTEX_INDEXED : SoNormalBinding::OVERALL;
      reference.root->removeChild(reference.mesh);
      auto * faces=new SoIndexedFaceSet;
      const auto indices=explicitTriangles(diagonal03);
      faces->coordIndex.setValues(0,int(indices.size()),indices.data());
      faces->materialIndex.setValues(0,int(indices.size()),indices.data());
      if (normalMode==SoNormalBinding::PER_VERTEX)
        faces->normalIndex.setValues(0,int(indices.size()),indices.data());
      faces->textureCoordIndex.setValues(0,int(indices.size()),indices.data());
      reference.root->addChild(faces);
      SoOffscreenRenderer triangleRenderer(SbViewportRegion(side,side));
      triangleRenderer.setComponents(SoOffscreenRenderer::RGB);
      triangleRenderer.setBackgroundColor(SbColor(0,0,0));
      const bool triangleGl=triangleRenderer.render(reference.root) &&
                            triangleRenderer.getBuffer();
      rendered &= triangleGl;
      if (triangleGl && quadGl) {
        const std::vector<unsigned char> triangles(triangleRenderer.getBuffer(),
          triangleRenderer.getBuffer()+side*side*3);
        const std::string label=std::string("material=vertex/normal=")+
          bindingName(normalMode)+(diagonal03 ? "/diagonal=0-3" : "/diagonal=1-2");
        reportDiagonal(nativeQuad,3,false,triangles,"CoinGL_QuadMesh_vs_"+label);
        reportDiagonal(gpu,4,true,triangles,"GPU_QuadMesh_vs_"+label);
      }
    }
  }
  return check(rendered,"diagonal study must capture both CoinGL references and GPU QuadMesh");
}
}
int main(int argc,char ** argv) {
  configureSamplingTest(argc,argv);
  bool executorReference=false;
  bool diagonalOnly=false;
  for(int i=1;i<argc;++i)
    if (std::string(argv[i])=="--eight-unit-executor-reference") executorReference=true;
    else if (std::string(argv[i])=="--quad-strip-diagonal-study") diagonalOnly=true;
  SoDB::init();
  CoinRenderAction::initClass();
  const char * precise=std::getenv("COIN_QUADMESH_PRECISE_LIGHTING");
  if (!precise || std::atoi(precise)<=0) return 2;
  CoinRenderCapabilities caps{};
  const bool available=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0 && caps.gpu_available;
  std::cout << "precise_multitexture_runtime backend=" << caps.backend
            << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
            << " device=" << caps.device_id << " adapter=" << caps.adapter_name
            << " gpu_available=" << caps.gpu_available << '\n';
  if (!available) return 77;
  if (diagonalOnly) return diagonalStudy() ? 0 : 1;
  std::unique_ptr<CoinRenderTarget> target(samplingTestOffscreen(SbVec2i32(side,side)));
  if (!target || !target->setDepthReadbackEnabled(TRUE)) return 1;
  CoinRenderAction action(SbViewportRegion(side,side));
  action.setRenderTarget(target.get());
  SoOffscreenRenderer native(SbViewportRegion(side,side));
  native.setComponents(SoOffscreenRenderer::RGB);
  native.setBackgroundColor(SbColor(0,0,0));
  Scene scene;
  auto captureScene=[&](Scene & current,Frame & f,bool renderGL=true) {
    const uint64_t before=target->getLastSubmissionSerial();
    action.apply(current.root);
    f.serial=target->getLastSubmissionSerial();
    target->readbackRGBA(f.gpu);
    target->readbackDepth(f.depth);
    const bool glOk=!renderGL || (native.render(current.root) && native.getBuffer());
    if (renderGL && glOk) f.gl.assign(native.getBuffer(),native.getBuffer()+side*side*3);
    const bool good=action.getLastStatus()==CoinRenderAction::SUCCESS &&
      f.serial>before && f.gpu.size()==size_t(side*side*4) &&
      f.depth.size()==size_t(side*side) && glOk;
    if (!good) std::cerr << "precise_multitexture_capture status=" << action.getLastStatus()
                         << " error=" << action.getLastError().getString() << '\n';
    return check(good,"valid precise multitexture frame must publish");
  };
  auto capture=[&](Frame & f) { return captureScene(scene,f); };
  bool ok=true;
  Frame baseline;
  scene.materialBinding->value=SoMaterialBinding::PER_FACE;
  scene.normalBinding->value=SoNormalBinding::PER_FACE;
  ok &= capture(baseline);
  if (!baseline.gl.empty()) ok &= compare(baseline,"face_face");
  std::unique_ptr<CoinRenderTarget> recordingTarget(samplingTestOffscreen(SbVec2i32(side,side)));
  if (recordingTarget) {
    auto * recorder=new RecordingBackend;
    recordingTarget->getPimpl()->backend.reset(recorder);
    CoinRenderAction recordingAction(SbViewportRegion(side,side));
    recordingAction.setRenderTarget(recordingTarget.get());
    recordingAction.apply(scene.root);
    ok &= check(recordingAction.getLastStatus()==CoinRenderAction::SUCCESS,
                "recorded precise fan must be valid for the CPU reference backend");
    if (recordingAction.getLastStatus()==CoinRenderAction::SUCCESS)
      ok &= checkFanCenters(recorder->plan,scene);
    recordingAction.setRenderTarget(nullptr);
  } else ok &= check(false,"recording target must be available");
  auto freshBindingControl=[&](int materialMode,int normalMode) {
    Scene fresh;
    fresh.materialBinding->value=materialMode;
    fresh.normalBinding->value=normalMode;
    std::unique_ptr<CoinRenderTarget> freshTarget(samplingTestOffscreen(SbVec2i32(side,side)));
    if (!freshTarget || !freshTarget->setDepthReadbackEnabled(TRUE))
      return check(false,"fresh binding control target");
    CoinRenderAction freshAction(SbViewportRegion(side,side));
    freshAction.setRenderTarget(freshTarget.get());
    SoOffscreenRenderer freshGl(SbViewportRegion(side,side));
    freshGl.setComponents(SoOffscreenRenderer::RGB);
    freshGl.setBackgroundColor(SbColor(0,0,0));
    const uint64_t before=freshTarget->getLastSubmissionSerial();
    freshAction.apply(fresh.root);
    Frame result;
    result.serial=freshTarget->getLastSubmissionSerial();
    freshTarget->readbackRGBA(result.gpu);
    freshTarget->readbackDepth(result.depth);
    const bool glRendered=freshGl.render(fresh.root) && freshGl.getBuffer();
    if (glRendered) result.gl.assign(freshGl.getBuffer(),freshGl.getBuffer()+side*side*3);
    const bool captured=freshAction.getLastStatus()==CoinRenderAction::SUCCESS &&
      result.serial>before && result.depth.size()==size_t(side*side) && glRendered;
    const std::string name=std::string("fresh/material=")+bindingName(materialMode)+
                           "/normal="+bindingName(normalMode);
    std::cout << "precise_multitexture_binding " << name
              << " captured=" << captured << " gl_units=" << fresh.nativeTextureUnits << '\n';
    bool passed=check(captured,"fresh binding control must publish on new target");
    if (captured) passed &= compare(result,name.c_str());
    freshAction.setRenderTarget(nullptr);
    return passed;
  };
  ok &= freshBindingControl(SoMaterialBinding::PER_VERTEX,SoNormalBinding::OVERALL);
  ok &= freshBindingControl(SoMaterialBinding::PER_VERTEX,SoNormalBinding::PER_VERTEX);
  std::unique_ptr<CoinRenderTarget> matrixRecording(samplingTestOffscreen(SbVec2i32(side,side)));
  RecordingBackend * matrixRecorder=nullptr;
  std::unique_ptr<CoinRenderAction> matrixAction;
  if (matrixRecording) {
    matrixRecorder=new RecordingBackend;
    matrixRecording->getPimpl()->backend.reset(matrixRecorder);
    matrixAction.reset(new CoinRenderAction(SbViewportRegion(side,side)));
    matrixAction->setRenderTarget(matrixRecording.get());
  } else ok &= check(false,"binding matrix recording target must be available");
  auto bindingMatrix=[&](Scene & current,const char * group,bool nativeReference) {
    for(int materialMode : bindingModes) for(int normalMode : bindingModes) {
      current.materialBinding->value=materialMode;
      current.normalBinding->value=normalMode;
      const std::string name=std::string(group)+"/material="+bindingName(materialMode)+
                             "/normal="+bindingName(normalMode);
      Frame result;
      const bool captured=captureScene(current,result,nativeReference);
      ok &= captured;
      if (captured && nativeReference) ok &= compare(result,name.c_str());
      if (matrixAction) {
        matrixAction->apply(current.root);
        std::vector<unsigned char> cpuReference;
        matrixRecording->readbackRGBA(cpuReference);
        const bool recorded=matrixAction->getLastStatus()==CoinRenderAction::SUCCESS;
        ok &= check(recorded,"binding matrix must be accepted by the CPU reference");
        if (recorded && hasNativeFan(materialMode))
          ok &= checkFanCenters(matrixRecorder->plan,current);
        if (recorded && captured && current.eightUnits)
          ok &= compareExecutor(result.gpu,cpuReference,name.c_str());
      }
      std::cout << "precise_multitexture_binding " << name
                << " fan=" << hasNativeFan(materialMode)
                << " native=" << nativeReference << " captured=" << captured << '\n';
    }
  };
  bindingMatrix(scene,"units_0_3",true);
  scene.materialBinding->value=SoMaterialBinding::PER_FACE;
  scene.normalBinding->value=SoNormalBinding::PER_FACE;
  const unsigned char altered[]={245,245,245, 80,85,245, 75,245,85, 245,80,80};
  scene.secondTexture->image.setValue(SbVec2s(2,2),3,altered);
  Frame changedTexture;
  if (capture(changedTexture)) {
    ok &= compare(changedTexture,"mutated_unit3_texels");
    ok &= check(changed(baseline.gpu,changedTexture.gpu)>500,
                "mutating unit three must visibly invalidate the reused frame");
  } else ok=false;
  const SbVec2f moved[]={SbVec2f(0,0),SbVec2f(.5f,0),SbVec2f(1,0),
    SbVec2f(0,.5f),SbVec2f(.5f,.5f),SbVec2f(1,.5f),
    SbVec2f(0,1),SbVec2f(.5f,1),SbVec2f(1,1)};
  scene.secondCoordinates->point.setValues(0,9,moved);
  Frame changedCoordinates;
  if (capture(changedCoordinates)) {
    ok &= compare(changedCoordinates,"mutated_unit3_uv");
    ok &= check(changed(changedTexture.gpu,changedCoordinates.gpu)>500,
                "unit three UV revision must change the frame");
  } else ok=false;
  scene.secondUnit->unit=8;
  std::vector<unsigned char> rejectedColor;
  std::vector<float> rejectedDepth;
  const uint64_t stableSerial=target->getLastSubmissionSerial();
  action.apply(scene.root);
  target->readbackRGBA(rejectedColor);
  target->readbackDepth(rejectedDepth);
  ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
              rejectedColor==changedCoordinates.gpu &&
              rejectedDepth==changedCoordinates.depth &&
              target->getLastSubmissionSerial()==stableSerial,
              "out-of-range texture unit must reject without publishing color, depth, or serial");
  scene.secondUnit->unit=3;
  Frame recovered;
  if (capture(recovered)) {
    ok &= compare(recovered,"recovered");
    ok &= check(recovered.gpu==changedCoordinates.gpu &&
                recovered.depth==changedCoordinates.depth,
                "restoring the valid texture unit must recover the exact frame");
  } else ok=false;
  // Two separate meshes exercise the SoSeparator push/pop of unit-three
  // image state. Changing only the local image must leave the later sibling
  // untouched, even though both shapes use the same mesh coordinate table.
  scene.root->removeChild(scene.mesh);
  auto * local=new SoSeparator;
  auto * localUnit=new SoTextureUnit;
  localUnit->unit=3;
  local->addChild(localUnit);
  auto * localTexture=new SoTexture2;
  localTexture->model=SoTexture2::MODULATE;
  const unsigned char localWhite[]={255,255,255};
  localTexture->image.setValue(SbVec2s(1,1),3,localWhite);
  local->addChild(localTexture);
  auto * localShift=new SoTranslation;
  localShift->translation.setValue(-.47f,0,0);
  local->addChild(localShift);
  auto * localScale=new SoScale;
  localScale->scaleFactor.setValue(.49f,.82f,1);
  local->addChild(localScale);
  auto * localMesh=new SoQuadMesh;
  localMesh->verticesPerRow=localMesh->verticesPerColumn=3;
  local->addChild(localMesh);
  scene.root->addChild(local);
  auto * sibling=new SoSeparator;
  auto * siblingShift=new SoTranslation;
  siblingShift->translation.setValue(.47f,0,0);
  sibling->addChild(siblingShift);
  auto * siblingScale=new SoScale;
  siblingScale->scaleFactor.setValue(.49f,.82f,1);
  sibling->addChild(siblingScale);
  auto * siblingMesh=new SoQuadMesh;
  siblingMesh->verticesPerRow=siblingMesh->verticesPerColumn=3;
  sibling->addChild(siblingMesh);
  scene.root->addChild(sibling);
  Frame whiteLocal,blueLocal;
  const bool whiteCaptured=capture(whiteLocal);
  ok &= whiteCaptured;
  if (whiteCaptured) ok &= compare(whiteLocal,"separator_white",1400);
  const unsigned char localBlue[]={45,60,250};
  localTexture->image.setValue(SbVec2s(1,1),3,localBlue);
  const bool blueCaptured=capture(blueLocal);
  ok &= blueCaptured;
  if (blueCaptured) ok &= compare(blueLocal,"separator_blue",1400);
  if (whiteCaptured && blueCaptured) {
    size_t leftChanged=0,rightChanged=0;
    for(int y=20;y<76;++y) for(int x=10;x<86;++x) {
      const size_t at=size_t(y*side+x)*4;
      const bool differs=std::abs(int(whiteLocal.gpu[at])-int(blueLocal.gpu[at]))+
        std::abs(int(whiteLocal.gpu[at+1])-int(blueLocal.gpu[at+1]))+
        std::abs(int(whiteLocal.gpu[at+2])-int(blueLocal.gpu[at+2]))>20;
      leftChanged+=differs && x<48;
      rightChanged+=differs && x>=48;
    }
    std::cout << "precise_multitexture_separator changed_left=" << leftChanged
              << " changed_right=" << rightChanged << '\n';
    ok &= check(leftChanged>200 && rightChanged==0,
      "local texture mutation must affect the inner mesh and restore unit three before the sibling");
  }
  Scene eight(true);
  eight.materialBinding->value=SoMaterialBinding::PER_FACE;
  eight.normalBinding->value=SoNormalBinding::PER_FACE;
  eight.nativeTextureUnits=scene.nativeTextureUnits;
  const bool nativeEight=eight.nativeTextureUnits>=8;
  const bool eightGl=nativeEight;
  Frame eightBase;
  const bool eightCaptured=captureScene(eight,eightBase,eightGl);
  ok &= eightCaptured;
  std::cout << "precise_multitexture_eight native_GL_MAX_TEXTURE_UNITS="
            << eight.nativeTextureUnits << " requested=8 native8_unavailable="
            << !nativeEight << " executor_reference=" << executorReference << '\n';
  if (!executorReference) ok &= check(nativeEight,
              "CoinGL device must expose eight fixed-function texture units for the native cell");
  if (eightCaptured && nativeEight)
    ok &= compare(eightBase,"all_eight_units");
  bindingMatrix(eight,"units_0_7",eightGl);
  eight.materialBinding->value=SoMaterialBinding::PER_FACE;
  eight.normalBinding->value=SoNormalBinding::PER_FACE;
  std::vector<unsigned char> eightCpuBase;
  std::unique_ptr<CoinRenderTarget> eightRecording(samplingTestOffscreen(SbVec2i32(side,side)));
  if (eightRecording) {
    auto * recorder=new RecordingBackend;
    eightRecording->getPimpl()->backend.reset(recorder);
    CoinRenderAction recordingAction(SbViewportRegion(side,side));
    recordingAction.setRenderTarget(eightRecording.get());
    recordingAction.apply(eight.root);
    ok &= check(recordingAction.getLastStatus()==CoinRenderAction::SUCCESS,
                "all eight active units must produce a captured fan");
    if (recordingAction.getLastStatus()==CoinRenderAction::SUCCESS)
      ok &= checkFanCenters(recorder->plan,eight);
    eightRecording->readbackRGBA(eightCpuBase);
    if (executorReference && eightCaptured)
      ok &= compareExecutor(eightBase.gpu,eightCpuBase,"all_eight_units");
    recordingAction.setRenderTarget(nullptr);
  } else ok &= check(false,"eight-unit recording target must be available");
  const unsigned char alteredLast[]={55,70,210};
  eight.lastTexture->image.setValue(SbVec2s(1,1),3,alteredLast);
  Frame eightChanged;
  const bool changedCaptured=captureScene(eight,eightChanged,eightGl);
  ok &= changedCaptured;
  if (changedCaptured) {
    if (nativeEight) ok &= compare(eightChanged,"unit_seven_mutated");
    ok &= check(changed(eightBase.gpu,eightChanged.gpu)>500,
                "editing unit seven must visibly invalidate the published frame");
  }
  if (executorReference && changedCaptured) {
    std::unique_ptr<CoinRenderTarget> mutatedRecording(samplingTestOffscreen(SbVec2i32(side,side)));
    if (mutatedRecording) {
      mutatedRecording->getPimpl()->backend.reset(new RecordingBackend);
      CoinRenderAction recordingAction(SbViewportRegion(side,side));
      recordingAction.setRenderTarget(mutatedRecording.get());
      recordingAction.apply(eight.root);
      std::vector<unsigned char> cpuChanged;
      mutatedRecording->readbackRGBA(cpuChanged);
      ok &= check(recordingAction.getLastStatus()==CoinRenderAction::SUCCESS,
                  "CPU executor must accept the changed unit-seven image");
      ok &= compareExecutor(eightChanged.gpu,cpuChanged,"unit_seven_mutated");
      recordingAction.setRenderTarget(nullptr);
    } else ok &= check(false,"mutated eight-unit recording target must be available");
  }
  SbVec2f completeLastUv[9];
  for(int i=0;i<9;++i) completeLastUv[i]=eight.coordinates[7]->point[i];
  eight.coordinates[7]->point.setNum(8);
  const uint64_t completeSerial=target->getLastSubmissionSerial();
  std::vector<unsigned char> afterInvalid;
  std::vector<float> invalidDepth;
  action.apply(eight.root); // Do not pass this invalid input to CoinGL.
  target->readbackRGBA(afterInvalid);
  target->readbackDepth(invalidDepth);
  ok &= check(action.getLastStatus()!=CoinRenderAction::SUCCESS &&
              afterInvalid==eightChanged.gpu && invalidDepth==eightChanged.depth &&
              target->getLastSubmissionSerial()==completeSerial,
              "missing last-unit UV corner must reject without publishing color, depth, or serial");
  eight.coordinates[7]->point.setValues(0,9,completeLastUv);
  Frame uvRecovered;
  const bool uvRecovery=captureScene(eight,uvRecovered,eightGl);
  ok &= uvRecovery;
  if (uvRecovery) ok &= check(uvRecovered.gpu==eightChanged.gpu &&
                             uvRecovered.depth==eightChanged.depth,
                             "restored last-unit UV table must reproduce the exact frame");
  Scene shifted(true,2);
  shifted.materialBinding->value=SoMaterialBinding::PER_FACE;
  shifted.normalBinding->value=SoNormalBinding::PER_FACE;
  shifted.nativeTextureUnits=scene.nativeTextureUnits;
  Frame shiftedFrame;
  const bool shiftedCaptured=captureScene(shifted,shiftedFrame,eightGl);
  ok &= shiftedCaptured;
  if (shiftedCaptured) {
    if (nativeEight) ok &= compare(shiftedFrame,"start_index_two_eight_units");
    ok &= check(shiftedFrame.gpu==eightBase.gpu && shiftedFrame.depth==eightBase.depth,
                "geometry startIndex must not shift the texture-coordinate index table");
  }
  std::unique_ptr<CoinRenderTarget> shiftedRecording(samplingTestOffscreen(SbVec2i32(side,side)));
  if (shiftedRecording) {
    auto * recorder=new RecordingBackend;
    shiftedRecording->getPimpl()->backend.reset(recorder);
    CoinRenderAction recordingAction(SbViewportRegion(side,side));
    recordingAction.setRenderTarget(shiftedRecording.get());
    recordingAction.apply(shifted.root);
    ok &= check(recordingAction.getLastStatus()==CoinRenderAction::SUCCESS,
                "offset geometry with mesh-relative UV indices must capture");
    if (recordingAction.getLastStatus()==CoinRenderAction::SUCCESS)
      ok &= checkFanCenters(recorder->plan,shifted);
    recordingAction.setRenderTarget(nullptr);
  } else ok &= check(false,"shifted recording target must be available");
  if (matrixAction) matrixAction->setRenderTarget(nullptr);
  action.setRenderTarget(nullptr);
  return ok ? 0 : 1;
}
