#ifndef COIN_RENDER_PBR_FIXTURES_H
#define COIN_RENDER_PBR_FIXTURES_H

// CAD fixtures built entirely with Coin geometry and Sb primitives. The
// material factory starts as a legacy baseline and will also serve PBR.
#include <Inventor/SbBox3f.h>
#include <Inventor/SbColor.h>
#include <Inventor/SbPlane.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbString.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/SbVec2i32.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/lists/SbList.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoTranslation.h>
#include <cmath>

namespace coin_pbr_fixture {

inline SoNode * material(const SbColor & color, float metallic, float roughness)
{
  SoMaterial * result = new SoMaterial;
  result->diffuseColor = color;
  result->specularColor = metallic > 0.5f ? color : SbColor(0.04f, 0.04f, 0.04f);
  result->shininess = 1.0f - roughness;
  result->setName("fixture_material_legacy");
  return result;
}

inline SoSeparator * object(SoSeparator * parent, const char * name,
                            const SbVec3f & position, const SbColor & color,
                            float metallic, float roughness)
{
  SoSeparator * result = new SoSeparator;
  result->setName(name);
  parent->addChild(result);
  result->addChild(material(color, metallic, roughness));
  SoTranslation * translation = new SoTranslation;
  translation->translation = position;
  result->addChild(translation);
  return result;
}

inline void box(SoSeparator * parent, const char * name, const SbVec3f & position,
                const SbVec3f & size, const SbColor & color,
                float metallic = 0.0f, float roughness = 0.4f)
{
  SoSeparator * part = object(parent, name, position, color, metallic, roughness);
  SoCube * shape = new SoCube;
  shape->width = size[0]; shape->height = size[1]; shape->depth = size[2];
  part->addChild(shape);
}

// Revolve a closed r/z profile. Each profile edge has its own normals so
// planar caps and chamfers remain geometric; cylindrical normals are smooth.
inline void ring(SoSeparator * parent, const char * name, const SbVec3f & position,
                 float inner, float outer, float height, float bevel,
                 const SbColor & color, float metallic, float roughness)
{
  const int segments = 64;
  const float half = height * 0.5f;
  const SbVec2f profile[] = {
    SbVec2f(inner + bevel, half), SbVec2f(outer - bevel, half),
    SbVec2f(outer, half - bevel), SbVec2f(outer, -half + bevel),
    SbVec2f(outer - bevel, -half), SbVec2f(inner + bevel, -half),
    SbVec2f(inner, -half + bevel), SbVec2f(inner, half - bevel)
  };
  SbList<SbVec3f> points(segments * 32), normals(segments * 32);
  SbList<int32_t> indices(segments * 40);
  for (int edge = 0; edge < 8; ++edge) {
    const SbVec2f & a = profile[edge];
    const SbVec2f & b = profile[(edge + 1) % 8];
    const SbVec2f delta = b - a;
    for (int segment = 0; segment < segments; ++segment) {
      const float angles[] = {float(segment) * 6.28318530718f / segments,
                             float(segment + 1) * 6.28318530718f / segments};
      const int first = points.getLength();
      const SbVec2f corners[] = {a, b, b, a};
      const int angleSlots[] = {0, 0, 1, 1};
      for (int vertex = 0; vertex < 4; ++vertex) {
        const float angle = angles[angleSlots[vertex]];
        const float cosine = std::cos(angle), sine = std::sin(angle);
        points.append(SbVec3f(corners[vertex][0] * cosine,
                             corners[vertex][0] * sine, corners[vertex][1]));
        SbVec3f normal(-delta[1] * cosine, -delta[1] * sine, delta[0]);
        normal.normalize();
        normals.append(normal);
        indices.append(first + vertex);
      }
      indices.append(-1);
    }
  }
  SoSeparator * part = object(parent, name, position, color, metallic, roughness);
  SoShapeHints * hints = new SoShapeHints;
  hints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
  hints->shapeType = SoShapeHints::SOLID;
  part->addChild(hints);
  SoCoordinate3 * coords = new SoCoordinate3;
  coords->point.setValues(0, points.getLength(), points.getArrayPtr());
  part->addChild(coords);
  SoNormal * normal = new SoNormal;
  normal->vector.setValues(0, normals.getLength(), normals.getArrayPtr());
  part->addChild(normal);
  SoNormalBinding * binding = new SoNormalBinding;
  binding->value = SoNormalBinding::PER_VERTEX_INDEXED;
  part->addChild(binding);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  face->coordIndex.setValues(0, indices.getLength(), indices.getArrayPtr());
  face->normalIndex.setValues(0, indices.getLength(), indices.getArrayPtr());
  part->addChild(face);
}

inline SoSeparator * create(const SbString & fixture, SbBool cut)
{
  SoSeparator * root = new SoSeparator;
  root->setName("CAD_fixture");
  if (cut) {
    SoClipPlane * plane = new SoClipPlane;
    plane->plane = SbPlane(SbVec3f(1, 0, 0), 0.0f);
    root->addChild(plane);
  }
  const SbColor aluminium(0.68f, 0.70f, 0.73f), steel(0.42f, 0.44f, 0.46f);
  const SbColor polymer(0.03f, 0.07f, 0.12f), paint(0.16f, 0.32f, 0.52f);
  if (fixture == "flange") {
    ring(root, "flange", SbVec3f(0, 0, 0), 0.65f, 2.2f, 0.35f, 0.06f,
         aluminium, 1, 0.25f);
    ring(root, "hub", SbVec3f(0, 0, 0.6f), 0.65f, 1.1f, 0.85f, 0.06f,
         aluminium, 1, 0.25f);
    for (int i = 0; i < 8; ++i) {
      const float angle = i * 6.28318530718f / 8;
      ring(root, "fastener", SbVec3f(1.65f * std::cos(angle),
           1.65f * std::sin(angle), 0.29f), 0.08f, 0.18f, 0.23f, 0.025f,
           steel, 1, 0.45f);
    }
  } else if (fixture == "assembly") {
    box(root, "base", SbVec3f(0, 0, -0.25f), SbVec3f(5, 3.6f, 0.3f),
        paint, 0, 0.35f);
    ring(root, "bearing", SbVec3f(0.5f, 0, 0.1f), 0.3f, 1.2f, 0.4f, 0.06f,
         aluminium, 1, 0.2f);
    ring(root, "shaft", SbVec3f(0.5f, 0, 0.7f), 0.08f, 0.3f, 1.2f, 0.025f,
         steel, 1, 0.3f);
    box(root, "motor", SbVec3f(-1.4f, 0, 0.55f), SbVec3f(1.2f, 1.2f, 1.4f),
        polymer, 0, 0.55f);
    for (int x = -1; x <= 1; x += 2)
      for (int y = -1; y <= 1; y += 2)
        ring(root, "bolt", SbVec3f(x * 1.85f, y * 1.25f, 0.08f),
             0.06f, 0.17f, 0.26f, 0.02f, steel, 1, 0.5f);
  } else if (fixture == "housing") {
    box(root, "floor", SbVec3f(0, 0, -1.1f), SbVec3f(5, 4, 0.3f), paint);
    box(root, "left_wall", SbVec3f(-2.35f, 0, 0), SbVec3f(0.3f, 4, 2.2f), paint);
    box(root, "right_wall", SbVec3f(2.35f, 0, 0), SbVec3f(0.3f, 4, 2.2f), paint);
    box(root, "back_wall", SbVec3f(0, 1.85f, 0), SbVec3f(4.4f, 0.3f, 2.2f), paint);
    box(root, "electronics", SbVec3f(-0.9f, 0.4f, -0.5f), SbVec3f(1.4f, 2, 0.5f),
        polymer, 0, 0.65f);
    ring(root, "internal_bearing", SbVec3f(0.95f, 0, -0.3f),
         0.28f, 0.8f, 1.2f, 0.045f, aluminium, 1, 0.35f);
  } else if (fixture == "grid") {
    const float roughness[] = {0.08f, 0.25f, 0.5f, 0.85f};
    for (int row = 0; row < 2; ++row) {
      for (int column = 0; column < 4; ++column) {
        SoSeparator * part = object(root, "sample",
          SbVec3f(column * 1.1f - 1.65f, row * 1.1f - 0.55f, 0),
          aluminium, float(row), roughness[column]);
        SoSphere * sphere = new SoSphere; sphere->radius = 0.45f;
        part->addChild(sphere);
      }
    }
  } else { root->ref(); root->unref(); return NULL; }
  return root;
}

inline SoCamera * camera(SoNode * scene, const SbString & preset,
                         const SbVec2i32 & size, SbVec3f & center)
{
  const SbViewportRegion viewport(static_cast<short>(size[0]), static_cast<short>(size[1]));
  SoGetBoundingBoxAction bounds(viewport);
  bounds.apply(scene);
  const SbBox3f box = bounds.getBoundingBox();
  if (box.isEmpty()) return NULL;
  center = box.getCenter();
  SbVec3f direction(1, -1, 1), up(0, 0, 1);
  if (preset == "front") direction.setValue(0, -1, 0);
  else if (preset == "top") { direction.setValue(0, 0, 1); up.setValue(0, 1, 0); }
  else if (preset == "right") direction.setValue(1, 0, 0);
  else if (preset != "iso-perspective" && preset != "iso-orthographic" &&
           preset != "detail") return NULL;
  SoCamera * result = preset == "iso-perspective" ?
    static_cast<SoCamera *>(new SoPerspectiveCamera) : new SoOrthographicCamera;
  direction.normalize();
  const float radius = box.getSize().length();
  result->position = center + direction * radius;
  result->pointAt(center, up);
  result->viewAll(scene, viewport, 1.15f);
  if (preset == "detail") result->scaleHeight(0.55f);
  return result;
}

} // namespace coin_pbr_fixture
#endif
