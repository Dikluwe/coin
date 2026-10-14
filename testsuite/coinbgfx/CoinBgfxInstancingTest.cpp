#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "../coinrender/CoinRenderTestEnvironment.h"
#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"

#include <Inventor/SoDB.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/actions/SoGLRenderAction.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool check(bool condition, const std::string & message) {
  if (!condition) std::cerr << "CoinBgfxInstancingTest: " << message << '\n';
  return condition;
}

class EnvironmentScope {
public:
  EnvironmentScope(const char * key, const char * value) : name(key) {
    const char * existing = std::getenv(key);
    hadValue = existing != nullptr;
    if (existing) previous = existing;
    coinRenderTestSetEnvironment(name.c_str(), value);
  }
  ~EnvironmentScope() {
    coinRenderTestSetEnvironment(name.c_str(), hadValue ? previous.c_str() : nullptr);
  }
  void set(const char * value) { coinRenderTestSetEnvironment(name.c_str(), value); }
private:
  std::string name, previous;
  bool hadValue = false;
};

CoinRenderFramePlan makeFrame(int width, int height) {
  CoinRenderFramePlan frame;
  frame.revision = 1000;
  frame.clearColor = SbColor4f(.03f, .045f, .065f, 1);
  frame.materials.resize(2);
  const float colors[2][3] = {{.75f,.18f,.11f}, {.11f,.3f,.78f}};
  for (size_t material = 0; material < 2; ++material) {
    auto & snapshot = frame.materials[material];
    for (int channel = 0; channel < 3; ++channel) {
      snapshot.diffuse[channel] = colors[material][channel];
      snapshot.ambient[channel] = colors[material][channel] * .25f;
      snapshot.specular[channel] = .16f + .025f * channel;
      snapshot.emission[channel] = .005f * float(material + channel);
    }
    snapshot.shininess = material ? .55f : .18f;
  }

  SbMatrix view;
  view.setRotate(SbRotation(SbVec3f(.2f,.7f,1), .13f));
  view[3][0] = .015f; view[3][1] = -.025f; view[3][2] = -.08f;
  SbViewVolume volume;
  volume.ortho(-1, 1, -1, 1, .1f, 4);
  SbMatrix unusedView, projection;
  volume.getMatrices(unusedView, projection);
  CoinRenderCameraSnapshot camera;
  camera.isPerspective = false;
  camera.nearDistance = .1f; camera.farDistance = 4;
  camera.viewMatrix = view; camera.projectionMatrixCoin = projection;
  frame.cameras.push_back(camera);
  CoinRenderViewportSnapshot viewport;
  viewport.width = width; viewport.height = height;
  frame.viewports.push_back(viewport);

  CoinRenderLightingSnapshot lighting;
  lighting.ambientIntensity = .25f;
  lighting.ambientColor[0] = .85f; lighting.ambientColor[1] = .9f;
  SbMatrix lightModel;
  lightModel.setRotate(SbRotation(SbVec3f(0,1,0), .23f));
  lightModel[3][0] = -.2f; lightModel[3][1] = .35f; lightModel[3][2] = .4f;
  const SbMatrix lightToEye = lightModel * view;
  CoinRenderLightSourceSnapshot directional;
  directional.sourceRevision = 17;
  directional.sourceModel = lightToEye;
  directional.intensity = .6f;
  directional.color[0] = .95f; directional.color[1] = .88f;
  SbVec3f direction;
  lightToEye.multDirMatrix(SbVec3f(-.35f,-.25f,-1), direction);
  direction.normalize();
  direction.getValue(directional.direction[0], directional.direction[1], directional.direction[2]);
  lighting.lights.push_back(directional);
  CoinRenderLightSourceSnapshot point;
  point.type = CoinRenderLightType::POINT;
  point.sourceRevision = 18; point.sourceModel = lightToEye;
  point.intensity = .45f;
  point.color[0] = .7f; point.color[1] = .85f;
  point.attenuation[0] = .08f; point.attenuation[1] = .12f; point.attenuation[2] = 1;
  SbVec3f position;
  lightToEye.multVecMatrix(SbVec3f(.15f,.2f,.4f), position);
  position.getValue(point.position[0], point.position[1], point.position[2]);
  lighting.lights.push_back(point);
  frame.lightingStates.push_back(lighting);

  // Four source ranges: two distinct meshes, each captured with two materials.
  // Material alternates within each consecutive mesh group, so batching must
  // emit two ordered draws without moving a shape across another geometry.
  const float positions[2][4][3] = {
    {{-.5f,-.5f,0}, {.5f,-.5f,0}, {.5f,.5f,0}, {-.5f,.5f,0}},
    {{-.48f,-.4f,-.12f}, {.5f,-.45f,.1f}, {.3f,.5f,.16f}, {-.4f,.35f,-.08f}}
  };
  const float normals[4][3] = {{-.2f,.3f,.9f}, {.35f,-.1f,.8f}, {.1f,.45f,.85f}, {0,0,0}};
  const uint32_t indices[] = {0,1,2,0,2,3};
  for (uint32_t mesh = 0; mesh < 2; ++mesh) for (uint32_t material = 0; material < 2; ++material) {
    const uint32_t first = static_cast<uint32_t>(frame.vertices.size());
    for (uint32_t corner = 0; corner < 4; ++corner) {
      CoinRenderVertexSnapshot vertex;
      for (int channel = 0; channel < 3; ++channel) {
        vertex.position[channel] = positions[mesh][corner][channel];
        vertex.normal[channel] = normals[corner][channel];
      }
      vertex.materialSlot = material;
      frame.vertices.push_back(vertex);
    }
    for (uint32_t index : indices) frame.indices.push_back(first + index);
  }
  for (uint32_t occurrence = 0; occurrence < 400; ++occurrence) {
    const uint32_t mesh = occurrence / 200, material = occurrence % 2;
    const uint32_t range = mesh * 2 + material;
    CoinRenderRenderStateSnapshot state;
    const float x = -.82f + float(occurrence % 20) * .086f;
    const float y = -.82f + float(occurrence / 20) * .086f;
    const float sx = .051f + float(occurrence % 3) * .006f;
    const float sy = .043f + float(occurrence % 5) * .004f;
    const float sz = .055f + float(occurrence % 7) * .003f;
    state.model = SbMatrix(sx,.009f,.004f,0, -.006f,sy,.008f,0,
                          .012f,-.01f,sz,0, x,y,-1.25f-float(occurrence % 4)*.012f,1);
    state.view = view; state.projectionCoin = projection;
    state.materialSlot = material;
    state.lightModel = CoinRenderLightModel::PHONG;
    state.cullMode = CoinRenderCullMode::NONE;
    state.transparencyType = SoGLRenderAction::NONE;
    frame.renderStates.push_back(state);
    CoinRenderDrawPacket draw;
    draw.renderStateSlot = occurrence;
    draw.frameNodeOrdinal = occurrence + 1;
    draw.geometry.firstVertex = range * 4; draw.geometry.vertexCount = 4;
    draw.geometry.firstIndex = range * 6; draw.geometry.indexCount = 6;
    frame.draws.push_back(draw);
  }
  return frame;
}

