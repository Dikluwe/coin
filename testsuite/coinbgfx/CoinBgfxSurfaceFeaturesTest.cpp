#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinBgfxAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/nodes/SoTextureCoordinatePlane.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTexture2Transform.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoEnvironment.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using Pixel = std::array<int,4>;
bool check(bool result, const char * message) {
  if (!result) std::cerr << "CoinBgfxSurfaceFeaturesTest: " << message << '\n';
  return result;
}
SoSeparator * scene(int topology, int units, SoTexture2::Model model,
                    int alpha = 255, bool fog = false, bool sparse = false,
                    bool varying = false) {
  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0,0,5); camera->height = 2;
  camera->nearDistance = 0.1f; camera->farDistance = 20;
  root->addChild(camera);
  auto * light = new SoLightModel; light->model = SoLightModel::BASE_COLOR;
  root->addChild(light);
  auto * material = new SoMaterial; material->diffuseColor.setValue(1,1,1);
  root->addChild(material);
  if (fog) {
    auto * env = new SoEnvironment; env->fogType = SoEnvironment::HAZE;
    env->fogVisibility = 10; env->fogColor.setValue(0,0,1);
    root->addChild(env);
  }
  for (int i = sparse ? units-1 : 0; i < units; ++i) {
    auto * unit = new SoTextureUnit; unit->unit = i; root->addChild(unit);
    auto * texture = new SoTexture2;
    texture->model = i ? model : SoTexture2::MODULATE;
    texture->blendColor.setValue(0.25f,0.5f,0.75f);
    texture->wrapS = i % 2 ? SoTexture2::CLAMP : SoTexture2::REPEAT;
    unsigned char image[64];
    for (int p=0; p<16; ++p) {
      image[p*4] = varying ? (p%4<2 ? 255 : 0) : (i ? 255 : 128);
      image[p*4+1] = varying ? (p%4<2 ? 0 : 255) : (i ? 128 : 255);
      image[p*4+2] = varying ? 0 : 255;
      image[p*4+3] = static_cast<unsigned char>(alpha);
    }
    texture->image.setValue(SbVec2s(4,4),4,image); root->addChild(texture);
    auto * uv = new SoTextureCoordinate2;
    const SbVec2f values[] = {SbVec2f(0.125f,0.5f),SbVec2f(0.875f,0.5f),
                             SbVec2f(0.875f,0.5f),SbVec2f(0.125f,0.5f)};
    uv->point.setValues(0,4,values); root->addChild(uv);
    auto * matrix = new SoTexture2Transform;
    matrix->translation.setValue(0,0.125f); root->addChild(matrix);
  }
  auto * style = new SoDrawStyle; style->lineWidth = 7; style->pointSize = 9;
  root->addChild(style);
  auto * coords = new SoCoordinate3;
  const SbVec3f quad[] = {SbVec3f(-0.75f,-0.75f,0),SbVec3f(0.75f,-0.75f,0),
                         SbVec3f(0.75f,0.75f,0),SbVec3f(-0.75f,0.75f,0)};
  const SbVec3f line[] = {SbVec3f(-0.75f,0,0),SbVec3f(0.75f,0,0)};
  coords->point.setValues(0,topology ? 2 : 4,topology ? line : quad);
  root->addChild(coords);
  if (!topology) {
    auto * face = new SoIndexedFaceSet;
    const int32_t indices[] = {0,1,2,3,-1};
    face->coordIndex.setValues(0,5,indices);
    face->textureCoordIndex.setValues(0,5,indices); root->addChild(face);
  } else if (topology == 1) {
    auto * lines = new SoIndexedLineSet;
    const int32_t indices[] = {0,1,-1};
    lines->coordIndex.setValues(0,3,indices);
    lines->textureCoordIndex.setValues(0,3,indices); root->addChild(lines);
  } else {
    auto * points = new SoPointSet; points->numPoints = 2; root->addChild(points);
  }
  return root;
}
bool render(SoSeparator * root, bool cpu, std::vector<uint8_t> & pixels, bool fast = true) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
  if (!target) return false;
  target->setDepthReadbackEnabled(FALSE);
  if (cpu) target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
  CoinBgfxAction action(SbViewportRegion(64,64));
  action.setFastPathEnabled(fast); action.setRenderTarget(target.get());
  action.setTransparencyType(CoinBgfxAction::SORTED_LAYERS_BLEND);
  action.apply(root);
  if (action.getLastStatus() != CoinBgfxAction::SUCCESS) {
    std::cerr << action.getLastError().getString() << '\n'; return false;
  }
  target->readbackRGBA(pixels);
  return pixels.size() == 64*64*4;
}
Pixel pixel(const std::vector<uint8_t> & data, int x, int y) {
  Pixel value;
  for (int c=0;c<4;++c) value[c]=data[(y*64+x)*4+c];
  return value;
}
bool compare(SoSeparator * root, int x, int y, Pixel * result = nullptr) {
  std::vector<uint8_t> cpu,gpu,fallback;
  if (!render(root,true,cpu) || !render(root,false,gpu) || !render(root,true,fallback,false)) return false;
  Pixel a=pixel(cpu,x,y), b=pixel(gpu,x,y), c=pixel(fallback,x,y);
  for (int ch=0;ch<4;++ch) if (std::abs(a[ch]-b[ch])>4 || std::abs(a[ch]-c[ch])>1) {
    std::cerr << "channel " << ch << " CPU=" << a[ch] << " BGFX=" << b[ch]
              << " fallback=" << c[ch] << '\n'; return false;
  }
  if (result) *result=a;
  if (std::getenv("COIN_WGPU_REQUIRE_GL_REFERENCE")) {
    SoOffscreenRenderer gl(SbViewportRegion(64,64)); gl.setComponents(SoOffscreenRenderer::RGB);
    gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
    if (!gl.render(root) || !gl.getBuffer()) return false;
    for (int ch=0;ch<3;++ch) if (std::abs(a[ch]-gl.getBuffer()[(y*64+x)*3+ch])>5) {
      std::cerr << "GL reference mismatch " << ch << " CPU=" << a[ch]
                << " GL=" << int(gl.getBuffer()[(y*64+x)*3+ch]) << '\n'; return false;
    }
  }
  return true;
}
}
int main() {
  SoDB::init(); CoinBgfxAction::initClass();
  CoinBgfxAction action(SbViewportRegion(64,64));
  if (!check(action.getTypeId().getName() == SbName("CoinBgfxAction") &&
             action.isOfType(CoinRenderAction::getClassTypeId()), "registered BGFX action type")) return 1;
  for (int topology=0;topology<3;++topology) {
    for (int units : {1,2,8}) for (bool fog : {false,true}) {
      auto * root=scene(topology,units,SoTexture2::MODULATE,255,fog);
      Pixel sample;
      const bool ok=compare(root,topology==2?8:32,32,&sample);
      root->unref();
      if (!check(ok,"texture cascade / textured stroke / fog")) return 1;
      if (!check(sample[2]>20,"missing primitive or texture color")) return 1;
    }
  }
  for (int topology=1;topology<3;++topology) {
    auto * root=scene(topology,2,SoTexture2::MODULATE,128,true);
    const bool ok=compare(root,topology==2?8:32,32); root->unref();
    if (!check(ok,"textured line/point alpha and fog")) return 1;
  }
  for (auto model : {SoTexture2::MODULATE,SoTexture2::REPLACE,SoTexture2::DECAL,SoTexture2::BLEND}) {
    for (int alpha : {255,128}) {
      auto * root=scene(0,2,model,alpha,true);
      Pixel actual;
      const bool ok=compare(root,32,32,&actual); root->unref();
      const float a = alpha / 255.0f, half = 128.0f/255.0f;
      float red=half, green=1, blue=1, opacity=a;
      if (model==SoTexture2::MODULATE) { green*=half; opacity*=a; }
      else if (model==SoTexture2::REPLACE) { red=1; green=half; opacity=a; }
      else if (model==SoTexture2::DECAL) { red=half*(1-a)+a; green=1*(1-a)+half*a; }
      else { red=0.25f; green=1*(1-half)+0.5f*half; blue=0.75f; opacity*=a; }
      Pixel expected={{int(red*0.5f*opacity*255+0.5f),int(green*0.5f*opacity*255+0.5f),int((blue*0.5f+0.5f)*opacity*255+0.5f),255}};
      for (int ch=0;ch<4;++ch) if (!check(std::abs(actual[ch]-expected[ch])<=3,"independent analytical texture/fog/alpha oracle")) return 1;
      if (!check(ok,"environment model and texture alpha")) return 1;
    }
  }
  for (int topology=0;topology<3;++topology) {
    auto * root=scene(topology,8,SoTexture2::REPLACE,255,false,true,true);
    Pixel left,right;
    const bool ok=compare(root,8,32,&left) && compare(root,55,32,&right);
    root->unref();
    if (!check(ok && left[0]>220 && right[1]>220,"unit 7 independent UVs with unit 0 disabled")) return 1;
  }
  {
    auto * root=scene(1,2,SoTexture2::MODULATE,255,false,false,true);
    auto * camera=new SoPerspectiveCamera;
    camera->position.setValue(0,0,3); camera->heightAngle=0.785398163f;
    camera->nearDistance=0.1f; camera->farDistance=20; root->replaceChild(0,camera);
    auto * coords=static_cast<SoCoordinate3 *>(root->getChild(root->getNumChildren()-2));
    coords->point.set1Value(0,SbVec3f(-1.25f,0,1));
    coords->point.set1Value(1,SbVec3f(2.5f,0,-1));
    Pixel sample;
    const bool ok=compare(root,32,32,&sample); root->unref();
    if (!check(ok && sample[0]>230 && sample[1]<20,"perspective-correct textured line UV")) return 1;
  }
  {
    auto * root=scene(1,2,SoTexture2::MODULATE,128,true);
    auto * style=static_cast<SoDrawStyle *>(root->getChild(root->getNumChildren()-3));
    style->linePattern=0x0f0f;
    std::vector<uint8_t> cpu,gpu;
    const bool ok=render(root,true,cpu) && render(root,false,gpu);
    root->unref();
    if (!check(ok,"textured stipple rendering")) return 1;
    int painted=0,clear=0;
    for (int x=9;x<55;++x) {
      const Pixel a=pixel(cpu,x,32), b=pixel(gpu,x,32);
      for (int ch=0;ch<4;++ch) if (!check(std::abs(a[ch]-b[ch])<=4,"stipple UV/alpha/fog mismatch")) return 1;
      if (b[2]) ++painted; else ++clear;
    }
    if (!check(painted>8 && clear>8,"stipple pattern discarded")) return 1;
  }
  {
    auto * root=scene(0,8,SoTexture2::REPLACE,255,false,true);
    auto * face=static_cast<SoIndexedFaceSet *>(root->getChild(root->getNumChildren()-1));
    face->textureCoordIndex.set1Value(0,9999);
    action.apply(root); root->unref();
    if (!check(action.getLastStatus()==CoinBgfxAction::INVALID_SCENE,"invalid higher-unit UV index")) return 1;
  }
  {
    auto * root=scene(0,2,SoTexture2::MODULATE);
    root->insertChild(new SoTextureCombine,root->getNumChildren()-1);
    action.apply(root); root->unref();
    if (!check(action.getLastStatus()==CoinBgfxAction::UNSUPPORTED,"custom combine must not be silently ignored")) return 1;
  }
  {
    auto * root=scene(0,2,SoTexture2::REPLACE,255,false,false,true);
    auto * uv=static_cast<SoTextureCoordinate2 *>(root->getChild(9));
    for (int i=0;i<4;++i) uv->point.set1Value(i,SbVec2f(0.125f,0.5f));
    auto * matrix=static_cast<SoTexture2Transform *>(root->getChild(10));
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64)));
    target->setDepthReadbackEnabled(FALSE);
    CoinBgfxAction cached(SbViewportRegion(64,64)); cached.setRenderTarget(target.get());
    for (int pass=0;pass<3;++pass) {
      if (pass==1) matrix->translation.setValue(0.75f,0);
      if (pass==2) {
        auto * texture=static_cast<SoTexture2 *>(root->getChild(8));
        unsigned char red[64];
        for (int p=0;p<16;++p) { red[p*4]=255; red[p*4+1]=0; red[p*4+2]=0; red[p*4+3]=255; }
        texture->image.setValue(SbVec2s(4,4),4,red);
      }
      cached.apply(root);
      std::vector<uint8_t> pixels; target->readbackRGBA(pixels);
      if (!check(cached.getLastStatus()==CoinBgfxAction::SUCCESS && pixels.size()==64*64*4,"cached multitexture rendering")) return 1;
      Pixel actual=pixel(pixels,32,32);
      if (!check(pass==1 ? actual[1]>250 && actual[0]<5 : actual[0]>250 && actual[1]<5,
                 "higher-unit UV/matrix/image mutation retained stale resources")) return 1;
    }
    root->unref();
  }
  {
    auto * root=scene(0,8,SoTexture2::REPLACE,255,false,true);
    root->replaceChild(5,new SoTextureCoordinatePlane);
    action.apply(root); root->unref();
    if (!check(action.getLastStatus()==CoinBgfxAction::UNSUPPORTED,"procedural higher unit rejected before primitive generation")) return 1;
  }
  auto * overflow=scene(0,9,SoTexture2::MODULATE);
  action.apply(overflow); overflow->unref();
  if (!check(action.getLastStatus()==CoinBgfxAction::UNSUPPORTED,"unit limit explicit rejection")) return 1;
  std::cout << "CoinBgfxSurfaceFeaturesTest passed\n";
}
