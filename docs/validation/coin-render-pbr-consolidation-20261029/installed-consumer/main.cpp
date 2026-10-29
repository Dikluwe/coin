#include <Inventor/SoDB.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/SbViewportRegion.h>
#include <cstdio>
#include <initializer_list>
int main() {
 SoDB::init();CoinRenderAction::initClass();
 auto * root=new SoSeparator;root->ref();
 auto * camera=new SoPerspectiveCamera;camera->position=SbVec3f(0,0,5);
 root->addChild(camera);root->addChild(new SoDirectionalLight);
 auto * material=new SoPBRMaterial;material->roughness=.3f;root->addChild(material);root->addChild(new SoCube);
 int result=0;
 for(auto mode:{COIN_RENDER_CAPTURE_CONVENTIONAL,COIN_RENDER_CAPTURE_RETAINED}) {
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;options.captureMode=mode;
  CoinRenderSceneManager manager(SbVec2i32(64,64),options);manager.setSceneGraph(root);
  if(manager.render()!=CoinRenderAction::SUCCESS){std::fprintf(stderr,"%s\n",manager.getLastError().getString());result=1;}
 }
 root->unref();return result;
}
