#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderBumpProfileFixture.h"
#include "CoinRenderTexture2dStartupTrace.h"
#include <Inventor/SoInteraction.h>
#include <Inventor/SoPath.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoBoxHighlightRenderAction.h>
#include <Inventor/actions/SoLineHighlightRenderAction.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#ifdef COIN_HIGHLIGHT_RTT_X11
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#endif
#include <array>
#include <set>
#include <sstream>
#include <chrono>
namespace highlight_rtt {
using RGB = std::array<unsigned, 3>;
using Color = std::array<double, 3>;
bool check(bool ok, const std::string &label) {
  if (!ok) std::cerr << "highlight_rtt FAIL " << label << std::endl;
  return ok;
}
struct Config {
  unsigned samples = 1;
  CoinRenderRenderer renderer = COIN_RENDER_RENDERER_UNKNOWN;
  bool line = false, ancestral = false, window = false, portable = false, core = false, producerControl = false, directControl = false;
  CoinRenderSceneTextureMode route = COIN_RENDER_SCENE_TEXTURE_STAGED;
};
struct Frame { std::vector<uint8_t> rgba; std::vector<float> depth; uint64_t serial = 0; };
template<class Base> class CountAction : public Base {
public:
  unsigned calls = 0;
  void apply(SoNode *n) override { ++calls; Base::apply(n); }
  void apply(SoPath *p) override { ++calls; Base::apply(p); }
  void apply(const SoPathList &p, SbBool obey = FALSE) override { ++calls; Base::apply(p, obey); }
};
struct Scene {
  SoSeparator *root = new SoSeparator, *producer = new SoSeparator, *shared = new SoSeparator;
  SoSelection *selection = new SoSelection;
  SoSceneTexture2 *texture = new SoSceneTexture2;
  SoIndexedFaceSet *mesh = new SoIndexedFaceSet;
  SoMaterial *sourceMaterials[4]{};
  SoIndexedFaceSet *sourceShapes[4]{};
  SoPath *paths[2]{};
  SoSwitch *occluder = new SoSwitch;
  SoIndexedFaceSet *occluderShape = nullptr;
  SoDepthBuffer *sourceDepth = new SoDepthBuffer;
  bool edited = false, foreground = false;
  int selected = -1;
  explicit Scene(const Config &config) {
    root->ref(); producer->ref();
    auto camera = []() {
      auto *n = new SoOrthographicCamera;
      n->position.setValue(0, 0, 3); n->height = 2;
      n->nearDistance = 1; n->farDistance = 5; return n;
    };
    root->addChild(camera()); producer->addChild(camera());
    sourceDepth->test = TRUE; sourceDepth->write = TRUE;
    sourceDepth->function = SoDepthBuffer::LESS; sourceDepth->range.setValue(0,1);
    producer->addChild(sourceDepth);
    for (auto *owner : {root, producer}) {
      auto *model = new SoLightModel; model->model = SoLightModel::BASE_COLOR; owner->addChild(model);
    }
    const int32_t quad[] = {0,1,2,-1,0,2,3,-1};
    for (unsigned i = 0; i < 4; ++i) {
      auto *branch = new SoSeparator;
      sourceMaterials[i] = new SoMaterial; branch->addChild(sourceMaterials[i]);
      const float x0 = i % 2 ? 0 : -1.1f, x1 = i % 2 ? 1.1f : 0;
      const float y0 = i / 2 ? 0 : -1.1f, y1 = i / 2 ? 1.1f : 0;
      auto *coordinates = new SoCoordinate3;
      const SbVec3f p[] = {SbVec3f(x0,y0,0),SbVec3f(x1,y0,0),SbVec3f(x1,y1,0),SbVec3f(x0,y1,0)};
      coordinates->point.setValues(0,4,p); branch->addChild(coordinates);
      sourceShapes[i] = new SoIndexedFaceSet; sourceShapes[i]->coordIndex.setValues(0,8,quad);
      branch->addChild(sourceShapes[i]); producer->addChild(branch);
    }
    paint(false);
    texture->scene = producer; texture->size.setValue(32,32);
    texture->type = SoSceneTexture2::RGBA8; texture->transparencyFunction = SoSceneTexture2::NONE;
    texture->backgroundColor.setValue(.03f,.05f,.08f,1);
    texture->model = SoSceneTexture2::MODULATE;
    texture->wrapS = texture->wrapT = SoSceneTexture2::CLAMP;
    auto *quality = new SoComplexity; quality->textureQuality = .1f; root->addChild(quality);
    if (config.ancestral) root->addChild(texture); else shared->addChild(texture);
    auto *material = new SoMaterial; material->diffuseColor.setValue(1,1,1);
    material->transparency = 0; shared->addChild(material);
    auto *uv = new SoTextureCoordinate2;
    const SbVec2f tex[] = {SbVec2f(0,0),SbVec2f(1,0),SbVec2f(0,1)};
    uv->point.setValues(0,3,tex); shared->addChild(uv);
    auto *coordinates = new SoCoordinate3;
    const SbVec3f points[] = {SbVec3f(-.36f,-.34f,0),SbVec3f(.36f,-.34f,0),SbVec3f(-.36f,.38f,0)};
    coordinates->point.setValues(0,3,points); shared->addChild(coordinates);
    const int32_t tri[] = {0,1,2,-1}; mesh->coordIndex.setValues(0,4,tri); shared->addChild(mesh);
    for (unsigned i = 0; i < 2; ++i) {
      auto *branch = new SoSeparator; auto *move = new SoTranslation;
      move->translation.setValue(i ? .48f : -.48f,0,0);
      branch->addChild(move); branch->addChild(shared); selection->addChild(branch);
      paths[i] = new SoPath(selection); paths[i]->ref(); paths[i]->append(int(i)); paths[i]->append(1);
    }
    root->addChild(selection);
    auto *front = new SoSeparator;
    front->addChild(new SoTexture2); // Explicitly untextured, including ancestral RTT state.
    auto *blue = new SoMaterial; blue->diffuseColor.setValue(.08f,.12f,.88f); front->addChild(blue);
    auto *fc = new SoCoordinate3;
    const SbVec3f fp[] = {SbVec3f(-.43f,-.44f,.35f),SbVec3f(-.08f,-.44f,.35f),
                         SbVec3f(-.08f,.46f,.35f),SbVec3f(-.43f,.46f,.35f)};
    fc->point.setValues(0,4,fp); front->addChild(fc);
    occluderShape = new SoIndexedFaceSet; occluderShape->coordIndex.setValues(0,8,quad); front->addChild(occluderShape);
    occluder->addChild(front); occluder->whichChild = SO_SWITCH_NONE; root->addChild(occluder);
  }
  ~Scene() { paths[0]->unref(); paths[1]->unref(); root->unref(); producer->unref(); }
  RGB palette(unsigned i) const {
    const RGB a[] = {{{32,176,224}},{{208,48,80}},{{96,208,48}},{{232,160,56}}};
    const RGB b[] = {{{176,64,192}},{{40,216,120}},{{216,112,32}},{{64,96,232}}};
    return edited ? b[i] : a[i];
  }
  void paint(bool b) {
    edited = b;
    for (unsigned i = 0; i < 4; ++i) {
      const auto p = palette(i);
      sourceMaterials[i]->diffuseColor.setValue(p[0]/255.f,p[1]/255.f,p[2]/255.f);
      sourceMaterials[i]->transparency = 0;
    }
  }
  void select(int occurrence) {
    selection->deselectAll(); selected = occurrence;
    if (occurrence >= 0) selection->select(paths[occurrence]);
  }
};
class Audit : public bump_profile::Proxy {
public:
  using Proxy::Proxy;
  unsigned roots = 0;
  mutable CoinRenderRttPlan graph;
  mutable CoinRenderFramePlan captured;
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g, const CoinRenderFramePlan &f,
                                     const SbVec2i32 &size) const override {
    graph = g.inspection(f); captured = f;
    return Proxy::preflightRtt(g,f,size);
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f, CoinRenderTargetP &t) override {
    ++roots; return Proxy::submit(f,t);
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f, CoinRenderTargetP &t,
                                const CoinRenderFrameReuseDecision &r) override {
    ++roots; return Proxy::submit(f,t,r);
  }
};
double segmentDistance(double x, double y, double ax, double ay, double bx, double by) {
  const double dx=bx-ax,dy=by-ay;
  const double t=std::max(0.,std::min(1.,((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy)));
  return std::hypot(x-ax-t*dx,y-ay-t*dy);
}
struct Metrics { unsigned stroke=0,box=0,line=0,base[2]={0,0},outside=0,front=0,bad=0,depthBad=0; };
Metrics inspect(const Scene &scene,const Config &config,const Frame &frame,unsigned side,
                bool active,double width,uint16_t pattern,const Color &stroke,bool physical=false,bool report=true) {
  Metrics m;
  if(frame.rgba.size()!=size_t(side)*side*4 || (!physical && !config.window && frame.depth.size()!=size_t(side)*side)) {m.bad=1;return m;}
  const double pixel=2./side, margin=2.1*pixel, band=(width*.5+2.1)*pixel;
  const bool highlighted=active && scene.selected>=0 && pattern;
  for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
    const size_t p=size_t(y)*side+x; const double wx=(x+.5)*pixel-1,wy=1-(y+.5)*pixel;
    const double lx=wx-(scene.selected==1?.48:-.48);
    double contour=1e9;
    if(highlighted) {
      if(config.line) contour=std::min({segmentDistance(lx,wy,-.36,-.34,.36,-.34),
        segmentDistance(lx,wy,.36,-.34,-.36,.38),segmentDistance(lx,wy,-.36,.38,-.36,-.34)});
      else contour=std::min({segmentDistance(lx,wy,-.36,-.34,.36,-.34),segmentDistance(lx,wy,.36,-.34,.36,.38),
        segmentDistance(lx,wy,.36,.38,-.36,.38),segmentDistance(lx,wy,-.36,.38,-.36,-.34)});
    }
    const bool front=scene.foreground && wx>-.43+margin && wx<-.08-margin && wy>-.44+margin && wy<.46-margin;
    bool fullStroke=true;
    for(unsigned c=0;c<3;++c)fullStroke &= std::abs(frame.rgba[4*p+c]-255*stroke[c])<=3;
    if(fullStroke) {
      ++m.stroke;
      m.bad += !highlighted || contour>band || front;
      if(std::abs(lx)<.17 && std::abs(wy-.38)<band)++m.box;
      if(std::abs(lx)<.17 && std::abs(lx+wy-.02)/std::sqrt(2.)<band)++m.line;
    }
    if(front) {
      ++m.front;
      const Color blue{{.08,.12,.88}};
      for(unsigned c=0;c<3;++c)m.bad+=std::abs(frame.rgba[4*p+c]-255*blue[c])>3;
      m.bad+=frame.rgba[4*p+3]!=255;
      if(!physical&&!config.window)m.depthBad+=std::abs(frame.depth[p]-.4125)>2e-5;
      continue;
    }
    const bool nearFront=scene.foreground && wx>-.43-margin && wx<-.08+margin && wy>-.44-margin && wy<.46+margin;
    if(nearFront || (highlighted && contour<=band))continue;
    bool outside=true;
    for(unsigned lane=0;lane<2;++lane) {
      const double local=wx-(lane?.48:-.48),s=(local+.36)/.72,t=(wy+.34)/.72;
      const double edge=std::min({local+.36,wy+.34,(.02-local-wy)/std::sqrt(2.)});
      outside &= edge < -margin;
      if(edge<=margin || std::abs(local)<margin || std::abs(wy-.02)<margin)continue;
      ++m.base[lane]; const auto color=scene.palette((s>=.5?1u:0u)+(t>=.5?2u:0u));
      for(unsigned c=0;c<3;++c)m.bad+=std::abs(int(frame.rgba[4*p+c])-int(color[c]))>3;
      m.bad+=frame.rgba[4*p+3]!=255;
      if(!physical&&!config.window)m.depthBad+=std::abs(frame.depth[p]-.5)>2e-5;
    }
    if(outside) {
      ++m.outside;for(unsigned c=0;c<3;++c)m.bad+=frame.rgba[4*p+c]>3;
      m.bad+=frame.rgba[4*p+3]!=255;
      if(!physical&&!config.window)m.depthBad+=frame.depth[p]!=1;
    }
  }
  if(report)std::cout<<"highlight_rtt_image base="<<m.base[0]<<","<<m.base[1]<<" stroke="<<m.stroke
    <<" box="<<m.box<<" line="<<m.line<<" outside="<<m.outside<<" front="<<m.front
    <<" bad="<<m.bad<<" depth_bad="<<m.depthBad<<std::endl;
  return m;
}
bool imageValid(const Scene &scene,const Config &config,const Frame &frame,unsigned side,
                bool visible,double width,uint16_t pattern,const Color &color,bool physical=false,bool report=true) {
  const auto m=inspect(scene,config,frame,side,visible,width,pattern,color,physical,report);
  const bool active=visible && scene.selected>=0 && pattern;
  const bool valid=!m.bad&&!m.depthBad && m.base[0]>12&&m.base[1]>40&&m.outside>500 &&
               (!scene.foreground || m.front>50) && (!active ||
                (m.stroke>8 && (config.line ? m.line>4&&m.box==0 : m.box>4&&m.line==0)));
  return report ? check(valid,"independent current base/stroke/depth and occurrence witness") : valid;
}

