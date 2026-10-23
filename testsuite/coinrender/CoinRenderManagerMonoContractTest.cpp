#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderManagerMonoFixture.h"
#include <Inventor/SoInteraction.h>
#include <Inventor/SoPath.h>
#include <set>
namespace manager_mono {
SO_NODE_SOURCE(Viewport);
using Layer=SoRenderManager::Superimposition;
struct Run {
  Config config;World native,gl;Spy spy;Counts counts;Reference reference;ReferenceAction glAction;
  std::unique_ptr<CoinRenderManagerAdapter> adapter;
  CoinRenderTarget *target=nullptr;Audit *audit=nullptr;
  Display *display=nullptr;Window window=0;unsigned side=96;bool activeRtt=false;
  bool countProducerCapture=false,nativeIdentityProved=false;
  unsigned expectedProducers=1,expectedCaptures=1;
  unsigned admissionCaptures=0;
  SbVec2i32 expectedProducerSize=SbVec2i32(32,32);
  std::string preparationEvents;
  Image stable;std::vector<uint8_t> stablePhysical;
  struct Publication {std::string label;unsigned extent;uint64_t serial,fingerprint;};
  std::vector<Publication> publications;
  XWindowAttributes physicalAttributes{};bool physicalAttributesValid=false;
  static uint64_t rgbFingerprint(const std::vector<uint8_t> &pixels){
    uint64_t hash=14695981039346656037ULL;
    for(size_t p=0;p+3<pixels.size();p+=4)for(unsigned c=0;c<3;++c){hash^=pixels[p+c];hash*=1099511628211ULL;}
    return hash;
  }
  void rememberPublication(const Image &image,const char *label){
    if(publications.size()==8)publications.erase(publications.begin());
    publications.push_back({label,side,image.serial,rgbFingerprint(image.rgba)});
  }
  Run(const Config &c,const CoinRenderOptions &options):config(c),glAction(gl.manager){
    spy.setTransparencyType(SoGLRenderAction::BLEND);native.manager.setGLRenderAction(&spy);
    native.manager.addPreRenderCallback(pre,&counts);native.manager.addPostRenderCallback(post,&counts);
    if(c.window){display=XOpenDisplay(nullptr);if(!display)return;
      window=XCreateSimpleWindow(display,DefaultRootWindow(display),32,32,side,side,0,0,0);
      XSetWindowAttributes a{};a.override_redirect=True;XChangeWindowAttributes(display,window,CWOverrideRedirect,&a);
      XMapRaised(display,window);XSync(display,False);
      CoinRenderNativeSurfaceDescriptor surface{};surface.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
      surface.structSize=sizeof(surface);surface.type=COIN_RENDER_SURFACE_XLIB;
      surface.native.xlib.display=display;surface.native.xlib.window=window;
      adapter.reset(new CoinRenderManagerAdapter(native.manager,surface,SbVec2i32(side,side),options));
    }else adapter.reset(new CoinRenderManagerAdapter(native.manager,SbVec2i32(side,side),options));
    target=adapter->getSceneManager()->getRenderTarget();
  }
  ~Run(){reference.unbind();adapter.reset();native.manager.setGLRenderAction(nullptr);
    native.manager.removePreRenderCallback(pre,&counts);native.manager.removePostRenderCallback(post,&counts);
    if(window)XDestroyWindow(display,window);if(display)XCloseDisplay(display);}
  bool prepare(){if(!target)return false;auto &p=target->getPimpl().get();
    startup::Trace trace;if(!require(trace.start(),"passive preparation identity trace"))return false;
    const auto status=p.prepareBackend();preparationEvents=trace.stop();
    if(!require(status==CoinRenderBackendStatus::SUCCESS&&p.backend&&p.backend->isGpuBackend(),"real native backend"))return false;
    audit=new Audit(std::move(p.backend));p.backend.reset(audit);if(!config.window)target->setDepthReadbackEnabled(TRUE);
    return require(target->getOptions().sampleCount==config.samples&&target->getOptions().sceneTexture==config.route&&
      target->getOptions().textureSamplingPolicy==samplingTestPolicy,"immutable sample/RTT/sampling target policy");}
  void both(const std::function<void(World&)> &edit){edit(native);edit(gl);}
  Image read(){Image image;target->readbackRGBA(image.rgba);if(!config.window)target->readbackDepth(image.depth);
    image.serial=target->getLastSubmissionSerial();return image;}
  bool resize(unsigned n){side=n;both([&](World &w){w.manager.setViewportRegion(SbViewportRegion(side,side));});
    if(window){XResizeWindow(display,window,n,n);XSync(display,False);}return adapter->resize(SbVec2i32(n,n));}
  std::vector<uint8_t> physical(){std::vector<uint8_t> pixels;XSync(display,False);
    physicalAttributesValid=XGetWindowAttributes(display,window,&physicalAttributes)!=0;
    const auto &a=physicalAttributes;if(!physicalAttributesValid||a.width!=int(side)||a.height!=int(side))return pixels;
    XImage *im=XGetImage(display,window,0,0,side,side,AllPlanes,ZPixmap);if(!im)return pixels;
    auto channel=[](unsigned long p,unsigned long mask){unsigned shift=0;if(!mask)return uint8_t(0);
      while(!(mask&1)){mask>>=1;++shift;}return uint8_t((((p>>shift)&mask)*255+mask/2)/mask);};
    pixels.resize(size_t(side)*side*4);
    for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){const auto p=XGetPixel(im,x,y);const size_t i=4*(size_t(y)*side+x);
      pixels[i]=channel(p,im->red_mask);pixels[i+1]=channel(p,im->green_mask);pixels[i+2]=channel(p,im->blue_mask);pixels[i+3]=255;}
    XDestroyImage(im);return pixels;}
  bool observe(const Image &frame){const auto start=std::chrono::steady_clock::now(),end=start+std::chrono::seconds(5);
    std::vector<uint8_t> lastObserved;uint64_t firstFingerprint=0;
    unsigned attempts=0;do{auto pixels=physical();++attempts;bool same=pixels.size()==frame.rgba.size();
      if(attempts==1)firstFingerprint=rgbFingerprint(pixels);
      if(same)for(size_t p=0;p<pixels.size();p+=4)for(unsigned c=0;c<3;++c)same &= std::abs(int(pixels[p+c])-int(frame.rgba[p+c]))<=1;
      if(same){stablePhysical=std::move(pixels);std::cout<<"manager_mono_present attempts="<<attempts<<" ms="
        <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<std::endl;return true;}
      lastObserved=std::move(pixels);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }while(std::chrono::steady_clock::now()<end);
    // Failure-only observation: no new render, present, synchronization fence
    // or altered deadline. Fingerprints identify images, never decide PASS.
    const auto expectedFingerprint=rgbFingerprint(frame.rgba),observedFingerprint=rgbFingerprint(lastObserved);
    std::set<uint32_t> expectedColors,observedColors;unsigned changedPixels=0,changedChannels=0,reports=0,maxDelta=0;
    auto colors=[](const std::vector<uint8_t> &pixels,std::set<uint32_t> &out){
      for(size_t p=0;p+3<pixels.size();p+=4)out.insert((uint32_t(pixels[p])<<16)|(uint32_t(pixels[p+1])<<8)|pixels[p+2]);};
    colors(frame.rgba,expectedColors);colors(lastObserved,observedColors);
    if(lastObserved.size()==frame.rgba.size())for(size_t p=0;p<lastObserved.size();p+=4){bool different=false;
      for(unsigned c=0;c<3;++c){const unsigned delta=unsigned(std::abs(int(lastObserved[p+c])-int(frame.rgba[p+c])));
        maxDelta=std::max(maxDelta,delta);changedChannels+=delta>1;different|=delta>1;}
      changedPixels+=different;if(different&&reports<8){++reports;
        std::cout<<"manager_mono_present_difference xy="<<(p/4)%side<<","<<(p/4)/side<<" expected=";
        for(unsigned c=0;c<3;++c)std::cout<<(c?",":"")<<unsigned(frame.rgba[p+c]);
        std::cout<<" observed=";for(unsigned c=0;c<3;++c)std::cout<<(c?",":"")<<unsigned(lastObserved[p+c]);
        std::cout<<std::endl;}
    }
    std::cout<<"manager_mono_present_failure attempts="<<attempts<<" ms="
      <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()
      <<" expected_extent="<<side<<"x"<<side<<" expected_bytes="<<frame.rgba.size()<<" observed_bytes="<<lastObserved.size()
      <<" expected_serial="<<frame.serial<<" current_serial="<<target->getLastSubmissionSerial()
      <<" comparable="<<(lastObserved.size()==frame.rgba.size())<<" changed_pixels="<<changedPixels
      <<" changed_channels="<<changedChannels<<" max_delta="<<maxDelta
      <<" distinct_RGB_expected="<<expectedColors.size()<<" distinct_RGB_observed="<<observedColors.size()
      <<" fingerprint_expected="<<expectedFingerprint<<" fingerprint_first_observed="<<firstFingerprint
      <<" fingerprint_last_observed="<<observedFingerprint<<std::endl;
    const auto &a=physicalAttributes;
    std::cout<<"manager_mono_present_window id="<<window<<" attributes_valid="<<physicalAttributesValid
      <<" xy="<<a.x<<","<<a.y<<" extent="<<a.width<<"x"<<a.height<<" border="<<a.border_width<<" depth="<<a.depth
      <<" class="<<a.c_class<<" map_state="<<a.map_state<<" override_redirect="<<a.override_redirect
      <<" root="<<a.root<<" colormap="<<a.colormap<<" map_installed="<<a.map_installed<<std::endl;
    for(const auto &published:publications)std::cout<<"manager_mono_present_history label="<<published.label
      <<" serial="<<published.serial<<" extent="<<published.extent<<"x"<<published.extent
      <<" fingerprint="<<published.fingerprint<<" exact_observed_match="<<(published.extent==side&&published.fingerprint==observedFingerprint)
      <<std::endl;
    return require(false,"physical first current frame differs from same-apply root readback");}
  bool technicalCompare(const Image &current,const Image &expected,const char *label){
    const auto mode=native.manager.getRenderMode();if(mode==SoRenderManager::AS_IS)return compare(current,expected,side,label);
    // P02/P07 qualifies common stroke samples, not GL_LINES versus expanded
    // strip coverage/junctions. Keep full RGB/depth checks away from the
    // authored footprint plus a one-pixel boundary, while retaining every
    // interior foreground-layer pixel and the fixed common witnesses below.
    // The six technical modes here use the fixed authored quad/camera above;
    // this is a finite manager oracle, not a general shape raster classifier.
    const double pixel=2./side;
    const double pointSize=native.base.pointStyle&&!native.base.pointStyle->pointSize.isIgnored()?
      native.base.pointStyle->pointSize.getValue():1;
    // A point's authored footprint already has radius size/2. The boundary
    // is one complete pixel beyond that footprint, as for face interiors;
    // it is not half a pixel measured from the point's center or its edge.
    const double pointRadius=(mode==SoRenderManager::POINTS?pointSize*.5+1:1)*pixel;
    auto position=[&](unsigned x,unsigned y){return SbVec2f(float((x+.5)*pixel-1),float(1-(y+.5)*pixel));};
    auto pointBand=[&](double x,double y){return std::abs(std::abs(x)-.8)<=pointRadius&&std::abs(std::abs(y)-.8)<=pointRadius;};
    auto outerBand=[&](double x,double y){return
      (std::abs(std::abs(x)-.8)<=pixel&&std::abs(y)<=.8+pixel)||
      (std::abs(std::abs(y)-.8)<=pixel&&std::abs(x)<=.8+pixel);};
    auto strokeBand=[&](unsigned x,unsigned y){const auto p=position(x,y);
      if(mode==SoRenderManager::POINTS)return pointBand(p[0],p[1]);
      if(outerBand(p[0],p[1]))return true;
      return mode!=SoRenderManager::BOUNDING_BOX&&std::abs(p[0]-p[1])/std::sqrt(2.)<=pixel&&
        std::abs(p[0])<=.8+pixel&&std::abs(p[1])<=.8+pixel;};
    auto rgbRegion=[&](unsigned x,unsigned y){const auto p=position(x,y);
      if(!strokeBand(x,y))return true;
      // Preserve full material/texture/RTT comparison in every actual opaque
      // layer interior, even where it overlaps an unqualified base stroke.
      for(int i=0;i<native.manager.getNumSuperimpositions();++i){const auto *layer=native.manager.getSuperimposition(i);
        if(!layer->isEnabled()||(layer->getStateFlags()&Layer::BACKGROUND))continue;
        for(auto *q:{&native.foreground,&native.second})if(layer->getSceneGraph()==q->root&&q->geometry->whichChild.getValue()!=SO_SWITCH_NONE){
          const auto *points=q->coordinates->point.getValues(0);float x0=points[0][0],x1=x0,y0=points[0][1],y1=y0;
          for(int v=1;v<4;++v){x0=std::min(x0,points[v][0]);x1=std::max(x1,points[v][0]);
            y0=std::min(y0,points[v][1]);y1=std::max(y1,points[v][1]);}
          if(p[0]>x0+pixel&&p[0]<x1-pixel&&p[1]>y0+pixel&&p[1]<y1-pixel)return true;
        }
      }
      return false;};
    auto depthRegion=[&](unsigned x,unsigned y){return !strokeBand(x,y);};
    bool ok=compare(current,expected,side,label,rgbRegion,depthRegion);
    unsigned common=0,badRgb=0,badDepth=0,nativeOnly=0,glOnly=0,witnessReports=0;
    for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){const auto p=position(x,y);
      // Pixel centers on the outside half of a straight outer edge cannot
      // classify a fully filled face interior as a stroke. Exclude both the
      // authored corner footprint and its diagonal/junction neighborhood.
      const bool outsideFace=std::abs(p[0])>.8||std::abs(p[1])>.8;
      const bool diagonal=std::abs(p[0]-p[1])/std::sqrt(2.)<=pixel;
      // The authored 3px sprites provide four fixed, fully covered center
      // pixels. Partial unit-sprite coverage remains outside P02's parity.
      const unsigned low=unsigned(std::floor(side*.1)),high=unsigned(std::floor(side*.9));
      const bool pointCenter=(x==low||x==high)&&(y==low||y==high);
      const bool witness=mode==SoRenderManager::POINTS?pointCenter:
        outerBand(p[0],p[1])&&outsideFace&&!pointBand(p[0],p[1])&&!diagonal;
      if(!witness)continue;
      SbColor underlay=mode==SoRenderManager::HIDDEN_LINE?SbColor(0,0,0):SbColor(.04f,.08f,.8f);
      const bool a=!color(current,side,x,y,underlay),b=!color(expected,side,x,y,underlay);
      nativeOnly+=a&&!b;glOnly+=b&&!a;if(!a||!b)continue;++common;
      const size_t at=size_t(y)*side+x;
      if(mode==SoRenderManager::POINTS){const auto authored=native.base.material->diffuseColor[0];
        ok &= require(color(current,side,x,y,authored)&&color(expected,side,x,y,authored),
          "all four fixed point centers have fully covered authored color in native and CoinGL");
        if(!current.depth.empty())ok &= require(std::abs(current.depth[at]-.5f)<=2e-5f,"fixed point center has authored plane depth");}
      unsigned rgbErrors=0;for(unsigned c=0;c<3;++c)rgbErrors+=std::abs(int(current.rgba[4*at+c])-int(expected.rgba[4*at+c]))>4;
      const bool depthError=!current.depth.empty()&&!expected.depth.empty()&&std::abs(current.depth[at]-expected.depth[at])>2e-5f;
      if((rgbErrors||depthError)&&witnessReports<5){++witnessReports;
        std::cout<<"manager_mono_common_stroke_error mode="<<mode<<" xy="<<x<<","<<y<<" native=";
        for(unsigned c=0;c<3;++c)std::cout<<(c?",":"")<<unsigned(current.rgba[4*at+c]);
        std::cout<<" CoinGL=";for(unsigned c=0;c<3;++c)std::cout<<(c?",":"")<<unsigned(expected.rgba[4*at+c]);
        if(!current.depth.empty()&&!expected.depth.empty())std::cout<<" depth_native="<<current.depth[at]<<" depth_CoinGL="<<expected.depth[at];
        std::cout<<std::endl;
      }
      badRgb+=rgbErrors;badDepth+=depthError;
    }
    unsigned styles=0;bool depthOnly=false;
    for(const auto &draw:audit->plan.draws)if(draw.sourceNodeId==native.base.shape->getNodeId()){
      const auto &state=audit->plan.renderStates[draw.renderStateSlot];styles|=state.polygonOffsetPrimitiveStyle;
      depthOnly|=!state.colorWrite&&state.polygonOffsetPrimitiveStyle==1;
    }
    bool modeWitness=false;
    switch(mode){
      case SoRenderManager::POINTS:modeWitness=(styles&4)&&!(styles&3);break;
      case SoRenderManager::WIREFRAME:case SoRenderManager::BOUNDING_BOX:modeWitness=(styles&2)&&!(styles&1);break;
      case SoRenderManager::WIREFRAME_OVERLAY:case SoRenderManager::SHADED_HIDDEN_LINES:modeWitness=(styles&3)==3;break;
      case SoRenderManager::HIDDEN_LINE:modeWitness=(styles&2)&&depthOnly;break;
      default:break;
    }
    std::cout<<"manager_mono_qualified_strokes mode="<<mode<<" common="<<common<<" native_only="<<nativeOnly
      <<" CoinGL_only="<<glOnly<<" bad_rgb="<<badRgb<<" bad_depth="<<badDepth<<" primitive_styles="<<styles
      <<" depth_only="<<depthOnly<<std::endl;
    return require(common>=4&&!badRgb&&!badDepth&&modeWitness,
      "qualified common stroke RGB/depth and actual technical primitive mode")&&ok;
  }
  bool draw(const char *label,bool demandChange=false,bool technicalMsaa=false,bool stippleMsaa=false){
    reference.unbind();const unsigned roots=audit->roots,producers=audit->producers,preCount=counts.pre,postCount=counts.post;
    const unsigned captures=native.producerCaptures;
    const auto serial=target->getLastSubmissionSerial(),node=native.base.root->getNodeId();
    const unsigned sensors=counts.sensor;SoOneShotSensor delayed(sensor,&counts);delayed.setPriority(1);delayed.schedule();
    startup::Trace trace;if(!require(trace.start(),"passive execution trace"))return false;
    if(config.window&&!require(target->requestWindowReadbackRGBA(),"same-apply window readback"))return false;
    const auto status=adapter->render();const auto events=trace.stop();Image current=read();
    unsigned ordinary=0,surface=0,expectedOrdinary=0,expectedSurface=0;
#ifdef COIN_MANAGER_MONO_BGFX
    ordinary=occurrences(events,"COIN_RENDER_PHASE bgfx lower_ms=");
    expectedOrdinary=1+(activeRtt&&config.route==COIN_RENDER_SCENE_TEXTURE_STAGED?expectedProducers:0);
#else
    ordinary=occurrences(events,"COIN_RENDER_PHASE bridge pack_ms=");
    surface=occurrences(events,"COIN_RENDER_PHASE wgpu_surface renderer=");
    expectedOrdinary=(config.window?0:1)+(activeRtt?expectedProducers:0);expectedSurface=config.window?1:0;
#endif
    std::cout<<"manager_mono_frame label="<<label<<" status="<<status<<" serial="<<serial<<"->"<<current.serial
      <<" roots="<<audit->roots-roots<<" direct_producers="<<audit->producers-producers<<" execution="<<ordinary
      <<" surfaces="<<surface<<" producer_captures="<<native.producerCaptures-captures
      <<" error="<<adapter->getLastError().getString()<<std::endl;
    bool ok=require(status==CoinRenderAction::SUCCESS&&current.serial>serial&&audit->roots==roots+1,
      "exactly one native root submission/publication");
    ok &= require(counts.pre==preCount+1&&counts.post==postCount+1&&counts.sensor==sensors+1&&spy.calls==0&&node==native.base.root->getNodeId(),
      "one sensor/preparation/completion pair, source unchanged, no GL render action");
    ok &= require(audit->producers==producers+(activeRtt&&config.route==COIN_RENDER_SCENE_TEXTURE_DIRECT?expectedProducers:0)&&
      ordinary==expectedOrdinary&&surface==expectedSurface,"exact logical RTT/root executions, no technical replay");
    if(activeRtt){std::cout<<"manager_mono_producer_sizes expected="<<expectedProducerSize[0]<<"x"<<expectedProducerSize[1];
      for(const auto &size:audit->producerSizes)std::cout<<" actual="<<size[0]<<"x"<<size[1];std::cout<<std::endl;
      ok &= require(audit->producerSizes.size()==expectedProducers&&std::all_of(audit->producerSizes.begin(),audit->producerSizes.end(),
        [&](const SbVec2i32 &size){return size==expectedProducerSize;}),"actual logical producer extents follow authored resource size");
      unsigned samplers=0;bool isotropic=true;
      for(const auto &draw:audit->plan.draws)for(auto *q:{&native.base,&native.foreground,&native.second})
        if(q->rttSwitch->whichChild.getValue()!=SO_SWITCH_NONE&&draw.sourceNodeId==q->shape->getNodeId()){
          const auto &state=audit->plan.renderStates[draw.renderStateSlot];if(!state.hasTexture)continue;
          if(state.samplerSlot>=audit->plan.samplers.size()||state.textureImageSlot>=audit->plan.textures.size()){isotropic=false;continue;}
          const auto &sampler=audit->plan.samplers[state.samplerSlot];const auto &texture=audit->plan.textures[state.textureImageSlot];
          ++samplers;isotropic &= sampler.filter==CoinRenderTextureFilter::LINEAR&&sampler.maxAnisotropy==1&&
            sampler.wrapS==CoinRenderTextureWrap::REPEAT&&sampler.wrapT==CoinRenderTextureWrap::REPEAT&&!texture.mipmapped;
          std::cout<<"manager_mono_native_rtt_sampler source="<<draw.sourceNodeId<<" filter="<<unsigned(sampler.filter)
            <<" anisotropy="<<sampler.maxAnisotropy<<" wrap="<<unsigned(sampler.wrapS)<<","<<unsigned(sampler.wrapT)
            <<" mipmapped="<<texture.mipmapped<<std::endl;
        }
      ok &= require(samplers&&isotropic&&target->getOptions().maxTextureAnisotropy==1,
        "actual RTT consumers use requested isotropic LINEAR REPEAT sampler without mipmaps");
    }
    if(countProducerCapture)ok &= require(native.producerCaptures==captures+expectedCaptures,
      "authorial producer callback captures preserved without synthetic technical replay");
    if(config.glTraceRequired&&!nativeIdentityProved){nativeIdentityProved=physicalGlIdentity(preparationEvents+events);
      ok &= require(nativeIdentityProved,"actual accelerated native OpenGL driver identity, independent of unavailable PCI IDs");}
    if(config.wgpuGlTraceRequired&&!nativeIdentityProved){nativeIdentityProved=physicalWgpuGlIdentity(events,config);
      ok &= require(nativeIdentityProved,"completed wgpu OpenGL publication has actual named nonsoftware adapter and queried vendor");}
    if(!ok)return false;
    if(demandChange)ok &= require(changed(stable,current)>20,"authored edit materially changes current pixels");
    const unsigned samplerObservations=gl.base.rttSamplerObservations+gl.foreground.rttSamplerObservations+gl.second.rttSamplerObservations;
    Image expected;if(!reference.render(gl,glAction,side,config.samples,expected,config.renderer))return false;
    if(activeRtt)ok &= require(gl.base.rttSamplerValid&&gl.foreground.rttSamplerValid&&gl.second.rttSamplerValid&&
      gl.base.rttSamplerObservations+gl.foreground.rttSamplerObservations+gl.second.rttSamplerObservations>samplerObservations,
      "real CoinGL RTT sampler observed and normalized only to requested anisotropy one");
    if(stippleMsaa&&config.samples>1){const double pixel=2./side;
      auto interior=[&](unsigned x,unsigned y){const double px=(x+.5)*pixel-1,py=1-(y+.5)*pixel;
        const bool edge=(std::abs(px+.55)<=pixel&&py>=-.45-pixel&&py<=.55+pixel)||
          (std::abs(px-.35)<=pixel&&py>=-.45-pixel&&py<=.55+pixel)||
          (std::abs(py+.45)<=pixel&&px>=-.55-pixel&&px<=.35+pixel)||
          (std::abs(py-.55)<=pixel&&px>=-.55-pixel&&px<=.35+pixel);
        return !edge;};
      // P02's face interior profile excludes the fixed one-pixel geometric
      // boundary, not pixels selected by agreement or observed mask values.
      ok &= compare(current,expected,side,label,interior);
    }else ok &= technicalMsaa&&config.samples>1?technicalCompare(current,expected,label):compare(current,expected,side,label);
    if(config.window){ok &= observe(current);if(ok)rememberPublication(current,label);}
    if(ok)stable=std::move(current);return ok;
  }
  bool refuse(CoinRenderAction::Status expected,const char *label){
    reference.unbind();const unsigned roots=audit->roots,producers=audit->producers,p=counts.pre,q=counts.post;
    const unsigned captureCount=admissionCaptures;
    startup::Trace trace;if(!trace.start())return false;const auto status=adapter->render();const auto events=trace.stop();const auto now=read();
    std::cout<<"manager_mono_refusal label="<<label<<" status="<<status<<" serial="<<now.serial
      <<" admission_captures="<<admissionCaptures-captureCount<<" error="<<adapter->getLastError().getString()<<std::endl;
    bool ok=require(status==expected&&adapter->getLastError().getLength()>0&&now.serial==stable.serial&&now.rgba==stable.rgba&&
      now.depth==stable.depth&&audit->roots==roots&&audit->producers==producers&&counts.pre==p+1&&counts.post==q+1&&spy.calls==0,
      "transactional exact status/pixels/depth/serial, no root or producer work, paired finish");
    ok &= require(!occurrences(events,"COIN_RENDER_PHASE bgfx lower_ms=")&&!occurrences(events,"COIN_RENDER_PHASE bridge pack_ms=")&&
      !occurrences(events,"COIN_RENDER_PHASE wgpu_surface renderer="),"refusal executes no factory-owned producer");
    ok &= require(admissionCaptures==captureCount,"admission refusal does not traverse authorial capture callbacks");
    if(config.window)ok &= require(physical()==stablePhysical,"refusal preserves complete physical image");return ok;
  }
};

