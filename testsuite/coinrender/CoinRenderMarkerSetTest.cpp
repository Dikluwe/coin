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
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedMarkerSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoVertexProperty.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int width = 192, height = 160;
using Color = std::array<int,3>;
size_t referencePairs = 0, extensionChecks = 0;
bool check(bool good, const std::string & message) {
  if (!good) std::cerr << "CoinRenderMarkerSetTest: " << message << '\n';
  return good;
}
class CaptureBackend : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override {
    ++submissions; draws = frame.draws.size(); vertices = frame.vertices.size();
    return CoinRenderCpuReferenceBackend::submit(frame,target);
  }
  size_t submissions = 0, draws = 0, vertices = 0;
};
struct Scene {
  explicit Scene(bool indexed = false) {
    root = new SoSeparator; root->ref();
    camera = new SoOrthographicCamera; camera->height = height;
    camera->position.setValue(0,0,10); camera->nearDistance = 1; camera->farDistance = 40;
    root->addChild(camera);
    lighting = new SoLightModel; lighting->model = SoLightModel::BASE_COLOR; root->addChild(lighting);
    shapeSwitch = new SoSwitch; shapeSwitch->whichChild = SO_SWITCH_ALL; root->addChild(shapeSwitch);
    group = new SoSeparator; shapeSwitch->addChild(group);
    transform = new SoTransform; group->addChild(transform);
    depth = new SoDepthBuffer; depth->function = SoDepthBuffer::LEQUAL; group->addChild(depth);
    material = new SoMaterial; material->diffuseColor.setValue(1,0,0); group->addChild(material);
    binding = new SoMaterialBinding; binding->value = SoMaterialBinding::OVERALL; group->addChild(binding);
    coordinates = new SoCoordinate3; coordinates->point.setValue(0,0,0); group->addChild(coordinates);
    if (indexed) {
      indexedMarkers = new SoIndexedMarkerSet; indexedMarkers->coordIndex = 0;
      indexedMarkers->markerIndex = SoMarkerSet::CROSS_9_9; group->addChild(indexedMarkers);
    } else {
      markers = new SoMarkerSet; markers->markerIndex = SoMarkerSet::CROSS_9_9; group->addChild(markers);
    }
  }
  ~Scene() { root->unref(); }
  Scene(const Scene &) = delete;
  Scene & operator=(const Scene &) = delete;
  void markerIndices(const std::vector<int32_t> & indices) {
    auto & field = markers ? markers->markerIndex : indexedMarkers->markerIndex;
    field.setNum(int(indices.size()));
    if (!indices.empty()) field.setValues(0,int(indices.size()),indices.data());
  }
  void points(const std::vector<SbVec3f> & points) {
    coordinates->point.setNum(int(points.size()));
    if (!points.empty()) coordinates->point.setValues(0,int(points.size()),points.data());
    if (indexedMarkers) {
      std::vector<int32_t> indices(points.size());
      for (size_t i = 0; i < indices.size(); ++i) indices[i] = int32_t(i);
      indexedMarkers->coordIndex.setNum(int(indices.size()));
      if (!indices.empty()) indexedMarkers->coordIndex.setValues(0,int(indices.size()),indices.data());
    }
  }
  void removeDepth() { group->removeChild(depth); depth = nullptr; }
  SoSeparator * root = nullptr, * group = nullptr;
  SoSwitch * shapeSwitch = nullptr;
  SoOrthographicCamera * camera = nullptr;
  SoLightModel * lighting = nullptr;
  SoTransform * transform = nullptr;
  SoDepthBuffer * depth = nullptr;
  SoMaterial * material = nullptr;
  SoMaterialBinding * binding = nullptr;
  SoCoordinate3 * coordinates = nullptr;
  SoMarkerSet * markers = nullptr;
  SoIndexedMarkerSet * indexedMarkers = nullptr;
};
size_t colored(const std::vector<uint8_t> & pixels) {
  size_t result = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    result += pixels[i] > 3 || pixels[i+1] > 3 || pixels[i+2] > 3;
  return result;
}
bool sample(const std::vector<uint8_t> & pixels, int x, int y, const Color & color,
            const std::string & label) {
  if (!check(pixels.size() == size_t(width * height * 4),label + ": incomplete readback")) return false;
  const size_t p = size_t(y * width + x) * 4; bool good = true;
  for (int c = 0; c < 3; ++c) good &= std::abs(int(pixels[p+c]) - color[c]) <= 2;
  if (!good) std::cerr << label << " at=" << x << ',' << y << " actual="
    << int(pixels[p]) << ',' << int(pixels[p+1]) << ',' << int(pixels[p+2]) << '\n';
  return check(good,label + ": material/depth sample mismatch");
}
struct Harness {
  explicit Harness(bool selectedGpu) : gpu(selectedGpu), cpuAction(SbViewportRegion(width,height)),
    gpuAction(SbViewportRegion(width,height))
#if COIN_HAVE_LEGACY_GL_RENDERER
    , gl(SbViewportRegion(width,height))
#endif
  {
    cpu.reset(CoinRenderTarget::createOffscreen(SbVec2i32(width,height)));
    if (cpu) {
      observer = new CaptureBackend; cpu->getPimpl()->backend.reset(observer);
      cpu->getPimpl()->depthBuffer.assign(width * height,1.0f); cpuAction.setRenderTarget(cpu.get());
    }
    if (gpu) {
      native.reset(CoinRenderTarget::createOffscreen(SbVec2i32(width,height)));
      if (native) gpuAction.setRenderTarget(native.get());
    }
    transparency(CoinRenderAction::BLEND);
    cpuAction.setBackgroundColor(SbColor4f(0,0,0,1));
    gpuAction.setBackgroundColor(SbColor4f(0,0,0,1));
#if COIN_HAVE_LEGACY_GL_RENDERER
    gl.setComponents(SoOffscreenRenderer::RGB); gl.setBackgroundColor(SbColor(0,0,0));
#endif
  }
  ~Harness() { cpuAction.setRenderTarget(nullptr); gpuAction.setRenderTarget(nullptr); }
  void transparency(CoinRenderAction::TransparencyType mode) {
    cpuAction.setTransparencyType(mode); gpuAction.setTransparencyType(mode);
#if COIN_HAVE_LEGACY_GL_RENDERER
    gl.getGLRenderAction()->setTransparencyType(static_cast<SoGLRenderAction::TransparencyType>(mode));
#endif
  }
  bool applyCpu(SoNode * root, const std::string & label) {
    const size_t before = observer->submissions;
    cpuAction.apply(root); cpu->readbackRGBA(lastCpu);
    return check(cpuAction.getLastStatus() == CoinRenderAction::SUCCESS &&
                 observer->submissions == before + 1 && lastCpu.size() == size_t(width * height * 4),
                 label + ": CPU capture/submission failed: " + cpuAction.getLastError().getString());
  }
  bool applyGpu(SoNode * root, const std::string & label) {
    gpuAction.apply(root); native->readbackRGBA(lastNative);
    return check(gpuAction.getLastStatus() == CoinRenderAction::SUCCESS &&
                 lastNative.size() == size_t(width * height * 4),
                 label + ": GPU submission failed: " + gpuAction.getLastError().getString());
  }
  bool compare(const std::vector<uint8_t> & actual, const std::string & label) {
    if (!check(actual.size() == reference.size(),label + ": complete pixels required")) return false;
    uint64_t error = 0; size_t active = 0; int maximum = 0;
    for (size_t i = 0; i < actual.size(); i += 4) {
      if (!(actual[i] > 3 || actual[i+1] > 3 || actual[i+2] > 3 ||
            reference[i] > 3 || reference[i+1] > 3 || reference[i+2] > 3)) continue;
      ++active;
      for (int c = 0; c < 3; ++c) {
        const int delta = std::abs(int(actual[i+c]) - int(reference[i+c]));
        maximum = std::max(maximum,delta); error += delta;
      }
    }
    const double mean = active ? double(error) / (active * 3) : 0;
    std::cout << label << " native_pixels=" << colored(actual) << " gl_pixels=" << colored(reference)
              << " rgb_roi_mae=" << mean << " rgb_max=" << maximum << '\n';
    if (maximum > 2 || mean > 1) {
      const auto dump = [](const char * path, const std::vector<uint8_t> & rgba) {
        std::ofstream output(path,std::ios::binary);
        output << "P6\n" << width << ' ' << height << "\n255\n";
        for (size_t i = 0; i < rgba.size(); i += 4)
          output.write(reinterpret_cast<const char *>(rgba.data() + i),3);
        return bool(output);
      };
      const bool actualDump = dump("/tmp/coin-marker-actual.ppm",actual);
      const bool referenceDump = dump("/tmp/coin-marker-reference.ppm",reference);
      std::cerr << label << " failure_images actual=/tmp/coin-marker-actual.ppm"
                << " reference=/tmp/coin-marker-reference.ppm"
                << " saved=" << (actualDump && referenceDump) << '\n';
    }
    return check(maximum <= 2 && mean <= 1,label + ": bitmap placement/color differs from CoinGL");
  }
  // Population is independently obtained from Coin's registered bitmap asset,
  // not the new capture code. -1 admits a nonempty depth/clipping composite.
  bool render(Scene & scene, const std::string & label, int population = -1,
              bool prunedReference = false) {
    if (!check(cpu && observer && (!gpu || native),label + ": target creation failed") ||
        !applyCpu(scene.root,label)) return false;
    if (!check(population >= 0 ? colored(lastCpu) == size_t(population) : colored(lastCpu) > 0,
               label + ": CPU bitmap population is wrong or the control is vacuous")) return false;
    if (population > 0 && !check(observer->draws && observer->vertices,
                               label + ": active marker was not captured")) return false;
    if (!gpu) return true;
#if COIN_HAVE_LEGACY_GL_RENDERER
    SoNode * root = scene.root; SoSeparator * pruned = nullptr;
    if (prunedReference) {
      pruned = static_cast<SoSeparator *>(scene.root->copy(TRUE)); pruned->ref();
      auto * toggle = static_cast<SoSwitch *>(pruned->getChild(scene.root->findChild(scene.shapeSwitch)));
      toggle->whichChild = SO_SWITCH_NONE; root = pruned;
    }
    const bool available = gl.render(root) && gl.getBuffer();
    if (available) {
      const uint8_t * data = gl.getBuffer(); reference.resize(width * height * 4);
      for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const size_t a = size_t(y * width + x) * 4, b = size_t((height - 1 - y) * width + x) * 3;
        for (int c = 0; c < 3; ++c) reference[a+c] = data[b+c]; reference[a+3] = 255;
      }
    }
    if (pruned) pruned->unref();
    if (!check(available,label + ": mandatory CoinGL oracle unavailable") ||
        !check(population >= 0 ? colored(reference) == size_t(population) : colored(reference) > 0,
               label + ": CoinGL control is vacuous or its native bitmap population differs") ||
        !applyGpu(scene.root,label)) return false;
    ++referencePairs;
    return compare(lastCpu,label + "/CPU") && compare(lastNative,label + "/GPU");
#else
    return check(false,label + ": mandatory CoinGL oracle was not compiled");
#endif
  }
  bool center(int x, int y, const Color & color, const std::string & label) {
    return sample(lastCpu,x,y,color,label + "/CPU") &&
      (!gpu || (sample(lastNative,x,y,color,label + "/GPU") && sample(reference,x,y,color,label + "/CoinGL")));
  }
  bool extension(Scene & scene, const std::string & label,
                 const std::vector<uint8_t> & expectedCpu, const std::vector<uint8_t> & expectedGpu) {
    // Native IndexedMarkerSet reads short markerIndex out of bounds in Release.
    // Qualify this safe extension against an explicit complete list instead.
    if (!applyCpu(scene.root,label) || !check(lastCpu == expectedCpu,label + ": CPU safe extension differs")) return false;
    if (gpu && (!applyGpu(scene.root,label) || !check(lastNative == expectedGpu,label + ": GPU safe extension differs"))) return false;
    ++extensionChecks; std::cout << label << " complete-list-equivalence passed (no unsafe CoinGL execution)\n";
    return true;
  }
  bool reject(Scene & scene, const std::string & label, CoinRenderAction::Status status) {
    const auto beforeCpu = lastCpu, beforeNative = lastNative; std::vector<uint8_t> pixels;
    const size_t submissions = observer->submissions; const uint64_t cpuSerial = cpu->getLastSubmissionSerial();
    cpuAction.apply(scene.root); cpu->readbackRGBA(pixels);
    if (!check(cpuAction.getLastStatus() == status && cpuAction.getLastError().getLength() &&
               observer->submissions == submissions && cpu->getLastSubmissionSerial() == cpuSerial && pixels == beforeCpu,
               label + ": CPU rejection must be transactional")) return false;
    if (gpu) {
      const uint64_t serial = native->getLastSubmissionSerial();
      gpuAction.apply(scene.root); native->readbackRGBA(pixels);
      if (!check(gpuAction.getLastStatus() == status && gpuAction.getLastError().getLength() &&
                 native->getLastSubmissionSerial() == serial && pixels == beforeNative,
                 label + ": GPU rejection must preserve pixels/serial")) return false;
    }
    std::cout << label << " explicit transactional rejection passed\n";
    return true;
  }
  bool gpu;
  std::unique_ptr<CoinRenderTarget> cpu,native;
  CaptureBackend * observer = nullptr;
  CoinRenderAction cpuAction,gpuAction;
