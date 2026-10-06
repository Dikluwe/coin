#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoPrimitiveVertex.h>
#if COIN_HAVE_LEGACY_GL_RENDERER
#include <Inventor/SoOffscreenRenderer.h>
#endif
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/fields/SoSFBool.h>
#include <Inventor/fields/SoSFVec3f.h>
#include <Inventor/elements/SoComplexityTypeElement.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoIndexedMarkerSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureCoordinateDefault.h>
#include <Inventor/nodes/SoTextureCoordinatePlane.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoVertexProperty.h>
#include <Inventor/nodes/SoVertexShape.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "actions/CoinRenderActionP.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// The native SoShape path consults computeBBox, uses the midpoint of its
// bounds, then calls sogl_render_cube. Deliberately give it a different
// center and a single original triangle, so neither can masquerade as a box.
class BoundingFixtureShape : public SoShape {
  SO_NODE_HEADER(BoundingFixtureShape);
public:
  BoundingFixtureShape() {
    SO_NODE_CONSTRUCTOR(BoundingFixtureShape);
    SO_NODE_ADD_FIELD(minimum, (SbVec3f(-.6f, -.4f, -.3f)));
    SO_NODE_ADD_FIELD(maximum, (SbVec3f(.6f, .4f, .3f)));
    SO_NODE_ADD_FIELD(empty, (FALSE));
  }
  static void initClass() { SO_NODE_INIT_CLASS(BoundingFixtureShape, SoShape, "Shape"); }
  SoSFVec3f minimum, maximum;
  SoSFBool empty;
  void computeBBox(SoAction *, SbBox3f & box, SbVec3f & center) override {
    if (empty.getValue()) box.makeEmpty();
    else box.setBounds(minimum.getValue(), maximum.getValue());
    center.setValue(37, -41, 99);
  }
protected:
  ~BoundingFixtureShape() override = default;
  void generatePrimitives(SoAction * action) override {
    if (empty.getValue()) return;
    const SbVec3f lo = minimum.getValue(), hi = maximum.getValue();
    const SbVec3f positions[] = {{lo[0], lo[1], hi[2]}, {hi[0], lo[1], hi[2]},
                                {(lo[0] + hi[0]) * .5f, hi[1], hi[2]}};
    beginShape(action, TRIANGLES);
    for (int i = 0; i < 3; ++i) {
      SoPrimitiveVertex vertex;
      vertex.setPoint(positions[i]); vertex.setNormal(SbVec3f(0, 0, 1));
      vertex.setTextureCoords(SbVec4f(i == 1 ? 1 : 0, i == 2 ? 1 : 0, 0, 1));
      vertex.setMaterialIndex(0); shapeVertex(&vertex);
    }
    endShape();
  }
};
SO_NODE_SOURCE(BoundingFixtureShape);

namespace {
constexpr int width = 96, height = 80;
size_t nativeGlComparisons = 0;
bool check(bool condition, const std::string & message) {
  if (!condition) std::cerr << "CoinRenderBoundingBoxTest: " << message << '\n';
  return condition;
}
size_t colored(const std::vector<uint8_t> & pixels) {
  size_t result = 0;
  for (size_t i = 0; i + 3 < pixels.size(); i += 4)
    result += pixels[i] > 3 || pixels[i + 1] > 3 || pixels[i + 2] > 3;
  return result;
}
class CaptureBackend : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & value, CoinRenderTargetP & target) override {
    ++submissions; frame = value;
    return CoinRenderCpuReferenceBackend::submit(value, target);
  }
  size_t submissions = 0;
  CoinRenderFramePlan frame;
};
struct Harness {
  explicit Harness(bool selectedGpu) : gpu(selectedGpu), cpuAction(SbViewportRegion(width, height)),
    gpuAction(SbViewportRegion(width, height))
#if COIN_HAVE_LEGACY_GL_RENDERER
    , gl(SbViewportRegion(width, height))
#endif
  {
    cpu.reset(CoinRenderTarget::createOffscreen(SbVec2i32(width, height)));
    if (cpu) {
      observer = new CaptureBackend; cpu->getPimpl()->backend.reset(observer);
      cpu->getPimpl()->depthBuffer.assign(width * height, 1.0f); cpuAction.setRenderTarget(cpu.get());
    }
    if (gpu) {
      native.reset(CoinRenderTarget::createOffscreen(SbVec2i32(width, height)));
      if (native) gpuAction.setRenderTarget(native.get());
    }
    cpuAction.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    gpuAction.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    cpuAction.setTransparencyType(CoinRenderAction::BLEND);
    gpuAction.setTransparencyType(CoinRenderAction::BLEND);
#if COIN_HAVE_LEGACY_GL_RENDERER
    gl.setComponents(SoOffscreenRenderer::RGB); gl.setBackgroundColor(SbColor(0, 0, 0));
    gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
#endif
  }
  ~Harness() { cpuAction.setRenderTarget(nullptr); gpuAction.setRenderTarget(nullptr); }
  void fast(bool value) { cpuAction.setFastPathEnabled(value); gpuAction.setFastPathEnabled(value); }
  void transparency(CoinRenderAction::TransparencyType mode) {
    coinMode=mode;cpuAction.setTransparencyType(mode);gpuAction.setTransparencyType(mode);
#if COIN_HAVE_LEGACY_GL_RENDERER
    gl.getGLRenderAction()->setTransparencyType(static_cast<SoGLRenderAction::TransparencyType>(mode));
#endif
  }
  bool compare(const std::vector<uint8_t> & actual, const std::string & label) {
    if (!check(actual.size() == reference.size() && !actual.empty(), label + ": incomplete comparison")) return false;
    size_t active = 0, bad = 0; uint64_t error = 0; int maximum = 0;
    for (size_t i = 0; i < actual.size(); i += 4) {
      if (!(actual[i] > 3 || actual[i + 1] > 3 || actual[i + 2] > 3 ||
            reference[i] > 3 || reference[i + 1] > 3 || reference[i + 2] > 3)) continue;
      ++active; bool different = false;
      for (int channel = 0; channel < 3; ++channel) {
        const int delta = std::abs(int(actual[i + channel]) - int(reference[i + channel]));
        error += delta; maximum = std::max(maximum, delta); different |= delta > 3;
      }
      bad += different;
    }
    const double mae = active ? double(error) / (active * 3) : 0;
    std::cout << label << " oracle=CoinGL reference_pixels=" << colored(reference)
              << " actual_pixels=" << colored(actual) << " roi_mae=" << mae
              << " max=" << maximum << " pixels_over3=" << bad << '\n';
    return check(mae <= 1 && bad <= std::max<size_t>(4, active / 50), label + ": differs from mandatory CoinGL oracle");
  }
  bool render(SoNode * root, const std::string & label, bool visible = true) {
    if (!check(cpu && observer && (!gpu || native), label + ": target unavailable")) return false;
    const size_t previous = observer->submissions;
    cpuAction.apply(root); cpu->readbackRGBA(lastCpu);
    if (!check(cpuAction.getLastStatus() == CoinRenderAction::SUCCESS && observer->submissions == previous + 1 &&
      lastCpu.size() == size_t(width * height * 4), label + ": CPU capture/submission failed: " +
      cpuAction.getLastError().getString())) return false;
    if (!check(visible ? colored(lastCpu) >= 8 : colored(lastCpu) == 0, label + ": CPU visibility control failed")) return false;
    if (!gpu) return true;
#if COIN_HAVE_LEGACY_GL_RENDERER
    if (!check(gl.render(root) && gl.getBuffer() &&
        int(gl.getGLRenderAction()->getTransparencyType()) == int(coinMode),
        label + ": mandatory CoinGL oracle unavailable or transparency changed")) return false;
    reference.resize(width * height * 4); const uint8_t * source = gl.getBuffer();
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
      const size_t dst = size_t(y * width + x) * 4, src = size_t((height - 1 - y) * width + x) * 3;
      for (int channel = 0; channel < 3; ++channel) reference[dst + channel] = source[src + channel];
      reference[dst + 3] = 255;
    }
#else
    (void)root; return check(false, label + ": mandatory CoinGL oracle not compiled");
#endif
    if (!check(visible ? colored(reference) >= 8 : colored(reference) == 0,
               label + ": CoinGL visibility control failed")) return false;
    gpuAction.apply(root); native->readbackRGBA(lastNative);
    if (!check(gpuAction.getLastStatus() == CoinRenderAction::SUCCESS && lastNative.size() == reference.size() &&
      (visible ? colored(lastNative) >= 8 : colored(lastNative) == 0), label + ": real GPU execution failed: " +
      gpuAction.getLastError().getString())) return false;
    ++nativeGlComparisons;
    return compare(lastNative, label + "/GPU") && compare(lastCpu, label + "/CPU");
  }
  bool nativeVisibilityProbe(SoNode * root,const std::string & label,bool visible=true) {
    if(!gpu)return true;
#if COIN_HAVE_LEGACY_GL_RENDERER
    if(!check(gl.render(root) && gl.getBuffer(),label+": native visibility probe unavailable"))return false;
    size_t active=0;const uint8_t * source=gl.getBuffer();
    for(size_t i=0;i<size_t(width*height)*3;i+=3)active+=source[i]>3 || source[i+1]>3 || source[i+2]>3;
    std::cout<<label<<" native_visibility_probe_pixels="<<active<<'\n';
    return check(visible?active>=8:active==0,label+": native visibility expectation was not confirmed");
#else
    (void)root;(void)visible;return check(false,label+": native visibility probe not compiled");
#endif
  }
  bool renderGpuOnly(SoNode * root, const std::string & label) {
    if (!check(gpu && native, label + ": real GPU target required")) return false;
#if COIN_HAVE_LEGACY_GL_RENDERER
    if (!check(gl.render(root) && gl.getBuffer(), label + ": mandatory native CoinGL reference unavailable")) return false;
    reference.resize(width * height * 4); const uint8_t * source = gl.getBuffer();
    for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
      const size_t dst=size_t(y*width+x)*4,src=size_t((height-1-y)*width+x)*3;
      for(int c=0;c<3;++c)reference[dst+c]=source[src+c];reference[dst+3]=255;
    }
