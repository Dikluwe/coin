#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace {
const uint64_t cubeByteLimit = 128ull * 1024ull * 1024ull;

struct CubeView {
  uint32_t faceSize, sourceComponents, width, height;
  bool mipmapped;
  const std::vector<uint8_t> & pixelsRgba;
};
CubeView cubeView(const CoinRenderCubeTextureCore::Atlas & atlas) {
  return {atlas.faceSize, atlas.sourceComponents, atlas.width, atlas.height,
          atlas.mipmapped, atlas.pixelsRgba};
}
CubeView cubeView(const CoinRenderTextureImageSnapshot & image) {
  return {image.cubeFaceSize, image.cubeSourceComponents, image.width, image.height,
          image.cubeMipmapped, image.pixelsRgba};
}
bool cubeImageFields(const CoinRenderTextureImageSnapshot & image) {
  return image.format == CoinRenderTextureFormat::RGBA8_LINEAR && image.components == 4 &&
    !image.volumeWidth && !image.volumeHeight && !image.volumeDepth && !image.volumeColumns &&
    !image.volumeSourceComponents && !image.volumeMipmapped && !(image.producerId && image.gpuToken) &&
    !image.mipmapped && image.mipmapsRgba.empty();
}


struct CubeLayout {
  uint32_t width, height, levels;
  uint64_t texels, bytes;
};

bool cubeLayout(uint32_t size, bool mipmapped, CubeLayout & result)
{
  if (!size || size > 2048 || (size & (size - 1))) return false;
  CubeLayout layout = {};
  layout.width = 2 * size;
  for (uint32_t extent = size;; extent /= 2) {
    layout.texels += 6 * uint64_t(extent) * extent;
    ++layout.levels;
    if (!mipmapped || extent == 1) break;
  }
  layout.height = uint32_t((layout.texels + layout.width - 1) / layout.width);
  layout.bytes = uint64_t(layout.width) * layout.height * 4;
  if (layout.width > 8192 || layout.height > 8192 || layout.bytes > cubeByteLimit)
    return false;
  result = layout;
  return true;
}

// Offsets are linear texel indices, independent of atlas row boundaries.
uint64_t cubeLevelOffset(uint32_t size, uint32_t level)
{
  uint64_t offset = 0;
  for (uint32_t i = 0; i < level; ++i, size /= 2)
    offset += 6 * uint64_t(size) * size;
  return offset;
}

SbVec4f cubeTexel(const CubeView & atlas, uint64_t faceOffset,
                  uint32_t size, int x, int y)
{
  x = std::max(0, std::min(int(size) - 1, x));
  y = std::max(0, std::min(int(size) - 1, y));
  const size_t offset = size_t((faceOffset + uint64_t(y) * size + uint32_t(x)) * 4);
  return SbVec4f(atlas.pixelsRgba[offset] / 255.0f,
                atlas.pixelsRgba[offset + 1] / 255.0f,
                atlas.pixelsRgba[offset + 2] / 255.0f,
                atlas.pixelsRgba[offset + 3] / 255.0f);
}

SbVec4f cubeLerp(const SbVec4f & a, const SbVec4f & b, float weight)
{
  SbVec4f value;
  for (int c = 0; c < 4; ++c) value[c] = a[c] + (b[c] - a[c]) * weight;
  return value;
}

SbVec4f cubeSampleLevel(const CubeView & atlas, uint32_t face,
                        const SbVec2f & st, uint32_t level, bool linear)
{
  const uint32_t size = atlas.faceSize >> level;
  const uint64_t faceOffset = cubeLevelOffset(atlas.faceSize, level) + uint64_t(face) * size * size;
  if (!linear)
    return cubeTexel(atlas, faceOffset, size, int(std::floor(double(st[0]) * size)),
                     int(std::floor(double(st[1]) * size)));
  const double x = double(st[0]) * size - 0.5;
  const double y = double(st[1]) * size - 0.5;
  const int ix = int(std::floor(x)), iy = int(std::floor(y));
  const float fx = float(x - ix), fy = float(y - iy);
  const SbVec4f lower = cubeLerp(cubeTexel(atlas, faceOffset, size, ix, iy),
                                cubeTexel(atlas, faceOffset, size, ix + 1, iy), fx);
  const SbVec4f upper = cubeLerp(cubeTexel(atlas, faceOffset, size, ix, iy + 1),
                                cubeTexel(atlas, faceOffset, size, ix + 1, iy + 1), fx);
  return cubeLerp(lower, upper, fy);
}
}

