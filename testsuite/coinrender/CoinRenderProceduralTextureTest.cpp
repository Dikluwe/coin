#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#if COIN_HAVE_LEGACY_GL_RENDERER
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/system/gl.h>
#endif
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureCoordinatePlane.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// An authored coordinate function with independent R/Q and a normal term.
// Both action paths use the public element; no executor interprets the node.
class ProceduralCoordinates : public SoNode {
  SO_NODE_HEADER(ProceduralCoordinates);
public:
  static void initClass() { SO_NODE_INIT_CLASS(ProceduralCoordinates, SoNode, "Node"); }
  ProceduralCoordinates() { SO_NODE_CONSTRUCTOR(ProceduralCoordinates); }
  bool invalid = false;
  float bias = 0;
  unsigned calls = 0;
  static SbVec4f expected(const SbVec3f & p, const SbVec3f & n) {
    return SbVec4f(.5f + .25f*p[0], .5f + .3f*p[1], .1f + .1f*n[2], 1.2f + .2f*p[0]);
  }
  static const SbVec4f & generate(void * data, const SbVec3f & p, const SbVec3f & n) {
    auto * self = static_cast<ProceduralCoordinates *>(data);
    ++self->calls;
    self->value = expected(p, n);
    self->value[0] += self->bias;
    if (self->invalid) self->value[0] = std::numeric_limits<float>::quiet_NaN();
    return self->value;
  }
  void doAction(SoAction * action) override {
    SoMultiTextureCoordinateElement::setFunction(action->getState(), this,
      SoTextureUnitElement::get(action->getState()), generate, this);
  }
  void callback(SoCallbackAction * action) override { doAction(action); }
  void GLRender(SoGLRenderAction * action) override { doAction(action); }
protected:
  ~ProceduralCoordinates() override = default;
private:
  SbVec4f value;
};
SO_NODE_SOURCE(ProceduralCoordinates);

class ProceduralFaces : public SoIndexedFaceSet {
  SO_NODE_HEADER(ProceduralFaces);
public:
  static void initClass() { SO_NODE_INIT_CLASS(ProceduralFaces, SoIndexedFaceSet, "IndexedFaceSet"); }
  ProceduralFaces() { SO_NODE_CONSTRUCTOR(ProceduralFaces); }
  unsigned callbacks=0;
  void callback(SoCallbackAction * action) override {
    ++callbacks;
    SoIndexedFaceSet::callback(action);
  }
protected:
  ~ProceduralFaces() override = default;
};
SO_NODE_SOURCE(ProceduralFaces);

// A custom state node may enable a unit without an image payload. The native
// callback still chooses that unit; the capture must not mistake another
// unit's function for the primary coordinate already present in the vertex.
class EnabledEmptyTexture : public SoNode {
  SO_NODE_HEADER(EnabledEmptyTexture);
public:
  static void initClass() { SO_NODE_INIT_CLASS(EnabledEmptyTexture, SoNode, "Node"); }
  EnabledEmptyTexture() { SO_NODE_CONSTRUCTOR(EnabledEmptyTexture); }
  void callback(SoCallbackAction * action) override {
    SoMultiTextureImageElement::setDefault(action->getState(),this,0);
    SoMultiTextureEnabledElement::set(action->getState(),this,0,TRUE);
  }
protected:
  ~EnabledEmptyTexture() override = default;
};
SO_NODE_SOURCE(EnabledEmptyTexture);

