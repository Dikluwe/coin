#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "CoinRenderTestEnvironment.h"
#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderDepthCore.h"
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoTranslation.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

// Test access to the private, capture-only optimization; production API and
// the independently usable assembly core retain their existing contracts.
struct CoinRenderFramePlanBuilderTestAccess {
  static uint32_t material(CoinRenderFramePlanBuilder & builder, const CoinRenderMaterialSnapshot & value) {
    return builder.internMaterial(value);
  }
  static std::vector<CoinRenderMaterialSnapshot> & materials(CoinRenderFramePlanBuilder & builder) {
    return builder.currentPlan.materials;
  }
  static size_t indexed(const CoinRenderFramePlanBuilder & builder) { return builder.materialNext.size(); }
  static bool disabled(const CoinRenderFramePlanBuilder & builder) { return builder.materialIndexDisabled; }
  static size_t limit() { return CoinRenderFramePlanBuilder::MATERIAL_INDEX_LIMIT; }
  static void collision(CoinRenderFramePlanBuilder & builder, const CoinRenderMaterialSnapshot & value, uint32_t otherSlot) {
    builder.materialHeads[builder.materialBytesKey(value)] = otherSlot;
  }
  static void polygonMaterial(CoinRenderFramePlanBuilder & builder, const CoinRenderMaterialSnapshot & value) {
    CoinRenderPolygonStyleResult polygon;
    polygon.vertices.resize(1); polygon.vertices[0].material = value;
    CoinRenderDrawPacket draw;
    CoinRenderPlanAssemblyCore::appendPolygon(builder.currentPlan, draw, polygon);
  }
  static void clippedLine(CoinRenderFramePlanBuilder & builder, uint32_t a, uint32_t b) {
    auto & plan = builder.currentPlan;
    plan.vertices.resize(2);
    plan.vertices[0].position[0] = -.8f; plan.vertices[1].position[0] = .8f;
    plan.vertices[0].materialSlot = a; plan.vertices[1].materialSlot = b;
    plan.indices = {0, 1}; plan.cameras.emplace_back(); plan.viewports.emplace_back();
    plan.viewports[0].width = plan.viewports[0].height = 32;
    plan.lightingStates.emplace_back(); plan.renderStates.emplace_back();
    plan.renderStates[0].lightModel = CoinRenderLightModel::BASE_COLOR;
    plan.renderStates[0].materialSlot = a;
    plan.renderStates[0].clipPlanesWorld.push_back(SbPlane(SbVec3f(1, 0, 0), 0));
    CoinRenderDrawPacket draw;
    draw.topology = CoinRenderPrimitiveTopology::LINE_LIST;
    draw.geometry.vertexCount = draw.geometry.indexCount = 2;
    plan.draws.push_back(draw);
  }
  static void invalidClear(CoinRenderFramePlanBuilder & builder) {
    builder.currentPlan.clearColor[0] = std::numeric_limits<float>::quiet_NaN();
  }
  static size_t cubeTemplates(const CoinRenderFramePlanBuilder & builder) { return builder.cubeGeometryCore.templateCount(); }
  static size_t cubeRanges(const CoinRenderFramePlanBuilder & builder) { return builder.cubeGeometryCore.rangeCount(); }
};

