$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1
#include <bgfx_shader.sh>
SAMPLER2DMS(s_stereoLeft, 0);
SAMPLER2DMS(s_stereoRight, 1);
SAMPLER2DMS(s_stereoLeftDepth, 2);
SAMPLER2DMS(s_stereoRightDepth, 3);
uniform vec4 u_stereoInfo;
uniform vec4 u_stereoViewport;
uniform vec4 u_depthInfo;
#define COIN_STEREO_FETCH(s) texelFetch(s, ivec2(gl_FragCoord.xy), int(u_depthInfo.w))
#include "coin_stereo.sh"
