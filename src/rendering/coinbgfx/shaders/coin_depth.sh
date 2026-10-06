// Window-depth contract shared by all geometry fragment shaders.
// x/y = depth range; z/w = effective slope factor/constant window-depth bias.
uniform vec4 u_coinDepth;

float coinWindowDepth(float fragmentDepth)
{
  float depth = u_coinDepth.x + (u_coinDepth.y - u_coinDepth.x) * fragmentDepth;
  if (u_coinDepth.z != 0.0) {
    float slope = max(abs(dFdx(depth)), abs(dFdy(depth)));
    depth += u_coinDepth.z * slope;
  }
  return clamp(depth + u_coinDepth.w, 0.0, 1.0);
}
