#include "rendering/coinrender/CoinRenderBumpMapCore.h"
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"

#include <Inventor/SbImage.h>
#include <Inventor/SbVec2s.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/details/SoFaceDetail.h>
#include <Inventor/details/SoPointDetail.h>
#include <Inventor/elements/SoBumpMapCoordinateElement.h>
#include <Inventor/engines/SoHeightMapToNormalMap.h>
#include <Inventor/nodes/SoBumpMap.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <limits>
#include <new>
#include <tuple>
#include <utility>

namespace {
template <typename CoordAt>
bool selectCoord(const CoinRenderBumpCoord & primitiveCoord,
                 size_t count, CoordAt coordAt,
                 bool hasPointDetail, int textureCoordIndex,
                 CoinRenderBumpCoord & output, std::string & diagnostic) {
  if (!std::isfinite(primitiveCoord.s) || !std::isfinite(primitiveCoord.t)) {
    diagnostic = "Nonfinite primitive bump coordinate";
    return false;
  }
  CoinRenderBumpCoord selected = primitiveCoord;
  if (hasPointDetail && count) {
    const size_t index = textureCoordIndex < 0 ? 0 :
      std::min(static_cast<size_t>(textureCoordIndex), count - 1);
    selected = coordAt(index);
  }
  if (!std::isfinite(selected.s) || !std::isfinite(selected.t)) {
    diagnostic = "Nonfinite authored bump coordinate";
    return false;
  }
  output = selected;
  diagnostic.clear();
  return true;
}
void normalize3(float * vector) {
  const float length = std::sqrt(vector[0]*vector[0] +
                                 vector[1]*vector[1] + vector[2]*vector[2]);
  if (length > 0.0f) {
    const float inverse = 1.0f / length;
    for (unsigned int axis = 0; axis < 3; ++axis) vector[axis] *= inverse;
  }
}
}

bool coin_render_prepare_bump_image(const std::vector<uint8_t> & source,
                                   uint32_t width, uint32_t height,
                                   uint32_t components, CoinRenderBumpImage & output,
                                   std::string & diagnostic)
{
  return coin_render_prepare_bump_image(source.data(), source.size(), width,
                                         height, components, output, diagnostic);
}

bool coin_render_prepare_bump_image(const uint8_t * source, size_t byteCount,
                                   uint32_t width, uint32_t height,
                                   uint32_t components, CoinRenderBumpImage & output,
                                   std::string & diagnostic)
{
  // SoHeightMapToNormalMap uses signed-int offsets and SbVec2s dimensions.
  // Keep both its intermediate and the final RGB image within a fixed budget.
  const uint64_t pixels = uint64_t(width) * height;
  if (!width || !height || width > 32767 || height > 32767 ||
      components < 1 || components > 4 || pixels > 16u * 1024u * 1024u ||
      pixels * components != byteCount || !source) {
    diagnostic = "Invalid or oversized SoBumpMap image";
    return false;
  }
  bool grayscale = components < 3;
  if (!grayscale) {
    grayscale = true;
    for (uint64_t pixel = 0; pixel < pixels; ++pixel) {
      const uint8_t * color = source + pixel * components;
      if (color[0] != color[1] || color[0] != color[2]) {
        grayscale = false;
        break;
      }
    }
  }
  try {
    CoinRenderBumpImage candidate;
    candidate.width = width;
    candidate.height = height;
    candidate.convertedHeight = grayscale;
    candidate.rgb.resize(static_cast<size_t>(pixels * 3u));
    if (grayscale) {
      SbImage converted;
      SoHeightMapToNormalMap::convert(source,
        SbVec2s(static_cast<short>(width), static_cast<short>(height)),
        static_cast<int>(components), converted);
      SbVec2s convertedSize;
      int convertedComponents = 0;
      const unsigned char * data = converted.getValue(convertedSize, convertedComponents);
      if (!data || convertedSize != SbVec2s(static_cast<short>(width), static_cast<short>(height)) ||
          convertedComponents != 3) {
        diagnostic = "SoBumpMap height conversion failed";
        return false;
      }
      candidate.rgb.assign(data, data + candidate.rgb.size());
    } else {
      for (uint64_t pixel = 0; pixel < pixels; ++pixel)
        for (uint32_t channel = 0; channel < 3; ++channel)
          candidate.rgb[static_cast<size_t>(pixel * 3u + channel)] =
            source[static_cast<size_t>(pixel * components + channel)];
    }
    output = std::move(candidate);
    diagnostic.clear();
    return true;
  } catch (const std::bad_alloc &) {
    diagnostic = "SoBumpMap image allocation failed";
    return false;
  }
}

