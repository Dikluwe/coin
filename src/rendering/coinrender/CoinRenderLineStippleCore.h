#ifndef COIN_RENDER_LINE_STIPPLE_CORE_H
#define COIN_RENDER_LINE_STIPPLE_CORE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

// Unit-width diamond-exit fragments define the polygon stipple counter.
// Wide strokes replicate each bit across the minor axis. Coordinates are
// relative to the integer viewport origin; no backend or scene state.
inline bool coin_render_polygon_stipple(double x0, double y0, double x1, double y1,
                                        uint32_t pattern, int repeat, uint32_t& phase,
                                        std::vector<std::pair<float, float>>& spans) {
  spans.clear();
  if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1))
    return false;
  const double dx = x1 - x0, dy = y1 - y0;
  const bool xMajor = std::abs(dx) >= std::abs(dy);
  const double major0 = xMajor ? x0 : y0, major1 = xMajor ? x1 : y1;
  const double delta = major1 - major0;
  if (std::abs(delta) <= 1e-9)
    return true;
  // Bound the work for raw Core callers before converting coordinates.
  if (std::abs(x0) > 65536 || std::abs(x1) > 65536 || std::abs(y0) > 65536 ||
      std::abs(y1) > 65536 || std::abs(delta) > 65536)
    return false;
  repeat = std::max(1, std::min(256, repeat));
  const uint32_t period = 16u * static_cast<uint32_t>(repeat);
  phase %= period;
  const int step = delta > 0 ? 1 : -1;
  const int last = static_cast<int>(std::floor(major1));
  // The spec's negative infinitesimal resolves diamond boundary ties.
  const double px = x0 - 1e-5, py = y0 - 1e-10;
  for (int cell = static_cast<int>(std::floor(major0));; cell += step) {
    const double center = cell + .5;
    const double t = (center - major0) / delta;
    const double minor = xMajor ? y0 + t * dy : x0 + t * dx;
    const int row = static_cast<int>(std::floor(minor));
    bool fragment = false;
    for (int candidate = row - 1; candidate <= row + 1 && !fragment; ++candidate) {
      const double cx = xMajor ? center : candidate + .5;
      const double cy = xMajor ? candidate + .5 : center;
      double enter = 0, leave = 1;
      bool hit = true;
      for (int nx : {-1, 1})
        for (int ny : {-1, 1}) {
          const double a = nx * (px - cx) + ny * (py - cy);
          const double b = nx * dx + ny * dy;
          if (b == 0) {
            if (a >= .5)
              hit = false;
          } else if (b > 0) {
            leave = std::min(leave, (.5 - a) / b);
          } else {
            enter = std::max(enter, (.5 - a) / b);
          }
        }
      // A terminal endpoint inside the diamond produces no fragment.
      fragment = hit && leave > enter && leave > 0 && enter < 1 && leave < 1;
    }
    if (fragment) {
      if (pattern & (1u << (phase / static_cast<uint32_t>(repeat)))) {
        double a = (cell - major0) / delta, b = (cell + 1 - major0) / delta;
        if (a > b)
          std::swap(a, b);
        // Cover the complete raster cell, including the start pixel. The
        // expander splits caps at source endpoints to preserve attributes.
        const float first = static_cast<float>(a);
        const float end = static_cast<float>(b);
        if (end > first) {
          if (!spans.empty() && std::abs(spans.back().second - first) < 1e-6f)
            spans.back().second = end;
          else
            spans.emplace_back(first, end);
        }
      }
      phase = (phase + 1) % period;
    }
    if (cell == last)
      break;
  }
  return true;
}
#endif
