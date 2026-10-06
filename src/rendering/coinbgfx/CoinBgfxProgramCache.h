#ifndef COIN_BGFX_PROGRAM_CACHE_H
#define COIN_BGFX_PROGRAM_CACHE_H

#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <vector>

// Immutable entries keep BGFX's separate size/read callbacks consistent.
// BGFX keys OpenGL binaries by shaders, platform, GPU and driver version.
class CoinBgfxProgramCache {
public:
  explicit CoinBgfxProgramCache(size_t budget = 32u * 1024u * 1024u,
                               size_t entryLimit = 8u * 1024u * 1024u,
                               size_t countLimit = 128)
    : budget(budget), entryLimit(entryLimit), countLimit(countLimit) {}

  uint32_t size(uint64_t key) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    const auto entry = this->entries.find(key);
    return entry == this->entries.end() ? 0 : static_cast<uint32_t>(entry->second.size());
  }

  bool read(uint64_t key, void * data, uint32_t size) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    const auto entry = this->entries.find(key);
    if (!data || entry == this->entries.end() || entry->second.size() != size) return false;
    std::memcpy(data, entry->second.data(), size);
    return true;
  }

  bool write(uint64_t key, const void * data, uint32_t size) {
    if (!data || size <= sizeof(uint32_t) || size > this->entryLimit) return false;
    std::lock_guard<std::mutex> lock(this->mutex);
    if (this->entries.find(key) != this->entries.end() ||
        this->entries.size() >= this->countLimit || size > this->budget - this->bytes) return false;
    try {
      const auto * begin = static_cast<const uint8_t *>(data);
      this->entries.emplace(key, std::vector<uint8_t>(begin, begin + size));
      this->bytes += size;
      return true;
    } catch (const std::bad_alloc &) {
      return false;
    }
  }

private:
  const size_t budget, entryLimit, countLimit;
  mutable std::mutex mutex;
  std::map<uint64_t, std::vector<uint8_t>> entries;
  size_t bytes = 0;
};
#endif