bool coin_render_capture_bump_node(const SoBumpMap & node,
                                   CoinRenderBumpImage & output,
                                   std::string & diagnostic)
{
  SbVec2s size;
  int components = 0;
  const unsigned char * bytes = node.image.getValue(size, components);
  if (size == SbVec2s(0, 0)) {
    if (node.filename.getValue().getLength() != 0) {
      diagnostic = "SoBumpMap filename has no loaded image";
      return false;
    }
    output = CoinRenderBumpImage();
    diagnostic.clear();
    return true;
  }
  if (size[0] <= 0 || size[1] <= 0 || components <= 0) {
    diagnostic = "Invalid SoBumpMap image dimensions";
    return false;
  }
  CoinRenderBumpImage candidate;
  const size_t byteCount = size_t(size[0]) * size_t(size[1]) * size_t(components);
  if (!coin_render_prepare_bump_image(bytes, byteCount,
        static_cast<uint32_t>(size[0]), static_cast<uint32_t>(size[1]),
        static_cast<uint32_t>(components), candidate, diagnostic)) return false;
  candidate.clampS = node.wrapS.getValue() != SoBumpMap::REPEAT;
  candidate.clampT = node.wrapT.getValue() != SoBumpMap::REPEAT;
  output = std::move(candidate);
  diagnostic.clear();
  return true;
}

bool coin_render_legacy_bump_tangents(
  const std::vector<CoinRenderBumpVertex> & vertices,
  const std::vector<uint32_t> & triangleIndices,
  std::vector<CoinRenderBumpTangents> & output, std::string & diagnostic)
{
  if (triangleIndices.size() % 3 != 0 || vertices.size() > UINT32_MAX ||
      triangleIndices.size() > UINT32_MAX) {
    diagnostic = "Invalid bump triangle topology";
    return false;
  }
  for (const auto & vertex : vertices) {
    for (float coordinate : vertex.position) if (!std::isfinite(coordinate)) {
      diagnostic = "Nonfinite bump vertex";
      return false;
    }
    for (float coordinate : vertex.coord) if (!std::isfinite(coordinate)) {
      diagnostic = "Nonfinite bump coordinate";
      return false;
    }
  }
  for (uint32_t index : triangleIndices) if (index >= vertices.size()) {
    diagnostic = "Bump triangle index outside vertices";
    return false;
  }
  try {
    std::vector<CoinRenderBumpTangents> candidate(vertices.size());
    for (size_t index = 0; index < triangleIndices.size(); index += 3) {
      const auto & v0 = vertices[triangleIndices[index]];
      const auto & v1 = vertices[triangleIndices[index + 1]];
      const auto & v2 = vertices[triangleIndices[index + 2]];
      float side0[3], side1[3], s[3], t[3];
      const float deltaT0 = v1.coord[1] - v0.coord[1];
      const float deltaT1 = v2.coord[1] - v0.coord[1];
      const float deltaS0 = v1.coord[0] - v0.coord[0];
      const float deltaS1 = v2.coord[0] - v0.coord[0];
      for (unsigned int axis = 0; axis < 3; ++axis) {
        side0[axis] = v1.position[axis] - v0.position[axis];
        side1[axis] = v2.position[axis] - v0.position[axis];
        s[axis] = deltaT1 * side0[axis] - deltaT0 * side1[axis];
        t[axis] = deltaS1 * side0[axis] - deltaS0 * side1[axis];
      }
      normalize3(s);
      normalize3(t);
      for (unsigned int corner = 0; corner < 3; ++corner) {
        auto & target = candidate[triangleIndices[index + corner]];
        for (unsigned int axis = 0; axis < 3; ++axis) {
          target.s[axis] += s[axis];
          target.t[axis] += t[axis];
        }
      }
    }
    for (auto & tangent : candidate) {
      normalize3(tangent.s);
      normalize3(tangent.t);
      for (unsigned int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(tangent.s[axis]) || !std::isfinite(tangent.t[axis])) {
          diagnostic = "Nonfinite bump tangent";
          return false;
        }
    }
    output.swap(candidate);
    diagnostic.clear();
    return true;
  } catch (const std::bad_alloc &) {
    diagnostic = "Bump tangent allocation failed";
    return false;
  }
}

