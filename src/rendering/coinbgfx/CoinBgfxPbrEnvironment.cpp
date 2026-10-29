// Global isotropic and bounded VNDF anisotropic IBL: CPU snapshots belong to the target, GPU resources to BGFX.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"
#include <Inventor/environment/CoinEnvironment.h>
#include <Inventor/environment/CoinEnvironmentAnisotropy.h>
#include "coin_bgfx_fs_pbr_array_ibl_spirv.h"
#include <mutex>
#include "coin_bgfx_vs_pbr_spirv.h"
#include "coin_bgfx_fs_pbr_ibl_spirv.h"
using namespace coin_bgfx_internal;

bool CoinBgfxBackend::preparePbrEnvironment(CoinRenderTargetP & target, bool needsAnisotropy)
{
  this->pbrIblActive=false;
  if(target.options.pbrAnisotropicEnvironmentSamples>128 || !target.options.pbrAnisotropyCpuBudgetBytes) {this->lastError="Invalid anisotropic source sampling bound/CPU budget";return false;}
  const auto & options=target.options;
  if(!options.pbrEnvironmentBundle.getLength() && !target.pbrEnvironmentSnapshot) return true;
  if(!target.pbrEnvironmentSnapshot) {
    auto candidate=std::make_shared<CoinEnvironment>();SbString diagnostic;
    if(!candidate->read(options.pbrEnvironmentBundle,diagnostic)) {
      this->lastError=diagnostic.getString();return false;
    }
    // The source is immutable for this target. File replacement cannot mutate a live snapshot.
    target.pbrEnvironmentSnapshot=candidate;
  }
  // The product is renderer-independent and immutable. Share the default
  // preparation while another target retains it; file products stay target-owned.
  if(needsAnisotropy && options.pbrAnisotropicEnvironmentSamples && !target.pbrAnisotropyProducts) {
    static std::mutex mutex;static std::weak_ptr<const CoinEnvironmentAnisotropy> common;
    std::lock_guard<std::mutex> lock(mutex);
    std::shared_ptr<const CoinEnvironmentAnisotropy> product;
    if(!options.pbrAnisotropyProducts.getLength())product=common.lock();
    if(!product) {
      auto candidate=std::make_shared<CoinEnvironmentAnisotropy>();SbString diagnostic;
      if(options.pbrAnisotropyProducts.getLength()) {
        if(!candidate->read(options.pbrAnisotropyProducts,diagnostic,options.pbrAnisotropyCpuBudgetBytes)) {this->lastError=diagnostic.getString();return false;}
      } else {
        CoinEnvironmentAnisotropyOptions config;
        config.workingBudgetBytes=options.pbrAnisotropyCpuBudgetBytes;
        if(!candidate->prepare(config,*candidate,diagnostic)) {this->lastError=diagnostic.getString();return false;}
        ++this->pbrAnisotropyPreparations;
      }
      product=candidate;
    }
    if(product->bytes()+sizeof(CoinEnvironmentAnisotropy)>options.pbrAnisotropyCpuBudgetBytes) {
      this->lastError="Anisotropic lighting product exceeds CPU budget";return false;
    }
    target.pbrAnisotropyProducts=product;
    if(!options.pbrAnisotropyProducts.getLength())common=product;
  }
  const auto product=options.pbrAnisotropicEnvironmentSamples?target.pbrAnisotropyProducts:nullptr;
  if(product && product->bytes()+sizeof(CoinEnvironmentAnisotropy)>options.pbrAnisotropyCpuBudgetBytes) {this->lastError="Retained anisotropic lighting product exceeds CPU budget";return false;}
  const bool array=bool(product);
  const auto snapshot=target.pbrEnvironmentSnapshot;
  const auto & environment=*snapshot;
  this->pbrIblParameters[0]=options.pbrEnvironmentIntensity;
  this->pbrIblParameters[1]=float(environment.levels-1);
  this->pbrIblParameters[2]=std::cos(options.pbrEnvironmentRotation);
  this->pbrIblParameters[3]=std::sin(options.pbrEnvironmentRotation);
  this->pbrQuality[0]=options.pbrEnergyCompensation?1.f:0.f;
  this->pbrQuality[1]=options.pbrSpecularFilterVariance;
  this->pbrQuality[2]=options.pbrSpecularFilterThreshold;
  this->pbrAnisoSampling[0]=float(options.pbrAnisotropicEnvironmentSamples);
  this->pbrAnisoSampling[1]=float(environment.size);
  this->pbrAnisoSampling[2]=float(environment.sourceLevels-1);
  if(this->pbrEnvironment==snapshot && this->pbrAnisotropy==product && bgfx::isValid(this->pbrSpecularCube) &&
     bgfx::isValid(this->pbrDfgTexture) && bgfx::isValid(this->pbrAverageTexture)) {
    if(this->pbrEnvironmentGpuBytes>options.pbrEnvironmentBudgetBytes) {this->lastError="Retained IBL textures exceed GPU budget";return false;}
    this->pbrIblActive=true;return true;
  }
  const auto * caps=bgfx::getCaps();
  const uint32_t tableWidth=product?std::max(environment.lutSize,product->atlasWidth()):environment.lutSize;
  const uint32_t tableHeight=environment.lutSize+(product?product->atlasHeight():0);
  const uint32_t averageWidth=product?product->strengthSize:1;
  const uint32_t averageHeight=environment.lutSize+(product?product->roughnessSize:0);
  uint64_t bytes=uint64_t(tableWidth)*tableHeight*8+uint64_t(averageWidth)*averageHeight*8;
  for(unsigned level=0;level<environment.levels;++level) {
    const uint64_t edge=std::max(1u,environment.size>>level);bytes+=edge*edge*8*(array?12:6);
  }
  if(bytes>options.pbrEnvironmentBudgetBytes || this->pbrEnvironmentGpuBytes>options.pbrEnvironmentBudgetBytes-bytes ||
     environment.size>caps->limits.maxTextureSize || tableWidth>caps->limits.maxTextureSize || tableHeight>caps->limits.maxTextureSize ||
     averageWidth>caps->limits.maxTextureSize || averageHeight>caps->limits.maxTextureSize ||
     (array && !(caps->supported&BGFX_CAPS_TEXTURE_CUBE_ARRAY)) ||
     !bgfx::isTextureValid(0,true,array?2:1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_NONE) ||
     !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_NONE)) {
    this->lastError="IBL textures exceed the environment budget or adapter capabilities";return false;
  }
  if(!bgfx::isValid(this->pbrIblProgram))
    this->pbrIblProgram=createLayerProgram(coin_bgfx_vs_pbr_spirv,sizeof(coin_bgfx_vs_pbr_spirv),
                                        coin_bgfx_fs_pbr_ibl_spirv,sizeof(coin_bgfx_fs_pbr_ibl_spirv));
  if(array && !bgfx::isValid(this->pbrArrayIblProgram))
    this->pbrArrayIblProgram=createLayerProgram(coin_bgfx_vs_pbr_spirv,sizeof(coin_bgfx_vs_pbr_spirv),
      coin_bgfx_fs_pbr_array_ibl_spirv,sizeof(coin_bgfx_fs_pbr_array_ibl_spirv));
  const auto uniform=[&](const char * name,bgfx::UniformType::Enum type,uint16_t count=1) {
    if(this->pbrUniforms.find(name)==this->pbrUniforms.end())
      this->pbrUniforms.emplace(name,bgfx::createUniform(name,type,count));
  };
  if(array)for(const char * name:{"u_pbrLutLayout","u_pbrAnisoGrid","u_pbrAverageLayout","u_pbrAnisoSampling"})uniform(name,bgfx::UniformType::Vec4);
  uniform("u_pbrViewToWorld",bgfx::UniformType::Mat4);
  uniform("u_pbrIbl",bgfx::UniformType::Vec4);
  uniform("u_pbrQuality",bgfx::UniformType::Vec4);
  uniform("u_pbrIrradiance",bgfx::UniformType::Vec4,9);
  for(const char * name:{"s_pbrSpecular","s_pbrDfg","s_pbrAlbedoAverage"})uniform(name,bgfx::UniformType::Sampler);
  if(!bgfx::isValid(this->pbrIblProgram) || (array&&!bgfx::isValid(this->pbrArrayIblProgram))) {this->lastError="IBL shader allocation failed";return false;}
  for(const auto & entry:this->pbrUniforms)if(!bgfx::isValid(entry.second)) {
    this->lastError="IBL uniform allocation failed";return false;
  }
  const uint64_t flags=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_W_CLAMP;
  const auto specular=bgfx::createTextureCube(environment.size,true,array?2:1,bgfx::TextureFormat::RGBA16F,flags);
  const auto table=bgfx::createTexture2D(tableWidth,tableHeight,false,1,bgfx::TextureFormat::RGBA16F,flags);
  const auto average=bgfx::createTexture2D(averageWidth,averageHeight,false,1,bgfx::TextureFormat::RGBA16F,flags);
  struct Candidate {
    bgfx::TextureHandle a,b,c;bool committed=false;
    Candidate(bgfx::TextureHandle x,bgfx::TextureHandle y,bgfx::TextureHandle z):a(x),b(y),c(z){}
    ~Candidate(){if(!committed)for(auto handle:{a,b,c})if(bgfx::isValid(handle))bgfx::destroy(handle);}
  } candidate{specular,table,average};
  if(!bgfx::isValid(specular)||!bgfx::isValid(table)||!bgfx::isValid(average)) {
    this->pbrPrepareStatus=CoinRenderBackendStatus::OUT_OF_MEMORY;
    this->lastError="IBL texture allocation failed";return false;
  }
  const auto averageData=environment.albedoAverage();
  if(averageData.size()!=size_t(environment.lutSize)*8) {
    this->lastError="Invalid IBL albedo-average payload";return false;
  }
  for(int i=0;i<environment.sections.getLength();++i) {
    const auto & section=environment.sections[i];
    if(section.kind==CoinEnvironment::SPECULAR || (array&&section.kind==CoinEnvironment::SOURCE))
      bgfx::updateTextureCube(specular,array&&section.kind==CoinEnvironment::SPECULAR?1:0,uint8_t(section.face),uint8_t(section.level),0,0,
        uint16_t(section.width),uint16_t(section.height),bgfx::copy(environment.data(section),uint32_t(section.bytes)));
    else if(section.kind==CoinEnvironment::LUT)
      bgfx::updateTexture2D(table,0,0,0,0,uint16_t(section.width),uint16_t(section.height),
        bgfx::copy(environment.data(section),uint32_t(section.bytes)));
  }
  bgfx::updateTexture2D(average,0,0,0,0,1,uint16_t(environment.lutSize),
    bgfx::copy(averageData.constData(),uint32_t(averageData.size())));
  if(product) {
    bgfx::updateTexture2D(table,0,0,0,uint16_t(environment.lutSize),uint16_t(product->atlasWidth()),uint16_t(product->atlasHeight()),bgfx::copy(product->directional.constData(),uint32_t(product->directional.size())));
    bgfx::updateTexture2D(average,0,0,0,uint16_t(environment.lutSize),uint16_t(product->strengthSize),uint16_t(product->roughnessSize),bgfx::copy(product->average.constData(),uint32_t(product->average.size())));
  }
  const float layouts[3][4]={{float(tableWidth),float(tableHeight),float(environment.lutSize),float(environment.lutSize)},
    {product?float(product->viewSize):0,product?float(product->roughnessSize):0,product?float(product->strengthSize):0,product?float(product->azimuthSize):0},
    {float(averageWidth),float(averageHeight),float(environment.lutSize),float(environment.lutSize)}};
  std::memcpy(pbrAnisotropyLayout,layouts,sizeof(layouts));
  this->pbrEnvironmentArray=array;this->pbrAnisotropy=product;
  for(int i=0;i<9;++i)for(int c=0;c<3;++c)this->pbrIrradiance[i][c]=environment.irradiance[i][c];
  for(auto handle:{this->pbrSpecularCube,this->pbrDfgTexture,this->pbrAverageTexture})
    if(bgfx::isValid(handle))bgfx::destroy(handle);
  this->pbrSpecularCube=specular;this->pbrDfgTexture=table;this->pbrAverageTexture=average;
  this->pbrEnvironment=snapshot;this->pbrEnvironmentGpuBytes=bytes;++this->pbrEnvironmentUploads;
  this->pbrIblActive=true;candidate.committed=true;
  return true;
}

