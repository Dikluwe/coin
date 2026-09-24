#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char * message, const SoWgpuRenderAction & action) {
  if (!condition) {
    std::cerr << "WgpuSceneTextureTest: " << message << ": "
              << action.getLastError().getString() << "\n";
  }
  return condition;
}

SoSeparator * makeTexturedQuad(SoSceneTexture2 * texture) {
  SoSeparator * quad = new SoSeparator;
  quad->addChild(texture);
  SoTextureCoordinate2 * uv = new SoTextureCoordinate2;
  uv->point.set1Value(0, SbVec2f(0, 0));
  uv->point.set1Value(1, SbVec2f(1, 0));
  uv->point.set1Value(2, SbVec2f(1, 1));
  uv->point.set1Value(3, SbVec2f(0, 1));
  quad->addChild(uv);
  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.set1Value(0, SbVec3f(-1, -1, 0));
  coordinates->point.set1Value(1, SbVec3f(1, -1, 0));
  coordinates->point.set1Value(2, SbVec3f(1, 1, 0));
  coordinates->point.set1Value(3, SbVec3f(-1, 1, 0));
  quad->addChild(coordinates);
  SoIndexedFaceSet * faces = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, 3, -1};
  faces->coordIndex.setValues(0, 5, indices);
  faces->textureCoordIndex.setValues(0, 5, indices);
  quad->addChild(faces);
  return quad;
}

SoPerspectiveCamera * makeCamera() {
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0, 0, 2.5f);
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  return camera;
}

} // namespace

int main() {
  SoDB::init();
  SoWgpuRenderAction::initClass();

  SoSeparator * child = new SoSeparator;
  child->ref();
  child->addChild(makeCamera());
  SoLightModel * childLighting = new SoLightModel;
  childLighting->model = SoLightModel::BASE_COLOR;
  child->addChild(childLighting);
  SoMaterial * red = new SoMaterial;
  red->diffuseColor.setValue(1, 0, 0);
  child->addChild(red);
  SoCoordinate3 * upperCoordinates = new SoCoordinate3;
  upperCoordinates->point.set1Value(0, SbVec3f(-1, 0.1f, 0));
  upperCoordinates->point.set1Value(1, SbVec3f(1, 0.1f, 0));
  upperCoordinates->point.set1Value(2, SbVec3f(1, 1, 0));
  upperCoordinates->point.set1Value(3, SbVec3f(-1, 1, 0));
  child->addChild(upperCoordinates);
  SoIndexedFaceSet * upperFace = new SoIndexedFaceSet;
  const int32_t upperIndices[] = {0, 1, 2, 3, -1};
  upperFace->coordIndex.setValues(0, 5, upperIndices);
  child->addChild(upperFace);

  SoSceneTexture2 * sceneTexture = new SoSceneTexture2;
  sceneTexture->size.setValue(32, 32);
  sceneTexture->scene.setValue(child);
  sceneTexture->backgroundColor.setValue(0, 0, 1, 1);
  sceneTexture->type.setValue(SoSceneTexture2::RGBA8);

  SoSeparator * parent = new SoSeparator;
  parent->ref();
  parent->addChild(makeCamera());
  SoLightModel * parentLighting = new SoLightModel;
  parentLighting->model = SoLightModel::BASE_COLOR;
  parent->addChild(parentLighting);
  SoMaterial * white = new SoMaterial;
  white->diffuseColor.setValue(1, 1, 1);
  parent->addChild(white);
  parent->addChild(makeTexturedQuad(sceneTexture));

  SoWgpuRenderTarget * target = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
  SoWgpuRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target);
  action.apply(parent);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
             "simple scene-to-texture pass failed", action)) return 1;
  std::vector<uint8_t> baseline;
  target->readbackRGBA(baseline);
  if (!check(baseline.size() == 64u * 64u * 4u,
             "parent readback missing", action)) return 1;
  const size_t upper = (22u * 64u + 32u) * 4u;
  const size_t lower = (42u * 64u + 32u) * 4u;
  if (!check(baseline[upper] > baseline[upper + 2] &&
             baseline[upper] > 80 &&
             baseline[lower + 2] > baseline[lower],
             "scene texture has incorrect colors or vertical orientation", action)) return 1;

  SoWgpuRenderTarget * swapped = SoWgpuRenderTarget::createOffscreen(SbVec2i32(64, 64));
  action.setRenderTarget(swapped);
  action.apply(parent);
  std::vector<uint8_t> swappedColor;
  swapped->readbackRGBA(swappedColor);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             swappedColor == baseline,
             "target swap changed the staged scene-texture result", action)) return 1;
  action.setRenderTarget(target);
  delete swapped;

  SoSeparator * middle = new SoSeparator;
  middle->ref();
  middle->addChild(makeCamera());
  SoLightModel * middleLighting = new SoLightModel;
  middleLighting->model = SoLightModel::BASE_COLOR;
  middle->addChild(middleLighting);
  SoMaterial * middleWhite = new SoMaterial;
  middleWhite->diffuseColor.setValue(1, 1, 1);
  middle->addChild(middleWhite);
  SoSceneTexture2 * nested = new SoSceneTexture2;
  nested->size.setValue(32, 32);
  nested->scene.setValue(child);
  nested->backgroundColor.setValue(0, 0, 1, 1);
  middle->addChild(makeTexturedQuad(nested));
  sceneTexture->scene.setValue(middle);
  action.apply(parent);
  std::vector<uint8_t> nestedColor;
  target->readbackRGBA(nestedColor);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS &&
             nestedColor.size() == baseline.size() &&
             nestedColor[upper] > nestedColor[upper + 2] &&
             nestedColor[lower + 2] > nestedColor[lower],
             "two dependent scene-texture passes failed", action)) return 1;
  sceneTexture->scene.setValue(child);
  middle->unref();

  SoSceneTexture2 * invalidSecond = new SoSceneTexture2;
  invalidSecond->scene.setValue(child);
  invalidSecond->type.setValue(SoSceneTexture2::DEPTH);
  parent->addChild(invalidSecond);
  action.apply(parent);
  std::vector<uint8_t> afterFailure;
  target->readbackRGBA(afterFailure);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED &&
             afterFailure == nestedColor,
             "second-pass rejection changed published parent frame", action)) return 1;
  parent->removeChild(invalidSecond);

  sceneTexture->scene.setValue(parent);
  action.apply(parent);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::UNSUPPORTED &&
             std::string(action.getLastError().getString()).find("cycle") != std::string::npos,
             "recursive scene-texture dependency was not rejected", action)) return 1;
  sceneTexture->scene.setValue(child);

  sceneTexture->size.setValue(64, 64);
  if (!check(target->resize(SbVec2i32(96, 96)), "target resize failed", action)) return 1;
  action.setViewportRegion(SbViewportRegion(96, 96));
  action.apply(parent);
  if (!check(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
             "resize/recovery after rejected passes failed", action)) return 1;
  std::vector<uint8_t> resized;
  target->readbackRGBA(resized);
  if (!check(resized.size() == 96u * 96u * 4u,
             "resized parent frame missing", action)) return 1;

  delete target;
  parent->unref();
  child->unref();
  std::cout << "WgpuSceneTextureTest passed\n";
  return 0;
}
