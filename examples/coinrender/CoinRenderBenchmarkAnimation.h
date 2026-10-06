#ifndef COIN_RENDER_BENCHMARK_ANIMATION_H
#define COIN_RENDER_BENCHMARK_ANIMATION_H

#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/misc/SoChildList.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

// Benchmark-only mutations, independent of the render backend. Initialize
// after viewAll, outside the update timer. Logical time is frameIndex / 60.0;
// negative indices are supported for warmup. Digests belong outside timers.
class CoinRenderBenchmarkAnimation {
public:
  enum class Mode { STATIC, CAMERA, TRANSFORMS, MATERIALS, GEOMETRY };

  CoinRenderBenchmarkAnimation() = default;
  CoinRenderBenchmarkAnimation(const CoinRenderBenchmarkAnimation &) = delete;
  CoinRenderBenchmarkAnimation & operator=(const CoinRenderBenchmarkAnimation &) = delete;
  ~CoinRenderBenchmarkAnimation() { release(); }

  static bool parseMode(const std::string & text, Mode & result) {
    if (text == "static") result = Mode::STATIC;
    else if (text == "camera") result = Mode::CAMERA;
    else if (text == "transforms") result = Mode::TRANSFORMS;
    else if (text == "materials") result = Mode::MATERIALS;
    else if (text == "geometry") result = Mode::GEOMETRY;
    else return false;
    return true;
  }
  static const char * modeName(Mode value) {
    switch (value) {
    case Mode::STATIC: return "static";
    case Mode::CAMERA: return "camera";
    case Mode::TRANSFORMS: return "transforms";
    case Mode::MATERIALS: return "materials";
    case Mode::GEOMETRY: return "geometry";
    }
    return "unknown";
  }

