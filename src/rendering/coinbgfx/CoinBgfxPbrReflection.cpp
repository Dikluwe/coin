// One GPU-only mirrored view over the existing VBO/IBO and supplied draw order.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"
#include "rendering/coinrender/CoinRenderPlanarReflectionCore.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
using namespace coin_bgfx_internal;

bool CoinBgfxBackend::preparePbrReflection(const CoinRenderFramePlan & frame,const CoinRenderTargetP & target) {
  const auto & e=target.options.pbrEffects;
  const auto refuse=[&](const char * message){lastError=message;return false;};
  if(!e.planarReflection) {destroyPbrReflection();return true;}
  pbrRoughMapping=CoinRenderRoughPlanar::Mapping{};
  pbrReflectionReceivers.assign(frame.draws.size(),0);
  if(frame.draws.size()>16u*1024u*1024u/sizeof(CoinBgfxDraw))return refuse("Reflected draw metadata exceeds 16 MiB working bound");
  const CoinRenderRenderStateSnapshot * basis=nullptr;bool receiver=false,physical=false;
  for(size_t slot=0;slot<frame.draws.size();++slot) {
    const auto & draw=frame.draws[slot];if(!draw.geometry.indexCount)continue;const auto & state=frame.renderStates[draw.renderStateSlot];
    const auto & viewport=frame.viewports[state.viewportSlot];
    if(draw.renderLayer||draw.shadowLightSlot||draw.authoredScene||viewport.x||viewport.y||
       viewport.width!=target.size[0]||viewport.height!=target.size[1]||
       (basis&&(state.view!=basis->view||state.projectionCoin!=basis->projectionCoin))||
       state.clipPlanesWorld.size()>=COIN_RENDER_MAX_CLIP_PLANES)
      return refuse("Planar PBR requires one full viewport/camera, no layers, and one free clipping plane");
    basis=&state;
    bool coplanar=draw.geometry.indexCount>0;
    for(uint32_t i=0;coplanar&&i<draw.geometry.indexCount;++i) {
      SbVec3f world;state.model.multVecMatrix(SbVec3f(frame.vertices[frame.indices[draw.geometry.firstIndex+i]].position),world);
      coplanar=std::abs(e.reflectionPlane.getDistance(world))<=e.reflectionPlaneTolerance;
    }
    if(coplanar) {
      const auto & material=frame.materials[state.materialSlot].pbr;
      if((material.roughness>.045f&&!e.roughPlanarReflection) || material.anisotropy>0 ||
         (e.roughPlanarReflection&&(material.clearcoat>0||material.ormTextureUnit>=0)) ||
         (material.clearcoat>0&&material.clearcoatRoughness>.045f)||state.pbrNormalImageSlot!=UINT32_MAX)
        return refuse("Planar receiver requires isotropic unperturbed normals, explicit rough filtering and no rough coat/ORM map");
      const auto normalMatrix=state.model.inverse().transpose();
      for(uint32_t i=0;i<draw.geometry.indexCount;++i) {
        SbVec3f normal;normalMatrix.multDirMatrix(SbVec3f(frame.vertices[frame.indices[draw.geometry.firstIndex+i]].normal),normal);
        if(normal.normalize()==0 || std::abs(normal.dot(e.reflectionPlane.getNormal()))<.9999f)
          return refuse("Planar receiver requires normals parallel to its physical plane");
      }
      pbrReflectionReceivers[slot]=1;receiver=true;
    } else physical=true;
  }
  if(!receiver||!physical)return refuse("Planar reflection requires a coplanar smooth receiver and physical scene geometry");
  if(e.roughPlanarReflection &&
     !CoinRenderRoughPlanar::prepare(frame,e.reflectionPlane,pbrReflectionReceivers,*basis,pbrRoughMapping,lastError))return false;
  const uint64_t mappingBytes=e.roughPlanarReflection?CoinRenderRoughPlanar::workingBytes:0;
  const int w=(target.size[0]+e.reflectionScale-1)/e.reflectionScale;
  const int h=(target.size[1]+e.reflectionScale-1)/e.reflectionScale;
  const uint64_t requested=uint64_t(w)*h*12;
  const bool replace=!bgfx::isValid(pbrReflectionBuffer)||w!=pbrReflectionWidth||h!=pbrReflectionHeight;
  if(mappingBytes>e.reflectionBudgetBytes || requested>e.reflectionBudgetBytes-mappingBytes ||
     (replace&&pbrReflectionBytes>e.reflectionBudgetBytes-mappingBytes-requested))
    return refuse("Planar reflection attachments exceed resident/replacement budget");
  if(!replace)return true;
  const uint64_t flags=BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
    BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
  if(w>bgfx::getCaps()->limits.maxTextureSize||h>bgfx::getCaps()->limits.maxTextureSize||
     !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA16F,flags)||
     !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::D24S8,flags))return refuse("Planar reflection requires sampleable HDR/depth attachments");
  bgfx::TextureHandle images[2]={bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::RGBA16F,flags),
    bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::D24S8,flags)};
  if(!bgfx::isValid(images[0])||!bgfx::isValid(images[1])) {
    for(auto handle:images)if(bgfx::isValid(handle))bgfx::destroy(handle);
    pbrPrepareStatus=CoinRenderBackendStatus::OUT_OF_MEMORY;return refuse("Cannot allocate planar reflection attachments");
  }
  const auto candidate=bgfx::createFrameBuffer(2,images,true);
  if(!bgfx::isValid(candidate)) {
    for(auto handle:images)bgfx::destroy(handle);
    pbrPrepareStatus=CoinRenderBackendStatus::OUT_OF_MEMORY;return refuse("Cannot allocate planar reflection framebuffer");
  }
  if(bgfx::isValid(pbrReflectionBuffer))bgfx::destroy(pbrReflectionBuffer);
  pbrReflectionBuffer=candidate;pbrReflectionWidth=w;pbrReflectionHeight=h;pbrReflectionBytes=requested;
  return true;
}

