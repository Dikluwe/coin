#ifndef COIN_RENDER_MANAGER_MONO_FIXTURE_H
#define COIN_RENDER_MANAGER_MONO_FIXTURE_H
#define GL_GLEXT_PROTOTYPES 1
#include "CoinRenderBumpProfileFixture.h"
#include "CoinRenderTexture2dStartupTrace.h"
#include "CoinRenderEglLifetimeFixture.h"
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/elements/SoTextureOverrideElement.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/sensors/SoSensorManager.h>
#include <Inventor/sensors/SoOneShotSensor.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <chrono>
#include <thread>
#include <functional>
#include <cstring>
#include <cctype>

namespace manager_mono {
// Reference-only coordinate convention; never read by native callback capture.
static bool upperLeftReference=false;
static unsigned referenceFramebufferHeight=0;
inline bool require(bool value,const std::string &message) {
  if(!value)std::cerr<<"manager_mono FAIL "<<message<<std::endl;
  return value;
}
struct Config {
  unsigned samples=1;
  bool core=false,window=false,srgb=false,glTraceRequired=false,wgpuGlTraceRequired=false;
  CoinRenderRenderer renderer=COIN_RENDER_RENDERER_UNKNOWN;
  uint32_t vendor=0;std::string adapterName;
  std::string profile="layers";
  CoinRenderSceneTextureMode route=COIN_RENDER_SCENE_TEXTURE_STAGED;
};
struct Image {std::vector<uint8_t> rgba;std::vector<float> depth;uint64_t serial=0;};
class Viewport : public SoNode {
  SO_NODE_HEADER(Viewport);
public:
  static void initClass(){SO_NODE_INIT_CLASS(Viewport,SoNode,"Node");}
  Viewport(){SO_NODE_CONSTRUCTOR(Viewport);}
  bool enabled=false;
  SbViewportRegion region;
  void callback(SoCallbackAction *a)override{if(enabled)SoViewportRegionElement::set(a->getState(),region);}
  void GLRender(SoGLRenderAction *a)override{if(enabled){
    auto observed=region;
    // clip control changes raster Y, but GL viewport API coordinates retain
    // their lower-left origin. Convert that API rectangle when raw readback
    // rows are interpreted directly as upper-left output rows.
    if(upperLeftReference){const auto p=region.getViewportOriginPixels(),s=region.getViewportSizePixels();
      observed.setViewportPixels(p[0],short(int(referenceFramebufferHeight)-p[1]-s[1]),s[0],s[1]);}
    SoViewportRegionElement::set(a->getState(),observed);
  }}
protected:~Viewport()override=default;
};
struct Quad {
  SoSeparator *root=new SoSeparator;
  Viewport *viewport=new Viewport;
  SoOrthographicCamera *camera=new SoOrthographicCamera;
  SoMaterial *material=new SoMaterial;
  SoMaterialBinding *gradientBinding=nullptr;
  SoDrawStyle *pointStyle=nullptr;
  SoCallback *rttSamplerProbe=nullptr;
  unsigned rttSamplerObservations=0;
  bool rttSamplerValid=true;
  SoComplexity *quality=new SoComplexity;
  SoSwitch *textures=new SoSwitch;
  SoTexture2 *texture[2]={new SoTexture2,new SoTexture2};
  SoTextureCoordinate2 *uv[2]={new SoTextureCoordinate2,new SoTextureCoordinate2};
  SoTextureCombine *combine=new SoTextureCombine;
  SoSwitch *rttSwitch=new SoSwitch;
  SoSceneTexture2 *rtt=new SoSceneTexture2;
  SoSwitch *geometry=new SoSwitch;
  SoCoordinate3 *coordinates=new SoCoordinate3;
  SoIndexedFaceSet *shape=new SoIndexedFaceSet;
  SoSwitch *alphaSwitch=new SoSwitch;
  SoAlphaTest *alpha=new SoAlphaTest;
  explicit Quad(const SbColor &color,float x0=-.8f,float x1=.8f,float y0=-.8f,float y1=.8f,float z=0) {
    root->ref();root->addChild(viewport);
    camera->position.setValue(0,0,3);camera->height=2;camera->nearDistance=1;camera->farDistance=5;
    root->addChild(camera);
    auto *model=new SoLightModel;model->model=SoLightModel::BASE_COLOR;root->addChild(model);
    material->diffuseColor=color;root->addChild(material);
    quality->textureQuality=.1f;root->addChild(quality);
    auto *t=new SoSeparator;
    for(unsigned i=0;i<2;++i){auto *unit=new SoTextureUnit;unit->unit=int(i);t->addChild(unit);
      // GL applies texture-environment state when Texture2 is declared;
      // follow SoTextureCombine's documented Combine-before-Image ordering.
      if(i)t->addChild(combine);
      texture[i]->model=SoTexture2::MODULATE;t->addChild(texture[i]);t->addChild(uv[i]);
    }
    auto *unit=new SoTextureUnit;unit->unit=0;t->addChild(unit);
    // Texture state must flow into the shape; a separator would discard it.
    auto *flow=new SoGroup;
    while(t->getNumChildren()){auto *n=t->getChild(0);flow->addChild(n);t->removeChild(0);}
    t->ref();t->unref();textures->addChild(flow);textures->whichChild=SO_SWITCH_NONE;
    root->addChild(textures);rttSwitch->addChild(rtt);rttSwitch->whichChild=SO_SWITCH_NONE;root->addChild(rttSwitch);
    root->addChild(uv[0]);
    alpha->function=SoAlphaTest::GREATER;alpha->value=.5f;
    alphaSwitch->addChild(alpha);alphaSwitch->whichChild=SO_SWITCH_NONE;root->addChild(alphaSwitch);
    setRect(x0,x1,y0,y1,z);geometry->addChild(coordinates);
    const int32_t indices[]={0,1,2,-1,0,2,3,-1};shape->coordIndex.setValues(0,8,indices);
    geometry->addChild(shape);geometry->whichChild=SO_SWITCH_ALL;root->addChild(geometry);
    setUv(0,1);setUv(1,1);
  }
  ~Quad(){root->unref();}
  Quad(const Quad&)=delete;
  void setRect(float x0,float x1,float y0,float y1,float z){const SbVec3f p[]={
    SbVec3f(x0,y0,z),SbVec3f(x1,y0,z),SbVec3f(x1,y1,z),SbVec3f(x0,y1,z)};coordinates->point.setValues(0,4,p);}
  void setUv(unsigned unit,float repeat){const SbVec2f p[]={SbVec2f(-.17f,-.11f),SbVec2f(repeat,-.11f),
    SbVec2f(repeat,repeat),SbVec2f(-.17f,repeat)};uv[unit]->point.setValues(0,4,p);}
  void authoredPointSize(float size){
    if(!pointStyle){pointStyle=new SoDrawStyle;pointStyle->style.setIgnored(TRUE);pointStyle->lineWidth.setIgnored(TRUE);
      pointStyle->linePattern.setIgnored(TRUE);pointStyle->linePatternScaleFactor.setIgnored(TRUE);root->insertChild(pointStyle,0);}
    pointStyle->pointSize=size;pointStyle->pointSize.setIgnored(FALSE);
  }
  void resetPointSize(){if(pointStyle)pointStyle->pointSize.setIgnored(TRUE);}
  void isotropicRttReference(){
    if(rttSamplerProbe)return;
    // SoSceneTexture2's legacy FBO unconditionally installs the driver's
    // maximum anisotropy. This reference explicitly matches the requested
    // isotropic native sampler, without changing filter, wrap, LOD or image.
    // It runs after the real GL producer/bind, never in the native world.
    rttSamplerProbe=new SoCallback;
    rttSamplerProbe->setCallback([](void *data,SoAction *action){
      auto &q=*static_cast<Quad*>(data);
      if(!action->isOfType(SoGLRenderAction::getClassTypeId())||q.rttSwitch->whichChild.getValue()==SO_SWITCH_NONE||
        SoTextureOverrideElement::getImageOverride(action->getState()))return;
      ++q.rttSamplerObservations;
      const auto *texture=static_cast<SoSceneTexture2*>(q.rttSwitch->getChild(0));
      const auto size=texture->size.getValue();
      GLint active=0,binding=0,width=0,height=0,minFilter=0,magFilter=0,wrapS=0,wrapT=0,base=0,max=0;
      GLfloat before=0,after=0,maximum=0,lodBias=0,minLod=0,maxLod=0;
      glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);
      glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
      const char *extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
      const bool anisotropy=extensions&&(std::strstr(extensions,"GL_EXT_texture_filter_anisotropic")||
        std::strstr(extensions,"GL_ARB_texture_filter_anisotropic"));
      if(binding){
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
        glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&minFilter);
        glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&magFilter);
        glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&wrapS);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&wrapT);
        glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&base);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,&max);
        glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_LOD_BIAS,&lodBias);
        glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MIN_LOD,&minLod);glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LOD,&maxLod);
        if(anisotropy){glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,&maximum);
          glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,&before);
          glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,1);
          glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,&after);}
      }
      // Query again to prove normalization did not change the other sampler
      // parameters. GL errors remain for Reference::render's existing gate.
      GLint minAfter=0,magAfter=0,sAfter=0,tAfter=0;
      GLfloat biasAfter=0,minLodAfter=0,maxLodAfter=0;
      if(binding){glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&minAfter);
        glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&magAfter);
        glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&sAfter);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&tAfter);
        glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_LOD_BIAS,&biasAfter);
        glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MIN_LOD,&minLodAfter);glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LOD,&maxLodAfter);}
      glActiveTexture(GLenum(active));
      const bool valid=binding&&width==size[0]&&height==size[1]&&minFilter==GL_LINEAR&&magFilter==GL_LINEAR&&
        wrapS==GL_REPEAT&&wrapT==GL_REPEAT&&minFilter==minAfter&&magFilter==magAfter&&wrapS==sAfter&&wrapT==tAfter&&
        lodBias==biasAfter&&minLod==minLodAfter&&maxLod==maxLodAfter&&anisotropy&&
        std::isfinite(before)&&before>=1&&before<=maximum&&after==1;
      q.rttSamplerValid &= valid;
      std::cout<<"manager_mono_reference_rtt_sampler texture="<<binding<<" size="<<width<<"x"<<height
        <<" filter="<<minFilter<<","<<magFilter<<" wrap="<<wrapS<<","<<wrapT<<" levels="<<base<<","<<max
        <<" lod="<<lodBias<<","<<minLod<<","<<maxLod<<" anisotropy="<<before<<"->"<<after
        <<" maximum="<<maximum<<" valid="<<valid<<std::endl;
    },this);
    root->insertChild(rttSamplerProbe,root->findChild(rttSwitch)+1);
  }
  void gradient(unsigned revision){
    if(!gradientBinding){gradientBinding=new SoMaterialBinding;
      gradientBinding->value=SoMaterialBinding::PER_VERTEX_INDEXED;root->insertChild(gradientBinding,root->findChild(material));}
    // Globally affine in X across both triangles, constant in Y. Fully
    // covered 1x and MSAA texels have the same mean; the two aspect ratios
    // still sample observably different horizontal ranges of the gradient.
    SbColor colors[4];
    for(unsigned i=0;i<4;++i){const bool right=i==1||i==2;
      const SbColor a=right?SbColor(.85f,.75f,.8f):SbColor(.15f,.2f,.25f);
      colors[i]=revision%3==1?SbColor(1-a[2],a[0],1-a[1]):
        revision%3==2?SbColor(a[1],1-a[0],a[2]):a;}
    material->diffuseColor.setValues(0,4,colors);
  }
  void image(unsigned unit,unsigned revision,bool decode=false,unsigned size=16){
    std::vector<uint8_t> pixels(size*size*4);
    const unsigned colors[4][3]={{48,176,224},{208,56,80},{96,208,48},{232,160,56}};
    for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
      const unsigned k=((x/(size/2))+(y/(size/2))*2+revision)%4;
      for(unsigned c=0;c<3;++c){double value=colors[k][(c+unit)%3]/255.;
        if(decode)value=bump_profile::decode(value);pixels[4*(y*size+x)+c]=uint8_t(std::lround(value*255));}
      pixels[4*(y*size+x)+3]=255;
    }
    texture[unit]->image.setValue(SbVec2s(short(size),short(size)),4,pixels.data());
  }
};
struct World {
  Quad base{SbColor(.8f,.08f,.04f)};
  Quad background{SbColor(.04f,.08f,.8f),-1.1f,1.1f,-1.1f,1.1f,-.8f};
  Quad foreground{SbColor(.04f,.8f,.08f),-.55f,.35f,-.45f,.55f,-.4f};
  Quad second{SbColor(.8f,.7f,.04f),-.2f,.7f,-.65f,.25f,.4f};
  Quad empty{SbColor(0,0,0)};
  Quad producer{SbColor(.1f,.7f,.9f),-1.1f,1.1f,-1.1f,1.1f,0};
  SoRenderManager manager;
  unsigned producerCaptures=0;
  std::vector<SoRenderManager::Superimposition*> layers;
  World(){empty.geometry->whichChild=SO_SWITCH_NONE;
    manager.setSceneGraph(base.root);manager.setCamera(base.camera);
    manager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);manager.setViewportRegion(SbViewportRegion(96,96));
    manager.setBackgroundColor(SbColor4f(0,0,0,1));manager.setStereoMode(SoRenderManager::MONO);
    for(auto *q:{&foreground,&second}){q->rtt->scene=producer.root;q->rtt->size.setValue(32,32);
      q->rtt->type=SoSceneTexture2::RGBA8;q->rtt->transparencyFunction=SoSceneTexture2::NONE;
      q->rtt->model=SoSceneTexture2::REPLACE;q->rtt->backgroundColor.setValue(.1f,.7f,.9f,1);}
  }
  ~World(){clear();manager.setSceneGraph(nullptr);manager.setCamera(nullptr);}
  void clear(){for(auto *p:layers)manager.removeSuperimposition(p);layers.clear();}
  SoRenderManager::Superimposition *add(Quad &q,uint32_t flags){auto *p=manager.addSuperimposition(q.root,flags);layers.push_back(p);return p;}
};
class Spy : public SoGLRenderAction {
public:unsigned calls=0;Spy():SoGLRenderAction(SbViewportRegion(96,96)){}
protected:void beginTraversal(SoNode*)override{++calls;}
};
struct Counts {unsigned pre=0,post=0,sensor=0,redraw=0;};
inline void pre(void *p,SoRenderManager*){++static_cast<Counts*>(p)->pre;}
inline void post(void *p,SoRenderManager*){++static_cast<Counts*>(p)->post;}
inline void redraw(void *p,SoRenderManager*){++static_cast<Counts*>(p)->redraw;}
inline void sensor(void *p,SoSensor*){++static_cast<Counts*>(p)->sensor;}
class Audit : public bump_profile::Proxy {
public:using Proxy::Proxy;unsigned roots=0;mutable std::vector<SbVec2i32> producerSizes;
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan &graph,const CoinRenderFramePlan &frame,const SbVec2i32 &size)const override{
    producerSizes.clear();for(const auto &producer:graph.producers)producerSizes.push_back(producer.size);
    return Proxy::preflightRtt(graph,frame,size);
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP &t)override{++roots;return Proxy::submit(f,t);}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP &t,const CoinRenderFrameReuseDecision &r)override{
    ++roots;return Proxy::submit(f,t,r);}
};
// A GPU-free observer at the real adapter boundary, never an image oracle.
class Inspection : public CoinRenderBackend {
public:mutable unsigned captures=0;unsigned submits=0;mutable CoinRenderFramePlan frame;
  const std::string error="test inspection stops before device execution";
  bool isGpuBackend()const override{return true;}
  CoinRenderBackendStatus getStatus()const override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderBackendStatus prepare(CoinRenderTargetP&)override{return CoinRenderBackendStatus::SUCCESS;}
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&,const CoinRenderFramePlan &f,const SbVec2i32&)const override{
    ++captures;frame=f;return {CoinRenderBackendStatus::UNSUPPORTED,error};}
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,CoinRenderTargetP&)override{
    ++submits;frame=f;return {CoinRenderBackendStatus::UNSUPPORTED,error};}
  void poll()override{}
  const std::string &getLastError()const override{return error;}
};
// Reference uses the public manager's actual multipass/layer implementation.
class ReferenceAction : public SoGLRenderAction {
  SoRenderManager &manager;bool inside=false;
public:explicit ReferenceAction(SoRenderManager &m):SoGLRenderAction(SbViewportRegion(96,96)),manager(m){
    setCacheContext(SoGLCacheContextElement::getUniqueCacheContext());}
  void apply(SoNode *n)override{if(inside){SoGLRenderAction::apply(n);return;}
    inside=true;manager.render(this,TRUE,TRUE,TRUE);inside=false;}
};
struct Reference {
  using ClipControl=void(*)(GLenum,GLenum);
  ClipControl clipControl=nullptr;bool clipControlSupported=false;
  struct OriginScope {
    ClipControl change=nullptr;GLint origin=GL_LOWER_LEFT,depth=GL_NEGATIVE_ONE_TO_ONE;
    bool previous=upperLeftReference;
    unsigned previousHeight=referenceFramebufferHeight;
    OriginScope(ClipControl call,bool upper,unsigned height){referenceFramebufferHeight=height;if(upper){change=call;
      glGetIntegerv(GL_CLIP_ORIGIN,&origin);glGetIntegerv(GL_CLIP_DEPTH_MODE,&depth);
      change(GL_UPPER_LEFT,GL_NEGATIVE_ONE_TO_ONE);upperLeftReference=true;}}
    void restore(){if(change){change(GLenum(origin),GLenum(depth));change=nullptr;}upperLeftReference=previous;
      referenceFramebufferHeight=previousHeight;}
    ~OriginScope(){restore();}
  };
  egl_lifetime::Api api;Display *display=nullptr;std::unique_ptr<egl_lifetime::Context> context;
  GLuint framebuffer=0,color=0,depth=0,resolved=0,resolvedColor=0,resolvedDepth=0;
  GLuint minimumFramebuffer=0,minimumTexture=0,minimumProgram=0;
  unsigned size=0,samples=0;
  ~Reference(){if(context){bind();releaseBuffers();context->close();context.reset();}if(display)XCloseDisplay(display);}
  bool bind(){return context&&api.MakeCurrent(context->display,context->surface,context->surface,context->context);}
  bool unbind(){return !context||api.MakeCurrent(context->display,nullptr,nullptr,nullptr);}
  bool initialize(){
    if(context)return bind();
    display=XOpenDisplay(nullptr);if(!display||!api.ready())return false;
    context.reset(new egl_lifetime::Context(api,display));
    if(!context->initialize(false)||!context->makeCurrent())return false;
    const char *vendor=reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char *renderer=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char *version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    GLint major=0,minor=0;glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    clipControl=reinterpret_cast<ClipControl>(api.GetProcAddress("glClipControl"));
    const char *extensions=(major>4||(major==4&&minor>=5))?nullptr:reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const bool clipExtension=extensions&&(std::string(" ")+extensions+" ").find(" GL_ARB_clip_control ")!=std::string::npos;
    clipControlSupported=clipControl&&((major>4||(major==4&&minor>=5))||clipExtension);
    std::cout<<"manager_mono_gl vendor="<<(vendor?vendor:"")<<" renderer="<<(renderer?renderer:"")
      <<" version="<<(version?version:"")<<" clip_control="<<clipControlSupported<<std::endl;
    const std::string name=renderer?renderer:"";
    return require(vendor&&renderer&&version&&!name.empty()&&name.find("llvmpipe")==std::string::npos&&
      name.find("softpipe")==std::string::npos,"real accelerated desktop CoinGL reference identity");
  }
  void releaseBuffers(){if(color)glDeleteRenderbuffers(1,&color);
    if(depth){if(samples>1)glDeleteTextures(1,&depth);else glDeleteRenderbuffers(1,&depth);}
    if(framebuffer)glDeleteFramebuffers(1,&framebuffer);if(resolvedColor)glDeleteRenderbuffers(1,&resolvedColor);
    if(resolvedDepth)glDeleteRenderbuffers(1,&resolvedDepth);if(resolved)glDeleteFramebuffers(1,&resolved);
    if(minimumTexture)glDeleteTextures(1,&minimumTexture);if(minimumFramebuffer)glDeleteFramebuffers(1,&minimumFramebuffer);
    if(minimumProgram)glDeleteProgram(minimumProgram);
    framebuffer=color=depth=resolved=resolvedColor=resolvedDepth=minimumFramebuffer=minimumTexture=minimumProgram=0;}
  bool minimumShader(){
    const char *vertex="#version 150 compatibility\nvoid main(){gl_Position=gl_Vertex;}\n";
    const char *fragment="#version 150 compatibility\nuniform sampler2DMS depthSamples;\nuniform int sampleCount;\n"
      "out float minimumDepth;\nvoid main(){ivec2 p=ivec2(gl_FragCoord.xy);float z=1.0;"
      "for(int i=0;i<sampleCount;++i)z=min(z,texelFetch(depthSamples,p,i).r);minimumDepth=z;}\n";
    auto compile=[](GLenum type,const char *source){const GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,nullptr);
      glCompileShader(shader);GLint valid=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&valid);
      if(!valid){char message[2048]={};glGetShaderInfoLog(shader,sizeof(message),nullptr,message);
        std::cerr<<"manager_mono MIN depth shader compilation: "<<message<<std::endl;glDeleteShader(shader);return GLuint(0);}
      return shader;};
    const GLuint vs=compile(GL_VERTEX_SHADER,vertex),fs=compile(GL_FRAGMENT_SHADER,fragment);
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return false;}
    minimumProgram=glCreateProgram();glAttachShader(minimumProgram,vs);glAttachShader(minimumProgram,fs);
    glBindFragDataLocation(minimumProgram,0,"minimumDepth");glLinkProgram(minimumProgram);
    glDeleteShader(vs);glDeleteShader(fs);GLint valid=0;glGetProgramiv(minimumProgram,GL_LINK_STATUS,&valid);
    if(!valid){char message[2048]={};glGetProgramInfoLog(minimumProgram,sizeof(message),nullptr,message);
      std::cerr<<"manager_mono MIN depth shader link: "<<message<<std::endl;return false;}
    return true;
  }
  bool buffers(unsigned n,unsigned count){
    if(size==n&&samples==count)return true;releaseBuffers();size=n;samples=count;
    auto allocate=[&](GLuint &fb,GLuint &rgb,GLuint &z,unsigned ms){
      glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
      glGenRenderbuffers(1,&rgb);glBindRenderbuffer(GL_RENDERBUFFER,rgb);
      if(ms>1)glRenderbufferStorageMultisample(GL_RENDERBUFFER,ms,GL_RGBA8,n,n);
      else glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,n,n);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rgb);
      if(ms>1){GLint previous=0;glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE,&previous);
        glGenTextures(1,&z);glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,z);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,ms,GL_DEPTH_COMPONENT24,n,n,GL_TRUE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D_MULTISAMPLE,z,0);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,GLuint(previous));
      }else{glGenRenderbuffers(1,&z);glBindRenderbuffer(GL_RENDERBUFFER,z);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,n,n);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,z);}
      if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
      // New GL storage is undefined. Match an initially empty native canvas
      // before manager backgrounds that intentionally preserve depth. This is
      // attachment initialization only, never a preparatory scene traversal.
      glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);
      glClearColor(0,0,0,1);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
      return true;
    };
    if(!allocate(framebuffer,color,depth,count))return false;
    if(count>1&&!allocate(resolved,resolvedColor,resolvedDepth,1))return false;
    if(count>1){GLint previous=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous);
      glGenFramebuffers(1,&minimumFramebuffer);glBindFramebuffer(GL_FRAMEBUFFER,minimumFramebuffer);
      glGenTextures(1,&minimumTexture);glBindTexture(GL_TEXTURE_2D,minimumTexture);
      glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,n,n,0,GL_RED,GL_FLOAT,nullptr);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,minimumTexture,0);
      glBindTexture(GL_TEXTURE_2D,GLuint(previous));
      if(!require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"MIN depth readback FBO")||!minimumShader())return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);GLint actual=0;glGetIntegerv(GL_SAMPLES,&actual);
    std::cout<<"manager_mono_gl samples_requested="<<count<<" samples_actual="<<actual<<std::endl;
    for(GLint i=0;i<actual;++i){GLfloat position[2]={};glGetMultisamplefv(GL_SAMPLE_POSITION,GLuint(i),position);
      std::cout<<"manager_mono_gl sample="<<i<<" position="<<position[0]<<","<<position[1]<<std::endl;}
    return require(actual==int(count>1?count:0),"CoinGL FBO exact requested sample count");
  }
  bool minimumDepth(std::vector<float> &out){
    // This is an observer of the actual multisample attachment, not another
    // scene render. glBlitFramebuffer's depth resolve is implementation-defined
    // and cannot serve the public CoinRender MIN-of-covered-samples contract.
    GLint oldDraw=0,oldRead=0,oldProgram=0,oldUnit=0,oldTexture=0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&oldDraw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_ACTIVE_TEXTURE,&oldUnit);
    glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE,&oldTexture);
    glPushAttrib(GL_ALL_ATTRIB_BITS);glBindFramebuffer(GL_FRAMEBUFFER,minimumFramebuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);glViewport(0,0,size,size);
    for(GLenum state:{GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_BLEND,GL_ALPHA_TEST,GL_CULL_FACE,GL_MULTISAMPLE,GL_POLYGON_STIPPLE,
        GL_COLOR_LOGIC_OP,GL_FRAMEBUFFER_SRGB})glDisable(state);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glUseProgram(minimumProgram);glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,depth);
    const GLint textureLocation=glGetUniformLocation(minimumProgram,"depthSamples"),countLocation=glGetUniformLocation(minimumProgram,"sampleCount");
    const bool uniforms=textureLocation>=0&&countLocation>=0;
    if(uniforms){glUniform1i(textureLocation,0);glUniform1i(countLocation,GLint(samples));
      glBegin(GL_TRIANGLE_STRIP);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(-1,1);glVertex2f(1,1);glEnd();
      glReadPixels(0,0,size,size,GL_RED,GL_FLOAT,out.data());}
    glUseProgram(GLuint(oldProgram));glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,GLuint(oldTexture));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(oldDraw));glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(oldRead));
    glPopAttrib();glActiveTexture(GLenum(oldUnit));
    return require(uniforms,"MIN depth reference uniforms");
  }
  bool render(World &world,ReferenceAction &action,unsigned n,unsigned count,Image &out,CoinRenderRenderer renderer){
    if(!initialize()||!buffers(n,count))return false;
#ifdef COIN_MANAGER_MONO_BGFX
    const bool upper=count>1&&renderer==COIN_RENDER_RENDERER_VULKAN;
#else
    // WebGPU's observer coordinates are top-left for both Vulkan and GL.
    const bool upper=count>1;
#endif
    if(upper&&!require(clipControlSupported,"top-left API MSAA reference requires GL4.5 or ARB_clip_control for normalized sample origin"))return false;
    OriginScope origin(clipControl,upper,n);
    std::cout<<"manager_mono_gl_origin samples="<<count<<" renderer="<<renderer<<" origin="<<(upper?"upper_left":"lower_left")
      <<" readback_row_flip="<<!upper<<std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
    // SoOffscreenRenderer::renderFromBase enables depth testing before its
    // action. A raw EGL context defaults it off; superimpositions preserve
    // that external host state around their authored ZBUFFERON scopes.
    glEnable(GL_DEPTH_TEST);
    if(count>1)glEnable(GL_MULTISAMPLE);else glDisable(GL_MULTISAMPLE);
    action.setViewportRegion(world.manager.getViewportRegion());action.setTransparencyType(SoGLRenderAction::BLEND);
    const bool ready=world.manager.prepareFrame();if(ready)action.apply(world.base.root);world.manager.finishFrame(ready);
    if(!ready)return false;
    if(count>1){glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,resolved);
      glBlitFramebuffer(0,0,n,n,0,0,n,n,GL_COLOR_BUFFER_BIT,GL_NEAREST);}
    glBindFramebuffer(GL_FRAMEBUFFER,count>1?resolved:framebuffer);glReadBuffer(GL_COLOR_ATTACHMENT0);glFinish();
    std::vector<uint8_t> rgba(size_t(n)*n*4);std::vector<float> z(size_t(n)*n);
    glReadPixels(0,0,n,n,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    if(count>1){if(!minimumDepth(z))return false;}else glReadPixels(0,0,n,n,GL_DEPTH_COMPONENT,GL_FLOAT,z.data());
    out.rgba.resize(rgba.size());out.depth.resize(z.size());
    // 1x and BGFX/OpenGL retain the conventional CoinGL oracle. WebGPU and
    // BGFX/Vulkan MSAA use upper-left raster origin without a readback flip.
    for(unsigned y=0;y<n;++y){const size_t row=upper?y:n-1-y;
      std::copy_n(rgba.data()+row*n*4,n*4,out.rgba.data()+size_t(y)*n*4);
      std::copy_n(z.data()+row*n,n,out.depth.data()+size_t(y)*n);}
    origin.restore();const auto error=glGetError();unbind();
    return require(error==GL_NO_ERROR,"CoinGL render/readback GL error="+std::to_string(error));
  }
};
inline bool compare(const Image &a,const Image &b,unsigned n,const char *label,
                    const std::function<bool(unsigned,unsigned)> &rgbRegion={},
                    const std::function<bool(unsigned,unsigned)> &depthRegion={}){
  if(!require(a.rgba.size()==size_t(n)*n*4&&b.rgba.size()==a.rgba.size(),"complete equally sized RGB readbacks"))return false;
  auto close=[&](const Image &x,size_t at,const Image &y,int xx,int yy){for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
    const int px=xx+dx,py=yy+dy;if(px<0||py<0||px>=int(n)||py>=int(n))continue;bool yes=true;
    for(unsigned c=0;c<3;++c)yes &= std::abs(int(x.rgba[at*4+c])-int(y.rgba[(size_t(py)*n+px)*4+c]))<=4;
    if(yes)return true;}return false;};
  auto boundary=[&](const Image &x,int xx,int yy){for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
    const int px=xx+dx,py=yy+dy;if(px<0||py<0||px>=int(n)||py>=int(n))continue;
    for(unsigned c=0;c<3;++c)if(std::abs(int(x.rgba[(size_t(yy)*n+xx)*4+c])-int(x.rgba[(size_t(py)*n+px)*4+c]))>4)return true;
    }return false;};
  unsigned solid=0,edge=0,depth=0,opaque=0,rgbReports=0,qualifiedRgb=0,qualifiedDepth=0;
  auto reportRGB=[&](const char *kind,unsigned x,unsigned y){
    if(rgbReports>=5)return;++rgbReports;
    auto pixel=[&](const Image &image,unsigned px,unsigned py){const size_t p=4*(size_t(py)*n+px);
      std::cout<<unsigned(image.rgba[p])<<","<<unsigned(image.rgba[p+1])<<","<<unsigned(image.rgba[p+2]);};
    std::cout<<"manager_mono_rgb label="<<label<<" kind="<<kind<<" xy="<<x<<","<<y<<" native=";
    pixel(a,x,y);std::cout<<" CoinGL=";pixel(b,x,y);
    for(const auto *image:{&a,&b}){
      std::cout<<(image==&a?" native_neighbors=[":" CoinGL_neighbors=[");
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int px=int(x)+dx,py=int(y)+dy;
        if(px<0||py<0||px>=int(n)||py>=int(n))continue;
        std::cout<<px<<","<<py<<":";pixel(*image,unsigned(px),unsigned(py));std::cout<<";";}
      std::cout<<"]";
    }
    std::cout<<std::endl;
  };
  for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){const size_t p=size_t(y)*n+x;
    const bool border=boundary(a,x,y)||boundary(b,x,y);
    if(!rgbRegion||rgbRegion(x,y)){++qualifiedRgb;
      if(border){const bool badA=!close(a,p,b,int(x),int(y)),badB=!close(b,p,a,int(x),int(y));
        edge+=badA;edge+=badB;if(badA||badB)reportRGB("boundary",x,y);}
      else{unsigned errors=0;for(unsigned c=0;c<3;++c)errors+=std::abs(int(a.rgba[p*4+c])-int(b.rgba[p*4+c]))>4;
        solid+=errors;if(errors)reportRGB("interior",x,y);}}
    if(!border&&(!depthRegion||depthRegion(x,y))&&!a.depth.empty()&&!b.depth.empty()){
      ++qualifiedDepth;if(std::abs(a.depth[p]-b.depth[p])>2e-5f){
        if(depth<5)std::cout<<"manager_mono_depth label="<<label<<" xy="<<x<<","<<y
          <<" native="<<a.depth[p]<<" CoinGL="<<b.depth[p]<<std::endl;
        ++depth;}}
    opaque+=a.rgba[p*4+3]!=255;
  }
  std::cout<<"manager_mono_compare label="<<label<<" interior_rgb="<<solid<<" boundary_rgb="<<edge<<" depth="<<depth
    <<" nonopaque="<<opaque<<" qualified_rgb="<<qualifiedRgb<<" qualified_depth="<<qualifiedDepth<<std::endl;
  // Existing draw-mode oracle: in-place RGB tolerance4; symmetric radius1 only at raster boundaries.
  // Alpha differs in legacy GL blend equations, so compare RGB and native opaque clear ownership.
  return require(!solid&&!edge&&!depth,std::string(label)+" material CoinGL RGB/depth equivalence");
}
inline bool color(const Image &image,unsigned n,unsigned x,unsigned y,const SbColor &expected){
  if(image.rgba.size()!=size_t(n)*n*4)return false;
  for(unsigned c=0;c<3;++c)if(std::abs(int(image.rgba[(size_t(y)*n+x)*4+c])-int(std::lround(expected[c]*255)))>4)return false;
  return true;
}
inline unsigned changed(const Image &a,const Image &b){if(a.rgba.size()!=b.rgba.size())return 0;
  unsigned count=0;for(size_t p=0;p<a.rgba.size();p+=4){bool different=false;
    for(unsigned c=0;c<3;++c)different|=std::abs(int(a.rgba[p+c])-int(b.rgba[p+c]))>4;count+=different;}return count;}
