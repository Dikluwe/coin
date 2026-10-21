#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderBumpMapCore.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderStateCore.h"
#include "rendering/coinrender/CoinRenderFloatCore.h"
#include "rendering/coinrender/CoinRenderLegacyColorCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "CoinRenderTestEnvironment.h"

#include <Inventor/SoDB.h>
#include <Inventor/SbImage.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/details/SoFaceDetail.h>
#include <Inventor/details/SoPointDetail.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoBumpMapCoordinate.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoBumpMapTransform.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureCoordinateBinding.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/engines/SoHeightMapToNormalMap.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
bool check(bool condition, const char * message);

// A height offset contributes exactly zero to the integer Sobel stencil.
// Native Coin, Core preparation and public-node capture are separate subjects;
// none of their outputs is used to construct another subject's expected bytes.
bool testBumpHeightOffsetInvariance() {
  unsigned cases = 0, nativeBad = 0, coreBad = 0, captureBad = 0, printed = 0;
  auto *node = new SoBumpMap;
  node->ref();
  auto oracle = [](const std::vector<uint8_t> &gray, unsigned w, unsigned h) {
    std::vector<uint8_t> out(size_t(w) * h * 3);
    const int weight[] = {1, 2, 1};
    auto at = [&](int x, int y) { return int(gray[size_t((y + int(h)) % int(h)) * w + unsigned((x + int(w)) % int(w))]); };
    for (unsigned y = 0; y < h; ++y)
      for (unsigned x = 0; x < w; ++x) {
        int gx = 0, gy = 0;
        for (int k = -1; k <= 1; ++k) {
          gx += weight[k + 1] * (at(int(x) + 1, int(y) + k) - at(int(x) - 1, int(y) + k));
          gy += weight[k + 1] * (at(int(x) + k, int(y) - 1) - at(int(x) + k, int(y) + 1));
        }
        const double nx = -gx / 255., ny = -gy / 255.;
        const double length = std::sqrt(nx * nx + ny * ny + 1);
        const double normal[] = {nx / length, ny / length, 1 / length};
        for (unsigned c = 0; c < 3; ++c)
          out[(size_t(y) * w + x) * 3 + c] = uint8_t(std::min(255., (normal[c] + 1) * 128));
      }
    return out;
  };
  auto verify = [&](const std::vector<uint8_t> &gray, unsigned w, unsigned h,
                    unsigned components, const std::vector<uint8_t> &expected,
                    unsigned offset, bool constant) {
    std::vector<uint8_t> source(size_t(w) * h * components);
    for (size_t p = 0; p < gray.size(); ++p)
      for (unsigned c = 0; c < components; ++c)
        source[p * components + c] = (components == 2 || components == 4) && c == components - 1
                                       ? uint8_t((p * 37 + offset * 11) % 256) : gray[p];
    SbImage native;
    SoHeightMapToNormalMap::convert(source.data(), SbVec2s(short(w), short(h)), int(components), native);
    SbVec2s actualSize; int channels = 0;
    const unsigned char *bytes = native.getValue(actualSize, channels);
    const bool nativeHasExtent = bytes && actualSize == SbVec2s(short(w), short(h)) && channels == 3;
    const bool nativeOK = nativeHasExtent &&
                          std::vector<uint8_t>(bytes, bytes + expected.size()) == expected;
    CoinRenderBumpImage core, captured;
    std::string diagnostic;
    const bool coreOK = coin_render_prepare_bump_image(source, w, h, components, core, diagnostic) &&
                        core.convertedHeight && core.width == w && core.height == h && core.rgb == expected;
    node->image.setValue(SbVec2s(short(w), short(h)), int(components), source.data());
    const bool captureOK = coin_render_capture_bump_node(*node, captured, diagnostic) &&
                          captured.convertedHeight && captured.width == w && captured.height == h && captured.rgb == expected;
    ++cases; nativeBad += !nativeOK; coreBad += !coreOK; captureBad += !captureOK;
    if ((!nativeOK || !coreOK || !captureOK) && printed++ < 4) {
      std::cerr << "height_offset_first constant=" << constant << " offset=" << offset << " extent=" << w << 'x' << h
                << " components=" << components << " native=" << nativeOK << " core=" << coreOK << " capture=" << captureOK
                << " diagnostic=" << diagnostic << '\n';
      for (size_t i = 0; i < expected.size(); ++i)
        if ((nativeHasExtent && bytes[i] != expected[i]) || (i < core.rgb.size() && core.rgb[i] != expected[i])) {
          std::cerr << "height_offset_byte index=" << i << " expected=" << unsigned(expected[i])
                    << " native=" << (nativeHasExtent ? int(bytes[i]) : -1)
                    << " core=" << (i < core.rgb.size() ? int(core.rgb[i]) : -1) << '\n';
          break;
        }
    }
  };
  const unsigned extents[][2] = {{1, 1}, {1, 5}, {5, 1}, {2, 2}, {3, 3}, {3, 5}};
  for (const auto &extent : extents)
    for (unsigned value = 0; value < 256; ++value) {
      const unsigned w = extent[0], h = extent[1];
      const std::vector<uint8_t> gray(size_t(w) * h, uint8_t(value));
      std::vector<uint8_t> expected(size_t(w) * h * 3);
      for (size_t p = 0; p < gray.size(); ++p) {
        expected[p * 3] = expected[p * 3 + 1] = 128;
        expected[p * 3 + 2] = 255;
      }
      for (unsigned components = 1; components <= 4; ++components)
        verify(gray, w, h, components, expected, value, true);
    }
  const unsigned w = 5, h = 4;
  std::vector<uint8_t> base(w * h);
  unsigned maximum = 0;
  for (unsigned y = 0; y < h; ++y)
    for (unsigned x = 0; x < w; ++x) {
      const unsigned value = 10 + (x * 9 + y * 13 + x * y * 3) % 71;
      base[y * w + x] = uint8_t(value); maximum = std::max(maximum, value);
    }
  const auto expected = oracle(base, w, h);
  for (unsigned offset = 0; offset <= 255 - maximum; ++offset) {
    auto gray = base;
    for (auto &value : gray) value = uint8_t(value + offset);
    const bool mathOK = oracle(gray, w, h) == expected;
    if (!mathOK) { ++coreBad; std::cerr << "independent integer Sobel offset identity violated\n"; }
    for (unsigned components = 1; components <= 4; ++components)
      verify(gray, w, h, components, expected, offset, false);
  }
  node->unref();
  std::cout << "height_offset_invariance cases=" << cases << " native_bad=" << nativeBad
            << " core_bad=" << coreBad << " capture_bad=" << captureBad << '\n';
  return check(cases >= 6144 && !nativeBad && !coreBad && !captureBad,
               "all 256 constant heights and every representable gradient offset preserve independent Sobel/normal encoding in native, Core and public capture");
}

