void main()
{
#if COIN_PEEL_SHADER_MASK
  gl_SampleMask[0] = u_depthInfo.z < 0.5 ? -1 : 1 << int(u_depthInfo.w);
#endif
  gl_FragDepth = 0.0;
  if (u_depthInfo.z < 0.5) {
    ivec3 counts = coinPeelCounts();
    gl_FragColor = vec4(counts.x == counts.y && counts.x == counts.z ? float(counts.x) : -1.0, 0.0, 0.0, 1.0);
  } else {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    int sampleIndex = int(u_depthInfo.w);
    gl_FragColor = texelFetch(s_peelColorMS, pixel, sampleIndex);
    if (u_depthInfo.z > 1.5) {
      if (texelFetch(s_peelMaskMS, pixel, sampleIndex).r < 0.5) discard;
      gl_FragDepth = texelFetch(s_peelDepthMS, pixel, sampleIndex).r;
    }
  }
}