#else
    (void)root; return check(false,label + ": native CoinGL reference not compiled");
#endif
    gpuAction.apply(root);native->readbackRGBA(lastNative);
    if (!check(gpuAction.getLastStatus()==CoinRenderAction::SUCCESS && lastNative.size()==reference.size() &&
        colored(lastNative)>=8 && colored(reference)>=8,label+": GPU submission or nonvacuous reference failed: "+
        gpuAction.getLastError().getString()))return false;
    ++nativeGlComparisons;
    return compare(lastNative,label+"/GPU");
  }
  bool reject(SoNode * root,const std::string & label,
              CoinRenderAction::Status expected=CoinRenderAction::UNSUPPORTED) {
    const auto beforeCpu=lastCpu;const size_t submissions=observer->submissions;
    const uint64_t cpuSerial=cpu->getLastSubmissionSerial();std::vector<uint8_t> current;
    cpuAction.apply(root);cpu->readbackRGBA(current);
    if (!check(cpuAction.getLastStatus()==expected && cpuAction.getLastError().getLength() &&
        observer->submissions==submissions && cpu->getLastSubmissionSerial()==cpuSerial && current==beforeCpu,
        label+": rejected CPU capture changed published pixels or serial"))return false;
    if(gpu) {
      const auto beforeGpu=lastNative;const uint64_t gpuSerial=native->getLastSubmissionSerial();
      gpuAction.apply(root);native->readbackRGBA(current);
      if(!check(gpuAction.getLastStatus()==expected && gpuAction.getLastError().getLength() &&
          native->getLastSubmissionSerial()==gpuSerial && current==beforeGpu,
          label+": rejected GPU capture changed published pixels or serial"))return false;
    }
    return true;
  }
  bool gpu;
  CoinRenderAction::TransparencyType coinMode=CoinRenderAction::BLEND;
  std::unique_ptr<CoinRenderTarget> cpu, native;
  CaptureBackend * observer = nullptr;
  CoinRenderAction cpuAction, gpuAction;
#if COIN_HAVE_LEGACY_GL_RENDERER
  SoOffscreenRenderer gl;
#endif
  std::vector<uint8_t> lastCpu, lastNative, reference;
};
struct Scene {
  Scene() {
    root = new SoSeparator; root->ref();
    auto * camera = new SoOrthographicCamera; camera->height = 2;
    camera->position.setValue(0, 0, 5); camera->nearDistance = 1; camera->farDistance = 20;
    root->addChild(camera);
    lighting = new SoLightModel; lighting->model = SoLightModel::BASE_COLOR; root->addChild(lighting);
    complexity = new SoComplexity; complexity->type = SoComplexity::BOUNDING_BOX;
    complexity->textureQuality = .5f; root->addChild(complexity);
    style = new SoDrawStyle; style->lineWidth = 1; style->pointSize = 5; root->addChild(style);
    material = new SoMaterial; material->diffuseColor.setValue(.8f, .3f, .6f); root->addChild(material);
    transform = new SoTransform; root->addChild(transform);
    content = new SoGroup; root->addChild(content);
    shape = new BoundingFixtureShape; content->addChild(shape);
  }
  ~Scene() { root->unref(); }
  SoSeparator * root = nullptr;
  SoComplexity * complexity = nullptr;
  SoDrawStyle * style = nullptr;
  SoMaterial * material = nullptr;
  SoLightModel * lighting = nullptr;
  SoTransform * transform = nullptr;
  SoGroup * content = nullptr;
  BoundingFixtureShape * shape = nullptr;
};
bool changed(const std::vector<uint8_t> & a, const std::vector<uint8_t> & b, const std::string & label) {
  size_t changes = 0;
  if (a.size() == b.size()) for (size_t i = 0; i < a.size(); i += 4)
    changes += std::abs(int(a[i]) - int(b[i])) > 3 || std::abs(int(a[i + 1]) - int(b[i + 1])) > 3 ||
               std::abs(int(a[i + 2]) - int(b[i + 2])) > 3;
  return check(changes >= 16, label + ": counterfactual is vacuous");
}
bool restored(Harness & test, const std::vector<uint8_t> & cpu, const std::vector<uint8_t> & native,
              const std::string & label) {
  return check(test.lastCpu == cpu && (!test.gpu || test.lastNative == native), label + ": A/B/A image not restored");
}
bool bounds(const CoinRenderFramePlan & frame, const SbVec3f & expectedMin, const SbVec3f & expectedMax,
            const std::string & label) {
  if (!check(!frame.vertices.empty() && !frame.draws.empty(), label + ": no captured geometry")) return false;
  SbBox3f actual;
  for (const auto & vertex : frame.vertices) actual.extendBy(SbVec3f(vertex.position));
  const SbVec3f lo = actual.getMin(), hi = actual.getMax();
  for (int c = 0; c < 3; ++c) if (!check(std::abs(lo[c] - expectedMin[c]) < 1e-5f &&
      std::abs(hi[c] - expectedMax[c]) < 1e-5f, label + ": cube bounds or computeBBox midpoint differ")) return false;
  return true;
}

