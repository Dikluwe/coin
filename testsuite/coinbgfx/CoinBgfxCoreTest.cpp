#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinbgfx/CoinBgfxLowering.h"

#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/SoDB.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinBgfxCoreTest: " << message << '\n';
  return condition;
}
}

int main()
{
  SoDB::init();
  CoinRenderFramePlan frame;
  frame.clearColor = SbColor4f(0.1f, 0.2f, 0.3f, 1.0f);
  frame.materials.push_back(CoinRenderMaterialSnapshot());
  frame.lightingStates.push_back(CoinRenderLightingSnapshot());
  frame.cameras.push_back(CoinRenderCameraSnapshot());
  CoinRenderViewportSnapshot viewport;
  viewport.width = 4;
  viewport.height = 4;
  frame.viewports.push_back(viewport);
  CoinRenderRenderStateSnapshot state;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  frame.renderStates.push_back(state);
  frame.vertices.resize(3);
  frame.indices = {0, 1, 2};
  CoinRenderDrawPacket draw;
  draw.geometry.vertexCount = 3;
  draw.geometry.indexCount = 3;
  frame.draws.push_back(draw);

  bool ok = true;
  std::string diagnostic;
  CoinBgfxPlan plan;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic),
              "valid BASE_COLOR triangle rejected");
  ok &= check(plan.draws[0].viewport[0] == 0 && plan.draws[0].viewport[1] == 0 &&
              plan.draws[0].viewport[2] == 4 && plan.draws[0].viewport[3] == 4,
              "full viewport was not preserved during lowering");
  ok &= check(plan.vertices.size() == 3 && plan.indices.size() == 3 &&
              plan.draws.size() == 1, "geometry structure changed");
  ok &= check(std::abs(plan.vertices[0].color[0] - 0.8f) < 1e-6f &&
              std::abs(plan.vertices[0].color[3] - 1.0f) < 1e-6f,
              "Coin diffuse color lost floating-point precision");
  ok &= check(plan.clearColor[0] == frame.clearColor[0] &&
              plan.clearColor[1] == frame.clearColor[1],
              "Coin clear color was quantized before GPU submission");
  ok &= check(std::abs(plan.draws[0].mvp[10] - 0.5f) < 1e-6f &&
              std::abs(plan.draws[0].mvp[14] - 0.5f) < 1e-6f,
              "Coin clip depth was not converted to Vulkan range");
  SbMatrix moved = SbMatrix::identity();
  moved.setTranslate(SbVec3f(0.25f, 0.0f, 0.0f));
  frame.cameras[0].viewMatrix = moved;
  frame.renderStates[0].view = moved;
  std::vector<CoinBgfxDraw> patched;
  CoinBgfxPlan movedPlan;
  ok &= check(CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic) &&
              CoinBgfxLowering::lower(frame, 4, 4, false, movedPlan, diagnostic) &&
              patched.size() == 1 &&
              std::memcmp(patched[0].mvp, movedPlan.draws[0].mvp,
                          sizeof(patched[0].mvp)) == 0,
              "camera patch differs from full lowering");
  frame.clearColor = SbColor4f(0.1001f, 0.2f, 0.3f, 1.0f);
  ok &= check(!CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic),
              "camera patch accepted a changed float clear color");
  frame.clearColor = SbColor4f(0.1f, 0.2f, 0.3f, 1.0f);
  frame.draws[0].geometry.indexCount = 2;
  ok &= check(!CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic) &&
              patched.size() == 1 &&
              std::memcmp(patched[0].mvp, movedPlan.draws[0].mvp,
                          sizeof(patched[0].mvp)) == 0,
              "invalid camera patch must not publish partial draws");
  frame.draws[0].geometry.indexCount = 3;
  SbMatrix invalidCamera = moved;
  invalidCamera[0][0] = std::numeric_limits<float>::quiet_NaN();
  frame.renderStates[0].view = invalidCamera;
  ok &= check(!CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic),
              "non-finite camera patch must be rejected");
  frame.renderStates[0].view = moved;
  frame.viewports[0].x = 1;
  frame.viewports[0].y = 1;
  frame.viewports[0].width = 2;
  frame.viewports[0].height = 2;
  CoinBgfxPlan subviewportPlan;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, subviewportPlan, diagnostic) &&
              subviewportPlan.draws[0].viewport[0] == 1 &&
              subviewportPlan.draws[0].viewport[1] == 1 &&
              subviewportPlan.draws[0].viewport[2] == 2 &&
              subviewportPlan.draws[0].viewport[3] == 2,
              "bounded subviewport was not lowered");
  frame.renderStates[0].depthTest = false;
  frame.renderStates[0].depthWrite = false;
  frame.renderStates[0].depthFunction = CoinRenderDepthFunction::ALWAYS;
  frame.renderStates[0].depthRange[0] = 0.25f;
  frame.renderStates[0].depthRange[1] = 0.75f;
  CoinBgfxPlan depthPlan;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, depthPlan, diagnostic) &&
              !depthPlan.draws[0].depthTest && !depthPlan.draws[0].depthWrite &&
              depthPlan.draws[0].depthFunction == CoinRenderDepthFunction::ALWAYS &&
              depthPlan.draws[0].depthRange[0] == 0.25f &&
              depthPlan.draws[0].depthRange[1] == 0.75f,
              "depth state was not lowered to BGFX draws");
  frame.renderStates[0].depthTest = true;
  frame.renderStates[0].depthWrite = true;
  frame.renderStates[0].depthFunction = CoinRenderDepthFunction::LESS;
  frame.renderStates[0].depthRange[0] = 0.0f;
  frame.renderStates[0].depthRange[1] = 1.0f;
  ok &= check(!CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic),
              "camera-only patch accepted a changed viewport");
  frame.viewports[0].x = 3;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, subviewportPlan, diagnostic),
              "partially external viewport was rejected");
  int32_t clipped[4];
  const int32_t partial[][4] = {{-1, 1, 2, 2}, {3, 1, 2, 2},
                               {1, -1, 2, 2}, {1, 3, 2, 2}, {-2, -2, 8, 8}};
  const int32_t expected[][4] = {{0, 1, 1, 2}, {3, 1, 1, 2},
                                {1, 0, 2, 1}, {1, 3, 2, 1}, {0, 0, 4, 4}};
  for (size_t i = 0; i < 5; ++i) {
    ok &= check(CoinBgfxLowering::clipViewport(partial[i], 4, 4, clipped) &&
                std::memcmp(clipped, expected[i], sizeof(clipped)) == 0,
                "partial viewport intersection is incorrect");
  }
  const int32_t external[][4] = {{-4, 0, 4, 4}, {4, 0, 4, 4},
                                {0, -4, 4, 4}, {0, 4, 4, 4},
                                {INT32_MAX, 0, INT32_MAX, 4}};
  for (const auto & rect : external)
    ok &= check(!CoinBgfxLowering::clipViewport(rect, 4, 4, clipped),
                "external viewport was not completely clipped");
  frame.viewports[0].x = -1;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, subviewportPlan, diagnostic) &&
              subviewportPlan.draws[0].viewport[0] == -1 &&
              std::abs(subviewportPlan.draws[0].mvp[12] + 0.875f) < 1e-6f,
              "clipping changed the original viewport projection");
  ok &= check(CoinBgfxLowering::patchCamera(frame, 4, 4, false, subviewportPlan,
                                         patched, diagnostic),
              "camera patch rejected a preserved negative viewport");
  frame.viewports[0].x = 0;
  frame.viewports[0].y = 0;
  frame.viewports[0].width = 4;
  frame.viewports[0].height = 4;

  const size_t originalDrawCount = plan.draws.size();
  frame.renderStates[0].fogMode = CoinRenderFogMode::FOG;
  frame.renderStates[0].fogEnd = 10.0f;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic) &&
              plan.draws.size() == originalDrawCount &&
              plan.draws[0].fogColorMode[3] == 2.0f && plan.draws[0].fogRange[1] == 10.0f,
              "fog must be lowered into draw uniforms");
  frame.renderStates[0].fogMode = CoinRenderFogMode::NONE;
  frame.materials[0].diffuse[3] = 0.5f;
  frame.materials[0].transparency = 0.5f;
  frame.renderStates[0].transparencyType = SoGLRenderAction::ADD;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic) &&
              plan.draws[0].additive && !plan.draws[0].deferred && plan.draws[0].depthWrite,
              "ADD must preserve immediate additive blending and depth writes");
  frame.renderStates[0].transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic) &&
              plan.draws.size() == 1 && plan.draws[0].blend &&
              std::abs(plan.draws[0].alpha - 0.5f) < 1e-6f,
              "SORTED_OBJECT_BLEND alpha was not lowered");
  frame.renderStates[0].depthTest = true;
  frame.renderStates[0].depthWrite = true;
  frame.renderStates[0].depthFunction = CoinRenderDepthFunction::LEQUAL;
  CoinBgfxPlan translucentCubeDepthPlan;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false,
                translucentCubeDepthPlan, diagnostic) &&
              translucentCubeDepthPlan.draws[0].blend &&
              translucentCubeDepthPlan.draws[0].depthTest &&
              !translucentCubeDepthPlan.draws[0].depthWrite &&
              translucentCubeDepthPlan.draws[0].depthFunction == CoinRenderDepthFunction::LEQUAL,
              "delayed transparency must disable depth writes while preserving LEQUAL");
  frame.renderStates[0].depthWrite = false;
  CoinBgfxPlan labelDepthPlan;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false,
                labelDepthPlan, diagnostic) &&
              labelDepthPlan.draws[0].blend &&
              labelDepthPlan.draws[0].depthTest &&
              !labelDepthPlan.draws[0].depthWrite &&
              labelDepthPlan.draws[0].depthFunction == CoinRenderDepthFunction::LEQUAL,
              "transparent label test-only LEQUAL state was not preserved");
  frame.renderStates[0].depthWrite = true;
  frame.renderStates[0].depthFunction = CoinRenderDepthFunction::LESS;
  frame.renderStates[0].lightModel = CoinRenderLightModel::PHONG;
  frame.lightingStates[0].ambientIntensity = 1.0f;
  CoinBgfxPlan litTransparentPlan;
  ok &= check(CoinBgfxLowering::lower(frame, 4, 4, false,
                litTransparentPlan, diagnostic) &&
              litTransparentPlan.draws[0].blend &&
              std::abs(litTransparentPlan.vertices[0].color[0] - 0.8f) < 1e-5f &&
              std::abs(litTransparentPlan.vertices[0].color[3] - 0.5f) < 1e-5f &&
              std::abs(litTransparentPlan.vertices[0].ambient[0] - 0.2f) < 1e-5f &&
              litTransparentPlan.vertices[0].material[1] == 1.0f &&
              litTransparentPlan.draws[0].ambientLight[3] == 1.0f,
              "transparent PHONG inputs and alpha were not lowered together");
  frame.renderStates[0].lightModel = CoinRenderLightModel::BASE_COLOR;
  frame.lightingStates[0].ambientIntensity = 0.2f;
  CoinBgfxTransparencyStrategy selected = CoinBgfxTransparencyStrategy::OBJECT;
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT &&
              diagnostic.empty(),
              "simple AUTO transparency must use object strategy");
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::AUTO, false, false, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT &&
              diagnostic.empty(),
              "simple AUTO transparency must not require advanced capabilities");
  std::vector<CoinBgfxDraw> opaqueDraws(12, plan.draws[0]);
  for (CoinBgfxDraw & opaque : opaqueDraws) opaque.blend = false;
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(opaqueDraws,
                CoinBgfxTransparencyMode::AUTO, false, false, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT &&
              diagnostic.empty(),
              "opaque-only AUTO frame must use the minimum-cost object path");
  std::vector<CoinBgfxDraw> boundaryDraws(7, plan.draws[0]);
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(boundaryDraws,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT,
              "AUTO must retain object below the complexity threshold");
  std::vector<CoinBgfxDraw> complexDraws(8, plan.draws[0]);
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(complexDraws,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT,
              "AUTO must preserve Coin ordering regardless of object count");
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(complexDraws,
                CoinBgfxTransparencyMode::OBJECT, false, false, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT,
              "explicit object mode must remain the compatibility override");
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(complexDraws,
                CoinBgfxTransparencyMode::AUTO, false, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::OBJECT,
              "ordinary Coin transparency must not require independent blending");
  ok &= check(!CoinBgfxLowering::selectTransparencyStrategy(complexDraws,
                CoinBgfxTransparencyMode::WEIGHTED_OIT, false, true,
                selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::WEIGHTED_OIT &&
              diagnostic.find("no fallback") != std::string::npos,
              "forced weighted_oit without MRT must fail without fallback");
  plan.draws[0].transparencyStrategy = CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::WEIGHTED_OIT,
              "sorted-triangle Coin mapping must select weighted_oit");
  plan.draws[0].transparencyStrategy = CoinBgfxTransparencyStrategy::SORTED_LAYERS;
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::AUTO, false, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::SORTED_LAYERS,
              "sorted_layers must take priority without requiring weighted OIT");
  ok &= check(!CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::AUTO, true, false, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
              diagnostic.find("no fallback") != std::string::npos,
              "missing depth peeling support must fail instead of falling back");
  ok &= check(!CoinBgfxLowering::selectTransparencyStrategy(plan.draws,
                CoinBgfxTransparencyMode::SORTED_LAYERS, true, false,
                selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::SORTED_LAYERS &&
              diagnostic.find("no fallback") != std::string::npos,
              "forced sorted_layers without depth targets must fail without fallback");
  CoinBgfxDraw weightedDraw = plan.draws[0];
  weightedDraw.transparencyStrategy = CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
  CoinBgfxDraw sortedDraw = plan.draws[0];
  sortedDraw.transparencyStrategy = CoinBgfxTransparencyStrategy::SORTED_LAYERS;
  std::vector<CoinBgfxDraw> mixedRequirements = {weightedDraw, sortedDraw};
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(mixedRequirements,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::SORTED_LAYERS,
              "highest-fidelity requirement must win in a mixed scene");
  std::reverse(mixedRequirements.begin(), mixedRequirements.end());
  ok &= check(CoinBgfxLowering::selectTransparencyStrategy(mixedRequirements,
                CoinBgfxTransparencyMode::AUTO, true, true, selected, diagnostic) &&
              selected == CoinBgfxTransparencyStrategy::SORTED_LAYERS,
              "mixed-scene selection must not depend on draw order");
  plan.draws[0].transparencyStrategy = CoinBgfxTransparencyStrategy::OBJECT;
  std::vector<CoinBgfxDraw> unordered(5, plan.draws[0]);
  for (auto & draw : unordered) { draw.deferred = true; draw.depthWrite = true; draw.depthFunction = CoinRenderDepthFunction::LESS; }
  unordered[0].blend = false;
  unordered[0].materialSignature = 2;
  unordered[0].firstIndex = 10;
  unordered[1].blend = true;
  unordered[1].materialSignature = 9;
  unordered[1].firstIndex = 20;
  unordered[2].blend = false;
  unordered[2].materialSignature = 1;
  unordered[2].firstIndex = 30;
  unordered[3].blend = true;
  unordered[3].materialSignature = 8;
  unordered[3].firstIndex = 40;
  unordered[4].blend = false;
  unordered[4].materialSignature = 1;
  unordered[4].firstIndex = 50;
  std::vector<CoinBgfxDraw> grouped;
  CoinBgfxLowering::groupOpaqueDraws(unordered, grouped);
  ok &= check(grouped.size() == unordered.size() &&
              grouped[0].firstIndex == 30 && grouped[1].firstIndex == 50 &&
              grouped[2].firstIndex == 10 &&
              grouped[3].firstIndex == 20 && grouped[4].firstIndex == 40,
              "opaque grouping changed stable material or transparent order");
  std::vector<CoinBgfxDraw> overlay = unordered;
  for (CoinBgfxDraw & draw : overlay) draw.renderLayer = 1;
  CoinBgfxLowering::groupOpaqueDraws(overlay, grouped);
  ok &= check(grouped.size() == overlay.size() &&
              grouped[0].firstIndex == 10 && grouped[1].firstIndex == 20 &&
              grouped[2].firstIndex == 30 && grouped[3].firstIndex == 40 &&
              grouped[4].firstIndex == 50,
              "overlay opaque/transparent draws must preserve traversal order");
  const auto checkUnsafeGroupingPreservesOrder = [&](CoinBgfxDraw unsafe,
                                                      const char * message) {
    std::vector<CoinBgfxDraw> input = unordered;
    input[2] = unsafe;
    input[2].firstIndex = 30;
    CoinBgfxLowering::groupOpaqueDraws(input, grouped);
    return check(grouped.size() == input.size() &&
                 grouped[0].firstIndex == 10 && grouped[1].firstIndex == 20 &&
                 grouped[2].firstIndex == 30 && grouped[3].firstIndex == 40 &&
                 grouped[4].firstIndex == 50, message);
  };
  CoinBgfxDraw unsafe = unordered[2];
  unsafe.depthFunction = CoinRenderDepthFunction::LEQUAL;
  ok &= checkUnsafeGroupingPreservesOrder(unsafe,
    "LEQUAL opaque draws must preserve traversal order");
  unsafe = unordered[2];
  unsafe.depthTest = false;
  ok &= checkUnsafeGroupingPreservesOrder(unsafe,
    "depth-test-disabled opaque draws must preserve traversal order");
  unsafe = unordered[2];
  unsafe.depthWrite = false;
  ok &= checkUnsafeGroupingPreservesOrder(unsafe,
    "depth-write-disabled opaque draws must preserve traversal order");
  unsafe = unordered[2];
  unsafe.depthRange[0] = 0.25f;
  unsafe.depthRange[1] = 0.75f;
  ok &= checkUnsafeGroupingPreservesOrder(unsafe,
    "non-default depth ranges must preserve traversal order");
  unsafe = unordered[2];
  unsafe.polygonOffsetUnits = -1.0f;
  ok &= checkUnsafeGroupingPreservesOrder(unsafe,
    "polygon-offset opaque draws must preserve traversal order");
  CoinBgfxPlan materialBase = movedPlan;
  CoinBgfxPlan materialUpdate = materialBase;
  materialUpdate.vertices[1].color[0] = 0.25f;
  materialUpdate.vertices[1].ambient[2] = 0.75f;
  materialUpdate.draws[0].materialSignature += 1;
  std::vector<CoinBgfxVertexRange> materialRanges;
  ok &= check(CoinBgfxLowering::materialPatchRanges(
                materialBase, materialUpdate, materialRanges) &&
              materialRanges.size() == 1 && materialRanges[0].first == 1 &&
              materialRanges[0].count == 1,
              "material-only update did not produce a minimal vertex range");
  materialUpdate.vertices[2].position[0] += 0.5f;
  ok &= check(!CoinBgfxLowering::materialPatchRanges(
                materialBase, materialUpdate, materialRanges) &&
              materialRanges.size() == 1,
              "geometry change was accepted as a material patch or published partial output");
  ok &= check(!CoinBgfxLowering::patchCamera(frame, 4, 4, false, plan, patched, diagnostic),
              "transparent camera patch must rebuild object order");
  frame.materials[0].diffuse[3] = 1.0f;
  frame.materials[0].transparency = 0.0f;
  frame.materials[0].diffuse[0] = std::numeric_limits<float>::quiet_NaN();
  ok &= check(!CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic),
              "non-finite color must be rejected");
  frame.materials[0].diffuse[0] = 0.8f;
  frame.renderStates[0].cullMode = static_cast<CoinRenderCullMode>(99);
  ok &= check(!CoinBgfxLowering::lower(frame, 4, 4, false, plan, diagnostic),
              "unknown cull state must be rejected");

  if (!ok) return 1;
  std::cout << "CoinBgfxCoreTest passed\n";
  return 0;
}
