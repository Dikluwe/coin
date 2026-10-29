// CPU-prepared four-dimensional directional albedo and anisotropic source IBL.
vec4 coinPbrAnisTile(vec2 vr,float strengthTile,float azimuthTile) {
  vec2 pixel=vec2(strengthTile*u_pbrAnisoGrid.x,azimuthTile*u_pbrAnisoGrid.y+u_pbrLutLayout.w)+vec2_splat(.5)+vr*(u_pbrAnisoGrid.xy-vec2_splat(1.0));
  return texture2D(s_pbrDfg,pixel/u_pbrLutLayout.xy);
}
vec4 coinPbrAnisTable(vec3 localView,float roughness,float strength) {
  float phi=atan2(abs(localView.y),max(abs(localView.x),1e-20));
  vec2 vr=vec2(clamp(localView.z,0.0,1.0),clamp((roughness-.045)/.955,0.0,1.0));
  float si=clamp(strength,0.0,1.0)*(u_pbrAnisoGrid.z-1.0),ai=clamp(phi/(.5*coinPbrPi),0.0,1.0)*(u_pbrAnisoGrid.w-1.0);
  float s0=floor(si),a0=floor(ai),s1=min(s0+1.0,u_pbrAnisoGrid.z-1.0),a1=min(a0+1.0,u_pbrAnisoGrid.w-1.0);
  return mix(mix(coinPbrAnisTile(vr,s0,a0),coinPbrAnisTile(vr,s1,a0),si-s0),
    mix(coinPbrAnisTile(vr,s0,a1),coinPbrAnisTile(vr,s1,a1),si-s0),ai-a0);
}
vec3 coinPbrAnisAverage(float roughness,float strength) {
  vec2 pixel=vec2(clamp(strength,0.0,1.0)*(u_pbrAnisoGrid.z-1.0),u_pbrAverageLayout.w+clamp((roughness-.045)/.955,0.0,1.0)*(u_pbrAnisoGrid.y-1.0))+vec2_splat(.5);
  return texture2D(s_pbrAlbedoAverage,pixel/u_pbrAverageLayout.xy).rgb;
}
vec3 coinPbrAnisQualityBrdf(vec3 n,vec3 t,vec3 b,vec3 v,vec3 l,vec2 axes,vec3 base,float metallic,float roughness,float strength) {
  float nv=dot(n,v),nl=dot(n,l);if(nv<=0.0||nl<=0.0)return vec3_splat(0.0);
  vec3 f0=mix(vec3_splat(.04),base,metallic),h=coinPbrSafeUnit(v+l);
  float fc=pow(1.0-clamp(dot(v,h),0.0,1.0),5.0);
  vec3 f=f0+(vec3_splat(1.0)-f0)*fc;
  vec3 ss=coinPbrAnisotropicFactor(n,t,b,v,l,axes)*f;
  if(u_pbrQuality.x<.5)return ss+base*(1.0-metallic)*(vec3_splat(1.0)-f)/coinPbrPi;
  vec4 vt=coinPbrAnisTable(vec3(dot(t,v),dot(b,v),nv),roughness,strength);
  vec4 lt=coinPbrAnisTable(vec3(dot(t,l),dot(b,l),nl),roughness,strength);
  vec3 avg=coinPbrAnisAverage(roughness,strength),multi=coinPbrMultipleWeight(f0,min(avg.z,1.0));
  float mv=max(1.0-vt.r-vt.g,0.0),ml=max(1.0-lt.r-lt.g,0.0);
  vec3 sv=coinPbrDirectionalAlbedo(f0,vt,multi),sl=coinPbrDirectionalAlbedo(f0,lt,multi);
  vec3 savg=clamp(f0*avg.x+vec3_splat(avg.y)+max(1.0-avg.z,0.0)*multi,vec3_splat(0.0),vec3_splat(1.0));
  vec3 diffuse=base*(1.0-metallic)*(vec3_splat(1.0)-sv)*(vec3_splat(1.0)-sl)/(coinPbrPi*max(vec3_splat(1.0)-savg,vec3_splat(1e-6)));
  return ss+multi*mv*ml/(coinPbrPi*max(1.0-avg.z,1e-6))+diffuse;
}
vec3 coinPbrAnisSource(vec3 viewPosition,vec3 n,vec3 t,vec3 b,vec3 v,vec2 axes,vec3 f0,vec4 table,out vec4 rawA,out vec4 rawB) {
  vec3 lv=vec3(dot(t,v),dot(b,v),dot(n,v));
  vec3 vh=coinPbrSafeUnit(vec3(axes*lv.xy,lv.z));
  float lensq=dot(vh.xy,vh.xy);
  vec3 e1=lensq>1e-20?vec3(-vh.y,vh.x,0.0)*inversesqrt(lensq):vec3(1.0,0.0,0.0),e2=cross(vh,e1);
  float vlen=length(vec3(axes*lv.xy,lv.z));
  vec3 integralA=vec3_splat(0.0),integralB=vec3_splat(0.0);float weightA=0.0,weightB=0.0;
  for(int i=0;i<128;++i) {
    if(float(i)>=u_pbrAnisoSampling.x)break;
    // Open-ended R2 sequence: subsequent stationary batches can extend it
    // without repeating the same radial nodes. Motion starts at offset zero.
    float index=float(i)+u_pbrAnisoSampling.w+1.0;
#ifdef COIN_PBR_ACCUMULATE
    // Halton 2/3 uses integer digits, avoiding float recurrence quantization
    // as the stationary sequence grows beyond the interactive batch.
    uint ordinal=uint(index);uint bits=ordinal;
    bits=(bits<<16)|(bits>>16);
    bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
    bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);
    bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
    bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
    float third=0.0,digitScale=1.0/3.0;uint digits=ordinal;
    for(int digit=0;digit<16;++digit) {if(digits==0u)break;third+=float(digits%3u)*digitScale;digits/=3u;digitScale/=3.0;}
    vec2 u=vec2(float(bits)*2.3283064365386963e-10,third);
#else
    vec2 u=fract(vec2_splat(.5)+index*vec2(.754877666,.569840291));
#endif
    float radius=sqrt(u.x),phi=2.0*coinPbrPi*u.y,x=radius*cos(phi),s=.5*(1.0+vh.z);
    float y=(1.0-s)*sqrt(max(0.0,1.0-x*x))+s*radius*sin(phi);
    vec3 projected=e1*x+e2*y+vh*sqrt(max(0.0,1.0-x*x-y*y));
    vec3 h=coinPbrSafeUnit(vec3(axes*projected.xy,max(projected.z,0.0)));
    float voh=max(dot(lv,h),0.0);vec3 l=2.0*voh*h-lv;
    if(l.z<=0.0)continue;
    float llen=length(vec3(axes*l.xy,l.z));
    float weight=l.z*(lv.z+vlen)/max(lv.z*llen+l.z*vlen,1e-20),fc=pow(max(1.0-voh,0.0),5.0);
    vec3 scaled=vec3(h.xy/axes,h.z);float den=dot(scaled,scaled);
    float d=1.0/(coinPbrPi*axes.x*axes.y*den*den),pdf=d/(2.0*(lv.z+vlen));
    vec3 direction=coinPbrEnvironmentDirection(t*l.x+b*l.y+n*l.z);
    float major=max(abs(direction.x),max(abs(direction.y),abs(direction.z)));
    float texelArea=4.0*major*major*major/(u_pbrAnisoSampling.y*u_pbrAnisoSampling.y);
#ifdef COIN_PBR_ACCUMULATE
    float sampleArea=1.0/max((u_pbrAnisoSampling.x+u_pbrAnisoSampling.w)*pdf,1e-20);
#else
    float sampleArea=1.0/max(u_pbrAnisoSampling.x*pdf,1e-20);
#endif
    float lod=clamp(.5*log2(sampleArea/max(texelArea,1e-20)),0.0,u_pbrAnisoSampling.z);
    vec3 source=coinPbrSourceEnvironment(viewPosition,direction,lod,sampleArea);
    float wa=weight*(1.0-fc),wb=weight*fc;
    integralA+=source*wa;integralB+=source*wb;weightA+=wa;weightB+=wb;
  }
  // A/B normalization gives the CPU-prepared furnace albedo even for a small
  // batch. This ratio estimator and finite source-mip footprint are explicit
  // interactive approximations; angular error is measured independently.
  rawA=vec4(integralA,weightA)/u_pbrAnisoSampling.x;
  rawB=vec4(integralB,weightB)/u_pbrAnisoSampling.x;
  vec3 fallback=coinPbrIrradiance(viewPosition,coinPbrEnvironmentDirection(n))/coinPbrPi;
  vec3 a=weightA>1e-12?integralA/weightA:fallback,bv=weightB>1e-12?integralB/weightB:fallback;
  return a*(f0*table.r)+bv*table.g;
}
vec3 coinPbrAnisEnvironment(vec3 viewPosition,vec3 n,vec3 t,vec3 b,vec3 v,vec2 axes,vec3 base,float metallic,float roughness,float strength,vec4 planar,float occlusion,out vec4 rawA,out vec4 rawB,out vec3 deterministic) {
  rawA=rawB=vec4_splat(0.0);deterministic=vec3_splat(0.0);
  float nv=dot(n,v);if(nv<=0.0)return vec3_splat(0.0);
  vec4 table=coinPbrAnisTable(vec3(dot(t,v),dot(b,v),nv),roughness,strength);
  vec3 f0=mix(vec3_splat(.04),base,metallic);
  vec3 single=coinPbrAnisSource(viewPosition,n,t,b,v,axes,f0,table,rawA,rawB),multiple=vec3_splat(0.0);
#ifndef COIN_PBR_DISTANT_ONLY
  if(u_pbrProbeInfo.x>0.0&&(!coinPbrLocalFinite(single)||!coinPbrLocalFinite(rawA.rgb)||!coinPbrLocalFinite(rawB.rgb))) {
    rawA=rawB=vec4(coinPbrLocalFailure(),0.0);deterministic=coinPbrLocalFailure();return coinPbrLocalFailure();
  }
#endif
  vec3 irradiance=coinPbrIrradiance(viewPosition,coinPbrEnvironmentDirection(n));
  vec3 diffuse=irradiance*base*(1.0-metallic)*(vec3_splat(1.0)-f0)*(1.0-coinPbrDfgLookup(vec2(nv,roughness)).b)/coinPbrPi;
  if(u_pbrQuality.x>.5) {
    vec3 avg=coinPbrAnisAverage(roughness,strength),multi=coinPbrMultipleWeight(f0,min(avg.z,1.0));
    multiple=irradiance*max(1.0-table.r-table.g,0.0)*multi/coinPbrPi;
    diffuse=irradiance*base*(1.0-metallic)*(vec3_splat(1.0)-coinPbrDirectionalAlbedo(f0,table,multi))/coinPbrPi;
  }
  vec3 weight=f0*table.r+vec3_splat(table.g);
  deterministic=(multiple+diffuse*occlusion)*coinPbrIblGain()+planar.rgb*weight;
  rawA.rgb*=f0*table.r*(1.0-planar.a);
  rawB.rgb*=table.g*(1.0-planar.a);
  // Finite float32 working values preserve the final binary16 saturation.
  rawA.rgb=min(rawA.rgb*coinPbrIblGain(),vec3_splat(1e30));rawB.rgb=min(rawB.rgb*coinPbrIblGain(),vec3_splat(1e30));
  return deterministic+single*(1.0-planar.a)*coinPbrIblGain();
}
