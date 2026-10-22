#include <Inventor/SoPath.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <cstdio>
#include <memory>
#include <vector>

static_assert(__cplusplus==201103L,"public consumer fixture must compile as C++11");

#if defined(COIN_RENDER_ACTION_OPTIONS_VERSION) && COIN_RENDER_ACTION_OPTIONS_VERSION >= 1 && \
    defined(COIN_RENDER_LEGACY_IO_VERSION) && COIN_RENDER_LEGACY_IO_VERSION >= 1
namespace {
CoinRenderAction::AbortCode abortCallback(void *) {return CoinRenderAction::CONTINUE;}
void preRender(void *,CoinRenderAction *) {}
float sortedDistance(void *,CoinRenderAction *) {return 3.f;}
}
// This translation unit deliberately uses only installed public headers and C++11.
// Linking its caller is optional; all overloads and ownership types are checked.
void coinRenderPublicCxx11Consumer(SoNode *scene,SoPath *path,FILE *output) {
  CoinRenderOptions options;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(17,13),options));
  if(!target)return;
  CoinRenderAction action(SbViewportRegion(17,13));action.setRenderTarget(target.get());
  action.setBackgroundColor(SbColor4f(.1f,.2f,.3f,1));
  action.setUpdateArea(SbVec2f(0,0),SbVec2f(1,1));
  SbVec2f origin,size;action.getUpdateArea(origin,size);
  action.setAbortCallback(abortCallback,nullptr);
  auto *function=&abortCallback;void *closure=nullptr;action.getAbortCallback(function,closure);
  action.addPreRenderCallback(preRender,nullptr);action.removePreRenderCallback(preRender,nullptr);
  action.setSortedObjectOrderStrategy(CoinRenderAction::CUSTOM_CALLBACK,sortedDistance,nullptr);
  action.setDelayedObjDepthWrite(FALSE);(void)action.getDelayedObjDepthWrite();
  (void)action.isRenderingDelayedPaths();(void)action.abortNow();
  action.apply(scene);action.apply(path);
  std::size_t bytes=0;(void)target->borrowLegacyComponents(CoinRenderLegacyComponents::RGB,bytes);
  (void)CoinRenderTarget::isLegacyImageFileSupported(SbString("PNG"));
  const int count=CoinRenderTarget::getNumLegacyImageFileTypes();
  std::vector<SbString> extensions;SbString name,description;
  if(count)(void)CoinRenderTarget::getLegacyImageFileTypeInfo(0,extensions,name,description);
  (void)target->writeLegacyRGB(output,CoinRenderLegacyComponents::RGB);
  (void)target->writeLegacyRGB("consumer.rgb",CoinRenderLegacyComponents::RGB);
  (void)target->writeLegacyPostScript(output,CoinRenderLegacyComponents::RGB,SbVec2f(8.5f,11),72);
  (void)target->writeLegacyPostScript("consumer.eps",CoinRenderLegacyComponents::RGB,SbVec2f(8.5f,11),72);
  action.setRenderTarget(nullptr);target->resize(SbVec2i32(19,11));
}
#else
const char *coinRenderPortableContractAbsent="new public API absent from baseline headers";
#endif
