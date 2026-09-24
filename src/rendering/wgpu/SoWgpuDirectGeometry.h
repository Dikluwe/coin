#ifndef COIN_SOWGPUDIRECTGEOMETRY_H
#define COIN_SOWGPUDIRECTGEOMETRY_H

#include <Inventor/SbVec3f.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoNormalBindingElement.h>

#include <cstdint>
#include <cstddef>

template <typename T>
struct SoWgpuSpan {
  const T * data;
  size_t size;

  SoWgpuSpan() : data(nullptr), size(0) {}
  SoWgpuSpan(const T * d, size_t s) : data(d), size(s) {}

  const T & operator[](size_t idx) const { return data[idx]; }
  bool empty() const { return size == 0 || data == nullptr; }
};

enum class FastPathResult {
  SUCCESS_PRUNE = 0,
  FALLBACK_CONTINUE = 1,
  INVALID_SCENE = 2,
  UNSUPPORTED = 3
};

struct DirectGeometryView {
  SoWgpuSpan<SbVec3f> positions;
  SoWgpuSpan<SbVec3f> normals;
  SoWgpuSpan<SbVec2f> texcoords;
  SoWgpuSpan<uint32_t> orderedRGBA;
  SoWgpuSpan<int32_t> coordIndex;
  SoWgpuSpan<int32_t> normalIndex;
  SoWgpuSpan<int32_t> materialIndex;
  SoWgpuSpan<int32_t> texCoordIndex;
  SoMaterialBindingElement::Binding materialBinding;
  SoNormalBindingElement::Binding normalBinding;

  DirectGeometryView()
    : materialBinding(SoMaterialBindingElement::OVERALL),
      normalBinding(SoNormalBindingElement::OVERALL)
  {
  }
};

#endif // !COIN_SOWGPUDIRECTGEOMETRY_H
