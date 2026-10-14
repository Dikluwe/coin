#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderIndexedGeometryCore.h"

#include <Inventor/caches/SoNormalCache.h>

#include <cmath>
#include <limits>
#include <map>

namespace {

struct IndexSection {
  size_t startIndex;
  size_t count;
};

struct VertexKey {
  int32_t coordIdx;
  int32_t normalIdx;
  int32_t texCoordIdx;
  int32_t materialIdx;

  bool operator<(const VertexKey & other) const
  {
    if (this->coordIdx != other.coordIdx) return this->coordIdx < other.coordIdx;
    if (this->normalIdx != other.normalIdx) return this->normalIdx < other.normalIdx;
    if (this->texCoordIdx != other.texCoordIdx) return this->texCoordIdx < other.texCoordIdx;
    return this->materialIdx < other.materialIdx;
  }
};

CoinRenderIndexedGeometryResult
finish(CoinRenderFastPathResult status, const char * diagnostic = NULL)
{
  CoinRenderIndexedGeometryResult result;
  result.status = status;
  if (diagnostic) result.diagnostic = diagnostic;
  return result;
}

bool
isQuadConvex(const SbVec3f & p0,
             const SbVec3f & p1,
             const SbVec3f & p2,
             const SbVec3f & p3)
{
  const SbVec3f e0 = p1 - p0;
  const SbVec3f e1 = p2 - p1;
  const SbVec3f e2 = p3 - p2;
  const SbVec3f e3 = p0 - p3;
  SbVec3f c0 = e0.cross(e1);
  const SbVec3f c1 = e1.cross(e2);
  const SbVec3f c2 = e2.cross(e3);
  const SbVec3f c3 = e3.cross(e0);
  if (c0.sqrLength() < 1e-10f) return false;
  if (c0.dot(c1) <= 1e-7f ||
      c0.dot(c2) <= 1e-7f ||
      c0.dot(c3) <= 1e-7f) return false;
  c0.normalize();
  const float distance = std::abs((p3 - p0).dot(c0));
  const float diagonal = (p2 - p0).length();
  return diagonal <= 1e-5f || (distance / diagonal) <= 1e-2f;
}

bool
parseSections(const CoinRenderDirectGeometryView & view,
              const char * invalidNegative,
              const char * outOfBounds,
              std::vector<IndexSection> & sections,
              CoinRenderIndexedGeometryResult & failure)
{
  size_t currentStart = 0;
  size_t currentCount = 0;
  for (size_t i = 0; i < view.coordIndex.size; ++i) {
    const int32_t index = view.coordIndex[i];
    if (index < -1) {
      failure = finish(CoinRenderFastPathResult::INVALID_SCENE, invalidNegative);
      return false;
    }
    if (index == -1) {
      if (currentCount > 0) {
        sections.push_back(IndexSection{currentStart, currentCount});
      }
      currentStart = i + 1;
      currentCount = 0;
    }
    else {
      if (static_cast<size_t>(index) >= view.positions.size) {
        failure = finish(CoinRenderFastPathResult::INVALID_SCENE, outOfBounds);
        return false;
      }
      ++currentCount;
    }
  }
  if (currentCount > 0) {
    sections.push_back(IndexSection{currentStart, currentCount});
  }
  return true;
}

bool
positionsAreFinite(const CoinRenderDirectGeometryView & view)
{
  for (size_t i = 0; i < view.positions.size; ++i) {
    const SbVec3f & position = view.positions[i];
    if (!std::isfinite(position[0]) ||
        !std::isfinite(position[1]) ||
        !std::isfinite(position[2])) return false;
  }
  return true;
}

bool
materialIndicesAreValid(const CoinRenderDirectGeometryView & view,
                        const char * invalidNegative,
                        CoinRenderIndexedGeometryResult & failure)
{
  // Coin clamps material lookups after state overrides; preserve indices here.
  for (size_t i = 0; i < view.materialIndex.size; ++i) {
    const int32_t value = view.materialIndex[i];
    if (value < -1) {
      failure = finish(CoinRenderFastPathResult::INVALID_SCENE, invalidNegative);
      return false;
    }
  }
  return true;
}

uint32_t
getOrAddVertex(std::map<VertexKey, uint32_t> & uniqueVertices,
               CoinRenderIndexedGeometryResult & output,
               int32_t coordIndex,
               int32_t normalIndex,
               int32_t texCoordIndex,
               int32_t materialIndex,
               const SbVec3f & position,
               const SbVec3f & normal,
               const SbVec2f & texcoord)
{
  const VertexKey key = {
    coordIndex, normalIndex, texCoordIndex, materialIndex
  };
  const std::map<VertexKey, uint32_t>::const_iterator found =
    uniqueVertices.find(key);
  if (found != uniqueVertices.end()) return found->second;

  CoinRenderIndexedVertex indexedVertex;
  indexedVertex.vertex.position[0] = position[0];
  indexedVertex.vertex.position[1] = position[1];
  indexedVertex.vertex.position[2] = position[2];
  indexedVertex.vertex.normal[0] = normal[0];
  indexedVertex.vertex.normal[1] = normal[1];
  indexedVertex.vertex.normal[2] = normal[2];
  indexedVertex.vertex.texcoord[0] = texcoord[0];
  indexedVertex.vertex.texcoord[1] = texcoord[1];
  indexedVertex.materialIndex = materialIndex;
  const uint32_t index = static_cast<uint32_t>(output.vertices.size());
  output.vertices.push_back(indexedVertex);
  uniqueVertices[key] = index;
  return index;
}

} // namespace

