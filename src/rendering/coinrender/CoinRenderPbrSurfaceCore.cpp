#include "CoinRenderPbrSurfaceCore.h"
#include "CoinRenderImageCore.h"
#include "CoinRenderPlanAssemblyCore.h"
#include "CoinRenderTextureSamplingCore.h"
#define genTangSpace coin_render_mikk_generate
#define genTangSpaceDefault coin_render_mikk_generate_default
#include "third_party/mikktspace/mikktspace.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

bool CoinRenderPbrSurfaceCore::normals(const unsigned char *bytes, uint32_t w, uint32_t h, int nc,
                                       bool repeatS, bool repeatT,
                                       CoinRenderTextureImageSnapshot &out, std::string &error) {
  if (!bytes || !w || !h || w > 8192 || h > 8192 || nc < 1 || nc > 4) {
    error = "Invalid stored SoBumpMap image";
    return false;
  }
  out = {};
  out.width = w;
  out.height = h;
  out.format = CoinRenderTextureFormat::RGBA8_LINEAR;
  out.pixelsRgba.resize(size_t(w) * h * 4);
  const auto height = [&](int x, int y) {
    x = repeatS ? (x + int(w)) % int(w) : std::max(0, std::min(x, int(w) - 1));
    y = repeatT ? (y + int(h)) % int(h) : std::max(0, std::min(y, int(h) - 1));
    return float(bytes[(size_t(y) * w + x) * nc]) / 255.f;
  };
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      const size_t i = size_t(y) * w + x;
      SbVec3f n;
      if (nc < 3)
        n = SbVec3f(-(height(x + 1, y) - height(int(x) - 1, y)) * w * .5f,
                    -(height(x, y + 1) - height(x, int(y) - 1)) * h * .5f, 1);
      else
        n = SbVec3f(bytes[i * nc] * 2.f / 255 - 1, bytes[i * nc + 1] * 2.f / 255 - 1,
                    bytes[i * nc + 2] * 2.f / 255 - 1);
      if (n.normalize() == 0 || !std::isfinite(n[0])) {
        error = "SoBumpMap requires finite nonzero tangent normals";
        return false;
      }
      for (int c = 0; c < 3; ++c)
        out.pixelsRgba[4 * i + c] =
            uint8_t(std::lround(std::max(0.f, std::min(1.f, n[c] * .5f + .5f)) * 255));
      out.pixelsRgba[4 * i + 3] = 255;
    }
  // Do not renormalize lower levels: length of the average normal carries
  // unresolved variance for specular filtering. RGB is linear data throughout.
  out.contentDigest = CoinRenderImageCore::rgba8Digest(out.pixelsRgba);
  return CoinRenderTextureSamplingCore::generate(out);
}
namespace {
struct MikkInput {
  const std::vector<float> *key;
  std::vector<float> *result;
  int uvOffset, tangentOffset;
};
const float *input(const SMikkTSpaceContext *c, int face, int vertex) {
  const auto *d = static_cast<const MikkInput *>(c->m_pUserData);
  return d->key->data() + (size_t(face) * 3 + vertex) * 10;
}
int faces(const SMikkTSpaceContext *c) {
  return int(static_cast<const MikkInput *>(c->m_pUserData)->key->size() / 30);
}
int vertices(const SMikkTSpaceContext *, int) { return 3; }
void position(const SMikkTSpaceContext *c, float v[], int f, int n) {
  std::copy_n(input(c, f, n), 3, v);
}
void normal(const SMikkTSpaceContext *c, float v[], int f, int n) {
  std::copy_n(input(c, f, n) + 3, 3, v);
}
void uv(const SMikkTSpaceContext *c, float v[], int f, int n) {
  std::copy_n(input(c, f, n) + static_cast<const MikkInput *>(c->m_pUserData)->uvOffset, 2, v);
}
void tangent(const SMikkTSpaceContext *c, const float t[], float sign, int f, int v) {
  auto *d = static_cast<MikkInput *>(c->m_pUserData);
  float *out = d->result->data() + (size_t(f) * 3 + v) * 8 + d->tangentOffset;
  std::copy_n(t, 3, out);
  out[3] = sign;
}
bool degenerate(const float *a, const float *b, const float *c) {
  const double u = double(b[0]) - a[0], v = double(b[1]) - a[1], s = double(c[0]) - a[0],
               t = double(c[1]) - a[1];
  const double area = u * t - v * s, extent = (u * u + v * v) * (s * s + t * t);
  return !std::isfinite(area) || extent == 0 || area * area <= extent * 1e-20;
}
} // namespace
bool CoinRenderPbrSurfaceCore::prepare(CoinRenderFramePlan &plan, uint64_t budget,
                                       std::string &error) {
  generatedTriangles = cacheHits = degenerateTriangles = 0;
  for (auto &draw : plan.draws) {
    const auto &state = plan.renderStates[draw.renderStateSlot];
    if ((state.pbrNormalImageSlot == UINT32_MAX &&
         plan.materials[state.materialSlot].pbr.anisotropy <= 0) ||
        draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST)
      continue;
    const size_t count = draw.geometry.indexCount;
    if (count / 3 > INT_MAX) {
      error = "PBR tangent triangle count exceeds MikkTSpace capacity";
      return false;
    }
    const uint64_t working =
        uint64_t(count) * (sizeof(CoinRenderVertexSnapshot) + 18 * sizeof(float));
    if (working > budget / 2) {
      error = "PBR tangent preparation exceeds CPU working budget";
      return false;
    }
    std::vector<CoinRenderVertexSnapshot> corners;
    std::vector<CoinRenderPbrSurfaceVertex> attributes; attributes.reserve(count);
    corners.reserve(count);
    Entry candidate;
    candidate.key.reserve(count * 10);
    for (size_t i = 0; i < count; ++i) {
      auto vertex = plan.vertices[plan.indices[draw.geometry.firstIndex + i]];
      CoinRenderPbrSurfaceVertex attributesForCorner;
      float normalUv[3]={},directionUv[3]={};
      const int unit=state.pbrNormalCoordinateUnit;
      const float * sourceUv=unit?vertex.extraTexcoords[unit-1]:vertex.texcoord;
      SbVec4f transformed;
      state.pbrNormalMatrix.multVecMatrix(SbVec4f(sourceUv[0],sourceUv[1],vertex.textureR[unit],vertex.textureQ[unit]),transformed);
      if(!std::isfinite(transformed[3])||transformed[3]==0){error="Invalid normal UV divisor";return false;}
      normalUv[0]=transformed[0];normalUv[1]=transformed[1];normalUv[2]=transformed[3];
      std::copy_n(normalUv,3,attributesForCorner.normalUv);
      state.pbrDirectionMatrix.multVecMatrix(SbVec4f(sourceUv[0],sourceUv[1],vertex.textureR[unit],vertex.textureQ[unit]),transformed);
      if(!std::isfinite(transformed[3])||transformed[3]==0){error="Invalid anisotropy UV divisor";return false;}
      directionUv[0]=transformed[0];directionUv[1]=transformed[1];directionUv[2]=transformed[3];
      attributes.push_back(attributesForCorner);
      const SbVec3f normal(vertex.normal);
      SbVec3f n = normal;
      if (n.normalize() == 0) {
        error = "PBR tangent input normal is zero";
        return false;
      }
      for (int c = 0; c < 3; ++c)
        candidate.key.push_back(vertex.position[c]);
      for (int c = 0; c < 3; ++c)
        candidate.key.push_back(n[c]);
      for (const float *uv : {normalUv, directionUv})
        for (int c = 0; c < 2; ++c) {
          float value = uv[c] / uv[2];
          if (!std::isfinite(value)) {
            error = "Nonfinite PBR tangent UV";
            return false;
          }
          candidate.key.push_back(value);
        }
      corners.push_back(vertex);
    }
    const Entry *prepared = nullptr;
    for (const auto &entry : cache)
      if (entry.key == candidate.key) {
        prepared = &entry;
        ++cacheHits;
        break;
      }
    if (!prepared) {
      candidate.tangents.resize(count * 8, 0);
      SMikkTSpaceInterface api = {};
      api.m_getNumFaces = faces;
      api.m_getNumVerticesOfFace = vertices;
      api.m_getPosition = position;
      api.m_getNormal = normal;
      api.m_getTexCoord = uv;
      api.m_setTSpaceBasic = tangent;
      for (int mapping = 0; mapping < 2; ++mapping) {
        MikkInput data{&candidate.key, &candidate.tangents, 6 + 2 * mapping, 4 * mapping};
        SMikkTSpaceContext context{&api, &data};
        if (count && !coin_render_mikk_generate_default(&context)) {
          error = "MikkTSpace tangent generation failed";
          return false;
        }
        for (size_t i = 0; i < count; i += 3)
          if (degenerate(candidate.key.data() + i * 10 + 6 + mapping * 2,
                         candidate.key.data() + (i + 1) * 10 + 6 + mapping * 2,
                         candidate.key.data() + (i + 2) * 10 + 6 + mapping * 2)) {
            if (!mapping || candidate.tangents[i * 8 + 3] != 0)
              ++candidate.degenerate;
            for (int c = 0; c < 3; ++c)
              std::fill_n(candidate.tangents.data() + (i + c) * 8 + mapping * 4, 4, 0);
          }
      }
      generatedTriangles += count / 3;
      prepared = &candidate;
    }
    degenerateTriangles += prepared->degenerate;
    if (count > UINT32_MAX || plan.vertices.size() > UINT32_MAX - count ||
        plan.indices.size() > UINT32_MAX - count) {
      error = "PBR prepared geometry exceeds index capacity";
      return false;
    }
    const uint32_t first = uint32_t(plan.vertices.size());
    const uint32_t firstIndex = uint32_t(plan.indices.size());
    for (size_t i = 0; i < count; ++i) {
      std::copy_n(prepared->tangents.data() + i * 8, 4, attributes[i].normalTangent);
      std::copy_n(prepared->tangents.data() + i * 8 + 4, 4, attributes[i].directionTangent);
      attributes[i].vertexIndex=uint32_t(plan.vertices.size());
      plan.pbrSurfaceVertices.push_back(attributes[i]);
      // Draws may share an authored index range with different UV transforms.
      // Keep that source untouched and publish a private corner range per draw.
      plan.indices.push_back(uint32_t(plan.vertices.size()));
      plan.vertices.push_back(corners[i]);
    }
    draw.geometry.firstIndex = firstIndex;
    draw.geometry.firstVertex = first;
    draw.geometry.vertexCount = uint32_t(count);
    if (prepared == &candidate) {
      const uint64_t bytes = (candidate.key.size() + candidate.tangents.size()) * sizeof(float);
      if (bytes <= budget / 4) {
        if (cacheBytes + bytes > budget / 4) {
          cache.clear();
          cacheBytes = 0;
        }
        cacheBytes += bytes;
        cache.push_back(std::move(candidate));
      }
    }
  }
  return true;
}

