#ifndef COIN_RENDER_STROKE_CORE_H
#define COIN_RENDER_STROKE_CORE_H

#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderLineStippleCore.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// Mechanical expansion of captured strokes, shared by every GPU backend.
inline bool coin_render_expand_strokes(CoinRenderFramePlan& plan, std::string& diagnostic) {
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
    expanded.topology = CoinRenderPrimitiveTopology::TRIANGLE_LIST;
    expanded.renderStateSlot = stateSlot;
    expanded.geometry.firstVertex = static_cast<uint32_t>(plan.vertices.size());
    expanded.geometry.vertexCount = 0;
    expanded.geometry.firstIndex = static_cast<uint32_t>(plan.indices.size());
    expanded.geometry.indexCount = 0;

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
      out.materialSlot = materialAt(a.materialSlot, b.materialSlot, t);
      return out;
    };

    if (original.topology == CoinRenderPrimitiveTopology::LINE_LIST) {
      const float width = std::max(sourceState.lineWidth, 1.0f);
      const float halfWidth = width * 0.5f;
      const uint32_t pattern = sourceState.linePattern & 0xffffu;
      const float patternScale =
          static_cast<float>(std::max(1, sourceState.linePatternScaleFactor));
      uint32_t polygonPhase = 0;
      for (size_t offset = 0; offset + 1 < original.geometry.indexCount; offset += 2) {
        const uint32_t firstIndex = plan.indices[original.geometry.firstIndex + offset];
        const uint32_t secondIndex = plan.indices[original.geometry.firstIndex + offset + 1];
        if (firstIndex >= plan.vertices.size() || secondIndex >= plan.vertices.size()) {
          return fail("Styled line references invalid vertex");
        }
        CoinRenderVertexSnapshot firstVertex = plan.vertices[firstIndex];
        CoinRenderVertexSnapshot secondVertex = plan.vertices[secondIndex];
        if (firstVertex.materialSlot >= plan.materials.size() ||
            secondVertex.materialSlot >= plan.materials.size()) {
          return fail("Styled line references invalid material");
        }
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
        float offsetX = (-dyPixels / lengthPixels) * halfWidth * 2.0f / viewport.width;
        float offsetY = (dxPixels / lengthPixels) * halfWidth * 2.0f / viewport.height;
        std::vector<std::pair<float, float>> spans;
        if (sourceState.polygonLinePattern && pattern != 0xffffu) {
          const bool xMajor = std::abs(dxPixels) >= std::abs(dyPixels);
          const float rasterHalfWidth = std::max(1.0f, std::floor(width + .5f)) * .5f;
          offsetX = xMajor ? 0 : rasterHalfWidth * 2 / viewport.width;
          offsetY = xMajor ? rasterHalfWidth * 2 / viewport.height : 0;
          if (!coin_render_polygon_stipple((firstNdc[0] + 1.0) * .5 * viewport.width,
                                           (firstNdc[1] + 1.0) * .5 * viewport.height,
                                           (secondNdc[0] + 1.0) * .5 * viewport.width,
                                           (secondNdc[1] + 1.0) * .5 * viewport.height, pattern,
                                           sourceState.linePatternScaleFactor, polygonPhase, spans))
            return fail("Polygon stipple exceeds the Core raster budget");
        } else {
          float cursor = 0;
          while (cursor < lengthPixels - 1e-5f) {
            float next = lengthPixels;
            bool visible = true;
            if (pattern != 0xffffu) {
              const uint32_t cell = static_cast<uint32_t>(std::floor(cursor / patternScale));
              next = std::min(lengthPixels, (static_cast<float>(cell) + 1) * patternScale);
              visible = (pattern & (1u << (cell & 15u))) != 0;
            }
            if (next <= cursor + 1e-6f)
              next = std::min(lengthPixels, cursor + patternScale);
            if (visible)
              spans.emplace_back(cursor / lengthPixels, next / lengthPixels);
            cursor = next;
          }
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
      const float halfSize = std::max(sourceState.pointSize, 1.0f) * 0.5f;
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
        CoinRenderVertexSnapshot attributes = vertex;
        attributes.screenSpaceW = clipW;
        attributes.fogEyeDepth =
            sourceState.fogMode == CoinRenderFogMode::NONE ? -1.0f : eyeDepth(vertex);
        const uint32_t materialSlot = vertex.materialSlot;
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
    if (expanded.geometry.indexCount != 0)
      expandedDraws.push_back(expanded);
  }

  plan.draws.swap(expandedDraws);
  return true;
}

#endif