bool typeAndReuse(bool gpu) {
  Scene scene; Harness test(gpu);
  // Non-centered bounds and a bogus computeBBox center exercise the native
  // midpoint contract independently of geometry generation.
  scene.shape->minimum.setValue(-.75f, -.35f, -.2f);
  scene.shape->maximum.setValue(.45f, .45f, .4f);
  if (!test.render(scene.root, "type/bounding-box-original") ||
      !bounds(test.observer->frame, scene.shape->minimum.getValue(), scene.shape->maximum.getValue(), "type")) return false;
  const auto firstCpu = test.lastCpu, firstNative = test.lastNative;
  const uint64_t revision = test.observer->frame.revision;
  if (!test.render(scene.root, "type/unchanged-reuse") || !restored(test, firstCpu, firstNative, "type/reuse") ||
      !check(test.observer->frame.revision == revision, "unchanged box capture revision changed")) return false;
  scene.complexity->type = SoComplexity::OBJECT_SPACE;
  if (!test.render(scene.root, "type/normal-triangle-control") || !changed(firstCpu, test.lastCpu, "type/CPU") ||
      (gpu && (!changed(firstNative, test.lastNative, "type/GPU") ||
               !changed(test.reference, firstNative, "type/CoinGL-control")))) return false;
  scene.complexity->type = SoComplexity::BOUNDING_BOX;
  if (!test.render(scene.root, "type/bounding-box-restored") || !restored(test, firstCpu, firstNative, "type")) return false;
  for (float value : {.1f, .9f}) {
    scene.complexity->value = value;
    if (!test.render(scene.root, "type/ignored-tessellation-value") || !restored(test, firstCpu, firstNative, "type/value")) return false;
  }
  const SbVec3f oldMax = scene.shape->maximum.getValue();
  scene.shape->maximum.setValue(.7f, .65f, .4f);
  if (!test.render(scene.root, "type/bounds-mutation") || !changed(firstCpu, test.lastCpu, "type/bounds")) return false;
  scene.shape->maximum = oldMax;
  return test.render(scene.root, "type/bounds-restored") && restored(test, firstCpu, firstNative, "type/bounds");
}

bool nativeShapeKinds(bool gpu) {
  for (bool fast : {false, true}) {
    Scene scene; Harness test(gpu); test.fast(fast);
    scene.content->removeAllChildren();
    std::vector<SoNode *> shapes;
    auto * sphere = new SoSphere; sphere->radius = .6f; shapes.push_back(sphere);
    auto * cylinder = new SoCylinder; cylinder->radius = .5f; cylinder->height = .8f; shapes.push_back(cylinder);
    auto * cone = new SoCone; cone->bottomRadius = .6f; cone->height = .8f; shapes.push_back(cone);
    auto * indexed = new SoSeparator;
    auto * coordinates = new SoCoordinate3;
    const SbVec3f points[] = {{-.6f,-.4f,-.3f}, {.6f,-.4f,.3f}, {0,.4f,0}};
    coordinates->point.setValues(0, 3, points); indexed->addChild(coordinates);
    auto * faces = new SoIndexedFaceSet; const int32_t indices[] = {0,1,2,-1};
    faces->coordIndex.setValues(0,4,indices); indexed->addChild(faces); shapes.push_back(indexed);
    auto * line = new SoSeparator; line->addChild(coordinates);
    auto * lines = new SoLineSet; lines->numVertices = 3; line->addChild(lines); shapes.push_back(line);
    auto * point = new SoSeparator; point->addChild(coordinates);
    auto * pointsShape = new SoPointSet; pointsShape->numPoints = 3; point->addChild(pointsShape); shapes.push_back(point);
    // Each node is held while moving it through the same content Group.
    for (SoNode * shape : shapes) shape->ref();
    for (size_t i = 0; i < shapes.size(); ++i) {
      scene.content->removeAllChildren(); scene.content->addChild(shapes[i]);
      const std::string label = std::string("shape/") + (fast ? "fast/" : "callback/") + std::to_string(i);
      if (!test.render(scene.root, label)) { for (SoNode * shape : shapes) shape->unref(); return false; }
      const auto previousCpu = test.lastCpu, previousNative = test.lastNative;
      test.fast(!fast);
      if (!test.render(scene.root, label + "/switch-path") ||
          !restored(test, previousCpu, previousNative, label + "/paths")) {
        for (SoNode * shape : shapes) shape->unref(); return false;
      }
      test.fast(fast);
    }
    for (SoNode * shape : shapes) shape->unref();
  }
  return true;
}

bool transformationsAndStyles(bool gpu) {
  Scene scene; Harness test(gpu);
  if (!test.render(scene.root, "transform/identity")) return false;
  const auto originalCpu = test.lastCpu, originalNative = test.lastNative;
  scene.transform->translation.setValue(.1f, -.075f, 0);
  scene.transform->rotation.setValue(SbVec3f(1, 1, .5f), .31f);
  scene.transform->scaleFactor.setValue(.9f, .8f, 1.1f);
  if (!test.render(scene.root, "transform/model-change") || !changed(originalCpu, test.lastCpu, "transform")) return false;
  scene.transform->translation.setValue(0, 0, 0); scene.transform->rotation.setValue(SbRotation::identity());
  scene.transform->scaleFactor.setValue(1, 1, 1);
  if (!test.render(scene.root, "transform/model-restored") || !restored(test, originalCpu, originalNative, "transform")) return false;
  // Pixel-center endpoints make point and line tests independent of an
  // ambiguous integer endpoint, while retaining the unchanged RGB gate.
  for (int style : {SoDrawStyle::FILLED, SoDrawStyle::LINES, SoDrawStyle::POINTS, SoDrawStyle::INVISIBLE}) {
    if(style==SoDrawStyle::FILLED) {
      scene.shape->minimum.setValue(-.6f,-.4f,-.3f);scene.shape->maximum.setValue(.6f,.4f,.3f);
    } else {
      scene.shape->minimum.setValue(-.4875f,-.4875f,-.25f);scene.shape->maximum.setValue(.5125f,.5125f,.25f);
    }
    scene.style->style = style;
    if (!test.render(scene.root, "style/" + std::to_string(style), style != SoDrawStyle::INVISIBLE)) return false;
    if (style == SoDrawStyle::INVISIBLE && !check(test.observer->frame.draws.empty() &&
         test.observer->frame.vertices.empty(), "INVISIBLE box allocated geometry")) return false;
  }
  scene.style->style = SoDrawStyle::FILLED;
  scene.shape->minimum.setValue(-.6f,-.4f,-.3f);scene.shape->maximum.setValue(.6f,.4f,.3f);
  return test.render(scene.root, "style/filled-restored");
}

