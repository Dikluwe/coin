#ifndef COIN_SO_PBR_MATERIAL_H
#define COIN_SO_PBR_MATERIAL_H
#include <Inventor/CoinRenderExport.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/fields/SoSFColor.h>
#include <Inventor/fields/SoSFFloat.h>
#include <Inventor/tools/SbPimplPtr.h>
class SoPBRMaterialP;

/** Experimental opaque metallic/roughness material consumed by CoinRender.
 * RGB factors are linear. Roughness is perceptual. This node is not a
 * replacement for SoMaterial's Phong fields or a CoinGL PBR implementation. */
class COIN_RENDER_DLL_API SoPBRMaterial : public SoNode {
  typedef SoNode inherited;
  SO_NODE_HEADER(SoPBRMaterial);
public:
  static void initClass();
  SoPBRMaterial();
  SoSFColor baseColor;
  SoSFFloat metallic;
  SoSFFloat roughness;
  SoSFColor emissive;
  void doAction(SoAction *) override;
  void callback(SoCallbackAction *) override;
protected:
  ~SoPBRMaterial() override;
private:
  SbPimplPtr<SoPBRMaterialP> pimpl;
};
#endif
