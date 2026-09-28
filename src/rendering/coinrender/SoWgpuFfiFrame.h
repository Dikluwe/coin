#ifndef COIN_SOWGPUFFIFRAME_H
#define COIN_SOWGPUFFIFRAME_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/coin_wgpu_ffi.h"

#include <cstdint>
#include <string>
#include <vector>

// Private Infra owner for the storage referenced by CoinWgpuFrameView.
class SoWgpuFfiFrame {
public:
  SoWgpuFfiFrame();
  bool prepare(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height,
               std::string & outDiagnostic);
  bool prepare(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height,
               const CoinRenderFrameReuseDecision & reuse,
               std::string & outDiagnostic);
  const CoinWgpuFrameView & getView() const;
  bool reusedLastPrepare() const;
  CoinRenderFrameReuseKind lastPrepareKind() const;

private:
  SoWgpuFfiFrame(const SoWgpuFfiFrame &);
  SoWgpuFfiFrame & operator=(const SoWgpuFfiFrame &);
  bool packStates(const CoinRenderFramePlan & frame, uint32_t targetWidth, uint32_t targetHeight,
                  std::string & outDiagnostic);
  void bindView(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height);

  uint64_t packedRevision;
  bool reused;
  CoinRenderFrameReuseKind prepareKind;
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