bool materialAndTexture(bool gpu) {
  Scene scene; Harness test(gpu);
  if (!test.render(scene.root, "material/first-slot")) return false;
  const auto originalCpu = test.lastCpu, originalNative = test.lastNative;
  const SbColor colors[] = {{.8f,.3f,.6f},{0,1,0},{0,0,1}};
  scene.material->diffuseColor.setValues(0,3,colors);
  auto * binding = new SoMaterialBinding; binding->value = SoMaterialBinding::PER_FACE_INDEXED;
  scene.root->insertChild(binding, scene.root->findChild(scene.transform));
  auto * normals = new SoNormal; const SbVec3f wrong[] = {{1,0,0},{0,-1,0},{0,0,-1}};
  normals->vector.setValues(0,3,wrong); scene.root->insertChild(normals, scene.root->findChild(scene.transform));
  auto * normalBinding = new SoNormalBinding; normalBinding->value = SoNormalBinding::PER_VERTEX_INDEXED;
  scene.root->insertChild(normalBinding, scene.root->findChild(scene.transform));
  if (!test.render(scene.root, "material/per-face-binding-ignored") ||
      !restored(test, originalCpu, originalNative, "material/binding")) return false;
  scene.material->diffuseColor.set1Value(0,SbColor(.2f,.7f,.4f));
  if (!test.render(scene.root, "material/first-slot-mutation") || !changed(originalCpu,test.lastCpu,"material/color")) return false;
  scene.material->diffuseColor.set1Value(0,colors[0]);
  if (!test.render(scene.root, "material/first-slot-restored") || !restored(test,originalCpu,originalNative,"material/color")) return false;
  scene.material->transparency = .35f;
  if (!test.render(scene.root, "material/transparency")) return false;
  const auto alphaCpu = test.lastCpu, alphaNative = test.lastNative;
  scene.material->transparency = .6f;
  if (!test.render(scene.root, "material/transparency-mutated") || !changed(alphaCpu,test.lastCpu,"material/alpha")) return false;
  scene.material->transparency = .35f;
  if (!test.render(scene.root, "material/transparency-restored") || !restored(test,alphaCpu,alphaNative,"material/alpha")) return false;
  scene.material->transparency = 0;
  auto * hints = new SoShapeHints; hints->vertexOrdering = SoShapeHints::CLOCKWISE;
  hints->shapeType = SoShapeHints::SOLID;
  scene.root->insertChild(hints, scene.root->findChild(scene.transform));
  if (!test.render(scene.root, "material/forced-box-shape-hints") ||
      !restored(test,originalCpu,originalNative,"material/shape-hints")) return false;
  auto * image = new SoTexture2; image->model = SoTexture2::REPLACE;
  image->wrapS = SoTexture2::CLAMP; image->wrapT = SoTexture2::CLAMP;
  std::vector<uint8_t> pixels(16*16*3);
  for (int y=0;y<16;++y) for(int x=0;x<16;++x) {
    const size_t o=size_t(y*16+x)*3; pixels[o]=32+12*x;pixels[o+1]=40+10*y;pixels[o+2]=96+4*x+3*y;
  }
  image->image.setValue(SbVec2s(16,16),3,pixels.data());
  scene.root->insertChild(image,scene.root->findChild(scene.transform));
  if (!test.render(scene.root,"texture/default-canonical-no-explicit-uv")) return false;
  const auto defaultCpu=test.lastCpu,defaultNative=test.lastNative;
  auto * uv = new SoTextureCoordinate2; const SbVec2f zeros[]={{0,0},{0,0},{0,0}};
  uv->point.setValues(0,3,zeros); scene.root->insertChild(uv,scene.root->findChild(scene.transform));
  auto * matrix = new SoTextureMatrixTransform;
  scene.root->insertChild(matrix,scene.root->findChild(scene.transform));
  if (!test.render(scene.root,"texture/canonical-cube-uv")) return false;
  const auto texturedCpu=test.lastCpu,texturedNative=test.lastNative;
  if(!restored(test,defaultCpu,defaultNative,"texture/default-vs-explicit"))return false;
  const SbVec2f random[]={{.9f,.1f},{.2f,.8f},{.6f,.6f}};uv->point.setValues(0,3,random);
  if (!test.render(scene.root,"texture/explicit-uv-ignored") ||
      !restored(test,texturedCpu,texturedNative,"texture/explicit-uv")) return false;
  SbMatrix changedMatrix=SbMatrix::identity();changedMatrix[0][0]=.65f;changedMatrix[1][1]=.7f;
  changedMatrix[3][0]=.14f;changedMatrix[3][1]=.12f;matrix->matrix=changedMatrix;
  if (!test.render(scene.root,"texture/matrix-mutation") || !changed(texturedCpu,test.lastCpu,"texture/matrix")) return false;
  matrix->matrix=SbMatrix::identity();
  if (!test.render(scene.root,"texture/matrix-restored") || !restored(test,texturedCpu,texturedNative,"texture/matrix")) return false;
  scene.complexity->textureQuality=0;
  if (!test.render(scene.root,"texture/quality-zero-disabled") ||
      !restored(test,originalCpu,originalNative,"texture/quality-zero")) return false;
  scene.complexity->textureQuality=.5f;
  return test.render(scene.root,"texture/quality-restored") && restored(test,texturedCpu,texturedNative,"texture/quality");
}

bool scopeAndOverride(bool gpu) {
  Scene scene; Harness test(gpu);
  scene.content->removeAllChildren(); scene.complexity->type = SoComplexity::OBJECT_SPACE;
  auto * left = new SoSeparator;
  auto * box = new SoComplexity;box->type=SoComplexity::BOUNDING_BOX;left->addChild(box);
  auto * leftTransform = new SoTransform;leftTransform->translation.setValue(-.55f,0,0);
  leftTransform->scaleFactor.setValue(.55f,.75f,1);left->addChild(leftTransform);
  auto * leftShape=new BoundingFixtureShape;left->addChild(leftShape);scene.content->addChild(left);
  auto * right=new SoSeparator;auto * rightTransform=new SoTransform;
  rightTransform->translation.setValue(.55f,0,0);rightTransform->scaleFactor.setValue(.55f,.75f,1);
  right->addChild(rightTransform);right->addChild(new BoundingFixtureShape);scene.content->addChild(right);
  if (!test.render(scene.root,"scope/box-left-triangle-right")) return false;
  const auto originalCpu=test.lastCpu,originalNative=test.lastNative;
  box->type=SoComplexity::OBJECT_SPACE;
  if (!test.render(scene.root,"scope/left-normal-mutation") || !changed(originalCpu,test.lastCpu,"scope/left")) return false;
  box->type=SoComplexity::BOUNDING_BOX;
  if (!test.render(scene.root,"scope/left-box-restored") || !restored(test,originalCpu,originalNative,"scope")) return false;
  scene.complexity->type=SoComplexity::BOUNDING_BOX;scene.complexity->setOverride(TRUE);
  box->type=SoComplexity::OBJECT_SPACE;
  if (!test.render(scene.root,"scope/bounding-override")) return false;
  const auto overrideCpu=test.lastCpu,overrideNative=test.lastNative;
  box->type=SoComplexity::SCREEN_SPACE;
  if (!test.render(scene.root,"scope/overridden-type-mutated") ||
      !restored(test,overrideCpu,overrideNative,"scope/override")) return false;
  scene.complexity->setOverride(FALSE);scene.complexity->type=SoComplexity::OBJECT_SPACE;
  box->type=SoComplexity::BOUNDING_BOX;
  if (!test.render(scene.root,"scope/override-removed") || !restored(test,originalCpu,originalNative,"scope")) return false;
  box->type.setIgnored(TRUE);
  if (!test.render(scene.root,"scope/ignored-box-type") || !changed(originalCpu,test.lastCpu,"scope/ignored")) return false;
  box->type.setIgnored(FALSE);
  return test.render(scene.root,"scope/ignored-type-restored") && restored(test,originalCpu,originalNative,"scope/ignored");
}

struct Observer { size_t pre=0,post=0,triangles=0;int mode=0; };
SoCallbackAction::Response observePre(void * data,SoCallbackAction *,const SoNode * node) {
  auto * observer=static_cast<Observer *>(data);++observer->pre;
  if(observer->mode==1)return SoCallbackAction::PRUNE;
  if(observer->mode==2)static_cast<BoundingFixtureShape *>(const_cast<SoNode *>(node))->maximum.setValue(.7f,.6f,.3f);
  return SoCallbackAction::CONTINUE;
}
SoCallbackAction::Response observePost(void * data,SoCallbackAction *,const SoNode *) {
  ++static_cast<Observer *>(data)->post;return SoCallbackAction::CONTINUE;
}
void observeTriangle(void * data,SoCallbackAction *,const SoPrimitiveVertex *,const SoPrimitiveVertex *,const SoPrimitiveVertex *) {
  ++static_cast<Observer *>(data)->triangles;
}
bool callbacks(bool gpu) {
  Scene scene;Observer cpu,gpuObserver;Harness test(gpu);
  const SoType type=BoundingFixtureShape::getClassTypeId();
  test.cpuAction.addPreCallback(type,observePre,&cpu);test.cpuAction.addPostCallback(type,observePost,&cpu);
  test.cpuAction.addTriangleCallback(type,observeTriangle,&cpu);
  test.gpuAction.addPreCallback(type,observePre,&gpuObserver);test.gpuAction.addPostCallback(type,observePost,&gpuObserver);
  test.gpuAction.addTriangleCallback(type,observeTriangle,&gpuObserver);
  if (!test.render(scene.root,"callback/original-primitives-observed") ||
      !check(cpu.pre==1 && cpu.post==1 && cpu.triangles==1 &&
       (!gpu || (gpuObserver.pre==1 && gpuObserver.post==1 && gpuObserver.triangles==1)),
       "BOUNDING_BOX changed user callback count or leaked synthesized cube primitives")) return false;
  const auto originalCpu=test.lastCpu,originalNative=test.lastNative;
  if (!test.render(scene.root,"callback/reuse-observers") ||
      !check(cpu.pre==2 && cpu.post==2 && cpu.triangles==2 &&
       (!gpu || (gpuObserver.pre==2 && gpuObserver.post==2 && gpuObserver.triangles==2)),
       "unchanged BOUNDING_BOX reuse skipped observers")) return false;
  cpu.mode=gpuObserver.mode=1;
  // Native CoinGL has no callback PRUNE. This gate validates empty capture
  // and real GPU execution without claiming a native comparison for PRUNE.
  test.cpuAction.apply(scene.root);test.cpu->readbackRGBA(test.lastCpu);
  if (!check(test.cpuAction.getLastStatus()==CoinRenderAction::SUCCESS && colored(test.lastCpu)==0 &&
      test.observer->frame.draws.empty() && cpu.triangles==2,"PRUNE captured a bounding box")) return false;
  if (gpu) {
    test.gpuAction.apply(scene.root);test.native->readbackRGBA(test.lastNative);
    if (!check(test.gpuAction.getLastStatus()==CoinRenderAction::SUCCESS && colored(test.lastNative)==0 &&
       gpuObserver.triangles==2,"GPU PRUNE captured a bounding box")) return false;
  }
  cpu.mode=gpuObserver.mode=2;
  if (!test.render(scene.root,"callback/continue-bounds-mutation") ||
      !changed(originalCpu,test.lastCpu,"callback/mutation") ||
      !bounds(test.observer->frame,scene.shape->minimum.getValue(),scene.shape->maximum.getValue(),"callback/mutation")) return false;
  cpu.mode=gpuObserver.mode=0;scene.shape->maximum.setValue(.6f,.4f,.3f);
  return test.render(scene.root,"callback/bounds-restored") && restored(test,originalCpu,originalNative,"callback");
}

