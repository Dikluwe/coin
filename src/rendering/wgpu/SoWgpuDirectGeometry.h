#ifndef COIN_SOWGPUDIRECTGEOMETRY_H
#define COIN_SOWGPUDIRECTGEOMETRY_H

#include <Inventor/SbVec3f.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoNormalBindingElement.h>

#include <cstdint>
#include <cstddef>
#include <cmath>

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

struct VertexDeduplicationKey {
  int32_t coordIdx;
  int32_t normalIdx;
  int32_t texCoordIdx;
  int32_t materialIdx;

  bool operator<(const VertexDeduplicationKey & o) const {
    if (coordIdx != o.coordIdx) return coordIdx < o.coordIdx;
    if (normalIdx != o.normalIdx) return normalIdx < o.normalIdx;
    if (texCoordIdx != o.texCoordIdx) return texCoordIdx < o.texCoordIdx;
    return materialIdx < o.materialIdx;
  }
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

class SoWgpuFastPathValidator {
public:
  static bool isQuadConvex(const SbVec3f & p0,
                           const SbVec3f & p1,
                           const SbVec3f & p2,
                           const SbVec3f & p3)
  {
    SbVec3f e0 = p1 - p0;
    SbVec3f e1 = p2 - p1;
    SbVec3f e2 = p3 - p2;
    SbVec3f e3 = p0 - p3;

    SbVec3f c0 = e0.cross(e1);
    SbVec3f c1 = e1.cross(e2);
    SbVec3f c2 = e2.cross(e3);
    SbVec3f c3 = e3.cross(e0);

    float lenSq0 = c0.sqrLength();
    if (lenSq0 < 1e-10f) return false;

    // Check that all corner normals point into the same half-space
    if (c0.dot(c1) <= 1e-7f) return false;
    if (c0.dot(c2) <= 1e-7f) return false;
    if (c0.dot(c3) <= 1e-7f) return false;

    // Check planarity: distance of p3 to the plane formed by (p0, p1, p2)
    c0.normalize();
    float dist = std::abs((p3 - p0).dot(c0));
    float diagonal = (p2 - p0).length();
    if (diagonal > 1e-5f && (dist / diagonal) > 1e-2f) {
      return false; // Non-planar quad
    }

    return true;
  }
};

#endif // !COIN_SOWGPUDIRECTGEOMETRY_H
