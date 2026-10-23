#ifndef COIN_TEXTURE2D_SURFACE
#include "coin_cube.sh"
#endif
// Texture cascade in Coin unit order; fog is applied afterwards and never changes alpha.
#ifdef COIN_SHADOW_MULTITEXTURE
SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texColor1, 1);
SAMPLER2D(s_texColor2, 2);
SAMPLER2D(s_texColor3, 3);
SAMPLER2D(s_texColor4, 4);
SAMPLER2D(s_texColor5, 5);
SAMPLER2D(s_texColor6, 6);
SAMPLER2D(s_texColor7, 7);
#else
SAMPLER2D(s_texColor, 2);
// The shadow profile permits only scene texture unit zero. Avoid aliasing
// scene texture registers with the shadow maps on native HLSL backends.
// A portable 2D module can sample all eight logical units. The fixed 2D
// specialization below still retains its original unit-zero-only cascade.
#if !defined(COIN_SHADOW_SURFACE) && (!defined(COIN_TEXTURE2D_SURFACE) || defined(COIN_PORTABLE_MODULE))
SAMPLER2D(s_texColor1, 3);
SAMPLER2D(s_texColor2, 4);
SAMPLER2D(s_texColor3, 5);
SAMPLER2D(s_texColor4, 6);
SAMPLER2D(s_texColor5, 7);
SAMPLER2D(s_texColor6, 8);
SAMPLER2D(s_texColor7, 9);
#endif
#endif
uniform vec4 u_texParams[8];
uniform vec4 u_coinSamplingSizes[8];
uniform vec4 u_texBlend[8];
uniform vec4 u_texCombine[32];
uniform vec4 u_fogColorMode;
uniform vec4 u_fogRange;
uniform vec4 u_screenDoor;
uniform vec4 u_clipMeta;
uniform vec4 u_clipPlanes[8];
#include "coin_alpha.sh"

#ifdef COIN_PORTABLE_SAMPLING_SHADER
vec4 coinPortableLerp(vec4 a,vec4 b,float w){return a+(b-a)*w;}
// Dimensions/filter/wrap describe the actual bound resource. GLSL330 uses
// ordinary derivatives; the public capability reports DEFAULT, not FINE.
vec4 coinPortableSample(sampler2D t, vec2 uv, vec4 suppliedSize) {
  if (suppliedSize.z != 2.0) return texture2D(t, uv);
  vec2 n = suppliedSize.xy;
  vec2 gx = dFdx(uv)*n, gy = dFdy(uv)*n;
  int maximum = int(floor(log2(max(n.x,n.y))));
  float lod = clamp(log2(max(max(length(gx),length(gy)),0.000001)),0.0,float(maximum));
  if(lod<=0.0)return texture2DLod(t,uv,0.0);
  int flags = int(suppliedSize.w);
  vec2 bounded = fract(uv);
  if (mod(float(flags),2.0)>0.5) bounded.x=clamp(uv.x,0.0,1.0);
  if (flags>=2) bounded.y=clamp(uv.y,0.0,1.0);
  int lo = int(floor(lod)), hi = min(lo+1,maximum);
  ivec2 ni = ivec2(n);
  vec2 a = vec2(max(ni >> lo, ivec2(1, 1)));
  bool pot = (ni.x & (ni.x-1)) == 0 && (ni.y & (ni.y-1)) == 0;
  if(pot)return texture2DLod(t,(floor(bounded*a)+0.5)/a,lod);
  vec2 b = vec2(max(ni >> hi, ivec2(1, 1)));
  return coinPortableLerp(texture2DLod(t,(floor(bounded*a)+0.5)/a,float(lo)),
                          texture2DLod(t,(floor(bounded*b)+0.5)/b,float(hi)),fract(lod));
}
#define COIN_SURFACE_SAMPLE(t, uv, enabled, unit) coinPortableSample(t, uv, u_coinSamplingSizes[unit])
#else
#define COIN_SURFACE_SAMPLE(t, uv, enabled, unit) texture2D(t, uv)
#endif

