#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>

#include "rendering/wgpu/SoWgpuIndexedGeometryCore.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool
check(bool condition, const char * message)
{
  if (!condition) {
    std::cerr << "WgpuIndexedGeometryCoreTest: " << message << '\n';
  }
  return condition;
}

DirectGeometryView
makeView(const SbVec3f * positions, size_t positionCount,
         const int32_t * indices, size_t indexCount)
{
  DirectGeometryView view;
  view.positions = SoWgpuSpan<SbVec3f>(positions, positionCount);
  view.coordIndex = SoWgpuSpan<int32_t>(indices, indexCount);
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
  SoWgpuIndexedGeometryOptions baseColor;
  baseColor.hasTraversalState = true;
  baseColor.baseColorLighting = true;

  const SoWgpuIndexedGeometryResult quadResult =
    SoWgpuIndexedGeometryCore::buildFaces(
      makeView(quad, 4, quadIndices, 5), baseColor);
  ok &= check(quadResult.status == FastPathResult::SUCCESS_PRUNE &&
              quadResult.vertices.size() == 4 &&
              quadResult.indices.size() == 6 &&
              quadResult.indices[0] == 0 &&
              quadResult.indices[3] == 0,
              "convex quad was not transformed deterministically");

  DirectGeometryView overriddenMaterialView =
    makeView(quad, 4, quadIndices, 5);
  const int32_t overriddenMaterialIndices[] = {7, 7, 7, 7, -1};
  overriddenMaterialView.materialIndex =
    SoWgpuSpan<int32_t>(overriddenMaterialIndices, 5);
  overriddenMaterialView.materialBinding =
    SoMaterialBindingElement::PER_VERTEX_INDEXED;
  SoWgpuIndexedGeometryOptions overriddenMaterialOptions = baseColor;
  overriddenMaterialOptions.materialCount = 1;
  const SoWgpuIndexedGeometryResult overriddenMaterial =
    SoWgpuIndexedGeometryCore::buildFaces(
      overriddenMaterialView, overriddenMaterialOptions);
  ok &= check(overriddenMaterial.status == FastPathResult::SUCCESS_PRUNE &&
              overriddenMaterial.vertices.size() == 4,
              "material override rejected pre-existing material indices");

  const int32_t invalidIndices[] = {0, 1, 7, -1};
  const SoWgpuIndexedGeometryResult invalid =
    SoWgpuIndexedGeometryCore::buildFaces(
      makeView(quad, 4, invalidIndices, 4), baseColor);
  ok &= check(invalid.status == FastPathResult::INVALID_SCENE &&
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
  const SoWgpuIndexedGeometryResult fallback =
    SoWgpuIndexedGeometryCore::buildFaces(
      makeView(concave, 4, quadIndices, 5), baseColor);
  ok &= check(fallback.status == FastPathResult::FALLBACK_CONTINUE &&
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
  DirectGeometryView generatedView =
    makeView(folded, 4, foldedIndices, 8);
  generatedView.normalBinding = SoNormalBindingElement::PER_VERTEX_INDEXED;
  SoWgpuIndexedGeometryOptions generatedOptions;
  generatedOptions.hasTraversalState = true;
  generatedOptions.baseColorLighting = false;
  generatedOptions.counterClockwise = true;
  generatedOptions.creaseAngle = 0.5f;
  const SoWgpuIndexedGeometryResult generated =
    SoWgpuIndexedGeometryCore::buildFaces(
      generatedView, generatedOptions);
  ok &= check(generated.status == FastPathResult::SUCCESS_PRUNE &&
              generated.indices.size() == 6 &&
              !generated.vertices.empty(),
              "generated normals require no traversal");
  for (size_t i = 0; i < generated.vertices.size(); ++i) {
    const VertexSnapshot & vertex = generated.vertices[i].vertex;
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
  const SoWgpuIndexedGeometryResult lines =
    SoWgpuIndexedGeometryCore::buildLines(
      makeView(linePositions, 3, lineIndices, 4), baseColor);
  ok &= check(lines.status == FastPathResult::SUCCESS_PRUNE &&
              lines.vertices.size() == 3 &&
              lines.indices.size() == 4,
              "polyline was not expanded into line-list segments");

  std::vector<SoWgpuIndexedVertex> digestVertices = quadResult.vertices;
  const uint64_t digest =
    SoWgpuIndexedGeometryCore::payloadDigest(
      digestVertices, quadResult.indices);
  digestVertices[0].vertex.materialSlot = 1;
  ok &= check(digest != 0 &&
              digest != SoWgpuIndexedGeometryCore::payloadDigest(
                digestVertices, quadResult.indices),
              "payload digest ignored backend-visible vertex data");

  if (!ok) return 1;
  std::cout << "WgpuIndexedGeometryCoreTest passed\n";
  return 0;
}