bool CoinBgfxBackend::encodePbrReflection(bgfx::ViewId & view,const std::vector<CoinBgfxDraw> & draws,
    bgfx::DynamicVertexBufferHandle vb,bgfx::DynamicIndexBufferHandle ib,
    const std::vector<bgfx::TextureHandle> & textures,const CoinRenderFramePlan & frame) {
  if(!pbrEffects.planarReflection)return true;
  const auto reflection=coin_render_plane_reflection(pbrEffects.reflectionPlane);
  pbrReflectedDraws.clear();pbrReflectedDraws.reserve(draws.size());
  // The far plane accounts for the longer reflected path without changing XY
  // projection or the main image/depth. Only existing captured positions are read.
  double farDepth=0;
  for(const auto & draw:draws) {
    if(pbrReflectionReceivers[draw.sourceDrawSlot])continue;
    const auto & state=frame.renderStates[draw.renderStateSlot];
    const auto mv=state.model*reflection*state.view;
    const auto & geometry=frame.draws[draw.sourceDrawSlot].geometry;
    for(uint32_t i=0;i<geometry.vertexCount;++i) {
      SbVec3f point;mv.multVecMatrix(SbVec3f(frame.vertices[geometry.firstVertex+i].position),point);
      farDepth=std::max(farDepth,-double(point[2]));
    }
  }
  for(const auto & original:draws) {
    if(pbrReflectionReceivers[original.sourceDrawSlot])continue;
    if(original.instanceCount) {lastError="Planar reflection requires ordinary retained PBR draws";return false;}
    auto draw=original;auto state=frame.renderStates[draw.renderStateSlot];
    const auto oldView=state.view;const auto delta=oldView.inverse()*reflection*oldView;
    SbVec3f eye;oldView.inverse().multVecMatrix(SbVec3f(0,0,0),eye);
    const auto & plane=pbrEffects.reflectionPlane;
    state.clipPlanesWorld.push_back(plane.getDistance(eye)>=0?plane:SbPlane(-plane.getNormal(),-plane.getDistanceFromOrigin()));
    state.view=reflection*oldView;
    if(!coin_render_reflection_projection(state.projectionCoin,farDepth,lastError)||
       !coin_render_clip_equations(state,draw.clipPlanes,lastError))return false;
    draw.clipMeta[0]=float(state.clipPlanesWorld.size());
    draw.pbrModelView=state.model*state.view;draw.pbrNormalToView=draw.pbrModelView.inverse().transpose();
    draw.pbrViewToWorld=state.view.inverse();draw.pbrTangentParity=-original.pbrTangentParity;
    const auto mvp=draw.pbrModelView*CoinRenderTransformCore::projection(state.projectionCoin,bgfx::getCaps()->homogeneousDepth);
    std::copy_n(&mvp[0][0],16,draw.mvp);
    draw.frontFace=draw.frontFace==CoinRenderFrontFace::CCW?CoinRenderFrontFace::CW:CoinRenderFrontFace::CCW;
    draw.viewport[0]=draw.viewport[1]=0;draw.viewport[2]=pbrReflectionWidth;draw.viewport[3]=pbrReflectionHeight;
    for(int i=0;i<int(draw.lightCount[0]);++i) {
      SbVec3f position,direction;delta.multVecMatrix(SbVec3f(original.lightPositionType[i]),position);
      delta.multDirMatrix(SbVec3f(original.lightDirectionCutoff[i]),direction);
      for(int c=0;c<3;++c){draw.lightPositionType[i][c]=position[c];draw.lightDirectionCutoff[i][c]=direction[c];}
    }
    pbrReflectedDraws.push_back(std::move(draw));
  }
  bgfx::resetView(view);bgfx::setViewName(view,"pbr_planar_reflection");bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
  bgfx::setViewFrameBuffer(view,pbrReflectionBuffer);bgfx::setViewRect(view,0,0,pbrReflectionWidth,pbrReflectionHeight);
  bgfx::setViewClear(view,BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,uint32_t(0),1.f,0);bgfx::setViewTransform(view,nullptr,nullptr);bgfx::touch(view);
  for(const auto & draw:pbrReflectedDraws) {
    bgfx::setTransform(draw.mvp);bgfx::setVertexBuffer(0,vb);bgfx::setIndexBuffer(ib,draw.firstIndex,draw.indexCount);
    bgfx::setState(drawState(draw));submitPbrDraw(view,draw,frame,textures,true);
  }
  ++view;++pbrReflectionRenders;return true;
}
void CoinBgfxBackend::destroyPbrReflection() {
  if(bgfx::isValid(pbrReflectionBuffer))bgfx::destroy(pbrReflectionBuffer);
  pbrReflectionBuffer=BGFX_INVALID_HANDLE;pbrReflectionWidth=pbrReflectionHeight=0;pbrReflectionBytes=0;
  pbrReflectionReceivers.clear();pbrReflectedDraws.clear();pbrRoughMapping=CoinRenderRoughPlanar::Mapping{};
}
