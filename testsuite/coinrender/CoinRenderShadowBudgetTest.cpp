#include "CoinRenderTestEnvironment.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
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
  const auto key=coin_render_shadow_content_key(frame,plan.passes[0],false,1,2,16*MiB);
  REQUIRE(!key.empty(),"Opaque map key captures actual caster content");
  REQUIRE(coin_render_shadow_content_key(frame,plan.passes[0],false,1,3,16*MiB)!=key,"Device generation invalidates map contents");
  REQUIRE(coin_render_shadow_content_key(frame,plan.passes[0],true,1,2,16*MiB)!=key,"Moment correction invalidates map contents");
  REQUIRE(coin_render_shadow_content_key(frame,plan.passes[0],false,1,2,4).empty(),"Oversized exact key safely disables content caching");
  auto changed=frame;changed.vertices[changed.indices[changed.draws[plan.passes[0].casterDraws[0]].geometry.firstIndex]].position[0]+=.1f;
  REQUIRE(coin_render_shadow_content_key(changed,plan.passes[0],false,1,2,16*MiB)!=key,"Caster position invalidates content");
  changed=frame;changed.materials[0].shininess=.8f;
  REQUIRE(coin_render_shadow_content_key(changed,plan.passes[0],false,1,2,16*MiB)==key,"Receiver BRDF does not invalidate opaque depth");
  auto lookup=plan.passes[0];lookup.epsilon=.01f;lookup.threshold=.3f;
  REQUIRE(coin_render_shadow_content_key(frame,lookup,false,1,2,16*MiB)==key,"VSM lookup controls remain independent of map content");
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
  options=CoinRenderOptions();options.pbrShadowFilter=static_cast<CoinRenderPbrShadowFilter>(3);
  REQUIRE(!coin_render_valid_options(options,diagnostic),"Unknown PBR shadow filter is rejected");
  options=CoinRenderOptions();options.pbrShadowSampleBudget=0;
  REQUIRE(!coin_render_valid_options(options,diagnostic),"Zero sample budget is rejected");
  options=CoinRenderOptions();options.pbrShadowMaxPcssRadiusTexels=0;
  REQUIRE(!coin_render_valid_options(options,diagnostic),"Zero PCSS admission radius is rejected");
  const char * spotText="ShadowSpotLight { sourceRadius 0.025 }";
  SoInput spotInput;spotInput.setBuffer(const_cast<char *>(spotText),std::strlen(spotText));SoNode * parsed=nullptr;
  REQUIRE(SoDB::read(&spotInput,parsed)&&parsed&&parsed->isOfType(SoShadowSpotLight::getClassTypeId()),"Native Coin spot emitter field reads from scene syntax");
  parsed->ref();REQUIRE(std::abs(static_cast<SoShadowSpotLight *>(parsed)->sourceRadius.getValue()-.025f)<1e-6,"Spot radius stores world distance");parsed->unref();
  const char * sunText="ShadowDirectionalLight { angularRadius 0.02 }";
  SoInput sunInput;sunInput.setBuffer(const_cast<char *>(sunText),std::strlen(sunText));parsed=nullptr;
  REQUIRE(SoDB::read(&sunInput,parsed)&&parsed&&parsed->isOfType(SoShadowDirectionalLight::getClassTypeId()),"Native Coin directional emitter field reads from scene syntax");
  parsed->ref();REQUIRE(std::abs(static_cast<SoShadowDirectionalLight *>(parsed)->angularRadius.getValue()-.02f)<1e-6,"Directional radius stores radians");parsed->unref();
  root->unref();
  std::cout << "shadow attachments: moments=128 MiB depth=32 MiB total=160 MiB; boundaries/rollback/views verified\n";
  return 0;
}
