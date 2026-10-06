#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <iostream>
#include <vector>

// Repeated geometry deliberately prevents baking/merging. 25,600 independent
// draw uniforms cross the 64 MiB arena boundary and exceed D3D12's old sampler
// descriptor allocation pattern. Check every cell, not just successful submit.
int main()
{
  if (!coin_wgpu_is_available()) return 77;
  const uint32_t columns = 200, rows = 128, width = columns * 5, height = rows * 5;
  CoinRenderFramePlan frame;
  frame.revision = 701;
  frame.materials.resize(2);
  for (int color = 0; color < 2; ++color) {
    auto & material = frame.materials[color];
    material.diffuse[0] = color ? 0 : 1;
    material.diffuse[1] = color ? 1 : 0;
    material.diffuse[2] = 0;
    material.diffuse[3] = 1;
  }
  frame.vertices.resize(6);
  for (uint32_t i = 0; i < 6; ++i) {
    auto & vertex = frame.vertices[i];
    vertex.position[0] = i % 3 == 1 ? 2.0f / columns : 0;
    vertex.position[1] = i % 3 == 2 ? -2.0f / rows : 0;
    vertex.normal[2] = 1;
    vertex.materialSlot = i / 3;
    vertex.screenSpaceW = 1;
    vertex.fogEyeDepth = -1;
    frame.indices.push_back(i);
  }
  frame.renderStates.resize(columns * rows);
  frame.draws.resize(columns * rows);
  for (uint32_t i = 0; i < columns * rows; ++i) {
    auto & state = frame.renderStates[i];
    state.lightModel = CoinRenderLightModel::BASE_COLOR;
    state.cullMode = CoinRenderCullMode::NONE;
    state.materialSlot = i % 2;
    state.model.setTranslate(SbVec3f(-1 + 2.0f * (i % columns) / columns,
                                    1 - 2.0f * (i / columns) / rows, 0));
    auto & draw = frame.draws[i];
    draw.renderStateSlot = i;
    draw.geometry.firstVertex = draw.geometry.firstIndex = (i % 2) * 3;
    draw.geometry.vertexCount = draw.geometry.indexCount = 3;
  }
  CoinWgpuFfiFrame packed;
  std::vector<uint8_t> pixels(width * height * 4);
  CoinWgpuTarget target{};
  target.width = width; target.height = height;
  target.color_buffer = pixels.data(); target.color_buffer_len = pixels.size();
  for (int phase = 0; phase < 4; ++phase) {
    if (phase == 2) {
      ++frame.revision;
      frame.materials[0].diffuse[0] = 0;
      frame.materials[0].diffuse[2] = 1;
    }
    if (phase == 3) {
      ++frame.revision;
      for (auto & state : frame.renderStates)
        state.view.setTranslate(SbVec3f(2.0f / columns, 0, 0));
    }
    std::string error;
    if (!packed.prepare(frame, width, height, error) ||
        packed.getView().draw_count != columns * rows) {
      std::cerr << "Large bindings packing failed: " << error << '\n';
      return 1;
    }
    char gpuError[1024] = {};
    if (coin_wgpu_submit(&target, &packed.getView(), gpuError, sizeof(gpuError)) != COIN_WGPU_OK) {
      std::cerr << "Large bindings phase " << phase << ": " << gpuError << '\n';
      return 1;
    }
    for (uint32_t row = 0; row < rows; ++row) {
      for (uint32_t col = 0; col < columns; ++col) {
        const bool clear = phase == 3 && col == 0;
        const bool green = ((col - (phase == 3 && col != 0 ? 1 : 0)) % 2) != 0;
        const uint8_t expected[] = {
          uint8_t(!clear && !green && phase < 2 ? 255 : 0),
          uint8_t(!clear && green ? 255 : 0),
          uint8_t(!clear && !green && phase >= 2 ? 255 : 0), 255
        };
        const size_t offset = ((row * 5 + 1) * width + col * 5 + 1) * 4;
        for (int channel = 0; channel < 4; ++channel) {
          if (pixels[offset + channel] != expected[channel]) {
            std::cerr << "Large bindings phase " << phase << " cell " << col << ',' << row
                      << " channel " << channel << " got " << int(pixels[offset + channel])
                      << " expected " << int(expected[channel]) << '\n';
            return 1;
          }
        }
      }
    }
  }
  std::cout << "25,600 draw uniforms, static reuse, material and camera updates passed\n";
  return 0;
}