bool requireLoweringProfile(const CoinRenderFramePlan & frame, int width, int height,
                           bool eligible, const std::string & label) {
  for (const bool homogeneousDepth : {false, true}) {
    CoinBgfxPlan lowered;
    std::string diagnostic;
    const bool accepted = CoinBgfxLowering::lowerInstanced(
      frame, width, height, homogeneousDepth, lowered, diagnostic);
    if (!check(accepted == eligible, label + " instancing qualification mismatch: " + diagnostic)) return false;
    if (accepted && !check(lowered.usesInstancing && lowered.draws.size() == 2 &&
        lowered.instances.size() == 400 && lowered.instancedVertices.size() == 8 &&
        lowered.draws[0].firstInstance == 0 && lowered.draws[0].instanceCount == 200 &&
        lowered.draws[1].firstInstance == 200 && lowered.draws[1].instanceCount == 200,
        label + " must actively lower to two ordered mesh draws and 400 instances")) return false;
  }
  return true;
}

bool compareImages(const std::vector<uint8_t> & instanced, const std::vector<uint8_t> & general,
                   int width, int height, const std::string & label) {
  if (!check(instanced.size() == size_t(width) * height * 4 && general.size() == instanced.size(),
             label + " incomplete readback")) return false;
  uint64_t absoluteError = 0, changedPixels = 0, overTolerancePixels = 0;
  int maximum = 0;
  size_t worst = 0;
  bool alphaMatches = true;
  for (size_t pixel = 0; pixel < instanced.size() / 4; ++pixel) {
    int pixelMaximum = 0;
    for (size_t channel = 0; channel < 3; ++channel) {
      const int delta = std::abs(int(instanced[pixel * 4 + channel]) - int(general[pixel * 4 + channel]));
      absoluteError += static_cast<uint64_t>(delta);
      pixelMaximum = std::max(pixelMaximum, delta);
    }
    if (pixelMaximum) ++changedPixels;
    if (pixelMaximum > 3) ++overTolerancePixels;
    if (pixelMaximum > maximum) { maximum = pixelMaximum; worst = pixel; }
    alphaMatches = alphaMatches && instanced[pixel * 4 + 3] == general[pixel * 4 + 3];
  }
  const double mean = double(absoluteError) / double(size_t(width) * height * 3);
  std::cout << label << " max_rgb_error=" << maximum << " mean_rgb_error=" << mean
            << " changed_pixels=" << changedPixels << " pixels_over_3=" << overTolerancePixels
            << " worst_xy=" << worst % size_t(width) << ',' << worst / size_t(width)
            << " instanced_rgb=" << int(instanced[worst * 4]) << ',' << int(instanced[worst * 4 + 1])
            << ',' << int(instanced[worst * 4 + 2]) << " general_rgb=" << int(general[worst * 4])
            << ',' << int(general[worst * 4 + 1]) << ',' << int(general[worst * 4 + 2]) << '\n';
  // Every RGB channel is bounded: large edge or lighting differences are never
  // removed from the comparison. Mean error additionally catches broad drift.
  return check(maximum <= 3 && mean <= .15 && alphaMatches,
               label + " differs beyond RGB tolerance (max 3, mean .15; alpha exact)");
}

