#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderShaderModuleCore.h"
#include <cmath>
#include <utility>
#include <algorithm>
#include <limits>
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"

bool
CoinRenderResourceStamp::operator==(const CoinRenderResourceStamp &rhs) const
{
  return owner == rhs.owner && targetGeneration == rhs.targetGeneration && device == rhs.device &&
         deviceGeneration == rhs.deviceGeneration;
}

size_t
CoinRenderRttPlan::budget()
{
  return size_t(64) * 1024 * 1024;
}

int32_t
CoinRenderRttPlan::maxForMode() const
{
  return mode == COIN_RENDER_SCENE_TEXTURE_DIRECT ? maxExtent : 2048;
}

bool
CoinRenderRttPlan::enter(uint64_t source, const SbVec2i32 &size, std::string &diagnostic, bool cube)
{
  if (cube) {
    CoinRenderTextureImageSnapshot image;
    if (size[0]!=size[1] || !CoinRenderCubeTextureCore::describe(uint32_t(size[0]),false,image)) {diagnostic="Invalid cube RTT extent";return false;}
  }
  if (!source || size[0] <= 0 || size[1] <= 0 || size[0] > maxForMode() || size[1] > maxForMode()) {
    diagnostic = mode == COIN_RENDER_SCENE_TEXTURE_DIRECT
                     ? "SoSceneTexture2 direct requires dimensions in 1..4096"
                     : "SoSceneTexture2 staged requires dimensions in 1..2048";
    return false;
  }
  if (activeSources.size() >= 8 ||
      std::find(activeSources.begin(), activeSources.end(), source) != activeSources.end()) {
    diagnostic = "SoSceneTexture2 dependency cycle or nesting beyond eight passes";
    return false;
  }
  // Staged retains the historical conservative charge per occurrence. Direct
  // charges distinct captured producers after their payload is known.
  if (!cube && mode == COIN_RENDER_SCENE_TEXTURE_STAGED && !charge(size, diagnostic))
    return false;
  activeSources.push_back(source);
  return true;
}

bool
CoinRenderRttPlan::append(CoinRenderRttProducer producer, uint64_t &id, std::string &diagnostic)
{
  id = 0;
  if (!producer.sourceRevision || producer.size[0] <= 0 || producer.size[1] <= 0 ||
      producer.size[0] > maxForMode() || producer.size[1] > maxForMode()) {
    diagnostic = "Invalid scene texture producer descriptor";
    return false;
  }
  if (producer.cube) {
    CoinRenderTextureImageSnapshot image;
    if (producer.capturedCubeFaces!=6 || producer.size[0]!=producer.size[1] ||
        producer.format!=CoinRenderTextureFormat::RGBA8_LINEAR ||
        !CoinRenderCubeTextureCore::describe(uint32_t(producer.size[0]),false,image)) {
      diagnostic="Cube RTT needs six captured square POT RGBA8 faces";return false;
    }
  } else if(producer.capturedCubeFaces) {diagnostic="2D RTT contains cube faces";return false;}
  for(size_t f=0;f<producer.faceCount();++f)
    if(!producer.face(f).isValid(&diagnostic)||!dependencies(producer.face(f),producers.size(),diagnostic))return false;
  for (size_t i = 0; i < producers.size(); ++i) {
    const auto &previous = producers[i];
    bool identical=previous.cube==producer.cube && previous.capturedCubeFaces==producer.capturedCubeFaces &&
      previous.format==producer.format && previous.sourceRevision==producer.sourceRevision && previous.size==producer.size;
    for(size_t f=0;identical && f<producer.faceCount();++f)identical=previous.face(f).hasSamePayload(producer.face(f));
    if (identical) {
      id = i + 1;
      return true;
    }
  }
  if (!producer.cube && mode == COIN_RENDER_SCENE_TEXTURE_DIRECT) {
    const size_t previousCharge = chargedBytes;
    if (!charge(producer.size, diagnostic))
      return false;
    const size_t extra = producer.format == CoinRenderTextureFormat::RGBA16_FLOAT
                             ? size_t(producer.size[0]) * producer.size[1] * 4
                             : 0;
    if (extra > budget() - chargedBytes) {
      chargedBytes = previousCharge;
      diagnostic = "HDR RTT exceeds the 64 MiB graph budget";
      return false;
    }
    chargedBytes += extra;
  }
  producers.push_back(std::move(producer));
  id = producers.size();
  return true;
}

