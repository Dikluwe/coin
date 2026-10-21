#ifndef COIN_RENDER_CUBE_TEXTURE_CORE_H
#define COIN_RENDER_CUBE_TEXTURE_CORE_H

#include <Inventor/CoinRenderExport.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbVec4f.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <array>
#include <cstdint>
#include <vector>

class CoinRenderCubeTextureCore {
public:
  struct Face {
    uint32_t width = 0, height = 0, components = 0;
    std::vector<uint8_t> pixels;
  };
  struct Subresource {
    uint32_t face=0, level=0, width=0, height=0, rowPitch=0;
    uint64_t linearByteOffset=0;
    bool originBottomLeft=true;
  };
  static COIN_RENDER_DLL_API bool subresources(uint32_t faceSize, bool mipmapped,
                                               std::vector<Subresource> & output);
  struct Atlas {
    uint32_t faceSize = 0, sourceComponents = 0, width = 0, height = 0;
    bool mipmapped = false;
    std::vector<uint8_t> pixelsRgba;
    CoinRenderTextureFormat format = CoinRenderTextureFormat::RGBA8_LINEAR;
  };

  static COIN_RENDER_DLL_API bool describe(uint32_t faceSize, bool mipmapped,
                                           CoinRenderTextureImageSnapshot & output);
  static COIN_RENDER_DLL_API bool validate(const CoinRenderFramePlan &, std::string & diagnostic);
  static COIN_RENDER_DLL_API bool prepare(const std::array<Face, 6> & faces,
                                          bool mipmapped, Atlas & output);
  static COIN_RENDER_DLL_API bool prepare(const std::array<Face, 6> & faces,
                                          bool mipmapped, Atlas & output,
                                          CoinRenderTextureFormat format);
  static COIN_RENDER_DLL_API bool valid(const Atlas & atlas);
  static COIN_RENDER_DLL_API bool valid(const CoinRenderTextureImageSnapshot & image);
  static COIN_RENDER_DLL_API bool sample(const CoinRenderTextureImageSnapshot & image,
                                         const SbVec3f & direction, float lod,
                                         CoinRenderTextureFilter filter, SbVec4f & rgba);
  static COIN_RENDER_DLL_API bool project(const SbVec3f & direction,
                                          uint32_t & face, SbVec2f & st);
  static COIN_RENDER_DLL_API bool sample(const Atlas & atlas, const SbVec3f & direction,
                                         float lod, CoinRenderTextureFilter filter,
                                         SbVec4f & rgba);
};
#endif