bool CoinRenderCubeTextureCore::subresources(uint32_t n,bool mips,std::vector<Subresource> & output) {
  CubeLayout layout;if(!cubeLayout(n,mips,layout))return false;
  std::vector<Subresource> candidate;uint64_t offset=0;
  for(uint32_t level=0,extent=n;level<layout.levels;++level,extent=std::max(1u,extent/2)) {
    for(uint32_t face=0;face<6;++face) {
      Subresource part;part.face=face;part.level=level;part.width=part.height=extent;
      part.rowPitch=extent*4;part.linearByteOffset=(offset+uint64_t(face)*extent*extent)*4;
      candidate.push_back(part);
    }
    offset+=6*uint64_t(extent)*extent;
  }
  output.swap(candidate);return true;
}

bool
CoinRenderCubeTextureCore::describe(uint32_t n, bool mips, CoinRenderTextureImageSnapshot & output)
{
  CubeLayout layout;
  if (!cubeLayout(n,mips,layout)) return false;
  CoinRenderTextureImageSnapshot image;
  image.width=layout.width; image.height=layout.height;
  image.cubeFaceSize=n; image.cubeSourceComponents=4; image.cubeMipmapped=mips;
  output=std::move(image); return true;
}

bool
CoinRenderCubeTextureCore::prepare(const std::array<Face, 6> & faces, bool mipmapped,
                                    Atlas & output)
{
  CubeLayout layout;
  const uint32_t size = faces[0].width;
  const uint32_t components = faces[0].components;
  if (!cubeLayout(size, mipmapped, layout) || components < 1 || components > 4)
    return false;
  const uint64_t faceBytes = uint64_t(size) * size * components;
  for (const Face & face : faces)
    if (face.width != size || face.height != size || face.components != components ||
        uint64_t(face.pixels.size()) != faceBytes)
      return false;
  Atlas candidate;
  candidate.faceSize = size;
  candidate.sourceComponents = components;
  candidate.width = layout.width;
  candidate.height = layout.height;
  candidate.mipmapped = mipmapped;
  try {
    candidate.pixelsRgba.assign(size_t(layout.bytes), 0);
  }
  catch (const std::bad_alloc &) { return false; }
  for (size_t face = 0; face < faces.size(); ++face) {
    const uint64_t faceOffset = uint64_t(face) * size * size;
    for (uint64_t pixel = 0; pixel < uint64_t(size) * size; ++pixel) {
      const size_t src = size_t(pixel * components);
      const size_t dst = size_t((faceOffset + pixel) * 4);
      const auto & bytes = faces[face].pixels;
      candidate.pixelsRgba[dst] = bytes[src];
      candidate.pixelsRgba[dst + 1] = components <= 2 ? bytes[src] : bytes[src + 1];
      candidate.pixelsRgba[dst + 2] = components <= 2 ? bytes[src] : bytes[src + 2];
      candidate.pixelsRgba[dst + 3] = components == 2 ? bytes[src + 1] :
                                     (components == 4 ? bytes[src + 3] : 255);
    }
  }
  uint64_t previousOffset = 0;
  uint32_t previousSize = size;
  for (uint32_t level = 1; level < layout.levels; ++level) {
    const uint32_t extent = previousSize / 2;
    const uint64_t levelOffset = previousOffset + 6 * uint64_t(previousSize) * previousSize;
    for (uint32_t face = 0; face < 6; ++face) {
      const uint64_t sourceFace = previousOffset + uint64_t(face) * previousSize * previousSize;
      const uint64_t targetFace = levelOffset + uint64_t(face) * extent * extent;
      for (uint32_t y = 0; y < extent; ++y) {
        for (uint32_t x = 0; x < extent; ++x) {
          const uint64_t source = sourceFace + uint64_t(y * 2) * previousSize + x * 2;
          const size_t dst = size_t((targetFace + uint64_t(y) * extent + x) * 4);
          for (size_t c = 0; c < 4; ++c) {
            const uint32_t sum = candidate.pixelsRgba[size_t(source * 4) + c] +
              candidate.pixelsRgba[size_t((source + 1) * 4) + c] +
              candidate.pixelsRgba[size_t((source + previousSize) * 4) + c] +
              candidate.pixelsRgba[size_t((source + previousSize + 1) * 4) + c];
            candidate.pixelsRgba[dst + c] = uint8_t((sum + 2) / 4);
          }
        }
      }
    }
    previousOffset = levelOffset;
    previousSize = extent;
  }
  output = std::move(candidate);
  return true;
}

