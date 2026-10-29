$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1
#include <bgfx_shader.sh>
#include "coin_pbr_output.sh"
SAMPLER2D(s_pbrRadiance, 0);
SAMPLER2D(s_readDepth, 1);
SAMPLER2D(s_pbrBackground, 2);
SAMPLER2D(s_pbrShadowCatcher, 3);
uniform vec4 u_pbrOutput; // xy: inverse target size; z: exposure multiplier.
uniform vec4 u_pbrBackgroundMode; // mode; invisible receiver; framebuffer originBottomLeft
uniform vec4 u_pbrBackgroundBottom;
uniform vec4 u_pbrBackgroundTop;
uniform vec4 u_pbrBackgroundRect; // screen rectangle origin xy, extent zw
float backgroundLinear(float c) {return c<=0.04045?c/12.92:pow((c+0.055)/1.055,2.4);}
vec3 backgroundLinear3(vec3 c) {return vec3(backgroundLinear(c.r),backgroundLinear(c.g),backgroundLinear(c.b));}
void main() {
  vec2 uv=gl_FragCoord.xy*u_pbrOutput.xy;
  gl_FragDepth=texture2D(s_readDepth,uv).r;
  vec4 scene=texture2D(s_pbrRadiance,uv);
  vec3 color=coinPbrNeutral(scene.rgb*u_pbrOutput.z);
  float shadow=u_pbrBackgroundMode.y>0.5?clamp(texture2D(s_pbrShadowCatcher,uv).r,0.0,1.0):0.0;
  float alpha=1.0;
  float mode=u_pbrBackgroundMode.x;
  if(mode>0.5&&abs(mode-4.0)>0.1) {
    vec2 screen=vec2(uv.x,u_pbrBackgroundMode.z>0.5?1.0-uv.y:uv.y);
    vec4 backdrop=vec4(backgroundLinear3(u_pbrBackgroundBottom.rgb),1.0);
    if(abs(mode-2.0)<0.1)
      backdrop.rgb=mix(backgroundLinear3(u_pbrBackgroundTop.rgb),backdrop.rgb,screen.y);
    if(abs(mode-3.0)<0.1) {
      vec2 sampleUv=(screen-u_pbrBackgroundRect.xy)/u_pbrBackgroundRect.zw;
      if(all(greaterThanEqual(sampleUv,vec2_splat(0.0)))&&all(lessThanEqual(sampleUv,vec2_splat(1.0)))) {
        vec4 image=texture2D(s_pbrBackground,sampleUv); // hardware decodes sRGB before filtering
        backdrop.rgb=backdrop.rgb*(1.0-image.a)+image.rgb; // filtered premultiplied linear image
      }
    }
    if(abs(mode-5.0)<0.1) {
      // Export boundary is straight sRGB with linear alpha. A black shadow
      // contributes coverage, while opaque surfaces retain their own RGB.
      alpha=scene.a+(1.0-scene.a)*shadow;
      color=alpha>0.0?color*scene.a/alpha:vec3_splat(0.0);
    }else {
      // This coupled profile is opaque. A clear may have alpha=1, so geometry
      // coverage comes from its depth, not the host clear's alpha.
      float coverage=gl_FragDepth<1.0?scene.a:0.0;
      color=mix(backdrop.rgb*(1.0-shadow),color,coverage);
      alpha=1.0;
    }
  }else color*=1.0-shadow;
  gl_FragColor=vec4(coinPbrSrgb(color.r),coinPbrSrgb(color.g),coinPbrSrgb(color.b),alpha);
}