bool core(){
  World world;Spy spy;Counts counts;world.manager.setGLRenderAction(&spy);
  world.manager.addPreRenderCallback(pre,&counts);world.manager.addPostRenderCallback(post,&counts);
  CoinRenderManagerAdapter adapter(world.manager,SbVec2i32(96,96),samplingTestOptions());
  auto *target=adapter.getSceneManager()->getRenderTarget();auto *inspection=new Inspection;
  target->getPimpl().get().backend.reset(inspection);
  unsigned checks=0;
  auto capture=[&](const char *label){const unsigned before=inspection->captures,attempts=inspection->submits,p=counts.pre,q=counts.post;
    const auto result=adapter.render();++checks;
    return require(result==CoinRenderAction::UNSUPPORTED&&
      (inspection->captures-before)+(inspection->submits-attempts)==1&&
      counts.pre==p+1&&counts.post==q+1&&spy.calls==0&&target->getLastSubmissionSerial()==0,
      std::string(label)+" stops once at inspection without device/GL/publication and finishes exactly once");};
  auto shapeDraws=[&](SoNode *shape){std::vector<const CoinRenderDrawPacket*> found;
    for(const auto &draw:inspection->frame.draws)if(draw.sourceNodeId==shape->getNodeId())found.push_back(&draw);return found;};
  bool ok=true;
  world.add(world.background,Layer::BACKGROUND|Layer::ZBUFFERON);world.add(world.foreground,Layer::ZBUFFERON);
  ok &= capture("ordered layers");
  const auto bg=shapeDraws(world.background.shape),base=shapeDraws(world.base.shape),fg=shapeDraws(world.foreground.shape);
  ok &= require(!bg.empty()&&!base.empty()&&!fg.empty()&&bg.front()<base.front()&&base.front()<fg.front(),
    "BACKGROUND before base before foreground, source occurrences preserved");
  world.clear();world.add(world.empty,Layer::CLEARZBUFFER);world.add(world.foreground,Layer::ZBUFFERON);
  ok &= capture("empty explicit clear then preserved depth");
  const auto afterEmpty=shapeDraws(world.foreground.shape);
  ok &= require(!afterEmpty.empty()&&shapeDraws(world.empty.shape).empty(),
    "empty layer contributes no authored geometry and does not omit following layer");
  world.clear();world.add(world.foreground,0);world.add(world.second,Layer::ZBUFFERON);
  ok &= capture("depth flags restored between layers");
  for(auto *q:{&world.foreground,&world.second})for(const auto *d:shapeDraws(q->shape))
    ok &= require(inspection->frame.renderStates[d->renderStateSlot].depthTest==(q==&world.second),"layer depth-test flags remain independent");
  world.clear();world.add(world.foreground,Layer::ZBUFFERON);world.add(world.foreground,0);
  ok &= capture("same subtree authorial occurrences");
  const auto shared=shapeDraws(world.foreground.shape);
  ok &= require(shared.size()==2&&shared[0]->renderLayer!=shared[1]->renderLayer,
    "same subtree inserted twice retains two layer occurrences");
  world.clear();world.add(world.foreground,0);world.foreground.textures->whichChild=SO_SWITCH_ALL;
  world.foreground.image(0,0);world.foreground.image(1,1);world.manager.setTexturesEnabled(FALSE);
  world.manager.setRenderMode(SoRenderManager::WIREFRAME);
  ok &= capture("technical base and authored textured layer");
  for(const auto *d:shapeDraws(world.foreground.shape))ok &= require(
    d->topology==CoinRenderPrimitiveTopology::TRIANGLE_LIST&&inspection->frame.renderStates[d->renderStateSlot].hasTexture,
    "technical wireframe and base texture disable do not escape into layer");
  // Capability refusals must occur before the observation backend/RTT boundary.
  const unsigned captures=inspection->captures,attempts=inspection->submits;
  world.manager.setRGBMode(FALSE);const auto rejected=adapter.render();++checks;
  ok &= require(rejected==CoinRenderAction::UNSUPPORTED&&inspection->captures==captures&&inspection->submits==attempts&&spy.calls==0&&
    target->getLastSubmissionSerial()==0,"color-index refusal does not enter producer or root execution");
  world.manager.setRGBMode(TRUE);world.manager.setRenderMode(SoRenderManager::AS_IS);
  world.clear();world.manager.setTexturesEnabled(TRUE);
  // AUTOREDRAW observes authored changes and schedules the application's callback;
  // it must not render from the callback or execute the source GL action.
  world.manager.setRenderCallback(redraw,&counts);world.manager.activate();
  auto *automatic=world.add(world.foreground,Layer::AUTOREDRAW);
  SoDB::getSensorManager()->processDelayQueue(TRUE);counts.redraw=0;
  world.foreground.material->diffuseColor.setValue(.2f,.3f,.4f);
  SoDB::getSensorManager()->processDelayQueue(TRUE);++checks;
  ok &= require(counts.redraw==1&&spy.calls==0&&inspection->submits==attempts&&inspection->captures==captures,
    "AUTOREDRAW schedules one host callback without rendering");
  world.manager.removeSuperimposition(automatic);world.layers.clear();
  world.add(world.foreground,0);SoDB::getSensorManager()->processDelayQueue(TRUE);counts.redraw=0;
  world.foreground.material->diffuseColor.setValue(.3f,.2f,.4f);
  SoDB::getSensorManager()->processDelayQueue(TRUE);++checks;
  ok &= require(counts.redraw==0,"layer without AUTOREDRAW does not schedule host callback");
  world.manager.deactivate();world.manager.setRenderCallback(nullptr,nullptr);
  world.manager.removePreRenderCallback(pre,&counts);world.manager.removePostRenderCallback(post,&counts);
  world.manager.setGLRenderAction(nullptr);
  std::cout<<"manager_mono_core cases="<<checks<<" GL="<<spy.calls<<" inspection_submit_attempts="<<inspection->submits<<std::endl;
  return ok;
}

