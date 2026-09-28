#ifndef COIN_SOWGPUDEPTHPOLICYELEMENT_H
#define COIN_SOWGPUDEPTHPOLICYELEMENT_H
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/elements/SoElement.h>
#include <Inventor/elements/SoSubElement.h>
// Tracks which depth fields a path will override when Coin replays it in the
// delayed pass. Its own stack preserves separator/instance scoping.
class COIN_WGPU_DLL_API SoWgpuDepthPolicyElement : public SoElement {
  SO_ELEMENT_HEADER(SoWgpuDepthPolicyElement);
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