void CoinBgfxBackend::bindPbrEnvironment(const CoinBgfxDraw & draw)
{
  if(this->pbrEnvironmentArray) {
    const char * names[3]={"u_pbrLutLayout","u_pbrAnisoGrid","u_pbrAverageLayout"};
    for(int i=0;i<3;++i)bgfx::setUniform(this->pbrUniforms.at(names[i]),this->pbrAnisotropyLayout[i]);
    bgfx::setUniform(this->pbrUniforms.at("u_pbrAnisoSampling"),this->pbrAnisoSampling);
  }
  bgfx::setUniform(this->pbrUniforms.at("u_pbrViewToWorld"),draw.pbrViewToWorld.getValue());
  bgfx::setUniform(this->pbrUniforms.at("u_pbrIbl"),this->pbrIblParameters);
  bgfx::setUniform(this->pbrUniforms.at("u_pbrQuality"),this->pbrQuality);
  bgfx::setUniform(this->pbrUniforms.at("u_pbrIrradiance"),this->pbrIrradiance,9);
  bgfx::setTexture(0,this->pbrUniforms.at("s_pbrSpecular"),this->pbrSpecularCube);
  bgfx::setTexture(1,this->pbrUniforms.at("s_pbrDfg"),this->pbrDfgTexture);
  bgfx::setTexture(2,this->pbrUniforms.at("s_pbrAlbedoAverage"),this->pbrAverageTexture);
}

void CoinBgfxBackend::destroyPbrEnvironment()
{
  for(auto handle:{this->pbrSpecularCube,this->pbrDfgTexture,this->pbrAverageTexture})
    if(bgfx::isValid(handle))bgfx::destroy(handle);
  for(auto handle:{this->pbrIblProgram,this->pbrArrayIblProgram})if(bgfx::isValid(handle))bgfx::destroy(handle);
  this->pbrSpecularCube=this->pbrDfgTexture=this->pbrAverageTexture=BGFX_INVALID_HANDLE;
  this->pbrIblProgram=this->pbrArrayIblProgram=BGFX_INVALID_HANDLE;this->pbrEnvironment.reset();
  this->pbrAnisotropy.reset();this->pbrEnvironmentArray=false;
  this->pbrEnvironmentGpuBytes=0;this->pbrIblActive=false;
}

uint64_t CoinBgfxBackend::getPbrAnisotropyProductBytes() const {return pbrAnisotropy?pbrAnisotropy->bytes():0;}
