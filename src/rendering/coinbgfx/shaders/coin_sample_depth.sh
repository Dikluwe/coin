// Explicit sample depth for custom MSAA intervals and polygon offset.
uniform vec4 u_depthInfo;
#if BGFX_SHADER_LANGUAGE_GLSL
#define coinSurfaceSampleClip(_value) interpolateAtSample(_value, gl_SampleID)
#define coinSurfaceSampleCoverage()
#else
#define coinSurfaceSampleClip(_value) EvaluateAttributeAtSample(_value, int(u_depthInfo.z))
#define coinSurfaceSampleCoverage()
#endif
