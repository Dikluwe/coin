#ifndef COIN_RENDER_LINE_STIPPLE_CORE_H
#define COIN_RENDER_LINE_STIPPLE_CORE_H

#include <Inventor/CoinRenderExport.h>

#include <cstdint>
#include <utility>
#include <vector>

// Unit-width diamond-exit fragments define the line stipple counter.
// Wide strokes replicate each bit across the minor axis. Coordinates are
// relative to the integer viewport origin; no backend or scene state.
COIN_RENDER_DLL_API bool coin_render_line_stipple(double x0, double y0, double x1, double y1, uint32_t pattern,
                                     int repeat, uint32_t& phase,
                                     std::vector<std::pair<float, float>>& spans);
#endif