#if COIN_HAVE_LEGACY_GL_RENDERER
  SoOffscreenRenderer gl;
#endif
  std::vector<uint8_t> lastCpu,lastNative,reference;
};

int bitmapPopulation(int id, int alignment) {
  SbVec2s size; const unsigned char * bytes = nullptr; SbBool lsb = FALSE;
  if (!SoMarkerSet::getMarker(id,size,bytes,lsb) || !bytes || size[0] <= 0 || size[1] <= 0) return 0;
  const int packed = (size[0] + 7) / 8, stride = ((packed + alignment - 1) / alignment) * alignment;
  int count = 0;
  for (int y = 0; y < size[1]; ++y) for (int x = 0; x < size[0]; ++x)
    count += (bytes[y * stride + x / 8] & (lsb ? 1u << (x % 8) : 0x80u >> (x % 8))) != 0;
  return count;
}
bool builtins(bool gpu) {
  int population = 0;
  for (int id = 0; id < SoMarkerSet::NUM_MARKERS; ++id) population += bitmapPopulation(id,4);
  if (!check(SoMarkerSet::NUM_MARKERS == 90 && population > 900,"all ninety native bitmap assets must be present")) return false;
  for (bool indexed : {false,true}) {
    Scene scene(indexed); Harness test(gpu); std::vector<SbVec3f> points; std::vector<int32_t> ids;
    for (int id = 0; id < SoMarkerSet::NUM_MARKERS; ++id) {
      points.emplace_back(16 + (id % 10) * 16 - width/2,16 + (id / 10) * 16 - height/2,0);
      ids.push_back(id);
    }
    scene.points(points); scene.markerIndices(ids);
    const std::string label = indexed ? "indexed/all-90-builtins" : "markers/all-90-builtins";
    if (!test.render(scene,label,population)) return false;
    const auto before = test.lastCpu;
    if (!test.render(scene,label + "/repeat",population) ||
        !check(test.lastCpu == before,label + ": repeated output changed")) return false;
    scene.markerIndices(std::vector<int32_t>(90,SoMarkerSet::NONE));
    if (!test.render(scene,label + "/none",0) || !check(test.observer->draws == 0,"NONE emitted geometry")) return false;
    scene.markerIndices(ids);
    if (!test.render(scene,label + "/restore",population) || !check(test.lastCpu == before,"NONE restore changed bitmaps")) return false;
  }
  return true;
}
bool selectionAndMaterials(bool gpu) {
  const SbColor colors[] = {{1,0,0},{0,1,0},{0,0,1},{1,1,1}};
  {
    Scene scene; Harness test(gpu); scene.points({{-36,0,0},{-18,0,0},{18,0,0},{36,0,0}});
    scene.material->diffuseColor.setValues(0,4,colors); scene.binding->value = SoMaterialBinding::PER_VERTEX;
    scene.markers->startIndex = 1; scene.markers->numPoints = 2;
    scene.markerIndices({SoMarkerSet::MINUS_9_9,SoMarkerSet::NONE,SoMarkerSet::PLUS_9_9});
    if (!test.render(scene,"markers/start-count-material-ordinal",bitmapPopulation(SoMarkerSet::MINUS_9_9,4)) ||
        !test.center(78,79,{{255,0,0}},"markers/material-starts-at-zero")) return false;
    scene.markers->numPoints = -1;
    if (!test.render(scene,"markers/all-remaining-none-consumes-material",
                     bitmapPopulation(SoMarkerSet::MINUS_9_9,4) + bitmapPopulation(SoMarkerSet::PLUS_9_9,4)) ||
        !test.center(132,79,{{0,0,255}},"markers/none-keeps-ordinal")) return false;
    scene.markers->startIndex = 0; scene.markerIndices({SoMarkerSet::CROSS_9_9,SoMarkerSet::PLUS_9_9});
    if (!test.render(scene,"markers/short-list-repeats-tail",bitmapPopulation(SoMarkerSet::CROSS_9_9,4) +
                     3 * bitmapPopulation(SoMarkerSet::PLUS_9_9,4))) return false;
    scene.markers->numPoints = 0;
    if (!test.render(scene,"markers/zero-points",0)) return false;
  }
  {
    Scene scene(true); Harness test(gpu); scene.points({{-24,0,0},{0,0,0},{24,0,0}});
    const int32_t indices[] = {2,0,1}; scene.indexedMarkers->coordIndex.setValues(0,3,indices);
    scene.markerIndices({SoMarkerSet::CROSS_9_9,SoMarkerSet::PLUS_9_9,SoMarkerSet::DIAMOND_FILLED_9_9});
    scene.material->diffuseColor.setValues(0,3,colors); scene.binding->value = SoMaterialBinding::PER_VERTEX;
    if (!test.render(scene,"indexed/per-vertex-ordinal") ||
        !test.center(120,79,{{255,0,0}},"indexed/ordinal-zero") ||
        !test.center(72,79,{{0,255,0}},"indexed/ordinal-one")) return false;
    scene.binding->value = SoMaterialBinding::PER_VERTEX_INDEXED; scene.indexedMarkers->materialIndex = -1;
    if (!test.render(scene,"indexed/coordinate-default-material-indices") ||
        !test.center(120,79,{{0,0,255}},"indexed/coordinate-material-two") ||
        !test.center(72,79,{{255,0,0}},"indexed/coordinate-material-zero")) return false;
    const int32_t explicitMaterials[] = {1,2,0}; scene.indexedMarkers->materialIndex.setValues(0,3,explicitMaterials);
    if (!test.render(scene,"indexed/explicit-material-indices") ||
        !test.center(120,79,{{0,255,0}},"indexed/explicit-one")) return false;
    scene.markerIndices({SoMarkerSet::CROSS_9_9,SoMarkerSet::CROSS_9_9,SoMarkerSet::CROSS_9_9});
    if (!test.render(scene,"indexed/complete-tail-control")) return false;
    const auto cpu = test.lastCpu, native = test.lastNative;
    scene.markerIndices({SoMarkerSet::CROSS_9_9});
    if (!test.extension(scene,"indexed/short-list-safe-extension",cpu,native)) return false;
    scene.markerIndices({SoMarkerSet::CROSS_9_9,SoMarkerSet::CROSS_9_9,SoMarkerSet::CROSS_9_9});
    const int32_t permutation[] = {1,2,0}; scene.indexedMarkers->coordIndex.setValues(0,3,permutation);
    if (!test.render(scene,"indexed/index-permutation-mutation") ||
        !test.center(96,79,{{0,255,0}},"indexed/permutation-material")) return false;
    scene.indexedMarkers->coordIndex.setNum(0);
    if (!test.render(scene,"indexed/empty-indices",0)) return false;
  }
  return true;
}
bool vertexProperties(bool gpu) {
  for (bool indexed : {false,true}) {
    Scene scene(indexed); Harness test(gpu); scene.material->diffuseColor.setValue(0,0,1);
    auto * property = new SoVertexProperty;
    const SbVec3f points[] = {{-20,10,0},{20,-10,0}};
    const uint32_t rgba[] = {0xff0000ffu,0x00ff00ffu};
    property->vertex.setValues(0,2,points); property->orderedRGBA.setValues(0,2,rgba);
    property->materialBinding = indexed ? SoVertexProperty::PER_VERTEX_INDEXED : SoVertexProperty::PER_VERTEX;
    if (indexed) {
      scene.indexedMarkers->vertexProperty = property;
      const int32_t order[] = {1,0}; scene.indexedMarkers->coordIndex.setValues(0,2,order);
      scene.markerIndices({SoMarkerSet::CROSS_9_9,SoMarkerSet::PLUS_9_9});
    } else { scene.markers->vertexProperty = property; scene.markerIndices({SoMarkerSet::CROSS_9_9}); }
    const std::string label = indexed ? "indexed/vertex-property" : "markers/vertex-property";
    if (!test.render(scene,label) || !test.center(76,69,{{255,0,0}},label + "/red") ||
        !test.center(116,89,{{0,255,0}},label + "/green")) return false;
    property->vertex.set1Value(1,SbVec3f(28,-16,0));
    property->orderedRGBA.set1Value(1,0xffffffffu);
    if (!test.render(scene,label + "/mutation") || !test.center(124,95,{{255,255,255}},label + "/changed")) return false;
  }
  return true;
}

