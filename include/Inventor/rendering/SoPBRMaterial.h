#ifndef COIN_SO_PBR_MATERIAL_H
#define COIN_SO_PBR_MATERIAL_H
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/fields/SoSFColor.h>
#include <Inventor/fields/SoSFFloat.h>
#include <Inventor/fields/SoSFInt32.h>
#include <Inventor/fields/SoSFBool.h>
#include <Inventor/tools/SbPimplPtr.h>
class SoPBRMaterialP;

/** Experimental opaque metallic/roughness material captured by the retained action.
 * RGB factors are linear. Roughness is perceptual. This node is not a
 * replacement for SoMaterial's Phong fields or a CoinGL PBR implementation. */
class COIN_DLL_API SoPBRMaterial : public SoNode {
  typedef SoNode inherited;
  SO_NODE_HEADER(SoPBRMaterial);
public:
  static void initClass();
  SoPBRMaterial();
  SoSFColor baseColor;
  SoSFFloat metallic;
  SoSFFloat roughness;
  SoSFColor emissive;
  // Logical Coin units, -1 disables a role. Static images only.
  SoSFInt32 baseColorTextureUnit;
  SoSFInt32 ormTextureUnit;
  SoSFInt32 emissiveTextureUnit;

  SoSFFloat clearcoat; // [0,1], opaque dielectric coating, IOR 1.5
  SoSFFloat clearcoatRoughness; // [0,1], default 0
  SoSFFloat anisotropy; // [0,1], KHR_materials_anisotropy strength
  SoSFFloat anisotropyRotation; // radians counterclockwise from tangent
  // SoBumpMap supplies tangent normals (RGB/A) or scalar height (L/LA).
  SoSFFloat normalScale; // nonnegative, default 1
  SoSFBool normalYFlip; // default FALSE
  SoSFInt32 tangentCoordinateUnit; // logical UV unit, default 0

  void doAction(SoAction *) override;
  void callback(SoCallbackAction *) override;
protected:
  ~SoPBRMaterial() override;
private:
  SbPimplPtr<SoPBRMaterialP> pimpl;
};
#endif
