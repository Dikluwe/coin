#ifndef COIN_SBMAPSTATS_H
#define COIN_SBMAPSTATS_H

// Lab-only instrumentation. NOT part of any Coin API, NOT for PRs.
//
// Purpose: measure real cardinality and operation frequency of the six
// small internal maps that are migration candidates for SbSmallMap
// (SoVBO::vbohash, SoGLSLShaderProgram::programHandles,
// SoShaderObjectP::glshaderobjects, SoUniformShaderParameterP::glparams,
// soshape_bumprender's diffuseprogramdict/specularprogramdict), by running
// an instrumented Coin build under a real FreeCAD session and reading the
// dump this produces on process exit.
//
// Entirely compiled out unless COIN_MAP_STATS_ENABLED is defined, so a
// normal build is completely unaffected. Enable it for a measurement
// build with -DCOIN_MAP_STATS_ENABLED, then point COIN_MAP_STATS_FILE at
// an output path before running FreeCAD; without that env var the dump
// goes to stderr.

#ifdef COIN_MAP_STATS_ENABLED

#include <cstddef>
#include <cstdint>

class SbMapStats {
public:
  // Call once per logically distinct map, typically into a local static,
  // e.g.: static const int slot = SbMapStats::registerSlot("SoVBO::vbohash");
  static int registerSlot(const char * name);

  // sizeafter is the map's element count right after the operation
  // completed (e.g. getNumElements() for SbHash, size() for std::map).
  static void recordGet(int slot, std::size_t sizeafter);
  static void recordPut(int slot, std::size_t sizeafter);
  static void recordErase(int slot, std::size_t sizeafter);
  static void recordClear(int slot, std::size_t sizebefore);
  static void recordIterateStart(int slot, std::size_t size);

  // contextid identifies which OpenGL cache context a key belongs to;
  // used to count how many distinct contexts a slot actually sees.
  static void recordContext(int slot, uint32_t contextid);

  // tracks how many owning objects (e.g. SoVBO instances) exist, to
  // relate per-map cardinality to how many maps are ever alive at once.
  static void recordOwnerCreated(int slot);
  static void recordOwnerDestroyed(int slot);

  // writes the current snapshot to the configured destination; also
  // called automatically at process exit.
  static void dump(void);
};

#define SBMAPSTATS_SLOT(name) (SbMapStats::registerSlot(name))
#define SBMAPSTATS_GET(slot, sizeafter) (SbMapStats::recordGet((slot), (sizeafter)))
#define SBMAPSTATS_PUT(slot, sizeafter) (SbMapStats::recordPut((slot), (sizeafter)))
#define SBMAPSTATS_ERASE(slot, sizeafter) (SbMapStats::recordErase((slot), (sizeafter)))
#define SBMAPSTATS_CLEAR(slot, sizebefore) (SbMapStats::recordClear((slot), (sizebefore)))
#define SBMAPSTATS_ITERATE(slot, size) (SbMapStats::recordIterateStart((slot), (size)))
#define SBMAPSTATS_CONTEXT(slot, ctx) (SbMapStats::recordContext((slot), (uint32_t)(ctx)))
#define SBMAPSTATS_OWNER_CREATED(slot) (SbMapStats::recordOwnerCreated((slot)))
#define SBMAPSTATS_OWNER_DESTROYED(slot) (SbMapStats::recordOwnerDestroyed((slot)))

#else /* !COIN_MAP_STATS_ENABLED */

#define SBMAPSTATS_SLOT(name) (0)
#define SBMAPSTATS_GET(slot, sizeafter) ((void) 0)
#define SBMAPSTATS_PUT(slot, sizeafter) ((void) 0)
#define SBMAPSTATS_ERASE(slot, sizeafter) ((void) 0)
#define SBMAPSTATS_CLEAR(slot, sizebefore) ((void) 0)
#define SBMAPSTATS_ITERATE(slot, size) ((void) 0)
#define SBMAPSTATS_CONTEXT(slot, ctx) ((void) 0)
#define SBMAPSTATS_OWNER_CREATED(slot) ((void) 0)
#define SBMAPSTATS_OWNER_DESTROYED(slot) ((void) 0)

#endif /* COIN_MAP_STATS_ENABLED */

#endif /* !COIN_SBMAPSTATS_H */
