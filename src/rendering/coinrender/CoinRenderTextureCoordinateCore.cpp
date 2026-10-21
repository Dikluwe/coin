#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"

SbVec4f
coin_render_texture_coordinate(const CoinRenderVertexSnapshot &vertex, size_t unit)
{
  const float *st = unit ? vertex.extraTexcoords[unit - 1] : vertex.texcoord;
  return SbVec4f(st[0], st[1], vertex.textureR[unit], vertex.textureQ[unit]);
}

SbVec4f
coin_render_transformed_texture_coordinate(const CoinRenderVertexSnapshot &vertex, size_t unit,
                                           const SbMatrix &matrix)
{
  SbVec4f transformed;
  matrix.multVecMatrix(coin_render_texture_coordinate(vertex, unit), transformed);
  return transformed;
}

SbVec2f
coin_render_project_texture_coordinate(const SbVec3f &coordinate,
                                       CoinRenderTextureProjection policy)
{
  if (policy == CoinRenderTextureProjection::DIRECT_ST || coordinate[2] == 1)
    return SbVec2f(coordinate[0], coordinate[1]);
  return SbVec2f(coordinate[0] / coordinate[2], coordinate[1] / coordinate[2]);
}

bool
coin_render_validate_texture_primitive(const CoinRenderFramePlan &frame,
                                       const CoinRenderRenderStateSnapshot &state,
                                       const uint32_t *indices, size_t count,
                                       std::string &diagnostic)
{
  if (state.portableShaderSlot) return true; // The module Core owns sampling.
  for (size_t i = 0; i < count; ++i)
    if (indices[i] >= frame.vertices.size())
      return true; // Ordinary validation owns bounds.
  for (size_t unit=0; unit<COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    const auto layer=coin_render_texture_unit(state,unit);
    if (!layer.enabled || layer.imageSlot>=frame.textures.size() || !frame.textures[layer.imageSlot].cubeFaceSize) continue;
    std::array<std::array<double,3>,3> d{};
    if (count > 3) { diagnostic="Unsupported cube primitive extent"; return false; }
    double scale=0;
    for (size_t i=0;i<count;++i) {
      const auto c=coin_render_transformed_texture_coordinate(frame.vertices[indices[i]],unit,layer.matrix);
      for (size_t axis=0;axis<3;++axis) { d[i][axis]=c[int(axis)]; scale=std::max(scale,std::fabs(d[i][axis])); }
    }
    if (scale>0 && std::isfinite(scale)) for (size_t i=0;i<count;++i) for (int j=0;j<3;++j) d[i][j]/=scale;
    auto cross=[](const std::array<double,3>&a,const std::array<double,3>&b) {
      return std::array<double,3>{{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};
    };
    auto dot=[](const std::array<double,3>&a,const std::array<double,3>&b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; };
    bool zero=false;
    if (count>=2) {
      const auto n=cross(d[0],d[1]);
      zero=dot(n,n)==0 && dot(d[0],d[1])<=0;
    }
    if (!zero && count==3) {
      const auto ab=cross(d[0],d[1]),bc=cross(d[1],d[2]),ca=cross(d[2],d[0]);
      const auto normal=dot(ab,ab)>0?ab:(dot(bc,bc)>0?bc:ca);
      if (dot(normal,normal)==0) zero=dot(d[0],d[2])<=0 || dot(d[1],d[2])<=0;
      else if (dot(normal,d[0])==0 && dot(normal,d[1])==0 && dot(normal,d[2])==0) {
        int axis=0; if(std::fabs(normal[1])>std::fabs(normal[axis]))axis=1;
        if(std::fabs(normal[2])>std::fabs(normal[axis]))axis=2;
        zero=(ab[axis]>=0 && bc[axis]>=0 && ca[axis]>=0) || (ab[axis]<=0 && bc[axis]<=0 && ca[axis]<=0);
      }
    }
    if (zero) { diagnostic="Cube direction reaches zero inside a primitive"; return false; }
  }
  if (!coin_render_validate_texture_coordinates(
      state, count,
      [&](size_t i) -> const CoinRenderVertexSnapshot & { return frame.vertices[indices[i]]; },
      diagnostic, &frame)) return false;
  for (size_t unit=0; unit<COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
    const auto layer=coin_render_texture_unit(state,unit);
    if (!layer.enabled || layer.imageSlot>=frame.textures.size() ||
        !frame.textures[layer.imageSlot].volumeDepth) continue;
    for (size_t i=0; i<count; ++i) {
      const SbVec4f coordinate=coin_render_transformed_texture_coordinate(
          frame.vertices[indices[i]],unit,layer.matrix);
      const float r=state.textureProjection==CoinRenderTextureProjection::DIRECT_ST ?
          coordinate[2] : coordinate[2]/coordinate[3];
      if (!coin_render_is_finite(r)) {
        diagnostic="Spatial texture R coordinate exceeds finite sampling precision";
        return false;
      }
    }
  }
  return true;
}

bool
coin_render_validate_stroke_texture_coordinates(const CoinRenderFramePlan &frame,
                                                std::string &diagnostic)
{
  if (frame.textures.empty())
    return true;
  for (const auto &draw : frame.draws) {
    if (draw.topology == CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        draw.renderStateSlot >= frame.renderStates.size() ||
        draw.geometry.firstIndex > frame.indices.size() ||
        draw.geometry.indexCount > frame.indices.size() - draw.geometry.firstIndex)
      continue;
    const size_t primitiveSize = draw.topology == CoinRenderPrimitiveTopology::LINE_LIST ? 2 : 1;
    for (size_t i = 0; i + primitiveSize <= draw.geometry.indexCount; i += primitiveSize)
      if (!coin_render_validate_texture_primitive(
              frame, frame.renderStates[draw.renderStateSlot],
              frame.indices.data() + draw.geometry.firstIndex + i, primitiveSize, diagnostic))
        return false;
  }
  return true;
}
