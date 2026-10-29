#include "CoinRenderPbrGalleryScenes.h"
int main(int argc,char ** argv) {
  try {
    require(argc==2,"Usage: coin_render_retained_pbr_gallery OUTPUT_DIRECTORY (existing directory)");
    SoDB::init();CoinRenderAction::initClass();
    CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(width,height),options));
    require(bool(target),"PBR target unavailable");
    std::ofstream metadata(std::string(argv[1])+"/gallery.tsv");
    require(bool(metadata),"Could not write gallery metadata");
    metadata<<"scene\texposure_stops\twidth\theight\tcaptures\tplans\ttransport_builds\n";
    for(bool cad:{false,true}) {
      auto * root=scene(cad);root->ref();
      const std::string name=cad?"cad":"materials";
#ifndef COIN_PBR_GALLERY_CURRENT
      CoinRenderRetainedRenderer renderer(SbViewportRegion(width,height));
      auto render=[&](){const auto status=renderer.render(root,*target);
        if(status!=CoinRenderAction::SUCCESS)std::cerr<<renderer.getLastError().getString()<<'\n';
        require(status==CoinRenderAction::SUCCESS,"Gallery rendering failed");};
#else
      CoinRenderAction renderer(SbViewportRegion(width,height));renderer.setRenderTarget(target.get());
      auto render=[&](){renderer.apply(root);if(renderer.getLastStatus()!=CoinRenderAction::SUCCESS)
          std::cerr<<renderer.getLastError().getString()<<'\n';
        require(renderer.getLastStatus()==CoinRenderAction::SUCCESS,"Gallery control rendering failed");};
#endif
      std::vector<uint8_t> baseline,pixels;std::vector<float> baselineDepth,depth;
      for(int stops:{0,-2,2,0}) {
        const auto serial=target->getLastSubmissionSerial();
        require(target->setPbrExposureStops(float(stops)),"Exposure setter failed");
        require(target->getLastSubmissionSerial()==serial,"Exposure setter published without render");
        render();target->readbackRGBA(pixels);target->readbackDepth(depth);
        require(pixels.size()==size_t(width*height*4)&&depth.size()==size_t(width*height),"Invalid gallery readback size");
        for(float value:depth)require(std::isfinite(value)&&value>=0&&value<=1,"Invalid gallery depth");
        if(baseline.empty()){baseline=pixels;baselineDepth=depth;}
        else {
          require(depth==baselineDepth,"Exposure changed scene depth");
          require(stops?pixels!=baseline:pixels==baseline,"Exposure failed to change/restore display output");
        }
        const std::string path=std::string(argv[1])+"/"+name+"-"+std::to_string(stops);
        save(path+".rgba",pixels.data(),pixels.size());save(path+".depth",depth.data(),depth.size()*sizeof(float));
#ifndef COIN_PBR_GALLERY_CURRENT
        const auto stats=renderer.getStatistics();
        require(stats.captures==1&&stats.planBuilds==1&&stats.transportBuilds==1,"Exposure rebuilt capture, plan or transport");
        metadata<<name<<'\t'<<stops<<'\t'<<width<<'\t'<<height<<'\t'<<stats.captures<<'\t'<<stats.planBuilds<<'\t'<<stats.transportBuilds<<'\n';
#else
        metadata<<name<<'\t'<<stops<<'\t'<<width<<'\t'<<height<<"\t-\t-\t-\n";
#endif
      }
      const auto serial=target->getLastSubmissionSerial();
      for(float invalid:{-17.f,17.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        require(!target->setPbrExposureStops(invalid),"Invalid exposure accepted");
      require(target->getOptions().pbrExposureStops==0&&target->getLastSubmissionSerial()==serial,"Invalid exposure changed configuration/publication");
      target->readbackRGBA(pixels);require(pixels==baseline,"Invalid exposure changed pixels");
#ifndef COIN_PBR_GALLERY_CURRENT
      require(renderer.getCaptureCount()==1&&renderer.getPlanBuildCount()==1,"Unexpected capture or plan");
#else
      renderer.setRenderTarget(nullptr);
#endif
      root->unref();
    }
    std::cout<<"PBR gallery: two 3D scenes, exposure -2/0/+2, restore and invalid inputs PASS\n";
    return 0;
  } catch(const std::exception & error) {std::cerr<<error.what()<<'\n';return 1;}
}