bool layers(Run &r){
  // Isolate each technical mode's manager semantics from previous point/line
  // junction coverage. Exact cross-API stroke junction history is outside the
  // R28 finite raster profiles; --peers independently qualifies preserved
  // depth history using discriminating filled silhouettes and no clear flag.
  r.both([](World &w){w.add(w.background,Layer::BACKGROUND|Layer::ZBUFFERON|Layer::CLEARZBUFFER);w.add(w.foreground,0);});
  for(auto mode:{SoRenderManager::AS_IS,SoRenderManager::WIREFRAME,SoRenderManager::POINTS,SoRenderManager::WIREFRAME_OVERLAY,
      SoRenderManager::HIDDEN_LINE,SoRenderManager::BOUNDING_BOX,SoRenderManager::SHADED_HIDDEN_LINES}){
    r.both([&](World &w){w.manager.setRenderMode(mode);
      if(r.config.samples>1&&mode==SoRenderManager::POINTS)w.base.authoredPointSize(3);
      else w.base.resetPointSize();});
    if(!r.draw(("technical-mode-"+std::to_string(mode)).c_str(),false,true))return false;
    if(!require(color(r.stable,r.side,r.side/2,r.side/2,SbColor(.04f,.8f,.08f)),"foreground stays authored filled green in every technical mode"))return false;
    if(!require(color(r.stable,r.side,2,2,mode==SoRenderManager::HIDDEN_LINE?SbColor(0,0,0):SbColor(.04f,.08f,.8f)),
      "background color survives layer clear; HIDDEN_LINE retains its distinct authored-base clear"))return false;
  }
  r.both([](World &w){w.manager.setRenderMode(SoRenderManager::AS_IS);w.clear();w.add(w.foreground,Layer::ZBUFFERON);});
  if(!r.draw("depth-on-behind")||!require(color(r.stable,r.side,r.side/2,r.side/2,SbColor(.8f,.08f,.04f)),"rear layer occluded by base depth"))return false;
  r.both([](World &w){w.clear();w.add(w.foreground,Layer::ZBUFFERON|Layer::CLEARZBUFFER);});
  if(!r.draw("explicit-clear",true)||!require(color(r.stable,r.side,r.side/2,r.side/2,SbColor(.04f,.8f,.08f)),"explicit clear reveals rear layer"))return false;
  r.both([](World &w){w.clear();});if(!r.draw("scene-before-terminal-empty-clear",true))return false;
  const auto beforeTerminal=r.stable;
  r.both([](World &w){w.add(w.empty,Layer::CLEARZBUFFER);});
  if(!r.draw("terminal-empty-layer-clear")||!require(r.stable.rgba==beforeTerminal.rgba,
    "terminal drawless layer preserves every scene color byte"))return false;
  if(!r.config.window&&!require(r.stable.depth.size()==size_t(r.side)*r.side&&
    std::all_of(r.stable.depth.begin(),r.stable.depth.end(),[](float depth){return std::abs(depth-1.f)<=2e-5f;}),
    "terminal drawless layer clears final depth even without a later draw"))return false;
  r.both([](World &w){w.clear();});if(!r.draw("terminal-empty-clear-removal"))return false;
  if(!require(r.stable.rgba==beforeTerminal.rgba&&r.stable.depth==beforeTerminal.depth,
    "removing terminal clear restores original scene depth and color on the same target"))return false;
  r.both([](World &w){w.clear();w.add(w.empty,Layer::CLEARZBUFFER);w.add(w.foreground,Layer::ZBUFFERON);});
  // Legacy SoRenderManager clears even a drawless layer. This GPU witness is
  // deliberately distinct from capture's no-leak rule: the clear occurs at the
  // empty layer itself, never at a later foreground scope.
  if(!r.draw("empty-layer-clear"))return false;
  r.both([](World &w){w.clear();w.add(w.empty,0);w.add(w.foreground,Layer::ZBUFFERON);});
  if(!r.draw("empty-layer-preserves-depth",true))return false;
  r.both([](World &w){w.clear();w.add(w.foreground,0);w.add(w.second,0);});
  if(!r.draw("insertion-green-yellow"))return false;
  const auto yellow=r.stable;
  r.both([](World &w){w.clear();w.add(w.second,0);w.add(w.foreground,0);});
  if(!r.draw("insertion-yellow-green",true))return false;
  if(!require(changed(yellow,r.stable)>100,"insertion order has an overlapping material witness"))return false;
  r.both([](World &w){w.clear();w.foreground.material->transparency=.5f;
    w.add(w.foreground,0)->setTransparencyType(SoGLRenderAction::BLEND);w.add(w.foreground,0)->setTransparencyType(SoGLRenderAction::BLEND);});
  if(!r.draw("shared-subtree-twice"))return false;
  const SbColor twice(.8f*.25f+.04f*.75f,.08f*.25f+.8f*.75f,.04f*.25f+.08f*.75f);
  if(!require(color(r.stable,r.side,r.side/2,r.side/2,twice),"same subtree blends twice, never deduplicated"))return false;
  r.both([](World &w){w.layers[1]->setEnabled(FALSE);});if(!r.draw("disable-one-occurrence",true))return false;
  r.both([](World &w){w.layers[1]->setEnabled(TRUE);});if(!r.draw("restore-occurrence",true))return false;
  const auto doubleBuffered=r.stable;
  for(bool value:{false,true}){r.both([&](World &w){w.manager.setDoubleBuffer(value);});
    if(!r.draw(value?"mono-doublebuffer-true":"mono-doublebuffer-false")||
      !require(r.stable.rgba==doubleBuffered.rgba&&r.stable.depth==doubleBuffered.depth,"MONO doubleBuffer has no framebuffer effect"))return false;}
  r.both([](World &w){w.clear();w.foreground.material->transparency=0;w.foreground.viewport->enabled=true;
    w.foreground.viewport->region=SbViewportRegion(96,96);w.foreground.viewport->region.setViewportPixels(11,17,53,47);
    w.add(w.foreground,0);w.add(w.second,Layer::ZBUFFERON);});
  if(!r.draw("layer-subviewport-restoration"))return false;
  r.both([](World &w){w.foreground.camera->position.setValue(.2f,-.1f,3);});if(!r.draw("layer-camera-edit",true))return false;
  if(!r.resize(128)||!r.draw("target-resize"))return false;
  r.both([](World &w){w.foreground.viewport->region=SbViewportRegion(128,128);
    w.foreground.viewport->region.setViewportPixels(29,7,71,79);});
  if(!r.draw("layer-viewport-edit",true))return false;
  // Override must change only the authored base; attached layers keep identity.
  r.adapter->setSceneGraphOverride(r.native.background.root);r.gl.manager.setSceneGraph(r.gl.background.root);
  if(!r.draw("host-base-override",true))return false;
  r.adapter->setSceneGraphOverride(nullptr);r.gl.manager.setSceneGraph(r.gl.base.root);
  if(!r.draw("host-base-override-restore",true))return false;
  r.both([](World &w){w.manager.setRGBMode(FALSE);});if(!r.refuse(CoinRenderAction::UNSUPPORTED,"color-index"))return false;
  r.both([](World &w){w.manager.setRGBMode(TRUE);w.manager.setStereoMode(SoRenderManager::SEPARATE_OUTPUT);});
  if(!r.refuse(CoinRenderAction::UNSUPPORTED,"R29-separate-output-preserved"))return false;
  r.both([](World &w){w.manager.setStereoMode(SoRenderManager::MONO);});
  if(!r.draw("same-manager-target-recovery"))return false;
  r.both([](World &w){w.foreground.camera->position.setValue(std::numeric_limits<float>::quiet_NaN(),0,3);});
  if(!r.refuse(CoinRenderAction::INVALID_SCENE,"nonfinite-layer-camera"))return false;
  r.both([](World &w){w.foreground.camera->position.setValue(.2f,-.1f,3);});
  if(!r.draw("invalid-authoring-recovery"))return false;
  // Observe admission before capture independently of root/GPU submission.
  // These callbacks are side effects, never an image or reference oracle.
  for(auto *quad:{&r.native.base,&r.native.background,&r.native.second,&r.native.foreground}){
    auto *probe=new SoCallback;probe->setCallback([](void *data,SoAction *action){
      if(action->isOfType(SoCallbackAction::getClassTypeId()))++*static_cast<unsigned*>(data);
    },&r.admissionCaptures);quad->root->insertChild(probe,0);
  }
  auto ownerReason=[&](){
    std::string reason=r.adapter->getLastError().getString();
    std::transform(reason.begin(),reason.end(),reason.begin(),[](unsigned char c){return char(std::tolower(c));});
    const bool clearOwner=reason.find("clear")!=std::string::npos&&reason.find("owner")!=std::string::npos;
    const bool colorLoad=reason.find("color")!=std::string::npos&&reason.find("load")!=std::string::npos;
    const bool colorBackground=reason.find("color")!=std::string::npos&&reason.find("background")!=std::string::npos;
    return require(clearOwner||colorLoad||colorBackground,"unsupported manager identifies missing clear/color LOAD ownership");
  };
  // In AS_IS, actuallyRender does not clear anything when scene==nullptr.
  // A background's depth therefore remains authoritative for foregrounds.
  r.both([](World &w){w.clear();w.manager.setRenderMode(SoRenderManager::AS_IS);
    w.background.setRect(-1.1f,1.1f,-1.1f,1.1f,.2f);
    w.foreground.setRect(-.55f,.35f,-.45f,.55f,-.4f);
    w.foreground.camera->position.setValue(0,0,3);w.foreground.viewport->enabled=false;
    w.add(w.background,Layer::BACKGROUND|Layer::ZBUFFERON|Layer::CLEARZBUFFER);
    w.add(w.foreground,Layer::ZBUFFERON);w.manager.setSceneGraph(nullptr);});
  if(!r.draw("null-base-preserves-background-depth",true)||
    !require(color(r.stable,r.side,r.side/2,r.side/2,SbColor(.04f,.08f,.8f)),
      "absent AS_IS base does not clear background depth to expose rear foreground"))return false;
  r.both([](World &w){w.layers.front()->setEnabled(FALSE);});
  if(!r.refuse(CoinRenderAction::UNSUPPORTED,"null-base-without-active-background-clear-owner"))return false;
  if(!ownerReason())return false;
  r.both([](World &w){w.layers.front()->setEnabled(TRUE);});
  if(!r.draw("null-base-background-owner-recovery")||
    !require(color(r.stable,r.side,r.side/2,r.side/2,SbColor(.04f,.08f,.8f)),
      "reactivated background restores supported layer-only publication"))return false;
  r.both([](World &w){w.manager.setSceneGraph(w.base.root);});
  if(!r.draw("null-base-restores-same-manager-target",true))return false;
  if(!require(color(r.stable,r.side,r.side/2,r.side/2,SbColor(.8f,.08f,.04f)),
    "restored base replaces the background at the center without exposing rear foreground"))return false;
  // The first REGISTERED background consumes clearwindow even when disabled.
  // A later enabled background or valid AS_IS base does not acquire that clear.
  r.both([](World &w){w.clear();w.add(w.background,Layer::BACKGROUND|Layer::ZBUFFERON)->setEnabled(FALSE);
    w.add(w.second,Layer::BACKGROUND|Layer::ZBUFFERON);w.add(w.foreground,Layer::ZBUFFERON);});
  if(!r.refuse(CoinRenderAction::UNSUPPORTED,"disabled-first-background-valid-base-later-background")||!ownerReason())return false;
  r.both([](World &w){w.layers.front()->setEnabled(TRUE);});
  if(!r.draw("first-background-reenabled-clear-owner-recovery"))return false;
  r.both([](World &w){w.layers.front()->setEnabled(FALSE);});
  if(!r.refuse(CoinRenderAction::UNSUPPORTED,"first-background-disabled-again")||!ownerReason())return false;
  r.both([](World &w){w.manager.removeSuperimposition(w.layers.front());w.layers.erase(w.layers.begin());});
  if(!r.draw("remove-disabled-first-background-next-owner-recovery",true))return false;
  r.both([](World &w){w.clear();w.add(w.foreground,Layer::ZBUFFERON);});
  if(!r.draw("no-registered-background-valid-base-owns-clear"))return false;
  r.both([](World &w){w.manager.setSceneGraph(nullptr);});
  if(!r.refuse(CoinRenderAction::UNSUPPORTED,"null-base-foreground-with-no-background-owner")||!ownerReason())return false;
  // HIDDEN_LINE clears color/depth before actuallyRender, including NULL base.
  // The foreground remains filled and owns its own state after that clear.
  r.both([](World &w){w.manager.setRenderMode(SoRenderManager::HIDDEN_LINE);});
  if(!r.draw("null-base-hidden-line-own-clear-foreground",true)||
    !require(color(r.stable,r.side,r.side/2,r.side/2,SbColor(.04f,.8f,.08f))&&
      color(r.stable,r.side,2,2,SbColor(0,0,0)),"NULL-base HIDDEN_LINE clears to black and renders authored filled foreground"))return false;
  r.both([](World &w){w.layers.front()->setEnabled(FALSE);});
  if(!r.draw("null-base-hidden-line-all-layers-disabled-own-clear",true)||
    !require(color(r.stable,r.side,r.side/2,r.side/2,SbColor(0,0,0))&&color(r.stable,r.side,2,2,SbColor(0,0,0)),
      "HIDDEN_LINE retains its own color clear with every registered layer disabled"))return false;
  if(!r.config.window&&!require(std::all_of(r.stable.depth.begin(),r.stable.depth.end(),
    [](float value){return std::abs(value-1.f)<=2e-5f;}),"HIDDEN_LINE clear-only frame has final depth one"))return false;
  r.both([](World &w){w.layers.front()->setEnabled(TRUE);});
  if(!r.draw("null-base-hidden-line-foreground-reenabled",true))return false;
  r.both([](World &w){w.manager.setRenderMode(SoRenderManager::AS_IS);});
  if(!r.refuse(CoinRenderAction::UNSUPPORTED,"null-base-AS-IS-after-hidden-line-no-color-owner")||!ownerReason())return false;
  r.both([](World &w){w.manager.setSceneGraph(w.base.root);});
  if(!r.draw("restore-base-after-hidden-line-owner-recovery",true))return false;
  if(!require(color(r.stable,r.side,r.side/2,r.side/2,SbColor(.8f,.08f,.04f)),
    "restored base reclaims the clear and recovers the same manager target"))return false;
  r.both([](World &w){w.clear();w.manager.setSceneGraph(nullptr);});
  if(!r.refuse(CoinRenderAction::INVALID_SCENE,"completely-empty-manager-remains-invalid"))return false;
  r.both([](World &w){w.manager.setSceneGraph(w.base.root);w.add(w.foreground,Layer::ZBUFFERON);});
  return r.draw("completely-empty-manager-base-recovery");
}