// Registered assets are encoded from an asymmetric logical picture. Coin's
// own registry decodes LSB/MSB and top/bottom ordering; GL is the raster oracle.
std::vector<uint8_t> customBytes(bool lsb, bool topDown, bool second = false) {
  const char * rows[] = {"#............",".##..........","..#.......#..","...####......",
                         ".............",".........##..","............#"};
  std::vector<uint8_t> bytes(2 * 7,0);
  for (int y = 0; y < 7; ++y) for (int x = 0; x < 13; ++x) {
    const bool bit = second ? (x == 2 || (y == 5 && x > 3)) : rows[y][x] == '#';
    if (bit) bytes[(topDown ? y : 6-y) * 2 + x/8] |= lsb ? uint8_t(1u << (x%8)) : uint8_t(0x80u >> (x%8));
  }
  return bytes;
}
struct CustomRegistry {
  int first = SoMarkerSet::getNumDefinedMarkers();
  int count = 0;
  ~CustomRegistry() { for (int i = count - 1; i >= 0; --i) SoMarkerSet::removeMarker(first + i); }
  int add(bool lsb, bool topDown) {
    const auto bytes = customBytes(lsb,topDown);
    const int id = first + count++; SoMarkerSet::addMarker(id,SbVec2s(13,7),bytes.data(),lsb,topDown); return id;
  }
};
bool customRegistration(bool gpu) {
  CustomRegistry registry; std::vector<int32_t> ids;
  for (bool lsb : {false,true}) for (bool topDown : {false,true}) ids.push_back(registry.add(lsb,topDown));
  int population = 0; for (int id : ids) population += bitmapPopulation(id,1);
  for (bool indexed : {false,true}) {
    Scene scene(indexed); Harness test(gpu); scene.points({{-36,0,0},{-12,0,0},{12,0,0},{36,0,0}}); scene.markerIndices(ids);
    const std::string label = indexed ? "indexed/custom-bit-order-row-order" : "markers/custom-bit-order-row-order";
    if (!test.render(scene,label,population)) return false;
    const auto before = test.lastCpu;
    const auto replacement = customBytes(false,true,true);
    SoMarkerSet::addMarker(ids[0],SbVec2s(13,7),replacement.data(),FALSE,TRUE);
    int changedPopulation = bitmapPopulation(ids[0],1);
    for (size_t i = 1; i < ids.size(); ++i) changedPopulation += bitmapPopulation(ids[i],1);
    if (!test.render(scene,label + "/reregister-same-node",changedPopulation) ||
        !check(test.lastCpu != before,"registry mutation without node notification retained stale bitmap")) return false;
    const auto original = customBytes(false,true);
    SoMarkerSet::addMarker(ids[0],SbVec2s(13,7),original.data(),FALSE,TRUE);
    if (!test.render(scene,label + "/restore",population) || !check(test.lastCpu == before,"custom A/B/A restore changed pixels")) return false;
  }
  {
    Scene scene(true); Harness test(gpu); const int id = ids.back(); scene.markerIndices({id});
    if (!test.render(scene,"indexed/custom-remove-visible-control",bitmapPopulation(id,1))) return false;
    if (!check(SoMarkerSet::removeMarker(id),"remove of custom last registration failed")) return false;
    --registry.count;
    if (!test.render(scene,"indexed/removed-custom-native-noop",0)) return false;
    const auto replacement = customBytes(false,true,true);
    SoMarkerSet::addMarker(id,SbVec2s(13,7),replacement.data(),FALSE,TRUE); ++registry.count;
    if (!test.render(scene,"indexed/readd-custom-same-id",bitmapPopulation(id,1))) return false;
  }
  return true;
}

