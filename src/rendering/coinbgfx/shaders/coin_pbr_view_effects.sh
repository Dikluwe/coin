// Bounded Vulkan profile: one directional light, one full viewport/camera.
// Depth inputs are GPU-only passes over the already captured/lowered geometry.
SAMPLER2D(s_pbrDirectionalDepth,8);
SAMPLER2D(s_pbrCameraDepth,9);
uniform mat4 u_pbrEffectViewToWorld;
uniform mat4 u_pbrShadowWorldToClip;
uniform mat4 u_pbrEffectProjection;
uniform mat4 u_pbrEffectInverseProjection;
uniform vec4 u_pbrShadowOptions; // enabled, inverse map size, bias, unused
uniform vec4 u_pbrAoOptions; // enabled, world radius, strength, unused
vec2 effectUv(vec4 clip) {return clip.xy/clip.w*vec2(.5,-.5)+vec2_splat(.5);}
float coinViewDirectionalVisibility(vec3 position,vec3 normal,vec3 lightDirection) {
  if(u_pbrShadowOptions.x<.5)return 1.0;
  vec3 world=mul(u_pbrEffectViewToWorld,vec4(position,1.0)).xyz;
  vec4 clip=mul(u_pbrShadowWorldToClip,vec4(world,1.0));
  vec2 uv=effectUv(clip);float depth=clip.z/clip.w;
  if(any(lessThan(uv,vec2_splat(0.0)))||any(greaterThan(uv,vec2_splat(1.0)))||depth<=0.0||depth>=1.0)return 1.0;
  float bias=u_pbrShadowOptions.z*(1.0+2.0*(1.0-max(dot(normal,lightDirection),0.0)));
  // Compare each PCF tap against the receiver plane at that tap. A constant
  // center depth self-shadows sloped floors even with a quantization bias.
  vec2 dx=dFdx(uv),dy=dFdy(uv);float zx=dFdx(depth),zy=dFdy(depth);
  float determinant=dx.x*dy.y-dx.y*dy.x;
  vec2 slope=abs(determinant)>1e-12?vec2(zx*dy.y-zy*dx.y,zy*dx.x-zx*dy.x)/determinant:vec2_splat(0.0);
  float visible=0.0;
  for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
    vec2 offset=vec2(float(x),float(y))*u_pbrShadowOptions.y;
    vec2 sampleUv=(floor((uv+offset)/u_pbrShadowOptions.y)+vec2_splat(.5))*u_pbrShadowOptions.y;
    sampleUv=clamp(sampleUv,vec2_splat(.5*u_pbrShadowOptions.y),vec2_splat(1.0-.5*u_pbrShadowOptions.y));
    float stored=texture2DLod(s_pbrDirectionalDepth,sampleUv,0.0).r;
    visible+=depth+clamp(dot(slope,sampleUv-uv),-.01,.01)-bias<=stored?1.0:0.0;
  }
  return visible/9.0;
}
float coinViewAmbientVisibility(vec3 position,vec3 normal) {
  if(u_pbrAoOptions.x<.5)return 1.0;
  vec3 t=normalize(cross(abs(normal.z)<.9?vec3(0,0,1):vec3(0,1,0),normal));
  vec3 b=cross(normal,t);float blocked=0.0;
  float radius=u_pbrAoOptions.y,bias=radius*.025;
  for(int i=0;i<16;++i) {
    float u=(float(i)+.5)/16.0,phi=float(i)*2.39996323;
    vec3 direction=t*(sqrt(u)*cos(phi))+b*(sqrt(u)*sin(phi))+normal*sqrt(1.0-u);
    vec3 samplePosition=position+normal*bias+direction*radius*(.25+.75*u);
    vec4 clip=mul(u_pbrEffectProjection,vec4(samplePosition,1.0));
    vec2 uv=effectUv(clip);
    if(clip.w<=0.0||any(lessThan(uv,vec2_splat(0.0)))||any(greaterThan(uv,vec2_splat(1.0))))continue;
    float depth=texture2DLod(s_pbrCameraDepth,uv,0.0).r;
    if(depth>=1.0)continue;
    vec4 view=mul(u_pbrEffectInverseProjection,vec4(uv*vec2(2.0,-2.0)+vec2(-1.0,1.0),depth,1.0));
    vec3 actual=view.xyz/view.w;
    float distance=length(actual-position);
    if(actual.z>samplePosition.z+bias)blocked+=1.0-smoothstep(radius*.5,radius,distance);
  }
  return clamp(1.0-u_pbrAoOptions.z*blocked/16.0,0.0,1.0);
}