bool coin_render_legacy_bump_corner_tangents(
  const std::vector<CoinRenderBumpLegacyCorner> & corners,
  const std::vector<uint32_t> & triangleIndices,
  std::vector<CoinRenderBumpTangents> & output, std::string & diagnostic)
{
  if (corners.size() > UINT32_MAX || triangleIndices.size() % 3 != 0) {
    diagnostic = "Invalid bump corner topology";
    return false;
  }
  struct Key {
    uint32_t occurrence, rgba;
    int32_t textureIndex;
    std::array<float, 12> values;
    bool operator<(const Key & other) const {
      return std::tie(occurrence, rgba, textureIndex, values) <
             std::tie(other.occurrence, other.rgba, other.textureIndex, other.values);
    }
  };
  try {
    std::map<Key, uint32_t> interned;
    std::vector<CoinRenderBumpVertex> unique;
    std::vector<uint32_t> remap;
    remap.reserve(corners.size());
    for (const auto & corner : corners) {
      Key key{corner.shapeOccurrence, corner.rgba, corner.textureCoordIndex, {}};
      size_t i = 0;
      for (float value : corner.position) key.values[i++] = value;
      for (float value : corner.normal) key.values[i++] = value;
      for (float value : corner.textureCoord0) key.values[i++] = value;
      for (float value : corner.bumpCoord) key.values[i++] = value;
      for (float value : key.values) if (!std::isfinite(value)) {
        diagnostic = "Nonfinite bump legacy corner";
        return false;
      }
      auto found = interned.find(key);
      if (found == interned.end()) {
        const uint32_t slot = static_cast<uint32_t>(unique.size());
        CoinRenderBumpVertex vertex;
        for (int axis = 0; axis < 3; ++axis) vertex.position[axis] = corner.position[axis];
        for (int axis = 0; axis < 2; ++axis) vertex.coord[axis] = corner.bumpCoord[axis];
        unique.push_back(vertex);
        found = interned.emplace(key, slot).first;
      }
      remap.push_back(found->second);
    }
    std::vector<uint32_t> canonicalIndices;
    canonicalIndices.reserve(triangleIndices.size());
    for (uint32_t index : triangleIndices) {
      if (index >= remap.size()) {
        diagnostic = "Bump corner index outside vertices";
        return false;
      }
      canonicalIndices.push_back(remap[index]);
    }
    std::vector<CoinRenderBumpTangents> canonical;
    if (!coin_render_legacy_bump_tangents(unique, canonicalIndices, canonical, diagnostic))
      return false;
    std::vector<CoinRenderBumpTangents> expanded;
    expanded.reserve(corners.size());
    for (uint32_t index : remap) expanded.push_back(canonical[index]);
    output.swap(expanded);
    diagnostic.clear();
    return true;
  } catch (const std::bad_alloc &) {
    diagnostic = "Bump corner tangent allocation failed";
    return false;
  }
}

bool coin_render_select_bump_coord(
  const CoinRenderBumpCoord & primitiveCoord,
  const std::vector<CoinRenderBumpCoord> & authoredCoords,
  bool hasPointDetail, int textureCoordIndex, CoinRenderBumpCoord & output,
  std::string & diagnostic)
{
  return selectCoord(primitiveCoord, authoredCoords.size(),
                     [&](size_t index) { return authoredCoords[index]; },
                     hasPointDetail, textureCoordIndex, output, diagnostic);
}

bool coin_render_capture_bump_coord(SoCallbackAction * action,
                                    const SoPrimitiveVertex * vertex,
                                    CoinRenderBumpCoord & output,
                                    std::string & diagnostic)
{
  if (!action || !vertex) {
    diagnostic = "Missing bump primitive callback";
    return false;
  }
  const SbVec4f & uv = vertex->getTextureCoords();
  const CoinRenderBumpCoord primitive(uv[0], uv[1]);
  const SoDetail * detail = vertex->getDetail();
  bool hasPointDetail = false;
  int textureCoordIndex = -1;
  if (detail && detail->isOfType(SoPointDetail::getClassTypeId())) {
    hasPointDetail = true;
    textureCoordIndex = static_cast<const SoPointDetail *>(detail)->getTextureCoordIndex();
  } else if (detail && detail->isOfType(SoFaceDetail::getClassTypeId())) {
    const auto * face = static_cast<const SoFaceDetail *>(detail);
    const int corner = action->getCurrentTrianglePointDetailIndex(vertex);
    if (corner >= 0 && corner < face->getNumPoints()) {
      hasPointDetail = true;
      textureCoordIndex = face->getPoint(corner)->getTextureCoordIndex();
    }
  }
  const auto * element = SoBumpMapCoordinateElement::getInstance(action->getState());
  const SbVec2f * authored = element->getArrayPtr();
  return selectCoord(primitive, static_cast<size_t>(element->getNum()),
                     [&](size_t index) { return CoinRenderBumpCoord(authored[index][0], authored[index][1]); },
                     hasPointDetail,
                     textureCoordIndex, output, diagnostic);
}

bool coin_render_legacy_bump_light_vector(
  const CoinRenderBumpTangents & tangents, const float normal[3],
  const float position[3], const float light[3], bool pointLight,
  float output[3])
{
  if (!normal || !position || !light || !output) return false;
  for (unsigned int axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(tangents.s[axis]) || !std::isfinite(tangents.t[axis]) ||
        !std::isfinite(normal[axis]) || !std::isfinite(position[axis]) ||
        !std::isfinite(light[axis])) return false;
  }
  float direction[3] = {light[0], light[1], light[2]};
  if (pointLight) {
    for (unsigned int axis = 0; axis < 3; ++axis)
      direction[axis] -= position[axis];
  }
  normalize3(direction);
  float projected[3] = {};
  for (unsigned int axis = 0; axis < 3; ++axis) {
    projected[0] += tangents.s[axis] * direction[axis];
    projected[1] += tangents.t[axis] * direction[axis];
    projected[2] += normal[axis] * direction[axis];
  }
  for (float component : projected) if (!std::isfinite(component)) return false;
  for (unsigned int axis = 0; axis < 3; ++axis) output[axis] = projected[axis];
  return true;
}

