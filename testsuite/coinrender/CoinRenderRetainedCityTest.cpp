#include "config.h"
#include "CoinRenderRetainedTestRenderer.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include "rendering/coinrender/CoinRenderComposition.h"
#include <Inventor/rendering/SoRenderIR.h>
#include <memory>
#include <vector>
#include <cmath>
#include <iostream>
int main() {
  SoDB::init(); CoinRenderAction::initClass();
  CoinRenderOptions options;
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE) || defined(HAVE_COIN_BGFX)
  options.renderer = COIN_RENDER_RENDERER_VULKAN;
#endif
  if (!coinRetainedTestRenderer(options)) return 1;
  const SbViewportRegion viewport(128,128);
  std::unique_ptr<CoinRenderTarget> a(CoinRenderTarget::createOffscreen(SbVec2i32(128,128),options));
  std::unique_ptr<CoinRenderTarget> b(CoinRenderTarget::createOffscreen(SbVec2i32(128,128),options));
  auto * root=new SoSeparator;root->ref(); root->addChild(new SoDepthBuffer);
  auto * camera=new SoPerspectiveCamera;root->addChild(camera);
  auto * model=new SoLightModel;model->model=SoLightModel::PHONG;root->addChild(model);
  auto * light=new SoDirectionalLight;light->direction.setValue(-.5f,-1,-.3f);light->intensity=.85f;root->addChild(light);
  auto * fill=new SoDirectionalLight;fill->direction.setValue(.7f,-.4f,.5f);fill->intensity=.35f;root->addChild(fill);
  auto * cube=new SoCube;cube->width=cube->height=cube->depth=1;
  auto * mat=new SoMaterial;mat->diffuseColor.setValue(.36f,.52f,.67f);
  auto * commonTranslation=new SoTranslation;root->addChild(commonTranslation);
  auto * moving=new SoTranslation;
  auto * uniqueMaterial=new SoMaterial;uniqueMaterial->diffuseColor=mat->diffuseColor;
  for(int i=0;i<400;++i) {
    auto * part=new SoSeparator;auto * transform=new SoTransform;
    transform->translation.setValue((i%20-10)*2.2f,1.5f,(i/20-10)*2.2f);
    transform->scaleFactor.setValue(1.3f,1.2f+(i%7),1.7f);
    if (!i) part->addChild(moving);
    part->addChild(transform);part->addChild(i ? mat : uniqueMaterial);part->addChild(cube);root->addChild(part);
  }
  camera->orientation.setValue(SbRotation(SbVec3f(0,0,-1),SbVec3f(-.5f,-.35f,-1)));
  camera->viewAll(root,viewport,1.15f);
  CoinRenderAction current(viewport);current.setRenderTarget(a.get());
  CoinRenderRetainedRenderer retained(viewport);
  auto compare=[&]() {
    // Fresh current capture is the oracle for each independent edit probe.
    current.setViewportRegion(viewport);
    current.apply(root);
    const auto status=retained.render(root,*b);
    if(current.getLastStatus()!=CoinRenderAction::SUCCESS || status!=CoinRenderAction::SUCCESS) {
      std::cerr<<"execution: "<<retained.getLastError().getString()<<" / "<<current.getLastError().getString()<<'\n';return false;
    }
    std::vector<uint8_t> x,y;a->readbackRGBA(x);b->readbackRGBA(y);
    std::vector<float> dx,dy;a->readbackDepth(dx);b->readbackDepth(dy);
    size_t different=0,depthDifferent=0,exactDepthDifferent=0;int maximum=0;
    for(size_t i=0;i<x.size();++i){int e=std::abs(int(x[i])-int(y[i]));maximum=std::max(maximum,e);different+=e>2;}
    for(size_t i=0;i<dx.size();++i){depthDifferent+=std::abs(dx[i]-dy[i])>2e-6f;exactDepthDifferent+=dx[i]!=dy[i];}
    std::cout<<"city channels_different="<<different<<" maximum="<<maximum<<" depth_different="<<depthDifferent<<'\n';
#if defined(HAVE_COIN_BGFX)
    if(exactDepthDifferent){std::cerr<<"exact BGFX depth differences="<<exactDepthDifferent<<'\n';return false;}
#endif
    return !x.empty() && x.size()==y.size() && dx.size()==dy.size() && !different && !depthDifferent;
  };
  bool ok=compare();auto first=retained.getStatistics();
  ok=ok && first.captures==1 && first.planBuilds==1 && first.nativeCommands==400 &&
    first.transportScheduleBytes==0 && first.nativeGeometryResources==1 && first.transportVertices==24 && first.transportCameras==1;
  ok=ok && first.nativeCompactionPasses==0 && first.nativeDependencyRanges==401 &&
    retained.getDrawList().hasCompactCommands() &&
    first.nativeCommandStorageBytes < uint64_t(400)*sizeof(SoRenderCommand)/4;
  // A copy owns its compact table. Legacy mutable references expand only the
  // copy; retained snapshots and per-command matrices must remain independent.
  SoDrawList copy=retained.getDrawList();
  const auto original=retained.getDrawList().getCommandSnapshot(7);
  auto changed=copy.getCommandSnapshot(7);
  changed.modelMatrix[3][0]+=3; changed.modelMatrix[0][1]=.125f; changed.modelMatrix[3][3]=1.25f; changed.material.diffuse[0]=.125f;
  changed.state.raster.linePattern=0x0123;
  changed.pixelRaster.originX=-0.0f;
  changed.pick.elementRanges.push_back(SoRenderElementRange());
  copy.setCommandForRetainedUpdate(7,changed);
  ok=ok && copy.hasCompactCommands() &&
    copy.getCommandSnapshot(7).modelMatrix==changed.modelMatrix &&
    copy.getCommandSnapshot(7).material.diffuse==changed.material.diffuse &&
    copy.getCommandSnapshot(7).state.raster.linePattern==0x0123 &&
    copy.getCommandSnapshot(7).pick.elementRanges.size()==1 &&
    retained.getDrawList().getCommandSnapshot(7).modelMatrix==original.modelMatrix;
  const auto checkpoint=copy.createMutationCheckpoint();
  copy.addCommand(changed);copy.restoreMutationCheckpoint(checkpoint);
  ok=ok && copy.getNumCommands()==400 && copy.hasCompactCommands();
  copy.getCommand(7).material.diffuse[1]=.25f;
  ok=ok && !copy.hasCompactCommands() &&
    copy.getCommandSnapshot(7).material.diffuse[1]==.25f &&
    retained.getDrawList().hasCompactCommands();
  ok=ok && copy.end()-copy.begin()==400;
  SoDrawList diverse;
  for (int i=0;i<64;++i) { SoRenderCommand c=original;c.objectId=i;diverse.addCommand(c); }
  diverse.compactRetainedStorage();
  ok=ok && !diverse.hasCompactCommands() && diverse.getNumCommands()==64 &&
    diverse.getCommandSnapshot(63).objectId==63;
  std::cout<<"commands="<<first.nativeCommands<<" geometry="<<first.nativeGeometryResources<<" vertices="<<first.transportVertices<<" cameras="<<first.transportCameras<<'\n';
  ok=retained.render(root,*a)==CoinRenderAction::SUCCESS && ok;
  ok=retained.render(root,*b)==CoinRenderAction::SUCCESS && ok;
  ok=ok && retained.getCaptureCount()==1 && retained.getPlanBuildCount()==1 && retained.getStatistics().transportBuilds==1;
  auto classifications=coin_render_composition_classification_count_for_tests();
  ok=compare() && ok;
  // Current path may classify; an isolated static retained frame must not.
  classifications=coin_render_composition_classification_count_for_tests();
  ok=retained.render(root,*b)==CoinRenderAction::SUCCESS && ok;
  ok=ok && coin_render_composition_classification_count_for_tests()==classifications && retained.getCaptureCount()==1 && retained.getPlanBuildCount()==1;
  moving->translation.setValue(.1f,0,.2f);ok=compare() && ok;
  auto moved=retained.getStatistics();
  ok=ok && moved.captures==1 && moved.planBuilds==1 && moved.transportBuilds==1 &&
    moved.incrementalUpdates==1 && moved.transportPatchedCommands==1 && retained.getDrawList().hasCompactCommands();
  uniqueMaterial->diffuseColor.setValue(.7f,.45f,.34f);ok=compare() && ok;
  auto colored=retained.getStatistics();
  ok=ok && colored.captures==1 && colored.planBuilds==1 && colored.transportBuilds==1 &&
    colored.incrementalUpdates==2 && colored.transportPatchedCommands==2 && colored.transportVertices==48 &&
    retained.getDrawList().hasCompactCommands();
  commonTranslation->translation.setValue(.1f,0,0);ok=compare() && ok;
  const auto commonMoved=retained.getStatistics();
  ok=ok && commonMoved.captures==1 && commonMoved.planBuilds==1 && commonMoved.transportBuilds==1 &&
    commonMoved.incrementalUpdates==3 && commonMoved.transportPatchedCommands==402 &&
    commonMoved.nativeCompactionPasses==0 && commonMoved.nativeDependencyRanges==401;
  mat->diffuseColor.setValue(.7f,.45f,.34f);ok=compare() && ok;
  cube->height=1.4f;ok=compare() && ok;
  camera->position.setValue(camera->position.getValue()+SbVec3f(.1f,0,.2f));ok=compare() && ok;
  retained.setViewportRegion(SbViewportRegion(128,128));
  current.setRenderTarget(nullptr);root->unref();return ok?0:1;
}
