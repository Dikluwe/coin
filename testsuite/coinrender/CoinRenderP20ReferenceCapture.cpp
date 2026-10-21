#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
// CPU rasterizer oracle, without a GPU context. Match the two historical P20
// camera contracts separately: orthographic window, fitted perspective offscreen.
int main(int argc,char**argv) {
  if(argc!=3) {std::cerr<<"Usage: CoinRenderP20ReferenceCapture FIXTURES OUTPUT_DIRECTORY\n";return 2;}
  SoDB::init();CoinRenderAction::initClass();
  for(const char* surface:{"window","offscreen"}) for(const char* scene:{"opaque-interleaved","transparent-overlap"}) {
    SoInput input;std::string path=std::string(argv[1])+"/"+scene+".iv";
    if(!input.openFile(path.c_str())) return 2;
    SoSeparator* imported=SoDB::readAll(&input);if(!imported)return 2;
    SoSeparator* root=new SoSeparator;root->ref();
    const bool window=std::string(surface)=="window";
    SoCamera* camera;
    if(window){auto* c=new SoOrthographicCamera;c->position.setValue(0,0,8);c->height=5.5f;c->nearDistance=.1f;c->farDistance=20;camera=c;}
    else {auto* c=new SoPerspectiveCamera;c->orientation.setValue(SbRotation(SbVec3f(0,0,-1),SbVec3f(-.5f,-.35f,-1)));camera=c;}
    root->addChild(camera);auto* light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);root->addChild(imported);
    if(!window)camera->viewAll(root,SbViewportRegion(128,128),1.15f);
    CoinRenderOptions options;options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
    CoinRenderSceneManager manager(SbVec2i32(128,128),options);
    manager.getRenderTarget()->getPimpl().get().backend.reset(new CoinRenderCpuReferenceBackend);
    manager.setSceneGraph(root);manager.setBackgroundColor(window?SbColor4f(0,0,0,1):SbColor4f(.1f,.1f,.1f,1));
    manager.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
    if(manager.render()!=CoinRenderAction::SUCCESS){std::cerr<<manager.getLastError().getString()<<'\n';return 1;}
    std::vector<uint8_t> pixels;manager.getRenderTarget()->readbackRGBA(pixels);if(pixels.size()!=128*128*4)return 1;
    std::string name=std::string(argv[2])+"/cpu-"+surface+"-"+scene;
    std::ofstream rgb(name+".ppm",std::ios::binary);rgb<<"P6\n128 128\n255\n";
    for(size_t i=0;i<pixels.size();i+=4)rgb.write(reinterpret_cast<const char*>(pixels.data()+i),3);
    std::ofstream rgba(name+".rgba",std::ios::binary);rgba.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
    rgb.flush();rgba.flush();if(!rgb||!rgba)return 1;
    std::cout<<surface<<' '<<scene<<" CPU canonical captured\n";root->unref();
  }
}
