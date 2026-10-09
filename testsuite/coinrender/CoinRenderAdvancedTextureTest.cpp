#include <Inventor/rendering/CoinRenderTarget.h>
#include "CoinRenderSamplingTestOptions.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderRttExecution.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoComplexity.h>
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
#include <Inventor/rendering/CoinRenderTarget.h>
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#endif
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
using F = CoinRenderTextureFormat;
using Format = CoinRenderTextureFormatCore;
using Sampling = CoinRenderTextureSamplingCore;
static unsigned controls = 0;
static bool check(bool ok, const std::string &name) {
  ++controls;
  if (!ok)
    std::cerr << "FAIL " << name << '\n';
  return ok;
}
class Capture : public CoinRenderCpuReferenceBackend {
public:
  CoinRenderFramePlan plan;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &p,
                                CoinRenderTargetP &t) override {
    plan = p;
    return CoinRenderCpuReferenceBackend::submit(p, t);
  }
};
struct Scene {
  SoSeparator *root = new SoSeparator;
  SoComplexity *quality = new SoComplexity;
  SoTexture2 *texture = new SoTexture2;
  SoMaterial *material = new SoMaterial;
  Scene() {
    root->ref();
    auto *camera = new SoOrthographicCamera;
    camera->height = 2;
    camera->position.setValue(0, 0, 3);
    root->addChild(camera);
    auto *light = new SoLightModel;
    light->model = SoLightModel::BASE_COLOR;
    root->addChild(light);
    quality->textureQuality = .3f;
    root->addChild(quality);
    material->diffuseColor.setValue(1, 1, 1);
    root->addChild(material);
    texture->model = SoTexture2::REPLACE;
    std::vector<uint8_t> bytes(4 * 4 * 4, 128);
    for (size_t i = 3; i < bytes.size(); i += 4)
      bytes[i] = 255;
    texture->image.setValue(SbVec2s(4, 4), 4, bytes.data());
    root->addChild(texture);
    auto *coords = new SoCoordinate3;
    const SbVec3f p[] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    coords->point.setValues(0, 4, p);
    root->addChild(coords);
    auto *uv = new SoTextureCoordinate2;
    const SbVec2f st[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    uv->point.setValues(0, 4, st);
    root->addChild(uv);
    auto *face = new SoIndexedFaceSet;
    const int32_t idx[] = {0, 1, 2, 3, -1};
    face->coordIndex.setValues(0, 5, idx);
    root->addChild(face);
  }
  ~Scene() { root->unref(); }
};
static bool numeric() {
  bool ok = true;
  for (auto pair : {std::pair<uint16_t, float>{0, 0},
                    {0x3c00, 1},
                    {0x4000, 2},
                    {0xbc00, -1},
                    {0x7bff, 65504},
                    {1, std::ldexp(1.f, -24)}})
    ok &= check(Format::fromHalf(pair.first) == pair.second &&
                    Format::toHalf(pair.second) == pair.first,
                "binary16 independent bit pattern");
  ok &= check(Format::toHalf(1.f + std::ldexp(1.f, -11)) == 0x3c00 &&
                  Format::toHalf(1.f + 3 * std::ldexp(1.f, -11)) == 0x3c02,
              "half ties to even");
  CoinRenderTextureImageSnapshot image;
  image.width = 2;
  image.height = 1;
  image.format = F::RGBA8_SRGB;
  image.pixelsRgba = {0, 0, 0, 0, 255, 255, 255, 255};
  ok &=
      check(Sampling::generate(image) &&
                image.mipmapsRgba == std::vector<uint8_t>({188, 188, 188, 128}),
            "SRGB mip averages linear RGB, linear alpha");
  image.width = 3;
  image.height = 1;
  image.format = F::RGBA8_LINEAR;
  image.mipmapped = false;
  image.mipmapsRgba.clear();
  image.pixelsRgba = {0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255};
  ok &= check(Sampling::generate(image) &&
                  image.mipmapsRgba == std::vector<uint8_t>({85, 85, 85, 255}),
              "NPOT area includes odd last texel");
  image.width = 2;
  image.height = 1;
  image.format = F::RGBA16_FLOAT;
  image.pixelsRgba.resize(16);
  image.mipmapsRgba.clear();
  for (size_t i = 0; i < 8; ++i)
    Format::write16(image.pixelsRgba.data() + 2 * i, i < 4 ? 0x4000 : 0x4400);
  ok &= check(Sampling::generate(image) &&
                  Format::read16(image.mipmapsRgba.data()) == 0x4200,
              "HDR mip preserves average 3 above one");
  Format::write16(image.pixelsRgba.data(), 0x7c00);
  ok &= check(!Sampling::validMipImage(image), "infinite half payload rejects");
  Format::write16(image.pixelsRgba.data(), 0x7e00);
  ok &= check(!Sampling::validMipImage(image), "NaN half payload rejects");
  std::vector<uint8_t> block(16, 0);
  block[0] = 255;
  block[1] = 0;
  block[8] = 0;
  block[9] = 0xf8;
  block[10] = 0xe0;
  block[11] = 7;
  auto red = Format::texel(block, 0, 4, F::BC3_LINEAR, 0, 0);
  block[12] = 1;
  auto green = Format::texel(block, 0, 4, F::BC3_LINEAR, 0, 0);
  ok &= check(red == SbVec4f(1, 0, 0, 1) && green == SbVec4f(0, 1, 0, 1),
              "BC3 independent RGB565 endpoints");
  return ok;
}
static bool center(const std::vector<uint8_t> &pixels, const int expected[3],
                   const std::string &label, int tolerance = 2) {
  bool ok = pixels.size() == 32 * 32 * 4;
  int maximum = 0;
  if (ok)
    for (int y = 12; y < 20; ++y)
      for (int x = 12; x < 20; ++x)
        for (int c = 0; c < 3; ++c)
          maximum =
              std::max(maximum, std::abs(int(pixels[(y * 32 + x) * 4 + c]) -
                                         expected[c]));
  std::cout << label << " maximumRGB=" << maximum << '\n';
  return check(ok && maximum <= tolerance, label);
}
int main(int argc, char **argv) {
  configureSamplingTest(argc, argv);
  SoDB::init();
  CoinRenderAction::initClass();
  bool gpu = argc > 1 && std::string(argv[1]) == "--gpu";
  bool ok = numeric();
  Scene scene;
  CoinRenderOptions options = samplingTestOptions();
  options.storedTextureColorSpace = COIN_RENDER_TEXTURE_SRGB;
  std::unique_ptr<CoinRenderTarget> captureTarget(
      samplingTestOffscreen(SbVec2i32(32, 32), options));
  auto *capture = new Capture;
  captureTarget->getPimpl()->backend.reset(capture);
  CoinRenderAction action(SbViewportRegion(32, 32));
  action.setRenderTarget(captureTarget.get());
  action.apply(scene.root);
  ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  capture->plan.textures[0].format == F::RGBA8_SRGB,
              "authored SRGB opt-in capture");
  CoinRenderFramePlan base = capture->plan;
  CoinRenderTargetP cpu(SbVec2i32(32, 32));
  cpu.options.textureSamplingPolicy = samplingTestPolicy;
  cpu.backend.reset(new CoinRenderCpuReferenceBackend);
  CoinRenderRenderer selectedRenderer = COIN_RENDER_RENDERER_UNKNOWN;
  std::unique_ptr<CoinRenderTarget> native;
  if (gpu) {
    native.reset(samplingTestOffscreen(SbVec2i32(32, 32)));
    CoinRenderCapabilities caps{};
    coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps,
                                   sizeof(caps));
    selectedRenderer = CoinRenderRenderer(caps.renderer);
    std::cout << "adapter=" << caps.adapter_name
              << " renderer=" << caps.renderer << " vendor=" << caps.vendor_id
              << " device=" << caps.device_id << " features=" << caps.features
              << '\n';
  }
  uint64_t revision = 1000000;
  const auto render = [&](CoinRenderFramePlan plan, const int expected[3],
                          const std::string &label) {
    plan.revision = ++revision;
    for (auto &draw : plan.draws)
      draw.sourceRevision = plan.revision;
    bool passed = true;
    auto result = cpu.executeFrame(plan);
    passed &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                    label + " CPU status " + result.diagnostic);
    passed &= center(cpu.colorBuffer, expected, label + " CPU");
    if (native) {
      result = native->getPimpl()->executeFrame(plan);
      passed &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                      label + " GPU status " + result.diagnostic);
      std::vector<uint8_t> pixels;
      native->readbackRGBA(pixels);
      passed &= center(pixels, expected, label + " GPU");
    }
    return passed;
  };
  if (argc > 2 && std::string(argv[2]) == "--sampling-rtt-study") {
    // Public SoSceneTexture2 always uses linear/trilinear. This explicitly
    // authored private consumer tests filter2 against a direct GPU resource.
    CoinRenderOptions directOptions = samplingTestOptions();directOptions.sceneTexture=COIN_RENDER_SCENE_TEXTURE_DIRECT;
    directOptions.renderer=selectedRenderer;
    std::unique_ptr<CoinRenderTarget> direct(samplingTestOffscreen(SbVec2i32(32,32),directOptions));
    unsigned cases=0;
    for(F format : {F::RGBA8_LINEAR,F::RGBA16_FLOAT}) for(unsigned unit : {0u,7u})
    for(auto size : {SbVec2i32(512,128),SbVec2i32(1024,64),SbVec2i32(128,256),SbVec2i32(512,128)})
    for(float step : {1.f/16384,1.f/16,3.f/32}) {
      auto root=base;
      auto &image=root.textures[0];image.width=size[0];image.height=size[1];image.format=format;
      image.pixelsRgba.clear();image.mipmapsRgba.clear();image.mipmapped=true;image.producerId=1;
      root.samplers[0].filter=CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR;
      for(auto &vertex:root.vertices) {
        vertex.texcoord[0]=.49999237060546875f+((vertex.position[0]+1)*16-15.5f)*step;
        vertex.texcoord[1]=.53125f;
        if(unit)for(unsigned c=0;c<2;++c)vertex.extraTexcoords[6][c]=vertex.texcoord[c];
      }
      for(auto &state:root.renderStates)if(unit) {
        auto layer=coin_render_texture_unit(state,0);state.hasTexture=false;state.extraTextures[6]=layer;
      }
      CoinRenderRttProducer producer;producer.sourceRevision=++revision;
      producer.size=size;producer.format=format;producer.plan=base;producer.plan.revision=revision;
      for(auto &draw:producer.plan.draws)draw.sourceRevision=revision;
      for(auto &viewport:producer.plan.viewports){viewport.width=size[0];viewport.height=size[1];}
      // Captured square projection and clip-space quad deliberately fill the
      // rectangular target. This isolates sampling from camera aspect policy.
      auto &source=producer.plan.textures[0];source.width=128;source.height=16;source.format=F::RGBA8_LINEAR;
      source.mipmapped=false;source.mipmapsRgba.clear();source.pixelsRgba.assign(128*16*4,255);
      for(unsigned y=0;y<16;++y)for(unsigned x=0;x<128;++x)for(unsigned c=0;c<3;++c)
        source.pixelsRgba[(y*128+x)*4+c]=x<64?40:160;
      producer.plan.samplers[0].filter=CoinRenderTextureFilter::NEAREST;
      CoinRenderRttPlan graph(COIN_RENDER_SCENE_TEXTURE_DIRECT);uint64_t id;std::string diagnostic;
      ok &= check(graph.append(producer,id,diagnostic),"direct half-band capture "+diagnostic);
      CoinRenderRttExecution execution(direct->getPimpl().operator->(),directOptions);CoinRenderFramePlan resolved;
      auto result=execution.prepare(graph,root,resolved);
      ok &= check(result.status==CoinRenderBackendStatus::SUCCESS,"direct half-band prepare "+result.diagnostic);
      if(result.status!=CoinRenderBackendStatus::SUCCESS)continue;
      resolved.revision=++revision;for(auto &draw:resolved.draws)draw.sourceRevision=revision;
      result=direct->getPimpl()->executeFrame(resolved);
      ok &= check(result.status==CoinRenderBackendStatus::SUCCESS,"direct half-band consumer "+result.diagnostic);
      std::vector<uint8_t> pixels;direct->readbackRGBA(pixels);
      double mae=0;int maximum=0;unsigned samples=0;
      if(pixels.size()==32*32*4)for(int y=2;y<30;++y)for(int x=2;x<30;++x)for(int c=0;c<3;++c) {
        const double uv=.49999237060546875+(x-15)*double(step);const double wrapped=uv-std::floor(uv);
        double expected=wrapped<.5?40:160;
        if(step==1.f/16384) {
          const double at=wrapped*size[0]-.5;const int left=int(std::floor(at));const double f=at-left;
          const auto band=[&](int at){at=(at%size[0]+size[0])%size[0];return at<size[0]/2?40.:160.;};
          expected=band(left)+(band(left+1)-band(left))*f;
        }
        const int delta=std::abs(int(pixels[(y*32+x)*4+c])-int(std::lround(expected)));
        mae+=delta;maximum=std::max(maximum,delta);++samples;
      }
      if(samples)mae/=samples;
      const std::string label="direct-filter2/"+std::to_string(size[0])+"x"+std::to_string(size[1])+"/format-"+
        std::to_string(unsigned(format))+"/unit-"+std::to_string(unit)+"/step-"+std::to_string(step);
      std::cout<<label<<" samples="<<samples<<" mae="<<mae<<" maximum="<<maximum<<'\n';
      ok &= check(samples==28*28*3 && mae<=1.5 && maximum<=4,label+" scalar oracle");++cases;
    }
    std::cout<<"direct filter2 sampling cases="<<cases<<" controls="<<controls<<" result="<<(ok?"PASS":"FAIL")<<'\n';
    return ok?0:1;
  }
  const bool viewportStudy=argc>2 && std::string(argv[2])=="--sampling-viewport-study";
  if (viewportStudy || (argc > 2 && std::string(argv[2]) == "--sampling-deep-study")) {
    // Independent half-band oracle at integer/fractional deep LOD. Original UV
    // derivatives select LOD8 at width4096; source coordinates lie just below
    // the half-texture boundary. Native AMD rounding chose the opposite band.
    unsigned cases = 0;
    for (auto size : {std::pair<unsigned,unsigned>{128,128}, {512,512},
                     {4096,4096}, {4096,128}, {511,257}})
    for (F format : {F::RGBA8_LINEAR, F::RGBA8_SRGB, F::RGBA16_FLOAT,
                     F::BC3_LINEAR, F::BC3_SRGB}) {
      // The larger square is the precision regression, typed formats use the
      // rectangular control to bound memory while preserving the same LOD.
      if (format != F::RGBA8_LINEAR && size != std::make_pair(4096u,128u)) continue;
      if(viewportStudy && (format!=F::RGBA8_LINEAR || (size!=std::make_pair(4096u,128u) && size!=std::make_pair(511u,257u))))continue;
      auto authored = base;
      auto &image = authored.textures[0];
      image.width=size.first; image.height=size.second; image.format=format;
      image.mipmapped=false; image.mipmapsRgba.clear();
      image.pixelsRgba.assign(Format::levelBytes(image.width,image.height,format),0);
      const auto fill = [&](std::vector<uint8_t> &bytes,unsigned width,unsigned height) {
        if (Format::compressed(format)) {
          // Independently authored BC3 blocks: opaque RGB565 endpoints and
          // selector zero; each mip is explicit, with no encode dependency.
          for(unsigned y=0;y<(height+3)/4;++y) for(unsigned x=0;x<(width+3)/4;++x) {
            auto *b=bytes.data()+(y*((width+3)/4)+x)*16;
            b[0]=255; Format::write16(b+8,x*4<width/2?0x2800:0xa000);
          }
        } else for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
          const uint8_t band=x<width/2?40:160;
          const size_t i=size_t(y)*width+x;
          if(format==F::RGBA16_FLOAT) {
            for(unsigned c=0;c<4;++c) Format::write16(bytes.data()+i*8+c*2,
              Format::toHalf(c==3?1.f:float(band)/255.f));
          } else {
            for(unsigned c=0;c<3;++c) bytes[i*4+c]=band;
            bytes[i*4+3]=255;
          }
        }
      };
      fill(image.pixelsRgba,image.width,image.height);
      if (Format::compressed(format)) {
        unsigned w=image.width,h=image.height;
        while(w>1 || h>1) {
          w=std::max(1u,w/2);h=std::max(1u,h/2);
          std::vector<uint8_t> level(Format::levelBytes(w,h,format),0);
          fill(level,w,h);image.mipmapsRgba.insert(image.mipmapsRgba.end(),level.begin(),level.end());
        }
        image.mipmapped=true;
      } else ok &= check(Sampling::generate(image),"deep authored complete chain");
      for(float step : {1.f/16384,1.f/16,3.f/32}) for(unsigned unit : {0u,7u,8u})
      for(CoinRenderTextureWrap wrap : {CoinRenderTextureWrap::REPEAT,CoinRenderTextureWrap::CLAMP}) {
        auto frame=authored;
        if(viewportStudy)for(auto &viewport:frame.viewports){viewport.x=4;viewport.y=5;viewport.width=24;viewport.height=18;}
        const unsigned x0=viewportStudy?6:2,x1=viewportStudy?26:30;
        const unsigned y0=viewportStudy?11:2,y1=viewportStudy?25:30;
        frame.samplers[0].filter=CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR;
        frame.samplers[0].wrapS=frame.samplers[0].wrapT=wrap;
        frame.samplers[0].maxAnisotropy=1;
        for(auto &vertex:frame.vertices) {
          vertex.texcoord[0]=.49999237060546875f+((vertex.position[0]+1)*16-15.5f)*step;
          vertex.texcoord[1]=.53125f;
          if(unit) for(unsigned c=0;c<2;++c) vertex.extraTexcoords[6][c]=vertex.texcoord[c];
        }
        for(auto &state:frame.renderStates) if(unit) {
          auto layer=coin_render_texture_unit(state,0);
          state.hasTexture=unit==8;state.extraTextures[6]=layer;
        }
        frame.revision=++revision;for(auto &draw:frame.draws) draw.sourceRevision=frame.revision;
        const std::string label=std::string(viewportStudy?"viewport-inset/":"deep/")+std::to_string(size.first)+"x"+std::to_string(size.second)+
          "/format-"+std::to_string(unsigned(format))+"/step-"+std::to_string(step)+
          "/unit-"+std::to_string(unit)+"/wrap-"+std::to_string(unsigned(wrap));
        auto result=cpu.executeFrame(frame);
        ok &= check(result.status==CoinRenderBackendStatus::SUCCESS,label+" CPU "+result.diagnostic);
        if(size.first==4096 && step>=1.f/16 && cpu.colorBuffer.size()==32*32*4) {
          const int expected = format==F::RGBA8_SRGB?5 : format==F::BC3_SRGB?6 : format==F::BC3_LINEAR?41:40;
          ok &= check(std::abs(int(cpu.colorBuffer[(16*32+15)*4])-expected)<=1,
            label+" independent low-band oracle at boundary pixel");
        }
        if(native) {
          result=native->getPimpl()->executeFrame(frame);
          ok &= check(result.status==CoinRenderBackendStatus::SUCCESS,label+" GPU "+result.diagnostic);
          std::vector<uint8_t> pixels;native->readbackRGBA(pixels);
          double mae=0;int maximum=0;unsigned samples=0;
          if(pixels.size()==cpu.colorBuffer.size() && pixels.size()==32*32*4)
            for(unsigned y=y0;y<y1;++y) for(unsigned x=x0;x<x1;++x) for(unsigned c=0;c<4;++c) {
              const auto i=(y*32+x)*4+c;const int delta=std::abs(int(pixels[i])-int(cpu.colorBuffer[i]));
              mae+=delta;maximum=std::max(maximum,delta);++samples;
            }
          if(samples) mae/=samples;
          std::cout<<label<<" samples="<<samples<<" mae="<<mae<<" maximum="<<maximum<<'\n';
          ok &= check(samples==(x1-x0)*(y1-y0)*4 && mae<=1.5 && maximum<=4,label+" CPU/GPU strict interior");
        }
        ++cases;
      }
    }
    std::cout<<"deep sampling cases="<<cases<<" controls="<<controls<<" result="<<(ok?"PASS":"FAIL")<<'\n';
    return ok?0:1;
  }
  // Independent alpha oracle: constant byte 128 must remain exact across
  // fractional/projective mip blends. This does not depend on CoinGL clamping.
  using Alpha = CoinRenderAlphaTestFunction;
  for (unsigned filter = 0; filter < 4; ++filter) for (unsigned unit : {0u, 7u}) {
    auto frame = base;
    frame.clearColor = SbColor4f(0, 0, 0, 1);
    auto & image = frame.textures[0];
    image.width = image.height = 64; image.format = F::RGBA8_LINEAR;
    image.mipmapsRgba.clear(); image.mipmapped = false;
    image.pixelsRgba.assign(64 * 64 * 4, 0);
    for (size_t i = 0; i < 64 * 64; ++i) {
      image.pixelsRgba[i * 4] = 255; image.pixelsRgba[i * 4 + 3] = 128;
    }
    frame.samplers[0].filter = CoinRenderTextureFilter(filter);
    if (filter >= 2) ok &= check(Sampling::generate(image), "alpha authored mip chain");
    for (auto & vertex : frame.vertices) if (unit)
      for (unsigned c = 0; c < 2; ++c) vertex.extraTexcoords[unit - 1][c] = vertex.texcoord[c];
    for (auto & state : frame.renderStates) {
      state.transparencyType = SoGLRenderAction::BLEND;
      state.textureMatrix[0][3] = .3f;
      if (unit) {
        auto layer = coin_render_texture_unit(state, 0);
        state.hasTexture = false; state.extraTextures[unit - 1] = layer;
      }
    }
    for (Alpha function : {Alpha::EQUAL, Alpha::NOTEQUAL, Alpha::LESS, Alpha::GREATER, Alpha::GEQUAL}) {
      for (auto & state : frame.renderStates) {
        state.alphaTestFunction = function; state.alphaTestReference = 128.f / 255.f;
      }
      const bool visible = function == Alpha::EQUAL || function == Alpha::GEQUAL;
      const int expected[] = {visible ? 128 : 0, 0, 0};
      ok &= render(frame, expected, "portable alpha/filter-" + std::to_string(filter) +
          "/unit-" + std::to_string(unit) + "/function-" + std::to_string(unsigned(function)));
    }
  }
  const int srgbExpected[] = {55, 55, 55};
  ok &= render(base, srgbExpected, "SRGB 128 decodes to linear 55");
  auto plan = base;
  plan.textures[0].format = F::RGBA8_LINEAR;
  const int linearExpected[] = {128, 128, 128};
  ok &= render(
      plan, linearExpected,
      "identical bytes with linear interpretation cannot alias SRGB cache");
  for (F format : {F::RGBA8_LINEAR, F::RGBA8_SRGB, F::BC3_LINEAR, F::BC3_SRGB,
                   F::RGBA16_FLOAT}) {
    auto frame = base;
    auto &image = frame.textures[0];
    image.width = 4;
    image.height = 4;
    image.format = format;
    image.mipmapped = false;
    image.mipmapsRgba.clear();
    image.pixelsRgba.assign(Format::levelBytes(4, 4, format), 0);
    int expected[3] = {};
    if (format == F::RGBA16_FLOAT) {
      for (size_t i = 0; i < 16; ++i)
        for (int c = 0; c < 4; ++c)
          Format::write16(image.pixelsRgba.data() + i * 8 + c * 2,
                          c == 0   ? 0x4000
                          : c == 3 ? 0x3c00
                                   : 0x3800);
      for (auto &material : frame.materials)
        material.diffuse[0] = material.diffuse[1] = material.diffuse[2] = .25f;
      for (auto &state : frame.renderStates)
        state.textureModel = CoinRenderTextureModel::MODULATE;
      expected[0] = 128;
      expected[1] = expected[2] = 32;
    } else if (Format::compressed(format)) {
      // BC3 alpha=1, red endpoint 0xf800, all color selectors zero.
      image.pixelsRgba[0] = 255;
      image.pixelsRgba[9] = 0xf8;
      expected[0] = 255;
    } else {
      for (size_t i = 0; i < 16; ++i) {
        image.pixelsRgba[i * 4] = 128;
        image.pixelsRgba[i * 4 + 3] = 255;
      }
      expected[0] = Format::srgb(format) ? 55 : 128;
    }
    ok &= render(frame, expected,
                 "format " + std::to_string(uint32_t(format)) + " base");
    if (!Format::compressed(format))
      ok &= check(Sampling::generate(image), "generate typed mip chain");
    else {
      image.mipmapped = true;
      image.mipmapsRgba = image.pixelsRgba;
      image.mipmapsRgba.insert(image.mipmapsRgba.end(),
                               image.pixelsRgba.begin(),
                               image.pixelsRgba.end());
    }
    frame.samplers[0].filter = CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR;
    for (auto &vertex : frame.vertices) {
      vertex.texcoord[0] *= 128;
      vertex.texcoord[1] *= 128;
    }
    ok &=
        render(frame, expected,
               "format " + std::to_string(uint32_t(format)) + " minification");
  }
  // Four legacy equations remain the same after explicit RGB decoding.
  for (F format : {F::RGBA8_SRGB, F::BC3_LINEAR, F::BC3_SRGB})
    for (int model = 0; model < 4; ++model)
      for (unsigned unit : {0u, 3u}) {
        auto frame = base;
        auto &image = frame.textures[0];
        image.format = format;
        image.pixelsRgba.assign(Format::levelBytes(4, 4, format), 0);
        const float red = Format::compressed(format) ? 1.f : 0.2158605f;
        if (Format::compressed(format)) {
          image.pixelsRgba[0] = 255;
          image.pixelsRgba[9] = 0xf8;
        } else
          for (size_t i = 0; i < 16; ++i) {
            image.pixelsRgba[i * 4] = 128;
            image.pixelsRgba[i * 4 + 3] = 255;
          }
        const float primary[] = {.4f, .6f, .8f}, blend[] = {.2f, .3f, .4f},
                    texture[] = {red, 0, 0};
        for (auto &material : frame.materials)
          for (int c = 0; c < 3; ++c)
            material.diffuse[c] = primary[c];
        for (auto &state : frame.renderStates) {
          state.textureModel = CoinRenderTextureModel(model);
          for (int c = 0; c < 3; ++c)
            state.textureBlendColor[c] = blend[c];
          if (unit) {
            auto layer = coin_render_texture_unit(state, 0);
            state.hasTexture = false;
            state.extraTextures[unit - 1] = layer;
          }
        }
        int expected[3];
        for (int c = 0; c < 3; ++c) {
          float value = model == 0   ? primary[c] * texture[c]
                        : model == 1 ? texture[c]
                        : model == 2 ? texture[c]
                                     : primary[c] * (1 - texture[c]) +
                                           blend[c] * texture[c];
          expected[c] = int(std::lround(value * 255));
        }
        ok &= render(frame, expected,
                     "legacy model " + std::to_string(model) + " format " +
                         std::to_string(uint32_t(format)) + " unit " +
                         std::to_string(unit));
      }
  // GPU alpha blending uses undecoded SRGB/BC3 alpha, independently of RGB.
  for (F format : {F::RGBA8_SRGB, F::BC3_LINEAR, F::RGBA16_FLOAT}) {
    auto frame = base;
    auto &image = frame.textures[0];
    image.format = format;
    image.pixelsRgba.assign(Format::levelBytes(4, 4, format), 0);
    frame.clearColor = SbColor4f(0, 0, 0, 1);
    for (auto &state : frame.renderStates)
      state.transparencyType = SoGLRenderAction::BLEND;
    int expected[3] = {0, 0, 0};
    if (format == F::RGBA8_SRGB) {
      for (size_t i = 0; i < 16; ++i) {
        image.pixelsRgba[i * 4] = 128;
        image.pixelsRgba[i * 4 + 3] = 128;
      }
      expected[0] = 28;
    } else if (format == F::BC3_LINEAR) {
      image.pixelsRgba[0] = 128;
      image.pixelsRgba[9] = 0xf8;
      expected[0] = 128;
    } else {
      for (size_t i = 0; i < 16; ++i) {
        Format::write16(image.pixelsRgba.data() + i * 8, 0x3c00);
        Format::write16(image.pixelsRgba.data() + i * 8 + 6, 0x3800);
      }
      expected[0] = 128;
    }
    ok &= render(frame, expected,
                 "linear alpha in format " + std::to_string(uint32_t(format)));
  }
  scene.texture->enableCompressedTexture = TRUE;
  scene.quality->textureQuality = .8f;
  action.apply(scene.root);
  ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  capture->plan.textures[0].format == F::BC3_SRGB &&
                  capture->plan.textures[0].mipmapsRgba.size() == 32,
              "authored compression produces BC3 full chain");
  std::vector<uint8_t> published;
  captureTarget->readbackRGBA(published);
  std::vector<uint8_t> odd(3 * 5 * 4, 255);
  scene.texture->image.setValue(SbVec2s(3, 5), 4, odd.data());
  action.apply(scene.root);
  std::vector<uint8_t> retained;
  captureTarget->readbackRGBA(retained);
  ok &= check(action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                  retained == published,
              "unaligned compressed base rejects without publication");
  scene.texture->enableCompressedTexture = FALSE;
  action.apply(scene.root);
  ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  capture->plan.textures[0].width == 3 &&
                  capture->plan.textures[0].height == 5,
              "NPOT recovery preserves native extent");
  scene.quality->textureQuality = .9f;
  action.apply(scene.root);
  ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  capture->plan.samplers[0].maxAnisotropy == 16,
              "anisotropy capture has explicit limit");
  scene.quality->textureQuality = 0;
  action.apply(scene.root);
  ok &= check(capture->plan.textures.empty(),
              "advanced texture off has no uploads");
  scene.quality->textureQuality = .8f;
  action.apply(scene.root);
  ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  capture->plan.textures[0].mipmapped,
              "advanced texture reactivation");
  // One image may be shared by base-only and mip-filtered consumers.
  auto mixed = base;
  auto &shared = mixed.textures[0];
  shared.format = F::RGBA8_LINEAR;
  shared.pixelsRgba.assign(4 * 4 * 4, 255);
  for (unsigned y = 0; y < 4; ++y)
    for (unsigned x = 0; x < 4; ++x) {
      auto i = (y * 4 + x) * 4;
      shared.pixelsRgba[i] = ((x + y) & 1) ? 255 : 0;
      shared.pixelsRgba[i + 1] = 0;
      shared.pixelsRgba[i + 2] = ((x + y) & 1) ? 0 : 255;
    }
  ok &= check(Sampling::generate(shared), "shared image complete chain");
  mixed.samplers[0].filter = CoinRenderTextureFilter::NEAREST;
  mixed.samplers.push_back(CoinRenderSamplerSnapshot());
  mixed.samplers[1].filter = CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR;
  for (auto &state : mixed.renderStates) {
    auto layer = coin_render_texture_unit(state, 0);
    layer.samplerSlot = 1;
    layer.model = CoinRenderTextureModel::MODULATE;
    state.extraTextures[2] = layer;
  }
  for (auto &vertex : mixed.vertices) {
    for (int c = 0; c < 2; ++c) {
      vertex.texcoord[c] = vertex.texcoord[c] * 16 + .03f;
      vertex.extraTexcoords[2][c] = vertex.texcoord[c];
    }
  }
  const int mixedExpected[] = {0, 0, 128};
  ok &= render(
      mixed, mixedExpected,
      "base-only and mip consumer share one image without LOD contamination");