bool compareDepth(const std::vector<float> & instanced, const std::vector<float> & general,
                  int width, int height, const std::string & label) {
  if (!check(instanced.size() == size_t(width) * height && general.size() == instanced.size(),
             label + " incomplete depth readback")) return false;
  float maximum = 0.0f;
  for (size_t pixel = 0; pixel < instanced.size(); ++pixel) {
    if (!check(std::isfinite(instanced[pixel]) && std::isfinite(general[pixel]),
               label + " non-finite depth readback")) return false;
    maximum = std::max(maximum, std::abs(instanced[pixel] - general[pixel]));
  }
  std::cout << label << " max_depth_error=" << maximum << '\n';
  return check(maximum < 1e-5f, label + " depth differs from general lowering");
}

int render(CoinRenderTargetP & target, const CoinRenderFramePlan & frame,
           const CoinRenderFrameReuseDecision & reuse, std::vector<uint8_t> & pixels,
           const std::string & label, bool allowUnavailable = false) {
  const auto result = target.executeFrame(frame, reuse);
  if (allowUnavailable && result.status == CoinRenderBackendStatus::NOT_READY) {
    std::cout << "BGFX renderer unavailable: " << result.diagnostic << '\n';
    return 77;
  }
  if (!check(result.status == CoinRenderBackendStatus::SUCCESS, label + ": " + result.diagnostic)) return 1;
  target.readbackRGBA(pixels);
  return check(pixels.size() == size_t(target.size[0]) * target.size[1] * 4,
               label + " must publish a complete current RGBA frame") ? 0 : 1;
}