#ifndef COIN_TEXTURE2D_SURFACE
vec4 coinVolumeLerp(vec4 a, vec4 b, float t) { return a + (b-a)*t; }
float coinVolumeAxis(float value, float extent, float clampAxis) {
  if (clampAxis > 0.5) return clamp(value,0.0,extent-1.0);
  // Normalized coordinates produce integer neighbors in [-1, extent].
  // One exact add/subtract avoids reciprocal-based float modulo at NPOT
  // boundaries, where zero can otherwise wrap to extent minus epsilon.
  if (value < 0.0) return value + extent;
  if (value >= extent) return value - extent;
  return value;
}
vec4 coinVolumeFetch(sampler2D atlasSampler, vec3 index, vec3 size,
                     float columns, float yBase, float atlasWidth, float atlasHeight, float flags) {
  float x=coinVolumeAxis(index.x,size.x,mod(flags,2.0));
  float y=coinVolumeAxis(index.y,size.y,mod(floor(flags/2.0),2.0));
  float z=coinVolumeAxis(index.z,size.z,mod(floor(flags/4.0),2.0));
  vec2 atlas=vec2(mod(z,columns)*size.x+x, yBase+floor(z/columns)*size.y+y);
  return texture2DLod(atlasSampler,(atlas+0.5)/vec2(atlasWidth,atlasHeight),0.0);
}
vec4 coinVolumeLevel(sampler2D atlasSampler, vec3 coordinate, vec3 baseSize,
                     float columns, float flags, float atlasWidth,
                     float atlasHeight, int level, bool spatialLinear) {
  vec3 size=baseSize;
  float yBase=0.0;
  for(int i=0;i<9;++i) {
    if(i>=level) break;
    yBase+=size.y*ceil(size.z/columns);
    size=max(floor(size*0.5),vec3(1.0));
  }
  vec3 bounded=fract(coordinate);
  if(mod(flags,2.0)>0.5)bounded.x=clamp(coordinate.x,0.0,1.0);
  if(mod(floor(flags/2.0),2.0)>0.5)bounded.y=clamp(coordinate.y,0.0,1.0);
  if(mod(floor(flags/4.0),2.0)>0.5)bounded.z=clamp(coordinate.z,0.0,1.0);
  vec3 f=bounded*size-0.5;
  vec3 p=floor(f);
  if(!spatialLinear) return coinVolumeFetch(atlasSampler,floor(bounded*size),size,columns,yBase,atlasWidth,atlasHeight,flags);
  vec3 weight=fract(f);
  vec4 a=coinVolumeLerp(coinVolumeFetch(atlasSampler,p,size,columns,yBase,atlasWidth,atlasHeight,flags),
    coinVolumeFetch(atlasSampler,p+vec3(1.0,0.0,0.0),size,columns,yBase,atlasWidth,atlasHeight,flags),weight.x);
  vec4 b=coinVolumeLerp(coinVolumeFetch(atlasSampler,p+vec3(0.0,1.0,0.0),size,columns,yBase,atlasWidth,atlasHeight,flags),
    coinVolumeFetch(atlasSampler,p+vec3(1.0,1.0,0.0),size,columns,yBase,atlasWidth,atlasHeight,flags),weight.x);
  vec4 c=coinVolumeLerp(coinVolumeFetch(atlasSampler,p+vec3(0.0,0.0,1.0),size,columns,yBase,atlasWidth,atlasHeight,flags),
    coinVolumeFetch(atlasSampler,p+vec3(1.0,0.0,1.0),size,columns,yBase,atlasWidth,atlasHeight,flags),weight.x);
  vec4 d=coinVolumeLerp(coinVolumeFetch(atlasSampler,p+vec3(0.0,1.0,1.0),size,columns,yBase,atlasWidth,atlasHeight,flags),
    coinVolumeFetch(atlasSampler,p+vec3(1.0,1.0,1.0),size,columns,yBase,atlasWidth,atlasHeight,flags),weight.x);
  return coinVolumeLerp(coinVolumeLerp(a,b,weight.y),coinVolumeLerp(c,d,weight.y),weight.z);
}
vec4 coinVolumeSample(sampler2D atlasSampler, vec3 coordinate, vec4 metadata) {
  vec3 size=vec3(metadata.xy,floor(metadata.w/16.0));
  float flags=mod(metadata.w,16.0);
  float columns=1.0;
  for(int i=0;i<5;++i) { if(columns*columns>=size.z)break;columns*=2.0; }
  float atlasWidth=size.x*columns;
  float atlasHeight=0.0;
  vec3 levelSize=size;
  for(int i=0;i<9;++i) {
    atlasHeight+=levelSize.y*ceil(levelSize.z/columns);
    if(mod(floor(flags/8.0),2.0)<0.5 || all(lessThanEqual(levelSize,vec3(1.0))))break;
    levelSize=max(floor(levelSize*0.5),vec3(1.0));
  }
  float filterMode=metadata.z-4.0;
  if(mod(floor(flags/8.0),2.0)<0.5 || filterMode<1.5)
    return coinVolumeLevel(atlasSampler,coordinate,size,columns,flags,atlasWidth,atlasHeight,0,filterMode>0.5);
  float rho=max(length(dFdx(coordinate)*size),length(dFdy(coordinate)*size));
  float maximum=floor(log2(max(size.x,max(size.y,size.z))));
  float lod=clamp(log2(max(rho,0.000001)),0.0,maximum);
  if(lod<=0.0)return coinVolumeLevel(atlasSampler,coordinate,size,columns,flags,atlasWidth,atlasHeight,0,true);
  int lo=int(floor(lod));int hi=min(lo+1,int(maximum));
  bool spatialLinear=filterMode>2.5;
  return coinVolumeLerp(
    coinVolumeLevel(atlasSampler,coordinate,size,columns,flags,atlasWidth,atlasHeight,lo,spatialLinear),
    coinVolumeLevel(atlasSampler,coordinate,size,columns,flags,atlasWidth,atlasHeight,hi,spatialLinear),fract(lod));
}

