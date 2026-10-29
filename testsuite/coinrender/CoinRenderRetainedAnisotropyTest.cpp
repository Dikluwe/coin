// Independent update/publication controls; no production capture or planning here.
#include "config.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/actions/CoinRenderAction.h>
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>
static void check(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
int main(int argc,char **argv) {try {
  check(argc==4||argc==5,"Usage: SCENE ENV OUTPUT [--current]");
  SoDB::init();CoinRenderAction::initClass();SoInput input;check(input.openFile(argv[1]),"scene");
  auto *root=SoDB::readAll(&input);check(root,"parse");root->ref();
  const bool current=argc==5;
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;options.pbrEnvironmentBundle=argv[2];
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(192,160),options));check(bool(target),"target");
  CoinRenderRetainedRenderer retained{SbViewportRegion(192,160)};
  CoinRenderAction callback{SbViewportRegion(192,160)};if(current)callback.setRenderTarget(target.get());
  auto *material=static_cast<SoPBRMaterial *>(SoNode::getByName("Surface"));
  auto *move=static_cast<SoTranslation *>(SoNode::getByName("Move"));check(material&&move,"fixture nodes");
  const auto submit=[&]() {
    if(current){callback.apply(root);return callback.getLastStatus();}
    return retained.render(root,*target);
  };
  std::vector<uint8_t> previous;std::vector<float> initialDepth;uint64_t copied=0;
  const auto render=[&](int step,bool changed) {
    check(submit()==CoinRenderAction::SUCCESS,current?callback.getLastError().getString():retained.getLastError().getString());
    std::vector<uint8_t> color;std::vector<float> depth;target->readbackRGBA(color);target->readbackDepth(depth);
    if(!step){initialDepth=depth;copied=retained.getStatistics().copiedVertices;}
    if(step<8)check(depth==initialDepth,"anisotropy changed depth");
    if(step)check((color!=previous)==changed,"anisotropy edit did not change expected pixels");
    if(!current) {
      const auto stats=retained.getStatistics();const uint64_t expected=1;
      check(stats.captures==expected&&stats.planBuilds==expected&&stats.transportBuilds==expected,"anisotropy repeated capture/plan/transport");
      check(stats.copiedVertices==copied,"uniform anisotropy recopied vertices");
      std::cout<<step<<" captures="<<stats.captures<<" plans="<<stats.planBuilds<<" transports="<<stats.transportBuilds<<" copied="<<stats.copiedVertices<<'\n';
    }
    const auto prefix=std::string(argv[3])+"-"+std::to_string(step);
    std::ofstream c(prefix+".rgba",std::ios::binary);c.write(reinterpret_cast<const char *>(color.data()),color.size());
    std::ofstream z(prefix+".depth",std::ios::binary);z.write(reinterpret_cast<const char *>(depth.data()),depth.size()*sizeof(float));
    previous=std::move(color);
  };
  render(0,false);render(1,false);
  material->anisotropy=.35f;render(2,true);
  material->anisotropyRotation=1.57079632679f;render(3,true);
  material->normalScale=0;render(4,true);
  material->anisotropy=0;render(5,true);
  // The initially captured direction remains available after disabling the lobe.
  material->anisotropy=.8f;render(6,true);
  material->anisotropyRotation=.4f;render(7,true);
  move->translation.setValue(.3f,0,0);render(8,true);
  if(!current) {
    const auto reject=[&](const char *message) {
      const auto serial=target->getLastSubmissionSerial();
      check(submit()==CoinRenderAction::UNSUPPORTED,message);
      std::vector<uint8_t> after;target->readbackRGBA(after);
      check(after==previous&&target->getLastSubmissionSerial()==serial,"refusal replaced publication");
    };
    material->anisotropy=-.1f;reject("negative anisotropy admitted");
    material->anisotropy=.8f;check(submit()==CoinRenderAction::SUCCESS,"factor retry");
    material->anisotropyRotation=std::numeric_limits<float>::quiet_NaN();reject("nonfinite angle admitted");
    material->anisotropyRotation=.4f;check(submit()==CoinRenderAction::SUCCESS,"angle retry");
    auto & settings=target->getPimpl()->options;
    settings.pbrAnisotropicEnvironmentSamples=129;reject("unbounded source sampling admitted");settings.pbrAnisotropicEnvironmentSamples=16;
    settings.pbrAnisotropyCpuBudgetBytes=1;reject("unbudgeted lighting product admitted");settings.pbrAnisotropyCpuBudgetBytes=options.pbrAnisotropyCpuBudgetBytes;
    settings.pbrEnvironmentBudgetBytes=1;reject("unbudgeted GPU environment admitted");settings.pbrEnvironmentBudgetBytes=options.pbrEnvironmentBudgetBytes;
    check(submit()==CoinRenderAction::SUCCESS,"environment retry");
    std::vector<uint8_t> after;target->readbackRGBA(after);check(after==previous,"retry changed image");
    CoinRenderRetainedRenderer initiallyIsotropic{SbViewportRegion(192,160)};
    material->anisotropy=0;check(initiallyIsotropic.render(root,*target)==CoinRenderAction::SUCCESS,"initial isotropic render");
    material->anisotropy=.8f;check(initiallyIsotropic.render(root,*target)==CoinRenderAction::SUCCESS,"first tangent capture");
    const auto fresh=initiallyIsotropic.getStatistics();
    check(fresh.captures==2&&fresh.planBuilds==2&&fresh.transportBuilds==2,"missing tangent dependencies did not recapture exactly once");
    target->readbackRGBA(after);check(after==previous,"first anisotropy enable changed reference image");

  }
  callback.setRenderTarget(nullptr);root->unref();return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
