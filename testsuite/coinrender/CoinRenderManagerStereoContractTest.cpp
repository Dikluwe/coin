#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderManagerMonoFixture.h"
#include "CoinRenderStereoStencilReference.h"
#include <Inventor/SoInteraction.h>
#include <Inventor/actions/SoBoxHighlightRenderAction.h>
#include <Inventor/actions/SoLineHighlightRenderAction.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoGroup.h>
#include <set>
#include <array>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <stdexcept>

namespace manager_mono { SO_NODE_SOURCE(Viewport); }
namespace manager_stereo {
using namespace manager_mono;
using Layer=SoRenderManager::Superimposition;
struct Options : Config { bool orthographic=false;CoinRenderTransparencyMode mechanism=COIN_RENDER_TRANSPARENCY_COIN; SoRenderManager::StereoMode stereo=SoRenderManager::ANAGLYPH; };
struct Observations {
  SoCamera *camera=nullptr;std::vector<int> order;unsigned producers=0;
  bool invalidRight=false,throwRight=false;SoIndexedFaceSet *shape=nullptr;
  std::array<unsigned,2> producerKinds{{0,0}};
};
struct CallbackData { Observations *state;int tag;unsigned producerKind=0; };
void authored(void *p,SoAction *action){
  if(!action->isOfType(SoCallbackAction::getClassTypeId()))return;
  auto &d=*static_cast<CallbackData*>(p);auto &s=*d.state;
  const int eye=int(s.camera->getStereoMode());s.order.push_back(d.tag*10+eye);
  if(d.tag==2&&eye==int(SoCamera::RIGHT_VIEW)){
    if(s.throwRight)throw std::runtime_error("authored right-eye exception");
    if(s.invalidRight)s.shape->coordIndex.set1Value(0,99999);
  }
  if(d.tag==4){++s.producers;++s.producerKinds[d.producerKind];}
}
struct OpaqueProducerReference {
  struct Entry {GLint framebuffer=0,depthFunction=0;GLboolean mask[4]={},depthWrite=GL_FALSE,depthTest=GL_FALSE;};
  std::vector<Entry> stack;unsigned begins=0,ends=0;bool installed=false,valid=true;SoSceneTexture2 *owner=nullptr;
  static void begin(void *data,SoAction *action){
    if(!action->isOfType(SoGLRenderAction::getClassTypeId()))return;
    auto &scope=*static_cast<OpaqueProducerReference*>(data);Entry entry;GLint viewport[4]={};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&entry.framebuffer);glGetIntegerv(GL_VIEWPORT,viewport);
    glGetBooleanv(GL_COLOR_WRITEMASK,entry.mask);glGetBooleanv(GL_DEPTH_WRITEMASK,&entry.depthWrite);
    glGetIntegerv(GL_DEPTH_FUNC,&entry.depthFunction);entry.depthTest=glIsEnabled(GL_DEPTH_TEST);
    const auto size=scope.owner?scope.owner->size.getValue():SbVec2s(32,32);
    scope.valid &= entry.framebuffer!=0&&viewport[2]==size[0]&&viewport[3]==size[1];scope.stack.push_back(entry);++scope.begins;
    glPushAttrib(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    // The legacy producer clear ran under the external eye channel mask.
    // Repeat the same clear values inside the actual producer FBO, with
    // independent color writes; preserve the host's depth state afterwards.
    glDepthMask(GL_TRUE);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDepthMask(entry.depthWrite);
    std::cout<<"manager_stereo_gl_opaque_producer_reference framebuffer="<<entry.framebuffer
      <<" input_mask="<<int(entry.mask[0])<<int(entry.mask[1])<<int(entry.mask[2])<<int(entry.mask[3])
      <<" producer_mask=1111 recolor_clear=1 delayed_alpha_oracle=0"<<std::endl;
  }
  static void end(void *data,SoAction *action){
    if(!action->isOfType(SoGLRenderAction::getClassTypeId()))return;
    auto &scope=*static_cast<OpaqueProducerReference*>(data);++scope.ends;
    if(scope.stack.empty()){scope.valid=false;return;}const auto entry=scope.stack.back();scope.stack.pop_back();
    GLint framebuffer=0,depthFunction=0;GLboolean mask[4]={},depthWrite=GL_FALSE;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);scope.valid &= framebuffer==entry.framebuffer;
    glPopAttrib();glGetBooleanv(GL_COLOR_WRITEMASK,mask);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);
    glGetIntegerv(GL_DEPTH_FUNC,&depthFunction);
    for(unsigned c=0;c<4;++c)scope.valid &= mask[c]==entry.mask[c];
    scope.valid &= depthWrite==entry.depthWrite&&depthFunction==entry.depthFunction&&glIsEnabled(GL_DEPTH_TEST)==entry.depthTest;
  }
  bool install(Quad &producer,SoSceneTexture2 *texture=nullptr){
    if(installed)return true;owner=texture;
    if(!require(producer.material->transparency[0]==0&&producer.textures->whichChild.getValue()==SO_SWITCH_NONE&&
      producer.rttSwitch->whichChild.getValue()==SO_SWITCH_NONE,"GL normalization is limited to this opaque, non-delayed producer fixture"))return false;
    auto *first=new SoCallback;first->setCallback(begin,this);producer.root->insertChild(first,0);
    auto *last=new SoCallback;last->setCallback(end,this);producer.root->addChild(last);installed=true;return true;
  }
};
struct WorldStereo : World {
  OpaqueProducerReference opaqueProducer,opaqueAlternate;
  Quad alternateProducer{SbColor(.85f,.2f,.6f),-2.2f,2.2f,-1.1f,1.1f,0};
  bool stagedIsolation=false;
  Observations seen;CallbackData callbacks[5];SoCamera *perspective=new SoPerspectiveCamera;
  WorldStereo(){
    perspective->position.setValue(0,0,3);perspective->nearDistance=1;perspective->farDistance=8;
    perspective->focalDistance=2;perspective->setBalanceAdjustment(1);static_cast<SoPerspectiveCamera*>(perspective)->heightAngle=.72f;perspective->setStereoAdjustment(.317f);
    base.root->replaceChild(base.camera,perspective);manager.setCamera(perspective);
    seen.camera=perspective;seen.shape=base.shape;
    unsigned i=0;for(auto *q:{&background,&base,&foreground,&producer,&alternateProducer}){
      callbacks[i]={&seen,i<4?int(i+1):4,i==4?1u:0u};auto *cb=new SoCallback;cb->setCallback(authored,&callbacks[i]);
      q->root->insertChild(cb,q->root->findChild(q->geometry));++i;
    }
    base.setRect(-.7f,.55f,-.65f,.7f,.35f);
    manager.setStereoOffset(.9f);manager.setStereoMode(SoRenderManager::ANAGLYPH);
  }
  void orthographic(){
    auto *c=new SoOrthographicCamera;c->position=perspective->position;c->nearDistance=perspective->nearDistance;
    c->farDistance=perspective->farDistance;c->focalDistance=perspective->focalDistance;c->height=.8f;
    c->setStereoAdjustment(perspective->getStereoAdjustment());c->setBalanceAdjustment(perspective->getBalanceAdjustment());
    base.root->replaceChild(perspective,c);manager.setCamera(c);perspective=c;seen.camera=c;
  }
  void resetObservations(){seen.order.clear();seen.producers=0;seen.producerKinds={{0,0}};}
  std::array<unsigned,2> expectedStagedCaptures()const{
    std::array<unsigned,2> count{{0,0}};auto add=[&](const Quad &q,unsigned occurrences){
      if(q.rttSwitch->whichChild.getValue()==SO_SWITCH_NONE)return;
      const auto *texture=static_cast<const SoSceneTexture2*>(q.rttSwitch->getChild(0));
      if(texture->scene.getValue()==producer.root)count[0]+=occurrences;
      if(texture->scene.getValue()==alternateProducer.root)count[1]+=occurrences;
    };
    if(manager.getSceneGraph()==base.root)add(base,2);
    for(int i=0;i<manager.getNumSuperimpositions();++i){const auto *layer=manager.getSuperimposition(i);if(!layer->isEnabled())continue;
      for(const auto *q:{&background,&foreground,&second})if(layer->getSceneGraph()==q->root)add(*q,1);
    }return count;
  }
};
struct CameraState {
  SoCamera::StereoMode eye;float offset,balance;SbVec3f position;SbRotation orientation;
  float nearDistance,farDistance,focalDistance,aspect;
  explicit CameraState(SoCamera *c):eye(c->getStereoMode()),offset(c->getStereoAdjustment()),
    balance(c->getBalanceAdjustment()),position(c->position.getValue()),
    orientation(c->orientation.getValue()),nearDistance(c->nearDistance.getValue()),farDistance(c->farDistance.getValue()),
    focalDistance(c->focalDistance.getValue()),aspect(c->aspectRatio.getValue()){}
  static bool equal(float a,float b){return a==b||(std::isnan(a)&&std::isnan(b));}
  bool same(SoCamera *c)const{return eye==c->getStereoMode()&&equal(offset,c->getStereoAdjustment())&&
    equal(balance,c->getBalanceAdjustment())&&position==c->position.getValue()&&
    orientation==c->orientation.getValue()&&equal(nearDistance,c->nearDistance.getValue())&&equal(farDistance,c->farDistance.getValue())&&
    equal(focalDistance,c->focalDistance.getValue())&&equal(aspect,c->aspectRatio.getValue());}
};
// Own desktop EGL FBO oracle. The actual native manager writes and consumes
// stencil; no CoinRender frame plan participates in the reference computation.
struct StereoReference : Reference {
  unsigned packedSide=0,packedSamples=0;
  bool imageReference=false;
  bool packed(unsigned n,unsigned count){
    if(!initialize()||!buffers(n,count))return false;
    if(packedSide==n&&packedSamples==count)return true;
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    if(count>1){glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,depth);
      glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,count,GL_DEPTH24_STENCIL8,n,n,GL_TRUE);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D_MULTISAMPLE,depth,0);
    }else{glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,n,n);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth);}
    glDepthMask(GL_TRUE);glStencilMask(~0u);glClearDepth(1);glClearStencil(0);
    glClear(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    packedSide=n;packedSamples=count;
    return require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"native packed stencil FBO");
  }
  static void uncache(SoNode *node,std::set<SoNode*> &visited){
    if(!node||!visited.insert(node).second)return;
    if(node->isOfType(SoSeparator::getClassTypeId())){
      auto *separator=static_cast<SoSeparator*>(node);
      if(separator->renderCaching.getValue()!=SoSeparator::OFF)separator->renderCaching=SoSeparator::OFF;
    }
    if(node->isOfType(SoGroup::getClassTypeId())){
      auto *group=static_cast<SoGroup*>(node);for(int i=0;i<group->getNumChildren();++i)uncache(group->getChild(i),visited);
    }
    if(node->isOfType(SoSceneTexture2::getClassTypeId()))uncache(static_cast<SoSceneTexture2*>(node)->scene.getValue(),visited);
    if(node->isOfType(SoShadowDirectionalLight::getClassTypeId()))uncache(static_cast<SoShadowDirectionalLight*>(node)->shadowMapScene.getValue(),visited);
  }
  bool draw(WorldStereo &w,unsigned side,unsigned samples,Image &out,CoinRenderRenderer renderer){
    // Stereo camera setters intentionally do not notify. The independent GL
    // oracle must traverse each authored eye instead of replaying a cached
    // display list with a prior camera. Only reference graphs are changed.
    std::set<SoNode*> visited;uncache(w.manager.getSceneGraph(),visited);
    for(auto *q:{&w.base,&w.background,&w.foreground,&w.second,&w.empty,&w.producer})uncache(q->root,visited);
    for(int i=0;i<w.manager.getNumSuperimpositions();++i)uncache(w.manager.getSuperimposition(i)->getSceneGraph(),visited);
    if(!packed(side,samples))return false;
#ifdef COIN_MANAGER_MONO_BGFX
    const bool upper=samples>1&&renderer==COIN_RENDER_RENDERER_VULKAN;
#else
    const bool upper=samples>1;
#endif
    class Action : public ReferenceAction {
      SoRenderManager &source;bool nested=false,upper;unsigned height;
    public:Action(SoRenderManager &m,bool top,unsigned h):ReferenceAction(m),source(m),upper(top),height(h){}
      void apply(SoNode*n)override{const bool outer=!nested;if(outer){nested=true;setTransparencyType(source.getGLRenderAction()->getTransparencyType());
          auto observed=source.getViewportRegion();if(upper){const auto p=observed.getViewportOriginPixels(),s=observed.getViewportSizePixels();
            observed.setViewportPixels(p[0],short(int(height)-p[1]-s[1]),s[0],s[1]);}
          // Only this external action is normalized. The manager-owned
          // action retains logical bottom-origin stencil phase. Authored
          // nested Viewport nodes already perform their own conversion.
          setViewportRegion(observed);
        }
        ReferenceAction::apply(n);if(outer)nested=false;}
    } action(w.manager,upper,side);
    // initStencilBufferForInterleavedStereo reads the manager-owned action's
    // viewport. Set both via the public manager, as a host does.
    action.setViewportRegion(w.manager.getViewportRegion());
    stereo_stencil_reference::Scope bitmapScope;bitmapScope.clipControlAvailable=clipControlSupported;bitmapScope.framebufferHeight=int(side);bitmapScope.imageUploads=imageReference;
    const bool rendered=render(w,action,side,samples,out,renderer);
    return require(bitmapScope.ok(),"native stencil/image upload reference: "+bitmapScope.error)&&
      require(!imageReference||!upper||bitmapScope.imageUploadCount>0,"upper-left SoImage reference observed its real image upload")&&rendered;
  }
};
// Finite authored-quad classifier for the same P02/P07 native profile as
// the mono manager. Geometry is projected before inspecting either image.
// No observed agreement, disagreement or color decides an interior band.
struct ProjectedQuad {
  const Quad *source=nullptr;SbVec2f p[4];int eye=-1;bool base=false,foreground=false;
  static double segment(double x,double y,const SbVec2f &a,const SbVec2f &b){
    const double dx=b[0]-a[0],dy=b[1]-a[1],den=dx*dx+dy*dy;
    const double t=den?std::max(0.,std::min(1.,((x-a[0])*dx+(y-a[1])*dy)/den)):0;
    return std::hypot(x-a[0]-t*dx,y-a[1]-t*dy);
  }
  bool edge(double x,double y)const{for(int i=0;i<4;++i)if(segment(x,y,p[i],p[(i+1)%4])<=1)return true;return false;}
  bool inside(double x,double y,double margin=0)const{
    double sign=0;for(int i=0;i<4;++i){const auto &a=p[i],&b=p[(i+1)%4];
      const double cross=(b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]);
      if(!i)sign=cross;else if(cross*sign<0)return false;
      if(margin&&segment(x,y,a,b)<=margin)return false;
    }return true;
  }
  bool corner(double x,double y,double radius)const{for(const auto &a:p)
    if(std::abs(x-a[0])<=radius&&std::abs(y-a[1])<=radius)return true;return false;}
  bool diagonal(double x,double y)const{return segment(x,y,p[0],p[2])<=1;}
  bool textureBand(double x,double y)const{
    if(source->textures->whichChild.getValue()==SO_SWITCH_NONE)return false;
    // Both authored images contain quadrant boundaries at every half UV
    // period. Project those fixed UV isocontours, including repeat seams.
    const double dx=p[1][0]-p[0][0],dy=p[3][1]-p[0][1];
    if(!dx||!dy)return true;
    const double tx=(x-p[0][0])/dx,ty=(y-p[0][1])/dy;
    if(tx<0||tx>1||ty<0||ty>1)return false;
    for(unsigned unit=0;unit<2;++unit){const auto *uv=source->uv[unit]->point.getValues(0);
      const double du=uv[1][0]-uv[0][0],dv=uv[3][1]-uv[0][1];
      const double u=uv[0][0]+tx*du,v=uv[0][1]+ty*dv;
      if((du&&std::abs(u*2-std::round(u*2))*std::abs(dx/(2*du))<=1)||
         (dv&&std::abs(v*2-std::round(v*2))*std::abs(dy/(2*dv))<=1))return true;
    }return false;
  }
};
struct NativeGeometry {
  std::vector<ProjectedQuad> quads;SoRenderManager::RenderMode mode;double pointRadius=1;
  explicit NativeGeometry(WorldStereo &w,unsigned side):mode(w.manager.getRenderMode()){
    auto add=[&](const Quad &q,SoCamera *camera,int eye,bool base,bool foreground){
      if(q.geometry->whichChild.getValue()==SO_SWITCH_NONE)return;
      const auto input=q.viewport->enabled?q.viewport->region:w.manager.getViewportRegion();SbViewportRegion vp;
      SbViewVolume vv=camera->getViewVolume(input,vp);
      if(eye>=0){SbMatrix view,projection;vv.getMatrices(view,projection);const SbMatrix transform=view.inverse();
        const float offset=w.manager.getStereoOffset()*(eye?.5f:-.5f),near=vv.getNearDist();
        const float focal=std::max(near,camera->focalDistance.getValue()*camera->getBalanceAdjustment());
        SbVec3f right=vv.getProjectionDirection().cross(vv.getViewUp());right.normalize();
        const float halfWidth=vv.getWidth()*.5f,halfHeight=vv.getHeight()*.5f,far=near+vv.getDepth();
        vv.frustum(-halfWidth-offset*near/focal,halfWidth-offset*near/focal,-halfHeight,halfHeight,near,far);
        vv.transform(transform);SbMatrix shift;shift.setTranslate(right*offset);vv.transform(shift);
      }
      const auto origin=vp.getViewportOriginPixels(),size=vp.getViewportSizePixels();
      ProjectedQuad projected;projected.source=&q;projected.eye=eye;projected.base=base;projected.foreground=foreground;
      for(unsigned v=0;v<4;++v){SbVec3f screen;vv.projectToScreen(q.coordinates->point[v],screen);
        projected.p[v].setValue(origin[0]+screen[0]*size[0],float(side)-origin[1]-screen[1]*size[1]);}
      quads.push_back(projected);
    };
    if(w.manager.getSceneGraph())for(int eye=0;eye<2;++eye)add(w.base,w.perspective,eye,true,false);
    if(w.base.root->findChild(w.second.root)>=0)add(w.second,w.second.camera,-1,false,false);
    for(int i=0;i<w.manager.getNumSuperimpositions();++i){const auto *layer=w.manager.getSuperimposition(i);
      if(!layer->isEnabled())continue;for(const auto *q:{&w.background,&w.foreground,&w.second,&w.empty})
        if(layer->getSceneGraph()==q->root)add(*q,q->camera,-1,false,!(layer->getStateFlags()&Layer::BACKGROUND));}
    if(mode==SoRenderManager::POINTS)pointRadius=(w.base.pointStyle&&!w.base.pointStyle->pointSize.isIgnored()?
      w.base.pointStyle->pointSize.getValue():1)*.5+1;
  }
  bool stroke(const ProjectedQuad &q,double x,double y)const{
    if(!q.base||mode==SoRenderManager::AS_IS)return false;
    if(mode==SoRenderManager::POINTS)return q.corner(x,y,pointRadius);
    return q.edge(x,y)||(mode!=SoRenderManager::BOUNDING_BOX&&q.diagonal(x,y));
  }
  bool foregroundInterior(double x,double y)const{for(const auto &q:quads)
    if(q.foreground&&q.source->material->transparency[0]==0&&q.inside(x,y,1))return true;return false;}
  bool rgb(unsigned x,unsigned y,bool msaa)const{
    if(!msaa)return true;const double px=x+.5,py=y+.5;if(foregroundInterior(px,py))return true;
    for(const auto &q:quads)if(stroke(q,px,py)||(mode==SoRenderManager::AS_IS&&q.edge(px,py)))return false;
    return true;
  }
  bool depth(unsigned x,unsigned y,bool msaa)const{
    const double px=x+.5,py=y+.5;for(const auto &q:quads)if(q.edge(px,py)||(msaa&&stroke(q,px,py)))return false;return true;
  }
  bool phase(unsigned x,unsigned y)const{
    const double px=x+.5,py=y+.5;for(const auto &q:quads)
      if(q.edge(px,py)||stroke(q,px,py)||q.textureBand(px,py))return false;return true;
  }
};
class StereoAudit : public Audit {
  template<class T>static auto forward(const T *backend,const CoinRenderFramePlan &frame,const CoinRenderTargetP &target,size_t tokens,int)
    ->decltype(backend->preflightStereo(frame,target,tokens)){return backend->preflightStereo(frame,target,tokens);}
  template<class T>static CoinRenderSubmitResult forward(const T*,const CoinRenderFramePlan&,const CoinRenderTargetP&,size_t,long){
    // Older baselines have no stereo transport admission entry point.
    return {CoinRenderBackendStatus::SUCCESS,{}};
  }
public:using Audit::Audit;std::vector<CoinRenderFramePlan> intermediate;
  struct LossProbe {bool armed=false;unsigned injections=0,completedPrivateViews=0;};
  std::shared_ptr<LossProbe> loss;
  bool injectMainLoss(const CoinRenderFramePlan &frame){
    if(!loss||!loss->armed)return false;loss->armed=false;++loss->injections;++roots;plan=frame;return true;
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &frame,CoinRenderTargetP &target)override{
    if(injectMainLoss(frame))return {CoinRenderBackendStatus::DEVICE_LOST,"test-only main DEVICE_LOST after completed stereo producer views"};
    return Audit::submit(frame,target);
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &frame,CoinRenderTargetP &target,const CoinRenderFrameReuseDecision &reuse)override{
    if(injectMainLoss(frame))return {CoinRenderBackendStatus::DEVICE_LOST,"test-only main DEVICE_LOST after completed stereo producer views"};
    return Audit::submit(frame,target,reuse);
  }
  void retireRtt(const std::vector<uint64_t> &tokens)override{real->retireRtt(tokens);}
  void configureMultipassTarget(const CoinRenderTargetP &target)override{real->configureMultipassTarget(target);}
  bool multipassOutputBytes(const CoinRenderTargetP &target,bool asynchronous,uint64_t &bytes)const override{
    return real->multipassOutputBytes(target,asynchronous,bytes);
  }
  CoinRenderSubmitResult preflightMultipass(const CoinRenderFramePlan &frame,const SbVec2i32 &size)const override{
    return real->preflightMultipass(frame,size);
  }
  CoinRenderSubmitResult preflightMultipassGraph(const CoinRenderRttPlan &graph,const CoinRenderFramePlan &frame,const SbVec2i32 &size)const override{
    return real->preflightMultipassGraph(graph,frame,size);
  }
  CoinRenderSubmitResult submitRtt(const CoinRenderFramePlan &frame,const SbVec2i32 &size,uint64_t id,CoinRenderTargetP &target,uint64_t &token)override{
    intermediate.push_back(frame);const auto result=Audit::submitRtt(frame,size,id,target,token);
    if(loss&&loss->armed&&result.status==CoinRenderBackendStatus::SUCCESS)++loss->completedPrivateViews;
    return result;
  }
  CoinRenderSubmitResult preflightStereo(const CoinRenderFramePlan &frame,const CoinRenderTargetP &target,size_t tokens)const{
    return forward(real.get(),frame,target,tokens,0);
  }
};
struct Run {
  Options options;CoinRenderOptions targetOptions;WorldStereo scene,referenceScene;Spy spy;Counts hooks;
  std::unique_ptr<CoinRenderManagerAdapter> adapter;CoinRenderTarget *target=nullptr;
  StereoReference reference;StereoAudit *audit=nullptr;Display *display=nullptr;Window window=0;unsigned side=96;
  Image stable;bool identity=false,stagedPipeline=false;std::string preparation;
  explicit Run(const Options &o,const CoinRenderOptions &t):options(o),targetOptions(t){
    if(o.orthographic){scene.orthographic();referenceScene.orthographic();}
    scene.manager.setStereoMode(o.stereo);referenceScene.manager.setStereoMode(o.stereo);
    spy.setTransparencyType(SoGLRenderAction::BLEND);scene.manager.setGLRenderAction(&spy);
    scene.manager.addPreRenderCallback(pre,&hooks);scene.manager.addPostRenderCallback(post,&hooks);
    startup::Trace trace;trace.start();
    if(o.window){display=XOpenDisplay(nullptr);if(display){window=XCreateSimpleWindow(display,DefaultRootWindow(display),16,16,side,side,0,0,0);
      XSetWindowAttributes a{};a.override_redirect=True;XChangeWindowAttributes(display,window,CWOverrideRedirect,&a);
      XMapRaised(display,window);XSync(display,False);
      CoinRenderNativeSurfaceDescriptor surface{};surface.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;surface.structSize=sizeof(surface);
      surface.type=COIN_RENDER_SURFACE_XLIB;surface.native.xlib.display=display;surface.native.xlib.window=window;
      adapter.reset(new CoinRenderManagerAdapter(scene.manager,surface,SbVec2i32(side,side),t));}}
    else adapter.reset(new CoinRenderManagerAdapter(scene.manager,SbVec2i32(side,side),t));
    preparation=trace.stop();if(adapter)target=adapter->getSceneManager()->getRenderTarget();
    if(target&&!o.window)target->setDepthReadbackEnabled(TRUE);
  }
  ~Run(){reference.unbind();adapter.reset();scene.manager.setGLRenderAction(nullptr);
    scene.manager.removePreRenderCallback(pre,&hooks);scene.manager.removePostRenderCallback(post,&hooks);
    if(window)XDestroyWindow(display,window);if(display)XCloseDisplay(display);}
  template<class F>void both(F f){f(scene);f(referenceScene);}
  Image read()const{Image out;target->readbackRGBA(out.rgba);target->readbackDepth(out.depth);out.serial=target->getLastSubmissionSerial();return out;}
  bool ready(){
    if(!require(target&&target->getStatus()==CoinRenderTarget::TARGET_READY,"actual ready GPU target"))return false;
    auto &implementation=target->getPimpl().get();
    // Lazy backend creation emits the effective GL adapter identity here.
    // Keep that real preparation inside the same passive evidence stream
    // used by draw(), before wrapping the already-prepared backend.
    startup::Trace trace;if(!require(trace.start(),"passive backend preparation identity trace"))return false;
    const auto status=implementation.prepareBackend();preparation+=trace.stop();
    if(!require(status==CoinRenderBackendStatus::SUCCESS&&implementation.backend&&implementation.backend->isGpuBackend(),"prepared real backend for passive technical audit"))return false;
    audit=new StereoAudit(std::move(implementation.backend));implementation.backend.reset(audit);return true;
  }
  bool draw(const char *label,bool compareNative=true){
    reference.unbind();scene.resetObservations();audit->intermediate.clear();const auto p=hooks.pre,q=hooks.post,s=hooks.sensor;
    const auto prior=target->getLastSubmissionSerial();CameraState saved(scene.perspective);
    const auto node=scene.base.root->getNodeId();SoOneShotSensor delayed(sensor,&hooks);delayed.setPriority(1);delayed.schedule();
    startup::Trace trace;if(!trace.start())return false;
    if(options.window&&!target->requestWindowReadbackRGBA())return false;
    const auto status=adapter->render();const auto events=trace.stop();Image current=read();
    std::cout<<"manager_stereo_frame label="<<label<<" status="<<status<<" serial="<<prior<<"->"<<current.serial
      <<" captures=";for(auto n:scene.seen.order)std::cout<<n<<",";
    std::cout<<" error="<<adapter->getLastError().getString()<<std::endl;
    bool ok=require(status==CoinRenderAction::SUCCESS&&current.serial==prior+1&&current.rgba.size()==size_t(side)*side*4,
      "one final complete publication per public frame");
    ok &= require(hooks.pre==p+1&&hooks.post==q+1&&hooks.sensor==s+1&&spy.calls==0&&saved.same(scene.perspective)&&node==scene.base.root->getNodeId(),
      "paired hooks/sensors, zero host GL apply, camera and no-touch source restoration");
    if(scene.manager.getSceneGraph()==scene.base.root){
      const unsigned baseCaptures=unsigned(std::count_if(scene.seen.order.begin(),scene.seen.order.end(),[](int value){return value/10==2;}));
      ok &= require(baseCaptures==(scene.manager.getStereoMode()==SoRenderManager::MONO?1u:2u),"one authored base callback per eye across synthetic passes");
    }
    if(!options.window)ok &= require(current.depth.size()==size_t(side)*side&&
      std::all_of(current.depth.begin(),current.depth.end(),[](float z){return std::isfinite(z)&&z>=0&&z<=1;}),"published finite final depth");
    if(options.glTraceRequired&&!identity){identity=physicalGlIdentity(preparation+events);ok &= require(identity,"actual accelerated BGFX OpenGL identity");}
    if(options.wgpuGlTraceRequired&&!identity){identity=physicalWgpuGlIdentity(events,options);ok &= require(identity,"actual accelerated wgpu OpenGL identity");}
    if(options.window)ok &= require(occurrences(events,"COIN_RENDER_PHASE wgpu_surface renderer=")<=1,"at most one surface render");
    if(ok&&scene.stagedIsolation){const auto expected=scene.expectedStagedCaptures();
      ok &= require(scene.seen.producerKinds==expected&&scene.seen.producers==expected[0]+expected[1],
        "STAGED captures each primary/alternate producer once per authored eye/layer occurrence");
      ok &= stagedDelivery();
#ifdef COIN_MANAGER_MONO_BGFX
      if(stagedPipeline){unsigned currentProducers=0,currentRoot=0;bool stale=false;
        std::istringstream lines(events);std::string line;
        while(std::getline(lines,line))if(line.find("COIN_RENDER_PHASE bgfx ")==0){
          const bool current=line.find("readback_latency_frames=0 ")!=std::string::npos;
          if(line.find("readback_pipeline_depth=3 ")!=std::string::npos){++currentProducers;stale|=!current;}
          if(line.find("readback_pipeline_depth=1 ")!=std::string::npos&&current)++currentRoot;
        }
        ok &= require(currentProducers>0&&currentRoot>0&&!stale,
          "actual STAGED producer keeps its three readback slots but publishes current pixels; prepared public target retains depth-one policy");
      }
#endif
    }
    if(!ok)return false;
    if(compareNative){Image expected;const unsigned begins[]={referenceScene.opaqueProducer.begins,referenceScene.opaqueAlternate.begins};
      const unsigned ends[]={referenceScene.opaqueProducer.ends,referenceScene.opaqueAlternate.ends};
      if(!reference.draw(referenceScene,side,options.samples,expected,options.renderer))return false;
      unsigned producerIndex=0;for(auto *scope:{&referenceScene.opaqueProducer,&referenceScene.opaqueAlternate}){
        if(scope->installed)ok &= require(scope->valid&&scope->stack.empty()&&scope->begins>0&&
          scope->begins-begins[producerIndex]==scope->ends-ends[producerIndex],
          "each opaque producer GL color/depth scope pairs and restores before native eye/layer consumer");
        ++producerIndex;
      }
      NativeGeometry geometry(scene,side);const bool msaa=options.samples>1;
      unsigned depthWitnesses=0;auto depthInterior=[&](unsigned x,unsigned y){
        if(!geometry.depth(x,y,msaa))return false;++depthWitnesses;return true;};
      auto rgbInterior=[&](unsigned x,unsigned y){return geometry.rgb(x,y,msaa);};
      ok &= compare(current,expected,side,label,rgbInterior,depthInterior);
      if(!options.window)ok &= require(depthWitnesses>50,"native depth comparison retains authored flat-interior witnesses");
      unsigned bad=0,witnesses=0,reports=0;
      for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){
        if(!geometry.phase(x,y))continue;++witnesses;const size_t at=(size_t(y)*side+x)*4;
        bool mismatch=false;for(unsigned c=0;c<3;++c){const bool different=std::abs(int(current.rgba[at+c])-int(expected.rgba[at+c]))>4;bad+=different;mismatch|=different;}
        if(mismatch&&reports++<5){std::cout<<"manager_stereo_phase_error xy="<<x<<","<<y<<" native=";
          for(unsigned c=0;c<4;++c)std::cout<<(c?",":"")<<unsigned(current.rgba[at+c]);
          std::cout<<" CoinGL=";for(unsigned c=0;c<4;++c)std::cout<<(c?",":"")<<unsigned(expected.rgba[at+c]);
          std::cout<<" bayer_base_top_bottom="<<stereo_stencil_reference::bayerPass(x,y,scene.base.material->transparency[0])
            <<","<<stereo_stencil_reference::bayerPass(x,side-1-y,scene.base.material->transparency[0])
            <<" bayer_fg_top_bottom="<<stereo_stencil_reference::bayerPass(x,y,scene.foreground.material->transparency[0])
            <<","<<stereo_stencil_reference::bayerPass(x,side-1-y,scene.foreground.material->transparency[0])<<std::endl;}

      }
      std::cout<<"manager_stereo_native_geometry phase_witnesses="<<witnesses<<" bad="<<bad<<std::endl;
      ok &= require(witnesses>50&&!bad,"strict in-place native stencil/channel witnesses selected only by authored geometry");
      if(msaa&&scene.manager.getRenderMode()!=SoRenderManager::AS_IS)ok &= commonStrokes(current,expected,geometry);
    }
    stable=std::move(current);return ok;
  }
  bool stagedDelivery(){
    bool ok=true;std::array<unsigned,2> observed{{0,0}};
    auto inspect=[&](const CoinRenderFramePlan &plan){
      for(const auto &draw:plan.draws)for(unsigned kind=0;kind<2;++kind){
        const Quad &consumer=kind?scene.foreground:scene.base;
        if(draw.sourceNodeId!=consumer.shape->getNodeId())continue;
        if(draw.renderStateSlot>=plan.renderStates.size()){ok=false;continue;}
        const auto &state=plan.renderStates[draw.renderStateSlot];if(!state.hasTexture)continue;
        if(state.textureImageSlot>=plan.textures.size()||state.samplerSlot>=plan.samplers.size()){ok=false;continue;}
        ++observed[kind];const auto &image=plan.textures[state.textureImageSlot];const auto &sampler=plan.samplers[state.samplerSlot];
        const auto size=consumer.rtt->size.getValue();const bool mips=consumer.quality->textureQuality.getValue()>.5f;
        const Quad &producer=kind?scene.alternateProducer:scene.producer;const auto color=producer.material->diffuseColor[0];
        size_t tail=0;unsigned width=unsigned(size[0]),height=unsigned(size[1]);
        while(width>1||height>1){width=std::max(1u,width/2);height=std::max(1u,height/2);tail+=size_t(width)*height*4;}
        ok &= require(image.width==unsigned(size[0])&&image.height==unsigned(size[1])&&image.components==4&&
          image.format==CoinRenderTextureFormat::RGBA8_LINEAR&&!image.gpuToken&&!image.producerId&&
          image.pixelsRgba.size()==size_t(size[0])*size[1]*4&&image.mipmapped==mips&&image.mipmapsRgba.size()==(mips?tail:0)&&
          sampler.filter==(mips?CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR:CoinRenderTextureFilter::LINEAR),
          "STAGED submitted extent/format/CPU pixels and full mip tail match this authored producer, never a peer/private-eye token");
        auto pixels=[&](const std::vector<uint8_t> &bytes){unsigned errors=0;
          for(size_t p=0;p+3<bytes.size();p+=4){for(unsigned c=0;c<3;++c)
            errors+=std::abs(int(bytes[p+c])-int(std::lround(color[c]*255)))>1;
            errors+=bytes[p+3]!=255;}
          return errors;
        };
        ok &= require(!pixels(image.pixelsRgba)&&!pixels(image.mipmapsRgba),
          "every STAGED base/mip texel retains its own opaque authored color across producer/occurrence/peer reuse");
      }
    };
    inspect(audit->plan);for(const auto &plan:audit->intermediate)inspect(plan);
    const auto expected=scene.expectedStagedCaptures();
    for(unsigned kind=0;kind<2;++kind)if(expected[kind])ok &= require(observed[kind]>=expected[kind],
      "actual STAGED consumer snapshots expose every active eye and foreground producer");
    std::cout<<"manager_stereo_staged_delivery primary="<<observed[0]<<" alternate="<<observed[1]
      <<" captures="<<scene.seen.producerKinds[0]<<","<<scene.seen.producerKinds[1]<<std::endl;
    return ok;
  }
  bool commonStrokes(const Image &current,const Image &expected,const NativeGeometry &geometry){
    const auto mode=scene.manager.getRenderMode();unsigned common=0,badRgb=0,badDepth=0,nativeOnly=0,glOnly=0,reports=0;
    // Native stereo clears each eye after BACKGROUND traversal. The
    // completed eye's outside-face underlay is the manager's public clear
    // color; the authored background layer material cannot classify strokes.
    const auto clear=scene.manager.getBackgroundColor();SbColor underlay(clear[0],clear[1],clear[2]);
    if(mode==SoRenderManager::HIDDEN_LINE)underlay.setValue(0,0,0);
    const auto vp=scene.manager.getViewportRegion();const auto origin=vp.getViewportOriginPixels();
    for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){const double px=x+.5,py=y+.5;
      if(geometry.foregroundInterior(px,py))continue;const size_t pixel=size_t(y)*side+x;
      for(const auto &quad:geometry.quads){if(!quad.base)continue;
        const bool witness=mode==SoRenderManager::POINTS?quad.corner(px,py,.5):
          quad.edge(px,py)&&!quad.inside(px,py)&&!quad.corner(px,py,1)&&!quad.diagonal(px,py);
        if(!witness)continue;
        const int phase=options.stereo==SoRenderManager::INTERLEAVED_ROWS?int(side-1-y)-origin[1]:int(x)-origin[0];
        const int chosen=(phase&1)?0:1;bool both=false;const unsigned rgbBefore=badRgb,depthBefore=badDepth;
        for(unsigned c=0;c<3;++c){const int wanted=options.stereo==SoRenderManager::ANAGLYPH?(c?1:0):chosen;
          if(wanted!=quad.eye)continue;const int background=int(std::lround(underlay[c]*255));
          const bool a=std::abs(int(current.rgba[pixel*4+c])-background)>4;
          const bool b=std::abs(int(expected.rgba[pixel*4+c])-background)>4;
          nativeOnly+=a&&!b;glOnly+=b&&!a;if(!a||!b)continue;both=true;
          badRgb+=std::abs(int(current.rgba[pixel*4+c])-int(expected.rgba[pixel*4+c]))>4;
        }
        if(!both)continue;++common;
        const int depthEye=options.stereo==SoRenderManager::ANAGLYPH?1:chosen;
        if(quad.eye==depthEye&&!current.depth.empty()&&!expected.depth.empty())
          badDepth+=std::abs(current.depth[pixel]-expected.depth[pixel])>2e-5f;
        if((badRgb!=rgbBefore||badDepth!=depthBefore)&&reports++<5){
          std::cout<<"manager_stereo_common_error xy="<<x<<","<<y<<" eye="<<quad.eye<<" selected="<<chosen<<" native=";
          for(unsigned c=0;c<4;++c)std::cout<<(c?",":"")<<unsigned(current.rgba[pixel*4+c]);
          std::cout<<" CoinGL=";for(unsigned c=0;c<4;++c)std::cout<<(c?",":"")<<unsigned(expected.rgba[pixel*4+c]);
          std::cout<<" underlay="<<underlay[0]<<","<<underlay[1]<<","<<underlay[2];
          if(!current.depth.empty()&&!expected.depth.empty())std::cout<<" depth="<<current.depth[pixel]<<","<<expected.depth[pixel];
          std::cout<<" projected=";for(const auto &p:quad.p)std::cout<<p[0]<<","<<p[1]<<";";std::cout<<std::endl;
        }
      }
    }
    unsigned styles=0;bool depthOnly=false;auto inspect=[&](const CoinRenderFramePlan &plan){
      for(const auto &draw:plan.draws)if(draw.sourceNodeId==scene.base.shape->getNodeId()){
        const auto &state=plan.renderStates[draw.renderStateSlot];styles|=state.polygonOffsetPrimitiveStyle;
        depthOnly|=!state.colorWrite&&state.polygonOffsetPrimitiveStyle==1;
      }
    };inspect(audit->plan);for(const auto &plan:audit->intermediate)inspect(plan);
    bool modeWitness=false;switch(mode){
      case SoRenderManager::POINTS:modeWitness=(styles&4)&&!(styles&3);break;
      case SoRenderManager::WIREFRAME:case SoRenderManager::BOUNDING_BOX:modeWitness=(styles&2)&&!(styles&1);break;
      case SoRenderManager::WIREFRAME_OVERLAY:case SoRenderManager::SHADED_HIDDEN_LINES:modeWitness=(styles&3)==3;break;
      case SoRenderManager::HIDDEN_LINE:modeWitness=(styles&2)&&depthOnly;break;default:break;
    }
    std::cout<<"manager_stereo_qualified_strokes mode="<<mode<<" common="<<common<<" native_only="<<nativeOnly
      <<" gl_only="<<glOnly<<" bad_rgb="<<badRgb<<" bad_depth="<<badDepth<<" primitive_styles="<<styles<<" depth_only="<<depthOnly<<std::endl;
    return require(common>=4&&!badRgb&&!badDepth&&modeWitness,"qualified common stereo stroke RGB4/depth2e-5 and actual primitive mode");
  }
  bool physical(const Image &expected){
    // A passive observer only. It never causes another render or extends the
    // established 5-second deadline for a stale Vulkan presentation.
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {XSync(display,False);XImage *image=XGetImage(display,window,0,0,side,side,AllPlanes,ZPixmap);
      unsigned bad=0;if(!image)return require(false,"physical XImage observation");
      for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){const auto pixel=XGetPixel(image,x,y);const unsigned long masks[]={image->red_mask,image->green_mask,image->blue_mask};
        for(unsigned c=0;c<3;++c){unsigned long mask=masks[c];unsigned shift=0;while(mask&&!(mask&1)){mask>>=1;++shift;}
          unsigned v=mask?unsigned(((pixel>>shift)&mask)*255/mask):0;bad+=std::abs(int(v)-int(expected.rgba[(size_t(y)*side+x)*4+c]))>4;}}
      XDestroyImage(image);if(!bad)return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }while(std::chrono::steady_clock::now()<end);
    return require(false,"physical publication stale/mismatched after 5s (B95 remains a failure)");
  }
  bool normalWindow(){
    if(!options.window)return true;reference.unbind();CameraState saved(scene.perspective);
    const auto serial=target->getLastSubmissionSerial();const unsigned p=hooks.pre,q=hooks.post;
    startup::Trace trace;if(!trace.start())return false;const auto status=adapter->render();const auto events=trace.stop();
    bool ok=require(status==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==serial+1&&saved.same(scene.perspective)&&
      hooks.pre==p+1&&hooks.post==q+1&&spy.calls==0,"normal uncaptured stereo window has one publication and paired host hooks");
#ifndef COIN_MANAGER_MONO_BGFX
    unsigned uncapturedSurface=0;std::istringstream lines(events);std::string line;
    while(std::getline(lines,line))if(line.find("COIN_RENDER_PHASE rust_surface_cpu ")==0&&
      line.find(" capture_requested=0 ")!=std::string::npos&&line.find(" capture_staging_bytes=0 ")!=std::string::npos&&
      line.find(" queue_submissions=1 ")!=std::string::npos)++uncapturedSurface;
    ok &= require(occurrences(events,"COIN_RENDER_PHASE wgpu_surface renderer=")==1&&
      occurrences(events,"COIN_RENDER_PHASE rust_surface_cpu ")==1&&uncapturedSurface==1,
      "same normal window submission has one present/queue submit and zero CPU capture staging");
#else
    ok &= require(occurrences(events,"COIN_RENDER_PHASE bgfx_window_scene_present ")==1,"one physical BGFX scene present for normal stereo frame");
#endif
    return ok&&physical(stable);
  }
  bool loseMainAfterStagedViews(){
    if(!require(scene.stagedIsolation&&!options.window,"loss probe owns an offscreen STAGED producer graph"))return false;
    reference.unbind();scene.resetObservations();const auto before=read();size_t bytes=0;const auto *storage=target->borrowRGBA(bytes);
    const auto expected=scene.expectedStagedCaptures();CameraState camera(scene.perspective);
    const unsigned pre=hooks.pre,post=hooks.post,sensors=hooks.sensor;const auto node=scene.base.root->getNodeId();
    SoOneShotSensor delayed(sensor,&hooks);delayed.setPriority(1);delayed.schedule();
    auto probe=std::make_shared<StereoAudit::LossProbe>();probe->armed=true;audit->loss=probe;
    startup::Trace trace;if(!trace.start())return false;const auto status=adapter->render();trace.stop();
    const auto after=read();size_t afterBytes=0;const auto *afterStorage=target->borrowRGBA(afterBytes);
    // The documented loss contract preserves CPU publication. Do not inspect
    // audit after the failure: Target may already have retired that backend.
    audit=nullptr;
    std::cout<<"manager_stereo_staged_device_loss status="<<status<<" injected="<<probe->injections
      <<" completed_private_views="<<probe->completedPrivateViews<<" serial="<<before.serial<<"->"<<after.serial
      <<" target_status="<<target->getStatus()<<" error="<<adapter->getLastError().getString()<<std::endl;
    return require(status==CoinRenderAction::DEVICE_LOST&&probe->injections==1&&!probe->armed&&probe->completedPrivateViews>=2&&
      std::strstr(adapter->getLastError().getString(),"test-only main DEVICE_LOST")!=nullptr&&
      after.rgba==before.rgba&&after.depth==before.depth&&after.serial==before.serial&&storage==afterStorage&&bytes==afterBytes&&
      hooks.pre==pre+1&&hooks.post==post+1&&hooks.sensor==sensors+1&&spy.calls==0&&camera.same(scene.perspective)&&
      node==scene.base.root->getNodeId()&&scene.seen.producerKinds==expected&&scene.seen.producers==expected[0]+expected[1],
      "one injected main loss after real private/producer work returns with paired hooks/camera and unchanged CPU publication");
  }
  bool reject(CoinRenderAction::Status expected,const char *label){
    reference.unbind();CameraState saved(scene.perspective);const unsigned p=hooks.pre,q=hooks.post;
    size_t bytes=0;const auto *borrowed=target->borrowRGBA(bytes);const Image before=read();
    startup::Trace trace;if(!trace.start())return false;const auto status=adapter->render();const auto events=trace.stop();const auto now=read();
    size_t afterBytes=0;const auto *after=target->borrowRGBA(afterBytes);
    std::cout<<"manager_stereo_refusal label="<<label<<" status="<<status<<" error="<<adapter->getLastError().getString()<<std::endl;
    bool ok=require(status==expected&&adapter->getLastError().getLength()>0&&now.rgba==before.rgba&&now.depth==before.depth&&
      now.serial==before.serial&&borrowed==after&&bytes==afterBytes&&hooks.pre==p+1&&hooks.post==q+1&&saved.same(scene.perspective)&&spy.calls==0,
      "atomic refusal preserves RGBA/depth/serial/borrowed storage, camera and paired hooks");
    ok &= require(!occurrences(events,"COIN_RENDER_PHASE bgfx lower_ms=")&&!occurrences(events,"COIN_RENDER_PHASE bridge pack_ms=")&&
      !occurrences(events,"COIN_RENDER_PHASE wgpu_surface renderer="),"no main or producer GPU execution before complete admission");
    return ok;
  }
  bool resize(unsigned n){reference.unbind();side=n;if(window){XResizeWindow(display,window,n,n);XSync(display,False);}
    both([&](WorldStereo &w){w.manager.setViewportRegion(SbViewportRegion(short(n),short(n)));});
    return require(adapter->resize(SbVec2i32(n,n)),"target resize");}
};
bool composition(Run &r,bool native=true){
  if(!r.draw("complete-independent-eye-composition",native))return false;
  if(r.options.profile=="rtt"&&!require(r.scene.seen.producers==2,
    "base-only analytic RTT manager frame captures its producer once per authored eye"))return false;
  // Compare to complete, independently rendered LEFT/RIGHT frames. They have
  // no manager stereo composition and cannot mask a swapped-eye mutation.
#ifdef COIN_MANAGER_MONO_BGFX
  std::unique_ptr<startup::Environment> ordinaryPipeline;
  if(r.stagedPipeline)ordinaryPipeline.reset(new startup::Environment("COIN_BGFX_READBACK_PIPELINE_DEPTH","1"));
#endif
  auto target=std::unique_ptr<CoinRenderTarget>(samplingTestOffscreen(SbVec2i32(r.side,r.side),r.targetOptions));
  if(!require(target&&target->getStatus()==CoinRenderTarget::TARGET_READY,"independent eye target"))return false;
#ifdef COIN_MANAGER_MONO_BGFX
  if(r.stagedPipeline){if(!require(target->getPimpl().get().prepareBackend()==CoinRenderBackendStatus::SUCCESS,
    "ordinary analytic target prepared under its own depth-one readback policy"))return false;ordinaryPipeline.reset();}
#endif
  target->setDepthReadbackEnabled(TRUE);CoinRenderAction a(r.scene.manager.getViewportRegion());a.setRenderTarget(target.get());
  a.setBackgroundColor(SbColor4f(0,0,0,1));a.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(r.scene.manager.getGLRenderAction()->getTransparencyType()));
  a.setTexturesEnabled(r.scene.manager.isTexturesEnabled());
  using Draw=CoinRenderAction::DrawMode;
  switch(r.scene.manager.getRenderMode()){
    case SoRenderManager::AS_IS:a.setDrawMode(Draw::SCENE);break;
    case SoRenderManager::WIREFRAME:a.setDrawMode(Draw::WIREFRAME);break;
    case SoRenderManager::POINTS:a.setDrawMode(Draw::POINTS);break;
    case SoRenderManager::BOUNDING_BOX:a.setDrawMode(Draw::BOUNDING_BOX);break;
    case SoRenderManager::WIREFRAME_OVERLAY:a.setDrawMode(Draw::WIREFRAME_OVERLAY);break;
    case SoRenderManager::HIDDEN_LINE:a.setDrawMode(Draw::HIDDEN_LINE);break;
    case SoRenderManager::SHADED_HIDDEN_LINES:a.setDrawMode(Draw::SHADED_HIDDEN_LINES);break;
  }
  Image eye[2];CameraState saved(r.scene.perspective);
  r.scene.perspective->setStereoAdjustment(r.scene.manager.getStereoOffset());
  for(unsigned i=0;i<2;++i){r.scene.perspective->setStereoMode(i?SoCamera::RIGHT_VIEW:SoCamera::LEFT_VIEW);a.apply(r.scene.base.root);
    if(!require(a.getLastStatus()==CoinRenderAction::SUCCESS,"ordinary independent eye capture"))return false;
    target->readbackRGBA(eye[i].rgba);target->readbackDepth(eye[i].depth);}
  r.scene.perspective->setStereoMode(saved.eye);r.scene.perspective->setStereoAdjustment(saved.offset);a.setRenderTarget(nullptr);
  if(r.options.profile=="rtt"&&!require(r.scene.seen.producers==4,
    "two explicitly independent ordinary eyes add exactly two producer captures"))return false;
  if((r.options.profile=="cameras"||r.options.profile=="technical")&&r.scene.manager.getRenderMode()==SoRenderManager::AS_IS){
    unsigned full=0,partial=0,odd=0,badCoverage=0;const auto color=r.scene.base.material->diffuseColor[0];
    for(const auto &image:eye)for(size_t pixel=0;pixel<image.rgba.size()/4;++pixel){
      const double fraction=double(image.rgba[pixel*4])/(255.*color[0]);
      const unsigned count=unsigned(std::lround(fraction*r.options.samples));
      badCoverage+=count>r.options.samples;full+=count==r.options.samples;
      partial+=count>0&&count<r.options.samples;odd+=count<r.options.samples&&(count&1);
      for(unsigned c=0;c<3;++c)badCoverage+=std::abs(int(image.rgba[pixel*4+c])-int(std::lround(255*color[c]*count/r.options.samples)))>2;
    }
    std::cout<<"manager_stereo_samples requested="<<r.options.samples<<" full="<<full<<" partial="<<partial<<" odd="<<odd<<" bad="<<badCoverage<<std::endl;
    if(!require(full>100&&!badCoverage&&(r.options.samples==1||partial>0)&&(r.options.samples!=4||odd>0),
      "actual 1/2/4-sample coverage with distinct quarter-sample witnesses"))return false;
  }
  unsigned bad=0,depthBad=0,different=0,wrong=0;
  const auto vp=r.scene.manager.getViewportRegion();const auto o=vp.getViewportOriginPixels(),z=vp.getViewportSizePixels();
  for(unsigned y=0;y<r.side;++y)for(unsigned x=0;x<r.side;++x){const size_t p=size_t(y)*r.side+x;const int localX=int(x)-o[0],localY=int(r.side-1-y)-o[1];
    const bool inside=localX>=0&&localY>=0&&localX<z[0]&&localY<z[1];
    const unsigned chosen=inside&&((r.options.stereo==SoRenderManager::INTERLEAVED_ROWS?localY:localX)&1)?0:1;
    const bool anaglyph=r.options.stereo==SoRenderManager::ANAGLYPH;
    // The native alpha contract is measured separately by --alpha: mono
    // target alpha is not an oracle for the shared ANAGLYPH alpha history.
    const bool opaqueAlpha=r.options.profile=="cameras"||r.options.profile=="technical"||r.options.profile=="shadows";
    for(unsigned c=0;c<(opaqueAlpha?4u:3u);++c){const unsigned wanted=anaglyph?(c==0?0:1):chosen,other=1-wanted;
      bad+=std::abs(int(r.stable.rgba[p*4+c])-int(eye[wanted].rgba[p*4+c]))>2;
      different+=std::abs(int(eye[0].rgba[p*4+c])-int(eye[1].rgba[p*4+c]))>20;
      wrong+=std::abs(int(r.stable.rgba[p*4+c])-int(eye[other].rgba[p*4+c]))>20;}
    if(!r.options.window)depthBad+=std::abs(r.stable.depth[p]-eye[anaglyph?1:chosen].depth[p])>2e-5f;
  }
  std::cout<<"manager_stereo_analytic rgb="<<bad<<" depth="<<depthBad<<" eye_difference="<<different<<" wrong_eye="<<wrong<<std::endl;
  return require(!bad&&!depthBad&&different>100&&wrong>50,"analytic full-eye color/channel/alpha/depth, wrong-eye and phase counterexamples");
}
bool technicalRasterPhase(Run &r){
  // Fixed authored inputs put all four projected corners well away from
  // pixel centers in both eyes. Integer disparity preserves that phase.
  // This qualifies the existing finite GL_LINES/strip profile without
  // changing its RGB4/radius-one comparison or hiding observed pixels.
  const auto *camera=r.scene.perspective;const double near=camera->nearDistance.getValue();
  const double focal=std::max(near,double(camera->focalDistance.getValue())*camera->getBalanceAdjustment());
  const double half= camera->isOfType(SoPerspectiveCamera::getClassTypeId())?
    near*std::tan(static_cast<const SoPerspectiveCamera*>(camera)->heightAngle.getValue()*.5):
    static_cast<const SoOrthographicCamera*>(camera)->height.getValue()*.5;
  const double size=r.side*.5,offset=r.scene.manager.getStereoOffset();
  const double distance=1./(1./focal-14.*half/(size*near*offset));
  auto worldX=[&](double pixel){return (pixel/size-1)*distance*half/near+offset*.5-distance*offset*.5/focal;};
  auto worldY=[&](double pixel){return (1-pixel/size)*distance*half/near;};
  const float left=float(worldX(21.0625)),right=float(worldX(81.0625)),bottom=float(worldY(80.0625)),top=float(worldY(15.0625));
  const float z=float(camera->position.getValue()[2]-distance);
  if(!require(std::isfinite(z)&&distance>near&&distance<camera->farDistance.getValue(),"fixed technical raster phase has a valid authored plane"))return false;
  r.both([&](WorldStereo&w){w.base.setRect(left,right,bottom,top,z);});
  std::cout<<"manager_stereo_technical_fixture disparity_pixels=14 corner_fraction=.0625 rect="<<left<<","<<right<<","<<bottom<<","<<top<<" z="<<z<<std::endl;
  return true;
}
bool layers(Run &r){
  if(!technicalRasterPhase(r)||!composition(r,false))return false;
  r.both([](WorldStereo&w){w.add(w.background,Layer::BACKGROUND|Layer::ZBUFFERON);w.add(w.foreground,0);});
  for(auto mode:{SoRenderManager::AS_IS,SoRenderManager::WIREFRAME,SoRenderManager::POINTS,SoRenderManager::WIREFRAME_OVERLAY,
      SoRenderManager::HIDDEN_LINE,SoRenderManager::BOUNDING_BOX,SoRenderManager::SHADED_HIDDEN_LINES}){
    r.both([&](WorldStereo&w){w.manager.setRenderMode(mode);if(mode==SoRenderManager::POINTS)w.base.authoredPointSize(5);else w.base.resetPointSize();});
    // Legacy HIDDEN_LINE resets glColorMask and clears each eye globally.
    // The completed-eye composition contract intentionally excludes that bug;
    // --technical independently measures both complete HIDDEN_LINE eyes.
    if(!r.draw(("native-mode-"+std::to_string(mode)).c_str(),mode!=SoRenderManager::HIDDEN_LINE))return false;
    const std::vector<int> order={10+int(SoCamera::MONOSCOPIC),20+int(SoCamera::LEFT_VIEW),20+int(SoCamera::RIGHT_VIEW),30+int(SoCamera::MONOSCOPIC)};
    if(!require(r.scene.seen.order==order,"BG once, base once per eye, FG once after mono-camera restoration in every mode"))return false;
    if(!require(color(r.stable,r.side,r.side/2,r.side/2,SbColor(.04f,.8f,.08f)),"FG full-color and filled after stereo/technical base"))return false;
  }
  r.both([](WorldStereo&w){w.manager.setRenderMode(SoRenderManager::AS_IS);w.clear();w.add(w.foreground,Layer::ZBUFFERON);});
  if(!r.draw("foreground-final-eye-depth"))return false;
  r.both([](WorldStereo&w){w.clear();w.add(w.empty,Layer::CLEARZBUFFER);w.add(w.foreground,Layer::ZBUFFERON);});
  if(!r.draw("empty-authored-layer-clears-depth"))return false;
  if(!require(color(r.stable,r.side,r.side/2,r.side/2,SbColor(.04f,.8f,.08f)),"empty CLEARZBUFFER keeps its effect"))return false;
  r.both([](WorldStereo&w){w.clear();auto *l=w.add(w.foreground,0);l->setEnabled(FALSE);});
  if(!r.draw("disabled-layer"))return false;
  if(!require(r.scene.seen.order.size()==2,"disabled layer produces no authored callback"))return false;
  r.both([](WorldStereo&w){w.clear();w.manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
    w.foreground.material->transparency=.5f;auto *l=w.add(w.foreground,0);l->setTransparencyType(SoGLRenderAction::BLEND);});
  if(!r.draw("layer-transparency-override-blend"))return false;
  r.both([](WorldStereo&w){w.clear();w.add(w.foreground,0);});
  if(!r.draw("layer-transparency-inherited-screen-door"))return false;
  r.both([](WorldStereo&w){w.foreground.material->transparency=0;w.manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);});
  r.both([](WorldStereo&w){w.clear();w.manager.setSceneGraph(nullptr);w.add(w.background,Layer::BACKGROUND);w.add(w.foreground,0);});
  return r.draw("null-base-authored-layers");
}
bool resources(Run&r){
  r.both([&](WorldStereo&w){w.base.textures->whichChild=SO_SWITCH_ALL;w.base.image(0,0);w.base.image(1,1);
    w.base.combine->rgbOperation=SoTextureCombine::MODULATE;
    w.foreground.textures->whichChild=SO_SWITCH_ALL;w.foreground.image(0,2);w.foreground.image(1,3);
    w.add(w.foreground,0);w.base.material->diffuseColor.setValue(1,1,1);w.foreground.material->diffuseColor.setValue(1,1,1);
    if(r.options.srgb)for(auto *q:{&w.base,&w.foreground})for(unsigned i=0;i<2;++i)q->image(i,i+1,&w==&r.referenceScene);});
  for(auto transparency:{SoGLRenderAction::SCREEN_DOOR,SoGLRenderAction::BLEND,SoGLRenderAction::DELAYED_BLEND,
      SoGLRenderAction::SORTED_OBJECT_BLEND,SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND}){
    r.both([&](WorldStereo&w){w.manager.getGLRenderAction()->setTransparencyType(transparency);w.base.material->transparency=.35f;
      w.foreground.material->transparency=.25f;});
    r.both([](WorldStereo&w){for(auto *layer:w.layers)layer->setEnabled(FALSE);});
    const bool complete=composition(r,false);
    r.both([](WorldStereo&w){for(auto *layer:w.layers)layer->setEnabled(TRUE);});
    if(!complete||!r.draw(("two-unit-transparency-"+std::to_string(transparency)).c_str()))return false;
  }
  r.both([](WorldStereo&w){w.base.material->transparency=0;w.foreground.material->transparency=0;
    w.manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);w.manager.setTexturesEnabled(FALSE);});
  if(!r.draw("textures-disabled-base-only"))return false;
  const auto a=r.stable;
  r.both([&](WorldStereo&w){w.foreground.image(1,0,r.options.srgb && &w==&r.referenceScene);});if(!r.draw("layer-unit1-edit"))return false;
  if(!require(changed(a,r.stable)>50,"layer texture unit1 remains enabled and distinct"))return false;
  r.both([&](WorldStereo&w){w.foreground.image(1,r.options.srgb?2:3,&w==&r.referenceScene&&r.options.srgb);});
  if(!r.draw("layer-resource-A-B-A"))return false;
  // Restore the exact A author bytes above (sRGB uses unit+1 initially).
  if(!require(a.rgba==r.stable.rgba&&a.depth==r.stable.depth,"resource revision A/B/A restores color/depth exactly"))return false;
  r.both([](WorldStereo&w){w.manager.setTexturesEnabled(TRUE);w.manager.setRenderMode(SoRenderManager::WIREFRAME_OVERLAY);});
  if(!technicalRasterPhase(r))return false;
  r.both([](WorldStereo&w){for(auto *layer:w.layers)layer->setEnabled(FALSE);});
  const bool completeEyes=composition(r,false);
  r.both([](WorldStereo&w){for(auto *layer:w.layers)layer->setEnabled(TRUE);});
  r.both([](WorldStereo&w){w.add(w.background,Layer::BACKGROUND);});
  return completeEyes&&r.draw("technical-textured-layer-keeps-resource-policy");
}
bool stagedSources(Run &r,bool addForeground){
  r.both([&](WorldStereo&w){
    w.stagedIsolation=true;w.producer.setRect(-2.2f,2.2f,-1.1f,1.1f,0);
    w.base.rtt->scene=w.producer.root;w.base.rtt->size.setValue(32,32);w.base.rtt->type=SoSceneTexture2::RGBA8;
    w.base.rtt->transparencyFunction=SoSceneTexture2::NONE;w.base.rtt->model=SoSceneTexture2::REPLACE;
    w.foreground.rtt->scene=w.alternateProducer.root;w.foreground.rtt->size.setValue(32,16);w.foreground.rtt->type=SoSceneTexture2::RGBA8;
    w.foreground.rtt->transparencyFunction=SoSceneTexture2::NONE;w.foreground.rtt->model=SoSceneTexture2::REPLACE;
    w.base.rttSwitch->whichChild=w.foreground.rttSwitch->whichChild=SO_SWITCH_ALL;
    w.base.quality->textureQuality=w.foreground.quality->textureQuality=.1f;
    w.base.isotropicRttReference();w.foreground.isotropicRttReference();if(addForeground)w.add(w.foreground,0);
  });
  return r.referenceScene.opaqueProducer.install(r.referenceScene.producer,r.referenceScene.base.rtt)&&
    r.referenceScene.opaqueAlternate.install(r.referenceScene.alternateProducer,r.referenceScene.foreground.rtt);
}
bool rtt(Run&r){
  r.both([](WorldStereo&w){w.base.rtt->scene=w.producer.root;w.base.rtt->size.setValue(32,32);w.base.rtt->type=SoSceneTexture2::RGBA8;
    w.base.rtt->transparencyFunction=SoSceneTexture2::NONE;w.base.rtt->model=SoSceneTexture2::REPLACE;
    w.base.rttSwitch->whichChild=SO_SWITCH_ALL;w.foreground.rttSwitch->whichChild=SO_SWITCH_ALL;
    w.base.isotropicRttReference();w.foreground.isotropicRttReference();w.add(w.foreground,0);});
  const bool staged=r.options.route==COIN_RENDER_SCENE_TEXTURE_STAGED;
  if(staged){if(!stagedSources(r,false))return false;}
  else if(!r.referenceScene.opaqueProducer.install(r.referenceScene.producer))return false;
#ifdef COIN_MANAGER_MONO_BGFX
  std::unique_ptr<startup::Environment> producerPipeline;
  if(staged&&!r.options.window){r.stagedPipeline=true;producerPipeline.reset(new startup::Environment("COIN_BGFX_READBACK_PIPELINE_DEPTH","3"));}
#endif
  if(!r.draw("RTT-base-eyes-and-monoscopic-FG"))return false;
  if(!require(r.scene.seen.producers==3,"RTT callback once for each eye occurrence and authored FG occurrence"))return false;
  const auto a=r.stable;
  r.both([&](WorldStereo&w){w.producer.material->diffuseColor.setValue(.8f,.2f,.5f);
    if(staged)w.producer.setRect(-2.2f,2.2f,-1.1f,1.1f,-.4f);});if(!r.draw("producer-revision-edit"))return false;
  if(!require(changed(a,r.stable)>100,"producer revision reaches stereo base and FG"))return false;
  r.both([&](WorldStereo&w){w.producer.material->diffuseColor.setValue(.1f,.7f,.9f);
    if(staged)w.producer.setRect(-2.2f,2.2f,-1.1f,1.1f,0);});if(!r.draw("producer-revision-A-B-A"))return false;
  if(!require(a.rgba==r.stable.rgba&&a.depth==r.stable.depth,"RTT A/B/A has no stale revision or eye slot"))return false;
  if(staged){
    r.both([](WorldStereo&w){w.base.rtt->size.setValue(64,32);w.base.quality->textureQuality=.8f;});
    if(!r.draw("STAGED-size-mips-revision")||!require(a.rgba==r.stable.rgba&&a.depth==r.stable.depth,
      "solid producer retains exact consumer pixels while actual extent and complete mip chain change"))return false;
    // Revise the other producer while the first asks for a real unsupported
    // format. No producer GPU execution or publication may escape admission.
    r.both([](WorldStereo&w){w.alternateProducer.material->diffuseColor.setValue(.2f,.85f,.25f);});
    r.scene.base.rtt->type=SoSceneTexture2::RGBA16F;
    if(!r.reject(CoinRenderAction::UNSUPPORTED,"STAGED-HDR-before-any-producer-GPU"))return false;
    r.scene.base.rtt->type=SoSceneTexture2::RGBA8;
    r.both([](WorldStereo&w){w.add(w.foreground,0);});
    if(!r.draw("STAGED-format-recovery-distinct-authored-occurrences")||!require(r.scene.seen.producers==4&&
      color(r.stable,r.side,r.side/2,r.side/2,SbColor(.2f,.85f,.25f)),
      "recovery consumes current alternate producer and preserves two distinct authored FG captures"))return false;
    r.both([](WorldStereo&w){w.manager.removeSuperimposition(w.layers.back());w.layers.pop_back();
      w.base.rtt->size.setValue(32,32);w.base.quality->textureQuality=.1f;
      w.foreground.quality->textureQuality=.8f;w.alternateProducer.material->diffuseColor.setValue(.85f,.2f,.6f);});
    if(!r.resize(97))return false;const auto invalidated=r.read();
    if(!require(!invalidated.serial&&invalidated.rgba.empty()&&invalidated.depth.empty(),
      "STAGED target resize invalidates publication and serial before fresh generation"))return false;
    if(!r.draw("STAGED-resize-new-generation-mips-switch-producer")||!require(r.stable.serial==1&&r.scene.seen.producers==3&&
      color(r.stable,r.side,r.side/2,r.side/2,SbColor(.85f,.2f,.6f)),
      "fresh resized frame restores source A and moves the actual mip requirement to the independent FG producer"))return false;
  }
  r.both([](WorldStereo&w){w.manager.setRenderMode(SoRenderManager::WIREFRAME_OVERLAY);});
  r.both([](WorldStereo&w){for(auto *layer:w.layers)layer->setEnabled(FALSE);});
  std::cout<<"manager_stereo_rtt_analytic extra_ordinary_eye_renders=2 qualified_native_bands_checked=all_pixels"<<std::endl;
  const bool completeEyes=composition(r,false);
  // composition() adds two explicitly independent ordinary-action captures.
  // Check the public manager's authored occurrences before that extra work
  // changes the producer callback total; the following native comparison
  // starts a fresh public frame and retains its original three captures.
  r.both([](WorldStereo&w){for(auto *layer:w.layers)layer->setEnabled(TRUE);w.add(w.background,Layer::BACKGROUND);});
  if(!completeEyes||!r.draw("technical-RTT-no-authored-producer-replay"))return false;
  return require(r.scene.seen.producers==3,"technical synthetic subpasses do not multiply authored RTT capture");
}
bool refusal(Run&r){
  if(!r.draw("before-refusals"))return false;
  r.both([](WorldStereo&w){auto *selection=new SoSelection;selection->addChild(w.base.root);selection->addChild(w.base.root);
    selection->select(w.base.root);w.manager.setSceneGraph(selection);});
  if(!r.draw("real-selection-shared-node-ordinary-GL-action"))return false;
  if(!require(r.scene.seen.order==std::vector<int>{21,21,22,22},"shared authored occurrence captured once per occurrence per eye"))return false;
  r.both([](WorldStereo&w){w.manager.setSceneGraph(w.base.root);});
  r.scene.manager.setStereoMode(SoRenderManager::SEPARATE_OUTPUT);
  if(!r.reject(CoinRenderAction::UNSUPPORTED,"genuine-native-stereo-buffer-unavailable"))return false;
  r.scene.manager.setStereoMode(r.options.stereo);if(!r.draw("hardware-refusal-recovery"))return false;
  const float offset=r.scene.manager.getStereoOffset();r.scene.manager.setStereoOffset(std::numeric_limits<float>::quiet_NaN());
  if(!r.reject(CoinRenderAction::INVALID_SCENE,"nonfinite-manager-stereo-offset"))return false;
  r.scene.manager.setStereoOffset(offset);if(!r.draw("nonfinite-recovery"))return false;
  const float savedNear=r.scene.perspective->nearDistance.getValue(),savedFar=r.scene.perspective->farDistance.getValue();
  const float savedFocal=r.scene.perspective->focalDistance.getValue(),savedBalance=r.scene.perspective->getBalanceAdjustment();
  for(unsigned invalid=0;invalid<5;++invalid){
    if(invalid==0)r.scene.perspective->nearDistance=0;
    if(invalid==1)r.scene.perspective->farDistance=savedNear;
    if(invalid==2)r.scene.perspective->focalDistance=std::numeric_limits<float>::quiet_NaN();
    if(invalid==3)r.scene.perspective->setBalanceAdjustment(std::numeric_limits<float>::infinity());
    if(invalid==4)r.scene.perspective->farDistance=std::numeric_limits<float>::infinity();
    if(!r.reject(CoinRenderAction::INVALID_SCENE,("active-camera-invalid-"+std::to_string(invalid)).c_str()))return false;
    r.scene.perspective->nearDistance=savedNear;r.scene.perspective->farDistance=savedFar;
    r.scene.perspective->focalDistance=savedFocal;r.scene.perspective->setBalanceAdjustment(savedBalance);r.scene.perspective->aspectRatio=1;
    if(!r.draw("active-camera-refusal-recovery"))return false;
  }
  // Left is valid; only the second authored eye contains an invalid index.
  // Add a real producer to prove that no producer runs while admission is incomplete.
  r.scene.base.rtt->scene=r.scene.producer.root;r.scene.base.rtt->size.setValue(32,32);
  r.scene.base.rtt->type=SoSceneTexture2::RGBA8;r.scene.base.rttSwitch->whichChild=SO_SWITCH_ALL;
  r.scene.seen.invalidRight=true;r.scene.resetObservations();
  if(!r.reject(CoinRenderAction::INVALID_SCENE,"right-eye-only-invalid-index"))return false;
  if(!require(std::find(r.scene.seen.order.begin(),r.scene.seen.order.end(),20+int(SoCamera::LEFT_VIEW))!=r.scene.seen.order.end()&&
    std::find(r.scene.seen.order.begin(),r.scene.seen.order.end(),20+int(SoCamera::RIGHT_VIEW))!=r.scene.seen.order.end(),"right-only refusal actually traversed valid left then invalid right"))return false;
  r.scene.seen.invalidRight=false;r.scene.base.shape->coordIndex.set1Value(0,0);r.scene.base.rttSwitch->whichChild=SO_SWITCH_NONE;
  if(!r.draw("right-eye-admission-recovery"))return false;
  r.scene.seen.throwRight=true;
  bool escaped=false;CameraState before(r.scene.perspective);const auto stable=r.read();const unsigned pre=r.hooks.pre,post=r.hooks.post;
  try{r.adapter->render();}catch(const std::runtime_error&){escaped=true;}
  r.scene.seen.throwRight=false;const auto after=r.read();
  if(!require(before.same(r.scene.perspective)&&r.hooks.pre==pre+1&&r.hooks.post==post+1&&after.rgba==stable.rgba&&after.depth==stable.depth&&after.serial==stable.serial,
    "right-eye exception restores camera, pairs finish, preserves publication"))return false;
  std::cout<<"manager_stereo_authored_exception escaped="<<escaped<<std::endl;
  if(!r.draw("exception-recovery"))return false;
  for(bool box:{false,true}){
    SoGLRenderAction *highlight=box?static_cast<SoGLRenderAction*>(new SoBoxHighlightRenderAction):static_cast<SoGLRenderAction*>(new SoLineHighlightRenderAction);
    auto *selection=new SoSelection;selection->ref();selection->addChild(r.scene.base.root);selection->select(r.scene.base.root);
    r.scene.manager.setSceneGraph(selection);r.scene.manager.setGLRenderAction(highlight);
    if(!r.reject(CoinRenderAction::UNSUPPORTED,box?"R35-active-BOX":"R35-active-LINE"))return false;
    r.scene.manager.setStereoMode(SoRenderManager::MONO);
    if(!r.draw("mono-highlight-recovery",false))return false;
    r.scene.manager.setGLRenderAction(&r.spy);r.scene.manager.setSceneGraph(r.scene.base.root);selection->unref();delete highlight;
    r.scene.manager.setStereoMode(r.options.stereo);
  }
  return r.draw("stereo-after-highlight-recovery");
}
bool depth(Run&r){
  r.both([](WorldStereo&w){auto *near=new SoSeparator;w.second.material->diffuseColor.setValue(.05f,.3f,.85f);
    w.second.setRect(-.45f,.35f,-.5f,.55f,1.1f);near->addChild(w.second.material);near->addChild(w.second.geometry);w.base.root->addChild(near);});
  if(!composition(r))return false;const auto a=r.stable;
  if(!r.options.window){unsigned near=0,far=0;for(float z:r.stable.depth){near+=z<.6f;far+=z>.65f&&z<.8f;}
    if(!require(near>50&&far>50,"two visible depth planes create cross-eye occlusion counterexample"))return false;}
  r.both([](WorldStereo&w){w.second.setRect(.15f,.75f,-.5f,.55f,1.1f);});if(!composition(r))return false;
  if(!require(changed(a,r.stable)>100,"near-occluder eye-dependent edit materially changes output"))return false;
  r.both([](WorldStereo&w){w.second.setRect(-.45f,.35f,-.5f,.55f,1.1f);});
  return composition(r)&&require(a.rgba==r.stable.rgba&&a.depth==r.stable.depth,"per-eye depth/cache A/B/A preserves independent composition");
}
bool alpha(Run&r){
  r.both([](WorldStereo&w){w.base.material->diffuseColor.setValue(.8f,.4f,.2f);w.base.material->transparency=.5f;
    w.manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);});
  if(!r.draw("native-stereo-material-BLEND-alpha",false))return false;
  Image expected;if(!r.reference.draw(r.referenceScene,r.side,r.options.samples,expected,r.options.renderer))return false;
  unsigned witnesses=0,wrong=0,overlap=0;
  for(unsigned y=2;y+2<r.side;++y)for(unsigned x=2;x+2<r.side;++x){const size_t p=size_t(y)*r.side+x;
    bool interior=true;for(int dy:{-2,2})for(int dx:{-2,2})for(unsigned c=0;c<4;++c)
      interior &= std::abs(int(expected.rgba[p*4+c])-int(expected.rgba[(size_t(int(y)+dy)*r.side+int(x)+dx)*4+c]))<=2;
    if(!interior||expected.rgba[p*4+3]>250)continue;++witnesses;
    wrong+=std::abs(int(r.stable.rgba[p*4+3])-int(expected.rgba[p*4+3]))>2;
    // With alpha=.5, native GL's two enabled alpha writes give
    // .5*.5 + (.5*.5+1*.5)*.5 = .625 on the overlapping silhouettes.
    overlap+=std::abs(int(expected.rgba[p*4+3])-159)<=2;
  }
  unsigned sampleEdges=0,edgeBad=0;
  if(r.options.stereo==SoRenderManager::ANAGLYPH&&r.options.samples>1){
    // The two silhouettes share these horizontal boundaries. Composing
    // already averaged alpha loses their per-sample correlation.
    for(unsigned y=0;y<r.side;++y)for(unsigned x=r.side*3/8;x<r.side*5/8;++x){const size_t p=size_t(y)*r.side+x;
      const int value=expected.rgba[p*4+3];if(value>=250||std::abs(value-159)<=2)continue;++sampleEdges;
      edgeBad+=std::abs(int(r.stable.rgba[p*4+3])-value)>2;
    }
  }
  std::cout<<"manager_stereo_native_alpha witnesses="<<witnesses<<" bad="<<wrong<<" both_eye_alpha="<<overlap
    <<" sample_edges="<<sampleEdges<<" edge_bad="<<edgeBad<<std::endl;
  if(!require(witnesses>100&&!wrong&&(r.options.stereo!=SoRenderManager::ANAGLYPH||overlap>100)&&!edgeBad&&
    (r.options.stereo!=SoRenderManager::ANAGLYPH||r.options.samples==1||sampleEdges>0),
    "native channel-mask alpha writes accumulate both eyes before MSAA resolve"))return false;
  r.both([](WorldStereo&w){w.foreground.material->transparency=.5f;w.add(w.foreground,0);});
  if(!r.draw("native-alpha-monoscopic-foreground-after-stereo",false))return false;
  if(!r.reference.draw(r.referenceScene,r.side,r.options.samples,expected,r.options.renderer))return false;
  unsigned fgWitnesses=0,fgBad=0;
  for(unsigned y=r.side*3/8;y<r.side*5/8;++y)for(unsigned x=r.side*3/8;x<r.side*5/8;++x){const size_t p=size_t(y)*r.side+x;
    ++fgWitnesses;fgBad+=std::abs(int(r.stable.rgba[p*4+3])-int(expected.rgba[p*4+3]))>2;}
  return require(fgWitnesses>100&&!fgBad,"FG alpha blends once after shared stereo alpha with restored full channels");
}
bool technical(Run&r){
  r.both([](WorldStereo&w){auto *light=new SoDirectionalLight;light->direction.setValue(0,0,-1);light->intensity=.9f;
    w.base.root->insertChild(light,w.base.root->findChild(w.perspective)+1);w.base.material->ambientColor.setValue(0,0,0);
    w.base.material->emissiveColor.setValue(0,0,0);w.base.material->specularColor.setValue(0,0,0);});
  for(auto mode:{SoRenderManager::AS_IS,SoRenderManager::WIREFRAME,SoRenderManager::POINTS,SoRenderManager::WIREFRAME_OVERLAY,
      SoRenderManager::HIDDEN_LINE,SoRenderManager::BOUNDING_BOX,SoRenderManager::SHADED_HIDDEN_LINES}){
    r.both([&](WorldStereo&w){w.manager.setRenderMode(mode);if(mode==SoRenderManager::POINTS)w.base.authoredPointSize(5);else w.base.resetPointSize();});
    if(!composition(r,false))return false;
  }
  return true;
}
bool oit(Run&r){
  r.both([](WorldStereo&w){w.base.material->transparency=.35f;w.second.material->transparency=.55f;
    w.second.material->diffuseColor.setValue(.1f,.7f,.9f);w.base.root->addChild(w.second.root);
    w.base.textures->whichChild=SO_SWITCH_ALL;w.base.image(0,0);w.base.image(1,1);});
  if(!composition(r,false))return false;const auto a=r.stable;
  r.both([](WorldStereo&w){w.base.image(1,3);});if(!composition(r,false))return false;
  if(!require(changed(a,r.stable)>50,"per-eye OIT includes actual resource edit before composition"))return false;
  r.both([](WorldStereo&w){w.base.image(1,1);});if(!composition(r,false))return false;
  return require(a.rgba==r.stable.rgba&&a.depth==r.stable.depth,"OIT resources preserve A/B/A identity across eyes");
}
bool raster(Run&r){
  r.reference.imageReference=true;
  r.both([](WorldStereo&w){
    auto *pixels=new SoImage;std::vector<uint8_t> rgba(17*13*4);
    for(unsigned i=0;i<17*13;++i){rgba[i*4]=220;rgba[i*4+1]=70;rgba[i*4+2]=160;rgba[i*4+3]=i%3?128:255;}
    pixels->image.setValue(SbVec2s(17,13),4,rgba.data());pixels->horAlignment=SoImage::CENTER;pixels->vertAlignment=SoImage::HALF;
    auto *screen=new SoSeparator;auto *position=new SoTranslation;position->translation.setValue(.4f,-.3f,.7f);
    screen->addChild(position);screen->addChild(pixels);w.base.root->addChild(screen);
    auto *annotation=new SoAnnotation;annotation->addChild(w.second.root);w.base.root->addChild(annotation);
  });
  if(!composition(r))return false;
  r.both([](WorldStereo&w){w.second.material->diffuseColor.setValue(.05f,.8f,.5f);});
  return composition(r);
}
struct ShadowFixture {
  SoShadowGroup *group=new SoShadowGroup;SoShadowDirectionalLight *light[2]={new SoShadowDirectionalLight,new SoShadowDirectionalLight};
  SoPerspectiveCamera *mapCamera[2]={new SoPerspectiveCamera,new SoPerspectiveCamera};SoTranslation *move[2]={new SoTranslation,new SoTranslation};
  explicit ShadowFixture(WorldStereo&w){
    group->precision=.125f;group->quality=1;group->visibilityRadius=8;group->epsilon=0;group->threshold=0;group->intensity=1;
    for(int i=0;i<2;++i){auto *map=new SoSeparator;mapCamera[i]->position.setValue(i?.3f:-.3f,0,2.5f);
      // Authored map cameras project casters; receiver lookup remains
      // light-owned. Put the caster in front in that depth domain instead
      // of tuning source near/far, which does not redefine receiver lookup.
      mapCamera[i]->nearDistance=1;mapCamera[i]->farDistance=8;mapCamera[i]->heightAngle=.8f;
      mapCamera[i]->focalDistance=3;mapCamera[i]->setStereoMode(i?SoCamera::RIGHT_VIEW:SoCamera::LEFT_VIEW);
      mapCamera[i]->setStereoAdjustment(.15f+float(i)*.1f);map->addChild(mapCamera[i]);
      auto *casterStyle=new SoShadowStyle;casterStyle->style=SoShadowStyle::CASTS_SHADOW;map->addChild(casterStyle);
      move[i]->translation.setValue(i?.45f:-.45f,0,.9f);map->addChild(move[i]);
      auto *cube=new SoCube;cube->width=.35f;cube->height=.65f;cube->depth=.2f;map->addChild(cube);
      light[i]->direction.setValue(0,0,-1);light[i]->bboxCenter.setValue(0,0,0);light[i]->bboxSize.setValue(4,4,8);
      light[i]->shadowMapScene=map;light[i]->intensity=i?.3f:.55f;group->addChild(light[i]);
    }
    auto *receiver=new SoShadowStyle;receiver->style=SoShadowStyle::SHADOWED;group->addChild(receiver);
    group->addChild(w.base.geometry);w.base.root->replaceChild(w.base.geometry,group);
    for(int i=0;i<w.base.root->getNumChildren();++i)if(w.base.root->getChild(i)->isOfType(SoLightModel::getClassTypeId()))
      static_cast<SoLightModel*>(w.base.root->getChild(i))->model=SoLightModel::PHONG;
  }
};
bool shadows(Run&r){
  ShadowFixture current(r.scene),native(r.referenceScene);
  CameraState c0(current.mapCamera[0]),c1(current.mapCamera[1]);
  // Each completed eye is measured against a real ordinary mono GPU render
  // of that eye, retaining the authored independent shadow-map cameras.
  if(!composition(r,false))return false;const auto a=r.stable;
  if(!require(c0.same(current.mapCamera[0])&&c1.same(current.mapCamera[1]),"manager eye ownership never overwrites independent map cameras"))return false;
  current.group->isActive=FALSE;native.group->isActive=FALSE;
  if(!composition(r,false)||!require(changed(a,r.stable)>5,"positive control: active authored maps produce visible shadows"))return false;
  const auto inactive=r.stable;
  current.move[1]->translation.setValue(-.1f,.3f,.9f);native.move[1]->translation.setValue(-.1f,.3f,.9f);
  if(!composition(r,false)||!require(inactive.rgba==r.stable.rgba&&inactive.depth==r.stable.depth,
    "negative control: moving an inactive map caster does not alter receiver pixels"))return false;
  current.group->isActive=TRUE;native.group->isActive=TRUE;
  if(!composition(r,false)||!require(changed(a,r.stable)>5,"second independent map revision affects actual stereo shadow pixels"))return false;
  current.move[1]->translation.setValue(.45f,0,.9f);native.move[1]->translation.setValue(.45f,0,.9f);
  if(!composition(r,false)||!require(a.rgba==r.stable.rgba&&a.depth==r.stable.depth,"VSM map revisions restore A/B/A without wrong-eye slot alias"))return false;
  current.mapCamera[1]->setBalanceAdjustment(.5f);native.mapCamera[1]->setBalanceAdjustment(.5f);
  return composition(r,false);
}
bool geometryControls(){
  bool ok=true;
  for(unsigned height:{96u,97u}){
    WorldStereo w;SbViewportRegion vp{short(height),short(height)};vp.setViewportPixels(3,5,87,85);w.manager.setViewportRegion(vp);
    NativeGeometry geometry(w,height);const auto point=w.base.coordinates->point[0];
    const double distance=3-point[2],focal=2,halfHeight=std::tan(.36),halfWidth=halfHeight*87/85;
    for(const auto &q:geometry.quads)if(q.base){const double eye=q.eye?.45:-.45;
      const double x=3+43.5*(1+((point[0]-eye)/distance+eye/focal)/halfWidth);
      const double y=height-5-42.5*(1+point[1]/distance/halfHeight);
      ok &= require(std::abs(q.p[0][0]-x)<1e-3&&std::abs(q.p[0][1]-y)<1e-3,
        "authored stereo projection agrees with independent frustum equation at odd-origin odd/even frame extents");
    }
  }
  ProjectedQuad q;q.p[0].setValue(10,80);q.p[1].setValue(90,80);q.p[2].setValue(90,15);q.p[3].setValue(10,15);
  ok &= require(q.edge(10.5,40)&&!q.edge(12,40)&&q.inside(12,40,1)&&!q.inside(9.5,40),
    "geometric boundary is exactly one pixel, independent of images");
  ok &= require(q.corner(13.4,83.4,3.5)&&!q.corner(13.6,83.4,3.5),"5px point footprint plus one pixel has finite authored extent");
  ok &= require(q.diagonal(50,47.5)&&!q.diagonal(50,50),"authored triangle diagonal band follows projected geometry");
  return ok;
}
bool core(){
  if(!geometryControls())return false;
  WorldStereo w;Spy spy;Counts hooks;w.manager.setGLRenderAction(&spy);w.manager.addPreRenderCallback(pre,&hooks);w.manager.addPostRenderCallback(post,&hooks);
  CoinRenderManagerAdapter adapter(w.manager,SbVec2i32(96,96),samplingTestOptions());auto *target=adapter.getSceneManager()->getRenderTarget();
  auto *inspection=new Inspection;target->getPimpl().get().backend.reset(inspection);bool ok=true;
  for(auto mode:{SoRenderManager::ANAGLYPH,SoRenderManager::INTERLEAVED_ROWS,SoRenderManager::INTERLEAVED_COLUMNS}){
    w.manager.setStereoMode(mode);w.clear();w.add(w.background,Layer::BACKGROUND);w.add(w.foreground,0);w.resetObservations();CameraState saved(w.perspective);
    unsigned p=hooks.pre,q=hooks.post;const unsigned attempts=inspection->captures+inspection->submits;const auto status=adapter.render();
    ok &= require(status==CoinRenderAction::UNSUPPORTED&&inspection->captures+inspection->submits>attempts&&hooks.pre==p+1&&hooks.post==q+1&&
      saved.same(w.perspective)&&spy.calls==0&&target->getLastSubmissionSerial()==0,"supported stereo reaches GPU admission with camera/hooks restored, no actual GPU or GL work");
  }
  // Authored redraw sensors remain host-owned even in a stereo manager.
  w.manager.setRenderCallback(redraw,&hooks);w.manager.activate();w.clear();w.add(w.foreground,Layer::AUTOREDRAW);
  SoDB::getSensorManager()->processDelayQueue(TRUE);hooks.redraw=0;
  w.foreground.material->diffuseColor.setValue(.15f,.35f,.55f);SoDB::getSensorManager()->processDelayQueue(TRUE);
  ok &= require(hooks.redraw==1&&spy.calls==0,"stereo AUTOREDRAW schedules one host callback without rendering");
  w.clear();w.add(w.foreground,0);SoDB::getSensorManager()->processDelayQueue(TRUE);hooks.redraw=0;
  w.foreground.material->diffuseColor.setValue(.35f,.15f,.55f);SoDB::getSensorManager()->processDelayQueue(TRUE);
  ok &= require(hooks.redraw==0,"layer lacking AUTOREDRAW does not schedule stereo redraw");
  w.manager.deactivate();w.manager.setRenderCallback(nullptr,nullptr);
  w.manager.setStereoMode(SoRenderManager::SEPARATE_OUTPUT);const auto attempts=inspection->captures+inspection->submits;
  ok &= require(adapter.render()==CoinRenderAction::UNSUPPORTED&&inspection->captures+inspection->submits==attempts,"hardware request refuses before backend execution");
  w.manager.removePreRenderCallback(pre,&hooks);w.manager.removePostRenderCallback(post,&hooks);w.manager.setGLRenderAction(nullptr);return ok;
}
bool cameras(Run&r){
  if(!composition(r))return false;const auto original=r.stable;
  r.both([](WorldStereo&w){w.manager.setStereoOffset(1.25f);});if(!r.draw("offset-edit"))return false;
  if(!require(changed(original,r.stable)>50,"manager offset affects frustum"))return false;
  r.both([](WorldStereo&w){w.perspective->setBalanceAdjustment(.4f);});if(!r.draw("balance-edit"))return false;
  r.both([](WorldStereo&w){w.perspective->setBalanceAdjustment(1);w.manager.setStereoOffset(.9f);});
  if(!r.draw("camera-A-B-A")||!require(original.rgba==r.stable.rgba&&original.depth==r.stable.depth,"camera no-touch setters invalidate eye caches"))return false;
  r.both([&](WorldStereo&w){SbViewportRegion v(short(r.side),short(r.side));v.setViewportPixels(3,5,short(r.side-9),short(r.side-11));w.manager.setViewportRegion(v);});
  if(!composition(r))return false;
  r.both([&](WorldStereo&w){w.base.viewport->enabled=true;SbViewportRegion v(short(r.side),short(r.side));v.setViewportPixels(5,7,short(r.side-13),short(r.side-15));w.base.viewport->region=v;});
  if(!r.draw("nested-viewport-preserves-manager-stencil-phase"))return false;
  r.both([](WorldStereo&w){w.base.viewport->enabled=false;});
  if(!r.resize(97)||!composition(r))return false;
  if(r.options.window&&!r.physical(r.stable))return false;
  return true;
}
bool peers(const Options&o,const CoinRenderOptions&t){
  Run a(o,t),b(o,t);if(!a.ready()||!b.ready())return false;
  if(o.glTraceRequired){
    const auto da=a.audit->real->resourceDomain(),db=b.audit->real->resourceDomain();
    const auto pa=a.audit->resourceDomain(),pb=b.audit->resourceDomain();
    if(!require(da.device&&da.generation&&da.device==db.device&&da.generation==db.generation&&
      pa.device==da.device&&pa.generation==da.generation&&pb.device==db.device&&pb.generation==db.generation,
      "prepared BGFX OpenGL peers and transparent audit expose the same nonzero physical device generation"))return false;
    // Driver identity is emitted once by the physical runtime, which is
    // proved shared above. Keep the actual joint preparation evidence;
    // each target still needs its own successful real GPU publication.
    const std::string physicalPreparation=a.preparation+b.preparation;
    a.preparation=physicalPreparation;b.preparation=physicalPreparation;
    std::cout<<"manager_stereo_peer_physical_domain device="<<da.device<<" generation="<<da.generation
      <<" joint_preparation_identity=1"<<std::endl;
  }
  const bool staged=o.route==COIN_RENDER_SCENE_TEXTURE_STAGED;
  if(staged&&(!stagedSources(a,true)||!stagedSources(b,true)))return false;
#ifdef COIN_MANAGER_MONO_BGFX
  std::unique_ptr<startup::Environment> producerPipeline;
  if(staged&&!o.window){a.stagedPipeline=b.stagedPipeline=true;
    producerPipeline.reset(new startup::Environment("COIN_BGFX_READBACK_PIPELINE_DEPTH","3"));}
#endif
  struct Publication {Image image;const void *storage=nullptr;size_t bytes=0;};
  auto snapshot=[](Run &r){Publication saved;saved.image=r.read();saved.storage=r.target->borrowRGBA(saved.bytes);return saved;};
  auto untouched=[](Run &r,const Publication &saved,const char *label){const auto image=r.read();size_t bytes=0;const auto *storage=r.target->borrowRGBA(bytes);
    return require(image.rgba==saved.image.rgba&&image.depth==saved.image.depth&&image.serial==saved.image.serial&&
      storage==saved.storage&&bytes==saved.bytes,std::string(label)+" preserves peer RGBA/depth/serial and borrowed publication without another render");};
  if(!a.draw("peer-A-first"))return false;const auto original=a.stable;const auto aBefore=snapshot(a);
  b.both([&](WorldStereo&w){if(staged){w.producer.material->diffuseColor.setValue(.8f,.25f,.05f);
      w.alternateProducer.material->diffuseColor.setValue(.2f,.85f,.25f);w.base.rtt->size.setValue(64,32);w.base.quality->textureQuality=.8f;
      w.foreground.rtt->size.setValue(64,32);}else w.base.material->diffuseColor.setValue(.05f,.7f,.9f);});
  if(!b.draw("peer-B-distinct")||!untouched(a,aBefore,"B producer/public frame")||
    !require(changed(original,b.stable)>100,"distinct peer producers/occurrences have visible independent contents"))return false;
  a.both([](WorldStereo&w){w.base.setRect(-.35f,.8f,-.8f,.2f,.8f);});if(!a.draw("peer-A-edited"))return false;
  const auto bBefore=snapshot(b);
  if(!a.resize(97))return false;
  if(staged){const auto invalidated=a.read();if(!require(!invalidated.serial&&invalidated.rgba.empty()&&invalidated.depth.empty(),
    "peer A resize invalidates only A's publication generation before any producer runs"))return false;}
  if(!a.draw("peer-A-resize")||!untouched(b,bBefore,"A producer/attachment resize"))return false;
  if(staged&&!require(a.stable.serial==1,"resized STAGED peer starts a fresh target serial domain"))return false;
  const auto aResized=snapshot(a);
  b.both([&](WorldStereo&w){if(staged){w.producer.material->diffuseColor.setValue(.1f,.7f,.9f);
      w.alternateProducer.material->diffuseColor.setValue(.85f,.2f,.6f);}else w.base.material->diffuseColor.setValue(.8f,.08f,.04f);});
  if(staged){b.scene.base.rtt->type=SoSceneTexture2::RGBA16F;
    if(!b.reject(CoinRenderAction::UNSUPPORTED,"peer-B-STAGED-HDR-preserves-both-peers")||
      !untouched(a,aResized,"B revised-producer refusal"))return false;
    b.scene.base.rtt->type=SoSceneTexture2::RGBA8;
  }
  if(!b.draw("peer-B-cache-return-and-current-producer-recovery")||!untouched(a,aResized,"B cache return"))return false;
  if(!require(original.rgba==b.stable.rgba&&original.depth==b.stable.depth,
    "global revisions, staged source bytes/mips and peer/device lifetimes restore A pixels without eye or producer aliasing"))return false;
  if(staged){const auto bRecovered=snapshot(b);const auto healthyDomain=b.audit->resourceDomain();const std::string physicalEvidence=b.preparation;
    if(o.samples==1){
      a.scene.producer.material->diffuseColor.setValue(.9f,.1f,.7f);
      if(!a.loseMainAfterStagedViews()||!untouched(b,bRecovered,"A injected main DEVICE_LOST"))return false;
    }
    a.reference.unbind();a.adapter.reset();a.target=nullptr;a.audit=nullptr;
    if(!untouched(b,bRecovered,"A adapter/retained-producer teardown"))return false;
    if(o.samples==1){
      // Loss may invalidate the shared runtime. B was observed read-only
      // while A retired; release B too before constructing a manual
      // recovery target, without presuming B is healthy or retryable.
      b.reference.unbind();b.adapter.reset();b.target=nullptr;b.audit=nullptr;
#ifdef COIN_MANAGER_MONO_BGFX
      startup::Environment replacementPolicy("COIN_BGFX_READBACK_PIPELINE_DEPTH","1");
#endif
      Run replacement(o,t);if(!replacement.ready())return false;
#ifdef COIN_MANAGER_MONO_BGFX
      // The replacement is prepared above; newly created producers still
      // exercise the original adversarial three-slot policy.
      startup::Environment replacementProducerPolicy("COIN_BGFX_READBACK_PIPELINE_DEPTH","3");
      replacement.stagedPipeline=true;
#endif
      if(o.glTraceRequired){const auto domain=replacement.audit->resourceDomain();
        if(domain.device&&domain.generation&&domain.device==healthyDomain.device&&domain.generation==healthyDomain.generation)
          replacement.preparation=physicalEvidence+replacement.preparation;
      }
      if(!stagedSources(replacement,true))return false;
      replacement.both([](WorldStereo&w){w.base.rtt->size.setValue(64,32);w.base.quality->textureQuality=.8f;});
      if(!replacement.draw("STAGED-new-target-manual-recovery-after-proxy-loss")||!require(replacement.stable.serial==1&&
        replacement.stable.rgba==original.rgba&&replacement.stable.depth==original.depth,
        "new public target manually recovers exact current STAGED output; no automatic lost-target recovery is presumed"))return false;
    }
  }
  return true;
}
}
int main(int argc,char **argv){using namespace manager_stereo;configureSamplingTest(argc,argv);Options config;config.profile="cameras";
  for(int i=1;i<argc;++i){std::string s=argv[i];if(s=="--core")config.core=true;else if(s=="--window")config.window=true;
    else if(s=="--anaglyph")config.stereo=SoRenderManager::ANAGLYPH;else if(s=="--rows")config.stereo=SoRenderManager::INTERLEAVED_ROWS;
    else if(s=="--columns")config.stereo=SoRenderManager::INTERLEAVED_COLUMNS;else if(s=="--srgb")config.srgb=true;else if(s=="--orthographic")config.orthographic=true;else if(s=="--weighted")config.mechanism=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;else if(s=="--peeling")config.mechanism=COIN_RENDER_TRANSPARENCY_PEELING;
    else if(s=="--layers"||s=="--resources"||s=="--rtt"||s=="--refusal"||s=="--peers"||s=="--cameras"||s=="--shadows"||s=="--raster"||s=="--technical"||s=="--oit"||s=="--alpha"||s=="--depth")config.profile=s.substr(2);
    else if(s=="--direct")config.route=COIN_RENDER_SCENE_TEXTURE_DIRECT;else if(s=="--staged")config.route=COIN_RENDER_SCENE_TEXTURE_STAGED;
    else if(s=="--portable-sampling"){}else if(s=="--samples"&&i+1<argc){config.samples=unsigned(std::atoi(argv[++i]));if(config.samples!=1&&config.samples!=2&&config.samples!=4)return 2;}
    else return 2;}
  XInitThreads();SoDB::init();SoInteraction::init();CoinRenderAction::initClass();Viewport::initClass();if(config.core)return core()?0:1;
  auto options=samplingTestOptions();options.sampleCount=config.samples;options.transparency=config.mechanism;options.sceneTexture=config.route;options.maxTextureAnisotropy=1;
  if(config.srgb)options.storedTextureColorSpace=COIN_RENDER_TEXTURE_SRGB;
  startup::Environment phases("COIN_RENDER_TRACE_PHASES","1"),glTrace("COIN_BGFX_TRACE_GL_ADAPTER","1");
  CoinRenderCapabilities caps{};const auto query=coin_render_query_capabilities_for_renderer(config.window?COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW:
    COIN_RENDER_EXPERIMENTAL_OFFSCREEN,options.renderer,&caps,sizeof(caps));
  bool ids=caps.vendor_id&&caps.device_id;
#ifdef COIN_MANAGER_MONO_BGFX
  config.glTraceRequired=caps.renderer==COIN_RENDER_RENDERER_OPENGL;ids=ids||config.glTraceRequired;
#else
  config.wgpuGlTraceRequired=caps.renderer==COIN_RENDER_RENDERER_OPENGL;if(config.wgpuGlTraceRequired)ids=caps.vendor_id&&acceleratedName(caps.adapter_name);
#endif
  std::cout<<"manager_stereo_environment renderer="<<caps.renderer<<" vendor="<<caps.vendor_id<<" adapter="<<caps.adapter_name<<" samples="<<config.samples<<std::endl;
  if(!require(!query&&caps.gpu_available&&ids&&(config.glTraceRequired||acceleratedName(caps.adapter_name))&&
    (caps.renderer==COIN_RENDER_RENDERER_OPENGL||caps.renderer==COIN_RENDER_RENDERER_VULKAN)&&
    (options.renderer==COIN_RENDER_RENDERER_UNKNOWN||unsigned(options.renderer)==caps.renderer),"real accelerated requested API/device qualification; unavailable is failure"))return 1;
  config.renderer=static_cast<CoinRenderRenderer>(caps.renderer);options.renderer=config.renderer;config.vendor=caps.vendor_id;config.adapterName=caps.adapter_name;
#ifdef COIN_MANAGER_MONO_BGFX
  std::unique_ptr<startup::Environment> publicPipeline;
  if(config.route==COIN_RENDER_SCENE_TEXTURE_STAGED&&!config.window&&(config.profile=="rtt"||config.profile=="peers"))
    publicPipeline.reset(new startup::Environment("COIN_BGFX_READBACK_PIPELINE_DEPTH","1"));
#endif
  if(config.profile=="peers")return peers(config,options)?0:1;
  Run run(config,options);if(!run.ready())return 1;bool ok=false;
  if(config.profile=="layers")ok=layers(run);else if(config.profile=="resources")ok=resources(run);else if(config.profile=="rtt")ok=rtt(run);
  else if(config.profile=="refusal")ok=refusal(run);else if(config.profile=="shadows")ok=shadows(run);else if(config.profile=="raster")ok=raster(run);else if(config.profile=="technical")ok=technical(run);else if(config.profile=="oit")ok=oit(run);else if(config.profile=="alpha")ok=alpha(run);else if(config.profile=="depth")ok=depth(run);else ok=cameras(run);
  if(ok&&config.window)ok=run.physical(run.stable)&&run.normalWindow();return ok?0:1;
}