#ifdef HAVE_COIN_BGFX
  if (native && selectedRenderer == COIN_RENDER_RENDERER_OPENGL) {
    mixed.revision = ++revision;
    for (auto &draw : mixed.draws)
      draw.sourceRevision = mixed.revision;
    for (int replay = 0; replay < 2; ++replay) {
      auto result = native->getPimpl()->executeFrame(mixed);
      ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                  "BGFX GL mixed sampler replay status");
      std::vector<uint8_t> pixels;
      native->readbackRGBA(pixels);
      ok &= center(pixels, mixedExpected,
                   "BGFX GL mixed sampler replay color");
    }
    CoinRenderOptions directOptions = samplingTestOptions();
    directOptions.sceneTexture = COIN_RENDER_SCENE_TEXTURE_DIRECT;
    directOptions.renderer = selectedRenderer;
    std::unique_ptr<CoinRenderTarget> direct(
        samplingTestOffscreen(SbVec2i32(32, 32), directOptions));
    auto root = mixed;
    root.textures[0].pixelsRgba.clear();
    root.textures[0].mipmapsRgba.clear();
    root.textures[0].producerId = 1;
    CoinRenderRttProducer producer;
    producer.sourceRevision = ++revision;
    producer.size = SbVec2i32(4, 4);
    producer.format = F::RGBA8_LINEAR;
    producer.plan = base;
    producer.plan.revision = revision;
    producer.plan.textures[0] = shared;
    producer.plan.samplers[0].filter = CoinRenderTextureFilter::NEAREST;
    for (auto &draw : producer.plan.draws) draw.sourceRevision = revision;
    for (auto &viewport : producer.plan.viewports) {
      viewport.width = 4;
      viewport.height = 4;
    }
    CoinRenderRttPlan graph(COIN_RENDER_SCENE_TEXTURE_DIRECT);
    uint64_t id = 0;
    std::string diagnostic;
    ok &= check(graph.append(producer, id, diagnostic),
                "BGFX GL mixed direct graph " + diagnostic);
    CoinRenderRttExecution execution(direct->getPimpl().operator->(), directOptions);
    CoinRenderFramePlan resolved;
    auto result = execution.prepare(graph, root, resolved);
    ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                "BGFX GL mixed direct prepare " + result.diagnostic);
    if (result.status == CoinRenderBackendStatus::SUCCESS) {
      resolved.revision = ++revision;
      for (auto &draw : resolved.draws) draw.sourceRevision = resolved.revision;
      for (int replay = 0; replay < 2; ++replay) {
        result = direct->getPimpl()->executeFrame(resolved);
        ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                    "BGFX GL mixed direct consumer " + result.diagnostic);
        std::vector<uint8_t> pixels;
        direct->readbackRGBA(pixels);
        ok &= center(pixels, mixedExpected, "BGFX GL mixed direct color");
      }
    }
    CoinRenderRttPlan nested(COIN_RENDER_SCENE_TEXTURE_DIRECT);
    ok &= check(nested.append(producer, id, diagnostic),
                "BGFX GL nested mixed first producer " + diagnostic);
    CoinRenderRttProducer second;
    second.sourceRevision = ++revision;
    second.size = SbVec2i32(4, 4);
    second.format = F::RGBA8_LINEAR;
    second.plan = root;
    second.plan.revision = revision;
    for (auto &draw : second.plan.draws) draw.sourceRevision = revision;
    for (auto &viewport : second.plan.viewports) {
      viewport.width = 4;
      viewport.height = 4;
    }
    ok &= check(nested.append(second, id, diagnostic),
                "BGFX GL nested mixed second producer " + diagnostic);
    auto nestedRoot = base;
    nestedRoot.textures[0].pixelsRgba.clear();
    nestedRoot.textures[0].mipmapsRgba.clear();
    nestedRoot.textures[0].format = F::RGBA8_LINEAR;
    nestedRoot.textures[0].producerId = 2;
    nestedRoot.textures[0].mipmapped = false;
    nestedRoot.samplers[0].filter = CoinRenderTextureFilter::NEAREST;
    CoinRenderRttExecution nestedExecution(direct->getPimpl().operator->(), directOptions);
    CoinRenderFramePlan nestedResolved;
    result = nestedExecution.prepare(nested, nestedRoot, nestedResolved);
    ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                "BGFX GL nested mixed prepare " + result.diagnostic);
    if (result.status == CoinRenderBackendStatus::SUCCESS) {
      nestedResolved.revision = ++revision;
      for (auto &draw : nestedResolved.draws) draw.sourceRevision = revision;
      result = direct->getPimpl()->executeFrame(nestedResolved);
      ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                  "BGFX GL nested mixed consumer " + result.diagnostic);
      std::vector<uint8_t> pixels;
      direct->readbackRGBA(pixels);
      ok &= center(pixels, mixedExpected, "BGFX GL nested mixed color");
    }
  }
