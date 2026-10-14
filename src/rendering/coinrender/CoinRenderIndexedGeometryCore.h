#ifndef COIN_RENDER_INDEXED_GEOMETRY_CORE_H
#define COIN_RENDER_INDEXED_GEOMETRY_CORE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderDirectGeometry.h"
#include "rendering/coinrender/CoinRenderFramePlan.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * Traversal-free facts needed to transform indexed Coin geometry.
 *
 * Wiring captures these values from SoState and elements. Core consumes the
 * values without reading the scene graph or controlling traversal.
 */
struct CoinRenderIndexedGeometryOptions {
  int32_t materialCount = 0;
  bool hasTexture = false;
  bool proceduralTextureCoordinates = false;
  bool hasTraversalState = false;
  bool baseColorLighting = false;
  bool counterClockwise = true;
  float creaseAngle = 0.0f;
};

/** Mechanical vertex plus the Coin material index that Wiring must resolve. */
struct CoinRenderIndexedVertex {
  CoinRenderVertexSnapshot vertex;
  int32_t materialIndex = 0;
};

/** Atomic output of an indexed-geometry transformation. */
struct CoinRenderIndexedGeometryResult {
  CoinRenderFastPathResult status = CoinRenderFastPathResult::FALLBACK_CONTINUE;
  std::vector<CoinRenderIndexedVertex> vertices;
  std::vector<uint32_t> indices;
  std::string diagnostic;
};

/**
 * Traversal-free indexed geometry and generated-normal algorithms.
 *
 * The Core accepts Coin-native arrays and already-captured state facts. It
 * owns no SoAction, SoState, element, GPU resource or user-visible policy.
 */
class CoinRenderIndexedGeometryCore {
public:
  COIN_RENDER_DLL_API static CoinRenderIndexedGeometryResult buildFaces(
    const CoinRenderDirectGeometryView & view,
    const CoinRenderIndexedGeometryOptions & options);
  COIN_RENDER_DLL_API static CoinRenderIndexedGeometryResult buildLines(
    const CoinRenderDirectGeometryView & view,
    const CoinRenderIndexedGeometryOptions & options);
  COIN_RENDER_DLL_API static uint64_t payloadDigest(
    const std::vector<CoinRenderIndexedVertex> & vertices,
    const std::vector<uint32_t> & indices);
};

#endif // !COIN_RENDER_INDEXED_GEOMETRY_CORE_H