bool degenerates(bool gpu) {
  Scene scene;Harness test(gpu);
  if (!test.render(scene.root,"degenerate/anchor")) return false;
  const auto originalCpu=test.lastCpu,originalNative=test.lastNative;
  scene.shape->minimum.setValue(-.6f,-.4f,0);scene.shape->maximum.setValue(.6f,.4f,0);
  if (!test.render(scene.root,"degenerate/planar-box")) return false;
  scene.shape->minimum.setValue(-.4875f,-.0125f,0);scene.shape->maximum.setValue(.5125f,-.0125f,0);
  scene.style->style=SoDrawStyle::LINES;
  if (!test.nativeVisibilityProbe(scene.root,"degenerate/line-box/native-control") ||
      !test.render(scene.root,"degenerate/line-box")) return false;
  scene.shape->minimum.setValue(.0125f,-.0125f,0);scene.shape->maximum=scene.shape->minimum.getValue();
  scene.style->style=SoDrawStyle::POINTS;
  if (!test.nativeVisibilityProbe(scene.root,"degenerate/point-box/native-control") ||
      !test.render(scene.root,"degenerate/point-box")) return false;
  scene.shape->empty=TRUE;scene.style->style=SoDrawStyle::FILLED;
  if (!test.nativeVisibilityProbe(scene.root,"degenerate/empty-box/native-control",false) ||
      !test.render(scene.root,"degenerate/empty-box",false) ||
      !check(test.observer->frame.draws.empty() && test.observer->frame.vertices.empty(),"empty bbox allocated geometry")) return false;
  scene.shape->empty=FALSE;scene.shape->minimum.setValue(-.6f,-.4f,-.3f);scene.shape->maximum.setValue(.6f,.4f,.3f);
  return test.render(scene.root,"degenerate/restored") && restored(test,originalCpu,originalNative,"degenerate");
}

bool faceNormals(bool gpu) {
  Scene scene;Harness test(gpu);scene.lighting->model=SoLightModel::PHONG;
  auto * environment=new SoEnvironment;environment->ambientIntensity=.2f;
  scene.root->insertChild(environment,scene.root->findChild(scene.complexity));
  auto * light=new SoDirectionalLight;light->direction.setValue(-.2f,-.3f,-1);
  scene.root->insertChild(light,scene.root->findChild(scene.complexity));
  scene.material->ambientColor.setValue(.15f,.15f,.15f);scene.material->specularColor.setValue(0,0,0);
  scene.material->emissiveColor.setValue(.03f,.02f,.01f);
  if (!test.render(scene.root,"normal/phong-front")) return false;
  const auto originalCpu=test.lastCpu,originalNative=test.lastNative;
  scene.transform->rotation.setValue(SbVec3f(0,1,0),.41f);
  if (!test.render(scene.root,"normal/phong-model-rotation") || !changed(originalCpu,test.lastCpu,"normal/rotation")) return false;
  scene.transform->rotation.setValue(SbRotation::identity());
  return test.render(scene.root,"normal/phong-restored") && restored(test,originalCpu,originalNative,"normal");
}

bool unsupportedTextures(bool gpu) {
  Scene scene;Harness test(gpu);
  auto * image=new SoTexture2;const uint8_t white[]={190,210,230};
  image->image.setValue(SbVec2s(1,1),3,white);image->model=SoTexture2::REPLACE;
  scene.root->insertChild(image,scene.root->findChild(scene.transform));
  auto * defaults=new SoTextureCoordinateDefault;
  scene.root->insertChild(defaults,scene.root->findChild(scene.transform));
  if(!test.render(scene.root,"admission/default-primary-image"))return false;
  const auto originalCpu=test.lastCpu,originalNative=test.lastNative;
  auto * function=new SoTextureCoordinatePlane;
  scene.root->insertChild(function,scene.root->findChild(scene.transform));
  if(!test.reject(scene.root,"admission/function-coordinate"))return false;
  scene.root->removeChild(function);
  if(!test.render(scene.root,"admission/function-repaired") ||
     !restored(test,originalCpu,originalNative,"admission/function"))return false;
  auto * unit=new SoTextureUnit;unit->unit=1;
  auto * second=new SoTexture2;const uint8_t cyan[]={0,200,230};
  second->image.setValue(SbVec2s(1,1),3,cyan);
  // A plain Group is needed so the secondary image remains active for shape.
  auto * inherited=new SoGroup;inherited->addChild(unit);inherited->addChild(second);
  auto * primary=new SoTextureUnit;primary->unit=0;inherited->addChild(primary);
  scene.root->insertChild(inherited,scene.root->findChild(scene.transform));
  if(!test.reject(scene.root,"admission/secondary-active-image"))return false;
  scene.root->removeChild(inherited);
  return test.render(scene.root,"admission/secondary-repaired") &&
    restored(test,originalCpu,originalNative,"admission/secondary");
}

bool vertexPropertyScope(bool gpu) {
  // Native VertexShape renderers apply VP before shouldGLRender, except
  // PointSet. Its bbox reads VP positions but uses the inherited material.
  for(int kind=0;kind<3;++kind) {
    Scene scene;Harness test(gpu);scene.content->removeAllChildren();
    auto * positions=new SoCoordinate3;const SbVec3f wrong[]={{-.9f,-.8f,0},{-.8f,-.8f,0},{-.85f,-.7f,0}};
    positions->point.setValues(0,3,wrong);scene.content->addChild(positions);
    auto * property=new SoVertexProperty;
    const SbVec3f actual[]={{-.6f,-.4f,-.3f},{.6f,-.4f,.3f},{0,.4f,0}};
    property->vertex.setValues(0,3,actual);
    property->orderedRGBA.set1Value(0,0x33cc66ffu);property->materialBinding=SoVertexProperty::OVERALL;
    SoVertexShape * selected=nullptr;
    if(kind==0) {auto * shape=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,-1};
      shape->coordIndex.setValues(0,4,indices);selected=shape;}
    else if(kind==1) {auto * shape=new SoLineSet;shape->numVertices=3;selected=shape;}
    else {auto * shape=new SoPointSet;shape->numPoints=3;selected=shape;}
    selected->vertexProperty=property;scene.content->addChild(selected);
    const std::string label="vertex-property/"+std::to_string(kind);
    if(!test.render(scene.root,label) || !bounds(test.observer->frame,SbVec3f(-.6f,-.4f,-.3f),
        SbVec3f(.6f,.4f,.3f),label))return false;
    const auto originalCpu=test.lastCpu,originalNative=test.lastNative;
    property->orderedRGBA.set1Value(0,0xcc6633ffu);
    if(!test.render(scene.root,label+"/material-mutated"))return false;
    if(kind==2) {if(!restored(test,originalCpu,originalNative,label+"/inherited-material"))return false;}
    else if(!changed(originalCpu,test.lastCpu,label+"/applied-material"))return false;
    property->orderedRGBA.set1Value(0,0x33cc66ffu);
    if(!test.render(scene.root,label+"/material-restored") ||
       !restored(test,originalCpu,originalNative,label+"/material"))return false;
    property->vertex.set1Value(2,SbVec3f(0,.6f,0));
    if(!test.render(scene.root,label+"/positions-mutated") || !changed(originalCpu,test.lastCpu,label+"/positions"))return false;
    property->vertex.set1Value(2,actual[2]);
    if(!test.render(scene.root,label+"/positions-restored") ||
       !restored(test,originalCpu,originalNative,label+"/positions"))return false;
  }
  return true;
}


