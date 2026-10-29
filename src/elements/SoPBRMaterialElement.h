#ifndef COIN_SO_PBR_MATERIAL_ELEMENT_H
#define COIN_SO_PBR_MATERIAL_ELEMENT_H
#include <Inventor/rendering/SoPBRMaterialData.h>
#include <memory>
#include <Inventor/elements/SoElement.h>
#include <Inventor/elements/SoSubElement.h>
class COIN_DLL_API SoPBRMaterialElement : public SoElement {
  SO_ELEMENT_HEADER(SoPBRMaterialElement);
public:
  static void initClass();
  void init(SoState *) override;
  void push(SoState *) override;
  SbBool matches(const SoElement *) const override;
  SoElement * copyMatchInfo() const override;
  static const SoPBRMaterialElement * get(SoState *);
  static void set(SoState *, const SoPBRMaterialData &, SbBool);
  static void selectLegacy(SoState *);
  SoRenderMaterialModel model = SoRenderMaterialModel::LEGACY;
  SoPBRMaterialData material;
  std::shared_ptr<const SoPBRMaterialData> resolved;
  SbBool overridden = FALSE;
};
#endif
