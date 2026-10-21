#include <bgfx_compute.sh>
#define COIN_CUBE_CONSTANT_LOD
#include "coin_cube.sh"
IMAGE2D_WO(i_rasterOutput, rgba32f, 0);
SAMPLER2D(s_rasterPrimary, 1);
SAMPLER2D(s_texColor, 2);
SAMPLER2D(s_texColor1, 3);
SAMPLER2D(s_texColor2, 4);
SAMPLER2D(s_texColor3, 5);
SAMPLER2D(s_texColor4, 6);
SAMPLER2D(s_texColor5, 7);
SAMPLER2D(s_texColor6, 8);
SAMPLER2D(s_texColor7, 9);
uniform vec4 u_rasterCoordinates[8];
uniform vec4 u_rasterExtent;
uniform vec4 u_texParams[8];
uniform vec4 u_coinSamplingSizes[8];
uniform vec4 u_texBlend[8];
uniform vec4 u_texCombine[32];
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
vec4 rasterSample(sampler2D image, int unit) {
  vec4 coordinate=u_rasterCoordinates[unit];
  vec4 params=u_texParams[unit];
  vec4 metadata=u_coinSamplingSizes[unit];
  if(metadata.z>=8.0)return coinCubeSample(image,coordinate.xyz,metadata);
  vec3 projected=params.w>0.5 ? coordinate.xyz : coordinate.xyz/coordinate.w;
  if(metadata.z>=4.0) {
    vec3 size=vec3(metadata.xy,floor(metadata.w/16.0));
    float columns=1.0;
    for(int i=0;i<5;++i){if(columns*columns>=size.z)break;columns*=2.0;}
    float width=size.x*columns;
    float height=size.y*ceil(size.z/columns);
    // Packed lower levels enlarge the physical atlas height.
    vec3 levelSize=size;
    if(mod(metadata.w,16.0)>=8.0) {
      for(int i=0;i<9;++i){
        if(max(levelSize.x,max(levelSize.y,levelSize.z))<=1.0)break;
        levelSize=max(floor(levelSize*0.5),vec3_splat(1.0));
        height+=levelSize.y*ceil(levelSize.z/columns);
      }
    }
    return coinVolumeLevel(image,projected,size,columns,mod(metadata.w,8.0),
      width,height,0,metadata.z>4.5);
  }
  vec2 uv=params.z>0.5 ? vec2(projected.x,1.0-projected.y) : projected.xy;
  return texture2DLod(image,uv,0.0);
}
NUM_THREADS(8,8,1)
void main() {
  vec2 pixel=vec2(gl_GlobalInvocationID.xy);
  if(any(greaterThanEqual(pixel,u_rasterExtent.xy)))return;
  vec4 primary=texture2DLod(s_rasterPrimary,(pixel+0.5)/u_rasterExtent.xy,0.0);
  vec4 color=primary;
  if(u_texParams[0].x>0.5)color=coinTextureProgram(primary,color,rasterSample(s_texColor,0),u_texParams[0],u_texBlend[0],0);
  if(u_texParams[1].x>0.5)color=coinTextureProgram(primary,color,rasterSample(s_texColor1,1),u_texParams[1],u_texBlend[1],1);
  if(u_texParams[2].x>0.5)color=coinTextureProgram(primary,color,rasterSample(s_texColor2,2),u_texParams[2],u_texBlend[2],2);
  if(u_texParams[3].x>0.5)color=coinTextureProgram(primary,color,rasterSample(s_texColor3,3),u_texParams[3],u_texBlend[3],3);
  if(u_texParams[4].x>0.5)color=coinTextureProgram(primary,color,rasterSample(s_texColor4,4),u_texParams[4],u_texBlend[4],4);
  if(u_texParams[5].x>0.5)color=coinTextureProgram(primary,color,rasterSample(s_texColor5,5),u_texParams[5],u_texBlend[5],5);
  if(u_texParams[6].x>0.5)color=coinTextureProgram(primary,color,rasterSample(s_texColor6,6),u_texParams[6],u_texBlend[6],6);
  if(u_texParams[7].x>0.5)color=coinTextureProgram(primary,color,rasterSample(s_texColor7,7),u_texParams[7],u_texBlend[7],7);
  imageStore(i_rasterOutput,ivec2(gl_GlobalInvocationID.xy),color);
}