SoImage * screenImage() {
  auto * image=new SoImage;std::vector<uint8_t> pixels(16*12*3);
  for(int y=0;y<12;++y)for(int x=0;x<16;++x) {
    const size_t offset=size_t(y*16+x)*3;pixels[offset]=30+10*x;
    pixels[offset+1]=40+14*y;pixels[offset+2]=180;
  }
  image->image.setValue(SbVec2s(16,12),3,pixels.data());return image;
}
bool specialDispatch(bool gpu) {
  for(int kind=0;kind<4;++kind) {
    Scene scene;Harness test(gpu);scene.content->removeAllChildren();
    scene.complexity->type=SoComplexity::OBJECT_SPACE;
    auto * isolated=new SoSeparator;
    auto * local=new SoComplexity;local->type=SoComplexity::BOUNDING_BOX;isolated->addChild(local);
    auto * move=new SoTransform;move->translation.setValue(-.2f,-.1f,0);isolated->addChild(move);
    if(kind==0)isolated->addChild(screenImage());
    else if(kind==1) {
      auto * font=new SoFont;font->name="defaultFont";font->size=16;isolated->addChild(font);
      auto * text=new SoText2;text->string.setValue("BOX");isolated->addChild(text);
    } else {
      auto * coords=new SoCoordinate3;
      const SbVec3f points[]={{-.35f,-.25f,0},{.45f,.35f,0},{.05f,.1f,0}};
      coords->point.setValues(0,3,points);isolated->addChild(coords);
      if(kind==2) {
        auto * markers=new SoMarkerSet;markers->numPoints=3;
        markers->markerIndex=SoMarkerSet::CROSS_9_9;isolated->addChild(markers);
      } else {
        auto * markers=new SoIndexedMarkerSet;const int32_t order[]={0,1,2};
        const int32_t ids[]={SoMarkerSet::CROSS_9_9,SoMarkerSet::CROSS_9_9,SoMarkerSet::CROSS_9_9};
        markers->coordIndex.setValues(0,3,order);markers->markerIndex.setValues(0,3,ids);isolated->addChild(markers);
      }
    }
    scene.content->addChild(isolated);
    // A normal sibling certifies that the special node and its local bbox
    // complexity do not leave rendering state changed after their scope.
    auto * sibling=new SoSeparator;auto * siblingMove=new SoTransform;
    siblingMove->translation.setValue(.7f,-.6f,0);siblingMove->scaleFactor.setValue(.25f,.25f,.25f);
    sibling->addChild(siblingMove);sibling->addChild(new BoundingFixtureShape);scene.content->addChild(sibling);
    const std::string label="special-dispatch/"+std::to_string(kind);
    if(!test.render(scene.root,label+"/bounding-with-normal-sibling"))return false;
    const auto originalCpu=test.lastCpu,originalNative=test.lastNative;
    local->type=SoComplexity::OBJECT_SPACE;
    if(!test.render(scene.root,label+"/original-raster-control") ||
       !changed(originalCpu,test.lastCpu,label+"/control"))return false;
    local->type=SoComplexity::BOUNDING_BOX;
    if(!test.render(scene.root,label+"/bounding-restored") ||
       !restored(test,originalCpu,originalNative,label))return false;
    if(kind>=2) {
      // MarkerSet changes light model and disables texturing BEFORE bbox;
      // IndexedMarkerSet makes those changes AFTER bbox. Native comparison
      // therefore distinguishes inherited texture/light state without
      // admitting an unsupported textured raster Image or Text2 profile.
      scene.lighting->model=SoLightModel::PHONG;
      auto * environment=new SoEnvironment;environment->ambientIntensity=.25f;
      scene.root->insertChild(environment,scene.root->findChild(scene.complexity));
      auto * light=new SoDirectionalLight;light->direction.setValue(0,0,-1);
      scene.root->insertChild(light,scene.root->findChild(scene.complexity));
      scene.material->ambientColor.setValue(.8f,.7f,.6f);scene.material->specularColor.setValue(0,0,0);
      auto * texture=new SoTexture2;const uint8_t cyan[]={30,180,220};
      texture->image.setValue(SbVec2s(1,1),3,cyan);texture->model=SoTexture2::MODULATE;
      isolated->insertChild(texture,1);
      if(!test.render(scene.root,label+"/inherited-phong-texture"))return false;
      const auto inheritedCpu=test.lastCpu,inheritedNative=test.lastNative;
      const uint8_t yellow[]={200,190,30};texture->image.setValue(SbVec2s(1,1),3,yellow);
      if(!test.render(scene.root,label+"/inherited-texture-mutated"))return false;
      if(kind==2) {if(!restored(test,inheritedCpu,inheritedNative,label+"/MarkerSet-disables-texture"))return false;}
      else if(!changed(inheritedCpu,test.lastCpu,label+"/IndexedMarkerSet-retains-texture"))return false;
      texture->image.setValue(SbVec2s(1,1),3,cyan);
      if(!test.render(scene.root,label+"/inherited-texture-restored") ||
         !restored(test,inheritedCpu,inheritedNative,label+"/inherited"))return false;
    }
  }
  return true;
}
struct ComplexityHook { SoComplexityTypeElement::Type type=SoComplexityTypeElement::OBJECT_SPACE; };
SoCallbackAction::Response changeComplexity(void * data,SoCallbackAction * action,const SoNode *) {
  SoComplexityTypeElement::set(action->getState(),static_cast<ComplexityHook *>(data)->type);
  return SoCallbackAction::CONTINUE;
}
bool specialCallbackDispatch(bool gpu) {
  Scene scene;ComplexityHook hook;Harness test(gpu);scene.content->removeAllChildren();
  scene.content->addChild(screenImage());scene.complexity->type=SoComplexity::OBJECT_SPACE;
  if(!test.render(scene.root,"special-callback/original-raster-oracle"))return false;
  const auto originalCpu=test.lastCpu,originalNative=test.lastNative,originalGl=test.reference;
  scene.complexity->type=SoComplexity::BOUNDING_BOX;
  if(!test.render(scene.root,"special-callback/bounding-oracle"))return false;
  const auto boxCpu=test.lastCpu,boxNative=test.lastNative,boxGl=test.reference;
  if(!changed(originalCpu,boxCpu,"special-callback/control"))return false;
  const SoType type=SoImage::getClassTypeId();
  test.cpuAction.addPreCallback(type,changeComplexity,&hook);test.gpuAction.addPreCallback(type,changeComplexity,&hook);
  auto execute=[&](const std::string & label,const std::vector<uint8_t> & cpu,
                   const std::vector<uint8_t> & native,const std::vector<uint8_t> & reference) {
    const size_t submissions=test.observer->submissions;
    test.cpuAction.apply(scene.root);test.cpu->readbackRGBA(test.lastCpu);
    if(!check(test.cpuAction.getLastStatus()==CoinRenderAction::SUCCESS &&
        test.observer->submissions==submissions+1 && test.lastCpu==cpu,
        label+": special dispatch ignored effective pre-callback complexity"))return false;
    if(gpu) {
      test.gpuAction.apply(scene.root);test.native->readbackRGBA(test.lastNative);
      if(!check(test.gpuAction.getLastStatus()==CoinRenderAction::SUCCESS && test.lastNative==native,
         label+": real GPU dispatch ignored effective pre-callback complexity"))return false;
      // The oracle is a saved actual CoinGL rendering of the equivalent
      // effective state; no callback exists in native GL traversal.
      test.reference=reference;++nativeGlComparisons;
      if(!test.compare(test.lastNative,label+"/GPU") || !test.compare(test.lastCpu,label+"/CPU"))return false;
    }
    return true;
  };
  scene.complexity->type=SoComplexity::OBJECT_SPACE;hook.type=SoComplexityTypeElement::BOUNDING_BOX;
  if(!execute("special-callback/object-to-bounding",boxCpu,boxNative,boxGl))return false;
  scene.complexity->type=SoComplexity::BOUNDING_BOX;hook.type=SoComplexityTypeElement::OBJECT_SPACE;
  if(!execute("special-callback/bounding-to-object",originalCpu,originalNative,originalGl))return false;
  scene.complexity->type=SoComplexity::OBJECT_SPACE;hook.type=SoComplexityTypeElement::BOUNDING_BOX;
  return execute("special-callback/object-to-bounding-restored",boxCpu,boxNative,boxGl);
}


