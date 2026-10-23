#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include "rendering/coinrender/CoinRenderStrokeCore.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#define CHECK_MESSAGE(condition, message) do { \
  if (!(condition)) { \
    std::cerr << "Failed: " << (message) << " at " << __FILE__ << ':' << __LINE__ << '\n'; \
    return false; \
  } \
} while (0)

namespace {

CoinRenderMaterialSnapshot materialValue(uint32_t value)
{
  CoinRenderMaterialSnapshot result;
  result.diffuse[0] = float(value + 1) / 8193.0f;
  result.emission[2] = float(value % 257) / 257.0f;
  result.shininess = float(value % 1021) / 1021.0f;
  return result;
}

CoinRenderLightingSnapshot lightingValue(uint32_t value)
{
  CoinRenderLightingSnapshot result;
  result.ambientIntensity = float(value + 1) / 8193.0f;
  result.lights.emplace_back();
  result.lights[0].sourceRevision = uint64_t(value) + 1;
  return result;
}

CoinRenderCameraSnapshot cameraValue(uint32_t value)
{
  CoinRenderCameraSnapshot result;
  result.viewMatrix[3][0] = float(value);
  result.focalDistance = 5.0f + float(value) / 1024.0f;
  return result;
}

CoinRenderViewportSnapshot viewportValue(uint32_t value)
{
  CoinRenderViewportSnapshot result;
  result.x = int32_t(value);
  result.y = -int32_t(value);
  result.width = 64 + int32_t(value % 2048);
  result.height = 32 + int32_t(value % 1024);
  return result;
}

CoinRenderTextureImageSnapshot textureValue(uint32_t value)
{
  CoinRenderTextureImageSnapshot result;
  result.width = 1;
  result.height = 1;
  result.contentDigest = uint64_t(value) + 1;
  result.pixelsRgba = {
    uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)
  };
  return result;
}

CoinRenderSamplerSnapshot samplerValue(uint32_t value)
{
  CoinRenderSamplerSnapshot result;
  result.wrapS = (value & 1) ? CoinRenderTextureWrap::CLAMP : CoinRenderTextureWrap::REPEAT;
  result.wrapT = (value & 2) ? CoinRenderTextureWrap::CLAMP : CoinRenderTextureWrap::REPEAT;
  result.wrapR = (value & 4) ? CoinRenderTextureWrap::CLAMP : CoinRenderTextureWrap::REPEAT;
  result.filter = static_cast<CoinRenderTextureFilter>((value >> 3) & 3);
  result.maxAnisotropy = 1u << ((value >> 5) % 5);
  return result;
}

bool materialIdentity()
{
  CoinRenderFramePlan plan;
  CoinRenderMaterialSnapshot base;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, base) == 0, "first material slot");
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, base) == 0, "repeated material slot");

  std::vector<CoinRenderMaterialSnapshot> variants;
  for (int group = 0; group < 4; ++group) for (int channel = 0; channel < 4; ++channel) {
    auto changed = base;
    float * fields[] = {changed.ambient, changed.diffuse, changed.specular, changed.emission};
    fields[group][channel] += .03125f;
    variants.push_back(changed);
  }
  auto changed = base; changed.shininess += .03125f; variants.push_back(changed);
  changed = base; changed.transparency += .03125f; variants.push_back(changed);
  for (size_t i = 0; i < variants.size(); ++i)
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, variants[i]) == i + 1,
                  "every material field contributes to identity");
  for (size_t i = variants.size(); i-- > 0;)
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, variants[i]) == i + 1,
                  "material repeat preserves first slot");
  return true;
}

