// Executor-only depth resources, fitted from the supplied captured geometry.
// No scene graph action, capture, semantic planner or CPU pixel readback.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"
#include <Inventor/SbBox3f.h>
#include <Inventor/SbViewVolume.h>
#include "coin_bgfx_vs_pbr_depth_spirv.h"
#include "coin_bgfx_fs_pbr_depth_spirv.h"
using namespace coin_bgfx_internal;
namespace {
bgfx::FrameBufferHandle depthBuffer(uint32_t w,uint32_t h) {
  const uint64_t flags=BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
    BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
  auto depth=bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::D24S8,flags);
  if(!bgfx::isValid(depth))return BGFX_INVALID_HANDLE;
  auto buffer=bgfx::createFrameBuffer(1,&depth,true);
  if(!bgfx::isValid(buffer))bgfx::destroy(depth);
  return buffer;
}
}
bool CoinBgfxBackend::preparePbrEffects(const CoinRenderFramePlan & frame,CoinRenderTargetP & target) {
  const auto refuse=[&](const char * text){this->lastError=text;return false;};
  const auto & e=target.options.pbrEffects;
  this->pbrEffects=e;
  const auto uniform=[&](const char * name,bgfx::UniformType::Enum type) {
    if(!this->pbrUniforms.count(name))this->pbrUniforms.emplace(name,bgfx::createUniform(name,type));
  };
  for(const char * name:{"u_pbrEffectViewToWorld","u_pbrShadowWorldToClip","u_pbrEffectProjection","u_pbrEffectInverseProjection"})
    uniform(name,bgfx::UniformType::Mat4);
  for(const char * name:{"u_pbrShadowOptions","u_pbrAoOptions"})uniform(name,bgfx::UniformType::Vec4);
  for(const char * name:{"s_pbrDirectionalDepth","s_pbrCameraDepth"})uniform(name,bgfx::UniformType::Sampler);
  for(const auto & entry:this->pbrUniforms)if(!bgfx::isValid(entry.second))return refuse("Cannot allocate PBR effects uniforms");
  if(!e.shadows&&!e.ambientOcclusion)return true;
  if(frame.draws.empty())return refuse("PBR effects require captured geometry");
  const auto & first=frame.renderStates[frame.draws.front().renderStateSlot];
  if(!CoinRenderTransformCore::rigidViewMatrix(first.view))return refuse("PBR effects require a rigid camera view");
  SbVec3f direction(0,0,-1);
  if(e.shadows) {
    if(first.lightingSlot>=frame.lightingStates.size()||frame.lightingStates[first.lightingSlot].lights.empty())
      return refuse("Directional PBR shadows require a captured first directional light");
    const auto & light=frame.lightingStates[first.lightingSlot].lights.front();
    if(light.type!=CoinRenderLightType::DIRECTIONAL)return refuse("PBR shadow profile requires first light directional");
    first.view.inverse().multDirMatrix(SbVec3f(light.direction),direction);
    if(direction.normalize()==0)return refuse("Invalid captured directional light");
  }
  for(const auto & draw:frame.draws) {
    const auto & state=frame.renderStates[draw.renderStateSlot];
    if(state.view!=first.view||state.projectionCoin!=first.projectionCoin||
       !state.clipPlanesWorld.empty()||state.polygonOffsetEnabled||!state.depthTest||!state.depthWrite||
       state.depthRange[0]!=0||state.depthRange[1]!=1||
       (state.depthFunction!=CoinRenderDepthFunction::LESS&&state.depthFunction!=CoinRenderDepthFunction::LEQUAL))
      return refuse("PBR effects require one camera, ordinary opaque depth and no clips/offset");
    const auto & viewport=frame.viewports[state.viewportSlot];
    if(viewport.x||viewport.y||viewport.width!=target.size[0]||viewport.height!=target.size[1])
      return refuse("PBR effects require one full-target viewport");
    if(e.shadows && state.lightingSlot!=first.lightingSlot)
      return refuse("PBR shadow profile requires shared captured lighting");
  }
  const auto * caps=bgfx::getCaps();
  if(e.shadows&&e.shadowMapSize>caps->limits.maxTextureSize)return refuse("PBR shadow map exceeds GPU texture limit");
  if(!(caps->formats[bgfx::TextureFormat::D24S8]&BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER))
    return refuse("PBR effects require sampleable D24S8 depth");
  this->pbrEffectProjection=CoinRenderTransformCore::projection(first.projectionCoin,caps->homogeneousDepth);
  this->pbrEffectInverseProjection=this->pbrEffectProjection.inverse();
  const auto identity=CoinRenderFrameBorrow::identityFor(frame);
  if(!identity||identity!=this->pbrEffectIdentity||(e.shadows&&this->pbrShadowDirty)) {
    this->pbrShadowDirty=this->pbrCameraDepthDirty=true;
    if(e.shadows) {
      SbBox3f bounds;
      for(const auto & draw:frame.draws) {
        const auto & state=frame.renderStates[draw.renderStateSlot];
        const auto & g=draw.geometry;
        for(uint32_t i=0;i<g.indexCount;++i) {
          SbVec3f world;state.model.multVecMatrix(SbVec3f(frame.vertices[frame.indices[g.firstIndex+i]].position),world);
          bounds.extendBy(world);
        }
      }
      if(bounds.isEmpty())return refuse("PBR shadow bounds are empty");
      const float radius=std::max(.01f,(bounds.getMax()-bounds.getMin()).length()*.56f);
      SbViewVolume volume;volume.ortho(-radius,radius,-radius,radius,radius*.01f,radius*4);
      volume.rotateCamera(SbRotation(SbVec3f(0,0,-1),direction));
      volume.translateCamera(bounds.getCenter()-direction*(radius*2));
      SbMatrix view,projection;volume.getMatrices(view,projection);
      this->pbrShadowWorldToClip=view*CoinRenderTransformCore::projection(projection,caps->homogeneousDepth);
    }
    // Only the immutable Wiring-issued borrow proves reuse. User revisions do
    // not establish that bounds or the GPU depth contents are still valid.
    this->pbrEffectIdentity=identity;
  }
  const uint32_t w=target.size[0],h=target.size[1];
  const bool newShadow=e.shadows&&(!bgfx::isValid(this->pbrShadowBuffer)||this->pbrShadowSize!=e.shadowMapSize);
  const bool newCamera=e.ambientOcclusion&&(!bgfx::isValid(this->pbrCameraDepthBuffer)||this->pbrDepthWidth!=w||this->pbrDepthHeight!=h);
  const uint64_t resident=(bgfx::isValid(this->pbrShadowBuffer)?uint64_t(this->pbrShadowSize)*this->pbrShadowSize*4:0)+
    (bgfx::isValid(this->pbrCameraDepthBuffer)?uint64_t(this->pbrDepthWidth)*this->pbrDepthHeight*4:0);
  const uint64_t candidate=(newShadow?uint64_t(e.shadowMapSize)*e.shadowMapSize*4:0)+(newCamera?uint64_t(w)*h*4:0);
  if(resident+candidate>e.resourceBudgetBytes)return refuse("PBR effect resident/candidate buffers exceed budget");
  if(!bgfx::isValid(this->pbrDepthProgram))
    this->pbrDepthProgram=createLayerProgram(coin_bgfx_vs_pbr_depth_spirv,sizeof(coin_bgfx_vs_pbr_depth_spirv),
      coin_bgfx_fs_pbr_depth_spirv,sizeof(coin_bgfx_fs_pbr_depth_spirv));
  if(!bgfx::isValid(this->pbrDepthProgram))return refuse("Cannot allocate PBR depth program");
  if(newShadow) {
    auto candidate=depthBuffer(e.shadowMapSize,e.shadowMapSize);
    if(!bgfx::isValid(candidate))return refuse("Cannot allocate PBR shadow depth");
    if(bgfx::isValid(this->pbrShadowBuffer))bgfx::destroy(this->pbrShadowBuffer);
    this->pbrShadowBuffer=candidate;this->pbrShadowSize=e.shadowMapSize;this->pbrShadowDirty=true;
  }
  if(newCamera) {
    auto candidate=depthBuffer(w,h);
    if(!bgfx::isValid(candidate))return refuse("Cannot allocate PBR occlusion depth");
    if(bgfx::isValid(this->pbrCameraDepthBuffer))bgfx::destroy(this->pbrCameraDepthBuffer);
    this->pbrCameraDepthBuffer=candidate;this->pbrDepthWidth=w;this->pbrDepthHeight=h;this->pbrCameraDepthDirty=true;
  }
  return true;
}
bgfx::ViewId CoinBgfxBackend::encodePbrEffectDepths(bgfx::ViewId view,const std::vector<CoinBgfxDraw> & draws,
  bgfx::DynamicVertexBufferHandle vb,bgfx::DynamicIndexBufferHandle ib,const CoinRenderFramePlan & frame) {
  const auto encode=[&](bgfx::FrameBufferHandle buffer,uint32_t w,uint32_t h,bool shadow) {
    bgfx::resetView(view);bgfx::setViewName(view,shadow?"pbr_directional_depth":"pbr_occlusion_depth");
    bgfx::setViewMode(view,bgfx::ViewMode::Sequential);bgfx::setViewRect(view,0,0,w,h);
    bgfx::setViewFrameBuffer(view,buffer);bgfx::setViewTransform(view,nullptr,nullptr);
    bgfx::setViewClear(view,BGFX_CLEAR_DEPTH,uint32_t(0),1.f);bgfx::touch(view);
    for(const auto & draw:draws) {
      if(!draw.indexCount)continue;
      const auto matrix=shadow?frame.renderStates[draw.renderStateSlot].model*this->pbrShadowWorldToClip:SbMatrix::identity();
      bgfx::setTransform(shadow?static_cast<const void *>(matrix.getValue()):static_cast<const void *>(draw.mvp));
      bgfx::setVertexBuffer(0,vb);bgfx::setIndexBuffer(ib,draw.firstIndex,draw.indexCount);
      // Opaque two-sided caster/depth profile; the material shader determines
      // visible shading. No screen-space or alpha-tested draws are admitted.
      bgfx::setState(BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS|
        (shadow?0:(drawState(draw)&BGFX_STATE_CULL_MASK)));
      bgfx::submit(view,this->pbrDepthProgram);
    }
    ++view;
  };
  if(this->pbrEffects.shadows&&this->pbrShadowDirty) {
    encode(this->pbrShadowBuffer,this->pbrShadowSize,this->pbrShadowSize,true);
    this->pbrShadowDirty=false;++this->pbrShadowPasses;
  }
  if(this->pbrEffects.ambientOcclusion&&this->pbrCameraDepthDirty) {
    encode(this->pbrCameraDepthBuffer,this->pbrDepthWidth,this->pbrDepthHeight,false);
    this->pbrCameraDepthDirty=false;++this->pbrCameraDepthPasses;
  }
  return view;
}
void CoinBgfxBackend::bindPbrEffects(const CoinBgfxDraw & draw, bool reflected) {
  const auto set=[&](const char * name,const void * value){bgfx::setUniform(this->pbrUniforms.at(name),value);};
  set("u_pbrEffectViewToWorld",draw.pbrViewToWorld.getValue());
  set("u_pbrShadowWorldToClip",this->pbrShadowWorldToClip.getValue());
  set("u_pbrEffectProjection",this->pbrEffectProjection.getValue());
  set("u_pbrEffectInverseProjection",this->pbrEffectInverseProjection.getValue());
  const float shadow[4]={this->pbrEffects.shadows?1.f:0.f,1.f/std::max(1u,this->pbrShadowSize),this->pbrEffects.shadowBias,0};
  const float ao[4]={this->pbrEffects.ambientOcclusion&&!reflected?1.f:0.f,this->pbrEffects.occlusionRadius,this->pbrEffects.occlusionStrength,0};
  set("u_pbrShadowOptions",shadow);set("u_pbrAoOptions",ao);
  bgfx::setTexture(8,this->pbrUniforms.at("s_pbrDirectionalDepth"),this->pbrEffects.shadows?bgfx::getTexture(this->pbrShadowBuffer,0):this->defaultTexture);
  bgfx::setTexture(9,this->pbrUniforms.at("s_pbrCameraDepth"),this->pbrEffects.ambientOcclusion?bgfx::getTexture(this->pbrCameraDepthBuffer,0):this->defaultTexture);
}
void CoinBgfxBackend::destroyPbrEffects() {
  for(auto h:{this->pbrShadowBuffer,this->pbrCameraDepthBuffer})if(bgfx::isValid(h))bgfx::destroy(h);
  if(bgfx::isValid(this->pbrDepthProgram))bgfx::destroy(this->pbrDepthProgram);
  this->pbrShadowBuffer=this->pbrCameraDepthBuffer=BGFX_INVALID_HANDLE;this->pbrDepthProgram=BGFX_INVALID_HANDLE;
  this->pbrEffectIdentity.reset();this->pbrShadowDirty=this->pbrCameraDepthDirty=true;
  this->pbrShadowSize=this->pbrDepthWidth=this->pbrDepthHeight=0;
}