bool textures(Run &r){
  r.both([](World &w){w.base.textures->whichChild=SO_SWITCH_ALL;w.foreground.textures->whichChild=SO_SWITCH_ALL;
    w.base.material->diffuseColor.setValue(.8f,.6f,.9f);w.foreground.material->diffuseColor.setValue(.7f,.9f,.5f);
    w.add(w.background,Layer::BACKGROUND|Layer::ZBUFFERON);w.add(w.foreground,0);});
  auto upload=[&](unsigned rev,float quality,float repeats,bool clamp){
    for(World *w:{&r.native,&r.gl})for(Quad *q:{&w->base,&w->foreground}){
      q->quality->textureQuality=quality;
      for(unsigned u=0;u<2;++u){q->image(u,rev+u,r.config.srgb&&w==&r.gl);
        q->setUv(u,repeats);q->texture[u]->wrapS=q->texture[u]->wrapT=clamp?SoTexture2::CLAMP:SoTexture2::REPEAT;}
    }
  };
  upload(0,.1f,1.7f,false);if(!r.draw("asymmetric-two-unit-nearest"))return false;
  r.both([](World &w){w.manager.setTexturesEnabled(FALSE);});if(!r.draw("base-textures-disabled-overlay-retained",true))return false;
  for(auto mode:{SoRenderManager::WIREFRAME,SoRenderManager::HIDDEN_LINE,SoRenderManager::SHADED_HIDDEN_LINES}){
    r.both([&](World &w){w.manager.setRenderMode(mode);});if(!r.draw("technical-base-textured-overlay",false,true))return false;}
  r.both([](World &w){w.manager.setRenderMode(SoRenderManager::AS_IS);w.manager.setTexturesEnabled(TRUE);});
  upload(1,.3f,1.7f,true);if(!r.draw("linear-clamp-revised-image",true))return false;
  upload(2,.6f,37.3f,false);if(!r.draw("mip-repeat-minification",true))return false;
  upload(3,.82f,29.7f,true);if(!r.draw("trilinear-clamp-minification",true))return false;
  const auto opaqueModulate=r.stable;
  r.both([](World &w){w.foreground.combine->rgbOperation=SoTextureCombine::ADD;});if(!r.draw("combine-add-edit",true))return false;
  r.both([](World &w){w.foreground.combine->rgbOperation=SoTextureCombine::MODULATE;
    w.foreground.material->transparency=.4f;w.layers.back()->setTransparencyType(SoGLRenderAction::BLEND);});
  if(!r.draw("layer-material-alpha-override",true))return false;
  r.both([](World &w){w.layers.back()->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);});
  if(!r.draw("layer-transparency-policy-edit",true,false,true))return false;
  const auto stippled=r.stable;
  unsigned policyDraws=0;bool policy=true;
  for(const auto &draw:r.audit->plan.draws)if(draw.sourceNodeId==r.native.foreground.shape->getNodeId()){
    ++policyDraws;policy &= r.audit->plan.renderStates[draw.renderStateSlot].transparencyType==SoGLRenderAction::SCREEN_DOOR;
  }
  if(!require(policyDraws&&policy,"actual authored foreground packets retain SCREEN_DOOR policy"))return false;
  r.both([](World &w){w.layers.back()->setEnabled(FALSE);});if(!r.draw("stipple-underlying-scene-control",true))return false;
  const auto baseOnly=r.stable;
  auto matches=[](const Image &a,const Image &b,size_t p){for(unsigned c=0;c<3;++c)
    if(std::abs(int(a.rgba[4*p+c])-int(b.rgba[4*p+c]))>4)return false;return true;};
  unsigned covered=0,holes=0,bad=0,depthBad=0;
  const double pixel=2./r.side;
  for(unsigned y=0;y<r.side;++y)for(unsigned x=0;x<r.side;++x){const double px=(x+.5)*pixel-1,py=1-(y+.5)*pixel;
    if(px<=-.55+pixel||px>=.35-pixel||py<=-.45+pixel||py>=.55-pixel)continue;
    const size_t p=size_t(y)*r.side+x;
    const bool foreground=matches(stippled,opaqueModulate,p),background=matches(stippled,baseOnly,p);
    covered+=foreground&&!background;holes+=background&&!foreground;bad+=!foreground&&!background;
    if(!stippled.depth.empty())depthBad+=std::abs(stippled.depth[p]-.5f)>2e-5f;
  }
  std::cout<<"manager_mono_stipple_interior foreground="<<covered<<" holes="<<holes<<" bad_rgb="<<bad<<" bad_depth="<<depthBad<<std::endl;
  if(!require(covered>20&&holes>20&&!bad&&!depthBad,
    "SCREEN_DOOR face interior contains opaque texture samples and underlying scene, preserving base depth"))return false;
  r.both([](World &w){w.layers.back()->setEnabled(TRUE);});
  if(!r.draw("stipple-layer-enabled-recovery",true,false,true)||
    !require(r.stable.rgba==stippled.rgba&&r.stable.depth==stippled.depth,
      "re-enabled stippled layer restores the exact prior current publication"))return false;
  r.both([](World &w){w.foreground.material->transparency=0;w.foreground.alphaSwitch->whichChild=SO_SWITCH_ALL;
    w.manager.setRenderMode(SoRenderManager::HIDDEN_LINE);});
  if(!r.refuse(CoinRenderAction::UNSUPPORTED,"technical-alpha-test-remains-outside-contract"))return false;
  r.both([](World &w){w.foreground.alphaSwitch->whichChild=SO_SWITCH_NONE;w.manager.setRenderMode(SoRenderManager::AS_IS);
    w.layers.back()->setTransparencyType(SoGLRenderAction::BLEND);});
  return r.draw("textured-layer-recovery");
}