CoinRenderIndexedGeometryResult
CoinRenderIndexedGeometryCore::buildFaces(
  const CoinRenderDirectGeometryView & inputView,
  const CoinRenderIndexedGeometryOptions & options)
{
  CoinRenderDirectGeometryView view = inputView;
  if (view.positions.empty() || view.coordIndex.empty()) {
    return finish(CoinRenderFastPathResult::SUCCESS_PRUNE);
  }
  if (options.hasTexture) {
    if (options.proceduralTextureCoordinates) {
      return finish(CoinRenderFastPathResult::UNSUPPORTED,
        "Procedural/DEFAULT texture coordinates are not supported in Subwave 3B");
    }
    if (view.texcoords.empty()) {
      return finish(CoinRenderFastPathResult::UNSUPPORTED,
        "Explicit texture coordinates missing for textured IndexedFaceSet");
    }
  }
  for (size_t i = 0; i < view.texCoordIndex.size; ++i) {
    const int32_t value = view.texCoordIndex[i];
    if (value < -1) {
      return finish(CoinRenderFastPathResult::INVALID_SCENE,
        "IndexedFaceSet textureCoordIndex contains invalid negative index < -1");
    }
    if (!view.texcoords.empty() && value >= 0 &&
        static_cast<size_t>(value) >= view.texcoords.size) {
      return finish(CoinRenderFastPathResult::INVALID_SCENE,
        "IndexedFaceSet textureCoordIndex references out-of-bounds texture coordinate");
    }
  }
  if (!positionsAreFinite(view)) {
    return finish(CoinRenderFastPathResult::INVALID_SCENE,
      "IndexedFaceSet vertex position contains NaN or Inf");
  }

  std::vector<IndexSection> faces;
  CoinRenderIndexedGeometryResult failure;
  if (!parseSections(
        view,
        "IndexedFaceSet contains invalid negative coordinate index < -1",
        "IndexedFaceSet coordinate index out of bounds",
        faces, failure)) return failure;
  if (faces.empty()) return finish(CoinRenderFastPathResult::SUCCESS_PRUNE);

  for (size_t i = 0; i < faces.size(); ++i) {
    const size_t count = faces[i].count;
    if (count < 3) continue;
    if (count > 4) return finish(CoinRenderFastPathResult::FALLBACK_CONTINUE);
    if (count == 4) {
      const size_t start = faces[i].startIndex;
      const int32_t i0 = view.coordIndex[start];
      const int32_t i1 = view.coordIndex[start + 1];
      const int32_t i2 = view.coordIndex[start + 2];
      const int32_t i3 = view.coordIndex[start + 3];
      if (!isQuadConvex(view.positions[i0], view.positions[i1],
                        view.positions[i2], view.positions[i3])) {
        return finish(CoinRenderFastPathResult::FALLBACK_CONTINUE);
      }
    }
  }
  if (!materialIndicesAreValid(
        view,
        "IndexedFaceSet contains invalid negative material index < -1",
        failure)) return failure;

  SoNormalCache generatedNormals(NULL);
  if (view.normals.empty() && !options.baseColorLighting) {
    if (!options.hasTraversalState ||
        (view.normalBinding != SoNormalBindingElement::PER_VERTEX &&
         view.normalBinding != SoNormalBindingElement::PER_VERTEX_INDEXED) ||
        view.positions.size > std::numeric_limits<unsigned int>::max() ||
        view.coordIndex.size >
          static_cast<size_t>(std::numeric_limits<int>::max())) {
      return finish(CoinRenderFastPathResult::FALLBACK_CONTINUE);
    }
    generatedNormals.generatePerVertex(
      view.positions.data, static_cast<unsigned int>(view.positions.size),
      view.coordIndex.data, static_cast<int>(view.coordIndex.size),
      options.creaseAngle, NULL, -1,
      options.counterClockwise ? TRUE : FALSE);
    if (generatedNormals.getNum() <= 0 ||
        generatedNormals.getNumIndices() !=
          static_cast<int>(view.coordIndex.size)) {
      return finish(CoinRenderFastPathResult::FALLBACK_CONTINUE);
    }
    view.normals = CoinRenderSpan<SbVec3f>(
      generatedNormals.getNormals(),
      static_cast<size_t>(generatedNormals.getNum()));
    view.normalIndex = CoinRenderSpan<int32_t>(
      generatedNormals.getIndices(),
      static_cast<size_t>(generatedNormals.getNumIndices()));
    view.normalBinding = SoNormalBindingElement::PER_VERTEX_INDEXED;
  }

  CoinRenderIndexedGeometryResult output;
  output.status = CoinRenderFastPathResult::SUCCESS_PRUNE;
  std::map<VertexKey, uint32_t> uniqueVertices;
  size_t vertexCounter = 0;
  bool invalidNormalIndex = false;
  size_t validFaceIndex = 0;

  for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
    const size_t count = faces[faceIndex].count;
    if (count < 3) continue;
    const size_t start = faces[faceIndex].startIndex;
    SbVec3f faceNormal(0.0f, 0.0f, 1.0f);
    if (view.normalBinding == SoNormalBindingElement::OVERALL &&
        !view.normals.empty()) {
      faceNormal = view.normals[0];
    }
    else if ((view.normalBinding == SoNormalBindingElement::PER_FACE ||
              view.normalBinding == SoNormalBindingElement::PER_PART ||
              view.normalBinding == SoNormalBindingElement::PER_FACE_INDEXED ||
              view.normalBinding == SoNormalBindingElement::PER_PART_INDEXED) &&
             !view.normals.empty()) {
      size_t normalIndex = validFaceIndex;
      const bool indexed = view.normalBinding == SoNormalBindingElement::PER_FACE_INDEXED ||
                           view.normalBinding == SoNormalBindingElement::PER_PART_INDEXED;
      if (indexed && !view.normalIndex.empty()) {
        if (validFaceIndex >= view.normalIndex.size || view.normalIndex[validFaceIndex] < 0)
          return finish(CoinRenderFastPathResult::FALLBACK_CONTINUE);
        normalIndex = static_cast<size_t>(view.normalIndex[validFaceIndex]);
      }
      if (normalIndex >= view.normals.size)
        return finish(CoinRenderFastPathResult::FALLBACK_CONTINUE);
      faceNormal = view.normals[normalIndex];
    }
    else if (view.normals.empty()) {
      const int32_t c0 = view.coordIndex[start];
      const int32_t c1 = view.coordIndex[start + 1];
      const int32_t c2 = view.coordIndex[start + 2];
      SbVec3f normal =
        (view.positions[c1] - view.positions[c0]).cross(
          view.positions[c2] - view.positions[c0]);
      if (normal.sqrLength() > 1e-10f) {
        normal.normalize();
        faceNormal = normal;
      }
    }

    int32_t faceMaterialIndex = static_cast<int32_t>(validFaceIndex);
    if (view.materialBinding == SoMaterialBindingElement::OVERALL) {
      faceMaterialIndex = 0;
    }
    else if (view.materialBinding == SoMaterialBindingElement::PER_FACE_INDEXED ||
             view.materialBinding == SoMaterialBindingElement::PER_PART_INDEXED) {
      if (!view.materialIndex.empty() &&
          validFaceIndex < view.materialIndex.size) {
        const int32_t value = view.materialIndex[validFaceIndex];
        if (value >= 0) faceMaterialIndex = value;
      }
    }

    const auto resolveVertex =
      [&](size_t vertexOffsetInFace) -> uint32_t {
        const size_t indexPosition = start + vertexOffsetInFace;
        const int32_t coordinateIndex = view.coordIndex[indexPosition];
        const size_t occurrence = vertexCounter++;
        const SbVec3f & position = view.positions[coordinateIndex];
        SbVec3f normal = faceNormal;
        int32_t normalKey = 0;
        if (view.normalBinding == SoNormalBindingElement::PER_FACE ||
            view.normalBinding == SoNormalBindingElement::PER_FACE_INDEXED ||
            view.normalBinding == SoNormalBindingElement::PER_PART ||
            view.normalBinding == SoNormalBindingElement::PER_PART_INDEXED) {
          normalKey = static_cast<int32_t>(validFaceIndex);
        }
        else if (view.normalBinding == SoNormalBindingElement::PER_VERTEX ||
                 view.normalBinding == SoNormalBindingElement::PER_VERTEX_INDEXED) {
          normalKey = coordinateIndex;
          if (!view.normals.empty()) {
            const bool indexed = view.normalBinding == SoNormalBindingElement::PER_VERTEX_INDEXED;
            size_t normalIndex = indexed ? static_cast<size_t>(coordinateIndex) : occurrence;
            if (indexed && !view.normalIndex.empty()) {
              if (indexPosition >= view.normalIndex.size || view.normalIndex[indexPosition] < 0)
                invalidNormalIndex = true;
              else normalIndex = static_cast<size_t>(view.normalIndex[indexPosition]);
            }
            if (normalIndex < view.normals.size) {
              normal = view.normals[normalIndex];
              normalKey = static_cast<int32_t>(normalIndex);
            } else invalidNormalIndex = true;
          }
        }

        int32_t materialIndex = faceMaterialIndex;
        if (view.materialBinding == SoMaterialBindingElement::OVERALL) {
          materialIndex = 0;
        }
        else if (view.materialBinding == SoMaterialBindingElement::PER_VERTEX) {
          materialIndex = static_cast<int32_t>(occurrence);
        }
        else if (view.materialBinding ==
                 SoMaterialBindingElement::PER_VERTEX_INDEXED) {
          materialIndex = coordinateIndex;
          if (!view.materialIndex.empty() &&
              indexPosition < view.materialIndex.size) {
            const int32_t value = view.materialIndex[indexPosition];
            if (value >= 0) materialIndex = value;
          }
        }

        SbVec2f texcoord(0.0f, 0.0f);
        int32_t texcoordKey = 0;
        if (!view.texcoords.empty()) {
          size_t texcoordIndex = static_cast<size_t>(coordinateIndex);
          if (!view.texCoordIndex.empty() &&
              indexPosition < view.texCoordIndex.size) {
            const int32_t value = view.texCoordIndex[indexPosition];
            if (value >= 0 &&
                static_cast<size_t>(value) < view.texcoords.size) {
              texcoordIndex = static_cast<size_t>(value);
            }
          }
          if (texcoordIndex < view.texcoords.size) {
            texcoord = view.texcoords[texcoordIndex];
            texcoordKey = static_cast<int32_t>(texcoordIndex);
          }
        }
        return getOrAddVertex(
          uniqueVertices, output,
          coordinateIndex, normalKey, texcoordKey, materialIndex,
          position, normal, texcoord);
      };

    if (count == 3) {
      output.indices.push_back(resolveVertex(0));
      output.indices.push_back(resolveVertex(1));
      output.indices.push_back(resolveVertex(2));
    }
    else {
      const uint32_t v0 = resolveVertex(0);
      const uint32_t v1 = resolveVertex(1);
      const uint32_t v2 = resolveVertex(2);
      const uint32_t v3 = resolveVertex(3);
      output.indices.push_back(v0);
      output.indices.push_back(v1);
      output.indices.push_back(v2);
      output.indices.push_back(v0);
      output.indices.push_back(v2);
      output.indices.push_back(v3);
    }
    ++validFaceIndex;
  }
  if (invalidNormalIndex) return finish(CoinRenderFastPathResult::FALLBACK_CONTINUE);
  return output;
}

