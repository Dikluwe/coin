#ifndef COIN_RENDER_COVERAGE_RTT_FIXTURES_H
#define COIN_RENDER_COVERAGE_RTT_FIXTURES_H
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <vector>
namespace CoinRenderCoverageRttFixtures {
inline void quad(SoSeparator* root,float left,float bottom,float right,float top,bool texture=false){
  auto* coords=new SoCoordinate3;const SbVec3f points[]={SbVec3f(left,bottom,0),SbVec3f(right,bottom,0),SbVec3f(right,top,0),SbVec3f(left,top,0)};
  coords->point.setValues(0,4,points);root->addChild(coords);
  if(texture){auto* uv=new SoTextureCoordinate2;const SbVec2f values[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};uv->point.setValues(0,4,values);root->addChild(uv);}
  auto* face=new SoFaceSet;face->numVertices=4;root->addChild(face);
}
inline SoSeparator* baseScene(){auto* root=new SoSeparator;root->ref();auto* camera=new SoOrthographicCamera;camera->height=2;camera->position.setValue(0,0,4);root->addChild(camera);auto* light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);return root;}
inline SoSeparator* reference(){
  // Authored expected child image, independent of all executor readbacks.
  std::vector<unsigned char> pixels(16*16*4,0);
  for(int y=0;y<16;++y)for(int x=0;x<16;++x){size_t i=(y*16+x)*4;pixels[i+3]=255;if(x>=5&&x<=12&&y>=5&&y<=12)pixels[i]=pixels[i+1]=pixels[i+2]=204;}
  auto* root=baseScene();auto* texture=new SoTexture2;texture->image.setValue(SbVec2s(16,16),4,pixels.data());texture->wrapS=SoTexture2::CLAMP;texture->wrapT=SoTexture2::CLAMP;texture->model=SoTexture2::REPLACE;root->addChild(texture);quad(root,-1,-1,1,1,true);return root;
}
inline SoSeparator* scene(){
  auto* child=baseScene();quad(child,-.4375f,-.4375f,float(12.50177127/8-1),.5625f);
  auto* root=baseScene();auto* texture=new SoSceneTexture2;texture->size.setValue(16,16);texture->backgroundColor.setValue(0,0,0,1);texture->scene=child;child->unref();
  texture->wrapS=SoSceneTexture2::CLAMP;texture->wrapT=SoSceneTexture2::CLAMP;texture->model=SoSceneTexture2::REPLACE;root->addChild(texture);quad(root,-1,-1,1,1,true);return root;
}
}
#endif
