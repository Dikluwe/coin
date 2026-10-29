// PBR executor resources; no scene traversal or semantic planning.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"
#include "coin_bgfx_vs_pbr_spirv.h"
#include "coin_bgfx_fs_pbr_spirv.h"
#include "coin_bgfx_fs_pbr_output_spirv.h"
#include "coin_bgfx_vs_pbr_output_spirv.h"
using namespace coin_bgfx_internal;

bool CoinBgfxBackend::hasPbr(const CoinRenderFramePlan & frame) const {
  for (const auto & draw : frame.draws)
    if (draw.renderStateSlot < frame.renderStates.size()) {
      const auto slot = frame.renderStates[draw.renderStateSlot].materialSlot;
      if (slot < frame.materials.size() && frame.materials[slot].model == CoinRenderMaterialModel::PBR)
        return true;
    }
  return false;
}

bool CoinBgfxBackend::preparePbr(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) {
  this->pbrPrepareStatus=CoinRenderBackendStatus::UNSUPPORTED;
  const auto refuse = [&](const char * text) { this->lastError=text; return false; };
  const float exposure = target.options.pbrExposureStops;
  if (!std::isfinite(exposure) || exposure < -16.f || exposure > 16.f)
    return refuse("PBR exposure must be finite in [-16,16] stops");
  this->pbrExposureMultiplier = std::exp2(exposure);
  if (bgfx::getRendererType()!=bgfx::RendererType::Vulkan ||
      (target.kind!=CoinRenderTargetP::KIND_OFFSCREEN && target.kind!=CoinRenderTargetP::KIND_WINDOW) ||
      this->sampleCount!=1 || frame.numPasses!=1 || frame.stereoMode || frame.managerFrame ||
      !frame.shadowGroups.empty() || frame.hasPortableShaderDraws() || this->canonicalCoverage)
    return refuse("Retained BGFX PBR requires native Vulkan, one sample/pass, no scene-manager effects or textures");
  for (const auto & draw:frame.draws) {
    const auto & state=frame.renderStates[draw.renderStateSlot];
    const auto & material=frame.materials[state.materialSlot];
    if (material.model!=CoinRenderMaterialModel::PBR || !coin_render_valid_pbr_material(material.pbr) ||
        draw.topology!=CoinRenderPrimitiveTopology::TRIANGLE_LIST || draw.renderLayer || draw.clearDepthBefore ||
        state.lightModel!=CoinRenderLightModel::PHONG || state.fogMode!=CoinRenderFogMode::NONE ||
        state.alphaTestFunction!=CoinRenderAlphaTestFunction::NONE || material.transparency!=0)
      return refuse("Retained BGFX PBR requires uniform opaque factors/textures and direct lights; mixed surfaces unsupported");
    for(int unit:{material.pbr.baseColorTextureUnit,material.pbr.ormTextureUnit,material.pbr.emissiveTextureUnit})if(unit>=0) {
      const auto layer=coin_render_texture_unit(state,unit);
      if(!layer.enabled||layer.imageSlot>=frame.textures.size()||layer.samplerSlot>=frame.samplers.size())
        return refuse("PBR role has no resolved image/sampler");
      const auto & image=frame.textures[layer.imageSlot];
      const auto expected=unit==material.pbr.ormTextureUnit?CoinRenderTextureFormat::RGBA8_LINEAR:CoinRenderTextureFormat::RGBA8_SRGB;
      if(image.format!=expected||image.gpuToken||image.producerId||image.cubeFaceSize||image.volumeDepth)
        return refuse("PBR roles require stored 2D images with their explicit color interpretation");
    }
    if(state.pbrNormalImageSlot!=UINT32_MAX &&
       (state.pbrNormalImageSlot>=frame.textures.size()||state.pbrNormalSamplerSlot>=frame.samplers.size()||
        frame.textures[state.pbrNormalImageSlot].format!=CoinRenderTextureFormat::RGBA8_LINEAR ||
        frame.textures[state.pbrNormalImageSlot].gpuToken || frame.textures[state.pbrNormalImageSlot].producerId ||
        frame.textures[state.pbrNormalImageSlot].cubeFaceSize || frame.textures[state.pbrNormalImageSlot].volumeDepth))
      return refuse("PBR normal role requires a resolved linear image/sampler");
    if(target.options.pbrEnvironmentBundle.getLength() && !CoinRenderTransformCore::rigidViewMatrix(state.view))
      return refuse("IBL requires a rigid camera view");
    const auto modelView=state.model*state.view;
    const float determinant=modelView.det4();
    if(!std::isfinite(determinant)||determinant==0||!CoinRenderTransformCore::finiteMatrix(modelView.inverse())||
       modelView[3][3]!=1 || modelView[0][3]!=0 || modelView[1][3]!=0 || modelView[2][3]!=0)
      return refuse("PBR requires a nonsingular affine model-view transform");
    for (uint32_t i=0;i<draw.geometry.indexCount;++i) {
      const auto index=frame.indices[draw.geometry.firstIndex+i];
      const SbVec3f normal(frame.vertices[index].normal);
      if(!std::isfinite(normal.dot(normal))||normal.dot(normal)<=1e-20f)
        return refuse("PBR faces require finite nonzero normals");
      if (frame.vertices[index].materialSlot!=state.materialSlot)
        return refuse("PBR requires uniform material per draw");
    }
  }
  if (!this->prepareFullscreenResources()) return false;
  if (!bgfx::isValid(this->pbrProgram))
    this->pbrProgram=createLayerProgram(coin_bgfx_vs_pbr_spirv,sizeof(coin_bgfx_vs_pbr_spirv),
                                      coin_bgfx_fs_pbr_spirv,sizeof(coin_bgfx_fs_pbr_spirv));
  if (!bgfx::isValid(this->pbrOutputProgram))
    this->pbrOutputProgram=createLayerProgram(coin_bgfx_vs_pbr_output_spirv,sizeof(coin_bgfx_vs_pbr_output_spirv),
                                            coin_bgfx_fs_pbr_output_spirv,sizeof(coin_bgfx_fs_pbr_output_spirv));
  const auto uniform=[&](const char * name,bgfx::UniformType::Enum type) {
    auto it=this->pbrUniforms.find(name);
    if(it==this->pbrUniforms.end())this->pbrUniforms.emplace(name,bgfx::createUniform(name,type));
  };
  for (const char * name:{"u_pbrBaseMetallic","u_pbrEmissionRoughness","u_pbrCamera","u_pbrTextureUnits",
       "u_pbrClearcoat","u_pbrTangentParity","u_pbrAnisotropy","u_pbrNormalParameters","u_pbrReflectionInfo",
       "u_pbrRoughNormalHeight","u_pbrOutput","u_pbrBackgroundMode","u_pbrBackgroundBottom","u_pbrBackgroundTop","u_pbrBackgroundRect"})
    uniform(name,bgfx::UniformType::Vec4);
  for (const char * name:{"u_pbrRoughViewToCanonical","u_pbrRoughCanonicalToClip","u_pbrVertexToView","u_pbrModelView","u_pbrNormalToView"}) uniform(name,bgfx::UniformType::Mat4);
  for (const char * name:{"s_pbrBase","s_pbrOrm","s_pbrNormal","s_pbrEmission","s_pbrReflection",
       "s_pbrRadiance","s_readDepth","s_pbrBackground","s_pbrShadowCatcher"}) uniform(name,bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->pbrProgram)||!bgfx::isValid(this->pbrOutputProgram))
    {this->pbrPrepareStatus=CoinRenderBackendStatus::BACKEND_ERROR;return refuse("PBR program allocation failed");}
  for (const auto & entry:this->pbrUniforms)if(!bgfx::isValid(entry.second)){this->pbrPrepareStatus=CoinRenderBackendStatus::OUT_OF_MEMORY;return refuse("PBR uniform allocation failed");}
  const int w=target.size[0],h=target.size[1];
  if (!bgfx::isValid(this->pbrFrameBuffer)||this->pbrWidth!=w||this->pbrHeight!=h) {
    const uint64_t flags=BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
      BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
    // Preserve the source PBR path's native depth format before output resolve.
    const auto depthFormat=(bgfx::getCaps()->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
      ? bgfx::TextureFormat::D24S8 : bgfx::TextureFormat::D32F;
    for (auto format:{bgfx::TextureFormat::RGBA16F,depthFormat})
      if(!bgfx::isTextureValid(1,false,1,format,flags))return refuse("PBR requires sampleable RGBA16F and native depth attachments");
    bgfx::TextureHandle attachments[2]={bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::RGBA16F,flags),
      bgfx::createTexture2D(w,h,false,1,depthFormat,flags)};
    if(!bgfx::isValid(attachments[0])||!bgfx::isValid(attachments[1])) {
      for(auto handle:attachments)if(bgfx::isValid(handle))bgfx::destroy(handle);
      this->pbrPrepareStatus=CoinRenderBackendStatus::OUT_OF_MEMORY;return refuse("PBR attachment allocation failed");
    }
    auto candidate=bgfx::createFrameBuffer(2,attachments,true);
    if(!bgfx::isValid(candidate)) {
      for(auto handle:attachments)bgfx::destroy(handle);
      this->pbrPrepareStatus=CoinRenderBackendStatus::OUT_OF_MEMORY;return refuse("PBR framebuffer allocation failed");
    }
    if(bgfx::isValid(this->pbrFrameBuffer))bgfx::destroy(this->pbrFrameBuffer);
    this->pbrFrameBuffer=candidate;this->pbrWidth=w;this->pbrHeight=h;
  }
  return this->preparePbrEffects(frame,target) && this->preparePbrReflection(frame,target) && this->preparePbrEnvironment(target,std::any_of(frame.materials.begin(),frame.materials.end(),[](const CoinRenderMaterialSnapshot & m){return m.model==CoinRenderMaterialModel::PBR&&m.pbr.anisotropy>0;}));
}

