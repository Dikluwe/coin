#include <Inventor/rendering/SoPBRMaterial.h>
#include "elements/SoPBRMaterialElement.h"
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/misc/SoState.h>
class SoPBRMaterialP { public: SoColorPacker colors; };
SO_NODE_SOURCE(SoPBRMaterial);
void SoPBRMaterial::initClass() {
  if (getClassTypeId() != SoType::badType()) return;
  SO_NODE_INIT_CLASS(SoPBRMaterial, SoNode, "Node");
  SoPBRMaterialElement::initClass();
  SO_ENABLE(SoCallbackAction, SoPBRMaterialElement);
}
SoPBRMaterial::SoPBRMaterial() {
  SO_NODE_CONSTRUCTOR(SoPBRMaterial);
  SO_NODE_ADD_FIELD(baseColor, (0.8f, 0.8f, 0.8f));
  SO_NODE_ADD_FIELD(metallic, (0.0f));
  SO_NODE_ADD_FIELD(roughness, (0.4f));
  SO_NODE_ADD_FIELD(emissive, (0.0f, 0.0f, 0.0f));
  SO_NODE_ADD_FIELD(baseColorTextureUnit, (-1));
  SO_NODE_ADD_FIELD(ormTextureUnit, (-1));
  SO_NODE_ADD_FIELD(emissiveTextureUnit, (-1));
  SO_NODE_ADD_FIELD(clearcoat, (0.f));
  SO_NODE_ADD_FIELD(clearcoatRoughness, (0.f));
  SO_NODE_ADD_FIELD(anisotropy, (0.f));
  SO_NODE_ADD_FIELD(anisotropyRotation, (0.f));
  SO_NODE_ADD_FIELD(normalScale, (1.0f));
  SO_NODE_ADD_FIELD(normalYFlip, (0));
  SO_NODE_ADD_FIELD(tangentCoordinateUnit, (0));


}
SoPBRMaterial::~SoPBRMaterial() = default;
void SoPBRMaterial::callback(SoCallbackAction * action) { doAction(action); }
void SoPBRMaterial::doAction(SoAction * action) {
  SoState * state = action->getState();
  const auto * inheritedMaterial = SoPBRMaterialElement::get(state);
  if (!inheritedMaterial || inheritedMaterial->overridden ||
      SoOverrideElement::getDiffuseColorOverride(state) ||
      SoOverrideElement::getTransparencyOverride(state)) return;
  if (baseColor.isIgnored() && metallic.isIgnored() && roughness.isIgnored() && emissive.isIgnored() && baseColorTextureUnit.isIgnored() && ormTextureUnit.isIgnored() && emissiveTextureUnit.isIgnored() && clearcoat.isIgnored() && clearcoatRoughness.isIgnored() && anisotropy.isIgnored() && anisotropyRotation.isIgnored() && normalScale.isIgnored() && normalYFlip.isIgnored() && tangentCoordinateUnit.isIgnored()) return;
  SoPBRMaterialData value = inheritedMaterial->material;
  if (!baseColor.isIgnored()) value.baseColor = baseColor.getValue();
  if (!metallic.isIgnored()) value.metallic = metallic.getValue();
  if (!roughness.isIgnored()) value.roughness = roughness.getValue();
  if (!emissive.isIgnored()) value.emissive = emissive.getValue();
  if (!baseColorTextureUnit.isIgnored()) value.baseColorTextureUnit = baseColorTextureUnit.getValue();
  if (!ormTextureUnit.isIgnored()) value.ormTextureUnit = ormTextureUnit.getValue();
  if (!emissiveTextureUnit.isIgnored()) value.emissiveTextureUnit = emissiveTextureUnit.getValue();
  if (!clearcoat.isIgnored()) value.clearcoat = clearcoat.getValue();
  if (!clearcoatRoughness.isIgnored()) value.clearcoatRoughness = clearcoatRoughness.getValue();
  if (!anisotropy.isIgnored()) value.anisotropy = anisotropy.getValue();
  if (!anisotropyRotation.isIgnored()) value.anisotropyRotation = anisotropyRotation.getValue();
  if (!normalScale.isIgnored()) value.normalScale = normalScale.getValue();
  if (!normalYFlip.isIgnored()) value.normalYFlip = normalYFlip.getValue();
  if (!tangentCoordinateUnit.isIgnored()) value.tangentCoordinateUnit = tangentCoordinateUnit.getValue();
  SoPBRMaterialElement::set(state, value, isOverride());
  // Keep Coin's callback visibility/alpha state consistent with this opaque
  // material. Invalid factors remain in the snapshot for the common validator;
  // they must not reach Coin's packed-color conversion before rejection.
  const auto * published = SoPBRMaterialElement::get(state);
  static const SbColor safeColor(0.8f, 0.8f, 0.8f);
  static const float opaque = 0.0f;
  const SbColor * color = so_valid_pbr_material(value) ? &published->material.baseColor : &safeColor;
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
