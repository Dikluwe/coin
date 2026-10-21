#ifndef COIN_RENDER_TRANSPARENCY_CORE_H
#define COIN_RENDER_TRANSPARENCY_CORE_H

#include <Inventor/CoinRenderExport.h>

#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <cstdint>
#include <string>

static const uint32_t COIN_RENDER_MAX_PEEL_LAYERS = 8;

struct CoinRenderTransparencyOptions {
  CoinRenderTransparencyMode mode = COIN_RENDER_TRANSPARENCY_COIN;
  uint32_t layers = 4;
  uint64_t bufferBudget = uint64_t(256) * 1024 * 1024;
};

COIN_RENDER_DLL_API bool coin_render_same_transparency_options(const CoinRenderTransparencyOptions& a,
                                                  const CoinRenderTransparencyOptions& b);

// Conservative attachment budget, including depth and alignment overhead.
// Core owns the limit; Infra allocates its concrete formats below this bound.
COIN_RENDER_DLL_API bool coin_render_transparency_budget(uint32_t width, uint32_t height,
                                            const CoinRenderTransparencyOptions& options,
                                            bool allocateLayers, uint64_t& required,
                                            std::string& diagnostic);

// Same conservative attachment estimate for a multisampled transparency target.
COIN_RENDER_DLL_API bool coin_render_transparency_sample_budget(uint32_t width, uint32_t height,
    const CoinRenderTransparencyOptions & options, bool allocateLayers, uint32_t samples,
    uint64_t & required, std::string & diagnostic);

#endif