bool
CoinRenderRttPlan::shaderResourceBytes(const CoinRenderFramePlan &root, uint64_t &bytes,
                                       std::string &diagnostic) const
{
  uint64_t total = 0;
  const auto charge = [&](const CoinRenderFramePlan &frame) {
    uint64_t count = 0;
    if (!coin_render_shader_resource_bytes(frame, count, diagnostic)) return false;
    if (count > COIN_RENDER_SHADER_RESOURCE_BYTES - total) {
      diagnostic = "Portable shader root and producer graph exceeds 32 MiB resource budget";
      return false;
    }
    total += count;
    return true;
  };
  if (!charge(root)) return false;
  for (const auto &producer : producers)
    for (size_t face = 0; face < producer.faceCount(); ++face)
      if (!charge(producer.face(face))) return false;
  bytes = total;
  diagnostic.clear();
  return true;
}

bool
CoinRenderRttPlan::validate(const CoinRenderFramePlan &root, std::string &diagnostic) const
{
  uint64_t shaderBytes = 0;
  if (!shaderResourceBytes(root, shaderBytes, diagnostic)) return false;
  if (mode != COIN_RENDER_SCENE_TEXTURE_STAGED && mode != COIN_RENDER_SCENE_TEXTURE_DIRECT) {
    diagnostic = "Invalid scene texture graph mode";
    return false;
  }
  size_t distinctBytes = 0;
  for (size_t i = 0; i < producers.size(); ++i) {
    const auto &producer = producers[i];
    if (!producer.sourceRevision || producer.size[0] <= 0 || producer.size[1] <= 0 ||
        producer.size[0] > maxForMode() || producer.size[1] > maxForMode()) {
      diagnostic = "Invalid scene texture producer descriptor";
      return false;
    }
    if (producer.format != CoinRenderTextureFormat::RGBA8_LINEAR &&
        producer.format != CoinRenderTextureFormat::RGBA16_FLOAT) {
      diagnostic = "RTT output supports RGBA8 linear or RGBA16F";
      return false;
    }
    if (producer.format == CoinRenderTextureFormat::RGBA16_FLOAT)
      for (int channel = 0; channel < 4; ++channel)
        if (std::abs(producer.plan.clearColor[channel]) > 65504.f) {
          diagnostic = "HDR RTT clear exceeds the finite binary16 range";
          return false;
        }
    if (producer.format == CoinRenderTextureFormat::RGBA16_FLOAT &&
        mode != COIN_RENDER_SCENE_TEXTURE_DIRECT) {
      diagnostic = "RGBA16F RTT requires an explicit direct GPU route";
      return false;
    }
    if (producer.cube) {
      CoinRenderTextureImageSnapshot image;
      if (producer.capturedCubeFaces!=6 || producer.size[0]!=producer.size[1] ||
          producer.format!=CoinRenderTextureFormat::RGBA8_LINEAR ||
          !CoinRenderCubeTextureCore::describe(uint32_t(producer.size[0]),requestsMips(i+1,root),image)) {
        diagnostic="Invalid or incomplete cube RTT producer";return false;
      }
    } else if(producer.capturedCubeFaces) {diagnostic="2D producer contains cube faces";return false;}
    const size_t bytes = producer.cube ? 0 : size_t(producer.size[0]) * producer.size[1] *
                         (mode == COIN_RENDER_SCENE_TEXTURE_DIRECT
                              ? (producer.format == CoinRenderTextureFormat::RGBA16_FLOAT ? 12 : 8)
                              : 4);
    if (bytes > budget() - distinctBytes) {
      diagnostic = "SoSceneTexture2 graph exceeds 64 MiB per apply";
      return false;
    }
    distinctBytes += bytes;
    for(size_t f=0;f<producer.faceCount();++f)
      if(!producer.face(f).isValid(&diagnostic)||!dependencies(producer.face(f),i,diagnostic))return false;
  }
  if (!root.isValid(&diagnostic) || !dependencies(root, producers.size(), diagnostic))
    return false;
  // Count requested lower levels before any producer is submitted.
  distinctBytes = std::max(distinctBytes, chargedBytes);
  const auto chargeMips = [&](const CoinRenderFramePlan &frame) {
    for (const auto &image : frame.textures) {
      if (!image.producerId || !image.mipmapped)
        continue;
      // Direct execution reserves one GPU-only scratch source per reduction
      // level, including the base for GL view isolation. Native-view APIs
      // may use less; the common graph never depends on that optimization.
      const size_t lower =
          CoinRenderTextureSamplingCore::mipBytes(image.width, image.height, image.format);
      const size_t bytes = mode == COIN_RENDER_SCENE_TEXTURE_DIRECT
                               ? lower * 2 + CoinRenderTextureFormatCore::levelBytes(
                                                 image.width, image.height, image.format)
                               : lower;
      if (bytes > budget() - distinctBytes) {
        diagnostic = "SoSceneTexture2 base images and mip chains exceed 64 MiB per apply";
        return false;
      }
      distinctBytes += bytes;
    }
    return true;
  };
  for (const auto &producer : producers)
    for(size_t f=0;f<producer.faceCount();++f)if(!chargeMips(producer.face(f)))return false;
  uint64_t cubeBytes=0;
  return chargeMips(root) && cubeNominalBytes(root,1,cubeBytes,diagnostic);
}

