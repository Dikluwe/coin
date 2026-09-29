#ifndef COIN_RENDER_RTT_CORE_H
#define COIN_RENDER_RTT_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <Inventor/rendering/CoinRenderOptions.h>
#include <algorithm>

// Capture identities have no relation to GPU tokens or addresses.
struct CoinRenderRttProducer {
  CoinRenderFramePlan plan;
  SbVec2i32 size;
  uint64_t sourceRevision = 0; // Coin node revision, copied by Wiring.
};

struct CoinRenderResourceStamp {
  uint64_t owner = 0, targetGeneration = 0, device = 0, deviceGeneration = 0;
  bool operator==(const CoinRenderResourceStamp& rhs) const {
    return owner == rhs.owner && targetGeneration == rhs.targetGeneration && device == rhs.device &&
           deviceGeneration == rhs.deviceGeneration;
  }
};

class CoinRenderRttPlan {
public:
  explicit CoinRenderRttPlan(CoinRenderSceneTextureMode selected = COIN_RENDER_SCENE_TEXTURE_STAGED)
      : mode(selected) {}
  CoinRenderSceneTextureMode mode;
  std::vector<CoinRenderRttProducer> producers;
  size_t chargedBytes = 0;
  std::vector<uint64_t> activeSources;
  static size_t budget() { return size_t(64) * 1024 * 1024; }

  bool enter(uint64_t source, const SbVec2i32& size, std::string& diagnostic) {
    if (!source || size[0] <= 0 || size[1] <= 0 || size[0] > 2048 || size[1] > 2048) {
      diagnostic = "SoSceneTexture2 requires a scene and dimensions in 1..2048";
      return false;
    }
    if (activeSources.size() >= 8 ||
        std::find(activeSources.begin(), activeSources.end(), source) != activeSources.end()) {
      diagnostic = "SoSceneTexture2 dependency cycle or nesting beyond eight passes";
      return false;
    }
    // Staged retains the historical conservative charge per occurrence. Direct
    // charges distinct captured producers after their payload is known.
    if (mode == COIN_RENDER_SCENE_TEXTURE_STAGED && !charge(size, diagnostic))
      return false;
    activeSources.push_back(source);
    return true;
  }
  void leave() { activeSources.pop_back(); }

  bool append(CoinRenderRttProducer producer, uint64_t& id, std::string& diagnostic) {
    id = 0;
    if (!producer.sourceRevision || producer.size[0] <= 0 || producer.size[1] <= 0 ||
        producer.size[0] > 2048 || producer.size[1] > 2048) {
      diagnostic = "Invalid scene texture producer descriptor";
      return false;
    }
    if (!producer.plan.isValid(&diagnostic) ||
        !dependencies(producer.plan, producers.size(), diagnostic))
      return false;
    for (size_t i = 0; i < producers.size(); ++i) {
      const auto& previous = producers[i];
      if (previous.sourceRevision == producer.sourceRevision && previous.size == producer.size &&
          previous.plan.hasSamePayload(producer.plan)) {
        id = i + 1;
        return true;
      }
    }
    if (mode == COIN_RENDER_SCENE_TEXTURE_DIRECT && !charge(producer.size, diagnostic))
      return false;
    producers.push_back(std::move(producer));
    id = producers.size();
    return true;
  }

