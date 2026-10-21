$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>
#include "coin_depth.sh"
#include "coin_surface.sh"
#ifdef COIN_SURFACE_SAMPLE_DEPTH
#include "coin_sample_depth.sh"
#endif

void main()
{
#ifdef COIN_SURFACE_SAMPLE_DEPTH
  vec2 sampleClip = coinSurfaceSampleClip(v_coinClipDepth);
  gl_FragDepth = coinWindowDepth(sampleClip.x / sampleClip.y);
#elif !COIN_NATIVE_SURFACE_DEPTH
  gl_FragDepth = coinWindowDepth(v_coinClipDepth.x / v_coinClipDepth.y);
#endif
  gl_FragColor = coinSurfaceColor(gl_FragCoord.xy, v_color0, v_texcoord0, v_viewPosition, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1, v_ambient, v_specular);
}