namespace {
constexpr int size = 80;
bool check(bool result, const std::string & label) {
  if (!result) std::cerr << "CoinRenderProceduralTextureTest: " << label << '\n';
  return result;
}
class CaptureBackend : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderFramePlan frame;
  unsigned submissions = 0;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & plan, CoinRenderTargetP & target) override {
    frame = plan; ++submissions;
    return CoinRenderCpuReferenceBackend::submit(plan, target);
  }
};
struct Scene {
  Scene(int shape = 0) {
    root = new SoSeparator; root->ref();
    auto * camera = new SoOrthographicCamera;
    camera->position.setValue(0, 0, 4); camera->height = 3;
    root->addChild(camera);
    auto * light = new SoLightModel; light->model = SoLightModel::BASE_COLOR; root->addChild(light);
    quality = new SoComplexity; quality->textureQuality = .5f; root->addChild(quality);
    style=new SoDrawStyle; style->lineWidth=4; style->pointSize=5; root->addChild(style);
    // State must reach geometry, so this group is deliberately not a separator.
    stages = new SoGroup; root->addChild(stages);
    auto * coords = new SoCoordinate3;
    const SbVec3f p[] = {{-.9f,-.6f,0},{.9f,-.6f,0},{.9f,.6f,0},{-.9f,.6f,0}};
    coords->point.setValues(0, 4, p); root->addChild(coords);
    switch (shape) {
    case 1: { auto * node = new SoCube; node->width=1.5f; node->height=1.2f; node->depth=.7f; root->addChild(node); break; }
    case 2: { auto * node = new SoSphere; node->radius=.8f; root->addChild(node); break; }
    case 3: { auto * node = new SoCone; node->height=1.4f; node->bottomRadius=.7f; root->addChild(node); break; }
    case 4: { auto * node = new SoCylinder; node->height=1.4f; node->radius=.7f; root->addChild(node); break; }
    case 5: { auto * node = new SoLineSet; node->numVertices=4; root->addChild(node); break; }
    case 6: { auto * node = new SoPointSet; node->numPoints=4; root->addChild(node); break; }
    default: { SoIndexedFaceSet * node = shape==7 ? static_cast<SoIndexedFaceSet *>(new ProceduralFaces) : new SoIndexedFaceSet;
      if (shape==7) faces=static_cast<ProceduralFaces *>(node);
      const int32_t indices[]={0,1,2,3,-1};
      node->coordIndex.setValues(0,5,indices); root->addChild(node); break; }
    }
  }
  ~Scene() { root->unref(); }
  // mode 0 default, 1 plane, 2 explicit, 3 authored homogeneous function.
  ProceduralCoordinates * stage(int unit, int mode) {
    auto * selected = new SoTextureUnit; selected->unit=unit; stages->addChild(selected);
    ProceduralCoordinates * function = nullptr;
    if (mode == 1) {
      auto * plane = new SoTextureCoordinatePlane;
      plane->directionS.setValue(.25f,0,0); plane->directionT.setValue(0,.3f,0);
      plane->directionR.setValue(0,0,0);
      stages->addChild(plane);
    } else if (mode == 2) {
      auto * coords = new SoTextureCoordinate2;
      const SbVec2f uv[]={{.275f,.32f},{.725f,.32f},{.725f,.68f},{.275f,.68f}};
      coords->point.setValues(0,4,uv); stages->addChild(coords);
    } else if (mode == 3) {
      function = new ProceduralCoordinates; stages->addChild(function);
    }
    auto * image = new SoTexture2;
    image->model=unit == 0 ? SoTexture2::REPLACE : SoTexture2::MODULATE;
    image->wrapS=SoTexture2::CLAMP; image->wrapT=SoTexture2::CLAMP;
    std::vector<uint8_t> pixels(16*16*3);
    for (int y=0;y<16;++y) for (int x=0;x<16;++x) {
      const int i=(y*16+x)*3;
      pixels[i]=unit == 0 ? 80+8*x : 220+2*x;
      pixels[i+1]=unit == 0 ? 80+8*y : 220+2*y; pixels[i+2]=unit == 0 ? 160 : 240;
    }
    image->image.setValue(SbVec2s(16,16),3,pixels.data()); stages->addChild(image);
    return function;
  }
  SoSeparator * root;
  SoGroup * stages;
  SoComplexity * quality;
  SoDrawStyle * style;
  ProceduralFaces * faces=nullptr;
};
SbVec4f coordinate(const CoinRenderVertexSnapshot & v, int unit) {
  return SbVec4f(unit ? v.extraTexcoords[unit-1][0] : v.texcoord[0],
                 unit ? v.extraTexcoords[unit-1][1] : v.texcoord[1],
                 v.textureR[unit], v.textureQ[unit]);
}
bool close(const SbVec4f & a, const SbVec4f & b) {
  for (int i=0;i<4;++i) if (std::abs(a[i]-b[i]) > 1e-5f) return false;
  return true;
}
struct Harness {
  Harness(bool gpu) : action(SbViewportRegion(size,size)), gpuAction(SbViewportRegion(size,size)) {
    cpu.reset(CoinRenderTarget::createOffscreen(SbVec2i32(size,size)));
    capture = new CaptureBackend; cpu->getPimpl()->backend.reset(capture); action.setRenderTarget(cpu.get());
    if (gpu) { native.reset(CoinRenderTarget::createOffscreen(SbVec2i32(size,size))); gpuAction.setRenderTarget(native.get()); }
    action.setBackgroundColor(SbColor4f(0,0,0,1)); gpuAction.setBackgroundColor(SbColor4f(0,0,0,1));
  }
  ~Harness() { action.setRenderTarget(nullptr); gpuAction.setRenderTarget(nullptr); }
  bool render(Scene & scene, const std::string & label, bool fast) {
    action.setFastPathEnabled(fast); gpuAction.setFastPathEnabled(fast);
    action.apply(scene.root);
    if (!check(action.getLastStatus()==CoinRenderAction::SUCCESS,
      label+": capture "+action.getLastError().getString())) return false;
    if (!check(!capture->frame.draws.empty() && !capture->frame.textures.empty(),label+": vacuous capture")) return false;
    if (native) {
      gpuAction.apply(scene.root);
      if (!check(gpuAction.getLastStatus()==CoinRenderAction::SUCCESS,
        label+": GPU "+gpuAction.getLastError().getString())) return false;
      std::vector<uint8_t> a,b; cpu->readbackRGBA(a); native->readbackRGBA(b);
      if (!compare(a,b,4,label+"/CPU-GPU")) return false;
    }
    ++cases;
    return true;
  }
  bool compare(const std::vector<uint8_t> & a, const std::vector<uint8_t> & b,
               int channels, const std::string & label) {
    if (!check(a.size()==size*size*4 && b.size()==size*size*channels,label+": empty pixels")) return false;
    size_t count=0; double sum=0; int maximum=0;
    auto covered=[&](const std::vector<uint8_t> & pixels,int x,int y,int c) {
      const size_t i=(y*size+x)*c;
      return pixels[i] || pixels[i+1] || pixels[i+2];
    };
    // Interior pixels separate UV semantics from existing raster edge contracts.
    for (int y=2;y<size-2;++y) for (int x=2;x<size-2;++x) {
      bool interior=true;
      for (int dy=-radius;dy<=radius;++dy) for (int dx=-radius;dx<=radius;++dx)
        interior &= covered(a,x+dx,y+dy,4) && covered(b,x+dx,y+dy,channels);
      if (!interior) continue;
      ++count;
      for (int c=0;c<3;++c) {
        const int d=std::abs(int(a[(y*size+x)*4+c])-int(b[(y*size+x)*channels+c]));
        sum+=d; maximum=std::max(maximum,d);
      }
    }
    if (!check(count>=minimum,label+": no nonempty interior reference")) return false;
    std::cout<<label<<" pixels="<<count<<" mae="<<sum/(3*count)<<" max="<<maximum<<'\n';
    return check(sum/(3*count)<=1.0 && maximum<=3,label+": UV differs");
  }
  bool gl(Scene & scene, const std::string & label) {
#if COIN_HAVE_LEGACY_GL_RENDERER
    SoOffscreenRenderer renderer(SbViewportRegion(size,size));
    renderer.setComponents(SoOffscreenRenderer::RGB); renderer.setBackgroundColor(SbColor(0,0,0));
    if (!check(renderer.render(scene.root),label+": mandatory CoinGL unavailable")) return false;
    const uint8_t * source=renderer.getBuffer(); std::vector<uint8_t> gl(size*size*3);
    for (int y=0;y<size;++y)
      std::copy(source+(size-1-y)*size*3,source+(size-y)*size*3,gl.begin()+y*size*3);
    std::vector<uint8_t> pixels; cpu->readbackRGBA(pixels);
    if (!compare(pixels,gl,3,label+"/CPU-CoinGL")) return false;
    if (native) { native->readbackRGBA(pixels); if (!compare(pixels,gl,3,label+"/GPU-CoinGL")) return false; }
    ++references; return true;
#else
    return check(false,label+": CoinGL not compiled");
#endif
  }
  std::unique_ptr<CoinRenderTarget> cpu,native;
  CaptureBackend * capture;
  CoinRenderAction action,gpuAction;
  unsigned cases=0,references=0;
  int radius=1; size_t minimum=100;
};
bool run(bool gpu) {
  Harness h(gpu);
  for (int shape=0;shape<8;++shape) for (int mode : {0,1,3}) {
    h.radius=(shape==5 || shape==6) ? 0 : 1; h.minimum=(shape==5 || shape==6) ? 4 : 100;
    Scene scene(shape); auto * function=scene.stage(0,mode);
    unsigned nativeCalls=0;
    if ((shape==0 || shape==7) && function) {
      SoCallbackAction probe;
      probe.addTriangleCallback(SoShape::getClassTypeId(),
        [](void *, SoCallbackAction *, const SoPrimitiveVertex *, const SoPrimitiveVertex *, const SoPrimitiveVertex *) {}, nullptr);
      const unsigned before=function->calls; probe.apply(scene.root); nativeCalls=function->calls-before;
      if (!check(nativeCalls>0,"native function witness empty")) return false;
    }
    for (bool fast : {false,true}) {
      const unsigned before=function ? function->calls : 0;
      if (!h.render(scene,"shape-"+std::to_string(shape)+"/mode-"+std::to_string(mode),fast)) return false;
      for (const auto & v : h.capture->frame.vertices) {
        if ((shape==0 || shape==7) && mode==0 && !check(close(coordinate(v,0),SbVec4f((v.position[0]+.9f)/1.8f,(v.position[1]+.6f)/1.2f,0,1)),"default bounding-box map")) return false;
        if ((shape<5 || shape==7) && mode==3 && !check(close(coordinate(v,0),ProceduralCoordinates::expected(SbVec3f(v.position),SbVec3f(v.normal))),"authored ST/R/Q")) return false;
        if ((shape<5 || shape==7) && mode==1 && !close(coordinate(v,0),SbVec4f(.25f*v.position[0],.3f*v.position[1],0,1))) {
          const auto uv=coordinate(v,0);
          std::cerr<<"shape="<<shape<<" fast="<<fast<<" position="<<v.position[0]<<","<<v.position[1]<<","<<v.position[2]
                   <<" uv="<<uv[0]<<","<<uv[1]<<","<<uv[2]<<","<<uv[3]<<'\n';
          return check(false,"plane object-space UV");
        }
      }
      if (function && !check(function->calls>0,"coordinate function not evaluated")) return false;
      if (nativeCalls && !check(function->calls-before==nativeCalls*(gpu ? 2 : 1),"primary native callback evaluated twice")) return false;
      // Lines/points have their own raster contracts; custom callbacks on built-in
      // primitive GL helpers do not install texgen. Compare filled supported cases.
      if (scene.faces && !check(scene.faces->callbacks>0,"subclass callback bypassed")) return false;
      if (gpu && (shape<5 || shape==7) && (mode!=3 || shape==0 || shape==7) && !h.gl(scene,"native")) return false;
    }
  }
  // P02/P07 interaction: native polygon contours/vertices retain procedural
  // coordinates on canonical primitives and indexed faces, also across units.
  h.radius=0; h.minimum=4;
  for (int shape=0;shape<(gpu ? 3 : 5);++shape)
    for (int style : {SoDrawStyle::LINES,SoDrawStyle::POINTS})
      for (int mode : {0,1}) for (bool multi : {false,true}) {
        Scene scene(shape); scene.style->style=style; scene.stage(0,mode);
        if (multi) scene.stage(1,1);
        for (bool fast : {false,true}) {
          if (!h.render(scene,"polygon-style/shape-"+std::to_string(shape)+"/style-"+std::to_string(style)+
            "/UV-"+std::to_string(mode)+"/multi-"+std::to_string(multi),fast)) return false;
          // Curved overlapping contours keep CPU/GPU qualification; their
          // textured CoinGL raster junctions need a dedicated sampling oracle.
          if (gpu && shape<2 && !h.gl(scene,"polygon-style-native")) return false;
        }
      }
  h.radius=1; h.minimum=100;
  // Per-unit functions, mixed modes, and a primary unit above zero.
  for (int mixed=0;mixed<9;++mixed) {
    Scene scene;
    if (mixed==0) { scene.stage(0,2); scene.stage(1,3); }
    if (mixed==1) { scene.stage(0,3); scene.stage(1,0); }
    if (mixed==2) { scene.stage(3,3); }
    if (mixed==3) { scene.stage(3,0); }
    if (mixed==4) { for (int unit=0;unit<8;++unit) scene.stage(unit,3); }
    if (mixed==5) { scene.stage(0,0); scene.stage(1,3); }
    if (mixed==6) { scene.stages->addChild(new EnabledEmptyTexture); scene.stage(3,3); }
    if (mixed==7) { scene.stage(0,0); scene.stage(1,2); }
    if (mixed==8) { scene.stage(0,2); scene.stage(1,0); }
    for (bool fast : {false,true}) {
      if (!h.render(scene,"mixed-"+std::to_string(mixed),fast)) return false;
      for (const auto & v : h.capture->frame.vertices) {
        const auto expect=ProceduralCoordinates::expected(SbVec3f(v.position),SbVec3f(v.normal));
        for (int unit=0;unit<8;++unit) {
          const bool authored=(mixed==0&&unit==1)||(mixed==1&&unit==0)||((mixed==2||mixed==6)&&unit==3)||mixed==4||(mixed==5&&unit==1);
          if (authored && !check(close(coordinate(v,unit),expect),"per-unit homogeneous function")) return false;
        }
        const int defaultUnit=(mixed==1 || mixed==8) ? 1 : mixed==3 ? 3 : (mixed==5 || mixed==7) ? 0 : -1;
        if (defaultUnit>=0 && !check(close(coordinate(v,defaultUnit),SbVec4f((v.position[0]+.9f)/1.8f,(v.position[1]+.6f)/1.2f,0,1)),"per-unit default bounding-box map")) return false;
        if (mixed==7 && !check(close(coordinate(v,1),SbVec4f(.5f+.25f*v.position[0],.5f+.3f*v.position[1],0,1)),"explicit upper corrupted by primary default")) return false;
        if ((mixed==0 || mixed==8) && !check(close(coordinate(v,0),SbVec4f(.5f+.25f*v.position[0],.5f+.3f*v.position[1],0,1)),"explicit primary corrupted by upper function")) return false;
      }
    }
  }
  Scene scene; auto * function=scene.stage(0,3);
  auto * matrix=new SoTextureMatrixTransform;
  SbMatrix projective=SbMatrix::identity();
  projective[0][0]=.7f; projective[1][1]=.65f; projective[2][0]=.15f;
  projective[0][3]=.4f; projective[1][3]=.2f; projective[2][3]=.3f;
  projective[3][0]=.1f; projective[3][1]=.12f; projective[3][3]=.9f;
  matrix->matrix=projective; scene.stages->addChild(matrix);
  if (!h.render(scene,"homogeneous-function/projective-matrix",true)) return false;
  if (gpu && !h.gl(scene,"homogeneous-function/projective-matrix")) return false;
  matrix->matrix=SbMatrix::identity();

  if (!h.render(scene,"recovery-before",true)) return false;
  std::vector<uint8_t> previous; h.cpu->readbackRGBA(previous);
  function->bias=.15f;
  if (!h.render(scene,"external-function-data-change",true)) return false;
  std::vector<uint8_t> changed; h.cpu->readbackRGBA(changed);
  if (!check(changed!=previous,"mutable function data retained stale frame")) return false;
  previous=changed;
  const unsigned submissions=h.capture->submissions;
  function->invalid=true; h.action.apply(scene.root);
  if (!check(h.action.getLastStatus()==CoinRenderAction::INVALID_SCENE && h.capture->submissions==submissions,"nonfinite UV submitted")) return false;
  std::vector<uint8_t> preserved; h.cpu->readbackRGBA(preserved);
  if (!check(preserved==previous,"invalid UV replaced pixels")) return false;
  if (h.native) {
    h.native->readbackRGBA(previous); h.gpuAction.apply(scene.root); h.native->readbackRGBA(preserved);
    if (!check(h.gpuAction.getLastStatus()==CoinRenderAction::INVALID_SCENE && preserved==previous,"GPU invalid UV publication")) return false;
  }
  function->invalid=false;
  if (!h.render(scene,"recovery-after",true)) return false;
  const unsigned beforeQuality=h.capture->submissions;
  h.cpu->readbackRGBA(previous);
  scene.quality->textureQuality=.8f; h.action.apply(scene.root);
  h.cpu->readbackRGBA(preserved);
  if (!check(h.action.getLastStatus()==CoinRenderAction::UNSUPPORTED &&
      h.capture->submissions==beforeQuality && preserved==previous,"unsupported quality publication")) return false;
  if (h.native) {
    h.native->readbackRGBA(previous); h.gpuAction.apply(scene.root); h.native->readbackRGBA(preserved);
    if (!check(h.gpuAction.getLastStatus()==CoinRenderAction::UNSUPPORTED && preserved==previous,"GPU quality publication")) return false;
  }
  scene.quality->textureQuality=0;
  h.action.apply(scene.root);
  if (!check(h.action.getLastStatus()==CoinRenderAction::SUCCESS && h.capture->frame.textures.empty(),"quality zero texture allocation")) return false;
  if (h.native) {
    h.gpuAction.apply(scene.root);
    if (!check(h.gpuAction.getLastStatus()==CoinRenderAction::SUCCESS,"GPU quality zero")) return false;
  }
  scene.quality->textureQuality=.5f;
  if (!h.render(scene,"quality-recovery",true)) return false;
  if (gpu) {
    CoinRenderCapabilities capabilities{};
    if (!check(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&capabilities,sizeof(capabilities))==0 &&
        (capabilities.features & COIN_RENDER_FEATURE_PROCEDURAL_TEXTURE_COORDINATES),"compiled procedural capability")) return false;
  }
  std::cout<<"P07 passed cases="<<h.cases<<" CoinGL_references="<<h.references<<'\n';
  return true;
}
}
int main(int argc,char ** argv) {
  SoDB::init(); CoinRenderAction::initClass(); ProceduralCoordinates::initClass(); ProceduralFaces::initClass(); EnabledEmptyTexture::initClass();
  // Opt-in reproducers for the open textured contour junctions. These are
  // intentionally outside the qualified CTest profile; thresholds stay intact.
  if (argc>1 && (std::string(argv[1])=="--probe-sphere" || std::string(argv[1])=="--probe-cone")) {
    const bool sphere=std::string(argv[1])=="--probe-sphere";
    Harness h(true);h.radius=0;h.minimum=4;Scene scene(sphere?2:3);
    scene.style->style=SoDrawStyle::LINES;scene.stage(0,0);
    if (!h.render(scene,"open-coincident-contour",true)) return 1;
    return sphere && !h.gl(scene,"open-curved-CoinGL") ? 1 : 0;
  }
  return run(argc>1 && std::string(argv[1])=="--gpu") ? 0 : 1;
}
