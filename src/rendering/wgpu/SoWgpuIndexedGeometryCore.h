#ifndef COIN_SOWGPUINDEXEDGEOMETRYCORE_H
#define COIN_SOWGPUINDEXEDGEOMETRYCORE_H

#include <Inventor/CoinWgpuExport.h>
#include "rendering/wgpu/SoWgpuDirectGeometry.h"
#include "rendering/wgpu/SoWgpuFramePlan.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * Traversal-free facts needed to transform indexed Coin geometry.
 *
 * Wiring captures these values from SoState and elements. Core consumes the
 * values without reading the scene graph or controlling traversal.
 */
struct SoWgpuIndexedGeometryOptions {
  int32_t materialCount = 0;
  bool hasTexture = false;
  bool proceduralTextureCoordinates = false;
  bool hasTraversalState = false;
  bool baseColorLighting = false;
  bool counterClockwise = true;
  float creaseAngle = 0.0f;
};

/** Mechanical vertex plus the Coin material index that Wiring must resolve. */
struct SoWgpuIndexedVertex {
  VertexSnapshot vertex;
  int32_t materialIndex = 0;
};

/** Atomic output of an indexed-geometry transformation. */
struct SoWgpuIndexedGeometryResult {
  FastPathResult status = FastPathResult::FALLBACK_CONTINUE;
  std::vector<SoWgpuIndexedVertex> vertices;
  std::vector<uint32_t> indices;
  std::string diagnostic;
};

/**
 * Traversal-free indexed geometry and generated-normal algorithms.
 *
 * The Core accepts Coin-native arrays and already-captured state facts. It
 * owns no SoAction, SoState, element, GPU resource or user-visible policy.
 */
class SoWgpuIndexedGeometryCore {
public:
  COIN_WGPU_DLL_API static SoWgpuIndexedGeometryResult buildFaces(
    const DirectGeometryView & view,
    const SoWgpuIndexedGeometryOptions & options);
  COIN_WGPU_DLL_API static SoWgpuIndexedGeometryResult buildLines(
    const DirectGeometryView & view,
    const SoWgpuIndexedGeometryOptions & options);
  COIN_WGPU_DLL_API static uint64_t payloadDigest(
    const std::vector<SoWgpuIndexedVertex> & vertices,
    const std::vector<uint32_t> & indices);
};

#endif // !COIN_SOWGPUINDEXEDGEOMETRYCORE_H
