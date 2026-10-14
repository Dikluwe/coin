#include "CoinRenderBenchmarkAnimation.h"
#include <Inventor/SoDB.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>

#include <iostream>
#include <limits>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "Failed: " << #condition << " at " << __LINE__ << '\n'; return 1; } } while (0)

namespace {
using Animation = CoinRenderBenchmarkAnimation;
struct City {
  SoSeparator * root = new SoSeparator;
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  SoMaterial * materials[8];
  SoMaterial * global = new SoMaterial;
  SoCube * sharedCube = new SoCube;
  SoCube * groundCube = new SoCube;
  std::vector<SoSeparator *> buildings;
  std::vector<SbVec3f> basePositions;

  explicit City(size_t count = 100, bool translations = false) {
    root->ref();
    root->addChild(camera);
    global->diffuseColor.setValue(.3f, .4f, .5f);
    root->addChild(global);
    auto * ground = new SoSeparator;
    ground->addChild(new SoMaterial);
    auto * groundPosition = new SoTranslation;
    groundPosition->translation.setValue(0, -.25f, 0);
    ground->addChild(groundPosition);
    groundCube->width = groundCube->depth = 40;
    groundCube->height = .5f;
    ground->addChild(groundCube);
    root->addChild(ground);
    sharedCube->width = sharedCube->height = sharedCube->depth = 1;
    for (int i = 0; i < 8; ++i) {
      materials[i] = new SoMaterial;
      materials[i]->diffuseColor.setValue(.2f + i * .07f, .3f, .6f - i * .04f);
      materials[i]->shininess = .2f;
    }
    for (size_t i = 0; i < count; ++i) {
      auto * building = new SoSeparator;
      building->addChild(materials[i % 8]);
      const SbVec3f position(float(i % 10) * 3, 2 + float(i % 3), float(i / 10) * 3);
      basePositions.push_back(position);
      if (translations) {
        auto * translation = new SoTranslation;
        translation->translation.setValue(position);
        building->addChild(translation);
      } else {
        auto * transform = new SoTransform;
        transform->translation.setValue(position);
        transform->scaleFactor.setValue(1 + float(i % 4) * .1f, 4 + float(i % 3), 1.1f);
        building->addChild(transform);
      }
      building->addChild(sharedCube);
      root->addChild(building);
      buildings.push_back(building);
    }
    camera->orientation.setValue(SbRotation(SbVec3f(0, 0, -1), SbVec3f(-.5f, -.35f, -1)));
    camera->viewAll(root, SbViewportRegion(640, 640), 1.15f);
  }
  ~City() { if (root) root->unref(); }
  bool selected(size_t ordinal, size_t selectedCount) const {
    for (size_t j = 0; j < selectedCount; ++j)
      if (((2 * j + 1) * buildings.size()) / (2 * selectedCount) == ordinal) return true;
    return false;
  }
  SbVec3f position(size_t i) const {
    SoNode * node = buildings[i]->getChild(1);
    return node->getTypeId() == SoTransform::getClassTypeId()
      ? static_cast<SoTransform *>(node)->translation.getValue()
      : static_cast<SoTranslation *>(node)->translation.getValue();
  }
};

int testObjectModes() {
  uint64_t selectionByFraction[3] = {};
  const unsigned fractions[] = {1, 10, 100};
  for (Animation::Mode mode : {Animation::Mode::TRANSFORMS, Animation::Mode::MATERIALS, Animation::Mode::GEOMETRY}) {
    for (unsigned f = 0; f < 3; ++f) {
      City city;
      Animation animation;
      std::string error;
      CHECK(animation.initialize(city.root, city.camera, mode, fractions[f], error));
      CHECK(error.empty() && animation.eligibleCount() == 100);
      CHECK(animation.selectedCount() == fractions[f]);
      CHECK(animation.preparedCloneCount() == (mode == Animation::Mode::TRANSFORMS ? 0 : fractions[f]));
      if (mode == Animation::Mode::TRANSFORMS) selectionByFraction[f] = animation.selectionDigest();
      else CHECK(animation.selectionDigest() == selectionByFraction[f]);
      const uint64_t baseDigest = animation.stateDigest();
      const SbColor globalColor = city.global->diffuseColor[0];
      uint64_t frames[3];
      const int64_t indices[] = {0, 100, 600};
      for (unsigned frame = 0; frame < 3; ++frame) {
        animation.update(indices[frame]);
        frames[frame] = animation.stateDigest();
        CHECK(frames[frame] != baseDigest);
        CHECK(city.global->diffuseColor[0] == globalColor && city.groundCube->height.getValue() == .5f);
        CHECK(city.sharedCube->width.getValue() == 1 && city.sharedCube->height.getValue() == 1 && city.sharedCube->depth.getValue() == 1);
        for (size_t i = 0; i < city.buildings.size(); ++i) {
          const bool selected = city.selected(i, animation.selectedCount());
          CHECK((city.buildings[i]->getChild(0) != city.materials[i % 8]) == (selected && mode == Animation::Mode::MATERIALS));
          CHECK((city.buildings[i]->getChild(2) != city.sharedCube) == (selected && mode == Animation::Mode::GEOMETRY));
          if (mode != Animation::Mode::TRANSFORMS || !selected) CHECK(city.position(i) == city.basePositions[i]);
          CHECK(city.materials[i % 8]->diffuseColor[0] == SbColor(.2f + int(i % 8) * .07f, .3f, .6f - int(i % 8) * .04f));
        }
      }
      CHECK(frames[0] != frames[1] && frames[1] != frames[2] && frames[0] != frames[2]);
      animation.restore();
      CHECK(animation.stateDigest() == baseDigest);
      animation.update(100);
      CHECK(animation.stateDigest() == frames[1]);
      animation.restore();
      CHECK(animation.stateDigest() == baseDigest);
    }
  }
  City odd(101, true);
  Animation animation;
  std::string error;
  CHECK(animation.initialize(odd.root, odd.camera, Animation::Mode::TRANSFORMS, 1, error));
  CHECK(animation.eligibleCount() == 101 && animation.selectedCount() == 2);
  animation.update(100);
  animation.restore();
  for (size_t i = 0; i < odd.buildings.size(); ++i) CHECK(odd.position(i) == odd.basePositions[i]);
  return 0;
}

int testDeterminismAndCamera() {
  for (Animation::Mode mode : {Animation::Mode::STATIC, Animation::Mode::CAMERA, Animation::Mode::TRANSFORMS,
                                Animation::Mode::MATERIALS, Animation::Mode::GEOMETRY}) {
    City first, second;
    Animation a, b;
    std::string error;
    CHECK(a.initialize(first.root, first.camera, mode, 10, error));
    CHECK(b.initialize(second.root, second.camera, mode, 10, error));
    CHECK(a.selectionDigest() == b.selectionDigest() && a.stateDigest() == b.stateDigest());
    const uint64_t initial = a.stateDigest();
    for (int64_t index : {-60, 0, 100, 600}) {
      a.update(index); b.update(index);
      CHECK(a.stateDigest() == b.stateDigest());
      if (mode == Animation::Mode::STATIC) CHECK(a.stateDigest() == initial);
    }
    if (mode == Animation::Mode::CAMERA) {
      CHECK(a.selectedCount() == 0 && a.preparedCloneCount() == 0 && a.stateDigest() != initial);
      CHECK((first.camera->position.getValue() - second.camera->position.getValue()).length() == 0);
    }
    a.restore(); b.restore();
    CHECK(a.stateDigest() == initial && b.stateDigest() == initial);
  }
  return 0;
}

int testRollbackAndLifetime() {
  City city;
  {
    Animation animation;
    std::string error;
    CHECK(animation.initialize(city.root, city.camera, Animation::Mode::MATERIALS, 10, error));
    animation.update(100);
    CHECK(animation.initialize(city.root, city.camera, Animation::Mode::GEOMETRY, 1, error));
    for (size_t i = 0; i < city.buildings.size(); ++i) CHECK(city.buildings[i]->getChild(0) == city.materials[i % 8]);
    animation.update(600);
  }
  for (size_t i = 0; i < city.buildings.size(); ++i) {
    CHECK(city.buildings[i]->getChild(0) == city.materials[i % 8]);
    CHECK(city.buildings[i]->getChild(2) == city.sharedCube);
    CHECK(city.position(i) == city.basePositions[i]);
  }
  City released;
  auto * held = static_cast<SoTransform *>(released.buildings[50]->getChild(1));
  held->ref();
  {
    Animation animation;
    std::string error;
    CHECK(animation.initialize(released.root, released.camera, Animation::Mode::TRANSFORMS, 1, error));
    animation.update(100);
    released.root->unref();
    released.root = nullptr; // The helper is now the scene's only owner.
    CHECK(held->translation.getValue() != released.basePositions[50]);
  }
  CHECK(held->translation.getValue() == released.basePositions[50]);
  held->unref();
  return 0;
}

int testInvalidInputs() {
  City city;
  Animation animation;
  Animation::Mode parsed = Animation::Mode::STATIC;
  CHECK(Animation::parseMode("geometry", parsed) && parsed == Animation::Mode::GEOMETRY);
  CHECK(!Animation::parseMode("unknown", parsed));
  CHECK(std::string(Animation::modeName(Animation::Mode::TRANSFORMS)) == "transforms");
  std::string error;
  CHECK(!animation.initialize(nullptr, city.camera, Animation::Mode::CAMERA, 10, error) && !error.empty());
  CHECK(!animation.initialize(city.root, nullptr, Animation::Mode::CAMERA, 10, error));
  CHECK(!animation.initialize(city.root, city.camera, Animation::Mode::TRANSFORMS, 0, error));
  CHECK(!animation.initialize(city.root, city.camera, Animation::Mode::TRANSFORMS, 101, error));
  auto * unknown = new SoSeparator;
  unknown->ref();
  unknown->addChild(new SoCube);
  CHECK(!animation.initialize(unknown, city.camera, Animation::Mode::MATERIALS, 10, error) && !error.empty());
  unknown->unref();
  const SbVec3f baseCamera = city.camera->position.getValue();
  city.camera->position.setValue(std::numeric_limits<float>::quiet_NaN(), 0, 0);
  CHECK(!animation.initialize(city.root, city.camera, Animation::Mode::CAMERA, 10, error));
  city.camera->position.setValue(baseCamera);
  auto * firstTransform = city.buildings[0]->getChild(1);
  city.buildings[1]->replaceChild(1, firstTransform);
  CHECK(!animation.initialize(city.root, city.camera, Animation::Mode::TRANSFORMS, 10, error));
  return 0;
}
}

int main() {
  SoDB::init();
  if (testObjectModes() || testDeterminismAndCamera() || testRollbackAndLifetime() || testInvalidInputs()) return 1;
  std::cout << "Benchmark animation selection, isolated mutations, deterministic replay and lifetime passed\n";
  return 0;
}
