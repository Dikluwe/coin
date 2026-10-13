#include <Inventor/SoDB.h>
#include <Inventor/C/base/time.h>
#include <Inventor/SbString.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSphere.h>
#include <cstdio>
#include <cstring>
#include <vector> // Installed public readback interface.
static bool save(const SbString & filename,const std::vector<uint8_t> & pixels,int width,int height) {
  if(pixels.size()!=size_t(width)*height*4)return false;
  FILE * output=std::fopen(filename.getString(),"wb");if(!output)return false;
  bool ok=std::fprintf(output,"P6\n%d %d\n255\n",width,height)>0;
  for(size_t i=0;ok&&i<pixels.size();i+=4)ok=std::fwrite(pixels.data()+i,1,3,output)==3;
  return std::fclose(output)==0&&ok;
}
int main(int argc,char ** argv) {
  SbString environment,output;bool expectUnsupported=false;
  for(int i=1;i<argc;++i) {
    if(!std::strcmp(argv[i],"--environment")&&i+1<argc)environment=argv[++i];
    else if(!std::strcmp(argv[i],"--output")&&i+1<argc)output=argv[++i];
    else if(!std::strcmp(argv[i],"--expect-unsupported"))expectUnsupported=true;
    else return 2;
  }
  SoDB::init();CoinRenderAction::initClass();SoPBRMaterial::initClass();
  CoinRenderOptions options;options.pbrEnvironmentBundle=environment;
  if(!expectUnsupported)options.renderer=COIN_RENDER_RENDERER_VULKAN;
  CoinRenderSceneManager manager(SbVec2i32(160,160),options);
  auto * root=new SoSeparator;root->ref();auto * camera=new SoOrthographicCamera;camera->position=SbVec3f(0,0,4);camera->height=2.5f;camera->nearDistance=.1f;camera->farDistance=10;root->addChild(camera);
  auto * light=new SoDirectionalLight;light->direction=SbVec3f(-.3f,.4f,-1);light->intensity=environment.getLength()?0:1;root->addChild(light);
  auto * control=new SoMaterial;control->diffuseColor=SbColor(1,0,0);root->addChild(control);root->addChild(new SoSphere);manager.setSceneGraph(root);
  if(manager.render()!=CoinRenderAction::SUCCESS)return 1;
  std::vector<uint8_t> prior;manager.getRenderTarget()->readbackRGBA(prior);const auto serial=manager.getRenderTarget()->getLastSubmissionSerial();
  auto * material=new SoPBRMaterial;material->baseColor=SbColor(.7f,.3f,.1f);material->metallic=1;material->roughness=.3f;root->replaceChild(2,material);
  const cc_time begin=cc_time_gettimeofday();const auto status=manager.render();const double elapsed=(cc_time_gettimeofday()-begin)*1000;
  auto * target=manager.getRenderTarget();std::vector<uint8_t> pixels;target->readbackRGBA(pixels);
  if(expectUnsupported) {
    const bool ok=status==CoinRenderAction::UNSUPPORTED&&pixels==prior&&target->getLastSubmissionSerial()==serial;
    std::printf("public SDK PBR rejection=%s prior publication preserved=%d\n",ok?"PASS":"FAIL",ok);root->unref();return ok?0:1;
  }
  if(status!=CoinRenderAction::SUCCESS||pixels.size()!=160u*160u*4u) {
    std::fprintf(stderr,"public SDK render: %s\n",manager.getLastError().getString());root->unref();return 1;
  }
  if(output.getLength()&&!save(output,pixels,160,160))return 1;
  const auto first=pixels;if(manager.render()!=CoinRenderAction::SUCCESS)return 1;target->readbackRGBA(pixels);if(pixels!=first)return 1;
  if(!target->setPbrExposureStops(1)||!target->setPbrEnvironmentParameters(.25f,1)||manager.render()!=CoinRenderAction::SUCCESS)return 1;
  target->readbackRGBA(pixels);if(pixels==first)return 1;
  material->roughness=.7f;if(manager.render()!=CoinRenderAction::SUCCESS)return 1;
  if(!manager.resize(SbVec2i32(0,0))||manager.render()!=CoinRenderAction::NOT_READY)return 1;
  target->readbackRGBA(pixels);if(!pixels.empty())return 1;
  if(!manager.resize(SbVec2i32(192,128))||manager.render()!=CoinRenderAction::SUCCESS)return 1;
  target->readbackRGBA(pixels);if(pixels.size()!=192u*128u*4u)return 1;
  std::printf("public SDK PBR: native node, HDR environment, replay, exposure, factor edit and suspend/resize PASS; initial render+readback %.3f ms (not a GPU benchmark)\n",elapsed);
  root->unref();return 0;
}