bool lightingIdentity()
{
  CoinRenderLightingSnapshot base;
  base.lights.emplace_back();
  std::vector<CoinRenderLightingSnapshot> variants;
  auto add = [&](CoinRenderLightingSnapshot changed) { variants.push_back(std::move(changed)); };
  auto changed = base; changed.ambientIntensity += .25f; add(changed);
  changed = base; changed.ambientColor[1] += .25f; add(changed);
  changed = base; changed.lights.emplace_back(); add(changed);
  changed = base; changed.lights[0].sourceRevision = 7; add(changed);
  changed = base; changed.lights[0].sourceModel[3][0] = 1; add(changed);
  changed = base; changed.lights[0].sourceWorldModel[3][1] = 1; add(changed);
  changed = base; changed.lights[0].hasAuthoredSource = true; add(changed);
  changed = base; changed.lights[0].authoredDirection[0] = 1; add(changed);
  changed = base; changed.lights[0].authoredPosition[1] = 1; add(changed);
  changed = base; changed.lights[0].type = CoinRenderLightType::POINT; add(changed);
  changed = base; changed.lights[0].color[2] = .5f; add(changed);
  changed = base; changed.lights[0].intensity = .5f; add(changed);
  changed = base; changed.lights[0].ambientIntensity = .5f; add(changed);
  changed = base; changed.lights[0].direction[0] = .5f; add(changed);
  changed = base; changed.lights[0].position[1] = .5f; add(changed);
  changed = base; changed.lights[0].cutOffAngle = .5f; add(changed);
  changed = base; changed.lights[0].dropOffRate = .5f; add(changed);
  changed = base; changed.lights[0].attenuation[0] = .5f; add(changed);

  CoinRenderFramePlan plan;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::lighting(plan, base) == 0, "first lighting slot");
  for (size_t i = 0; i < variants.size(); ++i)
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::lighting(plan, variants[i]) == i + 1,
                  "every lighting field contributes to identity");
  for (size_t i = variants.size(); i-- > 0;)
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::lighting(plan, variants[i]) == i + 1,
                  "lighting repeat preserves first slot");
  return true;
}

bool cameraIdentity()
{
  CoinRenderCameraSnapshot base;
  std::vector<CoinRenderCameraSnapshot> variants;
  auto changed = base; changed.viewMatrix[3][0] = 1; variants.push_back(changed);
  changed = base; changed.projectionMatrixCoin[2][1] = .25f; variants.push_back(changed);
  changed = base; changed.isPerspective = false; variants.push_back(changed);
  changed = base; changed.nearDistance = .2f; variants.push_back(changed);
  changed = base; changed.farDistance = 101; variants.push_back(changed);
  changed = base; changed.focalDistance = 6; variants.push_back(changed);
  changed = base; changed.aspectRatio = 1.25f; variants.push_back(changed);

  CoinRenderFramePlan plan;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::camera(plan, base) == 0, "first camera slot");
  for (size_t i = 0; i < variants.size(); ++i)
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::camera(plan, variants[i]) == i + 1,
                  "every camera field contributes to identity");
  for (size_t i = variants.size(); i-- > 0;)
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::camera(plan, variants[i]) == i + 1,
                  "camera repeat preserves first slot");
  return true;
}

bool viewportAndSamplerIdentity()
{
  CoinRenderFramePlan plan;
  CoinRenderViewportSnapshot viewport;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::viewport(plan, viewport) == 0, "first viewport slot");
  auto changedViewport = viewport; changedViewport.x = 1;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::viewport(plan, changedViewport) == 1, "viewport x identity");
  changedViewport = viewport; changedViewport.y = 1;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::viewport(plan, changedViewport) == 2, "viewport y identity");
  changedViewport = viewport; changedViewport.width = 1;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::viewport(plan, changedViewport) == 3, "viewport width identity");
  changedViewport = viewport; changedViewport.height = 1;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::viewport(plan, changedViewport) == 4, "viewport height identity");

  CoinRenderSamplerSnapshot sampler;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::sampler(plan, sampler) == 0, "first sampler slot");
  auto changedSampler = sampler; changedSampler.wrapS = CoinRenderTextureWrap::CLAMP;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::sampler(plan, changedSampler) == 1, "sampler wrap S identity");
  changedSampler = sampler; changedSampler.wrapT = CoinRenderTextureWrap::CLAMP;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::sampler(plan, changedSampler) == 2, "sampler wrap T identity");
  changedSampler = sampler; changedSampler.wrapR = CoinRenderTextureWrap::CLAMP;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::sampler(plan, changedSampler) == 3, "sampler wrap R identity");
  changedSampler = sampler; changedSampler.filter = CoinRenderTextureFilter::NEAREST;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::sampler(plan, changedSampler) == 4, "sampler filter identity");
  changedSampler = sampler; changedSampler.maxAnisotropy = 2;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::sampler(plan, changedSampler) == 5, "sampler anisotropy identity");
  return true;
}

