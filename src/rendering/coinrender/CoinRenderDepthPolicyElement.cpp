#include "CoinRenderDepthPolicyElement.h"
#include <Inventor/misc/SoState.h>
SO_ELEMENT_SOURCE(CoinRenderDepthPolicyElement);
void CoinRenderDepthPolicyElement::initClass() { SO_ELEMENT_INIT_CLASS(CoinRenderDepthPolicyElement, SoElement); }
void CoinRenderDepthPolicyElement::init(SoState *) { this->mask = 0; }
void CoinRenderDepthPolicyElement::push(SoState *) {
  this->mask = static_cast<const CoinRenderDepthPolicyElement *>(this->getNextInStack())->mask;
}
SbBool CoinRenderDepthPolicyElement::matches(const SoElement * other) const {
  return this->mask == static_cast<const CoinRenderDepthPolicyElement *>(other)->mask;
}
SoElement * CoinRenderDepthPolicyElement::copyMatchInfo() const {
  auto * result = new CoinRenderDepthPolicyElement; result->mask = this->mask; return result;
}
void CoinRenderDepthPolicyElement::add(SoState * state, uint32_t value) {
  static_cast<CoinRenderDepthPolicyElement *>(SoElement::getElement(state, classStackIndex))->mask |= value;
}
uint32_t CoinRenderDepthPolicyElement::get(SoState * state) {
  return static_cast<const CoinRenderDepthPolicyElement *>(SoElement::getConstElement(state, classStackIndex))->mask;
}
