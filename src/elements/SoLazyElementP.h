#ifndef COIN_SOLAZYELEMENTP_H
#define COIN_SOLAZYELEMENTP_H

#include <Inventor/elements/SoLazyElement.h>

// Compatibility seam for the imported FreeCAD retained capture. Alpha-test
// tokens remain native here and are converted explicitly at the IR boundary.
// Packed vertex colors still require a separate capture contract.
class SoLazyElementP {
public:
  struct RenderSnapshot {
    SbColor diffuse, ambient, emissive, specular;
    float transparency = 0, shininess = 0, packedOpacity = 1;
    float alphaTestValue = 0.5f;
    int lightModel = 0, blendSource = 0, blendDestination = 0;
    int alphaBlendSource = 0, alphaBlendDestination = 0, alphaTestFunction = 0;
    SbBool blending = FALSE, separateBlending = FALSE;
    SbBool twoSidedLighting = FALSE, packedVertexColors = FALSE;
  };
  static RenderSnapshot captureRenderSnapshot(SoState * state, int index) {
    RenderSnapshot result;
    result.diffuse = SoLazyElement::getDiffuse(state, index);
    result.ambient = SoLazyElement::getAmbient(state);
    result.emissive = SoLazyElement::getEmissive(state);
    result.specular = SoLazyElement::getSpecular(state);
    result.transparency = SoLazyElement::getTransparency(state, index);
    result.shininess = SoLazyElement::getShininess(state);
    result.lightModel = SoLazyElement::getLightModel(state);
    result.blending = SoLazyElement::getBlending(state, result.blendSource, result.blendDestination);
    result.separateBlending = SoLazyElement::getAlphaBlending(state, result.alphaBlendSource, result.alphaBlendDestination);
    float reference = 0;
    result.alphaTestFunction = SoLazyElement::getAlphaTest(state, reference);
    result.alphaTestValue = reference;
    result.twoSidedLighting = SoLazyElement::getTwoSidedLighting(state);
    return result;
  }
};

#endif
