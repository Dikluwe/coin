#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderPhaseTimer.h"
#include <cstdio>
#include <cstdlib>

CoinRenderPhaseTimer::CoinRenderPhaseTimer(const char *scope)
    : enabled(tracingEnabled()), scope(scope)
{
  if (enabled)
    previous = Clock::now();
}

void
CoinRenderPhaseTimer::mark(const char *name)
{
  if (!enabled || count == 12)
    return;
  const auto now = Clock::now();
  samples[count++] = {name, std::chrono::duration<double, std::milli>(now - previous).count()};
  previous = now;
}

CoinRenderPhaseTimer::~CoinRenderPhaseTimer()
{
  if (!enabled)
    return;
  mark("finish");
  std::fprintf(stderr, "COIN_RENDER_PHASE %s", scope);
  for (unsigned i = 0; i < count; ++i)
    std::fprintf(stderr, " %s_ms=%.6f", samples[i].name, samples[i].ms);
  std::fprintf(stderr, "\n");
}

bool
CoinRenderPhaseTimer::tracingEnabled()
{
  const char *value = std::getenv("COIN_RENDER_TRACE_PHASES");
  if (!value)
    value = std::getenv("COIN_WGPU_TRACE_PHASES");
  return value != nullptr;
}