bool rtt(Run &r){
  for(auto *q:{&r.gl.base,&r.gl.foreground,&r.gl.second})q->isotropicRttReference();
  r.activeRtt=true;r.both([](World &w){w.producer.setRect(-2.2f,2.2f,-1.1f,1.1f,0);w.producer.gradient(0);
    w.foreground.material->diffuseColor.setValue(1,1,1);w.second.material->diffuseColor.setValue(1,1,1);
    w.foreground.rttSwitch->whichChild=SO_SWITCH_ALL;w.second.rttSwitch->whichChild=SO_SWITCH_ALL;
    // Same logical texture node reused in two authorial layers; pure producer.
    w.second.rttSwitch->replaceChild(w.second.rtt,w.foreground.rtt);
    w.add(w.background,Layer::BACKGROUND|Layer::ZBUFFERON);w.add(w.foreground,0);w.add(w.second,0);});
  for(auto mode:{SoRenderManager::AS_IS,SoRenderManager::WIREFRAME_OVERLAY,SoRenderManager::HIDDEN_LINE,SoRenderManager::SHADED_HIDDEN_LINES}){
    r.both([&](World &w){w.manager.setRenderMode(mode);w.manager.setTexturesEnabled(FALSE);});
    if(!r.draw("RTT-authorial-layers-technical-base",false,true))return false;
  }
  r.both([](World &w){w.manager.setRenderMode(SoRenderManager::AS_IS);w.producer.gradient(1);});
  if(!r.draw("RTT-source-revision",true))return false;
  r.both([](World &w){w.foreground.rtt->size.setValue(64,32);});r.expectedProducerSize=SbVec2i32(64,32);
  if(!r.draw("RTT-resource-size-revision",true))return false;
  r.both([](World &w){w.foreground.rtt->size.setValue(0,32);});if(!r.refuse(CoinRenderAction::INVALID_SCENE,"invalid-producer-size-before-execution"))return false;
  r.both([](World &w){w.foreground.rtt->size.setValue(32,32);});r.expectedProducerSize=SbVec2i32(32,32);
  if(!r.draw("RTT-size-recovery",true))return false;
  r.both([](World &w){w.manager.setRGBMode(FALSE);w.producer.gradient(2);});
  if(!r.refuse(CoinRenderAction::UNSUPPORTED,"manager-refusal-must-not-run-revised-producer"))return false;
  r.both([](World &w){w.manager.setRGBMode(TRUE);});if(!r.draw("RTT-revised-producer-recovery",true))return false;
  r.both([](World &w){w.foreground.rtt->type=SoSceneTexture2::DEPTH;});
  if(!r.refuse(CoinRenderAction::UNSUPPORTED,"depth-texture-color-consumer-capability"))return false;
  r.both([](World &w){w.foreground.rtt->type=SoSceneTexture2::RGBA8;w.foreground.rtt->model=SoSceneTexture2::MODULATE;
    w.foreground.material->diffuseColor.setValue(.8f,.5f,1);w.foreground.material->transparency=.35f;
    w.layers[1]->setTransparencyType(SoGLRenderAction::BLEND);});
  if(!r.draw("RTT-material-transparency-mix",true))return false;
  if(!r.resize(128)||!r.draw("RTT-resize"))return false;
  r.both([](World &w){w.layers[2]->setEnabled(FALSE);});if(!r.draw("RTT-disable-shared-occurrence",true))return false;
  // R23 control: callbacks are authorial effects, so synthetic technical
  // passes may reuse a capture, but distinct authored occurrences retain both
  // callback effects. Identical resulting payloads may share GPU execution.
  r.both([](World &w){w.clear();w.manager.setTexturesEnabled(TRUE);
    w.base.material->diffuseColor.setValue(1,1,1);w.base.rttSwitch->whichChild=SO_SWITCH_ALL;
    w.base.rtt->scene=w.producer.root;w.base.rtt->size.setValue(32,32);w.base.rtt->type=SoSceneTexture2::RGBA8;
    w.base.rtt->transparencyFunction=SoSceneTexture2::NONE;w.base.rtt->model=SoSceneTexture2::REPLACE;
    w.base.rtt->backgroundColor.setValue(.1f,.7f,.9f,1);
    auto *callback=new SoCallback;callback->setCallback([](void *p,SoAction *a){
      if(a->isOfType(SoCallbackAction::getClassTypeId()))++*static_cast<unsigned*>(p);
    },&w.producerCaptures);w.producer.root->insertChild(callback,0);
  });
  r.countProducerCapture=true;r.expectedProducers=1;r.expectedCaptures=1;
  for(auto mode:{SoRenderManager::AS_IS,SoRenderManager::WIREFRAME_OVERLAY}){
    r.both([&](World &w){w.manager.setRenderMode(mode);});
    if(!r.draw("RTT-base-callback-once-across-technical-passes",false,true))return false;
  }
  // The supported layer resource contract does not establish opaque alpha for
  // a GPU RTT token in hidden-line BASE geometry. Preserve that capability
  // boundary, including no child execution and no partial root publication.
  for(auto mode:{SoRenderManager::HIDDEN_LINE,SoRenderManager::SHADED_HIDDEN_LINES}){
    r.both([&](World &w){w.manager.setRenderMode(mode);});
    if(!r.refuse(CoinRenderAction::UNSUPPORTED,"RTT-base-hidden-line-opaque-proof-unavailable"))return false;
  }
  r.both([](World &w){w.manager.setRenderMode(SoRenderManager::AS_IS);});
  if(!r.draw("RTT-base-supported-recovery"))return false;
  r.both([](World &w){w.foreground.material->transparency=0;w.foreground.material->diffuseColor.setValue(1,1,1);
    w.foreground.rttSwitch->replaceChild(w.foreground.rtt,w.base.rtt);w.add(w.foreground,0);});
  r.expectedProducers=1;r.expectedCaptures=2;
  for(auto mode:{SoRenderManager::AS_IS,SoRenderManager::WIREFRAME_OVERLAY}){
    r.both([&](World &w){w.manager.setRenderMode(mode);});
    if(!r.draw("RTT-callback-distinct-authorial-occurrences",false,true))return false;
  }
  return true;
}
bool peers(const Config &config,const CoinRenderOptions &options){
  if(!require(!config.window,"peer history control uses two independent offscreen canvases"))return false;
  std::unique_ptr<startup::Environment> attachmentCache;
  if(config.samples==1)attachmentCache.reset(new startup::Environment("COIN_WGPU_ATTACHMENT_CACHE","1"));
  std::cout<<"manager_mono_peers samples="<<config.samples<<" attachment_cache_1x="<<(config.samples==1)<<std::endl;
  Run a(config,options),b(config,options);if(!a.prepare()||!b.prepare())return false;
  a.both([](World &w){w.base.setRect(-.8f,-.15f,-.45f,.45f,.8f);
    w.background.setRect(-1.1f,1.1f,-1.1f,1.1f,0);
    w.add(w.background,Layer::BACKGROUND|Layer::ZBUFFERON);});
  b.both([](World &w){w.base.setRect(.2f,.85f,-.45f,.45f,-.8f);
    w.base.material->diffuseColor.setValue(.8f,.7f,.04f);
    w.background.material->diffuseColor.setValue(.04f,.7f,.75f);
    w.background.setRect(-1.1f,1.1f,-1.1f,1.1f,0);
    w.add(w.background,Layer::BACKGROUND|Layer::ZBUFFERON);});
  auto untouched=[](Run &r,const char *label){const auto actual=r.read();
    return require(actual.rgba==r.stable.rgba&&actual.depth==r.stable.depth&&actual.serial==r.stable.serial,
      std::string(label)+" peer publication/history remains byte-exact without another render");};
  if(!a.draw("peer-A-initial-left-near"))return false;
  // BGFX has one process renderer runtime; its actual GL-context identity is
  // printed once by that runtime, not once per target. A proved it on its
  // first real submit; B still requires its own GPU backend and root submits.
  if(config.glTraceRequired)b.nativeIdentityProved=a.nativeIdentityProved;
  if(!b.draw("peer-B-initial-right-far")||!untouched(a,"A after B"))return false;
  a.both([](World &w){w.base.setRect(.15f,.8f,-.45f,.45f,.8f);});
  if(!a.draw("peer-A-history-left-near",true)||!untouched(b,"B after A"))return false;
  // At this point the old left silhouette rejects the background, while the
  // current base moved right. The resulting black hole is history, not empty
  // current geometry; B's earlier far silhouette must not replace that depth.
  if(!require(color(a.stable,a.side,a.side/4,a.side/2,SbColor(0,0,0)),
    "A prior near silhouette rejects background after interleaved B"))return false;
  b.both([](World &w){w.base.setRect(-.3f,.3f,.2f,.85f,.7f);});
  if(!b.draw("peer-B-far-history-then-upper-near",true)||!untouched(a,"A after B edit"))return false;
  if(!require(color(b.stable,b.side,b.side*3/4,b.side/2,SbColor(.04f,.7f,.75f)),
    "B prior far silhouette admits its own cyan background"))return false;
  a.both([](World &w){w.base.setRect(-.8f,-.15f,-.45f,.45f,.8f);});
  if(!a.draw("peer-A-history-right-near",true)||!untouched(b,"B after second A"))return false;
  if(!require(color(a.stable,a.side,a.side*3/4,a.side/2,SbColor(0,0,0)),
    "A history follows its own right silhouette rather than B upper silhouette"))return false;
  if(!a.resize(128)||!a.draw("peer-A-resize-fresh-attachments")||!untouched(b,"B after A resize"))return false;
  b.both([](World &w){w.base.setRect(-.3f,.3f,-.85f,-.2f,.7f);});
  if(!b.draw("peer-B-upper-history-survives-A-resize",true)||!untouched(a,"A after B post-resize frame"))return false;
  if(!require(color(b.stable,b.side,b.side/2,b.side/4,SbColor(0,0,0)),
    "B upper near depth survives unrelated target resize and publication"))return false;
  a.adapter.reset();a.target=nullptr;a.audit=nullptr;
  if(!untouched(b,"B after A adapter/target destruction"))return false;
  b.both([](World &w){w.base.setRect(-.3f,.3f,.2f,.85f,.7f);});
  if(!b.draw("peer-B-history-survives-A-destruction",true))return false;
  if(!require(color(b.stable,b.side,b.side/2,b.side*3/4,SbColor(0,0,0)),
    "B lower near depth survives peer adapter/target destruction"))return false;
  return true;
}
}
int main(int argc,char **argv){
  using namespace manager_mono;configureSamplingTest(argc,argv);Config config;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];
    if(arg=="--core")config.core=true;else if(arg=="--window")config.window=true;else if(arg=="--srgb")config.srgb=true;
    else if(arg=="--layers")config.profile="layers";else if(arg=="--textures")config.profile="textures";else if(arg=="--rtt")config.profile="rtt";
    else if(arg=="--peers")config.profile="peers";
    else if(arg=="--direct")config.route=COIN_RENDER_SCENE_TEXTURE_DIRECT;else if(arg=="--staged")config.route=COIN_RENDER_SCENE_TEXTURE_STAGED;
    else if(arg=="--portable-sampling"){}else if(arg=="--samples"&&i+1<argc){config.samples=unsigned(std::atoi(argv[++i]));
      if(config.samples!=1&&config.samples!=2&&config.samples!=4)return 2;}
    else{std::cerr<<"unknown/incomplete option "<<arg<<std::endl;return 2;}
  }
  XInitThreads();SoDB::init();SoInteraction::init();CoinRenderAction::initClass();Viewport::initClass();
  if(config.core)return core()?0:1;
  auto options=samplingTestOptions();options.sampleCount=config.samples;options.sceneTexture=config.route;options.maxTextureAnisotropy=1;
  if(config.srgb)options.storedTextureColorSpace=COIN_RENDER_TEXTURE_SRGB;
  startup::Environment glIdentityTrace("COIN_BGFX_TRACE_GL_ADAPTER","1");
  CoinRenderCapabilities caps{};const auto query=coin_render_query_capabilities_for_renderer(config.window?COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW:
    COIN_RENDER_EXPERIMENTAL_OFFSCREEN,options.renderer,&caps,sizeof(caps));
  std::cout<<"manager_mono_environment query="<<query<<" gpu="<<caps.gpu_available<<" renderer="<<caps.renderer<<" adapter="<<caps.adapter_name
    <<" vendor="<<caps.vendor_id<<" device="<<caps.device_id<<" diagnostic="<<caps.diagnostic<<std::endl;
  bool hardwareIds=caps.vendor_id&&caps.device_id;
