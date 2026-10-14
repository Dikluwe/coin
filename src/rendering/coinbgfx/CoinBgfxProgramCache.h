#ifndef COIN_BGFX_PROGRAM_CACHE_H
#define COIN_BGFX_PROGRAM_CACHE_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

// Immutable entries keep BGFX's separate size/read callbacks consistent.
// BGFX keys OpenGL binaries by shaders, platform, GPU and driver version.
class CoinBgfxProgramCache {
public:
  explicit CoinBgfxProgramCache(size_t budget = 32u * 1024u * 1024u,
                               size_t entryLimit = 8u * 1024u * 1024u,
                               size_t countLimit = 128);

  uint32_t size(uint64_t key) const;

  bool read(uint64_t key, void * data, uint32_t size) const;

  bool write(uint64_t key, const void * data, uint32_t size);

private:
  const size_t budget, entryLimit, countLimit;
  mutable std::mutex mutex;
  std::map<uint64_t, std::vector<uint8_t>> entries;
  size_t bytes = 0;
};
#endif
