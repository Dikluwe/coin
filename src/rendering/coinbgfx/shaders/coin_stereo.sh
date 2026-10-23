void main()
{
  vec2 pixel = floor(gl_FragCoord.xy);
  if (u_stereoInfo.w < 0.5) pixel.y = 1.0 / u_stereoInfo.y - 1.0 - pixel.y;
  vec2 local = pixel - u_stereoViewport.xy;
  bool inside = local.x >= 0.0 && local.y >= 0.0 &&
    local.x < u_stereoViewport.z && local.y < u_stereoViewport.w;
  float phase = u_stereoInfo.z < 2.5 ? local.y : local.x;
  bool left = inside && mod(phase, 2.0) >= 1.0;
  vec4 l = COIN_STEREO_FETCH(s_stereoLeft);
  vec4 r = COIN_STEREO_FETCH(s_stereoRight);
  float ld = COIN_STEREO_FETCH(s_stereoLeftDepth).r;
  float rd = COIN_STEREO_FETCH(s_stereoRightDepth).r;
  gl_FragColor = left ? l : r;
  gl_FragDepth = left ? ld : rd;
  if (u_stereoInfo.z > 3.5) {
    gl_FragColor = vec4(u_depthInfo.xyz, l.a);
    gl_FragDepth = 1.0;
  }
  if (u_stereoInfo.z < 1.5) {
    gl_FragColor = vec4(l.r, r.gba);
    gl_FragDepth = rd;
  }
}
