#ifndef COIN_RENDER_RESOURCE_CORE_H
#define COIN_RENDER_RESOURCE_CORE_H
#include <cstdint>
// Nominal packed output admission; physical staging memory belongs to Infra.
inline bool coin_render_readback_admitted(uint64_t jobs, uint64_t bytes, uint64_t requested) {
  const uint64_t budget = UINT64_C(128) * 1024 * 1024;
  return jobs < 16 && requested <= budget && bytes <= budget - requested;
}
#endif
