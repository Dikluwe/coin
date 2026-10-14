#ifndef COIN_RENDER_STATE_CORE_H
#define COIN_RENDER_STATE_CORE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <cstring>

template <size_t N>
inline bool coin_render_same_texture_units_data(const CoinRenderTextureUnitSnapshot * a,
                                                const CoinRenderTextureUnitSnapshot * b)
{
  // Compare values, never the padding following the enabled flag.
  for (size_t i = 0; i < N; ++i) {
    if (a[i].enabled != b[i].enabled || a[i].imageSlot != b[i].imageSlot ||
        a[i].samplerSlot != b[i].samplerSlot || a[i].model != b[i].model ||
        std::memcmp(a[i].blendColor, b[i].blendColor, sizeof(a[i].blendColor)) != 0 ||
        a[i].matrix != b[i].matrix) return false;
  }
  return true;
}

template <size_t N>
inline bool coin_render_same_texture_units(const CoinRenderSparseArray<CoinRenderTextureUnitSnapshot, N> & a,
                                           const CoinRenderSparseArray<CoinRenderTextureUnitSnapshot, N> & b)
{
  return coin_render_same_texture_units_data<N>(a.data(), b.data());
}

template <size_t N>
inline bool coin_render_same_texture_units(const CoinRenderTextureUnitSnapshot (&a)[N],
                                           const CoinRenderTextureUnitSnapshot (&b)[N])
{
  return coin_render_same_texture_units_data<N>(a, b);
}

COIN_RENDER_DLL_API bool
coin_render_same_state_except_camera(const CoinRenderRenderStateSnapshot & a,
                            const CoinRenderRenderStateSnapshot & b);
#endif
