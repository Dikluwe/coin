#include "rendering/coinbgfx/CoinBgfxProgramCache.h"

#include <cstring>
#include <new>

CoinBgfxProgramCache::CoinBgfxProgramCache(size_t budget, size_t entryLimit, size_t countLimit)
    : budget(budget), entryLimit(entryLimit), countLimit(countLimit)
{
}

uint32_t
CoinBgfxProgramCache::size(uint64_t key) const
{
  std::lock_guard<std::mutex> lock(this->mutex);
  const auto entry = this->entries.find(key);
  return entry == this->entries.end() ? 0 : static_cast<uint32_t>(entry->second.size());
}

bool
CoinBgfxProgramCache::read(uint64_t key, void *data, uint32_t size) const
{
  std::lock_guard<std::mutex> lock(this->mutex);
  const auto entry = this->entries.find(key);
  if (!data || entry == this->entries.end() || entry->second.size() != size)
    return false;
  std::memcpy(data, entry->second.data(), size);
  return true;
}

bool
CoinBgfxProgramCache::write(uint64_t key, const void *data, uint32_t size)
{
  if (!data || size <= sizeof(uint32_t) || size > this->entryLimit)
    return false;
  std::lock_guard<std::mutex> lock(this->mutex);
  if (this->entries.find(key) != this->entries.end() || this->entries.size() >= this->countLimit ||
      size > this->budget - this->bytes)
    return false;
  try {
    const auto *begin = static_cast<const uint8_t *>(data);
    this->entries.emplace(key, std::vector<uint8_t>(begin, begin + size));
    this->bytes += size;
    return true;
  } catch (const std::bad_alloc &) {
    return false;
  }
}
