#ifndef COIN_RENDER_PBR_MATERIAL_ELEMENT_H
#define COIN_RENDER_PBR_MATERIAL_ELEMENT_H
#include "CoinRenderPbrMaterial.h"
#include <Inventor/CoinRenderExport.h>
#include <Inventor/elements/SoElement.h>
#include <Inventor/elements/SoSubElement.h>
class COIN_RENDER_DLL_API CoinRenderPbrMaterialElement : public SoElement {
  SO_ELEMENT_HEADER(CoinRenderPbrMaterialElement);
public:
  static void initClass();
  void init(SoState *) override;
  void push(SoState *) override;
  SbBool matches(const SoElement *) const override;
  SoElement * copyMatchInfo() const override;
  static const CoinRenderPbrMaterialElement * get(SoState *);
  static void set(SoState *, const CoinRenderPbrMaterialSnapshot &, SbBool);
  static void selectLegacy(SoState *);
  CoinRenderMaterialModel model = CoinRenderMaterialModel::LEGACY;
  CoinRenderPbrMaterialSnapshot material;
  SbBool overridden = FALSE;
};
#endif
