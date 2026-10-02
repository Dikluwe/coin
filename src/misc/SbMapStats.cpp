// Lab-only instrumentation. See SbMapStats.h.
#include "misc/SbMapStats.h"
#ifdef COIN_MAP_STATS_ENABLED
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {
const int MAX_SLOTS = 32;
const int MAX_CONTEXTS = 64;
struct Slot {
  const char * name = NULL;
  uint64_t gets = 0, puts = 0, erases = 0, clears = 0, iterations = 0;
  uint64_t ownerscreated = 0, ownersdestroyed = 0;
  uint64_t hist[6] = {};
  std::size_t maxsize = 0;
  uint32_t contexts[MAX_CONTEXTS] = {};
  int contextcount = 0;
  bool contextsoverflow = false;
  void bump(std::size_t size) {
    ++hist[size < 5 ? size : 5];
    if (size > maxsize) maxsize = size;
  }
  void context(uint32_t id) {
    for (int i = 0; i < contextcount; ++i) if (contexts[i] == id) return;
    if (contextcount == MAX_CONTEXTS) contextsoverflow = true;
    else contexts[contextcount++] = id;
  }
};
class Registry {
public:
  static Registry & instance() { static Registry singleton; return singleton; }
  int add(const char * name) {
    std::lock_guard<std::mutex> lock(mutex);
    for (int i = 0; i < count; ++i)
      if (std::strcmp(slots[i].name, name) == 0) return i;
    if (count == MAX_SLOTS) return -1;
    slots[count].name = name;
    return count++;
  }
  void record(int slot, std::size_t size, int operation) {
    std::lock_guard<std::mutex> lock(mutex);
    if (slot < 0) return;
    Slot & s = slots[slot];
    switch (operation) {
    case 0: ++s.gets; break;
    case 1: ++s.puts; break;
    case 2: ++s.erases; break;
    case 3: ++s.clears; break;
    case 4: ++s.iterations; break;
    }
    s.bump(size);
  }
  void context(int slot, uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex);
    if (slot >= 0) slots[slot].context(id);
  }
  void owner(int slot, bool created) {
    std::lock_guard<std::mutex> lock(mutex);
    if (slot >= 0) {
      if (created) ++slots[slot].ownerscreated;
      else ++slots[slot].ownersdestroyed;
    }
  }
  void dump() {
    std::lock_guard<std::mutex> lock(mutex);
    if (dumped) return;
    dumped = true;
    FILE * fp = stderr;
    const char * path = std::getenv("COIN_MAP_STATS_FILE");
    if (path && path[0]) {
      FILE * f = std::fopen(path, "w");
      if (f) fp = f;
    }
    std::fprintf(fp, "# SbMapStats lab dump (not for PRs)\n");
    std::fprintf(fp, "# name gets puts erases clears iterations maxsize distinct_contexts owners_created owners_alive hist[0,1,2,3,4,5+]\n");
    for (int i = 0; i < count; ++i) {
      const Slot & s = slots[i];
      std::fprintf(fp, "%s %llu %llu %llu %llu %llu %zu %d%s %llu %llu %llu,%llu,%llu,%llu,%llu,%llu\n",
        s.name, (unsigned long long)s.gets, (unsigned long long)s.puts,
        (unsigned long long)s.erases, (unsigned long long)s.clears,
        (unsigned long long)s.iterations, s.maxsize, s.contextcount,
        s.contextsoverflow ? "+" : "", (unsigned long long)s.ownerscreated,
        (unsigned long long)(s.ownerscreated - s.ownersdestroyed),
        (unsigned long long)s.hist[0], (unsigned long long)s.hist[1],
        (unsigned long long)s.hist[2], (unsigned long long)s.hist[3],
        (unsigned long long)s.hist[4], (unsigned long long)s.hist[5]);
    }
    if (fp != stderr) std::fclose(fp);
  }
  ~Registry() { dump(); }
private:
  std::mutex mutex;
  Slot slots[MAX_SLOTS];
  int count = 0;
  bool dumped = false;
};
}
int SbMapStats::registerSlot(const char * name) { return Registry::instance().add(name); }
void SbMapStats::recordGet(int slot, std::size_t size) { Registry::instance().record(slot, size, 0); }
void SbMapStats::recordPut(int slot, std::size_t size) { Registry::instance().record(slot, size, 1); }
void SbMapStats::recordErase(int slot, std::size_t size) { Registry::instance().record(slot, size, 2); }
void SbMapStats::recordClear(int slot, std::size_t size) { Registry::instance().record(slot, size, 3); }
void SbMapStats::recordIterateStart(int slot, std::size_t size) { Registry::instance().record(slot, size, 4); }
void SbMapStats::recordContext(int slot, uint32_t id) { Registry::instance().context(slot, id); }
void SbMapStats::recordOwnerCreated(int slot) { Registry::instance().owner(slot, true); }
void SbMapStats::recordOwnerDestroyed(int slot) { Registry::instance().owner(slot, false); }
void SbMapStats::dump() { Registry::instance().dump(); }
#endif