bool boundingSortedTriangles(bool gpu) {
  Scene scene;Harness test(gpu);scene.lighting->model=SoLightModel::PHONG;
  scene.transform->translation.setValue(.075f,-.025f,0);
  scene.transform->rotation.setValue(SbVec3f(1,1,.2f),.41f);
  scene.material->transparency=.45f;scene.material->ambientColor.setValue(.3f,.2f,.4f);
  scene.material->specularColor.setValue(0,0,0);scene.material->emissiveColor.setValue(.04f,.01f,.03f);
  auto * environment=new SoEnvironment;environment->ambientIntensity=.25f;
  scene.root->insertChild(environment,scene.root->findChild(scene.complexity));
  auto * light=new SoDirectionalLight;light->direction.setValue(-.3f,.2f,-1);
  scene.root->insertChild(light,scene.root->findChild(scene.complexity));
  const CoinRenderAction::TransparencyType objects[]={CoinRenderAction::SORTED_OBJECT_ADD,CoinRenderAction::SORTED_OBJECT_BLEND};
  const CoinRenderAction::TransparencyType triangles[]={CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_ADD,
                                                      CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND};
  SbMatrix expected;
  expected.setTransform(scene.transform->translation.getValue(),scene.transform->rotation.getValue(),
    scene.transform->scaleFactor.getValue(),scene.transform->scaleOrientation.getValue(),scene.transform->center.getValue());
  for(int i=0;i<2;++i) {
    const std::string label="sorted-triangles/"+std::to_string(i);
    test.transparency(objects[i]);
    if(!test.render(scene.root,label+"/native-object-control"))return false;
    const auto objectCpu=test.lastCpu,objectNative=test.lastNative,objectGl=test.reference;
    test.transparency(triangles[i]);
    if(!test.render(scene.root,label+"/bounding-object-policy") ||
       !restored(test,objectCpu,objectNative,label) ||
       (gpu && !check(test.reference==objectGl,label+": native bbox differs between object and triangle-sort modes")))return false;
    if(!check(!test.observer->frame.renderStates.empty() &&
      std::all_of(test.observer->frame.renderStates.begin(),test.observer->frame.renderStates.end(),
        [&](const CoinRenderRenderStateSnapshot & state) {
          return state.transparencyType==int(objects[i]) && state.model==expected;
        }),label+": bbox snapshot sorted its faces or changed source model"))return false;
    test.transparency(objects[i]);
    if(!test.render(scene.root,label+"/object-mode-restored") ||
       !restored(test,objectCpu,objectNative,label+"/A-B-A"))return false;
  }
  return true;
}
bool markerBoundingAlphaClassification(bool gpu) {
  Scene scene;Harness test(gpu);scene.content->removeAllChildren();
  test.transparency(CoinRenderAction::SORTED_OBJECT_BLEND);
  auto * texture=new SoTexture2;const uint8_t translucent[]={30,180,220,96};
  texture->image.setValue(SbVec2s(1,1),4,translucent);
  scene.root->insertChild(texture,scene.root->findChild(scene.transform));
  const float transparency[]={0,.65f};scene.material->transparency.setValues(0,2,transparency);
  auto * coords=new SoCoordinate3;const SbVec3f points[]={{-.6f,-.4f,0},{.6f,.4f,0}};
  coords->point.setValues(0,2,points);scene.content->addChild(coords);
  auto * markers=new SoMarkerSet;markers->numPoints=2;markers->markerIndex=SoMarkerSet::CROSS_9_9;
  scene.content->addChild(markers);
  if(!test.render(scene.root,"marker-alpha/opaque-slot-zero-translucent-array-and-image"))return false;
  const auto originalCpu=test.lastCpu,originalNative=test.lastNative;
  if(!check(!test.observer->frame.renderStates.empty() &&
      std::all_of(test.observer->frame.renderStates.begin(),test.observer->frame.renderStates.end(),
        [](const CoinRenderRenderStateSnapshot & state) {
          return !state.hasTexture && !state.rasterPixels && state.transparentMaterial && state.transparentTexture &&
            state.transparencyType==int(CoinRenderAction::SORTED_OBJECT_BLEND);
        }),"MarkerSet bbox lost native array/image transparency scheduling when texturing was disabled"))return false;
  const uint8_t opaque[]={200,190,30,255};texture->image.setValue(SbVec2s(1,1),4,opaque);
  scene.material->transparency.set1Value(1,0);
  if(!test.render(scene.root,"marker-alpha/unused-slots-made-opaque") ||
     !restored(test,originalCpu,originalNative,"marker-alpha/opaque-slot-zero"))return false;
  texture->image.setValue(SbVec2s(1,1),4,translucent);scene.material->transparency.set1Value(1,.65f);
  return test.render(scene.root,"marker-alpha/scheduling-restored") &&
    restored(test,originalCpu,originalNative,"marker-alpha/A-B-A");
}

bool clippedContourMultiplicity(bool gpu) {
  for(int style : {SoDrawStyle::POINTS,SoDrawStyle::LINES}) {
    Scene scene;Harness test(gpu);scene.style->style=style;scene.style->pointSize=5;
    scene.material->transparency=.5f;
    const SbVec3f lo(-.4875f,-.4875f,-.25f),hi(.5125f,.5125f,.25f);
    scene.shape->minimum=lo;scene.shape->maximum=hi;
    auto * depth=new SoDepthBuffer;depth->test=TRUE;depth->write=FALSE;depth->function=SoDepthBuffer::LEQUAL;
    scene.root->insertChild(depth,scene.root->findChild(scene.transform));
    auto * clip=new SoClipPlane;
    // x+y=.025 passes through two opposite XY corners. Its three points
    // share the source corner values, so the test exercises an actual
    // endpoint on the plane rather than an approximate near-corner clip.
    clip->plane=SbPlane(SbVec3f(lo[0],hi[1],lo[2]),SbVec3f(lo[0],hi[1],hi[2]),
                        SbVec3f(hi[0],lo[1],lo[2]));
    scene.root->insertChild(clip,scene.root->findChild(scene.transform));
    auto packedMaterial=[&](const std::string & label) {
      const auto & frame=test.observer->frame;
      const float expected[]={204.0f/255.0f,77.0f/255.0f,153.0f/255.0f,128.0f/255.0f};
      if(!check(!frame.vertices.empty() && !frame.draws.empty(),label+": empty packed material gate"))return false;
      // Inspect material slots selected by captured vertices, including the
      // expanded stroke/point attributes, instead of any unused table slot.
      for(const auto & vertex:frame.vertices) {
        if(!check(vertex.materialSlot<frame.materials.size(),label+": selected material slot out of bounds"))return false;
        const auto & material=frame.materials[vertex.materialSlot];
        for(int channel=0;channel<4;++channel) {
          if(!check(std::abs(material.diffuse[channel]-expected[channel])<=1e-7f,
              label+": native glColor4ub RGBA8 channel was not preserved"))return false;
        }
        if(!check(std::abs(material.transparency-(1.0f-expected[3]))<=1e-7f,
             label+": packed alpha and material transparency disagree"))return false;
      }
      for(const auto & draw:frame.draws) {
        if(!check(draw.renderStateSlot<frame.renderStates.size(),label+": selected state slot out of bounds"))return false;
        const auto & state=frame.renderStates[draw.renderStateSlot];
        if(!check(state.transparentMaterial && !state.transparentTexture && !state.rasterPixels &&
            state.transparencyType==int(CoinRenderAction::BLEND),
            label+": RGBA quantization changed original transparency classification"))return false;
      }
      return true;
    };
    const std::string label="clipped-contour/"+std::to_string(style);
    if(!test.render(scene.root,label+"/diagonal-through-corners") ||
       !packedMaterial(label+"/packed-clipped-material"))return false;
    const auto clippedCpu=test.lastCpu,clippedNative=test.lastNative,clippedGl=test.reference;
    clip->on=FALSE;
    if(!test.render(scene.root,label+"/disabled-control") || !packedMaterial(label+"/packed-control-material") ||
       !changed(clippedCpu,test.lastCpu,label+"/CPU-control") ||
       (gpu && (!changed(clippedNative,test.lastNative,label+"/GPU-control") ||
                !changed(clippedGl,test.reference,label+"/native-control"))))return false;
    clip->on=TRUE;
    if(!test.render(scene.root,label+"/diagonal-restored") || !packedMaterial(label+"/packed-restored-material") ||
       !restored(test,clippedCpu,clippedNative,label+"/A-B-A") ||
       (gpu && !check(test.reference==clippedGl,label+": native A/B/A did not restore clipping")))return false;
  }
  return true;
}

