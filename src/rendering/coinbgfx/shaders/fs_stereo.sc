$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1
#include <bgfx_shader.sh>
SAMPLER2D(s_stereoLeft, 0);
SAMPLER2D(s_stereoRight, 1);
SAMPLER2D(s_stereoLeftDepth, 2);
SAMPLER2D(s_stereoRightDepth, 3);
uniform vec4 u_stereoInfo;
uniform vec4 u_stereoViewport;
uniform vec4 u_depthInfo;
#define COIN_STEREO_FETCH(s) texture2D(s, gl_FragCoord.xy * u_stereoInfo.xy)
#include "coin_stereo.sh"
