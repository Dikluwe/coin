$input v_coinClipDepth
#include <bgfx_shader.sh>
void main() {
  gl_FragDepth=v_coinClipDepth.x/v_coinClipDepth.y;
  gl_FragColor=vec4_splat(0.0);
}