void CoinBgfxBackend::submitPbrDraw(bgfx::ViewId view,const CoinBgfxDraw & draw,const CoinRenderFramePlan & frame,const std::vector<bgfx::TextureHandle> & textures, bool reflected) {
  this->bindDrawLighting(draw,draw.viewport[3],draw.viewport[2]);
  const auto set=[&](const char * name,const void * values){bgfx::setUniform(this->pbrUniforms.at(name),values);};
  const float parity[4]={draw.pbrTangentParity,0,0,0};set("u_pbrTangentParity",parity);
  const bool receiver=!reflected && bgfx::isValid(pbrReflectionBuffer) && draw.sourceDrawSlot<pbrReflectionReceivers.size() && pbrReflectionReceivers[draw.sourceDrawSlot];
  float camera[4];std::copy_n(draw.pbrCamera,4,camera);camera[2]=receiver?1.f:0.f;
  set("u_pbrBaseMetallic",draw.pbrBaseMetallic);set("u_pbrEmissionRoughness",draw.pbrEmissionRoughness);
  set("u_pbrClearcoat",draw.pbrClearcoat);set("u_pbrCamera",camera);
  set("u_pbrModelView",draw.pbrModelView.getValue());set("u_pbrNormalToView",draw.pbrNormalToView.getValue());
  set("u_pbrVertexToView",draw.pbrModelView.getValue());
  const float zero[4]={};
  const auto & state=frame.renderStates[draw.renderStateSlot];
  const auto & material=frame.materials[state.materialSlot].pbr;
  const float units[4]={float(material.baseColorTextureUnit),float(material.ormTextureUnit),state.pbrNormalImageSlot!=UINT32_MAX?1.f:-1.f,float(material.emissiveTextureUnit)};
  const float authoredAnisotropy[4]={material.anisotropy,std::cos(material.anisotropyRotation),std::sin(material.anisotropyRotation),0};
  const float normals[4]={material.normalScale,material.normalYFlip?-1.f:1.f,0,0};
  const float reflectionInfo[4]={1.f/this->width,1.f/this->height,bgfx::getCaps()->originBottomLeft?1.f:0.f,receiver&&pbrRoughMapping.enabled?material.roughness:0};
  if(receiver&&pbrRoughMapping.enabled) {
    set("u_pbrRoughViewToCanonical",pbrRoughMapping.viewToCanonical);
    set("u_pbrRoughCanonicalToClip",pbrRoughMapping.canonicalToClip);
    set("u_pbrRoughNormalHeight",pbrRoughMapping.normalHeight);
  } else set("u_pbrRoughNormalHeight",zero);
  set("u_pbrTextureUnits",units);set("u_pbrReflectionInfo",receiver?reflectionInfo:zero);
  set("u_pbrAnisotropy",authoredAnisotropy);set("u_pbrNormalParameters",normals);
  const char * samplers[]={"s_pbrReflection","s_pbrBase","s_pbrOrm","s_pbrNormal","s_pbrEmission"};
  for(uint8_t i=0;i<5;++i)bgfx::setTexture(i+3,this->pbrUniforms.at(samplers[i]),this->defaultTexture);
  const auto bind=[&](uint8_t stage,const char * name,uint32_t image,uint32_t samplerSlot) {
    const auto & sampler=frame.samplers[samplerSlot];uint32_t flags=BGFX_SAMPLER_MIP_POINT;
    if(sampler.wrapS==CoinRenderTextureWrap::CLAMP)flags|=BGFX_SAMPLER_U_CLAMP;
    if(sampler.wrapT==CoinRenderTextureWrap::CLAMP)flags|=BGFX_SAMPLER_V_CLAMP;
    if(sampler.filter==CoinRenderTextureFilter::NEAREST)flags|=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT;
    if(sampler.filter==CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR)flags|=BGFX_SAMPLER_MIN_POINT;
    if(CoinRenderTextureSamplingCore::mipFilter(sampler.filter))flags&=~BGFX_SAMPLER_MIP_POINT;
    if(sampler.maxAnisotropy>1)flags|=BGFX_SAMPLER_MIN_ANISOTROPIC|BGFX_SAMPLER_MAG_ANISOTROPIC;
    bgfx::setTexture(stage,this->pbrUniforms.at(name),textures[image],flags);
  };
  const int roles[3]={material.baseColorTextureUnit,material.ormTextureUnit,material.emissiveTextureUnit};
  const char * names[3]={"s_pbrBase","s_pbrOrm","s_pbrEmission"};const uint8_t stages[3]={4,5,7};
  for(int role=0;role<3;++role)if(roles[role]>=0) {
    const auto layer=coin_render_texture_unit(state,roles[role]);bind(stages[role],names[role],layer.imageSlot,layer.samplerSlot);
  }
  if(state.pbrNormalImageSlot!=UINT32_MAX)bind(6,"s_pbrNormal",state.pbrNormalImageSlot,state.pbrNormalSamplerSlot);
  if(receiver)bgfx::setTexture(3,this->pbrUniforms.at("s_pbrReflection"),bgfx::getTexture(pbrReflectionBuffer,0),BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
    (pbrRoughMapping.enabled&&material.roughness>.045f?0:BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT));
  if (this->pbrIblActive) this->bindPbrEnvironment(draw);
  this->bindPbrEffects(draw,reflected);
  bgfx::submit(view,this->pbrIblActive ? (this->pbrEnvironmentArray?this->pbrArrayIblProgram:this->pbrIblProgram) : this->pbrProgram);
}