inline unsigned occurrences(const std::string &text,const std::string &needle){unsigned n=0;size_t at=0;
  while((at=text.find(needle,at))!=std::string::npos){++n;at+=needle.size();}return n;}
inline bool acceleratedName(std::string name){
  std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
  for(const char *software:{"llvmpipe","softpipe","software","swrast","swiftshader","lavapipe","unknown","mesa offscreen"})
    if(name.find(software)!=std::string::npos)return false;
  return name.size()>7&&name!="opengl";
}
inline bool physicalWgpuGlIdentity(const std::string &events,const Config &config){
  std::istringstream lines(events);std::string line;std::ostringstream vendor;vendor<<"vendor_id=0x"<<std::hex<<config.vendor;
  const std::string prefix=std::string("COIN_RENDER_PHASE wgpu_")+(config.window?"surface":"offscreen")+" renderer=opengl ";
  while(std::getline(lines,line)){if(line.find(prefix)!=0||line.find(vendor.str()+" ")==std::string::npos||
      line.find("device_type=Cpu")!=std::string::npos)continue;
    const size_t begin=line.find("adapter_name=\""),end=begin==std::string::npos?begin:line.find('"',begin+14);
    if(begin==std::string::npos||end==std::string::npos)continue;
    const auto name=line.substr(begin+14,end-begin-14);
    if(!acceleratedName(name)||(config.adapterName!=name&&config.adapterName!=name+" (Gl)"))continue;
    const size_t serial=line.find(" serial=");
    if(serial!=std::string::npos&&std::strtoull(line.c_str()+serial+8,nullptr,10)>0)return true;
  }
  return false;
}
inline bool physicalGlIdentity(const std::string &events){
  const std::string tag="COIN_RENDER_PHASE bgfx_gl_adapter vendor=";
  const size_t begin=events.find(tag);if(begin==std::string::npos)return false;
  const size_t renderer=events.find(" renderer=",begin+tag.size()),version=events.find(" version=",renderer);
  if(renderer==std::string::npos||version==std::string::npos)return false;
  const auto vendor=events.substr(begin+tag.size(),renderer-begin-tag.size());
  const auto name=events.substr(renderer+10,version-renderer-10);
  const auto end=events.find('\n',version);const auto api=events.substr(version+9,end==std::string::npos?end:end-version-9);
  std::string lower=vendor+" "+name+" "+api;
  std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
  for(const char *software:{"llvmpipe","softpipe","software","swrast","swiftshader","lavapipe","unknown"})
    if(lower.find(software)!=std::string::npos)return false;
  return !vendor.empty()&&name.size()>7&&!api.empty()&&name!="OpenGL";
}
}
#endif