// Each comparison ends with an instanced (or deliberately declined) rebuild.
// Thus the next classified update exercises a real retained instanced base,
// rather than inadvertently testing a general cache left by the opt-out.
int compareRoutes(CoinRenderTargetP & target, CoinRenderFramePlan & frame,
                  const CoinRenderFrameReuseDecision & reuse, EnvironmentScope & optout,
                  const std::string & label, bool eligible, std::vector<uint8_t> & image,
                  bool allowUnavailable = false) {
  if (!requireLoweringProfile(frame, target.size[0], target.size[1], eligible, label)) return 1;
  optout.set("0");
  int result = render(target, frame, reuse, image, label + " enabled", allowUnavailable);
  if (result) return result;
  std::vector<float> instancedDepth;
  if (target.depthReadbackEnabled) target.readbackDepth(instancedDepth);
  const auto staticReuse = CoinRenderFrameReuseCore::classify(frame, frame);
  if (!check(staticReuse.kind == CoinRenderFrameReuseKind::REUSE, label + " static frame must classify as reuse")) return 1;
  std::vector<uint8_t> repeated;
  result = render(target, frame, staticReuse, repeated, label + " repeated");
  if (result || !check(repeated == image, label + " same-revision GPU cache changed publication")) return 1;
  if (target.depthReadbackEnabled) {
    std::vector<float> repeatedDepth;
    target.readbackDepth(repeatedDepth);
    if (!check(repeatedDepth == instancedDepth, label + " same-revision GPU cache changed depth")) return 1;
  }
  std::vector<uint8_t> general, restored;
  optout.set("1");
  ++frame.revision;
  const CoinRenderFrameReuseDecision rebuild(CoinRenderFrameReuseKind::FULL_REBUILD, 0);
  result = render(target, frame, rebuild, general, label + " optout");
  if (result || !compareImages(image, general, target.size[0], target.size[1], label)) return 1;
  if (target.depthReadbackEnabled) {
    std::vector<float> generalDepth;
    target.readbackDepth(generalDepth);
    if (!compareDepth(instancedDepth, generalDepth, target.size[0], target.size[1], label)) return 1;
  }
  optout.set("0");
  ++frame.revision;
  result = render(target, frame, rebuild, restored, label + " restored");
  if (result || !check(restored == image, label + " pipeline restoration changed publication")) return 1;
  if (target.depthReadbackEnabled) {
    std::vector<float> restoredDepth;
    target.readbackDepth(restoredDepth);
    if (!check(restoredDepth == instancedDepth, label + " pipeline restoration changed depth")) return 1;
  }
  return 0;
}

int coplanarLastWins(CoinRenderTargetP & target, EnvironmentScope & optout, uint64_t revision) {
  auto frame = makeFrame(target.size[0], target.size[1]);
  frame.revision = revision;
  for (auto & state : frame.renderStates) state.depthFunction = CoinRenderDepthFunction::LEQUAL;
  // These consecutive occurrences use the same mesh and opposite materials.
  // Their model transforms are byte-identical, including Z. LEQUAL must leave
  // the later material visible, preserving traversal within an instance batch.
  const size_t first = 190, last = 191;
  frame.renderStates[last].model = frame.renderStates[first].model;
  const CoinRenderFrameReuseDecision rebuild(CoinRenderFrameReuseKind::FULL_REBUILD, 0);
  std::vector<uint8_t> tieImage;
  int result = compareRoutes(target, frame, rebuild, optout, "LEQUAL coplanar materials", true, tieImage);
  if (result) return result;

  // Compare against an independently submitted general-path scene where
  // both coplanar occurrences already have the expected winning material.
  optout.set("1");
  auto winnerFrame = frame;
  ++winnerFrame.revision;
  winnerFrame.draws[first].geometry = winnerFrame.draws[last].geometry;
  winnerFrame.renderStates[first].materialSlot = winnerFrame.renderStates[last].materialSlot;
  std::vector<uint8_t> winnerImage;
  result = render(target, winnerFrame, rebuild, winnerImage, "coplanar last-material reference");
  if (result || !compareImages(tieImage, winnerImage, target.size[0], target.size[1],
                              "coplanar last occurrence must win")) return 1;

  auto loserFrame = frame;
  loserFrame.revision = winnerFrame.revision + 1;
  loserFrame.draws[last].geometry = loserFrame.draws[first].geometry;
  loserFrame.renderStates[last].materialSlot = loserFrame.renderStates[first].materialSlot;
  std::vector<uint8_t> loserImage;
  result = render(target, loserFrame, rebuild, loserImage, "coplanar first-material reference");
  if (result) return result;
  size_t visiblyDifferent = 0;
  for (size_t pixel = 0; pixel < tieImage.size() / 4; ++pixel) {
    int maximum = 0;
    for (size_t channel = 0; channel < 3; ++channel)
      maximum = std::max(maximum, std::abs(int(tieImage[pixel * 4 + channel]) -
                                         int(loserImage[pixel * 4 + channel])));
    visiblyDifferent += maximum > 20;
  }
  std::cout << "coplanar first-vs-last reference pixels_over_20=" << visiblyDifferent << '\n';
  return check(visiblyDifferent > 8,
               "coplanar winning material must be visibly different from the first occurrence") ? 0 : 1;
}