namespace {
bool check(bool condition, const char * message) {
  if (!condition) std::cerr << "CoinRenderDepthContractTest: " << message << '\n';
  return condition;
}
void triangle(void * data, SoCallbackAction * a, const SoPrimitiveVertex * x,
              const SoPrimitiveVertex * y, const SoPrimitiveVertex * z) {
  static_cast<CoinRenderFramePlanBuilder *>(data)->addTriangle(a, x, y, z);
}
void line(void * data, SoCallbackAction * a, const SoPrimitiveVertex * x,
          const SoPrimitiveVertex * y) {
  static_cast<CoinRenderFramePlanBuilder *>(data)->addLine(a, x, y);
}
void point(void * data, SoCallbackAction * a, const SoPrimitiveVertex * x) {
  static_cast<CoinRenderFramePlanBuilder *>(data)->addPoint(a, x);
}
struct CubeCapture {
  CoinRenderFramePlanBuilder * builder;
  bool replay;
};
SoCallbackAction::Response cubeBegin(void * data, SoCallbackAction * action, const SoNode * node) {
  auto & capture = *static_cast<CubeCapture *>(data);
  capture.builder->beginShape(action, node);
  if (capture.replay && capture.builder->replayNativeCube(action, const_cast<SoNode *>(node)))
    return SoCallbackAction::PRUNE;
  return SoCallbackAction::CONTINUE;
}
SoCallbackAction::Response cubeEnd(void * data, SoCallbackAction *, const SoNode *) {
  static_cast<CubeCapture *>(data)->builder->endShape();
  return SoCallbackAction::CONTINUE;
}
void captureCubes(CoinRenderFramePlanBuilder & builder, SoNode * root, bool replay) {
  CubeCapture capture = {&builder, replay};
  // The render action enables the complete capture element set. Its extra
  // callbacks keep its own shortcuts disabled while this independent builder
  // exercises native replay against the full primitive callback oracle.
  CoinRenderAction action(SbViewportRegion(32, 32));
  action.addPreCallback(SoCube::getClassTypeId(), cubeBegin, &capture);
  action.addPostCallback(SoCube::getClassTypeId(), cubeEnd, &capture);
  action.addTriangleCallback(SoShape::getClassTypeId(), triangle, &builder);
  action.apply(root);
  builder.endShape();
}
CoinRenderFramePlan expandCubeDraws(const CoinRenderFramePlan & input) {
  CoinRenderFramePlan expanded = input;
  expanded.vertices.clear(); expanded.indices.clear();
  for (auto & draw : expanded.draws) {
    const auto old = draw.geometry;
    draw.geometry.firstVertex = static_cast<uint32_t>(expanded.vertices.size());
    draw.geometry.firstIndex = static_cast<uint32_t>(expanded.indices.size());
    draw.geometry.vertexCount = draw.geometry.indexCount = old.indexCount;
    for (size_t i = 0; i < old.indexCount; ++i) {
      expanded.indices.push_back(static_cast<uint32_t>(expanded.vertices.size()));
      expanded.vertices.push_back(input.vertices[input.indices[old.firstIndex + i]]);
    }
  }
  return expanded;
}
bool cubeTemplateLifecycle() {
  using Access = CoinRenderFramePlanBuilderTestAccess;
  struct Environment {
    std::string value; bool present;
    Environment() {
      const char * previous = std::getenv("COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE");
      present = previous != nullptr; value = previous ? previous : "";
      coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE", "0");
    }
    ~Environment() { coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE", present ? value.c_str() : nullptr); }
  } environment;
  SoSeparator * root = new SoSeparator; root->ref();
  for (int i = 0; i < 3; ++i) {
    auto * object = new SoSeparator;
    auto * material = new SoMaterial; material->diffuseColor.setValue(.25f, .5f, .75f);
    auto * translation = new SoTranslation; translation->translation.setValue(float(i) * 4, 0, 0);
    auto * cube = new SoCube; cube->height = i == 1 ? 3 : 2;
    object->addChild(material); object->addChild(translation); object->addChild(cube); root->addChild(object);
  }
  CoinRenderFramePlanBuilder builder, full;
  const auto begin = [](CoinRenderFramePlanBuilder & target) {
    target.beginFrame(SbColor4f(0, 0, 0, 1), SbViewportRegion(32, 32));
  };
  begin(builder); begin(full);
  captureCubes(builder, root, true); captureCubes(full, root, false);
  CoinRenderFramePlan captured, oracle;
  std::string diagnostic;
  bool ok = check(builder.build(captured, &diagnostic) && full.build(oracle, &diagnostic),
                  "multi-template native Cube and full callback captures must build");
  ok &= check(captured.vertices.size() == 48 && oracle.vertices.size() == 72 &&
              Access::cubeTemplates(builder) == 2 && Access::cubeRanges(builder) == 2,
              "A/B/A dimensions must reuse A across the interleaved B callback capture");
  ok &= check(captured.draws.size() == 3 && captured.draws[0].geometry.firstVertex == captured.draws[2].geometry.firstVertex,
              "independent equal Cubes retain sharing by captured values");
  ok &= check(expandCubeDraws(captured).hasSamePayload(expandCubeDraws(oracle)),
              "shared native templates must retain exact indexed geometry, attributes and states versus full callbacks");
  CoinRenderFramePlan copied;
  ok &= check(builder.build(copied, &diagnostic) && copied.hasSamePayload(captured),
              "copy build must retain the original frame-local template state");
  captureCubes(builder, root, true); captureCubes(full, root, false);
  CoinRenderFramePlan appended, fullAppended;
  ok &= check(builder.build(appended, &diagnostic) && full.build(fullAppended, &diagnostic) &&
              appended.vertices.size() == 48 && Access::cubeTemplates(builder) == 2 &&
              expandCubeDraws(appended).hasSamePayload(expandCubeDraws(fullAppended)) && copied.hasSamePayload(captured),
              "append after copy must reuse frame ranges while preserving independent snapshots and full callback semantics");

  SoSeparator * child = new SoSeparator; child->ref();
  auto * childCube = new SoCube; childCube->width = 9; child->addChild(childCube);
  CoinRenderFramePlanBuilder nested; begin(nested); captureCubes(nested, child, true);
  CoinRenderFramePlan childFrame; ok &= check(nested.build(childFrame, &diagnostic), "nested native Cube capture must build");
  std::swap(builder, nested);
  CoinRenderFramePlan movedMain, movedChild;
  ok &= check(Access::cubeTemplates(builder) == 1 && Access::cubeTemplates(nested) == 2 &&
              builder.build(movedChild, &diagnostic) && nested.build(movedMain, &diagnostic) &&
              movedChild.hasSamePayload(childFrame) && movedMain.hasSamePayload(appended),
              "shadow-style builder swap must move Cube templates together with their captured range arenas");
  std::swap(builder, nested);
  CoinRenderFramePlan transferred;
  ok &= check(builder.build(transferred, &diagnostic, true) && transferred.hasSamePayload(appended) &&
              Access::cubeTemplates(builder) == 0 && Access::cubeRanges(builder) == 0,
              "transfer build must clear frame-local Cube ranges without changing transferred payload");
  begin(builder); captureCubes(builder, child, true);
  CoinRenderFramePlan next;
  ok &= check(builder.build(next, &diagnostic) && Access::cubeTemplates(builder) == 1 && next.vertices.size() == 24 &&
              transferred.hasSamePayload(appended), "new frame must learn fresh Cube ranges and preserve the transferred frame");

  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE", "1");
  begin(builder); captureCubes(builder, root, true);
  CoinRenderFramePlan legacy;
  ok &= check(builder.build(legacy, &diagnostic) && legacy.vertices.size() == 72 && Access::cubeTemplates(builder) == 1 &&
              expandCubeDraws(legacy).hasSamePayload(expandCubeDraws(oracle)),
              "Cube template optout must reproduce the legacy A/B/A source layout and exact rendered payload");
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE", "0");
  begin(builder); captureCubes(builder, root, true);
  CoinRenderFramePlan restored;
  ok &= check(builder.build(restored, &diagnostic) && restored.vertices.size() == 48 && Access::cubeTemplates(builder) == 2,
              "frame reset must re-enable the bounded template cache after an optout frame");
  child->unref(); root->unref();
  return ok;
}
bool materialInterning() {
  using Access = CoinRenderFramePlanBuilderTestAccess;
  struct Environment {
    std::string value; bool present;
    Environment() {
      const char * previous = std::getenv("COIN_RENDER_DISABLE_MATERIAL_INTERNING");
      present = previous != nullptr; value = previous ? previous : "";
      coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_MATERIAL_INTERNING", "0");
    }
    ~Environment() { coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_MATERIAL_INTERNING", present ? value.c_str() : nullptr); }
  } environment;
  bool ok = true;
  CoinRenderFramePlanBuilder builder;
  builder.beginFrame(SbColor4f(0, 0, 0, 1), SbViewportRegion(32, 32));
  CoinRenderFramePlan oracle;
  std::vector<CoinRenderMaterialSnapshot> values(512);
  for (size_t i = 0; i < values.size(); ++i) {
    values[i].diffuse[0] = .1f + float(i) / 1024;
    values[i].diffuse[1] = .2f + float(i % 7) / 128;
    values[i].shininess = .3f + float(i % 11) / 128;
    ok &= check(Access::material(builder, values[i]) == CoinRenderPlanAssemblyCore::material(oracle, values[i]),
                "material hash lookup must retain exact first-occurrence slot order");
  }
  for (size_t i = values.size(); i-- > 0;)
    ok &= check(Access::material(builder, values[i]) == CoinRenderPlanAssemblyCore::material(oracle, values[i]),
                "reverse repeated material capture must equal the independent linear interner");
  ok &= check(Access::indexed(builder) == values.size(), "many materials must activate the optional index");
  const auto tableMatches = [&]() {
    const auto & table = Access::materials(builder);
    return table.size() == oracle.materials.size() &&
      std::memcmp(table.data(), oracle.materials.data(), table.size() * sizeof(CoinRenderMaterialSnapshot)) == 0;
  };
  ok &= check(tableMatches(), "material table bytes must equal the linear oracle");
  CoinRenderMaterialSnapshot collision; collision.emission[1] = .91f;
  Access::collision(builder, collision, 5);
  const auto collidedSlot = CoinRenderPlanAssemblyCore::material(oracle, collision);
  ok &= check(Access::material(builder, collision) == collidedSlot &&
              Access::material(builder, collision) == collidedSlot && tableMatches(),
              "hash collisions must compare bytes and keep distinct snapshots distinct");
  Access::materials(builder).push_back(values[7]);
  oracle.materials.push_back(values[7]);
  ok &= check(Access::material(builder, values[7]) == 7 && tableMatches(),
              "external duplicate suffix must preserve the earliest matching slot");
  CoinRenderMaterialSnapshot polygon; polygon.emission[0] = .87f;
  Access::polygonMaterial(builder, polygon);
  CoinRenderPlanAssemblyCore::material(oracle, polygon);
  ok &= check(Access::material(builder, polygon) == oracle.materials.size() - 1 && tableMatches(),
              "styled polygon assembly appended materials must be synchronized before capture lookup");
  CoinRenderFramePlan copied, repeated, transferred;
  std::string diagnostic;
  ok &= check(builder.build(copied, &diagnostic) && builder.build(repeated, &diagnostic) &&
              repeated.hasSamePayload(copied), "indexed builder must retain repeatable copy builds");
  CoinRenderMaterialSnapshot afterCopy; afterCopy.emission[2] = .79f;
  Access::polygonMaterial(builder, afterCopy);
  CoinRenderPlanAssemblyCore::material(oracle, afterCopy);
  ok &= check(Access::material(builder, afterCopy) == oracle.materials.size() - 1 && tableMatches() &&
              copied.materials.size() + 1 == Access::materials(builder).size(),
              "append after copy build must update only the builder and its append-only index");
  CoinRenderFramePlanBuilder nested;
  nested.beginFrame(SbColor4f(1, 0, 0, 1), SbViewportRegion(32, 32));
  for (size_t i = 0; i < 40; ++i) Access::material(nested, values[values.size() - 1 - i]);
  std::swap(builder, nested);
  ok &= check(Access::material(builder, values[511]) == 0 &&
              Access::material(nested, values[0]) == 0 && Access::indexed(nested) == oracle.materials.size(),
              "suspending capture must move material metadata with its own source table");
  std::swap(builder, nested);
  ok &= check(builder.build(transferred, &diagnostic, true) &&
              Access::indexed(builder) == 0 && Access::materials(builder).empty() &&
              transferred.materials.size() == oracle.materials.size(),
              "ownership transfer must clear the builder index without altering the transferred table");
  builder.beginFrame(SbColor4f(0, 0, 0, 1), SbViewportRegion(32, 32));
  ok &= check(Access::material(builder, values[123]) == 0, "reset must start a fresh material slot order");

  // Raw bit equality also preserves signed zero and NaN payload distinctions.
  // These lookup tests do not submit or validate a non-finite scene.
  for (size_t i = 0; i < 40; ++i) Access::material(builder, values[i]);
  CoinRenderMaterialSnapshot bits;
  bits.ambient[0] = 0;
  const auto positiveZero = Access::material(builder, bits);
  bits.ambient[0] = -0.0f;
  const auto negativeZero = Access::material(builder, bits);
  ok &= check(positiveZero != negativeZero && Access::material(builder, bits) == negativeZero,
              "material byte keys must preserve signed zero unlike numeric state equality");
  uint32_t nanBits = UINT32_C(0x7fc00001);
  std::memcpy(&bits.diffuse[2], &nanBits, sizeof(nanBits));
  const auto firstNan = Access::material(builder, bits);
  nanBits = UINT32_C(0x7fc00002); std::memcpy(&bits.diffuse[2], &nanBits, sizeof(nanBits));
  const auto secondNan = Access::material(builder, bits);
  ok &= check(firstNan != secondNan && Access::material(builder, bits) == secondNan,
              "material byte equality must preserve distinct and repeated NaN payloads");

  builder.beginFrame(SbColor4f(0, 0, 0, 1), SbViewportRegion(32, 32));
  for (size_t i = 0; i < 40; ++i) Access::material(builder, values[i]);
  Access::clippedLine(builder, 0, 39);
  const size_t beforeStroke = Access::materials(builder).size();
  ok &= check(builder.build(copied, &diagnostic) && copied.materials.size() > beforeStroke,
              "clipped line build must actually append interpolated stroke materials");
  const auto stroke = copied.materials.back();
  const auto strokeSlot = static_cast<uint32_t>(copied.materials.size() - 1);
  ok &= check(Access::material(builder, stroke) == strokeSlot &&
              builder.build(repeated, &diagnostic) && repeated.hasSamePayload(copied),
              "material suffix created during a copy build must be available to subsequent capture lookups");
  CoinRenderMaterialSnapshot failedSuffix; failedSuffix.emission[2] = .82f;
  Access::polygonMaterial(builder, failedSuffix); Access::invalidClear(builder);
  CoinRenderFramePlan failed;
  ok &= check(!builder.build(failed, &diagnostic) &&
              Access::material(builder, failedSuffix) == Access::materials(builder).size() - 1 &&
              repeated.hasSamePayload(copied),
              "failed builds must retain a synchronizable append suffix without mutating prior copied snapshots");

  // The metadata limit bounds only acceleration: the full material table and
  // independently callable core may continue past it with exact linear lookup.
  builder.beginFrame(SbColor4f(0, 0, 0, 1), SbViewportRegion(32, 32));
  auto & boundary = Access::materials(builder); boundary.resize(Access::limit());
  for (size_t i = 0; i < boundary.size(); ++i) boundary[i].diffuse[0] = float(i) / float(boundary.size());
  const auto first = boundary[0], last = boundary.back();
  ok &= check(Access::material(builder, last) == Access::limit() - 1 && Access::indexed(builder) == Access::limit(),
              "the inclusive material metadata cap must admit its complete table");
  CoinRenderMaterialSnapshot overflow; overflow.emission[0] = .83f;
  ok &= check(Access::material(builder, overflow) == Access::limit() && Access::disabled(builder) &&
              Access::indexed(builder) == 0 && Access::material(builder, first) == 0 &&
              Access::material(builder, overflow) == Access::limit(),
              "over-cap capture must retain every material and fall back linearly until reset");
  builder.beginFrame(SbColor4f(0, 0, 0, 1), SbViewportRegion(32, 32));
  for (size_t i = 0; i < 40; ++i) Access::material(builder, values[i]);
  ok &= check(!Access::disabled(builder) && Access::indexed(builder) == 40,
              "a later frame must readmit bounded interning after metadata fallback");
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_MATERIAL_INTERNING", "1");
  builder.beginFrame(SbColor4f(0, 0, 0, 1), SbViewportRegion(32, 32));
  oracle.materials.clear();
  for (const auto & value : values)
    ok &= check(Access::material(builder, value) == CoinRenderPlanAssemblyCore::material(oracle, value),
                "material interning optout must preserve the ordinary assembly slots");
  ok &= check(Access::indexed(builder) == 0 && tableMatches(), "optout must keep the bounded index disabled");
  return ok;
}
bool stateInterning() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoCube * cube = new SoCube;
  const auto append = [&](int position, bool blue) {
    SoSeparator * group = new SoSeparator;
    SoTranslation * translation = new SoTranslation;
    translation->translation.setValue(float(position), 0, 0);
    SoMaterial * material = new SoMaterial;
    material->diffuseColor.setValue(blue ? SbColor(0, 0, 1) : SbColor(1, 0, 0));
    group->addChild(translation);
    group->addChild(material);
    group->addChild(cube);
    root->addChild(group);
  };
  for (int i = 0; i < 128; ++i) append(i, false);
  append(3, false);
  append(3, true);
  CoinRenderFramePlanBuilder builder;
  bool ok = true;
  for (int frame = 0; frame < 2; ++frame) {
    builder.beginFrame(SbColor4f(0, 0, 0, 1), SbViewportRegion(32, 32));
    CoinRenderAction action(SbViewportRegion(32, 32));
    action.addTriangleCallback(SoShape::getClassTypeId(), triangle, &builder);
    action.apply(root);
    CoinRenderFramePlan captured;
    std::string diagnostic;
    ok &= check(builder.build(captured, &diagnostic), "transformed instance capture failed");
    ok &= check(captured.renderStates.size() == 129,
      "state reuse must preserve distinct transforms and materials across frames");
    CoinRenderFramePlan repeated, transferred;
    ok &= check(builder.build(repeated, &diagnostic) &&
      repeated.hasSamePayload(captured) && repeated.revision != captured.revision,
      "default build must retain a repeatable independent snapshot");
    ok &= check(builder.build(transferred, &diagnostic, true) &&
      transferred.hasSamePayload(captured) && transferred.isValid(&diagnostic),
      "ownership transfer must retain the complete valid captured payload");
    builder.beginFrame(SbColor4f(1, 0, 0, 1), SbViewportRegion(16, 16));
    ok &= check(transferred.hasSamePayload(captured),
      "next capture must not mutate the transferred snapshot");
  }
  root->unref();
  return ok;
}
bool capture() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  root->addChild(new SoCube);
  SoPolygonOffset * offset = new SoPolygonOffset;
  offset->factor = -2.0f;
  offset->units = 4.0f;
  offset->styles = SoPolygonOffset::LINES | SoPolygonOffset::POINTS;
  root->addChild(offset);
  SoDepthBuffer * depth = new SoDepthBuffer;
  depth->test = FALSE;
  depth->write = FALSE;
  depth->function = SoDepthBuffer::GREATER;
  depth->range = SbVec2f(0.2f, 0.8f);
  root->addChild(depth);
  root->addChild(new SoCube);
  SoCoordinate3 * coordinates = new SoCoordinate3;
  const SbVec3f positions[] = {SbVec3f(-0.5f, 0, 0), SbVec3f(0.5f, 0, 0)};
  coordinates->point.setValues(0, 2, positions);
  root->addChild(coordinates);
  SoLineSet * lines = new SoLineSet;
  lines->numVertices = 2;
  root->addChild(lines);
  SoPointSet * points = new SoPointSet;
  points->numPoints = 1;
  root->addChild(points);
  CoinRenderFramePlanBuilder builder;
  builder.beginFrame(SbColor4f(0, 0, 0, 1), SbViewportRegion(32, 32));
  CoinRenderAction action(SbViewportRegion(32, 32));
  action.addTriangleCallback(SoShape::getClassTypeId(), triangle, &builder);
  action.addLineSegmentCallback(SoShape::getClassTypeId(), line, &builder);
  action.addPointCallback(SoShape::getClassTypeId(), point, &builder);
  action.apply(root);
  CoinRenderFramePlan frame;
  std::string diagnostic;
  bool ok = check(builder.build(frame, &diagnostic), "scene capture failed");
  if (!ok) std::cerr << diagnostic << '\n';
  bool sawDefault = false, sawFill = false, sawLines = false, sawPoints = false;
  for (const CoinRenderDrawPacket & draw : frame.draws) {
    const CoinRenderRenderStateSnapshot & state = frame.renderStates[draw.renderStateSlot];
    if (!state.polygonOffsetEnabled) {
      sawDefault = true;
      ok &= check(state.polygonOffsetFactor == 0 && state.polygonOffsetUnits == 0 &&
        state.depthTest && state.depthWrite && state.depthRange[0] == 0 &&
        state.depthRange[1] == 1 && state.explicitDepthMask == 0, "default state changed");
    } else {
      sawFill |= state.polygonOffsetPrimitiveStyle == 1;
      sawLines |= state.polygonOffsetPrimitiveStyle == 2;
      sawPoints |= state.polygonOffsetPrimitiveStyle == 4;
      ok &= check(state.polygonOffsetFactor == -2 && state.polygonOffsetUnits == 4 &&
        state.polygonOffsetStyles == 6 && !state.depthTest && !state.depthWrite &&
        state.depthFunction == CoinRenderDepthFunction::GREATER &&
        state.depthRange[0] == 0.2f && state.depthRange[1] == 0.8f && state.explicitDepthMask == 15,
        "offset/depth state lost or merged across draws");
    }
  }
  ok &= check(sawDefault && sawFill && sawLines && sawPoints,
    "capture must retain default and fill/line/point styles after expansion");
  CoinWgpuFfiFrame ffi;
  ok &= check(ffi.prepare(frame, 32, 32, diagnostic), "FFI packing failed");
  for (size_t i = 0; i < frame.renderStates.size(); ++i) {
    const auto & src = frame.renderStates[i];
    const auto & dst = ffi.getView().states[i];
    ok &= check(dst.polygon_offset_factor == src.polygonOffsetFactor &&
      dst.polygon_offset_units == src.polygonOffsetUnits &&
      dst.polygon_offset_enabled == uint32_t(src.polygonOffsetEnabled) &&
      dst.polygon_offset_styles == src.polygonOffsetStyles &&
      dst.polygon_offset_primitive_style == src.polygonOffsetPrimitiveStyle &&
      dst.depth_range[0] == src.depthRange[0], "FFI offset/depth payload changed");
  }
  offset->factor = 3.0f;
  builder.beginFrame(SbColor4f(0, 0, 0, 1), SbViewportRegion(32, 32));
  action.apply(root);
  CoinRenderFramePlan changed;
  ok &= check(builder.build(changed, &diagnostic), "recapture failed");
  ok &= check(CoinRenderFrameReuseCore::classify(frame, changed).kind !=
    CoinRenderFrameReuseKind::REUSE &&
    CoinRenderFrameReuseCore::classify(frame, changed).kind !=
    CoinRenderFrameReuseKind::CAMERA_PATCH, "offset mutation reused stale frame state");
  root->unref();
  return ok;
}

CoinRenderFramePlan fixture() {
  CoinRenderFramePlan frame;
  frame.clearColor = SbColor4f(0, 0, 0, 1);
  frame.lightingStates.push_back(CoinRenderLightingSnapshot{});
  frame.cameras.push_back(CoinRenderCameraSnapshot{});
  CoinRenderViewportSnapshot viewport;
  viewport.width = viewport.height = 32;
  frame.viewports.push_back(viewport);
  for (int i = 0; i < 2; ++i) {
    CoinRenderMaterialSnapshot material;
    material.diffuse[0] = i == 0 ? 1.0f : 0.0f;
    material.diffuse[1] = i == 1 ? 1.0f : 0.0f;
    material.diffuse[2] = 0;
    frame.materials.push_back(material);
    CoinRenderRenderStateSnapshot state;
    state.lightModel = CoinRenderLightModel::BASE_COLOR;
    state.cullMode = CoinRenderCullMode::NONE;
    state.materialSlot = i;
    frame.renderStates.push_back(state);
    const float xy[4][2] = {{-0.8f,-0.8f},{0.8f,-0.8f},{0.8f,0.8f},{-0.8f,0.8f}};
    for (const auto & p : xy) {
      CoinRenderVertexSnapshot vertex;
      vertex.position[0] = p[0]; vertex.position[1] = p[1];
      vertex.materialSlot = i;
      frame.vertices.push_back(vertex);
    }
    for (uint32_t index : {0u,1u,2u,0u,2u,3u}) frame.indices.push_back(i * 4 + index);
    CoinRenderDrawPacket draw;
    draw.renderStateSlot = i;
    draw.geometry.firstVertex = i * 4; draw.geometry.vertexCount = 4;
    draw.geometry.firstIndex = i * 6; draw.geometry.indexCount = 6;
    frame.draws.push_back(draw);
  }
  return frame;
}

bool triangleDepthMetric() {
  bool ok = true;
  std::string diagnostic;
  for (bool reversed : {false, true}) {
    auto frame = fixture();
    for (auto &v : frame.vertices)
      v.position[2] = .2f * v.position[0];
    auto &state = frame.renderStates[1];
    state.polygonOffsetEnabled = true;
    state.polygonOffsetFactor = 2;
    state.polygonOffsetUnits = 16;
    state.depthRange[0] = reversed ? .8f : .2f;
    state.depthRange[1] = reversed ? .2f : .8f;
    ok &= check(coin_render_resolve_triangle_depth(frame, diagnostic), "triangle depth resolution");
    unsigned resolved = 0;
    for (const auto &draw : frame.draws) {
      const auto &rs = frame.renderStates[draw.renderStateSlot];
      if (!rs.polygonOffsetEnabled)
        continue;
      ++resolved;
      ok &= check(rs.polygonOffsetFactor == 0 &&
                      std::abs(rs.polygonOffsetSlopeBias - .0075f) < 1e-6f &&
                      rs.polygonOffsetMaxDepth >= .451f && rs.polygonOffsetMaxDepth <= .549f,
                  "original triangle slope = abs(.2/2 * .6 /16), independent of range order");
    }
    ok &= check(resolved == 2, "each filled triangle preserves its original depth metric");
    const auto before = frame;
    ok &=
        check(coin_render_resolve_triangle_depth(frame, diagnostic) && frame.hasSamePayload(before),
              "resolved depth pass is idempotent");
  }
  return ok;
}

bool resolvedDepthPixels(bool gpu) {
  CoinRenderTargetP target(SbVec2i32(32, 32));
  target.depthReadbackEnabled = false;
  if (!gpu)
    target.backend.reset(new CoinRenderCpuReferenceBackend);
  for (bool reversed : {false, true})
    for (float factor : {-2.f, 2.f})
      for (float units : {-16.f, 16.f}) {
        auto frame = fixture();
        for (auto &v : frame.vertices)
          v.position[2] = .2f * v.position[0];
        for (auto &state : frame.renderStates) {
          state.depthRange[0] = reversed ? .8f : .2f;
          state.depthRange[1] = reversed ? .2f : .8f;
        }
        auto &state = frame.renderStates[1];
        state.polygonOffsetEnabled = true;
        state.polygonOffsetFactor = factor;
        state.polygonOffsetUnits = units;
        std::string diagnostic;
        if (!coin_render_resolve_triangle_depth(frame, diagnostic))
          return false;
        const auto result = target.executeFrame(frame);
        if (result.status != CoinRenderBackendStatus::SUCCESS)
          return false;
        const auto pixel = (16 * 32 + 20) * 4;
        const bool green = factor < 0;
        if (!check(target.colorBuffer[pixel + (green ? 1 : 0)] > 240 &&
                       target.colorBuffer[pixel + (green ? 0 : 1)] < 10,
                   "resolved slope controls coplanar occlusion for either range order and opposing "
                   "units"))
          return false;
      }
  for (bool reversed : {false, true})
    for (float units : {-16.f, 16.f}) {
      auto frame = fixture();
      for (auto &state : frame.renderStates) {
        state.depthRange[0] = reversed ? .8f : .2f;
        state.depthRange[1] = reversed ? .2f : .8f;
      }
      frame.renderStates[1].polygonOffsetEnabled = true;
      frame.renderStates[1].polygonOffsetUnits = units;
      std::string diagnostic;
      if (!coin_render_resolve_triangle_depth(frame, diagnostic))
        return false;
      if (target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS)
        return false;
      const auto pixel = (16 * 32 + 20) * 4;
      const bool green = units < 0;
      if (!check(target.colorBuffer[pixel + (green ? 1 : 0)] > 240,
                 "resolved depth units keep their sign with a reversed range"))
        return false;
    }
  return true;
}

bool subnormalCpuDepthQuantum() {
  CoinRenderTargetP target(SbVec2i32(32, 32));
  target.backend.reset(new CoinRenderCpuReferenceBackend);
  auto frame = fixture();
  for (auto &state : frame.renderStates) {
    state.depthRange[0] = 0;
    state.depthRange[1] = 2e-40f;
  }
  frame.renderStates[1].polygonOffsetEnabled = true;
  frame.renderStates[1].polygonOffsetUnits = -16;
  std::string diagnostic;
  if (!coin_render_resolve_triangle_depth(frame, diagnostic) ||
      target.executeFrame(frame).status != CoinRenderBackendStatus::SUCCESS)
    return false;
  const auto pixel = (16 * 32 + 20) * 4;
  return check(target.colorBuffer[pixel + 1] > 240 && target.colorBuffer[pixel] < 10,
               "D32 CPU subnormal quantum remains one representable step, not zero");
}

bool annotationTransport() {
  CoinRenderFramePlan frame = fixture();
  frame.revision = 101;
  frame.draws[0].renderLayer = 3;
  frame.draws[0].clearDepthBefore = true;
  frame.draws[1].renderLayer = 3;
  CoinWgpuFfiFrame ffi;
  std::string diagnostic;
  if (!check(ffi.prepare(frame, 32, 32, diagnostic), "annotation FFI packing")) return false;
  const auto matches = [&]() {
    const auto & view = ffi.getView();
    return check(view.abi_version == COIN_WGPU_ABI_VERSION && view.draws[0].render_layer == 3 &&
      view.draws[0].clear_depth_before == 1 && view.draws[1].render_layer == 3 &&
      view.draws[1].clear_depth_before == 0, "annotation payload lost in FFI");
  };
  if (!matches() || !ffi.prepare(frame, 32, 32, diagnostic) ||
      !check(ffi.reusedLastPrepare(), "annotation frame reuse") || !matches()) return false;
  frame.revision = 102;
  if (!ffi.prepare(frame, 32, 32,
      CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::CAMERA_PATCH, 101), diagnostic) ||
      !check(ffi.lastPrepareKind() == CoinRenderFrameReuseKind::CAMERA_PATCH,
        "annotation camera patch") || !matches()) return false;
  frame.revision = 103;
  frame.draws[0].renderLayer = 0;
  frame.draws[0].clearDepthBefore = false;
  if (!ffi.prepare(frame, 32, 32, diagnostic)) return false;
  return check(ffi.getView().draws[0].render_layer == 0 &&
    ffi.getView().draws[0].clear_depth_before == 0, "annotation removal retained stale payload");
}

