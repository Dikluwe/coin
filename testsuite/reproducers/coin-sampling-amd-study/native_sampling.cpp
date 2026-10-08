// Pure Coin/OpenGL reproducer: no CoinRender headers, targets or executors.
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include "raw_gl.h"
#include "egl_context.h"
#include <Inventor/elements/SoGLCacheContextElement.h>
static void callback(void *data, SoAction *action) {
  if (!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
  auto &p=*static_cast<Probe*>(data);
  if (p.route=="coin") inspect(p);
  else drawRaw(p);
}
static SoSeparator *scene(Probe &p) {
  auto *root=new SoSeparator; root->ref();
  if (p.route=="coin") {
    auto *camera=new SoOrthographicCamera;camera->height=2;camera->position.setValue(0,0,4);root->addChild(camera);
    auto *light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
    auto *quality=new SoComplexity;quality->textureQuality=p.quality;root->addChild(quality);
    auto *material=new SoMaterial;material->diffuseColor.setValue(.7f,.5f,.3f);material->transparency=.2f;root->addChild(material);
    auto *texture=new SoTexture2;texture->model=SoTexture2::REPLACE;
    texture->wrapS=texture->wrapT=SoTexture2::REPEAT;auto bytes=mip(0);
    texture->image.setValue(SbVec2s(128,128),3,bytes.data());root->addChild(texture);
    auto *uv=new SoTextureCoordinate2;const SbVec2f st[]={{.03f,.02f},{.97f,.02f},{.97f,.98f},{.03f,.98f}};
    uv->point.setValues(0,4,st);root->addChild(uv);
    auto *matrix=new SoTextureMatrixTransform;SbMatrix m=SbMatrix::identity();m[0][3]=.3f;m[3][0]=m[3][1]=p.offset;
    matrix->matrix=m;root->addChild(matrix);
    auto *coords=new SoCoordinate3;const SbVec3f xy[]={{-.25f,-.25f,0},{.25f,-.25f,0},{.25f,.25f,0},{-.25f,.25f,0}};
    coords->point.setValues(0,4,xy);root->addChild(coords);
    auto *faces=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,3,-1};faces->coordIndex.setValues(0,5,indices);root->addChild(faces);
  }
  auto *node=new SoCallback;node->setCallback(callback,&p);root->addChild(node);return root;
}
int main(int argc,char **argv) {
  if(argc!=4 && argc!=5) { std::cerr<<"Usage: coin-native-sampling OUTPUT_PREFIX QUALITY OFFSET [--egl-current]\n";return 2; }
  const bool currentEgl=argc==5 && std::string(argv[4])=="--egl-current";
  EglContext context;
  if(currentEgl && !context.init())return 77;
  SoDB::init();std::cout<<std::setprecision(9);
  bool ok=true;
  for (const char *route : {"coin","fixed","implicit","explicit-lod","fetch","fetch-round256","query-lod","boundary-scan","boundary-fetch"}) {
    Probe p{std::stof(argv[2]),std::stof(argv[3]),route,argv[1],true};
    auto *root=scene(p);
    if(currentEgl) {
      glViewport(0,0,64,64);glClearColor(.1f,.1f,.1f,1);glClearDepth(1);
      glDepthMask(GL_TRUE);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
      SoGLRenderAction action(SbViewportRegion(64,64));
      action.setCacheContext(SoGLCacheContextElement::getUniqueCacheContext());
      action.setTransparencyType(SoGLRenderAction::BLEND);action.apply(root);
      std::vector<uint8_t> bottom(64*64*3),top(bottom.size());
      glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,64,64,GL_RGB,GL_UNSIGNED_BYTE,bottom.data());
      for(int y=0;y<64;++y)std::copy(bottom.begin()+(63-y)*64*3,bottom.begin()+(64-y)*64*3,top.begin()+y*64*3);
      std::ofstream out(p.output+"-"+p.route+".rgb",std::ios::binary);
      out.write(reinterpret_cast<const char*>(top.data()),top.size());p.ok &= bool(out) && glGetError()==GL_NO_ERROR;
    } else {
      SoOffscreenRenderer renderer(SbViewportRegion(64,64));renderer.setComponents(SoOffscreenRenderer::RGB);
      renderer.setBackgroundColor(SbColor(.1f,.1f,.1f));renderer.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
      if(!renderer.render(root)||!renderer.getBuffer()) {std::cerr<<route<<" unavailable\n";p.ok=false;}
      else {
        std::vector<uint8_t> bytes(64*64*3);const auto *source=renderer.getBuffer();
        for(int y=0;y<64;++y)std::copy(source+(63-y)*64*3,source+(64-y)*64*3,bytes.begin()+y*64*3);
        std::ofstream out(p.output+"-"+p.route+".rgb",std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());p.ok &= bool(out);
      }
    }
    root->unref();ok &= p.ok;
  }
  return ok?0:1;
}