void CoinBgfxBackend::encodePbrOutput(bgfx::ViewId view,bgfx::FrameBufferHandle output) {
  bgfx::resetView(view);bgfx::setViewName(view,"pbr_neutral_output");
  bgfx::setViewMode(view,bgfx::ViewMode::Sequential);bgfx::setViewRect(view,0,0,this->width,this->height);
  bgfx::setViewFrameBuffer(view,output);bgfx::setViewClear(view,BGFX_CLEAR_NONE);bgfx::setViewTransform(view,nullptr,nullptr);
  const auto identity=SbMatrix::identity();bgfx::setTransform(identity.getValue());
  bgfx::setVertexBuffer(0,this->fullscreenVertexBuffer);bgfx::setIndexBuffer(this->fullscreenIndexBuffer);
  const float options[4]={1.f/this->width,1.f/this->height,this->pbrExposureMultiplier,0},zero[4]={},rect[4]={0,0,1,1};
  bgfx::setUniform(this->pbrUniforms.at("u_pbrOutput"),options);
  const float background[4]={this->pbrEffects.background?2.f:0.f,0,bgfx::getCaps()->originBottomLeft?1.f:0.f,0};
  const float top[4]={this->pbrEffects.backgroundTop[0],this->pbrEffects.backgroundTop[1],this->pbrEffects.backgroundTop[2],1};
  const float bottom[4]={this->pbrEffects.backgroundBottom[0],this->pbrEffects.backgroundBottom[1],this->pbrEffects.backgroundBottom[2],1};
  bgfx::setUniform(this->pbrUniforms.at("u_pbrBackgroundMode"),background);
  bgfx::setUniform(this->pbrUniforms.at("u_pbrBackgroundTop"),top);
  bgfx::setUniform(this->pbrUniforms.at("u_pbrBackgroundBottom"),bottom);
  bgfx::setUniform(this->pbrUniforms.at("u_pbrBackgroundRect"),rect);
  const uint32_t flags=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT;
  bgfx::setTexture(0,this->pbrUniforms.at("s_pbrRadiance"),bgfx::getTexture(this->pbrFrameBuffer,0),flags);
  bgfx::setTexture(1,this->pbrUniforms.at("s_readDepth"),bgfx::getTexture(this->pbrFrameBuffer,1),flags);
  bgfx::setTexture(2,this->pbrUniforms.at("s_pbrBackground"),this->defaultTexture,flags);
  bgfx::setTexture(3,this->pbrUniforms.at("s_pbrShadowCatcher"),this->defaultTexture,flags);
  bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_ALWAYS);
  bgfx::submit(view,this->pbrOutputProgram);
}

void CoinBgfxBackend::destroyPbr() {
  this->pbrPresentationIdentity.reset();
  this->destroyPbrReflection();
  this->destroyPbrEffects();
  this->destroyPbrEnvironment();
  for(auto handle:{this->pbrProgram,this->pbrOutputProgram})if(bgfx::isValid(handle))bgfx::destroy(handle);
  for(const auto & entry:this->pbrUniforms)if(bgfx::isValid(entry.second))bgfx::destroy(entry.second);
  this->pbrUniforms.clear();this->pbrProgram=this->pbrOutputProgram=BGFX_INVALID_HANDLE;
  if(bgfx::isValid(this->pbrFrameBuffer))bgfx::destroy(this->pbrFrameBuffer);
  this->pbrFrameBuffer=BGFX_INVALID_HANDLE;this->pbrWidth=this->pbrHeight=0;
}