CoinRenderIndexedGeometryResult
CoinRenderIndexedGeometryCore::buildLines(
  const CoinRenderDirectGeometryView & view,
  const CoinRenderIndexedGeometryOptions & options)
{
  if (view.positions.empty() || view.coordIndex.empty()) {
    return finish(CoinRenderFastPathResult::SUCCESS_PRUNE);
  }
  if (options.hasTexture) {
    return finish(CoinRenderFastPathResult::FALLBACK_CONTINUE,
      "Textured lines require callback UV capture");
  }
  if (!positionsAreFinite(view)) {
    return finish(CoinRenderFastPathResult::INVALID_SCENE,
      "IndexedLineSet vertex position contains NaN or Inf");
  }

  std::vector<IndexSection> polylines;
  CoinRenderIndexedGeometryResult failure;
  if (!parseSections(
        view,
        "IndexedLineSet contains invalid negative coordinate index < -1",
        "IndexedLineSet coordinate index out of bounds",
        polylines, failure)) return failure;
  if (polylines.empty()) return finish(CoinRenderFastPathResult::SUCCESS_PRUNE);
  if (!materialIndicesAreValid(
        view,
        "IndexedLineSet contains invalid negative material index < -1",
        failure)) return failure;

  CoinRenderIndexedGeometryResult output;
  output.status = CoinRenderFastPathResult::SUCCESS_PRUNE;
  std::map<VertexKey, uint32_t> uniqueVertices;
  size_t validLineIndex = 0;
  size_t totalSegmentIndex = 0;
  size_t materialLineVertexCounter = 0;
  size_t normalLineVertexCounter = 0;
  size_t lineVertexBase = 0;
  bool invalidNormal = false;
  const bool normalsUsed = !options.baseColorLighting && !view.normals.empty();
  const bool independent =
      view.materialBinding == SoMaterialBindingElement::PER_PART ||
      view.materialBinding == SoMaterialBindingElement::PER_PART_INDEXED ||
      (normalsUsed && (view.normalBinding == SoNormalBindingElement::PER_PART ||
                       view.normalBinding == SoNormalBindingElement::PER_PART_INDEXED));

  for (size_t lineIndex = 0; lineIndex < polylines.size(); ++lineIndex) {
    const size_t count = polylines[lineIndex].count;
    if (count < 2) continue;
    const size_t start = polylines[lineIndex].startIndex;
    int32_t lineMaterialIndex = static_cast<int32_t>(validLineIndex);
    if (view.materialBinding == SoMaterialBindingElement::OVERALL) {
      lineMaterialIndex = 0;
    }
    else if (view.materialBinding ==
             SoMaterialBindingElement::PER_FACE_INDEXED) {
      if (!view.materialIndex.empty() &&
          validLineIndex < view.materialIndex.size) {
        const int32_t value = view.materialIndex[validLineIndex];
        if (value >= 0) lineMaterialIndex = value;
      }
    }

    for (size_t segment = 0; segment + 1 < count; ++segment) {
      int32_t segmentMaterialIndex = lineMaterialIndex;
      if (view.materialBinding == SoMaterialBindingElement::PER_PART) {
        segmentMaterialIndex = static_cast<int32_t>(totalSegmentIndex);
      }
      else if (view.materialBinding ==
               SoMaterialBindingElement::PER_PART_INDEXED) {
        segmentMaterialIndex = static_cast<int32_t>(totalSegmentIndex);
        if (!view.materialIndex.empty() &&
            totalSegmentIndex < view.materialIndex.size) {
          const int32_t value = view.materialIndex[totalSegmentIndex];
          if (value >= 0) segmentMaterialIndex = value;
        }
      }

      const auto resolveVertex =
        [&](size_t vertexOffsetInLine) -> uint32_t {
          const size_t indexPosition = start + vertexOffsetInLine;
          const int32_t coordinateIndex = view.coordIndex[indexPosition];
          int32_t materialIndex = segmentMaterialIndex;
          if (view.materialBinding == SoMaterialBindingElement::OVERALL) {
            materialIndex = 0;
          }
          else if (view.materialBinding == SoMaterialBindingElement::PER_VERTEX) {
            materialIndex = static_cast<int32_t>(independent ? materialLineVertexCounter++
                                                             : lineVertexBase + vertexOffsetInLine);
          }
          else if (view.materialBinding ==
                   SoMaterialBindingElement::PER_VERTEX_INDEXED) {
            materialIndex = coordinateIndex;
            if (!view.materialIndex.empty() &&
                indexPosition < view.materialIndex.size) {
              const int32_t value = view.materialIndex[indexPosition];
              if (value >= 0) materialIndex = value;
            }
          }
          SbVec3f normal(0, 0, 1);
          int32_t normalKey = 0;
          if (!view.normals.empty()) {
            size_t normalSlot = 0, indexSlot = 0;
            bool indexed = false;
            switch (view.normalBinding) {
            case SoNormalBindingElement::PER_PART:
              normalSlot = totalSegmentIndex;
              break;
            case SoNormalBindingElement::PER_PART_INDEXED:
              normalSlot = indexSlot = totalSegmentIndex;
              indexed = true;
              break;
            case SoNormalBindingElement::PER_FACE:
              normalSlot = validLineIndex;
              break;
            case SoNormalBindingElement::PER_FACE_INDEXED:
              normalSlot = indexSlot = validLineIndex;
              indexed = true;
              break;
            case SoNormalBindingElement::PER_VERTEX:
              normalSlot =
                  independent ? normalLineVertexCounter++ : lineVertexBase + vertexOffsetInLine;
              break;
            case SoNormalBindingElement::PER_VERTEX_INDEXED:
              normalSlot = coordinateIndex;
              indexSlot = indexPosition;
              indexed = true;
              break;
            default:
              break;
            }
            if (indexed && !view.normalIndex.empty()) {
              if (indexSlot >= view.normalIndex.size || view.normalIndex[indexSlot] < 0)
                invalidNormal = true;
              else
                normalSlot = static_cast<size_t>(view.normalIndex[indexSlot]);
            }
            if (normalSlot >= view.normals.size)
              invalidNormal = true;
            else {
              normal = view.normals[normalSlot];
              normalKey = static_cast<int32_t>(normalSlot);
            }
          }
          return getOrAddVertex(uniqueVertices, output, coordinateIndex, normalKey, 0,
                                materialIndex, view.positions[coordinateIndex], normal,
                                SbVec2f(0.0f, 0.0f));
        };

      output.indices.push_back(resolveVertex(segment));
      output.indices.push_back(resolveVertex(segment + 1));
      ++totalSegmentIndex;
    }
    ++validLineIndex;
    lineVertexBase += count;
  }
  if (invalidNormal)
    return finish(CoinRenderFastPathResult::FALLBACK_CONTINUE);
  return output;
}