bool coin_render_legacy_bump_eye_vector(
  const CoinRenderBumpTangents & tangents, const float normal[3],
  const float position[3], const float eye[3], float output[3])
{
  if (!normal || !position || !eye || !output) return false;
  for (unsigned int axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(tangents.s[axis]) || !std::isfinite(tangents.t[axis]) ||
        !std::isfinite(normal[axis]) || !std::isfinite(position[axis]) ||
        !std::isfinite(eye[axis])) return false;
  }
  float direction[3];
  for (unsigned int axis = 0; axis < 3; ++axis)
    direction[axis] = eye[axis] - position[axis];
  normalize3(direction);
  float projected[3] = {};
  for (unsigned int axis = 0; axis < 3; ++axis) {
    projected[0] += tangents.s[axis] * direction[axis];
    projected[1] += tangents.t[axis] * direction[axis];
    projected[2] += normal[axis] * direction[axis];
  }
  for (float component : projected) if (!std::isfinite(component)) return false;
  for (unsigned int axis = 0; axis < 3; ++axis) output[axis] = projected[axis];
  return true;
}

float coin_render_legacy_bump_dot3(const uint8_t normalRgb[3],
                                   const float interpolatedLight[3])
{
  if (!normalRgb || !interpolatedLight) return 0.0f;
  float light[3];
  for (unsigned int axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(interpolatedLight[axis])) return 0.0f;
    light[axis] = interpolatedLight[axis];
  }
  normalize3(light);
  float value = 0.0f;
  for (unsigned int axis = 0; axis < 3; ++axis)
    value += (2.0f * (float(normalRgb[axis]) / 255.0f) - 1.0f) * light[axis];
  return std::max(0.0f, std::min(1.0f, value));
}

float coin_render_legacy_bump_specular(const uint8_t normalRgb[3],
                                      const float interpolatedLight[3],
                                      const float interpolatedEye[3],
                                      float shininess)
{
  if (!normalRgb || !interpolatedLight || !interpolatedEye ||
      !std::isfinite(shininess) || shininess < 0.0f || shininess > 1.0f)
    return 0.0f;
  float halfVector[3];
  for (unsigned int axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(interpolatedLight[axis]) ||
        !std::isfinite(interpolatedEye[axis])) return 0.0f;
    halfVector[axis] = interpolatedLight[axis] + interpolatedEye[axis];
  }
  normalize3(halfVector);
  float value = 0.0f;
  for (unsigned int axis = 0; axis < 3; ++axis)
    value += (2.0f * (float(normalRgb[axis]) / 255.0f) - 1.0f) * halfVector[axis];
  return std::pow(std::max(0.0f, std::min(1.0f, value)), shininess * 64.0f);
}

