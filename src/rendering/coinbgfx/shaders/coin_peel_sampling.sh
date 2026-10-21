// The selected sample is supplied by the draw's explicit sample mask.
uniform vec4 u_depthInfo;
#if COIN_PEEL_MSAA
#ifdef COIN_SHADOW_MULTITEXTURE
SAMPLER2DMS(s_prevDepth, 8);
SAMPLER2DMS(s_opaqueDepth, 9);
#else
SAMPLER2DMS(s_prevDepth, 0);
SAMPLER2DMS(s_opaqueDepth, 1);
#endif
#define coinPeelRead(_sampler, _uv) texelFetch(_sampler, ivec2(gl_FragCoord.xy), int(u_depthInfo.z)).x
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_SPIRV
#define coinPeelClip(_value) EvaluateAttributeAtSample(_value, int(u_depthInfo.z))
#else
#define coinPeelClip(_value) interpolateAtSample(_value, int(u_depthInfo.z))
#endif
#else
#ifdef COIN_SHADOW_MULTITEXTURE
SAMPLER2D(s_prevDepth, 8);
SAMPLER2D(s_opaqueDepth, 9);
#else
SAMPLER2D(s_prevDepth, 0);
SAMPLER2D(s_opaqueDepth, 1);
#endif
#define coinPeelRead(_sampler, _uv) texture2D(_sampler, _uv).x
#define coinPeelClip(_value) _value
#endif

// Shader coverage is local to this fragment. Keeping the fixed-function mask
// unrestricted on GL prevents it from leaking into BGFX's later MRT clear draw.
#if COIN_PEEL_MSAA && BGFX_SHADER_LANGUAGE_GLSL
#define coinPeelCoverage() gl_SampleMask[0] = 1 << int(u_depthInfo.z)
#else
#define coinPeelCoverage()
#endif
