#ifndef COIN_RENDER_STROKE_CORE_H
#define COIN_RENDER_STROKE_CORE_H
#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"
#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"

#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderLineStippleCore.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstring>

// Mechanical expansion of captured strokes, shared by every GPU backend.
inline bool coin_render_expand_strokes(CoinRenderFramePlan& plan, std::string& diagnostic) {
  if (!coin_render_validate_stroke_texture_coordinates(plan, diagnostic)) return false;
  diagnostic.clear();
  const size_t originalDrawCount = plan.draws.size();
  std::vector<CoinRenderDrawPacket> expandedDraws;
  expandedDraws.reserve(originalDrawCount);

  auto fail = [&](const char* message) {
    diagnostic = message;
    return false;
  };

  auto materialAt = [&](uint32_t firstSlot, uint32_t secondSlot, float t) -> uint32_t {
    if (t <= 0.0f || firstSlot == secondSlot)
      return firstSlot;
    if (t >= 1.0f)
      return secondSlot;
    const CoinRenderMaterialSnapshot first = plan.materials[firstSlot];
    const CoinRenderMaterialSnapshot second = plan.materials[secondSlot];
    CoinRenderMaterialSnapshot material;
    for (int channel = 0; channel < 4; ++channel) {
      material.ambient[channel] =
          first.ambient[channel] + (second.ambient[channel] - first.ambient[channel]) * t;
      material.diffuse[channel] =
          first.diffuse[channel] + (second.diffuse[channel] - first.diffuse[channel]) * t;
      material.specular[channel] =
          first.specular[channel] + (second.specular[channel] - first.specular[channel]) * t;
      material.emission[channel] =
          first.emission[channel] + (second.emission[channel] - first.emission[channel]) * t;
    }
    material.shininess = first.shininess + (second.shininess - first.shininess) * t;
    material.transparency = first.transparency + (second.transparency - first.transparency) * t;
    for (size_t i = 0; i < plan.materials.size(); ++i) {
      if (std::memcmp(&plan.materials[i], &material, sizeof(CoinRenderMaterialSnapshot)) == 0) {
        return static_cast<uint32_t>(i);
      }
    }
    const uint32_t slot = static_cast<uint32_t>(plan.materials.size());
    plan.materials.push_back(material);
    return slot;
  };

  auto appendVertex = [&](float x, float y, float z, uint32_t materialSlot,
                          const CoinRenderVertexSnapshot& attributes) -> uint32_t {
    CoinRenderVertexSnapshot vertex = attributes;
    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.position[2] = z;
    vertex.normal[2] = 1.0f;
    vertex.materialSlot = materialSlot;
    const uint32_t index = static_cast<uint32_t>(plan.vertices.size());
    plan.vertices.push_back(vertex);
    return index;
  };

  std::unordered_map<uint64_t, uint32_t> stripPhases;
  for (size_t drawIndex = 0; drawIndex < originalDrawCount; ++drawIndex) {
    const CoinRenderDrawPacket original = plan.draws[drawIndex];
    if (original.topology == CoinRenderPrimitiveTopology::TRIANGLE_LIST) {
      expandedDraws.push_back(original);
      continue;
    }
    if (original.renderStateSlot >= plan.renderStates.size()) {
      return fail("Styled primitive references invalid render state");
    }
    const CoinRenderRenderStateSnapshot sourceState = plan.renderStates[original.renderStateSlot];
    auto shadeMaterial = [&](const CoinRenderVertexSnapshot &vertex) {
      if (sourceState.lightModel == CoinRenderLightModel::BASE_COLOR)
        return vertex.materialSlot;
      return CoinRenderPlanAssemblyCore::material(
          plan,
          coin_render_bake_vertex_material(vertex, plan.materials[vertex.materialSlot], sourceState,
                                           plan.lightingStates[sourceState.lightingSlot]));
    };
    if (sourceState.viewportSlot >= plan.viewports.size()) {
      return fail("Styled primitive references invalid viewport");
    }
    const CoinRenderViewportSnapshot viewport = plan.viewports[sourceState.viewportSlot];
    if (viewport.width <= 0 || viewport.height <= 0) {
      return fail("Styled primitive has an empty viewport");
    }
    if (original.geometry.firstIndex > plan.indices.size() ||
        original.geometry.indexCount > plan.indices.size() - original.geometry.firstIndex) {
      return fail("Styled primitive index range is invalid");
    }

    CoinRenderRenderStateSnapshot state = sourceState;
    state.polygonOffsetPrimitiveStyle =
        original.topology == CoinRenderPrimitiveTopology::LINE_LIST ? 2u : 4u;
    state.clipPlanesWorld.clear(); // Original strokes are clipped before expansion.
    state.model = SbMatrix::identity();
    state.view = SbMatrix::identity();
    state.projectionCoin = SbMatrix::identity();
    state.cullMode = CoinRenderCullMode::NONE;
    state.lightModel = CoinRenderLightModel::BASE_COLOR;
    state.lineWidth = 1.0f;
    state.pointSize = 1.0f;
    state.linePattern = 0xffffu;
    state.linePatternScaleFactor = 1;
    state.polygonLinePattern = false;
    const uint32_t stateSlot = static_cast<uint32_t>(plan.renderStates.size());
    plan.renderStates.push_back(state);

    CoinRenderDrawPacket expanded = original;
    // Stable draws receive a digest of the resolved payload below. The raw
    // indexed digest cannot identify expanded positions, fog or clipped UVs.
    expanded.topology = CoinRenderPrimitiveTopology::TRIANGLE_LIST;
    expanded.renderStateSlot = stateSlot;
    expanded.geometry.firstVertex = static_cast<uint32_t>(plan.vertices.size());
    expanded.geometry.vertexCount = 0;
    expanded.geometry.firstIndex = static_cast<uint32_t>(plan.indices.size());
    expanded.geometry.indexCount = 0;
    expanded.lineStripId = 0;

    const SbMatrix mvp = sourceState.model * sourceState.view * sourceState.projectionCoin;
    auto project = [&](const CoinRenderVertexSnapshot& vertex, SbVec3f& ndc, float& clipW) {
      SbVec4f clip;
      mvp.multVecMatrix(SbVec4f(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f),
                        clip);
      if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(clip[2]) ||
          !std::isfinite(clip[3]) || clip[3] <= 1.0e-6f)
        return false;
      clipW = clip[3];
      ndc.setValue(clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]);
      return std::isfinite(ndc[0]) && std::isfinite(ndc[1]) && std::isfinite(ndc[2]);
    };

    const SbMatrix modelView = sourceState.model * sourceState.view;
    auto eyeDepth = [&](const CoinRenderVertexSnapshot& vertex) {
      SbVec3f view;
      modelView.multVecMatrix(SbVec3f(vertex.position), view);
      return std::max(0.0f, -view[2]);
    };
    auto attributesAt = [&](const CoinRenderVertexSnapshot& a, const CoinRenderVertexSnapshot& b,
                            float wa, float wb, float screenT) {
      CoinRenderVertexSnapshot out = a;
      const float inverseW = (1.0f - screenT) / wa + screenT / wb;
      const float t = (screenT / wb) / inverseW;
      out.screenSpaceW = 1.0f / inverseW;
      out.fogEyeDepth = sourceState.fogMode == CoinRenderFogMode::NONE
                            ? -1.0f
                            : eyeDepth(a) * (1.0f - t) + eyeDepth(b) * t;
      for (int c = 0; c < 2; ++c) {
        out.texcoord[c] = a.texcoord[c] * (1.0f - t) + b.texcoord[c] * t;
        for (size_t u = 0; u < COIN_RENDER_MAX_TEXTURE_UNITS - 1; ++u)
          out.extraTexcoords[u][c] =
              a.extraTexcoords[u][c] * (1.0f - t) + b.extraTexcoords[u][c] * t;
      }
      for (size_t u = 0; u < COIN_RENDER_MAX_TEXTURE_UNITS; ++u) {
        out.textureR[u] = a.textureR[u] * (1.0f - t) + b.textureR[u] * t;
        out.textureQ[u] = a.textureQ[u] * (1.0f - t) + b.textureQ[u] * t;
      }
      out.materialSlot = materialAt(a.materialSlot, b.materialSlot, t);
      return out;
    };

    if (original.topology == CoinRenderPrimitiveTopology::LINE_LIST) {
      const float width = std::max(sourceState.lineWidth, 1.0f);
      const uint32_t pattern = sourceState.linePattern & 0xffffu;
      bool polygonArea = false;
      if (sourceState.polygonLinePattern && !sourceState.preservePolygonEdgeDirection &&
          pattern == 0xffffu && original.geometry.indexCount) {
        const auto first = plan.indices[original.geometry.firstIndex];
        if (first >= plan.vertices.size()) return fail("Styled polygon references invalid vertex");
        const SbVec3f origin(plan.vertices[first].position);
        SbVec3f direction(0, 0, 0);
        for (uint32_t i = 1; i < original.geometry.indexCount; ++i) {
          const auto index = plan.indices[original.geometry.firstIndex + i];
          if (index >= plan.vertices.size()) return fail("Styled polygon references invalid vertex");
          const SbVec3f delta = SbVec3f(plan.vertices[index].position) - origin;
          if (direction.sqrLength() == 0) direction = delta;
          else if (direction.cross(delta).sqrLength() > 0) { polygonArea = true; break; }
        }
      }
      uint32_t polygonPhase = 0;
      uint32_t& phase = original.lineStripId ? stripPhases[original.lineStripId] : polygonPhase;
      for (size_t offset = 0; offset + 1 < original.geometry.indexCount; offset += 2) {
        const uint32_t firstIndex = plan.indices[original.geometry.firstIndex + offset];
        const uint32_t secondIndex = plan.indices[original.geometry.firstIndex + offset + 1];
        if (firstIndex >= plan.vertices.size() || secondIndex >= plan.vertices.size()) {
          return fail("Styled line references invalid vertex");
        }
        CoinRenderVertexSnapshot firstVertex = plan.vertices[firstIndex];
        CoinRenderVertexSnapshot secondVertex = plan.vertices[secondIndex];
        // Use identical arithmetic for coincident solid polygon boundaries.
        // Distinct side/cap UVs retain their authored draw order under LEQUAL.
        // Patterned edges retain stipple phase; collapsed contours keep their
        // authored direction for endpoint coverage.
        if (polygonArea &&
            std::lexicographical_compare(secondVertex.position, secondVertex.position + 3,
                                         firstVertex.position, firstVertex.position + 3))
          std::swap(firstVertex, secondVertex);
        if (firstVertex.materialSlot >= plan.materials.size() ||
            secondVertex.materialSlot >= plan.materials.size()) {
          return fail("Styled line references invalid material");
        }
        firstVertex.materialSlot = shadeMaterial(firstVertex);
        secondVertex.materialSlot = shadeMaterial(secondVertex);
        float clipFirst, clipLast;
        if (!coin_render_clip_segment(sourceState, firstVertex, secondVertex, clipFirst, clipLast))
          continue;
        if (clipFirst != 0 || clipLast != 1) {
          const CoinRenderVertexSnapshot a = firstVertex, b = secondVertex;
          firstVertex = coin_render_clip_interpolate(
              a, b, clipFirst, materialAt(a.materialSlot, b.materialSlot, clipFirst));
          secondVertex = coin_render_clip_interpolate(
              a, b, clipLast, materialAt(a.materialSlot, b.materialSlot, clipLast));
        }
        SbVec3f firstNdc, secondNdc;
        float firstW, secondW;
        if (!project(firstVertex, firstNdc, firstW) || !project(secondVertex, secondNdc, secondW))
          continue;
        const float dxPixels = (secondNdc[0] - firstNdc[0]) * 0.5f * viewport.width;
        const float dyPixels = (secondNdc[1] - firstNdc[1]) * 0.5f * viewport.height;
        const float lengthPixels = std::sqrt(dxPixels * dxPixels + dyPixels * dyPixels);
        if (lengthPixels <= 1.0e-6f || pattern == 0u)
          continue;
        float offsetX = 0, offsetY = 0;
        std::vector<std::pair<float, float>> spans;
        {
          // Solid and patterned lines use the same unit-fragment assembly.
          // Rounded widths replicate those fragments along the minor axis.
          if (!sourceState.polygonLinePattern && original.lineStripId == 0)
            phase = 0;
          const bool xMajor = std::abs(dxPixels) >= std::abs(dyPixels);
          const float rasterHalfWidth = std::max(1.0f, std::floor(width + .5f)) * .5f;
          offsetX = xMajor ? 0 : rasterHalfWidth * 2 / viewport.width;
          offsetY = xMajor ? rasterHalfWidth * 2 / viewport.height : 0;
          if (!coin_render_line_stipple((firstNdc[0] + 1.0) * .5 * viewport.width,
                                        (firstNdc[1] + 1.0) * .5 * viewport.height,
                                        (secondNdc[0] + 1.0) * .5 * viewport.width,
                                        (secondNdc[1] + 1.0) * .5 * viewport.height, pattern,
                                        sourceState.linePatternScaleFactor, phase, spans))
            return fail("Line stipple exceeds the Core raster budget");
        }
        for (const auto& span : spans) {
          // Stipple operates on whole pixel cells, whose first/last boundaries
          // can lie beyond the original segment. Split those caps at the
          // endpoints: original intervals keep exact homogeneous attributes,
          // while caps retain endpoint depth, W, UV, color and fog. Shared
          // triangle edges follow the rasterizer's single-owner coverage rule.
          float cuts[4] = {span.first};
          size_t cutCount = 1;
          if (span.first < 0 && span.second > 0)
            cuts[cutCount++] = 0;
          if (span.first < 1 && span.second > 1)
            cuts[cutCount++] = 1;
          cuts[cutCount++] = span.second;
          for (size_t part = 1; part < cutCount; ++part) {
            const float t0 = cuts[part - 1], t1 = cuts[part];
            const float attributeT0 = std::max(0.0f, std::min(1.0f, t0));
            const float attributeT1 = std::max(0.0f, std::min(1.0f, t1));
            SbVec3f start = firstNdc + (secondNdc - firstNdc) * t0;
            SbVec3f end = firstNdc + (secondNdc - firstNdc) * t1;
            start[2] = firstNdc[2] + (secondNdc[2] - firstNdc[2]) * attributeT0;
            end[2] = firstNdc[2] + (secondNdc[2] - firstNdc[2]) * attributeT1;
            const CoinRenderVertexSnapshot startAttributes =
                attributesAt(firstVertex, secondVertex, firstW, secondW, attributeT0);
            const CoinRenderVertexSnapshot endAttributes =
                attributesAt(firstVertex, secondVertex, firstW, secondW, attributeT1);
            const uint32_t startMaterial = startAttributes.materialSlot;
            const uint32_t endMaterial = endAttributes.materialSlot;
            const uint32_t base = static_cast<uint32_t>(plan.vertices.size());
            appendVertex(start[0] + offsetX, start[1] + offsetY, start[2], startMaterial,
                         startAttributes);
            appendVertex(start[0] - offsetX, start[1] - offsetY, start[2], startMaterial,
                         startAttributes);
            appendVertex(end[0] + offsetX, end[1] + offsetY, end[2], endMaterial, endAttributes);
            appendVertex(end[0] - offsetX, end[1] - offsetY, end[2], endMaterial, endAttributes);
            const uint32_t quadIndices[6] = {base,     base + 1, base + 2,
                                             base + 2, base + 1, base + 3};
            plan.indices.insert(plan.indices.end(), quadIndices, quadIndices + 6);
          }
        }
      }
    } else if (original.topology == CoinRenderPrimitiveTopology::POINT_LIST) {
      const float size = std::max(1.0f, std::floor(sourceState.pointSize + .5f));
      const float halfSize = size * 0.5f;
      const float offsetX = halfSize * 2.0f / viewport.width;
      const float offsetY = halfSize * 2.0f / viewport.height;
      for (size_t offset = 0; offset < original.geometry.indexCount; ++offset) {
        const uint32_t vertexIndex = plan.indices[original.geometry.firstIndex + offset];
        if (vertexIndex >= plan.vertices.size()) {
          return fail("Styled point references invalid vertex");
        }
        const CoinRenderVertexSnapshot vertex = plan.vertices[vertexIndex];
        if (vertex.materialSlot >= plan.materials.size()) {
          return fail("Styled point references invalid material");
        }
        if (!coin_render_clip_point(sourceState, vertex))
          continue;
        SbVec3f ndc;
        float clipW;
        if (!project(vertex, ndc, clipW))
          continue;
        const bool odd = std::fmod(size, 2.0f) == 1.0f;
        for (int axis = 0; axis < 2; ++axis) {
          const float dimension = axis == 0 ? float(viewport.width) : float(viewport.height);
          const float pixel = (ndc[axis] + 1) * .5f * dimension;
          const float center = odd ? std::floor(pixel) + .5f : std::floor(pixel + .5f);
          ndc[axis] = center * 2 / dimension - 1;
        }
        CoinRenderVertexSnapshot attributes = vertex;
        attributes.materialSlot = shadeMaterial(vertex);
        attributes.screenSpaceW = clipW;
        attributes.fogEyeDepth =
            sourceState.fogMode == CoinRenderFogMode::NONE ? -1.0f : eyeDepth(vertex);
        const uint32_t materialSlot = attributes.materialSlot;
        const uint32_t base = static_cast<uint32_t>(plan.vertices.size());
        appendVertex(ndc[0] - offsetX, ndc[1] - offsetY, ndc[2], materialSlot, attributes);
        appendVertex(ndc[0] + offsetX, ndc[1] - offsetY, ndc[2], materialSlot, attributes);
        appendVertex(ndc[0] - offsetX, ndc[1] + offsetY, ndc[2], materialSlot, attributes);
        appendVertex(ndc[0] + offsetX, ndc[1] + offsetY, ndc[2], materialSlot, attributes);
        const uint32_t quadIndices[6] = {base, base + 1, base + 2, base + 2, base + 1, base + 3};
        plan.indices.insert(plan.indices.end(), quadIndices, quadIndices + 6);
      }
    }

    expanded.geometry.vertexCount =
        static_cast<uint32_t>(plan.vertices.size()) - expanded.geometry.firstVertex;
    expanded.geometry.indexCount =
        static_cast<uint32_t>(plan.indices.size()) - expanded.geometry.firstIndex;
    // Resolve samples exactly on a quad boundary with the reference's top/left
    // ownership. One subpixel step makes that choice unambiguous across APIs;
    // native bounding-box contours retain their separately qualified ownership.
    if (!sourceState.preservePolygonEdgeDirection) {
      const float dx = 2.0f / (256.0f * viewport.width);
      const float dy = 2.0f / (256.0f * viewport.height);
      for (uint32_t i = 0; i < expanded.geometry.vertexCount; ++i) {
        auto &vertex = plan.vertices[expanded.geometry.firstVertex + i];
        vertex.position[0] -= dx;
        vertex.position[1] += dy;
        // Publish the same eight-bit window grid used for CPU coverage.
        // GPU interpolation must consume these positions too, rather than
        // keeping unsnapped planes behind quantized coverage endpoints.
        for (int axis = 0; axis < 2; ++axis) {
          const float dimension = axis == 0 ? float(viewport.width) : float(viewport.height);
          const float pixel = (vertex.position[axis] + 1.0f) * 0.5f * dimension;
          vertex.position[axis] = std::round(pixel * 256.0f) / 256.0f * 2.0f / dimension - 1.0f;
        }
      }
    }
    // A coplanar line/point range has exactly constant mapped depth. Express
    // that constant as a collapsed interval rather than relying on hardware
    // interpolation/format rounding to retain equality between shared endpoints.
    // Original polygon lines and native bounding-box ownership keep their profile.
    if (expanded.geometry.vertexCount && sourceState.depthTest && sourceState.depthWrite &&
        !sourceState.polygonLinePattern && !sourceState.preservePolygonEdgeDirection) {
      const float z = plan.vertices[expanded.geometry.firstVertex].position[2];
      bool constant = true;
      for (uint32_t i = 1; i < expanded.geometry.vertexCount; ++i)
        constant &= plan.vertices[expanded.geometry.firstVertex + i].position[2] == z;
      if (constant) {
        const float depth = sourceState.depthRange[0] + (z * .5f + .5f) *
                            (sourceState.depthRange[1] - sourceState.depthRange[0]);
        plan.renderStates[stateSlot].depthRange[0] = std::max(0.0f, std::min(1.0f, depth));
        plan.renderStates[stateSlot].depthRange[1] = plan.renderStates[stateSlot].depthRange[0];
      }
    }
    if (expanded.stableNodeId && expanded.geometry.indexCount) {
      uint64_t digest = 14695981039346656037ULL;
      auto hashBytes = [&](const void *data, size_t size) {
        const auto *bytes = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < size; ++i) {
          digest ^= bytes[i];
          digest *= 1099511628211ULL;
        }
      };
      for (uint32_t i = 0; i < expanded.geometry.vertexCount; ++i) {
        const auto &vertex = plan.vertices[expanded.geometry.firstVertex + i];
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
      for (uint32_t i = 0; i < expanded.geometry.indexCount; ++i) {
        const uint32_t local = plan.indices[expanded.geometry.firstIndex + i] -
                               expanded.geometry.firstVertex;
        hashBytes(&local, sizeof(local));
      }
      expanded.sourceRevision = digest ? digest : 1;
    }
    if (expanded.geometry.indexCount != 0)
      expandedDraws.push_back(expanded);
  }

  plan.draws.swap(expandedDraws);
  return true;
}

#endif