bool coin_render_build_bump_gpu_stream(
  const CoinRenderFramePlan & frame, std::vector<CoinRenderBumpGpuVertex> & output,
  std::string & diagnostic)
{
  // The inactive stream is still untrusted data. Validate every declaration
  // before deciding that no draw consumes it; failures leave output intact.
  uint64_t imageBytes = 0;
  if (frame.bumpImages.size() > 4096) {
    diagnostic = "Too many bump images";
    return false;
  }
  for (const auto & image : frame.bumpImages) {
    imageBytes += image.rgb.size();
    if (!image.width || !image.height || image.width > 32767 || image.height > 32767 ||
        uint64_t(image.width) * image.height * 3u != image.rgb.size() ||
        image.rgb.size() > 48u * 1024u * 1024u || imageBytes > 256u * 1024u * 1024u) {
      diagnostic = "Invalid bump image snapshot";
      return false;
    }
  }
  for (const auto & matrix : frame.bumpTransforms)
    if (!CoinRenderTransformCore::finiteMatrix(matrix)) {
      diagnostic = "Nonfinite bump transform";
      return false;
    }
  uint32_t lastCoord = 0;
  bool firstCoord = true;
  for (const auto & coord : frame.bumpCoords) {
    if (coord.vertexIndex >= frame.vertices.size() || (!firstCoord && coord.vertexIndex <= lastCoord) ||
        coord.imageSlot > frame.bumpImages.size() || coord.transformSlot > frame.bumpTransforms.size() ||
        (coord.transformSlot && !coord.imageSlot) ||
        !std::isfinite(coord.coord[0]) || !std::isfinite(coord.coord[1])) {
      diagnostic = "Invalid sparse bump coordinates";
      return false;
    }
    firstCoord = false;
    lastCoord = coord.vertexIndex;
  }
  size_t coordCursor = 0;
  uint32_t lastTangent = 0;
  bool firstTangent = true;
  for (const auto & tangent : frame.bumpTangents) {
    while (coordCursor < frame.bumpCoords.size() &&
           frame.bumpCoords[coordCursor].vertexIndex < tangent.vertexIndex) ++coordCursor;
    if ((!firstTangent && tangent.vertexIndex <= lastTangent) ||
        coordCursor >= frame.bumpCoords.size() ||
        frame.bumpCoords[coordCursor].vertexIndex != tangent.vertexIndex ||
        !frame.bumpCoords[coordCursor].imageSlot) {
      diagnostic = "Invalid sparse bump tangent source";
      return false;
    }
    for (unsigned int axis = 0; axis < 3; ++axis)
      if (!std::isfinite(tangent.tangent.s[axis]) || !std::isfinite(tangent.tangent.t[axis])) {
        diagnostic = "Nonfinite bump tangent";
        return false;
      }
    firstTangent = false;
    lastTangent = tangent.vertexIndex;
  }
  const bool active = std::any_of(frame.bumpCoords.begin(), frame.bumpCoords.end(),
    [](const CoinRenderBumpCoordOverride & coord) { return coord.imageSlot != 0; });
  if (!active) {
    output.clear();
    diagnostic.clear();
    return true;
  }
  try {
    std::vector<uint8_t> surfaceVertices;
    const bool hasMapOnlyDraws =
        std::any_of(frame.draws.begin(), frame.draws.end(),
                    [](const CoinRenderDrawPacket &draw) { return draw.shadowLightSlot != 0; });
    if (hasMapOnlyDraws) {
      surfaceVertices.assign(frame.vertices.size(), 0);
      for (const auto &draw : frame.draws) {
        if (draw.shadowLightSlot)
          continue;
        const size_t first = draw.geometry.firstIndex;
        if (first > frame.indices.size() ||
            draw.geometry.indexCount > frame.indices.size() - first) {
          diagnostic = "Bump surface index range is out of bounds";
          return false;
        }
        for (size_t i = first; i < first + draw.geometry.indexCount; ++i) {
          if (frame.indices[i] >= surfaceVertices.size()) {
            diagnostic = "Bump surface vertex is out of bounds";
            return false;
          }
          surfaceVertices[frame.indices[i]] = 1;
        }
      }
    }
    std::vector<CoinRenderBumpGpuVertex> stream(frame.vertices.size());
    size_t tangentCursor = 0;
    uint32_t previousIndex = 0;
    bool first = true;
    for (const auto & coord : frame.bumpCoords) {
      if (coord.vertexIndex >= frame.vertices.size() ||
          (!first && coord.vertexIndex <= previousIndex) ||
          coord.imageSlot > frame.bumpImages.size() ||
          coord.transformSlot > frame.bumpTransforms.size()) {
        diagnostic = "Invalid bump GPU stream slot or vertex order";
        return false;
      }
      first = false;
      previousIndex = coord.vertexIndex;
      // Map-only occurrences use geometric shadow execution, not bump sampling.
      // A shared vertex used by any output draw remains a live bump vertex.
      if (!coord.imageSlot || (!surfaceVertices.empty() && !surfaceVertices[coord.vertexIndex]))
        continue;
      while (tangentCursor < frame.bumpTangents.size() &&
             frame.bumpTangents[tangentCursor].vertexIndex < coord.vertexIndex)
        ++tangentCursor;
      if (tangentCursor == frame.bumpTangents.size() ||
          frame.bumpTangents[tangentCursor].vertexIndex != coord.vertexIndex) {
        diagnostic = "Missing tangent for active bump vertex";
        return false;
      }
      const auto & tangent = frame.bumpTangents[tangentCursor].tangent;
      SbVec4f projected(coord.coord[0], coord.coord[1], 0.0f, 1.0f);
      if (coord.transformSlot)
        frame.bumpTransforms[coord.transformSlot - 1].multVecMatrix(projected, projected);
      CoinRenderBumpGpuVertex & vertex = stream[coord.vertexIndex];
      for (unsigned int axis = 0; axis < 4; ++axis) {
        if (!std::isfinite(projected[axis])) {
          diagnostic = "Nonfinite transformed bump coordinate";
          return false;
        }
        vertex.coord[axis] = projected[axis];
      }
      for (unsigned int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(tangent.s[axis]) || !std::isfinite(tangent.t[axis])) {
          diagnostic = "Nonfinite bump GPU tangent";
          return false;
        }
        vertex.tangentS[axis] = tangent.s[axis];
        vertex.tangentT[axis] = tangent.t[axis];
      }
      vertex.imageSlot = coord.imageSlot;
    }
    if (hasMapOnlyDraws &&
        std::none_of(stream.begin(), stream.end(),
                     [](const CoinRenderBumpGpuVertex &vertex) { return vertex.imageSlot != 0; }))
      stream.clear();
    output.swap(stream);
    diagnostic.clear();
    return true;
  } catch (const std::bad_alloc &) {
    diagnostic = "Bump GPU stream allocation failed";
    return false;
  }
}

