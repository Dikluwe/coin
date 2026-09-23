#include <Inventor/CoinWgpuExport.h>
#ifndef SOWGPURENDERTARGETP_H
#define SOWGPURENDERTARGETP_H

#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>
#include "rendering/wgpu/SoWgpuFramePlan.h"
#include <Inventor/SbVec2i32.h>
#include "rendering/wgpu/SoWgpuBackend.h"
#include <vector>
#include <cstdint>
#include <string>
#include <memory>

class COIN_WGPU_DLL_API SoWgpuRenderTargetP {
public:
  enum TargetKind {
    KIND_OFFSCREEN = 0,
    KIND_WINDOW = 1
  };

  SoWgpuRenderTargetP(const SbVec2i32 & sz = SbVec2i32(0, 0));
  ~SoWgpuRenderTargetP();

  TargetKind kind{KIND_OFFSCREEN};
  SoWgpuRenderTarget::Status status{SoWgpuRenderTarget::TARGET_READY};
  SbVec2i32 size{0, 0};
  std::vector<uint8_t> colorBuffer; // RGBA8 (offscreen)
  std::vector<float> depthBuffer;   // Depth [0, 1] (offscreen software / CPU fallback)
  uint32_t generation{0};

  // Window surface specific members
  SoWgpuNativeSurfaceDescriptor nativeDesc{};
  uint64_t surfaceId{0};
  bool suspended{false};
  bool needsReconfigure{false};
  std::string lastError;

  bool initWindow(const SoWgpuNativeSurfaceDescriptor & desc, const SbVec2i32 & fbSize);
  bool resize(const SbVec2i32 & newSize);
  void clear(float r, float g, float b, float a, float depthVal = 1.0f);
  void readbackRGBA(std::vector<uint8_t> & outRgba) const;
  void readbackDepth(std::vector<float> & outDepth) const;
  uint64_t lastSubmissionSerial{0};

  // Preflight validation according to Onda 1 profile (Section 4.6)
  static FrameExecutionResult validateProfile(const FramePlan & frame, const SbVec2i32 & targetSize);
  static bool validateProfile(const FramePlan & frame, std::string & outDiagnostic);

  // Render execution for target (offscreen or window)
  FrameExecutionResult executeFrame(const FramePlan & frame);

  std::unique_ptr<SoWgpuBackend> backend;
};

#endif // !SOWGPURENDERTARGETP_H
