// Public API consumer shared with the pinned codex/pbr-clean control build.
#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#ifdef COIN_RETAINED_SURFACE_REFERENCE
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#endif
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
int main(int argc,char **argv) {
  if(argc<4 || argc>6)return 1;
  SoDB::init();CoinRenderAction::initClass();SoInput input;if(!input.openFile(argv[1]))return 2;
  auto *root=SoDB::readAll(&input);if(!root)return 3;root->ref();
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;if(std::string(argv[2])!="-")options.pbrEnvironmentBundle=argv[2];
  if(argc>=5)options.pbrAnisotropicEnvironmentSamples=std::stoul(argv[4]);
  if(argc==6)options.pbrAnisotropyProducts=argv[5];
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(192,160),options));
  CoinRenderAction action(SbViewportRegion(192,160));
#ifdef COIN_RETAINED_SURFACE_REFERENCE
  CoinRenderRetainedRenderer retained(SbViewportRegion(192,160));
  const auto status=retained.render(root,*target);
  if(status!=CoinRenderAction::SUCCESS){std::cerr<<retained.getLastError().getString()<<'\n';return 4;}
#else
  action.setRenderTarget(target.get());action.apply(root);
#endif
#ifndef COIN_RETAINED_SURFACE_REFERENCE
  if(action.getLastStatus()!=CoinRenderAction::SUCCESS){std::cerr<<action.getLastError().getString()<<'\n';return 4;}
#endif
  std::vector<uint8_t> rgba;std::vector<float> depth;target->readbackRGBA(rgba);target->readbackDepth(depth);
  std::ofstream color(std::string(argv[3])+".rgba",std::ios::binary);color.write(reinterpret_cast<const char *>(rgba.data()),rgba.size());
  std::ofstream z(std::string(argv[3])+".depth",std::ios::binary);z.write(reinterpret_cast<const char *>(depth.data()),depth.size()*sizeof(float));
  action.setRenderTarget(nullptr);root->unref();return 0;
}