bool coin_render_remap_bump_gpu_stream(
  const std::vector<CoinRenderBumpGpuVertex> & source,
  const std::vector<uint32_t> & sourceVertexForEmitted,
  std::vector<CoinRenderBumpGpuVertex> & output, std::string & diagnostic)
{
  for (uint32_t index : sourceVertexForEmitted) {
    if (index >= source.size()) {
      diagnostic = "Bump emitted vertex has no source vertex";
      return false;
    }
  }
  try {
    std::vector<CoinRenderBumpGpuVertex> remapped;
    remapped.reserve(sourceVertexForEmitted.size());
    for (uint32_t index : sourceVertexForEmitted)
      remapped.push_back(source[index]);
    output.swap(remapped);
    diagnostic.clear();
    return true;
  } catch (const std::bad_alloc &) {
    diagnostic = "Bump remapped stream allocation failed";
    return false;
  }
}

bool coin_render_bump_draw_slot(const CoinRenderFramePlan &frame,
  const std::vector<CoinRenderBumpGpuVertex> &stream, const CoinRenderDrawPacket &draw,
  uint32_t &slot, std::string &diagnostic)
{
  const size_t first = draw.geometry.firstIndex;
  if (first > frame.indices.size() || draw.geometry.indexCount > frame.indices.size() - first ||
      (!stream.empty() && stream.size() != frame.vertices.size())) {
    diagnostic = "Bump occurrence index range or stream extent is invalid";
    return false;
  }
  uint32_t selected = 0;
  for (size_t i = 0; i < draw.geometry.indexCount; ++i) {
    const auto index = frame.indices[first + i];
    if (index >= frame.vertices.size()) {
      diagnostic = "Bump occurrence vertex index is invalid";
      return false;
    }
    const uint32_t current = stream.empty() ? 0 : stream[index].imageSlot;
    if (current > frame.bumpImages.size()) {
      diagnostic = "Bump occurrence has an invalid image slot";
      return false;
    }
    if (current) selected = current;
  }
  slot = selected;
  return true;
}

bool coin_render_uses_extended_bump(const CoinRenderFramePlan &frame)
{
  // Preserve the ordinary-frame fast path. Common validation still owns
  // inactive payloads; no bump stream or indexed occurrence scan is needed.
  if (std::none_of(frame.bumpCoords.begin(), frame.bumpCoords.end(),
                  [](const CoinRenderBumpCoordOverride &corner) { return corner.imageSlot != 0; }))
    return false;

  std::vector<CoinRenderBumpGpuVertex> stream;
  std::string diagnostic;
  if (!coin_render_build_bump_gpu_stream(frame, stream, diagnostic)) return true;
  if (stream.empty()) return false;
  bool consumed = false;
  for (const auto &draw : frame.draws) {
    if (draw.shadowLightSlot) continue;
    uint32_t slot = 0;
    if (!coin_render_bump_draw_slot(frame, stream, draw, slot, diagnostic)) return true;
    consumed = consumed || slot != 0;
  }
  if (!consumed) return false;
  if (!frame.shadowGroups.empty())
    return true;
  for (const auto &draw : frame.draws) {
    if (draw.shadowLightSlot)
      continue;
    if (draw.renderStateSlot >= frame.renderStates.size())
      continue;
    uint32_t slot = 0;
    if (!coin_render_bump_draw_slot(frame, stream, draw, slot, diagnostic)) return true;
    const auto &state = frame.renderStates[draw.renderStateSlot];
    if (slot && state.lightingSlot < frame.lightingStates.size() &&
        frame.lightingStates[state.lightingSlot].lights.size() > 1)
      return true;
    if (std::any_of(std::begin(state.extraTextures), std::end(state.extraTextures),
                    [](const CoinRenderTextureUnitSnapshot &unit) { return unit.enabled; }))
      return true;
    if (!state.hasTexture || state.textureImageSlot >= frame.textures.size())
      continue;
    const auto &image = frame.textures[state.textureImageSlot];
    if (image.volumeDepth || image.cubeFaceSize || image.gpuToken || image.producerId ||
        (image.format != CoinRenderTextureFormat::RGBA8_LINEAR &&
         image.format != CoinRenderTextureFormat::RGBA8_SRGB) ||
        state.textureModel != CoinRenderTextureModel::MODULATE ||
        state.textureProjection != CoinRenderTextureProjection::PROJECTIVE ||
        state.textureCombines[0].instructions[0][0] != 0)
      return true;
  }
  return false;
}

