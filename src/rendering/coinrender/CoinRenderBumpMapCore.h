#ifndef COIN_RENDER_BUMP_MAP_CORE_H
#define COIN_RENDER_BUMP_MAP_CORE_H

#include <Inventor/CoinRenderExport.h>

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

struct CoinRenderFramePlan;
struct CoinRenderDrawPacket;
// Pure admission for the directional eight-unit/VSM extension. Legacy bump is unchanged.
COIN_RENDER_DLL_API bool coin_render_uses_extended_bump(const CoinRenderFramePlan &);
COIN_RENDER_DLL_API bool coin_render_validate_extended_bump(const CoinRenderFramePlan &,
                                                            std::string &);

struct CoinRenderBumpImage {
  uint32_t width = 0;
  uint32_t height = 0;
  bool convertedHeight = false;
  bool clampS = false;
  bool clampT = false;
  std::vector<uint8_t> rgb;
};

struct CoinRenderBumpVertex {
  float position[3] = {};
  float coord[2] = {};
};

struct CoinRenderBumpTangents {
  float s[3] = {};
  float t[3] = {};
};

struct CoinRenderBumpGpuVertex {
  float coord[4] = {}; // Bump matrix applied to (s,t,0,1); divide by Q in shader.
  float tangentS[3] = {};
  float tangentT[3] = {};
  uint32_t imageSlot = 0; // One-based; zero is inactive.
};

struct CoinRenderFramePlan;
// Build an optional vertex stream aligned to the frame's original vertex
// indices. No stream is allocated when the frame has no active bump image.
COIN_RENDER_DLL_API bool coin_render_build_bump_gpu_stream(
  const CoinRenderFramePlan & frame, std::vector<CoinRenderBumpGpuVertex> & output,
  std::string & diagnostic);
// Classify the indexed occurrence, not its declaration or shared shape.
// Ranges and vertex/image slots are validated before returning zero for an
// ordinary draw; any indexed active image makes the occurrence a consumer.
// Primitive slot consistency remains the existing executor's responsibility.
// Map-only casters are caller-owned.
COIN_RENDER_DLL_API bool coin_render_bump_draw_slot(
  const CoinRenderFramePlan &, const std::vector<CoinRenderBumpGpuVertex> &,
  const CoinRenderDrawPacket &, uint32_t &slot, std::string &diagnostic);
// Each emitted vertex names its original frame vertex. Used after a backend
// expands or reorders geometry; failure preserves the previous output packet.
COIN_RENDER_DLL_API bool coin_render_remap_bump_gpu_stream(
  const std::vector<CoinRenderBumpGpuVertex> & source,
  const std::vector<uint32_t> & sourceVertexForEmitted,
  std::vector<CoinRenderBumpGpuVertex> & output, std::string & diagnostic);

// Identity used by SoPrimitiveVertexCache when accumulating legacy tangents.
// An occurrence separates the cache of one shape from another.
struct CoinRenderBumpLegacyCorner {
  uint32_t shapeOccurrence = 0;
  int32_t textureCoordIndex = -1;
  uint32_t rgba = 0;
  float position[3] = {};
  float normal[3] = {};
  float textureCoord0[4] = {};
  float bumpCoord[2] = {};
};

struct CoinRenderBumpCoord {
  CoinRenderBumpCoord(float svalue = 0.0f, float tvalue = 0.0f)
    : s(svalue), t(tvalue) {}
  float s;
  float t;
};

class SoCallbackAction;
class SoPrimitiveVertex;
class SoBumpMap;

// Prepare Coin's legacy bump image without GL. RGB/RGBA images with unequal
// color channels are tangent-space normals; grayscale images use Coin's
// SoHeightMapToNormalMap Sobel conversion. Failure leaves the output unchanged.
COIN_RENDER_DLL_API bool coin_render_prepare_bump_image(
  const std::vector<uint8_t> & source, uint32_t width, uint32_t height,
  uint32_t components, CoinRenderBumpImage & output, std::string & diagnostic);
COIN_RENDER_DLL_API bool coin_render_prepare_bump_image(
  const uint8_t * source, size_t byteCount, uint32_t width, uint32_t height,
  uint32_t components, CoinRenderBumpImage & output, std::string & diagnostic);
COIN_RENDER_DLL_API bool coin_render_capture_bump_node(
  const SoBumpMap & node, CoinRenderBumpImage & output,
  std::string & diagnostic);

// Mirrors soshape_bumprender::calcTangentSpace: normalized per-triangle S/T
// directions are accumulated by shared vertex, then normalized again.
COIN_RENDER_DLL_API bool coin_render_legacy_bump_tangents(
  const std::vector<CoinRenderBumpVertex> & vertices,
  const std::vector<uint32_t> & triangleIndices,
  std::vector<CoinRenderBumpTangents> & output, std::string & diagnostic);

// Deduplicate callback corners by the CoinGL cache identity before tangent
// accumulation, then expand the result back to each captured corner.
COIN_RENDER_DLL_API bool coin_render_legacy_bump_corner_tangents(
  const std::vector<CoinRenderBumpLegacyCorner> & corners,
  const std::vector<uint32_t> & triangleIndices,
  std::vector<CoinRenderBumpTangents> & output, std::string & diagnostic);

// CPU oracle for the legacy diffuse pass. CoinGL projects the object-space
// light into S/T/N at each vertex, interpolates that vector, then normalizes
// it in the cube-map lookup before GL_DOT3_RGB with the sampled normal.
COIN_RENDER_DLL_API bool coin_render_legacy_bump_light_vector(
  const CoinRenderBumpTangents & tangents, const float normal[3],
  const float position[3], const float light[3], bool pointLight,
  float output[3]);
// The specular vertex program projects the normalized direction from each
// object-space vertex to the object's projection point into the same basis.
COIN_RENDER_DLL_API bool coin_render_legacy_bump_eye_vector(
  const CoinRenderBumpTangents & tangents, const float normal[3],
  const float position[3], const float eye[3], float output[3]);
COIN_RENDER_DLL_API float coin_render_legacy_bump_dot3(
  const uint8_t normalRgb[3], const float interpolatedLight[3]);
// CPU oracle for the legacy specular fragment program: normalize the sum of
// interpolated tangent-space light and eye vectors, then apply shininess*64.
COIN_RENDER_DLL_API float coin_render_legacy_bump_specular(
  const uint8_t normalRgb[3], const float interpolatedLight[3],
  const float interpolatedEye[3], float shininess);

// SoPrimitiveVertexCache uses the primitive's unit-0 UV by default. When a
// SoBumpMapCoordinate is active and the primitive has indexed point detail,
// its texture-coordinate index is clamped to the authored array, independently
// of the unit-0 texture image.
COIN_RENDER_DLL_API bool coin_render_select_bump_coord(
  const CoinRenderBumpCoord & primitiveCoord,
  const std::vector<CoinRenderBumpCoord> & authoredCoords,
  bool hasPointDetail, int textureCoordIndex, CoinRenderBumpCoord & output,
  std::string & diagnostic);

COIN_RENDER_DLL_API bool coin_render_capture_bump_coord(
  SoCallbackAction * action, const SoPrimitiveVertex * vertex,
  CoinRenderBumpCoord & output, std::string & diagnostic);

#endif