bool invalidBoundingDomain(bool gpu) {
  Scene scene;Harness test(gpu);
  if(!test.render(scene.root,"bounds-domain/anchor"))return false;
  const auto originalCpu=test.lastCpu,originalNative=test.lastNative;
  const SbVec3f lo=scene.shape->minimum.getValue(),hi=scene.shape->maximum.getValue();
  scene.shape->minimum.setValue(std::numeric_limits<float>::quiet_NaN(),lo[1],lo[2]);
  if(!test.reject(scene.root,"bounds-domain/nonfinite-minimum",CoinRenderAction::INVALID_SCENE))return false;
  scene.shape->minimum=lo;
  scene.shape->maximum.setValue(hi[0],std::numeric_limits<float>::infinity(),hi[2]);
  if(!test.reject(scene.root,"bounds-domain/nonfinite-maximum",CoinRenderAction::INVALID_SCENE))return false;
  // Every bound and the midpoint are finite, while max-min overflows. This
  // is distinct from a NaN or infinity already present in computeBBox.
  const float large=std::numeric_limits<float>::max()*.75f;
  scene.shape->minimum.setValue(-large,lo[1],lo[2]);scene.shape->maximum.setValue(large,hi[1],hi[2]);
  if(!test.reject(scene.root,"bounds-domain/finite-bounds-overflow-extent",CoinRenderAction::INVALID_SCENE))return false;
  scene.shape->minimum=lo;scene.shape->maximum=hi;
  return test.render(scene.root,"bounds-domain/repaired") &&
    restored(test,originalCpu,originalNative,"bounds-domain/A-B-A");
}

bool shadowMapGeometry(bool gpu) {
  if(!gpu) {
    std::cout<<"shadow-map/original-caster-profile requires actual GPU; not certified by CPU capture mode\n";
    return true;
  }
  Scene scene;Harness test(gpu);scene.lighting->model=SoLightModel::PHONG;
  scene.content->removeAllChildren();
  auto * group=new SoShadowGroup;group->isActive=TRUE;group->precision=.0625f;
  auto * light=new SoShadowSpotLight;light->location.setValue(0,0,3);light->direction.setValue(0,0,-1);
  light->cutOffAngle=.7f;light->nearDistance=.1f;light->farDistance=10;light->intensity=0;group->addChild(light);
  auto * style=new SoShadowStyle;style->style=SoShadowStyle::CASTS_SHADOW_AND_SHADOWED;group->addChild(style);
  auto * material=new SoMaterial;material->ambientColor.setValue(0,0,0);material->specularColor.setValue(0,0,0);
  material->diffuseColor.setValue(0,0,0);material->emissiveColor.setValue(.8f,.3f,.6f);group->addChild(material);
  auto * sphere=new SoSphere;sphere->radius=.6f;group->addChild(sphere);scene.content->addChild(group);
  if(!test.renderGpuOnly(scene.root,"shadow-map/bounding-main-original-caster"))return false;
  auto inspect=[&](const std::string & label) {
    const auto & frame=test.gpuAction.getPimpl()->lastValidPlan;
    size_t mainTriangles=0,mapTriangles=0;
    for(const auto & draw:frame.draws) {
      if(draw.topology!=CoinRenderPrimitiveTopology::TRIANGLE_LIST)continue;
      const size_t count=(draw.geometry.indexCount?draw.geometry.indexCount:draw.geometry.vertexCount)/3;
      if(draw.shadowLightSlot)mapTriangles+=count;else mainTriangles+=count;
    }
    std::cout<<label<<" main_triangles="<<mainTriangles<<" original_caster_triangles="<<mapTriangles<<'\n';
    return check(frame.shadowLights.size()==1 && mainTriangles==12 && mapTriangles>12,
                 label+": native shadow-map profile must retain original sphere, not main bounding box");
  };
  if(!inspect("shadow-map/capture"))return false;
  const auto originalNative=test.lastNative;
  sphere->radius=.7f;
  if(!test.renderGpuOnly(scene.root,"shadow-map/bounds-mutated") ||
     !changed(originalNative,test.lastNative,"shadow-map/bounds") || !inspect("shadow-map/mutation"))return false;
  sphere->radius=.6f;
  return test.renderGpuOnly(scene.root,"shadow-map/bounds-restored") &&
    check(test.lastNative==originalNative,"shadow-map/A/B/A did not restore pixels") && inspect("shadow-map/restoration");
}
} // namespace

int main(int argc,char ** argv) {
  bool gpu=false;
  for(int i=1;i<argc;++i) {
    const std::string argument=argv[i];
    if(argument=="--gpu")gpu=true;else if(argument=="--capture")gpu=false;
    else {std::cerr<<"Usage: CoinRenderBoundingBoxTest --capture|--gpu\n";return 2;}
  }
  SoDB::init();CoinRenderAction::initClass();BoundingFixtureShape::initClass();
  if(gpu && !CoinRenderAction::isGpuBackendAvailable()) {
    std::cerr<<"CoinRenderBoundingBoxTest requires the selected GPU backend\n";return 77;
  }
#if !COIN_HAVE_LEGACY_GL_RENDERER
  if(gpu){std::cerr<<"CoinRenderBoundingBoxTest mandatory CoinGL oracle not compiled\n";return 1;}
#endif
  if(!typeAndReuse(gpu) || !nativeShapeKinds(gpu) || !transformationsAndStyles(gpu) ||
     !materialAndTexture(gpu) || !unsupportedTextures(gpu) || !vertexPropertyScope(gpu) ||
     !scopeAndOverride(gpu) || !callbacks(gpu) || !degenerates(gpu) || !faceNormals(gpu) ||
     !specialDispatch(gpu) || !specialCallbackDispatch(gpu) || !boundingSortedTriangles(gpu) ||
     !markerBoundingAlphaClassification(gpu) || !clippedContourMultiplicity(gpu) ||
     !invalidBoundingDomain(gpu) || !shadowMapGeometry(gpu))return 1;
  std::cout<<"CoinRenderBoundingBoxTest "<<(gpu?"GPU/CoinGL":"capture/CPU")
           <<" passed native_gl_comparisons="<<nativeGlComparisons<<'\n';
  return 0;
}