bool testBumpImagePreparation() {
  bool ok = true;
  std::string diagnostic;
  CoinRenderBumpImage result;
  const std::vector<uint8_t> height = {0, 64, 128, 255};
  ok &= check(coin_render_prepare_bump_image(height, 2, 2, 1, result, diagnostic) &&
              result.convertedHeight && result.width == 2 && result.height == 2 &&
              result.rgb.size() == 12 && diagnostic.empty(),
              "height bump image must convert to RGB normals");
  SbImage oracle;
  SoHeightMapToNormalMap::convert(height.data(), SbVec2s(2, 2), 1, oracle);
  SbVec2s size;
  int components = 0;
  const unsigned char * bytes = oracle.getValue(size, components);
  ok &= check(bytes && size == SbVec2s(2, 2) && components == 3 &&
              result.rgb == std::vector<uint8_t>(bytes, bytes + 12),
              "height conversion must match CoinGL's byte oracle");
  const std::vector<uint8_t> grayRgba = {0, 0, 0, 1, 64, 64, 64, 2,
                                         128, 128, 128, 3, 255, 255, 255, 4};
  ok &= check(coin_render_prepare_bump_image(grayRgba, 2, 2, 4, result, diagnostic) &&
              result.convertedHeight && result.rgb.size() == 12,
              "grayscale RGBA bump image must be treated as height");
  SoHeightMapToNormalMap::convert(grayRgba.data(), SbVec2s(2, 2), 4, oracle);
  bytes = oracle.getValue(size, components);
  ok &= check(bytes && result.rgb == std::vector<uint8_t>(bytes, bytes + 12),
              "grayscale RGBA conversion must match CoinGL's byte oracle");
  const std::vector<uint8_t> normal = {12, 34, 56, 7, 89, 101, 112, 8};
  ok &= check(coin_render_prepare_bump_image(normal, 2, 1, 4, result, diagnostic) &&
              !result.convertedHeight &&
              result.rgb == std::vector<uint8_t>({12, 34, 56, 89, 101, 112}),
              "RGBA normal image must preserve RGB and ignore alpha");
  const std::vector<uint8_t> prior = result.rgb;
  ok &= check(!coin_render_prepare_bump_image(normal, 2, 2, 4, result, diagnostic) &&
              result.rgb == prior && !diagnostic.empty(),
              "invalid bump image must not mutate output");
  SoBumpMap * node = new SoBumpMap;
  node->ref();
  node->image.setValue(SbVec2s(2,2), 1, height.data());
  node->wrapS = SoBumpMap::CLAMP;
  node->wrapT = SoBumpMap::REPEAT;
  CoinRenderBumpImage captured;
  ok &= check(coin_render_capture_bump_node(*node, captured, diagnostic) &&
              captured.width == 2 && captured.height == 2 &&
              captured.convertedHeight && captured.clampS && !captured.clampT,
              "SoBumpMap height image and sampler wrap must capture without GL");
  SbImage nodeOracle;
  SoHeightMapToNormalMap::convert(height.data(), SbVec2s(2,2), 1, nodeOracle);
  bytes = nodeOracle.getValue(size, components);
  ok &= check(bytes && captured.rgb == std::vector<uint8_t>(bytes, bytes + 12),
              "SoBumpMap height capture must match the CoinGL converter");
  const unsigned char colored[] = {12,34,56,7, 89,101,112,8};
  node->image.setValue(SbVec2s(2,1), 4, colored);
  ok &= check(coin_render_capture_bump_node(*node, captured, diagnostic) &&
              !captured.convertedHeight &&
              captured.rgb == std::vector<uint8_t>({12,34,56,89,101,112}),
              "SoBumpMap RGBA normal capture must preserve RGB");
  const auto capturedBefore = captured.rgb;
  node->image.setValue(SbVec2s(0,0), 0, nullptr);
  node->filename.enableNotify(FALSE);
  node->filename.setValue("pending-bump-not-loaded.png");
  ok &= check(!coin_render_capture_bump_node(*node, captured, diagnostic) &&
              captured.rgb == capturedBefore && !diagnostic.empty(),
              "pending bump filename must not be mistaken for an empty image");
  node->filename.setValue("");
  node->filename.enableNotify(TRUE);
  ok &= check(coin_render_capture_bump_node(*node, captured, diagnostic) &&
              captured.rgb.empty() && captured.width == 0 && diagnostic.empty(),
              "empty SoBumpMap must clear the captured image");
  node->unref();
  return ok;
}
bool testBumpTangents() {
  bool ok = true;
  std::string diagnostic;
  std::vector<CoinRenderBumpVertex> vertices(4);
  vertices[0].position[0] = 0.0f; vertices[0].position[1] = 0.0f;
  vertices[1].position[0] = 1.0f; vertices[1].position[1] = 0.0f;
  vertices[2].position[0] = 0.0f; vertices[2].position[1] = 1.0f;
  vertices[3].position[0] = 1.0f; vertices[3].position[1] = 1.0f;
  for (auto & vertex : vertices) {
    vertex.coord[0] = vertex.position[0];
    vertex.coord[1] = vertex.position[1];
  }
  std::vector<CoinRenderBumpTangents> tangents;
  ok &= check(coin_render_legacy_bump_tangents(vertices, {0,1,2, 1,3,2},
                                                tangents, diagnostic) &&
              tangents.size() == 4 && diagnostic.empty(),
              "shared bump vertices must receive tangent pairs");
  for (const auto & tangent : tangents)
    ok &= check(std::fabs(tangent.s[0] - 1.0f) < 1e-6f &&
                std::fabs(tangent.s[1]) < 1e-6f &&
                std::fabs(tangent.t[0]) < 1e-6f &&
                std::fabs(tangent.t[1] + 1.0f) < 1e-6f,
                "tangent directions must match CoinGL's signed formula");
  vertices[3].position[2] = 1.0f;
  std::vector<CoinRenderBumpTangents> shared;
  ok &= check(coin_render_legacy_bump_tangents(vertices, {0,1,2, 1,3,2},
                                               shared, diagnostic),
              "nonplanar shared tangent oracle must compute");
  std::vector<CoinRenderBumpLegacyCorner> corners(6);
  const uint32_t cornerVertices[] = {0,1,2,1,3,2};
  for (size_t i = 0; i < corners.size(); ++i) {
    corners[i].shapeOccurrence = 1;
    corners[i].rgba = 0xff8080ffu;
    corners[i].textureCoordIndex = static_cast<int32_t>(cornerVertices[i]);
    for (int axis = 0; axis < 3; ++axis) corners[i].position[axis] =
      vertices[cornerVertices[i]].position[axis];
    for (int axis = 0; axis < 2; ++axis) {
      corners[i].textureCoord0[axis] = vertices[cornerVertices[i]].coord[axis];
      corners[i].bumpCoord[axis] = vertices[cornerVertices[i]].coord[axis];
    }
    corners[i].textureCoord0[3] = 1.0f;
    corners[i].normal[2] = 1.0f;
  }
  std::vector<CoinRenderBumpTangents> expanded;
  ok &= check(coin_render_legacy_bump_corner_tangents(corners, {0,1,2,3,4,5},
                                                       expanded, diagnostic) &&
              expanded.size() == 6 &&
              std::fabs(expanded[1].s[2] - shared[1].s[2]) < 1e-6f &&
              std::fabs(expanded[3].s[2] - shared[1].s[2]) < 1e-6f,
              "duplicated callback corners must share CoinGL tangent accumulation");
  corners[3].rgba ^= 1;
  ok &= check(coin_render_legacy_bump_corner_tangents(corners, {0,1,2,3,4,5},
                                                       expanded, diagnostic) &&
              std::fabs(expanded[1].s[2] - expanded[3].s[2]) > 1e-4f,
              "legacy color identity must prevent tangent sharing across a seam");
  vertices[2].coord[0] = vertices[1].coord[0];
  vertices[2].coord[1] = vertices[1].coord[1];
  ok &= check(coin_render_legacy_bump_tangents(vertices, {0,1,2},
                                                tangents, diagnostic) &&
              std::fabs(tangents[0].s[0]) < 1e-6f &&
              std::fabs(tangents[0].s[1]) < 1e-6f,
              "degenerate bump S direction must remain zero as in CoinGL");
  const auto before = tangents;
  ok &= check(!coin_render_legacy_bump_tangents(vertices, {0,1,4},
                                                 tangents, diagnostic) &&
              tangents.size() == before.size() &&
              tangents[0].s[0] == before[0].s[0] && !diagnostic.empty(),
              "invalid bump index must leave tangents unchanged");
  return ok;
}
bool testBumpDiffuseOracle() {
  bool ok = true;
  CoinRenderBumpTangents tangents;
  tangents.s[0] = 1.0f;
  tangents.t[1] = -1.0f;
  const float normal[3] = {0.0f, 0.0f, 1.0f};
  const float position[3] = {1.0f, 0.0f, 0.0f};
  const float directional[3] = {0.0f, -2.0f, 0.0f};
  float projected[3] = {-9.0f, -9.0f, -9.0f};
  ok &= check(coin_render_legacy_bump_light_vector(tangents, normal, position,
                                                    directional, false, projected) &&
              std::fabs(projected[0]) < 1e-6f &&
              std::fabs(projected[1] - 1.0f) < 1e-6f &&
              std::fabs(projected[2]) < 1e-6f,
              "directional bump light must project through signed CoinGL tangents");
  const uint8_t tangentY[3] = {128, 255, 128};
  const uint8_t oppositeY[3] = {128, 0, 128};
  ok &= check(coin_render_legacy_bump_dot3(tangentY, projected) > 0.99f &&
              coin_render_legacy_bump_dot3(oppositeY, projected) == 0.0f,
              "legacy DOT3 must decode the sampled tangent-space normal");
  const float tangentLight[3] = {0.0f, 0.0f, 1.0f};
  const float tangentEye[3] = {0.0f, 0.0f, 1.0f};
  const float oppositeLight[3] = {0.0f, 0.0f, -1.0f};
  const uint8_t tangentZ[3] = {128, 128, 255};
  ok &= check(coin_render_legacy_bump_specular(tangentZ, tangentLight,
                                                tangentEye, .5f) > .99f &&
              coin_render_legacy_bump_specular(tangentZ, oppositeLight,
                                                tangentEye, .5f) == 0.0f,
              "legacy specular must use the tangent-space halfway vector");
  const float angledLight[3] = {0.8f, 0.0f, 0.6f};
  const float specular = coin_render_legacy_bump_specular(tangentZ, angledLight,
                                                          tangentEye, .5f);
  ok &= check(specular > .027f && specular < .030f,
              "legacy specular must use the shininess times 64 exponent");
  const float point[3] = {1.0f, 0.0f, 2.0f};
  ok &= check(coin_render_legacy_bump_light_vector(tangents, normal, position,
                                                    point, true, projected) &&
              std::fabs(projected[2] - 1.0f) < 1e-6f,
              "point bump light must subtract each vertex position");
  const float eye[3] = {1.0f, -2.0f, 2.0f};
  float eyeProjected[3] = {-9.0f, -9.0f, -9.0f};
  ok &= check(coin_render_legacy_bump_eye_vector(tangents, normal, position,
                                                  eye, eyeProjected) &&
              std::fabs(eyeProjected[0]) < 1e-6f &&
              std::fabs(eyeProjected[1] - 0.70710678f) < 1e-6f &&
              std::fabs(eyeProjected[2] - 0.70710678f) < 1e-6f,
              "specular bump eye must project the object-space projection point");
  const float previousEye[3] = {eyeProjected[0], eyeProjected[1], eyeProjected[2]};
  const float invalidEye[3] = {NAN, 0.0f, 0.0f};
  ok &= check(!coin_render_legacy_bump_eye_vector(tangents, normal, position,
                                                   invalidEye, eyeProjected) &&
              std::memcmp(previousEye, eyeProjected, sizeof(previousEye)) == 0,
              "invalid bump eye must preserve the previous projection");
  const float invalid[3] = {NAN, 0.0f, 0.0f};
  ok &= check(!coin_render_legacy_bump_light_vector(tangents, normal, position,
                                                     invalid, false, projected) &&
              std::fabs(projected[2] - 1.0f) < 1e-6f,
              "invalid bump light must preserve the previous output");
  return ok;
}
bool testBumpCoordinateSelection() {
  bool ok = true;
  std::string diagnostic;
  CoinRenderBumpCoord output;
  const CoinRenderBumpCoord primitive{0.25f, 0.75f};
  const std::vector<CoinRenderBumpCoord> authored{{0.1f, 0.2f}, {0.8f, 0.9f}};
  ok &= check(coin_render_select_bump_coord(primitive, {}, true, 7, output, diagnostic) &&
              output.s == primitive.s && output.t == primitive.t,
              "missing authored bump UV must use primitive unit-0 UV");
  ok &= check(coin_render_select_bump_coord(primitive, authored, true, 1, output, diagnostic) &&
              output.s == authored[1].s && output.t == authored[1].t,
              "authored bump UV must use texture-coordinate index");
  ok &= check(coin_render_select_bump_coord(primitive, authored, true, 99, output, diagnostic) &&
              output.s == authored[1].s,
              "out-of-range bump UV index must clamp like CoinGL");
  ok &= check(coin_render_select_bump_coord(primitive, authored, true, -1, output, diagnostic) &&
              output.s == authored[0].s,
              "negative bump UV index must clamp like CoinGL");
  ok &= check(coin_render_select_bump_coord(primitive, authored, false, -1, output, diagnostic) &&
              output.s == primitive.s && output.t == primitive.t,
              "authored UV without a point detail must leave primitive UV intact");
  const CoinRenderBumpCoord before = output;
  const CoinRenderBumpCoord invalid{std::numeric_limits<float>::quiet_NaN(), 0.0f};
  ok &= check(!coin_render_select_bump_coord(primitive, {invalid}, true, 0, output, diagnostic) &&
              output.s == before.s && output.t == before.t && !diagnostic.empty(),
              "invalid bump UV must leave output unchanged");
  return ok;
}
bool testTriangleCornerDetailIndices() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoCoordinate3 * coordinates = new SoCoordinate3;
  const SbVec3f positions[] = {SbVec3f(0,0,0), SbVec3f(1,0,0), SbVec3f(0,1,0)};
  coordinates->point.setValues(0, 3, positions);
  root->addChild(coordinates);
  SoTextureCoordinate2 * textureCoordinates = new SoTextureCoordinate2;
  const SbVec2f uv[] = {SbVec2f(0,0), SbVec2f(1,0), SbVec2f(0,1)};
  textureCoordinates->point.setValues(0, 3, uv);
  root->addChild(textureCoordinates);
  SoTextureCoordinateBinding * binding = new SoTextureCoordinateBinding;
  binding->value = SoTextureCoordinateBinding::PER_VERTEX_INDEXED;
  root->addChild(binding);
  SoTexture2 * texture = new SoTexture2;
  const unsigned char texel[] = {255, 255, 255};
  texture->image.setValue(SbVec2s(1, 1), 3, texel);
  root->addChild(texture);
  SoBumpMapCoordinate * bumpCoords = new SoBumpMapCoordinate;
  const SbVec2f bumpUvValues[] = {SbVec2f(.1f,.2f), SbVec2f(.3f,.4f), SbVec2f(.5f,.6f)};
  bumpCoords->point.setValues(0, 3, bumpUvValues);
  root->addChild(bumpCoords);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, -1};
  const int32_t textureIndices[] = {2, 0, 1, -1};
  face->coordIndex.setValues(0, 4, indices);
  face->textureCoordIndex.setValues(0, 4, textureIndices);
  root->addChild(face);
  struct Receipt { bool seen = false; bool exact = true; bool expectIndices = true; } receipt;
  SoCallbackAction action;
  action.addTriangleCallback(SoIndexedFaceSet::getClassTypeId(),
    [](void * userdata, SoCallbackAction * callback, const SoPrimitiveVertex * a,
       const SoPrimitiveVertex * b, const SoPrimitiveVertex * c) {
      auto * result = static_cast<Receipt *>(userdata);
      result->seen = true;
      const SoPrimitiveVertex * corners[] = {a, b, c};
      for (int corner = 0; corner < 3; ++corner) {
        const int index = callback->getCurrentTrianglePointDetailIndex(corner);
        if (callback->getCurrentTrianglePointDetailIndex(corners[corner]) != index)
          result->exact = false;
        CoinRenderBumpCoord bumpUv;
        std::string diagnostic;
        if (!coin_render_capture_bump_coord(callback, corners[corner], bumpUv, diagnostic))
          result->exact = false;
        if (!result->expectIndices) {
          if (index != -1) result->exact = false;
          const SbVec4f & primitiveUv = corners[corner]->getTextureCoords();
          if (bumpUv.s != primitiveUv[0] || bumpUv.t != primitiveUv[1])
            result->exact = false;
          continue;
        }
        const SoDetail * detail = corners[corner]->getDetail();
        if (!detail || !detail->isOfType(SoFaceDetail::getClassTypeId()) ||
            index < 0 || index >= 3) { result->exact = false; continue; }
        const auto * point = static_cast<const SoFaceDetail *>(detail)->getPoint(index);
        const int coordinate = point->getCoordinateIndex();
        const int expectedTexture[] = {2, 0, 1};
        if (coordinate < 0 || coordinate >= 3 ||
            point->getTextureCoordIndex() != expectedTexture[coordinate])
          result->exact = false;
        const SbVec2f expected[] = {SbVec2f(.1f,.2f), SbVec2f(.3f,.4f), SbVec2f(.5f,.6f)};
        if (bumpUv.s != expected[expectedTexture[coordinate]][0] ||
            bumpUv.t != expected[expectedTexture[coordinate]][1])
          result->exact = false;
      }
    }, &receipt);
  action.apply(root);
  const bool indexed = receipt.seen && receipt.exact;
  root->removeChild(bumpCoords);
  receipt.seen = false;
  receipt.exact = true;
  receipt.expectIndices = false;
  action.apply(root);
  const bool skipped = receipt.seen && receipt.exact;
  root->unref();
  return check(indexed && skipped,
               "callback corner indices must be exact when needed and absent otherwise");
}
bool testBumpUvPlanCapture() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoCoordinate3 * coordinates = new SoCoordinate3;
  const SbVec3f positions[] = {SbVec3f(0,0,0), SbVec3f(1,0,0), SbVec3f(0,1,0)};
  coordinates->point.setValues(0, 3, positions);
  root->addChild(coordinates);
  SoTextureCoordinate2 * textureCoordinates = new SoTextureCoordinate2;
  const SbVec2f uv[] = {SbVec2f(0,0), SbVec2f(1,0), SbVec2f(0,1)};
  textureCoordinates->point.setValues(0, 3, uv);
  root->addChild(textureCoordinates);
  SoTexture2 * texture = new SoTexture2;
  const unsigned char texel[] = {255,255,255};
  texture->image.setValue(SbVec2s(1,1), 3, texel);
  root->addChild(texture);
  SoBumpMapCoordinate * bumpCoords = new SoBumpMapCoordinate;
  const SbVec2f authored[] = {SbVec2f(.1f,.2f), SbVec2f(.3f,.4f), SbVec2f(.5f,.6f)};
  bumpCoords->point.setValues(0, 3, authored);
  root->addChild(bumpCoords);
  SoBumpMap * bumpMap = new SoBumpMap;
  const unsigned char normal[] = {12,34,56, 78,90,123};
  bumpMap->image.setValue(SbVec2s(2,1), 3, normal);
  bumpMap->wrapS = SoBumpMap::CLAMP;
  root->addChild(bumpMap);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  const int32_t indices[] = {0,1,2,-1};
  const int32_t textureIndices[] = {2,0,1,-1};
  face->coordIndex.setValues(0, 4, indices);
  face->textureCoordIndex.setValues(0, 4, textureIndices);
  root->addChild(face);
  CoinRenderFramePlanBuilder builder;
  builder.beginFrame(SbColor4f(0,0,0,1), SbViewportRegion(32,32));
  CoinRenderAction action(SbViewportRegion(32,32));
  action.addPreCallback(SoIndexedFaceSet::getClassTypeId(),
    [](void * data, SoCallbackAction * callback, const SoNode * node) {
      static_cast<CoinRenderFramePlanBuilder *>(data)->beginShape(callback, node);
      return SoCallbackAction::CONTINUE;
    }, &builder);
  action.addPostCallback(SoIndexedFaceSet::getClassTypeId(),
    [](void * data, SoCallbackAction *, const SoNode *) {
      static_cast<CoinRenderFramePlanBuilder *>(data)->endShape();
      return SoCallbackAction::CONTINUE;
    }, &builder);
  action.addTriangleCallback(SoIndexedFaceSet::getClassTypeId(),
    [](void * data, SoCallbackAction * callback, const SoPrimitiveVertex * a,
       const SoPrimitiveVertex * b, const SoPrimitiveVertex * c) {
      static_cast<CoinRenderFramePlanBuilder *>(data)->addTriangle(callback, a, b, c);
    }, &builder);
  action.apply(root);
  CoinRenderFramePlan plan;
  std::string diagnostic;
  bool ok = check(builder.build(plan, &diagnostic) && plan.bumpCoords.size() == 3 &&
                  plan.bumpTangents.size() == 3 &&
                  plan.bumpImages.size() == 1 &&
                  plan.bumpImages[0].rgb == std::vector<uint8_t>(normal, normal + 6) &&
                  plan.bumpImages[0].clampS,
                  "indexed face must bind the active bump image and three sparse UV overrides");
  if (ok) {
    const int expectedIndex[] = {2,0,1};
    for (const auto & override : plan.bumpCoords) {
      const int vertex = static_cast<int>(override.vertexIndex);
      ok &= check(vertex >= 0 && vertex < 3 &&
                  override.imageSlot == 1 &&
                  override.coord[0] == authored[expectedIndex[vertex]][0] &&
                  override.coord[1] == authored[expectedIndex[vertex]][1],
                  "sparse bump UV must follow the exact texture index per corner");
    }
    CoinRenderFramePlan changed = plan;
    changed.bumpCoords[0].coord[0] += .125f;
    ok &= check(!changed.hasSamePayload(plan),
                "changing authored bump UV must invalidate frame payload equality");
    changed = plan;
    changed.bumpCoords[0].vertexIndex = static_cast<uint32_t>(changed.vertices.size());
    ok &= check(!changed.isValid(&diagnostic) &&
                diagnostic == "Invalid sparse bump coordinates",
                "out-of-range bump UV override must fail plan validation");
    changed = plan;
    changed.bumpCoords[0].imageSlot = 2;
    ok &= check(!changed.isValid(&diagnostic) &&
                diagnostic == "Invalid sparse bump coordinates",
                "out-of-range bump image slot must fail plan validation");
    changed = plan;
    changed.bumpImages[0].rgb[0] ^= 1;
    ok &= check(!changed.hasSamePayload(plan),
                "changing the active bump image must invalidate frame payload equality");
    changed = plan;
    changed.bumpTangents[0].tangent.s[0] += .125f;
    ok &= check(!changed.hasSamePayload(plan),
                "changing a captured bump tangent must invalidate frame payload equality");
    changed = plan;
    changed.bumpTangents[0].vertexIndex = static_cast<uint32_t>(changed.vertices.size());
    ok &= check(!changed.isValid(&diagnostic) &&
                diagnostic == "Invalid sparse bump tangents",
                "out-of-range bump tangent must fail plan validation");
    changed = plan;
    changed.bumpCoords[0].imageSlot = 0;
    ok &= check(!changed.isValid(&diagnostic) &&
                diagnostic == "Invalid sparse bump tangents",
                "bump tangent must reference an active image coordinate");
    changed = plan;
    changed.bumpCoords[0].coord[1] = std::numeric_limits<float>::quiet_NaN();
    ok &= check(!changed.isValid(&diagnostic) &&
                diagnostic == "Invalid sparse bump coordinates",
                "nonfinite bump UV override must fail plan validation");
  }
  SoBumpMapTransform * transform = new SoBumpMapTransform;
  transform->translation = SbVec2f(.25f, .5f);
  root->insertChild(transform, root->findChild(face));
  builder.beginFrame(SbColor4f(0,0,0,1), SbViewportRegion(32,32));
  action.apply(root);
  CoinRenderFramePlan transformed;
  ok &= check(builder.build(transformed, &diagnostic) &&
              transformed.bumpTransforms.size() == 1 &&
              transformed.bumpCoords.size() == 3 &&
              transformed.bumpCoords[0].transformSlot == 1 &&
              transformed.bumpTransforms[0][3][0] == .25f &&
              transformed.bumpTransforms[0][3][1] == .5f &&
              transformed.bumpCoords[0].coord[0] == plan.bumpCoords[0].coord[0],
              "bump transform must be captured separately from tangent UVs");
  if (transformed.bumpTransforms.size() == 1) {
    std::vector<CoinRenderBumpGpuVertex> stream;
    ok &= check(coin_render_build_bump_gpu_stream(transformed, stream, diagnostic) &&
                stream.size() == transformed.vertices.size() && diagnostic.empty(),
                "active bump must produce a vertex-aligned GPU stream");
    if (stream.size() == transformed.vertices.size()) {
      for (const auto & coord : transformed.bumpCoords) {
        const auto & gpu = stream[coord.vertexIndex];
        const auto & tangent = transformed.bumpTangents[coord.vertexIndex].tangent;
        ok &= check(gpu.imageSlot == coord.imageSlot &&
                    std::fabs(gpu.coord[0] - coord.coord[0] - .25f) < 1e-6f &&
                    std::fabs(gpu.coord[1] - coord.coord[1] - .5f) < 1e-6f &&
                    gpu.coord[2] == 0.0f && gpu.coord[3] == 1.0f &&
                    std::memcmp(gpu.tangentS, tangent.s, sizeof(gpu.tangentS)) == 0 &&
                    std::memcmp(gpu.tangentT, tangent.t, sizeof(gpu.tangentT)) == 0,
                    "GPU bump stream must apply the matrix after tangent capture");
      }
      const std::vector<uint32_t> emittedSources = {2, 0, 2, 1};
      std::vector<CoinRenderBumpGpuVertex> emitted;
      ok &= check(coin_render_remap_bump_gpu_stream(stream, emittedSources,
                                                     emitted, diagnostic) &&
                  emitted.size() == emittedSources.size() && diagnostic.empty() &&
                  std::memcmp(&emitted[0], &stream[2], sizeof(stream[2])) == 0 &&
                  std::memcmp(&emitted[1], &stream[0], sizeof(stream[0])) == 0 &&
                  std::memcmp(&emitted[2], &stream[2], sizeof(stream[2])) == 0 &&
                  std::memcmp(&emitted[3], &stream[1], sizeof(stream[1])) == 0,
                  "expanded bump vertices must retain their original UV, tangent and image slot");
      const auto previousEmitted = emitted;
      ok &= check(!coin_render_remap_bump_gpu_stream(stream,
                    {0, static_cast<uint32_t>(stream.size())}, emitted, diagnostic) &&
                  diagnostic == "Bump emitted vertex has no source vertex" &&
                  emitted.size() == previousEmitted.size() &&
                  std::memcmp(emitted.data(), previousEmitted.data(),
                              emitted.size() * sizeof(emitted[0])) == 0,
                  "invalid emitted bump mapping must preserve the previous packet");
    }
    CoinRenderFramePlan missingTangent = transformed;
    missingTangent.bumpTangents.pop_back();
    const auto prior = stream;
    ok &= check(!coin_render_build_bump_gpu_stream(missingTangent, stream, diagnostic) &&
                diagnostic == "Missing tangent for active bump vertex" &&
                stream.size() == prior.size() &&
                std::memcmp(stream.data(), prior.data(), prior.size() * sizeof(prior[0])) == 0,
                "invalid GPU bump stream must leave the previous stream intact");
    CoinRenderFramePlan changed = transformed;
    changed.bumpTransforms[0][3][0] += .125f;
    ok &= check(!changed.hasSamePayload(transformed),
                "bump transform must invalidate frame payload equality");
    changed = transformed;
    changed.bumpCoords[0].transformSlot = 2;
    ok &= check(!changed.isValid(&diagnostic) &&
                diagnostic == "Invalid sparse bump coordinates",
                "out-of-range bump transform slot must fail validation");
    changed = transformed;
    changed.bumpTransforms[0][0][0] = std::numeric_limits<float>::quiet_NaN();
    ok &= check(!changed.isValid(&diagnostic) &&
                diagnostic == "Invalid bump transform snapshot",
                "nonfinite bump transform must fail validation");
  }
  // Keep the node alive while moving it between child slots: removeChild()
  // drops the group's sole reference before insertChild() acquires a new one.
  transform->ref();
  root->removeChild(transform);
  root->insertChild(transform, root->findChild(face));
  transform->unref();
  SoBumpMap * alternateMap = new SoBumpMap;
  const unsigned char alternateNormal[] = {160,128,255};
  alternateMap->image.setValue(SbVec2s(1,1), 3, alternateNormal);
  SoBumpMapTransform * alternateTransform = new SoBumpMapTransform;
  alternateTransform->translation = SbVec2f(.75f, .125f);
  SoSeparator * nested = new SoSeparator;
  nested->addChild(alternateMap);
  nested->addChild(alternateTransform);
  nested->addChild(face);
  root->addChild(nested);
  root->addChild(face);
  builder.beginFrame(SbColor4f(0,0,0,1), SbViewportRegion(32,32));
  action.apply(root);
  CoinRenderFramePlan alternatingMaps;
  ok &= check(builder.build(alternatingMaps, &diagnostic) &&
              alternatingMaps.bumpImages.size() == 2 &&
              alternatingMaps.bumpTransforms.size() == 2 &&
              alternatingMaps.bumpCoords.size() == 9 &&
              alternatingMaps.bumpTangents.size() == 9 &&
              alternatingMaps.bumpCoords[0].imageSlot == 1 &&
              alternatingMaps.bumpCoords[0].transformSlot == 1 &&
              alternatingMaps.bumpCoords[3].imageSlot == 2 &&
              alternatingMaps.bumpCoords[3].transformSlot == 2 &&
              alternatingMaps.bumpCoords[6].imageSlot == 1 &&
              alternatingMaps.bumpCoords[6].transformSlot == 1,
              "separator-scoped A/B/A bump occurrences must restore image and transform slots");
  for (int i = 0; i < 2; ++i) root->removeChild(root->getNumChildren() - 1);
  root->removeChild(transform);
  root->removeChild(bumpMap);
  root->removeChild(bumpCoords);
  builder.beginFrame(SbColor4f(0,0,0,1), SbViewportRegion(32,32));
  action.apply(root);
  CoinRenderFramePlan withoutBumpCoords;
  ok &= check(builder.build(withoutBumpCoords, &diagnostic) &&
              withoutBumpCoords.bumpCoords.empty() && withoutBumpCoords.bumpTangents.empty() &&
              withoutBumpCoords.bumpImages.empty(),
              "removing the bump nodes must clear the optional plan payload");
  std::vector<CoinRenderBumpGpuVertex> inactiveStream(1);
  ok &= check(coin_render_build_bump_gpu_stream(withoutBumpCoords, inactiveStream,
                                                  diagnostic) && inactiveStream.empty(),
              "ordinary frames must not allocate a bump GPU stream");
  ok &= check(plan.vertices.size() == withoutBumpCoords.vertices.size() &&
              std::memcmp(plan.vertices.data(), withoutBumpCoords.vertices.data(),
                          plan.vertices.size() * sizeof(CoinRenderVertexSnapshot)) == 0,
              "authored bump UVs must not change the fixed GPU vertex layout");
  root->unref();
  return ok;
}
bool testBumpSharedCornerPlanCapture() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoCoordinate3 * positions = new SoCoordinate3;
  const SbVec3f points[] = {SbVec3f(0,0,0), SbVec3f(1,0,0),
                            SbVec3f(0,1,0), SbVec3f(1,1,0)};
  positions->point.setValues(0, 4, points);
  root->addChild(positions);
  const SbVec2f uv[] = {SbVec2f(0,0), SbVec2f(1,0), SbVec2f(0,1),
                        SbVec2f(2,2), SbVec2f(1,0)};
  SoTextureCoordinate2 * texcoords = new SoTextureCoordinate2;
  texcoords->point.setValues(0, 5, uv);
  root->addChild(texcoords);
  SoTexture2 * texture = new SoTexture2;
  const unsigned char white[] = {255,255,255};
  texture->image.setValue(SbVec2s(1,1), 3, white);
  root->addChild(texture);
  SoBumpMapCoordinate * authored = new SoBumpMapCoordinate;
  authored->point.setValues(0, 5, uv);
  root->addChild(authored);
  SoBumpMap * image = new SoBumpMap;
  const unsigned char neutral[] = {128,128,255};
  image->image.setValue(SbVec2s(1,1), 3, neutral);
  root->addChild(image);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  const int32_t coordIndex[] = {0,1,2,-1, 1,3,2,-1};
  const int32_t textureIndex[] = {0,1,2,-1, 1,3,2,-1};
  face->coordIndex.setValues(0, 8, coordIndex);
  face->textureCoordIndex.setValues(0, 8, textureIndex);
  root->addChild(face);
  CoinRenderFramePlanBuilder builder;
  CoinRenderAction action(SbViewportRegion(32,32));
  action.addPreCallback(SoIndexedFaceSet::getClassTypeId(),
    [](void * data, SoCallbackAction * callback, const SoNode * node) {
      static_cast<CoinRenderFramePlanBuilder *>(data)->beginShape(callback, node);
      return SoCallbackAction::CONTINUE;
    }, &builder);
  action.addPostCallback(SoIndexedFaceSet::getClassTypeId(),
    [](void * data, SoCallbackAction *, const SoNode *) {
      static_cast<CoinRenderFramePlanBuilder *>(data)->endShape();
      return SoCallbackAction::CONTINUE;
    }, &builder);
  action.addTriangleCallback(SoIndexedFaceSet::getClassTypeId(),
    [](void * data, SoCallbackAction * callback, const SoPrimitiveVertex * a,
       const SoPrimitiveVertex * b, const SoPrimitiveVertex * c) {
      static_cast<CoinRenderFramePlanBuilder *>(data)->addTriangle(callback, a, b, c);
    }, &builder);
  auto capture = [&](CoinRenderFramePlan & plan) {
    builder.beginFrame(SbColor4f(0,0,0,1), SbViewportRegion(32,32));
    action.apply(root);
    std::string diagnostic;
    return builder.build(plan, &diagnostic);
  };
  CoinRenderFramePlan joined;
  bool ok = check(capture(joined) && joined.bumpTangents.size() == 6 &&
                  std::fabs(joined.bumpTangents[1].tangent.s[0] -
                            joined.bumpTangents[3].tangent.s[0]) < 1e-6f &&
                  std::fabs(joined.bumpTangents[1].tangent.s[1] -
                            joined.bumpTangents[3].tangent.s[1]) < 1e-6f,
                  "two callback triangles must share CoinGL tangents at common corners");
  const int32_t seamIndex[] = {0,1,2,-1, 4,3,2,-1};
  face->textureCoordIndex.setValues(0, 8, seamIndex);
  CoinRenderFramePlan split;
  ok &= check(capture(split) && split.bumpTangents.size() == 6 &&
              std::fabs(split.bumpTangents[1].tangent.s[1] -
                        split.bumpTangents[3].tangent.s[1]) > 1e-4f,
              "equal UV values with distinct authored indices must split CoinGL tangents");
  root->removeChild(texture);
  CoinRenderFramePlan withoutBase;
  bool capturedWithoutBase = capture(withoutBase) && withoutBase.bumpCoords.size() == 6;
  if (capturedWithoutBase)
    for (const auto & coord : withoutBase.bumpCoords)
      capturedWithoutBase &= coord.coord[0] == uv[0][0] && coord.coord[1] == uv[0][1];
  ok &= check(capturedWithoutBase,
              "without a base texture, authored bump UVs must select entry zero as CoinGL does");
  root->unref();
  return ok;
}
bool testLegacyColorConversion() {
  const uint8_t rgba[] = {255,0,0,7, 0,255,0,9,
                          0,0,255,11, 10,20,30,13};
  struct Case { CoinRenderLegacyComponents component; std::vector<uint8_t> expected; };
  const Case cases[] = {
    {CoinRenderLegacyComponents::LUMINANCE, {28,18,76,150}},
    {CoinRenderLegacyComponents::LUMINANCE_TRANSPARENCY, {28,11,18,13,76,7,150,9}},
    {CoinRenderLegacyComponents::RGB, {0,0,255,10,20,30,255,0,0,0,255,0}},
    {CoinRenderLegacyComponents::RGB_TRANSPARENCY,
      {0,0,255,11,10,20,30,13,255,0,0,7,0,255,0,9}}
  };
  bool ok = true;
  for (const Case & item : cases) {
    std::vector<uint8_t> actual;
    ok &= check(coin_render_convert_legacy_components(rgba, 2, 2,
                                                       item.component, actual) &&
                actual == item.expected,
                "legacy component conversion differs from bottom-up CoinGL layout");
    std::vector<uint8_t> encoded;
    const unsigned channels = static_cast<unsigned>(item.component);
    ok &= check(coin_render_encode_legacy_rgb(actual.data(), 2, 2,
                                               item.component, encoded) &&
                encoded.size() == 512u + 4u * channels &&
                encoded[0] == 1 && encoded[1] == 218 &&
                encoded[5] == (channels == 1 ? 2 : 3) &&
                encoded[7] == 2 && encoded[9] == 2 &&
                encoded[11] == channels && encoded[19] == 255,
                "SGI RGB header differs from CoinGL");
    for (unsigned channel = 0; channel < channels; ++channel)
      for (unsigned pixel = 0; pixel < 4; ++pixel)
        ok &= check(encoded[512u + channel * 4u + pixel] ==
                    actual[pixel * channels + channel],
                    "SGI RGB planar data differs from CoinGL");
  }
  std::vector<uint8_t> output(1, 42);
  ok &= check(!coin_render_convert_legacy_components(rgba, 2, 2,
              static_cast<CoinRenderLegacyComponents>(0), output) && output.empty(),
              "invalid component format must fail and clear output");
  return ok;
}
bool
check(bool condition, const char * message)
{
  if (!condition) std::cerr << "CoinRenderFrameCoreTest: " << message << '\n';
  return condition;
}

