#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoMaterial.h>
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <cmath>
#include <iostream>
#include <string>

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
  return ok ? 0 : 1;
}
}

int main(int argc, char ** argv) {
  SoDB::init();
  CoinRenderAction::initClass();
  if (argc == 2 && std::string(argv[1]) == "--gpu") return gpu();
  return stateInterning() && capture() && lowerAndCache() && annotationTransport() ? 0 : 1;
}