bool
CoinRenderRttPlan::requestsMips(uint64_t id, const CoinRenderFramePlan &root) const
{
  auto requests = [&](const CoinRenderFramePlan &p) {
    for (const auto &t : p.textures)
      if (t.producerId == id && (t.mipmapped || t.cubeMipmapped))
        return true;
    return false;
  };
  if (requests(root))
    return true;
  if(id && id<=producers.size() && producers[size_t(id-1)].plan.outputMipmaps)return true;
  for (const auto &producer : producers)
    for(size_t f=0;f<producer.faceCount();++f)if(requests(producer.face(f)))return true;
  return false;
}

size_t CoinRenderRttPlan::tokenCount() const {
  size_t retained=0,peak=0;
  for(const auto & producer:producers) {
    // Earlier producer outputs remain live. A cube temporarily adds six faces
    // and its independent atlas, then retires the six faces through the queue.
    const size_t candidates=producer.cube?7:1;
    if(retained>std::numeric_limits<size_t>::max()-candidates)
      return std::numeric_limits<size_t>::max();
    peak=std::max(peak,retained+candidates);
    ++retained;
  }
  return peak;
}
CoinRenderRttPlan CoinRenderRttPlan::inspection(const CoinRenderFramePlan & root) const {
  CoinRenderRttPlan result(mode);
  for(size_t i=0;i<producers.size();++i) {
    const auto & source=producers[i];
    for(size_t face=0;face<source.faceCount();++face) {
      CoinRenderRttProducer p;p.plan=source.face(face);p.size=source.size;
      p.sourceRevision=source.sourceRevision;p.format=source.format;
      p.plan.outputMipmaps=requestsMips(i+1,root);result.producers.push_back(std::move(p));
    }
  }
  return result;
}
bool CoinRenderRttPlan::cubeMemory(const CoinRenderFramePlan & root,uint32_t samples,
                                  CoinRenderCubeRttMemory & output,std::string & diagnostic) const {
  if(samples!=1 && samples!=2 && samples!=4){diagnostic="Invalid cube RTT sample count";return false;}
  CoinRenderCubeRttMemory result;uint64_t retainedCpuAtlases=0;
  const auto add=[&](uint64_t & total,uint64_t value) {
    if(value>std::numeric_limits<uint64_t>::max()-total){diagnostic="Cube RTT memory count overflow";return false;}
    total+=value;return true;
  };
  const auto resolvedCopies=[&](const CoinRenderFramePlan & frame) {
    uint64_t total=0;
    for(const auto & image:frame.textures)if(image.producerId && image.producerId<=producers.size() && producers[size_t(image.producerId-1)].cube) {
      CoinRenderTextureImageSnapshot actual;
      if(!CoinRenderCubeTextureCore::describe(uint32_t(producers[size_t(image.producerId-1)].size[0]),requestsMips(image.producerId,root),actual))return uint64_t(0);
      total+=uint64_t(actual.width)*actual.height*4;
    }
    return total;
  };
  for(size_t i=0;i<producers.size();++i) {
    const auto & p=producers[i];if(!p.cube)continue;
    CoinRenderTextureImageSnapshot atlas;const bool mips=requestsMips(i+1,root);
    if(p.size[0]!=p.size[1] || !CoinRenderCubeTextureCore::describe(uint32_t(p.size[0]),mips,atlas)) {
      diagnostic="Invalid cube RTT nominal extent";return false;
    }
    const uint64_t base=uint64_t(p.size[0])*uint64_t(p.size[1])*4;
    const uint64_t atlasBytes=uint64_t(atlas.width)*atlas.height*4;
    const uint64_t chain=base+(mips?CoinRenderTextureSamplingCore::mipBytes(p.size[0],p.size[1],p.format):0);
    if(mode==COIN_RENDER_SCENE_TEXTURE_STAGED) {
      // Only six CPU bases coexist with Core::prepare's destination atlas.
      // Face-local mips are written directly into that atlas, not six chains.
      result.coreCpuPeakBytes=std::max(result.coreCpuPeakBytes,retainedCpuAtlases+6*base+atlasBytes);
      for(size_t face=0;face<6;++face)
        result.coreCpuPeakBytes=std::max(result.coreCpuPeakBytes,
          retainedCpuAtlases+7*base+resolvedCopies(p.face(face)));
      if(!add(retainedCpuAtlases,atlasBytes))return false;
      // One child exists at a time. A multisampled child retains one resolved
      // color plus its sample color/depth; 1x retains color and depth only.
      const uint64_t attachments=samples==1?2*base:base+2*base*samples;
      result.stagedColorDepthPeakBytes=std::max(result.stagedColorDepthPeakBytes,attachments);
      const uint64_t pitch=((uint64_t(p.size[0])*4+255)/256)*256;
      result.stagedReadbackPeakBytes=std::max(result.stagedReadbackPeakBytes,pitch*uint64_t(p.size[1]));
    } else {
      if(!add(result.directColorAtlasBytes,6*chain+atlasBytes) ||
         !add(result.directDepthPendingBytes,6*base*samples) ||
         !add(result.directMsaaPendingBytes,samples>1?6*base*samples:0) ||
         !add(result.directMipScratchPendingBytes,mips?6*chain:0))return false;
      // These are separate queue-pending envelopes: retiring face token
      // registrations after assembly does not imply GPU completion or free
      // their queued storage. wgpu may retain all six submissions until the
      // fence; BGFX reuses one MSAA pair and retires face framebuffers through
      // its ordered destruction queue. They are not a common exact
      // resident peak or a new admission ceiling.
    }
  }
  if(mode==COIN_RENDER_SCENE_TEXTURE_STAGED)
    result.coreCpuPeakBytes=std::max(result.coreCpuPeakBytes,retainedCpuAtlases+resolvedCopies(root));
  output=result;return true;
}
bool CoinRenderRttPlan::cubeNominalBytes(const CoinRenderFramePlan & root,uint32_t samples,
                                        uint64_t & bytes,std::string & diagnostic) const {
  CoinRenderCubeRttMemory memory;if(!cubeMemory(root,samples,memory,diagnostic))return false;
  bytes=0;
  const uint64_t values[]={memory.coreCpuPeakBytes,memory.stagedColorDepthPeakBytes,memory.stagedReadbackPeakBytes,
    memory.directColorAtlasBytes,memory.directDepthPendingBytes,memory.directMsaaPendingBytes,memory.directMipScratchPendingBytes};
  for(uint64_t value:values) {
    if(value>std::numeric_limits<uint64_t>::max()-bytes){diagnostic="Cube RTT envelope overflow";return false;}
    bytes+=value;
  }
  return true;
}