// Inspection uses the captured graph; it never substitutes it for a rendered image.
bool graphDelivery(const Scene &scene,const Config &config,const CoinRenderRttPlan &graph,
                   const CoinRenderFramePlan &frame,bool active,bool resolved) {
  bool ok=check(graph.mode==config.route && graph.producers.size()==1,
                "two occurrences share exactly one authored RTT producer");
  if(graph.producers.size()!=1)return false;
  const auto &producer=graph.producers.front();
  const auto expectedPolicy=config.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE;
  std::cout<<"highlight_rtt_policy requested="<<expectedPolicy<<" consumer="<<frame.textureSamplingPolicy
    <<" producer="<<producer.plan.textureSamplingPolicy<<std::endl;
  ok &= check(frame.textureSamplingPolicy==expectedPolicy && producer.plan.textureSamplingPolicy==expectedPolicy,
              "authored sampling policy reaches both consumer and producer plans");
  ok &= check(!producer.cube && producer.size==SbVec2i32(32,32) &&
              producer.sourceRevision==scene.texture->getNodeId() &&
              producer.format==CoinRenderTextureFormat::RGBA8_LINEAR,
              "producer identity, size and RGBA8 route preserved");
  std::set<SbUniqueId> sourceIds;
  unsigned sourceIndices=0;
  for(const auto &draw:producer.plan.draws) {
    sourceIds.insert(draw.sourceNodeId); sourceIndices+=draw.geometry.indexCount;
    if(draw.renderStateSlot>=producer.plan.renderStates.size()){ok=false;continue;}
    const auto &state=producer.plan.renderStates[draw.renderStateSlot];
    ok &= check(draw.renderLayer==0 && !state.hasTexture,
                "producer has original geometry and no highlight or recursive texture");
  }
  std::set<SbUniqueId> expectedIds;
  for(auto *shape:scene.sourceShapes)expectedIds.insert(shape->getNodeId());
  ok &= check(sourceIndices==24 && sourceIds==expectedIds,
              "producer contains precisely the four asymmetric source quads");
  unsigned bases=0,strokes=0;
  std::set<uint64_t> resources;
  for(const auto &draw:frame.draws) {
    if(draw.renderStateSlot>=frame.renderStates.size()){ok=false;continue;}
    const auto &state=frame.renderStates[draw.renderStateSlot];
    if(draw.renderLayer) {
      ++strokes;
      bool textured=false;
      for(unsigned unit=0;unit<8;++unit)textured |= coin_render_texture_unit_enabled(state,unit);
      ok &= check(!textured,"highlight occurrence is untextured in every unit");
    } else if(draw.sourceNodeId==scene.mesh->getNodeId()) {
      ++bases;
      if(!check(state.hasTexture && state.textureImageSlot<frame.textures.size(),
                "each shared base occurrence consumes its RTT")){ok=false;continue;}
      const auto &image=frame.textures[state.textureImageSlot];
      ok &= check(image.width==32 && image.height==32 && image.components==4,
                  "base image extent and components");
      resources.insert(resolved ? image.gpuToken : image.producerId);
      if(!resolved)ok &= check(image.producerId!=0,"captured base retains logical producer");
      else if(config.route==COIN_RENDER_SCENE_TEXTURE_DIRECT)
        ok &= check(image.gpuToken!=0 && image.pixelsRgba.empty() && image.mipmapsRgba.empty(),
                    "DIRECT consumes GPU resource without intermediate CPU pixels");
      else {
        bool bytes=image.gpuToken==0 && image.pixelsRgba.size()==32*32*4;
        if(bytes)for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
          const auto rgb=scene.palette((x>=16?1:0)+(y>=16?2:0));
          const size_t p=4*(size_t(y)*32+x); // Captured STAGED payload is bottom-left.
          for(unsigned c=0;c<3;++c)bytes &= std::abs(int(image.pixelsRgba[p+c])-int(rgb[c]))<=3;
          bytes &= image.pixelsRgba[p+3]==255;
        }
        ok &= check(bytes,"STAGED entire producer pixels independently match authored quadrants");
      }
    }
  }
  ok &= check(bases==2 && resources.size()==1 && (active?strokes>0:strokes==0),
              "selected and unselected occurrences retain base resource and distinct stroke layer");
  return ok;
}

