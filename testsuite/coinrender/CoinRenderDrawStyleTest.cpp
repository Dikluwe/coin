#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderFramePlanBuilder.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
bool check(bool condition, const char * message) {
  if (!condition) std::cerr << "CoinRenderDrawStyleTest: " << message << '\n';
  return condition;
}
void triangle(void * p, SoCallbackAction * a, const SoPrimitiveVertex * x,
              const SoPrimitiveVertex * y, const SoPrimitiveVertex * z) {
  static_cast<CoinRenderFramePlanBuilder *>(p)->addTriangle(a,x,y,z);
}
void line(void * p, SoCallbackAction * a, const SoPrimitiveVertex * x, const SoPrimitiveVertex * y) {
  static_cast<CoinRenderFramePlanBuilder *>(p)->addLine(a,x,y);
}
void point(void * p, SoCallbackAction * a, const SoPrimitiveVertex * x) {
  static_cast<CoinRenderFramePlanBuilder *>(p)->addPoint(a,x);
}
SoDrawStyle * style(int value, bool overrideState = false) {
  SoDrawStyle * node = new SoDrawStyle;
  node->style = value; node->setOverride(overrideState ? TRUE : FALSE);
  return node;
}
SoCoordinate3 * coordinates() {
  SoCoordinate3 * node = new SoCoordinate3;
  const SbVec3f points[] = {SbVec3f(-.8f,-.8f,0), SbVec3f(.8f,-.8f,0),
                          SbVec3f(.8f,.8f,0), SbVec3f(-.8f,.8f,0)};
  node->point.setValues(0,4,points);
  return node;
}
SoIndexedFaceSet * faces() {
  SoIndexedFaceSet * node = new SoIndexedFaceSet;
  const int32_t indices[] = {0,1,2,3,-1}; node->coordIndex.setValues(0,5,indices);
  return node;
}
bool capture(SoNode * root, CoinRenderFramePlan & frame) {
  CoinRenderFramePlanBuilder builder;
  builder.beginFrame(SbColor4f(0,0,0,1), SbViewportRegion(64,64));
  CoinRenderAction action(SbViewportRegion(64,64));
  action.addTriangleCallback(SoShape::getClassTypeId(),triangle,&builder);
  action.addLineSegmentCallback(SoShape::getClassTypeId(),line,&builder);
  action.addPointCallback(SoShape::getClassTypeId(),point,&builder);
  action.apply(root);
  std::string error;
  return builder.build(frame,&error);
}
bool stateContract() {
  SoSeparator * root = new SoSeparator; root->ref();
  root->addChild(new SoCube);
  SoSeparator * hidden = new SoSeparator; root->addChild(hidden);
  hidden->addChild(style(SoDrawStyle::INVISIBLE));
  // A hidden shape does not consume the unsupported clipping state.
  for (int i=0;i<9;++i) hidden->addChild(new SoClipPlane);
  hidden->addChild(new SoCube); hidden->addChild(coordinates());
  SoLineSet * lines = new SoLineSet; lines->numVertices=4; hidden->addChild(lines);
  SoPointSet * points = new SoPointSet; points->numPoints=4; hidden->addChild(points);
  hidden->addChild(faces());
  SoIndexedLineSet * indexedLines = new SoIndexedLineSet;
  const int32_t indices[]={0,1,2,3,-1};indexedLines->coordIndex.setValues(0,5,indices);
  hidden->addChild(indexedLines);
  root->addChild(new SoCube);
  CoinRenderFramePlan frame;
  bool ok=check(capture(root,frame) && frame.draws.size()==2 && frame.vertices.size()==72,
                "INVISIBLE suppresses polygon, line and point callbacks; Separator restores visibility");
  root->unref();

  root=new SoSeparator;root->ref();
  SoGroup * group=new SoGroup;root->addChild(group);
  group->addChild(style(SoDrawStyle::INVISIBLE));group->addChild(new SoCube);
  // State continues after the invisible shape and beyond a plain Group.
  group->addChild(style(SoDrawStyle::FILLED));
  SoMaterial * red=new SoMaterial;red->diffuseColor=SbColor(1,0,0);group->addChild(red);
  root->addChild(new SoCube);
  ok=check(capture(root,frame) && frame.draws.size()==1 && frame.materials.size()==1 &&
           frame.materials[0].diffuse[0]==1 && frame.materials[0].diffuse[1]==0,
           "invisible shape does not interrupt subsequent state traversal") && ok;
  root->unref();

  root=new SoSeparator;root->ref();
  root->addChild(style(SoDrawStyle::INVISIBLE,true));
  root->addChild(style(SoDrawStyle::FILLED));root->addChild(new SoCube);
  ok=check(capture(root,frame) && frame.draws.empty() && frame.vertices.empty() &&
           frame.materials.empty() && frame.renderStates.empty(),
           "effective invisible override emits no geometry or captured resources") && ok;
  root->unref();

  root=new SoSeparator;root->ref();root->addChild(style(SoDrawStyle::FILLED,true));
  root->addChild(style(SoDrawStyle::INVISIBLE));root->addChild(new SoCube);
  ok=check(capture(root,frame) && frame.draws.size()==1,"filled override keeps shape visible") && ok;
  root->unref();

  root=new SoSeparator;root->ref();SoDrawStyle * ignored=style(SoDrawStyle::INVISIBLE);
  ignored->style.setIgnored(TRUE);root->addChild(ignored);root->addChild(new SoCube);
  ok=check(capture(root,frame) && frame.draws.size()==1,"ignored style preserves inherited visibility") && ok;
  root->unref();return ok;
}
bool pixel(const std::vector<uint8_t> & image, int red, int blue) {
  const size_t offset=(32*64+32)*4;
  return image.size()==64*64*4 && std::abs(int(image[offset])-red)<=2 &&
    image[offset+1]<=2 && std::abs(int(image[offset+2])-blue)<=2;
}
bool actionContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if(cpu) target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  SoSeparator * root=new SoSeparator;root->ref();
  SoOrthographicCamera * camera=new SoOrthographicCamera;camera->height=2;
  camera->position=SbVec3f(0,0,3);camera->nearDistance=.1f;camera->farDistance=10;root->addChild(camera);
  SoLightModel * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
  SoMaterial * blue=new SoMaterial;blue->diffuseColor=SbColor(0,0,1);root->addChild(blue);
  root->addChild(coordinates());root->addChild(faces());
  SoSeparator * nested=new SoSeparator;root->addChild(nested);
  SoDrawStyle * visibility=style(SoDrawStyle::INVISIBLE);nested->addChild(visibility);
  SoMaterial * red=new SoMaterial;red->diffuseColor=SbColor(1,0,0);nested->addChild(red);
  SoCube * cube=new SoCube;cube->width=1;cube->height=1;cube->depth=1;nested->addChild(cube);
  nested->addChild(faces());
  SoIndexedLineSet * lines=new SoIndexedLineSet;const int32_t ix[]={0,2,-1};
  lines->coordIndex.setValues(0,3,ix);nested->addChild(lines);
  SoPointSet * points=new SoPointSet;points->startIndex=0;points->numPoints=4;nested->addChild(points);
  CoinRenderAction action(SbViewportRegion(64,64));action.setRenderTarget(target.get());
  action.setBackgroundColor(SbColor4f(0,0,0,1));
  bool ok=true;
  for(int fast=0;fast<2;++fast) {
    action.setFastPathEnabled(fast ? TRUE : FALSE);
    for(int step=0;step<3;++step) {
      visibility->style=step==1 ? SoDrawStyle::FILLED : SoDrawStyle::INVISIBLE;
      action.apply(root);std::vector<uint8_t> image;target->readbackRGBA(image);
      ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
               pixel(image,step==1 ? 255 : 0,step==1 ? 0 : 255),
               "CPU/GPU fast and callback paths update visibility without stale cached geometry") && ok;
    }
  }
  root->unref();
  // An all-invisible frame must publish the clear, replacing the previous image.
  root=new SoSeparator;root->ref();visibility=style(SoDrawStyle::INVISIBLE);root->addChild(visibility);
  SoTextureUnit * unit=new SoTextureUnit;unit->unit=8;root->addChild(unit);
  SoTexture2 * texture=new SoTexture2;const unsigned char white[]={255,255,255};
  texture->image.setValue(SbVec2s(1,1),3,white);root->addChild(texture);
  for(int i=0;i<9;++i)root->addChild(new SoClipPlane);
  root->addChild(new SoCube);root->addChild(coordinates());root->addChild(faces());
  root->addChild(lines=new SoIndexedLineSet);lines->coordIndex.setValues(0,3,ix);
  const uint64_t serial=target->getLastSubmissionSerial();
  action.apply(root);std::vector<uint8_t> image;target->readbackRGBA(image);
  ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS && pixel(image,0,0) &&
           target->getLastSubmissionSerial()>serial,"empty invisible frame publishes a fresh clear") && ok;
  const uint64_t hiddenSerial=target->getLastSubmissionSerial();
  visibility->style=SoDrawStyle::FILLED;action.apply(root);target->readbackRGBA(image);
  ok=check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED && pixel(image,0,0) &&
           target->getLastSubmissionSerial()==hiddenSerial,
           "unsupported resources become errors when the shape becomes visible") && ok;
  visibility->style=SoDrawStyle::INVISIBLE;action.apply(root);
  ok=check(action.getLastStatus()==CoinRenderAction::SUCCESS &&
           target->getLastSubmissionSerial()>hiddenSerial,
           "invisible frame recovers after visible unsupported resources") && ok;
  root->unref();return ok;
}
}
int main() {
  SoDB::init();CoinRenderAction::initClass();
  if(!stateContract() || !actionContract(true))return 1;
  if(!CoinRenderAction::isGpuBackendAvailable()) {
    std::cerr<<"[SKIP] GPU adapter unavailable\n";return 77;
  }
  if(!actionContract(false))return 1;
  std::cout<<"DrawStyle INVISIBLE capture/CPU/GPU contracts passed\n";return 0;
}
