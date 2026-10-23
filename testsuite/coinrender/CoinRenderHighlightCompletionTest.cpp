// Reuse the qualified R29 EGL, physical observer and passive backend fixtures.
// The renamed entry point is never called by this executable.
#define main coin_r29_unused_main
#include "CoinRenderManagerStereoContractTest.cpp"
#undef main
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <limits>

namespace highlight_completion {
using namespace manager_stereo;
struct Config : Options {
  bool line=false,cpu=false,paths=false;
  unsigned mode=0,alpha=0;
  std::string test="visual";
};
template<class Base> struct Instrumented : Base {
  unsigned calls=0;
  void apply(SoNode *n)override{++calls;Base::apply(n);}
  void apply(SoPath *p)override{++calls;Base::apply(p);}
  void apply(const SoPathList &p,SbBool obey=FALSE)override{++calls;Base::apply(p,obey);}
};
struct ActionBinding {
  SoRenderManager &manager;
  ActionBinding(SoRenderManager &m,SoGLRenderAction &action):manager(m){manager.setGLRenderAction(&action);}
  ~ActionBinding(){manager.setGLRenderAction(nullptr);}
  ActionBinding(const ActionBinding&)=delete;
  ActionBinding& operator=(const ActionBinding&)=delete;
};
struct Selection {
  WorldStereo &world;bool multiple;
  SoSelection *first=new SoSelection,*second=new SoSelection;
  SoSeparator *branch[2]{};SoSwitch *sw=new SoSwitch;
  SoPath *path[2]{};SoPath *ignored=nullptr;
  SoClipPlane *clip=new SoClipPlane;
  std::vector<int> sequence,boundsSequence,searchSequence;
  struct Probe {Selection *owner;int id;};
  std::array<Probe,3> probes;
  bool invalidReplay=false,invalidResourceRight=false,throwBounds=false,throwSearch=false;
  bool detachSelection=false,detachedDuringCapture=false;
  unsigned replayVisits=0;
  static void callback(void *p,SoAction *action){
    auto &raw=*static_cast<Probe*>(p);auto &owner=*raw.owner;
    if(action->isOfType(SoSearchAction::getClassTypeId())){
      owner.searchSequence.push_back(raw.id*10+int(owner.world.perspective->getStereoMode()));
      if(owner.throwSearch&&raw.id==0)throw std::runtime_error("authored selection Search exception");
      return;
    }
    if(action->isOfType(SoGetBoundingBoxAction::getClassTypeId())){
      owner.boundsSequence.push_back(raw.id*10+int(owner.world.perspective->getStereoMode()));
      if(owner.throwBounds&&raw.id==1)throw std::runtime_error("authored BOX bounds exception");
      return;
    }
    if(!action->isOfType(SoCallbackAction::getClassTypeId())&&
       !action->isOfType(SoGLRenderAction::getClassTypeId())&&
       !action->isOfType(CoinRenderAction::getClassTypeId()))return;
    auto &probe=*static_cast<Probe*>(p);auto &s=*probe.owner;
    const int eye=int(s.world.perspective->getStereoMode());
    s.sequence.push_back(probe.id*10+eye);
    if(probe.id==0&&s.detachSelection&&!s.detachedDuringCapture){
      s.first->removeChild(s.sw);s.detachedDuringCapture=true;
    }
    if(probe.id==0&&s.invalidResourceRight&&eye==int(SoCamera::RIGHT_VIEW))
      s.world.base.rtt->type=SoSceneTexture2::RGBA16F;
    if(probe.id==1&&s.invalidReplay&&++s.replayVisits==2)
      s.world.base.shape->coordIndex.set1Value(0,999999);
  }
  Selection(WorldStereo &w,bool many=false,SoNode *payload=nullptr):world(w),multiple(many){
    if(!payload)payload=w.base.geometry;
    for(unsigned i=0;i<probes.size();++i)probes[i]={this,int(i)};
    // Off-path author callbacks live in a separator, as in the qualified
    // native R35.2 traversal fixture. State-bearing ancestor callbacks below
    // intentionally remain on the selected path and may replay.
    for(int i=0;i<w.base.root->getNumChildren();++i){auto *node=w.base.root->getChild(i);
      if(node->isOfType(SoCallback::getClassTypeId())){auto *scope=new SoSeparator;
        scope->renderCaching=SoSeparator::OFF;scope->addChild(node);w.base.root->replaceChild(i,scope);}
    }
    auto *outside=new SoCallback;outside->setCallback(callback,&probes[0]);
    auto *scope=new SoSeparator;scope->renderCaching=SoSeparator::OFF;scope->addChild(outside);
    w.base.root->insertChild(scope,w.base.root->findChild(w.base.rttSwitch));
    // Select geometry below the authored camera/material/RTT state. A replay
    // has to reconstruct that state without executing extra producers.
    auto *shared=new SoSeparator;shared->renderCaching=SoSeparator::OFF;
    clip->plane.setValue(SbPlane(SbVec3f(0,1,0),0));clip->on=FALSE;
    shared->addChild(clip);shared->addChild(payload);
    for(unsigned i=0;i<(many?2u:1u);++i){
      branch[i]=new SoSeparator;branch[i]->renderCaching=SoSeparator::OFF;
      auto *probe=new SoCallback;probe->setCallback(callback,&probes[i+1]);branch[i]->addChild(probe);
      auto *move=new SoTranslation;move->translation.setValue(many?(i?.46f:-.46f):0,0,0);
      branch[i]->addChild(move);branch[i]->addChild(shared);sw->addChild(branch[i]);
    }
    sw->whichChild=SO_SWITCH_ALL;first->addChild(sw);
    w.base.root->replaceChild(payload,first);
    w.base.root->renderCaching=first->renderCaching=SoSeparator::OFF;
    for(unsigned i=0;i<(many?2u:1u);++i){
      path[i]=new SoPath(first);path[i]->ref();path[i]->append(0);path[i]->append(int(i));path[i]->append(2);
    }
    if(many){
      auto *other=new SoSeparator;auto *move=new SoTranslation;move->translation.setValue(0,.72f,0);
      other->addChild(move);other->addChild(shared);second->addChild(other);w.base.root->addChild(second);
      ignored=new SoPath(second);ignored->ref();ignored->append(0);ignored->append(1);second->select(ignored);
    }
  }
  ~Selection(){for(auto *p:path)if(p)p->unref();if(ignored)ignored->unref();}
  void select(bool rightFirst=false){first->deselectAll();
    if(multiple&&rightFirst)first->select(path[1]);first->select(path[0]);
    if(multiple&&!rightFirst)first->select(path[1]);}
  void reset(){sequence.clear();boundsSequence.clear();searchSequence.clear();replayVisits=0;}
  bool visits(bool line,bool selected,bool rightFirst=false)const{
    std::vector<int> expected;
    const bool mono=world.manager.getStereoMode()==SoRenderManager::MONO;
    const int modes[]={mono?int(world.perspective->getStereoMode()):int(SoCamera::LEFT_VIEW),int(SoCamera::RIGHT_VIEW)};
    for(unsigned e=0;e<(mono?1u:2u);++e){const int eye=modes[e];expected.push_back(eye);
      if(sw->whichChild.getValue()!=SO_SWITCH_NONE){expected.push_back(10+eye);if(multiple)expected.push_back(20+eye);}
      if(line&&selected&&sw->whichChild.getValue()!=SO_SWITCH_NONE){
        if(multiple&&rightFirst)expected.push_back(20+eye);expected.push_back(10+eye);
        if(multiple&&!rightFirst)expected.push_back(20+eye);
      }
    }
    if(sequence!=expected){std::cout<<"highlight_completion_callback actual=";for(int x:sequence)std::cout<<x<<",";
      std::cout<<" expected=";for(int x:expected)std::cout<<x<<",";std::cout<<std::endl;}
    return require(sequence==expected,"native base then selected-path order; no off-path/synthetic replay");
  }
};
SoRenderManager::RenderMode renderMode(unsigned mode){
  const SoRenderManager::RenderMode modes[]={SoRenderManager::AS_IS,SoRenderManager::WIREFRAME,
    SoRenderManager::POINTS,SoRenderManager::WIREFRAME_OVERLAY,SoRenderManager::HIDDEN_LINE,
    SoRenderManager::BOUNDING_BOX,SoRenderManager::SHADED_HIDDEN_LINES};return modes[mode];
}
unsigned strokePixels(const Image &f){unsigned n=0;
  for(size_t p=0;p<f.rgba.size();p+=4)n+=f.rgba[p]>150&&f.rgba[p+1]<90&&f.rgba[p+2]>100;return n;}
bool identical(const Image&a,const Image&b,const char*why){return require(a.rgba==b.rgba&&a.depth==b.depth,why);}

template<class Highlight>
bool capture(Run &r,Selection &s,Highlight &h,const char *label,bool selected=true,bool rightFirst=false){
  s.reset();if(!r.draw(label,false))return false;
  return require(h.calls==0,"instrumentation keeps native runtime type without host GL apply")&&s.visits(r.options.profile=="line",selected,rightFirst);
}
// Completed ordinary mono managers are independent of the stereo selector.
// They still render into GPU attachments with the same requested samples.
// CPU selection below is exclusively a test oracle over their final readbacks.
bool independentEyes(Run &r,bool needWrongEye=true){
  const auto stereo=r.scene.manager.getStereoMode();CameraState saved(r.scene.perspective);
  const auto original=r.stable;const bool mono=stereo==SoRenderManager::MONO;
  Image eyes[2];bool ok=true;
  {
    // R29 executes authorial BG once, then each complete eye owns the
    // manager's public clear. Mono-with-BG would retain its material instead.
    // These finite BG fixtures have full-viewport eyes; copy FG only for the
    // stereo controls. Source manager/layer registrations remain intact.
    SoRenderManager eyeManager;eyeManager.setStereoMode(SoRenderManager::MONO);
    eyeManager.setAutoClipping(SoRenderManager::NO_AUTO_CLIPPING);
    eyeManager.setSceneGraph(r.scene.manager.getSceneGraph());eyeManager.setCamera(r.scene.perspective);
    eyeManager.setViewportRegion(r.scene.manager.getViewportRegion());eyeManager.setBackgroundColor(r.scene.manager.getBackgroundColor());
    eyeManager.setRenderMode(r.scene.manager.getRenderMode());eyeManager.setTexturesEnabled(r.scene.manager.isTexturesEnabled());
    ActionBinding binding(eyeManager,*r.scene.manager.getGLRenderAction());
    for(int i=0;i<r.scene.manager.getNumSuperimpositions();++i){auto *layer=r.scene.manager.getSuperimposition(i);
      if(!mono&&(layer->getStateFlags()&Layer::BACKGROUND))continue;
      auto *copy=eyeManager.addSuperimposition(layer->getSceneGraph(),layer->getStateFlags());copy->setEnabled(layer->isEnabled());
    }
    CoinRenderManagerAdapter ordinary(eyeManager,SbVec2i32(r.side,r.side),r.targetOptions);
    auto *target=ordinary.getSceneManager()->getRenderTarget();
    ok &= require(target&&target->getStatus()==CoinRenderTarget::TARGET_READY,"independent complete-eye GPU target");
    if(target){target->setDepthReadbackEnabled(TRUE);
      r.scene.perspective->setStereoAdjustment(mono?saved.offset:r.scene.manager.getStereoOffset());
      for(unsigned eye=0;ok&&eye<(mono?1u:2u);++eye){
        r.scene.perspective->setStereoMode(mono?saved.eye:(eye?SoCamera::RIGHT_VIEW:SoCamera::LEFT_VIEW));
        const auto status=ordinary.render();target->readbackRGBA(eyes[eye].rgba);target->readbackDepth(eyes[eye].depth);
        ok &= require(status==CoinRenderAction::SUCCESS&&eyes[eye].rgba.size()==size_t(r.side)*r.side*4&&eyes[eye].depth.size()==size_t(r.side)*r.side,
          "ordinary complete eye includes base, native highlight and authored layers");
      }
    }
  }
  r.scene.perspective->setStereoMode(saved.eye);r.scene.perspective->setStereoAdjustment(saved.offset);
  if(!ok)return false;if(mono)eyes[1]=eyes[0];
  unsigned bad=0,depthBad=0,different=0,wrong=0,strokeWrong=0,strokeDifference=0;
  const auto vp=r.scene.manager.getViewportRegion();const auto origin=vp.getViewportOriginPixels(),size=vp.getViewportSizePixels();
  for(unsigned y=0;y<r.side;++y)for(unsigned x=0;x<r.side;++x){const size_t p=size_t(y)*r.side+x;
    const int lx=int(x)-origin[0],ly=int(r.side-1-y)-origin[1];
    const bool inside=lx>=0&&ly>=0&&lx<size[0]&&ly<size[1];
    const unsigned chosen=mono?0:(inside&&((stereo==SoRenderManager::INTERLEAVED_ROWS?ly:lx)&1)?0:1);
    for(unsigned c=0;c<3;++c){const unsigned wanted=mono?0:(stereo==SoRenderManager::ANAGLYPH?(c?1:0):chosen),other=1-wanted;
      bad+=std::abs(int(original.rgba[p*4+c])-int(eyes[wanted].rgba[p*4+c]))>2;
      const bool diff=std::abs(int(eyes[0].rgba[p*4+c])-int(eyes[1].rgba[p*4+c]))>20;
      different+=diff;wrong+=std::abs(int(original.rgba[p*4+c])-int(eyes[other].rgba[p*4+c]))>20;
      const bool stroke=eyes[wanted].rgba[p*4]>150&&eyes[wanted].rgba[p*4+1]<90&&eyes[wanted].rgba[p*4+2]>100;
      strokeDifference+=stroke&&diff;strokeWrong+=stroke&&std::abs(int(original.rgba[p*4+c])-int(eyes[other].rgba[p*4+c]))>20;
    }
    if(!original.depth.empty())depthBad+=std::abs(original.depth[p]-eyes[mono?0:(stereo==SoRenderManager::ANAGLYPH?1:chosen)].depth[p])>2e-5f;
  }
  std::cout<<"highlight_completion_eye_oracle rgb="<<bad<<" depth="<<depthBad<<" eye_difference="<<different
    <<" wrong_eye="<<wrong<<" stroke_difference="<<strokeDifference<<" stroke_wrong="<<strokeWrong<<std::endl;
  return require(!bad&&!depthBad&&(!needWrongEye||mono||(different>50&&wrong>20&&strokeDifference>8&&strokeWrong>8)),
    "complete-eye RGB2/depth2e-5 equivalence with highlight-specific wrong-eye counterexamples");
}
// P02/P07 qualify face interiors outside the authored stroke footprint plus
// one pixel, not universal GL_LINES/expanded-strip endpoint ownership. This
// finite classifier projects only the authored quad and native BOX bounds;
// neither candidate pixels nor reference pixels decide either region.
struct NativeHighlightGeometry {
  ProjectedQuad face,box;
  bool line;double footprint;
  NativeHighlightGeometry(WorldStereo&w,unsigned side,bool isLine,float width):line(isLine),footprint(width*.5+1){
    SbViewportRegion vp;auto *camera=w.perspective;
    auto vv=camera->getViewVolume(w.manager.getViewportRegion(),vp);
    if(camera->getStereoMode()!=SoCamera::MONOSCOPIC){
      SbMatrix view,projection;vv.getMatrices(view,projection);const SbMatrix transform=view.inverse();
      const float offset=camera->getStereoAdjustment()*(camera->getStereoMode()==SoCamera::RIGHT_VIEW?.5f:-.5f);
      const float near=vv.getNearDist(),focal=std::max(near,camera->focalDistance.getValue()*camera->getBalanceAdjustment());
      SbVec3f right=vv.getProjectionDirection().cross(vv.getViewUp());right.normalize();
      const float halfWidth=vv.getWidth()*.5f,halfHeight=vv.getHeight()*.5f,far=near+vv.getDepth();
      vv.frustum(-halfWidth-offset*near/focal,halfWidth-offset*near/focal,-halfHeight,halfHeight,near,far);
      vv.transform(transform);SbMatrix shift;shift.setTranslate(right*offset);vv.transform(shift);
    }
    const auto origin=vp.getViewportOriginPixels(),size=vp.getViewportSizePixels();
    // This finite planar rectangle's native BOX has exactly the same four
    // corners: SoBoxHighlightRenderAction uses the authored bbox size/center.
    for(unsigned i=0;i<4;++i){SbVec3f projected;
      vv.projectToScreen(w.base.coordinates->point[int(i)],projected);
      face.p[i].setValue(origin[0]+projected[0]*size[0],float(side)-origin[1]-projected[1]*size[1]);
      box.p[i]=face.p[i];
    }
  }
  static double outer(const ProjectedQuad&q,double x,double y){double distance=1e30;
    for(unsigned i=0;i<4;++i)distance=std::min(distance,ProjectedQuad::segment(x,y,q.p[i],q.p[(i+1)%4]));return distance;}
  bool band(unsigned x,unsigned y)const{const double px=x+.5,py=y+.5;
    if(outer(face,px,py)<=footprint)return true;
    if(line)return ProjectedQuad::segment(px,py,face.p[0],face.p[2])<=footprint;
    return outer(box,px,py)<=footprint;
  }
  bool straightExterior(unsigned x,unsigned y)const{const double px=x+.5,py=y+.5;const auto&q=line?face:box;
    // Half a pixel from the centerline is fully within the authored 3px
    // stroke even after the native half-pixel quantization. Corners and
    // coincident diagonal endpoints keep their existing geometric exclusion.
    return !face.inside(px,py)&&outer(q,px,py)<=.5&&!q.corner(px,py,footprint)&&
      ProjectedQuad::segment(px,py,face.p[0],face.p[2])>footprint;
  }
};
// Real CoinGL is used only for AS_IS, one-sample, unlayered scenes. It never
// imitates the legacy manager's technical clear/channel-mask defects.
template<class Highlight>
bool nativeOracle(Run &r,Selection &s,Highlight &style){
  Reference native;if(!native.initialize()||!native.buffers(r.side,1))return false;
  Highlight action;action.setColor(style.getColor());action.setLineWidth(style.getLineWidth());
  action.setLinePattern(style.getLinePattern());action.setTransparencyType(style.getTransparencyType());
  action.setCacheContext(SoGLCacheContextElement::getUniqueCacheContext());
  CameraState saved(r.scene.perspective);const auto stereo=r.scene.manager.getStereoMode();
  const bool mono=stereo==SoRenderManager::MONO;Image referenceEye[2];bool ok=true;
  for(unsigned eye=0;eye<(mono?1u:2u);++eye){
    r.scene.perspective->setStereoMode(mono?saved.eye:(eye?SoCamera::RIGHT_VIEW:SoCamera::LEFT_VIEW));
    r.scene.perspective->setStereoAdjustment(mono?saved.offset:r.scene.manager.getStereoOffset());
    native.bind();glBindFramebuffer(GL_FRAMEBUFFER,native.framebuffer);glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);glDisable(GL_MULTISAMPLE);glClearColor(0,0,0,1);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    action.setViewportRegion(r.scene.manager.getViewportRegion());s.reset();action.apply(r.scene.base.root);glFinish();
    const auto nativeSequence=s.sequence,nativeBounds=s.boundsSequence;
    std::vector<uint8_t> pixels(size_t(r.side)*r.side*4);glReadPixels(0,0,r.side,r.side,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    referenceEye[eye].rgba.resize(pixels.size());for(unsigned y=0;y<r.side;++y)
      std::copy_n(pixels.data()+size_t(r.side-1-y)*r.side*4,r.side*4,referenceEye[eye].rgba.data()+size_t(y)*r.side*4);
    ok &= require(glGetError()==GL_NO_ERROR,"native highlight EGL render/readback");native.unbind();
    // Compare this native complete eye to a manager with an ordinary camera.
    r.scene.manager.setStereoMode(SoRenderManager::MONO);
    CoinRenderManagerAdapter ordinary(r.scene.manager,SbVec2i32(r.side,r.side),r.targetOptions);s.reset();
    const auto status=ordinary.render();Image candidate;ordinary.getSceneManager()->getRenderTarget()->readbackRGBA(candidate.rgba);
    ok &= require(status==CoinRenderAction::SUCCESS&&s.sequence==nativeSequence&&s.boundsSequence==nativeBounds,
      "actual native callback order/count matches ordinary highlighted capture");
    NativeHighlightGeometry geometry(r.scene,r.side,r.options.profile=="line",style.getLineWidth());
    unsigned qualified=0,witnesses=0,badStroke=0;
    auto interior=[&](unsigned x,unsigned y){return !geometry.band(x,y);};
    ok &= compare(candidate,referenceEye[eye],r.side,"native-AS_IS-highlight",interior,interior);
    for(unsigned y=0;y<r.side;++y)for(unsigned x=0;x<r.side;++x){
      if(!geometry.band(x,y))++qualified;
      if(!geometry.straightExterior(x,y))continue;++witnesses;
      badStroke+=!color(candidate,r.side,x,y,style.getColor());
      badStroke+=!color(referenceEye[eye],r.side,x,y,style.getColor());
    }
    std::cout<<"highlight_completion_native_geometry qualified="<<qualified<<" fixed_exterior_strokes="<<witnesses<<" bad_rgb4="<<badStroke<<std::endl;
    ok &= require(qualified>50&&witnesses>8&&!badStroke&&strokePixels(candidate)>8&&strokePixels(referenceEye[eye])>8,
      "native RGB4 outside authored bands plus fixed fully-covered exterior highlight witnesses");
    r.scene.manager.setStereoMode(stereo);
  }
  r.scene.perspective->setStereoMode(saved.eye);r.scene.perspective->setStereoAdjustment(saved.offset);
  return ok;
}

template<class Highlight>
bool visual(Run &r,Selection &s,Highlight &h,const Config &c){
  s.first->deselectAll();if(!capture(r,s,h,"unselected-technical-base",false))return false;const auto bare=r.stable;
  s.select();if(!capture(r,s,h,"selected-complete-frame"))return false;const auto selected=r.stable;
  if(!require(changed(bare,selected)>20&&strokePixels(selected)>8,"positive highlight witness rejects global refusal and missing BOX/LINE"))return false;
  if(!independentEyes(r))return false;
  if(c.mode==0&&c.samples==1&&c.test=="visual"&&c.mechanism==COIN_RENDER_TRANSPARENCY_COIN&&!nativeOracle(r,s,h))return false;
  h.setLinePattern(0);if(!capture(r,s,h,"zero-pattern"))return false;
  if(!identical(bare,r.stable,"empty pattern preserves exact technical base"))return false;
  h.setLinePattern(0xffff);h.setVisible(FALSE);if(!capture(r,s,h,"invisible",false))return false;
  if(!identical(bare,r.stable,"invisible highlight preserves RGBA/depth"))return false;
  h.setVisible(TRUE);s.first->deselectAll();if(!capture(r,s,h,"deselected",false))return false;
  if(!identical(bare,r.stable,"deselection restores technical base"))return false;
  s.select();if(!capture(r,s,h,"selection-A-B-A"))return false;
  if(!identical(selected,r.stable,"selection A/B/A restores exact RGBA/depth"))return false;
  if(c.test=="technical"){
    r.scene.manager.setTexturesEnabled(FALSE);if(!capture(r,s,h,"base-textures-disabled"))return false;
    if(!require(strokePixels(r.stable)>8,"technical texture switch cannot disable or recolor highlight"))return false;
    if(!independentEyes(r))return false;r.scene.manager.setTexturesEnabled(TRUE);
    if(!capture(r,s,h,"base-textures-A-B-A")||!identical(selected,r.stable,"texture state A/B/A is exact"))return false;
  }
  return true;
}
template<class Highlight>
bool history(Run&r,Selection&s,Highlight&h){
  s.select();if(!capture(r,s,h,"history-A"))return false;const auto a=r.stable;
  h.setLineWidth(1);if(!capture(r,s,h,"thin"))return false;const auto thin=r.stable;
  h.setLineWidth(5);if(!capture(r,s,h,"wide")||!require(strokePixels(r.stable)>strokePixels(thin),"line width changes actual coverage"))return false;
  h.setLinePattern(0x00ff);if(!capture(r,s,h,"pattern")||!require(changed(a,r.stable)>8,"pattern changes actual coverage"))return false;
  h.setLineWidth(3);h.setLinePattern(0xffff);h.setColor(SbColor(.1f,.9f,.85f));
  if(!capture(r,s,h,"color")||!require(changed(a,r.stable)>20,"color setter without graph touch reaches highlight"))return false;
  h.setColor(SbColor(1,.15f,.8f));if(!capture(r,s,h,"style-A-B-A")||!identical(a,r.stable,"style A/B/A exact"))return false;
  r.scene.perspective->setBalanceAdjustment(.4f);if(!capture(r,s,h,"balance-no-touch")||!independentEyes(r))return false;
  if(!require(changed(a,r.stable)>20,"camera setter changes actual highlighted eye bounds"))return false;
  r.scene.perspective->setBalanceAdjustment(1);if(!capture(r,s,h,"camera-A-B-A")||!identical(a,r.stable,"camera setter A/B/A exact"))return false;
  SbViewportRegion vp(short(r.side),short(r.side));vp.setViewportPixels(3,5,short(r.side-9),short(r.side-11));r.scene.manager.setViewportRegion(vp);
  if(!capture(r,s,h,"odd-origin-viewport")||!independentEyes(r))return false;
  if(!r.resize(97)||!capture(r,s,h,"resize97")||!independentEyes(r))return false;
  return true;
}
template<class Highlight>
bool resources(Run&r,Selection&s,Highlight&h,const Config&c){
  s.first->deselectAll();if(!capture(r,s,h,"resources-unselected",false))return false;
  const auto bare=r.stable;const auto ordinary=r.scene.seen.producerKinds;
  const auto ordinarySubmits=r.audit->producers;
  s.select();if(!capture(r,s,h,"resources-selected"))return false;const auto a=r.stable;
  if(!require(changed(a,bare)>20&&strokePixels(a)>8&&r.scene.seen.producerKinds==ordinary,
    "highlight is visible without recapturing primary/foreground producers"))return false;
  if(!require(r.audit->producers-ordinarySubmits<=ordinarySubmits,
    "highlight adds no GPU producer execution over the same ordinary frame"))return false;
  if(!independentEyes(r))return false;
  r.scene.producer.material->diffuseColor.setValue(.8f,.25f,.05f);
  r.scene.alternateProducer.material->diffuseColor.setValue(.2f,.85f,.25f);
  if(!capture(r,s,h,"distinct-producer-revisions")||!independentEyes(r))return false;
  if(!require(changed(a,r.stable)>100&&color(r.stable,r.side,r.side/2,r.side/2,SbColor(.2f,.85f,.25f)),
    "base/FG consume distinct current producer resources, never the other slot"))return false;
  r.scene.producer.material->diffuseColor.setValue(.1f,.7f,.9f);
  r.scene.alternateProducer.material->diffuseColor.setValue(.85f,.2f,.6f);
  if(!capture(r,s,h,"producer-A-B-A")||!identical(a,r.stable,"producer revisions A/B/A exact"))return false;
  if(c.route==COIN_RENDER_SCENE_TEXTURE_STAGED){
    r.scene.base.rtt->size.setValue(64,32);r.scene.base.quality->textureQuality=.8f;
    if(!capture(r,s,h,"current-producer-extent-and-mips")||!independentEyes(r))return false;
    r.scene.base.rtt->type=SoSceneTexture2::RGBA16F;
    if(!r.reject(CoinRenderAction::UNSUPPORTED,"active-highlight-real-RTT-refusal"))return false;
    r.scene.base.rtt->type=SoSceneTexture2::RGBA8;
    if(!capture(r,s,h,"current-producer-recovery"))return false;
  }
  return true;
}
template<class Highlight>
bool layers(Run&r,Selection&s,Highlight&h){
  s.select();if(!capture(r,s,h,"BG-base-highlight-FG")||!independentEyes(r))return false;
  const auto layered=r.stable;
  auto *fg=r.scene.layers.back();fg->setEnabled(FALSE);
  if(!capture(r,s,h,"foreground-disabled"))return false;const auto plain=r.stable;
  unsigned replaced=0;
  // Fixed authored ROI crosses the upper horizontal outline common to both
  // eyes. ANAGLYPH splits the displaced vertical edges into R and GB, while
  // this common segment retains the complete magenta highlight color.
  for(unsigned y=r.side/12;y<r.side/4;++y)for(unsigned x=r.side/4;x<r.side*3/5;++x){
    const size_t p=(size_t(y)*r.side+x)*4;
    const bool wasStroke=plain.rgba[p]>150&&plain.rgba[p+1]<90&&plain.rgba[p+2]>100;
    const bool green=layered.rgba[p+1]>150&&layered.rgba[p]<40&&layered.rgba[p+2]<40;
    replaced+=wasStroke&&green;
  }
  if(!require(replaced>5,"full-color authored FG covers actual highlight after eye selection"))return false;
  fg->setEnabled(TRUE);if(!capture(r,s,h,"layer-A-B-A")||!identical(layered,r.stable,"layers A/B/A restores exact RGBA/depth"))return false;
  r.scene.base.geometry->whichChild=SO_SWITCH_NONE;s.first->deselectAll();
  if(!capture(r,s,h,"empty-base-retains-authorial-layers",false))return false;
  const auto bgVisits=std::count_if(r.scene.seen.order.begin(),r.scene.seen.order.end(),[](int n){return n/10==1;});
  const auto fgVisits=std::count_if(r.scene.seen.order.begin(),r.scene.seen.order.end(),[](int n){return n/10==3;});
  if(!independentEyes(r,false))return false;
  const auto clear=r.scene.manager.getBackgroundColor();
  const SbColor underlay=r.scene.manager.getStereoMode()==SoRenderManager::MONO?SbColor(.04f,.08f,.8f):SbColor(clear[0],clear[1],clear[2]);
  return require(bgVisits==1&&fgVisits==1&&color(r.stable,r.side,3,r.side/2,underlay)&&
    color(r.stable,r.side,r.side/2,r.side/2,SbColor(.04f,.8f,.08f)),
    "empty base preserves one authorial BG/FG traversal, per-eye public clear and full-color FG");
}
template<class Highlight>
bool paths(Run&r,Selection&s,Highlight&h){
  s.first->deselectAll();if(!capture(r,s,h,"shared-occurrences-bare",false))return false;const auto bare=r.stable;
  s.select(true);if(!capture(r,s,h,"path-order-RL",true,true)||!independentEyes(r))return false;const auto a=r.stable;
  if(!require(changed(a,bare)>20&&strokePixels(a)>8,"shared selected path occurrences generate actual highlight"))return false;
  s.select(false);if(!capture(r,s,h,"path-order-LR")||!independentEyes(r))return false;
  s.select(true);if(!capture(r,s,h,"path-order-RL-A-B-A",true,true)||!identical(a,r.stable,"selection order A/B/A exact"))return false;
  s.sw->whichChild=SO_SWITCH_NONE;if(!capture(r,s,h,"SoSwitch-NONE",true,true))return false;
  if(!require(strokePixels(r.stable)==0,"inactive selected occurrences and second SoSelection are not highlighted"))return false;
  s.sw->whichChild=SO_SWITCH_ALL;if(!capture(r,s,h,"SoSwitch-ALL-recovery",true,true)||!identical(a,r.stable,"SoSwitch path A/B/A exact"))return false;
  return true;
}
template<class Highlight>
bool rejectException(Run&r,Selection&s,Highlight&h,bool bounds,bool search=false){
  r.reference.unbind();s.reset();const auto before=r.read();size_t bytes=0;const void *borrow=r.target->borrowRGBA(bytes);
  CameraState camera(r.scene.perspective);const auto pre=r.hooks.pre,post=r.hooks.post;
  bool escaped=false;auto status=CoinRenderAction::SUCCESS;
  if(bounds)s.throwBounds=true;else if(search)s.throwSearch=true;else r.scene.seen.throwRight=true;
  startup::Trace trace;if(!trace.start())return false;
  try{status=r.adapter->render();}catch(const std::runtime_error&){escaped=true;}
  const auto events=trace.stop();s.throwBounds=false;s.throwSearch=false;r.scene.seen.throwRight=false;
  const auto after=r.read();size_t afterBytes=0;const void *afterBorrow=r.target->borrowRGBA(afterBytes);
  bool ok=require((escaped||status!=CoinRenderAction::SUCCESS)&&(!bounds||!s.boundsSequence.empty())&&(!search||!s.searchSequence.empty())&&
    before.rgba==after.rgba&&before.depth==after.depth&&before.serial==after.serial&&borrow==afterBorrow&&bytes==afterBytes&&
    camera.same(r.scene.perspective)&&r.hooks.pre==pre+1&&r.hooks.post==post+1&&h.calls==0,
    bounds?"BOX bounds exception preserves complete public frame and paired state":(search?"selection Search exception preserves complete public frame and paired state":"right authorial exception preserves complete public frame and paired state"));
  ok &= require(!occurrences(events,"COIN_RENDER_PHASE bgfx lower_ms=")&&!occurrences(events,"COIN_RENDER_PHASE bridge pack_ms="),
    "exception admission performs no main/producer GPU work");
  if(!ok||!capture(r,s,h,"exception-next-valid-capture"))return false;
  return identical(before,r.stable,"exception recovery restores exact highlighted RGBA/depth");
}
template<class Highlight>
bool refusal(Run&r,Selection&s,Highlight&h,const Config&c){
  s.select();if(!capture(r,s,h,"transactional-good"))return false;const auto good=r.stable;
  const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
  h.setLineWidth(nan);if(!r.reject(CoinRenderAction::INVALID_SCENE,"nonfinite-active-width"))return false;
  h.setVisible(FALSE);if(!capture(r,s,h,"invisible-invalid-width",false))return false;
  h.setVisible(TRUE);s.first->deselectAll();if(!capture(r,s,h,"unselected-invalid-width",false))return false;
  h.setLineWidth(3);s.select();h.setColor(SbColor(inf,.15f,.8f));
  if(!r.reject(CoinRenderAction::INVALID_SCENE,"nonfinite-active-color"))return false;
  h.setColor(SbColor(1,.15f,.8f));if(!capture(r,s,h,"style-invalid-recovery")||!identical(good,r.stable,"invalid style recovery exact"))return false;
  for(auto mode:{SoRenderManager::SEPARATE_OUTPUT,SoRenderManager::QUAD_BUFFER}){
    r.scene.manager.setStereoMode(mode);if(!r.reject(CoinRenderAction::UNSUPPORTED,"unsupported-native-buffer-domain"))return false;
    r.scene.manager.setStereoMode(c.stereo);if(!capture(r,s,h,"unsupported-domain-recovery"))return false;
  }
  r.scene.seen.invalidRight=true;if(!r.reject(CoinRenderAction::INVALID_SCENE,"invalid-only-right-geometry"))return false;
  r.scene.seen.invalidRight=false;r.scene.base.shape->coordIndex.set1Value(0,0);
  if(!capture(r,s,h,"right-geometry-recovery"))return false;
  r.scene.base.rtt->scene=r.scene.producer.root;r.scene.base.rtt->size.setValue(32,32);
  r.scene.base.rtt->type=SoSceneTexture2::RGBA8;r.scene.base.rttSwitch->whichChild=SO_SWITCH_ALL;
  s.invalidResourceRight=true;s.reset();if(!r.reject(CoinRenderAction::UNSUPPORTED,"resource-invalid-only-right"))return false;
  s.invalidResourceRight=false;r.scene.base.rtt->type=SoSceneTexture2::RGBA8;
  r.scene.base.rttSwitch->whichChild=SO_SWITCH_NONE;
  if(!capture(r,s,h,"right-resource-recovery")||!identical(good,r.stable,"right resource failure retained original state"))return false;
  // Invalidate an actual selected occurrence after adapter sync. SoPath
  // auditing can truncate it automatically; that is still stale capture state.
  s.sw->ref();s.detachSelection=true;s.detachedDuringCapture=false;s.reset();
  const bool staleRejected=r.reject(CoinRenderAction::INVALID_SCENE,"selected-path-detached-during-base");
  s.detachSelection=false;const bool didDetach=s.detachedDuringCapture;
  if(didDetach)s.first->addChild(s.sw);
  for(auto *path:s.path)if(path){path->truncate(1);path->append(0);path->append(path==s.path[1]?1:0);path->append(2);}
  s.first->deselectAll();s.select();s.sw->unref();
  if(!require(staleRejected&&didDetach,"actual selected path became stale during base capture")||
    !capture(r,s,h,"stale-path-restored")||!identical(good,r.stable,"restored path and selection recover on same target"))return false;
  if(c.line){s.invalidReplay=true;s.reset();if(!r.reject(CoinRenderAction::INVALID_SCENE,"invalid-index-only-selected-replay"))return false;
    s.invalidReplay=false;r.scene.base.shape->coordIndex.set1Value(0,0);
    if(!capture(r,s,h,"selected-replay-recovery")||!identical(good,r.stable,"replay failure recovery exact"))return false;
  }
  if(!rejectException(r,s,h,!c.line)||!rejectException(r,s,h,false)||!rejectException(r,s,h,false,true))return false;
  // A selected path from a different, still-live graph is not an occurrence
  // of this manager root and cannot silently paint or execute that graph.
  auto *foreign=new SoSeparator;foreign->ref();auto *foreignShape=new SoCube;foreign->addChild(foreignShape);
  auto *wrong=new SoPath(foreign);wrong->ref();wrong->append(0);s.first->deselectAll();s.first->select(wrong);
  const bool retained=s.first->getNumSelected()>0;bool ok=true;
  if(retained)ok &= r.reject(CoinRenderAction::INVALID_SCENE,"foreign-selected-path");
  else ok &= require(s.first->getNumSelected()==0,"public SoSelection rejects foreign path before capture");
  s.first->deselectAll();wrong->unref();foreign->unref();s.select();
  return ok&&capture(r,s,h,"path-refusal-recovery")&&identical(good,r.stable,"path validation leaves reusable action/camera state");
}
template<class Highlight>
bool shadows(Run&r,Selection&s,Highlight&h,ShadowFixture &shadow){
  std::array<unsigned,2> maps{{0,0}};
  for(unsigned i=0;i<2;++i){auto *callback=new SoCallback;
    callback->setCallback([](void *p,SoAction *a){
      if(a->isOfType(SoCallbackAction::getClassTypeId())||a->isOfType(CoinRenderAction::getClassTypeId()))++*static_cast<unsigned*>(p);
    },&maps[i]);
    static_cast<SoGroup*>(shadow.light[i]->shadowMapScene.getValue())->insertChild(callback,0);
  }
  s.first->deselectAll();if(!capture(r,s,h,"VSM-unselected",false))return false;const auto bare=r.stable;
  const auto ordinaryMaps=maps;maps={{0,0}};
  s.select();if(!capture(r,s,h,"VSM-highlighted"))return false;const auto a=r.stable;
  if(!require(maps==ordinaryMaps&&maps[0]>0&&maps[1]>0,"highlight captures no extra shadow-map scenes"))return false;
  if(!independentEyes(r))return false;
  if(!require(changed(bare,a)>20&&strokePixels(a)>8,"VSM receiver keeps a real generated highlight"))return false;
  CameraState c0(shadow.mapCamera[0]),c1(shadow.mapCamera[1]);
  shadow.group->isActive=FALSE;if(!capture(r,s,h,"maps-disabled")||!independentEyes(r))return false;
  if(!require(changed(a,r.stable)>5,"actual VSM maps change receiver pixels"))return false;
  const auto noMaps=r.stable;shadow.move[1]->translation.setValue(-.1f,.3f,.9f);
  if(!capture(r,s,h,"inactive-caster-edit")||!identical(noMaps,r.stable,"inactive map negative control"))return false;
  shadow.group->isActive=TRUE;if(!capture(r,s,h,"second-map-current")||!independentEyes(r))return false;
  if(!require(changed(a,r.stable)>5,"second map current caster has visible independent effect"))return false;
  shadow.move[1]->translation.setValue(.45f,0,.9f);
  if(!capture(r,s,h,"map-A-B-A")||!identical(a,r.stable,"VSM caster A/B/A exact"))return false;
  return require(c0.same(shadow.mapCamera[0])&&c1.same(shadow.mapCamera[1]),"highlight/eye overrides preserve map cameras");
}

template<class Highlight>
bool occlusion(Run&r,Selection&s,Highlight&h,const Config&c){
  s.select();if(!capture(r,s,h,"unoccluded-highlight"))return false;const auto a=r.stable;
  auto *occluder=new SoSeparator;occluder->ref();
  r.scene.second.setRect(0,.85f,-.85f,.85f,1.1f);r.scene.second.material->diffuseColor.setValue(.04f,.1f,.85f);
  occluder->addChild(r.scene.second.material);occluder->addChild(r.scene.second.geometry);r.scene.base.root->addChild(occluder);
  if(!capture(r,s,h,"closer-authored-occluder")||!independentEyes(r))return false;const auto covered=r.stable;
  unsigned blue=0;for(size_t p=0;p<covered.rgba.size();p+=4)
    blue+=covered.rgba[p]<40&&covered.rgba[p+1]<50&&covered.rgba[p+2]>150;
  if(!require(blue>100&&strokePixels(covered)+8<strokePixels(a),"closer authored depth occludes actual generated highlight"))return false;
  r.scene.base.root->removeChild(occluder);occluder->unref();
  if(!capture(r,s,h,"occluder-A-B-A")||!identical(a,r.stable,"depth state A/B/A exact"))return false;
  if(c.line){s.clip->on=TRUE;if(!capture(r,s,h,"line-inherits-clipping")||!independentEyes(r))return false;
    if(!require(strokePixels(r.stable)+8<strokePixels(a)&&strokePixels(r.stable)>8,"active clip plane removes lower outline and keeps upper outline"))return false;
    s.clip->on=FALSE;if(!capture(r,s,h,"clip-A-B-A")||!identical(a,r.stable,"clip state A/B/A exact"))return false;
  }
  return true;
}
template<class Highlight>
bool peers(const Config&c,const CoinRenderOptions&options){
  Run a(c,options),b(c,options);if(!a.ready()||!b.ready())return false;
  if(c.glTraceRequired){
    const auto da=a.audit->real->resourceDomain(),db=b.audit->real->resourceDomain();
    const auto pa=a.audit->resourceDomain(),pb=b.audit->resourceDomain();
    if(!require(da.device&&da.generation&&da.device==db.device&&da.generation==db.generation&&
      pa.device==da.device&&pa.generation==da.generation&&pb.device==db.device&&pb.generation==db.generation,
      "prepared BGFX OpenGL peers and audits share one nonzero physical device generation"))return false;
    // R29: this real driver identity is emitted once per physical runtime.
    // Share only preparation actually observed from these proved peers;
    // both targets still independently publish and pass their identity gate.
    const std::string physicalPreparation=a.preparation+b.preparation;
    a.preparation=physicalPreparation;b.preparation=physicalPreparation;
    std::cout<<"highlight_completion_peer_physical_domain device="<<da.device<<" generation="<<da.generation
      <<" joint_preparation_identity=1"<<std::endl;
  }
  if(!stagedSources(a,true)||!stagedSources(b,true))return false;
  if(c.route==COIN_RENDER_SCENE_TEXTURE_DIRECT)a.scene.stagedIsolation=b.scene.stagedIsolation=false;
  Selection sa(a.scene),sb(b.scene);sa.select();sb.select();Highlight ha,hb;
  for(auto *h:{&ha,&hb}){h->setColor(SbColor(1,.15f,.8f));h->setLineWidth(3);h->setLinePattern(0xffff);}
  ActionBinding bindA(a.scene.manager,ha),bindB(b.scene.manager,hb);
  auto untouched=[](Run&r,const Image&prior,const void*borrow,size_t bytes){size_t n=0;const void *now=r.target->borrowRGBA(n);const auto f=r.read();
    return require(f.rgba==prior.rgba&&f.depth==prior.depth&&f.serial==prior.serial&&now==borrow&&n==bytes,
      "other target preserves RGBA/depth/serial/borrow without a render");};
  if(!capture(a,sa,ha,"peer-A-first"))return false;const auto first=a.stable;size_t bytesA=0;const void *borrowA=a.target->borrowRGBA(bytesA);
  b.scene.producer.material->diffuseColor.setValue(.8f,.25f,.05f);b.scene.alternateProducer.material->diffuseColor.setValue(.2f,.85f,.25f);
  if(!capture(b,sb,hb,"peer-B-distinct")||!independentEyes(b)||!untouched(a,first,borrowA,bytesA))return false;
  if(!require(changed(first,b.stable)>100,"peer resource identities are visibly distinct"))return false;
  const auto second=b.stable;size_t bytesB=0;const void *borrowB=b.target->borrowRGBA(bytesB);
  if(!a.resize(97)||!capture(a,sa,ha,"peer-A-resize")||!independentEyes(a)||!untouched(b,second,borrowB,bytesB))return false;
  const auto resized=a.stable;borrowA=a.target->borrowRGBA(bytesA);
  b.scene.base.rtt->type=SoSceneTexture2::RGBA32F;
  if(!b.reject(CoinRenderAction::UNSUPPORTED,"peer-B-unsupported-resource")||!untouched(a,resized,borrowA,bytesA))return false;
  b.scene.base.rtt->type=SoSceneTexture2::RGBA8;b.scene.producer.material->diffuseColor.setValue(.1f,.7f,.9f);
  b.scene.alternateProducer.material->diffuseColor.setValue(.85f,.2f,.6f);
  const bool ok=capture(b,sb,hb,"peer-B-resource-A-B-A")&&identical(first,b.stable,"peer cache return restores original exact highlighted frame")&&untouched(a,resized,borrowA,bytesA);
  return ok;
}


// The fitted directional lookup for bboxSize(4,4,8) has half-extent
// K=sqrt(24). The independent perspective map has half-aperture
// A=(cameraZ-casterZ)*tan(heightAngle/2). Solve its source projection so
// the receiver shadows, rather than the raw casters, have authored centers
// inside the selected quad. Retain both maps' distinct stereo camera state.
void configureCoupledReceiver(ShadowFixture &fixture){
  const double lookup=std::sqrt(24.);
  const SbVec2f centers[]={SbVec2f(-.4f,.3f),SbVec2f(.1f,.4f)};
  for(unsigned i=0;i<2;++i){auto *camera=fixture.mapCamera[i];
    const auto position=camera->position.getValue();const double distance=position[2]-.9;
    const double aperture=distance*std::tan(camera->heightAngle.getValue()*.5);
    const double focal=std::max(double(camera->nearDistance.getValue()),double(camera->focalDistance.getValue())*camera->getBalanceAdjustment());
    const double eye=camera->getStereoAdjustment()*(camera->getStereoMode()==SoCamera::RIGHT_VIEW?.5:-.5);
    fixture.move[i]->translation.setValue(float(position[0]+aperture*centers[i][0]/lookup+eye*(1-distance/focal)),
      float(position[1]+aperture*centers[i][1]/lookup),.9f);
    auto *map=static_cast<SoGroup*>(fixture.light[i]->shadowMapScene.getValue());
    auto *caster=static_cast<SoCube*>(map->getChild(map->getNumChildren()-1));
    caster->width=caster->height=float(.28*aperture/lookup);caster->depth=.02f;
  }
}
struct ProducerShadow {
  SoShadowGroup *group=new SoShadowGroup;
  unsigned mapCaptures=0;
  explicit ProducerShadow(WorldStereo&w){
    group->quality=1;group->precision=.125f;group->epsilon=0;group->threshold=0;group->intensity=1;
    auto *map=new SoSeparator;auto *camera=new SoPerspectiveCamera;
    camera->position.setValue(0,0,2.5f);camera->nearDistance=1;camera->farDistance=8;camera->heightAngle=.8f;map->addChild(camera);
    auto *probe=new SoCallback;probe->setCallback([](void*p,SoAction*a){
      if(a->isOfType(SoCallbackAction::getClassTypeId())||a->isOfType(CoinRenderAction::getClassTypeId()))++*static_cast<unsigned*>(p);
    },&mapCaptures);map->addChild(probe);
    auto *style=new SoShadowStyle;style->style=SoShadowStyle::CASTS_SHADOW;map->addChild(style);
    // Reverse the same source projection for a .4-square producer shadow
    // centered at(.55,-.55), separate from the consumer's two upper patches.
    const double scale=(2.5-.9)*std::tan(.4)/std::sqrt(24.);
    auto *move=new SoTranslation;move->translation.setValue(float(.55*scale),float(-.55*scale),.9f);map->addChild(move);
    auto *cube=new SoCube;cube->width=cube->height=float(.4*scale);cube->depth=.02f;map->addChild(cube);
    auto *light=new SoShadowDirectionalLight;light->direction.setValue(0,0,-1);light->bboxCenter.setValue(0,0,0);
    light->bboxSize.setValue(4,4,8);light->shadowMapScene=map;light->intensity=.8f;group->addChild(light);
    auto *receiver=new SoShadowStyle;receiver->style=SoShadowStyle::SHADOWED;group->addChild(receiver);
    group->addChild(w.producer.geometry);w.producer.root->replaceChild(w.producer.geometry,group);
    for(int i=0;i<w.producer.root->getNumChildren();++i)if(w.producer.root->getChild(i)->isOfType(SoLightModel::getClassTypeId()))
      static_cast<SoLightModel*>(w.producer.root->getChild(i))->model=SoLightModel::PHONG;
    w.producer.material->ambientColor.setValue(0,0,0);w.producer.material->emissiveColor.setValue(0,0,0);w.producer.material->specularColor.setValue(0,0,0);
  }
};
template<class Highlight>
bool coupled(Run&r,Selection&s,Highlight&h,ShadowFixture&receiver,ProducerShadow&producer){
  s.first->deselectAll();producer.mapCaptures=0;if(!capture(r,s,h,"source-consumer-VSM-bare",false))return false;
  const auto ordinary=r.scene.seen.producerKinds;const auto sourceMaps=producer.mapCaptures;const auto bare=r.stable;
  s.select();producer.mapCaptures=0;if(!capture(r,s,h,"source-consumer-VSM-highlight"))return false;const auto a=r.stable;
  if(!require(changed(a,bare)>20&&strokePixels(a)>8&&producer.mapCaptures==sourceMaps&&sourceMaps>0&&r.scene.seen.producerKinds==ordinary,
    "highlight retains producer/consumer maps without extra producer/map capture"))return false;
  if(!independentEyes(r))return false;
  producer.group->isActive=FALSE;if(!capture(r,s,h,"source-VSM-disabled")||!require(changed(a,r.stable)>5,"source VSM has actual texture witnesses"))return false;
  producer.group->isActive=TRUE;if(!capture(r,s,h,"source-VSM-A-B-A")||!identical(a,r.stable,"source VSM A/B/A exact"))return false;
  receiver.group->isActive=FALSE;if(!capture(r,s,h,"consumer-VSM-disabled")||!require(changed(a,r.stable)>5,"consumer VSM has separate actual receiver witnesses"))return false;
  receiver.group->isActive=TRUE;return capture(r,s,h,"consumer-VSM-A-B-A")&&identical(a,r.stable,"consumer VSM A/B/A exact");
}

struct CpuInspection : Inspection {
  mutable unsigned admissions=0;
  CoinRenderSubmitResult preflightRtt(const CoinRenderRttPlan&,const CoinRenderFramePlan &f,const SbVec2i32&)const override{
    ++admissions;frame=f;return {CoinRenderBackendStatus::UNSUPPORTED,error};}
};
template<class Highlight>
bool cpu(const Config&c){
  WorldStereo w;w.manager.setStereoMode(c.stereo);w.manager.setRenderMode(renderMode(c.mode));
  w.base.material->diffuseColor.setValue(.05f,.65f,.1f);Selection selection(w,c.paths);selection.select();
  Highlight h;h.setColor(SbColor(1,.15f,.8f));h.setLineWidth(3);h.setLinePattern(0xffff);ActionBinding binding(w.manager,h);
  Counts hooks;w.manager.addPreRenderCallback(pre,&hooks);w.manager.addPostRenderCallback(post,&hooks);
  CoinRenderManagerAdapter adapter(w.manager,SbVec2i32(96,96),samplingTestOptions());
  auto *target=adapter.getSceneManager()->getRenderTarget();if(!target)return false;
  auto *inspection=new CpuInspection;target->getPimpl().get().backend.reset(inspection);
  auto admitted=[&](const char *label){selection.reset();CameraState saved(w.perspective);const unsigned before=inspection->admissions+inspection->submits,p=hooks.pre,q=hooks.post;
    const auto status=adapter.render();const auto after=inspection->admissions+inspection->submits;
    std::cout<<"highlight_completion_cpu label="<<label<<" status="<<status<<" admissions="<<before<<"->"<<after<<std::endl;
    return require(status==CoinRenderAction::UNSUPPORTED&&after>before&&h.calls==0&&saved.same(w.perspective)&&hooks.pre==p+1&&hooks.post==q+1&&
      target->getLastSubmissionSerial()==0&&std::string(adapter.getLastError().getString()).find(inspection->error)!=std::string::npos,
      "supported highlight reaches independent backend admission with paired frame state");};
  bool ok=admitted("active-style");h.setLineWidth(std::numeric_limits<float>::quiet_NaN());
  const auto calls=inspection->admissions+inspection->submits;
  ok &= require(adapter.render()==CoinRenderAction::INVALID_SCENE&&inspection->admissions+inspection->submits==calls,"invalid active style rejected before backend");
  h.setVisible(FALSE);ok &= admitted("inactive-invalid-style");h.setVisible(TRUE);selection.first->deselectAll();ok &= admitted("unselected-invalid-style");
  h.setLineWidth(3);selection.select();ok &= admitted("style-recovered");
  w.manager.removePreRenderCallback(pre,&hooks);w.manager.removePostRenderCallback(post,&hooks);return ok;
}
void setup(Run&r,const Config&c){
  r.scene.base.material->diffuseColor.setValue(.05f,.65f,.1f);
  r.scene.manager.setRenderMode(renderMode(c.mode));
  if(c.paths)r.scene.base.setRect(-.24f,.24f,-.26f,.27f,.35f);
  if((c.test=="technical"&&c.mode!=5)||c.test=="alpha"){
    r.scene.base.textures->whichChild=SO_SWITCH_ALL;r.scene.base.image(0,0);r.scene.base.image(1,1);
  }
  // BOUNDING_BOX plus additional texture units retains its real inherited
  // unsupported domain; this accepted-mode fixture uses ordinary base state.
  if(c.test=="layers"||c.test=="window"){
    if(c.test=="layers")r.scene.foreground.setRect(-.55f,.35f,-.45f,.9f,-.4f);
    r.scene.add(r.scene.background,Layer::BACKGROUND);r.scene.add(r.scene.foreground,0);
  }
  if(c.test=="raster"){
    auto *image=new SoImage;std::vector<uint8_t> pixels(17*13*4);
    for(size_t p=0;p<pixels.size();p+=4){pixels[p]=220;pixels[p+1]=70;pixels[p+2]=160;pixels[p+3]=128;}
    image->image.setValue(SbVec2s(17,13),4,pixels.data());image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;
    auto *screen=new SoSeparator;auto *move=new SoTranslation;move->translation.setValue(.4f,-.3f,.7f);screen->addChild(move);screen->addChild(image);
    r.scene.base.root->addChild(screen);auto *annotation=new SoAnnotation;annotation->addChild(r.scene.second.root);r.scene.base.root->addChild(annotation);
  }
}
template<class Highlight>
bool run(const Config&c,const CoinRenderOptions&options){
  Run r(c,options);if(!r.ready())return false;
  if(!require(r.target->getOptions().sampleCount==c.samples&&r.target->getOptions().renderer==c.renderer&&
    r.target->getOptions().textureSamplingPolicy==samplingTestPolicy&&r.target->getOptions().sceneTexture==c.route,
    "actual target retains requested samples, renderer, sampling and RTT route"))return false;
  setup(r,c);
  if(c.test=="visual"&&c.samples==1&&!technicalRasterPhase(r))return false;
  std::unique_ptr<ShadowFixture> shadow;
  if(c.test=="shadows"||c.test=="combined"||c.test=="coupled")shadow.reset(new ShadowFixture(r.scene));
  if(c.test=="resources"||c.test=="combined"||c.test=="coupled"){
    if(!stagedSources(r,true))return false;
    if(c.route==COIN_RENDER_SCENE_TEXTURE_DIRECT)r.scene.stagedIsolation=false;
  }
  std::unique_ptr<ProducerShadow> producerShadow;
  if(c.test=="coupled"){
    configureCoupledReceiver(*shadow);
    producerShadow.reset(new ProducerShadow(r.scene));r.scene.stagedIsolation=false;
    // Keep FG outside the two map casters' receiver region. A central opaque
    // FG could hide the consumer control while the source control still moved.
    r.scene.foreground.setRect(-.96f,-.78f,.74f,.94f,-.4f);
    r.scene.base.rtt->model=SoSceneTexture2::MODULATE;r.scene.base.material->diffuseColor.setValue(.8f,.6f,.4f);
  }
  Selection selection(r.scene,c.paths,shadow?static_cast<SoNode*>(shadow->group):nullptr);
  Highlight highlight;highlight.setColor(SbColor(1,.15f,.8f));highlight.setLineWidth(3);highlight.setLinePattern(0xffff);
  highlight.setTransparencyType(SoGLRenderAction::BLEND);ActionBinding binding(r.scene.manager,highlight);
  if(c.test=="alpha"){
    const SoGLRenderAction::TransparencyType modes[]={SoGLRenderAction::BLEND,SoGLRenderAction::ADD,
      SoGLRenderAction::DELAYED_BLEND,SoGLRenderAction::SORTED_OBJECT_BLEND,SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND,SoGLRenderAction::SCREEN_DOOR};
    highlight.setTransparencyType(modes[c.alpha]);r.scene.base.material->transparency=.25f;
    r.scene.second.material->transparency=.45f;r.scene.base.root->addChild(r.scene.second.root);
  }
  bool ok=false;
  if(c.test=="history")ok=history(r,selection,highlight);
  else if(c.test=="coupled")ok=coupled(r,selection,highlight,*shadow,*producerShadow);
  else if(c.test=="resources"||c.test=="combined")ok=resources(r,selection,highlight,c);
  else if(c.test=="layers")ok=layers(r,selection,highlight);
  else if(c.test=="paths")ok=paths(r,selection,highlight);
  else if(c.test=="occlusion")ok=occlusion(r,selection,highlight,c);
  else if(c.test=="refusal")ok=refusal(r,selection,highlight,c);
  else if(c.test=="shadows")ok=shadows(r,selection,highlight,*shadow);
  else if(c.test=="window"){
    selection.select();ok=capture(r,selection,highlight,"window-first-highlight")&&independentEyes(r)&&r.physical(r.stable);
    if(ok)ok=r.resize(97)&&capture(r,selection,highlight,"window-resize-highlight")&&independentEyes(r)&&r.physical(r.stable);
    if(ok)ok=r.normalWindow();
  }else ok=visual(r,selection,highlight,c);
  return ok;
}
}
int main(int argc,char **argv){using namespace highlight_completion;
  configureSamplingTest(argc,argv);highlight_completion::Config c;
  for(int i=1;i<argc;++i){const std::string s=argv[i];
    if(s=="--cpu")c.cpu=true;else if(s=="--line")c.line=true;else if(s=="--box")c.line=false;
    else if(s=="--mono")c.stereo=SoRenderManager::MONO;else if(s=="--rows")c.stereo=SoRenderManager::INTERLEAVED_ROWS;
    else if(s=="--columns")c.stereo=SoRenderManager::INTERLEAVED_COLUMNS;else if(s=="--anaglyph")c.stereo=SoRenderManager::ANAGLYPH;
    else if(s=="--window")c.window=true;else if(s=="--paths")c.paths=true;
    else if(s=="--direct")c.route=COIN_RENDER_SCENE_TEXTURE_DIRECT;else if(s=="--staged")c.route=COIN_RENDER_SCENE_TEXTURE_STAGED;
    else if(s=="--weighted")c.mechanism=COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT;else if(s=="--peeling")c.mechanism=COIN_RENDER_TRANSPARENCY_PEELING;
    else if(s=="--portable-sampling"){}
    else if(s=="--profile"&&i+1<argc)c.test=argv[++i];
    else if(s=="--mode"&&i+1<argc){c.mode=unsigned(std::atoi(argv[++i]));if(c.mode>6)return 2;}
    else if(s=="--alpha"&&i+1<argc){c.alpha=unsigned(std::atoi(argv[++i]));if(c.alpha>5)return 2;}
    else if(s=="--samples"&&i+1<argc){c.samples=unsigned(std::atoi(argv[++i]));if(c.samples!=1&&c.samples!=2&&c.samples!=4)return 2;}
    else return 2;
  }
  c.profile=c.line?"line":"box";
  if(c.test=="paths")c.paths=true;
  XInitThreads();SoDB::init();SoInteraction::init();CoinRenderAction::initClass();Viewport::initClass();
  if(SoBoxHighlightRenderAction::getClassTypeId().isBad())SoBoxHighlightRenderAction::initClass();
  if(SoLineHighlightRenderAction::getClassTypeId().isBad())SoLineHighlightRenderAction::initClass();
  if(c.cpu)return (c.line?cpu<Instrumented<SoLineHighlightRenderAction>>(c):cpu<Instrumented<SoBoxHighlightRenderAction>>(c))?0:1;
  auto options=samplingTestOptions();options.sampleCount=c.samples;options.transparency=c.mechanism;
  options.sceneTexture=c.route;options.maxTextureAnisotropy=1;
  startup::Environment phases("COIN_RENDER_TRACE_PHASES","1"),traceGL("COIN_BGFX_TRACE_GL_ADAPTER","1");
  CoinRenderCapabilities caps{};const auto query=coin_render_query_capabilities_for_renderer(c.window?COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW:
    COIN_RENDER_EXPERIMENTAL_OFFSCREEN,options.renderer,&caps,sizeof(caps));
  bool ids=caps.vendor_id&&caps.device_id;
#ifdef COIN_MANAGER_MONO_BGFX
  c.glTraceRequired=caps.renderer==COIN_RENDER_RENDERER_OPENGL;ids=ids||c.glTraceRequired;
#else
  c.wgpuGlTraceRequired=caps.renderer==COIN_RENDER_RENDERER_OPENGL;if(c.wgpuGlTraceRequired)ids=caps.vendor_id&&acceleratedName(caps.adapter_name);
#endif
  std::cout<<"highlight_completion_environment renderer="<<caps.renderer<<" vendor="<<caps.vendor_id<<" adapter="<<caps.adapter_name
    <<" samples="<<c.samples<<" profile="<<c.test<<" kind="<<c.profile<<" mode="<<c.mode<<std::endl;
  if(!require(!query&&caps.gpu_available&&ids&&(c.glTraceRequired||acceleratedName(caps.adapter_name))&&
    (caps.renderer==COIN_RENDER_RENDERER_OPENGL||caps.renderer==COIN_RENDER_RENDERER_VULKAN)&&
    (options.renderer==COIN_RENDER_RENDERER_UNKNOWN||unsigned(options.renderer)==caps.renderer),"real requested GPU; missing adapter/reference is a failure"))return 1;
  c.renderer=static_cast<CoinRenderRenderer>(caps.renderer);options.renderer=c.renderer;c.vendor=caps.vendor_id;c.adapterName=caps.adapter_name;
#ifdef COIN_MANAGER_MONO_BGFX
  // Preserve the established synchronous policy for the public target and
  // independently constructed ordinary-eye targets; no retry/new render.
  startup::Environment publicPipeline("COIN_BGFX_READBACK_PIPELINE_DEPTH","1");
#endif
  if(c.test=="peers")return (c.line?peers<Instrumented<SoLineHighlightRenderAction>>(c,options):peers<Instrumented<SoBoxHighlightRenderAction>>(c,options))?0:1;
  return (c.line?run<Instrumented<SoLineHighlightRenderAction>>(c,options):run<Instrumented<SoBoxHighlightRenderAction>>(c,options))?0:1;
}
