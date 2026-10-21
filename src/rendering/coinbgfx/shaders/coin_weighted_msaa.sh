void main()
{
  if (u_depthInfo.z < 0.5)
  {
    ivec2 count = coinWeightedCounts();
    gl_FragColor = vec4(count.x == count.y ? float(count.x) : -1.0, 0.0, 0.0, 1.0);
  }
  else
  {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    int sampleIndex = int(u_depthInfo.w);
    gl_FragColor = coinWeightedComposite(texelFetch(s_oitAccumMS, pixel, sampleIndex),
      texelFetch(s_oitRevealMS, pixel, sampleIndex).r);
  }
}