#endif
  // A long X footprint must retain Y stripes rather than blur both axes.
  auto stripes = base;
  auto &striped = stripes.textures[0];
  striped.format = F::RGBA8_LINEAR;
  striped.width = striped.height = 128;
  striped.pixelsRgba.assign(128 * 128 * 4, 255);
  for (unsigned y = 0; y < 128; ++y)
    for (unsigned x = 0; x < 128; ++x)
      for (unsigned c = 0; c < 3; ++c)
        striped.pixelsRgba[(y * 128 + x) * 4 + c] = ((y / 16) & 1) ? 255 : 0;
  ok &= check(Sampling::generate(striped), "anisotropic stripe chain");
  stripes.samplers[0].filter = CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR;
  for (auto &vertex : stripes.vertices)
    vertex.texcoord[0] = vertex.texcoord[0] * 8 + vertex.texcoord[1] * .25f;
  const auto contrast = [](const std::vector<uint8_t> &pixels) {
    int low = 255, high = 0;
    if (pixels.size() != 32 * 32 * 4)
      return -1;
    for (int y = 4; y < 28; ++y) {
      const int v = pixels[(y * 32 + 16) * 4];
      low = std::min(low, v);
      high = std::max(high, v);
    }
    return high - low;
  };
  for (unsigned limit : {1u, 2u, 4u, 8u, 16u}) {
    stripes.samplers[0].maxAnisotropy = limit;
    stripes.revision = ++revision;
    for (auto &draw : stripes.draws)
      draw.sourceRevision = stripes.revision;
    ok &= check(cpu.executeFrame(stripes).status ==
                    CoinRenderBackendStatus::SUCCESS,
                "CPU anisotropy limit");
    int cpuContrast = contrast(cpu.colorBuffer);
    if (limit == 1)
      ok &= check(cpuContrast <= 2, "isotropic stripe control is blurred");
    if (limit >= 8)
      ok &= check(cpuContrast > 100, "CPU anisotropy retains stripe contrast");
    if (native) {
      std::vector<uint8_t> before;
      native->readbackRGBA(before);
      auto result = native->getPimpl()->executeFrame(stripes);
#ifdef HAVE_COIN_BGFX
      if (limit > 1 && limit < 16) {
        std::vector<uint8_t> retained;
        native->readbackRGBA(retained);
        ok &= check(
            result.status == CoinRenderBackendStatus::UNSUPPORTED &&
                retained == before,
            "BGFX rejects unavailable anisotropy factor without publication");
        continue;
      }
#endif
      ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                  "GPU anisotropy limit " + result.diagnostic);
      std::vector<uint8_t> pixels;
      native->readbackRGBA(pixels);
      const int gpuContrast = contrast(pixels);
      std::cout << "anisotropy=" << limit << " CPUcontrast=" << cpuContrast
                << " GPUcontrast=" << gpuContrast << '\n';
      if (limit == 1)
        ok &= check(gpuContrast <= 2, "GPU isotropic control");
      if (limit >= 8)
        ok &=
            check(gpuContrast > 100, "GPU anisotropy retains stripe contrast");
    }
  }
  stripes.samplers[0].maxAnisotropy = 3;
  stripes.revision = ++revision;
  for (auto &draw : stripes.draws)
    draw.sourceRevision = stripes.revision;
  const auto prior = cpu.colorBuffer;
  ok &= check(cpu.executeFrame(stripes).status !=
                      CoinRenderBackendStatus::SUCCESS &&
                  prior == cpu.colorBuffer,
              "invalid anisotropy preserves publication");
  if (gpu) {
    CoinRenderOptions directOptions = samplingTestOptions();
    directOptions.sceneTexture = COIN_RENDER_SCENE_TEXTURE_DIRECT;
    directOptions.renderer = selectedRenderer;
    std::unique_ptr<CoinRenderTarget> direct(
        samplingTestOffscreen(SbVec2i32(32, 32), directOptions));
    auto hdrRoot = base;
    hdrRoot.textures[0].pixelsRgba.clear();
    hdrRoot.textures[0].mipmapsRgba.clear();
    hdrRoot.textures[0].producerId = 1;
    hdrRoot.textures[0].format = F::RGBA16_FLOAT;
    for (auto &material : hdrRoot.materials)
      material.diffuse[0] = material.diffuse[1] = material.diffuse[2] = .25f;
    for (auto &state : hdrRoot.renderStates)
      state.textureModel = CoinRenderTextureModel::MODULATE;
    CoinRenderRttPlan graph(COIN_RENDER_SCENE_TEXTURE_DIRECT);
    CoinRenderRttProducer producer;
    producer.sourceRevision = 123;
    producer.size = SbVec2i32(4, 4);
    producer.format = F::RGBA16_FLOAT;
    producer.plan = base;
    producer.plan = CoinRenderFramePlan();
    producer.plan.textureSamplingPolicy = samplingTestPolicy;
    producer.plan.clearColor = SbColor4f(2, .5f, .25f, 1);
    uint64_t id;
    std::string diagnostic;
    ok &= check(graph.append(producer, id, diagnostic), "HDR graph capture");
    for (auto &vertex : hdrRoot.vertices) {
      vertex.texcoord[0] *= 128;
      vertex.texcoord[1] *= 128;
    }
    for (auto &draw : hdrRoot.draws)
      draw.sourceRevision = ++revision;
    const int hdrExpected[] = {128, 32, 16};
    for (bool mips : {false, true}) {
      hdrRoot.textures[0].mipmapped = mips;
      hdrRoot.samplers[0].filter =
          mips ? CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR
               : CoinRenderTextureFilter::LINEAR;
      CoinRenderRttExecution execution(direct->getPimpl().operator->(),
                                       directOptions);
      CoinRenderFramePlan resolved;
      auto result = execution.prepare(graph, hdrRoot, resolved);
      ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                  "HDR direct prepare " + result.diagnostic);
      if (result.status == CoinRenderBackendStatus::SUCCESS) {
        auto submitted = direct->getPimpl()->executeFrame(resolved);
        ok &= check(submitted.status == CoinRenderBackendStatus::SUCCESS,
                    "HDR direct consumer " + submitted.diagnostic);
      }
      std::vector<uint8_t> pixels;
      direct->readbackRGBA(pixels);
      ok &= center(pixels, hdrExpected,
                   mips ? "HDR direct GPU mips"
                        : "HDR direct base retains values above 1");
    }
    // Nonconstant NPOT producer: exact area reduction must include column 2.
    auto npotRoot = base;
    npotRoot.textures[0].format = F::RGBA8_LINEAR;
    npotRoot.textures[0].pixelsRgba.clear();
    npotRoot.textures[0].producerId = 1;
    npotRoot.textures[0].width = 3;
    npotRoot.textures[0].height = 5;
    npotRoot.textures[0].mipmapped = true;
    npotRoot.samplers[0].filter = CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR;
    for (auto &vertex : npotRoot.vertices) {
      vertex.texcoord[0] *= 128;
      vertex.texcoord[1] *= 128;
    }
    auto oddProducer = producer;
    oddProducer.format = F::RGBA8_LINEAR;
    oddProducer.size = SbVec2i32(3, 5);
    oddProducer.sourceRevision = 456;
    oddProducer.plan = base;
    oddProducer.plan.revision = ++revision;
    for (auto &draw : oddProducer.plan.draws)
      draw.sourceRevision = oddProducer.plan.revision;
    for (auto &viewport : oddProducer.plan.viewports) {
      viewport.width = 3;
      viewport.height = 5;
    }
    auto &oddImage = oddProducer.plan.textures[0];
    oddImage.width = 3;
    oddImage.height = 5;
    oddImage.format = F::RGBA8_LINEAR;
    oddImage.pixelsRgba.assign(3 * 5 * 4, 255);
    for (unsigned y = 0; y < 5; ++y)
      for (unsigned x = 0; x < 3; ++x)
        for (unsigned c = 0; c < 3; ++c)
          oddImage.pixelsRgba[(y * 3 + x) * 4 + c] = x == 2 ? 255 : 0;
    oddProducer.plan.samplers[0].filter = CoinRenderTextureFilter::NEAREST;
    CoinRenderRttPlan oddGraph(COIN_RENDER_SCENE_TEXTURE_DIRECT);
    ok &= check(oddGraph.append(oddProducer, id, diagnostic),
                "NPOT producer graph");
    {
      CoinRenderRttExecution execution(direct->getPimpl().operator->(),
                                       directOptions);
      CoinRenderFramePlan resolved;
      std::vector<uint8_t> before;
      direct->readbackRGBA(before);
      const auto priorSerial = direct->getPimpl()->lastSubmissionSerial;
      auto result = execution.prepare(oddGraph, npotRoot, resolved);
#ifdef HAVE_COIN_BGFX
      ok &= check(result.status == CoinRenderBackendStatus::UNSUPPORTED &&
                      result.diagnostic.find("POT") != std::string::npos,
                  "BGFX refuses NPOT direct mips before producer publication");
      std::vector<uint8_t> after;
      direct->readbackRGBA(after);
      ok &= check(before == after &&
                      priorSerial == direct->getPimpl()->lastSubmissionSerial,
                  "BGFX NPOT mip rejection retains pixels/serial");
#else
      ok &= check(result.status == CoinRenderBackendStatus::SUCCESS,
                  "NPOT direct mip prepare " + result.diagnostic);
      if (result.status == CoinRenderBackendStatus::SUCCESS) {
        resolved.revision = ++revision;
        ok &= check(direct->getPimpl()->executeFrame(resolved).status ==
                        CoinRenderBackendStatus::SUCCESS,
                    "NPOT direct mip consumer");
      }
      std::vector<uint8_t> pixels;
      direct->readbackRGBA(pixels);
      const int expected[] = {85, 85, 85};
      ok &= center(pixels, expected,
                   "NPOT direct final mip independent area oracle");
#endif
    }
    CoinRenderRttPlan stagedHdr = graph;
    stagedHdr.mode = COIN_RENDER_SCENE_TEXTURE_STAGED;
    ok &= check(!stagedHdr.validate(hdrRoot, diagnostic),
                "HDR staged refuses lossy RGBA8 fallback");
    // Public authored RTT capture must select the same typed producer contract.
    Scene authored;
    authored.quality->textureQuality = .8f;
    authored.material->diffuseColor.setValue(.25f, .25f, .25f);
    auto *sceneTexture = new SoSceneTexture2;
    sceneTexture->type = SoSceneTexture2::RGBA16F;
    sceneTexture->size = SbVec2s(4, 4);
    sceneTexture->scene = new SoSeparator;
    sceneTexture->backgroundColor.setValue(2, .5f, .25f, 1);
    sceneTexture->model = SoSceneTexture2::MODULATE;
    sceneTexture->transparencyFunction = SoSceneTexture2::NONE;
    authored.root->replaceChild(authored.texture, sceneTexture);
    CoinRenderAction authoredAction(SbViewportRegion(32, 32));
    authoredAction.setRenderTarget(direct.get());
    authoredAction.apply(authored.root);
    ok &= check(authoredAction.getLastStatus() == CoinRenderAction::SUCCESS,
                "authored RGBA16F direct capture " +
                    std::string(authoredAction.getLastError().getString()));
    std::vector<uint8_t> authoredPixels;
    direct->readbackRGBA(authoredPixels);
    ok &=
        center(authoredPixels, hdrExpected, "authored HDR direct mip producer");
    sceneTexture->type = SoSceneTexture2::RGBA32F;
    authoredAction.apply(authored.root);
    std::vector<uint8_t> rejectedPixels;
    direct->readbackRGBA(rejectedPixels);
    ok &=
        check(authoredAction.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                  authoredPixels == rejectedPixels,
              "unsupported HDR32 preserves publication");
    sceneTexture->type = SoSceneTexture2::RGBA16F;
    authoredAction.apply(authored.root);
    ok &= check(authoredAction.getLastStatus() == CoinRenderAction::SUCCESS,
                "authored HDR recovery");
    authoredAction.setRenderTarget(nullptr);
