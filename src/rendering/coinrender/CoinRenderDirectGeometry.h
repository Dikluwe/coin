#ifndef COIN_RENDER_DIRECT_GEOMETRY_H
#define COIN_RENDER_DIRECT_GEOMETRY_H

#include <Inventor/SbVec3f.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoNormalBindingElement.h>

#include <cstdint>
#include <cstddef>

template <typename T>
struct CoinRenderSpan {
  const T * data;
  size_t size;

  CoinRenderSpan() : data(nullptr), size(0) {}
  CoinRenderSpan(const T * d, size_t s) : data(d), size(s) {}

  const T & operator[](size_t idx) const { return data[idx]; }
  bool empty() const { return size == 0 || data == nullptr; }
};

enum class CoinRenderFastPathResult {
  SUCCESS_PRUNE = 0,
  FALLBACK_CONTINUE = 1,
  INVALID_SCENE = 2,
  UNSUPPORTED = 3
};

struct CoinRenderDirectGeometryView {
  CoinRenderSpan<SbVec3f> positions;
  CoinRenderSpan<SbVec3f> normals;
  CoinRenderSpan<SbVec2f> texcoords;
  CoinRenderSpan<uint32_t> orderedRGBA;
  CoinRenderSpan<int32_t> coordIndex;
  CoinRenderSpan<int32_t> normalIndex;
  CoinRenderSpan<int32_t> materialIndex;
  CoinRenderSpan<int32_t> texCoordIndex;
  SoMaterialBindingElement::Binding materialBinding;
  SoNormalBindingElement::Binding normalBinding;

  CoinRenderDirectGeometryView()
    : materialBinding(SoMaterialBindingElement::OVERALL),
      normalBinding(SoNormalBindingElement::OVERALL)
  {
  }
};

#endif // !COIN_RENDER_DIRECT_GEOMETRY_H
