#ifndef COIN_RENDER_TEXT2_CAPTURE_H
#define COIN_RENDER_TEXT2_CAPTURE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderScreenRasterCore.h"

#include <cstdint>
#include <string>
#include <vector>

class SoState;
class SoText2;

struct CoinRenderText2RasterPass {
  CoinRenderTextureImageSnapshot image;
  // One byte per texel, bottom-up, 0/1. A zero mask must produce no fragment
  // (including no depth write). Mono coverage is independent of opacity;
  // gray coverage is the original GL alpha test, alpha/255 > 0.3.
  std::vector<uint8_t> coverageMask;
  // Mono keeps the raw raster position for the GL raster-validity test; its
  // coverage geometry must then use floor(pixel), with bitmap origin zero.
  // Gray is already
  // rounded exactly as SoText2's final DrawPixels operation.
  float pixelX = 0.0f;
  float pixelY = 0.0f;
  float depthCoin = 0.0f;
  float alphaCutoff = -1.0f;
  bool mono = false;
  bool forceBlend = false;
  bool materialColorBaked = false;
};

struct CoinRenderText2Raster {
  CoinRenderScreenRasterAnchor anchor;
  // Exact GL glyph-cache layout before screen justification/raster rounding.
  int bboxMin[2] = {0, 0};
  int bboxMax[2] = {0, 0};
  int maxWidth = 0;
  std::vector<int> lineWidths;
  std::vector<CoinRenderText2RasterPass> passes;
  bool empty = true;
};

class CoinRenderText2Capture {
public:
  enum class Status { SUCCESS, INVALID_INPUT, UNSUPPORTED, RESOURCE_ERROR };
  enum Limits { MAX_GLYPHS = 4096, MAX_LINES = 256, MAX_STRING_BYTES = 1048576,
                MAX_FONT_SIZE = 1024, MAX_OWNED_BYTES = 16777216 };

  // Wiring only: reads Coin fields/elements and the private Coin glyph service.
  // No GLRender, context, submission, resource cache, or backend identity.
  // Output changes only on success. Status/diagnostic always describe this call.
  COIN_RENDER_DLL_API static bool capture(const SoText2 & node, SoState * state,
                      CoinRenderText2Raster & output,
                      Status & status, std::string & diagnostic);
};

#endif
