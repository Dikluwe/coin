#ifndef COIN_SOWGPUFFIFRAME_H
#define COIN_SOWGPUFFIFRAME_H

#include "rendering/wgpu/SoWgpuFramePlan.h"
#include "rendering/wgpu/SoWgpuFrameReuseCore.h"
#include "rendering/wgpu/coin_wgpu_ffi.h"

#include <cstdint>
#include <string>
#include <vector>

// Private Infra owner for the storage referenced by CoinWgpuFrameView.
class SoWgpuFfiFrame {
public:
  SoWgpuFfiFrame();
  bool prepare(const FramePlan & frame, uint32_t width, uint32_t height,
               std::string & outDiagnostic);
  bool prepare(const FramePlan & frame, uint32_t width, uint32_t height,
               const SoWgpuFrameReuseDecision & reuse,
               std::string & outDiagnostic);
  const CoinWgpuFrameView & getView() const;
  bool reusedLastPrepare() const;
  SoWgpuFrameReuseKind lastPrepareKind() const;

private:
  SoWgpuFfiFrame(const SoWgpuFfiFrame &);
  SoWgpuFfiFrame & operator=(const SoWgpuFfiFrame &);
  bool packStates(const FramePlan & frame, std::string & outDiagnostic);
  void bindView(const FramePlan & frame, uint32_t width, uint32_t height);

  uint64_t packedRevision;
  bool reused;
  SoWgpuFrameReuseKind prepareKind;
  CoinWgpuFrameView view;
  std::vector<CoinWgpuVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<CoinWgpuDraw> draws;
  std::vector<CoinWgpuMaterial> materials;
  std::vector<CoinWgpuRenderState> states;
  std::vector<CoinWgpuTexture> textures;
  std::vector<CoinWgpuSampler> samplers;
  std::vector<std::vector<uint8_t> > texturePixels;
};

#endif // !COIN_SOWGPUFFIFRAME_H
