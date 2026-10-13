#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoOutput.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/actions/SoWriteAction.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/SoPBRMaterial.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <vector> // Existing target readback contract.

#define REQUIRE(condition, text) do { if (!(condition)) { std::fprintf(stderr, "%s (line %d)\n", text, __LINE__); return 1; } } while (0)
static int count(const SbString & log, const char * needle) {
  int result = 0; const char * p = log.getString();
  while ((p = std::strstr(p, needle))) { ++result; p += std::strlen(needle); }
  return result;
}
static SoSeparator * scene(SoNode * material) {
  SoSeparator * root = new SoSeparator; root->ref();
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 4); camera->height = 3;
  camera->nearDistance = 0.1f; camera->farDistance = 10;
  root->addChild(camera); root->addChild(material); root->addChild(new SoCube);
  return root;
}
static void ignoreLegacyFields(SoMaterial * material) {
  material->ambientColor.setIgnored(TRUE); material->diffuseColor.setIgnored(TRUE);
  material->specularColor.setIgnored(TRUE); material->emissiveColor.setIgnored(TRUE);
  material->shininess.setIgnored(TRUE); material->transparency.setIgnored(TRUE);
}

int main(int argc, char ** argv) {
  SoDB::init(); CoinRenderAction::initClass();
  SoPBRMaterial::initClass(); // Public initialization is idempotent.
  CoinRenderAction action;
  SoPBRMaterial * material = new SoPBRMaterial;
  REQUIRE(material->baseColor.getValue() == SbColor(0.8f, 0.8f, 0.8f) &&
          material->metallic.getValue() == 0 && material->roughness.getValue() == 0.4f,
          "physical defaults");
  material->baseColor.setValue(0.25f, 0.5f, 0.75f);
  material->metallic = 1; material->roughness = 0.25f;
  SoSeparator * root = scene(material);
  action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::SUCCESS, "PBR capture succeeds without a target");
  REQUIRE(count(action.getRecordingLog(), "model=PBR") == 1, "physical tag survives capture");
  const SbString before = action.getRecordingLog();
  material->roughness = 0.75f; action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::SUCCESS && before != action.getRecordingLog(),
          "roughness mutation invalidates captured payload");
  material->emissive.setValue(2, 4, 8); action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::SUCCESS, "HDR emission is not clamped/rejected at one");
  material->roughness = std::numeric_limits<float>::quiet_NaN(); action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::INVALID_SCENE, "NaN material rejected before execution");
  material->roughness = 1.1f; action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::INVALID_SCENE, "out-of-range roughness rejected");
  material->roughness = 0.25f; material->baseColor.setValue(-0.1f, 0.5f, 0.75f); action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::INVALID_SCENE, "negative base color rejected");
  material->baseColor.setValue(0.25f, 0.5f, 0.75f); material->emissive.setValue(0, 0, 0);
  material->metallic = -0.1f; action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::INVALID_SCENE, "negative metallic rejected");
  material->metallic = 1; material->emissive.setValue(-1, 0, 0); action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::INVALID_SCENE, "negative emission rejected");
  material->emissive.setValue(0, 0, 0);
  SoMaterialBinding * binding = new SoMaterialBinding;
  binding->value = SoMaterialBinding::PER_FACE; root->insertChild(binding, 1); action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::UNSUPPORTED, "unimplemented material binding rejected");
  root->removeChild(binding);
  SoMaterial * invisible = new SoMaterial; invisible->transparency = 1;
  root->insertChild(invisible, 1); action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::SUCCESS && count(action.getRecordingLog(), "model=PBR") == 1 &&
          std::strstr(action.getRecordingLog().getString(), "vertices count: 0") == NULL,
          "opaque PBR clears inherited material invisibility");
  root->removeChild(invisible);

  SoSeparator * inner = new SoSeparator;
  SoPBRMaterial * innerMaterial = new SoPBRMaterial; innerMaterial->roughness = 0.9f;
  innerMaterial->baseColor.setIgnored(TRUE); innerMaterial->metallic.setIgnored(TRUE); innerMaterial->emissive.setIgnored(TRUE);
  inner->addChild(innerMaterial); inner->addChild(new SoCube);
  root->addChild(inner); root->addChild(new SoCube); action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::SUCCESS && count(action.getRecordingLog(), "model=PBR") == 2,
          "separator restores outer PBR and ignored fields inherit physical state");
  root->removeChild(inner); root->removeChild(root->getNumChildren() - 1);
  SoMaterial * legacy = new SoMaterial;
  legacy->diffuseColor.setValue(1, 0, 0); root->addChild(legacy); root->addChild(new SoCube);
  action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::SUCCESS && count(action.getRecordingLog(), "model=PBR") == 1 &&
          std::strstr(action.getRecordingLog().getString(), "diff=[1.0000,0.0000,0.0000,1.0000]") != NULL,
          "effective SoMaterial selects legacy");
  material->setOverride(TRUE); action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::SUCCESS && count(action.getRecordingLog(), "model=PBR") == 1 &&
          std::strstr(action.getRecordingLog().getString(), "diff=[1.0000,0.0000,0.0000,1.0000]") == NULL,
          "PBR override protects material model and factors");
  material->setOverride(FALSE); ignoreLegacyFields(legacy); action.apply(root);
  REQUIRE(action.getLastStatus() == CoinRenderAction::SUCCESS && count(action.getRecordingLog(), "model=PBR") == 1 &&
          std::strstr(action.getRecordingLog().getString(), "diff=[1.0000,0.0000,0.0000,1.0000]") == NULL,
          "ignored SoMaterial does not change the active model");
  root->removeChild(root->getNumChildren() - 1); root->removeChild(legacy);

  material->emissive.setValue(2, 4, 8);
  char buffer[32768]; SoOutput output; output.setBuffer(buffer, sizeof(buffer), NULL); output.setFloatPrecision(9);
  SoWriteAction writer(&output); writer.apply(root);
  void * bytes = NULL; size_t length = 0;
  REQUIRE(output.getBuffer(bytes, length) && length > 0, "Coin writer serializes physical node");
  SoInput input; input.setBuffer(bytes, length);
  SoSeparator * decoded = SoDB::readAll(&input); REQUIRE(decoded, "Coin reader decodes physical node"); decoded->ref();
  SoSearchAction search; search.setType(SoPBRMaterial::getClassTypeId()); search.apply(decoded);
  REQUIRE(search.getPath(), "registered PBR type survives roundtrip");
  const auto * decodedMaterial = static_cast<const SoPBRMaterial *>(search.getPath()->getTail());
  REQUIRE(decodedMaterial->baseColor.getValue() == material->baseColor.getValue() &&
          decodedMaterial->roughness.getValue() == material->roughness.getValue() &&
          decodedMaterial->emissive.getValue() == SbColor(2, 4, 8), "physical factors and HDR emission roundtrip");
  action.apply(decoded); REQUIRE(action.getLastStatus() == CoinRenderAction::SUCCESS &&
      count(action.getRecordingLog(), "model=PBR") == 1, "roundtrip is captured as physical material");
  decoded->unref();

  SoMaterial * overridingLegacy = new SoMaterial; overridingLegacy->setOverride(TRUE);
  SoSeparator * legacyRoot = scene(overridingLegacy);
  legacyRoot->addChild(new SoPBRMaterial); legacyRoot->addChild(new SoCube);
  action.apply(legacyRoot);
  REQUIRE(action.getLastStatus() == CoinRenderAction::SUCCESS && count(action.getRecordingLog(), "model=PBR") == 0,
          "legacy override blocks physical material activation");
  overridingLegacy->setOverride(FALSE); action.apply(legacyRoot);
  REQUIRE(action.getLastStatus() == CoinRenderAction::SUCCESS && count(action.getRecordingLog(), "model=PBR") == 1,
          "quiet legacy override mutation invalidates material reuse");
  legacyRoot->unref();

  SoSeparator * callbackRoot = scene(new SoPBRMaterial);
  SoMaterial * callbackLegacy = new SoMaterial;
  callbackLegacy->diffuseColor.setValue(1, 0, 0);
  callbackRoot->addChild(callbackLegacy); callbackRoot->addChild(new SoCube);
  CoinRenderAction callbackAction;
  callbackAction.addPreCallback(SoMaterial::getClassTypeId(),
    [](void *, SoCallbackAction *, const SoNode * node) {
      ignoreLegacyFields(static_cast<SoMaterial *>(const_cast<SoNode *>(node)));
      return SoCallbackAction::CONTINUE;
    }, NULL);
  callbackAction.apply(callbackRoot);
  REQUIRE(callbackAction.getLastStatus() == CoinRenderAction::SUCCESS &&
      count(callbackAction.getRecordingLog(), "model=PBR") == 1 &&
      std::strstr(callbackAction.getRecordingLog().getString(), "diff=[1.0000,0.0000,0.0000,1.0000]") == NULL,
      "model selection observes user pre callbacks before applying legacy fields");
  callbackRoot->unref();

  SoSeparator * pruneRoot = scene(new SoPBRMaterial);
  CoinRenderAction pruneAction;
  pruneAction.addPreCallback(SoPBRMaterial::getClassTypeId(),
    [](void *, SoCallbackAction *, const SoNode *) { return SoCallbackAction::PRUNE; }, NULL);
  pruneAction.apply(pruneRoot);
  REQUIRE(pruneAction.getLastStatus() == CoinRenderAction::SUCCESS &&
      count(pruneAction.getRecordingLog(), "model=PBR") == 0,
      "pruned physical material does not alter state");
  pruneRoot->unref();

  CoinRenderFramePlan a, b;
  a.materials.emplace_back(); a.materials[0].model = CoinRenderMaterialModel::PBR;
  b = a; REQUIRE(a.hasSamePayload(b), "identical PBR payload reusable");
  b.materials[0].pbr.roughness = 0.7f; REQUIRE(!a.hasSamePayload(b), "PBR factor participates in payload equality");

  if (argc > 1 && std::strcmp(argv[1], "--target") == 0) {
    CoinRenderCapabilities caps{};
    REQUIRE(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) == 0,
            "target capabilities available");
    CoinRenderSceneManager manager(SbVec2i32(64, 64));
    SoMaterial * controlMaterial = new SoMaterial; controlMaterial->diffuseColor.setValue(1, 0, 0);
    SoSeparator * control = scene(controlMaterial); manager.setSceneGraph(control); control->unref();
    REQUIRE(manager.render() == CoinRenderAction::SUCCESS, "legacy control frame renders");
    CoinRenderTarget * target = manager.getRenderTarget();
    const uint64_t serial = target->getLastSubmissionSerial();
    std::vector<uint8_t> prior; target->readbackRGBA(prior);
    manager.setSceneGraph(root);
    const auto result = manager.render();
    if (!(caps.features & COIN_RENDER_FEATURE_PBR_OPAQUE)) {
      REQUIRE(result == CoinRenderAction::UNSUPPORTED && target->getLastSubmissionSerial() == serial,
              "unsupported PBR is never silently executed as Phong");
      std::vector<uint8_t> after; target->readbackRGBA(after);
      REQUIRE(after == prior, "failed candidate preserves the previous publication");
    } else REQUIRE(result == CoinRenderAction::SUCCESS, "advertised PBR profile executes");
  }
  root->unref();
  std::printf("SoPBRMaterial: defaults, capture, mutation, validation, binding, opacity, scoping, override and serialization PASS\n");
  return 0;
}
