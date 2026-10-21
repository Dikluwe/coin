#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
int side=64;
bool oddSubviewport=false;
using Pixels=std::vector<uint8_t>;

class LayerViewport : public SoNode {
  SO_NODE_HEADER(LayerViewport);
public:
  static void initClass() { SO_NODE_INIT_CLASS(LayerViewport,SoNode,"Node"); }
  LayerViewport() { SO_NODE_CONSTRUCTOR(LayerViewport); }
  SbViewportRegion region;
  void callback(SoCallbackAction * action) override {
    SoViewportRegionElement::set(action->getState(),region);
  }
  void GLRender(SoGLRenderAction * action) override {
    SoViewportRegionElement::set(action->getState(),region);
  }
protected:
  ~LayerViewport() override=default;
};
SO_NODE_SOURCE(LayerViewport);

class ReferenceAction : public SoGLRenderAction {
public:
  ReferenceAction(SoRenderManager & source,const SbViewportRegion & requestedViewport)
    : SoGLRenderAction(requestedViewport), source(source), requestedViewport(requestedViewport) {}
  void apply(SoNode * root) override {
    if (inside) { SoGLRenderAction::apply(root); return; }
    inside=true;
    // OffscreenRenderer sets its framebuffer-sized viewport before apply().
    // Restore the manager's requested main viewport independently of target size.
    SoGLRenderAction::setViewportRegion(requestedViewport);
    source.render(this,TRUE,TRUE,TRUE);
    inside=false;
  }
private:
  SoRenderManager & source;
  SbViewportRegion requestedViewport;
  bool inside=false;
};