#ifdef COIN_MANAGER_MONO_BGFX
  // BGFX's OpenGL caps can have no PCI IDs. Require its actual current-context
  // driver strings on the first real submission instead, including offscreen.
  config.glTraceRequired=caps.renderer==COIN_RENDER_RENDERER_OPENGL;
  hardwareIds=hardwareIds||config.glTraceRequired;
#else
  config.wgpuGlTraceRequired=caps.renderer==COIN_RENDER_RENDERER_OPENGL;
  if(config.wgpuGlTraceRequired)hardwareIds=caps.vendor_id&&acceleratedName(caps.adapter_name);
#endif
  if(!require(query==0&&caps.gpu_available&&caps.adapter_name[0]&&hardwareIds&&
    (config.glTraceRequired||acceleratedName(caps.adapter_name))&&
    (caps.renderer==COIN_RENDER_RENDERER_VULKAN||caps.renderer==COIN_RENDER_RENDERER_OPENGL)&&
    (options.renderer==COIN_RENDER_RENDERER_UNKNOWN||caps.renderer==unsigned(options.renderer)),"physical requested native renderer/adapter evidence"))return 1;
  options.renderer=static_cast<CoinRenderRenderer>(caps.renderer);
  config.renderer=options.renderer;config.vendor=caps.vendor_id;config.adapterName=caps.adapter_name;
  startup::Environment trace("COIN_RENDER_TRACE_PHASES","1");
  if(config.profile=="peers")return peers(config,options)?0:1;
  Run run(config,options);if(!run.prepare())return 1;
  const bool ok=config.profile=="layers"?layers(run):config.profile=="textures"?textures(run):rtt(run);
  std::cout<<"manager_mono_result profile="<<config.profile<<" samples="<<config.samples<<" window="<<config.window
    <<" srgb="<<config.srgb<<" passed="<<ok<<std::endl;return ok?0:1;
}