std::vector<SbUniqueId> sourceStamp(const Scene &scene) {
  std::vector<SbUniqueId> ids{scene.root->getNodeId(),scene.producer->getNodeId(),
    scene.texture->getNodeId(),scene.mesh->getNodeId(),scene.selection->getNodeId(),scene.shared->getNodeId()};
  for(auto *material:scene.sourceMaterials)ids.push_back(material->getNodeId());
  return ids;
}
unsigned phaseCount(const std::string &trace,const std::string &event) {
  unsigned count=0;size_t pos=0;
  while((pos=trace.find(event,pos))!=std::string::npos){++count;pos+=event.size();}
  return count;
}
unsigned completedPhases(const std::string &trace) {
#ifdef COIN_HIGHLIGHT_RTT_BGFX
  return phaseCount(trace,"COIN_RENDER_PHASE bgfx lower_ms=");
#else
  return phaseCount(trace,"COIN_RENDER_PHASE bridge pack_ms=");
#endif
}
unsigned completedWindowPhases(const std::string &trace) {
#ifdef COIN_HIGHLIGHT_RTT_BGFX
  (void)trace;return 0; // BGFX uses its common completion event for both targets.
#else
  return phaseCount(trace,"COIN_RENDER_PHASE wgpu_surface renderer=");
#endif
}
unsigned expectedConnectorPhases(const Config &config) {
#ifdef COIN_HIGHLIGHT_RTT_BGFX
  // DIRECT submitRtt is separate from the ordinary root completion event.
  return config.route==COIN_RENDER_SCENE_TEXTURE_STAGED?2u:1u;
#else
  // submitRtt calls ordinary submit on its offscreen texture-output child.
  // Surface submission uses a distinct event, outside the C++ offscreen branch.
  return config.window?1u:2u;
#endif
}
unsigned expectedWindowPhases(const Config &config) {
#ifdef COIN_HIGHLIGHT_RTT_BGFX
  (void)config;return 0;
#else
  return config.window?1u:0u;
#endif
}

