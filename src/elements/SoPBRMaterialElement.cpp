#include "SoPBRMaterialElement.h"
#include <Inventor/misc/SoState.h>
SO_ELEMENT_SOURCE(SoPBRMaterialElement);
void SoPBRMaterialElement::initClass() {
  if (getClassTypeId() != SoType::badType()) return;
  SO_ELEMENT_INIT_CLASS(SoPBRMaterialElement, SoElement);
}
void SoPBRMaterialElement::init(SoState *) {
  model = SoRenderMaterialModel::LEGACY; material = SoPBRMaterialData(); resolved.reset(); overridden = FALSE;
}
void SoPBRMaterialElement::push(SoState *) {
  const auto * previous = static_cast<const SoPBRMaterialElement *>(getNextInStack());
  model = previous->model; material = previous->material; resolved = previous->resolved; overridden = previous->overridden;
}
SbBool SoPBRMaterialElement::matches(const SoElement * element) const {
  const auto * other = static_cast<const SoPBRMaterialElement *>(element);
  return model == other->model && overridden == other->overridden &&
         so_same_pbr_material(material, other->material);
}
SoElement * SoPBRMaterialElement::copyMatchInfo() const {
  auto * result = new SoPBRMaterialElement;
  result->model = model; result->material = material; result->resolved = resolved; result->overridden = overridden; return result;
}
const SoPBRMaterialElement * SoPBRMaterialElement::get(SoState * state) {
  if (!state || !state->isElementEnabled(classStackIndex)) return NULL;
  return static_cast<const SoPBRMaterialElement *>(getConstElement(state, classStackIndex));
}
void SoPBRMaterialElement::set(SoState * state, const SoPBRMaterialData & value, SbBool overrideValue) {
  auto * element = static_cast<SoPBRMaterialElement *>(getElement(state, classStackIndex));
  element->model = SoRenderMaterialModel::PBR; element->material = value;
  element->resolved = std::make_shared<const SoPBRMaterialData>(value);
  element->overridden = overrideValue;
}
void SoPBRMaterialElement::selectLegacy(SoState * state) {
  auto * element = static_cast<SoPBRMaterialElement *>(getElement(state, classStackIndex));
  element->model = SoRenderMaterialModel::LEGACY;
  element->material = SoPBRMaterialData(); element->resolved.reset(); element->overridden = FALSE;
}
