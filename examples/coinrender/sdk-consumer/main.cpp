#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/system/gl.h>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include <fstream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

static void inspectGl(void *, SoAction * action) {
  if (!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
  GLint coords=0, samplers=0;
  glGetIntegerv(GL_MAX_TEXTURE_COORDS_ARB, &coords);
  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &samplers);
  std::cout << "CoinGL renderer=" << glGetString(GL_RENDERER)
            << " texture_coords=" << coords << " fragment_samplers=" << samplers << '\n';
}


static void savePixels(const std::string & path, const std::vector<uint8_t> & pixels, int side) {
  if (path.empty() || pixels.size()!=size_t(side*side*4)) return;
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << side << ' ' << side << "\n255\n";
  for(size_t i=0;i<pixels.size();i+=4) out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
}

static bool samplingOracle(CoinRenderOptions options, const std::string & prefix) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64),options));
  if(!target || target->getStatus()!=CoinRenderTarget::TARGET_READY) return false;
  auto* root=new SoSeparator; root->ref();
  auto* camera=new SoOrthographicCamera; camera->height=2; camera->position=SbVec3f(0,0,3); root->addChild(camera);
  auto* light=new SoLightModel; light->model=SoLightModel::BASE_COLOR; root->addChild(light);
  auto* quality=new SoComplexity; quality->textureQuality=.5f; root->addChild(quality);
  auto* texture=new SoTexture2; texture->model=SoTexture2::REPLACE;
  std::vector<unsigned char> bytes(128*128*4,255);
  for(int y=0;y<128;++y)for(int x=0;x<128;++x)for(int c=0;c<3;++c)bytes[(y*128+x)*4+c]=x<64?40:160;
  texture->image.setValue(SbVec2s(128,128),4,bytes.data()); root->addChild(texture);
  auto* coords=new SoCoordinate3; const SbVec3f positions[]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
  coords->point.setValues(0,4,positions); root->addChild(coords);
  auto* uv=new SoTextureCoordinate2; const SbVec2f st[]={{-1.46875f,.53125f},{2.53125f,.53125f},{2.53125f,.53125f},{-1.46875f,.53125f}};
  uv->point.setValues(0,4,st); root->addChild(uv);
  auto* face=new SoIndexedFaceSet; const int32_t indices[]={0,1,2,3,-1};face->coordIndex.setValues(0,5,indices);root->addChild(face);
  CoinRenderAction action(SbViewportRegion(64,64)); action.setRenderTarget(target.get());action.apply(root);
  std::vector<uint8_t> pixels;target->readbackRGBA(pixels);
  bool ok=action.getLastStatus()==CoinRenderAction::SUCCESS && pixels.size()==64*64*4;
  int maximum=0;
  if(ok && options.textureSamplingPolicy==COIN_RENDER_SAMPLING_PORTABLE) {
    for(int y=8;y<56;++y)for(int x:{30,31})for(int c=0;c<3;++c)
      maximum=std::max(maximum,std::abs(int(pixels[(y*64+x)*4+c])-(x==30?40:160)));
    ok=maximum<=1;
  }
  if(!prefix.empty())savePixels(prefix+"-mip.ppm",pixels,64);
  std::cout << "sdk_sampling policy=" << options.textureSamplingPolicy << " serial=" << target->getLastSubmissionSerial()
            << " floor_oracle=" << (options.textureSamplingPolicy==COIN_RENDER_SAMPLING_PORTABLE)
            << " maximumRGB=" << maximum << " result=" << ok << '\n';
  if(!ok)std::cerr << action.getLastError().getString() << '\n';
  action.setRenderTarget(nullptr);root->unref();return ok;
}

