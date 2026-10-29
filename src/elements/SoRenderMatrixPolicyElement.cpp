#include <Inventor/elements/SoRenderMatrixPolicyElement.h>

SO_ELEMENT_SOURCE(SoRenderMatrixPolicyElement);

void
SoRenderMatrixPolicyElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoRenderMatrixPolicyElement, inherited);
}

SoRenderMatrixPolicyElement::~SoRenderMatrixPolicyElement()
{
}

void
SoRenderMatrixPolicyElement::set(SoState * state, SoNode * node,
                                 Policy policy)
{
  inherited::set(classStackIndex, state, node, static_cast<int32_t>(policy));
}

SoRenderMatrixPolicyElement::Policy
SoRenderMatrixPolicyElement::get(SoState * state)
{
  return static_cast<Policy>(inherited::get(classStackIndex, state));
}