int clippedViewportDepthReadback(CoinRenderTargetP & target, EnvironmentScope & optout,
                                uint64_t revision) {
  auto frame = makeFrame(target.size[0], target.size[1]);
  frame.revision = revision;
  frame.clearColor = SbColor4f(0, 0, 0, 1);
  frame.viewports[0].x = target.size[0] + 16;
  frame.viewports[0].y = target.size[1] + 16;
  target.depthReadbackEnabled = true;
  const CoinRenderFrameReuseDecision rebuild(CoinRenderFrameReuseKind::FULL_REBUILD, 0);
  std::vector<uint8_t> clipped;
  // The plan must still qualify for 400 instances, while the executor skips
  // every geometry submit. Its fullscreen depth conversion must use its own
  // ordinary draw state after those skips.
  int result = compareRoutes(target, frame, rebuild, optout,
                             "outside viewport depth readback", true, clipped);
  if (result) return result;
  for (size_t pixel = 0; pixel < clipped.size() / 4; ++pixel)
    if (!check(clipped[pixel * 4] == 0 && clipped[pixel * 4 + 1] == 0 &&
               clipped[pixel * 4 + 2] == 0 && clipped[pixel * 4 + 3] == 255,
               "outside viewport must publish only the clear color")) return 1;
  if (!check(target.depthBuffer.size() == size_t(target.size[0]) * target.size[1] &&
             std::all_of(target.depthBuffer.begin(), target.depthBuffer.end(),
                         [](float depth) { return depth == 1.0f; }),
             "outside viewport must publish a complete depth clear of one")) return 1;

  frame.viewports[0].x = frame.viewports[0].y = 0;
  ++frame.revision;
  std::vector<uint8_t> visible;
  result = compareRoutes(target, frame, rebuild, optout,
                         "return from outside viewport with depth", true, visible);
  if (result || !check(visible != clipped, "restored viewport must render visible geometry")) return 1;
  return check(std::count_if(target.depthBuffer.begin(), target.depthBuffer.end(),
                            [](float depth) { return depth < 1.0f; }) > 100,
               "restored viewport must replace the depth clear with visible geometry") ? 0 : 1;
}
}

