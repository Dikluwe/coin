#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderPortableApiFixture.h"
#if !defined(COIN_RENDER_LEGACY_IO_VERSION) || COIN_RENDER_LEGACY_IO_VERSION < 1
int main() {
  std::cout<<"CoinRender legacy consumer API contract absent (baseline headers)\n";
  return 78;
}
#else
#include "glue/simage_wrapper.h"
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/nodes/SoImage.h>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace portable_api;
namespace {
constexpr int width=17,height=13;
struct Directory {
  std::string path;
  std::vector<std::string> names;
  Directory() {
    char pattern[]="/dev/shm/coin-portable-consumer-XXXXXX";
    char *value=mkdtemp(pattern);if(value)path=value;
    check(!path.empty(),"unique RAM directory for file I/O");
  }
  std::string file(const std::string &name) {names.push_back(path+"/"+name);return names.back();}
  ~Directory(){for(const auto &name:names)std::remove(name.c_str());if(!path.empty())rmdir(path.c_str());}
};
std::vector<uint8_t> readFile(const std::string &path) {
  std::ifstream input(path,std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
}
std::vector<uint8_t> readStream(FILE *file) {
  if(!file||std::fflush(file)!=0||std::fseek(file,0,SEEK_END)!=0)return {};
  const long length=std::ftell(file);if(length<0||std::fseek(file,0,SEEK_SET)!=0)return {};
  std::vector<uint8_t> result(static_cast<size_t>(length));
  if(std::fread(result.data(),1,result.size(),file)!=result.size())return {};
  return result;
}
std::vector<uint8_t> authored(int phase) {
  std::vector<uint8_t> result(width*height*4);
  for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
    const size_t p=(y*width+x)*4;
    result[p]=uint8_t(19+(x*11+y*3+phase*37)%211);
    result[p+1]=uint8_t(13+(x*2+y*17+phase*29)%223);
    result[p+2]=uint8_t(7+(x*7+y*5+phase*43)%239);
    result[p+3]=uint8_t(31+(x*5+y*9+phase*17)%211);
  }
  return result;
}
std::vector<uint8_t> components(const std::vector<uint8_t> &rgba,int n) {
  std::vector<uint8_t> result(width*height*n);
  for(int p=0;p<width*height;++p) {
    if(n<3) {
      result[p*n]=uint8_t(.30*double(rgba[p*4])+.59*double(rgba[p*4+1])+.11*double(rgba[p*4+2]));
      if(n==2)result[p*n+1]=rgba[p*4+3];
    } else for(int c=0;c<n;++c)result[p*n+c]=rgba[p*4+c];
  }
  return result;
}
std::vector<uint8_t> topDown(const std::vector<uint8_t> &bottom) {
  std::vector<uint8_t> result(bottom.size());
  for(int y=0;y<height;++y)std::copy(bottom.begin()+y*width*4,bottom.begin()+(y+1)*width*4,
    result.begin()+(height-1-y)*width*4);
  return result;
}
struct Authored : Scene {
  SoImage *image=new SoImage;
  Authored(){image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;root->addChild(image);set(0);}
  void set(int phase){const auto bytes=authored(phase);image->image.setValue(SbVec2s(width,height),4,bytes.data());}
};
// SGI's fixed big-endian header and planar bottom-up uncompressed channels.
bool rgbStructure(const std::vector<uint8_t> &bytes,const std::vector<uint8_t> &source,int n) {
  if(!check(bytes.size()==512+source.size(),"SGI fixed header plus exact pixel size"))return false;
  const auto be16=[&](size_t offset){return unsigned(bytes[offset])*256+bytes[offset+1];};
  bool ok=check(be16(0)==474&&bytes[2]==0&&bytes[3]==1&&be16(6)==width&&be16(8)==height&&be16(10)==unsigned(n),
    "independent SGI magic/storage/sample width/dimensions/channel count");
  size_t bad=0;
  for(int c=0;c<n;++c)for(int p=0;p<width*height;++p)
    bad+=bytes[512+c*width*height+p]!=source[p*n+c];
  return check(bad==0,"SGI independent asymmetric planar channel/row oracle")&&ok;
}
std::string upper(std::string text) {for(auto &c:text)c=char(std::toupper(static_cast<unsigned char>(c)));return text;}
std::vector<std::string> split(const char *text) {
  std::vector<std::string> result;std::stringstream stream(text?text:"");std::string item;
  while(std::getline(stream,item,','))if(!item.empty())result.push_back(item);
  return result;
}
void discovery() {
  const auto *codec=simage_wrapper();
  const bool available=codec->available&&codec->versionMatchesAtLeast(1,1,0)&&
    codec->simage_get_num_savers&&codec->simage_get_saver_handle&&codec->simage_get_saver_extensions;
  const int native=available?codec->simage_get_num_savers():0;
  check(CoinRenderTarget::getNumLegacyImageFileTypes()==native,"discover actual saver count, including absent simage");
  for(int index:{-1,native,std::numeric_limits<int>::max()}) {
    std::vector<SbString> extensions(2,SbString("sentinel"));SbString fullname("sentinel"),description("sentinel");
    check(!CoinRenderTarget::getLegacyImageFileTypeInfo(index,extensions,fullname,description)&&
      extensions.empty()&&fullname.getLength()==0&&description.getLength()==0,
      "invalid saver index clears every output");
  }
  for(const char *invalid:{"",".png","coin-no-such-format","png,sgi"," png","png/../tiff"})
    check(!CoinRenderTarget::isLegacyImageFileSupported(SbString(invalid)),"invalid extension refuses deterministically");
  for(int i=0;i<native;++i) {
    auto *handle=codec->simage_get_saver_handle(i);
    auto expected=split(codec->simage_get_saver_extensions(handle));
    std::vector<SbString> extensions;SbString fullname,description;
    check(CoinRenderTarget::getLegacyImageFileTypeInfo(i,extensions,fullname,description),"installed saver info succeeds");
    std::vector<std::string> actual;for(const auto &extension:extensions)actual.emplace_back(extension.getString());
    check(actual==expected,"all native aliases in native order, no hardcoded families");
    if(codec->simage_get_saver_fullname)check(fullname==SbString(codec->simage_get_saver_fullname(handle)),"native saver fullname");
    if(codec->simage_get_saver_description)check(description==SbString(codec->simage_get_saver_description(handle)),"native saver description");
    for(const auto &extension:expected) {
      check(CoinRenderTarget::isLegacyImageFileSupported(SbString(extension.c_str())),"installed alias supported");
      check(CoinRenderTarget::isLegacyImageFileSupported(SbString(upper(extension).c_str())),"query aliases case insensitive");
    }
  }
}
struct FailingCookie {bool closed=false;unsigned writes=0;};
#if defined(__GLIBC__)
ssize_t failWrite(void *cookie,const char *,size_t) {
  ++static_cast<FailingCookie*>(cookie)->writes;errno=ENOSPC;return -1;
}
int cookieClose(void *cookie){static_cast<FailingCookie*>(cookie)->closed=true;return 0;}
#endif
void writes(CoinRenderTarget &target,Directory &dir,int phase,int n) {
  const auto layout=static_cast<CoinRenderLegacyComponents>(n);
  const auto source=components(authored(phase),n);
  std::vector<uint8_t> encodedRGB,encodedPS;
  const SbVec2f page(7.25f,10.5f);const float dpi=83;
  check(target.encodeLegacyRGB(layout,encodedRGB)&&target.encodeLegacyPostScript(layout,page,dpi,encodedPS),"existing encoders available");
  const auto stable=read(target);std::size_t borrowedSize=0;
  const auto *borrowed=target.borrowLegacyComponents(layout,borrowedSize);
  const std::vector<uint8_t> borrowedCopy=borrowed?std::vector<uint8_t>(borrowed,borrowed+borrowedSize):std::vector<uint8_t>();
  const auto prefix=std::to_string(phase)+"-"+std::to_string(n);
  const auto rgbName=dir.file(prefix+".rgb"),psName=dir.file(prefix+".eps");
  check(target.writeLegacyRGB(rgbName.c_str(),layout)&&readFile(rgbName)==encodedRGB,
    "owned RGB filename writes exact encoder bytes");
  rgbStructure(readFile(rgbName),source,n);
  check(target.writeLegacyPostScript(psName.c_str(),layout,page,dpi)&&readFile(psName)==encodedPS,
    "owned EPS filename writes exact encoder bytes");
  FILE *stream=std::fopen(dir.file(prefix+"-stream.rgb").c_str(),"w+b");
  if(check(stream!=nullptr,"borrowed RAM FILE open")) {
    check(std::fputc(0x33,stream)!=EOF,"borrowed FILE prefix");
    auto withPrefix=encodedRGB;withPrefix.insert(withPrefix.begin(),0x33);
    check(target.writeLegacyRGB(stream,layout)&&readStream(stream)==withPrefix,
      "borrowed RGB FILE remains open and matches independent SGI bytes");
    // Writing at the current position must not rewind or close a caller's stream.
    const long before=std::ftell(stream);check(std::fseek(stream,0,SEEK_END)==0,"seek RGB end");
    const long at=std::ftell(stream);check(before>=0&&at==long(encodedRGB.size()+1),"RGB stream length exact including caller prefix");
    check(std::fputc(0x5a,stream)!=EOF&&std::fflush(stream)==0,"caller owns FILE after RGB writer");
    std::fclose(stream);
  }
  stream=std::fopen(dir.file(prefix+"-stream.eps").c_str(),"w+b");
  if(check(stream!=nullptr,"borrowed EPS RAM FILE open")) {
    check(target.writeLegacyPostScript(stream,layout,page,dpi)&&readStream(stream)==encodedPS,
      "borrowed EPS FILE exact stream remains open");
    check(std::fputc('\n',stream)!=EOF,"caller owns FILE after EPS writer");std::fclose(stream);
  }
  check(!target.writeLegacyRGB(static_cast<FILE*>(nullptr),layout)&&
    !target.writeLegacyPostScript(static_cast<FILE*>(nullptr),layout,page,dpi),"NULL borrowed streams reject");
  check(!target.writeLegacyRGB(static_cast<const char*>(nullptr),layout)&&
    !target.writeLegacyPostScript(static_cast<const char*>(nullptr),layout,page,dpi),"NULL filenames reject");
  const std::vector<uint8_t> sentinel={17,83,199,0,42};
  const auto untouched=dir.file(prefix+"-untouched");
  {std::ofstream output(untouched,std::ios::binary);output.write(reinterpret_cast<const char*>(sentinel.data()),sentinel.size());}
  for(unsigned invalid:{0u,5u,255u}) {
    const auto invalidLayout=static_cast<CoinRenderLegacyComponents>(invalid);
    check(!target.writeLegacyRGB(untouched.c_str(),invalidLayout)&&readFile(untouched)==sentinel,
      "invalid RGB encoding cannot truncate existing destination");
    check(!target.writeLegacyPostScript(untouched.c_str(),invalidLayout,page,dpi)&&readFile(untouched)==sentinel,
      "invalid EPS encoding cannot truncate existing destination");
  }
  for(float invalid:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
    check(!target.writeLegacyPostScript(untouched.c_str(),layout,page,invalid)&&readFile(untouched)==sentinel,
      "invalid DPI rejects before opening output");
    check(!target.writeLegacyPostScript(untouched.c_str(),layout,SbVec2f(invalid,10),dpi)&&readFile(untouched)==sentinel,
      "invalid page rejects before opening output");
  }
  const auto missing=dir.file(prefix+"-never-created");
  check(!target.writeLegacyRGB(missing.c_str(),static_cast<CoinRenderLegacyComponents>(0)),"failed encode cannot create file");
  struct stat metadata{};check(stat(missing.c_str(),&metadata)!=0&&errno==ENOENT,"no file created before encoding succeeds");
  check(!target.writeLegacyRGB(dir.path.c_str(),layout)&&
    !target.writeLegacyPostScript((dir.path+"/missing/child").c_str(),layout,page,dpi),"unwritable destinations report I/O failure");
  stream=std::fopen(untouched.c_str(),"rb");
  if(check(stream!=nullptr,"read-only borrowed stream")) {
    check(!target.writeLegacyRGB(stream,layout)&&std::ferror(stream),"read-only RGB stream ferror propagated");
    check(std::fseek(stream,0,SEEK_SET)==0,"failed writer did not close borrowed stream");std::fclose(stream);
  }
#if defined(__GLIBC__)
  for(bool postscript:{false,true}) {
    FailingCookie cookie;cookie_io_functions_t functions{};functions.write=failWrite;functions.close=cookieClose;
    stream=fopencookie(&cookie,"w",functions);
    if(check(stream!=nullptr,"deterministic failing FILE cookie")) {
      std::setvbuf(stream,nullptr,_IONBF,0);
      const bool result=postscript?target.writeLegacyPostScript(stream,layout,page,dpi):target.writeLegacyRGB(stream,layout);
      check(!result&&cookie.writes>0&&!cookie.closed&&std::ferror(stream),"short/failed I/O reports failure and preserves caller FILE ownership");
      std::fclose(stream);check(cookie.closed,"caller closes failing stream exactly once");
    }
  }
#endif
  check(!target.writeLegacyImageFile(SbString(untouched.c_str()),SbString("absent-codec"),layout),"codec write failure");
  std::size_t afterSize=0;const auto *after=target.borrowLegacyComponents(layout,afterSize);
  check(after==borrowed&&afterSize==borrowedSize&&after&&std::equal(borrowedCopy.begin(),borrowedCopy.end(),after),
    "all successful exports and failed encoding/I/O preserve component pointer and bytes");
  unchanged(target,stable,"all writer success/failure paths");
}
bool nativeOracle(Authored &scene,CoinRenderTarget &target,Directory &dir) {
  for(int n=1;n<=4;++n) {
    const auto layout=static_cast<CoinRenderLegacyComponents>(n);
    SoOffscreenRenderer native(SbViewportRegion(width,height));
    native.setComponents(static_cast<SoOffscreenRenderer::Components>(n));native.setBackgroundColor(SbColor(0,0,0));
    native.getGLRenderAction()->setTransparencyType(SoGLRenderAction::NONE);
    if(!native.render(scene.root)) {std::cout<<"Native CoinGL context unavailable for independent oracle\n";return false;}
    std::size_t bytes=0;const auto *borrowed=target.borrowLegacyComponents(layout,bytes);
    const auto expected=components(authored(0),n);
    check(native.getBuffer()&&std::equal(expected.begin(),expected.end(),native.getBuffer())&&
      borrowed&&bytes==expected.size()&&std::equal(expected.begin(),expected.end(),borrowed),
      "asymmetric four-layout native CoinGL and independent author buffer agree");
    const auto filename=dir.file("native-"+std::to_string(n)+".rgb");FILE *file=std::fopen(filename.c_str(),"w+b");
    if(check(file!=nullptr,"native oracle RAM FILE")) {
      check(native.writeToRGB(file),"native SGI writer");const auto nativeBytes=readStream(file);std::fclose(file);
      const auto product=dir.file("product-"+std::to_string(n)+".rgb");
      check(target.writeLegacyRGB(product.c_str(),layout)&&readFile(product)==nativeBytes,"public SGI writer equals native CoinGL stream");
    }
  }
  return true;
}
}
int main(int argc,char **argv) {
  bool gpu=false,native=false;
  for(int i=1;i<argc;++i) {const std::string arg=argv[i];if(arg=="--cpu")gpu=false;else if(arg=="--gpu")gpu=true;else if(arg=="--native")native=true;else return 2;}
  SoDB::init();CoinRenderAction::initClass();discovery();Fixture f(gpu,width,height);
  if(!f.target)return gpu&&good()?77:1;
  Directory dir;if(dir.path.empty())return 1;
  Authored scene;
  for(int n=1;n<=4;++n) {std::size_t count=99;check(!f.target->borrowLegacyComponents(static_cast<CoinRenderLegacyComponents>(n),count)&&count==0,
    "no publication has no invented legacy buffer");}
  const auto missing=dir.file("unpublished.rgb");
  check(!f.target->writeLegacyRGB(missing.c_str(),CoinRenderLegacyComponents::RGB)&&readFile(missing).empty(),"unpublished writer refuses before creating destination");
  for(int phase:{0,1,0}) {
    scene.set(phase);const auto nodeId=scene.image->getNodeId();
    if(phase==1) {SoPath *path=new SoPath(scene.root);path->ref();path->append(2);f.action.apply(path);path->unref();
      check(f.action.getLastStatus()==CoinRenderAction::SUCCESS,"public SoPath render for consumer");}
    else f.apply(scene.root,"legacy consumer node render");
    const auto image=read(*f.target);check(image.rgba==topDown(authored(phase)),"independent asymmetric RGBA frame bytes");
    check(scene.image->getNodeId()==nodeId,"consumer leaves source image revision unchanged");
    for(int n=1;n<=4;++n) {
      const auto layout=static_cast<CoinRenderLegacyComponents>(n);std::size_t count=0,again=0;
      const auto expected=components(authored(phase),n);
      const uint8_t *pointer=f.target->borrowLegacyComponents(layout,count);
      check(pointer&&count==expected.size()&&std::equal(expected.begin(),expected.end(),pointer),"borrowed L/LA/RGB/RGBA exact bottom-up conversion");
      check(f.target->borrowLegacyComponents(layout,again)==pointer&&again==count,"repeated same-layout borrow pointer stable");
      writes(*f.target,dir,phase,n);
      f.action.setNumPasses(0);const auto before=read(*f.target);f.action.apply(scene.root);
      check(f.action.getLastStatus()!=CoinRenderAction::SUCCESS,"invalid render refuses");
      check(f.target->borrowLegacyComponents(layout,again)==pointer&&again==count&&
        std::equal(expected.begin(),expected.end(),pointer),"failed render preserves legacy borrow allocation and bytes");
      unchanged(*f.target,before,"failed consumer render");f.action.setNumPasses(1);
    }
    for(unsigned invalid:{0u,5u,255u}) {std::size_t count=999;
      check(!f.target->borrowLegacyComponents(static_cast<CoinRenderLegacyComponents>(invalid),count)&&count==0,"invalid legacy component domain returns null/zero");}
  }
  // Keep each layout active across all three publications. Cycling layouts
  // between frames would hide a cache that invalidates only on layout changes.
  for(int n=1;n<=4;++n) {
    const auto layout=static_cast<CoinRenderLegacyComponents>(n);
    for(int phase:{0,1,0}) {
      scene.set(phase);f.apply(scene.root,"same-layout publication A/B/A");
      const auto expected=components(authored(phase),n);std::size_t bytes=0,repeated=0;
      const auto *pointer=f.target->borrowLegacyComponents(layout,bytes);
      check(pointer&&bytes==expected.size()&&std::equal(expected.begin(),expected.end(),pointer),
        "unchanged active component layout observes new publication bytes");
      check(f.target->borrowLegacyComponents(layout,repeated)==pointer&&repeated==bytes,
        "same-publication repeated borrow remains stable within same-layout A/B/A");
    }
  }
  if(native&&!nativeOracle(scene,*f.target,dir))return good()?77:1;
  check(f.target->resize(SbVec2i32(width,height)),"consumer same-size resize");
  std::size_t count=99;check(!f.target->borrowLegacyComponents(CoinRenderLegacyComponents::RGB,count)&&count==0,"resize invalidates old legacy buffer");
  f.apply(scene.root,"consumer resize recovery");
  check(f.target->setDepthReadbackEnabled(FALSE),"consumer output policy changes");count=99;
  check(!f.target->borrowLegacyComponents(CoinRenderLegacyComponents::RGB,count)&&count==0,"output-policy change invalidates legacy buffer");
  f.apply(scene.root,"consumer policy recovery");
  check(f.target->borrowLegacyComponents(CoinRenderLegacyComponents::RGB,count)&&count==width*height*3,"legacy buffer recovers from policy invalidation");
  return good()?0:1;
}
#endif
