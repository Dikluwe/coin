#ifndef COIN_SO_RENDER_DEPTH_POLICY_ELEMENT_H
#define COIN_SO_RENDER_DEPTH_POLICY_ELEMENT_H
#include <Inventor/SbBasic.h>
#include <cstdint>
#include <Inventor/elements/SoElement.h>
#include <Inventor/elements/SoSubElement.h>
// Tracks which depth fields a path will override when Coin replays it in the
// delayed pass. Its own stack preserves separator/instance scoping.
class COIN_DLL_API SoRenderDepthPolicyElement : public SoElement {
  SO_ELEMENT_HEADER(SoRenderDepthPolicyElement);
public:
  static void initClass();
  void init(SoState *) override;
  void push(SoState *) override;
  SbBool matches(const SoElement *) const override;
  SoElement * copyMatchInfo() const override;
  static void add(SoState *, uint32_t mask);
  static uint32_t get(SoState *);
private:
  uint32_t mask = 0;
};
#endif
