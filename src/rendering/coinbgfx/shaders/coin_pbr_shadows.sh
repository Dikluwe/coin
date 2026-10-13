// Per-light visibility applies only to direct GGX radiance.
SAMPLER2D(s_shadow0, 8);
uniform mat4 u_shadowViewToClip0;
uniform mat4 u_shadowViewToLight0;
uniform vec4 u_shadowParams0;
uniform vec4 u_shadowMeta0;
SAMPLER2D(s_shadow1, 9);
uniform mat4 u_shadowViewToClip1;
uniform mat4 u_shadowViewToLight1;
uniform vec4 u_shadowParams1;
uniform vec4 u_shadowMeta1;
SAMPLER2D(s_shadow2, 10);
uniform mat4 u_shadowViewToClip2;
uniform mat4 u_shadowViewToLight2;
uniform vec4 u_shadowParams2;
uniform vec4 u_shadowMeta2;
SAMPLER2D(s_shadow3, 11);
uniform mat4 u_shadowViewToClip3;
uniform mat4 u_shadowViewToLight3;
uniform vec4 u_shadowParams3;
uniform vec4 u_shadowMeta3;
SAMPLER2D(s_shadow4, 12);
uniform mat4 u_shadowViewToClip4;
uniform mat4 u_shadowViewToLight4;
uniform vec4 u_shadowParams4;
uniform vec4 u_shadowMeta4;
SAMPLER2D(s_shadow5, 13);
uniform mat4 u_shadowViewToClip5;
uniform mat4 u_shadowViewToLight5;
uniform vec4 u_shadowParams5;
uniform vec4 u_shadowMeta5;
SAMPLER2D(s_shadow6, 14);
uniform mat4 u_shadowViewToClip6;
uniform mat4 u_shadowViewToLight6;
uniform vec4 u_shadowParams6;
uniform vec4 u_shadowMeta6;
SAMPLER2D(s_shadow7, 15);
uniform mat4 u_shadowViewToClip7;
uniform mat4 u_shadowViewToLight7;
uniform vec4 u_shadowParams7;
uniform vec4 u_shadowMeta7;
uniform vec4 u_shadowLightIndices;
uniform vec4 u_shadowLightIndicesExtra;
uniform vec4 u_shadowDiagnostic;
#include "coin_shadow_visibility.sh"
uniform vec4 u_pbrShadowBias[8];
float coinPbrDepthBias(vec3 position, mat4 viewToLight, vec4 meta, vec4 bias) {
  vec3 p = mul(viewToLight, vec4(position, 1.0)).xyz;
  // The geometric normal is independent of smoothing and material normal maps.
  vec3 geometric = cross(dFdx(p), dFdy(p));
  float scale = max(abs(geometric.x),max(abs(geometric.y),abs(geometric.z)));
  if (scale == 0.0) return 0.0;
  geometric = normalize(geometric / scale);
  vec3 direction = meta.x > 0.5 ? normalize(-p) : vec3(0.0,0.0,1.0);
  float cosine = abs(dot(geometric, direction));
  float slope = min(4.0, sqrt(max(0.0,1.0-cosine*cosine))/max(cosine,0.001));
  float footprint = bias.x * (meta.x > 0.5 ? max(0.0,-p.z) : 1.0);
  return footprint * (bias.y + bias.z * slope);
}
// PCF compares individual axial light-space depths before averaging. Moment
// maps store radial distance for spots; undo the sample ray length first.
uniform vec4 u_pbrShadowFilter[8];
uniform vec4 u_pbrShadowProjection[8];
float coinPbrAxialDepth(sampler2D mapTexture,vec2 uv,vec4 params,vec4 meta,vec4 projection) {
  float moment=texture2D(mapTexture,uv).x;
  float distance=params.x+moment*(params.y-params.x);
  if(meta.x>0.5) {
    vec2 ray=(uv*2.0-1.0)/projection.xy;
    distance*=inversesqrt(1.0+dot(ray,ray));
  }
  return distance;
}
float coinPbrCompare(sampler2D mapTexture,vec2 uv,float receiver,vec4 params,vec4 meta,vec4 projection) {
  if(uv.x<0.0||uv.x>=1.0||uv.y<0.0||uv.y>=1.0)return 1.0;
  return receiver<=coinPbrAxialDepth(mapTexture,uv,params,meta,projection)?1.0:0.0;
}
vec2 coinPbrDiskTap(int index) {
  float radius=sqrt((float(index)+0.5)/16.0),angle=float(index)*2.39996323;
  return radius*vec2(cos(angle),sin(angle));
}
float coinPbrFilteredVisibility(vec3 position,mat4 viewToClip,mat4 viewToLight,
  vec4 params,vec4 meta,sampler2D mapTexture,float bias,vec4 filter,vec4 projection) {
  if(filter.x<0.5)return coinShadowVisibilityDepthBias(position,viewToClip,viewToLight,params,meta,mapTexture,bias);
  vec4 coord=mul(viewToClip,vec4(position,1.0));
  if(coord.w<=0.0)return 1.0;
  vec3 clip=coord.xyz/coord.w;
  vec2 uv=vec2(clip.x*.5+.5,meta.w>.5?clip.y*.5+.5:.5-clip.y*.5);
  if(uv.x<0.0||uv.x>=1.0||uv.y<0.0||uv.y>=1.0||clip.z<0.0||clip.z>1.0)return 1.0;
  vec3 lightPosition=mul(viewToLight,vec4(position,1.0)).xyz;
  float distance=-lightPosition.z;
  float receiver=distance-bias*(params.y-params.x);
  float visibility=0.0;
  if(filter.x<1.5) {
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
      visibility+=coinPbrCompare(mapTexture,uv+vec2(float(x),float(y))*filter.y*filter.z,receiver,params,meta,projection);
    visibility/=9.0;
  } else if(filter.w==0.0) {
    visibility=coinPbrCompare(mapTexture,uv,receiver,params,meta,projection);
  } else {
    float searchRadius=meta.x>.5
      ? filter.w*projection.x*.5*max(0.0,distance-params.x)/(distance*params.x)
      : filter.w*projection.x*.5*(projection.z-params.x);
    float blockers=0.0,blockerSum=0.0;
    for(int i=0;i<16;++i) {
      vec2 tap=uv+coinPbrDiskTap(i)*searchRadius;
      if(tap.x<0.0||tap.x>=1.0||tap.y<0.0||tap.y>=1.0)continue;
      float depth=coinPbrAxialDepth(mapTexture,tap,params,meta,projection);
      if(depth<receiver){blockers+=1.0;blockerSum+=depth;}
    }
    if(blockers==0.0)return 1.0;
    float blocker=blockerSum/blockers;
    float penumbra=filter.w*max(0.0,distance-blocker);
    if(meta.x>.5)penumbra/=blocker;
    float radius=penumbra*projection.x*.5/(meta.x>.5?distance:1.0);
    for(int i=0;i<16;++i)
      visibility+=coinPbrCompare(mapTexture,uv+coinPbrDiskTap(i)*radius,receiver,params,meta,projection);
    visibility/=16.0;
  }
  if(meta.x<.5&&meta.y>0.0) {
    float fade=min(1.0,exp(meta.z*position.z*abs(position.z)/(meta.y*meta.y)));
    visibility=1.0-(1.0-visibility)*fade;
  }
  return visibility;
}