#endif // COIN_TEXTURE2D_SURFACE

vec2 coinSurfaceUv(vec2 st, float q, vec4 params)
{
  // Coin's fixed-function surface divides after perspective interpolation.
  // Its ShadowGroup shader samples transformed ST directly, ignoring Q.
  // Core captures this policy independently of the selected GPU pipeline.
  vec2 uv = params.w > 0.5 ? st : st / q;
  return params.z > 0.5 ? vec2(uv.x, 1.0 - uv.y) : uv;
}
vec4 coinSurfaceSample(sampler2D atlasSampler, vec3 str, float q, vec4 params, int unit)
{
#ifndef COIN_TEXTURE2D_SURFACE
  if (u_coinSamplingSizes[unit].z >= 8.0) return coinCubeSample(atlasSampler, str, u_coinSamplingSizes[unit]);
  if (u_coinSamplingSizes[unit].z >= 4.0) {
    vec3 coordinate = params.w > 0.5 ? str : str / q;
    return coinVolumeSample(atlasSampler, coordinate, u_coinSamplingSizes[unit]);
  }
#endif
  return COIN_SURFACE_SAMPLE(atlasSampler, coinSurfaceUv(str.xy, q, params), params.x, unit);
}
vec4 coinTextureLayer(vec4 color, vec4 texColor, vec4 params, vec4 blendColor)
{
  if (params.y < 0.5) return color * texColor;
  if (params.y < 1.5) return texColor;
  if (params.y < 2.5) return vec4(mix(color.rgb, texColor.rgb, texColor.a), color.a);
  return vec4(mix(color.rgb, blendColor.rgb, texColor.rgb), color.a * texColor.a);
}
vec4 coinCombineArg(float code, vec4 primary, vec4 tex, vec4 constantColor, vec4 previous) {
  float source = mod(code, 4.0), operand = floor(code / 4.0);
  vec4 value = previous;
  if (source < 0.5) value = primary;
  else if (source < 1.5) value = tex;
  else if (source < 2.5) value = constantColor;
  if (operand > 1.5) value = vec4_splat(value.a);
  if ((operand > .5 && operand < 1.5) || operand > 2.5) value = vec4_splat(1.0) - value;
  return value;
}
vec4 coinCombineOp(float op, vec4 a, vec4 b, vec4 c) {
  if (op < .5) return a;
  if (op < 1.5) return a * b;
  if (op < 2.5) return a + b;
  if (op < 3.5) return a + b - vec4_splat(.5);
  if (op < 4.5) return a - b;
  if (op < 5.5) return a * c + b * (vec4_splat(1.0) - c);
  return vec4_splat(4.0 * dot(a.rgb - vec3_splat(.5), b.rgb - vec3_splat(.5)));
}
vec4 coinTextureProgram(vec4 primary, vec4 previous, vec4 tex, vec4 params, vec4 blendColor, int unit) {
  int base = unit * 4;
  vec4 meta = u_texCombine[base];
  if (meta.x < .5) return coinTextureLayer(previous, tex, params, blendColor);
  vec4 rgbArgs = u_texCombine[base + 1], alphaArgs = u_texCombine[base + 2];
  vec4 constantColor = u_texCombine[base + 3];
  vec4 rgb = coinCombineOp(meta.y, coinCombineArg(rgbArgs.x, primary, tex, constantColor, previous),
      coinCombineArg(rgbArgs.y, primary, tex, constantColor, previous), coinCombineArg(rgbArgs.z, primary, tex, constantColor, previous));
  vec4 alpha = coinCombineOp(meta.z, coinCombineArg(alphaArgs.x, primary, tex, constantColor, previous),
      coinCombineArg(alphaArgs.y, primary, tex, constantColor, previous), coinCombineArg(alphaArgs.z, primary, tex, constantColor, previous));
  float a = meta.y > 6.5 ? rgb.r * alphaArgs.w : alpha.a * alphaArgs.w;
  return clamp(vec4(rgb.rgb * rgbArgs.w, a), 0.0, 1.0);
}
#ifdef COIN_PORTABLE_MODULE
#include "coin_portable_module.sh"
#endif
vec4 coinSurfaceColor(vec2 pixelCoord, vec4 v_color0, vec4 v_texcoord0, vec3 v_viewPosition,
                      vec4 v_texcoords4, vec4 v_texcoords5, vec4 v_texcoords6, vec4 v_texcoords7,
                      vec4 textureQ0, vec4 textureQ1, vec4 textureR0, vec4 textureR1)
{
  // Compute coverage without terminating lanes. Keep these small loops out
  // of the texture result's live range and use one coverage kill afterwards.
  bool rejectCoverage = false;
  for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_clipMeta.x) break;
    if (dot(u_clipPlanes[i], vec4(v_viewPosition, 1.0)) < 0.0) rejectCoverage = true;
  }
  // Coin's 32x32 polygon stipple, quantized into its 65 transparency levels.
  // Texture alpha is not a stipple source. Polygon LINE/POINT and native
  // line/point primitives do not use polygon stipple.
  if (u_screenDoor.x > 0.5) {
    float x = floor(pixelCoord.x);
    float y = floor(u_screenDoor.z > 0.5 ? pixelCoord.y : u_screenDoor.y - pixelCoord.y);
    float rank = 0.0;
    float weight = 256.0;
    for (int bit = 0; bit < 5; ++bit) {
      float bx = mod(x, 2.0);
      float by = mod(y, 2.0);
      float digit = by < 0.5 ? bx * 2.0 : 3.0 - 2.0 * bx;
      rank += digit * weight;
      x = floor(x * 0.5); y = floor(y * 0.5); weight *= 0.25;
    }
    if (rank <= u_screenDoor.x * 16.0 - 1.0) rejectCoverage = true;
  }
  vec4 color = v_color0;
  if (u_screenDoor.w > 0.5) color.a = 1.0;
  vec4 primary = color;
