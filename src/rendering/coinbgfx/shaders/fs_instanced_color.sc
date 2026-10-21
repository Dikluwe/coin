$input v_color0, v_coinClipDepth

#include <bgfx_shader.sh>
#include "coin_depth.sh"
#ifdef COIN_SURFACE_SAMPLE_DEPTH
#include "coin_sample_depth.sh"
#endif

void main()
{
#ifdef COIN_SURFACE_SAMPLE_DEPTH
  vec2 sampleClip = coinSurfaceSampleClip(v_coinClipDepth);
  gl_FragDepth = coinWindowDepth(sampleClip.x / sampleClip.y);
#elif !COIN_NATIVE_SURFACE_DEPTH
  gl_FragDepth = coinWindowDepth(coinSurfaceClipDepth(v_coinClipDepth, gl_FragCoord.z));
#endif
  gl_FragColor = v_color0;
}