CoinRenderFramePlan validPlan() {
  CoinRenderFramePlan plan;
  plan.vertices.resize(3);
  plan.indices = {0, 1, 2};
  plan.materials.emplace_back();
  plan.lightingStates.emplace_back();
  plan.cameras.emplace_back();
  plan.viewports.emplace_back();
  plan.viewports[0].width = plan.viewports[0].height = 1;
  plan.renderStates.emplace_back();
  plan.draws.emplace_back();
  plan.draws[0].geometry.vertexCount = plan.draws[0].geometry.indexCount = 3;
  return plan;
}

bool testAuthoredSceneLayerIdentity() {
  auto plan=validPlan();
  plan.revision=9041;
  plan.draws.resize(3,plan.draws[0]);
  plan.renderStates.resize(3,plan.renderStates[0]);
  plan.materials[0].transparency=.5f;
  plan.materials[0].diffuse[3]=.5f;
  plan.transparency.mode=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;
  for(size_t i=0;i<3;++i){
    plan.draws[i].renderLayer=uint32_t(i+1); // background, authored, foreground
    plan.draws[i].drawOrdinal=uint32_t(i);
    plan.draws[i].renderStateSlot=uint32_t(i);
    plan.renderStates[i].transparencyType=SoGLRenderAction::SORTED_OBJECT_BLEND;
  }
  plan.draws[1].authoredScene=true;
  std::string diagnostic;
  std::vector<CoinRenderCompositionItem> order;
  bool ok=check(plan.isValid(&diagnostic)&&
    coin_render_scene_layers_valid(plan,diagnostic)&&
    coin_render_composition_order(plan,order,diagnostic),
    "remapped authored scene between background and foreground is valid");
  if(ok){
    bool sceneWeighted=false,otherObject=true;
    for(const auto &item:order){
      if(item.drawIndex==1)sceneWeighted|=
        item.transparencyStrategy==CoinRenderCompositionItem::WEIGHTED_OIT;
      else otherObject&=item.transparencyStrategy==CoinRenderCompositionItem::OBJECT;
    }
    ok&=check(order.size()==3&&sceneWeighted&&otherObject,
      "only explicitly authored remapped layer receives weighted strategy");
  }
  const auto valid=plan;
  auto invalid=valid;
  invalid.draws[2].renderLayer=2;
  ok&=check(!coin_render_scene_layers_valid(invalid,diagnostic),
    "one layer cannot mix authored and non-authored occurrences");
  invalid=valid;invalid.draws[0].authoredScene=true;
  ok&=check(!coin_render_scene_layers_valid(invalid,diagnostic),
    "background cannot also claim a second authored scene identity");
  invalid=valid;invalid.draws[0].renderLayer=0;
  invalid.draws[1].renderLayer=0;
  ok&=check(!coin_render_scene_layers_valid(invalid,diagnostic),
    "implicit layer zero and explicit authored identity cannot mix");
  auto changed=valid;changed.draws[1].authoredScene=false;
  ok&=check(!valid.hasSamePayload(changed),
    "same-revision authored identity change invalidates payload equality");
  changed=valid;changed.draws[1].renderLayer=4;
  ok&=check(!valid.hasSamePayload(changed),
    "same-revision authored layer change invalidates payload equality");
  changed=valid;changed.draws[1].clearDepthBefore=true;
  ok&=check(!valid.hasSamePayload(changed),
    "same-revision layer depth barrier change invalidates payload equality");
  ok&=check(valid.hasSamePayload(plan),
    "valid authored scene packet remains reusable after invalid probes");
  return ok;
}