#ifdef COIN_PORTABLE_MODULE
#ifdef COIN_SHADOW_MULTITEXTURE
  color = coinPortableModule(primary, v_texcoord0,
    vec4(v_texcoords4.xy, textureR0.x, v_texcoords4.z),
    vec4(v_texcoords4.w, v_texcoords5.x, textureR0.y, v_texcoords5.y),
    vec4(v_texcoords5.zw, textureR0.z, v_texcoords6.x),
    vec4(v_texcoords6.yz, textureR0.w, v_texcoords6.w),
    vec4(v_texcoords7.xy, textureR1.x, v_texcoords7.z),
    vec4(v_texcoords7.w, textureQ0.x, textureR1.y, textureQ0.y),
    vec4(textureQ0.zw, textureR1.z, textureQ1.x));
#else
  color = coinPortableModule(primary, v_texcoord0,
    vec4(v_texcoords4.xy, textureR0.x, textureQ0.x),
    vec4(v_texcoords4.zw, textureR0.y, textureQ0.y),
    vec4(v_texcoords5.xy, textureR0.z, textureQ0.z),
    vec4(v_texcoords5.zw, textureR0.w, textureQ0.w),
    vec4(v_texcoords6.xy, textureR1.x, textureQ1.x),
    vec4(v_texcoords6.zw, textureR1.y, textureQ1.y),
    vec4(v_texcoords7.xy, textureR1.z, textureQ1.z));