// This predicate runs before graph execution, including every captured producer.
// It admits only the actual intersection, without changing ordinary shadow or
// the original point/directional, single-base bump validators.
bool coin_render_validate_extended_bump(const CoinRenderFramePlan &frame, std::string &diagnostic)
{
  if (frame.bumpImages.empty() && frame.bumpCoords.empty() &&
      frame.bumpTransforms.empty() && frame.bumpTangents.empty()) return true;
  // Validate current payloads, including inactive images, before any shortcut.
  if (!frame.isValid(&diagnostic)) return false;
  // Preserve the ordinary-frame fast path. Common validation still owns
  // inactive payloads; no bump stream or indexed occurrence scan is needed.
  if (std::none_of(frame.bumpCoords.begin(), frame.bumpCoords.end(),
                  [](const CoinRenderBumpCoordOverride &corner) { return corner.imageSlot != 0; }))
    return true;

  const auto fail = [&](const char *message) { diagnostic = message; return false; };
  std::vector<CoinRenderBumpGpuVertex> stream;
  if (!coin_render_build_bump_gpu_stream(frame, stream, diagnostic)) return false;
  if (stream.empty()) return true;
  const bool extended = coin_render_uses_extended_bump(frame);
  for (const auto &draw : frame.draws) {
    if (draw.shadowLightSlot) continue;
    uint32_t slot = 0;
    if (!coin_render_bump_draw_slot(frame, stream, draw, slot, diagnostic)) return false;
    if (!slot) continue;
    if (draw.renderStateSlot >= frame.renderStates.size()) return fail("Invalid bump state slot");
    const auto &state = frame.renderStates[draw.renderStateSlot];
    if (state.lightingSlot >= frame.lightingStates.size()) return fail("Invalid bump lighting slot");
    const auto &lights = frame.lightingStates[state.lightingSlot].lights;
    if (state.lightModel != CoinRenderLightModel::PHONG || lights.empty() ||
        lights.size() > (extended ? 8u : 1u) ||
        std::any_of(lights.begin(), lights.end(), [&](const CoinRenderLightSourceSnapshot &light) {
          return light.type != CoinRenderLightType::DIRECTIONAL &&
            (!frame.shadowGroups.empty() ||
             (light.type != CoinRenderLightType::POINT && light.type != CoinRenderLightType::SPOT));
        })) return fail("Consumed bump requires PHONG, one to eight legacy lights, and directional-only shadows");
  }
  if (!extended) return true;
  if (frame.rasterizationPolicy != COIN_RENDER_RASTER_NATIVE ||
      frame.outputColorFormat != CoinRenderTextureFormat::RGBA8_LINEAR)
    return fail("Extended bump requires native RGBA8 output");
  size_t mapCount=0;
  uint32_t mapGroup=0;
  for (const auto &light : frame.shadowLights) {
    if (!light.enabled || !light.shadowEligible) continue;
    if (light.type != CoinRenderLightType::DIRECTIONAL || ++mapCount>8 ||
        (mapGroup && mapGroup!=light.groupSlot))
      return fail("Bump shadows require up to eight directional maps and one active group");
    mapGroup=light.groupSlot;
  }
  for (const auto &draw : frame.draws) {
    // ShadowCore owns map-only draw admission (including unlit custom scenes).
    // Backends validate that full shadow profile before encoding any caster.
    if (draw.shadowLightSlot)
      continue;
    uint32_t drawBumpSlot = 0;
    if (!coin_render_bump_draw_slot(frame, stream, draw, drawBumpSlot, diagnostic)) return false;
    const auto &state = frame.renderStates[draw.renderStateSlot];
    const auto &material = frame.materials[state.materialSlot];
    const auto &lights = frame.lightingStates[state.lightingSlot].lights;
    if (drawBumpSlot && state.shadowGroupSlot == mapGroup && mapCount > lights.size())
      return fail("Bump map count exceeds its directional lighting state");
    if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST || !draw.geometry.indexCount ||
        draw.geometry.indexCount % 3 || draw.renderLayer || draw.clearDepthBefore ||
        state.rasterPixels ||
        state.rasterTextureSourceSlot != UINT32_MAX || state.fogMode != CoinRenderFogMode::NONE ||
        !state.clipPlanesWorld.empty() || !state.depthTest || !state.depthWrite ||
        state.cullMode != CoinRenderCullMode::NONE ||
        (state.depthFunction != CoinRenderDepthFunction::LESS &&
         state.depthFunction != CoinRenderDepthFunction::LEQUAL) ||
        state.depthRange[0] != 0 || state.depthRange[1] != 1 || state.polygonOffsetEnabled ||
        state.alphaTestFunction != CoinRenderAlphaTestFunction::NONE ||
        state.screenDoorTransparency || state.transparentMaterial || state.transparentTexture ||
        material.transparency != 0 || material.diffuse[3] != 1)
      return fail(
          "Extended bump requires opaque one-to-eight legacy-light triangles and default depth");
    if (!std::isfinite(material.shininess) || material.shininess < 0 || material.shininess > 1)
      return fail("Invalid extended bump shininess");
    for (const auto &light : lights)
      for (int channel = 0; channel < 3; ++channel)
        if (!std::isfinite(material.diffuse[channel] * light.color[channel] * light.intensity) ||
            !std::isfinite(material.specular[channel]))
          return fail("Invalid extended bump diffuse/specular product");
    const SbMatrix modelView = state.model * state.view;
    if (modelView[0][3] != 0 || modelView[1][3] != 0 || modelView[2][3] != 0 ||
        modelView[3][3] != 1)
      return fail("Extended bump requires affine model-view");
    const auto inverseDirection = CoinRenderTransformCore::normalMatrix(modelView).transpose();
    SbVec3f light, eye;
    inverseDirection.multDirMatrix(-SbVec3f(modelView[3][0], modelView[3][1], modelView[3][2]),
                                   eye);
    uint32_t slot = UINT32_MAX;
    for (size_t i = draw.geometry.firstIndex;
         i < size_t(draw.geometry.firstIndex) + draw.geometry.indexCount; ++i) {
      const auto index = frame.indices[i];
      const auto &corner = stream[index];
      if (slot == UINT32_MAX)
        slot = corner.imageSlot;
      if (slot != corner.imageSlot)
        return fail("Extended bump draw mixes image slots");
      if (!slot)
        continue;
      const auto &vertex = frame.vertices[index];
      if (lights.size()>1) {
        const auto &cornerMaterial=frame.materials[vertex.materialSlot];
        if (cornerMaterial.transparency!=0 || cornerMaterial.diffuse[3]!=1 ||
            !std::isfinite(cornerMaterial.shininess) || cornerMaterial.shininess<0 ||
            cornerMaterial.shininess>1)
          return fail("Invalid multilight bump corner material");
        for (const auto &sourceLight : lights)
          for (int channel=0;channel<3;++channel)
            if (!std::isfinite(cornerMaterial.diffuse[channel]*sourceLight.color[channel]*sourceLight.intensity) ||
                !std::isfinite(cornerMaterial.specular[channel]))
              return fail("Invalid multilight bump corner light product");
      }
      CoinRenderBumpTangents tangents;
      std::copy_n(corner.tangentS, 3, tangents.s);
      std::copy_n(corner.tangentT, 3, tangents.t);
      float l[3], e[3];
      for (const auto &sourceLight : lights) {
        const bool positional = sourceLight.type != CoinRenderLightType::DIRECTIONAL;
        inverseDirection.multDirMatrix(positional ? SbVec3f(sourceLight.position) -
          SbVec3f(modelView[3][0], modelView[3][1], modelView[3][2]) :
          -SbVec3f(sourceLight.direction), light);
        if (!coin_render_legacy_bump_light_vector(tangents, vertex.normal, vertex.position,
                                                  light.getValue(), positional, l))
          return fail("Extended bump light exceeds finite precision");
      }
      if (!coin_render_legacy_bump_eye_vector(tangents, vertex.normal, vertex.position,
                                              eye.getValue(), e))
        return fail("Extended bump eye exceeds finite precision");
      if (std::abs(corner.coord[3]) < std::numeric_limits<float>::min() ||
          !std::isfinite(corner.coord[0] / corner.coord[3]) ||
          !std::isfinite(corner.coord[1] / corner.coord[3]))
        return fail("Invalid extended bump projected coordinate");
      const size_t first = i - (i - draw.geometry.firstIndex) % 3;
      if (std::signbit(corner.coord[3]) != std::signbit(stream[frame.indices[first]].coord[3]))
        return fail("Extended bump Q changes sign inside a primitive");
    }
    for (size_t i = draw.geometry.firstIndex;
         i < size_t(draw.geometry.firstIndex) + draw.geometry.indexCount; i += 3)
      if (!coin_render_validate_texture_primitive(frame, state, frame.indices.data() + i, 3,
                                                  diagnostic))
        return false;
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      const auto layer = coin_render_texture_unit(state, unit);
      if (!layer.enabled)
        continue;
      if (layer.imageSlot >= frame.textures.size() || layer.samplerSlot >= frame.samplers.size())
        return fail("Extended bump texture or sampler slot is invalid");
      if (drawBumpSlot && frame.textures[layer.imageSlot].format != CoinRenderTextureFormat::RGBA8_LINEAR &&
          frame.textures[layer.imageSlot].format != CoinRenderTextureFormat::RGBA8_SRGB)
        return fail("Bump receiver bases require RGBA8 linear/sRGB sources");
      if (coin_render_texture_has_transparency(frame.textures[layer.imageSlot]))
        return fail("Extended bump textures must be opaque");
    }
  }
  return true;
}
