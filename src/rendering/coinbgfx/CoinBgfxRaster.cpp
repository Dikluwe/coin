// CoinRender BGFX Raster responsibilities.
#include "rendering/coinbgfx/CoinBgfxBackendInternal.h"

using namespace coin_bgfx_internal;

bool
CoinBgfxBackend::prepareRasterImageResources()
{
  if (!bgfx::isValid(this->rasterImageProgram)) this->rasterImageProgram = createRasterImageProgram();
  if (!bgfx::isValid(this->rasterPrimarySampler))
    this->rasterPrimarySampler = bgfx::createUniform("s_rasterPrimary", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(this->rasterCoordinatesUniform))
    this->rasterCoordinatesUniform = bgfx::createUniform("u_rasterCoordinates", bgfx::UniformType::Vec4, 8);
  if (!bgfx::isValid(this->rasterExtentUniform))
    this->rasterExtentUniform = bgfx::createUniform("u_rasterExtent", bgfx::UniformType::Vec4);
  if (bgfx::isValid(this->rasterImageProgram) && bgfx::isValid(this->rasterPrimarySampler) &&
      bgfx::isValid(this->rasterCoordinatesUniform) && bgfx::isValid(this->rasterExtentUniform)) return true;
  this->lastError = "BGFX image prepass program allocation failed";
  return false;
}

