#ifndef COIN_SORENDERMATRIXPOLICYELEMENT_H
#define COIN_SORENDERMATRIXPOLICYELEMENT_H

#include <Inventor/elements/SoInt32Element.h>

/*!
  \class SoRenderMatrixPolicyElement
  \brief Selects how render commands obtain view and projection matrices.

  Application-defined nodes can install screen-space or other local camera
  matrices in the ordinary Coin matrix elements and select CAPTURE_CURRENT.
  Render actions that record commands will then preserve those matrices
  instead of applying their frame-wide camera matrices.

  \ingroup coin_elements
*/
class COIN_DLL_API SoRenderMatrixPolicyElement : public SoInt32Element {
  typedef SoInt32Element inherited;

  SO_ELEMENT_HEADER(SoRenderMatrixPolicyElement);

public:
  enum Policy {
    INHERIT_CAMERA_MATRICES = 0,
    CAPTURE_CURRENT_MATRICES
  };

  static void initClass(void);
  static void set(SoState * state, SoNode * node, Policy policy);
  static Policy get(SoState * state);

protected:
  virtual ~SoRenderMatrixPolicyElement();
};

#endif // !COIN_SORENDERMATRIXPOLICYELEMENT_H
