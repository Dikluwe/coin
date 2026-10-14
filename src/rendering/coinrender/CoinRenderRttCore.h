#ifndef COIN_RENDER_RTT_CORE_H
#define COIN_RENDER_RTT_CORE_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include <Inventor/rendering/CoinRenderOptions.h>

// Capture identities have no relation to GPU tokens or addresses.
struct CoinRenderRttProducer {
  CoinRenderFramePlan plan;
  SbVec2i32 size;
  uint64_t sourceRevision = 0; // Coin node revision, copied by Wiring.
  CoinRenderTextureFormat format = CoinRenderTextureFormat::RGBA8_LINEAR;
};

struct CoinRenderResourceStamp {
  uint64_t owner = 0, targetGeneration = 0, device = 0, deviceGeneration = 0;
  COIN_RENDER_DLL_API bool operator==(const CoinRenderResourceStamp& rhs) const;
};

class CoinRenderRttPlan {
public:
  static constexpr int32_t maxExtent = 4096;
  explicit CoinRenderRttPlan(CoinRenderSceneTextureMode selected = COIN_RENDER_SCENE_TEXTURE_STAGED)
      : mode(selected) {}
  CoinRenderSceneTextureMode mode;
  std::vector<CoinRenderRttProducer> producers;
  size_t chargedBytes = 0;
  std::vector<uint64_t> activeSources;
  COIN_RENDER_DLL_API static size_t budget();
  COIN_RENDER_DLL_API int32_t maxForMode() const;

  COIN_RENDER_DLL_API bool enter(uint64_t source, const SbVec2i32& size, std::string& diagnostic);
  void leave() { activeSources.pop_back(); }

  COIN_RENDER_DLL_API bool append(CoinRenderRttProducer producer, uint64_t& id, std::string& diagnostic);

  COIN_RENDER_DLL_API bool validate(const CoinRenderFramePlan& root, std::string& diagnostic) const;

  COIN_RENDER_DLL_API bool requestsMips(uint64_t id,const CoinRenderFramePlan& root) const;
private:
  COIN_RENDER_DLL_API bool charge(const SbVec2i32& size, std::string& diagnostic);
  COIN_RENDER_DLL_API bool dependencies(const CoinRenderFramePlan& frame, size_t completed,
                    std::string& diagnostic) const;
};

// Core resolves descriptions only. Infra retains concrete resources separately.
class CoinRenderRttResources {
public:
  struct Entry {
    uint64_t id = 0, producer = 0;
    CoinRenderResourceStamp stamp;
    CoinRenderTextureImageSnapshot texture;
  };
  std::vector<Entry> entries;
  COIN_RENDER_DLL_API bool bind(uint64_t id, uint64_t producer, const CoinRenderResourceStamp& stamp,
            CoinRenderTextureImageSnapshot texture, std::string& diagnostic);
  COIN_RENDER_DLL_API bool resolve(const CoinRenderFramePlan& captured, const CoinRenderResourceStamp& current,
               CoinRenderFramePlan& output, std::string& diagnostic) const;
  void invalidate() { entries.clear(); }
};
#endif
