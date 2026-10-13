#include <Inventor/rendering/SoPBRMaterial.h>
#include "CoinRenderPbrMaterialElement.h"
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/misc/SoState.h>
class SoPBRMaterialP { public: SoColorPacker colors; };
SO_NODE_SOURCE(SoPBRMaterial);
void SoPBRMaterial::initClass() {
  if (getClassTypeId() != SoType::badType()) return;
  SO_NODE_INIT_CLASS(SoPBRMaterial, SoNode, "Node");
  CoinRenderPbrMaterialElement::initClass();
}
SoPBRMaterial::SoPBRMaterial() {
  SO_NODE_CONSTRUCTOR(SoPBRMaterial);
  SO_NODE_ADD_FIELD(baseColor, (0.8f, 0.8f, 0.8f));
  SO_NODE_ADD_FIELD(metallic, (0.0f));
  SO_NODE_ADD_FIELD(roughness, (0.4f));
  SO_NODE_ADD_FIELD(emissive, (0.0f, 0.0f, 0.0f));
}
SoPBRMaterial::~SoPBRMaterial() = default;
void SoPBRMaterial::callback(SoCallbackAction * action) { doAction(action); }
void SoPBRMaterial::doAction(SoAction * action) {
  SoState * state = action->getState();
  const auto * inheritedMaterial = CoinRenderPbrMaterialElement::get(state);
  if (!inheritedMaterial || inheritedMaterial->overridden ||
      SoOverrideElement::getDiffuseColorOverride(state) ||
      SoOverrideElement::getTransparencyOverride(state)) return;
  if (baseColor.isIgnored() && metallic.isIgnored() && roughness.isIgnored() && emissive.isIgnored()) return;
  CoinRenderPbrMaterialSnapshot value = inheritedMaterial->material;
  if (!baseColor.isIgnored()) value.baseColor = baseColor.getValue();
  if (!metallic.isIgnored()) value.metallic = metallic.getValue();
  if (!roughness.isIgnored()) value.roughness = roughness.getValue();
  if (!emissive.isIgnored()) value.emissive = emissive.getValue();
  CoinRenderPbrMaterialElement::set(state, value, isOverride());
  // Keep Coin's callback visibility/alpha state consistent with this opaque
  // material. Invalid factors remain in the snapshot for the common validator;
  // they must not reach Coin's packed-color conversion before rejection.
  const auto * published = CoinRenderPbrMaterialElement::get(state);
  static const SbColor safeColor(0.8f, 0.8f, 0.8f);
  static const float opaque = 0.0f;
  const SbColor * color = coin_render_valid_pbr_material(value) ? &published->material.baseColor : &safeColor;
  SoLazyElement::setDiffuse(state, this, 1, color, &pimpl->colors);
  SoLazyElement::setTransparency(state, this, 1, &opaque, &pimpl->colors);
  if (isOverride()) {
    SoOverrideElement::setDiffuseColorOverride(state, this, TRUE);
    SoOverrideElement::setTransparencyOverride(state, this, TRUE);
    SoOverrideElement::setAmbientColorOverride(state, this, TRUE);
    SoOverrideElement::setEmissiveColorOverride(state, this, TRUE);
    SoOverrideElement::setSpecularColorOverride(state, this, TRUE);
    SoOverrideElement::setShininessOverride(state, this, TRUE);
  }
}