int main(int argc, char ** argv) {
  std::cout << std::unitbuf;
#ifdef _WIN32
  for(const char* name:{"Coin4.dll","CoinRender4.dll"}) {
    const HMODULE module=GetModuleHandleA(name);char path[32768]{};
    if(!module || !GetModuleFileNameA(module,path,sizeof(path)))return 6;
    std::cout << "sdk_loaded_module name=" << name << " path=" << path << '\n';
  }
#endif
  if (argc < 2 || argc > 4) {
    std::cerr << "Usage: coin-render-sdk-consumer d3d12|vulkan|opengl [native|portable] [pixel-prefix]\n"; return 2;
  }
  CoinRenderRenderer renderer;
  if (!std::strcmp(argv[1], "d3d12")) renderer=COIN_RENDER_RENDERER_D3D12;
  else if (!std::strcmp(argv[1], "vulkan")) renderer=COIN_RENDER_RENDERER_VULKAN;
  else if (!std::strcmp(argv[1], "opengl")) renderer=COIN_RENDER_RENDERER_OPENGL;
  else return 2;
  SoDB::init(); CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  if (coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
      renderer, &caps, sizeof(caps)) != 0 || !caps.gpu_available || caps.renderer != renderer) {
    std::cerr << "Requested renderer unavailable: " << caps.diagnostic << '\n'; return 3;
  }
  CoinRenderOptions options; options.renderer=renderer;
  if(argc>=3) {
    if(!std::strcmp(argv[2],"portable")) options.textureSamplingPolicy=COIN_RENDER_SAMPLING_PORTABLE;
    else if(std::strcmp(argv[2],"native")) return 2;
  }
  const std::string prefix=argc==4 ? argv[3] : "";
  const auto selection=coin_render_select_sampling_policy(&caps,options.textureSamplingPolicy,0);
  const auto strict=coin_render_select_sampling_policy(&caps,options.textureSamplingPolicy,1);
  std::cout << "sdk_policy=" << options.textureSamplingPolicy << " available_selection=" << selection.reason
            << " qualified_selection=" << strict.reason << '\n';
  if(selection.reason!=COIN_RENDER_SELECTION_SUPPORTED)return 3;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(32,32), options));
  if (!target || target->getStatus()!=CoinRenderTarget::TARGET_READY) return 4;
  auto * root=new SoSeparator; root->ref();
  auto * camera=new SoOrthographicCamera; camera->position.setValue(0,0,4); camera->height=3;
  root->addChild(camera);
  auto * model=new SoLightModel; model->model=SoLightModel::BASE_COLOR; root->addChild(model);
  auto * material=new SoMaterial; material->diffuseColor.setValue(1,0,0); root->addChild(material);
  auto * callback=new SoCallback; callback->setCallback(inspectGl); root->addChild(callback);
  root->addChild(new SoCube);
  CoinRenderAction action(SbViewportRegion(32,32)); action.setRenderTarget(target.get()); action.apply(root);
  std::vector<uint8_t> pixels; target->readbackRGBA(pixels);
  bool ok=action.getLastStatus()==CoinRenderAction::SUCCESS && pixels.size()==32*32*4;
  const size_t center=(16*32+16)*4;
  if (ok) ok=pixels[center]>200 && pixels[center+1]<5 && pixels[center+2]<5;
  SoOffscreenRenderer legacy(SbViewportRegion(32,32));
  legacy.setComponents(SoOffscreenRenderer::RGB);
  const bool referenceReady=legacy.render(root) && legacy.getBuffer();
  ok=referenceReady && ok;
  int maximum=0;
  if (ok) {
    for (int y=0; y<32; ++y) for (int x=0; x<32; ++x) for (int c=0; c<3; ++c) {
      const size_t a=(size_t(y)*32+x)*4+c;
      const size_t b=(size_t(31-y)*32+x)*3+c;
      maximum=std::max(maximum,std::abs(int(pixels[a])-int(legacy.getBuffer()[b])));
    }
    ok=maximum<=3;
  }
  std::cout << "sdk_CoinGL_rgb_max=" << maximum << " reference_ready=" << referenceReady << '\n';
  std::cout << "installed_sdk renderer=" << caps.renderer << " vendor_id=0x" << std::hex
            << caps.vendor_id << " device_id=0x" << caps.device_id << std::dec
            << " adapter=" << caps.adapter_name << " pixels_ok=" << ok << '\n';
  if(!prefix.empty())savePixels(prefix+"-cube.ppm",pixels,32);
  action.setRenderTarget(nullptr);
  root->unref();
  ok=samplingOracle(options,prefix) && ok;
  return ok ? 0 : 5;
}