  bool validate(const CoinRenderFramePlan& root, std::string& diagnostic) const {
    if (mode != COIN_RENDER_SCENE_TEXTURE_STAGED && mode != COIN_RENDER_SCENE_TEXTURE_DIRECT) {
      diagnostic = "Invalid scene texture graph mode";
      return false;
    }
    size_t distinctBytes = 0;
    for (size_t i = 0; i < producers.size(); ++i) {
      const auto& producer = producers[i];
      if (!producer.sourceRevision || producer.size[0] <= 0 || producer.size[1] <= 0 ||
          producer.size[0] > 2048 || producer.size[1] > 2048) {
        diagnostic = "Invalid scene texture producer descriptor";
        return false;
      }
      const size_t bytes = size_t(producer.size[0]) * producer.size[1] *
                           (mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? 8 : 4);
      if (bytes > budget() - distinctBytes) {
        diagnostic = "SoSceneTexture2 graph exceeds 64 MiB per apply";
        return false;
      }
      distinctBytes += bytes;
      if (!producer.plan.isValid(&diagnostic) || !dependencies(producer.plan, i, diagnostic))
        return false;
    }
    return root.isValid(&diagnostic) && dependencies(root, producers.size(), diagnostic);
  }

private:
  bool charge(const SbVec2i32& size, std::string& diagnostic) {
    if (size[0] <= 0 || size[1] <= 0 || size[0] > 2048 || size[1] > 2048) {
      diagnostic = "Invalid scene texture producer dimensions";
      return false;
    }
    const size_t bytes =
        size_t(size[0]) * size[1] * (mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? 8 : 4);
    if (chargedBytes > budget() || bytes > budget() - chargedBytes) {
      diagnostic = mode == COIN_RENDER_SCENE_TEXTURE_DIRECT
                       ? "SoSceneTexture2 GPU attachment budget exceeds 64 MiB per apply"
                       : "SoSceneTexture2 staged RGBA8 budget exceeds 64 MiB per apply";
      return false;
    }
    chargedBytes += bytes;
    return true;
  }
  bool dependencies(const CoinRenderFramePlan& frame, size_t completed,
                    std::string& diagnostic) const {
    for (const auto& texture : frame.textures) {
      if (texture.gpuToken) {
        diagnostic = "Captured scene texture graph contains a concrete GPU token";
        return false;
      }
      if (!texture.producerId)
        continue;
      if (texture.producerId > completed) {
        diagnostic = "SoSceneTexture2 pass references a missing or future producer";
        return false;
      }
      const auto& producer = producers[size_t(texture.producerId - 1)];
      if (texture.width != uint32_t(producer.size[0]) ||
          texture.height != uint32_t(producer.size[1])) {
        diagnostic = "SoSceneTexture2 consumer dimensions differ from its producer";
        return false;
      }
    }
    return true;
  }
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
  bool bind(uint64_t id, uint64_t producer, const CoinRenderResourceStamp& stamp,
            CoinRenderTextureImageSnapshot texture, std::string& diagnostic) {
    if (id != entries.size() + 1 || !producer || !stamp.owner || texture.producerId ||
        !texture.width || !texture.height || texture.width > 2048 || texture.height > 2048 ||
        texture.components != 4 ||
        (texture.gpuToken && (!stamp.device || !stamp.deviceGeneration)) ||
        (texture.gpuToken && !texture.pixelsRgba.empty()) ||
        (!texture.gpuToken &&
         texture.pixelsRgba.size() != uint64_t(texture.width) * texture.height * 4)) {
      diagnostic = "Invalid or unordered scene texture resource binding";
      return false;
    }
    Entry entry;
    entry.id = id;
    entry.producer = producer;
    entry.stamp = stamp;
    entry.texture = std::move(texture);
    entries.push_back(std::move(entry));
    return true;
  }
  bool resolve(const CoinRenderFramePlan& captured, const CoinRenderResourceStamp& current,
               CoinRenderFramePlan& output, std::string& diagnostic) const {
    CoinRenderFramePlan candidate = captured;
    for (auto& texture : candidate.textures) {
      if (!texture.producerId)
        continue;
      if (texture.producerId > entries.size()) {
        diagnostic = "Scene texture producer has no retained result";
        return false;
      }
      const Entry& entry = entries[size_t(texture.producerId - 1)];
      if (!(entry.stamp == current) || entry.id != texture.producerId ||
          texture.width != entry.texture.width || texture.height != entry.texture.height) {
        diagnostic = "Scene texture resource belongs to another owner, device or generation";
        return false;
      }
      const bool opaque = texture.gpuOpaque;
      texture = entry.texture;
      texture.gpuOpaque = opaque;
    }
    output = std::move(candidate);
    return true;
  }
  void invalidate() { entries.clear(); }
};
#endif
