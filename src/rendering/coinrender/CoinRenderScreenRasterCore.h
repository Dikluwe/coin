#ifndef COIN_RENDER_SCREEN_RASTER_CORE_H
#define COIN_RENDER_SCREEN_RASTER_CORE_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"
#include <Inventor/SbViewVolume.h>

#include <cstddef>
#include <cstdint>
#include <utility>

// Raster positions use the active viewport's bottom-left pixel coordinates,
// excluding its origin. Depth keeps Coin's [-1,1] projection convention.
// This Core only consumes values. Wiring owns node/state traversal, and the
// Builder supplies an identity model/view/projection state for these quads.
struct CoinRenderScreenRasterAnchor {
  float pixelX = 0.0f;
  float pixelY = 0.0f;
  float depthCoin = 0.0f;
};

struct CoinRenderScreenRasterQuad {
  // Bottom-left, bottom-right, top-right, top-left. Indices are 0,1,2,0,2,3.
  CoinRenderVertexSnapshot vertices[4];
  bool visible = false;
};

struct CoinRenderScreenImageLayout {
  CoinRenderScreenRasterQuad quad;
  uint32_t sourceX = 0;
  uint32_t sourceY = 0;
  uint32_t sourceWidth = 0;
  uint32_t sourceHeight = 0;
  float rasterX = 0.0f;
  float rasterY = 0.0f;
  float zoomX = 1.0f;
  float zoomY = 1.0f;
};

class CoinRenderScreenRasterCore {
public:
  // Numeric values match SoImage, without depending on the node class.
  enum HorizontalAlignment { LEFT = 0, CENTER = 1, RIGHT = 2 };
  enum VerticalAlignment { BOTTOM = 0, HALF = 1, TOP = 2 };

  // GLRender temporarily uses identity model-view and glOrtho(0,w,0,h,-1,1).
  // RasterPos is clipped as one point, including user planes stored in eye
  // space when glClipPlane was called. The Text2 workaround first clamps
  // negative x/y to zero, then moves the valid raster position with a zero-size
  // Bitmap. That movement neither clips again nor recomputes associated data.
  // SoImage calls this with its already-cropped, nonnegative raster position.
  // Return false only for malformed input; a clipped point succeeds with
  // visible=false. Errors preserve the caller's visibility result.
  COIN_RENDER_DLL_API static bool rasterVisible(const CoinRenderRenderStateSnapshot & sourceState,
                            const CoinRenderViewportSnapshot & viewport,
                            float x, float y, float depthCoin,
                            bool & visible, std::string & diagnostic,
                            bool clampNegative = true);

  // Fog for Bitmap/DrawPixels uses the associated raster distance, constant
  // across the entire pixel rectangle and unchanged by Bitmap's x/y move.
  // OpenGL 2.1 sections 2.13/3.10 define an eye-space norm but also permit its
  // approximation by abs(z_eye). With the temporary identity model-view this
  // approximation is abs(depthCoin), NOT the original camera-space depth.
  // The caller must qualify that approximation against its CoinGL reference;
  // this Core does not infer the driver's fog-distance policy.
  COIN_RENDER_DLL_API static float planarRasterFogDepth(float depthCoin);

  // Matches SoImage::getNilpoint: model transforms the origin first, then
  // SbViewVolume projects using its double-precision implementation. A float
  // MVP multiplication instead can change integer alignment at pixel edges.
  COIN_RENDER_DLL_API static bool imageAnchor(const SbMatrix & model, const SbViewVolume & volume,
                          const CoinRenderViewportSnapshot & viewport,
                          CoinRenderScreenRasterAnchor & output,
                          std::string & diagnostic);

  // Matches SoText2's GLRender projection through the captured float MVP.
  // The caller retains the mono/gray raster rounding policy of its producer.
  COIN_RENDER_DLL_API static bool matrixAnchor(const SbMatrix & modelViewProjection,
                           const CoinRenderViewportSnapshot & viewport,
                           CoinRenderScreenRasterAnchor & output,
                           std::string & diagnostic);

  // Qualified marker Bitmap origin profile: the NVIDIA CoinGL runtime keeps
  // its raster window position on an absolute 1/2048-pixel grid (11 fractional
  // bits), then Bitmap floors that position. This reproduces measured origins
  // near integer boundaries and at fractional positions; it is not a claim
  // about every GL driver's raster precision or their stored fractional ties.
  // Other CoinGL runtimes require independent qualification of this profile.
  // The caller first tests the ORIGINAL, unclamped marker raster position for
  // visibility. Quantization must never admit a negative/outside anchor.
  COIN_RENDER_DLL_API static bool markerBitmapOrigin(const CoinRenderViewportSnapshot & viewport,
                                 float rasterX, float rasterY,
                                 float & originX, float & originY,
                                 std::string & diagnostic);

  // A clipped/empty rectangle is a successful no-op. Output changes only on
  // success; invalid inputs keep it intact. Pixel dimensions are independent
  // of model rotation/scaling and camera distance, like GL raster operations.
  COIN_RENDER_DLL_API static bool pixelQuad(const CoinRenderViewportSnapshot & viewport,
                        float x, float y, float width, float height,
                        float depthCoin, const float uvBounds[4],
                        uint32_t materialSlot, CoinRenderScreenRasterQuad & output,
                        std::string & diagnostic);

  // Mirrors SoImage::GLRender rather than generatePrimitives/getImage:
  // integer truncation/alignment, viewport crop, then float glPixelZoom with
  // truncated source skip/count. Original image rows remain bottom-origin.
  // Requested values <=0 keep that source dimension, exactly like getSize.
  COIN_RENDER_DLL_API static bool imageLayout(const CoinRenderScreenRasterAnchor & anchor,
                          const CoinRenderViewportSnapshot & viewport,
                          int32_t originalWidth, int32_t originalHeight,
                          int32_t requestedWidth, int32_t requestedHeight,
                          int horizontalAlignment, int verticalAlignment,
                          uint32_t materialSlot, CoinRenderScreenImageLayout & output,
                          std::string & diagnostic);

  // No filtered resize and no row flip: image fields already use Coin's
  // bottom-origin convention. All four component layouts retain source alpha.
  COIN_RENDER_DLL_API static bool imagePayload(const uint8_t * bytes, size_t byteCount,
                           uint32_t width, uint32_t height, int components,
                           CoinRenderTextureImageSnapshot & output,
                           bool & transparent, std::string & diagnostic);

private:
  COIN_RENDER_DLL_API static bool fail(const char * message, std::string & diagnostic);
  COIN_RENDER_DLL_API static bool validViewport(const CoinRenderViewportSnapshot & viewport,
                            std::string & diagnostic);
  COIN_RENDER_DLL_API static bool finitePoint(const SbVec3f & point);
  COIN_RENDER_DLL_API static bool finiteAnchor(const CoinRenderScreenRasterAnchor & anchor);
  COIN_RENDER_DLL_API static bool truncatePixel(float value, int32_t & result);
};

#endif // COIN_RENDER_SCREEN_RASTER_CORE_H