CoinRenderTextureCombineSnapshot activeCombine(float constant = .25f) {
  CoinRenderTextureCombineSnapshot program;
  program.instructions[0][0] = 1;
  program.instructions[0][1] = program.instructions[0][2] = 1;
  program.instructions[1][3] = program.instructions[2][3] = 1;
  for (int c = 0; c < 3; ++c) {
    program.instructions[1][c] = float(c);
    program.instructions[2][c] = float(8 + c);
    program.instructions[3][c] = constant;
  }
  program.instructions[3][3] = 1;
  return program;
}
CoinRenderFramePlan repeatedCombinePlan(bool active = false) {
  auto plan = validPlan();
  plan.revision = 151;
  plan.renderStates.resize(16, plan.renderStates[0]);
  // Only state zero is drawn. Every subsequent state remains part of Common
  // validation and deliberately repeats its predecessors' combine bytes.
  if (active)
    for (auto & state : plan.renderStates)
      for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit)
        state.textureCombines[unit] = activeCombine(float(unit) / 16);
  return plan;
}
void enableTextureUnit(CoinRenderFramePlan & plan, size_t stateSlot, size_t unit) {
  if (plan.textures.empty()) {
    CoinRenderTextureImageSnapshot image;
    image.width = image.height = 1; image.components = 4;
    image.pixelsRgba = {255, 255, 255, 255};
    plan.textures.push_back(image); plan.samplers.emplace_back();
  }
  auto & state = plan.renderStates[stateSlot];
  if (unit == 0) state.hasTexture = true;
  else state.extraTextures[unit - 1].enabled = true;
}
bool combineValidationOracle(const CoinRenderFramePlan & plan, bool expected,
                             const char * diagnostic, const char * label) {
  const std::string sentinel = "unchanged successful validation diagnostic";
  std::string literalDiagnostic = sentinel, memoDiagnostic = sentinel;
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO", "1");
  const bool literal = plan.isValid(&literalDiagnostic);
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO", "0");
  const bool memo = plan.isValid(&memoDiagnostic);
  const bool ok = literal == expected && memo == literal && memoDiagnostic == literalDiagnostic &&
    literalDiagnostic == (diagnostic ? std::string(diagnostic) : sentinel);
  if (!ok) std::cerr << label << ": literal=" << literal << " (" << literalDiagnostic << ") memo="
                    << memo << " (" << memoDiagnostic << ")\n";
  return check(ok, label);
}
bool testCombineValidationMemo() {
  struct Environment {
    std::string value; bool present;
    Environment() {
      const char * previous = std::getenv("COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO");
      present = previous != nullptr; value = previous ? previous : "";
    }
    ~Environment() { coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO", present ? value.c_str() : nullptr); }
  } environment;
  bool ok = combineValidationOracle(repeatedCombinePlan(), true, nullptr,
                                   "repeated inactive programs remain valid in referenced and unused states");
  ok &= combineValidationOracle(repeatedCombinePlan(true), true, nullptr,
                                "active programs remain validated even with disabled texture units");
  const float invalid[] = {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
  for (bool active : {false, true}) {
    const auto base = repeatedCombinePlan(active);
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      for (size_t field = 0; field < 16; ++field) {
        for (const float value : invalid) {
          auto changed = base;
          changed.renderStates.back().textureCombines[unit].instructions[field / 4][field % 4] = value;
          ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
            "every program float in every unit rejects NaN/Inf after earlier identical valid programs");
        }
        auto signedZero = repeatedCombinePlan();
        signedZero.renderStates.back().textureCombines[unit].instructions[field / 4][field % 4] = -0.0f;
        ok &= combineValidationOracle(signedZero, true, nullptr, "all program positions preserve signed-zero acceptance");
      }
    }
  }
  auto changed = repeatedCombinePlan(true);
  for (size_t state = 0; state < changed.renderStates.size(); ++state)
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      changed.renderStates[state].textureCombines[unit] = activeCombine(state % 3 == 1 ? .75f : .25f);
      if (state % 2) enableTextureUnit(changed, state, unit);
    }
  ok &= combineValidationOracle(changed, true, nullptr,
      "A/B/A program values and changing texture enablement preserve ordinary validation");

  const struct InvalidProgramField { int row, column; float value; } badFields[] = {
    {0, 0, 2}, {0, 1, 1.5f}, {0, 2, 6}, {1, 3, 3}, {2, 0, 7}, {3, 0, 1.01f}
  };
  for (const auto & field : badFields) {
    changed = repeatedCombinePlan(true);
    changed.renderStates.back().textureCombines[7].instructions[field.row][field.column] = field.value;
    ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
        "finite invalid active instructions cannot inherit a previous valid program");
  }
  changed = repeatedCombinePlan();
  for (auto & state : changed.renderStates) state.textureCombines[3].instructions[2][0] = -200;
  ok &= combineValidationOracle(changed, true, nullptr,
      "inactive finite instructions retain the original unrestricted representation");
  changed.renderStates.back().textureCombines[3].instructions[0][0] = 1;
  ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
      "changing only activation invalidates an earlier inactive-program result");

  // All enablement-dependent checks still execute after a valid combine hit,
  // including those in states with no draw and units after unit zero.
  for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    changed = repeatedCombinePlan(true); enableTextureUnit(changed, 15, unit);
    ok &= combineValidationOracle(changed, true, nullptr, "enabled texture units remain valid after repeated active programs");
    if (unit == 0) changed.renderStates.back().textureImageSlot = 1;
    else changed.renderStates.back().extraTextures[unit - 1].imageSlot = 1;
    ok &= combineValidationOracle(changed, false, "RenderState references out-of-bounds texture image slot",
        "combine cache hits do not bypass image bounds in unused states");
    changed = repeatedCombinePlan(); enableTextureUnit(changed, 15, unit);
    if (unit == 0) changed.renderStates.back().samplerSlot = 1;
    else changed.renderStates.back().extraTextures[unit - 1].samplerSlot = 1;
    ok &= combineValidationOracle(changed, false, "RenderState references out-of-bounds sampler slot",
        "inactive program hits do not bypass enabled-unit sampler bounds");
    changed = repeatedCombinePlan(true); enableTextureUnit(changed, 15, unit);
    if (unit == 0) changed.renderStates.back().textureMatrix[0][0] = invalid[0];
    else changed.renderStates.back().extraTextures[unit - 1].matrix[0][0] = invalid[0];
    ok &= combineValidationOracle(changed, false, "RenderState contains non-finite texture matrix",
        "combine cache hits do not bypass enabled-unit matrix validation");
    changed = repeatedCombinePlan(true); enableTextureUnit(changed, 15, unit);
    if (unit == 0) changed.renderStates.back().textureModel = static_cast<CoinRenderTextureModel>(99);
    else changed.renderStates.back().extraTextures[unit - 1].model = static_cast<CoinRenderTextureModel>(99);
    ok &= combineValidationOracle(changed, false, "RenderState contains unsupported texture model",
        "combine cache hits do not bypass enabled-unit model validation");
  }

  changed = repeatedCombinePlan();
  changed.renderStates.back().textureCombines[0].instructions[3][0] = invalid[0];
  changed.renderStates.back().viewportSlot = 1;
  ok &= combineValidationOracle(changed, false, "RenderState references out-of-bounds viewport slot",
      "an earlier state property retains precedence over an invalid combine in that state");
  changed = repeatedCombinePlan();
  changed.renderStates.back().textureCombines[0].instructions[3][0] = invalid[0];
  changed.renderStates.back().fogColor[0] = invalid[1];
  ok &= combineValidationOracle(changed, false, "Invalid fog color",
      "fog validation retains precedence over an invalid combine in the same state");
  changed = repeatedCombinePlan();
  changed.renderStates[1].textureCombines[7].instructions[3][0] = invalid[0];
  changed.renderStates.back().viewportSlot = 1;
  ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
      "an earlier invalid combine retains precedence over a later invalid unused state");
  changed = repeatedCombinePlan(); enableTextureUnit(changed, 15, 0);
  changed.renderStates.back().textureImageSlot = 1;
  changed.renderStates.back().textureCombines[1].instructions[3][0] = invalid[0];
  ok &= combineValidationOracle(changed, false, "RenderState references out-of-bounds texture image slot",
      "earlier-unit image failure retains precedence over a later-unit invalid combine");
  changed.renderStates.back().textureCombines[0].instructions[3][0] = invalid[0];
  ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
      "same-unit combine failure remains before image validation");
  changed = repeatedCombinePlan();
  changed.renderStates.back().textureCombines[7].instructions[3][0] = invalid[0];
  changed.renderStates[0].model[0][0] = invalid[1];
  ok &= combineValidationOracle(changed, false, "Invalid texture combine program",
      "state combine validation retains precedence over the later draw-matrix validation pass");
  changed = repeatedCombinePlan();
  changed.renderStates.back().textureMatrix[0][0] = invalid[0];
  changed.renderStates.back().textureImageSlot = 99;
  changed.renderStates.back().extraTextures[6].matrix[0][0] = invalid[1];
  changed.renderStates.back().extraTextures[6].imageSlot = 99;
  ok &= combineValidationOracle(changed, true, nullptr,
      "disabled texture data remains ignored after validating its inactive program bytes");

  auto sameRevision = repeatedCombinePlan(true);
  ok &= combineValidationOracle(sameRevision, true, nullptr, "initial same-revision frame validates");
  sameRevision.renderStates.back().textureCombines[6].instructions[3][2] = invalid[1];
  ok &= combineValidationOracle(sameRevision, false, "Invalid texture combine program",
      "mutation without a revision change cannot inherit prior invocation results");
  sameRevision.renderStates.back().textureCombines[6] = activeCombine(.5f);
  ok &= combineValidationOracle(sameRevision, true, nullptr,
      "valid retry with the same revision is accepted after a failed invocation");
  coinRenderTestSetEnvironment("COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO", "0");
  ok &= check(sameRevision.isValid(nullptr), "the optional diagnostic pointer remains optional with combine memo enabled");
  return ok;
}

