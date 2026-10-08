#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/system/gl.h>
#include <cmath>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

static SoSeparator * base() {
  auto * root = new SoSeparator;
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 3); camera->height = 2;
  root->addChild(camera);
  auto * lighting = new SoLightModel; lighting->model = SoLightModel::BASE_COLOR;
  root->addChild(lighting);
  auto * quality = new SoComplexity; quality->textureQuality = .3f;
  root->addChild(quality);
  return root;
}
static void quad(SoSeparator * root) {
  auto * coords = new SoCoordinate3;
  const SbVec3f points[] = {{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
  coords->point.setValues(0,4,points); root->addChild(coords);
  auto * uv = new SoTextureCoordinate2;
  const SbVec2f values[] = {{0,0},{1,0},{1,1},{0,1}};
  uv->point.setValues(0,4,values); root->addChild(uv);
  auto * face = new SoIndexedFaceSet;
  const int32_t indices[] = {0,1,2,3,-1};
  face->coordIndex.setValues(0,5,indices); root->addChild(face);
}
int main(int argc, char ** argv) {
  if (!std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) return 77;
  SoDB::init(); CoinRenderAction::initClass();
  const bool samplingStudy = argc>1 && std::string(argv[1])=="--sampling-rtt-study";
  const bool mipOnly = samplingStudy || argc>1 && (std::string(argv[1])=="--mips" || std::string(argv[1])=="--mips-direct");
  const bool direct = samplingStudy || argc > 1 && (std::string(argv[1]) == "--direct" || std::string(argv[1])=="--mips-direct");
  std::string optionError;
  CoinRenderOptions options=CoinRenderDiagnosticShell::renderOptions(optionError);
  if (!optionError.empty()) { std::cerr<<optionError<<'\n'; return 1; }
  options.sceneTexture = direct ? COIN_RENDER_SCENE_TEXTURE_DIRECT : COIN_RENDER_SCENE_TEXTURE_STAGED;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),options));
  CoinRenderAction action(SbViewportRegion(32,32)); action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::BLEND);
  action.setBackgroundColor(SbColor4f(.1f,.2f,.3f,1));
  SoOffscreenRenderer gl(SbViewportRegion(32,32)); gl.setComponents(SoOffscreenRenderer::RGB);
  gl.setBackgroundColor(SbColor(.1f,.2f,.3f));
  gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  if (samplingStudy) {
    CoinRenderCapabilities caps{};
    coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps));
    std::cout<<"adapter="<<caps.adapter_name<<" renderer="<<caps.renderer<<" vendor="<<caps.vendor_id<<" device="<<caps.device_id<<'\n';
    unsigned controls=0;bool ok=true;
    for(int unit : {0,7}) {
      auto *child=base();static_cast<SoComplexity *>(child->getChild(2))->textureQuality=.1f;
      // Cover the rectangular producer completely: ADJUST_CAMERA would widen
      // its frustum and leave clear-color borders outside the authored quad.
      auto *producerCamera=static_cast<SoOrthographicCamera *>(child->getChild(0));
      producerCamera->viewportMapping=SoCamera::LEAVE_ALONE;producerCamera->aspectRatio=1;
      auto *pattern=new SoTexture2;pattern->model=SoTexture2::REPLACE;
      std::vector<unsigned char> bands(128*16*4,255);
      for(int y=0;y<16;++y)for(int x=0;x<128;++x)for(int c=0;c<3;++c)bands[(y*128+x)*4+c]=x<64?40:160;
      pattern->image.setValue(SbVec2s(128,16),4,bands.data());child->addChild(pattern);quad(child);
      auto *root=base();root->ref();auto *quality=static_cast<SoComplexity *>(root->getChild(2));
      quality->textureQuality=.7f;
      auto *active=new SoTextureUnit;active->unit=unit;root->addChild(active);
      auto *texture=new SoSceneTexture2;texture->scene=child;texture->model=SoSceneTexture2::REPLACE;
      root->addChild(texture);quad(root);
      auto *uv=static_cast<SoTextureCoordinate2 *>(root->getChild(root->getNumChildren()-2));
      for(auto size : {SbVec2s(512,128),SbVec2s(1024,64),SbVec2s(128,256),SbVec2s(512,128)})
      for(float step : {1.f/16384,1.f/16,3.f/32}) {
        texture->size=size;
        const float first=.49999237060546875f-15.5f*step,last=first+32*step;
        const SbVec2f points[]={{first,.53125f},{last,.53125f},{last,.53125f},{first,.53125f}};
        uv->point.setValues(0,4,points);action.apply(root);
        std::vector<uint8_t> pixels;target->readbackRGBA(pixels);
        bool passed=action.getLastStatus()==CoinRenderAction::SUCCESS && pixels.size()==32*32*4;
        int maximum=0;double mae=0;unsigned samples=0;
        if(passed)for(int y=2;y<30;++y)for(int x=2;x<30;++x)for(int c=0;c<3;++c) {
          const double s=.49999237060546875+(x-15)*double(step);
          // In minification each participating POT mip preserves the half band.
          // In magnification the boundary pixel approaches the linear midpoint.
          const double wrapped=s-std::floor(s);
          double expected=wrapped<.5?40:160;
          if(step==1.f/16384) {
            const double at=wrapped*size[0]-.5;const int left=int(std::floor(at));const double f=at-left;
            const auto band=[&](int at){at=(at%size[0]+size[0])%size[0];return at<size[0]/2?40.:160.;};
            expected=band(left)+(band(left+1)-band(left))*f;
          }
          const int delta=std::abs(int(pixels[(y*32+x)*4+c])-int(std::lround(expected)));
          maximum=std::max(maximum,delta);mae+=delta;++samples;
        }
        if(samples)mae/=samples;
        std::cout<<"direct-rtt/"<<size[0]<<'x'<<size[1]<<"/unit-"<<unit<<"/step-"<<step<<" samples="<<samples<<" mae="<<mae<<" maximum="<<maximum<<" status="<<action.getLastStatus()<<'\n';
        passed=passed && samples==28*28*3 && mae<=1.5 && maximum<=4;
        if(!passed)std::cerr<<"FAIL direct RTT oracle "<<action.getLastError().getString()<<'\n';
        ok &= passed;
        const auto previous=pixels;texture->size=SbVec2s(0,64);action.apply(root);target->readbackRGBA(pixels);
        ok &= action.getLastStatus()==CoinRenderAction::UNSUPPORTED && pixels==previous;
        texture->size=size;action.apply(root);target->readbackRGBA(pixels);
        ok &= action.getLastStatus()==CoinRenderAction::SUCCESS && pixels==previous;
        ++controls;
      }
      root->unref();
    }
    std::cout<<"direct RTT sampling controls="<<controls<<" resize/reject/recover result="<<(ok?"PASS":"FAIL")<<'\n';
    return ok?0:1;
  }
  unsigned passed = 0, nativePassed = 0;
  GLint nativeUnits = 0;
  for (int unit = 0; unit < (mipOnly ? 0 : 8); ++unit)
  for (int model : {SoSceneTexture2::MODULATE,SoSceneTexture2::REPLACE,SoSceneTexture2::DECAL,SoSceneTexture2::BLEND})
  for (int policy : {SoSceneTexture2::NONE,SoSceneTexture2::ALPHA_BLEND,SoSceneTexture2::ALPHA_TEST})
  for (int producerPolicy : {-1, int(SoTransparencyType::NONE), int(SoTransparencyType::BLEND)}) {
    auto * child = base();
    auto * material = new SoMaterial; material->diffuseColor.setValue(.8f,.1f,.2f);
    material->transparency = .5f; child->addChild(material); quad(child);
    auto * root = base(); root->ref();
    auto * witness = new SoCallback;
    witness->setCallback([](void * data, SoAction * a) {
      if (a->isOfType(SoGLRenderAction::getClassTypeId()))
        glGetIntegerv(GL_MAX_TEXTURE_UNITS, static_cast<GLint *>(data));
    }, &nativeUnits);
    root->addChild(witness);
    auto * inherited = new SoTransparencyType; inherited->value = SoTransparencyType::NONE;
    root->addChild(inherited);
    auto * parentMaterial = new SoMaterial; parentMaterial->diffuseColor.setValue(.4f,.6f,.8f);
    root->addChild(parentMaterial);
    // An ordinary active unit precedes the scene producer, proving composition
    // with a different image rather than testing a lone higher unit only.
    if (unit) {
      auto * ordinary = new SoTexture2;
      const unsigned char pixel[] = {128,192,255,255};
      ordinary->image.setValue(SbVec2s(1,1),4,pixel); root->addChild(ordinary);
    }
    auto * activeUnit = new SoTextureUnit; activeUnit->unit = unit; root->addChild(activeUnit);
    auto * texture = new SoSceneTexture2;
    texture->scene = child; texture->size.setValue(16,24);
    texture->backgroundColor.setValue(.1f,.2f,.7f,.5f);
    texture->model = model; texture->blendColor.setValue(.2f,.3f,.4f);
    texture->transparencyFunction = policy;
    if (producerPolicy >= 0) {
      auto * explicitPolicy = new SoTransparencyType; explicitPolicy->value = producerPolicy;
      texture->sceneTransparencyType = explicitPolicy;
    }
    root->addChild(texture);
    auto * consumerPolicy = new SoTransparencyType; consumerPolicy->value = SoTransparencyType::BLEND;
    root->addChild(consumerPolicy); quad(root);
    if (!gl.render(root) || !gl.getBuffer()) { std::cerr << "CoinGL reference failed\n"; return 1; }
    const unsigned char * reference = gl.getBuffer()+(16*32+16)*3;
    int expected[] = {reference[0],reference[1],reference[2]};
    if (nativeUnits <= 0) return 1;
    if (unit < nativeUnits) ++nativePassed;
    else {
      // Fixed-function CoinGL cannot be an oracle beyond its actual unit limit.
      // Independent scalar legacy equations qualify the portable higher units.
      const float tex[] = {producerPolicy == SoTransparencyType::BLEND ? .45f : .8f,
                           producerPolicy == SoTransparencyType::BLEND ? .15f : .1f,
                           producerPolicy == SoTransparencyType::BLEND ? .45f : .2f};
      const float primary[] = {.4f*128/255,.6f*192/255,.8f};
      const float blendColor[] = {.2f,.3f,.4f};
      const float background[] = {.1f,.2f,.3f};
      float alpha = model == SoSceneTexture2::DECAL ? 1.f : .5f;
      for (int c=0;c<3;++c) {
        float color = model == SoSceneTexture2::MODULATE ? primary[c]*tex[c]
                    : model == SoSceneTexture2::REPLACE ? tex[c]
                    : model == SoSceneTexture2::DECAL ? primary[c]*.5f+tex[c]*.5f
                    : primary[c]*(1-tex[c])+blendColor[c]*tex[c];
        if (policy != SoSceneTexture2::NONE) color = color*alpha+background[c]*(1-alpha);
        expected[c] = int(std::lround(color*255));
      }
    }
    action.apply(root);
    std::vector<uint8_t> pixels; target->readbackRGBA(pixels);
    bool ok = action.getLastStatus() == CoinRenderAction::SUCCESS && pixels.size()==32*32*4;
    int difference = 0;
    if (ok) for (int c=0;c<3;++c) difference=std::max(difference,std::abs(int(pixels[(16*32+16)*4+c])-expected[c]));
    if (!ok || difference > 2) {
      std::cerr << "RTT unit=" << unit << " model=" << model << " policy=" << policy
                << " producerPolicy=" << producerPolicy << " maxRGB=" << difference
                << " expected=" << expected[0] << ',' << expected[1] << ',' << expected[2]
                << " actual=" << (pixels.empty()? -1:int(pixels[(16*32+16)*4])) << ',' << (pixels.empty()? -1:int(pixels[(16*32+16)*4+1])) << ',' << (pixels.empty()? -1:int(pixels[(16*32+16)*4+2]))
                << " error=" << action.getLastError().getString() << '\n'; return 1;
    }
    // A nonportable format must preserve the successful frame and then recover.
    texture->type = SoSceneTexture2::RGBA32F; action.apply(root);
    std::vector<uint8_t> retained; target->readbackRGBA(retained);
    if (action.getLastStatus()!=CoinRenderAction::UNSUPPORTED || retained!=pixels) return 1;
    texture->type = SoSceneTexture2::RGBA8; action.apply(root); target->readbackRGBA(retained);
    if (action.getLastStatus()!=CoinRenderAction::SUCCESS || retained!=pixels) return 1;
    root->unref(); ++passed;
  }
  if (mipOnly) {
    auto * child=base();
    static_cast<SoComplexity *>(child->getChild(2))->textureQuality=.1f;
    auto * pattern=new SoTexture2; pattern->model=SoTexture2::REPLACE; std::vector<unsigned char> checker(16*16*4,255);
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
      const unsigned i=(y*16+x)*4; checker[i]=((x+y)&1)?255:0;
      checker[i+1]=0; checker[i+2]=((x+y)&1)?0:255;
    }
    pattern->image.setValue(SbVec2s(16,16),4,checker.data()); child->addChild(pattern); quad(child);
    auto * root=base(); root->ref(); auto * quality=static_cast<SoComplexity *>(root->getChild(2));
    auto * texture=new SoSceneTexture2; texture->scene=child; texture->size=SbVec2s(64,64);
    texture->model=SoSceneTexture2::REPLACE; root->addChild(texture); quad(root);
    auto * uv=static_cast<SoTextureCoordinate2 *>(root->getChild(root->getNumChildren()-2));
    const SbVec2f repeated[]={{.023f,.031f},{10.323f,.031f},{10.323f,10.331f},{.023f,10.331f}};
    uv->point.setValues(0,4,repeated);
    double largestMae=0;
    for(float q : {.3f,.5f,.51f,.7f,.85f,.5f,.7f}) {
      quality->textureQuality=q; action.apply(root);
      std::vector<uint8_t> pixels; target->readbackRGBA(pixels);
      if(action.getLastStatus()!=CoinRenderAction::SUCCESS || pixels.size()!=32*32*4) {
        std::cerr<<"RTT mip quality "<<q<<": "<<action.getLastError().getString()<<'\n';return 1;
      }
      if(!gl.render(root) || !gl.getBuffer()) { std::cerr<<"RTT mip CoinGL render failed at quality "<<q<<'\n'; return 1; }
      const auto * reference=gl.getBuffer(); double mae=0;
      for(unsigned i=0;i<32*32;++i) for(unsigned c=0;c<3;++c) mae+=std::abs(int(pixels[i*4+c])-int(reference[i*3+c]));
      mae/=32*32*3; if(q>.5f) largestMae=std::max(largestMae,mae);
      if(q>.5f) {
        for(unsigned i=0;i<32*32;++i) if(std::abs(int(pixels[i*4])-128)>3 || pixels[i*4+1]>3 || std::abs(int(pixels[i*4+2])-128)>3) {
          std::cerr<<"RTT minification lost deterministic mip average at "<<i<<'\n';return 1;
        }
        if(mae>4) { std::cerr<<"RTT mip CoinGL MAE="<<mae<<'\n';return 1; }
      }
      const auto previous=pixels;
      texture->size=SbVec2s(64,48); quality->textureQuality=.7f; action.apply(root); target->readbackRGBA(pixels);
#ifdef HAVE_COIN_BGFX
      const bool npotDirectMipUnavailable=direct;
#else
      const bool npotDirectMipUnavailable=false;
#endif
      if(npotDirectMipUnavailable) {
        std::vector<uint8_t> retained;target->readbackRGBA(retained);
        if(action.getLastStatus()!=CoinRenderAction::UNSUPPORTED || retained!=previous || pixels!=previous) return 1;
      } else if(action.getLastStatus()!=CoinRenderAction::SUCCESS) { std::cerr<<"RTT mip NPOT capture failed at "<<q<<'\n'; return 1; }
      texture->size=SbVec2s(0,48);action.apply(root);
      std::vector<uint8_t> preserved;target->readbackRGBA(preserved);
      if(action.getLastStatus()!=CoinRenderAction::UNSUPPORTED || preserved!=pixels) {
        std::cerr<<"invalid RTT dimension published a frame\n";return 1;
      }
      texture->size=SbVec2s(64,64); quality->textureQuality=q; action.apply(root); target->readbackRGBA(pixels);
      if(action.getLastStatus()!=CoinRenderAction::SUCCESS || pixels!=previous) { std::cerr<<"RTT mip recovery failed at "<<q<<": "<<action.getLastError().getString()<<'\n'; return 1; }
      ++passed;
    }
    quality->textureQuality=0; action.apply(root);
    if(action.getLastStatus()!=CoinRenderAction::SUCCESS) return 1;
    quality->textureQuality=.7f; action.apply(root);
    if(action.getLastStatus()!=CoinRenderAction::SUCCESS) return 1;
    root->unref(); std::cout<<"RTT mip transitions: "<<passed<<" controls; maximum RGB MAE "<<largestMae<<"; NPOT admission/explicit limits, invalid-size rejection/publication and recovery passed\n";
  }
  action.setRenderTarget(nullptr);
  if (!mipOnly) std::cout << "RTT extended " << (direct ? "direct" : "staged") << ": " << passed
            << " controls (" << nativePassed << " native CoinGL, " << passed-nativePassed
            << " scalar higher-unit expectations), rejection and recovery passed\n";
  return 0;
}
