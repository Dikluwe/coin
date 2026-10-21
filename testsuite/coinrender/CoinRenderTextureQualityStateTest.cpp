// Reuse the qualified nonlinear volume fixture without changing its source.
#define main coin_quality_transition_fixture_main
#include "CoinRenderTextureQualityTransitionTest.cpp"
#undef main
#include <Inventor/C/glue/gl.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/elements/SoGLMultiTextureEnabledElement.h>
#include <Inventor/elements/SoGLMultiTextureImageElement.h>
#include <Inventor/misc/SoGLImage.h>
#include "glue/glp.h"

namespace {
struct UploadHook {
  cc_glglue * glue=nullptr;
  COIN_PFNGLTEXIMAGE3DPROC image=nullptr;
  COIN_PFNGLTEXSUBIMAGE3DPROC subimage=nullptr;
  int cacheContext=0;
  unsigned uploads=0;
  bool installed=false;
};
UploadHook * activeHook=nullptr;
void APIENTRY countImage(GLenum target,GLint level,GLenum internalformat,
                         GLsizei width,GLsizei height,GLsizei depth,GLint border,
                         GLenum format,GLenum type,const GLvoid * pixels) {
  if(target==GL_TEXTURE_3D) ++activeHook->uploads;
  activeHook->image(target,level,internalformat,width,height,depth,border,format,type,pixels);
}
void APIENTRY countSubimage(GLenum target,GLint level,GLint x,GLint y,GLint z,
                            GLsizei width,GLsizei height,GLsizei depth,
                            GLenum format,GLenum type,const GLvoid * pixels) {
  if(target==GL_TEXTURE_3D) ++activeHook->uploads;
  activeHook->subimage(target,level,x,y,z,width,height,depth,format,type,pixels);
}
void beginUploads(void * data,SoAction * action) {
  if(!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
  auto & hook=*static_cast<UploadHook *>(data);
  hook.cacheContext=SoGLCacheContextElement::get(action->getState());
  hook.glue=const_cast<cc_glglue *>(cc_glglue_instance(hook.cacheContext));
  if(!hook.glue || !hook.glue->glTexImage3D || !hook.glue->glTexSubImage3D || activeHook) return;
  hook.image=hook.glue->glTexImage3D;
  hook.subimage=hook.glue->glTexSubImage3D;
  activeHook=&hook;
  hook.glue->glTexImage3D=countImage;
  hook.glue->glTexSubImage3D=countSubimage;
  hook.installed=true;
}
void endUploads(void * data,SoAction * action) {
  if(!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
  auto & hook=*static_cast<UploadHook *>(data);
  if(hook.installed && activeHook==&hook) {
    hook.glue->glTexImage3D=hook.image;
    hook.glue->glTexSubImage3D=hook.subimage;
    activeHook=nullptr;
    hook.installed=false;
  }
}
void addUploadHooks(Scene & scene,UploadHook & hook) {
  auto * start=new SoCallback;start->setCallback(beginUploads,&hook);
  scene.root->insertChild(start,scene.root->findChild(scene.texture));
  auto * finish=new SoCallback;finish->setCallback(endUploads,&hook);
  scene.root->addChild(finish);
}
bool renderAndCheck(SoOffscreenRenderer & renderer,Scene & scene,
                    UploadHook & hook,int min,int mag,bool initial) {
  hook.uploads=0;scene.observation.seen=false;
  const bool rendered=renderer.render(scene.root) && renderer.getBuffer();
  const auto & o=scene.observation;
  const bool state=rendered && o.seen && o.error==GL_NO_ERROR &&
    hook.cacheContext>0 && !hook.installed && !activeHook &&
    o.id>0 && o.min==min && o.mag==mag && o.mipWidth==4;
  const bool uploads=initial ? hook.uploads>0 : hook.uploads==0;
  std::cout << "quality_state context=" << hook.cacheContext
            << " min=" << o.min << " mag=" << o.mag
            << " uploads=" << hook.uploads << " initial=" << initial
            << " state=" << state << " upload_gate=" << uploads << '\n';
  return check(state,"shared-image context/filter state") &&
         check(uploads,"quality-only transition must not upload existing mip data");
}
bool interleavedContexts() {
  Scene first(0),second(0);
  second.root->replaceChild(second.texture,first.texture);
  second.texture=first.texture;
  UploadHook a,b;
  addUploadHooks(first,a);addUploadHooks(second,b);
  first.quality->textureQuality=.6f;
  second.quality->textureQuality=.8f;
  SoOffscreenRenderer rendererA(SbViewportRegion(128,128));
  SoOffscreenRenderer rendererB(SbViewportRegion(128,128));
  bool ok=renderAndCheck(rendererA,first,a,GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR,true);
  ok &= renderAndCheck(rendererB,second,b,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR,true);
  ok &= check(a.cacheContext!=b.cacheContext,
              "two offscreen renderers must provide distinct cache contexts");
  ok &= renderAndCheck(rendererA,first,a,GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR,false);
  ok &= renderAndCheck(rendererB,second,b,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR,false);
  first.quality->textureQuality=.8f;
  ok &= renderAndCheck(rendererA,first,a,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR,false);
  first.quality->textureQuality=.6f;
  ok &= renderAndCheck(rendererA,first,a,GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR,false);
  return ok;
}
bool separatorRestores() {
  Scene scene(0);UploadHook hook;addUploadHooks(scene,hook);
  scene.quality->textureQuality=.6f;
  SoOffscreenRenderer renderer(SbViewportRegion(128,128));
  bool ok=renderAndCheck(renderer,scene,hook,GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR,true);
  Observation inner;inner.unit=0;
  auto * branch=new SoSeparator;
  auto * changed=new SoComplexity;changed->textureQuality=.8f;branch->addChild(changed);
  branch->addChild(scene.texture);
  auto * probe=new SoCallback;probe->setCallback(observe,&inner);branch->addChild(probe);
  scene.root->insertChild(branch,scene.root->findChild(scene.texture)+1);
  inner.seen=false;
  ok &= renderAndCheck(renderer,scene,hook,GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR,false);
  ok &= check(inner.seen && inner.min==GL_LINEAR_MIPMAP_LINEAR &&
              inner.mag==GL_LINEAR && inner.error==GL_NO_ERROR,
              "separator's inner quality and outer filter must both be restored");
  scene.root->removeChild(branch);
  ok &= renderAndCheck(renderer,scene,hook,GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR,false);
  return ok;
}
struct QualityProbe {
  float uploaded=-1.f;
  bool usesQuality=false;
  bool seen=false;
};
void observeUploadedQuality(void * raw,SoAction * action) {
  if(!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
  auto & probe=*static_cast<QualityProbe *>(raw);
  SoMultiTextureImageElement::Model model;
  SbColor blend;
  SoGLImage * image=SoGLMultiTextureImageElement::get(action->getState(),0,model,blend);
  probe.seen=image!=nullptr;
  if(image) {
    probe.uploaded=image->getQuality();
    probe.usesQuality=(image->getFlags() & SoGLImage::USE_QUALITY_VALUE)!=0;
  }
}
bool upgradeFromUnmipped() {
  Scene scene(0);UploadHook hook;addUploadHooks(scene,hook);
  QualityProbe qualityProbe;
  auto * qualityObserver=new SoCallback;
  qualityObserver->setCallback(observeUploadedQuality,&qualityProbe);
  scene.root->insertChild(qualityObserver,scene.root->findChild(scene.texture)+1);
  SoOffscreenRenderer renderer(SbViewportRegion(128,128));
  const float qualities[]={.1f,.8f,.3f,.6f,.8f};
  const int minimums[]={GL_NEAREST,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR,
                        GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR_MIPMAP_LINEAR};
  const int magnifications[]={GL_NEAREST,GL_LINEAR,GL_LINEAR,GL_LINEAR,GL_LINEAR};
  const int redExpected[]={44,70,42,50,70};
  GLint mipReadyId=0;
  bool ok=true;
  for(size_t step=0;step<5;++step) {
    scene.quality->textureQuality=qualities[step];
    scene.observation.seen=false;qualityProbe.seen=false;hook.uploads=0;
    const bool rendered=renderer.render(scene.root) && renderer.getBuffer();
    const auto & o=scene.observation;
    const int red=rendered ? int(renderer.getBuffer()[(64*128+64)*3]) : -1;
    const bool resource=rendered && o.seen && o.error==GL_NO_ERROR &&
      hook.cacheContext>0 && !hook.installed && !activeHook && o.id>0 &&
      o.min==minimums[step] && o.mag==magnifications[step] &&
      qualityProbe.seen && qualityProbe.usesQuality &&
      std::abs(qualityProbe.uploaded-(step==0?.1f:.8f))<.0001f &&
      (step==0 ? o.mipWidth==0 :
       (o.mipWidth==4 && o.mipHeight==4 && o.mipDepth==4)) &&
      (step<2 || (o.id==mipReadyId && hook.uploads==0));
    if(step==1) mipReadyId=o.id;
    const bool color=red>=0 && std::abs(red-redExpected[step])<=5;
    std::cout << "quality_upgrade step=" << step << " quality=" << qualities[step]
              << " red=" << red << " expected=" << redExpected[step]
              << " min=" << o.min << " mag=" << o.mag
              << " mip1_width=" << o.mipWidth << " texture_id=" << o.id
              << " uploaded_quality=" << qualityProbe.uploaded
              << " gl_uploads=" << hook.uploads << " resource=" << resource
              << " color=" << color << '\n';
    ok &= check(resource,"unmipped-to-mipped resource/filter transition");
    ok &= check(color,"unmipped-to-mipped analytic image transition");
  }
  return ok;
}
struct ExplicitImage {
  SoGLImage * image=nullptr;
  SoNode * node=nullptr;
};
void installExplicit(void * raw,SoAction * action) {
  if(!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
  auto & fixture=*static_cast<ExplicitImage *>(raw);
  SoState * state=action->getState();
  SoGLMultiTextureEnabledElement::enableTexture3(state,fixture.node,0);
  SoGLMultiTextureImageElement::set(state,fixture.node,0,fixture.image,
                                   SoMultiTextureImageElement::MODULATE,SbColor(0,0,0));
}
bool explicitFlags(uint32_t flags,int expectedMin,int expectedMag,int mipWidth,
                   float expectedQuality=.8f) {
  SoGLImage * image=new SoGLImage;
  image->setFlags(flags);
  std::vector<unsigned char> pixels(4*4*4*4,192);
  image->setData(pixels.data(),SbVec3s(4,4,4),4,
                 SoGLImage::CLAMP,SoGLImage::CLAMP,SoGLImage::CLAMP,.8f);
  SoSeparator * root=new SoSeparator;root->ref();
  auto * camera=new SoOrthographicCamera;camera->position.setValue(0,0,3);root->addChild(camera);
  auto * quality=new SoComplexity;quality->textureQuality=.8f;root->addChild(quality);
  auto * installer=new SoCallback;
  ExplicitImage context;context.image=image;context.node=installer;
  installer->setCallback(installExplicit,&context);
  root->addChild(installer);
  Observation observation;auto * probe=new SoCallback;
  probe->setCallback(observe,&observation);root->addChild(probe);
  SoOffscreenRenderer renderer(SbViewportRegion(16,16));
  const bool rendered=renderer.render(root) && renderer.getBuffer();
  const bool valid=rendered && observation.seen && observation.error==GL_NO_ERROR &&
    observation.id>0 && observation.min==expectedMin &&
    observation.mag==expectedMag && observation.mipWidth==mipWidth &&
    std::abs(image->getQuality()-expectedQuality)<.0001f;
  std::cout << "quality_flags flags=" << flags << " min=" << observation.min
            << " mag=" << observation.mag << " mip1_width=" << observation.mipWidth
            << " uploaded_quality=" << image->getQuality()
            << " pass=" << valid << '\n';
  root->unref();image->unref();
  return check(valid,"explicit SoGLImage flags must prevail over quality");
}
}
int main(int argc,char ** argv) {
  if(argc!=2) return 2;
  SoDB::init();
  const std::string mode(argv[1]);
  if(mode=="--contexts") return interleavedContexts()?0:1;
  if(mode=="--separator") return separatorRestores()?0:1;
  if(mode=="--upgrade") return upgradeFromUnmipped()?0:1;
  if(mode=="--flags") {
    bool ok=explicitFlags(SoGLImage::NO_MIPMAP|SoGLImage::LINEAR_MIN_FILTER|
                          SoGLImage::LINEAR_MAG_FILTER,GL_LINEAR,GL_LINEAR,0);
    ok &= explicitFlags(SoGLImage::LINEAR_MIPMAP_FILTER|
                        SoGLImage::LINEAR_MAG_FILTER,GL_LINEAR_MIPMAP_NEAREST,GL_LINEAR,2);
    ok &= explicitFlags(SoGLImage::LINEAR_MIPMAP_FILTER|SoGLImage::LINEAR_MIN_FILTER|
                        SoGLImage::LINEAR_MAG_FILTER,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR,2);
    ok &= explicitFlags(SoGLImage::USE_QUALITY_VALUE,
                        GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR,2);
    return ok?0:1;
  }
  return 2;
}
