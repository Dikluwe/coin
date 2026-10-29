#include "CoinRenderPbrGalleryScenes.h"
#include <Inventor/SoInput.h>
#include <cstdio>
#ifdef COIN_PBR_IBL_TELEMETRY
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinbgfx/CoinBgfxBackend.h"
#endif
namespace {
void copyFile(const std::string & source,const std::string & destination) {
  std::ifstream input(source,std::ios::binary);std::ofstream output(destination,std::ios::binary);
  require(bool(input)&&bool(output),"Could not copy environment fixture");output<<input.rdbuf();
  require(bool(output),"Could not save environment fixture");
}
}
int main(int argc,char **argv) {
  try {
    require(argc==3,"Usage: coin_render_retained_ibl_gallery OUTPUT_DIRECTORY ENVIRONMENT.cibl");
    SoDB::init();CoinRenderAction::initClass();
    std::ofstream metadata(std::string(argv[1])+"/ibl.tsv");require(bool(metadata),"Cannot write IBL metadata");
    metadata<<"scene\tstate\twidth\theight\tcaptures\tplans\ttransport_builds\tenvironment_uploads\n";
    for(bool cad:{true}) {
      const std::string name="bracket";
      const std::string live=std::string(argv[1])+"/live-"+name+".cibl";copyFile(argv[2],live);
      CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;options.pbrEnvironmentBundle=live.c_str();
#ifdef COIN_PBR_IBL_PINNED_REFERENCE
      // Same isotropic profile; anisotropic source arrays are a separate feature.
      options.pbrAnisotropicEnvironmentSamples=0;
#endif
      std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(width,height),options));
      require(bool(target),"IBL target unavailable");
      SoInput input; require(input.openFile("/tmp/freecad-retained-ibl/bracket.iv"),"Cannot open FreeCAD mesh");
      auto *root=SoDB::readAll(&input);require(root,"Cannot read FreeCAD mesh");root->ref();
      auto *camera=static_cast<SoPerspectiveCamera *>(root->getChild(0));camera->pointAt(SbVec3f(0,0,14),SbVec3f(0,0,1));
#ifndef COIN_PBR_GALLERY_CURRENT
      CoinRenderRetainedRenderer renderer(SbViewportRegion(width,height));
      auto render=[&](){auto status=renderer.render(root,*target);
        if(status!=CoinRenderAction::SUCCESS)std::cerr<<renderer.getLastError().getString()<<'\n';
        require(status==CoinRenderAction::SUCCESS,"Retained IBL render failed");};
#else
      CoinRenderAction renderer(SbViewportRegion(width,height));renderer.setRenderTarget(target.get());
      auto render=[&](){renderer.apply(root);if(renderer.getLastStatus()!=CoinRenderAction::SUCCESS)
          std::cerr<<renderer.getLastError().getString()<<'\n';
        require(renderer.getLastStatus()==CoinRenderAction::SUCCESS,"Current IBL render failed");};
#endif
      std::vector<uint8_t> baseline,pixels;std::vector<float> baselineDepth,depth;
      const char *states[]={"base","rotated","dim","off","exposure","restored"};
      for(int i=0;i<6;++i) {
        const auto serial=target->getLastSubmissionSerial();
        require(target->setPbrEnvironmentParameters(i==1?1.57079632679f:0,i==2?.25f:i==3?0:1),"IBL scalar setter failed");
        require(target->setPbrExposureStops(i==4?2:0),"IBL exposure setter failed");
        require(target->getLastSubmissionSerial()==serial,"IBL setter published before rendering");
        render();target->readbackRGBA(pixels);target->readbackDepth(depth);
        require(pixels.size()==size_t(width*height*4)&&depth.size()==size_t(width*height),"Invalid IBL readback");
        bool coverage=false;for(float v:depth){require(std::isfinite(v)&&v>=0&&v<=1,"Invalid IBL depth");coverage|=v<1;}
        require(coverage,"Empty IBL geometry");
        if(i==0) {baseline=pixels;baselineDepth=depth;require(std::remove(live.c_str())==0,"Cannot remove loaded fixture");}
        else {require(depth==baselineDepth,"IBL scalar edits changed depth");require(i==5?pixels==baseline:pixels!=baseline,"IBL scalar edit/restore not visible");}
        if(i==3)for(size_t c=0;c<pixels.size();++c)if(c%4!=3)require(pixels[c]==0,"IBL off invented lighting");
        if(i<5) {
          const auto path=std::string(argv[1])+"/"+name+"-"+states[i];
          save(path+".rgba",pixels.data(),pixels.size());save(path+".depth",depth.data(),depth.size()*sizeof(float));
        }
        uint64_t uploads=0;
#ifdef COIN_PBR_IBL_TELEMETRY
        uploads=static_cast<CoinBgfxBackend *>(target->getPimpl()->backend.get())->getPbrEnvironmentUploadCount();
        require(uploads==1,"IBL scalar edit reuploaded environment");
#endif
#ifndef COIN_PBR_GALLERY_CURRENT
        const auto stats=renderer.getStatistics();
        require(stats.captures==1&&stats.planBuilds==1&&stats.transportBuilds==1,"IBL changed native capture/plan/transport");
        metadata<<name<<'\t'<<states[i]<<'\t'<<width<<'\t'<<height<<"\t1\t1\t1\t"<<uploads<<'\n';
#else
        metadata<<name<<'\t'<<states[i]<<'\t'<<width<<'\t'<<height<<"\t-\t-\t-\t"<<uploads<<'\n';
#endif
      }
      const auto serial=target->getLastSubmissionSerial();
      require(!target->setPbrEnvironmentParameters(std::numeric_limits<float>::quiet_NaN(),1),"NaN IBL rotation accepted");
      for(float invalid:{-1.f,65.f,std::numeric_limits<float>::infinity()})
        require(!target->setPbrEnvironmentParameters(0,invalid),"Invalid IBL intensity accepted");
      require(target->getOptions().pbrEnvironmentRotation==0&&target->getOptions().pbrEnvironmentIntensity==1&&
        target->getLastSubmissionSerial()==serial,"Invalid IBL setter changed state/publication");
      target->readbackRGBA(pixels);require(pixels==baseline,"Invalid IBL setter changed image");
#ifdef COIN_PBR_GALLERY_CURRENT
      renderer.setRenderTarget(nullptr);
#endif
      root->unref();
    }
    std::cout<<"IBL gallery: frozen source, rotation/intensity/exposure, reuse and invalid inputs PASS\n";return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