static bool
validCubeView(const CubeView & atlas)
{
  CubeLayout layout;
  if (!cubeLayout(atlas.faceSize, atlas.mipmapped, layout) ||
      atlas.sourceComponents < 1 || atlas.sourceComponents > 4 ||
      atlas.width != layout.width || atlas.height != layout.height ||
      uint64_t(atlas.pixelsRgba.size()) != layout.bytes)
    return false;
  for (uint64_t i = layout.texels * 4; i < layout.bytes; ++i)
    if (atlas.pixelsRgba[size_t(i)] != 0) return false;
  return true;
}

bool
CoinRenderCubeTextureCore::project(const SbVec3f & direction, uint32_t & face, SbVec2f & st)
{
  // Double retains every finite float, including subnormals, without squaring
  // or normalizing the direction. Ratios cannot exceed one on the chosen axis.
  const double x = direction[0], y = direction[1], z = direction[2];
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
  const double ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z);
  double sc, tc, major;
  uint32_t selected;
  // OpenGL 2.1 table 3.19; portable ties prefer X, then Y, then Z.
  if (ax >= ay && ax >= az) {
    if (ax == 0.0) return false;
    selected = x > 0.0 ? 0 : 1;
    sc = x > 0.0 ? -z : z;
    tc = -y;
    major = ax;
  }
  else if (ay >= az) {
    selected = y > 0.0 ? 2 : 3;
    sc = x;
    tc = y > 0.0 ? z : -z;
    major = ay;
  }
  else {
    selected = z > 0.0 ? 4 : 5;
    sc = z > 0.0 ? x : -x;
    tc = -y;
    major = az;
  }
  const SbVec2f projected(float(0.5 * (sc / major + 1.0)),
                          float(0.5 * (tc / major + 1.0)));
  face = selected;
  st = projected;
  return true;
}

static bool
sampleCubeView(const CubeView & atlas, const SbVec3f & direction, float lod,
               CoinRenderTextureFilter filter, SbVec4f & rgba)
{
  if (!validCubeView(atlas) || !std::isfinite(lod) ||
      static_cast<uint32_t>(filter) > static_cast<uint32_t>(CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR))
    return false;
  uint32_t face;
  SbVec2f st;
  if (!CoinRenderCubeTextureCore::project(direction, face, st)) return false;
  uint32_t last = 0;
  if (atlas.mipmapped)
    for (uint32_t extent = atlas.faceSize; extent > 1; extent /= 2) ++last;
  const bool interpolateLevels = filter == CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR ||
                                  filter == CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR;
  // Both mip filters magnify linearly. For nearest-mip filtering, retain
  // the original LOD sign even when no chain exists or LOD clamps to zero.
  const bool linear = filter == CoinRenderTextureFilter::LINEAR ||
                       filter == CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR ||
                       (filter == CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR && lod <= 0.0f);
  const double level = interpolateLevels ? std::max(0.0, std::min(double(last), double(lod))) : 0.0;
  const uint32_t lower = uint32_t(std::floor(level));
  const uint32_t upper = std::min(lower + 1, last);
  SbVec4f value = cubeSampleLevel(atlas, face, st, lower, linear);
  if (upper != lower && level > lower)
    value = cubeLerp(value, cubeSampleLevel(atlas, face, st, upper, linear), float(level - lower));
  rgba = value;
  return true;
}

bool CoinRenderCubeTextureCore::valid(const Atlas & atlas) { return validCubeView(cubeView(atlas)); }
bool CoinRenderCubeTextureCore::valid(const CoinRenderTextureImageSnapshot & image) {
  if (!cubeImageFields(image)) return false;
  if (image.producerId || image.gpuToken) {
    CubeLayout layout;
    return cubeLayout(image.cubeFaceSize,image.cubeMipmapped,layout) &&
      image.cubeSourceComponents==4 && image.width==layout.width && image.height==layout.height &&
      image.pixelsRgba.empty() && image.mipmapsRgba.empty();
  }
  return validCubeView(cubeView(image));
}
bool CoinRenderCubeTextureCore::sample(const Atlas & atlas, const SbVec3f & direction,
                                       float lod, CoinRenderTextureFilter filter, SbVec4f & rgba) {
  return sampleCubeView(cubeView(atlas), direction, lod, filter, rgba);
}
bool CoinRenderCubeTextureCore::sample(const CoinRenderTextureImageSnapshot & image,
                                       const SbVec3f & direction, float lod,
                                       CoinRenderTextureFilter filter, SbVec4f & rgba) {
  return cubeImageFields(image) && sampleCubeView(cubeView(image), direction, lod, filter, rgba);
}