// Convert already-captured logical images. No traversal or planning occurs here.
bool CoinRenderPbrSurfaceCore::images(CoinRenderFramePlan & frame,CoinRenderRenderStateSnapshot & state,
 const SoPBRSurfaceData & source,uint32_t anisotropy,std::string & error) {
  const auto fail=[&](const char * text){error=text;return false;};
  const auto & material=frame.materials[state.materialSlot].pbr;
  state.pbrDirectionMatrix=source.directionMatrix;
  state.pbrNormalCoordinateUnit=source.normalCoordinateUnit;
  state.pbrNormalMatrix=source.normalMatrix;
  const auto image=[&](const SoTextureData & input,int role,CoinRenderSamplerSnapshot & sampler,uint32_t & slot) {
    if(!input.pixels || input.width<=0 || input.height<=0 || input.numComponents<1 || input.numComponents>4)
      return fail("Invalid captured PBR image");
    sampler.wrapS=input.wrapS==SO_TEXTURE_WRAP_REPEAT?CoinRenderTextureWrap::REPEAT:CoinRenderTextureWrap::CLAMP;
    sampler.wrapT=input.wrapT==SO_TEXTURE_WRAP_REPEAT?CoinRenderTextureWrap::REPEAT:CoinRenderTextureWrap::CLAMP;
    if(input.wrapS==SO_TEXTURE_WRAP_CLAMP_TO_BORDER || input.wrapT==SO_TEXTURE_WRAP_CLAMP_TO_BORDER)return fail("PBR border samplers are not qualified");
    sampler.filter=role==2?CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR:
      input.minFilter==SO_TEXTURE_FILTER_NEAREST?CoinRenderTextureFilter::NEAREST:
      input.minFilter==SO_TEXTURE_FILTER_LINEAR?CoinRenderTextureFilter::LINEAR:
      input.minFilter==SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR?CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR:CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR;
    sampler.maxAnisotropy=input.anisotropic?anisotropy:1;
    const bool mip=CoinRenderTextureSamplingCore::mipFilter(sampler.filter);
    const auto key=std::make_tuple(reinterpret_cast<uintptr_t>(input.pixels),input.width,input.height,input.numComponents,role,int(input.wrapS),int(input.wrapT),mip);
    auto found=imageSlots.find(key);if(found!=imageSlots.end()){slot=found->second;return true;}
    const uint64_t bytes=uint64_t(input.width)*input.height*4;
    uint64_t resident=0;for(const auto & texture:frame.textures)resident+=texture.pixelsRgba.size()+texture.mipmapsRgba.size();
    if(bytes>96u*1024u*1024u || resident+bytes+bytes/2>128u*1024u*1024u)return fail("PBR image/mip working budget exceeded");
    CoinRenderTextureImageSnapshot converted;
    if(role==2) {if(!normals(input.pixels,input.width,input.height,input.numComponents,sampler.wrapS==CoinRenderTextureWrap::REPEAT,sampler.wrapT==CoinRenderTextureWrap::REPEAT,converted,error))return false;}
    else {
      converted.width=input.width;converted.height=input.height;
      converted.format=role==0?CoinRenderTextureFormat::RGBA8_SRGB:CoinRenderTextureFormat::RGBA8_LINEAR;
      if(!CoinRenderImageCore::convertToRgba8(input.pixels,size_t(input.width)*input.height,input.numComponents,converted.pixelsRgba))return fail("Invalid PBR image components");
      converted.contentDigest=CoinRenderImageCore::rgba8Digest(converted.pixelsRgba);
      if(mip&&!CoinRenderTextureSamplingCore::generate(converted))return fail("PBR mip generation failed");
    }
    slot=CoinRenderPlanAssemblyCore::texture(frame,std::move(converted));imageSlots.emplace(key,slot);return true;
  };
  for(int role=0;role<3;++role) {
    const int unit=role==0?material.baseColorTextureUnit:role==1?material.ormTextureUnit:material.emissiveTextureUnit;
    if(unit<0)continue;
    uint32_t slot;CoinRenderSamplerSnapshot sampler;
    if(!image(source.textures[unit],role==1?1:0,sampler,slot))return false;
    const uint32_t samplerSlot=CoinRenderPlanAssemblyCore::sampler(frame,sampler);
    if(unit==0){state.hasTexture=true;state.textureImageSlot=slot;state.samplerSlot=samplerSlot;state.textureMatrix=source.textures[unit].matrix;}
    else {auto & layer=state.extraTextures[unit-1];layer.enabled=true;layer.imageSlot=slot;layer.samplerSlot=samplerSlot;layer.matrix=source.textures[unit].matrix;}
  }
  if(source.normalMap.pixels) {
    CoinRenderSamplerSnapshot sampler;uint32_t slot;
    if(!image(source.normalMap,2,sampler,slot))return false;
    state.pbrNormalImageSlot=slot;state.pbrNormalSamplerSlot=CoinRenderPlanAssemblyCore::sampler(frame,sampler);
    state.pbrNormalMatrix=source.normalMatrix;state.pbrNormalCoordinateUnit=source.normalCoordinateUnit;
  }
  state.transparentTexture=false;return true;
}
