#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include <cstdio>
#include <memory>
#include <vector>

#define CHECK(condition, reason) do { if (!(condition)) { \
  std::fprintf(stderr, "msaa_bump FAIL line %d: %s\n", __LINE__, reason); return false; \
} } while (0)

struct Snapshot {
  std::vector<uint8_t> color;
  std::vector<float> depth;
};

struct Fixture {
  SoSeparator * root;
  SoTexture2 * texture;
};

static Fixture makeFixture(unsigned variant) {
  auto * root = new SoSeparator;
  root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 4);
  camera->height = 4;
  root->addChild(camera);
  auto * light = new SoDirectionalLight;
  light->direction.setValue(0, 0, -1);
  root->addChild(light);
  auto * hints = new SoShapeHints;
  hints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;
  root->addChild(hints);
  auto * material = new SoMaterial;
  material->diffuseColor.setValue(.8f, .2f, .1f);
  if (variant == 2) {
    material->specularColor.setValue(.6f, .6f, .6f);
    material->shininess = .2f;
  }
  root->addChild(material);
  auto * bump = new SoBumpMap;
  const unsigned char normal[] = {128, 128, 255};
  bump->image.setValue(SbVec2s(1, 1), 3, normal);
  root->addChild(bump);
  SoTexture2 * texture = nullptr;
  if (variant == 1) {
    texture = new SoTexture2;
    const unsigned char opaque[] = {128, 128, 128, 255};
    texture->image.setValue(SbVec2s(1, 1), 4, opaque);
    texture->model = SoTexture2::MODULATE;
    root->addChild(texture);
  }
  auto * rotation = new SoRotation;
  rotation->rotation.setValue(SbVec3f(0, 0, 1), .23f);
  root->addChild(rotation);
  root->addChild(new SoCube);
  return {root, texture};
}

static bool read(CoinRenderTarget * target, CoinRenderAction & action,
                 SoSeparator * scene, Snapshot & result) {
  action.apply(scene);
  target->readbackRGBA(result.color);
  target->readbackDepth(result.depth);
  if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
      result.color.size() != 64u * 64u * 4u || result.depth.size() != 64u * 64u) {
    std::fprintf(stderr, "msaa_bump read status=%d color=%zu depth=%zu error=%s\n",
                 int(action.getLastStatus()), result.color.size(), result.depth.size(),
                 action.getLastError().getString());
    return false;
  }
  return true;
}

static bool validate(const Snapshot & shot, uint32_t samples, float & plane) {
  CHECK(shot.color.size() == 64u * 64u * 4u && shot.depth.size() == 64u * 64u,
        "complete color/depth readback");
  plane = shot.depth[32u * 64u + 32u];
  CHECK(plane > 0.0f && plane < 1.0f, "center depth must be on cube front plane");
  size_t covered = 0, empty = 0, partial = 0;
  for (size_t i = 0; i < shot.depth.size(); ++i) {
    const uint8_t alpha = shot.color[4u * i + 3u];
    if (alpha == 0) {
      ++empty;
      if (shot.depth[i] != 1.0f) {
        std::fprintf(stderr, "msaa_bump depth mismatch sample=%u xy=(%zu,%zu) alpha=0 depth=%.9g\n",
                     samples, i % 64u, i / 64u, shot.depth[i]);
        return false;
      }
    } else {
      ++covered;
      if (alpha < 255) ++partial;
      if (shot.depth[i] != plane) {
        std::fprintf(stderr, "msaa_bump depth mismatch sample=%u xy=(%zu,%zu) alpha=%u depth=%.9g plane=%.9g\n",
                     samples, i % 64u, i / 64u, unsigned(alpha), shot.depth[i], plane);
        return false;
      }
    }
  }
  std::printf("msaa_bump coverage samples=%u covered=%zu empty=%zu partial=%zu depth=%.9g\n",
              samples, covered, empty, partial, plane);
  return covered > 0 && empty > 0 && (samples == 1 || partial > 0);
}