bool textureIdentity()
{
  CoinRenderTextureImageSnapshot base;
  base.width = base.height = 1;
  base.pixelsRgba = {1, 2, 3, 4};
  base.contentDigest = 17;
  std::vector<CoinRenderTextureImageSnapshot> variants;
  auto add = [&](CoinRenderTextureImageSnapshot changed) { variants.push_back(std::move(changed)); };
  auto changed = base; changed.format = CoinRenderTextureFormat::RGBA8_SRGB; add(changed);
  changed = base; changed.width = 2; add(changed);
  changed = base; changed.height = 2; add(changed);
  changed = base; changed.components = 3; add(changed);
  changed = base; changed.cubeFaceSize = 1; add(changed);
  changed = base; changed.cubeSourceComponents = 3; add(changed);
  changed = base; changed.cubeMipmapped = true; add(changed);
  changed = base; changed.volumeWidth = 1; add(changed);
  changed = base; changed.volumeHeight = 1; add(changed);
  changed = base; changed.volumeDepth = 1; add(changed);
  changed = base; changed.volumeColumns = 1; add(changed);
  changed = base; changed.volumeSourceComponents = 3; add(changed);
  changed = base; changed.volumeMipmapped = true; add(changed);
  changed = base; changed.contentDigest = 18; add(changed);
  changed = base; changed.producerId = 23; add(changed);
  changed = base; changed.gpuToken = 29; add(changed);
  changed = base; changed.sceneTransparencyFunction = 0; add(changed);
  changed = base; changed.gpuOpaque = true; add(changed);
  changed = base; changed.mipmapped = true; add(changed);
  changed = base; changed.mipmapsRgba = {5, 6, 7, 8}; add(changed);
  changed = base; changed.pixelsRgba[2] = 9; add(changed);

  CoinRenderFramePlan plan;
  auto first = base;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::texture(plan, std::move(first)) == 0, "first texture slot");
  for (size_t i = 0; i < variants.size(); ++i) {
    auto value = variants[i];
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::texture(plan, std::move(value)) == i + 1,
                  "every captured texture field contributes to identity");
  }
  for (size_t i = variants.size(); i-- > 0;) {
    auto value = variants[i];
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::texture(plan, std::move(value)) == i + 1,
                  "texture repeat preserves first slot");
  }
  return true;
}

bool scaleAndFirstOccurrence()
{
  constexpr uint32_t count = 4096;
  CoinRenderFramePlan plan;
  for (uint32_t i = 0; i < count; ++i) {
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, materialValue(i)) == i, "material scale insertion");
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::lighting(plan, lightingValue(i)) == i, "lighting scale insertion");
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::camera(plan, cameraValue(i)) == i, "camera scale insertion");
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::viewport(plan, viewportValue(i)) == i, "viewport scale insertion");
    auto texture = textureValue(i);
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::texture(plan, std::move(texture)) == i, "texture scale insertion");
  }
  for (uint32_t i = count; i-- > 0;) {
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, materialValue(i)) == i, "material reverse lookup");
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::lighting(plan, lightingValue(i)) == i, "lighting reverse lookup");
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::camera(plan, cameraValue(i)) == i, "camera reverse lookup");
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::viewport(plan, viewportValue(i)) == i, "viewport reverse lookup");
    auto texture = textureValue(i);
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::texture(plan, std::move(texture)) == i, "texture reverse lookup");
  }
  for (uint32_t i = 0; i < 160; ++i)
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::sampler(plan, samplerValue(i)) == i, "sampler valid-domain insertion");
  for (uint32_t i = 160; i-- > 0;)
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::sampler(plan, samplerValue(i)) == i, "sampler reverse lookup");
  return true;
}

