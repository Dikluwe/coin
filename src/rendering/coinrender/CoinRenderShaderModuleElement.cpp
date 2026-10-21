#include "CoinRenderShaderModuleElement.h"
#include <Inventor/misc/SoState.h>
SO_ELEMENT_SOURCE(CoinRenderShaderModuleElement);
void CoinRenderShaderModuleElement::initClass() {
  SO_ELEMENT_INIT_CLASS(CoinRenderShaderModuleElement, SoElement);
}
void CoinRenderShaderModuleElement::init(SoState *) { this->registration.reset(); }
void CoinRenderShaderModuleElement::push(SoState *) {
  this->registration = static_cast<const CoinRenderShaderModuleElement *>(getNextInStack())->registration;
}
SbBool CoinRenderShaderModuleElement::matches(const SoElement *other) const {
  return this->registration == static_cast<const CoinRenderShaderModuleElement *>(other)->registration;
}
SoElement *CoinRenderShaderModuleElement::copyMatchInfo() const {
  auto *result = new CoinRenderShaderModuleElement;
  result->registration = this->registration;
  return result;
}
void CoinRenderShaderModuleElement::set(SoState *state,
  const std::shared_ptr<const CoinRenderShaderRegistration> &registration) {
  static_cast<CoinRenderShaderModuleElement *>(SoElement::getElement(state, classStackIndex))->registration = registration;
}
std::shared_ptr<const CoinRenderShaderRegistration> CoinRenderShaderModuleElement::get(SoState *state) {
  return static_cast<const CoinRenderShaderModuleElement *>(SoElement::getConstElement(state, classStackIndex))->registration;
}
