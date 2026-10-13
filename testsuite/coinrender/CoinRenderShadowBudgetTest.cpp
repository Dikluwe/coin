#include "CoinRenderTestEnvironment.h"
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCube.h>
#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include <iostream>

#define REQUIRE(condition, message) do { if (!(condition)) { \
  std::cerr << message << '\n'; return 1; } } while (0)

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0,0,8); camera->nearDistance=1; camera->farDistance=20;
  root->addChild(camera);
  auto * group = new SoShadowGroup; root->addChild(group);
  for (int i=0;i<8;++i) {
    auto * light = new SoShadowSpotLight;
    light->location.setValue(2,2,4);light->direction.setValue(-2,-2,-4);
    group->addChild(light);
  }
  group->addChild(new SoCube);
  CoinRenderAction capture(SbViewportRegion(64,64)); capture.apply(root);
  const auto frame=capture.getPimpl()->lastRejectedShadowFrame;
  CoinRenderShadowPlan plan; std::string diagnostic;
  REQUIRE(coin_render_plan_shadows(frame,plan,diagnostic),"Default eight-map plan must remain admitted");
  REQUIRE(plan.passes.size()==8,"Eight independent maps are captured");
  constexpr uint64_t MiB=1024u*1024u;
  REQUIRE(plan.momentsBytes==128*MiB && plan.depthBytes==32*MiB &&
          plan.attachmentBytes==160*MiB,"Count all 1024-square RGBA32F and D32F attachments");
  const auto previous=plan;
  REQUIRE(!coin_render_plan_shadows(frame,plan,diagnostic,128*MiB),
          "128 MiB cannot admit 160 MiB of attachments");
  REQUIRE(plan.attachmentBytes==previous.attachmentBytes && plan.passes.size()==8,
          "Refused plans preserve the caller's previous publication");
  REQUIRE(coin_render_plan_shadows(frame,plan,diagnostic,160*MiB),"Exact boundary is admitted");
  auto larger=frame;larger.shadowGroups[0].precision=1;
  REQUIRE(!coin_render_plan_shadows(larger,plan,diagnostic),"Eight 2048-square maps exceed the physical limit");
  REQUIRE(plan.attachmentBytes==160*MiB,"Over-budget precision preserves the previous plan");
  REQUIRE(coin_render_shadow_view_budget(2,3,9,14,diagnostic),"All passes exactly fit the usable views");
  REQUIRE(!coin_render_shadow_view_budget(2,3,10,14,diagnostic),"Overlays cannot overlap shadow views");
  REQUIRE(!coin_render_shadow_view_budget(SIZE_MAX,3,0,14,diagnostic),"View arithmetic cannot wrap");
  CoinRenderOptions options;options.shadowAttachmentBudgetBytes=128*MiB;
  REQUIRE(coin_render_valid_options(options,diagnostic),"Lower explicit budget is legal");
  options.shadowAttachmentBudgetBytes=0;
  REQUIRE(!coin_render_valid_options(options,diagnostic),"Zero budget is invalid");
  options.shadowAttachmentBudgetBytes=161*MiB;
  REQUIRE(!coin_render_valid_options(options,diagnostic),"Unqualified larger budget is invalid");
  options=CoinRenderOptions();options.shadowDiagnostic=COIN_RENDER_SHADOW_DIAGNOSTIC_VISIBILITY;
  options.shadowDiagnosticMap=7;
  REQUIRE(coin_render_valid_options(options,diagnostic),"The eighth diagnostic slot is valid");
  options.shadowDiagnosticMap=8;
  REQUIRE(!coin_render_valid_options(options,diagnostic),"Diagnostic index cannot exceed the qualified map slots");
  options.shadowDiagnosticMap=0;options.shadowDiagnostic=static_cast<CoinRenderShadowDiagnostic>(4);
  REQUIRE(!coin_render_valid_options(options,diagnostic),"Unknown diagnostic mode is invalid");
  options=CoinRenderOptions();options.pbrShadowBiasTexels=-1;
  REQUIRE(!coin_render_valid_options(options,diagnostic),"Negative geometric bias is rejected");
  options.pbrShadowBiasTexels=4;options.pbrShadowSlopeBiasTexels=4;
  REQUIRE(coin_render_valid_options(options,diagnostic),"Bounded bias texel coefficients are admitted");
  options.pbrShadowSlopeBiasTexels=5;
  REQUIRE(!coin_render_valid_options(options,diagnostic),"Unbounded slope correction is rejected");
  root->unref();
  std::cout << "shadow attachments: moments=128 MiB depth=32 MiB total=160 MiB; boundaries/rollback/views verified\n";
  return 0;
}
