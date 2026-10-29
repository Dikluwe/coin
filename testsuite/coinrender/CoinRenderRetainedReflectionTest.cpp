// Independent image/depth oracle: a white ideal receiver must reproduce an
// emissive object's geometrically mirrored image, including proper clipping.
#include "config.h"
#include <Inventor/SoDB.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/actions/CoinRenderAction.h>
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderPlanarReflectionCore.h"
#include "rendering/coinbgfx/CoinBgfxBackend.h"
#include <memory>
#include <iostream>
#include <fstream>
#include <stdexcept>
static void check(bool b,const char * m){if(!b)throw std::runtime_error(m);}
static SoSeparator * box(const SbVec3f & position,const SbColor & color) {
  auto * s=new SoSeparator;auto * t=new SoTranslation;t->translation=position;s->addChild(t);
  auto * m=new SoPBRMaterial;m->baseColor.setValue(0,0,0);m->emissive=color;s->addChild(m);
  auto * hints=new SoShapeHints;hints->vertexOrdering=SoShapeHints::COUNTERCLOCKWISE;hints->shapeType=SoShapeHints::SOLID;s->addChild(hints);
  auto * cube=new SoCube;cube->width=1.5f;cube->height=1;cube->depth=1;s->addChild(cube);return s;
}
int main(int argc,char **argv){try {
  SoDB::init();CoinRenderAction::initClass();
  auto * root=new SoSeparator;root->ref();auto * camera=new SoPerspectiveCamera;
  camera->position.setValue(4,-7,5);camera->nearDistance=.1f;camera->farDistance=30;camera->heightAngle=.7f;
  camera->pointAt(SbVec3f(0,0,.5f),SbVec3f(0,0,1));root->addChild(camera);
  auto * model=new SoLightModel;model->model=SoLightModel::PHONG;root->addChild(model);
  auto * red=box(SbVec3f(.5f,0,1),SbColor(.4f,0,0));root->addChild(red);
  root->addChild(box(SbVec3f(-1.5f,0,-1),SbColor(0,0,.4f)));
  auto * floor=new SoSeparator;auto * material=new SoPBRMaterial;material->baseColor.setValue(1,1,1);material->metallic=1;material->roughness=.035f;floor->addChild(material);
  auto * coordinates=new SoCoordinate3;const SbVec3f points[4]={{-20,-20,0},{20,-20,0},{20,20,0},{-20,20,0}};coordinates->point.setValues(0,4,points);floor->addChild(coordinates);
  auto * normal=new SoNormal;normal->vector.set1Value(0,SbVec3f(0,0,1));floor->addChild(normal);
  auto * binding=new SoNormalBinding;binding->value=SoNormalBinding::OVERALL;floor->addChild(binding);
  auto * face=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,-1,0,2,3,-1};face->coordIndex.setValues(0,8,indices);floor->addChild(face);root->addChild(floor);
  const SbVec2i32 size(256,192);const SbViewportRegion viewport(256,192);
  CoinRenderOptions options;options.renderer=COIN_RENDER_RENDERER_VULKAN;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(size,options));check(bool(target),"target");
  CoinRenderRetainedRenderer retained(viewport);CoinRenderPbrEffects effects;
  effects.reflectionScale=1;
  const auto render=[&](){check(retained.render(root,*target)==CoinRenderAction::SUCCESS,retained.getLastError().getString());};
  std::vector<uint8_t> off,on,control;std::vector<float> depth,afterDepth;
#ifdef COIN_WGPU_TEST_HOOKS
  const auto classifications=coin_render_composition_classification_count_for_tests();
#endif
  render();target->readbackRGBA(off);target->readbackDepth(depth);
  effects.planarReflection=true;check(target->setPbrEffects(effects),"enable reflection");render();target->readbackRGBA(on);target->readbackDepth(afterDepth);
#ifdef COIN_WGPU_TEST_HOOKS
  check(coin_render_composition_classification_count_for_tests()==classifications,"reflection invoked another composition planner");
  std::cout<<"additional_composition_classifications=0\n";
