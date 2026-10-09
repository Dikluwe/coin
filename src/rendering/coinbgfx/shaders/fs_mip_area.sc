$input v_coinClipDepth, v_color0, v_ambient, v_specular, v_emission, v_texcoord0, v_viewPosition, v_viewNormal, v_material, v_texcoords4, v_texcoords5, v_texcoords6, v_texcoords7, v_textureQ0, v_textureQ1

#include <bgfx_shader.sh>

SAMPLER2D(s_mipSource, 0);
uniform vec4 u_mipExtent; // source width/height, destination width/height

void main()
{
  vec2 sourceSize = u_mipExtent.xy;
  vec2 destinationSize = u_mipExtent.zw;
  vec2 destinationPixel = floor(gl_FragCoord.xy);
  vec2 areaStart = destinationPixel * sourceSize / destinationSize;
  vec2 areaEnd = (destinationPixel + vec2(1.0, 1.0)) * sourceSize / destinationSize;
  vec2 firstTexel = floor(areaStart);
  vec4 sum = vec4(0.0, 0.0, 0.0, 0.0);
  // Halving with floor dimensions intersects no more than three source texels
  // per axis. The weights cover odd borders instead of dropping the last row.
  for (int y = 0; y < 3; ++y) {
    float sourceY = firstTexel.y + float(y);
    float weightY = max(0.0, min(areaEnd.y, sourceY + 1.0) - max(areaStart.y, sourceY));
    for (int x = 0; x < 3; ++x) {
      float sourceX = firstTexel.x + float(x);
      float weightX = max(0.0, min(areaEnd.x, sourceX + 1.0) - max(areaStart.x, sourceX));
      if (weightX * weightY > 0.0) {
        vec2 uv = (vec2(sourceX, sourceY) + vec2(0.5, 0.5)) / sourceSize;
        sum += texture2DLod(s_mipSource, uv, 0.0) * weightX * weightY;
      }
    }
  }
  gl_FragColor = sum / ((sourceSize.x / destinationSize.x) *
                        (sourceSize.y / destinationSize.y));
}