SoSeparator * quad(float left, float right, float z, const SbColor & color, bool depthTest = true) {
  auto * result = new SoSeparator;
  auto * lighting = new SoLightModel; lighting->model = SoLightModel::BASE_COLOR;
  result->addChild(lighting);
  auto * depth = new SoDepthBuffer; depth->test = depthTest; depth->write = depthTest;
  depth->function = SoDepthBuffer::LEQUAL; result->addChild(depth);
  auto * material = new SoMaterial; material->diffuseColor.setValue(color); result->addChild(material);
  auto * coordinates = new SoCoordinate3;
  const SbVec3f points[] = {{left,-20,z},{right,-20,z},{right,20,z},{left,20,z}};
  coordinates->point.setValues(0,4,points); result->addChild(coordinates);
  auto * faces = new SoIndexedFaceSet; const int32_t indices[] = {0,1,2,3,-1};
  faces->coordIndex.setValues(0,5,indices); result->addChild(faces); return result;
}
bool anchorsAndDepth(bool gpu) {
  for (bool indexed : {false,true}) {
    Scene scene(indexed); Harness test(gpu);
    const std::string label = indexed ? "indexed/anchors" : "markers/anchors";
    const int population = bitmapPopulation(SoMarkerSet::CROSS_9_9,4);
    for (float fractional : {.25f,.75f}) {
      scene.points({{fractional,fractional,0}});
      if (!test.render(scene,label + "/fractional-" + std::to_string(fractional),population)) return false;
    }
    for (float offset : {-.00001f,.00001f,-.0003f,.0003f}) {
      // Exercise native raster precision immediately around absolute pixel 16;
      // preserve the authored coordinates and let CoinGL decide placement.
      scene.points({{16.f + offset - width/2,16.f + offset - height/2,0}});
      if (!test.render(scene,label + "/near-integer-16-offset-" + std::to_string(offset),population)) return false;
    }
    for (const auto & position : std::vector<SbVec3f>{{-95,0,0},{0,-79,0},{95,0,0},{0,79,0},{100,0,0}}) {
      scene.points({position});
      // Native glRasterPos invalidates a left/bottom shifted bitmap origin;
      // right/top bitmaps are clipped after their valid raster origin.
      const bool absent = position[0] < -90 || position[1] < -74 || position[0] > 96;
      if (!test.render(scene,label + "/border-x-" + std::to_string(position[0]) +
                        "-y-" + std::to_string(position[1]),absent ? 0 : -1)) return false;
    }
    scene.points({{-24,0,0},{24,0,0}}); scene.markerIndices({SoMarkerSet::CROSS_9_9,SoMarkerSet::CROSS_9_9});
    scene.root->insertChild(quad(-90,90,-1,SbColor(0,0,1)),2);
    scene.root->insertChild(quad(-90,-2,.5f,SbColor(1,0,0)),3);
    scene.material->diffuseColor.setValue(1,1,1);
    scene.root->addChild(quad(2,90,-.5f,SbColor(0,1,0)));
    if (!test.render(scene,label + "/depth-write-on") ||
        !test.center(72,79,{{255,0,0}},label + "/behind-near-occluder") ||
        !test.center(120,79,{{255,255,255}},label + "/marker-depth-preserved")) return false;
    scene.depth->write = FALSE;
    if (!test.render(scene,label + "/depth-write-off") || !test.center(120,79,{{0,255,0}},label + "/later-draw")) return false;
    scene.depth->test = FALSE; scene.depth->write = TRUE;
    if (!test.render(scene,label + "/depth-test-off-write-on") ||
        !test.center(72,79,{{255,255,255}},label + "/test-disabled") ||
        !test.center(120,79,{{0,255,0}},label + "/disabled-test-does-not-write")) return false;
    scene.camera->position.setValue(3,2,10);
    if (!test.render(scene,label + "/camera-mutation")) return false;
  }
  return true;
}
bool inheritedState(bool gpu) {
  for (bool indexed : {false,true}) {
    Scene scene(indexed); Harness test(gpu); scene.removeDepth();
    scene.lighting->model = SoLightModel::PHONG;
    auto * light = new SoDirectionalLight; light->color.setValue(0,1,0); scene.root->insertChild(light,2);
    auto * style = new SoDrawStyle; style->pointSize = 27; style->style = SoDrawStyle::POINTS; scene.group->insertChild(style,0);
    auto * texture = new SoTexture2; texture->model = SoTexture2::REPLACE;
    const uint8_t rgb[] = {0,255,255}; texture->image.setValue(SbVec2s(1,1),3,rgb); scene.group->insertChild(texture,0);
    const std::string label = indexed ? "indexed/inherited-state" : "markers/inherited-state";
    if (!test.render(scene,label + "/lighting-texture-pointsize-disabled",bitmapPopulation(SoMarkerSet::CROSS_9_9,4)) ||
        !test.center(96,79,{{255,0,0}},label + "/native-base-color")) return false;
    scene.material->diffuseColor.setValue(1,1,1); test.transparency(CoinRenderAction::SORTED_OBJECT_BLEND);
    scene.root->insertChild(quad(-90,90,-1,SbColor(0,0,1)),2);
    scene.root->addChild(quad(-90,90,-.5f,SbColor(0,1,0),false));
    if (!test.render(scene,label + "/opaque-inherited-rgb-immediate") ||
        !test.center(96,79,{{0,255,0}},label + "/immediate-covered")) return false;
    const uint8_t opaqueRgba[] = {0,255,255,255}; texture->image.setValue(SbVec2s(1,1),4,opaqueRgba);
    if (!test.render(scene,label + "/opaque-inherited-rgba-immediate") ||
        !test.center(96,79,{{0,255,0}},label + "/rgba-alpha-content-opaque")) return false;
    const uint8_t rgba[] = {0,255,255,64}; texture->image.setValue(SbVec2s(1,1),4,rgba);
    if (!test.render(scene,label + "/inherited-rgba-traversal-classification") ||
        !test.center(96,79,{{255,255,255}},label + "/native-order")) return false;
    const uint8_t la[] = {255,0}; texture->image.setValue(SbVec2s(1,1),2,la);
    if (!test.render(scene,label + "/inherited-la-transparent") ||
        !test.center(96,79,{{255,255,255}},label + "/la-alpha-content-transparent")) return false;
    const uint8_t opaqueLa[] = {255,255}; texture->image.setValue(SbVec2s(1,1),2,opaqueLa);
    if (!test.render(scene,label + "/inherited-la-opaque") ||
        !test.center(96,79,{{0,255,0}},label + "/la-alpha-content-opaque")) return false;
  }
  {
    Scene scene; Harness test(gpu); scene.material->transparency = .5f;
    if (!test.render(scene,"markers/blended-single-bitmap",bitmapPopulation(SoMarkerSet::CROSS_9_9,4)) ||
        !test.center(96,79,{{128,0,0}},"markers/no-duplicate-generic-point")) return false;
    auto * alpha = new SoAlphaTest; alpha->function = SoAlphaTest::NEVER; scene.group->insertChild(alpha,0);
    if (!test.render(scene,"markers/inherited-alpha-never",0)) return false;
    alpha->function = SoAlphaTest::ALWAYS;
    if (!test.render(scene,"markers/inherited-alpha-repair",bitmapPopulation(SoMarkerSet::CROSS_9_9,4))) return false;
  }
  return true;
}
struct Hook { bool prune = false; bool mutate = false; int pre = 0, post = 0; };
SoCallbackAction::Response markerPre(void * data, SoCallbackAction *, const SoNode * node) {
  auto & hook = *static_cast<Hook *>(data); ++hook.pre;
  if (hook.prune) return SoCallbackAction::PRUNE;
  if (hook.mutate) {
    if (node->isOfType(SoMarkerSet::getClassTypeId()))
      static_cast<SoMarkerSet *>(const_cast<SoNode *>(node))->markerIndex = SoMarkerSet::PLUS_9_9;
    else static_cast<SoIndexedMarkerSet *>(const_cast<SoNode *>(node))->markerIndex = SoMarkerSet::PLUS_9_9;
  }
  return SoCallbackAction::CONTINUE;
}
SoCallbackAction::Response markerPost(void * data, SoCallbackAction *, const SoNode *) {
  ++static_cast<Hook *>(data)->post; return SoCallbackAction::CONTINUE;
}
bool callbacks(bool gpu) {
  for (bool indexed : {false,true}) {
    Scene scene(indexed); Hook hook; Harness test(gpu);
    const SoType type = indexed ? SoIndexedMarkerSet::getClassTypeId() : SoMarkerSet::getClassTypeId();
    test.cpuAction.addPreCallback(type,markerPre,&hook); test.gpuAction.addPreCallback(type,markerPre,&hook);
    test.cpuAction.addPostCallback(type,markerPost,&hook); test.gpuAction.addPostCallback(type,markerPost,&hook);
    const std::string label = indexed ? "indexed/callback" : "markers/callback";
    if (!test.render(scene,label + "/control",bitmapPopulation(SoMarkerSet::CROSS_9_9,4))) return false;
    const auto before = test.lastCpu; hook.prune = true;
    if (!test.render(scene,label + "/prune",0,true) ||
        !check(test.observer->draws == 0 && test.observer->vertices == 0,"PRUNE emitted marker or generic point")) return false;
    hook.prune = false; hook.mutate = true;
    if (!test.render(scene,label + "/continue-mutation",bitmapPopulation(SoMarkerSet::PLUS_9_9,4)) ||
        !check(test.lastCpu != before && hook.pre >= (gpu ? 6 : 3) && hook.post > 0,
               "callback mutation/registration did not affect the current capture")) return false;
  }
  return true;
}
struct PointObserver {
  std::vector<SbVec3f> points;
};
void observePoint(void * data, SoCallbackAction *, const SoPrimitiveVertex * vertex) {
  static_cast<PointObserver *>(data)->points.push_back(vertex->getPoint());
}
class UserCallbackMarker : public SoMarkerSet {
  SO_NODE_HEADER(UserCallbackMarker);
public:
  UserCallbackMarker() { SO_NODE_CONSTRUCTOR(UserCallbackMarker); }
  static void initClass() { SO_NODE_INIT_CLASS(UserCallbackMarker, SoMarkerSet, "MarkerSet"); }
  void callback(SoCallbackAction *) override { ++visits; }
  int visits = 0;
protected:
  ~UserCallbackMarker() override = default;
};
SO_NODE_SOURCE(UserCallbackMarker);
bool primitiveObservers(bool gpu) {
  for (bool indexed : {false,true}) {
    Scene scene(indexed); PointObserver expected,cpu,gpuObserver; Harness test(gpu);
    scene.points({{-20,0,0},{0,0,0},{20,0,0}});
    scene.markerIndices({SoMarkerSet::CROSS_9_9,SoMarkerSet::NONE,SoMarkerSet::MINUS_9_9});
    scene.material->transparency = .5f;
    const SoType type = indexed ? SoIndexedMarkerSet::getClassTypeId() : SoMarkerSet::getClassTypeId();
    SoCallbackAction nativeCallbacks; nativeCallbacks.addPointCallback(type,observePoint,&expected);
    nativeCallbacks.apply(scene.root);
    test.cpuAction.addPointCallback(type,observePoint,&cpu);
    test.gpuAction.addPointCallback(type,observePoint,&gpuObserver);
    const std::string label = indexed ? "indexed/point-observer" : "markers/point-observer";
    if (!test.render(scene,label,bitmapPopulation(SoMarkerSet::CROSS_9_9,4) + bitmapPopulation(SoMarkerSet::MINUS_9_9,4)) ||
        !check(expected.points.size() == 3 && cpu.points == expected.points &&
               (!gpu || gpuObserver.points == expected.points),label + ": native point observers must fire exactly once per point") ||
        !test.center(76,79,{{128,0,0}},label + "/no-generic-one-pixel-overdraw")) return false;
  }
  // A derived node's user-defined virtual callback is its capture contract.
  // It deliberately emits no primitives; this is not a native bitmap parity case.
  Scene scene; Harness test(gpu);
  auto * custom = new UserCallbackMarker;
  scene.group->replaceChild(scene.markers,custom); scene.markers = nullptr;
  if (!test.applyCpu(scene.root,"markers/subclass-user-callback") ||
      !check(custom->visits == 1 && test.observer->draws == 0 && colored(test.lastCpu) == 0,
             "derived marker virtual callback must execute without interception")) return false;
  if (gpu && (!test.applyGpu(scene.root,"markers/subclass-user-callback") ||
              !check(custom->visits == 2 && colored(test.lastNative) == 0,"derived GPU action must retain user callback"))) return false;
  std::cout << "markers/subclass-user-callback passed (callback semantics; no native bitmap parity claim)\n";
  return true;
}
bool perspectiveFogAndScreenDoor(bool gpu) {
  for (bool indexed : {false,true}) {
    const std::string label = indexed ? "indexed/extra-raster-state" : "markers/extra-raster-state";
    {
      Scene scene(indexed); Harness test(gpu);
      auto * camera = new SoPerspectiveCamera; camera->position.setValue(0,0,10);
      camera->nearDistance = 1; camera->farDistance = 40; camera->heightAngle = 1;
      scene.root->replaceChild(scene.camera,camera); scene.camera = nullptr;
      scene.points({{-1,.125f,0},{1,.125f,2}});
      scene.markerIndices({SoMarkerSet::CROSS_9_9,SoMarkerSet::CROSS_9_9});
      const int population = 2 * bitmapPopulation(SoMarkerSet::CROSS_9_9,4);
      if (!test.render(scene,label + "/perspective-two-depths-fixed-bitmap-size",population)) return false;
      camera->position.setValue(.25f,.125f,18);
      if (!test.render(scene,label + "/perspective-camera-mutation",population)) return false;
    }
    {
      Scene scene(indexed); Harness test(gpu);
      const int population = bitmapPopulation(SoMarkerSet::CROSS_9_9,4);
      if (!test.render(scene,label + "/before-planar-fog",population)) return false;
      const auto cpuBefore = test.lastCpu,referenceBefore = test.reference;
      auto * fog = new SoEnvironment; fog->fogType = SoEnvironment::FOG;
      fog->fogColor.setValue(0,0,1); fog->fogVisibility = 1.5f; scene.root->insertChild(fog,2);
      if (!test.render(scene,label + "/planar-fog",population) ||
          !check(test.lastCpu != cpuBefore && (!gpu || test.reference != referenceBefore),
                 label + ": planar fog control must visibly change the native marker")) return false;
    }
    {
      Scene scene(indexed); Harness test(gpu); scene.material->transparency = .5f;
      test.transparency(CoinRenderAction::SCREEN_DOOR);
      const int population = bitmapPopulation(SoMarkerSet::CROSS_9_9,4);
      if (!test.render(scene,label + "/screen-door-native-bitmap-holes",population) ||
          !test.center(96,79,{{255,0,0}},label + "/screen-door-primary-alpha-one")) return false;
      auto * alpha = new SoAlphaTest; alpha->function = SoAlphaTest::GREATER; alpha->value = .75f;
      scene.group->insertChild(alpha,0);
      if (!test.render(scene,label + "/screen-door-alpha-greater",population)) return false;
      alpha->function = SoAlphaTest::LESS;
      if (!test.render(scene,label + "/screen-door-alpha-less-discard",0)) return false;
    }
  }
  return true;
}
bool transparentOrdering(bool gpu) {
  for (bool indexed : {false,true}) {
    Scene red(indexed),blue(indexed); Harness test(gpu); red.removeDepth(); blue.removeDepth();
    red.material->transparency = .5f; blue.material->transparency = .5f;
    blue.material->diffuseColor.setValue(0,0,1);
    // Coin's object center is the arithmetic mean, including NONE points.
    // The red mean is farther than blue although its visible center is nearer;
    // the bbox midpoint would give the opposite object order.
    red.points({{0,0,0},{40,0,-10},{40,0,-10}});
    red.markerIndices({SoMarkerSet::CROSS_9_9,SoMarkerSet::CROSS_9_9,SoMarkerSet::NONE});
    blue.points({{0,0,-6}}); blue.markerIndices({SoMarkerSet::CROSS_9_9});
    red.root->addChild(blue.group);
    const std::string label = indexed ? "indexed/transparent-order" : "markers/transparent-order";
    test.transparency(CoinRenderAction::SORTED_OBJECT_BLEND);
    if (!test.render(red,label + "/mean-depth-red-before-blue") ||
        !test.center(96,79,{{64,0,128}},label + "/mean-depth-blend")) return false;
    red.points({{0,0,0},{40,0,-4},{40,0,-4}});
    if (!test.render(red,label + "/mean-depth-mutation-blue-before-red") ||
        !test.center(96,79,{{128,0,64}},label + "/mutated-depth-blend")) return false;
    test.transparency(CoinRenderAction::ADD);
    // Native ADD is immediate and keeps the ordinary depth write. The near
    // red marker therefore blocks the later, farther blue marker here.
    if (!test.render(red,label + "/immediate-add-depth-write") ||
        !test.center(96,79,{{128,0,0}},label + "/immediate-add-near-depth")) return false;
    test.transparency(CoinRenderAction::SORTED_OBJECT_ADD);
    if (!test.render(red,label + "/sorted-object-add") ||
        !test.center(96,79,{{128,0,128}},label + "/sorted-add-color")) return false;
  }
  return true;
}
bool clipAndCameraLimits(bool gpu) {
  for (bool indexed : {false,true}) {
    Scene scene(indexed); Harness test(gpu);
    auto * clip = new SoClipPlane; clip->plane = SbPlane(SbVec3f(1,0,0),0); scene.group->insertChild(clip,0);
    const std::string label = indexed ? "indexed/anchor-clip" : "markers/anchor-clip";
    scene.points({{1,0,0}});
    // Native disables GL clip planes around glBitmap and culls the center.
    // A marker can cross the plane while its center remains inside.
    if (!test.render(scene,label + "/crossing-bitmap-full",bitmapPopulation(SoMarkerSet::CROSS_9_9,4))) return false;
    scene.points({{-1,0,0}});
    if (!test.render(scene,label + "/outside-center",0)) return false;
  }
  Scene scene; Harness test(gpu); const int population = bitmapPopulation(SoMarkerSet::CROSS_9_9,4);
  scene.camera->nearDistance = 0;
  if (!test.render(scene,"markers/orthographic-near-zero-supported",population)) return false;
  auto * perspective = new SoPerspectiveCamera; perspective->position.setValue(0,0,10);
  perspective->nearDistance = 0; perspective->farDistance = 40;
  scene.root->replaceChild(scene.camera,perspective); scene.camera = nullptr;
  if (!test.reject(scene,"markers/degenerate-perspective-volume",CoinRenderAction::UNSUPPORTED)) return false;
  perspective->nearDistance = 1;
  return test.render(scene,"markers/perspective-volume-repair",population);
}
bool invalidControls(bool gpu) {
  Scene scene; Harness test(gpu);
  const int population = bitmapPopulation(SoMarkerSet::CROSS_9_9,4);
  if (!test.render(scene,"markers/invalid-visible-anchor",population)) return false;
  const auto cpu = test.lastCpu,native = test.lastNative;
  scene.markers->startIndex = 7;
  if (!test.reject(scene,"markers/start-outside-coordinates",CoinRenderAction::INVALID_SCENE)) return false;
  scene.markers->startIndex = 0; scene.markers->numPoints = 2;
  if (!test.reject(scene,"markers/count-outside-coordinates",CoinRenderAction::INVALID_SCENE)) return false;
  scene.markers->numPoints = 1; scene.markerIndices({-2});
  if (!test.reject(scene,"markers/invalid-negative-marker",CoinRenderAction::INVALID_SCENE)) return false;
  scene.markerIndices({SoMarkerSet::CROSS_9_9});
  scene.coordinates->point.setValue(std::numeric_limits<float>::quiet_NaN(),0,0);
  if (!test.reject(scene,"markers/nonfinite-coordinate",CoinRenderAction::INVALID_SCENE)) return false;
  scene.markerIndices({SoMarkerSet::NONE});
  if (!test.render(scene,"markers/none-skips-nonfinite-position-use",0)) return false;
  scene.coordinates->point.setValue(0,0,0); scene.markerIndices({SoMarkerSet::CROSS_9_9});
  if (!test.render(scene,"markers/invalid-repair",population) ||
      !check(test.lastCpu == cpu && (!gpu || test.lastNative == native),"invalid repair did not restore exact output")) return false;
  Scene indexed(true); Harness indexedTest(gpu);
  if (!indexedTest.render(indexed,"indexed/invalid-visible-anchor",population)) return false;
  indexed.indexedMarkers->coordIndex = -1;
  if (!indexedTest.reject(indexed,"indexed/invalid-coordinate-index",CoinRenderAction::INVALID_SCENE)) return false;
  indexed.markerIndices({SoMarkerSet::NONE});
  // Native IndexedMarkerSet skips NONE before reading the coordinate index.
  if (!indexedTest.render(indexed,"indexed/none-skips-invalid-coordinate-index",0)) return false;
  indexed.indexedMarkers->coordIndex = 0; indexed.markerIndices({SoMarkerSet::CROSS_9_9});
  return indexedTest.render(indexed,"indexed/invalid-index-repair",population);
}
bool redefineBuiltinLast(bool gpu) {
  // Public addMarker cannot restore a builtin's original alignment metadata.
  // This runs last: no unsafe Indexed CoinGL call is made after redefinition.
  Scene ordinary; Scene indexed(true); Harness test(gpu),indexedTest(gpu);
  ordinary.markerIndices({SoMarkerSet::CROSS_5_5}); indexed.markerIndices({SoMarkerSet::CROSS_5_5});
  if (!test.render(ordinary,"markers/builtin-redefine-control",bitmapPopulation(SoMarkerSet::CROSS_5_5,4)) ||
      !indexedTest.render(indexed,"indexed/builtin-redefine-control",bitmapPopulation(SoMarkerSet::CROSS_5_5,4))) return false;
  const auto before = test.lastCpu; const auto bytes = customBytes(false,true);
  SoMarkerSet::addMarker(SoMarkerSet::CROSS_5_5,SbVec2s(13,7),bytes.data(),FALSE,TRUE);
  if (!test.render(ordinary,"markers/builtin-redefined-real-stride",bitmapPopulation(SoMarkerSet::CROSS_5_5,1)) ||
      !check(test.lastCpu != before,"redefined builtin retained stale bitmap")) return false;
  return indexedTest.reject(indexed,"indexed/builtin-native-stride-incompatibility",CoinRenderAction::UNSUPPORTED);
}
} // namespace

