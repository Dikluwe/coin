#ifndef COIN_RENDER_PBR_SURFACE_CORE_H
#define COIN_RENDER_PBR_SURFACE_CORE_H
#include "CoinRenderFramePlan.h"
#include <Inventor/CoinRenderExport.h>
#include <Inventor/rendering/SoRenderIR.h>
#include <map>
#include <tuple>
class CoinRenderPbrSurfaceCore {
public:
  void beginCapture() { imageSlots.clear(); }
  COIN_RENDER_DLL_API bool images(CoinRenderFramePlan &, CoinRenderRenderStateSnapshot &,
                     const SoPBRSurfaceData &, uint32_t, std::string &);
  COIN_RENDER_DLL_API static bool normals(const unsigned char *, uint32_t, uint32_t, int, bool,
                                          bool, CoinRenderTextureImageSnapshot &, std::string &);
  COIN_RENDER_DLL_API bool prepare(CoinRenderFramePlan &, uint64_t, std::string &);
  uint64_t retainedBytes() const { return cacheBytes; }
  uint64_t generatedTriangles = 0, cacheHits = 0, degenerateTriangles = 0;

private:
  struct Entry {
    std::vector<float> key, tangents;
    uint64_t degenerate = 0;
  };
  std::map<std::tuple<uintptr_t,int,int,int,int,int,int,bool>,uint32_t> imageSlots;
  std::vector<Entry> cache;
  uint64_t cacheBytes = 0;
};
#endif
