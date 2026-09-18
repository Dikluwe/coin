// Lab-only instrumentation. See SbMapStats.h for what this is and why.
// Never merge this file into a PR branch.

#include "misc/SbMapStats.h"

#ifdef COIN_MAP_STATS_ENABLED

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

// post-op size buckets: 0, 1, 2, 3, 4, 5+
const int NUM_BUCKETS = 6;

struct SlotStats {
  std::string name;
  uint64_t gets = 0;
  uint64_t puts = 0;
  uint64_t erases = 0;
  uint64_t clears = 0;
  uint64_t iterations = 0;
  std::size_t maxsize = 0;
  uint64_t sizehist[NUM_BUCKETS] = {0, 0, 0, 0, 0, 0};
  std::unordered_set<uint32_t> contexts;
  uint64_t ownerscreated = 0;
  uint64_t ownersdestroyed = 0;

  void bump(std::size_t size) {
    const int bucket = size < (NUM_BUCKETS - 1) ? static_cast<int>(size) : (NUM_BUCKETS - 1);
    this->sizehist[bucket]++;
    if (size > this->maxsize) { this->maxsize = size; }
  }
};

class Registry {
public:
  static Registry & instance(void) {
    static Registry singleton;
    return singleton;
  }

  int registerSlot(const char * name) {
    std::lock_guard<std::mutex> lock(this->mutex);
    for (std::size_t i = 0; i < this->slots.size(); ++i) {
      if (this->slots[i].name == name) { return static_cast<int>(i); }
    }
    this->slots.emplace_back();
    this->slots.back().name = name;
    return static_cast<int>(this->slots.size() - 1);
  }

  void recordGet(int slot, std::size_t sizeafter) {
    std::lock_guard<std::mutex> lock(this->mutex);
    SlotStats & s = this->slots[slot];
    s.gets++;
    s.bump(sizeafter);
  }

  void recordPut(int slot, std::size_t sizeafter) {
    std::lock_guard<std::mutex> lock(this->mutex);
    SlotStats & s = this->slots[slot];
    s.puts++;
    s.bump(sizeafter);
  }

  void recordErase(int slot, std::size_t sizeafter) {
    std::lock_guard<std::mutex> lock(this->mutex);
    SlotStats & s = this->slots[slot];
    s.erases++;
    s.bump(sizeafter);
  }

  void recordClear(int slot, std::size_t sizebefore) {
    std::lock_guard<std::mutex> lock(this->mutex);
    SlotStats & s = this->slots[slot];
    s.clears++;
    s.bump(sizebefore);
  }

  void recordIterateStart(int slot, std::size_t size) {
    std::lock_guard<std::mutex> lock(this->mutex);
    SlotStats & s = this->slots[slot];
    s.iterations++;
    s.bump(size);
  }

  void recordContext(int slot, uint32_t contextid) {
    std::lock_guard<std::mutex> lock(this->mutex);
    this->slots[slot].contexts.insert(contextid);
  }

  void recordOwnerCreated(int slot) {
    std::lock_guard<std::mutex> lock(this->mutex);
    this->slots[slot].ownerscreated++;
  }

  void recordOwnerDestroyed(int slot) {
    std::lock_guard<std::mutex> lock(this->mutex);
    this->slots[slot].ownersdestroyed++;
  }

  void dump(void) {
    std::lock_guard<std::mutex> lock(this->mutex);
    if (this->dumped) { return; }
    this->dumped = true;

    FILE * fp = stderr;
    const char * path = getenv("COIN_MAP_STATS_FILE");
    if (path && path[0] != '\0') {
      FILE * f = fopen(path, "w");
      if (f) { fp = f; }
    }

    fprintf(fp, "# SbMapStats dump (lab instrumentation, not for PRs)\n");
    fprintf(fp, "# columns: name gets puts erases clears iterations maxsize "
                "distinct_contexts owners_created owners_alive "
                "hist[0,1,2,3,4,5+]\n");
    for (const SlotStats & s : this->slots) {
      fprintf(fp, "%s %llu %llu %llu %llu %llu %zu %zu %llu %llu %llu,%llu,%llu,%llu,%llu,%llu\n",
              s.name.c_str(),
              static_cast<unsigned long long>(s.gets),
              static_cast<unsigned long long>(s.puts),
              static_cast<unsigned long long>(s.erases),
              static_cast<unsigned long long>(s.clears),
              static_cast<unsigned long long>(s.iterations),
              s.maxsize,
              s.contexts.size(),
              static_cast<unsigned long long>(s.ownerscreated),
              static_cast<unsigned long long>(s.ownerscreated - s.ownersdestroyed),
              static_cast<unsigned long long>(s.sizehist[0]),
              static_cast<unsigned long long>(s.sizehist[1]),
              static_cast<unsigned long long>(s.sizehist[2]),
              static_cast<unsigned long long>(s.sizehist[3]),
              static_cast<unsigned long long>(s.sizehist[4]),
              static_cast<unsigned long long>(s.sizehist[5]));
    }

    if (fp != stderr) { fclose(fp); }
  }

  ~Registry(void) { this->dump(); }

private:
  std::mutex mutex;
  std::vector<SlotStats> slots;
  bool dumped = false;
};

} // namespace

int SbMapStats::registerSlot(const char * name) { return Registry::instance().registerSlot(name); }
void SbMapStats::recordGet(int slot, std::size_t sizeafter) { Registry::instance().recordGet(slot, sizeafter); }
void SbMapStats::recordPut(int slot, std::size_t sizeafter) { Registry::instance().recordPut(slot, sizeafter); }
void SbMapStats::recordErase(int slot, std::size_t sizeafter) { Registry::instance().recordErase(slot, sizeafter); }
void SbMapStats::recordClear(int slot, std::size_t sizebefore) { Registry::instance().recordClear(slot, sizebefore); }
void SbMapStats::recordIterateStart(int slot, std::size_t size) { Registry::instance().recordIterateStart(slot, size); }
void SbMapStats::recordContext(int slot, uint32_t contextid) { Registry::instance().recordContext(slot, contextid); }
void SbMapStats::recordOwnerCreated(int slot) { Registry::instance().recordOwnerCreated(slot); }
void SbMapStats::recordOwnerDestroyed(int slot) { Registry::instance().recordOwnerDestroyed(slot); }
void SbMapStats::dump(void) { Registry::instance().dump(); }

#endif /* COIN_MAP_STATS_ENABLED */