bool testFiniteValidation() {
  bool ok = true;
  if (sizeof(float) == sizeof(uint32_t) && std::numeric_limits<float>::is_iec559 &&
      std::numeric_limits<float>::digits == 24) {
    const uint32_t mantissas[] = {0, 1, 0x3fffff, 0x7fffff};
    for (uint32_t sign = 0; sign < 2; ++sign)
      for (uint32_t exponent = 0; exponent < 256; ++exponent)
        for (const uint32_t mantissa : mantissas) {
          const uint32_t bits = (sign << 31) | (exponent << 23) | mantissa;
          float value;
          std::memcpy(&value, &bits, sizeof(value));
          ok &= check(coin_render_is_finite(value) == std::isfinite(value),
                      "finite classification must cover every exponent, both signs and NaN payloads");
          ok &= check(coin_render_is_finite_inline(value) == coin_render_is_finite(value),
                      "inlined validation must agree with the exported finite classifier");
        }
  }
  const float invalid[] = {std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
  for (float value : invalid) {
    for (unsigned field = 0; field < 40; ++field) {
      auto plan = validPlan();
      auto & v = plan.vertices[0];
      const char * expected;
      if (field < 3) { v.position[field] = value; expected = "Vertex contains non-finite position or normal"; }
      else if (field < 6) { v.normal[field - 3] = value; expected = "Vertex contains non-finite position or normal"; }
      else if (field < 8) { v.texcoord[field - 6] = value; expected = "Vertex contains non-finite texcoord"; }
      else if (field == 8) { v.screenSpaceW = value; expected = "Invalid expanded primitive attributes"; }
      else if (field == 9) { v.fogEyeDepth = value; expected = "Invalid expanded primitive attributes"; }
      else if (field < 24) { v.extraTexcoords[(field - 10) / 2][(field - 10) % 2] = value; expected = "Non-finite multitexture coordinate"; }
      else { (field < 32 ? v.textureR[field - 24] : v.textureQ[field - 32]) = value; expected = "Non-finite homogeneous texture coordinate"; }
      std::string diagnostic;
      ok &= check(!plan.isValid(&diagnostic) && diagnostic == expected,
                  "every captured vertex float must reject invalid data with the same diagnostic");
    }
    for (unsigned unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      auto plan = validPlan();
      plan.renderStates[0].textureCombines[unit].instructions[3][2] = value;
      std::string diagnostic;
      ok &= check(!plan.isValid(&diagnostic) && diagnostic == "Invalid texture combine program",
                  "disabled texture units must still validate their combine floats");
    }
    auto plan = validPlan();
    plan.renderStates[0].model[1][2] = value;
    std::string diagnostic;
    ok &= check(!plan.isValid(&diagnostic) && diagnostic == "RenderState matrix contains non-finite values",
                "matrix finiteness must remain enforced");
  }
  auto finite = validPlan();
  auto invalidPolicy = validPlan();
  invalidPolicy.renderStates[0].textureProjection = static_cast<CoinRenderTextureProjection>(2);
  std::string policyDiagnostic;
  ok &= check(!invalidPolicy.isValid(&policyDiagnostic) &&
              policyDiagnostic == "Invalid texture coordinate projection policy",
              "invalid texture sampling policy must reject transactionally");
  finite.vertices[0].position[0] = std::numeric_limits<float>::max();
  finite.vertices[0].normal[0] = -std::numeric_limits<float>::max();
  finite.vertices[0].texcoord[0] = std::numeric_limits<float>::denorm_min();
  finite.vertices[0].extraTexcoords[6][1] = -0.0f;
  ok &= check(finite.isValid(), "finite extremes, subnormals and signed zeros remain accepted");
  return ok;
}

class CountingBackend : public CoinRenderBackend {
public:
  unsigned submissions = 0;
  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override { return CoinRenderBackendStatus::SUCCESS; }
  void poll() override {}
  const std::string & getLastError() const override { return error; }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &, CoinRenderTargetP &) override {
    ++submissions;
    return {};
  }
private:
  std::string error;
};

