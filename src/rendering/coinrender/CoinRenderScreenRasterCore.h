#ifndef COIN_RENDER_SCREEN_RASTER_CORE_H
#define COIN_RENDER_SCREEN_RASTER_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"
#include <Inventor/SbViewVolume.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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
  static bool rasterVisible(const CoinRenderRenderStateSnapshot & sourceState,
                            const CoinRenderViewportSnapshot & viewport,
                            float x, float y, float depthCoin,
                            bool & visible, std::string & diagnostic,
                            bool clampNegative = true) {
    diagnostic.clear();
    if (!validViewport(viewport, diagnostic)) return false;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(depthCoin))
      return fail("Screen raster position is not finite", diagnostic);
    float equations[COIN_RENDER_MAX_CLIP_PLANES][4] = {};
    if (!coin_render_clip_equations(sourceState, equations, diagnostic)) return false;
    const float eyeX = clampNegative && x < 0.0f ? 0.0f : x;
    const float eyeY = clampNegative && y < 0.0f ? 0.0f : y;
    // The upper canonical boundaries themselves are valid raster positions,
    // even if a subsequent positive-sized pixel rectangle has no coverage.
    bool candidate = eyeX >= 0.0f && eyeX <= float(viewport.width) &&
      eyeY >= 0.0f && eyeY <= float(viewport.height) &&
      depthCoin >= -1.0f && depthCoin <= 1.0f;
    if (candidate) {
      for (size_t i = 0; i < sourceState.clipPlanesWorld.size(); ++i) {
        const double distance = double(equations[i][0]) * eyeX +
          double(equations[i][1]) * eyeY -
          double(equations[i][2]) * depthCoin + equations[i][3];
        if (!std::isfinite(distance))
          return fail("Screen raster user-plane distance is not finite", diagnostic);
        if (distance < 0.0) candidate = false;
      }
    }
    visible = candidate;
    return true;
  }

  // Fog for Bitmap/DrawPixels uses the associated raster distance, constant
  // across the entire pixel rectangle and unchanged by Bitmap's x/y move.
  // OpenGL 2.1 sections 2.13/3.10 define an eye-space norm but also permit its
  // approximation by abs(z_eye). With the temporary identity model-view this
  // approximation is abs(depthCoin), NOT the original camera-space depth.
  // The caller must qualify that approximation against its CoinGL reference;
  // this Core does not infer the driver's fog-distance policy.
  static float planarRasterFogDepth(float depthCoin) {
    return std::abs(depthCoin);
  }

  // Matches SoImage::getNilpoint: model transforms the origin first, then
  // SbViewVolume projects using its double-precision implementation. A float
  // MVP multiplication instead can change integer alignment at pixel edges.
  static bool imageAnchor(const SbMatrix & model, const SbViewVolume & volume,
                          const CoinRenderViewportSnapshot & viewport,
                          CoinRenderScreenRasterAnchor & output,
                          std::string & diagnostic) {
    diagnostic.clear();
    if (!validViewport(viewport, diagnostic)) return false;
    if (!CoinRenderTransformCore::finiteMatrix(model))
      return fail("Screen image model matrix is not finite", diagnostic);
    SbVec3f projected(0.0f, 0.0f, 0.0f);
    model.multVecMatrix(projected, projected);
    if (!finitePoint(projected))
      return fail("Screen image world anchor is not finite", diagnostic);
    volume.projectToScreen(projected, projected);
    CoinRenderScreenRasterAnchor candidate;
    candidate.pixelX = projected[0] * float(viewport.width);
    candidate.pixelY = projected[1] * float(viewport.height);
    candidate.depthCoin = projected[2] * 2.0f - 1.0f;
    if (!finiteAnchor(candidate))
      return fail("Screen image projected anchor is not finite", diagnostic);
    output = candidate;
    return true;
  }

  // Matches SoText2's GLRender projection through the captured float MVP.
  // The caller retains the mono/gray raster rounding policy of its producer.
  static bool matrixAnchor(const SbMatrix & modelViewProjection,
                           const CoinRenderViewportSnapshot & viewport,
                           CoinRenderScreenRasterAnchor & output,
                           std::string & diagnostic) {
    diagnostic.clear();
    if (!validViewport(viewport, diagnostic)) return false;
    if (!CoinRenderTransformCore::finiteMatrix(modelViewProjection))
      return fail("Screen raster projection matrix is not finite", diagnostic);
    SbVec3f projected(0.0f, 0.0f, 0.0f);
    modelViewProjection.multVecMatrix(projected, projected);
    CoinRenderScreenRasterAnchor candidate;
    candidate.pixelX = (projected[0] + 1.0f) * 0.5f * float(viewport.width);
    candidate.pixelY = (projected[1] + 1.0f) * 0.5f * float(viewport.height);
    candidate.depthCoin = projected[2];
    if (!finiteAnchor(candidate))
      return fail("Screen raster projected anchor is not finite", diagnostic);
    output = candidate;
    return true;
  }

  // Qualified marker Bitmap origin profile: the NVIDIA CoinGL runtime keeps
  // its raster window position on an absolute 1/2048-pixel grid (11 fractional
  // bits), then Bitmap floors that position. This reproduces measured origins
  // near integer boundaries and at fractional positions; it is not a claim
  // about every GL driver's raster precision or their stored fractional ties.
  // Other CoinGL runtimes require independent qualification of this profile.
  // The caller first tests the ORIGINAL, unclamped marker raster position for
  // visibility. Quantization must never admit a negative/outside anchor.
  static bool markerBitmapOrigin(const CoinRenderViewportSnapshot & viewport,
                                 float rasterX, float rasterY,
                                 float & originX, float & originY,
                                 std::string & diagnostic) {
    diagnostic.clear();
    if (!validViewport(viewport, diagnostic)) return false;
    if (!std::isfinite(rasterX) || !std::isfinite(rasterY) || rasterX < 0.0f || rasterY < 0.0f ||
        rasterX > float(viewport.width) || rasterY > float(viewport.height))
      return fail("Marker Bitmap origin requires a finite admitted raster position", diagnostic);
    // Power-of-two scaling and the half step are exact in double throughout
    // signed-int viewport bounds, including every source float mantissa. No
    // nearbyint/process rounding mode or integer-boundary epsilon is used.
    // Half-up and nearest-even yield the same final integer origin: every
    // integral-pixel tick is an even multiple of the 2048 subpixel ticks.
    const double scale = 2048.0;
    const float x = float(std::floor(std::floor(double(rasterX) * scale + 0.5) / scale));
    const float y = float(std::floor(std::floor(double(rasterY) * scale + 0.5) / scale));
    originX = x;
    originY = y;
    return true;
  }

  // A clipped/empty rectangle is a successful no-op. Output changes only on
  // success; invalid inputs keep it intact. Pixel dimensions are independent
  // of model rotation/scaling and camera distance, like GL raster operations.
  static bool pixelQuad(const CoinRenderViewportSnapshot & viewport,
                        float x, float y, float width, float height,
                        float depthCoin, const float uvBounds[4],
                        uint32_t materialSlot, CoinRenderScreenRasterQuad & output,
                        std::string & diagnostic) {
    diagnostic.clear();
    if (!validViewport(viewport, diagnostic)) return false;
    if (!uvBounds || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(depthCoin) || width < 0.0f || height < 0.0f)
      return fail("Screen raster rectangle is invalid", diagnostic);
    for (int i = 0; i < 4; ++i)
      if (!std::isfinite(uvBounds[i]))
        return fail("Screen raster texture coordinates are not finite", diagnostic);
    const float right = x + width;
    const float top = y + height;
    if (!std::isfinite(right) || !std::isfinite(top))
      return fail("Screen raster rectangle exceeds finite coordinates", diagnostic);
    CoinRenderScreenRasterQuad candidate;
    if (width == 0.0f || height == 0.0f || depthCoin < -1.0f || depthCoin > 1.0f ||
        right <= 0.0f || top <= 0.0f || x >= float(viewport.width) ||
        y >= float(viewport.height)) {
      output = candidate;
      return true;
    }
    const float xs[2] = {x * 2.0f / float(viewport.width) - 1.0f,
                         right * 2.0f / float(viewport.width) - 1.0f};
    const float ys[2] = {y * 2.0f / float(viewport.height) - 1.0f,
                         top * 2.0f / float(viewport.height) - 1.0f};
    const int corners[4][2] = {{0,0}, {1,0}, {1,1}, {0,1}};
    for (int i = 0; i < 4; ++i) {
      auto & vertex = candidate.vertices[i];
      vertex.position[0] = xs[corners[i][0]];
      vertex.position[1] = ys[corners[i][1]];
      vertex.position[2] = depthCoin;
      vertex.texcoord[0] = uvBounds[corners[i][0] ? 2 : 0];
      vertex.texcoord[1] = uvBounds[corners[i][1] ? 3 : 1];
      vertex.materialSlot = materialSlot;
      if (!std::isfinite(vertex.position[0]) || !std::isfinite(vertex.position[1]))
        return fail("Screen raster NDC rectangle is not finite", diagnostic);
    }
    candidate.visible = true;
    output = candidate;
    return true;
  }

  // Mirrors SoImage::GLRender rather than generatePrimitives/getImage:
  // integer truncation/alignment, viewport crop, then float glPixelZoom with
  // truncated source skip/count. Original image rows remain bottom-origin.
  // Requested values <=0 keep that source dimension, exactly like getSize.
  static bool imageLayout(const CoinRenderScreenRasterAnchor & anchor,
                          const CoinRenderViewportSnapshot & viewport,
                          int32_t originalWidth, int32_t originalHeight,
                          int32_t requestedWidth, int32_t requestedHeight,
                          int horizontalAlignment, int verticalAlignment,
                          uint32_t materialSlot, CoinRenderScreenImageLayout & output,
                          std::string & diagnostic) {
    diagnostic.clear();
    if (originalWidth < 0 || originalHeight < 0 ||
        originalWidth > 32767 || originalHeight > 32767)
      return fail("Screen image source dimensions are invalid", diagnostic);
    CoinRenderScreenImageLayout candidate;
    // SoImage returns before inspecting raster state/resize/alignment when
    // either source dimension is zero. Unused fields cannot reject that no-op.
    if (originalWidth == 0 || originalHeight == 0) {
      output = candidate;
      return true;
    }
    if (!validViewport(viewport, diagnostic)) return false;
    if (!finiteAnchor(anchor))
      return fail("Screen image projected anchor is not finite", diagnostic);
    if (requestedWidth > 32767 || requestedHeight > 32767)
      return fail("UNSUPPORTED: screen image resize exceeds Coin's signed-short size", diagnostic);
    if (horizontalAlignment < LEFT || horizontalAlignment > RIGHT ||
        verticalAlignment < BOTTOM || verticalAlignment > TOP)
      return fail("Screen image alignment is invalid", diagnostic);
    if (anchor.depthCoin < -1.0f || anchor.depthCoin > 1.0f) {
      output = candidate;
      return true;
    }
    int32_t anchorX, anchorY;
    if (!truncatePixel(anchor.pixelX, anchorX) || !truncatePixel(anchor.pixelY, anchorY))
      return fail("Screen image anchor exceeds integer raster coordinates", diagnostic);
    const int32_t width = requestedWidth > 0 ? requestedWidth : originalWidth;
    const int32_t height = requestedHeight > 0 ? requestedHeight : originalHeight;
    // Use wider scratch arithmetic for alignment/crop; no GLint subtraction
    // overflow is permitted at extreme projected positions.
    int64_t x = anchorX;
    int64_t y = anchorY;
    if (horizontalAlignment == RIGHT) x -= width;
    else if (horizontalAlignment == CENTER) x -= width >> 1;
    if (verticalAlignment == TOP) y -= height;
    else if (verticalAlignment == HALF) y -= height >> 1;
    int64_t columns = width, rows = height, skipX = 0, skipY = 0;
    if (x >= viewport.width || x < -int64_t(width) ||
        y > viewport.height || y < -int64_t(height)) {
      output = candidate;
      return true;
    }
    if (x < 0) { columns += x; skipX = -x; x = 0; }
    if (y < 0) { rows += y; skipY = -y; y = 0; }
    if (columns > int64_t(viewport.width) - x) columns = int64_t(viewport.width) - x;
    if (rows > int64_t(viewport.height) - y) rows = int64_t(viewport.height) - y;
    candidate.zoomX = float(width) / float(originalWidth);
    candidate.zoomY = float(height) / float(originalHeight);
    if (width != originalWidth || height != originalHeight) {
      columns = static_cast<int32_t>(float(columns) / candidate.zoomX);
      rows = static_cast<int32_t>(float(rows) / candidate.zoomY);
      skipX = static_cast<int32_t>(float(skipX) / candidate.zoomX);
      skipY = static_cast<int32_t>(float(skipY) / candidate.zoomY);
      if (skipX + columns > originalWidth) columns = originalWidth - skipX;
      if (skipY + rows > originalHeight) rows = originalHeight - skipY;
    }
    if (columns <= 0 || rows <= 0) {
      output = candidate;
      return true;
    }
    if (skipX < 0 || skipY < 0 || skipX + columns > originalWidth ||
        skipY + rows > originalHeight)
      return fail("Screen image source crop is invalid", diagnostic);
    candidate.sourceX = static_cast<uint32_t>(skipX);
    candidate.sourceY = static_cast<uint32_t>(skipY);
    candidate.sourceWidth = static_cast<uint32_t>(columns);
    candidate.sourceHeight = static_cast<uint32_t>(rows);
    candidate.rasterX = float(x);
    candidate.rasterY = float(y);
    const float uv[4] = {float(skipX) / originalWidth, float(skipY) / originalHeight,
      float(skipX + columns) / originalWidth, float(skipY + rows) / originalHeight};
    if (!pixelQuad(viewport, candidate.rasterX, candidate.rasterY,
                   float(columns) * candidate.zoomX, float(rows) * candidate.zoomY,
                   anchor.depthCoin, uv, materialSlot, candidate.quad, diagnostic)) return false;
    output = candidate;
    return true;
  }

  // No filtered resize and no row flip: image fields already use Coin's
  // bottom-origin convention. All four component layouts retain source alpha.
  static bool imagePayload(const uint8_t * bytes, size_t byteCount,
                           uint32_t width, uint32_t height, int components,
                           CoinRenderTextureImageSnapshot & output,
                           bool & transparent, std::string & diagnostic) {
    diagnostic.clear();
    if (!width || !height || components < 1 || components > 4 || !bytes)
      return fail("Screen image payload dimensions/components are invalid", diagnostic);
    if (width > 8192 || height > 8192)
      return fail("UNSUPPORTED: screen image texture dimensions exceed 8192", diagnostic);
    const size_t count = size_t(width) * size_t(height);
    if (count > std::numeric_limits<size_t>::max() / size_t(components) ||
        byteCount != count * size_t(components))
      return fail("Screen image payload byte count mismatch", diagnostic);
    CoinRenderTextureImageSnapshot candidate;
    candidate.width = width;
    candidate.height = height;
    if (!CoinRenderImageCore::convertToRgba8(bytes, count, components, candidate.pixelsRgba))
      return fail("Screen image RGBA conversion failed", diagnostic);
    candidate.contentDigest = CoinRenderImageCore::rgba8Digest(candidate.pixelsRgba);
    const bool candidateTransparent = coin_render_image_has_transparency(bytes, count, components);
    output = std::move(candidate);
    transparent = candidateTransparent;
    return true;
  }

private:
  static bool fail(const char * message, std::string & diagnostic) {
    diagnostic = message;
    return false;
  }
  static bool validViewport(const CoinRenderViewportSnapshot & viewport,
                            std::string & diagnostic) {
    return viewport.width > 0 && viewport.height > 0 ? true :
      fail("Screen raster viewport must have positive dimensions", diagnostic);
  }
  static bool finitePoint(const SbVec3f & point) {
    return std::isfinite(point[0]) && std::isfinite(point[1]) && std::isfinite(point[2]);
  }
  static bool finiteAnchor(const CoinRenderScreenRasterAnchor & anchor) {
    return std::isfinite(anchor.pixelX) && std::isfinite(anchor.pixelY) &&
      std::isfinite(anchor.depthCoin);
  }
  static bool truncatePixel(float value, int32_t & result) {
    if (!std::isfinite(value) || double(value) < std::numeric_limits<int32_t>::min() ||
        double(value) > std::numeric_limits<int32_t>::max()) return false;
    result = static_cast<int32_t>(value);
    return true;
  }
};

#endif // COIN_RENDER_SCREEN_RASTER_CORE_H
