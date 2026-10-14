#ifndef COIN_RENDER_SELECTION_CORE_H
#define COIN_RENDER_SELECTION_CORE_H

#include <Inventor/CoinRenderExport.h>

#include <Inventor/rendering/CoinRenderOptions.h>
#include <string>

COIN_RENDER_DLL_API bool coin_render_valid_options(const CoinRenderOptions& options, std::string& diagnostic);

// Pure selection: hardware facts and implementation/profile evidence are inputs.
// No probe, scene traversal, environment parsing or GPU commands occur here.
COIN_RENDER_DLL_API CoinRenderSelection coin_render_selection(uint64_t required, uint64_t implemented,
                                                 uint64_t available, uint64_t qualified,
                                                 bool requireQualified = false);

COIN_RENDER_DLL_API CoinRenderSelection coin_render_requested_mechanism(CoinRenderTransparencyMode mode,
                                                           uint64_t required, bool enabled);

COIN_RENDER_DLL_API const char* coin_render_selection_diagnostic(uint32_t reason);

#endif
