#include "config.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/lists/SoFieldList.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include "rendering/SoRenderPlan.h"
#include "rendering/SoRenderBackend.h"
#include "elements/SoPBRMaterialElement.h"
#include <iostream>
#include <limits>
#include <cstring>

namespace {
int failures = 0;
void check(bool value, const char * label) {
  if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
void ignoreAll(SoMaterial * m) {
  m->ambientColor.setIgnored(TRUE); m->diffuseColor.setIgnored(TRUE);
  m->specularColor.setIgnored(TRUE); m->emissiveColor.setIgnored(TRUE);
  m->shininess.setIgnored(TRUE); m->transparency.setIgnored(TRUE);
}
SoCallbackAction::Response inspect(void * data, SoCallbackAction * action, const SoNode *) {
  const auto * state = SoPBRMaterialElement::get(action->getState());
  *static_cast<bool *>(data) = state && state->model == SoRenderMaterialModel::PBR &&
    state->material.metallic == 0.75f;
  return SoCallbackAction::CONTINUE;
}
}
int main() {
  SoDB::init();
  const SoRenderParams defaults;
  check(defaults.viewMatrix==SbMatrix::identity()&&defaults.projMatrix==SbMatrix::identity(),"native default matrices are identities");
  for(int c=0;c<4;++c)check(defaults.clearColor[c]==0,"native default clear color is initialized");
  const SbViewportRegion viewport(32,32);
  for (bool compact : {false, true}) {
    auto * root = new SoSeparator; root->ref();
    auto * pbr = new SoPBRMaterial; pbr->metallic = 0.75f; pbr->roughness = 0.25f;
    pbr->emissive.setValue(3,2,1); root->addChild(pbr);
    auto * cube = new SoCube; root->addChild(cube); root->addChild(cube);
    auto * nested = new SoSeparator; auto * legacy = new SoMaterial;
    nested->addChild(legacy); nested->addChild(cube); root->addChild(nested); root->addChild(cube);
    auto * ignored = new SoMaterial; ignoreAll(ignored); root->addChild(ignored); root->addChild(cube);
    root->addChild(new SoMaterial); root->addChild(cube);
    SoIRRenderAction action(viewport); action.getMutableDrawList().setCompactCommandStorage(compact);
    action.apply(root);
    check(!action.hasUnsupportedRendering(), "native PBR factor capture accepted");
    const auto & list = action.getDrawList();
    check(list.getNumCommands() == 6, "one command per cube occurrence");
    auto first = list.getCommandSnapshot(0);
    check(first.material.shadingModel == SO_SHADING_PBR_METALLIC_ROUGHNESS && first.material.pbr,
          "PBR model explicit in IR");
    check(first.material.pbr && first.material.pbr->emissive[0] == 3 &&
          first.material.pbr->metallic == 0.75f && first.material.pbr->roughness == 0.25f,
          "authored factors and HDR emission preserved");
    check(first.material.pbr == list.getCommandSnapshot(1).material.pbr,
          "unchanged effective material shares owned descriptor");
    check(!list.getCommandSnapshot(2).material.pbr && list.getCommandSnapshot(3).material.pbr &&
          list.getCommandSnapshot(4).material.pbr && !list.getCommandSnapshot(5).material.pbr,
          "legacy switch, separator restoration and ignored legacy node");
    SoRenderPlan plan; SoRenderPlanner().build(list, plan);
    check(plan.getNumOperations() >= list.getNumCommands(), "native planner consumes captured PBR");
    auto second = first;
    auto factors = *first.material.pbr; factors.roughness = 0.5f;
    second.material.pbr = std::make_shared<const SoPBRMaterialData>(factors);
    check(!SoRenderCommandTraits::sameMaterialUniformState(first.material,second.material),
          "planner comparison distinguishes PBR factors");
    SoDrawList keys; keys.setCompactCommandStorage(compact); keys.addCommand(first); keys.addCommand(second);
    check(keys.getCommandSnapshot(0).material.pbr->roughness == 0.25f &&
          keys.getCommandSnapshot(1).material.pbr->roughness == 0.5f,
          "compact interning retains different PBR descriptors");
    auto same = first; same.material.pbr = std::make_shared<const SoPBRMaterialData>(*first.material.pbr);
    check(SoRenderCommandTraits::sameMaterialUniformState(first.material,same.material),
          "material equality uses values rather than descriptor addresses");
    pbr->roughness = 0.625f; action.apply(root);
    check(action.getDrawList().getCommandSnapshot(0).material.pbr->roughness == 0.625f &&
          first.material.pbr->roughness == 0.25f, "recapture publishes new immutable factors");
    pbr->roughness = std::numeric_limits<float>::quiet_NaN(); action.apply(root);
    check(action.hasUnsupportedRendering(), "nonfinite factors rejected in sole capture");
    pbr->roughness = 0.25f; pbr->setOverride(TRUE); action.apply(root);
    check(action.getDrawList().getCommandSnapshot(2).material.pbr != nullptr,
          "PBR override blocks following legacy material");
    pbr->setOverride(FALSE);
    bool replayed = false; SoCallbackAction callback;
    callback.addPreCallback(SoCube::getClassTypeId(), inspect, &replayed);
    // Last cube follows legacy state; use a dedicated PBR root for replay.
    auto * replayRoot = new SoSeparator; replayRoot->ref(); replayRoot->addChild(pbr); replayRoot->addChild(cube);
    callback.apply(replayRoot); check(replayed, "targeted callback replay sees PBR state"); replayRoot->unref();
    root->unref();
  }
  // Inheritance and admission are scene semantics, independent of shader availability.
  auto * inheritedRoot = new SoSeparator; inheritedRoot->ref();
  auto * source = new SoPBRMaterial; source->metallic = 0.75f;
  source->emissive.setValue(3,2,1); inheritedRoot->addChild(source);
  auto * partial = new SoPBRMaterial; partial->baseColor.setIgnored(TRUE);
  partial->metallic.setIgnored(TRUE); partial->emissive.setIgnored(TRUE);
  partial->roughness = 0.875f; inheritedRoot->addChild(partial);
  inheritedRoot->addChild(new SoCube);
  SoIRRenderAction inheritedCapture(viewport); inheritedCapture.apply(inheritedRoot);
  auto inheritedMaterial = inheritedCapture.getDrawList().getCommandSnapshot(0).material.pbr;
  check(inheritedMaterial && inheritedMaterial->metallic == 0.75f &&
        inheritedMaterial->roughness == 0.875f && inheritedMaterial->emissive[0] == 3,
        "ignored PBR factors inherit effective authored values");
  SoFieldList fields; partial->getFields(fields);
  for (int i=0; i<fields.getLength(); ++i) fields[i]->setIgnored(TRUE);
  inheritedCapture.apply(inheritedRoot);
  check(inheritedCapture.getDrawList().getCommandSnapshot(0).material.pbr->roughness == 0.4f,
        "fully ignored PBR node preserves previous model and factors");
  auto * binding = new SoMaterialBinding; binding->value = SoMaterialBinding::PER_FACE;
  inheritedRoot->insertChild(binding,2); inheritedCapture.apply(inheritedRoot);
  check(inheritedCapture.hasUnsupportedRendering(), "unsupported PBR material binding rejected");
  inheritedRoot->unref();
  auto * overrideRoot = new SoSeparator; overrideRoot->ref();
  auto * legacyOverride = new SoMaterial; legacyOverride->setOverride(TRUE);
  overrideRoot->addChild(legacyOverride); overrideRoot->addChild(new SoPBRMaterial);
  overrideRoot->addChild(new SoCube);
  SoIRRenderAction overrideCapture(viewport); overrideCapture.apply(overrideRoot);
  check(!overrideCapture.getDrawList().getCommandSnapshot(0).material.pbr,
        "legacy diffuse/transparency override prevents PBR activation");
  overrideRoot->unref();
  SoInput input; const char scene[] = "#Inventor V2.1 ascii\nSeparator { SoPBRMaterial { metallic 0.75 roughness 0.25 } Cube {} }";
  input.setBuffer(scene, std::strlen(scene)); auto * parsed = SoDB::readAll(&input);
  check(parsed != nullptr, "source branch node name and fields deserialize");
  if (parsed) { parsed->ref(); SoIRRenderAction action(viewport); action.apply(parsed);
    check(action.getDrawList().getNumCommands() == 1 &&
          action.getDrawList().getCommandSnapshot(0).material.pbr->metallic == 0.75f,
          "serialized material captured without executor"); parsed->unref(); }
  std::cout << "Native retained PBR: " << (failures ? "FAIL" : "PASS") << '\n';
  return failures ? 1 : 0;
}
