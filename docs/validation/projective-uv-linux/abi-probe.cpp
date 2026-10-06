#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include <cstdio>
int main() {
  std::printf("{\"protocol\":%d,\"common_vertex\":%zu,\"common_state\":%zu,\"wgpu_vertex\":%zu,\"wgpu_state\":%zu,\"bgfx_vertex\":%zu,\"bgfx_compact_vertex\":%zu}\n",
    COIN_WGPU_BRIDGE_PROTOCOL_REVISION, sizeof(CoinRenderVertexSnapshot), sizeof(CoinRenderRenderStateSnapshot),
    sizeof(CoinWgpuVertex), sizeof(CoinWgpuRenderState), sizeof(CoinBgfxVertex), sizeof(CoinBgfxVertexPrefix));
}