  bool initialize(SoNode * root, SoCamera * fittedCamera, Mode requestedMode,
                  unsigned percent, std::string & diagnostic) {
    // Reinitialization may name the old scene after its caller has unref'd it.
    // Keep the incoming arguments alive while releasing the previous setup.
    struct IncomingLifetime {
      SoNode * root;
      SoCamera * camera;
      IncomingLifetime(SoNode * r, SoCamera * c) : root(r), camera(c) {
        if (root) root->ref();
        if (camera) camera->ref();
      }
      ~IncomingLifetime() {
        if (camera) camera->unref();
        if (root) root->unref();
      }
    } incoming(root, fittedCamera);
    release();
    diagnostic.clear();
    if (!root || !fittedCamera || percent < 1 || percent > 100 ||
        requestedMode < Mode::STATIC || requestedMode > Mode::GEOMETRY) {
      diagnostic = "Animation requires a scene, fitted camera and object percent in [1,100]";
      return false;
    }
    bool finiteOrientation = true;
    for (int c = 0; c < 4; ++c)
      finiteOrientation = finiteOrientation && std::isfinite(fittedCamera->orientation.getValue().getValue()[c]);
    if (fittedCamera->position.isConnected() || fittedCamera->position.isIgnored() ||
        !finiteOrientation ||
        !finite(fittedCamera->position.getValue()) ||
        !std::isfinite(fittedCamera->focalDistance.getValue()) ||
        fittedCamera->focalDistance.getValue() <= 0) {
      diagnostic = "Animation requires a finite fitted camera with an unconnected position";
      return false;
    }
    std::vector<Object> objects;
    std::vector<SoNode *> pending(1, root);
    std::unordered_set<SoNode *> visited;
    while (!pending.empty()) {
      SoNode * node = pending.back();
      pending.pop_back();
      if (!visited.insert(node).second) continue;
      Object object;
      if (recognize(node, object)) objects.push_back(object);
      const SoChildList * children = node->getChildren();
      if (children) for (int i = children->getLength() - 1; i >= 0; --i)
        pending.push_back((*children)[i]);
    }
    const bool objectMode = requestedMode == Mode::TRANSFORMS ||
      requestedMode == Mode::MATERIALS || requestedMode == Mode::GEOMETRY;
    if (objectMode && objects.empty()) {
      diagnostic = "Object animation requires city building Separators with local Material, Translation/Transform and Cube; ground and global materials are excluded";
      return false;
    }
    if (requestedMode == Mode::TRANSFORMS) {
      std::unordered_set<SoSFVec3f *> positions;
      for (const Object & object : objects) if (!positions.insert(&positionField(object)).second) {
        diagnostic = "Individual transform animation requires unshared building position nodes";
        return false;
      }
    }
    this->root = root;
    this->camera = fittedCamera;
    root->ref();
    camera->ref();
    animationMode = requestedMode;
    requestedPercent = percent;
    eligible = objects.size();
    baseCameraPosition = camera->position.getValue();
    baseCameraPositionDefault = camera->position.isDefault();
    camera->orientation.getValue().multVec(SbVec3f(1, 0, 0), cameraRight);
    camera->orientation.getValue().multVec(SbVec3f(0, 1, 0), cameraUp);
    camera->orientation.getValue().multVec(SbVec3f(0, 0, -1), cameraForward);
    cameraDistance = camera->focalDistance.getValue();
    const size_t count = objectMode ? (eligible * percent + 99) / 100 : 0;
    selectionHash = offsetBasis;
    hashInteger(selectionHash, eligible);
    hashInteger(selectionHash, count);
    selected.reserve(count);
    for (size_t index = 0; index < count; ++index) {
      // Midpoints of equal ordinal intervals distribute selections throughout
      // the imported city, instead of animating only its first row or block.
      const size_t ordinal = ((2 * index + 1) * eligible) / (2 * count);
      Object object = objects[ordinal];
      object.ordinal = ordinal;
      object.phase = float((ordinal * UINT64_C(2654435761)) % 65536) *
                     (6.2831853071795864769f / 65536.0f);
      hashInteger(selectionHash, ordinal);
      if (animationMode == Mode::MATERIALS || animationMode == Mode::GEOMETRY) {
        const int child = animationMode == Mode::MATERIALS ? object.materialChild : object.cubeChild;
        object.original = object.parent->getChild(child);
        object.original->ref();
        object.clone = object.original->copy(FALSE);
        object.clone->ref();
        object.parent->replaceChild(child, object.clone);
        if (animationMode == Mode::MATERIALS) object.material = static_cast<SoMaterial *>(object.clone);
        else object.cube = static_cast<SoCube *>(object.clone);
      }
      selected.push_back(object);
    }
    return true;
  }

  void update(int64_t frameIndex) {
    if (!root || animationMode == Mode::STATIC) return;
    const double t = double(frameIndex) / 60.0;
    if (animationMode == Mode::CAMERA) {
      // Visible pan/dolly relative to the fitted view, independent of city size.
      camera->position.setValue(baseCameraPosition +
        cameraRight * float(cameraDistance * .10 * std::sin(t * 1.1)) +
        cameraUp * float(cameraDistance * .05 * std::sin(t * .7)) +
        cameraForward * float(cameraDistance * .03 * std::sin(t * .8)));
    } else for (Object & object : selected) {
      const double phase = object.phase;
      if (animationMode == Mode::TRANSFORMS) {
        positionField(object).setValue(object.position + SbVec3f(
          float(object.extent[0] * .20 * std::sin(t * 1.4 + phase)),
          float(object.extent[1] * .15 * std::sin(t * .9 + phase)),
          float(object.extent[2] * .20 * std::sin(t * 1.2 + phase + .6))));
      } else if (animationMode == Mode::MATERIALS) {
        SbColor color;
        for (int c = 0; c < 3; ++c)
          color[c] = std::max(0.0f, std::min(1.0f,
            object.color[c] + float(.18 * std::sin(t * 1.3 + phase + c * 2.0943951023931953))));
        object.material->diffuseColor.setValue(color);
      } else {
        object.cube->width.setValue(object.dimensions[0] * float(1 + .15 * std::sin(t * 1.3 + phase)));
        object.cube->height.setValue(object.dimensions[1] * float(1 + .25 * std::sin(t * 1.1 + phase + .4)));
        object.cube->depth.setValue(object.dimensions[2] * float(1 + .12 * std::sin(t * .9 + phase + .8)));
      }
    }
    changed = true;
  }

