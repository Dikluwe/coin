#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>

#include "rendering/coinrender/CoinRenderIndexedGeometryCore.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool
check(bool condition, const char * message)
{
  if (!condition) {
    std::cerr << "CoinRenderIndexedGeometryCoreTest: " << message << '\n';
  }
  return condition;
}

CoinRenderDirectGeometryView
makeView(const SbVec3f * positions, size_t positionCount,
         const int32_t * indices, size_t indexCount)
{
  CoinRenderDirectGeometryView view;
  view.positions = CoinRenderSpan<SbVec3f>(positions, positionCount);
  view.coordIndex = CoinRenderSpan<int32_t>(indices, indexCount);
  return view;
}

} // namespace

int
main()
{
  SoDB::init();
  bool ok = true;

  const SbVec3f quad[] = {
    SbVec3f(0.0f, 0.0f, 0.0f),
    SbVec3f(1.0f, 0.0f, 0.0f),
    SbVec3f(1.0f, 1.0f, 0.0f),
    SbVec3f(0.0f, 1.0f, 0.0f)
  };
  const int32_t quadIndices[] = {0, 1, 2, 3, -1};
  CoinRenderIndexedGeometryOptions baseColor;
  baseColor.hasTraversalState = true;
  baseColor.baseColorLighting = true;

  const CoinRenderIndexedGeometryResult quadResult =
    CoinRenderIndexedGeometryCore::buildFaces(
      makeView(quad, 4, quadIndices, 5), baseColor);
  ok &= check(quadResult.status == CoinRenderFastPathResult::SUCCESS_PRUNE &&
              quadResult.vertices.size() == 4 &&
              quadResult.indices.size() == 6 &&
              quadResult.indices[0] == 0 &&
              quadResult.indices[3] == 0,
              "convex quad was not transformed deterministically");

  CoinRenderDirectGeometryView overriddenMaterialView =
    makeView(quad, 4, quadIndices, 5);
  const int32_t overriddenMaterialIndices[] = {7, 7, 7, 7, -1};
  overriddenMaterialView.materialIndex =
    CoinRenderSpan<int32_t>(overriddenMaterialIndices, 5);
  overriddenMaterialView.materialBinding =
    SoMaterialBindingElement::PER_VERTEX_INDEXED;
  CoinRenderIndexedGeometryOptions overriddenMaterialOptions = baseColor;
  overriddenMaterialOptions.materialCount = 1;
  const CoinRenderIndexedGeometryResult overriddenMaterial =
    CoinRenderIndexedGeometryCore::buildFaces(
      overriddenMaterialView, overriddenMaterialOptions);
  ok &= check(overriddenMaterial.status == CoinRenderFastPathResult::SUCCESS_PRUNE &&
              overriddenMaterial.vertices.size() == 4,
              "material override rejected pre-existing material indices");

  const int32_t invalidIndices[] = {0, 1, 7, -1};
  const CoinRenderIndexedGeometryResult invalid =
    CoinRenderIndexedGeometryCore::buildFaces(
      makeView(quad, 4, invalidIndices, 4), baseColor);
  ok &= check(invalid.status == CoinRenderFastPathResult::INVALID_SCENE &&
              invalid.vertices.empty() && invalid.indices.empty() &&
              invalid.diagnostic ==
                "IndexedFaceSet coordinate index out of bounds",
              "invalid input did not fail atomically with stable diagnostic");

  const SbVec3f concave[] = {
    SbVec3f(0.0f, 0.0f, 0.0f),
    SbVec3f(1.0f, 0.0f, 0.0f),
    SbVec3f(0.2f, 0.2f, 0.0f),
    SbVec3f(0.0f, 1.0f, 0.0f)
  };
  const CoinRenderIndexedGeometryResult fallback =
    CoinRenderIndexedGeometryCore::buildFaces(
      makeView(concave, 4, quadIndices, 5), baseColor);
  ok &= check(fallback.status == CoinRenderFastPathResult::FALLBACK_CONTINUE &&
              fallback.vertices.empty() && fallback.indices.empty(),
              "concave quad did not preserve the Coin fallback");

  const SbVec3f folded[] = {
    SbVec3f(0.0f, 0.0f, 0.0f),
    SbVec3f(1.0f, 0.0f, 0.0f),
    SbVec3f(0.0f, 1.0f, 0.0f),
    SbVec3f(0.0f, 0.0f, 1.0f)
  };
  const int32_t foldedIndices[] = {
    0, 1, 2, -1,
    0, 3, 1, -1
  };
  CoinRenderDirectGeometryView generatedView =
    makeView(folded, 4, foldedIndices, 8);
  generatedView.normalBinding = SoNormalBindingElement::PER_VERTEX_INDEXED;
  CoinRenderIndexedGeometryOptions generatedOptions;
  generatedOptions.hasTraversalState = true;
  generatedOptions.baseColorLighting = false;
  generatedOptions.counterClockwise = true;
  generatedOptions.creaseAngle = 0.5f;
  const CoinRenderIndexedGeometryResult generated =
    CoinRenderIndexedGeometryCore::buildFaces(
      generatedView, generatedOptions);
  ok &= check(generated.status == CoinRenderFastPathResult::SUCCESS_PRUNE &&
              generated.indices.size() == 6 &&
              !generated.vertices.empty(),
              "generated normals require no traversal");
  for (size_t i = 0; i < generated.vertices.size(); ++i) {
    const CoinRenderVertexSnapshot & vertex = generated.vertices[i].vertex;
    const float lengthSquared =
      vertex.normal[0] * vertex.normal[0] +
      vertex.normal[1] * vertex.normal[1] +
      vertex.normal[2] * vertex.normal[2];
    ok &= check(std::isfinite(lengthSquared) && lengthSquared > 0.5f,
                "generated normal is invalid");
  }

  const SbVec3f linePositions[] = {
    SbVec3f(0.0f, 0.0f, 0.0f),
    SbVec3f(1.0f, 0.0f, 0.0f),
    SbVec3f(2.0f, 0.0f, 0.0f)
  };
  const int32_t lineIndices[] = {0, 1, 2, -1};
  const CoinRenderIndexedGeometryResult lines =
    CoinRenderIndexedGeometryCore::buildLines(
      makeView(linePositions, 3, lineIndices, 4), baseColor);
  ok &= check(lines.status == CoinRenderFastPathResult::SUCCESS_PRUNE &&
              lines.vertices.size() == 3 &&
              lines.indices.size() == 4,
              "polyline was not expanded into line-list segments");

  std::vector<CoinRenderIndexedVertex> digestVertices = quadResult.vertices;
  const uint64_t digest =
    CoinRenderIndexedGeometryCore::payloadDigest(
      digestVertices, quadResult.indices);
  digestVertices[0].vertex.materialSlot = 1;
  ok &= check(digest != 0 &&
              digest != CoinRenderIndexedGeometryCore::payloadDigest(
                digestVertices, quadResult.indices),
              "payload digest ignored backend-visible vertex data");

  if (!ok) return 1;
  std::cout << "CoinRenderIndexedGeometryCoreTest passed\n";
  return 0;
}
