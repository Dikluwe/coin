#include "SoWgpuDepthPolicyElement.h"
#include <Inventor/misc/SoState.h>
SO_ELEMENT_SOURCE(SoWgpuDepthPolicyElement);
void SoWgpuDepthPolicyElement::initClass() { SO_ELEMENT_INIT_CLASS(SoWgpuDepthPolicyElement, SoElement); }
void SoWgpuDepthPolicyElement::init(SoState *) { this->mask = 0; }
void SoWgpuDepthPolicyElement::push(SoState *) {
  this->mask = static_cast<const SoWgpuDepthPolicyElement *>(this->getNextInStack())->mask;
}
SbBool SoWgpuDepthPolicyElement::matches(const SoElement * other) const {
  return this->mask == static_cast<const SoWgpuDepthPolicyElement *>(other)->mask;
}
SoElement * SoWgpuDepthPolicyElement::copyMatchInfo() const {
  auto * result = new SoWgpuDepthPolicyElement; result->mask = this->mask; return result;
}
void SoWgpuDepthPolicyElement::add(SoState * state, uint32_t value) {
  static_cast<SoWgpuDepthPolicyElement *>(SoElement::getElement(state, classStackIndex))->mask |= value;
}
uint32_t SoWgpuDepthPolicyElement::get(SoState * state) {
  return static_cast<const SoWgpuDepthPolicyElement *>(SoElement::getConstElement(state, classStackIndex))->mask;
}