#endif
  check(depth==afterDepth,"reflection changed primary depth");check(off!=on,"reflection has no effect");
  auto * reference=new SoSeparator;reference->ref();reference->addChild(camera);reference->addChild(model);
  auto * scale=new SoScale;scale->scaleFactor.setValue(1,1,-1);reference->addChild(scale);
  auto * virtualBox=box(SbVec3f(.5f,0,1),SbColor(.4f,0,0));
  static_cast<SoShapeHints *>(virtualBox->getChild(2))->vertexOrdering=SoShapeHints::UNKNOWN_ORDERING;reference->addChild(virtualBox);
  std::unique_ptr<CoinRenderTarget> oracle(CoinRenderTarget::createOffscreen(size,options));
  CoinRenderAction action(viewport);action.setRenderTarget(oracle.get());action.apply(reference);check(action.getLastStatus()==CoinRenderAction::SUCCESS,"independent mirrored geometry oracle");oracle->readbackRGBA(control);
  if(argc>1) {const std::string prefix=argv[1];for(auto pair:{std::make_pair("off",&off),std::make_pair("on",&on),std::make_pair("oracle",&control)}) {std::ofstream out(prefix+"-"+pair.first+".rgba",std::ios::binary);out.write(reinterpret_cast<const char *>(pair.second->data()),pair.second->size());}}
  size_t pixels=0;unsigned maxError=0;
  for(size_t p=0;p<depth.size();++p) {
    if(control[p*4]>150&&control[p*4+1]<5&&control[p*4+2]<5&&off[p*4]<5&&off[p*4+1]<5&&off[p*4+2]<5&&depth[p]<1) {
      ++pixels;for(int c=0;c<4;++c)maxError=std::max(maxError,unsigned(std::abs(int(on[p*4+c])-int(control[p*4+c]))));
    }
    check(!(on[p*4+2]>150&&on[p*4]<5),"geometry below mirror plane leaked into reflection");
  }
  check(pixels>100,"mirrored oracle has insufficient floor coverage");
  std::cout<<"oracle_pixels="<<pixels<<" maximum_channel_error="<<maxError<<'\n';check(maxError==0,"ideal mirror differs from independent virtual-object rendering");
  std::unique_ptr<CoinRenderTarget> callbackTarget(CoinRenderTarget::createOffscreen(size,options));callbackTarget->setPbrEffects(effects);
  action.setRenderTarget(callbackTarget.get());action.apply(root);check(action.getLastStatus()==CoinRenderAction::SUCCESS,"callback reflection");
  std::vector<uint8_t> callback;callbackTarget->readbackRGBA(callback);check(callback==on,"retained/callback reflection mismatch");
  const auto stats=retained.getStatistics();check(stats.captures==1&&stats.planBuilds==1&&stats.transportBuilds==1,"reflection duplicated capture/plan/transport");
  const auto serial=target->getLastSubmissionSerial();auto invalid=effects;invalid.reflectionBudgetBytes=1;target->setPbrEffects(invalid);
  check(retained.render(root,*target)==CoinRenderAction::UNSUPPORTED,"reflection budget admitted");target->readbackRGBA(callback);check(callback==on&&serial==target->getLastSubmissionSerial(),"budget failure replaced publication");
  target->setPbrEffects(effects);render();target->readbackRGBA(callback);check(callback==on,"reflection retry changed image");
  auto bad=effects;bad.reflectionPlane=SbPlane(SbVec3f(0,0,1),10);
  target->setPbrEffects(bad);check(retained.render(root,*target)==CoinRenderAction::UNSUPPORTED,"missing receiver admitted");
  target->setPbrEffects(effects);render();target->readbackRGBA(callback);check(callback==on,"receiver retry changed image");
  // Receiver admission is explicit; an opaque but rough plane is not silently
  // rendered as an ideal mirror. Failed frames preserve the current image.
  const auto roughSerial=target->getLastSubmissionSerial();material->roughness=.5f;
  check(retained.render(root,*target)==CoinRenderAction::UNSUPPORTED,"rough mirror receiver admitted");
  target->readbackRGBA(callback);check(callback==on&&target->getLastSubmissionSerial()==roughSerial,"rough receiver refusal published");
  material->roughness=.035f;render();target->readbackRGBA(callback);check(callback==on,"smooth receiver retry changed image");
  for(uint32_t divisor:{2u,4u}) {
    auto scaled=effects;scaled.reflectionScale=divisor;target->setPbrEffects(scaled);render();
    const auto * backend=static_cast<CoinBgfxBackend *>(target->getPimpl()->backend.get());
    check(backend->getPbrReflectionAttachmentBytes()==uint64_t((size[0]+divisor-1)/divisor)*((size[1]+divisor-1)/divisor)*12,"reflection allocation footprint");
    callbackTarget->setPbrEffects(scaled);action.apply(root);check(action.getLastStatus()==CoinRenderAction::SUCCESS,"scaled callback reflection");
    std::vector<uint8_t> expected;callbackTarget->readbackRGBA(expected);target->readbackRGBA(callback);check(callback==expected,"scaled reflected buffers differ");
    target->readbackDepth(afterDepth);check(afterDepth==depth,"scaled reflection changed primary depth");
  }
  target->setPbrEffects(effects);render();target->readbackRGBA(callback);check(callback==on,"resolution restoration changed image");
  auto invalidScale=effects;invalidScale.reflectionScale=3;check(!target->setPbrEffects(invalidScale),"invalid reflection scale accepted");
  auto * legacy=new SoSeparator;legacy->ref();legacy->addChild(camera);legacy->addChild(model);legacy->addChild(new SoCube);
  CoinRenderRetainedRenderer legacyRenderer(viewport);const auto legacySerial=target->getLastSubmissionSerial();
  check(legacyRenderer.render(legacy,*target)==CoinRenderAction::UNSUPPORTED,"reflection disappeared on a legacy scene");
  target->readbackRGBA(callback);check(callback==on&&target->getLastSubmissionSerial()==legacySerial,"legacy reflection refusal published");legacy->unref();
  const auto matrix=coin_render_plane_reflection(SbPlane(SbVec3f(0,0,1),2));SbVec3f point;matrix.multVecMatrix(SbVec3f(3,4,5),point);check(point==SbVec3f(3,4,-1),"offset plane reflection");
  SbVec3f restored;matrix.multVecMatrix(point,restored);check(restored==SbVec3f(3,4,5),"reflection involution");
  SbMatrix ortho=SbMatrix::identity();ortho[2][2]=-2.f/9.f;ortho[3][2]=-3.f/9.f;
  std::string projectionError;check(coin_render_reflection_projection(ortho,20,projectionError),"orthographic negative near plane rejected");
  check(ortho[0][0]==1&&ortho[1][1]==1,"reflection altered XY projection");
  const SbPlane tilted(SbVec3f(.6f,0,.8f),.4f);const auto tiltedMirror=coin_render_plane_reflection(tilted);
  SbVec3f reflectedPoint;const SbVec3f sourcePoint(3,2,5);tiltedMirror.multVecMatrix(sourcePoint,reflectedPoint);
  check(std::abs(tilted.getDistance(sourcePoint)+tilted.getDistance(reflectedPoint))<1e-5f,"tilted reflection distance");
  SbMatrix view;view.setTransform(SbVec3f(2,3,4),SbRotation(SbVec3f(0,1,0),.4f),SbVec3f(1,1,1));
  const auto delta=view.inverse()*tiltedMirror*view;SbVec3f oldEyeLight,newEyeLight,directEyeLight;
  view.multVecMatrix(SbVec3f(2,3,7),oldEyeLight);delta.multVecMatrix(oldEyeLight,newEyeLight);
  (tiltedMirror*view).multVecMatrix(SbVec3f(2,3,7),directEyeLight);
  check(newEyeLight.equals(directEyeLight,1e-4f),"reflection moved the physical world light");
  // Positive finite proxy height: remove the clipped blue control, retaining
  // the physical red box. Bounds are now centered one unit above the plane.
  root->removeChild(3);target->setPbrEffects(effects);render();
  std::vector<uint8_t> smooth;target->readbackRGBA(smooth);target->readbackDepth(depth);
  const auto roughStats=retained.getStatistics();
  auto roughEffects=effects;roughEffects.roughPlanarReflection=true;target->setPbrEffects(roughEffects);
  std::vector<uint8_t> previousRough;
  const auto roughRender=[&](){
#ifdef COIN_WGPU_TEST_HOOKS
    const auto count=coin_render_composition_classification_count_for_tests();
#endif
    render();
#ifdef COIN_WGPU_TEST_HOOKS
    check(count==coin_render_composition_classification_count_for_tests(),"rough filter invoked composition planning");
#endif
  };
  for(float roughness:{.18f,.32f,.65f}) {
    material->roughness=roughness;roughRender();target->readbackRGBA(callback);target->readbackDepth(afterDepth);
    check(depth==afterDepth,"rough reflection changed physical depth");
    check(callback!=smooth&&callback!=previousRough,"roughness failed to change reflected image");
    size_t spread=0;
    for(size_t pixel=0;pixel<depth.size();++pixel)
      if(depth[pixel]<1&&smooth[pixel*4]<5&&callback[pixel*4]>10)++spread;
    std::cout<<"roughness="<<roughness<<" newly_lit_floor_pixels="<<spread<<'\n';
    if(roughness>.6f)check(spread>20,"rough filter did not broaden the reflected silhouette");
    callbackTarget->setPbrEffects(roughEffects);action.apply(root);
    check(action.getLastStatus()==CoinRenderAction::SUCCESS,"rough callback admission");
    std::vector<uint8_t> expected;callbackTarget->readbackRGBA(expected);
    check(callback==expected,"rough retained/callback pixels differ");previousRough=callback;
    if(argc>1){std::ofstream file(std::string(argv[1])+"-rough-"+std::to_string(roughness)+".rgba",std::ios::binary);file.write(reinterpret_cast<const char *>(callback.data()),callback.size());}
  }
  for(uint32_t divisor:{2u,4u}) {
    auto scaled=roughEffects;scaled.reflectionScale=divisor;target->setPbrEffects(scaled);roughRender();
    callbackTarget->setPbrEffects(scaled);action.apply(root);
    check(action.getLastStatus()==CoinRenderAction::SUCCESS,"rough scaled callback admission");
    std::vector<uint8_t> expected;callbackTarget->readbackRGBA(expected);target->readbackRGBA(callback);
    check(expected==callback,"rough scaled callback buffers differ");target->readbackDepth(afterDepth);
    check(afterDepth==depth,"rough scaled primary depth changed");
  }
  target->setPbrEffects(roughEffects);roughRender();target->readbackRGBA(callback);
  check(callback==previousRough,"rough scale restoration changed image");
  // Both paths above use one capture/planner each. Only material scalars change.
  const auto finalStats=retained.getStatistics();
  check(finalStats.captures==roughStats.captures&&finalStats.planBuilds==roughStats.planBuilds&&
        finalStats.transportBuilds==roughStats.transportBuilds&&finalStats.copiedVertices==roughStats.copiedVertices,
        "rough material edits duplicated retained geometry/capture/planning");
  auto roughBudget=roughEffects;roughBudget.reflectionBudgetBytes=uint64_t(size[0])*size[1]*12;
  target->setPbrEffects(roughBudget);const auto roughFrame=target->getLastSubmissionSerial();
  check(retained.render(root,*target)==CoinRenderAction::UNSUPPORTED,"rough mapping working bytes escaped budget");
  target->readbackRGBA(callback);check(callback==previousRough&&target->getLastSubmissionSerial()==roughFrame,"rough budget failure published");
  target->setPbrEffects(roughEffects);render();
  material->anisotropy=.5f;check(retained.render(root,*target)==CoinRenderAction::UNSUPPORTED,"anisotropic receiver admitted");
  material->anisotropy=0;material->clearcoat=.3f;
  check(retained.render(root,*target)==CoinRenderAction::UNSUPPORTED,"rough coat reused base roughness filter");
  material->clearcoat=0;render();target->readbackRGBA(callback);check(callback==previousRough,"rough failure/retry changed publication");
  material->roughness=.035f;render();target->readbackRGBA(callback);check(callback==smooth,"rough selector leaked into smooth mirror");
  std::cout<<"PASS: rough GGX .18/.32/.65, exact callback/depth, scalar reuse, mapping budget, refusal/retry and smooth reset\n";
  action.setRenderTarget(nullptr);reference->unref();root->unref();std::cout<<"PASS: reflection image, clipping, depth, reuse and failure/retry\n";return 0;
}catch(const std::exception & e){std::cerr<<e.what()<<'\n';return 1;}}
