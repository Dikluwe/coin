#include <Inventor/SoDB.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

static_assert(__cplusplus==201103L,"installed CoinRender consumer must actually compile as C++11");

#if !defined(COIN_RENDER_ACTION_OPTIONS_VERSION) || COIN_RENDER_ACTION_OPTIONS_VERSION < 1 || \
    !defined(COIN_RENDER_LEGACY_IO_VERSION) || COIN_RENDER_LEGACY_IO_VERSION < 1
int main(){std::cout<<"new portable API absent from installed baseline headers\n";return 78;}
#else
namespace {
bool good=true;
bool check(bool value,const char *message) {if(!value){good=false;std::cerr<<"installed_consumer FAIL "<<message<<'\n';}return value;}
struct Directory {
  std::string path;std::vector<std::string> files;
  Directory(){char pattern[]="/dev/shm/coin-installed-consumer-XXXXXX";char *value=mkdtemp(pattern);if(value)path=value;}
  std::string file(const std::string &name){files.push_back(path+"/"+name);return files.back();}
  ~Directory(){for(std::size_t i=0;i<files.size();++i)std::remove(files[i].c_str());if(!path.empty())rmdir(path.c_str());}
};
std::vector<uint8_t> read(const std::string &filename){std::ifstream input(filename.c_str(),std::ios::binary);return std::vector<uint8_t>(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());}
std::vector<uint8_t> authored(int width,int height,int phase) {
  std::vector<uint8_t> result(std::size_t(width)*height*4);
  for(int y=0;y<height;++y)for(int x=0;x<width;++x){const std::size_t p=(y*width+x)*4;
    result[p]=uint8_t(17+(x*13+y*3+phase*19)%211);result[p+1]=uint8_t(11+(x*7+y*17+phase*23)%223);
    result[p+2]=uint8_t(3+(x*19+y*11+phase*29)%239);result[p+3]=uint8_t(31+(x*5+y*9+phase*13)%211);}
  return result;
}
std::vector<uint8_t> converted(const std::vector<uint8_t> &source,int n) {
  std::vector<uint8_t> result(source.size()/4*n);
  for(std::size_t p=0;p<source.size()/4;++p) {
    if(n<3){result[p*n]=uint8_t(.30*double(source[p*4])+.59*double(source[p*4+1])+.11*double(source[p*4+2]));if(n==2)result[p*n+1]=source[p*4+3];}
    else for(int c=0;c<n;++c)result[p*n+c]=source[p*4+c];
  }
  return result;
}
}
int main() {
  SoDB::init();CoinRenderAction::initClass();CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))!=0||!caps.gpu_available) {
    std::cout<<"installed consumer GPU unavailable\n";return 77;
  }
  Directory directory;if(!check(!directory.path.empty(),"RAM artifacts directory"))return 1;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(13,9)));
  if(!check(bool(target),"public target factory"))return 1;
  CoinRenderAction action(SbViewportRegion(13,9));action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(.125f,.25f,.5f,1));
  SoSeparator *root=new SoSeparator;root->ref();
  SoOrthographicCamera *camera=new SoOrthographicCamera;camera->position.setValue(0,0,3);camera->height=2;
  camera->nearDistance=1;camera->farDistance=5;camera->viewportMapping=SoCamera::LEAVE_ALONE;root->addChild(camera);
  SoLightModel *light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
  action.apply(root);std::vector<uint8_t> background;target->readbackRGBA(background);
  check(action.getLastStatus()==CoinRenderAction::SUCCESS&&background.size()==13*9*4,"public background-only render");
  bool backgroundCorrect=background.size()==13*9*4;
  if(backgroundCorrect)for(std::size_t p=0;p<background.size();p+=4)
    backgroundCorrect &= std::abs(int(background[p])-32)<=1&&std::abs(int(background[p+1])-64)<=1&&std::abs(int(background[p+2])-128)<=1&&background[p+3]==255;
  check(backgroundCorrect,"public authored background reaches all canvas pixels");
  SoImage *image=new SoImage;image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;root->addChild(image);
  for(int step=0;step<3;++step) {
    const int width=step==1?17:13,height=step==1?11:9,phase=step==1?1:0;
    if(step)check(target->resize(SbVec2i32(width,height))!=FALSE,"public resize");
    action.setViewportRegion(SbViewportRegion(width,height));const std::vector<uint8_t> source=authored(width,height,phase);
    image->image.setValue(SbVec2s(width,height),4,source.data());
    if(step==1){SoPath *path=new SoPath(root);path->ref();path->append(2);action.apply(path);path->unref();}
    else action.apply(root);
    if(!check(action.getLastStatus()==CoinRenderAction::SUCCESS,"public node/path render"))break;
    std::vector<uint8_t> rgba;target->readbackRGBA(rgba);bool same=rgba.size()==source.size();
    if(same)for(int y=0;y<height;++y)for(int x=0;x<width*4;++x)same &= rgba[(height-1-y)*width*4+x]==source[y*width*4+x];
    check(same,"author pixels survive resize and path rendering");
    for(int n=1;n<=4;++n) {
      const CoinRenderLegacyComponents layout=static_cast<CoinRenderLegacyComponents>(n);
      const std::vector<uint8_t> expected=converted(source,n);std::size_t count=0,repeated=0;
      const uint8_t *borrow=target->borrowLegacyComponents(layout,count);
      check(borrow&&count==expected.size()&&std::equal(expected.begin(),expected.end(),borrow),"public exact four layouts");
      check(target->borrowLegacyComponents(layout,repeated)==borrow&&repeated==count,"public repeated borrow stable");
      const std::string prefix=std::to_string(step)+"-"+std::to_string(n);
      std::vector<uint8_t> encodedRGB,encodedPS;const SbVec2f page(8.5f,11.f);
      check(target->encodeLegacyRGB(layout,encodedRGB)&&target->encodeLegacyPostScript(layout,page,72,encodedPS),"public encoders");
      const std::string rgb=directory.file(prefix+".rgb"),eps=directory.file(prefix+".eps");
      check(target->writeLegacyRGB(rgb.c_str(),layout)&&read(rgb)==encodedRGB,"public owned RGB export");
      check(target->writeLegacyPostScript(eps.c_str(),layout,page,72)&&read(eps)==encodedPS,"public owned EPS export");
      const std::string borrowed=directory.file(prefix+"-FILE.rgb");FILE *file=std::fopen(borrowed.c_str(),"wb");
      if(check(file!=nullptr,"public caller-owned stream")) {
        check(target->writeLegacyRGB(file,layout)&&std::fflush(file)==0,"public FILE export");
        check(std::fputc(0x42,file)!=EOF,"public FILE retains caller ownership");check(std::fclose(file)==0,"caller closes stream");
        std::vector<uint8_t> expectedFile=encodedRGB;expectedFile.push_back(0x42);check(read(borrowed)==expectedFile,"public FILE exact bytes and suffix");
      }
    }
  }
  action.setRenderTarget(nullptr);root->unref();return good?0:1;
}
#endif
