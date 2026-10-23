// Informational Release benchmark.  CTest intentionally does not run it: the
// printed operation scaling is the review artifact, while source guards gate
// accidental reintroduction of the known nested scans.
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif
#include "rendering/coinrender/CoinRenderDepthCore.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderPolygonStyleCore.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

template <class F> double measure(F && fn) {
  const auto begin = Clock::now();
  if (!fn()) return -1.0;
  return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

CoinRenderVertexSnapshot vertex(float x, float y, float z, uint32_t material = 0) {
  CoinRenderVertexSnapshot value;
  value.position[0] = x; value.position[1] = y; value.position[2] = z;
  value.normal[2] = 1.0f;
  value.materialSlot = material;
  value.texcoord[0] = (x + 1.0f) * .5f;
  value.texcoord[1] = (y + 1.0f) * .5f;
  return value;
}

bool polygonCase(size_t count, uint64_t & checksum) {
  std::vector<CoinRenderVertexSnapshot> ring, triangles;
  ring.reserve(count); triangles.reserve((count - 2) * 3);
  constexpr double pi = 3.14159265358979323846;
  for (size_t i = 0; i < count; ++i) {
    const double angle = 2.0 * pi * double(i) / double(count);
    // A small alternating Z makes the source nonplanar, selecting the
    // triangle-boundary reconstruction whose old implementation was O(n^2).
    ring.push_back(vertex(float(.8 * std::cos(angle)), float(.8 * std::sin(angle)),
                          i & 1 ? .02f : -.02f));
  }
  for (size_t i = 1; i + 1 < count; ++i) {
    triangles.push_back(ring[0]); triangles.push_back(ring[i]); triangles.push_back(ring[i + 1]);
  }
  CoinRenderRenderStateSnapshot state;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  state.cullMode = CoinRenderCullMode::NONE;
  state.polygonOffsetEnabled = true;
  state.polygonOffsetStyles = 2;
  state.polygonOffsetFactor = 1.0f;
  CoinRenderViewportSnapshot viewport; viewport.width = viewport.height = 1024;
  std::vector<CoinRenderMaterialSnapshot> materials(1);
  CoinRenderLightingSnapshot lighting;
  CoinRenderPolygonStyleResult result;
  std::string diagnostic;
  if (!coin_render_prepare_polygon_style(ring, state, materials, lighting,
      CoinRenderPolygonStyle::LINES, result, diagnostic, viewport, false, &triangles)) {
    std::cerr << "polygon benchmark failed: " << diagnostic << '\n'; return false;
  }
  if (result.indices.size() != count * 2 || result.vertices.size() != count * 2) {
    std::cerr << "polygon boundary contract changed: vertices=" << result.vertices.size()
              << " indices=" << result.indices.size() << '\n'; return false;
  }
  for (size_t i = 0; i < result.vertices.size(); ++i)
    checksum = checksum * 1099511628211ULL + result.indices[i] +
      uint64_t(std::lround((result.vertices[i].vertex.texcoord[0] + 2.0f) * 100000.0f));
  return true;
}

CoinRenderFramePlan plan(size_t triangleCount, uint64_t revision) {
  CoinRenderFramePlan result;
  result.revision = revision;
  result.materials.push_back(CoinRenderMaterialSnapshot{});
  result.lightingStates.push_back(CoinRenderLightingSnapshot{});
  result.cameras.push_back(CoinRenderCameraSnapshot{});
  CoinRenderViewportSnapshot viewport; viewport.width = viewport.height = 1024;
  result.viewports.push_back(viewport);
  CoinRenderRenderStateSnapshot state;
  state.lightModel = CoinRenderLightModel::BASE_COLOR;
  state.cullMode = CoinRenderCullMode::NONE;
  state.polygonOffsetEnabled = true;
  state.polygonOffsetStyles = 1;
  state.polygonOffsetPrimitiveStyle = 1;
  state.polygonOffsetFactor = 1.0f;
  state.polygonOffsetMaxDepth = -1.0f;
  result.renderStates.push_back(state);
  result.vertices.reserve(triangleCount * 3);
  result.indices.reserve(triangleCount * 3);
  for (size_t i = 0; i < triangleCount; ++i) {
    const float x = -.9f + 1.8f * float(i % 512) / 512.0f;
    const float y = -.9f + 1.8f * float((i / 512) % 512) / 512.0f;
    const uint32_t base = uint32_t(result.vertices.size());
    result.vertices.push_back(vertex(x, y, 0));
    result.vertices.push_back(vertex(x + .001f, y, .0001f));
    result.vertices.push_back(vertex(x, y + .001f, .0002f));
    result.indices.insert(result.indices.end(), {base, base + 1, base + 2});
  }
  CoinRenderDrawPacket draw;
  draw.topology = CoinRenderPrimitiveTopology::TRIANGLE_LIST;
  draw.geometry.vertexCount = uint32_t(result.vertices.size());
  draw.geometry.indexCount = uint32_t(result.indices.size());
  draw.stableNodeId = 17; draw.sourceRevision = 19; draw.drawOrdinal = 23;
  result.draws.push_back(draw);
  return result;
}

bool depthCase(size_t triangles, size_t & draws) {
  auto frame = plan(triangles, 1);
  std::string diagnostic;
  if (!coin_render_resolve_triangle_depth(frame, diagnostic)) {
    std::cerr << "depth benchmark failed: " << diagnostic << '\n'; return false;
  }
  draws = frame.draws.size();
  size_t covered = 0;
  for (const auto & draw : frame.draws) covered += draw.geometry.indexCount;
  return covered == triangles * 3 && !frame.draws.empty() && frame.draws.front().clearDepthBefore == false;
}

bool reuseCase(size_t triangles, CoinRenderFrameReuseKind & kind) {
  auto before = plan(triangles, 31), after = before;
  after.revision = 32;
  SbMatrix view = SbMatrix::identity(); view[3][0] = .01f;
  after.cameras[0].viewMatrix = view;
  after.renderStates[0].view = view;
  kind = CoinRenderFrameReuseCore::classify(before, after).kind;
  return kind == CoinRenderFrameReuseKind::CAMERA_PATCH;
}
}

int main() {
  for (size_t n : {1000u, 2000u, 4000u}) {
    uint64_t checksum = 1469598103934665603ULL;
    const double ms = measure([&] { return polygonCase(n, checksum); });
    if (ms < 0) return 1;
    std::cout << "polygon_vertices=" << n << " ms=" << ms << " checksum=" << checksum << '\n';
  }
  for (size_t n : {1000u, 4000u, 16000u}) {
    size_t draws = 0;
    const double ms = measure([&] { return depthCase(n, draws); });
    if (ms < 0) return 1;
    std::cout << "depth_triangles=" << n << " ms=" << ms << " output_draws=" << draws
              << " amplification=" << double(draws) << '\n';
  }
  for (size_t n : {1000u, 4000u, 16000u}) {
    CoinRenderFrameReuseKind kind = CoinRenderFrameReuseKind::UNKNOWN;
    const double ms = measure([&] { return reuseCase(n, kind); });
    if (ms < 0) return 1;
    std::cout << "reuse_triangles=" << n << " ms=" << ms << " kind=" << int(kind) << '\n';
  }
  return 0;
}
