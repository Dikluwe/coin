#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#define GL_GLEXT_PROTOTYPES 1
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinateCube.h>
#include <Inventor/nodes/SoTextureCoordinateCylinder.h>
#include <Inventor/nodes/SoTextureCoordinateDefault.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/nodes/SoTextureCoordinateEnvironment.h>
#include <Inventor/nodes/SoTextureCoordinateNormalMap.h>
#include <Inventor/nodes/SoTextureCoordinateObject.h>
#include <Inventor/nodes/SoTextureCoordinatePlane.h>
#include <Inventor/nodes/SoTextureCoordinateReflectionMap.h>
#include <Inventor/nodes/SoTextureCoordinateSphere.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/system/gl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr int side=128;
bool require(bool result,const std::string & label) {
  if(!result) std::cerr << "texgen_transition FAIL " << label << '\n';
  return result;
}
struct State {
  std::array<unsigned,8> before{};
  std::array<unsigned,8> after{};
  std::array<unsigned,8> restored{};
  int maxUnits=0;
  int captures=0;
  GLenum error=GL_NO_ERROR;
};
struct FunctionWitness {
  int unit=0;
  int referenceCalls=0,glCalls=0;
  SoMultiTextureCoordinateElement::CoordType referenceType=SoMultiTextureCoordinateElement::NONE;
  SoMultiTextureCoordinateElement::CoordType glType=SoMultiTextureCoordinateElement::NONE;
  std::array<SbVec4f,2> reference{},gl{};
};
void observeFunction(void * userdata,SoAction * action) {
  auto * witness=static_cast<FunctionWitness *>(userdata);
  const bool gl=action->isOfType(SoGLRenderAction::getClassTypeId());
  const bool callback=action->isOfType(SoCallbackAction::getClassTypeId());
  if(!gl && !callback) return;
  const auto * element=SoMultiTextureCoordinateElement::getInstance(action->getState());
  const auto type=SoMultiTextureCoordinateElement::getType(action->getState(),witness->unit);
  if(gl) {witness->glType=type;++witness->glCalls;}
  else {witness->referenceType=type;++witness->referenceCalls;}
  if(type!=SoMultiTextureCoordinateElement::FUNCTION) return;
  const SbVec3f points[]={SbVec3f(.31f,.42f,.17f),SbVec3f(-.43f,.27f,-.22f)};
  const SbVec3f normals[]={SbVec3f(.31f,.41f,.857f),SbVec3f(-.52f,.38f,.765f)};
  for(int sample=0;sample<2;++sample) {
    const SbVec4f value=element->get(witness->unit,points[sample],normals[sample]);
    if(gl) witness->gl[size_t(sample)]=value;
    else witness->reference[size_t(sample)]=value;
  }
}
class TextureFunctionProbe : public SoShape {
  SO_NODE_HEADER(TextureFunctionProbe);
public:
  static void initClass() {SO_NODE_INIT_CLASS(TextureFunctionProbe,SoShape,"Shape");}
  TextureFunctionProbe() {SO_NODE_CONSTRUCTOR(TextureFunctionProbe);}
  FunctionWitness * witness=nullptr;
  void GLRender(SoGLRenderAction * action) override {observeFunction(witness,action);}
  void callback(SoCallbackAction * action) override {observeFunction(witness,action);}
protected:
  ~TextureFunctionProbe() override=default;
  void computeBBox(SoAction *,SbBox3f & box,SbVec3f & center) override {
    box.setBounds(SbVec3f(-.7f,-.6f,-.4f),SbVec3f(.8f,.7f,.5f));
    center.setValue(.05f,.05f,.05f);
  }
  void generatePrimitives(SoAction *) override {}
};
SO_NODE_SOURCE(TextureFunctionProbe);
bool equalFunction(const FunctionWitness & witness,int kind,const std::string & label) {
  const auto expected=kind<3?SoMultiTextureCoordinateElement::FUNCTION:
    kind==3?SoMultiTextureCoordinateElement::DEFAULT:
    SoMultiTextureCoordinateElement::EXPLICIT;
  bool ok=require(witness.referenceCalls==1 && witness.glCalls==1 &&
    witness.referenceType==expected && witness.glType==expected,
    label+" callback traversal and GL traversal must carry successor coordinate type");
  if(kind<3) for(int sample=0;sample<2;++sample)
    for(int component=0;component<4;++component)
      ok &= require(std::abs(witness.gl[size_t(sample)][component]-
        witness.reference[size_t(sample)][component])<=1e-5f,
        label+" software coordinate callback differs from original callback action");
  return ok;
}
void observe(void * userdata,SoAction * action) {
  if(!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
  auto * state=static_cast<State *>(userdata);
  GLint original=GL_TEXTURE0,maximum=0;
  glGetIntegerv(GL_ACTIVE_TEXTURE,&original);
  glGetIntegerv(GL_MAX_TEXTURE_UNITS,&maximum);
  state->maxUnits=maximum;
  for(int unit=0;unit<std::min(maximum,8);++unit) {
    glActiveTexture(GL_TEXTURE0+unit);
    const unsigned bits=(glIsEnabled(GL_TEXTURE_GEN_S)?1u:0u)|
      (glIsEnabled(GL_TEXTURE_GEN_T)?2u:0u)|
      (glIsEnabled(GL_TEXTURE_GEN_R)?4u:0u)|
      (glIsEnabled(GL_TEXTURE_GEN_Q)?8u:0u);
    (state->captures==0?state->before:
      state->captures==1?state->after:state->restored)[size_t(unit)]=bits;
  }
  glActiveTexture(GLenum(original));
  state->error=glGetError();
  ++state->captures;
}
SoTexture2 * texture(bool colored) {
  auto * result=new SoTexture2;
  result->model=colored?SoTexture2::REPLACE:SoTexture2::MODULATE;
  result->wrapS=SoTexture2::CLAMP;result->wrapT=SoTexture2::CLAMP;
  const unsigned char pixels[]={255,0,0,255,0,255,0,255,
                                0,0,255,255,255,255,0,255};
  const unsigned char white[]={255,255,255,255};
  result->image.setValue(colored?SbVec2s(2,2):SbVec2s(1,1),4,
    colored?pixels:white);
  return result;
}
SoNode * successor(int kind) {
  switch(kind) {
  case 0:return new SoTextureCoordinateSphere;
  case 1:return new SoTextureCoordinateCylinder;
  case 2:return new SoTextureCoordinateCube;
  case 3:return new SoTextureCoordinateDefault;
  case 4:{auto * coordinates=new SoTextureCoordinate2;
    coordinates->point.set1Value(0,SbVec2f(0,0));return coordinates;}
  default:{auto * coordinates=new SoTextureCoordinate3;
    coordinates->point.set1Value(0,SbVec3f(0,0,0));return coordinates;}
  }
}
SoNode * predecessor(int kind) {
  switch(kind) {
  case 0:{auto * object=new SoTextureCoordinateObject;
    object->factorS.setValue(.02f,0,0,0);
    object->factorT.setValue(0,0,0,0);return object;}
  case 1:return new SoTextureCoordinatePlane;
  case 2:return new SoTextureCoordinateEnvironment;
  case 3:return new SoTextureCoordinateReflectionMap;
  default:return new SoTextureCoordinateNormalMap;
  }
}
bool run(int unit,int kind,int initial) {
  State state;
  FunctionWitness function;function.unit=unit;
  SoSeparator * root=new SoSeparator;root->ref();
  auto * camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,3);camera->height=2;
  camera->nearDistance=.1f;camera->farDistance=10;
  root->addChild(camera);
  auto * quality=new SoComplexity;quality->textureQuality=.1f;
  root->addChild(quality);
  auto * material=new SoMaterial;material->diffuseColor.setValue(1,1,1);
  root->addChild(material);
  const int neighbor=unit==0?3:0;
  auto * neighborUnit=new SoTextureUnit;neighborUnit->unit=neighbor;
  root->addChild(neighborUnit);root->addChild(texture(false));
  auto * neighborGen=new SoTextureCoordinateObject;
  root->addChild(neighborGen);
  auto * selectedUnit=new SoTextureUnit;selectedUnit->unit=unit;
  root->addChild(selectedUnit);root->addChild(texture(true));
  root->addChild(predecessor(initial));
  auto * before=new SoCallback;before->setCallback(observe,&state);root->addChild(before);
  root->addChild(successor(kind));
  auto * functionProbe=new TextureFunctionProbe;
  functionProbe->witness=&function;root->addChild(functionProbe);
  auto * after=new SoCallback;after->setCallback(observe,&state);root->addChild(after);
  auto * image=new SoImage;
  const unsigned char payload[]={180,80,40};
  image->image.setValue(SbVec2s(1,1),3,payload);
  image->width=16;image->height=16;
  root->addChild(image);
  SoOffscreenRenderer renderer{SbViewportRegion(side,side)};
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0,0,0));
  SoCallbackAction callbackReference;
  callbackReference.apply(root);
  if(!renderer.render(root)) {root->unref();return require(false,"CoinGL context unavailable");}
  const unsigned char * pixels=renderer.getBuffer();
  const size_t at=size_t(70*side+70)*3;
  const std::string label="unit="+std::to_string(unit)+" successor="+
    std::to_string(kind)+" predecessor="+std::to_string(initial);
  bool ok=require(pixels!=nullptr,label+" readback");
  if(ok) {
    const int expected[]={255,0,0};
    for(int channel=0;channel<3;++channel)
      ok &= require(std::abs(int(pixels[at+channel])-expected[channel])<=2,
        label+" SoImage must use current UV zero after software generator");
  }
  ok &= require(state.captures==2 && state.error==GL_NO_ERROR,label+" GL state probe");
  ok &= equalFunction(function,kind,label);
  if(unit<state.maxUnits && neighbor<state.maxUnits) {
    ok &= require(state.before[size_t(unit)]!=0,label+" hardware texgen control");
    ok &= require(state.after[size_t(unit)]==0,label+" selected unit texgen cleared");
    ok &= require(state.before[size_t(neighbor)]!=0 &&
      state.after[size_t(neighbor)]==state.before[size_t(neighbor)],
      label+" adjacent unit texgen preserved");
  }
  std::cout << "texgen_transition " << label << " max_units=" << state.maxUnits
            << " selected_before=" << state.before[size_t(unit)]
            << " selected_after=" << state.after[size_t(unit)]
            << " neighbor_before=" << state.before[size_t(neighbor)]
            << " neighbor_after=" << state.after[size_t(neighbor)] << '\n';
  const State first=state;
  state=State{};
  function.glCalls=0;
  ok &= require(renderer.render(root),label+" successive frame");
  const unsigned char * next=renderer.getBuffer();
  ok &= require(next && next[at]==255 && next[at+1]==0 && next[at+2]==0,
    label+" successive image");
  ok &= require(state.captures==2 && state.before==first.before &&
    state.after==first.after,label+" successive GL state");
  ok &= equalFunction(function,kind,label+" successive callback");
  root->unref();
  return ok;
}
bool separatorRestore(int unit,int kind) {
  State state;
  auto * root=new SoSeparator;root->ref();
  auto * camera=new SoOrthographicCamera;root->addChild(camera);
  auto * quality=new SoComplexity;quality->textureQuality=.1f;root->addChild(quality);
  const int neighbor=unit==0?3:0;
  auto * neighborUnit=new SoTextureUnit;neighborUnit->unit=neighbor;
  root->addChild(neighborUnit);root->addChild(texture(false));
  root->addChild(new SoTextureCoordinateObject);
  auto * selectedUnit=new SoTextureUnit;selectedUnit->unit=unit;
  root->addChild(selectedUnit);root->addChild(texture(true));
  root->addChild(predecessor(0));
  auto * before=new SoCallback;before->setCallback(observe,&state);root->addChild(before);
  auto * inner=new SoSeparator;
  inner->addChild(successor(kind));
  auto * inside=new SoCallback;inside->setCallback(observe,&state);inner->addChild(inside);
  root->addChild(inner);
  auto * restored=new SoCallback;restored->setCallback(observe,&state);
  root->addChild(restored);
  SoOffscreenRenderer renderer{SbViewportRegion(side,side)};
  const bool rendered=renderer.render(root);
  const std::string label="separator unit="+std::to_string(unit)+
    " successor="+std::to_string(kind);
  bool ok=require(rendered && state.captures==3 && state.error==GL_NO_ERROR,
    label+" three GL observations");
  if(ok) {
    ok &= require(state.before[size_t(unit)]!=0 && state.after[size_t(unit)]==0 &&
      state.restored[size_t(unit)]==state.before[size_t(unit)],
      label+" selected hardware texgen restored");
    ok &= require(state.before[size_t(neighbor)]!=0 &&
      state.before[size_t(neighbor)]==state.after[size_t(neighbor)] &&
      state.after[size_t(neighbor)]==state.restored[size_t(neighbor)],
      label+" adjacent texgen unchanged by separator");
  }
  root->unref();
  return ok;
}
SoSeparator * geometryScene(int kind,bool hardware,bool software) {
  auto * root=new SoSeparator;root->ref();
  auto * camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,3);camera->height=2;
  camera->nearDistance=.1f;camera->farDistance=10;
  root->addChild(camera);
  auto * lighting=new SoLightModel;lighting->model=SoLightModel::BASE_COLOR;
  root->addChild(lighting);
  auto * quality=new SoComplexity;quality->textureQuality=.1f;
  root->addChild(quality);
  root->addChild(texture(true));
  if(hardware) {
    auto * generated=new SoTextureCoordinateObject;
    generated->factorS.setValue(0,0,0,0);
    generated->factorT.setValue(0,0,0,0);
    root->addChild(generated);
  }
  if(software) root->addChild(successor(kind));
  auto * cube=new SoCube;cube->width=1.4f;cube->height=1.4f;cube->depth=.5f;
  root->addChild(cube);
  return root;
}
bool geometrySoftware(int kind) {
  std::vector<uint8_t> images[3];
  const bool hardware[]={true,false,true};
  const bool software[]={true,true,false};
  for(int caseIndex=0;caseIndex<3;++caseIndex) {
    SoSeparator * scene=geometryScene(kind,hardware[caseIndex],software[caseIndex]);
    SoOffscreenRenderer renderer{SbViewportRegion(side,side)};
    renderer.setComponents(SoOffscreenRenderer::RGB);
    renderer.setBackgroundColor(SbColor(0,0,0));
    const bool rendered=renderer.render(scene);
    const auto * pixels=renderer.getBuffer();
    if(rendered && pixels) images[caseIndex].assign(pixels,pixels+size_t(side*side*3));
    scene->unref();
    if(!require(images[caseIndex].size()==size_t(side*side*3),
       "geometry software capture "+std::to_string(kind))) return false;
  }
  int maxSoftwareDelta=0;
  size_t hardwareDifference=0,nonbackground=0;
  for(size_t at=0;at<images[0].size();++at) {
    maxSoftwareDelta=std::max(maxSoftwareDelta,
      std::abs(int(images[0][at])-int(images[1][at])));
    hardwareDifference+=std::abs(int(images[1][at])-int(images[2][at]))>24;
    nonbackground+=images[1][at]>20;
  }
  std::cout << "texgen_transition geometry successor=" << kind
            << " software_max=" << maxSoftwareDelta
            << " hardware_difference_channels=" << hardwareDifference
            << " nonbackground_channels=" << nonbackground << '\n';
  return require(maxSoftwareDelta<=1 && hardwareDifference>100 && nonbackground>100,
    "software geometry must match with/without predecessor and differ from hardware-only control");
}
int nativeUnits() {
  State state;
  auto * root=new SoSeparator;root->ref();
  auto * camera=new SoOrthographicCamera;root->addChild(camera);
  auto * callback=new SoCallback;callback->setCallback(observe,&state);
  root->addChild(callback);
  SoOffscreenRenderer renderer{SbViewportRegion(4,4)};
  const bool rendered=renderer.render(root);
  root->unref();
  return rendered && state.error==GL_NO_ERROR ? state.maxUnits : 0;
}
}

int main(int argc,char ** argv) {
  if(argc!=1) return 2;
#ifndef _WIN32
  setenv("COIN_GLX_PIXMAP_DIRECT_RENDERING","1",0);
#endif
  SoDB::init();
  TextureFunctionProbe::initClass();
  const int available=nativeUnits();
  if(available==0) return 77;
  bool ok=true;
  for(int unit:{0,3,7}) for(int kind=0;kind<6;++kind) for(int initial=0;initial<5;++initial) {
    if(unit>=available || (unit==0 && available<=3)) {
      std::cout << "texgen_transition unit=" << unit << " native_unavailable max_units="
                << available << '\n';
      continue;
    }
    ok &= run(unit,kind,initial);
  }
  for(int unit:{0,3,7}) for(int kind=0;kind<6;++kind) {
    if(unit<available && (unit!=0 || available>3)) ok &= separatorRestore(unit,kind);
  }
  for(int kind=0;kind<4;++kind) ok &= geometrySoftware(kind);
  return ok?0:1;
}
