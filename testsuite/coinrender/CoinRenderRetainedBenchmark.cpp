#include "config.h"
#include "CoinRenderRetainedTestRenderer.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include "rendering/coinrender/CoinRenderComposition.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char ** argv) {
  if (argc != 4) { std::cerr << "usage: benchmark current|retained|rebuild occurrences samples\n"; return 2; }
  const std::string mode = argv[1];
  const int count = std::atoi(argv[2]), samples = std::atoi(argv[3]);
  if ((mode != "current" && mode != "retained" && mode != "rebuild") || count < 1 || count > 4096 || samples < 3 || samples > 1000) return 2;
  SoDB::init();
  CoinRenderAction::initClass();
  CoinRenderOptions options;
  const char * backend = "recording";
#if defined(HAVE_COIN_BGFX)
  backend = "bgfx-vulkan"; options.renderer = COIN_RENDER_RENDERER_VULKAN;
#elif defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  backend = "wgpu-vulkan"; options.renderer = COIN_RENDER_RENDERER_VULKAN;
#endif
  if (!coinRetainedTestRenderer(options)) return 1;
#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  if (options.renderer == COIN_RENDER_RENDERER_OPENGL) backend = "wgpu-opengl";
#elif defined(HAVE_COIN_BGFX)
  if (options.renderer == COIN_RENDER_RENDERER_OPENGL) backend = "bgfx-opengl";
