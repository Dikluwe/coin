#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/wgpu/SoWgpuBgfxCore.h"

#include <Inventor/SoDB.h>

#include <cmath>
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
  ok &= check(std::abs(plan.draws[0].mvp[10] - 0.5f) < 1e-6f &&
              std::abs(plan.draws[0].mvp[14] - 0.5f) < 1e-6f,
              "Coin clip depth was not converted to Vulkan range");
  const size_t originalDrawCount = plan.draws.size();
  frame.renderStates[0].fogMode = FogMode::FOG;
  ok &= check(!SoWgpuBgfxCore::lower(frame, 4, 4, false, plan, diagnostic) &&
              !diagnostic.empty() && plan.draws.size() == originalDrawCount,
              "unsupported fog must fail transactionally");
  frame.renderStates[0].fogMode = FogMode::NONE;
  frame.materials[0].diffuse[3] = 0.5f;
  frame.materials[0].transparency = 0.5f;
  ok &= check(!SoWgpuBgfxCore::lower(frame, 4, 4, false, plan, diagnostic),
              "transparent material must not render as opaque");
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
