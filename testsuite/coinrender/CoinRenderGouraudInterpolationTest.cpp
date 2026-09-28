#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <cmath>
#include <iostream>

// Analytic oracle: three distinct emissive materials, clamped before
// perspective interpolation, with heterogeneous alpha. No implementation
// under test is used to calculate the expected primary colors.
static bool runCase(bool cpu, bool clipped, bool perspective) {
  CoinRenderFramePlan frame;
  frame.clearColor = SbColor4f(0, 0, 0, 1);
  frame.materials.resize(3);
  const float alpha[] = {0.2f, 0.6f, 1.0f};
  const float intensity[] = {2.0f, 0.7f, 0.9f};
  for (int i = 0; i < 3; ++i) {
    auto & mat = frame.materials[i];
    for (int c = 0; c < 3; ++c)
      mat.ambient[c] = mat.diffuse[c] = mat.specular[c] = mat.emission[c] = 0;
    mat.emission[i] = intensity[i];
    mat.diffuse[3] = alpha[i];
    mat.transparency = 1.0f - alpha[i];
  }
  frame.lightingStates.push_back(CoinRenderLightingSnapshot());
  frame.lightingStates[0].ambientIntensity = 0;
  frame.cameras.push_back(CoinRenderCameraSnapshot());
  CoinRenderViewportSnapshot viewport;
  viewport.width = viewport.height = 32;
  frame.viewports.push_back(viewport);
  CoinRenderRenderStateSnapshot state;
  state.transparencyType = SoGLRenderAction::SORTED_OBJECT_BLEND;
  state.lightModel = CoinRenderLightModel::PHONG;
  state.cullMode = CoinRenderCullMode::NONE;
  if (perspective) {
    state.projectionCoin = SbMatrix(1, 0, 0, 0, 0, 1, 0, 0,
                                   0, 0, -1.020202f, -1,
                                   0, 0, -0.2020202f, 0);
  }
  frame.renderStates.push_back(state);
  frame.vertices.resize(3);
  const float xy[3][2] = {{-1, -1}, {1, -1}, {0, 1}};
  for (int i = 0; i < 3; ++i) {
    frame.vertices[i].position[0] = xy[i][0];
    frame.vertices[i].position[1] = xy[i][1];
    frame.vertices[i].position[2] = perspective ? (i == 0 ? -1 : -2) :
                                  (clipped && i == 0 ? -1.5f : 0.5f);
    frame.vertices[i].normal[2] = 1;
    frame.vertices[i].materialSlot = i;
  }
  frame.indices = {0, 1, 2};
  CoinRenderDrawPacket draw;
  draw.geometry.vertexCount = draw.geometry.indexCount = 3;
  frame.draws.push_back(draw);

  // Independent 2D area weights at the center pixel, then reciprocal-W.
  const double px = 1.0 / 32.0, py = -1.0 / 32.0;
  const double w[] = {1, perspective ? 2.0 : 1.0, perspective ? 2.0 : 1.0};
  double x[3], y[3];
  for (int i = 0; i < 3; ++i) { x[i] = xy[i][0] / w[i]; y[i] = xy[i][1] / w[i]; }
  const double area = (y[1]-y[2])*(x[0]-x[2]) + (x[2]-x[1])*(y[0]-y[2]);
  double b[3];
  b[0] = ((y[1]-y[2])*(px-x[2]) + (x[2]-x[1])*(py-y[2])) / area;
  b[1] = ((y[2]-y[0])*(px-x[2]) + (x[0]-x[2])*(py-y[2])) / area;
  b[2] = 1-b[0]-b[1];
  double sum = 0;
  for (int i = 0; i < 3; ++i) { b[i] /= w[i]; sum += b[i]; }
  double opacity = 0;
  for (int i = 0; i < 3; ++i) { b[i] /= sum; opacity += b[i]*alpha[i]; }

  CoinRenderTargetP target(SbVec2i32(32, 32));
  target.depthReadbackEnabled = false;
  if (cpu) {
    target.backend.reset(new CoinRenderCpuReferenceBackend);
    target.depthBuffer.assign(32*32, 1.0f);
  }
  const CoinRenderFrameExecutionResult result = target.executeFrame(frame);
  if (result.status != CoinRenderBackendStatus::SUCCESS || target.colorBuffer.size() != 32*32*4) {
    std::cerr << "Gouraud interpolation submit failed: " << result.diagnostic << '\n';
    return false;
  }
  for (int c = 0; c < 3; ++c) {
    const double expected = b[c] * std::min(1.0f, intensity[c]) * opacity * 255;
    const int actual = target.colorBuffer[(16*32+16)*4+c];
    if (std::abs(actual-expected) > 2) {
      std::cerr << "Gouraud interpolation mismatch cpu=" << cpu << " clipped=" << clipped
                << " perspective=" << perspective << " channel=" << c
                << " actual=" << actual << " expected=" << expected << '\n';
      return false;
    }
  }
  return true;
}

int main() {
  SoDB::init();
  for (bool cpu : {true, false}) {
#if !defined(HAVE_WGPU_BGFX) && !defined(HAVE_COIN_WGPU_RUST_BRIDGE)
    if (!cpu) continue;
#endif
    if (!runCase(cpu, false, false) || !runCase(cpu, true, false) ||
        !runCase(cpu, false, true)) return 1;
  }
  std::cout << "CoinRenderGouraudInterpolationTest passed\n";
  return 0;
}
