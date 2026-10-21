$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>
SAMPLER2DMS(s_msaaColor, 0);
SAMPLER2DMS(s_msaaDepth, 1);
uniform vec4 u_depthInfo;

ivec2 coinMsaaCounts()
{
    uint width, height, colorSamples, depthSamples;
    s_msaaColorTexture.GetDimensions(width, height, colorSamples);
    s_msaaDepthTexture.GetDimensions(width, height, depthSamples);
    return ivec2(int(colorSamples), int(depthSamples));
}

#include "coin_msaa_resolve.sh"
