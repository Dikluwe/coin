#ifndef COIN_RENDER_PHASE_TIMER_H
#define COIN_RENDER_PHASE_TIMER_H

#include <Inventor/CoinRenderExport.h>

#include <chrono>

// Collect CPU intervals without printing inside a measured section. Disabled
// tracing neither reads the clock nor allocates. These intervals are nested
// inside the existing action/target/backend records, not additional costs.
class CoinRenderPhaseTimer {
public:
  COIN_RENDER_DLL_API explicit CoinRenderPhaseTimer(const char * scope);
  COIN_RENDER_DLL_API void mark(const char * name);
  COIN_RENDER_DLL_API ~CoinRenderPhaseTimer();
private:
  // Keep this header usable by standalone Core tests/packers that do not link
  // CoinRenderDiagnosticShell. Matches environmentOption, including its alias.
  COIN_RENDER_DLL_API static bool tracingEnabled();
  using Clock = std::chrono::steady_clock;
  struct Sample { const char * name; double ms; };
  bool enabled;
  const char * scope;
  Clock::time_point previous;
  Sample samples[12];
  unsigned count = 0;
};

#endif