bool seededCopyAndPolygonSuffix()
{
  CoinRenderFramePlan plan;
  const auto first = materialValue(1), second = materialValue(2), third = materialValue(3);
  plan.materials.push_back(first);
  plan.materials.push_back(second);
  plan.materials.push_back(first);
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, first) == 0,
                "preseeded duplicate must return earliest material slot");
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, third) == 3,
                "new material follows externally seeded suffix");

  CoinRenderPolygonStyleResult polygon;
  polygon.vertices.resize(3);
  polygon.vertices[0].material = second;
  polygon.vertices[1].material = materialValue(4);
  polygon.vertices[2].material = first;
  polygon.indices = {0, 1, 2};
  CoinRenderDrawPacket draw;
  CoinRenderPlanAssemblyCore::appendPolygon(plan, draw, polygon);
  CHECK_MESSAGE(polygon.vertices[0].vertex.materialSlot == 1 &&
                polygon.vertices[1].vertex.materialSlot == 4 &&
                polygon.vertices[2].vertex.materialSlot == 0,
                "polygon helper shares first-occurrence material table");
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, materialValue(4)) == 4,
                "direct lookup sees polygon helper insertion");

  CoinRenderFramePlan copied = plan;
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(copied, first) == 0,
                "copied plan retains earliest material lookup");
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(copied, materialValue(5)) == 5,
                "copied plan admits a new material independently");
  CHECK_MESSAGE(plan.materials.size() == 5, "copy insertion does not mutate source plan");
  return true;
}

bool strokeSuffixSynchronization()
{
  CoinRenderFramePlan plan;
  for (uint32_t i = 0; i < 96; ++i)
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, materialValue(i)) == i,
                  "stroke fixture material prefix");
  plan.lightingStates.emplace_back();
  CoinRenderViewportSnapshot viewport;
  viewport.width = viewport.height = 32;
  plan.viewports.push_back(viewport);
  CoinRenderRenderStateSnapshot state;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  state.viewportSlot = 0;
  state.lineWidth = 1;
  state.linePattern = 0xffffu;
  state.clipPlanesWorld.push_back(SbPlane(SbVec3f(1, 0, 0), 0));
  plan.renderStates.push_back(state);
  CoinRenderVertexSnapshot a, b;
  a.position[0] = -.75f; a.position[2] = 0; a.materialSlot = 0;
  b.position[0] = .75f; b.position[2] = 0; b.materialSlot = 95;
  plan.vertices = {a, b};
  plan.indices = {0, 1};
  CoinRenderDrawPacket draw;
  draw.topology = CoinRenderPrimitiveTopology::LINE_LIST;
  draw.renderStateSlot = 0;
  draw.geometry.vertexCount = 2;
  draw.geometry.indexCount = 2;
  plan.draws.push_back(draw);
  std::string diagnostic;
  CHECK_MESSAGE(coin_render_expand_strokes(plan, diagnostic), diagnostic);
  CHECK_MESSAGE(plan.materials.size() > 96, "stroke expansion creates interpolated material suffix");
  for (uint32_t slot = 96; slot < plan.materials.size(); ++slot) {
    const auto captured = plan.materials[slot];
    CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, captured) <= slot,
                  "direct lookup sees stroke-generated material suffix");
  }
  const auto appended = plan.materials.back();
  const auto expected = CoinRenderPlanAssemblyCore::material(plan, appended);
  CHECK_MESSAGE(CoinRenderPlanAssemblyCore::material(plan, appended) == expected,
                "stroke-generated material repeat is stable");
  return true;
}

} // namespace

int main()
{
  if (!materialIdentity() || !lightingIdentity() || !cameraIdentity() ||
      !viewportAndSamplerIdentity() || !textureIdentity() ||
      !scaleAndFirstOccurrence() || !seededCopyAndPolygonSuffix() ||
      !strokeSuffixSynchronization()) return 1;
  std::cout << "CoinRender frame-local interning identity, scale and helper synchronization passed\n";
  return 0;
}