float coinPbrVisibility(int lightIndex, vec3 position) {
  float visibility = 1.0;
  if (u_shadowLightIndices.x >= 0.0 && abs(u_shadowLightIndices.x - float(lightIndex)) < 0.5)
    visibility *= coinPbrFilteredVisibility(position, u_shadowViewToClip0, u_shadowViewToLight0, u_shadowParams0, u_shadowMeta0, s_shadow0, coinPbrDepthBias(position, u_shadowViewToLight0, u_shadowMeta0, u_pbrShadowBias[0]), u_pbrShadowFilter[0], u_pbrShadowProjection[0]);
  if (u_shadowLightIndices.y >= 0.0 && abs(u_shadowLightIndices.y - float(lightIndex)) < 0.5)
    visibility *= coinPbrFilteredVisibility(position, u_shadowViewToClip1, u_shadowViewToLight1, u_shadowParams1, u_shadowMeta1, s_shadow1, coinPbrDepthBias(position, u_shadowViewToLight1, u_shadowMeta1, u_pbrShadowBias[1]), u_pbrShadowFilter[1], u_pbrShadowProjection[1]);
  if (u_shadowLightIndices.z >= 0.0 && abs(u_shadowLightIndices.z - float(lightIndex)) < 0.5)
    visibility *= coinPbrFilteredVisibility(position, u_shadowViewToClip2, u_shadowViewToLight2, u_shadowParams2, u_shadowMeta2, s_shadow2, coinPbrDepthBias(position, u_shadowViewToLight2, u_shadowMeta2, u_pbrShadowBias[2]), u_pbrShadowFilter[2], u_pbrShadowProjection[2]);
  if (u_shadowLightIndices.w >= 0.0 && abs(u_shadowLightIndices.w - float(lightIndex)) < 0.5)
    visibility *= coinPbrFilteredVisibility(position, u_shadowViewToClip3, u_shadowViewToLight3, u_shadowParams3, u_shadowMeta3, s_shadow3, coinPbrDepthBias(position, u_shadowViewToLight3, u_shadowMeta3, u_pbrShadowBias[3]), u_pbrShadowFilter[3], u_pbrShadowProjection[3]);
  if (u_shadowLightIndicesExtra.x >= 0.0 && abs(u_shadowLightIndicesExtra.x - float(lightIndex)) < 0.5)
    visibility *= coinPbrFilteredVisibility(position, u_shadowViewToClip4, u_shadowViewToLight4, u_shadowParams4, u_shadowMeta4, s_shadow4, coinPbrDepthBias(position, u_shadowViewToLight4, u_shadowMeta4, u_pbrShadowBias[4]), u_pbrShadowFilter[4], u_pbrShadowProjection[4]);
  if (u_shadowLightIndicesExtra.y >= 0.0 && abs(u_shadowLightIndicesExtra.y - float(lightIndex)) < 0.5)
    visibility *= coinPbrFilteredVisibility(position, u_shadowViewToClip5, u_shadowViewToLight5, u_shadowParams5, u_shadowMeta5, s_shadow5, coinPbrDepthBias(position, u_shadowViewToLight5, u_shadowMeta5, u_pbrShadowBias[5]), u_pbrShadowFilter[5], u_pbrShadowProjection[5]);
  if (u_shadowLightIndicesExtra.z >= 0.0 && abs(u_shadowLightIndicesExtra.z - float(lightIndex)) < 0.5)
    visibility *= coinPbrFilteredVisibility(position, u_shadowViewToClip6, u_shadowViewToLight6, u_shadowParams6, u_shadowMeta6, s_shadow6, coinPbrDepthBias(position, u_shadowViewToLight6, u_shadowMeta6, u_pbrShadowBias[6]), u_pbrShadowFilter[6], u_pbrShadowProjection[6]);
  if (u_shadowLightIndicesExtra.w >= 0.0 && abs(u_shadowLightIndicesExtra.w - float(lightIndex)) < 0.5)
    visibility *= coinPbrFilteredVisibility(position, u_shadowViewToClip7, u_shadowViewToLight7, u_shadowParams7, u_shadowMeta7, s_shadow7, coinPbrDepthBias(position, u_shadowViewToLight7, u_shadowMeta7, u_pbrShadowBias[7]), u_pbrShadowFilter[7], u_pbrShadowProjection[7]);
  return visibility;
}