  // Keep the prepared per-occurrence clones installed between backend runs.
  // Destruction/reinitialization also restores the original child pointers.
  void restore() {
    if (!root || !changed) return;
    if (animationMode == Mode::CAMERA) {
      camera->position.setValue(baseCameraPosition);
      camera->position.setDefault(baseCameraPositionDefault);
    } else for (Object & object : selected) {
      if (animationMode == Mode::TRANSFORMS) {
        positionField(object).setValue(object.position);
        positionField(object).setDefault(object.positionDefault);
      } else if (animationMode == Mode::MATERIALS) {
        object.material->diffuseColor.setValue(object.color);
        object.material->diffuseColor.setDefault(object.colorDefault);
      } else if (animationMode == Mode::GEOMETRY) {
        object.cube->width.setValue(object.dimensions[0]);
        object.cube->height.setValue(object.dimensions[1]);
        object.cube->depth.setValue(object.dimensions[2]);
        object.cube->width.setDefault(object.dimensionDefaults[0]);
        object.cube->height.setDefault(object.dimensionDefaults[1]);
        object.cube->depth.setDefault(object.dimensionDefaults[2]);
      }
    }
    changed = false;
  }

  size_t eligibleCount() const { return eligible; }
  size_t selectedCount() const { return selected.size(); }
  size_t preparedCloneCount() const {
    return animationMode == Mode::MATERIALS || animationMode == Mode::GEOMETRY ? selected.size() : 0;
  }
  unsigned objectPercent() const { return requestedPercent; }
  Mode mode() const { return animationMode; }
  uint64_t selectionDigest() const { return selectionHash; }
  uint64_t stateDigest() const {
    uint64_t result = offsetBasis;
    hashInteger(result, selectionHash);
    if (camera) {
      for (int c = 0; c < 3; ++c) hashFloat(result, camera->position.getValue()[c]);
      for (int c = 0; c < 4; ++c) hashFloat(result, camera->orientation.getValue().getValue()[c]);
      hashFloat(result, camera->focalDistance.getValue());
    }
    for (const Object & object : selected) {
      hashInteger(result, object.ordinal);
      for (int c = 0; c < 3; ++c) {
        hashFloat(result, positionField(object).getValue()[c]);
        hashFloat(result, object.material->diffuseColor[0][c]);
      }
      hashFloat(result, object.cube->width.getValue());
      hashFloat(result, object.cube->height.getValue());
      hashFloat(result, object.cube->depth.getValue());
    }
    return result;
  }

private:
  struct Object {
    SoSeparator * parent = nullptr;
    SoTranslation * translation = nullptr;
    SoTransform * transform = nullptr;
    SoMaterial * material = nullptr;
    SoCube * cube = nullptr;
    SoNode * original = nullptr;
    SoNode * clone = nullptr;
    int materialChild = -1, cubeChild = -1;
    size_t ordinal = 0;
    SbVec3f position, extent;
    SbColor color;
    float dimensions[3] = {};
    float phase = 0;
    SbBool positionDefault = FALSE, colorDefault = FALSE;
    SbBool dimensionDefaults[3] = {};
  };
  static bool finite(const SbVec3f & value) {
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
  }
  static SoSFVec3f & positionField(const Object & object) {
    return object.translation ? object.translation->translation : object.transform->translation;
  }
  static bool recognize(SoNode * node, Object & object) {
    if (node->getTypeId() != SoSeparator::getClassTypeId()) return false;
    auto * group = static_cast<SoSeparator *>(node);
    if (group->getNumChildren() != 3) return false;
    int positionChild = -1;
    for (int i = 0; i < 3; ++i) {
      SoNode * child = group->getChild(i);
      const SoType type = child->getTypeId();
      if (type == SoMaterial::getClassTypeId()) {
        object.material = static_cast<SoMaterial *>(child); object.materialChild = i;
      } else if (type == SoTranslation::getClassTypeId()) {
        object.translation = static_cast<SoTranslation *>(child); positionChild = i;
      } else if (type == SoTransform::getClassTypeId()) {
        object.transform = static_cast<SoTransform *>(child); positionChild = i;
      } else if (type == SoCube::getClassTypeId()) {
        object.cube = static_cast<SoCube *>(child); object.cubeChild = i;
      } else return false;
    }
    if (!object.material || !object.cube || positionChild < 0 ||
        positionChild > object.cubeChild || object.materialChild > object.cubeChild ||
        object.material->diffuseColor.getNum() != 1) return false;
    SoSFVec3f & position = positionField(object);
    if (position.isConnected() || position.isIgnored() ||
        object.material->diffuseColor.isConnected() || object.material->diffuseColor.isIgnored() ||
        object.cube->width.isConnected() || object.cube->height.isConnected() || object.cube->depth.isConnected() ||
        object.cube->width.isIgnored() || object.cube->height.isIgnored() || object.cube->depth.isIgnored()) return false;
    object.position = position.getValue();
    object.color = object.material->diffuseColor[0];
    object.dimensions[0] = object.cube->width.getValue();
    object.dimensions[1] = object.cube->height.getValue();
    object.dimensions[2] = object.cube->depth.getValue();
    object.extent.setValue(object.dimensions);
    if (object.transform) {
      const SbVec3f & scale = object.transform->scaleFactor.getValue();
      for (int c = 0; c < 3; ++c) object.extent[c] *= std::abs(scale[c]);
    }
    // The city ground has its center below y=0; buildings have positive centers.
    if (!finite(object.position) || !finite(object.extent) || !finite(object.color) ||
        object.position[1] <= 0 || object.extent[0] <= 0 || object.extent[1] <= 0 || object.extent[2] <= 0) return false;
    object.parent = group;
    object.positionDefault = position.isDefault();
    object.colorDefault = object.material->diffuseColor.isDefault();
    object.dimensionDefaults[0] = object.cube->width.isDefault();
    object.dimensionDefaults[1] = object.cube->height.isDefault();
    object.dimensionDefaults[2] = object.cube->depth.isDefault();
    return true;
  }
  void release() {
    restore();
    for (Object & object : selected) if (object.clone) {
      object.parent->replaceChild(animationMode == Mode::MATERIALS ? object.materialChild : object.cubeChild,
                                  object.original);
      object.clone->unref();
      object.original->unref();
    }
    selected.clear();
    if (camera) camera->unref();
    if (root) root->unref();
    camera = nullptr; root = nullptr; eligible = 0;
    changed = false; selectionHash = offsetBasis;
    animationMode = Mode::STATIC; requestedPercent = 100;
  }
  static void hashInteger(uint64_t & hash, uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte) {
      hash = (hash ^ uint8_t(value >> (byte * 8))) * UINT64_C(1099511628211);
    }
  }
  static void hashFloat(uint64_t & hash, float value) {
    uint32_t bits = 0;
    if (value != 0) std::memcpy(&bits, &value, sizeof(bits));
    hashInteger(hash, bits);
  }
  static constexpr uint64_t offsetBasis = UINT64_C(14695981039346656037);
  SoNode * root = nullptr;
  SoCamera * camera = nullptr;
  Mode animationMode = Mode::STATIC;
  unsigned requestedPercent = 100;
  size_t eligible = 0;
  uint64_t selectionHash = offsetBasis;
  bool changed = false;
  SbVec3f baseCameraPosition, cameraRight, cameraUp, cameraForward;
  float cameraDistance = 1;
  SbBool baseCameraPositionDefault = FALSE;
  std::vector<Object> selected;
};

#endif
