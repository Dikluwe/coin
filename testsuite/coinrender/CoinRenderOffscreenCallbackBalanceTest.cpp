#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {
constexpr int width=37,height=29;
bool require(bool good,const std::string & label) {
  if(!good) std::cerr << "offscreen_callback_balance FAIL " << label << '\n';
  return good;
}
struct Diagnostics {
  SoErrorCB * previous=SoDebugError::getHandlerCallback();
  void * previousData=SoDebugError::getHandlerData();
  int missingRemoval=0;
  int unexpectedErrors=0;
  Diagnostics() {SoDebugError::setHandlerCallback(capture,this);}
  ~Diagnostics() {SoDebugError::setHandlerCallback(previous,previousData);}
  static void capture(const SoError * error,void * data) {
    auto * self=static_cast<Diagnostics *>(data);
    const char * message=error->getDebugString().getString();
    if(std::strstr(message,"removePreRenderCallback")) ++self->missingRemoval;
    const auto * debug=static_cast<const SoDebugError *>(error);
    if(debug->getSeverity()==SoDebugError::ERROR) ++self->unexpectedErrors;
    std::cerr << "offscreen_callback_balance Coin diagnostic: " << message << '\n';
  }
};
void userPreRender(void * context,SoGLRenderAction *) {
  ++*static_cast<int *>(context);
}
struct MultiApplyAction : SoGLRenderAction {
  explicit MultiApplyAction(const SbViewportRegion & viewport):SoGLRenderAction(viewport) {}
  SoNode * overlay=nullptr;
  void apply(SoNode * root) override {
    SoGLRenderAction::apply(root);
    if(overlay) SoGLRenderAction::apply(overlay);
  }
};
SoSeparator * rectangle(float left,float right,const SbColor & color,float transparency) {
  auto * root=new SoSeparator;
  auto * camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,3);camera->height=2;
  camera->nearDistance=.1f;camera->farDistance=10;
  root->addChild(camera);
  auto * lighting=new SoLightModel;
  lighting->model=SoLightModel::BASE_COLOR;
  root->addChild(lighting);
  auto * material=new SoMaterial;
  material->diffuseColor.setValue(color);
  material->transparency=transparency;
  root->addChild(material);
  auto * points=new SoCoordinate3;
  const SbVec3f vertices[]={SbVec3f(left,-.75f,0),SbVec3f(right,-.75f,0),
    SbVec3f(right,.75f,0),SbVec3f(left,.75f,0)};
  points->point.setValues(0,4,vertices);root->addChild(points);
  auto * face=new SoFaceSet;face->numVertices.set1Value(0,4);root->addChild(face);
  return root;
}
bool sample(const SoOffscreenRenderer & renderer,int x,int y,
            int red,int green,int blue,const std::string & label) {
  const auto * image=renderer.getBuffer();
  if(!require(image!=nullptr,label+" readback")) return false;
  const size_t at=size_t(y*width+x)*3;
  const int actual[]={image[at],image[at+1],image[at+2]};
  const int expected[]={red,green,blue};
  bool pass=true;
  for(int channel=0;channel<3;++channel)
    pass &= std::abs(actual[channel]-expected[channel])<=4;
  if(!pass) std::cerr << label << " x=" << x << " y=" << y << " RGB="
    << actual[0] << ',' << actual[1] << ',' << actual[2] << " expected="
    << red << ',' << green << ',' << blue << '\n';
  return require(pass,label+" pixel");
}
bool verify(const SoOffscreenRenderer & renderer,bool overlay,const std::string & label) {
  bool ok=true;
  ok &= sample(renderer,3,3,0,0,0,label+" corner-bottom-left");
  ok &= sample(renderer,33,25,0,0,0,label+" corner-top-right");
  ok &= sample(renderer,8,14,255,0,0,label+" base-left");
  ok &= sample(renderer,28,14,0,overlay?128:0,0,label+" overlay-right");
  return ok;
}
}

int main(int argc,char ** argv) {
  const bool tiled=argc==2 && std::string(argv[1])=="--tiled";
  if(argc>2 || (argc==2 && !tiled)) return 2;
#ifndef _WIN32
  setenv("COIN_GLX_PIXMAP_DIRECT_RENDERING","1",0);
#endif
  SoDB::init();
  Diagnostics diagnostics;
  SoSeparator * base=rectangle(-.85f,-.15f,SbColor(1,0,0),0);
  SoSeparator * overlay=rectangle(.15f,.85f,SbColor(0,1,0),.5f);
  base->ref();overlay->ref();
  const SbViewportRegion viewport(width,height);
  MultiApplyAction multi(viewport);
  multi.overlay=overlay;
  multi.setTransparencyType(SoGLRenderAction::BLEND);
  int firstCallback=0,secondCallback=0;
  multi.addPreRenderCallback(userPreRender,&firstCallback);
  bool ok=true;
  {
    SoOffscreenRenderer renderer(viewport);
    renderer.setGLRenderAction(&multi);
    renderer.setComponents(SoOffscreenRenderer::RGB);
    renderer.setBackgroundColor(SbColor(0,0,0));
    if(!renderer.render(base)) {
      std::cerr << "offscreen_callback_balance GL context unavailable\n";
      base->unref();overlay->unref();
      return 77;
    }
    ok &= verify(renderer,true,"first-two-applies");
    const int afterFirst=firstCallback;
    ok &= require(afterFirst>=(tiled?4:2),"user callback ran for both applies and tiles");
    ok &= require(renderer.render(base),"successive render");
    ok &= verify(renderer,true,"successive-two-applies");
    ok &= require(firstCallback==afterFirst*2,"user callback survived successive render");

    SoPath * path=new SoPath(base);path->ref();
    multi.overlay=nullptr;
    ok &= require(renderer.render(path),"SoPath render");
    ok &= verify(renderer,false,"path-no-overlay");
    path->unref();

    SoGLRenderAction replacement(viewport);
    replacement.setTransparencyType(SoGLRenderAction::BLEND);
    replacement.addPreRenderCallback(userPreRender,&secondCallback);
    renderer.setGLRenderAction(&replacement);
    const int oldCount=firstCallback;
    ok &= require(renderer.render(base),"external replacement action render");
    ok &= verify(renderer,false,"replacement-action");
    ok &= require(firstCallback==oldCount && secondCallback>0,
      "callback registration stays with its own action");
    replacement.removePreRenderCallback(userPreRender,&secondCallback);
    renderer.setGLRenderAction(&multi);
  }
  multi.removePreRenderCallback(userPreRender,&firstCallback);
  ok &= require(diagnostics.missingRemoval==0 && diagnostics.unexpectedErrors==0,
    "no unbalanced callback diagnostic or other GL error");
  std::cout << "offscreen_callback_balance tiled=" << tiled
            << " first_callback=" << firstCallback
            << " replacement_callback=" << secondCallback
            << " missing_removal=" << diagnostics.missingRemoval << '\n';
  base->unref();overlay->unref();
  return ok?0:1;
}
