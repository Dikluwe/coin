#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoIndexedTriangleStripSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoQuadMesh.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoTriangleStripSet.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

class FixtureViewport : public SoNode {
  SO_NODE_HEADER(FixtureViewport);
public:
  static void initClass(){SO_NODE_INIT_CLASS(FixtureViewport,SoNode,"Node");}
  FixtureViewport(){SO_NODE_CONSTRUCTOR(FixtureViewport);}
  SbViewportRegion viewport;
  void callback(SoCallbackAction * a) override {SoViewportRegionElement::set(a->getState(),viewport);}
  void GLRender(SoGLRenderAction * a) override {SoViewportRegionElement::set(a->getState(),viewport);}
protected:
  ~FixtureViewport() override=default;
};
SO_NODE_SOURCE(FixtureViewport);
namespace {
bool check(bool v, const std::string & message) {
  if (!v) std::cerr << message << '\n';
  return v;
}
struct Scene {
  Scene(bool indexed = true, int primitive = 0) {
    root=new SoSeparator; root->ref();
    viewportNode=new FixtureViewport; root->addChild(viewportNode);
    auto * camera=new SoOrthographicCamera;
    camera->height=2; camera->position.setValue(0,0,4);
    camera->nearDistance=.1f; camera->farDistance=10; root->addChild(camera);
    model=new SoLightModel; model->model=SoLightModel::BASE_COLOR; root->addChild(model);
    environment=new SoEnvironment; environment->ambientIntensity=.1f;
    environment->fogColor.setValue(.1f,.15f,.3f); environment->fogVisibility=10; root->addChild(environment);
    auto * directional=new SoDirectionalLight; directional->direction.setValue(.2f,0,-1);
    directional->intensity=.35f; root->addChild(directional);
    auto *point = new SoPointLight;
    point->location.setValue(0, .7f, 2);
    point->intensity = .15f;
    root->addChild(point);
    auto *spot = new SoSpotLight;
    spot->location.setValue(0, -.35f, 2);
    spot->direction.setValue(0, .2f, -1);
    spot->cutOffAngle=.8f; spot->dropOffRate=.02f; spot->intensity=.15f; root->addChild(spot);
    material=new SoMaterial; material->ambientColor.setValue(.1f,.1f,.1f);
    material->specularColor.setValue(0,0,0);
    for(int i=0;i<8;++i) material->diffuseColor.set1Value(i,SbColor(.3f+.07f*i,.8f-.06f*i,.4f+.04f*i));
    root->addChild(material);
    mb=new SoMaterialBinding; root->addChild(mb);
    normal=new SoNormal;
    for(int i=0;i<8;++i) normal->vector.set1Value(i,SbVec3f(.1f*(i%3),0,1));
    root->addChild(normal); nb=new SoNormalBinding; nb->value=SoNormalBinding::OVERALL; root->addChild(nb);
    style=new SoDrawStyle; style->lineWidth=3; style->pointSize=5; root->addChild(style);
    depth=new SoDepthBuffer; root->addChild(depth);
    auto * coords=new SoCoordinate3;
    const SbVec3f points[]={{-.85f,-.65f,0},{-.1f,-.65f,0},{-.1f,.65f,0},{-.85f,.65f,0},
                         {.1f,-.65f,0},{.85f,-.65f,0},{.85f,.65f,0},{.1f,.65f,0}};
    coords->point.setValues(0,8,points); root->addChild(coords);
    if (primitive == 1) {
      auto *n = new SoCube;
      n->width = 1.5f;
      n->height = 1.2f;
      n->depth = .7f;
      root->addChild(n);
    } else if (primitive == 2) {
      auto *n = new SoSphere;
      n->radius = .7f;
      root->addChild(n);
    } else if (primitive == 3) {
      auto *n = new SoCone;
      n->height = 1.4f;
      n->bottomRadius = .7f;
      root->addChild(n);
    } else if (primitive == 4) {
      auto *n = new SoCylinder;
      n->height = 1.4f;
      n->radius = .7f;
      root->addChild(n);
    } else if (primitive == 5) {
      const SbVec3f grid[] = {{-.8f, -.6f, 0}, {-.3f, -.6f, 0}, {.3f, -.6f, 0}, {.8f, -.6f, 0},
                              {-.8f, .6f, 0},  {-.3f, .6f, 0},  {.3f, .6f, 0},  {.8f, .6f, 0}};
      coords->point.setValues(0, 8, grid);
      auto *n = new SoQuadMesh;
      n->verticesPerRow = 4;
      n->verticesPerColumn = 2;
      root->addChild(n);
    } else if (primitive == 6 || primitive == 7) {
      const SbVec3f strips[] = {{-.8f, -.6f, 0}, {-.8f, .6f, 0}, {-.1f, -.6f, 0}, {-.1f, .6f, 0},
                                {.1f, -.6f, 0},  {.1f, .6f, 0},  {.8f, -.6f, 0},  {.8f, .6f, 0}};
      coords->point.setValues(0, 8, strips);
      if (primitive == 6) {
        auto *n = new SoTriangleStripSet;
        const int32_t nv[] = {4, 4};
        n->numVertices.setValues(0, 2, nv);
        root->addChild(n);
      } else {
        auto *n = new SoIndexedTriangleStripSet;
        const int32_t ci[] = {0, 1, 2, 3, -1, 4, 5, 6, 7, -1};
        n->coordIndex.setValues(0, 10, ci);
        root->addChild(n);
      }
    } else if (primitive == 8) {
      auto *n = new SoLineSet;
      const int32_t nv[] = {4, 4};
      n->numVertices.setValues(0, 2, nv);
      root->addChild(n);
    } else if (primitive == 9) {
      auto *n = new SoIndexedLineSet;
      const int32_t ci[] = {0, 1, 2, 3, -1, 4, 5, 6, 7, -1};
      n->coordIndex.setValues(0, 10, ci);
      root->addChild(n);
    } else if (primitive == 10) {
      auto *n = new SoPointSet;
      n->numPoints = 8;
      root->addChild(n);
    } else if (indexed) {
      faces=new SoIndexedFaceSet;
      const int32_t ci[]={0,1,2,3,-1,4,5,6,7,-1}; faces->coordIndex.setValues(0,10,ci);
      root->addChild(faces);
    } else {
      auto *node = new SoFaceSet;
      const int32_t counts[] = {4, 4};
      node->numVertices.setValues(0, 2, counts);
      root->addChild(node);
    }
  }
  ~Scene(){root->unref();}
  FixtureViewport * viewportNode; SoSeparator * root; SoIndexedFaceSet * faces=nullptr;
  SoMaterial * material; SoMaterialBinding * mb; SoNormal * normal; SoNormalBinding * nb;
  SoDrawStyle * style; SoLightModel * model; SoEnvironment * environment; SoDepthBuffer * depth;
};
class GeometryCapture : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderFramePlan frame;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &plan,
                                CoinRenderTargetP &target) override {
    frame = plan;
    return CoinRenderCpuReferenceBackend::submit(plan, target);
  }
};
struct Harness {
  Harness(bool gpu):action(SbViewportRegion(64,64)),gpuAction(SbViewportRegion(64,64)) {
    cpu.reset(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
    capture = new GeometryCapture;
    cpu->getPimpl()->backend.reset(capture);
    action.setRenderTarget(cpu.get());
    if(gpu){native.reset(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));gpuAction.setRenderTarget(native.get());}
    action.setBackgroundColor(SbColor4f(0,0,0,1));gpuAction.setBackgroundColor(SbColor4f(0,0,0,1));
    action.setTransparencyType(CoinRenderAction::BLEND);gpuAction.setTransparencyType(CoinRenderAction::BLEND);
  }
  ~Harness(){action.setRenderTarget(nullptr);gpuAction.setRenderTarget(nullptr);}
  bool compare(const std::vector<uint8_t> & a,const std::vector<uint8_t> & b,int channels,bool sparse,bool empty,const std::string & name) {
    if(!check(a.size()==size*size*4 && b.size()==size*size*channels,name+": readback extent"))return false;
    size_t count = 0;
    int maximum = 0;
    double error = 0;
    int worstX = 0, worstY = 0;
    auto covered=[](const std::vector<uint8_t>&p,size_t i){return p[i]||p[i+1]||p[i+2];};
    if(empty){for(size_t i=0;i<a.size();i+=4)if(!check(!covered(a,i),name+": empty viewport/style writes color"))return false;
      for(size_t i=0;i<b.size();i+=channels)if(!check(!covered(b,i),name+": reference writes empty viewport"))return false;return true;}
    for(int y=1;y<size-1;++y)for(int x=1;x<size-1;++x){
      bool stable=true;
      // Ignore only raster boundary pixels. Compare both covered interiors and
      // uncovered interiors so a shifted or missing shape cannot pass.
      const bool coverage=covered(b,(y*size+x)*channels);
      if(!sparse)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        stable &= coverage==covered(b,((y+dy)*size+x+dx)*channels);
      else stable=coverage && covered(a,(y*size+x)*4);
      if(!stable)continue;
      if(coverage)++count;
      for (int c = 0; c < 3; ++c) {
        const int d =
            std::abs(int(a[(y * size + x) * 4 + c]) - int(b[(y * size + x) * channels + c]));
        if (d > maximum) {
          maximum = d;
          worstX = x;
          worstY = y;
        }
        error += d;
      }
    }
    std::cout<<name<<" covered="<<count<<" max="<<maximum<<'\n';
    if (maximum > 4) {
      std::cerr << "worst=" << worstX << "," << worstY << " source=";
      for (int c = 0; c < 3; ++c)
        std::cerr << int(a[(worstY * size + worstX) * 4 + c]) << ",";
      std::cerr << " reference=";
      for (int c = 0; c < 3; ++c)
        std::cerr << int(b[(worstY * size + worstX) * channels + c]) << ",";
      std::cerr << '\n';
    }
    return check(count>=(sparse?4u:30u) && maximum<=4,name+": pixel parity");
  }
  bool render(Scene &scene, const SbViewportRegion &viewport, const std::string &label, bool fast,
              bool empty = false, bool nativeReference = true) {
    size=viewport.getWindowSize()[0]; scene.viewportNode->viewport=viewport;scene.viewportNode->touch();
    const SbViewportRegion full(size,size);action.setViewportRegion(full);gpuAction.setViewportRegion(full);
    cpu->resize(SbVec2i32(size,size));if(native)native->resize(SbVec2i32(size,size));
    action.setFastPathEnabled(fast);gpuAction.setFastPathEnabled(fast);
    action.apply(scene.root);
    if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS,label+": CPU "+action.getLastError().getString()))return false;
    std::vector<uint8_t>a,b;cpu->readbackRGBA(a);
    const bool sparse=scene.style->style.getValue()!=SoDrawStyle::FILLED;
    if(native){gpuAction.apply(scene.root);if(!check(gpuAction.getLastStatus()==CoinRenderAction::SUCCESS,label+": GPU "+gpuAction.getLastError().getString()))return false;
      native->readbackRGBA(b);if(!compare(a,b,4,sparse,empty,label+" CPU/GPU"))return false;
      if (nativeReference) {
        SoOffscreenRenderer gl(full);
        gl.setComponents(SoOffscreenRenderer::RGB);
        gl.setBackgroundColor(SbColor(0, 0, 0));
        gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
        if (!check(gl.render(scene.root) && gl.getBuffer(), label + ": mandatory CoinGL"))
          return false;
        const auto *p = gl.getBuffer();
        std::vector<uint8_t> reference(size * size * 3);
        for (int y = 0; y < size; ++y)
          std::copy(p + (size - 1 - y) * size * 3, p + (size - y) * size * 3,
                    reference.begin() + y * size * 3);
        if (!compare(a, reference, 3, sparse, empty, label + " CPU/CoinGL") ||
            !compare(b, reference, 3, sparse, empty, label + " GPU/CoinGL"))
          return false;
      }
    }
    ++cases;return true;
  }
  GeometryCapture *capture = nullptr;
  std::unique_ptr<CoinRenderTarget> cpu, native;
  CoinRenderAction action, gpuAction;
  int size = 64;
  unsigned cases = 0;
};
bool run(bool gpu) {
  Harness h(gpu);Scene scene;
  const std::array<int,4> rectangles[]={{0,0,64,64},{-16,0,64,64},{16,0,64,64},{0,-16,64,64},
    {0,16,64,64},{-16,-16,96,96},{16,16,64,64},{-80,0,64,64},{80,0,64,64},{0,80,64,64}};
  for(int size:{64,80,64})for(const auto&r:rectangles)for(int style:{SoDrawStyle::FILLED,SoDrawStyle::LINES,SoDrawStyle::POINTS,SoDrawStyle::INVISIBLE}) {
    scene.style->style=style;SbViewportRegion vp(size,size);vp.setViewportPixels(r[0],r[1],r[2],r[3]);
    const bool absent=r[0]+r[2]<=0||r[1]+r[3]<=0||r[0]>=size||r[1]>=size||style==SoDrawStyle::INVISIBLE;
    // A corner crop can exclude every point while still intersecting the viewport.
    if(style==SoDrawStyle::POINTS && r[0]==16 && r[1]==16 && size==64)continue;
    if(!h.render(scene,vp,"viewport/"+std::to_string(size)+"/"+std::to_string(r[0])+","+std::to_string(r[1])+"/style-"+std::to_string(style),true,absent))return false;
  }
  const int bindings[]={SoMaterialBinding::OVERALL,SoMaterialBinding::PER_FACE,SoMaterialBinding::PER_FACE_INDEXED,
    SoMaterialBinding::PER_VERTEX,SoMaterialBinding::PER_VERTEX_INDEXED};
  for(bool indexed:{false,true})for(int binding:bindings)for(bool generated:{false,true})for(bool alpha:{false,true})for(bool fast:{false,true}) {
    Scene bound(indexed);bound.model->model=SoLightModel::PHONG;bound.mb->value=binding;
    bound.nb->value= binding==SoMaterialBinding::OVERALL?SoNormalBinding::OVERALL:
      binding==SoMaterialBinding::PER_FACE?SoNormalBinding::PER_FACE:
      binding==SoMaterialBinding::PER_FACE_INDEXED?SoNormalBinding::PER_FACE_INDEXED:
      binding==SoMaterialBinding::PER_VERTEX?SoNormalBinding::PER_VERTEX:SoNormalBinding::PER_VERTEX_INDEXED;
    if(generated)bound.normal->vector.setNum(0);
    if(alpha)for(int i=0;i<8;++i)bound.material->transparency.set1Value(i,.1f+.08f*i);
    if(bound.faces){const int32_t perFace[]={1,0};const int32_t perVertex[]={3,2,1,0,-1,7,6,5,4,-1};
      bound.faces->materialIndex.setValues(0,binding==SoMaterialBinding::PER_FACE_INDEXED?2:10,binding==SoMaterialBinding::PER_FACE_INDEXED?perFace:perVertex);
      bound.faces->normalIndex.setValues(0,binding==SoMaterialBinding::PER_FACE_INDEXED?2:10,binding==SoMaterialBinding::PER_FACE_INDEXED?perFace:perVertex);}
    if(!h.render(bound,SbViewportRegion(64,64),"bindings/"+std::to_string(indexed)+"/"+std::to_string(binding)+"/generated-"+std::to_string(generated)+"/alpha-"+std::to_string(alpha)+"/fast-"+std::to_string(fast),fast))return false;
  }
  for(int fog:{SoEnvironment::NONE,SoEnvironment::HAZE,SoEnvironment::FOG,SoEnvironment::SMOKE})for(bool fast:{false,true})for(bool external:{false,true}) {
    scene.style->style=SoDrawStyle::FILLED;scene.model->model=SoLightModel::PHONG;scene.environment->fogType=fog;
    scene.depth->range.setValue(.2f,.8f);SbViewportRegion vp(64,64);if(external)vp.setViewportPixels(-16,-8,80,80);
    if(!h.render(scene,vp,"mixed-lights/fog-"+std::to_string(fog)+"/external-"+std::to_string(external),fast))return false;
  }
  // Native primitives choose their own normal/binding rules. Compare the
  // callbacks and GL helper under all seven effective material/normal bindings.
  const int primitiveBindings[] = {
      SoMaterialBinding::OVERALL,           SoMaterialBinding::PER_PART,
      SoMaterialBinding::PER_PART_INDEXED,  SoMaterialBinding::PER_FACE,
      SoMaterialBinding::PER_FACE_INDEXED,  SoMaterialBinding::PER_VERTEX,
      SoMaterialBinding::PER_VERTEX_INDEXED};
  for (int primitive = 1; primitive <= 10; ++primitive)
    for (int binding : primitiveBindings)
      for (bool fast : {false, true})
        for (bool generated : {false, true})
          for (bool alpha : {false, true}) {
            Scene bound(true, primitive);
            bound.model->model = SoLightModel::PHONG;
            bound.mb->value = binding;
            bound.nb->value = binding;
            if (generated)
              bound.normal->vector.setNum(0);
            if (alpha)
              for (int i = 0; i < 8; ++i)
                bound.material->transparency.set1Value(i, .1f + .08f * i);
            if (primitive >= 8)
              bound.style->style = primitive == 10 ? SoDrawStyle::POINTS : SoDrawStyle::LINES;
            if (!h.render(bound, SbViewportRegion(64, 64),
                          "primitive-bindings/" + std::to_string(primitive) + "/binding-" +
                              std::to_string(binding) + "/fast-" + std::to_string(fast) +
                              "/generated-" + std::to_string(generated) + "/alpha-" +
                              std::to_string(alpha),
                          fast))
              return false;
          }
  for (int primitive = 1; primitive <= 10; ++primitive)
    for (int fog :
         {SoEnvironment::NONE, SoEnvironment::HAZE, SoEnvironment::FOG, SoEnvironment::SMOKE})
      for (bool external : {false, true})
        for (bool fast : {false, true}) {
          Scene lit(true, primitive);
          lit.model->model = SoLightModel::PHONG;
          lit.environment->fogType = fog;
          if (primitive >= 8)
            lit.style->style = primitive == 10 ? SoDrawStyle::POINTS : SoDrawStyle::LINES;
          SbViewportRegion vp(64, 64);
          if (external)
            vp.setViewportPixels(-16, -8, 80, 80);
          if (!h.render(lit, vp,
                        "primitive-lights/" + std::to_string(primitive) + "/fog-" +
                            std::to_string(fog) + "/external-" + std::to_string(external) +
                            "/fast-" + std::to_string(fast),
                        fast))
            return false;
        }
  for (int primitive = 0; primitive <= 10; ++primitive)
    for (int binding : primitiveBindings)
      for (int style : {SoDrawStyle::LINES, SoDrawStyle::POINTS})
        for (bool fast : {false, true}) {
          Scene styled(true, primitive);
          styled.mb->value = binding;
          styled.nb->value = binding;
          styled.model->model = SoLightModel::PHONG;
          styled.style->style = style;
          for (int i = 0; i < 8; ++i)
            styled.material->transparency.set1Value(i, .1f + .08f * i);
          SbViewportRegion vp(64, 64);
          vp.setViewportPixels(-16, -8, 80, 80);
          // Portable ownership includes transparent polygon/line junctions.
          // Native references remain in the independent binding/light matrices;
          // differing endpoint coverage has dedicated diagnostic controls.
          const bool native = false;
          if (!h.render(styled, vp,
                        "styled-bindings/" + std::to_string(primitive) + "/binding-" +
                            std::to_string(binding) + "/style-" + std::to_string(style) + "/fast-" +
                            std::to_string(fast),
                        fast, false, native))
            return false;
        }
  // Independent authored diffuse values verify face-slot progression without
  // treating CoinGL pixels or the implementation as the expected binding.
  for (int primitive : {5, 6, 7}) {
    Scene flat(true, primitive);
    flat.mb->value = SoMaterialBinding::PER_FACE;
    flat.nb->value = SoNormalBinding::PER_FACE;
    if (!h.render(flat, SbViewportRegion(64, 64),
                  "numeric-face-binding/" + std::to_string(primitive), true))
      return false;
    for (const auto &draw : h.capture->frame.draws)
      for (uint32_t off = 0; off < draw.geometry.indexCount; off += 3) {
        float x = 0, y = 0;
        for (int j = 0; j < 3; ++j) {
          const auto &v =
              h.capture->frame
                  .vertices[h.capture->frame.indices[draw.geometry.firstIndex + off + j]];
          x += v.position[0] / 3;
          y += v.position[1] / 3;
        }
        const int face = primitive == 5 ? (x < -.3f  ? 0
                                           : x < .3f ? 1
                                                     : 2)
                                        : (x > 0 ? 2 : 0) + (y > 0 ? 1 : 0);
        const float expected[] = {.3f + .07f * face, .8f - .06f * face, .4f + .04f * face};
        for (int j = 0; j < 3; ++j) {
          const auto &v =
              h.capture->frame
                  .vertices[h.capture->frame.indices[draw.geometry.firstIndex + off + j]];
          const auto &m = h.capture->frame.materials[v.materialSlot];
          for (int c = 0; c < 3; ++c)
            if (!check(std::abs(m.diffuse[c] - expected[c]) < 1e-6f,
                       "authored per-face diffuse slot"))
              return false;
        }
      }
  }
  const SbVec2f ranges[] = {{.8f, .2f}, {1, 0},      {.5f, .5f}, {-2, 3},
                            {3, -2},    {-.5f, .4f}, {.6f, 2}};
  for (const auto &range : ranges)
    for (bool external : {false, true})
      for (int style : {SoDrawStyle::FILLED, SoDrawStyle::LINES, SoDrawStyle::POINTS}) {
        Scene ranged;
        ranged.depth->range = range;
        ranged.style->style = style;
        SbViewportRegion vp(64, 64);
        if (external)
          vp.setViewportPixels(-16, -8, 80, 80);
        if (!h.render(ranged, vp,
                      "depth-range/" + std::to_string(range[0]) + "," + std::to_string(range[1]) +
                          "/style-" + std::to_string(style) + "/external-" +
                          std::to_string(external),
                      true))
          return false;
      }
  // Nonfinite range is rejected before publication; the same action recovers.
  Scene recovery;
  if (!h.render(recovery, SbViewportRegion(64, 64), "range-before-rejection", true))
    return false;
  for (float invalid :
       {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
    std::vector<uint8_t> before, after;
    h.cpu->readbackRGBA(before);
    const auto serial = h.cpu->getLastSubmissionSerial();
    recovery.depth->range.setValue(invalid, .8f);
    h.action.apply(recovery.root);
    h.cpu->readbackRGBA(after);
    if (!check(h.action.getLastStatus() == CoinRenderAction::INVALID_SCENE && before == after &&
                   h.cpu->getLastSubmissionSerial() == serial,
               "invalid range CPU publication"))
      return false;
    if (h.native) {
      h.native->readbackRGBA(before);
      const auto gpuSerial = h.native->getLastSubmissionSerial();
      h.gpuAction.apply(recovery.root);
      h.native->readbackRGBA(after);
      if (!check(h.gpuAction.getLastStatus() == CoinRenderAction::INVALID_SCENE &&
                     before == after && h.native->getLastSubmissionSerial() == gpuSerial,
                 "invalid range GPU publication"))
        return false;
    }
    recovery.depth->range.setValue(0, 1);
    if (!h.render(recovery, SbViewportRegion(64, 64), "range-recovery", true))
      return false;
  }
  std::cout<<"Geometry/viewport cases="<<h.cases<<" GPU="<<gpu<<'\n';return true;
}
}
int main(int argc,char**argv){SoDB::init();CoinRenderAction::initClass();FixtureViewport::initClass();const bool gpu=argc>1&&std::string(argv[1])=="--gpu";
#ifndef HAVE_COIN_BGFX
  if(gpu&&!CoinRenderAction::isGpuBackendAvailable())return 77;
#endif
  if (argc > 1 && std::string(argv[1]) == "--study-curved-styles") {
    if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
    Harness h(true); unsigned attempted = 0, failures = 0;
    const int bindings[] = {SoMaterialBinding::OVERALL, SoMaterialBinding::PER_PART,
      SoMaterialBinding::PER_PART_INDEXED, SoMaterialBinding::PER_FACE,
      SoMaterialBinding::PER_FACE_INDEXED, SoMaterialBinding::PER_VERTEX,
      SoMaterialBinding::PER_VERTEX_INDEXED};
    for (int primitive : {2, 3, 4}) for (int binding : bindings)
      for (int style : {SoDrawStyle::LINES, SoDrawStyle::POINTS}) for (bool fast : {false, true}) {
        Scene scene(true, primitive); scene.mb->value=binding; scene.nb->value=binding;
        scene.model->model=SoLightModel::PHONG;scene.style->style=style;
        for (int i=0;i<8;++i)scene.material->transparency.set1Value(i,.1f+.08f*i);
        SbViewportRegion vp(64,64);vp.setViewportPixels(-16,-8,80,80);
        ++attempted;
        if (!h.render(scene,vp,"study-curved/"+std::to_string(primitive)+"/binding-"+
          std::to_string(binding)+"/style-"+std::to_string(style)+"/fast-"+
          std::to_string(fast),fast,false,false)) ++failures;
      }
    std::cout << "Raster study attempted=" << attempted << " failures=" << failures << '\n';
    return failures ? 1 : 0;
  }
  if (argc > 1 && std::string(argv[1]).find("--probe-style-") == 0) {
    const std::string mode = argv[1];
    const bool line = mode.find("line") != std::string::npos;
    Harness h(true);
    Scene scene(true, line ? 8 : 3);
    scene.mb->value = line ? SoMaterialBinding::PER_PART : SoMaterialBinding::OVERALL;
    scene.nb->value = line ? SoNormalBinding::PER_PART : SoNormalBinding::OVERALL;
    scene.model->model = SoLightModel::PHONG;
    scene.style->style = SoDrawStyle::LINES;
    if (mode.find("width1") != std::string::npos) scene.style->lineWidth = 1;
    for (int i = 0; i < 8; ++i) scene.material->transparency.set1Value(i, .1f + .08f * i);
    if (mode.find("depthless") != std::string::npos) scene.depth->test = FALSE;
    if (mode.find("readonly") != std::string::npos) scene.depth->write = FALSE;
    SbViewportRegion vp(64, 64);
    vp.setViewportPixels(-16, -8, 80, 80);
    return h.render(scene, vp, mode, false, false, line) ? 0 : 1;
  }
  return run(gpu)?0:1;}