class PreflightBackend : public CoinRenderBackend {
public:
  bool ok = true;
  bool fail = false;
  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override { return CoinRenderBackendStatus::SUCCESS; }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override { return CoinRenderBackendStatus::SUCCESS; }
  void poll() override {}
  const std::string & getLastError() const override { return error; }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override {
    const auto * receipt = target.submissionPreflight(frame);
    ok &= check(receipt && receipt->compositionFor(frame), "fresh validation supplies a submission-local receipt");
    auto foreign = frame;
    foreign.vertices[0].position[2] = std::numeric_limits<float>::quiet_NaN();
    ok &= check(!target.submissionPreflight(foreign), "a different plan with the same revision cannot use the receipt");
    std::vector<CoinRenderCompositionItem> order;
    std::string diagnostic;
    ok &= check(!coin_render_composition_schedule(foreign, order, diagnostic, receipt),
                "a foreign mutated plan must take the validating composition path");
    auto & mutableFrame = const_cast<CoinRenderFramePlan &>(frame);
    const uint64_t revision = mutableFrame.revision;
    ++mutableFrame.revision;
    ok &= check(!receipt->compositionFor(frame), "a changed revision invalidates the borrowed receipt");
    mutableFrame.revision = revision;
    const auto transparency = mutableFrame.transparency;
    ++mutableFrame.transparency.layers;
    ok &= check(!receipt->compositionFor(frame), "changed composition policy invalidates the receipt");
    mutableFrame.transparency = transparency;

    CoinRenderTargetP other(SbVec2i32(1, 1));
    other.depthReadbackEnabled = false;
    auto * counter = new CountingBackend;
    other.backend.reset(counter);
    const CoinRenderFrameReuseDecision rebuild(CoinRenderFrameReuseKind::FULL_REBUILD, 0);
    // Even a revision previously accepted by this target cannot make a
    // foreign invalid payload inherit another plan's borrowed proof.
    other.lastValidatedPlanRevision = foreign.revision;
    ok &= check(other.executeFrame(foreign, rebuild, receipt).status == CoinRenderBackendStatus::BACKEND_ERROR &&
                counter->submissions == 0, "foreign proof must trigger complete validation before submission");
    other.options.transparency = static_cast<CoinRenderTransparencyMode>(99);
    ok &= check(other.executeFrame(frame, rebuild, receipt).status == CoinRenderBackendStatus::UNSUPPORTED &&
                counter->submissions == 0, "a valid proof cannot bypass invalid target options");
    other.options.transparency = COIN_RENDER_TRANSPARENCY_COIN;
    other.suspended = true;
    ok &= check(other.executeFrame(frame, rebuild, receipt).status == CoinRenderBackendStatus::NOT_READY &&
                counter->submissions == 0, "a valid proof cannot bypass a suspended target");
    other.suspended = false;
    ok &= check(other.resize(SbVec2i32(2, 2)) &&
                other.executeFrame(frame, rebuild, receipt).status == CoinRenderBackendStatus::SUCCESS &&
                counter->submissions == 1, "resized target must retain its own checks with a borrowed capture proof");
    return {fail ? CoinRenderBackendStatus::BACKEND_ERROR : CoinRenderBackendStatus::SUCCESS};
  }
private:
  std::string error;
};

