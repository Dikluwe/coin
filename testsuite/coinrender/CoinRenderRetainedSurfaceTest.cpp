#include "config.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <vector>
static void check(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
int main(int argc,char **argv){try {
  check(argc>=4,"Usage: SCENE ENV OUTPUT [--current]");
  SoDB::init();CoinRenderAction::initClass();SoInput input;check(input.openFile(argv[1]),"scene");
  auto *root=SoDB::readAll(&input);check(root,"scene parse");root->ref();
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;options.pbrEnvironmentBundle=argv[2];
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(192,160),options));check(bool(target),"target");
  CoinRenderRetainedRenderer retained(SbViewportRegion(192,160));CoinRenderAction current(SbViewportRegion(192,160));
  const bool control=argc>4;current.setRenderTarget(control?target.get():nullptr);
  auto *material=static_cast<SoPBRMaterial *>(SoNode::getByName("Surface"));check(material,"named material");
  auto *move=static_cast<SoTranslation *>(SoNode::getByName("Move"));check(move,"named translation");
  std::vector<float> originalDepth;std::vector<uint8_t> previous;
  const auto render=[&](int step,bool uniformOnly) {
    if(control){current.apply(root);check(current.getLastStatus()==CoinRenderAction::SUCCESS,current.getLastError().getString());}
    else {const auto status=retained.render(root,*target);check(status==CoinRenderAction::SUCCESS,retained.getLastError().getString());}
    std::vector<uint8_t> rgba;std::vector<float> depth;target->readbackRGBA(rgba);target->readbackDepth(depth);
    check(rgba.size()==192*160*4&&depth.size()==192*160,"pixels");
    if(step==0)originalDepth=depth;
    if(uniformOnly)check(depth==originalDepth,"material edit changed depth");
    if(step==1)check(rgba==previous,"static pixels changed");
    if(step>1&&step<7)check(rgba!=previous,"material factor had no shading effect");
    const auto prefix=std::string(argv[3])+"-"+std::to_string(step);
    std::ofstream color(prefix+".rgba",std::ios::binary);color.write(reinterpret_cast<const char *>(rgba.data()),rgba.size());
    std::ofstream z(prefix+".depth",std::ios::binary);z.write(reinterpret_cast<const char *>(depth.data()),depth.size()*sizeof(float));
    const auto stats=retained.getStatistics();
    if(!control&&step<8)check(stats.captures==1&&stats.planBuilds==1&&stats.transportBuilds==1,"uniform update recaptured/replanned/rebuilt transport");
    std::cout<<step<<" captures="<<stats.captures<<" plans="<<stats.planBuilds<<" transports="<<stats.transportBuilds<<" copied="<<stats.copiedVertices<<'\n';
    previous=std::move(rgba);
  };
  render(0,false);render(1,true);
  material->baseColor.setValue(.35f,.8f,.55f);render(2,true);
  material->roughness=.25f;render(3,true);
  material->emissive.setValue(.1f,.02f,.01f);render(4,true);
  material->normalScale=0;render(5,true);
  material->normalScale=1.5f;render(6,true);
  material->normalYFlip=TRUE;render(7,true);
  move->translation.setValue(.3f,0,0);render(8,false);
  auto *texture=static_cast<SoTexture2 *>(SoNode::getByName("BaseMap"));check(texture,"base image");
  SbVec2s size;int nc;const auto *pixels=texture->image.getValue(size,nc);std::vector<unsigned char> changed(pixels,pixels+size[0]*size[1]*nc);
  for(size_t i=0;i<changed.size();i+=nc)changed[i]=255-changed[i];texture->image.setValue(size,nc,changed.data());render(9,false);
  if(!control) {
    const auto serial=target->getLastSubmissionSerial();std::vector<uint8_t> before,after;target->readbackRGBA(before);
    const auto scale=material->normalScale.getValue();material->normalScale=std::numeric_limits<float>::quiet_NaN();
    check(retained.render(root,*target)==CoinRenderAction::UNSUPPORTED,"invalid normal factor admitted");
    target->readbackRGBA(after);check(after==before&&target->getLastSubmissionSerial()==serial,"invalid factor published");
    material->normalScale=scale;check(retained.render(root,*target)==CoinRenderAction::SUCCESS,"factor retry");
    target->readbackRGBA(after);check(after==before,"factor retry changed pixels");
    const auto nextSerial=target->getLastSubmissionSerial();material->ormTextureUnit=7;
    check(retained.render(root,*target)==CoinRenderAction::UNSUPPORTED,"unresolved texture role admitted");
    target->readbackRGBA(after);check(after==before&&target->getLastSubmissionSerial()==nextSerial,"missing texture published");
    material->ormTextureUnit=1;check(retained.render(root,*target)==CoinRenderAction::SUCCESS,"texture retry");
    auto * legacy=new SoSeparator;legacy->ref();auto * bump=new SoBumpMap;
    const unsigned char normal[3]={128,128,255};bump->image.setValue(SbVec2s(1,1),3,normal);
    legacy->addChild(bump);legacy->addChild(new SoCube);
    const auto legacySerial=target->getLastSubmissionSerial();
    check(retained.render(legacy,*target)==CoinRenderAction::UNSUPPORTED,"legacy bump silently ignored");
    check(target->getLastSubmissionSerial()==legacySerial,"legacy bump rejection published");legacy->unref();

    target->readbackRGBA(after);check(after==before,"texture retry changed pixels");
  }
  root->unref();current.setRenderTarget(nullptr);return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
