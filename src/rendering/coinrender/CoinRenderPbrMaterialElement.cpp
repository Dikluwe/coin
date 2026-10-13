#include "CoinRenderPbrMaterialElement.h"
#include <Inventor/misc/SoState.h>
SO_ELEMENT_SOURCE(CoinRenderPbrMaterialElement);
void CoinRenderPbrMaterialElement::initClass() {
  SO_ELEMENT_INIT_CLASS(CoinRenderPbrMaterialElement, SoElement);
}
void CoinRenderPbrMaterialElement::init(SoState *) {
  model = CoinRenderMaterialModel::LEGACY; material = CoinRenderPbrMaterialSnapshot(); overridden = FALSE;
}
void CoinRenderPbrMaterialElement::push(SoState *) {
  const auto * previous = static_cast<const CoinRenderPbrMaterialElement *>(getNextInStack());
  model = previous->model; material = previous->material; overridden = previous->overridden;
}
SbBool CoinRenderPbrMaterialElement::matches(const SoElement * element) const {
  const auto * other = static_cast<const CoinRenderPbrMaterialElement *>(element);
  return model == other->model && overridden == other->overridden &&
         coin_render_same_pbr_material(material, other->material);
}
SoElement * CoinRenderPbrMaterialElement::copyMatchInfo() const {
  auto * result = new CoinRenderPbrMaterialElement;
  result->model = model; result->material = material; result->overridden = overridden; return result;
}
const CoinRenderPbrMaterialElement * CoinRenderPbrMaterialElement::get(SoState * state) {
  if (!state || !state->isElementEnabled(classStackIndex)) return NULL;
  return static_cast<const CoinRenderPbrMaterialElement *>(getConstElement(state, classStackIndex));
}
void CoinRenderPbrMaterialElement::set(SoState * state, const CoinRenderPbrMaterialSnapshot & value, SbBool overrideValue) {
  auto * element = static_cast<CoinRenderPbrMaterialElement *>(getElement(state, classStackIndex));
  element->model = CoinRenderMaterialModel::PBR; element->material = value; element->overridden = overrideValue;
}
void CoinRenderPbrMaterialElement::selectLegacy(SoState * state) {
  auto * element = static_cast<CoinRenderPbrMaterialElement *>(getElement(state, classStackIndex));
  element->model = CoinRenderMaterialModel::LEGACY;
  element->material = CoinRenderPbrMaterialSnapshot(); element->overridden = FALSE;
}
