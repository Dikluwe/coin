#include "SoRenderDepthPolicyElement.h"
#include <Inventor/misc/SoState.h>
SO_ELEMENT_SOURCE(SoRenderDepthPolicyElement);
void SoRenderDepthPolicyElement::initClass() {
  if (getClassTypeId() != SoType::badType()) return;
  SO_ELEMENT_INIT_CLASS(SoRenderDepthPolicyElement, SoElement);
}
void SoRenderDepthPolicyElement::init(SoState *) { this->mask = 0; }
void SoRenderDepthPolicyElement::push(SoState *) {
  this->mask = static_cast<const SoRenderDepthPolicyElement *>(this->getNextInStack())->mask;
}
SbBool SoRenderDepthPolicyElement::matches(const SoElement * other) const {
  return this->mask == static_cast<const SoRenderDepthPolicyElement *>(other)->mask;
}
SoElement * SoRenderDepthPolicyElement::copyMatchInfo() const {
  auto * result = new SoRenderDepthPolicyElement; result->mask = this->mask; return result;
}
void SoRenderDepthPolicyElement::add(SoState * state, uint32_t value) {
  static_cast<SoRenderDepthPolicyElement *>(SoElement::getElement(state, classStackIndex))->mask |= value;
}
uint32_t SoRenderDepthPolicyElement::get(SoState * state) {
  return static_cast<const SoRenderDepthPolicyElement *>(SoElement::getConstElement(state, classStackIndex))->mask;
}