bool lowerAndCache() {
  bool ok = true;
  for (uint32_t primitive : {1u,2u,4u}) {
    for (uint32_t styles : {1u,2u,4u,7u}) {
      CoinRenderFramePlan frame = fixture();
      frame.revision = 1;
      auto & state = frame.renderStates[1];
      state.polygonOffsetEnabled = true;
      state.polygonOffsetFactor = -2;
      state.polygonOffsetUnits = 3;
      state.polygonOffsetSlopeBias = -.02f;
      state.polygonOffsetStyles = styles;
      state.polygonOffsetPrimitiveStyle = primitive;
      CoinBgfxPlan lowered;
      std::string diagnostic;
      ok &= check(CoinBgfxLowering::lower(frame,32,32,false,lowered,diagnostic), "lowering failed");
      if (lowered.draws.size() != 2) return false;
      ok &= check(lowered.draws[0].polygonOffsetFactor == 0 &&
        lowered.draws[1].polygonOffsetFactor == ((styles & primitive) ? -2 : 0) &&
        lowered.draws[1].polygonOffsetUnits == ((styles & primitive) ? 3 : 0) &&
        lowered.draws[1].polygonOffsetSlopeBias == ((styles & primitive) ? -.02f : 0),
        "style filtering or per-draw isolation failed");
      CoinRenderFramePlan changed;
      std::vector<CoinBgfxDraw> patched;
      for (int mutation : {0,1}) {
        changed = frame;
        changed.revision = frame.revision + 1;
        if (mutation == 0) changed.renderStates[1].polygonOffsetUnits = -3;
        else changed.renderStates[1].polygonOffsetSlopeBias = .02f;
        ok &= check(CoinRenderFrameReuseCore::classify(frame,changed).kind !=
          CoinRenderFrameReuseKind::REUSE &&
          CoinRenderFrameReuseCore::classify(frame,changed).kind !=
          CoinRenderFrameReuseKind::CAMERA_PATCH, "bias change reused stale cache");
        if (styles & primitive) {
          ok &= check(!CoinBgfxLowering::patchCamera(changed,32,32,false,lowered,patched,diagnostic),
            "camera patch accepted changed effective bias");
        }
      }
      changed = frame;
      changed.renderStates[1].depthRange[0] = 0.25f;
      ok &= check(CoinRenderFrameReuseCore::classify(frame,changed).kind !=
        CoinRenderFrameReuseKind::CAMERA_PATCH &&
        !CoinBgfxLowering::patchCamera(changed,32,32,false,lowered,patched,diagnostic),
        "range mutation reused camera-only cache");
    }
  }
  return ok;
}

