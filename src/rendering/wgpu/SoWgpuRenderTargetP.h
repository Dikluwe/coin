#ifndef SOWGPURENDERTARGETP_H
#define SOWGPURENDERTARGETP_H

#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include "rendering/wgpu/SoWgpuFramePlan.h"
#include <Inventor/SbVec2i32.h>
#include <vector>
#include <cstdint>
#include <string>

class SoWgpuRenderTargetP {
public:
  SoWgpuRenderTargetP(const SbVec2i32 & sz = SbVec2i32(0, 0));
  ~SoWgpuRenderTargetP();

  SoWgpuRenderTarget::Status status;
  SbVec2i32 size;
  std::vector<uint8_t> colorBuffer; // RGBA8
  std::vector<float> depthBuffer;   // Depth [0, 1]

  bool resize(const SbVec2i32 & newSize);
  void clear(float r, float g, float b, float a, float depthVal = 1.0f);
  void readbackRGBA(std::vector<uint8_t> & outRgba) const;

  // Preflight validation according to Onda 1 profile (Section 4.6)
  static bool validateProfile(const FramePlan & frame, std::string & outDiagnostic);

  // Render execution for offscreen target
  bool executeFrame(const FramePlan & frame, std::string & outError);
};

#endif // !SOWGPURENDERTARGETP_H
