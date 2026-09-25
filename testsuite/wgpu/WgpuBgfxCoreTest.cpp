#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuBgfxCore.h"

#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/SoDB.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char * message)
{
  if (!condition) std::cerr << "WgpuBgfxCoreTest: " << message << '\n';
  return condition;
}
}

int main()
{
  SoDB::init();
  FramePlan frame;
  frame.clearColor = SbColor4f(0.1f, 0.2f, 0.3f, 1.0f);
  frame.materials.push_back(MaterialSnapshot());
  frame.lightingStates.push_back(LightingSnapshot());
  frame.cameras.push_back(CameraSnapshot());
  ViewportSnapshot viewport;
  viewport.width = 4;
  viewport.height = 4;
  frame.viewports.push_back(viewport);
  RenderStateSnapshot state;
  state.lightModel = LightModel::BASE_COLOR;
  frame.renderStates.push_back(state);
  frame.vertices.resize(3);
  frame.indices = {0, 1, 2};
  DrawPacket draw;
  draw.geometry.vertexCount = 3;
  draw.geometry.indexCount = 3;
  frame.draws.push_back(draw);

  bool ok = true;
  std::string diagnostic;
  SoWgpuBgfxPlan plan;
  ok &= check(SoWgpuBgfxCore::lower(frame, 4, 4, false, plan, diagnostic),
              "valid BASE_COLOR triangle rejected");
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
  std::vector<SoWgpuBgfxDraw> patched;
  SoWgpuBgfxPlan movedPlan;
  ok &= check(SoWgpuBgfxCore::patchCamera(frame, false, plan, patched, diagnostic) &&
              SoWgpuBgfxCore::lower(frame, 4, 4, false, movedPlan, diagnostic) &&
              patched.size() == 1 &&
              std::memcmp(patched[0].mvp, movedPlan.draws[0].mvp,
                          sizeof(patched[0].mvp)) == 0,
              "camera patch differs from full lowering");
  frame.clearColor = SbColor4f(0.1001f, 0.2f, 0.3f, 1.0f);
  ok &= check(!SoWgpuBgfxCore::patchCamera(frame, false, plan, patched, diagnostic),
              "camera patch accepted a changed float clear color");
  frame.clearColor = SbColor4f(0.1f, 0.2f, 0.3f, 1.0f);
  frame.draws[0].geometry.indexCount = 2;
  ok &= check(!SoWgpuBgfxCore::patchCamera(frame, false, plan, patched, diagnostic) &&
              patched.size() == 1 &&
              std::memcmp(patched[0].mvp, movedPlan.draws[0].mvp,
                          sizeof(patched[0].mvp)) == 0,
              "invalid camera patch must not publish partial draws");
  frame.draws[0].geometry.indexCount = 3;
  SbMatrix invalidCamera = moved;
  invalidCamera[0][0] = std::numeric_limits<float>::quiet_NaN();
  frame.renderStates[0].view = invalidCamera;
  ok &= check(!SoWgpuBgfxCore::patchCamera(frame, false, plan, patched, diagnostic),
              "non-finite camera patch must be rejected");
  frame.renderStates[0].view = moved;
  const size_t originalDrawCount = plan.draws.size();
  frame.renderStates[0].fogMode = FogMode::FOG;
  ok &= check(!SoWgpuBgfxCore::lower(frame, 4, 4, false, plan, diagnostic) &&
              !diagnostic.empty() && plan.draws.size() == originalDrawCount,
              "unsupported fog must fail transactionally");
  frame.renderStates[0].fogMode = FogMode::NONE;
  frame.materials[0].diffuse[3] = 0.5f;
  frame.materials[0].transparency = 0.5f;
  ok &= check(!SoWgpuBgfxCore::lower(frame, 4, 4, false, plan, diagnostic) &&
              diagnostic.find("SORTED_OBJECT_BLEND") != std::string::npos,
              "unselected transparency mode must be rejected");
  frame.renderStates[0].transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  ok &= check(SoWgpuBgfxCore::lower(frame, 4, 4, false, plan, diagnostic) &&
              plan.draws.size() == 1 && plan.draws[0].blend &&
              std::abs(plan.draws[0].alpha - 0.5f) < 1e-6f,
              "SORTED_OBJECT_BLEND alpha was not lowered");
  ok &= check(!SoWgpuBgfxCore::patchCamera(frame, false, plan, patched, diagnostic),
              "transparent camera patch must rebuild object order");
  frame.materials[0].diffuse[3] = 1.0f;
  frame.materials[0].transparency = 0.0f;
  frame.materials[0].diffuse[0] = std::numeric_limits<float>::quiet_NaN();
  ok &= check(!SoWgpuBgfxCore::lower(frame, 4, 4, false, plan, diagnostic),
              "non-finite color must be rejected");
  frame.materials[0].diffuse[0] = 0.8f;
  frame.renderStates[0].cullMode = static_cast<CullMode>(99);
  ok &= check(!SoWgpuBgfxCore::lower(frame, 4, 4, false, plan, diagnostic),
              "unknown cull state must be rejected");

  if (!ok) return 1;
  std::cout << "WgpuBgfxCoreTest passed\n";
  return 0;
}