// No context/device is queried here. The inspection backend stops the actual
// adapter capture at the public preflight boundary, before any execution.
class Inspection : public CoinRenderBackend {
public:
  mutable unsigned calls=0;
  unsigned submits=0;
  mutable CoinRenderRttPlan graph;
  mutable CoinRenderFramePlan frame;
  std::string error="independent inspection stops before execution";
  bool isGpuBackend()const override{return true;}
  CoinRenderBackendStatus getStatus()const override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderBackendStatus prepare(CoinRenderTargetP&)override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &g,const CoinRenderFramePlan &f,
                                     const SbVec2i32&)const override {
    ++calls;graph=g.inspection(f);frame=f;
    return {CoinRenderBackendStatus::UNSUPPORTED,error};
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan&,CoinRenderTargetP&)override {
    ++submits;return {CoinRenderBackendStatus::UNSUPPORTED,error};
  }
  void poll()override{}
  const std::string &getLastError()const override{return error;}
};

template<class Highlight> struct Run {
  Config config;
  Scene scene;
  Highlight highlight;
  SoRenderManager manager;
  std::unique_ptr<CoinRenderManagerAdapter> adapter;
  CoinRenderTarget *target=nullptr;
  Audit *audit=nullptr;
#ifdef COIN_HIGHLIGHT_RTT_X11
  Display *display=nullptr;
  Window window=0;
#endif
  std::vector<uint8_t> stablePhysical;
  unsigned side=96;
  bool visible=false;
  float width=3;
  uint16_t pattern=0xffff;
  Color color{{1,0,1}};
  Frame stable;
  explicit Run(const Config &c):config(c),scene(c) {
    manager.setSceneGraph(scene.root);
    manager.setViewportRegion(SbViewportRegion(side,side));
    manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
    manager.setStereoMode(SoRenderManager::MONO);
    manager.setRenderMode(SoRenderManager::AS_IS);
    manager.setBackgroundColor(SbColor4f(0,0,0,1));
    highlight.setTransparencyType(SoGLRenderAction::NONE);
    highlight.setColor(SbColor(1,0,1));highlight.setLineWidth(width);
    highlight.setLinePattern(pattern);highlight.setVisible(FALSE);
    manager.setGLRenderAction(&highlight);
    auto options=samplingTestOptions();options.sampleCount=c.samples;options.sceneTexture=c.route;
    if(c.renderer!=COIN_RENDER_RENDERER_UNKNOWN)options.renderer=c.renderer;
    options.textureSamplingPolicy=c.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE;
#ifdef COIN_HIGHLIGHT_RTT_X11
    if(c.window) {
      display=XOpenDisplay(nullptr);
      if(!display)return;
      window=XCreateSimpleWindow(display,DefaultRootWindow(display),16,16,side,side,0,0,0);
      XSetWindowAttributes attributes{};attributes.override_redirect=True;
      XChangeWindowAttributes(display,window,CWOverrideRedirect,&attributes);
      XMapRaised(display,window);XSync(display,False);
      CoinRenderNativeSurfaceDescriptor surface{};
      surface.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;surface.structSize=sizeof(surface);
      surface.type=COIN_RENDER_SURFACE_XLIB;
      surface.native.xlib.display=display;surface.native.xlib.window=window;
      adapter.reset(new CoinRenderManagerAdapter(manager,surface,SbVec2i32(side,side),options));
    } else
#endif
      adapter.reset(new CoinRenderManagerAdapter(manager,SbVec2i32(side,side),options));
    target=adapter->getSceneManager()->getRenderTarget();
  }
  ~Run(){
    adapter.reset();manager.setSceneGraph(nullptr);manager.setGLRenderAction(nullptr);
#ifdef COIN_HIGHLIGHT_RTT_X11
    if(window)XDestroyWindow(display,window);
    if(display)XCloseDisplay(display);
#endif
  }
  bool prepare() {
    if(!target)return false;
    auto &p=target->getPimpl().get();
    if(p.prepareBackend()!=CoinRenderBackendStatus::SUCCESS || !p.backend || !p.backend->isGpuBackend())return false;
    audit=new Audit(std::move(p.backend));p.backend.reset(audit);
    if(!config.window)target->setDepthReadbackEnabled(TRUE);
    const auto expectedPolicy=config.portable?COIN_RENDER_SAMPLING_PORTABLE:COIN_RENDER_SAMPLING_NATIVE;
    std::cout<<"highlight_rtt_target_policy requested="<<expectedPolicy
      <<" retained="<<target->getOptions().textureSamplingPolicy<<std::endl;
    return check(target->getOptions().sampleCount==config.samples && target->getOptions().sceneTexture==config.route &&
                 target->getOptions().textureSamplingPolicy==expectedPolicy,
                 "immutable public target sample, RTT and sampling options retained");
  }
  void style(bool show,float w,uint16_t bits,const Color &rgb) {
    visible=show;width=w;pattern=bits;color=rgb;
    highlight.setVisible(show);highlight.setLineWidth(w);highlight.setLinePattern(bits);
    highlight.setColor(SbColor(float(rgb[0]),float(rgb[1]),float(rgb[2])));
  }
  Frame read()const {
    Frame frame;target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
    frame.serial=target->getLastSubmissionSerial();return frame;
  }
  bool draw(const char *label,Metrics *metrics=nullptr) {
    const auto stamp=sourceStamp(scene);
    const unsigned roots=audit->roots,producers=audit->producers;
    const uint64_t previous=target->getLastSubmissionSerial();
    startup::Trace trace;if(!check(trace.start(),"passive phase capture"))return false;
    if(config.window && !check(target->requestWindowReadbackRGBA(),"explicit same-apply window readback request"))return false;
    const auto status=adapter->render();const auto events=trace.stop();
    const auto phases=completedPhases(events),windowPhases=completedWindowPhases(events);
    Frame current=read();
    std::cout<<"highlight_rtt "<<label<<" status="<<status<<" serial="<<previous<<"->"<<current.serial
      <<" root="<<audit->roots-roots<<" direct_producers="<<audit->producers-producers
      <<" completed_phases="<<phases<<" window_phases="<<windowPhases<<" error="<<adapter->getLastError().getString()<<std::endl;
    bool ok=check(status==CoinRenderAction::SUCCESS && current.serial>previous && audit->roots==roots+1,
                  "one current main publication and exactly one root submit");
    if(!ok)return false;
    ok &= check(sourceStamp(scene)==stamp && highlight.calls==0,"source and selected paths unchanged; no GL action fallback");
    ok &= check(audit->producers==producers+(config.route==COIN_RENDER_SCENE_TEXTURE_DIRECT?1:0),
                "exact DIRECT producer work");
    // Factory-owned STAGED children do not pass through the parent proxy.
    // Count their real completion events and the target-specific root event.
    ok &= check(phases==expectedConnectorPhases(config) && windowPhases==expectedWindowPhases(config),
                "exact completed child/root execution phases, no highlight replay producer");
    ok &= graphDelivery(scene,config,audit->graph,audit->plan,visible&&scene.selected>=0&&pattern!=0,true);
    ok &= imageValid(scene,config,current,side,visible,width,pattern,color);
    if(ok && config.window)ok &= observe(current);
    if(metrics)*metrics=inspect(scene,config,current,side,visible,width,pattern,color,false,false);
    if(ok)stable=std::move(current);
    return ok;
  }
  bool refuse(const char *label) {
    const auto stamp=sourceStamp(scene);
    const unsigned roots=audit->roots,producers=audit->producers;
    startup::Trace trace;if(!check(trace.start(),"passive refusal trace"))return false;
    const auto status=adapter->render();const auto events=trace.stop();
    const auto phases=completedPhases(events),windowPhases=completedWindowPhases(events);
    const auto current=read();
    std::cout<<"highlight_rtt_refuse "<<label<<" status="<<status<<" serial="<<current.serial
      <<" root="<<audit->roots-roots<<" producer="<<audit->producers-producers
      <<" phases="<<phases<<" window_phases="<<windowPhases<<" error="<<adapter->getLastError().getString()<<std::endl;
    return check(status!=CoinRenderAction::SUCCESS && current.serial==stable.serial &&
      current.rgba==stable.rgba && current.depth==stable.depth && audit->roots==roots &&
      audit->producers==producers && phases==0 && windowPhases==0 && sourceStamp(scene)==stamp && highlight.calls==0,
      "preflight refusal preserves exact publication and executes no producer/root") &&
      (!config.window || check(physical()==stablePhysical,"refusal preserves complete physical image bitwise"));
  }
  std::vector<uint8_t> physical()const {
    std::vector<uint8_t> result;
#ifdef COIN_HIGHLIGHT_RTT_X11
    XSync(display,False);
    XWindowAttributes attributes{};
    if(!XGetWindowAttributes(display,window,&attributes) || attributes.width!=int(side) || attributes.height!=int(side))return result;
    XImage *image=XGetImage(display,window,0,0,side,side,AllPlanes,ZPixmap);
    if(!image)return result;
    result.resize(size_t(side)*side*4);
    auto channel=[](unsigned long pixel,unsigned long mask) {
      if(!mask)return uint8_t(0);
      unsigned shift=0;while(!(mask&1)){mask>>=1;++shift;}
      return uint8_t((((pixel>>shift)&mask)*255+mask/2)/mask);
    };
    for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
      const auto pixel=XGetPixel(image,x,y);const size_t p=4*(size_t(y)*side+x);
      result[p]=channel(pixel,image->red_mask);result[p+1]=channel(pixel,image->green_mask);
      result[p+2]=channel(pixel,image->blue_mask);result[p+3]=255;
    }
    XDestroyImage(image);
#endif
    return result;
  }
  bool observe(const Frame &current) {
    const auto start=std::chrono::steady_clock::now();
    const auto deadline=start+std::chrono::seconds(5);
    unsigned attempts=0;unsigned maxError=0;
    do {
      Frame shown;shown.rgba=physical();++attempts;
      bool agrees=shown.rgba.size()==current.rgba.size();maxError=0;
      if(agrees)for(size_t p=0;p<current.rgba.size();p+=4) {
        for(unsigned c=0;c<3;++c) {
          const unsigned error=unsigned(std::abs(int(shown.rgba[p+c])-int(current.rgba[p+c])));
          maxError=std::max(maxError,error);agrees &= error<=3;
        }
        agrees &= shown.rgba[p+3]==255;
      }
      if(agrees && imageValid(scene,config,shown,side,visible,width,pattern,color,true,false)) {
        stablePhysical=std::move(shown.rgba);
        std::cout<<"highlight_rtt_present attempts="<<attempts<<" max_rgb="<<maxError
          <<" present_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<std::endl;
        return true;
      }
    } while(std::chrono::steady_clock::now()<deadline);
    std::cerr<<"highlight_rtt_present timeout attempts="<<attempts<<" last_max_rgb="<<maxError<<std::endl;
    return false;
  }
  bool resize(unsigned extent) {
#ifdef COIN_HIGHLIGHT_RTT_X11
    if(config.window){XResizeWindow(display,window,extent,extent);XSync(display,False);}
#endif
    side=extent;manager.setViewportRegion(SbViewportRegion(short(side),short(side)));
    return check(adapter->resize(SbVec2i32(side,side)),"same adapter/target explicit framebuffer resize");
  }
  bool exercise() {
    if(!draw("ordinary RTT predecessor"))return false;
    scene.select(0);style(true,3,0xffff,{{1,0,1}});
    Metrics solid,wide,stipple;
    if(!draw("selected shared occurrence",&solid))return false;
    style(true,3,0xffff,{{0,1,1}});if(!draw("stroke color mutation"))return false;
    style(true,3,0xffff,{{1,0,1}});if(!draw("stroke color restoration"))return false;
    style(true,5,0xffff,{{1,0,1}});if(!draw("stroke width mutation",&wide))return false;
    if(!check(wide.stroke>solid.stroke,"width changes measured untextured stroke coverage"))return false;
    style(true,3,0xffff,{{1,0,1}});if(!draw("stroke width restoration"))return false;
    style(true,3,0x00ff,{{1,0,1}});if(!draw("stipple mutation",&stipple))return false;
    if(!check(stipple.stroke<solid.stroke,"line pattern removes authored stroke fragments"))return false;
    style(true,3,0,{{1,0,1}});if(!draw("empty stroke pattern"))return false;
    style(true,3,0xffff,{{1,0,1}});if(!draw("pattern restoration"))return false;
    scene.paint(true);if(!draw("producer asymmetric mutation"))return false;
    scene.paint(false);if(!draw("producer restoration"))return false;
    style(false,3,0xffff,{{1,0,1}});if(!draw("highlight invisible"))return false;
    style(true,3,0xffff,{{1,0,1}});if(!draw("highlight visible again"))return false;
    scene.select(1);if(!draw("second shared path selected"))return false;
    scene.select(0);if(!draw("selection path restoration"))return false;
    scene.select(-1);if(!draw("deselected RTT base"))return false;
    scene.select(0);if(!draw("selection restoration"))return false;
    scene.foreground=true;scene.occluder->whichChild=SO_SWITCH_ALL;
    if(!draw("foreground occludes base and stroke"))return false;
    scene.foreground=false;scene.occluder->whichChild=SO_SWITCH_NONE;
    if(!draw("foreground restoration"))return false;
    style(true,std::numeric_limits<float>::quiet_NaN(),0xffff,{{1,0,1}});
    if(!refuse("nonfinite active style"))return false;
    style(true,3,0xffff,{{1,0,1}});if(!draw("style recovery"))return false;
    scene.sourceDepth->range.setValue(0,std::numeric_limits<float>::quiet_NaN());
    if(!refuse("invalid producer state"))return false;
    scene.sourceDepth->range.setValue(0,1);if(!draw("producer state recovery"))return false;
    scene.texture->size.setValue(0,32);if(!refuse("invalid producer descriptor"))return false;
    scene.texture->size.setValue(32,32);if(!draw("descriptor recovery"))return false;
    if(config.window) {
      for(unsigned extent:{129u,160u,96u})
        if(!resize(extent) || !draw("resized current physical/readback image"))return false;
    }
    return true;
  }
};

