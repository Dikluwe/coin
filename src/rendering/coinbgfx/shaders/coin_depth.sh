// Window-depth contract shared by all geometry fragment shaders.
// x/y = depth range; z/w = effective slope factor/constant window-depth bias.
uniform vec4 u_coinDepth;
uniform vec4 u_rasterViewport;
float coinSurfaceClipDepth(vec2 captured, float nativeDepth)
{
  return u_rasterViewport.z > 0.0 ? captured.x / captured.y : nativeDepth;
}

float coinWindowDepth(float fragmentDepth)
{
  if (u_rasterViewport.z > 0.0 && (fragmentDepth < 0.0 || fragmentDepth > 1.0)) discard;
  float depth = u_coinDepth.x + (u_coinDepth.y - u_coinDepth.x) * fragmentDepth;
  if (u_coinDepth.z != 0.0) {
    float slope = max(abs(dFdx(depth)), abs(dFdy(depth)));
    depth += u_coinDepth.z * slope;
  }
  return clamp(depth + u_coinDepth.w, 0.0, 1.0);
}

bool coinPeelDepthAccepts(float candidate, float storedDepth, float functionCode)
{
  if (functionCode < 0.5) return false;
  if (functionCode < 1.5) return true;
  if (functionCode < 2.5) return candidate < storedDepth;
  if (functionCode < 3.5) return candidate <= storedDepth;
  if (functionCode < 4.5) return candidate == storedDepth;
  if (functionCode < 5.5) return candidate >= storedDepth;
  if (functionCode < 6.5) return candidate > storedDepth;
  return candidate != storedDepth;
}