bool
CoinRenderRttPlan::charge(const SbVec2i32 &size, std::string &diagnostic)
{
  if (size[0] <= 0 || size[1] <= 0 || size[0] > maxForMode() || size[1] > maxForMode()) {
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

bool
CoinRenderRttPlan::dependencies(const CoinRenderFramePlan &frame, size_t completed,
                                std::string &diagnostic) const
{
  for (const auto &texture : frame.textures) {
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
    const auto &producer = producers[size_t(texture.producerId - 1)];
    CoinRenderTextureImageSnapshot description;
    if(producer.cube && !CoinRenderCubeTextureCore::describe(uint32_t(producer.size[0]),texture.cubeMipmapped,description))return false;
    if ((producer.cube ? (!texture.cubeFaceSize || texture.cubeFaceSize!=uint32_t(producer.size[0]) ||
          texture.width!=description.width || texture.height!=description.height || texture.cubeSourceComponents!=4) :
          (texture.cubeFaceSize || texture.width!=uint32_t(producer.size[0]) || texture.height!=uint32_t(producer.size[1]))) ||
        texture.format != producer.format) {
      diagnostic = "SoSceneTexture2 consumer dimensions differ from its producer";
      return false;
    }
  }
  return true;
}

bool
CoinRenderRttResources::bind(uint64_t id, uint64_t producer, const CoinRenderResourceStamp &stamp,
                             CoinRenderTextureImageSnapshot texture, std::string &diagnostic)
{
  if (id != entries.size() + 1 || !producer || !stamp.owner || texture.producerId ||
      !texture.width || !texture.height || texture.width > 8192 ||
      texture.height > 8192 || texture.components != 4 ||
      (texture.gpuToken && (!stamp.device || !stamp.deviceGeneration)) ||
      (texture.gpuToken && !texture.pixelsRgba.empty()) ||
      (texture.cubeFaceSize && !CoinRenderCubeTextureCore::valid(texture)) ||
      (!texture.gpuToken && texture.pixelsRgba.size() != uint64_t(texture.width) * texture.height * 4)) {
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

bool
CoinRenderRttResources::resolve(const CoinRenderFramePlan &captured,
                                const CoinRenderResourceStamp &current, CoinRenderFramePlan &output,
                                std::string &diagnostic) const
{
  CoinRenderFramePlan candidate = captured;
  for (auto &texture : candidate.textures) {
    if (!texture.producerId)
      continue;
    if (texture.producerId > entries.size()) {
      diagnostic = "Scene texture producer has no retained result";
      return false;
    }
    const Entry &entry = entries[size_t(texture.producerId - 1)];
    if (!(entry.stamp == current) || entry.id != texture.producerId ||
        texture.width != entry.texture.width || (!texture.cubeFaceSize && texture.height != entry.texture.height) ||
        texture.format != entry.texture.format || texture.cubeFaceSize!=entry.texture.cubeFaceSize ||
        texture.cubeSourceComponents!=entry.texture.cubeSourceComponents ||
        (texture.cubeMipmapped && !entry.texture.cubeMipmapped)) {
      diagnostic = "Scene texture resource belongs to another owner, device or generation";
      return false;
    }
    const bool requestedMips = texture.mipmapped;
    const bool opaque = texture.gpuOpaque;
    const int32_t transparencyFunction = texture.sceneTransparencyFunction;
    texture = entry.texture;
    texture.gpuOpaque = opaque;
    texture.sceneTransparencyFunction = transparencyFunction;
    if (requestedMips && texture.gpuToken && !texture.mipmapped) {
      diagnostic = "GPU RTT token lacks the requested mip chain";
      return false;
    }
    if (requestedMips && !texture.gpuToken && !texture.cubeFaceSize && !CoinRenderTextureSamplingCore::generate(texture)) {
      diagnostic = "Cannot resolve the staged RTT mip chain";
      return false;
    }
  }
  output = std::move(candidate);
  return true;
}
