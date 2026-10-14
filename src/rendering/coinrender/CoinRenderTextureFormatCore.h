#ifndef COIN_RENDER_TEXTURE_FORMAT_CORE_H
#define COIN_RENDER_TEXTURE_FORMAT_CORE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"

// Mechanical format rules, independent of Coin nodes, callbacks and GPU APIs.
class CoinRenderTextureFormatCore {
public:
  COIN_RENDER_DLL_API static bool valid(CoinRenderTextureFormat f);
  COIN_RENDER_DLL_API static bool srgb(CoinRenderTextureFormat f);
  COIN_RENDER_DLL_API static bool compressed(CoinRenderTextureFormat f);
  COIN_RENDER_DLL_API static size_t levelBytes(uint32_t w, uint32_t h, CoinRenderTextureFormat f);
  COIN_RENDER_DLL_API static size_t mipBytes(uint32_t w, uint32_t h, CoinRenderTextureFormat f);
  COIN_RENDER_DLL_API static float decodeSrgb(float c);
  COIN_RENDER_DLL_API static float encodeSrgb(float c);
  COIN_RENDER_DLL_API static uint8_t unorm(float x);
  // IEEE binary16 conversion, round-to-nearest-even; no host half dependency.
  COIN_RENDER_DLL_API static float fromHalf(uint16_t h);
  COIN_RENDER_DLL_API static uint16_t toHalf(float x);
  COIN_RENDER_DLL_API static uint16_t read16(const uint8_t *p);
  COIN_RENDER_DLL_API static void write16(uint8_t *p, uint16_t n);
  COIN_RENDER_DLL_API static void color565(uint16_t n, uint8_t rgb[3]);
  COIN_RENDER_DLL_API static void bcPalette(const uint8_t *block, uint8_t rgb[4][3],
                        uint8_t alpha[8]);
  COIN_RENDER_DLL_API static SbVec4f texel(const std::vector<uint8_t> &bytes, size_t offset,
                       uint32_t w, CoinRenderTextureFormat f, uint32_t x,
                       uint32_t y);
  COIN_RENDER_DLL_API static void put(std::vector<uint8_t> &bytes, size_t p,
                  CoinRenderTextureFormat f, const SbVec4f &value);
  COIN_RENDER_DLL_API static bool finiteHalfPayload(const std::vector<uint8_t> &bytes);
  // Bounded deterministic BC3 encoder. Input is encoded RGBA8, never linearized
  // SRGB values. Every edge block repeats its final valid texel.
  COIN_RENDER_DLL_API static bool encodeBc3(const std::vector<uint8_t> &input, uint32_t w,
                        uint32_t h, std::vector<uint8_t> &output);
};
#endif