uint64_t
CoinRenderIndexedGeometryCore::payloadDigest(
  const std::vector<CoinRenderIndexedVertex> & vertices,
  const std::vector<uint32_t> & indices)
{
  uint64_t hash = 14695981039346656037ULL;
  const auto hashBytes = [&hash](const void * data, size_t length) {
    const uint8_t * bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < length; ++i) {
      hash ^= static_cast<uint64_t>(bytes[i]);
      hash *= 1099511628211ULL;
    }
  };
  for (size_t i = 0; i < vertices.size(); ++i) {
    const CoinRenderVertexSnapshot & vertex = vertices[i].vertex;
    hashBytes(vertex.position, sizeof(vertex.position));
    hashBytes(vertex.normal, sizeof(vertex.normal));
    hashBytes(vertex.texcoord, sizeof(vertex.texcoord));
    hashBytes(&vertex.materialSlot, sizeof(vertex.materialSlot));
    hashBytes(vertex.extraTexcoords, sizeof(vertex.extraTexcoords));
    hashBytes(&vertex.screenSpaceW, sizeof(vertex.screenSpaceW));
    hashBytes(&vertex.fogEyeDepth, sizeof(vertex.fogEyeDepth));
    hashBytes(vertex.textureR, sizeof(vertex.textureR));
    hashBytes(vertex.textureQ, sizeof(vertex.textureQ));
  }
  if (!indices.empty()) {
    hashBytes(indices.data(), indices.size() * sizeof(uint32_t));
  }
  return hash == 0 ? 1 : hash;
}