#endif
#else
  if (u_texParams[0].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor, v_texcoord0.xyz, v_texcoord0.w, u_texParams[0], 0), u_texParams[0], u_texBlend[0], 0);
#ifdef COIN_SHADOW_MULTITEXTURE
  if (u_texParams[1].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor1, vec3(v_texcoords4.x, v_texcoords4.y, textureR0.x), v_texcoords4.z, u_texParams[1], 1), u_texParams[1], u_texBlend[1], 1);
  if (u_texParams[2].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor2, vec3(v_texcoords4.w, v_texcoords5.x, textureR0.y), v_texcoords5.y, u_texParams[2], 2), u_texParams[2], u_texBlend[2], 2);
  if (u_texParams[3].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor3, vec3(v_texcoords5.z, v_texcoords5.w, textureR0.z), v_texcoords6.x, u_texParams[3], 3), u_texParams[3], u_texBlend[3], 3);
  if (u_texParams[4].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor4, vec3(v_texcoords6.y, v_texcoords6.z, textureR0.w), v_texcoords6.w, u_texParams[4], 4), u_texParams[4], u_texBlend[4], 4);
  if (u_texParams[5].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor5, vec3(v_texcoords7.x, v_texcoords7.y, textureR1.x), v_texcoords7.z, u_texParams[5], 5), u_texParams[5], u_texBlend[5], 5);
  if (u_texParams[6].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor6, vec3(v_texcoords7.w, textureQ0.x, textureR1.y), textureQ0.y, u_texParams[6], 6), u_texParams[6], u_texBlend[6], 6);
  if (u_texParams[7].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor7, vec3(textureQ0.z, textureQ0.w, textureR1.z), textureQ1.x, u_texParams[7], 7), u_texParams[7], u_texBlend[7], 7);
#elif !defined(COIN_SHADOW_SURFACE) && !defined(COIN_TEXTURE2D_SURFACE)
  if (u_texParams[1].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor1, vec3(v_texcoords4.xy,textureR0.x), textureQ0.x, u_texParams[1], 1), u_texParams[1], u_texBlend[1], 1);
  if (u_texParams[2].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor2, vec3(v_texcoords4.zw,textureR0.y), textureQ0.y, u_texParams[2], 2), u_texParams[2], u_texBlend[2], 2);
  if (u_texParams[3].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor3, vec3(v_texcoords5.xy,textureR0.z), textureQ0.z, u_texParams[3], 3), u_texParams[3], u_texBlend[3], 3);
  if (u_texParams[4].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor4, vec3(v_texcoords5.zw,textureR0.w), textureQ0.w, u_texParams[4], 4), u_texParams[4], u_texBlend[4], 4);
  if (u_texParams[5].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor5, vec3(v_texcoords6.xy,textureR1.x), textureQ1.x, u_texParams[5], 5), u_texParams[5], u_texBlend[5], 5);
  if (u_texParams[6].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor6, vec3(v_texcoords6.zw,textureR1.y), textureQ1.y, u_texParams[6], 6), u_texParams[6], u_texBlend[6], 6);
  if (u_texParams[7].x > 0.5)
    color = coinTextureProgram(primary, color, coinSurfaceSample(s_texColor7, vec3(v_texcoords7.xy,textureR1.z), textureQ1.z, u_texParams[7], 7), u_texParams[7], u_texBlend[7], 7);
#endif
#endif // COIN_PORTABLE_MODULE
  // Coverage is applied only after all implicit LOD/portable derivatives.
  if (rejectCoverage) discard;
  coinAlphaTest(color.a);
  float mode = u_fogColorMode.w;
  if (mode < 0.5) return color;
  float distanceToEye = u_fogRange.z > 0.5 ? u_fogRange.w : max(-v_viewPosition.z, 0.0);
  float factor = 1.0;
  if (mode < 1.5) factor = (u_fogRange.y - distanceToEye) / (u_fogRange.y - u_fogRange.x);
  else if (mode < 2.5) factor = exp(-5.545 * distanceToEye / u_fogRange.y);
  else {
    float x = 2.35 * distanceToEye / u_fogRange.y;
    factor = exp(-(x * x));
  }
  return vec4(mix(u_fogColorMode.rgb, color.rgb, clamp(factor, 0.0, 1.0)), color.a);
}
