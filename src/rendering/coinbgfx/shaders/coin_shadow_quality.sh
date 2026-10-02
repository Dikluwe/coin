uniform vec4 u_shadowQuality;
vec3 coinShadowVertexColor(int slot, vec4 p0, vec4 p1, vec4 p2, vec4 p3, vec4 p4, vec4 p5)
{
  if (slot == 0) return p0.xyz;
  if (slot == 1) return vec3(p0.w, p1.xy);
  if (slot == 2) return vec3(p1.zw, p2.x);
  if (slot == 3) return p2.yzw;
  if (slot == 4) return p3.xyz;
  if (slot == 5) return vec3(p3.w, p4.xy);
  if (slot == 6) return vec3(p4.zw, p5.x);
  return p5.yzw;
}
vec3 coinShadowContribution(int index, vec3 vertexColor, vec4 diffuse, vec4 specular,
  vec3 positionView, vec3 normalView, float shininess)
{
  if (u_shadowQuality.x > 0.5) return vertexColor;
  return coinGroupLightContribution(index, diffuse, specular, positionView, normalView, shininess);
}
vec3 coinOrdinaryFragmentColor(vec4 diffuse, vec4 specular,
  vec3 positionView, vec3 normalView, float shininess, out vec3 specularColor)
{
  specularColor = vec3(0.0);
  vec3 color = vec3(0.0);
  if (u_shadowQuality.y > 0.5) for (int i = 0; i < 8; ++i) {
    if (float(i) >= u_lightCount.x) break;
    if (abs(float(i) - u_shadowLightIndices.x) < 0.5 ||
        abs(float(i) - u_shadowLightIndices.y) < 0.5 ||
        abs(float(i) - u_shadowLightIndices.z) < 0.5 ||
        abs(float(i) - u_shadowLightIndices.w) < 0.5 ||
        abs(float(i) - u_shadowLightIndicesExtra.x) < 0.5 ||
        abs(float(i) - u_shadowLightIndicesExtra.y) < 0.5 ||
        abs(float(i) - u_shadowLightIndicesExtra.z) < 0.5 ||
        abs(float(i) - u_shadowLightIndicesExtra.w) < 0.5) continue;
    color += coinGroupLightContribution(i, diffuse, vec4(0.0), positionView, normalView, shininess);
    specularColor += coinGroupLightContribution(i, vec4(0.0), specular, positionView, normalView, shininess);
  }
  return color;
}