bool testSubmissionPreflight() {
  CoinRenderTargetP target(SbVec2i32(1, 1));
  target.depthReadbackEnabled = false;
  auto * backend = new PreflightBackend;
  target.backend.reset(backend);
  auto plan = validPlan();
  plan.revision = 1;
  bool ok = check(target.executeFrame(plan).status == CoinRenderBackendStatus::SUCCESS && backend->ok,
                  "validated submission must succeed");
  ok &= check(!target.submissionPreflight(plan), "receipt must be cleared after successful submission");
  backend->fail = true;
  ++plan.revision;
  ok &= check(target.executeFrame(plan).status == CoinRenderBackendStatus::BACKEND_ERROR && backend->ok,
              "failure path must exercise the receipt");
  ok &= check(!target.submissionPreflight(plan), "receipt must be cleared after failed submission");
  plan.vertices[0].position[0] = std::numeric_limits<float>::infinity();
  ++plan.revision;
  ok &= check(target.executeFrame(plan).status != CoinRenderBackendStatus::SUCCESS &&
              !target.submissionPreflight(plan), "invalid subsequent data cannot inherit a receipt");
  return ok;
}

bool testCapturePreflightFallbacks() {
  auto plan = validPlan();
  bool ok = check(coin_render_capture_preflight_eligible(plan),
                  "ordinary captured frame is eligible for submission-local proof reuse");
  plan.shadowGroups.emplace_back();
  ok &= check(!coin_render_capture_preflight_eligible(plan),
              "shadow group capture must retain complete submission validation");
  plan.shadowGroups.clear();
  plan.shadowLights.emplace_back();
  ok &= check(!coin_render_capture_preflight_eligible(plan),
              "shadow-owned scene data cannot use an ordinary capture receipt");
  plan.shadowLights.clear();
  plan.textures.emplace_back();
  auto & texture = plan.textures.back();
  texture.width = texture.height = 1;
  texture.producerId = 7;
  ok &= check(!coin_render_capture_preflight_eligible(plan),
              "unresolved RTT producer must take complete submission validation");
  texture.producerId = 0;
  texture.gpuToken = 9;
  ok &= check(!coin_render_capture_preflight_eligible(plan),
              "resolved RTT resource must also retain complete submission validation");
  texture.gpuToken = 0;
  texture.pixelsRgba = {255, 255, 255, 255};
  ok &= check(plan.isValid() && coin_render_capture_preflight_eligible(plan),
              "ordinary captured image pixels remain eligible after full common validation");
  return ok;
}
}

