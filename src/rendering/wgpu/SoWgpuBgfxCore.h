#ifndef COIN_SOWGPUBGFXCORE_H
#define COIN_SOWGPUBGFXCORE_H

#include "rendering/wgpu/SoWgpuFramePlan.h"

#include <cstdint>
#include <string>
#include <vector>

// Backend-neutral lowering for the deliberately narrow BGFX evaluation profile.
// No BGFX headers or GPU state leak into Core or Open Inventor traversal.
struct SoWgpuBgfxVertex {
  float position[3];
  float color[4];
};

struct SoWgpuBgfxDraw {
  float mvp[16];
  uint32_t firstVertex;
  uint32_t vertexCount;
  uint32_t firstIndex;
  uint32_t indexCount;
  CullMode cullMode;
  FrontFace frontFace;
};

struct SoWgpuBgfxPlan {
  std::vector<SoWgpuBgfxVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<SoWgpuBgfxDraw> draws;
  uint32_t clearRgba;
};

class SoWgpuBgfxCore {
public:
  static bool lower(const FramePlan & frame, int width, int height,
                    bool homogeneousDepth, SoWgpuBgfxPlan & output,
                    std::string & diagnostic);
};

#endif