int main() {
  SoDB::init();
  EnvironmentScope optout("COIN_BGFX_DISABLE_INSTANCING", "0");
  EnvironmentScope batching("COIN_BGFX_DISABLE_DRAW_BATCHING", "0");
  EnvironmentScope readback("COIN_BGFX_READBACK_PIPELINE_DEPTH", "1");
  CoinRenderTargetP target(SbVec2i32(256, 256));
  target.depthReadbackEnabled = false;
  auto frame = makeFrame(256, 256);
  const CoinRenderFrameReuseDecision rebuild(CoinRenderFrameReuseKind::FULL_REBUILD, 0);
  std::vector<uint8_t> image;
  int result = compareRoutes(target, frame, rebuild, optout, "affine PHONG directional+point", true, image, true);
  if (result) return result;
  size_t red = 0, blue = 0;
  for (size_t pixel = 0; pixel < image.size() / 4; ++pixel) {
    red += int(image[pixel * 4]) > int(image[pixel * 4 + 2]) + 12;
    blue += int(image[pixel * 4 + 2]) > int(image[pixel * 4]) + 12;
  }
  if (!check(red > 100 && blue > 100, "both alternating material meshes must produce visible lit pixels")) return 1;

  auto previous = frame;
  const auto beforeCamera = image;
  ++frame.revision;
  SbMatrix cameraShift;
  cameraShift.setTranslate(SbVec3f(.065f,-.035f,0));
  frame.cameras[0].viewMatrix = previous.cameras[0].viewMatrix * cameraShift;
  for (auto & state : frame.renderStates) state.view = frame.cameras[0].viewMatrix;
  const auto cameraReuse = CoinRenderFrameReuseCore::classify(previous, frame);
  if (!check(cameraReuse.kind == CoinRenderFrameReuseKind::CAMERA_PATCH, "view-only update must classify as camera patch")) return 1;
  result = compareRoutes(target, frame, cameraReuse, optout, "classified camera update", true, image);
  if (result || !check(image != beforeCamera, "camera update must actually move visible geometry")) return 1;

  previous = frame;
  const auto beforeMaterial = image;
  ++frame.revision;
  frame.materials[0].diffuse[0] = .16f; frame.materials[0].diffuse[1] = .72f;
  frame.materials[1].diffuse[0] = .65f; frame.materials[1].diffuse[2] = .2f;
  frame.materials[0].shininess = .62f;
  const auto materialReuse = CoinRenderFrameReuseCore::classify(previous, frame);
  if (!check(materialReuse.kind == CoinRenderFrameReuseKind::RESOURCE_REBUILD, "material-only update must classify as resource rebuild")) return 1;
  result = compareRoutes(target, frame, materialReuse, optout, "classified material update", true, image);
  if (result || !check(image != beforeMaterial, "material rebuild must update visible instance colors")) return 1;

  if (!check(target.resize(SbVec2i32(320, 240)), "target resize failed")) return 1;
  ++frame.revision;
  frame.viewports[0].width = 320; frame.viewports[0].height = 240;
  result = compareRoutes(target, frame, rebuild, optout, "resized target", true, image);
  if (result) return result;

  const auto ordinaryStates = frame.renderStates;
  const auto beforeFog = image;
  ++frame.revision;
  for (auto & state : frame.renderStates) {
    state.fogMode = CoinRenderFogMode::HAZE;
    state.fogColor[0] = .7f; state.fogColor[1] = .8f; state.fogColor[2] = .9f;
    state.fogStart = .2f; state.fogEnd = 2;
  }
  result = compareRoutes(target, frame, rebuild, optout, "fog fallback", false, image);
  if (result || !check(image != beforeFog, "fog fallback must visibly affect pixels")) return 1;
  frame.renderStates = ordinaryStates;
  ++frame.revision;
  result = compareRoutes(target, frame, rebuild, optout, "return from fog", true, image);
  if (result || !check(image == beforeFog, "return from fog must restore the instanced image")) return 1;

  const auto beforeClip = image;
  ++frame.revision;
  for (auto & state : frame.renderStates)
    state.clipPlanesWorld.push_back(SbPlane(SbVec3f(1,0,0), 0));
  result = compareRoutes(target, frame, rebuild, optout, "clip fallback", false, image);
  if (result || !check(image != beforeClip, "clip fallback must remove visible geometry")) return 1;
  frame.renderStates = ordinaryStates;
  ++frame.revision;
  result = compareRoutes(target, frame, rebuild, optout, "return from clip", true, image);
  if (result || !check(image == beforeClip, "return from clip must restore the instanced image")) return 1;

  ++frame.revision;
  for (auto & state : frame.renderStates) state.depthFunction = CoinRenderDepthFunction::LEQUAL;
  result = compareRoutes(target, frame, rebuild, optout, "uniform LEQUAL PHONG", true, image);
  if (result) return result;
  result = coplanarLastWins(target, optout, frame.revision + 1);
  if (result) return result;
  result = clippedViewportDepthReadback(target, optout, frame.revision + 16);
  if (result) return result;
  std::cout << "CoinBgfxInstancingTest passed\n";
  return 0;
}