template<class Highlight> bool core(Config config) {
  config.samples=1;Run<Highlight> run(config);
  auto *inspection=new Inspection;
  run.target->getPimpl().get().backend.reset(inspection);
  auto capture=[&](bool active,const char *label) {
    const auto stamp=sourceStamp(run.scene);const unsigned before=inspection->calls;
    const auto status=run.adapter->render();
    std::cout<<"highlight_rtt_core "<<label<<" status="<<status<<" preflights="<<inspection->calls
      <<" error="<<run.adapter->getLastError().getString()<<std::endl;
    bool ok=check(status==CoinRenderAction::UNSUPPORTED && inspection->calls==before+1 &&
                  inspection->submits==0 && run.target->getLastSubmissionSerial()==0 &&
                  sourceStamp(run.scene)==stamp && run.highlight.calls==0,
                  "actual manager capture reaches inspection without device or execution");
    if(ok)ok &= graphDelivery(run.scene,config,inspection->graph,inspection->frame,active,false);
    return ok;
  };
  if(!capture(false,"ordinary graph"))return false;
  run.scene.select(0);run.style(true,3,0xffff,{{1,0,1}});
  if(!capture(true,"highlight shares producer"))return false;
  run.scene.select(1);if(!capture(true,"shared occurrence selection"))return false;
  run.scene.paint(true);if(!capture(true,"producer mutation"))return false;
  run.scene.paint(false);return capture(true,"producer restoration");
}
// Independent producer control: a separate process/target with no manager or
// highlight validates all four authored quadrants, including the quadrant not
// sampled by the main triangular UV domain. No warmup of the tested manager.
bool producerControl(const Config &config) {
  Scene scene(config);
  auto options=samplingTestOptions();options.renderer=config.renderer;
  options.sampleCount=1;options.sceneTexture=config.route;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),options));
  if(!check(bool(target),"independent source target"))return false;
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(32,32));
  action.setRenderTarget(target.get());action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(SbColor4f(.03f,.05f,.08f,1));
  bool ok=true;uint64_t previous=0;
  for(bool edited:{false,true,false}) {
    scene.paint(edited);const auto stamp=sourceStamp(scene);
    action.apply(scene.producer);
    Frame frame;target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
    frame.serial=target->getLastSubmissionSerial();
    unsigned bad=0,depthBad=0;
    if(frame.rgba.size()!=32*32*4 || frame.depth.size()!=32*32)bad=1;
    else for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
      const size_t pixel=size_t(y)*32+x;
      const auto expected=scene.palette((x>=16?1:0)+(y<16?2:0));
      for(unsigned c=0;c<3;++c)bad+=std::abs(int(frame.rgba[4*pixel+c])-int(expected[c]))>3;
      bad+=frame.rgba[4*pixel+3]!=255;
      depthBad+=std::abs(frame.depth[pixel]-.5)>2e-5;
    }
    std::cout<<"highlight_rtt_source edited="<<edited<<" status="<<action.getLastStatus()
      <<" serial="<<frame.serial<<" bad="<<bad<<" depth_bad="<<depthBad
      <<" error="<<action.getLastError().getString()<<std::endl;
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS && frame.serial>previous &&
                !bad&&!depthBad&&sourceStamp(scene)==stamp,"independent complete producer RGB/depth A/B/A");
    previous=frame.serial;
    if(!ok)break;
  }
  action.setRenderTarget(nullptr);return ok;
}
// A separate full-UV consumer witnesses the actual DIRECT token, rather than
// merely rendering the source. It never constructs the manager under test.
bool directProducerControl(Config config) {
  config.route=COIN_RENDER_SCENE_TEXTURE_DIRECT;config.samples=1;config.window=false;
  Scene scene(config);
  auto *consumer=new SoSeparator;consumer->ref();
  auto *camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,3);camera->height=2;camera->nearDistance=1;camera->farDistance=5;
  consumer->addChild(camera);
  auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;consumer->addChild(model);
  auto *quality=new SoComplexity;quality->textureQuality=.1f;consumer->addChild(quality);
  consumer->addChild(scene.texture);
  auto *white=new SoMaterial;white->diffuseColor.setValue(1,1,1);white->transparency=0;consumer->addChild(white);
  auto *uv=new SoTextureCoordinate2;
  const SbVec2f st[]={SbVec2f(0,0),SbVec2f(1,0),SbVec2f(1,1),SbVec2f(0,1)};
  uv->point.setValues(0,4,st);consumer->addChild(uv);
  auto *coordinates=new SoCoordinate3;
  const SbVec3f vertices[]={SbVec3f(-1,-1,0),SbVec3f(1,-1,0),SbVec3f(1,1,0),SbVec3f(-1,1,0)};
  coordinates->point.setValues(0,4,vertices);consumer->addChild(coordinates);
  auto *quad=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,-1,0,2,3,-1};
  quad->coordIndex.setValues(0,8,indices);consumer->addChild(quad);
  auto options=samplingTestOptions();options.renderer=config.renderer;
  options.sampleCount=1;options.sceneTexture=COIN_RENDER_SCENE_TEXTURE_DIRECT;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(32,32),options));
  if(!target){consumer->unref();return check(false,"DIRECT full-UV target");}
  auto &p=target->getPimpl().get();
  if(p.prepareBackend()!=CoinRenderBackendStatus::SUCCESS || !p.backend || !p.backend->isGpuBackend()) {
    consumer->unref();return check(false,"DIRECT control uses the actual GPU connector");
  }
  auto *audit=new Audit(std::move(p.backend));p.backend.reset(audit);
  target->setDepthReadbackEnabled(TRUE);
  CoinRenderAction action(SbViewportRegion(32,32));action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::NONE);action.setBackgroundColor(SbColor4f(0,0,0,1));
  startup::Environment tracing("COIN_RENDER_TRACE_PHASES","1");
  uint64_t previous=0;bool ok=true;
  for(bool edited:{false,true,false}) {
    scene.paint(edited);const auto stamp=sourceStamp(scene);const auto consumerId=consumer->getNodeId();
    const unsigned roots=audit->roots,producers=audit->producers;
    startup::Trace trace;
    if(!check(trace.start(),"DIRECT control passive phases")){ok=false;break;}
    action.apply(consumer);const auto events=trace.stop();
    Frame frame;target->readbackRGBA(frame.rgba);target->readbackDepth(frame.depth);
    frame.serial=target->getLastSubmissionSerial();
    unsigned bad=0,depthBad=0;unsigned quadrants[4]={0,0,0,0};
    if(frame.rgba.size()!=32*32*4 || frame.depth.size()!=32*32)bad=1;
    else for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
      const unsigned quadrant=(x>=16?1:0)+(y<16?2:0);++quadrants[quadrant];
      const auto expected=scene.palette(quadrant);const size_t pixel=size_t(y)*32+x;
      for(unsigned c=0;c<3;++c)bad+=std::abs(int(frame.rgba[4*pixel+c])-int(expected[c]))>3;
      bad+=frame.rgba[4*pixel+3]!=255;depthBad+=std::abs(frame.depth[pixel]-.5)>2e-5;
    }
    bool delivery=audit->graph.mode==COIN_RENDER_SCENE_TEXTURE_DIRECT && audit->graph.producers.size()==1;
    if(delivery) {
      const auto &producer=audit->graph.producers.front();
      delivery &= producer.sourceRevision==scene.texture->getNodeId() && producer.size==SbVec2i32(32,32);
      unsigned sourceIndices=0;std::set<SbUniqueId> sourceIds;
      for(const auto &draw:producer.plan.draws){sourceIndices+=draw.geometry.indexCount;sourceIds.insert(draw.sourceNodeId);}
      delivery &= sourceIndices==24 && sourceIds.size()==4;
      for(auto *shape:scene.sourceShapes)delivery &= sourceIds.count(shape->getNodeId())==1;
    }
    unsigned consumedIndices=0;std::set<uint64_t> tokens;
    for(const auto &draw:audit->plan.draws) {
      delivery &= draw.sourceNodeId==quad->getNodeId() && draw.renderLayer==0;
      consumedIndices+=draw.geometry.indexCount;
      if(draw.renderStateSlot>=audit->plan.renderStates.size()){delivery=false;continue;}
      const auto &state=audit->plan.renderStates[draw.renderStateSlot];
      if(!state.hasTexture || state.textureImageSlot>=audit->plan.textures.size()){delivery=false;continue;}
      const auto &image=audit->plan.textures[state.textureImageSlot];
      delivery &= image.width==32 && image.height==32 && image.components==4 && image.gpuToken!=0 &&
        image.pixelsRgba.empty() && image.mipmapsRgba.empty();
      tokens.insert(image.gpuToken);
    }
    delivery &= consumedIndices==6 && tokens.size()==1;
    const unsigned phases=completedPhases(events),surfaces=completedWindowPhases(events);
    std::cout<<"highlight_rtt_direct_source edited="<<edited<<" status="<<action.getLastStatus()
      <<" serial="<<previous<<"->"<<frame.serial<<" root="<<audit->roots-roots
      <<" producer="<<audit->producers-producers<<" phases="<<phases<<" surface_phases="<<surfaces
      <<" quadrants="<<quadrants[0]<<","<<quadrants[1]<<","<<quadrants[2]<<","<<quadrants[3]
      <<" bad="<<bad<<" depth_bad="<<depthBad<<" delivery="<<delivery
      <<" error="<<action.getLastError().getString()<<std::endl;
    ok &= check(action.getLastStatus()==CoinRenderAction::SUCCESS && frame.serial>previous &&
      audit->roots==roots+1 && audit->producers==producers+1 && delivery &&
      phases==expectedConnectorPhases(config) && surfaces==0 && !bad&&!depthBad &&
      quadrants[0]==256 && quadrants[1]==256 && quadrants[2]==256 && quadrants[3]==256 &&
      sourceStamp(scene)==stamp && consumer->getNodeId()==consumerId,
      "actual DIRECT token sampled over complete full-UV quad, authored source RGB/depth A/B/A");
    previous=frame.serial;
    if(!ok)break;
  }
  action.setRenderTarget(nullptr);consumer->unref();return ok;
}
} // namespace highlight_rtt

