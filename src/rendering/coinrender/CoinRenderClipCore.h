#ifndef COIN_RENDER_CLIP_CORE_H
#define COIN_RENDER_CLIP_CORE_H
#include "rendering/coinrender/CoinRenderFloatCore.h"

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <algorithm>
#include <cmath>

// Coin supplies world-space planes at node traversal. Keep n.p - distance >= 0.
// This Core consumes snapshots only; no actions, SoState or GPU operations.
inline bool coin_render_clip_equations(const CoinRenderRenderStateSnapshot & state,
                                       float equations[COIN_RENDER_MAX_CLIP_PLANES][4],
                                       std::string & diagnostic) {
  if (state.clipPlanesWorld.size() > COIN_RENDER_MAX_CLIP_PLANES) {
    diagnostic = "UNSUPPORTED: more than eight active Coin clipping planes";
    return false;
  }
  if (!state.clipPlanesWorld.empty() &&
      (!coin_render_is_finite(state.view.det4()) || std::abs(state.view.det4()) <= 1e-12f)) {
    diagnostic = "Invalid view matrix for Coin clipping planes";
    return false;
  }
  for (size_t i = 0; i < state.clipPlanesWorld.size(); ++i) {
    SbPlane plane = state.clipPlanesWorld[i];
    plane.transform(state.view);
    const SbVec3f & normal = plane.getNormal();
    for (int c = 0; c < 3; ++c) equations[i][c] = normal[c];
    equations[i][3] = -plane.getDistanceFromOrigin();
    for (int c = 0; c < 4; ++c) if (!coin_render_is_finite(equations[i][c])) {
      diagnostic = "Invalid Coin clipping plane equation";
      return false;
    }
    if (normal.sqrLength() <= 1e-12f) {
      diagnostic = "Invalid zero-normal Coin clipping plane";
      return false;
    }
  }
  return true;
}

inline bool coin_render_clip_point(const CoinRenderRenderStateSnapshot & state,
                                   const CoinRenderVertexSnapshot & vertex) {
  if (state.clipPlanesWorld.empty()) return true;
  SbVec3f world;
  state.model.multVecMatrix(SbVec3f(vertex.position), world);
  for (const SbPlane & plane : state.clipPlanesWorld)
    if (plane.getDistance(world) < 0) return false;
  return true;
}

// Parametric interval of the original segment, before screen-space expansion.
inline bool coin_render_clip_segment(const CoinRenderRenderStateSnapshot & state,
                                     const CoinRenderVertexSnapshot & a,
                                     const CoinRenderVertexSnapshot & b,
                                     float & first, float & last) {
  first = 0; last = 1;
  if (state.clipPlanesWorld.empty()) return true;
  SbVec3f wa, wb;
  state.model.multVecMatrix(SbVec3f(a.position), wa);
  state.model.multVecMatrix(SbVec3f(b.position), wb);
  for (const SbPlane & plane : state.clipPlanesWorld) {
    const float da = plane.getDistance(wa), db = plane.getDistance(wb);
    if (da < 0 && db < 0) return false;
    if (da < 0) first = std::max(first, da / (da - db));
    if (db < 0) last = std::min(last, da / (da - db));
    if (first > last) return false;
  }
  return true;
}

inline CoinRenderVertexSnapshot coin_render_clip_interpolate(
  const CoinRenderVertexSnapshot & a, const CoinRenderVertexSnapshot & b,
  float t, uint32_t materialSlot) {
  CoinRenderVertexSnapshot out = a;
  for (int c = 0; c < 3; ++c) {
    out.position[c] = a.position[c] + (b.position[c] - a.position[c]) * t;
    out.normal[c] = a.normal[c] + (b.normal[c] - a.normal[c]) * t;
  }
  for (int c = 0; c < 2; ++c) {
    out.texcoord[c] = a.texcoord[c] + (b.texcoord[c] - a.texcoord[c]) * t;
    for (size_t u = 0; u < COIN_RENDER_MAX_TEXTURE_UNITS - 1; ++u)
      out.extraTexcoords[u][c] = a.extraTexcoords[u][c] +
        (b.extraTexcoords[u][c] - a.extraTexcoords[u][c]) * t;
  }
  for (size_t u = 0; u < COIN_RENDER_MAX_TEXTURE_UNITS; ++u) {
    out.textureR[u] = a.textureR[u] + (b.textureR[u] - a.textureR[u]) * t;
    out.textureQ[u] = a.textureQ[u] + (b.textureQ[u] - a.textureQ[u]) * t;
  }
  out.materialSlot = materialSlot;
  return out;
}
#endif
