#ifndef COIN_RENDER_TEXTURE_COMBINE_CORE_H
#define COIN_RENDER_TEXTURE_COMBINE_CORE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderFloatCore.h"

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <Inventor/elements/SoTextureCombineElement.h>

// Four vec4 instructions per unit: active/RGB-op/alpha-op, RGB arguments/scale,
// alpha arguments/scale, constant. Sources 0..3 are primary/texture/constant/previous;
// operands 0..3 are color/complement/alpha/alpha-complement. No Coin enum reaches Infra.
COIN_RENDER_DLL_API CoinRenderTextureCombineSnapshot coin_render_replace_rgb_preserve_alpha();

COIN_RENDER_DLL_API bool coin_render_compile_combine(const SoTextureCombineElement::UnitData& raw,
                                        CoinRenderTextureCombineSnapshot& result,
                                        std::string& diagnostic);

COIN_RENDER_DLL_API bool coin_render_validate_combine(const CoinRenderTextureCombineSnapshot& program);

COIN_RENDER_DLL_API SbVec4f coin_render_combine_argument(float instruction, const SbVec4f& primary,
                                            const SbVec4f& texture, const SbVec4f& constant,
                                            const SbVec4f& previous);

COIN_RENDER_DLL_API SbVec4f coin_render_combine_operation(int operation, const SbVec4f& a, const SbVec4f& b,
                                             const SbVec4f& c);

COIN_RENDER_DLL_API SbVec4f coin_render_texture_combine(const CoinRenderTextureCombineSnapshot& program,
                                           const SbVec4f& primary, const SbVec4f& texture,
                                           const SbVec4f& previous);

// Conservative alpha proof; a false proof routes to blending, never drops alpha.
COIN_RENDER_DLL_API bool coin_render_combine_may_have_alpha(const CoinRenderTextureCombineSnapshot& program,
                                               bool primaryAlpha, bool textureAlpha,
                                               bool previousAlpha);
#endif