int main(int argc,char ** argv) {
  bool gpu = false;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--gpu") gpu = true;
    else if (argument == "--capture") gpu = false;
    else { std::cerr << "Usage: CoinRenderMarkerSetTest --capture|--gpu\n"; return 2; }
  }
  SoDB::init(); CoinRenderAction::initClass(); UserCallbackMarker::initClass();
  if (gpu && !CoinRenderAction::isGpuBackendAvailable()) {
    std::cerr << "CoinRenderMarkerSetTest requires selected GPU backend\n"; return 77;
  }
#if !COIN_HAVE_LEGACY_GL_RENDERER
  if (gpu) { std::cerr << "CoinRenderMarkerSetTest mandatory CoinGL oracle not compiled\n"; return 1; }
#endif
  if (!builtins(gpu) || !selectionAndMaterials(gpu) || !vertexProperties(gpu) ||
      !customRegistration(gpu) || !anchorsAndDepth(gpu) || !inheritedState(gpu) ||
      !callbacks(gpu) || !primitiveObservers(gpu) || !perspectiveFogAndScreenDoor(gpu) ||
      !transparentOrdering(gpu) || !clipAndCameraLimits(gpu) || !invalidControls(gpu) || !redefineBuiltinLast(gpu)) return 1;
  std::cout << "CoinRenderMarkerSetTest " << (gpu ? "GPU/CoinGL" : "capture/CPU")
            << " passed reference_pairs=" << referencePairs << " safe_extension_checks=" << extensionChecks << '\n';
  return 0;
}