int gpu() {
  CoinRenderTargetP target(SbVec2i32(32,32));
  target.depthReadbackEnabled = false;
  auto render = [&](const CoinRenderFramePlan & frame, bool green, const char * message) {
    const auto result = target.executeFrame(frame);
    if (result.status != CoinRenderBackendStatus::SUCCESS) {
      std::cerr << message << ": " << result.diagnostic << '\n';
      return false;
    }
    const size_t pixel = (16 * 32 + 20) * 4;
    return check(target.colorBuffer[pixel + (green ? 1 : 0)] > 240 &&
      target.colorBuffer[pixel + (green ? 0 : 1)] < 10, message);
  };
  CoinRenderFramePlan frame = fixture();
  auto result = target.executeFrame(frame);
  if (result.status == CoinRenderBackendStatus::NOT_READY) return 77;
  bool ok = render(frame, false, "default LESS coplanar overlap");
  frame.renderStates[1].polygonOffsetEnabled = true;
  frame.renderStates[1].polygonOffsetUnits = -16;
  ok &= render(frame, true, "negative units must bring coplanar overlay forward");
  frame.renderStates[1].polygonOffsetUnits = 16;
  ok &= render(frame, false, "positive units must move overlay behind");
  frame.renderStates[1].polygonOffsetEnabled = false;
  ok &= render(frame, false, "disabled offset must not retain prior bias");
  frame.renderStates[1].polygonOffsetEnabled = true;
  frame.renderStates[1].polygonOffsetUnits = -16;
  for (uint32_t style : {1u,2u,4u}) {
    frame.renderStates[1].polygonOffsetStyles = style;
    ok &= render(frame, style == 1, "fill-only style mask");
  }
  frame = fixture();
  frame.renderStates[0].polygonOffsetEnabled = true;
  frame.renderStates[0].polygonOffsetUnits = 16;
  ok &= render(frame, true, "offset must reset on following draw in the same frame");
  for (uint32_t primitive : {1u,2u,4u}) {
    for (uint32_t mask : {1u,2u,4u}) {
      frame = fixture();
      frame.renderStates[1].polygonOffsetEnabled = true;
      frame.renderStates[1].polygonOffsetUnits = -16;
      frame.renderStates[1].polygonOffsetPrimitiveStyle = primitive;
      frame.renderStates[1].polygonOffsetStyles = mask;
      ok &= render(frame, (primitive & mask) != 0, "expanded fill/line/point style filtering");
    }
  }
  frame = fixture();
  for (auto & vertex : frame.vertices) vertex.position[2] = vertex.position[0] * 0.2f;
  frame.renderStates[1].polygonOffsetEnabled = true;
  frame.renderStates[1].polygonOffsetFactor = -1;
  ok &= render(frame, true, "negative slope factor on slanted overlay");
  frame.renderStates[1].polygonOffsetFactor = 1;
  ok &= render(frame, false, "positive slope factor on slanted overlay");
  frame = fixture();
  frame.renderStates[1].depthRange[0] = 0.0f;
  frame.renderStates[1].depthRange[1] = 0.5f;
  ok &= render(frame, true, "non-default depth range must be applied");
  frame.renderStates[1].depthRange[0] = 0.75f;
  frame.renderStates[1].depthRange[1] = 1.0f;
  ok &= render(frame, false, "far depth range must occlude overlay");
  frame.renderStates[1].depthTest = false;
  ok &= render(frame, true, "depth test disabled must remain disabled");
  frame = fixture();
  frame.renderStates[0].depthWrite = false;
  ok &= render(frame, true, "depth write disabled must preserve following draw");
  frame = fixture();
  frame.renderStates[1].depthFunction = CoinRenderDepthFunction::LEQUAL;
  ok &= render(frame, true, "LEQUAL coplanar overlay");
  frame.renderStates[1].depthFunction = CoinRenderDepthFunction::NEVER;
  ok &= render(frame, false, "NEVER depth function");
  frame.renderStates[1].depthFunction = CoinRenderDepthFunction::ALWAYS;
  ok &= render(frame, true, "ALWAYS depth function");
  return ok && resolvedDepthPixels(true) ? 0 : 1;
}
}

int main(int argc, char ** argv) {
  SoDB::init();
  CoinRenderAction::initClass();
  if (argc == 2 && std::string(argv[1]) == "--gpu") return gpu();
  return materialInterning() && cubeTemplateLifecycle() && stateInterning() && capture() &&
                 lowerAndCache() && annotationTransport() && triangleDepthMetric() &&
                 resolvedDepthPixels(false) && subnormalCpuDepthQuantum()
             ? 0
             : 1;
}