static bool capture(Fixture fixture, unsigned variant, uint32_t samples,
                    Snapshot & shot) {
  CoinRenderOptions options;
  options.sampleCount = samples;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(64, 64), options));
  CHECK(target && target->setDepthReadbackEnabled(TRUE), "depth-enabled target");
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  action.setRenderTarget(target.get());
  CHECK(read(target.get(), action, fixture.root, shot), "bump capture");
  float plane = 1.0f;
  CHECK(validate(shot, samples, plane), "color/depth coverage");
  CHECK(target->getLastSubmissionSerial() > 0, "published serial");
  std::printf("msaa_bump variant=%u samples=%u center=(%u,%u,%u,%u) serial=%llu\n",
              variant, samples, unsigned(shot.color[(32u*64u+32u)*4u]),
              unsigned(shot.color[(32u*64u+32u)*4u+1u]),
              unsigned(shot.color[(32u*64u+32u)*4u+2u]),
              unsigned(shot.color[(32u*64u+32u)*4u+3u]),
              static_cast<unsigned long long>(target->getLastSubmissionSerial()));
  action.setRenderTarget(nullptr);
  return true;
}

static bool transactional(Fixture fixture, unsigned variant, uint32_t samples) {
  CoinRenderOptions options;
  options.sampleCount = samples;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(64, 64), options));
  CHECK(target && target->setDepthReadbackEnabled(TRUE), "transaction target");
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  action.setRenderTarget(target.get());
  Snapshot original, after;
  CHECK(read(target.get(), action, fixture.root, original), "transaction baseline");
  const uint64_t serial = target->getLastSubmissionSerial();
  coin_wgpu_inject_fault(COIN_WGPU_FAULT_MSAA_AFTER_ENCODE);
  action.apply(fixture.root);
  coin_wgpu_inject_fault(0);
  target->readbackRGBA(after.color);
  target->readbackDepth(after.depth);
  CHECK(action.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
        after.color == original.color && after.depth == original.depth &&
        target->getLastSubmissionSerial() == serial,
        "fault 307 must preserve color/depth/serial");
  CHECK(read(target.get(), action, fixture.root, after) &&
        after.color == original.color && after.depth == original.depth &&
        target->getLastSubmissionSerial() > serial,
        "retry after fault 307");
  if (fixture.texture) {
    const unsigned char translucent[] = {128, 128, 128, 128};
    fixture.texture->image.setValue(SbVec2s(1, 1), 4, translucent);
    const uint64_t stable = target->getLastSubmissionSerial();
    action.apply(fixture.root);
    target->readbackRGBA(after.color);
    target->readbackDepth(after.depth);
    const bool rejected = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                          after.color == original.color && after.depth == original.depth &&
                          target->getLastSubmissionSerial() == stable;
    const unsigned char opaque[] = {128, 128, 128, 255};
    fixture.texture->image.setValue(SbVec2s(1, 1), 4, opaque);
    CHECK(rejected, "translucent bump composition rejects before publication");
    CHECK(read(target.get(), action, fixture.root, after) &&
          after.color == original.color && after.depth == original.depth &&
          target->getLastSubmissionSerial() > stable,
          "recovery after unsupported composition");
  }
  action.setRenderTarget(nullptr);
  std::printf("msaa_bump transaction variant=%u samples=%u PASS\n", variant, samples);
  return true;
}

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  for (unsigned variant = 0; variant < 3; ++variant) {
    Fixture fixture = makeFixture(variant);
    Snapshot one, two, four;
    const bool passed = capture(fixture, variant, 1, one) &&
                        capture(fixture, variant, 2, two) &&
                        capture(fixture, variant, 4, four);
    if (!passed) { fixture.root->unref(); return 1; }
    const size_t center = (32u * 64u + 32u) * 4u;
    bool sameInterior = one.depth[center / 4u] == two.depth[center / 4u] &&
                        one.depth[center / 4u] == four.depth[center / 4u];
    for (size_t channel = 0; channel < 4; ++channel)
      sameInterior &= one.color[center + channel] == two.color[center + channel] &&
                      one.color[center + channel] == four.color[center + channel];
    bool changedEdges = false;
    for (size_t i = 0; i < one.color.size(); ++i)
      changedEdges |= one.color[i] != four.color[i];
    if (!sameInterior || !changedEdges ||
        !transactional(fixture, variant, 2) ||
        !transactional(fixture, variant, 4)) {
      std::fprintf(stderr, "msaa_bump variant=%u interior=%d changed_edges=%d\n",
                   variant, int(sameInterior), int(changedEdges));
      fixture.root->unref(); return 1;
    }
    fixture.root->unref();
  }
  return 0;
}
