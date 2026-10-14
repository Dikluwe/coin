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

  // Authored indices affect only indexed normal bindings. Reversed coordinate
  // order distinguishes PER_VERTEX occurrences from coordinate-index lookup.
  const int32_t reversed[] = {3,2,1,0,-1};
  const SbVec3f authoredNormals[] = {{1,0,0},{0,1,0},{0,0,1},{-1,0,0}};
  const int32_t normalIndices[] = {2,1,0,3,-1};
  auto normalsView = makeView(quad,4,reversed,5);
  normalsView.normals = CoinRenderSpan<SbVec3f>(authoredNormals,4);
  normalsView.normalIndex = CoinRenderSpan<int32_t>(normalIndices,5);
  for (auto binding : {SoNormalBindingElement::PER_FACE,SoNormalBindingElement::PER_FACE_INDEXED,
                       SoNormalBindingElement::PER_PART,SoNormalBindingElement::PER_PART_INDEXED,
                       SoNormalBindingElement::PER_VERTEX,SoNormalBindingElement::PER_VERTEX_INDEXED}) {
    normalsView.normalBinding=binding;
    auto result=CoinRenderIndexedGeometryCore::buildFaces(normalsView,baseColor);
    ok &= check(result.status==CoinRenderFastPathResult::SUCCESS_PRUNE && result.vertices.size()==4,
                "authored normal binding capture");
    for(size_t i=0;i<result.vertices.size();++i) {
      const size_t expected=binding==SoNormalBindingElement::PER_VERTEX ? i :
        binding==SoNormalBindingElement::PER_VERTEX_INDEXED ? normalIndices[i] :
        (binding==SoNormalBindingElement::PER_FACE_INDEXED || binding==SoNormalBindingElement::PER_PART_INDEXED) ? 2 : 0;
      ok &= check(SbVec3f(result.vertices[i].vertex.normal)==authoredNormals[expected],
                  "normal binding ignores or applies indices according to contract");
    }
  }
  normalsView.normalBinding=SoNormalBindingElement::PER_FACE_INDEXED;
  normalsView.normalIndex=CoinRenderSpan<int32_t>(overriddenMaterialIndices,5);
  const auto absentNormal=CoinRenderIndexedGeometryCore::buildFaces(normalsView,baseColor);
  ok &= check(absentNormal.status==CoinRenderFastPathResult::FALLBACK_CONTINUE && absentNormal.vertices.empty(),
              "unavailable authored normal falls back atomically to Coin");

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

  const int32_t reversedLine[] = {2, 0, 1, -1};
  const int32_t lineNormalIndices[] = {3, 2, 1, -1};
  auto lineView = makeView(linePositions, 3, reversedLine, 4);
  lineView.normals = CoinRenderSpan<SbVec3f>(authoredNormals, 4);
  lineView.normalIndex = CoinRenderSpan<int32_t>(lineNormalIndices, 4);
  const int occurrences[] = {0, 1, 1, 2};
  for (auto binding : {SoNormalBindingElement::OVERALL, SoNormalBindingElement::PER_PART,
                       SoNormalBindingElement::PER_PART_INDEXED, SoNormalBindingElement::PER_FACE,
                       SoNormalBindingElement::PER_FACE_INDEXED, SoNormalBindingElement::PER_VERTEX,
                       SoNormalBindingElement::PER_VERTEX_INDEXED}) {
    lineView.normalBinding = binding;
    const auto result = CoinRenderIndexedGeometryCore::buildLines(lineView, baseColor);
    ok &= check(result.status == CoinRenderFastPathResult::SUCCESS_PRUNE &&
                    result.indices.size() == 4,
                "line authored normal bindings");
    for (size_t i = 0; i < result.indices.size(); ++i) {
      size_t expected = 0;
      if (binding == SoNormalBindingElement::PER_PART)
        expected = i / 2;
      if (binding == SoNormalBindingElement::PER_PART_INDEXED)
        expected = lineNormalIndices[i / 2];
      if (binding == SoNormalBindingElement::PER_FACE_INDEXED)
        expected = 3;
      if (binding == SoNormalBindingElement::PER_VERTEX)
        expected = occurrences[i];
      if (binding == SoNormalBindingElement::PER_VERTEX_INDEXED)
        expected = lineNormalIndices[occurrences[i]];
      ok &= check(SbVec3f(result.vertices[result.indices[i]].vertex.normal) ==
                      authoredNormals[expected],
                  "line normal occurrence/index vector");
    }
  }
  lineView.normalBinding = SoNormalBindingElement::OVERALL;
  lineView.materialBinding = SoMaterialBindingElement::PER_VERTEX;
  const auto materialLine = CoinRenderIndexedGeometryCore::buildLines(lineView, baseColor);
  for (size_t i = 0; i < materialLine.indices.size(); ++i)
    ok &= check(materialLine.vertices[materialLine.indices[i]].materialIndex == occurrences[i],
                "shared polyline endpoint does not consume another material occurrence");
  lineView.normalBinding = SoNormalBindingElement::PER_FACE_INDEXED;
  const int32_t invalidLineNormal[] = {8};
  lineView.normalIndex = CoinRenderSpan<int32_t>(invalidLineNormal, 1);
  const auto normalFallback = CoinRenderIndexedGeometryCore::buildLines(lineView, baseColor);
  ok &= check(normalFallback.status == CoinRenderFastPathResult::FALLBACK_CONTINUE &&
                  normalFallback.vertices.empty() && normalFallback.indices.empty(),
              "invalid line normal index falls back atomically");

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
