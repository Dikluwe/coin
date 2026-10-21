#ifndef COIN_RENDER_POLYGON_STYLE_CORE_H
#define COIN_RENDER_POLYGON_STYLE_CORE_H

#include <Inventor/CoinRenderExport.h>

#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderLightingCore.h"
#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"
#include <Inventor/SbVec3d.h>

struct CoinRenderPolygonStyleVertex {
  CoinRenderVertexSnapshot vertex;
  CoinRenderMaterialSnapshot material;
  bool boundaryEdge = true; // Boundary flag of the outgoing edge.
};

// Shade a captured vertex before clipping, for flat primary color or Gouraud
// stroke endpoints. Texture coordinates and fog keep their own interpolation.
COIN_RENDER_DLL_API CoinRenderMaterialSnapshot coin_render_bake_vertex_material(
    const CoinRenderVertexSnapshot &provoking, const CoinRenderMaterialSnapshot &material,
    const CoinRenderRenderStateSnapshot &state, const CoinRenderLightingSnapshot &lighting);

// Original polygon ring; no scene traversal, Coin elements or GPU calls.
// Outputs a clipped, culled ring with Gouraud colors baked before interpolation.
COIN_RENDER_DLL_API bool coin_render_resolve_polygon_style(
    const std::vector<CoinRenderVertexSnapshot>& ring, const CoinRenderRenderStateSnapshot& state,
    const std::vector<CoinRenderMaterialSnapshot>& materials,
    const CoinRenderLightingSnapshot& lighting, std::vector<CoinRenderPolygonStyleVertex>& output,
    std::string& diagnostic, bool preserveDegenerateContour = false,
    const bool * boundaryEdges = nullptr);
enum class CoinRenderPolygonStyle { LINES, POINTS };
struct CoinRenderPolygonStyleResult {
  std::vector<CoinRenderPolygonStyleVertex> vertices;
  std::vector<uint32_t> indices;
  // Nonplanar polygon offset is resolved per originating triangle edge/point.
  std::vector<float> primitiveSlopeBias;
  std::vector<float> primitiveMaximumDepth;
  CoinRenderRenderStateSnapshot state;
  CoinRenderPrimitiveTopology topology = CoinRenderPrimitiveTopology::LINE_LIST;
};
COIN_RENDER_DLL_API bool coin_render_prepare_polygon_style(
    const std::vector<CoinRenderVertexSnapshot>& ring, const CoinRenderRenderStateSnapshot& state,
    const std::vector<CoinRenderMaterialSnapshot>& materials,
    const CoinRenderLightingSnapshot& lighting, CoinRenderPolygonStyle style,
    CoinRenderPolygonStyleResult& result, std::string& diagnostic,
    const CoinRenderViewportSnapshot& viewport, bool preserveDegenerateContour = false,
    const std::vector<CoinRenderVertexSnapshot>* triangles = nullptr,
    const CoinRenderFramePlan* frame = nullptr);
#endif