#ifdef HAVE_COIN_WGPU_RUST_BRIDGE
    std::vector<uint8_t> before;
    direct->readbackRGBA(before);
    const auto serial = direct->getPimpl()->lastSubmissionSerial;
    coin_wgpu_inject_fault(COIN_WGPU_FAULT_RTT_MIP_ENCODING);
    {
      CoinRenderRttExecution execution(direct->getPimpl().operator->(),
                                       directOptions);
      CoinRenderFramePlan resolved;
      auto failure = execution.prepare(graph, hdrRoot, resolved);
      ok &= check(failure.status == CoinRenderBackendStatus::OUT_OF_MEMORY,
                  "mip encoding failure is reported before root publication");
    }
    std::vector<uint8_t> after;
    direct->readbackRGBA(after);
    ok &= check(before == after &&
                    serial == direct->getPimpl()->lastSubmissionSerial,
                "failed mip generation retains published pixels and serial");
    coin_wgpu_inject_fault(0);
    {
      CoinRenderRttExecution execution(direct->getPimpl().operator->(),
                                       directOptions);
      CoinRenderFramePlan resolved;
      auto recovery = execution.prepare(graph, hdrRoot, resolved);
      ok &= check(recovery.status == CoinRenderBackendStatus::SUCCESS &&
                      direct->getPimpl()->executeFrame(resolved).status ==
                          CoinRenderBackendStatus::SUCCESS,
                  "mip failure recovery");
    }
#endif
  }
  action.setRenderTarget(nullptr);
  std::cout << "Advanced textures controls=" << controls << " GPU=" << gpu
            << " result=" << (ok ? "PASS" : "FAIL") << '\n';
  return ok ? 0 : 1;
}
