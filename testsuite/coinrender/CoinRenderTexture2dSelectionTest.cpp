#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int side=64;
bool check(bool value,const std::string & label) {
  if(!value) std::cerr << "texture2d_selection FAIL " << label << '\n';
  return value;
}
class GeneralSwitch {
public:
  GeneralSwitch() {
    const char * prior=std::getenv("COIN_BGFX_DISABLE_TEXTURE2D_PROGRAM");
    present=prior!=nullptr;
    if(present) saved=prior;
  }
  ~GeneralSwitch() {set(present?saved.c_str():nullptr);}
  void disableTexture2d(bool disabled) {set(disabled?"1":nullptr);}
private:
  void set(const char * value) {
#ifdef _WIN32
    _putenv_s("COIN_BGFX_DISABLE_TEXTURE2D_PROGRAM",value?value:"");
#else
    if(value) setenv("COIN_BGFX_DISABLE_TEXTURE2D_PROGRAM",value,1);
    else unsetenv("COIN_BGFX_DISABLE_TEXTURE2D_PROGRAM");
#endif
  }
  bool present=false;
  std::string saved;
};
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoMaterial * material=new SoMaterial;
  SoAlphaTest * alphaTest=new SoAlphaTest;
  SoLightModel * model=new SoLightModel;
  SoTexture2 * textureNode=nullptr;
  SoComplexity * quality=nullptr;
  SoTextureMatrixTransform * textureMatrix=nullptr;
  SoTextureCoordinate2 * textureCoords=nullptr;
  Scene() {
    root->ref();
    auto * camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,3);camera->height=2;
    camera->nearDistance=.1f;camera->farDistance=10;root->addChild(camera);
    model->model=SoLightModel::BASE_COLOR;root->addChild(model);
    auto * light=new SoDirectionalLight;light->direction.setValue(-.2f,.1f,-1);
    root->addChild(light);
    material->diffuseColor.setValue(.8f,.3f,.2f);
    material->transparency=0;root->addChild(material);
    alphaTest->function=SoAlphaTest::ALWAYS;
    alphaTest->value=.5f;root->addChild(alphaTest);
    auto * coordinates=new SoCoordinate3;
    const SbVec3f corners[]={SbVec3f(-.8f,-.8f,0),SbVec3f(.8f,-.8f,0),
      SbVec3f(.8f,.8f,0),SbVec3f(-.8f,.8f,0)};
    coordinates->point.setValues(0,4,corners);root->addChild(coordinates);
    auto * face=new SoFaceSet;face->numVertices.set1Value(0,4);
    root->addChild(face);
    texture();
  }
  ~Scene(){root->unref();}
  void vertexAlpha(bool zeroFirst=false) {
    const SbColor colors[]={SbColor(.8f,.3f,.2f),SbColor(.3f,.8f,.2f),
      SbColor(.2f,.3f,.8f),SbColor(.7f,.6f,.2f)};
    const float alpha[]={zeroFirst?0.f:.25f,.5f,.75f,1.f};
    material->diffuseColor.setValues(0,4,colors);
    material->transparency.setValues(0,4,alpha);
    auto * binding=new SoMaterialBinding;
    binding->value=SoMaterialBinding::PER_VERTEX;
    root->insertChild(binding,root->findChild(material)+1);
  }
  void texture() {
    if(textureNode) return;
    quality=new SoComplexity;quality->textureQuality=.1f;
    root->insertChild(quality,root->findChild(material));
    textureNode=new SoTexture2;
    const unsigned char pixels[]={80,160,220,255,220,80,40,255,
      40,220,80,255,200,170,90,255};
    textureNode->image.setValue(SbVec2s(2,2),4,pixels);
    textureNode->model=SoTexture2::MODULATE;
    root->insertChild(textureNode,root->findChild(material));
    textureMatrix=new SoTextureMatrixTransform;
    root->insertChild(textureMatrix,root->findChild(material));
    textureCoords=new SoTextureCoordinate2;
    const SbVec2f uv[]={SbVec2f(.25f,.25f),SbVec2f(.75f,.25f),
      SbVec2f(.75f,.75f),SbVec2f(.25f,.75f)};
    textureCoords->point.setValues(0,4,uv);
    root->insertChild(textureCoords,root->findChild(material));
  }
  void mutateTexture() {
    const unsigned char pixels[]={17,210,65,255,230,35,180,255,
      165,85,245,255,45,160,210,255};
    textureNode->image.setValue(SbVec2s(2,2),4,pixels);
  }
  void q(float value) {
    SbMatrix matrix=SbMatrix::identity();matrix[3][3]=value;
    textureMatrix->matrix=matrix;
  }
  void extraUnit() {
    auto * unit=new SoTextureUnit;unit->unit=3;
    root->insertChild(unit,root->findChild(material));
    auto * extra=new SoTexture2;
    const unsigned char pixels[]={230,170,90,255};
    extra->image.setValue(SbVec2s(1,1),4,pixels);
    extra->model=SoTexture2::MODULATE;
    root->insertChild(extra,root->findChild(material));
    auto * resetUnit=new SoTextureUnit;resetUnit->unit=0;
    root->insertChild(resetUnit,root->findChild(material));
  }
  void fog() {
    auto * environment=new SoEnvironment;
    environment->fogType=SoEnvironment::HAZE;
    environment->fogColor.setValue(.12f,.35f,.6f);
    environment->fogVisibility=4;
    root->insertChild(environment,root->findChild(material));
  }
  void clip() {
    auto * plane=new SoClipPlane;
    plane->plane=SbPlane(SbVec3f(1,0,0),0);
    root->insertChild(plane,root->findChild(material));
  }
};
struct Frame {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial=0;
  int64_t applyMs=0;
};
struct Harness {
  CoinRenderOptions options{};
  std::unique_ptr<CoinRenderTarget> solid,general,cpu;
  CoinRenderAction solidAction{SbViewportRegion(side,side)};
  CoinRenderAction generalAction{SbViewportRegion(side,side)};
  CoinRenderAction cpuAction{SbViewportRegion(side,side)};
  GeneralSwitch switcher;
  explicit Harness(bool canonical,bool portable,CoinRenderRenderer renderer) {
    options.renderer=renderer;
    if(canonical) options.rasterizationPolicy=COIN_RENDER_RASTER_CANONICAL_8BIT;
    if(portable) options.textureSamplingPolicy=COIN_RENDER_SAMPLING_PORTABLE;
    solid.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    general.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    cpu.reset(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
    if(!solid||!general||!cpu) return;
    solid->setDepthReadbackEnabled(TRUE);
    general->setDepthReadbackEnabled(TRUE);
    cpu->setDepthReadbackEnabled(TRUE);
    cpu->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    solidAction.setRenderTarget(solid.get());
    generalAction.setRenderTarget(general.get());
    cpuAction.setRenderTarget(cpu.get());
    const SbColor4f background(.03f,.05f,.08f,1);
    solidAction.setBackgroundColor(background);
    generalAction.setBackgroundColor(background);
    cpuAction.setBackgroundColor(background);
  }
  ~Harness() {
    solidAction.setRenderTarget(nullptr);
    generalAction.setRenderTarget(nullptr);
    cpuAction.setRenderTarget(nullptr);
  }
  bool available() const {return solid&&general&&cpu;}
  bool capture(CoinRenderAction & action,CoinRenderTarget & target,SoNode * root,
               Frame & out,const std::string & label) {
    const auto begin=std::chrono::steady_clock::now();
    action.apply(root);
    out.applyMs=std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now()-begin).count();
    target.readbackRGBA(out.rgba);target.readbackDepth(out.depth);
    out.serial=target.getLastSubmissionSerial();
    const bool good=action.getLastStatus()==CoinRenderAction::SUCCESS &&
      out.rgba.size()==size_t(side*side*4) && out.depth.size()==size_t(side*side);
    if(!good) std::cerr << label << " status=" << int(action.getLastStatus())
                        << " error=" << action.getLastError().getString() << '\n';
    return check(good,label+" capture");
  }
  void mode(CoinRenderAction::TransparencyType type) {
    solidAction.setTransparencyType(type);
    generalAction.setTransparencyType(type);
    cpuAction.setTransparencyType(type);
  }
};
bool compare(const Frame & solid,const Frame & general,const Frame & cpu,
             const std::string & label,bool canonical,int expectVisible) {
  // This BLEND fixture writes depth. Its nearly transparent edge pixels can
  // round from background blue 20 to 21 without changing geometric coverage.
  const bool depthCoverage=label=="blend-vertex-alpha";
  size_t coverage=0,coverageMismatch=0,alphaMismatch=0;
  size_t cpuCoverageMismatch=0,cpuAlphaMismatch=0;
  size_t reportedCpuCoverage=0;
  size_t depthWritten[3]={0,0,0};
  int maxRgb=0,maxCpuRgb=0;
  float maxDepth=0,maxCpuDepth=0;
  for(size_t pixel=0;pixel<size_t(side*side);++pixel) {
    const size_t at=pixel*4;
    bool a=false,b=false,c=false;
    for(int channel=0;channel<3;++channel) {
      a|=solid.rgba[at+channel]>20;
      b|=general.rgba[at+channel]>20;
      c|=cpu.rgba[at+channel]>20;
      maxRgb=std::max(maxRgb,std::abs(int(solid.rgba[at+channel])-
        int(general.rgba[at+channel])));
      maxCpuRgb=std::max(maxCpuRgb,std::abs(int(solid.rgba[at+channel])-
        int(cpu.rgba[at+channel])));
    }
    depthWritten[0]+=solid.depth[pixel]!=1.0f;
    depthWritten[1]+=general.depth[pixel]!=1.0f;
    depthWritten[2]+=cpu.depth[pixel]!=1.0f;
    if(depthCoverage) {
      a=solid.depth[pixel]!=1.0f;
      b=general.depth[pixel]!=1.0f;
      c=cpu.depth[pixel]!=1.0f;
    }
    coverage+=a;coverageMismatch+=a!=b;
    cpuCoverageMismatch+=a!=c;
    if(a!=c && reportedCpuCoverage++<12) {
      std::cerr << "texture2d_selection " << label << " CPU coverage witness x="
                << pixel%side << " y=" << pixel/side << " GPU_RGBA=";
      for(int channel=0;channel<4;++channel)
        std::cerr << int(solid.rgba[at+channel]) << (channel==3?' ':',');
      std::cerr << " CPU_RGBA=";
      for(int channel=0;channel<4;++channel)
        std::cerr << int(cpu.rgba[at+channel]) << (channel==3?' ':',');
      std::cerr << " GPU_depth=" << solid.depth[pixel]
                << " CPU_depth=" << cpu.depth[pixel] << '\n';
    }
    alphaMismatch+=solid.rgba[at+3]!=general.rgba[at+3];
    cpuAlphaMismatch+=std::abs(int(solid.rgba[at+3])-int(cpu.rgba[at+3]))>1;
    maxDepth=std::max(maxDepth,std::abs(solid.depth[pixel]-general.depth[pixel]));
    maxCpuDepth=std::max(maxCpuDepth,std::abs(solid.depth[pixel]-cpu.depth[pixel]));
  }
  std::cout << "texture2d_selection " << label << " apply_ms=" << solid.applyMs
            << " general_apply_ms=" << general.applyMs
            << " cpu_apply_ms=" << cpu.applyMs
            << " coverage=" << coverage << " coverage_mismatch=" << coverageMismatch
            << " cpu_coverage_mismatch=" << cpuCoverageMismatch
            << " RGB_max=" << maxRgb << " cpu_RGB_max=" << maxCpuRgb
            << " alpha_mismatch=" << alphaMismatch << " depth_max=" << maxDepth
            << " cpu_alpha_mismatch=" << cpuAlphaMismatch
            << " cpu_depth_max=" << maxCpuDepth << '\n';
  return check(coverageMismatch==0 && alphaMismatch==0 && maxRgb<=1 &&
    maxDepth<=1e-6f && solid.serial>0 && general.serial>0 && cpu.serial>0,
    label+" texture2d must match forced general over the full framebuffer") &&
    check(!depthCoverage || (depthWritten[0]>0 && depthWritten[1]>0 &&
      depthWritten[2]>0),label+" BLEND fixture must write depth in all renderers") &&
    check(expectVisible<0 || (expectVisible ? coverage>0 : coverage==0),
      label+" independent visibility expectation") &&
    check(!canonical || (cpuCoverageMismatch==0 && cpuAlphaMismatch==0 && maxCpuRgb<=3 &&
      maxCpuDepth<=2e-5f),label+" canonical GPU must match CPU coverage/RGB/depth");
}
bool run(bool canonical,bool portable) {
  CoinRenderCapabilities caps{};
  const bool gpu=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,
    &caps,sizeof(caps))==0 && caps.gpu_available &&
    (caps.renderer==COIN_RENDER_RENDERER_VULKAN ||
     caps.renderer==COIN_RENDER_RENDERER_OPENGL);
  std::cout << "texture2d_selection backend=" << caps.backend << " renderer=" << caps.renderer
            << " vendor=" << caps.vendor_id << " adapter=" << caps.adapter_name
            << " canonical=" << canonical << " portable=" << portable
            << " gpu=" << gpu << '\n';
  if(!check(gpu,"real GPU required")) return false;
  const auto renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  Harness h(canonical,portable,renderer);
  if(!check(h.available() && h.solid->getOptions().renderer==renderer &&
    h.general->getOptions().renderer==renderer &&
    h.cpu->getOptions().renderer==renderer,"three targets use queried renderer")) return false;
  bool ok=true;
  const auto one=[&](Scene & scene,const std::string & label,
                     CoinRenderAction::TransparencyType type,int visible=1,
                     bool forceOpaquePixels=false) {
    h.mode(type);
    Frame fast,ordinary,reference;
    h.switcher.disableTexture2d(false);
    const bool a=h.capture(h.solidAction,*h.solid,scene.root,fast,label+" texture2d");
    h.switcher.disableTexture2d(true);
    const bool b=h.capture(h.generalAction,*h.general,scene.root,ordinary,label+" general");
    h.switcher.disableTexture2d(false);
    const bool c=h.capture(h.cpuAction,*h.cpu,scene.root,reference,label+" CPU");
    if(!(a&&b&&c)) return false;
    bool qualified=compare(fast,ordinary,reference,label,canonical,visible);
    if(forceOpaquePixels) {
      size_t covered=0;
      size_t incorrect=0;
      for(size_t pixel=0;pixel<size_t(side*side);++pixel) {
        if(fast.depth[pixel]==1.0f) continue;
        ++covered;
        incorrect+=fast.rgba[pixel*4+3]!=255;
        incorrect+=ordinary.rgba[pixel*4+3]!=255;
        incorrect+=reference.rgba[pixel*4+3]!=255;
      }
      qualified &= check(covered>0 && incorrect==0,
        label+" all covered pixels must force framebuffer alpha to one");
    }
    return qualified;
  };
  {
    Scene scene;
    ok &= one(scene,"opaque-base-first-apply",CoinRenderAction::NONE);
    scene.model->model=SoLightModel::PHONG;
    ok &= one(scene,"opaque-PHONG",CoinRenderAction::NONE);
  }
  {
    Scene scene;scene.material->transparency=.5f;
    ok &= one(scene,"screen-door-half",CoinRenderAction::SCREEN_DOOR);
    scene.vertexAlpha();
    ok &= one(scene,"screen-door-vertex-alpha",CoinRenderAction::SCREEN_DOOR);
    ok &= one(scene,"blend-vertex-alpha",CoinRenderAction::BLEND);
  }
  {
    Scene scene;
    ok &= one(scene,"screen-door-zero-opaque",CoinRenderAction::SCREEN_DOOR);
    scene.vertexAlpha(true);
    scene.alphaTest->function=SoAlphaTest::NONE;
    scene.alphaTest->value=.5f;
    const SbColor4f transparentBackground(.03f,.05f,.08f,0);
    const SbColor4f opaqueBackground(.03f,.05f,.08f,1);
    h.solidAction.setBackgroundColor(transparentBackground);
    h.generalAction.setBackgroundColor(transparentBackground);
    h.cpuAction.setBackgroundColor(transparentBackground);
    ok &= one(scene,"screen-door-zero-vertex-no-alpha-test",
      CoinRenderAction::SCREEN_DOOR,1,true);
    h.solidAction.setBackgroundColor(opaqueBackground);
    h.generalAction.setBackgroundColor(opaqueBackground);
    h.cpuAction.setBackgroundColor(opaqueBackground);
    scene.alphaTest->function=SoAlphaTest::GREATER;
    scene.alphaTest->value=.9f;
    ok &= one(scene,"screen-door-zero-vertex-greater",CoinRenderAction::SCREEN_DOOR);
    scene.alphaTest->function=SoAlphaTest::EQUAL;
    scene.alphaTest->value=1.f;
    ok &= one(scene,"screen-door-zero-vertex-equal",CoinRenderAction::SCREEN_DOOR);
  }
  const SoAlphaTest::Function functions[]={SoAlphaTest::NEVER,SoAlphaTest::ALWAYS,
    SoAlphaTest::LESS,SoAlphaTest::LEQUAL,SoAlphaTest::EQUAL,
    SoAlphaTest::GEQUAL,SoAlphaTest::GREATER,SoAlphaTest::NOTEQUAL};
  for(size_t index=0;index<8;++index) {
    Scene scene;scene.material->transparency=.5f;
    scene.alphaTest->function=functions[index];scene.alphaTest->value=.4f;
    const int visible[]={0,1,0,0,0,1,1,1};
    ok &= one(scene,"alpha-comparator-"+std::to_string(index),CoinRenderAction::BLEND,
      visible[index]);
    ok &= one(scene,"screen-door-alpha-comparator-"+std::to_string(index),
      CoinRenderAction::SCREEN_DOOR,visible[index]);
  }
  {
    Scene scene;scene.material->transparency=.4f;
    ok &= one(scene,"blend-fractional",CoinRenderAction::BLEND);
    ok &= one(scene,"sorted-object-blend-fractional",
      CoinRenderAction::SORTED_OBJECT_BLEND);
  }
  {
    Scene scene;
    scene.textureNode->model=SoTexture2::REPLACE;
    const SbVec2f uv(.25f,.25f);
    for(int i=0;i<4;++i) scene.textureCoords->point.set1Value(i,uv);
    ok &= one(scene,"replace-texel00",CoinRenderAction::NONE);
    h.mode(CoinRenderAction::NONE);
    Frame analytic;
    h.switcher.disableTexture2d(false);
    if(h.capture(h.solidAction,*h.solid,scene.root,analytic,"replace-analytic")) {
      const size_t center=(side/2*side+side/2)*4;
      const int expected[]={80,160,220,255};
      bool exact=true;
      for(int c=0;c<4;++c)
        exact &= std::abs(int(analytic.rgba[center+c])-expected[c])<=1;
      ok &= check(exact,"REPLACE interior texel matches authored RGBA analytically");
    } else ok=false;
  }
  {
    Scene scene;
    Frame original,changed;
    h.mode(CoinRenderAction::NONE);
    h.switcher.disableTexture2d(false);
    const bool before=h.capture(h.solidAction,*h.solid,scene.root,original,
      "texture-mutation-before");
    scene.mutateTexture();
    const bool after=h.capture(h.solidAction,*h.solid,scene.root,changed,
      "texture-mutation-after");
    ok &= check(before && after && original.rgba!=changed.rgba &&
      changed.serial>original.serial,"texture mutation changes actual pixels and serial");
    ok &= one(scene,"texture-mutation-general-reference",CoinRenderAction::NONE);
    scene.quality->textureQuality=.8f;
    ok &= one(scene,"linear-mip-band",CoinRenderAction::NONE);
    scene.q(1.4f);
    ok &= one(scene,"projective-q",CoinRenderAction::NONE);
  }
  {
    Scene scene;
    const unsigned char pixels[]={80,160,220,64,220,80,40,128,
      40,220,80,192,200,170,90,255};
    scene.textureNode->image.setValue(SbVec2s(2,2),4,pixels);
    scene.textureNode->blendColor.setValue(.2f,.6f,.1f);
    const SoTexture2::Model models[]={SoTexture2::MODULATE,SoTexture2::REPLACE,
      SoTexture2::DECAL,SoTexture2::BLEND};
    const float qualities[]={.1f,.3f,.6f,.8f};
    for(int model=0;model<4;++model) for(int quality=0;quality<4;++quality) {
      scene.textureNode->model=models[model];
      scene.quality->textureQuality=qualities[quality];
      ok &= one(scene,"model-"+std::to_string(model)+"-quality-"+
        std::to_string(quality),CoinRenderAction::BLEND);
    }
  }
  {
    Scene scene;scene.fog();
    ok &= one(scene,"fog-shared",CoinRenderAction::NONE);
  }
  {
    Scene scene;scene.clip();
    ok &= one(scene,"clipping-shared",CoinRenderAction::NONE);
  }
  {
    Scene scene;scene.extraUnit();
    ok &= one(scene,"fallback-extra-unit3",CoinRenderAction::NONE);
  }
  {
    Scene scene;
    h.mode(CoinRenderAction::NONE);
    Frame fast,ordinary,reference;
    h.switcher.disableTexture2d(false);
    const bool a=h.capture(h.solidAction,*h.solid,scene.root,fast,
      "malformed-control texture2d");
    h.switcher.disableTexture2d(true);
    const bool b=h.capture(h.generalAction,*h.general,scene.root,ordinary,
      "malformed-control general");
    h.switcher.disableTexture2d(false);
    const bool c=h.capture(h.cpuAction,*h.cpu,scene.root,reference,
      "malformed-control CPU");
    if(a&&b&&c) {
      scene.alphaTest->value=std::numeric_limits<float>::quiet_NaN();
      const auto reject=[&](CoinRenderAction & action,CoinRenderTarget & target,
                            const Frame & stable,const char * label,bool disableSolid) {
        h.switcher.disableTexture2d(disableSolid);
        action.apply(scene.root);
        std::vector<uint8_t> rgba;std::vector<float> depth;
        target.readbackRGBA(rgba);target.readbackDepth(depth);
        const bool refused=action.getLastStatus()!=CoinRenderAction::SUCCESS &&
          action.getLastError().getLength()>0 && rgba==stable.rgba &&
          depth==stable.depth && target.getLastSubmissionSerial()==stable.serial;
        h.switcher.disableTexture2d(false);
        return check(refused,std::string(label)+
          " nonfinite alpha reference preserves RGBA/depth/serial");
      };
      ok &= reject(h.solidAction,*h.solid,fast,"texture2d",false);
      ok &= reject(h.generalAction,*h.general,ordinary,"general",true);
      ok &= reject(h.cpuAction,*h.cpu,reference,"CPU",false);
      scene.alphaTest->value=.5f;
      ok &= one(scene,"malformed-recovery",CoinRenderAction::NONE);
    } else ok=false;
  }
  return ok;
}
}
int main(int argc,char ** argv) {
  bool canonical=false,portable=false;
  for(int index=1;index<argc;++index) {
    if(std::string(argv[index])=="--canonical-raster") canonical=true;
    else if(std::string(argv[index])=="--portable-sampling") portable=true;
    else {std::cerr << "Usage: CoinRenderTexture2dSelectionTest [--canonical-raster] [--portable-sampling]\n";return 2;}
  }
  SoDB::init();CoinRenderAction::initClass();
  return run(canonical,portable)?0:1;
}
