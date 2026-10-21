#ifndef COIN_RENDER_SHADER_MODULE_ELEMENT_H
#define COIN_RENDER_SHADER_MODULE_ELEMENT_H
#include "CoinRenderShaderModuleCore.h"
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/elements/SoElement.h>
#include <Inventor/elements/SoSubElement.h>
// The registration is retained by the state stack, independently of changes to
// the action registry during a callback. Parameters are captured at each draw.
class COIN_RENDER_DLL_API CoinRenderShaderModuleElement : public SoElement {
  SO_ELEMENT_HEADER(CoinRenderShaderModuleElement);
public:
  static void initClass();
  void init(SoState *) override;
  void push(SoState *) override;
  SbBool matches(const SoElement *) const override;
  SoElement *copyMatchInfo() const override;
  static void set(SoState *, const std::shared_ptr<const CoinRenderShaderRegistration> &);
  static std::shared_ptr<const CoinRenderShaderRegistration> get(SoState *);
private:
  std::shared_ptr<const CoinRenderShaderRegistration> registration;
};
#endif