struct Quad { SoSeparator * root; SoCoordinate3 * coordinates; SoMaterial * material; SoAlphaTest * alpha; };
Quad makeQuad(float size,float depth,const SbColor & color,float transparency) {
  auto * root=new SoSeparator; root->ref();
  if (oddSubviewport && transparency>0) {
    auto * viewport=new LayerViewport;
    viewport->region=SbViewportRegion(side,side);
    viewport->region.setViewportPixels(7,11,49,47);
    root->addChild(viewport);
  }
  auto * camera=new SoOrthographicCamera;
  camera->position.setValue(0,0,4); camera->height=4;
  root->addChild(camera);
  auto * light=new SoLightModel; light->model=SoLightModel::BASE_COLOR;
  root->addChild(light);
  auto * material=new SoMaterial; material->diffuseColor.setValue(color);
  material->transparency=transparency;
  root->addChild(material);
  auto * alpha=new SoAlphaTest; alpha->function=SoAlphaTest::NONE;
  root->addChild(alpha);
  auto * coordinates=new SoCoordinate3;
  const SbVec3f corners[]={{-size,-size,depth},{size,-size,depth},
                            {size,size,depth},{-size,size,depth}};
  coordinates->point.setValues(0,4,corners);
  root->addChild(coordinates);
  auto * face=new SoFaceSet; face->numVertices.set1Value(0,4); root->addChild(face);
  return {root,coordinates,material,alpha};
}
void addVariableAlphaTexture(Quad & quad,int width,int height) {
  auto * texture=new SoTexture2;
  texture->model=SoTexture2::MODULATE;
  texture->wrapS=SoTexture2::CLAMP;
  texture->wrapT=SoTexture2::CLAMP;
  std::vector<uint8_t> texels(size_t(width*height)*4u,255);
  for (int i=0;i<width*height;++i)
    texels[size_t(i)*4u+3u]=width*height==1 ? 170 :
      uint8_t(i*255/(width*height-1));
  texture->image.setValue(SbVec2s(width,height),4,texels.data());
  quad.root->insertChild(texture,4);
  auto * coordinates=new SoTextureCoordinate2;
  const SbVec2f uv[]={{0,0},{1,0},{1,1},{0,1}};
  coordinates->point.setValues(0,4,uv);
  quad.root->insertChild(coordinates,5);
}
void moveQuad(Quad & quad,float size,float depth) {
  const SbVec3f corners[]={{-size,-size,depth},{size,-size,depth},
                            {size,size,depth},{-size,size,depth}};
  quad.coordinates->point.setValues(0,4,corners);
}
bool captureCoin(CoinRenderManagerAdapter & adapter,Pixels & image) {
  if (adapter.render()!=CoinRenderAction::SUCCESS) {
    std::fprintf(stderr,"technical_blend CoinRender error: %s\n",adapter.getLastError().getString());
    return false;
  }
  Pixels rgba;
  adapter.getSceneManager()->getRenderTarget()->readbackRGBA(rgba);
  if (rgba.size()!=side*side*4u) return false;
  image.resize(side*side*3u);
  for (size_t p=0;p<side*side;++p)
    for (size_t c=0;c<3;++c) image[p*3+c]=rgba[p*4+c];
  return true;
}
bool captureGL(SoRenderManager & manager,SoOffscreenRenderer & renderer,
               SoSeparator * root,Pixels & image) {
  const bool ready=manager.prepareFrame();
  const bool drawn=ready && renderer.render(root);
  const uint8_t * data=drawn?renderer.getBuffer():nullptr;
  manager.finishFrame(drawn && data);
  if (!data) return false;
  image.resize(side*side*3u);
  // SoOffscreenRenderer returns GL rows bottom-up; CoinRenderTarget readback
  // is top-down. Apply this fixed orientation conversion for every profile.
  // The 2x2/3x2 alpha ramp differs across Y, so an omitted flip is visible.
  for (int y=0;y<side;++y)
    for (int x=0;x<side;++x)
      for (int c=0;c<3;++c)
        image[3u*(size_t(y)*side+size_t(x))+size_t(c)]=
            data[3u*(size_t(side-1-y)*side+size_t(x))+size_t(c)];
  return true;
}
bool boundary(const Pixels & image,int x,int y) {
  const size_t p=3u*(y*side+x);
  for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
    const int xx=x+dx, yy=y+dy;
    if ((dx==0 && dy==0) || xx<0 || xx>=side || yy<0 || yy>=side) continue;
    const size_t q=3u*(yy*side+xx);
    for (int c=0;c<3;++c) if (std::abs(int(image[p+c])-int(image[q+c]))>4) return true;
  }
  return false;
}
bool matchNearby(const Pixels & a,const Pixels & b,int x,int y) {
  const size_t p=3u*(y*side+x);
  for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
    const int xx=x+dx, yy=y+dy;
    if (xx<0 || xx>=side || yy<0 || yy>=side) continue;
    const size_t q=3u*(yy*side+xx);
    bool close=true;
    for (int c=0;c<3;++c) close &= std::abs(int(a[p+c])-int(b[q+c]))<=4;
    if (close) return true;
  }
  return false;
}
bool compare(const Pixels & coin,const Pixels & gl,int mode,const char * profile,const char * label,int tolerance) {
  if (coin.size()!=side*side*3u || gl.size()!=coin.size()) return false;
  size_t exact=0,solid=0,edges=0;
  unsigned maxDelta=0;
  int worstX=-1,worstY=-1;
  for (int y=0;y<side;++y) for (int x=0;x<side;++x) {
    const size_t p=3u*(y*side+x);
    unsigned delta=0;
    for (int c=0;c<3;++c) delta=std::max(delta,unsigned(std::abs(int(coin[p+c])-int(gl[p+c]))));
    exact+=delta==0;
    if (delta>maxDelta) { maxDelta=delta; worstX=x; worstY=y; }
    if (boundary(coin,x,y) || boundary(gl,x,y))
      edges+=!matchNearby(coin,gl,x,y) || !matchNearby(gl,coin,x,y);
    else solid+=delta>4;
  }
  const size_t center=3u*(32*side+32);
  unsigned centerDelta=0;
  for (int c=0;c<3;++c)
    centerDelta=std::max(centerDelta,unsigned(std::abs(int(coin[center+c])-int(gl[center+c]))));
  std::printf("technical_alpha mode=%d profile=%s scenario=%s side=%d subviewport=%d exact=%zu/%d max=%u worst_xy=%d,%d solid=%zu edges=%zu center_delta=%u coin_center=%u,%u,%u gl_center=%u,%u,%u\n",
              mode,profile,label,side,int(oddSubviewport),exact,side*side,maxDelta,worstX,worstY,solid,edges,centerDelta,
              unsigned(coin[center]),unsigned(coin[center+1]),unsigned(coin[center+2]),
              unsigned(gl[center]),unsigned(gl[center+1]),unsigned(gl[center+2]));
  return solid==0 && edges==0 && centerDelta<=unsigned(tolerance) && maxDelta<=unsigned(tolerance);
}
bool blueAtCenter(const Pixels & image) {
  const size_t p=3u*(32*side+32);
  return image[p+2]>32 && image[p+2]>image[p+1]*2u;
}
enum class Profile { ScreenDoor, TextureBlend, MaterialTextureBlend };
const char * profileName(Profile profile) {
  switch(profile) {
  case Profile::ScreenDoor: return "material_screen_door";
  case Profile::TextureBlend: return "texture_blend";
  case Profile::MaterialTextureBlend: return "material_texture_blend";
  }
  return "invalid";
}
int exercise(SoRenderManager::RenderMode mode,Profile profile,int textureWidth,int textureHeight) {
  const float baseExtent=.8f;
  const float overlayExtent=oddSubviewport?1.2f:.8f;
  Quad base=makeQuad(baseExtent,0,SbColor(1,0,0),0);
  Quad overlay=makeQuad(overlayExtent,.5f,SbColor(0,0,1),profile==Profile::TextureBlend?0:.5f);
  if (profile!=Profile::ScreenDoor) addVariableAlphaTexture(overlay,textureWidth,textureHeight);
  const auto transparency=profile==Profile::ScreenDoor ? SoGLRenderAction::SCREEN_DOOR : SoGLRenderAction::BLEND;
  const int tolerance=profile==Profile::ScreenDoor?0:2;
  char profileLabel[96];
  std::snprintf(profileLabel,sizeof(profileLabel),"%s_%dx%d",profileName(profile),textureWidth,textureHeight);
  const char * name=profileLabel;
  SoRenderManager coinManager,glManager;
  coinManager.setSceneGraph(base.root); glManager.setSceneGraph(base.root);
  SbViewportRegion mainViewport(side,side);
  if (oddSubviewport) mainViewport.setViewportPixels(0,0,64,64);
  coinManager.setViewportRegion(mainViewport);
  glManager.setViewportRegion(mainViewport);
  coinManager.setBackgroundColor(SbColor4f(0,0,0,1));
  glManager.setBackgroundColor(SbColor4f(0,0,0,1));
  coinManager.setRenderMode(mode); glManager.setRenderMode(mode);
  coinManager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  CoinRenderManagerAdapter adapter(coinManager,SbVec2i32(side,side));
  auto * depthTarget=adapter.getSceneManager()->getRenderTarget();
  if (!depthTarget || !depthTarget->setDepthReadbackEnabled(TRUE)) {
    overlay.root->unref(); base.root->unref(); return 1;
  }
  ReferenceAction glAction(glManager,mainViewport);
  glAction.setTransparencyType(SoGLRenderAction::BLEND);
  glManager.setGLRenderAction(&glAction);
  SoOffscreenRenderer renderer(SbViewportRegion(side,side));
  renderer.setGLRenderAction(&glAction);
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0,0,0));
  Pixels coin,gl,front,rear,cleared;
  std::vector<float> frontDepth,rearDepth,clearDepth;
  if (!captureGL(glManager,renderer,base.root,gl)) {
    overlay.root->unref(); base.root->unref(); return 77;
  }
  bool ok=captureCoin(adapter,coin) && compare(coin,gl,int(mode),name,"baseline",tolerance);
  const Pixels baseline=coin;
  const auto flags=SoRenderManager::Superimposition::AUTOREDRAW |
                   SoRenderManager::Superimposition::ZBUFFERON;
  auto * layer=coinManager.addSuperimposition(overlay.root,flags);
  layer->setTransparencyType(transparency);
  auto * glLayer=glManager.addSuperimposition(overlay.root,flags);
  glLayer->setTransparencyType(transparency);
  const bool frontCaptured=captureGL(glManager,renderer,base.root,gl) && captureCoin(adapter,coin);
  ok=frontCaptured && compare(coin,gl,int(mode),name,"front",tolerance) && ok;
  front=coin;
  depthTarget->readbackDepth(frontDepth);
  moveQuad(overlay,overlayExtent,-.5f);
  const bool rearCaptured=captureGL(glManager,renderer,base.root,gl) && captureCoin(adapter,coin);
  ok=rearCaptured && compare(coin,gl,int(mode),name,"rear",tolerance) && ok;
  rear=coin;
  depthTarget->readbackDepth(rearDepth);
  coinManager.removeSuperimposition(layer); glManager.removeSuperimposition(glLayer);
  layer=coinManager.addSuperimposition(overlay.root,flags|SoRenderManager::Superimposition::CLEARZBUFFER);
  layer->setTransparencyType(transparency);
  glLayer=glManager.addSuperimposition(overlay.root,flags|SoRenderManager::Superimposition::CLEARZBUFFER);
  glLayer->setTransparencyType(transparency);
  const bool clearCaptured=captureGL(glManager,renderer,base.root,gl) && captureCoin(adapter,coin);
  ok=clearCaptured && compare(coin,gl,int(mode),name,"clear_rear",tolerance) && ok;
  cleared=coin;
  depthTarget->readbackDepth(clearDepth);
  size_t orderedDepth=0,clearChanged=0;
  if (frontDepth.size()==side*side && rearDepth.size()==frontDepth.size() &&
      clearDepth.size()==frontDepth.size()) {
    for (size_t p=0;p<frontDepth.size();++p) {
      orderedDepth+=frontDepth[p]+1e-6f<clearDepth[p];
      clearChanged+=std::abs(clearDepth[p]-rearDepth[p])>1e-6f;
    }
  }
  std::printf("technical_alpha mode=%d profile=%s depth_ordered_pixels=%zu clear_changed_pixels=%zu\n",
              int(mode),name,orderedDepth,clearChanged);
  ok=orderedDepth>0 && clearChanged>0 && ok;
  if (profile==Profile::ScreenDoor) {
    // The two alpha limits are distinct: zero leaves the baseline untouched;
    // one makes every foreground fragment opaque under SCREEN_DOOR.
    moveQuad(overlay,overlayExtent,.5f);
    overlay.material->transparency=1.0f;
    const bool zero=captureGL(glManager,renderer,base.root,gl) && captureCoin(adapter,coin);
    ok=zero && compare(coin,gl,int(mode),name,"alpha_zero",0) && coin==baseline && ok;
    overlay.material->transparency=0.0f;
    const bool one=captureGL(glManager,renderer,base.root,gl) && captureCoin(adapter,coin);
    ok=one && compare(coin,gl,int(mode),name,"alpha_one",0) && coin!=baseline && ok;
    overlay.material->transparency=.5f;
    moveQuad(overlay,overlayExtent,-.5f);
    ok=captureCoin(adapter,coin) && coin==cleared && ok;
    if (mode==SoRenderManager::WIREFRAME) {
      // Every 6-bit SCREEN_DOOR threshold, including the two endpoints,
      // must preserve CoinGL's framebuffer-anchored stipple phase.
      size_t passingLevels=0;
      for (int level=0;level<=64;++level) {
        overlay.material->transparency=float(level)/64.0f;
        char levelLabel[32];
        std::snprintf(levelLabel,sizeof(levelLabel),"level_%02d",level);
        const bool captured=captureGL(glManager,renderer,base.root,gl) &&
                            captureCoin(adapter,coin);
        const bool same=captured && compare(coin,gl,int(mode),name,levelLabel,0);
        passingLevels+=same;
        ok=same && ok;
      }
      overlay.material->transparency=.5f;
      ok=captureCoin(adapter,coin) && coin==cleared && ok;
      std::printf("technical_alpha mode=%d profile=%s screen_door_levels=%zu/65 side=%d subviewport=%d\n",
                  int(mode),name,passingLevels,side,int(oddSubviewport));
    }
  }
  // A failed frame must preserve the last published image and allow recovery.
  Pixels beforeFailure;
  auto * target=adapter.getSceneManager()->getRenderTarget();
  target->readbackRGBA(beforeFailure);
  const uint64_t serialBefore=target->getLastSubmissionSerial();
  overlay.alpha->function=SoAlphaTest::GREATER;
  overlay.alpha->value=.5f;
  const auto failure=adapter.render();
  Pixels afterFailure;
  target->readbackRGBA(afterFailure);
  const uint64_t serialAfter=target->getLastSubmissionSerial();
  overlay.alpha->function=SoAlphaTest::NONE;
  Pixels recovered;
  const bool recoveredOK=captureCoin(adapter,recovered);
  const bool atomic=failure==CoinRenderAction::UNSUPPORTED &&
      beforeFailure.size()==side*side*4u && afterFailure==beforeFailure &&
      serialAfter==serialBefore && recoveredOK && recovered==cleared;
  std::printf("technical_alpha mode=%d profile=%s distinct_front_rear=%d distinct_clear=%d atomic=%d reject_status=%d expected=%d rgba_preserved=%d serial_preserved=%d recovery=%d\n",
              int(mode),name,int(front!=rear),int(rear!=cleared),int(atomic),int(failure),
              int(CoinRenderAction::UNSUPPORTED),int(afterFailure==beforeFailure),
              int(serialAfter==serialBefore),int(recoveredOK && recovered==cleared));
  ok=ok && front!=rear && rear!=cleared && atomic;
  coinManager.removeSuperimposition(layer); glManager.removeSuperimposition(glLayer);
  overlay.root->unref(); base.root->unref();
  return ok?0:1;
}
}
int main(int argc,char ** argv) {
  SoDB::init(); CoinRenderAction::initClass(); LayerViewport::initClass();
  if (argc==2 && std::strcmp(argv[1],"--odd-subviewport")==0) {
    side=65;
    oddSubviewport=true;
  } else if (argc!=1) return 2;
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  CoinRenderCapabilities caps{};
  if (coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))!=0) return 77;
  std::printf("technical_alpha_runtime backend=%u renderer=%u vendor=0x%x device=0x%x adapter=%s\n",
              unsigned(caps.backend),unsigned(caps.renderer),unsigned(caps.vendor_id),
              unsigned(caps.device_id),caps.adapter_name);
  bool ok=true;
  for (const auto profile : {Profile::ScreenDoor,Profile::TextureBlend,Profile::MaterialTextureBlend})
   for (const auto dimensions : {std::array<int,2>{1,1},std::array<int,2>{2,2},std::array<int,2>{3,2}})
    for (const auto mode : {SoRenderManager::WIREFRAME,SoRenderManager::POINTS,
                          SoRenderManager::BOUNDING_BOX,SoRenderManager::WIREFRAME_OVERLAY,
                          SoRenderManager::HIDDEN_LINE,SoRenderManager::SHADED_HIDDEN_LINES}) {
    if (oddSubviewport && profile!=Profile::ScreenDoor) continue;
    if (profile==Profile::ScreenDoor && dimensions[0]!=2) continue;
    const int result=exercise(mode,profile,dimensions[0],dimensions[1]);
    if (result==77) return 77;
    ok &= result==0;
  }
  return ok?0:1;
}