CoinRenderSubmitResult
CoinBgfxBackend::encodeRasterImages(const CoinRenderFramePlan & frame,
  const std::vector<bgfx::TextureHandle> & textures, std::vector<bgfx::TextureHandle> & owned)
{
  this->rasterImageOutputs.clear();
  bool needed = false;
  size_t outputCount = 0;
  uint64_t bytes = 0;
  for (const auto & state : frame.renderStates) {
    if (state.rasterTextureSourceSlot == UINT32_MAX) continue;
    needed = true;
    if (++outputCount > 4096)
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited images exceed 4096 output resources"};
    if (!state.rasterPixels || state.rasterTextureSourceSlot >= frame.renderStates.size() ||
        !state.hasTexture || state.textureImageSlot >= frame.textures.size() ||
        state.textureImageSlot >= textures.size() ||
        frame.renderStates[state.rasterTextureSourceSlot].rasterTextureSourceSlot != UINT32_MAX ||
        frame.renderStates[state.rasterTextureSourceSlot].rasterPixels)
      return {CoinRenderBackendStatus::UNSUPPORTED, "Invalid inherited image source or payload"};
    const auto & payload = frame.textures[state.textureImageSlot];
    bytes += uint64_t(payload.width) * payload.height * 16;
    if (bytes > 128ull * 1024ull * 1024ull)
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited image outputs exceed 128 MiB"};
    const auto & source = frame.renderStates[state.rasterTextureSourceSlot];
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      const auto layer = coin_render_texture_unit(source, unit);
      const auto & raw = state.rasterTextureCoordinates[unit];
      for (int c = 0; c < 4; ++c) if (!std::isfinite(raw[c]))
        return {CoinRenderBackendStatus::UNSUPPORTED, "Non-finite raster texture coordinate"};
      if (!layer.enabled) continue;
      if (layer.imageSlot >= textures.size() || layer.samplerSlot >= frame.samplers.size() ||
          !bgfx::isValid(textures[layer.imageSlot]))
        return {CoinRenderBackendStatus::UNSUPPORTED, "Invalid inherited image GPU resource"};
      SbVec4f transformed;
      layer.matrix.multVecMatrix(raw, transformed);
      for (int c = 0; c < 4; ++c) if (!std::isfinite(transformed[c]))
        return {CoinRenderBackendStatus::UNSUPPORTED, "Non-finite raster texture transform"};
      if (!frame.textures[layer.imageSlot].cubeFaceSize && source.textureProjection == CoinRenderTextureProjection::PROJECTIVE &&
          (transformed[3] == 0 || !std::isfinite(transformed[0]/transformed[3]) ||
           !std::isfinite(transformed[1]/transformed[3]) || !std::isfinite(transformed[2]/transformed[3])))
        return {CoinRenderBackendStatus::UNSUPPORTED, "Invalid raster texture projection"};
    }
  }
  if (!needed) return {};
  const auto * caps = bgfx::getCaps();
  const uint64_t flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
  if (!caps || !(caps->supported & BGFX_CAPS_COMPUTE) || caps->limits.maxTextureSamplers < 10 ||
      !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE) ||
      !bgfx::isTextureValid(1, false, 1, bgfx::TextureFormat::RGBA32F, flags))
    return {CoinRenderBackendStatus::UNSUPPORTED, "BGFX inherited images require RGBA32F compute storage and nine prepass textures"};
  if (!this->prepareRasterImageResources())
    return {CoinRenderBackendStatus::BACKEND_ERROR, this->lastError};
  // Allocate the complete bounded output set before encoding any dispatch.
  for (size_t slot = 0; slot < frame.renderStates.size(); ++slot) {
    const auto & state = frame.renderStates[slot];
    if (state.rasterTextureSourceSlot == UINT32_MAX) continue;
    const auto & payload = frame.textures[state.textureImageSlot];
    if (!payload.width || !payload.height || payload.width > caps->limits.maxTextureSize ||
        payload.height > caps->limits.maxTextureSize)
      return {CoinRenderBackendStatus::UNSUPPORTED, "Inherited image output exceeds adapter extent"};
    auto texture = bgfx::createTexture2D(uint16_t(payload.width), uint16_t(payload.height),
      false, 1, bgfx::TextureFormat::RGBA32F, flags);
    if (!bgfx::isValid(texture))
      return {CoinRenderBackendStatus::OUT_OF_MEMORY, "BGFX image RGBA32F allocation failed"};
    owned.push_back(texture);
    this->rasterImageOutputs[uint32_t(slot)] = texture;
    this->samplingBoundTextureSizes[texture.idx] = {payload.width, payload.height};
  }
  // DIRECT producers reuse this backend's view block. A previous opaque
  // render leaves its framebuffer and clear flags on viewBase; OpenGL applies
  // that clear on view entry even for a compute dispatch. Start an isolated
  // compute view so sampling the producer cannot clear its attachment first.
  bgfx::resetView(this->viewBase);
  bgfx::setViewName(this->viewBase, "image_inherited_rgba32f");
  bgfx::setViewMode(this->viewBase, bgfx::ViewMode::Sequential);
  for (const auto & entry : this->rasterImageOutputs) {
    const auto & state = frame.renderStates[entry.first];
    const auto & source = frame.renderStates[state.rasterTextureSourceSlot];
    const auto & payload = frame.textures[state.textureImageSlot];
    float params[8][4] = {}, blends[8][4] = {}, sizes[8][4] = {}, coordinates[8][4] = {};
    for (size_t unit = 0; unit < 8; ++unit) {
      const auto layer = coin_render_texture_unit(source, unit);
      SbVec4f transformed;
      layer.matrix.multVecMatrix(state.rasterTextureCoordinates[unit], transformed);
      for (int c = 0; c < 4; ++c) coordinates[unit][c] = transformed[c];
      params[unit][0] = layer.enabled ? 1.0f : 0.0f;
      params[unit][1] = float(layer.model);
      params[unit][3] = float(source.textureProjection);
      std::copy(layer.blendColor, layer.blendColor + 4, blends[unit]);
      auto texture = this->defaultTexture;
      uint32_t samplerFlags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
      if (layer.enabled) {
        texture = textures[layer.imageSlot];
        const auto & image = frame.textures[layer.imageSlot];
        const auto & sampler = frame.samplers[layer.samplerSlot];
        samplerFlags = BGFX_SAMPLER_MIP_POINT;
        if (sampler.wrapS == CoinRenderTextureWrap::CLAMP) samplerFlags |= BGFX_SAMPLER_U_CLAMP;
        if (sampler.wrapT == CoinRenderTextureWrap::CLAMP) samplerFlags |= BGFX_SAMPLER_V_CLAMP;
        if (sampler.filter == CoinRenderTextureFilter::NEAREST)
          samplerFlags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
        params[unit][2] = image.gpuToken && !image.cubeFaceSize && !caps->originBottomLeft ? 1.0f : 0.0f;
        sizes[unit][0] = float(image.width); sizes[unit][1] = float(image.height);
        sizes[unit][2] = float(sampler.filter);
        if (image.cubeFaceSize) {
          sizes[unit][0] = float(image.cubeFaceSize); sizes[unit][1] = image.cubeMipmapped ? 1.0f : 0.0f;
          sizes[unit][2] += 8; sizes[unit][3] = 0;
          samplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        }
        if (image.volumeDepth) {
          sizes[unit][0] = float(image.volumeWidth); sizes[unit][1] = float(image.volumeHeight);
          sizes[unit][2] += 4;
          sizes[unit][3] = float(image.volumeDepth * 16 + unsigned(sampler.wrapS) +
            2 * unsigned(sampler.wrapT) + 4 * unsigned(sampler.wrapR) + (image.volumeMipmapped ? 8 : 0));
          samplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        }
      }
      bgfx::setTexture(uint8_t(unit + 2), unit == 0 ? this->textureSampler :
        this->extraTextureSamplers[unit - 1], texture, 0, 1, 0, 1, samplerFlags);
    }
    const float extent[4] = {float(payload.width), float(payload.height), 0, 0};
    bgfx::setUniform(this->rasterExtentUniform, extent);
    bgfx::setUniform(this->rasterCoordinatesUniform, coordinates, 8);
    bgfx::setUniform(this->textureParamsUniform, params, 8);
    bgfx::setUniform(this->samplingTextureSizesUniform, sizes, 8);
    bgfx::setUniform(this->textureBlendUniform, blends, 8);
    bgfx::setUniform(this->textureCombineUniform, source.textureCombines.data(), 32);
    bgfx::setTexture(1, this->rasterPrimarySampler, textures[state.textureImageSlot],
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
      BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT);
    bgfx::setImage(0, entry.second, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
    bgfx::dispatch(this->viewBase, this->rasterImageProgram, (payload.width + 7) / 8, (payload.height + 7) / 8);
  }
  return {};
}
