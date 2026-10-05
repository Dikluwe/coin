#ifndef COIN_RENDER_ALPHA_TEST_CAPTURE_H
#define COIN_RENDER_ALPHA_TEST_CAPTURE_H
// Wiring: Coin's lazy element keeps GL function tokens so separator push/pop
// and callbacks see its native state. Only semantic values leave this adapter.
#include "rendering/coinrender/CoinRenderAlphaTestCore.h"
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/system/gl.h>

inline bool coin_render_capture_alpha_node(const SoAlphaTest & node, SoState * state) {
  static const int native[] = {0, GL_NEVER, GL_ALWAYS, GL_LESS, GL_LEQUAL,
    GL_EQUAL, GL_GEQUAL, GL_GREATER, GL_NOTEQUAL};
  const int function = node.function.getValue();
  if (function < 0 || function > 8 || std::isnan(node.value.getValue())) return false;
  // SoAlphaTest::GLRender applies both fields even when ignored/override flags
  // are set. Preserve that interpretation, without invoking GLRender.
  SoLazyElement::setAlphaTest(state, native[function], node.value.getValue());
  return true;
}
inline bool coin_render_snapshot_alpha_test(SoState * state,
                                            CoinRenderAlphaTestFunction & function,
                                            float & reference) {
  const int native = SoLazyElement::getAlphaTest(state, reference);
  if (std::isnan(reference)) return false;
  reference = std::max(0.0f, std::min(1.0f, reference));
  switch (native) {
  case 0: function = CoinRenderAlphaTestFunction::NONE; break;
  case GL_NEVER: function = CoinRenderAlphaTestFunction::NEVER; break;
  case GL_ALWAYS: function = CoinRenderAlphaTestFunction::ALWAYS; break;
  case GL_LESS: function = CoinRenderAlphaTestFunction::LESS; break;
  case GL_LEQUAL: function = CoinRenderAlphaTestFunction::LEQUAL; break;
  case GL_EQUAL: function = CoinRenderAlphaTestFunction::EQUAL; break;
  case GL_GEQUAL: function = CoinRenderAlphaTestFunction::GEQUAL; break;
  case GL_GREATER: function = CoinRenderAlphaTestFunction::GREATER; break;
  case GL_NOTEQUAL: function = CoinRenderAlphaTestFunction::NOTEQUAL; break;
  default: return false;
  }
  return true;
}
#endif