int main(int argc,char **argv) {
  using namespace highlight_rtt;
  Config config;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--core")config.core=true;
    else if(arg=="--producer-control")config.producerControl=true;
    else if(arg=="--producer-direct-control")config.directControl=true;
    else if(arg=="--box")config.line=false;
    else if(arg=="--line")config.line=true;
    else if(arg=="--ancestral")config.ancestral=true;
    else if(arg=="--window")config.window=true;
    else if(arg=="--direct")config.route=COIN_RENDER_SCENE_TEXTURE_DIRECT;
    else if(arg=="--staged")config.route=COIN_RENDER_SCENE_TEXTURE_STAGED;
    else if(arg=="--portable-sampling")config.portable=true;
    else if(arg=="--samples" && i+1<argc) {
      const std::string value=argv[++i];
      if(value=="1")config.samples=1;else if(value=="2")config.samples=2;else if(value=="4")config.samples=4;
      else {std::cerr<<"invalid sample count\n";return 2;}
    } else {std::cerr<<"unknown/incomplete option "<<arg<<std::endl;return 2;}
  }
#ifdef COIN_HIGHLIGHT_RTT_X11
  if(config.window)XInitThreads();
#else
  if(config.window){std::cerr<<"X11 support unavailable\n";return 77;}
#endif
  SoDB::init();SoInteraction::init();CoinRenderAction::initClass();
  if(config.core) {
    bool ok=true;
    for(bool line:{false,true})for(bool ancestral:{false,true})
      for(auto route:{COIN_RENDER_SCENE_TEXTURE_STAGED,COIN_RENDER_SCENE_TEXTURE_DIRECT}) {
        Config c=config;c.line=line;c.ancestral=ancestral;c.route=route;
        ok &= line?core<CountAction<SoLineHighlightRenderAction>>(c):core<CountAction<SoBoxHighlightRenderAction>>(c);
      }
    return ok?0:1;
  }
  CoinRenderCapabilities caps{};
  const int query=coin_render_query_capabilities(config.window?COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW:COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps));
  std::cout<<"highlight_rtt_environment query="<<query<<" gpu="<<caps.gpu_available
    <<" backend="<<caps.backend<<" renderer="<<caps.renderer<<" adapter="<<caps.adapter_name
    <<" diagnostic="<<caps.diagnostic<<std::endl;
  if(query!=0)return 1;
  if(!caps.gpu_available)return 77;
#ifdef COIN_HIGHLIGHT_RTT_X11
  if(config.window && !std::getenv("DISPLAY"))return 77;
#endif
  config.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  if(config.producerControl)return producerControl(config)?0:1;
  if(config.directControl)return directProducerControl(config)?0:1;
  if(config.line) {
    Run<CountAction<SoLineHighlightRenderAction>> run(config);
    if(!run.prepare())return 1;
    startup::Environment tracing("COIN_RENDER_TRACE_PHASES","1");
    return run.exercise()?0:1;
  }
  Run<CountAction<SoBoxHighlightRenderAction>> run(config);
  if(!run.prepare())return 1;
  startup::Environment tracing("COIN_RENDER_TRACE_PHASES","1");
  return run.exercise()?0:1;
}