bool CoinRenderCubeTextureCore::validate(const CoinRenderFramePlan & frame, std::string & diagnostic)
{
  bool present = false;
  for (const auto & image : frame.textures) {
    if (!image.cubeFaceSize && !image.cubeSourceComponents && !image.cubeMipmapped) continue;
    present = true;
    if (!valid(image)) { diagnostic = "Invalid cube texture atlas metadata, bytes or padding"; return false; }
  }
  if (!present) return true;
  for (const auto & state : frame.renderStates) {
    if (static_cast<unsigned>(state.textureProjection) > 1) { diagnostic="Invalid cube projection policy"; return false; }
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      const auto layer = coin_render_texture_unit(state, unit);
      if (!layer.enabled) continue;
      if (layer.imageSlot >= frame.textures.size() || layer.samplerSlot >= frame.samplers.size()) {
        diagnostic = "Cube frame references an invalid texture unit resource"; return false;
      }
      const auto & image = frame.textures[layer.imageSlot];
      if (!image.cubeFaceSize) continue;
      const auto & sampler = frame.samplers[layer.samplerSlot];
      if (sampler.maxAnisotropy != 1 || sampler.wrapS != CoinRenderTextureWrap::CLAMP ||
          sampler.wrapT != CoinRenderTextureWrap::CLAMP || sampler.wrapR != CoinRenderTextureWrap::CLAMP ||
          static_cast<unsigned>(sampler.filter) > 3 || static_cast<unsigned>(layer.model) > 3 ||
          (layer.model == CoinRenderTextureModel::DECAL && image.cubeSourceComponents < 3)) {
        diagnostic = "Unsupported cube sampler or source-component model"; return false;
      }
    }
    if (state.rasterTextureSourceSlot != UINT32_MAX) {
      if (state.rasterTextureSourceSlot >= frame.renderStates.size()) {
        diagnostic = "Invalid cube raster source state"; return false;
      }
      const auto & source = frame.renderStates[state.rasterTextureSourceSlot];
      for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
        const auto layer = coin_render_texture_unit(source, unit);
        if (!layer.enabled || layer.imageSlot >= frame.textures.size() || !frame.textures[layer.imageSlot].cubeFaceSize) continue;
        SbVec4f coordinate; layer.matrix.multVecMatrix(state.rasterTextureCoordinates[unit], coordinate);
        for (int c = 0; c < 4; ++c) if (!std::isfinite(coordinate[c])) {
          diagnostic = "Non-finite cube raster coordinate"; return false;
        }
        uint32_t face; SbVec2f st;
        if (!project(SbVec3f(coordinate[0], coordinate[1], coordinate[2]), face, st)) {
          diagnostic = "Zero cube raster direction"; return false;
        }
      }
    }
  }
  for (const auto & draw : frame.draws) {
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "Cube frame draw references an invalid state"; return false;
    }
    const auto & state = frame.renderStates[draw.renderStateSlot];
    bool cube = false;
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      const auto layer = coin_render_texture_unit(state, unit);
      cube = cube || (layer.enabled && frame.textures[layer.imageSlot].cubeFaceSize);
    }
    if (!cube) continue;
    if (static_cast<unsigned>(draw.topology) > 2) { diagnostic="Invalid cube primitive topology"; return false; }
    const auto & range = draw.geometry;
    if (range.firstIndex > frame.indices.size() || range.indexCount > frame.indices.size() - range.firstIndex) {
      diagnostic = "Cube draw has invalid index range"; return false;
    }
    const size_t primitive = draw.topology == CoinRenderPrimitiveTopology::TRIANGLE_LIST ? 3 :
                             draw.topology == CoinRenderPrimitiveTopology::LINE_LIST ? 2 : 1;
    if (range.indexCount % primitive) { diagnostic = "Cube draw has incomplete primitives"; return false; }
    for (size_t i = 0; i < range.indexCount; i += primitive) {
      const uint32_t * indices = frame.indices.data() + range.firstIndex + i;
      for (size_t j = 0; j < primitive; ++j) if (indices[j] >= frame.vertices.size()) {
        diagnostic = "Cube draw index is out of bounds"; return false;
      }
      if (!coin_render_validate_texture_primitive(frame, state, indices, primitive, diagnostic)) return false;
    }
  }
  return true;
}
