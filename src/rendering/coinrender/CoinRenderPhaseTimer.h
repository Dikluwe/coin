#ifndef COIN_RENDER_PHASE_TIMER_H
#define COIN_RENDER_PHASE_TIMER_H

#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include <chrono>
#include <cstdio>

// Collect CPU intervals without printing inside a measured section. Disabled
// tracing neither reads the clock nor allocates. These intervals are nested
// inside the existing action/target/backend records, not additional costs.
class CoinRenderPhaseTimer {
public:
  explicit CoinRenderPhaseTimer(const char * scope)
    : enabled(CoinRenderDiagnosticShell::phaseTracingEnabled()), scope(scope) {
    if (enabled) previous = Clock::now();
  }
  void mark(const char * name) {
    if (!enabled || count == 12) return;
    const auto now = Clock::now();
    samples[count++] = {name,
      std::chrono::duration<double, std::milli>(now - previous).count()};
    previous = now;
  }
  ~CoinRenderPhaseTimer() {
    if (!enabled) return;
    mark("finish");
    std::fprintf(stderr, "COIN_RENDER_PHASE %s", scope);
    for (unsigned i = 0; i < count; ++i)
      std::fprintf(stderr, " %s_ms=%.6f", samples[i].name, samples[i].ms);
    std::fprintf(stderr, "\n");
  }
private:
  using Clock = std::chrono::steady_clock;
  struct Sample { const char * name; double ms; };
  bool enabled;
  const char * scope;
  Clock::time_point previous;
  Sample samples[12];
  unsigned count = 0;
};

#endif