#endif
  const SbViewportRegion viewport(128,128);
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(128,128),options));
  std::unique_ptr<CoinRenderTarget> control(CoinRenderTarget::createOffscreen(SbVec2i32(128,128),options));
  CoinRenderAction current(viewport);
  current.setRenderTarget(target.get()); current.setBackgroundColor(SbColor4f(0,0,0,1));
  CoinRenderRetainedRenderer retained(viewport);
  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera;
  const int side = static_cast<int>(std::ceil(std::sqrt(double(count))));
  camera->position.setValue(0,0,5); camera->height = float(side+1);
  camera->nearDistance = 1; camera->farDistance = 10; root->addChild(camera);
  auto * light = new SoLightModel; light->model = SoLightModel::BASE_COLOR; root->addChild(light);
  root->addChild(new SoDepthBuffer);
  auto * recipe = new SoSeparator;
  auto * coordinates = new SoCoordinate3;
  const SbVec3f triangle[] = {SbVec3f(-0.35f,-0.35f,0),SbVec3f(0.35f,-0.35f,0),SbVec3f(0,0.35f,0)};
  coordinates->point.setValues(0,3,triangle);
  auto * face = new SoFaceSet; face->numVertices.setValue(3);
  recipe->addChild(coordinates); recipe->addChild(face);
  SoTranslation * moving = nullptr; SoMaterial * colored = nullptr;
  SbVec3f anchor;
  for (int i=0; i<count; ++i) {
    auto * part = new SoSeparator;
    auto * translation = new SoTranslation;
    translation->translation.setValue(float(i%side)-float(side-1)*0.5f,float(i/side)-float(side-1)*0.5f,0);
    auto * material = new SoMaterial; material->diffuseColor.setValue(0.7f,0.3f,0.2f);
    part->addChild(translation); part->addChild(material); part->addChild(recipe); root->addChild(part);
    if (!i) { moving=translation; colored=material; anchor=translation->translation.getValue(); }
  }
  const auto render = [&]() {
    if (mode == "current") { current.apply(root); return current.getLastStatus(); }
    if (mode == "rebuild") retained.invalidate();
    return retained.render(root,*target);
  };
  const auto mutate = [&](int phase, int frame) {
    const float step = (frame%2) ? 0.08f : -0.08f;
    if (phase == 1) moving->translation.setValue(anchor+SbVec3f(step,0,0));
    if (phase == 2) colored->diffuseColor.setValue(0.7f+step,0.3f,0.2f);
    if (phase == 3) coordinates->point.set1Value(0,SbVec3f(-0.35f+step,-0.35f,0));
  };
  const char * names[] = {"static","translation_one","diffuse_one","geometry_shared"};
  bool allEqual=true;
  for (int phase=0; phase<4; ++phase) {
    // Each mode is a separate process. Warm after the previous phase's control
    // rendering so shared runtime caches have the measured target's payload.
    for (int warm=0; warm<7; ++warm) {
      mutate(phase,warm);
      if (render()!=CoinRenderAction::SUCCESS) { std::cerr << "warmup failed " << retained.getLastError().getString() << '\n'; return 1; }
    }
    const auto before=retained.getStatistics();
    const auto classifications=coin_render_composition_classification_count_for_tests();
    std::vector<double> milliseconds, editMilliseconds, totalMilliseconds;
    uint64_t uploaded=0,uploads=0; bool telemetry=true;
    for (int sample=0; sample<samples; ++sample) {
      const auto editStart=std::chrono::steady_clock::now();
      mutate(phase,sample+7);
      const auto start=std::chrono::steady_clock::now();
      const auto status=render();
      const auto end=std::chrono::steady_clock::now();
      if (status!=CoinRenderAction::SUCCESS) { std::cerr << "frame failed\n"; return 1; }
      editMilliseconds.push_back(std::chrono::duration<double,std::milli>(start-editStart).count());
      totalMilliseconds.push_back(std::chrono::duration<double,std::milli>(end-editStart).count());
      milliseconds.push_back(std::chrono::duration<double,std::milli>(end-start).count());
      CoinRenderCacheTelemetry cache;
      if (target->getCacheTelemetry(cache)) { uploaded+=cache.frameUploadedBytes; uploads+=cache.frameUploads; }
      else telemetry=false;
    }
    const auto after=retained.getStatistics();
    const auto extraClassifications=coin_render_composition_classification_count_for_tests()-classifications;
    std::vector<uint8_t> image,reference; std::vector<float> depth,referenceDepth;
    target->readbackRGBA(image); target->readbackDepth(depth);
    // Keep the correctness observer outside measured mutation/notification work.
    CoinRenderRetainedRenderer forced(viewport);
    forced.invalidate();
    if (forced.render(root,*control)!=CoinRenderAction::SUCCESS) { std::cerr << "control failed\n"; return 1; }
    control->readbackRGBA(reference); control->readbackDepth(referenceDepth);
    const bool pixelsEqual=image==reference,depthEqual=depth==referenceDepth;
    allEqual=allEqual&&pixelsEqual&&depthEqual;
    std::sort(milliseconds.begin(),milliseconds.end());
    std::sort(editMilliseconds.begin(),editMilliseconds.end());
    std::sort(totalMilliseconds.begin(),totalMilliseconds.end());
    const auto quantile=[&](double q){return milliseconds[static_cast<size_t>(q*(milliseconds.size()-1))];};
    std::cout << std::fixed << std::setprecision(6)
      << "{\"backend\":\"" << backend << "\",\"path\":\"" << mode << "\",\"phase\":\"" << names[phase]
      << "\",\"occurrences\":" << count << ",\"samples\":" << samples << ",\"warmup\":7,\"width\":128,\"height\":128"
      << ",\"median_ms\":" << quantile(0.5) << ",\"p90_ms\":" << quantile(0.9)
      << ",\"edit_median_ms\":" << editMilliseconds[editMilliseconds.size()/2]
      << ",\"total_median_ms\":" << totalMilliseconds[totalMilliseconds.size()/2]
      << ",\"pixels_equal\":" << (pixelsEqual?"true":"false") << ",\"depth_equal\":" << (depthEqual?"true":"false")
      << ",\"classifications\":" << extraClassifications;
    if (mode!="current") std::cout
      << ",\"captures\":" << after.captures-before.captures << ",\"plan_builds\":" << after.planBuilds-before.planBuilds
      << ",\"incremental_updates\":" << after.incrementalUpdates-before.incrementalUpdates
      << ",\"patched_commands\":" << after.patchedCommands-before.patchedCommands
      << ",\"transport_builds\":" << after.transportBuilds-before.transportBuilds
      << ",\"transport_reuses\":" << after.transportReuses-before.transportReuses
      << ",\"transport_patched_commands\":" << after.transportPatchedCommands-before.transportPatchedCommands
      << ",\"copied_vertices\":" << after.copiedVertices-before.copiedVertices
      << ",\"copied_indices\":" << after.copiedIndices-before.copiedIndices
      << ",\"transport_geometry_patches\":" << after.transportGeometryPatches-before.transportGeometryPatches
      << ",\"geometry_patched_vertices\":" << after.geometryPatchedVertices-before.geometryPatchedVertices
      << ",\"material_slots\":" << after.materialSlots;
    if (telemetry) std::cout << ",\"cache_uploaded_bytes\":" << uploaded << ",\"cache_uploads\":" << uploads;
    std::cout << "}\n" << std::flush;
  }
  current.setRenderTarget(nullptr); root->unref();
  return allEqual?0:1;
}
