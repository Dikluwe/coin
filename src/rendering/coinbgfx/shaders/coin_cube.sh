// Cube atlas: metadata = (face size, mip chain, 8 + filter, 0).
// Keep level offsets as row/column pairs, then fetch with integer addresses:
// the full 2048 atlas exceeds exact linear integer addressing in float.
vec3 coinCubeChart(vec3 d) {
  vec3 a=abs(d); float face; vec2 c; float major;
  if(a.x>=a.y && a.x>=a.z) { major=a.x; if(d.x>=0.0){face=0.0;c=vec2(-d.z,-d.y);}else{face=1.0;c=vec2(d.z,-d.y);} }
  else if(a.y>=a.z) { major=a.y; if(d.y>=0.0){face=2.0;c=vec2(d.x,d.z);}else{face=3.0;c=vec2(d.x,-d.z);} }
  else { major=a.z; if(d.z>=0.0){face=4.0;c=vec2(d.x,-d.y);}else{face=5.0;c=vec2(-d.x,-d.y);} }
  return vec3(0.5*(c/major+1.0),face);
}
vec4 coinCubeFetch(sampler2D image, vec2 p, float size, float face,
                    vec2 base, float width, float height) {
  int n=int(size), w=int(width);
  ivec2 texel=ivec2(clamp(p,vec2_splat(0.0),vec2_splat(size-1.0)));
  // All addresses fit u32/i32 (maximum 33,554,429). Integer loads avoid
  // float reassociation and the normalized sampler coordinate conversion.
  int index=int(base.y)*w+int(base.x)+int(face)*n*n+texel.y*n+texel.x;
  return texelFetch(image,ivec2(index%w,index/w),0);
}
vec4 coinCubeLevel(sampler2D image, vec3 chart, vec4 metadata, int level, bool linearTexel) {
  float width=2.0*metadata.x; float size=metadata.x;
  vec2 base=vec2_splat(0.0); vec2 cursor=vec2_splat(0.0); float height=0.0;
  for(int i=0;i<12;++i) {
    if(i==level)base=cursor;
    float count=6.0*size*size;
    float col=cursor.x+mod(count,width);
    cursor.y+=floor(count/width)+floor(col/width);cursor.x=mod(col,width);
    if(metadata.y<0.5 || size<=1.0)break;
    size=max(1.0,size*0.5);
  }
  height=cursor.y+(cursor.x>0.0?1.0:0.0);
  size=max(1.0,metadata.x/exp2(float(level)));
  if(!linearTexel)return coinCubeFetch(image,floor(chart.xy*size),size,chart.z,base,width,height);
  vec2 f=chart.xy*size-0.5;vec2 p=floor(f);vec2 w=fract(f);
  vec4 a=mix(coinCubeFetch(image,p,size,chart.z,base,width,height),coinCubeFetch(image,p+vec2(1.0,0.0),size,chart.z,base,width,height),w.x);
  vec4 b=mix(coinCubeFetch(image,p+vec2(0.0,1.0),size,chart.z,base,width,height),coinCubeFetch(image,p+vec2(1.0,1.0),size,chart.z,base,width,height),w.x);
  return mix(a,b,w.y);
}
vec4 coinCubeSample(sampler2D image, vec3 direction, vec4 metadata) {
  vec3 chart=coinCubeChart(direction/max(abs(direction.x),max(abs(direction.y),abs(direction.z))));
  float filterMode=metadata.z-8.0;
  if(filterMode<1.5)return coinCubeLevel(image,chart,metadata,0,filterMode>0.5);
#ifdef COIN_CUBE_CONSTANT_LOD
  float lod=0.0;
#else
  // Differentiate a single face's chart, avoiding an artificial seam when
  // adjacent fragments choose different faces.
  vec3 d=direction/max(abs(direction.x),max(abs(direction.y),abs(direction.z)));
  float scale=max(abs(direction.x),max(abs(direction.y),abs(direction.z)));
  vec3 dx=dFdx(direction)/scale,dy=dFdy(direction)/scale;vec2 c;vec2 cx;vec2 cy;float m;float mx;float my;
  if(chart.z<1.5){float signX=chart.z<0.5?1.0:-1.0;c=vec2(-signX*d.z,-d.y);cx=vec2(-signX*dx.z,-dx.y);cy=vec2(-signX*dy.z,-dy.y);m=signX*d.x;mx=signX*dx.x;my=signX*dy.x;}
  else if(chart.z<3.5){float signY=chart.z<2.5?1.0:-1.0;c=vec2(d.x,signY*d.z);cx=vec2(dx.x,signY*dx.z);cy=vec2(dy.x,signY*dy.z);m=signY*d.y;mx=signY*dx.y;my=signY*dy.y;}
  else{float signZ=chart.z<4.5?1.0:-1.0;c=vec2(signZ*d.x,-d.y);cx=vec2(signZ*dx.x,-dx.y);cy=vec2(signZ*dy.x,-dy.y);m=signZ*d.z;mx=signZ*dx.z;my=signZ*dy.z;}
  vec2 sx=0.5*(cx*m-c*mx)/(m*m)*metadata.x;
  vec2 sy=0.5*(cy*m-c*my)/(m*m)*metadata.x;
  float lod=log2(max(max(length(sx),length(sy)),0.000001));
#endif
  if(lod<=0.0)return coinCubeLevel(image,chart,metadata,0,true);
  float maximum=metadata.y>0.5?log2(metadata.x):0.0;
  float bounded=clamp(lod,0.0,maximum);int lo=int(floor(bounded));int hi=min(lo+1,int(maximum));
  return mix(coinCubeLevel(image,chart,metadata,lo,filterMode>2.5),coinCubeLevel(image,chart,metadata,hi,filterMode>2.5),fract(bounded));
}
