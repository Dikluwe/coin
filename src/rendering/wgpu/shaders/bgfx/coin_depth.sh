// Window-depth contract shared by all geometry fragment shaders.
// x/y = depth range; z/w = effective slope factor/constant window-depth bias.
uniform vec4 u_coinDepth;

float coinWindowDepth(float fragmentDepth)
{
  float depth = mix(u_coinDepth.x, u_coinDepth.y, fragmentDepth);
  float slope = max(abs(dFdx(depth)), abs(dFdy(depth)));
  return clamp(depth + u_coinDepth.z * slope + u_coinDepth.w, 0.0, 1.0);
}
