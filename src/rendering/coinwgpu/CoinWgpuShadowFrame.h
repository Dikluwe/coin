#ifndef COIN_WGPU_SHADOW_FRAME_H
#define COIN_WGPU_SHADOW_FRAME_H

#include "rendering/coinrender/CoinRenderShadowCore.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"


#include <cstdint>
#include <string>
#include <vector>


CoinWgpuLight
coin_wgpu_pack_light(const CoinRenderLightSourceSnapshot & light);

// Infra transport for the Core-selected casters. These draws refer to the
// original frame vertex/index arrays, before composition reorders main draws.
struct CoinWgpuShadowPass {
  uint32_t mapSize = 0;
  uint32_t kind = 0; // 0=directional axial distance, 1=spot radial distance
  float nearDistance = 0.0f;
  float farDistance = 0.0f;
  float epsilon = 0.0f;
  float threshold = 0.0f;
  std::vector<CoinWgpuShadowDraw> casters;
  std::vector<CoinWgpuShadowReceiver> receivers;
};

struct CoinWgpuShadowFrame : CoinWgpuShadowPass {
  CoinWgpuShadowPass second;
  CoinWgpuShadowPass third;
  CoinWgpuShadowPass fourth;
  std::vector<CoinWgpuShadowPass> extra;
  bool hasSecond = false;
  bool hasThird = false;
  bool hasFourth = false;

  // Composition may clone a packed state to change depth policy. Its shadow
  // receiver stays identical; transport the same slot in every concrete pass.
  void appendReceiverState(uint32_t sourceSlot);

  bool prepare(
    const CoinRenderFramePlan & frame, std::string & diagnostic);
};


#endif // COIN_WGPU_SHADOW_FRAME_H