int
main()
{
  SoDB::init();
  CoinRenderAction::initClass();
  bool ok = true;
  CoinRenderRenderStateSnapshot sparse;
  ok &= check(!sparse.extraTextures.allocated() && !sparse.textureCombines.allocated() &&
              !static_cast<const CoinRenderRenderStateSnapshot &>(sparse).extraTextures[0].enabled &&
              !sparse.extraTextures.allocated(),
              "unused texture state must stay sparse after a const read");
  CoinRenderRenderStateSnapshot detached = sparse;
  detached.extraTextures[0].enabled = true;
  detached.textureCombines[0].instructions[3][2] = .75f;
  ok &= check(!static_cast<const CoinRenderRenderStateSnapshot &>(sparse).extraTextures[0].enabled &&
              static_cast<const CoinRenderRenderStateSnapshot &>(sparse).textureCombines[0].instructions[3][2] == 0 &&
              detached.extraTextures.allocated() && detached.textureCombines.allocated(),
              "texture writes must leave a copied plan unchanged");
  CoinRenderRenderStateSnapshot secondCopy = detached;
  secondCopy.extraTextures[0].enabled = false;
  secondCopy.textureCombines[0].instructions[3][2] = .25f;
  ok &= check(static_cast<const CoinRenderRenderStateSnapshot &>(detached).extraTextures[0].enabled &&
              static_cast<const CoinRenderRenderStateSnapshot &>(detached).textureCombines[0].instructions[3][2] == .75f,
              "materialized texture payload must detach on later writes");
  std::cout << "render_state_stride=" << sizeof(CoinRenderRenderStateSnapshot) << '\n';
  ok &= testLegacyColorConversion();
  ok &= testFiniteValidation();
  ok &= testCombineValidationMemo();
  ok &= testAuthoredSceneLayerIdentity();
  ok &= testSubmissionPreflight();
  ok &= testCapturePreflightFallbacks();
  ok &= testBumpImagePreparation();
  ok &= testBumpHeightOffsetInvariance();
  ok &= testBumpTangents();
  ok &= testBumpDiffuseOracle();
  ok &= testBumpCoordinateSelection();
  ok &= testTriangleCornerDetailIndices();
  ok &= testBumpUvPlanCapture();
  ok &= testBumpSharedCornerPlanCapture();

  CoinRenderTextureUnitSnapshot unitsA[1], unitsB[1];
  for (size_t i = sizeof(bool); i < offsetof(CoinRenderTextureUnitSnapshot, imageSlot); ++i) {
    reinterpret_cast<unsigned char*>(&unitsA[0])[i] = 0x55;
    reinterpret_cast<unsigned char*>(&unitsB[0])[i] = 0xaa;
  }
  ok &= check(coin_render_same_texture_units(unitsA, unitsB),
              "texture unit padding must not split equal render states");
  unitsB[0].enabled = true;
  ok &= check(!coin_render_same_texture_units(unitsA, unitsB),
              "texture unit values must still invalidate equality");

  CoinRenderFramePlan first;
  CoinRenderFramePlan second;
  first.revision = 41;
  second.revision = 99;
  ok &= check(first.hasSamePayload(second),
              "revision must not be part of immutable payload equality");

  CoinRenderFramePlan paddingA, paddingB;
  paddingA.draws.emplace_back(); paddingB.draws.emplace_back();
  for (size_t i = offsetof(CoinRenderDrawPacket, hasSortingCenter) + sizeof(bool);
       i < offsetof(CoinRenderDrawPacket, sortingCenterWorld); ++i) {
    reinterpret_cast<unsigned char *>(&paddingA.draws[0])[i] = 0x55;
    reinterpret_cast<unsigned char *>(&paddingB.draws[0])[i] = 0xaa;
  }
  ok &= check(paddingA.hasSamePayload(paddingB),
              "draw padding must not split equal immutable payloads");
  paddingB.draws[0].sortingCenterWorld[0] = 1.0f;
  ok &= check(!paddingA.hasSamePayload(paddingB),
              "draw field changes must still invalidate immutable payload equality");

  first.vertices.push_back(CoinRenderVertexSnapshot());
  second.vertices.push_back(CoinRenderVertexSnapshot());
  ok &= check(first.hasSamePayload(second), "equal vertices must compare equal");
  second.vertices[0].position[1] = 2.0f;
  ok &= check(!first.hasSamePayload(second), "changed vertex must invalidate equality");

  CoinRenderFramePlan invalid;
  invalid.clearColor[0] = std::numeric_limits<float>::quiet_NaN();
  std::string diagnostic;
  ok &= check(!invalid.isValid(&diagnostic) &&
              diagnostic == "Invalid clearColor (NaN or inf)",
              "validation diagnostic must remain stable");

  std::vector<uint8_t> pixels = {
    1, 2, 3, 4, 5, 6, 7, 8,
    9, 10, 11, 12, 13, 14, 15, 16
  };
  ok &= check(CoinRenderImageCore::flipRgba8Rows(pixels, SbVec2i32(2, 2)),
              "valid RGBA8 image must flip");
  const std::vector<uint8_t> expected = {
    9, 10, 11, 12, 13, 14, 15, 16,
    1, 2, 3, 4, 5, 6, 7, 8
  };
  ok &= check(pixels == expected, "RGBA8 rows must be reversed exactly once");

  const std::vector<uint8_t> before = pixels;
  ok &= check(!CoinRenderImageCore::flipRgba8Rows(pixels, SbVec2i32(3, 2)) &&
              pixels == before,
              "invalid byte count must be rejected without mutation");

  if (!ok) return 1;
  std::cout << "CoinRenderFrameCoreTest passed\n";
  return 0;
}
