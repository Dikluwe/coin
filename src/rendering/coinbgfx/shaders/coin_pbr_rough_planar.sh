// Fixed 128-tap isotropic split-sum prefilter of the actual premultiplied
// planar image. N=V=R; a world proxy plane approximates scene depth.
uniform mat4 u_pbrRoughViewToCanonical;
uniform mat4 u_pbrRoughCanonicalToClip;
uniform vec4 u_pbrRoughNormalHeight;
bool coinPbrRoughFinite(vec3 x) {return all(lessThan(abs(x),vec3_splat(3.402823466e38)));}
float coinPbrRoughRadical(uint x) {
  x=(x<<16u)|(x>>16u);x=((x&0x00ff00ffu)<<8u)|((x&0xff00ff00u)>>8u);
  x=((x&0x0f0f0f0fu)<<4u)|((x&0xf0f0f0f0u)>>4u);x=((x&0x33333333u)<<2u)|((x&0xccccccccu)>>2u);
  x=((x&0x55555555u)<<1u)|((x&0xaaaaaaaau)>>1u);return float(x)*(1.0/4294967296.0);
}
vec4 coinPbrRoughPlanar(vec3 viewPosition,vec3 viewMirror,float roughness) {
  vec3 position=mul(u_pbrRoughViewToCanonical,vec4(viewPosition,1.0)).xyz;
  vec3 rawR=mul(u_pbrRoughViewToCanonical,vec4(viewMirror,0.0)).xyz;
  if(!coinPbrRoughFinite(position)||!coinPbrRoughFinite(rawR))return vec4_splat(0.0);
  float largest=max(abs(rawR.x),max(abs(rawR.y),abs(rawR.z)));
  if(largest==0.0)return vec4_splat(0.0);
  vec3 r=normalize(rawR/largest);
  vec3 tangent=normalize(cross(abs(r.z)<.75?vec3(0.0,0.0,1.0):vec3(0.0,1.0,0.0),r));
  vec3 bitangent=cross(r,tangent);
  float alpha=roughness*roughness,alphaSquared=alpha*alpha;
  vec4 accumulated=vec4_splat(0.0);float totalWeight=0.0;
  LOOP for(int i=0;i<128;++i) {
    float u=(float(i)+.5)*(1.0/128.0),phi=6.283185307179586*coinPbrRoughRadical(uint(i));
    float cosTheta=sqrt((1.0-u)/(1.0+(alphaSquared-1.0)*u));
    float sinTheta=sqrt(max(0.0,1.0-cosTheta*cosTheta));
    vec3 h=tangent*(cos(phi)*sinTheta)+bitangent*(sin(phi)*sinTheta)+r*cosTheta;
    vec3 l=reflect(-r,h);float weight=max(dot(r,l),0.0);
    // Missing samples retain fallback coverage, rather than renormalize away
    // source holes or turn transparent borders into a black radiance halo.
    totalWeight+=weight;
    if(weight<=0.0)continue;
    float denominator=dot(u_pbrRoughNormalHeight.xyz,l);
    if(denominator==0.0)continue;
    float distance=-dot(u_pbrRoughNormalHeight.xyz,position)/denominator;
    if(!(distance>0.0)||!(distance<3.402823466e38))continue;
    vec3 hit=position+l*distance;
    if(!coinPbrRoughFinite(hit))continue;
    vec4 projected=mul(u_pbrRoughCanonicalToClip,vec4(hit,1.0));
    if(!(projected.w>0.0)||!(projected.w<3.402823466e38))continue;
    vec2 uv=vec2(.5+.5*projected.x/projected.w,.5-.5*projected.y/projected.w);
    // Vulkan's source image is top-first. Other executors refuse this profile.
    if(!(uv.x>=0.0)||!(uv.x<=1.0)||!(uv.y>=0.0)||!(uv.y<=1.0))continue;
    vec4 sampleValue=texture2DLod(s_pbrReflection,uv,0.0);
    accumulated+=sampleValue*weight;
  }
  if(!(totalWeight>0.0))return vec4_splat(0.0);
  // RGBA remains premultiplied. Dividing RGB by coverage here would brighten
  // edges twice when the existing Fresnel/environment composition consumes it.
  return accumulated/totalWeight;
}
