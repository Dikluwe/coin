#include <exception>
#include "rendering/coinrender/CoinRenderMultipassCore.h"
#include "rendering/coinrender/CoinRenderBumpMapCore.h"
#include "rendering/coinrender/CoinRenderIndexedGeometryCore.h"
#include "rendering/coinrender/CoinRenderSamplingCore.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoSubAction.h>
#include <Inventor/nodes/SoShape.h>
#ifdef HAVE_VRML97
#include <Inventor/VRMLnodes/SoVRMLShape.h>
#include <Inventor/VRMLnodes/SoVRMLAppearance.h>
#include <Inventor/VRMLnodes/SoVRMLMaterial.h>
#include <Inventor/VRMLnodes/SoVRMLLight.h>
#include <Inventor/VRMLnodes/SoVRMLFog.h>
#include <Inventor/VRMLnodes/SoVRMLBackground.h>
#include <Inventor/VRMLnodes/SoVRMLPixelTexture.h>
#include <Inventor/VRMLnodes/SoVRMLImageTexture.h>
#include <Inventor/VRMLnodes/SoVRMLBox.h>
#include <Inventor/VRMLnodes/SoVRMLCone.h>
#include <Inventor/VRMLnodes/SoVRMLCylinder.h>
#include <Inventor/VRMLnodes/SoVRMLSphere.h>
#include <Inventor/VRMLnodes/SoVRMLIndexedFaceSet.h>
#include <Inventor/VRMLnodes/SoVRMLIndexedLineSet.h>
#include <Inventor/VRMLnodes/SoVRMLPointSet.h>
#include <Inventor/VRMLnodes/SoVRMLElevationGrid.h>
#include <Inventor/VRMLnodes/SoVRMLExtrusion.h>
#include <Inventor/VRMLnodes/SoVRMLCoordinate.h>
#include <Inventor/VRMLnodes/SoVRMLNormal.h>
#include <Inventor/VRMLnodes/SoVRMLColor.h>
#include <Inventor/VRMLnodes/SoVRMLTextureCoordinate.h>
#include <Inventor/VRMLnodes/SoVRMLText.h>
#include <Inventor/VRMLnodes/SoVRMLFontStyle.h>
#endif
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoTextureCoordinateFunction.h>
#include <Inventor/fields/SoSFNode.h>
#include <Inventor/fields/SoMFNode.h>
#include <Inventor/fields/SoSFImage.h>
#include <Inventor/fields/SoSFImage3.h>
#include <Inventor/fields/SoSFString.h>
#include <Inventor/fields/SoMFString.h>
#include <Inventor/fields/SoSFPath.h>
#include <Inventor/fields/SoMFPath.h>
#include <Inventor/fields/SoSFEngine.h>
#include <Inventor/fields/SoMFEngine.h>
#include <Inventor/fields/SoMFBitMask.h>
#include <Inventor/fields/SoMFBool.h>
#include <Inventor/fields/SoMFColor.h>
#include <Inventor/fields/SoMFColorRGBA.h>
#include <Inventor/fields/SoMFDouble.h>
#include <Inventor/fields/SoMFEnum.h>
#include <Inventor/fields/SoMFFloat.h>
#include <Inventor/fields/SoMFInt32.h>
#include <Inventor/fields/SoMFMatrix.h>
#include <Inventor/fields/SoMFName.h>
#include <Inventor/fields/SoMFPlane.h>
#include <Inventor/fields/SoMFRotation.h>
#include <Inventor/fields/SoMFShort.h>
#include <Inventor/fields/SoMFTime.h>
#include <Inventor/fields/SoMFUInt32.h>
#include <Inventor/fields/SoMFUShort.h>
#include <Inventor/fields/SoMFVec2b.h>
#include <Inventor/fields/SoMFVec2d.h>
#include <Inventor/fields/SoMFVec2f.h>
#include <Inventor/fields/SoMFVec2i32.h>
#include <Inventor/fields/SoMFVec2s.h>
#include <Inventor/fields/SoMFVec3b.h>
#include <Inventor/fields/SoMFVec3d.h>
#include <Inventor/fields/SoMFVec3f.h>
#include <Inventor/fields/SoMFVec3i32.h>
#include <Inventor/fields/SoMFVec3s.h>
#include <Inventor/fields/SoMFVec4b.h>
#include <Inventor/fields/SoMFVec4d.h>
#include <Inventor/fields/SoMFVec4f.h>
#include <Inventor/fields/SoMFVec4i32.h>
#include <Inventor/fields/SoMFVec4s.h>
#include <Inventor/fields/SoMFVec4ub.h>
#include <Inventor/fields/SoMFVec4ui32.h>
#include <Inventor/fields/SoMFVec4us.h>
#include <Inventor/SbImage.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedShape.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/nodes/SoTexture2.h>
#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
#include <Inventor/nodes/SoSceneTextureCubeMap.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/elements/SoShadowStyleElement.h>
#include <Inventor/elements/SoEnvironmentElement.h>
#include <Inventor/elements/SoBumpMapElement.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoIndexedPointSet.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoVertexShape.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/elements/SoTextureCombineElement.h>
#include <Inventor/nodes/SoVertexProperty.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/fields/SoField.h>
#include <Inventor/fields/SoFieldData.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoCube.h>
#include <typeinfo>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoIndexedMarkerSet.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/SoPath.h>
#include <Inventor/bundles/SoTextureCoordinateBundle.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/nodes/SoTextureCoordinateObject.h>
#include <Inventor/nodes/SoTextureCoordinatePlane.h>
#include <Inventor/nodes/SoTextureCoordinateEnvironment.h>
#include <Inventor/nodes/SoTextureCoordinateReflectionMap.h>
#include <Inventor/nodes/SoTextureCoordinateNormalMap.h>
#include <Inventor/elements/SoNormalElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoNormalBindingElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/elements/SoTextureOverrideElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoClipPlaneElement.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoLightModelElement.h>
#include <Inventor/elements/SoComplexityTypeElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoLinePatternElement.h>
#include <Inventor/elements/SoLineWidthElement.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include "rendering/coinrender/CoinRenderDepthPolicyElement.h"
#include "rendering/coinrender/CoinRenderShaderModuleElement.h"
#include <algorithm>
#include <utility>
#include <chrono>
#include <memory>
#include <new>
#include <unordered_set>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <cmath>
#include <Inventor/misc/SoState.h>
#include <Inventor/misc/SoChildList.h>

#include "actions/CoinRenderActionP.h"
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"
#include "rendering/coinrender/CoinRenderShadowCameraMath.h"
#include "rendering/coinrender/CoinRenderRttExecution.h"
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderNativeSurfaceCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderManagerLayerPurpose.h"
#include "rendering/coinrender/CoinRenderGuardedTraversal.h"
#include "rendering/coinrender/CoinRenderSelectionCore.h"
#include "rendering/coinrender/CoinRenderAlphaTestCapture.h"
#include "rendering/coinrender/CoinRenderPhaseTimer.h"
#include "actions/SoSubActionP.h"

static bool rttNodeHasKnownTraversal(const SoNode * node)
{
  // This SDK's concrete SceneTextureCubeMap uses SO_NODE_CONSTRUCTOR, unlike
  // its other internal texture nodes. Qualify that exact implementation only;
  // application subclasses retain the dynamic/custom traversal contract.
  return node->getIsBuiltIn() || node->getTypeId()==SoSceneTextureCubeMap::getClassTypeId();
}

// This proof is local to one outer apply. It owns field values (including
// copied image bytes), not revisions/digests as a substitute for the payload.
// isSame supplies field-value equality, not IEEE representation equality. User
// code capable of editing values during this apply invalidates proofs before
// it runs; no proof survives into a later apply (including notify-disabled edits).
// Dynamic traversal is deliberately not memoized: its callbacks still execute
// at every logical occurrence, just as before graph payload deduplication.
class CoinRenderRttCaptureMemo {
public:
  struct Identity {
    const SoNode * node;
    uint64_t revision;
    bool overrideValue;
    int stereoMode = 0;
    float stereoAdjustment = 0, stereoBalance = 0;
    bool matches() const {
      if (node->getNodeId()!=revision || bool(node->isOverride())!=overrideValue) return false;
      if (node->isOfType(SoCamera::getClassTypeId())) {
        const auto * camera=static_cast<const SoCamera *>(node);
        if (camera->getStereoMode()!=stereoMode || camera->getStereoAdjustment()!=stereoAdjustment ||
            camera->getBalanceAdjustment()!=stereoBalance) return false;
      }
      return true;
    }
  };
  struct Baseline {
    bool complete = true;
    std::vector<Identity> nodes;
    bool matches() const {
      if (!complete) return false;
      for (const auto & identity : nodes) if (!identity.matches()) return false;
      return true;
    }
  };
  struct Field {
    const SoField * original;
    std::shared_ptr<SoField> value;
    bool ignored, defaultValue;
  };
  struct Node {
    std::shared_ptr<const SoNode> original;
    uint64_t revision;
    bool overrideValue;
    std::vector<Field> fields;
    std::vector<const SoNode *> children;
    int stereoMode = 0;
    float stereoAdjustment = 0, stereoBalance = 0;
  };
  struct Scene {
    std::vector<Node> nodes;
    bool stable = true;
    // The proof is only an optimization. Bound its per-apply copied payload;
    // exceeding this allowance preserves full traversal, never rejects a scene.
    static bool charge(size_t amount, size_t & used) {
      const size_t allowance = size_t(1024)*1024;
      if (amount > allowance-used) return false;
      used += amount; return true;
    }
    static bool chargeProduct(uint64_t a, uint64_t b, size_t & used) {
      const size_t allowance = size_t(1024)*1024;
      if (a && b > (allowance-used)/a) return false;
      return charge(size_t(a*b), used);
    }
    static size_t multiFieldElementSize(const SoField * field) {
      // Public typed accessors expose the real contiguous element type. Do not
      // reach through SoMField's protected storage API for this optional proof.
#define COIN_RTT_MFIELD_SIZE(Type) \
      if (field->getTypeId()==Type::getClassTypeId()) \
        return sizeof(*static_cast<const Type *>(field)->getValues(0))
      COIN_RTT_MFIELD_SIZE(SoMFBitMask);
      COIN_RTT_MFIELD_SIZE(SoMFBool);
      COIN_RTT_MFIELD_SIZE(SoMFColor);
      COIN_RTT_MFIELD_SIZE(SoMFColorRGBA);
      COIN_RTT_MFIELD_SIZE(SoMFDouble);
      COIN_RTT_MFIELD_SIZE(SoMFEnum);
      COIN_RTT_MFIELD_SIZE(SoMFFloat);
      COIN_RTT_MFIELD_SIZE(SoMFInt32);
      COIN_RTT_MFIELD_SIZE(SoMFMatrix);
      COIN_RTT_MFIELD_SIZE(SoMFName);
      COIN_RTT_MFIELD_SIZE(SoMFNode);
      COIN_RTT_MFIELD_SIZE(SoMFPlane);
      COIN_RTT_MFIELD_SIZE(SoMFRotation);
      COIN_RTT_MFIELD_SIZE(SoMFShort);
      COIN_RTT_MFIELD_SIZE(SoMFString);
      COIN_RTT_MFIELD_SIZE(SoMFTime);
      COIN_RTT_MFIELD_SIZE(SoMFUInt32);
      COIN_RTT_MFIELD_SIZE(SoMFUShort);
      COIN_RTT_MFIELD_SIZE(SoMFVec2b);
      COIN_RTT_MFIELD_SIZE(SoMFVec2d);
      COIN_RTT_MFIELD_SIZE(SoMFVec2f);
      COIN_RTT_MFIELD_SIZE(SoMFVec2i32);
      COIN_RTT_MFIELD_SIZE(SoMFVec2s);
      COIN_RTT_MFIELD_SIZE(SoMFVec3b);
      COIN_RTT_MFIELD_SIZE(SoMFVec3d);
      COIN_RTT_MFIELD_SIZE(SoMFVec3f);
      COIN_RTT_MFIELD_SIZE(SoMFVec3i32);
      COIN_RTT_MFIELD_SIZE(SoMFVec3s);
      COIN_RTT_MFIELD_SIZE(SoMFVec4b);
      COIN_RTT_MFIELD_SIZE(SoMFVec4d);
      COIN_RTT_MFIELD_SIZE(SoMFVec4f);
      COIN_RTT_MFIELD_SIZE(SoMFVec4i32);
      COIN_RTT_MFIELD_SIZE(SoMFVec4s);
      COIN_RTT_MFIELD_SIZE(SoMFVec4ub);
      COIN_RTT_MFIELD_SIZE(SoMFVec4ui32);
      COIN_RTT_MFIELD_SIZE(SoMFVec4us);
#undef COIN_RTT_MFIELD_SIZE
      return 0; // Unknown/custom fields retain ordinary capture.
    }
    bool capture(const SoNode * root, size_t & used) {
      std::vector<const SoNode *> pending(1, root);
      std::unordered_set<const SoNode *> seen;
      while (!pending.empty()) {
        const SoNode * node = pending.back(); pending.pop_back();
        if (!node || !seen.insert(node).second) continue;
        if (!rttNodeHasKnownTraversal(node) || node->isOfType(SoCallback::getClassTypeId()) ||
            node->isOfType(SoTextureCoordinateFunction::getClassTypeId())) return stable = false;
        if (!charge(sizeof(Node)+256, used)) return stable = false;
        Node saved;
        node->ref();
        saved.original = std::shared_ptr<const SoNode>(node, [](const SoNode * n) { n->unref(); });
        saved.revision = node->getNodeId(); saved.overrideValue = node->isOverride();
        const SoFieldData * data = node->getFieldData();
        for (int i = 0; data && i < data->getNumFields(); ++i) {
          const SoField * field = data->getField(node, i);
          // Do not evaluate engines or scheduled image callbacks for a cache proof.
          if (field->isConnected() || !field->getTypeId().canCreateInstance() ||
              field->isOfType(SoSFPath::getClassTypeId()) || field->isOfType(SoMFPath::getClassTypeId()) ||
              field->isOfType(SoSFEngine::getClassTypeId()) || field->isOfType(SoMFEngine::getClassTypeId()) ||
              !charge(sizeof(Field)+256, used)) return stable = false;
          if (field->getTypeId() == SoSFImage::getClassTypeId()) {
            const SbImage & image = static_cast<const SoSFImage *>(field)->getValue();
            // SbImage equality is false for scheduled reads, even against itself.
            if (!image.hasData() || !(image == image)) return stable = false;
            SbVec2s size; int components;
            static_cast<const SoSFImage *>(field)->getValue(size, components);
            if (size[0]<0 || size[1]<0 || components<0 ||
                !chargeProduct(size_t(size[0])*size_t(size[1]), size_t(components), used)) return stable = false;
          } else if (field->getTypeId() == SoSFImage3::getClassTypeId()) {
            SbVec3s size; int components;
            static_cast<const SoSFImage3 *>(field)->getValue(size, components);
            if (size[0]<0 || size[1]<0 || size[2]<0 || components<0 ||
                !chargeProduct(uint64_t(size[0])*uint64_t(size[1])*uint64_t(size[2]), uint64_t(components), used)) return stable = false;
          } else if (field->isOfType(SoMField::getClassTypeId())) {
            const auto * values = static_cast<const SoMField *>(field);
            const size_t elementSize=multiFieldElementSize(field);
            if (values->getNum()<0 || !elementSize ||
                !chargeProduct(size_t(values->getNum()), elementSize, used)) return stable = false;
            if (field->getTypeId() == SoMFString::getClassTypeId()) {
              const auto * strings = static_cast<const SoMFString *>(field);
              for (int j=0;j<strings->getNum();++j)
                if (!charge(size_t((*strings)[j].getLength())+1, used)) return stable = false;
            }
          } else if (field->getTypeId() == SoSFString::getClassTypeId()) {
            if (!charge(size_t(static_cast<const SoSFString *>(field)->getValue().getLength())+1, used)) return stable = false;
          }
          Field copy;
          copy.original = field; copy.ignored = field->isIgnored(); copy.defaultValue = field->isDefault();
          copy.value.reset(static_cast<SoField *>(field->getTypeId().createInstance()));
          if (!copy.value) return stable = false;
          copy.value->copyFrom(*field);
          saved.fields.push_back(std::move(copy));
          if (field->isOfType(SoSFNode::getClassTypeId()))
            pending.push_back(static_cast<const SoSFNode *>(field)->getValue());
          else if (field->isOfType(SoMFNode::getClassTypeId())) {
            const auto * refs = static_cast<const SoMFNode *>(field);
            for (int j = 0; j < refs->getNum(); ++j) pending.push_back((*refs)[j]);
          }
        }
        const SoChildList * children = node->getChildren();
        for (int i = 0; children && i < children->getLength(); ++i) {
          if (!charge(3*sizeof(const SoNode *), used)) return stable = false;
          saved.children.push_back((*children)[i]); pending.push_back((*children)[i]);
        }
        if (node->isOfType(SoCamera::getClassTypeId())) {
          const auto * camera = static_cast<const SoCamera *>(node);
          saved.stereoMode = camera->getStereoMode();
          saved.stereoAdjustment = camera->getStereoAdjustment();
          saved.stereoBalance = camera->getBalanceAdjustment();
        }
        nodes.push_back(std::move(saved));
      }
      return true;
    }
    bool matches() const {
      if (!stable) return false;
      for (const auto & saved : nodes) {
        const SoNode * node = saved.original.get();
        if (node->getNodeId() != saved.revision || bool(node->isOverride()) != saved.overrideValue) return false;
        const SoFieldData * data = node->getFieldData();
        if (size_t(data ? data->getNumFields() : 0) != saved.fields.size()) return false;
        for (size_t i = 0; i < saved.fields.size(); ++i) {
          const Field & field = saved.fields[i];
          if (data->getField(node, int(i)) != field.original || field.original->isConnected() ||
              bool(field.original->isIgnored()) != field.ignored ||
              bool(field.original->isDefault()) != field.defaultValue ||
              !field.value->isSame(*field.original)) return false;
        }
        const SoChildList * children = node->getChildren();
        if (size_t(children ? children->getLength() : 0) != saved.children.size()) return false;
        for (size_t i = 0; i < saved.children.size(); ++i)
          if ((*children)[int(i)] != saved.children[i]) return false;
        if (node->isOfType(SoCamera::getClassTypeId())) {
          const auto * camera = static_cast<const SoCamera *>(node);
          if (camera->getStereoMode() != saved.stereoMode ||
              camera->getStereoAdjustment() != saved.stereoAdjustment ||
              camera->getBalanceAdjustment() != saved.stereoBalance) return false;
        }
      }
      return true;
    }
  };
  struct Visit {
    uint64_t source;
    size_t relativeDepth;
  };
  struct Trace {
    size_t baseDepth;
    bool complete = true;
    size_t reservedBytes = 0;
    std::vector<Visit> visits;
  };
  struct Entry {
    const SoNode * source;
    std::vector<uint64_t> context;
    Scene scene;
    std::vector<Visit> visits;
    uint64_t epoch, producer;
    size_t stagedCharge = 0;
    size_t reservedBytes = 0;
  };
  bool allowed = true;
  const CoinRenderActionP * owner = NULL;
  uint64_t epoch = 0;
  size_t proofBytes = 0;
  std::vector<Entry> entries;
  std::vector<Trace *> activeTraces;

  void invalidate() {
    ++epoch;
    // Release field auditors and strong node references before the effect,
    // without disturbing the numeric traces owned by active capture scopes.
    for (const auto & entry : entries) proofBytes -= entry.reservedBytes;
    entries.clear();
  }

  void recordVisit(uint64_t source, size_t depth) {
    for (auto * trace : activeTraces) {
      if (!trace->complete) continue;
      bool found = false;
      for (auto & visit : trace->visits) if (visit.source == source) {
        visit.relativeDepth = std::max(visit.relativeDepth, depth-trace->baseDepth);
        found = true; break;
      }
      if (!found) {
        if (!Scene::charge(sizeof(Visit)+32, proofBytes)) { trace->complete = false; continue; }
        trace->reservedBytes += sizeof(Visit)+32;
        Visit visit; visit.source = source; visit.relativeDepth = depth-trace->baseDepth;
        trace->visits.push_back(visit);
      }
    }
  }
  struct TraceScope {
    CoinRenderRttCaptureMemo * memo;
    Trace trace;
    TraceScope(CoinRenderRttCaptureMemo * owner, uint64_t source, size_t depth)
      : memo(owner && owner->allowed ? owner : NULL) {
      trace.baseDepth = depth;
      if (memo) { memo->activeTraces.push_back(&trace); memo->recordVisit(source, depth); }
    }
    ~TraceScope() {
      if (memo) { memo->activeTraces.pop_back(); memo->proofBytes -= trace.reservedBytes; }
    }
  };

  static bool activeSource(const CoinRenderRttPlan & graph, uint64_t source) {
    return std::find(graph.activeSources.begin(), graph.activeSources.end(), source) != graph.activeSources.end();
  }
  static bool checkSubgraph(const CoinRenderRttPlan & graph, uint64_t id,
                            size_t depth, std::string & diagnostic, bool & cycle) {
    std::vector<std::pair<uint64_t, size_t>> pending(1, std::make_pair(id, depth));
    std::vector<size_t> deepest(graph.producers.size(), 0);
    while (!pending.empty()) {
      const auto visit = pending.back(); pending.pop_back();
      if (!visit.first || visit.first > graph.producers.size()) {
        diagnostic = "Invalid memoized RTT dependency"; return false;
      }
      const auto & producer = graph.producers[size_t(visit.first-1)];
      if (activeSource(graph, producer.sourceRevision)) {
        cycle = true; diagnostic = "SoSceneTexture2 dependency cycle"; return false;
      }
      if (visit.second > 8) {
        diagnostic = "SoSceneTexture2 nesting beyond eight passes"; return false;
      }
      if (deepest[size_t(visit.first-1)] >= visit.second) continue;
      deepest[size_t(visit.first-1)] = visit.second;
      for (size_t f = 0; f < producer.faceCount(); ++f)
        for (const auto & texture : producer.face(f).textures) if (texture.producerId) {
          if (texture.producerId >= visit.first) {
            diagnostic = "Invalid memoized RTT dependency order"; return false;
          }
          pending.push_back(std::make_pair(texture.producerId, visit.second+1));
        }
    }
    return true;
  }
  bool find(const SoNode * source, const std::vector<uint64_t> & context,
            CoinRenderRttPlan & graph, uint64_t & id, std::string & diagnostic, bool & cycle) {
    id = 0;
    cycle = activeSource(graph, source->getNodeId());
    if (cycle) { diagnostic = "SoSceneTexture2 dependency cycle"; return false; }
    if (!allowed) return true;
    for (const auto & entry : entries)
      if (entry.source == source && entry.context == context && entry.epoch == epoch && entry.scene.matches()) {
        const size_t depth = graph.activeSources.size()+1;
        bool exceedsDepth = false;
        for (const auto & visit : entry.visits) {
          if (activeSource(graph, visit.source)) {
            cycle = true; diagnostic = "SoSceneTexture2 dependency cycle"; return false;
          }
          exceedsDepth = exceedsDepth || depth+visit.relativeDepth > 8;
        }
        if (exceedsDepth) {
          diagnostic = "SoSceneTexture2 nesting beyond eight passes"; return false;
        }
        if (!checkSubgraph(graph, entry.producer, depth, diagnostic, cycle)) return false;
        // STAGED has a conservative 2D charge per logical occurrence. A hit
        // skips traversal, not admission: replay the entire captured subtree.
        if (graph.mode==COIN_RENDER_SCENE_TEXTURE_STAGED) {
          if (graph.chargedBytes>graph.budget() || entry.stagedCharge>graph.budget()-graph.chargedBytes) {
            diagnostic="SoSceneTexture2 staged RGBA8 budget exceeds 64 MiB per apply"; return false;
          }
          graph.chargedBytes += entry.stagedCharge;
        }
        for (const auto & visit : entry.visits) recordVisit(visit.source, depth+visit.relativeDepth);
        id = entry.producer; return true;
      }
    return true;
  }
  void remember(const SoNode * source, const std::vector<uint64_t> & context,
                const Baseline & baseline, const Trace & trace, uint64_t capturedEpoch,
                uint64_t id, size_t stagedCharge, bool captureStable) {
    if (!allowed || !captureStable || !trace.complete || epoch!=capturedEpoch || !baseline.matches()) return;
    // Ancestors keep only lightweight identities while their children capture.
    // Copy field payloads after the children, so an ancestor cannot consume the
    // entire proof allowance before the first leaf is eligible for reuse.
    const size_t reservedBefore = proofBytes;
    Scene scene;
    if (!scene.capture(source, proofBytes) || !scene.matches() ||
        !Scene::chargeProduct(trace.visits.size(), sizeof(Visit), proofBytes)) {
      scene.nodes.clear();
      proofBytes = reservedBefore;
      return;
    }
    Entry entry;
    entry.source = source; entry.context = context; entry.scene = std::move(scene);
    entry.visits = trace.visits;
    entry.epoch = epoch; entry.producer = id; entry.stagedCharge = stagedCharge;
    entry.reservedBytes = proofBytes-reservedBefore;
    entries.push_back(std::move(entry));
  }
};

static std::vector<uint64_t>
rttCaptureContext(const CoinRenderActionP & p, SoState * state,
                  int transparency, bool bumpProducer)
{
  const auto & options = p.executionOptions;
  // These are exactly the parent inputs installed on the private child action.
  return {uint64_t(options.renderer), uint64_t(options.transparency), uint64_t(options.sceneTexture),
    uint64_t(options.storedTextureColorSpace), uint64_t(options.maxTextureAnisotropy),
    uint64_t(options.textureSamplingPolicy), uint64_t(options.rasterizationPolicy), uint64_t(options.sampleCount),
    uint64_t(transparency), uint64_t(SoOverrideElement::getTransparencyTypeOverride(state)),
    uint64_t(p.transparencyOptions.layers), p.transparencyOptions.bufferBudget, uint64_t(bumpProducer)};
}

static bool rttNodeHasEffects(const SoNode * node)
{
  if (!rttNodeHasKnownTraversal(node) || node->isOfType(SoCallback::getClassTypeId()) ||
      node->isOfType(SoTextureCoordinateFunction::getClassTypeId())) return true;
  const SoFieldData * data = node->getFieldData();
  for (int i=0;data && i<data->getNumFields();++i) {
    const SoField * field = data->getField(node,i);
    if (field->isConnected()) return true;
    if (field->getTypeId() == SoSFImage::getClassTypeId()) {
      const SbImage & image = static_cast<const SoSFImage *>(field)->getValue();
      if (!image.hasData() || !(image == image)) return true;
    }
  }
  return false;
}

#ifdef HAVE_VRML97
static bool scanVRMLSnapshot(const SoNode * root, bool & cameraIndependent)
{
  std::vector<const SoNode *> pending(1, root);
  std::unordered_set<const SoNode *> seen;
  while (!pending.empty()) {
    const SoNode * node = pending.back(); pending.pop_back();
    if (!node || !seen.insert(node).second) continue;
    if (node->getNodeType() == SoNode::VRML2) return true;
    const SoFieldData * fields = node->getFieldData();
    for (int i = 0; fields && i < fields->getNumFields(); ++i) {
      const SoField * field = fields->getField(node, i);
      // An engine/field connection can propagate a camera update into a node
      // reference, including through nodes reached only by SFNode/MFNode.
      if (field->isConnected()) cameraIndependent = false;
      if (field->isOfType(SoSFNode::getClassTypeId()))
        pending.push_back(static_cast<const SoSFNode *>(field)->getValue());
      else if (field->isOfType(SoMFNode::getClassTypeId())) {
        const auto * nodes = static_cast<const SoMFNode *>(field);
        for (int j = 0; j < nodes->getNum(); ++j) pending.push_back((*nodes)[j]);
      }
    }
    const SoChildList * children = node->getChildren();
    for (int i = 0; children && i < children->getLength(); ++i) pending.push_back((*children)[i]);
  }
  return false;
}
#endif

static bool rttSceneHasEffects(const SoNode * root, CoinRenderRttCaptureMemo::Baseline * baseline=NULL)
{
  std::vector<const SoNode *> pending(1,root);
  std::unordered_set<const SoNode *> seen;
  while (!pending.empty()) {
    const SoNode * node = pending.back(); pending.pop_back();
    if (!node || !seen.insert(node).second) continue;
    if (rttNodeHasEffects(node)) return true;
    if (baseline && baseline->complete) {
      // This bounds transient metadata only; it never changes scene admission.
      if (baseline->nodes.size()>=4096) { baseline->complete=false; baseline->nodes.clear(); }
      else {
        CoinRenderRttCaptureMemo::Identity identity;
        identity.node=node; identity.revision=node->getNodeId(); identity.overrideValue=node->isOverride();
        if (node->isOfType(SoCamera::getClassTypeId())) {
          const auto * camera=static_cast<const SoCamera *>(node);
          identity.stereoMode=camera->getStereoMode(); identity.stereoAdjustment=camera->getStereoAdjustment();
          identity.stereoBalance=camera->getBalanceAdjustment();
        }
        baseline->nodes.push_back(identity);
      }
    }
    const SoFieldData * data = node->getFieldData();
    for (int i=0;data && i<data->getNumFields();++i) {
      const SoField * field = data->getField(node,i);
      if (field->isOfType(SoSFNode::getClassTypeId()))
        pending.push_back(static_cast<const SoSFNode *>(field)->getValue());
      else if (field->isOfType(SoMFNode::getClassTypeId())) {
        const auto * refs=static_cast<const SoMFNode *>(field);
        for (int j=0;j<refs->getNum();++j) pending.push_back((*refs)[j]);
      }
    }
    const SoChildList * children=node->getChildren();
    for (int i=0;children && i<children->getLength();++i) pending.push_back((*children)[i]);
  }
  return false;
}

SoCallbackAction::Response
CoinRenderActionP::rttMutationPreCB(void * data, SoCallbackAction *, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(data);
  // An effect outside a producer can change state not represented by fields.
  // Invalidate earlier proofs instead of assuming a callback only touches its node.
  const auto memo = p->sceneTextureCaptureMemo;
  if (memo && memo->allowed && memo->owner &&
      memo->owner->master->callbackRegistrationRevision() != memo->owner->captureCallbackRevision) {
    // A callback may install more action observers while the apply is active.
    memo->allowed = false;
    memo->invalidate();
  }
  if (memo && memo->allowed && (!memo->entries.empty() || !memo->activeTraces.empty()) &&
      rttNodeHasEffects(node)) memo->invalidate();
  return SoCallbackAction::CONTINUE;
}

SO_ACTION_SOURCE(CoinRenderAction);

// Contain authored C++ exceptions before they unwind through legacy
// SoAction::apply, whose read lock/root reference and path bookkeeping are
// released on normal return. Each dispatch restores its own traversal depth;
// the enclosing action then observes termination and rejects the whole frame.
template <void (*Method)(SoAction *, SoNode *)>
void CoinRenderActionP::guardedNodeMethod(SoAction * action, SoNode * node)
{
  auto * render = static_cast<CoinRenderAction *>(action);
  if (render->hasTerminated()) return;
  SoState * state = render->getState();
  const int depth = state->getDepth();
  const int pathLength = render->getCurPath()->fullPath().getLength();
  const auto reject = [&](CoinRenderAction::Status status, const char * message) {
    while (state->getDepth() > depth) state->pop();
    while (render->getCurPath()->fullPath().getLength() > pathLength) render->popCurPath();
    render->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(status,
      CoinRenderDiagnosticDomain::ACTION, SbString(message)));
    render->setTerminated(TRUE);
  };
  try { Method(action, node); }
  catch (const std::bad_alloc &) { reject(CoinRenderAction::OUT_OF_MEMORY, "Authored render traversal allocation failed"); }
  catch (const std::exception & error) { reject(CoinRenderAction::BACKEND_ERROR, error.what()); }
  catch (...) { reject(CoinRenderAction::BACKEND_ERROR, "Authored render traversal threw an exception"); }
}

void
CoinRenderAction::initClass(void)
{
  SO_ACTION_INTERNAL_INIT_CLASS(CoinRenderAction, SoCallbackAction);
  SO_ENABLE(CoinRenderAction, SoDepthBufferElement);
  SO_ENABLE(CoinRenderAction, SoTextureCombineElement);
  SO_ENABLE(CoinRenderAction, SoShadowStyleElement);
  CoinRenderDepthPolicyElement::initClass();
  SO_ENABLE(CoinRenderAction, CoinRenderDepthPolicyElement);
  CoinRenderShaderModuleElement::initClass();
  SO_ENABLE(CoinRenderAction, CoinRenderShaderModuleElement);
  SO_ACTION_ADD_METHOD(SoNode, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::portableNodeMethod>);
  SO_ACTION_ADD_METHOD(SoShape, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::shapeContentMethod>);
  SO_ACTION_ADD_METHOD(SoImage, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::screenContentMethod>);
  SO_ACTION_ADD_METHOD(SoText2, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::screenContentMethod>);
  SO_ACTION_ADD_METHOD(SoMarkerSet, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::markerContentMethod>);
  SO_ACTION_ADD_METHOD(SoIndexedMarkerSet, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::markerContentMethod>);
  SO_ACTION_ADD_METHOD(SoAlphaTest, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::alphaTestMethod>);
#ifdef HAVE_VRML97
  SO_ACTION_ADD_METHOD(SoVRMLShape, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLMaterial, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLLight, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLFog, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLBackground, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLPixelTexture, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
  SO_ACTION_ADD_METHOD(SoVRMLImageTexture, CoinRenderActionP::guardedNodeMethod<CoinRenderActionP::vrmlStateMethod>);
#endif
}

SbBool
CoinRenderAction::isGpuBackendAvailable(void)
{
  return CoinRenderTargetP::isGpuBackendAvailable() ? TRUE : FALSE;
}

CoinRenderAction::CoinRenderAction(void)
{
  this->pimpl->master = this;
  SO_ACTION_CONSTRUCTOR(CoinRenderAction);
  this->pimpl->viewport = SbViewportRegion(640, 512);
  this->pimpl->initCallbacks();
}

CoinRenderAction::CoinRenderAction(const SbViewportRegion & viewport)
  : inherited(viewport)
{
  this->pimpl->master = this;
  SO_ACTION_CONSTRUCTOR(CoinRenderAction);
  this->pimpl->viewport = viewport;
  this->setViewportRegion(viewport);
  this->pimpl->initCallbacks();
}

CoinRenderAction::~CoinRenderAction(void)
{
}

void
CoinRenderAction::setViewportRegion(const SbViewportRegion & region)
{
  inherited::setViewportRegion(region);
  this->pimpl->viewport = region;
  this->pimpl->cachedRoot = NULL;
}

const SbViewportRegion &
CoinRenderAction::getViewportRegion(void) const
{
  return this->pimpl->viewport;
}

void CoinRenderAction::setAbortCallback(CoinRenderAbortCB * callback, void * userdata)
{
  this->pimpl->abortCallback = callback; this->pimpl->abortUserdata = userdata;
  this->pimpl->abortResponse = CONTINUE;
  this->pimpl->cachedRoot = nullptr;
  this->pimpl->clearTranslationProof();
}
void CoinRenderAction::getAbortCallback(CoinRenderAbortCB * & callback, void * & userdata) const
{ callback = this->pimpl->abortCallback; userdata = this->pimpl->abortUserdata; }
SbBool CoinRenderAction::abortNow()
{
  auto & p = this->pimpl.get();
  if (!p.isApplying || !p.abortCallback) return FALSE;
  p.abortResponse = p.abortCallback(p.abortUserdata);
  if (p.abortResponse < CONTINUE || p.abortResponse > DELAY) {
    p.invalidAbortCode = true;
    p.setDiagnostic(CoinRenderDiagnosticShell::action(INVALID_SCENE,
      CoinRenderDiagnosticDomain::ACTION, SbString("Abort callback returned an invalid code")));
    this->setTerminated(TRUE);
    return TRUE;
  }
  if (p.abortResponse == DELAY && p.replayingAnnotations) p.abortResponse = CONTINUE;
  if (p.abortResponse == ABORT) {
    p.candidateAborted = true;
    this->setTerminated(TRUE);
  } else if (p.abortResponse == DELAY) {
    if (!this->addDelayedPath(const_cast<SoPath *>(this->getCurPath()))) p.abortResponse = PRUNE;
  }
  return p.abortResponse == CONTINUE ? FALSE : TRUE;
}
SbBool CoinRenderAction::addDelayedPath(SoPath * path)
{ return path ? this->deferOverlayPath(path) : FALSE; }
SbBool CoinRenderAction::isRenderingDelayedPaths() const
{ return this->pimpl->isApplying && this->pimpl->replayingAnnotations ? TRUE : FALSE; }
void CoinRenderAction::addPreRenderCallback(CoinRenderPreRenderCB * callback, void * userdata)
{ if (callback) this->pimpl->preRenderCallbacks.push_back({callback, userdata}); }
void CoinRenderAction::removePreRenderCallback(CoinRenderPreRenderCB * callback, void * userdata)
{
  auto & list = this->pimpl->preRenderCallbacks;
  list.erase(std::remove_if(list.begin(), list.end(),
    [=](const CoinRenderActionP::PreRenderCallback & entry) {
      return entry.callback == callback && entry.userdata == userdata;
    }), list.end());
}
void CoinRenderAction::invalidateState()
{
  auto & p = this->pimpl.get();
  p.cachedRoot = nullptr;
  p.invalidatedDuringApply = true;
  if (p.isApplying) { p.stateInvalidationPending = true; return; }
  p.stateInvalidationPending = false;
  this->inherited::invalidateState();
  p.cameraSensor.detach(); p.cachedCamera = p.candidateCamera = nullptr;
  p.cameraOverlayBasis = CoinRenderCameraOverlayBasis();
  p.cameraPatchInvalidated = true; p.cameraRecaptureRequired = true;
  p.clearTranslationProof();
  CoinRenderFramePlanBuilder cleanBuilder;
  std::swap(p.builder, cleanBuilder);
}
void CoinRenderAction::setUpdateArea(const SbVec2f & origin, const SbVec2f & size)
{ this->pimpl->updateOrigin = origin; this->pimpl->updateSize = size; }
void CoinRenderAction::getUpdateArea(SbVec2f & origin, SbVec2f & size) const
{ origin = this->pimpl->updateOrigin; size = this->pimpl->updateSize; }
void CoinRenderAction::setSortedObjectOrderStrategy(SortedObjectOrderStrategy strategy,
  CoinRenderSortedObjectOrderCB * callback, void * userdata)
{
  this->pimpl->sortingStrategy = strategy; this->pimpl->sortingCallback = callback;
  this->pimpl->sortingUserdata = userdata; this->pimpl->cachedRoot = nullptr;
  this->pimpl->clearTranslationProof();
}
void CoinRenderAction::setTransparentDelayedObjectRenderType(TransparentDelayedObjectRenderType type)
{
  this->pimpl->delayedRenderType = type;
  this->pimpl->cachedRoot = nullptr; this->pimpl->clearTranslationProof();
}
CoinRenderAction::TransparentDelayedObjectRenderType
CoinRenderAction::getTransparentDelayedObjectRenderType() const { return this->pimpl->delayedRenderType; }
void CoinRenderAction::setSmoothing(SbBool enabled)
{
  this->pimpl->smoothing = enabled != FALSE;
  this->pimpl->cachedRoot = nullptr; this->pimpl->clearTranslationProof();
}
SbBool CoinRenderAction::isSmoothing() const { return this->pimpl->smoothing; }
void CoinRenderAction::setDelayedObjDepthWrite(SbBool enabled)
{
  if (this->pimpl->delayedObjDepthWrite != (enabled != FALSE)) {
    this->pimpl->delayedObjDepthWrite = enabled != FALSE;
    this->pimpl->cachedRoot = nullptr; this->pimpl->clearTranslationProof();
  }
}
SbBool CoinRenderAction::getDelayedObjDepthWrite() const
{ return this->pimpl->delayedObjDepthWrite; }

void
CoinRenderAction::setRenderTarget(CoinRenderTarget * target)
{
  if (this->pimpl->target != target && this->pimpl->target)
    this->pimpl->target->getPimpl()->detachedFromAction();
  this->pimpl->target = target;
  this->pimpl->cachedRoot = NULL;
}

CoinRenderTarget *
CoinRenderAction::getRenderTarget(void) const
{
  return this->pimpl->target;
}

void
CoinRenderAction::setBackgroundColor(const SbColor4f & color)
{
  this->pimpl->backgroundColor = color;
  this->pimpl->cachedRoot = NULL;
}

const SbColor4f &
CoinRenderAction::getBackgroundColor(void) const
{
  return this->pimpl->backgroundColor;
}

void
CoinRenderAction::setTransparencyType(TransparencyType type)
{
  if (this->pimpl->transparencyType != type) {
    this->pimpl->transparencyType = type;
    this->pimpl->cachedRoot = NULL;
  }
}

CoinRenderAction::TransparencyType
CoinRenderAction::getTransparencyType(void) const
{
  return this->pimpl->transparencyType;
}

void
CoinRenderAction::setDrawMode(DrawMode mode)
{
  if (this->pimpl->drawMode != mode) {
    this->pimpl->drawMode = mode;
    this->pimpl->cachedRoot = NULL;
  }
}

CoinRenderAction::DrawMode
CoinRenderAction::getDrawMode(void) const
{
  return this->pimpl->drawMode;
}

void
CoinRenderAction::setOverlayColor(const SbColor4f & color)
{
  if (this->pimpl->overlayColor != color) {
    this->pimpl->overlayColor = color;
    this->pimpl->cachedRoot = NULL;
  }
}

const SbColor4f &
CoinRenderAction::getOverlayColor(void) const
{
  return this->pimpl->overlayColor;
}

void
CoinRenderAction::setTexturesEnabled(SbBool enabled)
{
  if (this->pimpl->texturesEnabled != enabled) {
    this->pimpl->texturesEnabled = enabled;
    this->pimpl->cachedRoot = NULL;
  }
}

SbBool
CoinRenderAction::isTexturesEnabled(void) const
{
  return this->pimpl->texturesEnabled;
}

void CoinRenderAction::setPassCallback(CoinRenderPassCB * callback, void * userdata)
{
  this->pimpl->passCallback = callback; this->pimpl->passUserdata = userdata;
  this->pimpl->cachedRoot = nullptr; this->pimpl->clearTranslationProof();
}
int CoinRenderAction::getCurPass() const { return this->pimpl->currentPass; }
void CoinRenderAction::setCurPass(int pass, int total)
{
  this->pimpl->externalPass = true; this->pimpl->requestedExternalPass = pass;
  this->pimpl->currentPass = pass; this->pimpl->numPasses = total;
  this->pimpl->cachedRoot = nullptr; this->pimpl->clearTranslationProof();
}
void CoinRenderAction::setNumPasses(int passes) {
  this->pimpl->externalPass = false; this->pimpl->currentPass = 0;
  if (this->pimpl->numPasses != passes) {
    this->pimpl->numPasses = passes;
    this->pimpl->cachedRoot = NULL;
  }
}
int CoinRenderAction::getNumPasses() const { return this->pimpl->numPasses; }
void CoinRenderAction::setMultipassBufferBudget(uint64_t bytes) {
  if (this->pimpl->multipassBufferBudget != bytes) {
    this->pimpl->multipassBufferBudget = bytes;
    this->pimpl->cachedRoot = NULL;
  }
}
uint64_t CoinRenderAction::getMultipassBufferBudget() const {
  return this->pimpl->multipassBufferBudget;
}

void CoinRenderAction::setSortedLayersNumPasses(int passes) {
  if (this->pimpl->transparencyOptions.layers != static_cast<uint32_t>(passes)) {
    this->pimpl->transparencyOptions.layers = static_cast<uint32_t>(passes);
    this->pimpl->cachedRoot = NULL;
  }
}

int CoinRenderAction::getSortedLayersNumPasses(void) const {
  return static_cast<int>(this->pimpl->transparencyOptions.layers);
}

void CoinRenderAction::setTransparencyBufferBudget(uint64_t bytes) {
  if (this->pimpl->transparencyOptions.bufferBudget != bytes) {
    this->pimpl->transparencyOptions.bufferBudget = bytes;
    this->pimpl->cachedRoot = NULL;
  }
}

uint64_t CoinRenderAction::getTransparencyBufferBudget(void) const {
  return this->pimpl->transparencyOptions.bufferBudget;
}


SbBool CoinRenderAction::setPortableShaderModule(SoShaderProgram *program,
                                                const CoinRenderShaderModule &module)
{
  if(!program) return FALSE;
  SbString diagnostic;
  if(!coin_render_validate_shader_module(module,diagnostic)) return FALSE;
  try {
    auto registration=std::make_shared<CoinRenderShaderRegistration>();
    registration->module=module;
    program->ref();
    registration->program=std::shared_ptr<SoShaderProgram>(program,[](SoShaderProgram *node){node->unref();});
    this->pimpl->portableShaderModules[program]=registration;
    this->pimpl->hasLastValidPlan=false;
    return TRUE;
  } catch(const std::bad_alloc &) { return FALSE; }
}

void CoinRenderAction::clearPortableShaderModule(SoShaderProgram *program)
{
  this->pimpl->portableShaderModules.erase(program);
  this->pimpl->hasLastValidPlan=false;
}

void CoinRenderAction::clearPortableShaderModules()
{
  this->pimpl->portableShaderModules.clear();
  this->pimpl->hasLastValidPlan=false;
}

void
CoinRenderAction::setFastPathEnabled(SbBool enable)
{
  this->pimpl->fastPathEnabled = (enable != FALSE);
  this->pimpl->cachedRoot = NULL;
}

SbBool
CoinRenderAction::isFastPathEnabled(void) const
{
  return this->pimpl->fastPathEnabled ? TRUE : FALSE;
}

CoinRenderAction::Status
CoinRenderAction::getLastStatus(void) const
{
  return this->pimpl->lastStatus;
}

const SbString &
CoinRenderAction::getLastError(void) const
{
  return this->pimpl->lastError;
}

const SbString &
CoinRenderAction::getRecordingLog(void) const
{
  if (this->pimpl->hasLastValidPlan && !this->pimpl->recordingLogValid) {
    this->pimpl->lastRecordingLog =
      this->pimpl->recordingBackend.recordToString(this->pimpl->lastValidPlan).c_str();
    this->pimpl->recordingLogValid = true;
  }
  return this->pimpl->lastRecordingLog;
}

SbBool
CoinRenderAction::rejectUnsupported(const SbString & reason)
{
  if (!this->pimpl->isApplying || this->hasTerminated()) return FALSE;
  this->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
    UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, reason));
  this->setTerminated(TRUE);
  return TRUE;
}

SbBool
CoinRenderAction::rejectInvalidScene(const SbString & reason)
{
  if (!this->pimpl->isApplying || this->hasTerminated()) return FALSE;
  this->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
    INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN, reason));
  this->setTerminated(TRUE);
  return TRUE;
}

SbBool
CoinRenderAction::captureScreenContent(SoNode * node)
{
  if (!this->pimpl->isApplying || this->hasTerminated()) return FALSE;
  if (!node || this->getCurPathTail() != node ||
      (!node->isOfType(SoText2::getClassTypeId()) && !node->isOfType(SoImage::getClassTypeId()))) {
    this->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
      UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("Screen-content delegation requires the current SoText2/SoImage callback node")));
    this->setTerminated(TRUE);
    return FALSE;
  }
  const bool success = this->pimpl->boundingBoxObservers ||
    this->pimpl->builder.captureScreenContent(this, node);
  if (!success) {
    this->setTerminated(TRUE);
    return FALSE;
  }
  if (!this->hasSingleShapeCallbacks(node->getTypeId())) {
    // Observe inherited primitives once without reentering the host callback
    // or capturing its image quad a second time. Keep observer state local.
    SoState * state = this->getState();
    state->push();
    struct ObserverScope {
      CoinRenderActionP & owner;
      SoState * state;
      bool previous;
      ~ObserverScope() { state->pop(); owner.boundingBoxObservers = previous; }
    } scope{this->pimpl.get(), state, this->pimpl->boundingBoxObservers};
    this->pimpl->boundingBoxObservers = true;
    static_cast<SoShape *>(node)->SoShape::callback(this);
  }
  return TRUE;
}

void
CoinRenderAction::beginForegroundPass()
{
  this->beginForegroundPass(FALSE);
}

void
CoinRenderAction::beginForegroundPass(SbBool clearDepth)
{
  if (this->pimpl->isApplying && !this->pimpl->replayingAnnotations)
    this->pimpl->builder.beginForeground(clearDepth != FALSE);
}

void
CoinRenderAction::endForegroundPass()
{
  if (this->pimpl->isApplying && !this->pimpl->replayingAnnotations)
    this->pimpl->builder.endForeground();
}

void
CoinRenderAction::clearManagerLayerBoundaries()
{
  this->pimpl->managerLayerBoundaries.clear();
}

void
CoinRenderAction::addManagerLayerBoundary(SoNode * separator, SbBool clearDepth,
                                          int transparencyType)
{
  if (separator || transparencyType == COIN_RENDER_MANAGER_OWNER)
    this->pimpl->managerLayerBoundaries.emplace_back(
      separator, clearDepth != FALSE, transparencyType);
}

SbBool
CoinRenderAction::deferOverlayPath(SoPath * supplied, int priority)
{
  if (!this->pimpl->isApplying || this->pimpl->replayingAnnotations) return FALSE;
  SoPath * path = (supplied ? supplied : this->getCurPath())->copy();
  path->ref();
  try { this->pimpl->delayedOverlays.push_back({path, priority}); }
  catch (...) { path->unref(); throw; }
  return TRUE;
}

SbBool
CoinRenderAction::deferAnnotation(int priority)
{
  if (!this->pimpl->isApplying || this->pimpl->replayingAnnotations) return FALSE;
  SoPath * path = this->getCurPath()->copy();
  path->ref();
  try { this->pimpl->delayedAnnotations.push_back({path, priority}); }
  catch (...) { path->unref(); throw; }
  return TRUE;
}

void
CoinRenderAction::apply(SoNode * root)
{
  this->pimpl->executeApply([&]() {
    if (root) {
      this->inherited::apply(root);
    }
  }, root);
}

void
CoinRenderAction::applyAsync(SoNode * root, CoinRenderReadbackTicket & outTicket)
{
  if (this->pimpl->isApplying) {
    this->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
      INVALID_SCENE, CoinRenderDiagnosticDomain::ACTION,
      SbString("Nested applyAsync() calls are not permitted")));
    this->pimpl->hasReentrancyError = true;
    return;
  }
  this->pimpl->asyncTicket = &outTicket;
  this->apply(root);
  this->pimpl->asyncTicket = NULL;
}

void
CoinRenderAction::apply(SoPath * path)
{
  this->pimpl->executeApply([&]() {
    if (path) {
      this->inherited::apply(path);
    }
  });
}

void
CoinRenderAction::apply(const SoPathList & pathlist, SbBool obeysrules)
{
  this->pimpl->executeApply([&]() {
    this->inherited::apply(pathlist, obeysrules);
  });
}

void
CoinRenderAction::beginTraversal(SoNode * root)
{
  if (this->pimpl->isApplying) {
    SoState * state = this->getState();
    state->push();
    SoShapeStyleElement::setTransparencyType(
      state, static_cast<int32_t>(this->pimpl->transparencyType));
    SoLazyElement::setTransparencyType(
      state, static_cast<int32_t>(this->pimpl->transparencyType));
    if (root && ((!this->pimpl->planOnly && this->pimpl->transparencyType == SORTED_LAYERS_BLEND) ||
                 this->pimpl->inheritedTransparencyOverride)) {
      SoOverrideElement::setTransparencyTypeOverride(state, root, TRUE);
    }
    // The adapter marks its base explicitly. Each superimposition is an
    // independent authored traversal; only the base receives technical and
    // texture-quality overrides. Keep every traversal in this same builder.
    if (root && root->isOfType(SoSeparator::getClassTypeId())) {
      const auto * group = static_cast<const SoGroup *>(root);
      int baseIndex = -1;
      for (int i = 0; i < group->getNumChildren(); ++i)
        for (const auto & boundary : this->pimpl->managerLayerBoundaries)
          if (std::get<0>(boundary) == group->getChild(i) &&
              std::get<2>(boundary) == COIN_RENDER_MANAGER_BASE_LAYER) baseIndex = i;
      if (baseIndex >= 0) {
        for (int i = 0; i < group->getNumChildren() && !this->hasTerminated(); ++i) {
          SoNode * child = group->getChild(i);
          const bool highlight = std::any_of(this->pimpl->managerLayerBoundaries.begin(),
            this->pimpl->managerLayerBoundaries.end(),
            [child](const std::tuple<const SoNode *, bool, int> & boundary) {
              return std::get<0>(boundary) == child &&
                std::get<2>(boundary) == COIN_RENDER_MANAGER_HIGHLIGHT_LAYER;
            });
          const int section = i < baseIndex ? 0 : (i == baseIndex || highlight) ? 1 : 2;
          if (this->pimpl->managerCaptureSection >= 0 &&
              this->pimpl->managerCaptureSection != section) continue;
          this->pushCurPath(i, child);
          if (i == baseIndex) {
            // CoinGL HIDDEN_LINE clears both buffers even after backgrounds.
            // Their traversal/RTT captures still occur, but their draws no
            // longer contribute to this frame's attachments.
            if (this->pimpl->drawMode == DrawMode::HIDDEN_LINE)
              this->pimpl->builder.discardDrawOutput();
            const auto * base = static_cast<const SoGroup *>(child);
            const bool hasBase = base->getNumChildren() != 0;
            this->pimpl->builder.beginAuthoredScene(hasBase ||
              this->pimpl->drawMode == DrawMode::HIDDEN_LINE);
            if (base->getNumChildren()) {
              this->pushCurPath(0, base->getChild(0));
              this->beginTraversal(base->getChild(0));
              this->popCurPath();
            }
            this->pimpl->builder.capturePendingDepthClear(this, !hasBase);
            this->pimpl->builder.endAuthoredScene();
          }
          else this->inherited::beginTraversal(child);
          this->popCurPath();
        }
        state->pop();
        return;
      }
    }
    struct TechnicalCaptureScope {
      CoinRenderActionP * p;
      explicit TechnicalCaptureScope(CoinRenderActionP * owner) : p(owner) {
        p->technicalRttCaptures.clear();
        p->technicalRttReplay = false;
        p->technicalRttCapture = p->drawMode == DrawMode::HIDDEN_LINE ||
          p->drawMode == DrawMode::SHADED_HIDDEN_LINES || p->drawMode == DrawMode::WIREFRAME_OVERLAY;
      }
      ~TechnicalCaptureScope() {
        p->technicalRttCaptures.clear();
        p->technicalRttCapture = p->technicalRttReplay = false;
      }
    } technicalScope(&this->pimpl.get());
    const auto traverseBase = [&]() {
      this->inherited::beginTraversal(root);
      if (this->pimpl->technicalRttCapture) this->pimpl->technicalRttReplay = true;
    };
    if (root && !this->pimpl->texturesEnabled) {
      SoTextureQualityElement::set(state, 0.0f);
      SoTextureOverrideElement::setQualityOverride(state, TRUE);
    }
    if (root && this->pimpl->drawMode == DrawMode::HIDDEN_LINE) {
      state->push();
      SoMaterialBindingElement::set(state, root, SoMaterialBindingElement::OVERALL);
      SoLightModelElement::set(state, root, SoLightModelElement::BASE_COLOR);
      SoPolygonOffsetElement::set(state, root, 1.0f, 1.0f,
                                  SoPolygonOffsetElement::FILLED, TRUE);
      SoOverrideElement::setMaterialBindingOverride(state, root, TRUE);
      SoOverrideElement::setLightModelOverride(state, root, TRUE);
      SoOverrideElement::setPolygonOffsetOverride(state, root, TRUE);
      this->pimpl->builder.setColorWriteEnabled(false);
      traverseBase();
      this->pimpl->builder.setColorWriteEnabled(true);
      state->pop();

      state->push();
      SoMaterialBindingElement::set(state, root, SoMaterialBindingElement::OVERALL);
      SoLightModelElement::set(state, root, SoLightModelElement::BASE_COLOR);
      SoPolygonOffsetElement::set(state, root, 0.0f, 0.0f,
                                  SoPolygonOffsetElement::FILLED, FALSE);
      SoDrawStyleElement::set(state, root, SoDrawStyleElement::LINES);
      SoOverrideElement::setLightModelOverride(state, root, TRUE);
      SoOverrideElement::setPolygonOffsetOverride(state, root, TRUE);
      SoOverrideElement::setDrawStyleOverride(state, root, TRUE);
      traverseBase();
      state->pop();
      state->pop();
      return;
    }
    if (root && this->pimpl->drawMode == DrawMode::SHADED_HIDDEN_LINES) {
      state->push();
      SoDrawStyleElement::set(state, root, SoDrawStyleElement::FILLED);
      SoLightModelElement::set(state, root, SoLightModelElement::PHONG);
      SoPolygonOffsetElement::set(state, root, 1.0f, 1.0f,
                                  SoPolygonOffsetElement::FILLED, TRUE);
      SoOverrideElement::setDrawStyleOverride(state, root, TRUE);
      SoOverrideElement::setLightModelOverride(state, root, TRUE);
      SoOverrideElement::setPolygonOffsetOverride(state, root, TRUE);
      traverseBase();

      SoPolygonOffsetElement::set(state, root, 0.0f, 0.0f,
                                  SoPolygonOffsetElement::FILLED, FALSE);
      SoDrawStyleElement::set(state, root, SoDrawStyleElement::LINES);
      SoLinePatternElement::set(state, root, 0xffff, 1);
      SoOverrideElement::setLinePatternOverride(state, root, TRUE);
      SoDepthBufferElement::set(state, TRUE, FALSE, SoDepthBufferElement::LEQUAL,
                                SoDepthBufferElement::getRange(state));
      traverseBase();

      SoLineWidthElement::set(state, root, 1.0f);
      SoOverrideElement::setLineWidthOverride(state, root, TRUE);
      SoLinePatternElement::set(state, root, 0xf0f0, 2);
      this->pimpl->builder.setLinePatternOverride(true, 0xf0f0u, 2);
      SoDepthBufferElement::set(state, TRUE, FALSE, SoDepthBufferElement::GREATER,
                                SoDepthBufferElement::getRange(state));
      traverseBase();
      this->pimpl->builder.setLinePatternOverride(false);
      state->pop();
      state->pop();
      return;
    }
    if (root && this->pimpl->drawMode == DrawMode::WIREFRAME_OVERLAY) {
      state->push();
      SoPolygonOffsetElement::set(state, root, 1.0f, 1.0f,
                                  SoPolygonOffsetElement::FILLED, TRUE);
      SoOverrideElement::setPolygonOffsetOverride(state, root, TRUE);
      traverseBase();
      state->pop();

      state->push();
      SoPolygonOffsetElement::set(state, root, 0.0f, 0.0f,
                                  SoPolygonOffsetElement::FILLED, FALSE);
      SoOverrideElement::setPolygonOffsetOverride(state, root, TRUE);
      const uint32_t packed = this->pimpl->overlayColor.getPackedValue();
      SoLazyElement::setPacked(state, root, 1, &packed, TRUE);
      SoLightModelElement::set(state, root, SoLightModelElement::BASE_COLOR);
      SoMaterialBindingElement::set(state, root, SoMaterialBindingElement::OVERALL);
      SoDrawStyleElement::set(state, root, SoDrawStyleElement::LINES);
      SoOverrideElement::setLightModelOverride(state, root, TRUE);
      SoOverrideElement::setDiffuseColorOverride(state, root, TRUE);
      SoOverrideElement::setMaterialBindingOverride(state, root, TRUE);
      SoOverrideElement::setDrawStyleOverride(state, root, TRUE);
      traverseBase();
      state->pop();
      state->pop();
      return;
    }
    if (root && this->pimpl->drawMode != DrawMode::SCENE) {
      SoDrawStyleElement::set(state, root,
        this->pimpl->drawMode == DrawMode::POINTS
          ? SoDrawStyleElement::POINTS : SoDrawStyleElement::LINES);
      SoLightModelElement::set(state, root, SoLightModelElement::BASE_COLOR);
      SoOverrideElement::setDrawStyleOverride(state, root, TRUE);
      SoOverrideElement::setLightModelOverride(state, root, TRUE);
      if (this->pimpl->drawMode == DrawMode::BOUNDING_BOX) {
        SoComplexityTypeElement::set(state, root, SoComplexityTypeElement::BOUNDING_BOX);
        SoOverrideElement::setComplexityTypeOverride(state, root, TRUE);
      }
    }
    if (root) {
      traverseBase();
    }
    state->pop();
    return;
  }

  this->pimpl->executeApply([&]() {
    if (root) {
      this->inherited::beginTraversal(root);
    }
  });
}

// CoinRenderActionP implementation

CoinRenderActionP::CoinRenderActionP(CoinRenderAction * m)
  : master(m),
    target(NULL),
    asyncTicket(NULL),
    backgroundColor(0.0f, 0.0f, 0.0f, 1.0f),
    transparencyType(CoinRenderAction::SCREEN_DOOR),
    lastStatus(CoinRenderAction::SUCCESS),
    lastDiagnosticDomain(CoinRenderDiagnosticDomain::NONE),
    hasLastValidPlan(false),
    recordingLogValid(false),
    isApplying(false),
    hasReentrancyError(false),
    fastPathEnabled(true),
    vrmlSnapshotSensor(CoinRenderActionP::vrmlSnapshotSensorCB, this),
    cameraSensor(CoinRenderActionP::cameraSensorCB, this)
{
  this->vrmlSnapshotSensor.setPriority(0);
  this->cameraSensor.setPriority(0);
}

CoinRenderActionP::~CoinRenderActionP()
{
}

void
CoinRenderActionP::setDiagnostic(const CoinRenderActionDiagnostic & diagnostic)
{
  this->lastStatus = diagnostic.status;
  this->lastDiagnosticDomain = diagnostic.domain;
  this->lastError = diagnostic.message;
}

namespace {
size_t
captureStorageEstimate(SoNode * root)
{
  if (!root) return 0;
  // Inspect at most 64 nodes through two child levels. Broad groups already
  // supply a useful hint; their descendants are never scanned here.
  struct Pending { SoNode * node; unsigned depth; };
  Pending pending[64] = {{root, 0}};
  size_t next = 0, end = 1, estimate = 0;
  while (next < end) {
    const Pending current = pending[next++];
    const SoChildList * children = current.node->getChildren();
    if (!children) continue;
    const int count = children->getLength();
    estimate = std::max(estimate, std::min(size_t(count), size_t(65536)));
    if (current.depth == 2 || count >= 256) continue;
    for (int i = 0; i < count && end < 64; ++i)
      pending[end++] = {(*children)[i], current.depth + 1};
  }
  return estimate;
}

// Exact types only: custom subclasses and view-dependent traversal fall back.
bool
cameraStableScene(SoNode * root, SoCamera * camera, bool translationProfile = false,
                  std::unordered_map<const SoNode *, size_t> * materialVisits = NULL)
{
  std::vector<SoNode *> pending(1, root);
  // Node types are fixed for this qualification. Shared leaves need their
  // fields checked once; groups still expand on every occurrence so a reused
  // camera cannot disappear behind a cached ancestor.
  const SoType separatorType = SoSeparator::getClassTypeId();
  const SoType groupType = SoGroup::getClassTypeId();
  const SoType leafTypes[] = {
    SoCube::getClassTypeId(), SoTransform::getClassTypeId(), SoMaterial::getClassTypeId(),
    SoTranslation::getClassTypeId(), SoLightModel::getClassTypeId(),
    SoDirectionalLight::getClassTypeId(), SoEnvironment::getClassTypeId(),
    SoCoordinate3::getClassTypeId(), SoNormal::getClassTypeId(),
    SoNormalBinding::getClassTypeId(), SoMaterialBinding::getClassTypeId(),
    SoShapeHints::getClassTypeId(), SoPointLight::getClassTypeId(), SoSpotLight::getClassTypeId(),
    SoIndexedFaceSet::getClassTypeId(), SoIndexedLineSet::getClassTypeId()
  };
  const SoType * leafEnd = leafTypes + sizeof(leafTypes) / sizeof(leafTypes[0]);
  // Runtime SoType alone does not exclude C++ subclasses which inherit their
  // parent's registration but override traversal. Object overlays skip that
  // traversal, so every admitted occurrence must be a concrete native node.
  const std::type_info * objectTypes[] = {
    &typeid(SoCube), &typeid(SoTransform), &typeid(SoMaterial),
    &typeid(SoTranslation), &typeid(SoLightModel), &typeid(SoDirectionalLight),
    &typeid(SoEnvironment), &typeid(SoCoordinate3), &typeid(SoNormal),
    &typeid(SoNormalBinding), &typeid(SoMaterialBinding), &typeid(SoShapeHints),
    &typeid(SoPointLight), &typeid(SoSpotLight), &typeid(SoIndexedFaceSet),
    &typeid(SoIndexedLineSet), &typeid(SoSeparator), &typeid(SoGroup),
    &typeid(SoPerspectiveCamera), &typeid(SoOrthographicCamera)
  };
  std::unordered_set<SoNode *> qualifiedSharedLeaves;
  unsigned cameraOccurrences = 0;
  size_t materialOccurrences = 0;
  while (!pending.empty()) {
    SoNode * node = pending.back();
    pending.pop_back();
    // Count every syntactic material occurrence before shared-leaf dedup.
    // Held references do not represent traversal through a different scope.
    if (materialVisits && node->getTypeId() == SoMaterial::getClassTypeId()) {
      if (++materialOccurrences > 65536) return false;
      ++(*materialVisits)[node];
    }
    if (node == camera) {
      if (++cameraOccurrences != 1) return false;
      if (!translationProfile) continue;
    }
    const bool shared = node->getRefCount() > 1;
    if (shared && qualifiedSharedLeaves.find(node) != qualifiedSharedLeaves.end()) continue;
    const SoType type = node->getTypeId();
    const bool isGroup = type == separatorType || type == groupType;
    if (node != camera && !isGroup && std::find(leafTypes, leafEnd, type) == leafEnd) return false;
    if (translationProfile && std::none_of(std::begin(objectTypes), std::end(objectTypes),
        [node](const std::type_info * native) { return typeid(*node) == *native; })) return false;
    // Connected scene fields may change as a consequence of camera fields.
    // Qualify their absence once, outside all subsequent overlay updates.
    const SoFieldData * fields = node->getFieldData();
    if (fields) {
      const int fieldCount = fields->getNumFields();
      for (int i = 0; i < fieldCount; ++i)
        if (fields->getField(node, i)->isConnected() ||
            (translationProfile && fields->getField(node, i)->isIgnored())) return false;
    }
    if (isGroup) {
      SoGroup * group = static_cast<SoGroup *>(node);
      for (int i = 0; i < group->getNumChildren(); ++i) {
        pending.push_back(group->getChild(i));
      }
    } else if (shared) qualifiedSharedLeaves.insert(node);
  }
  return cameraOccurrences == 1;
}
bool translationFieldsStable(const SoNode *);
bool objectOverlayEnabled(const char *);
}

void
CoinRenderActionP::vrmlSnapshotSensorCB(void * data, SoSensor * sensor)
{
  auto * self = static_cast<CoinRenderActionP *>(data);
  auto * observer = static_cast<SoNodeSensor *>(sensor);
  SoNode * root = observer->getAttachedNode();
  SoNode * trigger = observer->getTriggerNode();
  SoField * field = observer->getTriggerField();
  // Only exact builtin camera fields have known, non-structural semantics.
  // In particular, inherited fields on application camera subclasses, node
  // references, propagated connection updates, touch(), and group edits must
  // not carry the proof forward. Their next apply scans the graph again.
  const bool cameraField = trigger && field && field->getContainer() == trigger &&
    (typeid(*trigger) == typeid(SoPerspectiveCamera) ||
     typeid(*trigger) == typeid(SoOrthographicCamera)) &&
    !field->isOfType(SoSFNode::getClassTypeId()) &&
    !field->isOfType(SoMFNode::getClassTypeId()) &&
    observer->getTriggerOperationType() == SoNotRec::FIELD_UPDATE;
  // Reuse the existing whole-scene camera qualification, and additionally
  // exclude connections on referenced nodes which that child-list proof does
  // not inspect. Duplicate notifications can be suppressed at an ancestor,
  // so a camera origin alone is not proof against connected topology changes.
  if (self->vrmlSnapshotValid && !self->vrmlSnapshotPresent && root && cameraField &&
      self->vrmlSnapshotCameraIndependent && self->cachedCamera == trigger &&
      self->cameraSensor.getAttachedNode() == root && !self->cameraPatchInvalidated)
    self->vrmlSnapshotRootId = root->getNodeId();
  else self->vrmlSnapshotValid = false;
}

bool
CoinRenderActionP::containsVRMLSnapshot(SoNode * root)
{
#ifdef HAVE_VRML97
  if (this->vrmlSnapshotSensor.getAttachedNode() != root) {
    this->vrmlSnapshotSensor.detach();
    this->vrmlSnapshotValid = false;
    if (root) this->vrmlSnapshotSensor.attach(root);
  }
  if (!root) return false;
  const SbUniqueId revision = root->getNodeId();
  if (this->vrmlSnapshotValid && !this->vrmlSnapshotPresent &&
      this->vrmlSnapshotRootId == revision) return false;
  // Keep scanning positive scenes: VRML capture observes mutable state which
  // does not necessarily notify. Only a stable negative result is reusable.
  // Install the auditor before reading fields, since evaluating a connection
  // can itself notify. Such a scan cannot qualify cache reuse this apply.
  this->vrmlSnapshotValid = false;
  bool cameraIndependent = true;
  const bool present = scanVRMLSnapshot(root, cameraIndependent);
  this->vrmlSnapshotCameraIndependent = cameraIndependent;
  this->vrmlSnapshotPresent = present;
  this->vrmlSnapshotRootId = root->getNodeId();
  this->vrmlSnapshotValid = this->vrmlSnapshotRootId == revision;
  return present || !this->vrmlSnapshotValid;
#else
  return false;
#endif
}

void
CoinRenderActionP::cameraSensorCB(void * data, SoSensor * sensor)
{
  CoinRenderActionP * self = static_cast<CoinRenderActionP *>(data);
  SoNodeSensor * nodeSensor = static_cast<SoNodeSensor *>(sensor);
  SoNode * trigger = nodeSensor->getTriggerNode();
  SoField * field = nodeSensor->getTriggerField();
  SoSFVec3f * position = NULL;
  if (trigger && trigger->getTypeId() == SoTranslation::getClassTypeId())
    position = &static_cast<SoTranslation *>(trigger)->translation;
  else if (trigger && trigger->getTypeId() == SoTransform::getClassTypeId())
    position = &static_cast<SoTransform *>(trigger)->translation;
  if (position && field == position && field->getContainer() == trigger &&
      nodeSensor->getTriggerOperationType() == SoNotRec::FIELD_UPDATE) {
    self->translationInputDirty = true;
    const auto found = self->translationByNode.find(trigger);
    if (!position->isConnected() && !position->isIgnored() && self->translationProofValid && !self->translationInvalidated &&
        !self->cameraOnlyDirty && found != self->translationByNode.end())
      self->translationDirty.insert(found->second);
    else self->translationInvalidated = true;
  } else if (trigger && field && field->getContainer() == trigger &&
             nodeSensor->getTriggerOperationType() == SoNotRec::FIELD_UPDATE &&
             trigger->getTypeId() == SoMaterial::getClassTypeId()) {
    auto * material = static_cast<SoMaterial *>(trigger);
    const bool knownField = field == &material->ambientColor || field == &material->diffuseColor ||
      field == &material->specularColor || field == &material->emissiveColor || field == &material->shininess;
    // Notifications record changes regardless of the current optout. Admission
    // and preparation read the option once per operation, including changes
    // made between this notification and apply().
    self->translationInputDirty = true;
    // Validate all fields once per dirty source during overlay preparation.
    if (knownField && self->translationProofValid &&
        !self->translationInvalidated && !self->cameraOnlyDirty && self->materialByNode.count(trigger))
      self->materialDirty.insert(trigger);
    else self->translationInvalidated = true;
  } else if (trigger && field && field->getContainer() == trigger &&
             nodeSensor->getTriggerOperationType() == SoNotRec::FIELD_UPDATE &&
             trigger->getTypeId() == SoCube::getClassTypeId()) {
    auto * cube = static_cast<SoCube *>(trigger);
    self->translationInputDirty = true;
    if ((field == &cube->width || field == &cube->height || field == &cube->depth) &&
        self->translationProofValid && !self->translationInvalidated &&
        !self->cameraOnlyDirty && self->cubeByNode.count(trigger)) self->geometryDirty.insert(trigger);
    else self->translationInvalidated = true;
  } else self->translationInvalidated = true;
  if (self->candidateCamera &&
      nodeSensor->getTriggerNode() == self->candidateCamera &&
      nodeSensor->getTriggerField() &&
      nodeSensor->getTriggerField()->getContainer() == self->candidateCamera &&
      nodeSensor->getTriggerOperationType() == SoNotRec::FIELD_UPDATE) {
    self->cameraOnlyDirty = true;
  } else {
    self->cameraPatchInvalidated = true;
  }
}

void
CoinRenderActionP::rememberFrameRoot(SoNode * root, bool qualifyCamera)
{
  ++this->captureCameraBasisCalls;
  struct BasisTraceScope {
    CoinRenderActionP * action;
    bool enabled;
    ~BasisTraceScope() {
      if (enabled)
        std::fprintf(stderr, "COIN_RENDER_PHASE capture_camera_basis calls=%zu prepares=%zu reuse=%zu prepare_ms=%.6f\n",
          action->captureCameraBasisCalls, action->captureCameraBasisPrepares,
          action->captureCameraBasisReuses, action->captureCameraBasisPrepareMs);
    }
  } basisTrace{this, CoinRenderDiagnosticShell::phaseTracingEnabled()};
  struct CaptureScope { bool & capturing; ~CaptureScope() { capturing = false; } } scope{this->capturingTranslations};
  this->cameraSensor.detach();
  this->candidateCamera = NULL;
  this->cachedCamera = NULL;
  this->cameraOverlayBasis = CoinRenderCameraOverlayBasis();
  this->cameraOnlyDirty = false;
  this->cameraPatchInvalidated = false;
  this->cameraRecaptureRequired = false;
  this->translationInputDirty = false;
  this->translationInvalidated = false;
  if (!root || root->getTypeId() != SoSeparator::getClassTypeId()) return;
  SoSeparator * group = static_cast<SoSeparator *>(root);
  if (group->getNumChildren() < 2) return;
  SoNode * first = group->getChild(0);
  const SoType type = first->getTypeId();
  if (type != SoPerspectiveCamera::getClassTypeId() &&
      type != SoOrthographicCamera::getClassTypeId()) return;
  SoCamera * camera = static_cast<SoCamera *>(first);
  if (camera->getStereoMode() != SoCamera::MONOSCOPIC) return;
  if (camera->viewportMapping.getValue() != SoCamera::ADJUST_CAMERA &&
      camera->viewportMapping.getValue() != SoCamera::LEAVE_ALONE) return;
  // Object animation must not pay for a scene-wide camera qualification after
  // every capture. Keep only this cheap candidate/sensor until the camera moves.
  this->candidateCamera = camera;
  this->cameraSensor.attach(root);
  const CoinRenderCameraOverlayBasis * capturedBasis = NULL;
  if (qualifyCamera && cameraStableScene(root, camera)) {
    ++this->captureCameraBasisPrepares;
    const auto before = basisTrace.enabled ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point();
    const bool prepared = CoinRenderFrameReuseCore::prepareCameraOverlayBasis(
      this->lastValidPlan, this->cameraOverlayBasis);
    if (basisTrace.enabled)
      this->captureCameraBasisPrepareMs += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - before).count();
    if (prepared) {
      this->cachedCamera = camera;
      capturedBasis = &this->cameraOverlayBasis;
    }
  }
  this->qualifyTranslationCapture(root, capturedBasis);
}

namespace {
bool translationFieldsStable(const SoNode * node)
{
  const SoFieldData * fields = node->getFieldData();
  for (int i = 0; fields && i < fields->getNumFields(); ++i) {
    const SoField * field = fields->getField(node, i);
    if (field->isConnected() || field->isIgnored()) return false;
  }
  return true;
}
bool objectOverlayEnabled(const char * name) {
  const char * value = std::getenv(name);
  return !(value && std::strcmp(value, "1") == 0);
}
bool opaqueMaterialSnapshot(const SoMaterial * node, CoinRenderMaterialSnapshot & result) {
  if (node->getNodeType() != SoNode::INVENTOR || node->isOverride() || !translationFieldsStable(node) ||
      node->ambientColor.getNum() != 1 || node->diffuseColor.getNum() != 1 ||
      node->specularColor.getNum() != 1 || node->emissiveColor.getNum() != 1 || node->shininess.getNum() != 1 ||
      node->transparency.getNum() != 1 || node->transparency[0] != 0) return false;
  CoinRenderMaterialSnapshot candidate;
  const SbColor colors[] = {node->ambientColor[0], node->diffuseColor[0], node->specularColor[0], node->emissiveColor[0]};
  float * output[] = {candidate.ambient, candidate.diffuse, candidate.specular, candidate.emission};
  for (int color = 0; color < 4; ++color) for (int axis = 0; axis < 3; ++axis) {
    const float value = colors[color][axis];
    if (!std::isfinite(value) || value < 0 || value > 1) return false;
    output[color][axis] = value;
  }
  candidate.shininess = node->shininess[0];
  if (!std::isfinite(candidate.shininess) || candidate.shininess < 0 || candidate.shininess > 1) return false;
  result = candidate;
  return true;
}
bool cubeDimensions(const SoCube * node, SbVec3f & result) {
  const SbVec3f value(node->width.getValue(), node->height.getValue(), node->depth.getValue());
  for (int axis = 0; axis < 3; ++axis)
    if (!std::isfinite(value[axis]) || value[axis] < .0001f || value[axis] > 32768) return false;
  result = value;
  return true;
}
}

void CoinRenderActionP::clearTranslationProof()
{
  this->translationProofValid = false;
  this->translationProofRevision = this->translationProofGeneration = 0;
  this->translationBindings.clear();
  this->translationByNode.clear();
  this->translationDirty.clear();
  this->materialDirty.clear(); this->geometryDirty.clear();
  this->materialByNode.clear(); this->cubeByNode.clear();
  if (this->translationGeneration != std::numeric_limits<uint64_t>::max())
    ++this->translationGeneration;
}

void CoinRenderActionP::beginTranslationCapture(bool enabled)
{
  this->clearTranslationProof();
  this->translationCapture.clear();
  this->translationCaptureByNode.clear();
  this->capturedDrawSources.clear(); this->pendingCaptureShape = NULL;
  this->translationShapeCandidate = SIZE_MAX;
  const char * disabled = std::getenv("COIN_RENDER_DISABLE_TRANSLATION_OVERLAY");
  this->capturingTranslations = enabled && this->translationGeneration != std::numeric_limits<uint64_t>::max() &&
    !(disabled && std::strcmp(disabled, "1") == 0);
  this->translationCaptureInvalid = false;
}

SoCallbackAction::Response CoinRenderActionP::translationPreCB(
  void * data, SoCallbackAction * action, const SoNode * node)
{
  auto * self = static_cast<CoinRenderActionP *>(data);
  if (!self->capturingTranslations || self->translationCaptureInvalid) return SoCallbackAction::CONTINUE;
  // Internal inherited pre/post observers make the shape lists non-single.
  // The registration revision covers every callback kind, including tails;
  // only the unchanged complete internal registration can be skipped later.
  if (self->master->callbackRegistrationRevision() != self->captureCallbackRevision) {
    self->translationCaptureInvalid = true;
    return SoCallbackAction::CONTINUE;
  }
  const SoType type = node->getTypeId();
  if (type != SoTransform::getClassTypeId() && type != SoTranslation::getClassTypeId())
    return SoCallbackAction::CONTINUE;
  const SoPath * path = action->getCurPath();
  if (path->getLength() < 2) return SoCallbackAction::CONTINUE;
  SoNode * parentNode = path->getNodeFromTail(1);
  if (parentNode->getTypeId() != SoSeparator::getClassTypeId()) return SoCallbackAction::CONTINUE;
  auto * parent = static_cast<SoSeparator *>(parentNode);
  if (parent->getNumChildren() != 3 || parent->getRefCount() != 1 || node->getRefCount() != 1 ||
      parent->getChild(2)->getTypeId() != SoCube::getClassTypeId() ||
      !translationFieldsStable(parent) || !translationFieldsStable(node) ||
      !translationFieldsStable(parent->getChild(2))) return SoCallbackAction::CONTINUE;
  SoNode * material = parent->getChild(0) == node ? parent->getChild(1) : parent->getChild(0);
  if ((parent->getChild(0) != node && parent->getChild(1) != node) ||
      material->getTypeId() != SoMaterial::getClassTypeId() || !translationFieldsStable(material) ||
      typeid(*parent->getChild(2)) != typeid(SoCube)) return SoCallbackAction::CONTINUE;
  if (self->translationCapture.size() == 65536 || self->translationCaptureByNode.count(node)) {
    self->translationCaptureInvalid = true;
    return SoCallbackAction::CONTINUE;
  }
  TranslationBinding binding;
  binding.transform = const_cast<SoNode *>(node);
  binding.parent = parent;
  binding.cube = parent->getChild(2);
  binding.material = static_cast<SoMaterial *>(material);
  const auto * cube = static_cast<const SoCube *>(binding.cube);
  binding.cubeDimensions.setValue(cube->width.getValue(), cube->height.getValue(), cube->depth.getValue());
  if (type == SoTransform::getClassTypeId()) {
    auto * transform = static_cast<SoTransform *>(binding.transform);
    if (transform->center.getValue() != SbVec3f(0,0,0)) return SoCallbackAction::CONTINUE;
    binding.field = &transform->translation;
  } else binding.field = &static_cast<SoTranslation *>(binding.transform)->translation;
  binding.originalPosition = binding.field->getValue();
  binding.prefix = action->getModelMatrix();
  self->translationCaptureByNode[node] = self->translationCapture.size();
  self->translationCapture.push_back(binding);
  return SoCallbackAction::CONTINUE;
}

void CoinRenderActionP::beginTranslationShape(SoCallbackAction * action, const SoNode * node)
{
  this->translationShapeCandidate = SIZE_MAX;
  if (this->capturingTranslations && !this->translationCaptureInvalid) {
    this->pendingCaptureShape = node;
    this->firstCaptureDraw = this->builder.capturedDrawCount();
  }
  if (!this->capturingTranslations || this->translationCaptureInvalid ||
      node->getTypeId() != SoCube::getClassTypeId()) return;
  const SoPath * path = action->getCurPath();
  if (path->getLength() < 2) return;
  SoNode * parent = path->getNodeFromTail(1);
  if (parent->getTypeId() != SoSeparator::getClassTypeId()) return;
  auto * group = static_cast<SoSeparator *>(parent);
  if (group->getNumChildren() != 3 || group->getChild(2) != node) return;
  for (int child = 0; child < 2; ++child) {
    const auto found = this->translationCaptureByNode.find(group->getChild(child));
    if (found == this->translationCaptureByNode.end()) continue;
    auto & binding = this->translationCapture[found->second];
    if (binding.parent != parent || binding.cube != node) continue;
    this->translationShapeCandidate = found->second;
    binding.firstDraw = this->builder.capturedDrawCount();
    SoState * state = action->getState();
    binding.overallMaterial = SoMaterialBindingElement::get(state) == SoMaterialBindingElement::OVERALL &&
      !binding.material->isOverride() && !SoOverrideElement::getAmbientColorOverride(state) &&
      !SoOverrideElement::getDiffuseColorOverride(state) && !SoOverrideElement::getSpecularColorOverride(state) &&
      !SoOverrideElement::getEmissiveColorOverride(state) && !SoOverrideElement::getShininessOverride(state) &&
      !SoOverrideElement::getTransparencyOverride(state);
    return;
  }
}

void CoinRenderActionP::endTranslationShape()
{
  if (this->pendingCaptureShape) {
    const size_t count = this->builder.capturedDrawCount();
    if (count > 65536) this->translationCaptureInvalid = true;
    else {
      this->capturedDrawSources.resize(count, NULL);
      for (size_t draw = this->firstCaptureDraw; draw < count; ++draw)
        this->capturedDrawSources[draw] = this->pendingCaptureShape;
    }
    this->pendingCaptureShape = NULL;
  }
  if (this->translationShapeCandidate != SIZE_MAX) {
    auto & binding = this->translationCapture[this->translationShapeCandidate];
    binding.endDraw = this->builder.capturedDrawCount();
    if (binding.endDraw == binding.firstDraw + 1)
      binding.geometry = this->builder.capturedDraw(binding.firstDraw)->geometry;
    this->translationShapeCandidate = SIZE_MAX;
  }
}

void CoinRenderActionP::qualifyTranslationCapture(
  SoNode * root, const CoinRenderCameraOverlayBasis * capturedBasis)
{
  CoinRenderPhaseTimer timer("object_qualification");
  struct CaptureScope { bool & capturing; ~CaptureScope() { capturing = false; } } scope{this->capturingTranslations};
  std::unordered_map<const SoNode *, size_t> materialVisits;
  if (!this->capturingTranslations || this->translationCaptureInvalid || this->translationCapture.empty() ||
      this->master->callbackRegistrationRevision() != this->captureCallbackRevision ||
      !root || !this->candidateCamera || this->lastValidPlan.renderStates.size() > 65536 ||
      this->lastValidPlan.draws.size() > 65536 ||
      !cameraStableScene(root, this->candidateCamera, true, &materialVisits)) return;
  timer.mark("scene_profile");
  const auto & plan = this->lastValidPlan;
  // The strict object scene profile above must always run, independently of
  // camera admission. This lens exists only within rememberFrameRoot: neither
  // a retained basis nor matching revisions in later captures are a license.
  bool reuseBasis = objectOverlayEnabled("COIN_RENDER_DISABLE_CAPTURE_CAMERA_BASIS_REUSE") &&
    capturedBasis && capturedBasis->owner == &plan && capturedBasis->revision == plan.revision &&
    capturedBasis->stateCount == plan.renderStates.size() &&
    capturedBasis->referenceLighting.size() == plan.lightingStates.size() &&
    capturedBasis->worldLighting.size() == plan.lightingStates.size();
  for (size_t i = 0; reuseBasis && i < plan.lightingStates.size(); ++i)
    reuseBasis = capturedBasis->referenceLighting[i].lights.size() == plan.lightingStates[i].lights.size() &&
      capturedBasis->worldLighting[i].lights.size() == plan.lightingStates[i].lights.size();
  CoinRenderCameraOverlayBasis profile;
  if (reuseBasis) ++this->captureCameraBasisReuses;
  else {
    ++this->captureCameraBasisPrepares;
    const bool trace = CoinRenderDiagnosticShell::phaseTracingEnabled();
    const auto before = trace ? std::chrono::steady_clock::now()
                              : std::chrono::steady_clock::time_point();
    const bool prepared = CoinRenderFrameReuseCore::prepareCameraOverlayBasis(plan, profile);
    if (trace)
      this->captureCameraBasisPrepareMs += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - before).count();
    if (!prepared) return;
  }
  for (const auto & state : plan.renderStates) {
    if (state.materialSlot >= plan.materials.size() || state.hasTexture ||
        state.transparentMaterial || state.transparentTexture || state.screenDoorTransparency > 0 ||
        !state.clipPlanesWorld.empty() || state.fogMode != CoinRenderFogMode::NONE ||
        state.polygonOffsetEnabled || state.polygonLinePattern || state.shadowGroupSlot ||
        plan.materials[state.materialSlot].transparency != 0 || plan.materials[state.materialSlot].diffuse[3] < 1) return;
    for (const auto & texture : state.extraTextures) if (texture.enabled) return;
  }
  for (const auto & draw : plan.draws)
    if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST || draw.renderLayer ||
        draw.clearDepthBefore || draw.shadowLightSlot || draw.lineStripId) return;
  timer.mark("frame_profile");
  std::vector<size_t> owners(plan.renderStates.size(), SIZE_MAX);
  std::vector<size_t> drawOwners(plan.draws.size(), SIZE_MAX);
  for (size_t object = 0; object < this->translationCapture.size(); ++object) {
    auto & binding = this->translationCapture[object];
    // One native Cube packet and one state make ownership explicit, including
    // aliases produced by model/state interning. Shared Cube geometry is fine.
    if (binding.endDraw != binding.firstDraw + 1 || binding.endDraw > plan.draws.size() ||
        binding.field->getValue() != binding.originalPosition) return;
    const auto & draw = plan.draws[binding.firstDraw];
    // A later occurrence may have been merged into this packet. Its captured
    // range must still describe exactly this Cube, before assigning ownership.
    if (draw.geometry.firstVertex != binding.geometry.firstVertex || draw.geometry.vertexCount != binding.geometry.vertexCount ||
        draw.geometry.firstIndex != binding.geometry.firstIndex || draw.geometry.indexCount != binding.geometry.indexCount ||
        draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        draw.renderLayer || draw.clearDepthBefore || draw.shadowLightSlot || draw.lineStripId ||
        draw.renderStateSlot >= plan.renderStates.size() || owners[draw.renderStateSlot] != SIZE_MAX) return;
    binding.stateSlot = draw.renderStateSlot;
    binding.anchor = plan.renderStates[binding.stateSlot].model;
    SbMatrix qualified;
    if (!CoinRenderFrameReuseCore::translatedModel(binding.anchor, binding.prefix, binding.originalPosition, qualified) ||
        std::memcmp(qualified.getValue(), binding.anchor.getValue(), sizeof(float) * 16) != 0) return;
    if (draw.hasSortingCenter) {
      SbVec3f center;
      binding.anchor.multVecMatrix(SbVec3f(0, 0, 0), center);
      if (std::memcmp(center.getValue(), draw.sortingCenterWorld, sizeof(float) * 3) != 0) return;
    }
    owners[binding.stateSlot] = object;
    drawOwners[binding.firstDraw] = object;
  }
  for (size_t draw = 0; draw < plan.draws.size(); ++draw)
    if (owners[plan.draws[draw].renderStateSlot] != SIZE_MAX &&
        owners[plan.draws[draw].renderStateSlot] != drawOwners[draw]) return;
  this->translationBindings.swap(this->translationCapture);
  this->translationByNode.swap(this->translationCaptureByNode);
  this->translationDirty.clear();
  this->translationProofGeneration = this->translationGeneration;
  this->translationProofRevision = plan.revision;
  this->translationProofValid = true;
  timer.mark("ownership");
  this->qualifyObjectPayloads(materialVisits);
  timer.mark("payload");
}

void CoinRenderActionP::qualifyObjectPayloads(const std::unordered_map<const SoNode *, size_t> & materialVisits)
{
  const auto & plan = this->lastValidPlan;
  if (plan.materials.size() > 65536 || this->capturedDrawSources.size() != plan.draws.size()) return;
  // All proofs below are local to this immutable qualification. There is no
  // revision-, pointer- or node-based reuse across captures. Ownership and
  // syntactic occurrence checks still consume every draw/binding.
  const bool memoEnabled = objectOverlayEnabled("COIN_RENDER_DISABLE_OBJECT_PROOF_MEMOIZATION");
  constexpr size_t maxMemoEntries = 1024;
  // Three hash caches charge at most 128 bytes per admitted entry, plus one
  // byte per material slot: <=448 KiB optional temporary logical metadata.
  struct IndexKey {
    uint32_t first, count, material;
    bool operator==(const IndexKey & other) const {
      return first == other.first && count == other.count && material == other.material;
    }
  };
  struct IndexHash {
    size_t operator()(const IndexKey & key) const {
      return (size_t(key.first)*16777619u ^ key.count)*16777619u ^ key.material;
    }
  };
  struct VertexKey {
    uint32_t first, count;
    const SoNode * cube;
    bool operator==(const VertexKey & other) const {
      return first == other.first && count == other.count && cube == other.cube;
    }
  };
  struct VertexHash {
    size_t operator()(const VertexKey & key) const {
      return (std::hash<const SoNode *>()(key.cube)*16777619u ^ key.first)*16777619u ^ key.count;
    }
  };
  struct Dimensions { bool valid; SbVec3f value; };
  std::unordered_set<IndexKey, IndexHash> checkedIndices;
  std::unordered_map<const SoNode *, Dimensions> checkedDimensions;
  std::unordered_map<VertexKey, bool, VertexHash> checkedVertices;
  bool indexMemoEnabled = memoEnabled, dimensionsMemoEnabled = memoEnabled, vertexMemoEnabled = memoEnabled;
  std::vector<uint8_t> checkedMaterials;
  if (memoEnabled) {
    try { checkedMaterials.resize(plan.materials.size(), 0); }
    catch (const std::bad_alloc &) {} // Optional acceleration; ordinary proofs still run.
  }
  size_t materialChecks = 0, materialHits = 0, indexChecks = 0, indexHits = 0;
  size_t dimensionsChecks = 0, dimensionsHits = 0, vertexChecks = 0, vertexHits = 0;
  std::vector<size_t> drawOwners(plan.draws.size(), SIZE_MAX);
  for (size_t i = 0; i < this->translationBindings.size(); ++i)
    drawOwners[this->translationBindings[i].firstDraw] = i;
  std::vector<const SoNode *> materialOwners(plan.materials.size(), NULL);
  std::vector<bool> materialConflicts(plan.materials.size(), false);
  for (size_t draw = 0; draw < plan.draws.size(); ++draw) {
    const auto & packet = plan.draws[draw];
    const uint32_t slot = plan.renderStates[packet.renderStateSlot].materialSlot;
    const SoNode * owner = drawOwners[draw] == SIZE_MAX ? NULL : this->translationBindings[drawOwners[draw]].material;
    if (!owner) materialConflicts[slot] = true;
    else if (!materialOwners[slot]) materialOwners[slot] = owner;
    else if (materialOwners[slot] != owner) materialConflicts[slot] = true;
    // An exact consumed span/expected-slot proof can be shared, including its
    // conflict side effects. A mismatching span must record EVERY other slot.
    const IndexKey key{packet.geometry.firstIndex, packet.geometry.indexCount, slot};
    if (indexMemoEnabled && checkedIndices.count(key)) ++indexHits;
    else {
      ++indexChecks;
      for (uint32_t index = 0; index < packet.geometry.indexCount; ++index) {
        const uint32_t material = plan.vertices[plan.indices[packet.geometry.firstIndex + index]].materialSlot;
        if (material != slot) { materialConflicts[slot] = true; materialConflicts[material] = true; }
      }
      if (indexMemoEnabled && checkedIndices.size() < maxMemoEntries) {
        try { checkedIndices.insert(key); }
        catch (const std::bad_alloc &) { checkedIndices.clear(); indexMemoEnabled = false; }
      }
    }
  }
  struct Range { uint64_t first, end; size_t draw; const SoNode * cube; };
  std::vector<Range> ranges;
  ranges.reserve(plan.draws.size());
  for (size_t draw = 0; draw < plan.draws.size(); ++draw) {
    const auto & range = plan.draws[draw].geometry;
    ranges.push_back({range.firstVertex, uint64_t(range.firstVertex) + range.vertexCount, draw,
      drawOwners[draw] == SIZE_MAX ? NULL : this->translationBindings[drawOwners[draw]].cube});
  }
  const auto materialMatches = [&](uint32_t slot, const SoMaterial * source) {
    if (!checkedMaterials.empty() && checkedMaterials[slot]) {
      ++materialHits; return checkedMaterials[slot] == 1;
    }
    ++materialChecks;
    CoinRenderMaterialSnapshot snapshot;
    const bool valid = opaqueMaterialSnapshot(source, snapshot) &&
      std::memcmp(&snapshot, &plan.materials[slot], sizeof(snapshot)) == 0;
    if (!checkedMaterials.empty()) checkedMaterials[slot] = valid ? 1 : 2;
    return valid;
  };
  const auto dimensionsFor = [&](const SoNode * source, SbVec3f & dimensions) {
    if (dimensionsMemoEnabled) {
      const auto found = checkedDimensions.find(source);
      if (found != checkedDimensions.end()) {
        ++dimensionsHits;
        if (found->second.valid) dimensions = found->second.value;
        return found->second.valid;
      }
    }
    ++dimensionsChecks;
    const bool valid = cubeDimensions(static_cast<const SoCube *>(source), dimensions);
    if (dimensionsMemoEnabled && checkedDimensions.size() < maxMemoEntries) {
      try { checkedDimensions.emplace(source, Dimensions{valid, valid ? dimensions : SbVec3f(0,0,0)}); }
      catch (const std::bad_alloc &) { checkedDimensions.clear(); dimensionsMemoEnabled = false; }
    }
    return valid;
  };
  const auto verticesMatch = [&](const TranslationBinding & binding, const SbVec3f & dimensions) {
    const VertexKey key{binding.geometry.firstVertex, binding.geometry.vertexCount, binding.cube};
    if (vertexMemoEnabled) {
      const auto found = checkedVertices.find(key);
      if (found != checkedVertices.end()) { ++vertexHits; return found->second; }
    }
    ++vertexChecks;
    bool valid = true;
    for (uint32_t offset = 0; offset < binding.geometry.vertexCount; ++offset) {
      const auto & vertex = plan.vertices[binding.geometry.firstVertex + offset];
      for (int axis = 0; axis < 3; ++axis)
        if (std::abs(vertex.position[axis]) != dimensions[axis] * .5f) valid = false;
      if (vertex.screenSpaceW != 1 || vertex.fogEyeDepth != -1) valid = false;
    }
    if (vertexMemoEnabled && checkedVertices.size() < maxMemoEntries) {
      try { checkedVertices.emplace(key, valid); }
      catch (const std::bad_alloc &) { checkedVertices.clear(); vertexMemoEnabled = false; }
    }
    return valid;
  };
  for (auto & binding : this->translationBindings) {
    const auto & state = plan.renderStates[binding.stateSlot];
    binding.materialEligible = binding.overallMaterial && !materialConflicts[state.materialSlot] &&
      materialOwners[state.materialSlot] == binding.material &&
      materialMatches(state.materialSlot, binding.material);
    SbVec3f dimensions;
    binding.geometryEligible = binding.overallMaterial && binding.geometry.vertexCount == 24 &&
      binding.geometry.indexCount == 36 && dimensionsFor(binding.cube, dimensions) &&
      dimensions == binding.cubeDimensions && verticesMatch(binding, dimensions);
  }
  std::sort(ranges.begin(), ranges.end(), [](const Range & a, const Range & b) { return a.first < b.first; });
  for (size_t begin = 0; begin < ranges.size();) {
    size_t end = begin + 1; uint64_t maximum = ranges[begin].end;
    const SoNode * owner = ranges[begin].cube; bool overlapConflict = !owner;
    while (end < ranges.size() && ranges[end].first < maximum) {
      if (ranges[end].cube != owner || ranges[end].first != ranges[begin].first ||
          ranges[end].end != ranges[begin].end) overlapConflict = true;
      maximum = std::max(maximum, ranges[end].end); ++end;
    }
    if (overlapConflict) for (size_t i = begin; i < end; ++i)
      if (drawOwners[ranges[i].draw] != SIZE_MAX) this->translationBindings[drawOwners[ranges[i].draw]].geometryEligible = false;
    begin = end;
  }
  // A source is admitted only if all of its captured occurrences are admitted.
  std::unordered_set<const SoNode *> rejectedMaterials, rejectedCubes;
  std::unordered_set<const SoNode *> capturedCubes;
  std::unordered_map<const SoNode *, size_t> materialOccurrences;
  for (const auto & binding : this->translationBindings) {
    capturedCubes.insert(binding.cube);
    ++materialOccurrences[binding.material];
  }
  for (size_t draw = 0; draw < plan.draws.size(); ++draw) if (drawOwners[draw] == SIZE_MAX) {
    if (capturedCubes.count(this->capturedDrawSources[draw])) rejectedCubes.insert(this->capturedDrawSources[draw]);
  }
  for (const auto & binding : this->translationBindings) {
    // Unmapped syntax can use this material under partial overrides, creating
    // a different table slot which still depends on some fields of this source.
    // Require every occurrence in this root to belong to an isolated object.
    const auto visits = materialVisits.find(binding.material);
    if (!binding.materialEligible || visits == materialVisits.end() || visits->second != materialOccurrences[binding.material])
      rejectedMaterials.insert(binding.material);
    if (!binding.geometryEligible) rejectedCubes.insert(binding.cube);
  }
  // Options are constant during this qualification, but can change before the
  // next capture. Avoid two CRT environment scans for every object on Windows.
  const bool materialOverlayEnabled = objectOverlayEnabled("COIN_RENDER_DISABLE_MATERIAL_OVERLAY");
  const bool cubeOverlayEnabled = objectOverlayEnabled("COIN_RENDER_DISABLE_CUBE_OVERLAY");
  for (size_t i = 0; i < this->translationBindings.size(); ++i) {
    const auto & binding = this->translationBindings[i];
    if (!rejectedMaterials.count(binding.material) && materialOverlayEnabled)
      this->materialByNode[binding.material].push_back(i);
    if (!rejectedCubes.count(binding.cube) && cubeOverlayEnabled)
      this->cubeByNode[binding.cube].push_back(i);
  }
  // Conservative accounting includes source-map nodes/buckets and occurrence
  // indices. The existing transform proof has its independent 65,536 cap.
  const size_t metadataBytes = this->translationBindings.size() * 80 +
    (this->materialByNode.size() + this->cubeByNode.size()) * 128;
  if (metadataBytes > 16 * 1024 * 1024) { this->materialByNode.clear(); this->cubeByNode.clear(); }
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE object_proof_cache memo_enabled=%d material_checks=%zu material_hits=%zu index_checks=%zu index_hits=%zu dimensions_checks=%zu dimensions_hits=%zu vertex_checks=%zu vertex_hits=%zu cached_index_spans=%zu cached_sources=%zu cached_vertex_spans=%zu\n",
      memoEnabled ? 1 : 0, materialChecks, materialHits, indexChecks, indexHits,
      dimensionsChecks, dimensionsHits, vertexChecks, vertexHits,
      checkedIndices.size(), checkedDimensions.size(), checkedVertices.size());
}

bool CoinRenderActionP::prepareTranslationOverlay(SoNode * root, CoinRenderObjectOverlayUndo & undo)
{
  const char * disabled = std::getenv("COIN_RENDER_DISABLE_TRANSLATION_OVERLAY");
  if ((disabled && std::strcmp(disabled, "1") == 0) || !this->translationProofValid ||
      this->translationInvalidated || !this->translationInputDirty ||
      (this->translationDirty.empty() && this->materialDirty.empty() && this->geometryDirty.empty()) ||
      this->cameraOnlyDirty || root != this->cachedRoot || this->cameraSensor.getAttachedNode() != root ||
      this->translationProofGeneration != this->translationGeneration || !this->translationProofRevision) return false;
  CoinRenderPhaseTimer updateTimer("object_update_prepare");
  std::vector<CoinRenderModelUpdate> updates;
  updates.reserve(this->translationDirty.size());
  for (size_t index : this->translationDirty) {
    if (index >= this->translationBindings.size()) return false;
    const auto & binding = this->translationBindings[index];
    CoinRenderModelUpdate update;
    update.stateSlot = binding.stateSlot;
    if (this->lastValidPlan.draws[binding.firstDraw].hasSortingCenter)
      update.sortingDrawSlot = static_cast<uint32_t>(binding.firstDraw);
    if (binding.field->isConnected() || binding.field->isIgnored() ||
        !CoinRenderFrameReuseCore::translatedModel(binding.anchor, binding.prefix, binding.field->getValue(), update.model)) return false;
    updates.push_back(update);
  }
  updateTimer.mark("models");
  std::vector<CoinRenderMaterialUpdate> materials;
  std::unordered_set<uint32_t> materialSlots;
  if (!this->materialDirty.empty() && !objectOverlayEnabled("COIN_RENDER_DISABLE_MATERIAL_OVERLAY")) return false;
  for (const SoNode * node : this->materialDirty) {
    const auto found = this->materialByNode.find(node);
    CoinRenderMaterialSnapshot snapshot;
    if (found == this->materialByNode.end() || !opaqueMaterialSnapshot(static_cast<const SoMaterial *>(node), snapshot)) return false;
    for (size_t object : found->second) {
      const uint32_t slot = this->lastValidPlan.renderStates[this->translationBindings[object].stateSlot].materialSlot;
      if (materialSlots.insert(slot).second) materials.push_back({slot, snapshot});
    }
  }
  updateTimer.mark("materials");
  std::vector<CoinRenderPositionUpdate> positions;
  std::vector<CoinRenderCubeRangeUpdate> cubeRanges;
  std::vector<CoinRenderDrawSourceUpdate> draws;
  const bool reserveUpdates = objectOverlayEnabled("COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE");
  const bool compactCubes = objectOverlayEnabled("COIN_RENDER_DISABLE_CUBE_RANGE_OVERLAY");
  // The admitted Cube profile has 24 vertices. Estimate from dirty sources,
  // bounded by the existing position/draw limits. Shared sources may require
  // more ranges; normal growth remains available without a second proof pass.
  const size_t estimatedPositions = std::min(this->geometryDirty.size(),
    COIN_RENDER_OBJECT_OVERLAY_MAX_POSITIONS / size_t(24)) * size_t(24);
  const auto growthCapacity = [](size_t estimate, size_t limit) {
    size_t capacity = estimate ? 1 : 0;
    while (capacity < estimate && capacity <= limit / 2) capacity *= 2;
    return capacity < estimate ? limit : capacity;
  };
  // Keep the literal growth classes: an underestimated aliased range must not
  // double an arbitrary reserved capacity beyond the existing memory bound.
  if (reserveUpdates && !compactCubes) {
    try { positions.reserve(growthCapacity(estimatedPositions, COIN_RENDER_OBJECT_OVERLAY_MAX_POSITIONS)); } catch (const std::bad_alloc &) {}
  }
  if (reserveUpdates && compactCubes) {
    try { cubeRanges.reserve(growthCapacity(this->geometryDirty.size(), 65536)); } catch (const std::bad_alloc &) {}
  }
  if (reserveUpdates) {
    try { draws.reserve(growthCapacity(std::min(this->geometryDirty.size(), this->translationBindings.size()), 65536)); }
    catch (const std::bad_alloc &) {}
  }
  const size_t initialPositionCapacity = positions.capacity(), initialDrawCapacity = draws.capacity();
  // Admitted overlapping ranges are exact aliases of the same Cube source.
  // Deduplicate 24-vertex ranges, avoiding a hash allocation for every vertex.
  std::unordered_set<uint32_t> vertexRanges;
  if (!this->geometryDirty.empty() && !objectOverlayEnabled("COIN_RENDER_DISABLE_CUBE_OVERLAY")) return false;
  for (const SoNode * node : this->geometryDirty) {
    const auto found = this->cubeByNode.find(node); SbVec3f dimensions;
    if (found == this->cubeByNode.end() || !translationFieldsStable(node) ||
        !cubeDimensions(static_cast<const SoCube *>(node), dimensions)) return false;
    for (size_t object : found->second) {
      const auto & binding = this->translationBindings[object];
      if (binding.material->isOverride() || binding.material->getNodeType() != SoNode::INVENTOR) return false;
      draws.push_back({static_cast<uint32_t>(binding.firstDraw), node->getNodeId()});
      if (!vertexRanges.insert(binding.geometry.firstVertex).second) continue;
      if (compactCubes) {
        CoinRenderCubeRangeUpdate range;
        range.firstVertex = binding.geometry.firstVertex;
        range.vertexCount = binding.geometry.vertexCount;
        range.halfExtents = dimensions * .5f;
        cubeRanges.push_back(range);
      } else {
        for (uint32_t offset = 0; offset < binding.geometry.vertexCount; ++offset) {
          const uint32_t slot = binding.geometry.firstVertex + offset;
          if (positions.size() == COIN_RENDER_OBJECT_OVERLAY_MAX_POSITIONS) return false;
          const auto & previous = this->lastValidPlan.vertices[slot];
          SbVec3f position;
          for (int axis = 0; axis < 3; ++axis) position[axis] = std::copysign(dimensions[axis] * .5f, previous.position[axis]);
          positions.push_back({slot, position});
        }
      }
    }
  }
  updateTimer.mark("geometry");
  if (CoinRenderDiagnosticShell::phaseTracingEnabled())
    std::fprintf(stderr, "COIN_RENDER_PHASE object_update_storage reserve=%d positions=%zu estimated_positions=%zu position_capacity=%zu position_bytes=%zu draws=%zu draw_capacity=%zu draw_bytes=%zu initial_position_capacity=%zu initial_draw_capacity=%zu position_grew=%d draw_grew=%d\n",
      reserveUpdates ? 1 : 0, positions.size(), estimatedPositions, positions.capacity(),
      positions.capacity() * sizeof(CoinRenderPositionUpdate), draws.size(), draws.capacity(),
      draws.capacity() * sizeof(CoinRenderDrawSourceUpdate), initialPositionCapacity, initialDrawCapacity,
      positions.capacity() != initialPositionCapacity, draws.capacity() != initialDrawCapacity);
  const uint64_t revision = CoinRenderFramePlanBuilder::nextRevision();
  bool applied = compactCubes && CoinRenderFrameReuseCore::beginCubeObjectOverlay(
    this->lastValidPlan, updates, materials, cubeRanges, draws, revision, undo);
  if (!applied) {
    if (compactCubes) {
      if (reserveUpdates) {
        try { positions.reserve(growthCapacity(estimatedPositions, COIN_RENDER_OBJECT_OVERLAY_MAX_POSITIONS)); }
        catch (const std::bad_alloc &) {}
      }
      for (const auto & range : cubeRanges)
        for (uint32_t offset = 0; offset < range.vertexCount; ++offset) {
          const size_t slot = size_t(range.firstVertex) + offset;
          if (slot > UINT32_MAX || slot >= this->lastValidPlan.vertices.size() ||
              positions.size() == COIN_RENDER_OBJECT_OVERLAY_MAX_POSITIONS) return false;
          const auto & previous = this->lastValidPlan.vertices[slot];
          SbVec3f position;
          for (int axis = 0; axis < 3; ++axis)
            position[axis] = std::copysign(range.halfExtents[axis], previous.position[axis]);
          positions.push_back({static_cast<uint32_t>(slot), position});
        }
    }
    applied = CoinRenderFrameReuseCore::beginObjectOverlay(
      this->lastValidPlan, updates, materials, positions, draws, revision, undo);
  }
  updateTimer.mark("overlay");
  return applied;
}

void CoinRenderActionP::commitTranslationOverlay()
{
  this->translationDirty.clear();
  this->materialDirty.clear(); this->geometryDirty.clear();
  this->translationInputDirty = false;
  this->translationInvalidated = false;
  this->cameraPatchInvalidated = false;
  this->cachedCamera = NULL;
  this->cameraOverlayBasis = CoinRenderCameraOverlayBasis();
  this->cameraRecaptureRequired = true;
}

bool
CoinRenderActionP::prepareCameraOverlay(SoNode * root,
                                         CoinRenderCameraOverlayUndo & undo)
{
  const char * disabled = std::getenv("COIN_RENDER_DISABLE_CAMERA_OVERLAY");
  if ((disabled && std::strcmp(disabled, "1") == 0) ||
      !root || root != this->cachedRoot || !this->candidateCamera || this->cameraRecaptureRequired ||
      this->cameraSensor.getAttachedNode() != root ||
      !this->cameraOnlyDirty || this->cameraPatchInvalidated ||
      static_cast<SoGroup *>(root)->getChild(0) != this->candidateCamera) return false;

  SbViewportRegion adjusted;
  const SbViewVolume vv = this->candidateCamera->getViewVolume(
    this->master->getViewportRegion(), adjusted, SbMatrix::identity());
  if (adjusted != this->master->getViewportRegion()) return false;
  CoinRenderCameraSnapshot snapshot;
  if (vv.getDepth() == 0.0f || vv.getWidth() == 0.0f ||
      vv.getHeight() == 0.0f) {
    snapshot.viewMatrix = SbMatrix::identity();
    snapshot.projectionMatrixCoin = SbMatrix::identity();
  } else {
    vv.getMatrices(snapshot.viewMatrix, snapshot.projectionMatrixCoin);
  }
  snapshot.isPerspective = vv.getProjectionType() == SbViewVolume::PERSPECTIVE;
  snapshot.nearDistance = vv.getNearDist();
  snapshot.farDistance = vv.getNearDist() + vv.getDepth();
  if (snapshot.isPerspective && snapshot.nearDistance <= 0.0f)
    snapshot.nearDistance = 0.1f;
  if (snapshot.farDistance <= snapshot.nearDistance)
    snapshot.farDistance = snapshot.nearDistance + 100.0f;
  snapshot.focalDistance = this->candidateCamera->focalDistance.getValue();
  snapshot.aspectRatio = adjusted.getViewportAspectRatio();
  if (this->cameraPatchInvalidated ||
      this->cameraSensor.getAttachedNode() != root) return false;
  if (!this->cachedCamera) {
    CoinRenderCameraOverlayBasis basis;
    if (!CoinRenderFrameReuseCore::prepareCameraOverlayBasis(this->lastValidPlan, basis) ||
        !cameraStableScene(root, this->candidateCamera) || this->cameraPatchInvalidated ||
        this->cameraSensor.getAttachedNode() != root) return false;
    this->cameraOverlayBasis = std::move(basis);
    this->cachedCamera = this->candidateCamera;
  }
  return CoinRenderFrameReuseCore::beginCameraOverlay(
    this->lastValidPlan, snapshot,
    CoinRenderFramePlanBuilder::nextRevision(), undo, this->cameraOverlayBasis);
}

namespace {
bool appendPassSnapshot(CoinRenderFramePlan & output, const CoinRenderFramePlan & snapshot,
                        unsigned pass, std::string & diagnostic)
{
  if (!snapshot.shadowGroups.empty() || !snapshot.shadowLights.empty() || !snapshot.bumpImages.empty() ||
      !snapshot.rasterOccurrences.empty() || snapshot.hasPortableShaderDraws() || snapshot.smoothing) {
    diagnostic = "Snapshot passes exclude shadow, bump, raster, portable shaders and smoothing"; return false;
  }
  for (const auto & texture : snapshot.textures)
    if (texture.producerId || texture.gpuToken || texture.volumeDepth || texture.cubeFaceSize ||
        (texture.format != CoinRenderTextureFormat::RGBA8_LINEAR && texture.format != CoinRenderTextureFormat::RGBA8_SRGB)) {
      diagnostic = "Snapshot passes support local RGBA2D textures without RTT"; return false;
    }
  std::vector<CoinRenderCompositionItem> order;
  if (!coin_render_composition_order(snapshot, order, diagnostic)) return false;
  for (const auto & item : order)
    if (item.blend || item.deferred || item.screenDoorLevel || item.transparencyStrategy != CoinRenderCompositionItem::OBJECT) {
      diagnostic = "Snapshot passes require opaque ordinary geometry"; return false;
    }
  for (const auto & state : snapshot.renderStates)
    if (state.rasterPixels || state.shadowGroupSlot) {
      diagnostic = "Snapshot passes exclude raster and spatial shader states"; return false;
    }
  uint64_t bytes = uint64_t(output.vertices.size() + snapshot.vertices.size()) * sizeof(CoinRenderVertexSnapshot) +
    uint64_t(output.indices.size() + snapshot.indices.size()) * sizeof(uint32_t) +
    uint64_t(output.renderStates.size() + snapshot.renderStates.size()) * sizeof(CoinRenderRenderStateSnapshot);
  bytes += uint64_t(output.materials.size() + snapshot.materials.size()) * sizeof(CoinRenderMaterialSnapshot) +
    uint64_t(output.cameras.size() + snapshot.cameras.size()) * sizeof(CoinRenderCameraSnapshot) +
    uint64_t(output.viewports.size() + snapshot.viewports.size()) * sizeof(CoinRenderViewportSnapshot) +
    uint64_t(output.samplers.size() + snapshot.samplers.size()) * sizeof(CoinRenderSamplerSnapshot);
  for (const CoinRenderFramePlan * frame : {static_cast<const CoinRenderFramePlan *>(&output), &snapshot}) {
    for (const auto & texture : frame->textures) bytes += texture.pixelsRgba.size() + texture.mipmapsRgba.size();
    for (const auto & lighting : frame->lightingStates) bytes += lighting.lights.size() * sizeof(CoinRenderLightSourceSnapshot);
  }
  if (bytes > output.multipassBufferBudget) { diagnostic = "Snapshot payload exceeds multipass buffer budget"; return false; }
  const uint32_t vertexBase = static_cast<uint32_t>(output.vertices.size());
  const uint32_t indexBase = static_cast<uint32_t>(output.indices.size());
  const uint32_t materialBase = static_cast<uint32_t>(output.materials.size());
  const uint32_t lightingBase = static_cast<uint32_t>(output.lightingStates.size());
  const uint32_t cameraBase = static_cast<uint32_t>(output.cameras.size());
  const uint32_t viewportBase = static_cast<uint32_t>(output.viewports.size());
  const uint32_t textureBase = static_cast<uint32_t>(output.textures.size());
  const uint32_t samplerBase = static_cast<uint32_t>(output.samplers.size());
  const uint32_t stateBase = static_cast<uint32_t>(output.renderStates.size());
  output.passClearColors.push_back(snapshot.clearColor);
  output.materials.insert(output.materials.end(), snapshot.materials.begin(), snapshot.materials.end());
  output.lightingStates.insert(output.lightingStates.end(), snapshot.lightingStates.begin(), snapshot.lightingStates.end());
  output.cameras.insert(output.cameras.end(), snapshot.cameras.begin(), snapshot.cameras.end());
  output.viewports.insert(output.viewports.end(), snapshot.viewports.begin(), snapshot.viewports.end());
  output.textures.insert(output.textures.end(), snapshot.textures.begin(), snapshot.textures.end());
  output.samplers.insert(output.samplers.end(), snapshot.samplers.begin(), snapshot.samplers.end());
  for (auto vertex : snapshot.vertices) { vertex.materialSlot += materialBase; output.vertices.push_back(vertex); }
  for (uint32_t index : snapshot.indices) output.indices.push_back(index + vertexBase);
  for (auto state : snapshot.renderStates) {
    state.materialSlot += materialBase; state.lightingSlot += lightingBase;
    state.cameraSlot += cameraBase; state.viewportSlot += viewportBase;
    if (state.hasTexture) { state.textureImageSlot += textureBase; state.samplerSlot += samplerBase; }
    for (size_t slot = 0; slot < state.extraTextures.size(); ++slot) {
      auto & unit = state.extraTextures[slot];
      if (unit.enabled) { unit.imageSlot += textureBase; unit.samplerSlot += samplerBase; }
    }
    output.renderStates.push_back(std::move(state));
  }
  for (auto draw : snapshot.draws) {
    draw.geometry.firstVertex += vertexBase; draw.geometry.firstIndex += indexBase;
    draw.renderStateSlot += stateBase; draw.multipassMask = uint16_t(1u << pass);
    draw.stableNodeId = 0; // Snapshot-owned payload; never alias a previous mutable mesh.
    output.draws.push_back(draw);
  }
  return true;
}
}

template <typename F>
void CoinRenderActionP::executePassSnapshots(F traversalFn, SoNode * cacheRoot)
{
  const int count = this->numPasses;
  CoinRenderTarget * const destination = this->target;
  if (count < 1 || count > 16 || !destination || this->asyncTicket || this->planOnly ||
      destination->getOptions().sampleCount != 1 ||
      destination->getPimpl()->kind != CoinRenderTargetP::KIND_OFFSCREEN ||
      this->updateOrigin != SbVec2f(0, 0) || this->updateSize != SbVec2f(1, 1) || this->smoothing ||
      destination->getOptions().transparency != COIN_RENDER_TRANSPARENCY_COIN) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      count < 1 || count > 16 ? CoinRenderAction::INVALID_SCENE : !destination && this->asyncTicket ? CoinRenderAction::NO_TARGET : CoinRenderAction::UNSUPPORTED,
      CoinRenderDiagnosticDomain::ACTION, SbString("Snapshot callbacks require mono synchronous RGBA8 offscreen, one sample and a full update")));
    return;
  }
  auto saved = std::move(this->lastValidPlan);
  const bool hadSaved = this->hasLastValidPlan;
  const bool oldPlanOnly = this->planOnly;
  const auto callback = this->passCallback; void * const userdata = this->passUserdata;
  struct SnapshotScope {
    CoinRenderActionP * owner; CoinRenderFramePlan & saved; bool hadSaved, oldPlanOnly, committed = false;
    ~SnapshotScope() {
      if (!committed) { owner->lastValidPlan = std::move(saved); owner->hasLastValidPlan = hadSaved; }
      owner->planOnly = oldPlanOnly; owner->capturingPassSnapshots = owner->skipSnapshotPreRender = false;
      owner->currentPass = 0; owner->isApplying = false; owner->cachedRoot = nullptr;
      owner->clearTranslationProof();
    }
  } scope{this, saved, hadSaved, oldPlanOnly};
  this->capturingPassSnapshots = true;
  this->hasLastValidPlan = false; this->cachedRoot = nullptr;
  this->planOnly = true;
  try {
    CoinRenderFramePlan combined;
    combined.numPasses = count; combined.multipassBufferBudget = this->multipassBufferBudget;
    combined.textureSamplingPolicy = destination->getOptions().textureSamplingPolicy;
    combined.rasterizationPolicy = destination->getOptions().rasterizationPolicy;
    combined.transparency = this->transparencyOptions;
    combined.transparency.mode = destination->getOptions().transparency;
    combined.delayedObjDepthWrite = this->delayedObjDepthWrite != FALSE;
    for (int pass = 0; pass < count; ++pass) {
      if (pass) {
        this->currentPass = pass - 1;
        this->isApplying = true;
        callback(userdata, this->master);
        this->isApplying = false;
        if (this->hasReentrancyError || this->candidateAborted || this->invalidAbortCode) {
          this->setDiagnostic(CoinRenderDiagnosticShell::action(this->candidateAborted ? CoinRenderAction::ABORTED : CoinRenderAction::INVALID_SCENE,
            CoinRenderDiagnosticDomain::ACTION, SbString(this->invalidAbortCode ? "Abort callback returned an invalid code" : "Snapshot callback aborted or attempted nested apply")));
          return;
        }
      }
      if (this->numPasses != count || this->externalPass || this->target != destination) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
          CoinRenderDiagnosticDomain::ACTION, SbString("Snapshot callback changed target or sample mode"))); return;
      }
      this->currentPass = pass; this->skipSnapshotPreRender = pass != 0;
      if (pass) { CoinRenderFramePlanBuilder fresh; std::swap(this->builder, fresh); }
      this->executeApply(traversalFn, cacheRoot);
      if (this->lastStatus != CoinRenderAction::SUCCESS) return;
      if (this->numPasses != count || this->externalPass || this->target != destination) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
          CoinRenderDiagnosticDomain::ACTION, SbString("Snapshot capture changed target or sample mode"))); return;
      }
      for (const auto & camera : this->frameStereoStates)
        if (camera.mode != static_cast<int>(SoCamera::MONOSCOPIC)) {
          this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
            CoinRenderDiagnosticDomain::ACTION, SbString("Snapshot callbacks require a mono camera"))); return;
        }
      std::string diagnostic;
      if (!appendPassSnapshot(combined, this->lastValidPlan, unsigned(pass), diagnostic)) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(diagnostic.find("budget") != std::string::npos ? CoinRenderAction::OUT_OF_MEMORY : CoinRenderAction::UNSUPPORTED,
          CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(diagnostic.c_str()))); return;
      }
    }
    combined.clearColor = combined.passClearColors.front();
    combined.revision = CoinRenderFramePlanBuilder::nextRevision();
    std::string diagnostic;
    if (!combined.isValid(&diagnostic)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(diagnostic.c_str()))); return;
    }
    const auto result = destination->getPimpl()->executeFrame(combined);
    if (result.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(result)); return;
    }
    this->lastValidPlan = std::move(combined); this->hasLastValidPlan = true;
    this->recordingLogValid = false;
    scope.committed = true;
    this->setDiagnostic(CoinRenderDiagnosticShell::success());
  } catch (const std::bad_alloc &) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::OUT_OF_MEMORY,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Cannot allocate pass snapshots")));
  }
}

template <typename F>
void
CoinRenderActionP::executeApply(F traversalFn, SoNode * cacheRoot)
{
  if (this->isApplying) {
    this->hasReentrancyError = true;
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::ACTION,
      SbString("Nested apply() calls are not permitted on CoinRenderAction")));
    return;
  }
  if (!this->capturingPassSnapshots && !this->planOnly && !this->externalPass && this->passCallback && this->numPasses > 1) {
    this->executePassSnapshots(traversalFn, cacheRoot); return;
  }
  // Nested apply must not erase the outer capture's diagnostic counters.
  if (!this->isApplying) {
    this->captureCameraBasisCalls = this->captureCameraBasisPrepares = this->captureCameraBasisReuses = 0;
    this->captureCameraBasisPrepareMs = 0;
  }
  typedef std::chrono::steady_clock ProfileClock;
  const ProfileClock::time_point profileBegin = ProfileClock::now();
  if (this->target)
    this->executionOptions = this->target->pimpl->options;
  std::string optionsDiagnostic;
  if ((this->target && !this->target->pimpl->optionsDiagnostic.empty()) ||
      !coin_render_valid_options(this->executionOptions, optionsDiagnostic)) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::TARGET,
        SbString(this->target && !this->target->pimpl->optionsDiagnostic.empty()
                     ? this->target->pimpl->optionsDiagnostic.c_str()
                     : optionsDiagnostic.c_str())));
    return;
  }
  if (this->numPasses < 1 || this->numPasses > 16) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      this->numPasses < 1 ? CoinRenderAction::INVALID_SCENE : CoinRenderAction::UNSUPPORTED,
      CoinRenderDiagnosticDomain::ACTION, SbString("Multipass AA requires a requested count in 1..16")));
    return;
  }
  if (!this->externalPass && this->numPasses > 1 && this->multipassBufferBudget == 0) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::OUT_OF_MEMORY,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Multipass AA buffer budget is zero")));
    return;
  }
  const bool tracePhases = CoinRenderDiagnosticShell::phaseTracingEnabled();
  if (this->isApplying) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::ACTION,
        SbString("Nested apply() calls are not permitted on CoinRenderAction")));
    this->hasReentrancyError = true;
    return;
  }

  struct AnnotationScope {
    CoinRenderActionP * action;
    ~AnnotationScope() {
      for (const auto & entry : action->delayedAnnotations) entry.path->unref();
      action->delayedAnnotations.clear();
      for (const auto & entry : action->delayedOverlays) entry.path->unref();
      action->delayedOverlays.clear();
      action->replayingAnnotations = false;
      action->isApplying = false;
      if (!action->externalPass && !action->capturingPassSnapshots) action->currentPass = 0;
      if (action->stateInvalidationPending) action->master->invalidateState();
    }
  } annotationScope{this};
  this->isApplying = true;
  this->hasReentrancyError = false;
  this->candidateAborted = false;
  this->invalidAbortCode = false;
  this->invalidatedDuringApply = false;
  auto validateRequest = [&]() -> bool {
    for (int axis = 0; axis < 2; ++axis) {
      const float origin = this->updateOrigin[axis], size = this->updateSize[axis];
      if (!std::isfinite(origin) || !std::isfinite(size) || origin < 0 || size < 0 ||
          origin > 1 || size > 1 || origin + size > 1.0f) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
          CoinRenderDiagnosticDomain::ACTION, SbString("Invalid normalized update area")));
        return false;
      }
    }
    if (this->externalPass && (this->requestedExternalPass < 0 ||
        this->requestedExternalPass >= this->numPasses || this->numPasses > 16)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::ACTION, SbString("Invalid external pass index/count"))); return false;
    }
    if (this->externalPass && (this->asyncTicket || (this->target &&
        (this->target->getPimpl()->kind != CoinRenderTargetP::KIND_OFFSCREEN || this->target->getOptions().sampleCount != 1 ||
          this->target->getOptions().transparency != COIN_RENDER_TRANSPARENCY_COIN)))) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::ACTION, SbString("External samples require synchronous offscreen one-sample targets"))); return false;
    }
    if (this->delayedRenderType != CoinRenderAction::ONE_PASS &&
        this->delayedRenderType != CoinRenderAction::NONSOLID_SEPARATE_BACKFACE_PASS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::ACTION, SbString("Invalid delayed-object render type")));
      return false;
    }
    if (this->sortingStrategy < CoinRenderAction::BBOX_CENTER ||
        this->sortingStrategy > CoinRenderAction::CUSTOM_CALLBACK ||
        (this->sortingStrategy == CoinRenderAction::CUSTOM_CALLBACK && !this->sortingCallback)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::ACTION, SbString("Invalid object sorting strategy or missing callback")));
      return false;
    }
    if (this->smoothing && (this->asyncTicket || this->numPasses != 1 ||
        (this->target && (this->target->pimpl->kind != CoinRenderTargetP::KIND_OFFSCREEN ||
          this->target->getOptions().sampleCount != 1)))) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::ACTION, SbString("Smoothing requires mono synchronous RGBA8 with one sample/pass")));
      return false;
    }
    const bool partial = this->updateOrigin != SbVec2f(0, 0) || this->updateSize != SbVec2f(1, 1);
    if (partial && (!this->target || this->asyncTicket || this->planOnly ||
        this->target->pimpl->kind != CoinRenderTargetP::KIND_OFFSCREEN ||
        this->target->pimpl->directTextureOutput || this->target->getOptions().sampleCount != 1 ||
        this->numPasses != 1 || this->executionOptions.transparency != COIN_RENDER_TRANSPARENCY_COIN)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::ACTION, SbString("Partial updates require synchronous offscreen RGBA8, one sample/pass and Coin transparency")));
      return false;
    }
    if (this->asyncTicket && (!this->target || this->target->pimpl->kind != CoinRenderTargetP::KIND_OFFSCREEN)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(this->target ? CoinRenderAction::UNSUPPORTED : CoinRenderAction::NO_TARGET,
        CoinRenderDiagnosticDomain::ACTION, SbString("applyAsync requires an offscreen target")));
      return false;
    }
    if (this->target && this->target->getStatus() != CoinRenderTarget::TARGET_READY &&
        this->target->getStatus() != CoinRenderTarget::TARGET_LOST) {
      const CoinRenderTargetP * targetState = &this->target->getPimpl().get();
      // A transient timeout or lost surface leaves the target recoverable.
      // Let the next explicit apply reach executeFrame's prepareBackend
      // path; suspended/invalid targets must still stop before callbacks.
      const bool recoverableTarget =
        (this->target->getStatus() == CoinRenderTarget::TARGET_NOT_READY ||
         this->target->getStatus() == CoinRenderTarget::TARGET_SURFACE_LOST) &&
        !targetState->suspended && targetState->size[0] > 0 && targetState->size[1] > 0 &&
        (targetState->kind != CoinRenderTargetP::KIND_WINDOW ||
          (coin_render_surface_header_diagnostic(targetState->nativeDesc).empty() &&
           coin_render_surface_handles_diagnostic(targetState->nativeDesc).empty()));
      if (!recoverableTarget) {
        this->setDiagnostic(CoinRenderDiagnosticShell::fromTarget(this->target->getStatus(), this->target->getLastError()));
        return false;
      }
    }
    if (this->target) this->executionOptions = this->target->getOptions();
    std::string error;
    if (!coin_render_valid_options(this->executionOptions, error) || this->numPasses < 1 || this->numPasses > 16) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::ACTION, SbString("Invalid current render configuration")));
      return false;
    }
    return true;
  };
  if (!validateRequest()) return;
  const auto callbacks = this->skipSnapshotPreRender ? std::vector<PreRenderCallback>() : this->preRenderCallbacks;
  for (const auto & entry : callbacks) {
    entry.callback(entry.userdata, this->master);
    if (this->hasReentrancyError || this->candidateAborted || this->invalidAbortCode) break;
  }
  if (this->hasReentrancyError || this->invalidAbortCode) return;
  if (this->candidateAborted) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::ABORTED,
      CoinRenderDiagnosticDomain::ACTION, SbString("Render aborted by callback")));
    return;
  }
  if (!validateRequest()) return;
  if (this->stateInvalidationPending) {
    this->isApplying = false;
    this->master->invalidateState();
    this->isApplying = true;
  }
  const bool partialUpdate = this->updateOrigin != SbVec2f(0, 0) || this->updateSize != SbVec2f(1, 1);
  int updateRect[4] = {0, 0, 0, 0};
  if (partialUpdate) {
    const auto canvas = this->target->getSize();
    const auto viewport = this->master->getViewportRegion();
    const auto vpOrigin = viewport.getViewportOriginPixels(), vpSize = viewport.getViewportSizePixels();
    for (int axis = 0; axis < 2; ++axis) {
      const int first = std::max(0, std::max(int(vpOrigin[axis]), int(this->updateOrigin[axis] * canvas[axis])));
      const int last = std::min(int(canvas[axis]), std::min(int(vpOrigin[axis]) + int(vpSize[axis]),
        int(this->updateOrigin[axis] * canvas[axis]) + int(this->updateSize[axis] * canvas[axis])));
      updateRect[axis] = first;
      updateRect[axis + 2] = this->updateSize[axis] == 0 ? 0 : std::max(0, last - first);
    }
    if (!updateRect[2] || !updateRect[3]) {
      this->setDiagnostic(CoinRenderDiagnosticShell::success()); return;
    }
  }
  this->setDiagnostic(CoinRenderDiagnosticShell::success());
  struct RasterHistoryScope {
    CoinRenderFramePlanBuilder & builder;
    CoinRenderFramePlanBuilder::RasterHistory saved;
    bool committed = false;
    explicit RasterHistoryScope(CoinRenderFramePlanBuilder & owner)
      : builder(owner), saved(owner.rasterHistory()) {}
    ~RasterHistoryScope() { if (!committed) builder.restoreRasterHistory(saved); }
  } rasterHistoryScope(this->builder);
  // External callbacks can change capture or have observable side effects.
  // Registration after construction permanently requires their traversal.
  bool planCacheAllowed = this->managerCaptureSection < 0 && !this->externalPass && !this->capturingPassSnapshots && !partialUpdate && !this->abortCallback &&
    this->preRenderCallbacks.empty() && this->sortingStrategy == CoinRenderAction::BBOX_CENTER &&
    this->portableShaderModules.empty() &&
    this->master->callbackRegistrationRevision() == this->captureCallbackRevision &&
    !this->containsVRMLSnapshot(cacheRoot);
  if (planCacheAllowed && cacheRoot && cacheRoot == this->cachedRoot &&
      cacheRoot->getNodeId() == this->cachedRootId) {
    // SoCamera's stereo setters are not fields and do not touch the node.
    // Detect eye/offset changes before a cached plan or camera patch is used.
    for (const auto & saved : this->capturedStereoStates)
      if (saved.mode != static_cast<int>(saved.camera->getStereoMode()) ||
          saved.adjustment != saved.camera->getStereoAdjustment() ||
          saved.balance != saved.camera->getBalanceAdjustment()) {
        planCacheAllowed = false;
        break;
      }
  }

  const bool ownsSceneTexturePlan = !this->sceneTexturePlan;
  if (ownsSceneTexturePlan) {
    this->sceneTexturePlan =
        std::make_shared<CoinRenderRttPlan>(this->executionOptions.sceneTexture);
    this->sceneTextureCaptureMemo = std::make_shared<CoinRenderRttCaptureMemo>();
    this->sceneTextureCaptureMemo->allowed = planCacheAllowed;
    this->sceneTextureCaptureMemo->owner = this;
  }
  struct SceneTexturePlanScope {
    CoinRenderActionP * action;
    bool owns;
    ~SceneTexturePlanScope() {
      if (owns) {
        action->sceneTextureCaptureMemo.reset();
        action->sceneTexturePlan.reset();
      }
    }
  } sceneTexturePlanScope{this, ownsSceneTexturePlan};
  for (const auto& texture : this->lastValidPlan.textures)
    if (texture.producerId)
      planCacheAllowed = false;
  // The adapter rebuilds the list with clear/add on every sync. Compare its
  // final effective configuration once per logical apply, so changing only a
  // depth barrier is captured while identical lists keep the existing reuse.
  if (this->capturedManagerLayerBoundaries != this->managerLayerBoundaries) {
    this->cachedRoot = NULL;
    this->capturedManagerLayerBoundaries = this->managerLayerBoundaries;
  }
  const bool qualifyCapturedCamera = !this->hasLastValidPlan || this->cachedRoot != cacheRoot;
  const bool traversalSkipped = planCacheAllowed && cacheRoot && this->hasLastValidPlan &&
    this->cachedRoot == cacheRoot && this->cachedRootId == cacheRoot->getNodeId() &&
    !this->cameraOnlyDirty && !this->cameraPatchInvalidated && !this->translationInputDirty;
  CoinRenderFrameReuseDecision reuseDecision = traversalSkipped
    ? CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::REUSE,
                               this->lastValidPlan.revision)
    : CoinRenderFrameReuseDecision(CoinRenderFrameReuseKind::UNKNOWN, 0);
  this->sceneTexturePixels.clear();
  this->shadowSceneCaptures.clear();
  this->encounteredActiveBump = false;
  this->shadowStyleBeforeGroups.clear();
  this->managerHighlightDepth = 0;
  this->activeShadowGroupNodes.clear();
  this->lastRejectedShadowFrame = CoinRenderFramePlan();
  this->lastRejectedShadowPlan = CoinRenderShadowPlan();
  CoinRenderFramePlan plan;
  CoinRenderFramePreflight capturedPreflight;
  CoinRenderCameraOverlayUndo overlayUndo;
  CoinRenderObjectOverlayUndo translationUndo;
  struct CameraOverlayScope {
    CoinRenderFramePlan & frame;
    CoinRenderCameraOverlayUndo & undo;
    CoinRenderObjectOverlayUndo & translationUndo;
    bool committed;
    ~CameraOverlayScope() {
      if (!committed) {
        CoinRenderFrameReuseCore::rollbackCameraOverlay(frame, undo);
        CoinRenderFrameReuseCore::rollbackObjectOverlay(frame, translationUndo);
      }
    }
  } overlayScope{this->lastValidPlan, overlayUndo, translationUndo, false};
  ProfileClock::time_point profileTraversed = ProfileClock::now();
  ProfileClock::time_point profilePlanned = profileTraversed;
  const bool translationOverlay = !traversalSkipped && planCacheAllowed && !this->planOnly &&
    this->hasLastValidPlan && this->prepareTranslationOverlay(cacheRoot, translationUndo);
  const bool cameraOverlay = !translationOverlay && !traversalSkipped && planCacheAllowed &&
    !this->planOnly && this->hasLastValidPlan &&
    this->prepareCameraOverlay(cacheRoot, overlayUndo);
  if (cameraOverlay) {
    reuseDecision = CoinRenderFrameReuseDecision(
      CoinRenderFrameReuseKind::CAMERA_PATCH, overlayUndo.revision);
  }
  if (translationOverlay) {
    reuseDecision = CoinRenderFrameReuseDecision(
      CoinRenderFrameReuseKind::RESOURCE_REBUILD, translationUndo.revision);
  }
  if (!traversalSkipped && !cameraOverlay && !translationOverlay) {
    this->frameStereoStates.clear();
    this->beginTranslationCapture(planCacheAllowed && cacheRoot &&
      (qualifyCapturedCamera || this->translationInputDirty || this->cameraRecaptureRequired));
    this->builder.smoothing = this->smoothing != FALSE;
    this->builder.separateBackfaces = this->delayedRenderType == CoinRenderAction::NONSOLID_SEPARATE_BACKFACE_PASS;
    this->builder.delayedObjDepthWrite = this->delayedObjDepthWrite != FALSE;
    this->builder.beginFrame(this->backgroundColor, this->master->getViewportRegion(), this->externalPass ? 1 : this->numPasses);
    this->builder.sortingStrategy = static_cast<int>(this->sortingStrategy);
    this->builder.objectSortingEnabled = this->executionOptions.transparency == COIN_RENDER_TRANSPARENCY_COIN ||
      this->executionOptions.transparency == COIN_RENDER_TRANSPARENCY_OBJECT;
    this->builder.customSortingDistance = [this](SoCallbackAction *) {
      return this->sortingCallback(this->sortingUserdata, this->master);
    };
    this->managerCapturedLayers.clear();
    this->builder.configureTextures(this->executionOptions);
    this->builder.reserveCaptureStorage(captureStorageEstimate(cacheRoot));
    traversalFn();
    const uint32_t delayedLayers = (this->delayedOverlays.empty() ? 0 : 1) +
                                  (this->delayedAnnotations.empty() ? 0 : 1);
    if (delayedLayers && !this->master->hasTerminated()) {
      const uint32_t firstDelayedLayer = this->builder.reserveDelayedLayers(delayedLayers);
      for (uint32_t & layer : this->managerCapturedLayers)
        if (layer >= firstDelayedLayer) layer += delayedLayers;
      uint32_t layer = firstDelayedLayer;
      auto replay = [&](std::vector<DelayedAnnotation> & paths, bool clearDepth) {
        if (paths.empty() || this->master->hasTerminated()) return;
        std::stable_sort(paths.begin(), paths.end(),
          [](const DelayedAnnotation & a, const DelayedAnnotation & b) {
            return a.priority < b.priority;
          });
        this->builder.beginDelayedAnnotations(layer++, clearDepth);
        this->replayingAnnotations = true;
        for (const auto & entry : paths) {
          this->master->SoCallbackAction::apply(entry.path);
          if (this->master->hasTerminated()) break;
        }
        this->builder.endAnnotation();
        this->replayingAnnotations = false;
      };
      replay(this->delayedOverlays, false);
      replay(this->delayedAnnotations, true);
    }
    profileTraversed = ProfileClock::now();
  }
  // A node can register an observer while it traverses. Never retain a plan
  // or object proof based on the registration checked before that traversal.
  planCacheAllowed = planCacheAllowed && !this->invalidatedDuringApply &&
    this->master->callbackRegistrationRevision() == this->captureCallbackRevision;

  if (this->candidateAborted) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::ABORTED,
      CoinRenderDiagnosticDomain::ACTION, SbString("Render aborted by callback")));
    return;
  }

  if (this->hasReentrancyError) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::ACTION,
      SbString("Nested apply() calls are not permitted on CoinRenderAction")));
    return;
  }

  if (this->master->hasTerminated() && this->lastStatus != CoinRenderAction::SUCCESS) {
    return;
  }

  std::string err;
  // Pixel geometry is already projected. Camera/transform overlays cannot
  // patch it like object-space geometry, including a currently clipped node.
  if (this->builder.hasScreenContent()) planCacheAllowed = false;
  // Coordinate callbacks can depend on Coin state or mutable application data.
  // Snapshots may be reused by the executor only after a fresh capture.
  if (this->builder.hasCoordinateFunctions()) planCacheAllowed = false;
  CoinRenderTransparencyOptions capturedTransparency = this->transparencyOptions;
  capturedTransparency.mode = this->executionOptions.transparency;
  if (!traversalSkipped && !cameraOverlay && !translationOverlay &&
      !this->builder.build(plan, &err, true, &capturedTransparency,
                           this->target && !this->planOnly ? &capturedPreflight : nullptr)) {
    if (this->encounteredActiveBump) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("SoBumpMap requires a qualified bump/normal executor")));
      return;
    }
    const CoinRenderAction::Status status = this->builder.isUnsupportedBuild()
      ? CoinRenderAction::UNSUPPORTED : CoinRenderAction::INVALID_SCENE;
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      status, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(err.c_str())));
    return;
  }
  plan.numPasses = this->externalPass ? 1 : this->numPasses;
  if (this->externalPass) {
    if (!plan.shadowGroups.empty() || !plan.bumpImages.empty() || !plan.rasterOccurrences.empty() || plan.hasPortableShaderDraws()) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("External sample profile excludes spatial shaders and raster"))); return;
    }
    for (const auto & texture : plan.textures) if (texture.producerId || texture.gpuToken) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("External sample profile excludes RTT"))); return;
    }
    for (auto & state : plan.renderStates) {
      SbMatrix shifted;
      if (!CoinRenderMultipassCore::projection(state.projectionCoin, this->numPasses, this->requestedExternalPass,
          plan.viewports[state.viewportSlot], shifted)) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
          CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Invalid external sample projection"))); return;
      }
      state.projectionCoin = shifted;
    }
    capturedPreflight.invalidate();
  }
  plan.multipassBufferBudget = this->multipassBufferBudget;
  if (!CoinRenderMultipassCore::projectionsFinite(plan)) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Multipass translated projection is non-finite or invalid")));
    return;
  }


  plan.managerFrame = std::any_of(this->managerLayerBoundaries.begin(), this->managerLayerBoundaries.end(),
    [](const std::tuple<const SoNode *, bool, int> & boundary) {
      return std::get<2>(boundary) == COIN_RENDER_MANAGER_OWNER;
    });
  // Visiting a bump declaration is not consumption: a later empty map may
  // neutralize it before every shape. The builder captures and validates each
  // effective map while emitting its vertices; failed capture is still handled
  // above. Only emitted active image slots require the bump executor/profile.
  bool capturedActiveBump = false;
  { // Classification storage must not survive into admission or GPU execution.
    std::vector<CoinRenderBumpGpuVertex> capturedBumpStream;
    std::string bumpConsumptionDiagnostic;
    bool bumpConsumptionValid = coin_render_build_bump_gpu_stream(plan, capturedBumpStream,
                                                                 bumpConsumptionDiagnostic);
    if (bumpConsumptionValid && !capturedBumpStream.empty()) for (const auto &draw : plan.draws) {
      if (draw.shadowLightSlot) continue;
      uint32_t slot = 0;
      if (!coin_render_bump_draw_slot(plan, capturedBumpStream, draw, slot, bumpConsumptionDiagnostic)) {
        bumpConsumptionValid = false;
        break;
      }
      capturedActiveBump = capturedActiveBump || slot != 0;
    }
    if (!bumpConsumptionValid) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString(bumpConsumptionDiagnostic.c_str())));
      return;
    }
  }
  if (capturedActiveBump) {
    const bool targetQualified = (this->target && !this->planOnly &&
      this->target->getPimpl()->supportsOpaqueBump(this->asyncTicket != nullptr)) ||
      (this->planOnly && this->sceneTextureBumpProducer);
    std::string bumpProfileDiagnostic;
    const bool bumpProfileValid = coin_render_validate_extended_bump(plan, bumpProfileDiagnostic);
    const bool frameQualified =
        bumpProfileValid && !plan.draws.empty() && !plan.bumpImages.empty() &&
        !plan.bumpCoords.empty() &&
        size_t(std::count_if(plan.bumpCoords.begin(), plan.bumpCoords.end(),
                             [](const CoinRenderBumpCoordOverride &coord) {
                               return coord.imageSlot != 0;
                             })) == plan.bumpTangents.size() &&
        std::all_of(plan.draws.begin(), plan.draws.end(),
                    [&plan](const CoinRenderDrawPacket &draw) {
                      if (draw.renderStateSlot >= plan.renderStates.size())
                        return false;
                      const auto &state = plan.renderStates[draw.renderStateSlot];
                      // The common bump validator checks lighting only on
                      // indexed occurrences which consume a normal image.
                      if (draw.shadowLightSlot) return true;
                      return state.lightingSlot < plan.lightingStates.size() &&
                             state.cullMode == CoinRenderCullMode::NONE && state.depthTest &&
                             state.depthWrite &&
                             (state.depthFunction == CoinRenderDepthFunction::LESS ||
                              state.depthFunction == CoinRenderDepthFunction::LEQUAL) &&
                             !state.transparentMaterial && !state.transparentTexture &&
                             state.transparencyType == SoGLRenderAction::NONE &&
                             state.fogMode == CoinRenderFogMode::NONE &&
                             draw.renderLayer == 0 && !draw.clearDepthBefore;
                    });
    if (!targetQualified || !frameQualified) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString(bumpProfileDiagnostic.empty()
                       ? "SoBumpMap requires a qualified opaque bump/normal executor"
                       : bumpProfileDiagnostic.c_str())));
      return;
    }
    planCacheAllowed = false;
  }
  if ((this->drawMode == CoinRenderAction::DrawMode::HIDDEN_LINE ||
       this->drawMode == CoinRenderAction::DrawMode::SHADED_HIDDEN_LINES) &&
      !traversalSkipped && !cameraOverlay && !translationOverlay) {
    const auto textureHasUnqualifiedAlpha = [&plan](uint32_t slot) {
      if (slot >= plan.textures.size()) return true;
      const auto & texture = plan.textures[slot];
      if (texture.producerId || texture.gpuToken) return true;
      if (texture.pixelsRgba.empty()) return !texture.gpuOpaque;
      return coin_render_image_has_transparency(texture.pixelsRgba.data(),
                                                texture.pixelsRgba.size() / 4, 4);
    };
    for (const auto & draw : plan.draws) {
      const auto & state = plan.renderStates[draw.renderStateSlot];
      const bool managerForeground = draw.managerSuperimposition;
      bool textureAlpha = state.hasTexture && textureHasUnqualifiedAlpha(state.textureImageSlot);
      for (const auto & texture : state.extraTextures) {
        if (texture.enabled && textureHasUnqualifiedAlpha(texture.imageSlot)) textureAlpha = true;
      }
      if ((!managerForeground &&
           (state.transparentMaterial || state.transparentTexture || textureAlpha ||
            state.screenDoorTransparency > 0 ||
            (!draw.isSceneLayer() && (draw.renderLayer || draw.clearDepthBefore)))) ||
          (!draw.generatedHighlight && coin_render_alpha_test_active(state.alphaTestFunction)) || state.shadowGroupSlot) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("hidden-line modes require opaque base geometry and no shadows or alpha test")));
        return;
      }
    }
  }
  if (this->drawMode != CoinRenderAction::DrawMode::SCENE &&
      !this->managerCapturedLayers.empty() &&
      !traversalSkipped && !cameraOverlay && !translationOverlay) {
    for (const auto & draw : plan.draws) {
      if (!draw.managerSuperimposition || draw.generatedHighlight) continue;
      const auto & state = plan.renderStates[draw.renderStateSlot];
      if (coin_render_alpha_test_active(state.alphaTestFunction) ||
          state.shadowGroupSlot || draw.shadowLightSlot) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Manager technical layers do not support shadows or alpha test")));
        return;
      }
    }
  }
  if (!this->shadowSceneCaptures.empty()) capturedPreflight.invalidate();
  for (const auto & capture : this->shadowSceneCaptures) {
    for (size_t l = 0; l < plan.shadowLights.size(); ++l) {
      auto & light = plan.shadowLights[l];
      if (light.groupSlot != capture.groupSlot || light.sourceRevision != capture.lightRevision) continue;
      const bool authoredCamera = std::any_of(capture.frame.renderStates.begin(),
        capture.frame.renderStates.end(), [](const CoinRenderRenderStateSnapshot &state) {
          return state.shadowMapCameraOverride;
        });
      if (authoredCamera) {
        const SbMatrix &entryModel = plan.shadowGroups[light.groupSlot - 1].entryModel;
        SbMatrix inverseEntry;
        if (!coin_render_shadow_camera_math::inverse(entryModel, inverseEntry)) {
          this->setDiagnostic(CoinRenderDiagnosticShell::action(
            CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
            SbString("shadowMapScene camera has a non-invertible group entry transform")));
          return;
        }
      }
      coin_render_append_shadow_scene(plan, capture.frame, static_cast<uint32_t>(l + 1),
                                      capture.inheritedClipPlaneCount);
      light.mapSceneCaptured = true;
    }
  }
  if (!plan.shadowGroups.empty()) {
    const bool hasShadowMap = std::any_of(plan.shadowLights.begin(), plan.shadowLights.end(),
      [](const CoinRenderShadowLightSnapshot & light) {
        return light.enabled && light.shadowEligible;
      });
    if (hasShadowMap) {
      for (auto &state : plan.renderStates) {
        if (!state.shadowGroupSlot || !(state.shadowStyle & 2u) ||
            !state.rasterShadowImagePhong) continue;
        if (!state.rasterPixels || !state.rasterShadowAnchorValid ||
            state.cameraSlot >= plan.cameras.size()) {
          this->setDiagnostic(CoinRenderDiagnosticShell::action(
            CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
            SbString("Raster image shadow receiver has no observer camera anchor")));
          return;
        }
        // SoImage retains the authored light model. With an active shadow
        // shader, PHONG modulates the pixel color by the existing receiver
        // lighting path; BASE_COLOR remains unlit.
        state.lightModel = CoinRenderLightModel::PHONG;
        state.textureModel = CoinRenderTextureModel::MODULATE;
      }
    }
    if (!hasShadowMap) {
      // An active group without a map is ordinary scene traversal. Keep its
      // captured lights and raster draws, but remove the inactive map scope so
      // the CPU and GPU paths use the same normal compositor.
      const bool flatReceiver = std::any_of(plan.renderStates.begin(), plan.renderStates.end(),
        [](const CoinRenderRenderStateSnapshot & state) { return state.flatShadowPrimary; });
      if (!flatReceiver) {
        for (auto & state : plan.renderStates) {
          if (state.shadowGroupSlot) {
            state.textureProjection = CoinRenderTextureProjection::PROJECTIVE;
            state.shadowGroupSlot = 0;
            state.shadowStyle = 0;
          }
        }
        plan.shadowGroups.clear();
        plan.shadowLights.clear();
      }
    }
  }
  if (!coin_render_valid_sampling(plan, err)) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(err.c_str())));
    return;
  }
  if (!this->shadowSceneCaptures.empty() && !plan.isValid(&err)) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(err.c_str())));
    return;
  }
  if (!plan.shadowGroups.empty()) {
    CoinRenderShadowPlan shadowPlan;
    std::string shadowDiagnostic;
    if (!coin_render_plan_shadows(plan, shadowPlan, shadowDiagnostic)) {
      this->lastRejectedShadowFrame = std::move(plan);
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString(shadowDiagnostic.c_str())));
      return;
    }
    if (!plan.rasterOccurrences.empty()) {
      if (!coin_render_materialize_raster_casters(plan, shadowPlan, shadowDiagnostic) ||
          !plan.isValid(&shadowDiagnostic) ||
          !coin_render_plan_shadows(plan, shadowPlan, shadowDiagnostic)) {
        this->lastRejectedShadowFrame = std::move(plan);
        this->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString(shadowDiagnostic.c_str())));
        return;
      }
    }
    bool executableShadow = false;
    // SoSceneTexture2 producers are captured without a target. The resolved
    // frame is later submitted by the selected staged or direct executor.
    if (this->planOnly &&
        (this->sceneTexturePlan->mode == COIN_RENDER_SCENE_TEXTURE_STAGED ||
         this->sceneTexturePlan->mode == COIN_RENDER_SCENE_TEXTURE_DIRECT)) {
      std::string profileDiagnostic;
      executableShadow = coin_render_shadow_object_profile(
        plan, shadowPlan, shadowPlan.passes.size(), profileDiagnostic);
    }
    if (this->target &&
        this->target->getPimpl()->supportsOffscreenShadows(this->asyncTicket != nullptr)) {
      std::string profileDiagnostic;
      executableShadow = coin_render_shadow_object_profile(
        plan, shadowPlan, shadowPlan.passes.size(), profileDiagnostic);
    }
    if (!executableShadow) {
      this->lastRejectedShadowFrame = std::move(plan);
      this->lastRejectedShadowPlan = std::move(shadowPlan);
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("Active SoShadowGroup requires a qualified opaque shadow executor for this target")));
      return;
    }
    // Shadow maps depend on captured light/geometry state; reuse and camera
    // overlays are not qualified for this first executable profile.
    planCacheAllowed = false;
  }
  profilePlanned = ProfileClock::now();
  if (!this->sceneTexturePlan->producers.empty())
    planCacheAllowed = false;
  if (!traversalSkipped && !cameraOverlay && !translationOverlay) {
    if (planCacheAllowed && cacheRoot && this->hasLastValidPlan &&
        this->cachedRoot == cacheRoot) {
      reuseDecision = CoinRenderFrameReuseCore::classify(this->lastValidPlan, plan);
    } else if (planCacheAllowed) {
      reuseDecision = CoinRenderFrameReuseDecision(
        CoinRenderFrameReuseKind::FULL_REBUILD, 0);
    }
  }
  const bool reusePreviousPlan =
    reuseDecision.kind == CoinRenderFrameReuseKind::REUSE &&
    this->hasLastValidPlan;
  const bool useCachedPlan = reusePreviousPlan || cameraOverlay || translationOverlay;
  if (useCachedPlan) capturedPreflight.invalidate();
  const CoinRenderFramePlan& capturedPlan = useCachedPlan ? this->lastValidPlan : plan;
  const CoinRenderFramePlan& framePlan = capturedPlan;

  if (this->planOnly) {
    if (!useCachedPlan) {
      this->lastValidPlan = std::move(plan);
      this->hasLastValidPlan = true;
      this->capturedStereoStates = this->frameStereoStates;
    }
    this->setDiagnostic(CoinRenderDiagnosticShell::success());
    return;
  }

  // The typed capture/transport increment is independently usable by Core
  // consumers. GPU execution is enabled only with the matching executor.
  bool portableExecution = capturedPlan.hasPortableShaderDraws();
  for (const auto &producer : this->sceneTexturePlan->producers)
    for (size_t face = 0; face < producer.faceCount(); ++face)
      portableExecution = portableExecution || producer.face(face).hasPortableShaderDraws();
  if (portableExecution) {
    std::string error;
    bool valid = coin_render_shader_execution_profile(capturedPlan, error);
    for (const auto &producer : this->sceneTexturePlan->producers)
      for (size_t face = 0; valid && face < producer.faceCount(); ++face)
        valid = coin_render_shader_execution_profile(producer.face(face), error);
    if (!valid) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(error.c_str())));
      return;
    }
  }
  if (portableExecution && (!this->target ||
      !this->target->getPimpl().get().supportsPortableShaderModules())) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("Portable shader module execution requires a GPU executor for this target")));
    return;
  }

  if (this->target && this->target->getStatus() == CoinRenderTarget::TARGET_ERROR) {
    this->setDiagnostic(CoinRenderDiagnosticShell::fromTarget(this->target->getStatus(),
                                                              this->target->getLastError()));
    return;
  }
  if (!this->target && this->asyncTicket) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::NO_TARGET, CoinRenderDiagnosticDomain::TARGET,
        SbString("applyAsync() requires an offscreen render target")));
    return;
  }
  if (this->target) {
    const auto admission =
        this->target->getPimpl().get().preflightSubmission(this->asyncTicket != nullptr);
    if (admission.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(admission));
      return;
    }
  }
  if (this->target) {
    const auto admission = this->target->getPimpl().get().preflightMultipass(
      capturedPlan, this->asyncTicket != nullptr, this->sceneTexturePlan.get());
    if (admission.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(admission));
      return;
    }
  } else {
    const SbVec2s viewport = this->master->getViewportRegion().getWindowSize();
    const auto admission = this->recordingBackend.preflightMultipassGraph(
      *this->sceneTexturePlan, capturedPlan, SbVec2i32(viewport[0], viewport[1]));
    if (admission.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(admission));
      return;
    }
  }
  if (this->smoothing && std::any_of(this->frameStereoStates.begin(), this->frameStereoStates.end(),
      [](const CameraStereoState & state) { return state.mode != static_cast<int>(SoCamera::MONOSCOPIC); })) {
    this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Smoothing stereo camera is outside the mono profile")));
    return;
  }
  if (partialUpdate) {
    if (!capturedPlan.shadowGroups.empty() || !capturedPlan.bumpImages.empty() ||
        capturedPlan.hasPortableShaderDraws()) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Partial update with spatial shaders is not qualified")));
      return;
    }
    std::vector<CoinRenderCompositionItem> partialOrder;
    std::string diagnostic;
    if (!coin_render_composition_order(capturedPlan, partialOrder, diagnostic)) {
      this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(diagnostic.c_str())));
      return;
    }
    for (const auto & item : partialOrder)
      if (item.screenDoorLevel || item.transparencyStrategy != CoinRenderCompositionItem::OBJECT) {
        this->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
          CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Stippled and OIT partial updates need a canvas-origin profile")));
        return;
      }
  }
  CoinRenderRttExecution rttExecution(this->target ? &this->target->getPimpl().get() : nullptr,
                                      this->executionOptions);
  CoinRenderFramePlan resolvedPlan;
  const bool hasSceneTextures = !this->sceneTexturePlan->producers.empty();
  if (hasSceneTextures) {
    capturedPreflight.invalidate();
    const auto prepared = rttExecution.prepare(*this->sceneTexturePlan, capturedPlan, resolvedPlan);
    if (prepared.status != CoinRenderBackendStatus::SUCCESS) {
      this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(prepared));
      return;
    }
  }
  const CoinRenderFramePlan& executionPlan = hasSceneTextures ? resolvedPlan : capturedPlan;
  const bool retainStagedPixels =
      hasSceneTextures && this->sceneTexturePlan->mode == COIN_RENDER_SCENE_TEXTURE_STAGED;

  if (this->target == NULL) {
    // Mode 0: Recording backend
    this->lastRecordingLog = this->recordingBackend.recordToString(executionPlan).c_str();
    this->recordingLogValid = true;
    if (!useCachedPlan) {
      this->lastValidPlan = retainStagedPixels ? std::move(resolvedPlan) : std::move(plan);
      this->hasLastValidPlan = true;
      this->capturedStereoStates = this->frameStereoStates;
    }
    if (cacheRoot) {
      this->cachedRoot = planCacheAllowed ? cacheRoot : NULL;
      this->cachedRootId = cacheRoot->getNodeId();
      if (cameraOverlay) {
        this->cameraOnlyDirty = false;
        this->clearTranslationProof();
      } else if (translationOverlay) this->commitTranslationOverlay();
      else if (!traversalSkipped)
        this->rememberFrameRoot(planCacheAllowed ? cacheRoot : NULL, qualifyCapturedCamera);
    }
    overlayScope.committed = true;
    this->setDiagnostic(CoinRenderDiagnosticShell::success());
    return;
  }

  // Execute frame on target
  const auto * submissionPreflight = capturedPreflight.compositionFor(executionPlan)
    ? &capturedPreflight : nullptr;
  CoinRenderFrameExecutionResult execRes =
      this->asyncTicket
          ? this->target->pimpl->executeFrameAsync(executionPlan, *this->asyncTicket, reuseDecision,
                                                  submissionPreflight)
          : partialUpdate
              ? this->target->pimpl->executePartialFrame(executionPlan, updateRect)
              : this->target->pimpl->executeFrame(executionPlan, reuseDecision, submissionPreflight);
  if (execRes.status != CoinRenderBackendStatus::SUCCESS) {
    this->setDiagnostic(CoinRenderDiagnosticShell::fromBackend(execRes));
    return;
  }

  rasterHistoryScope.committed = true;
  const ProfileClock::time_point profileExecuted = ProfileClock::now();
  if (tracePhases) {
    CoinRenderActionPhaseSample sample;
    sample.traversalMs = std::chrono::duration<double, std::milli>(
      profileTraversed - profileBegin).count();
    sample.framePlanMs = std::chrono::duration<double, std::milli>(
      profilePlanned - profileTraversed).count();
    sample.backendMs = std::chrono::duration<double, std::milli>(
      profileExecuted - profilePlanned).count();
    sample.vertices = framePlan.vertices.size();
    sample.indices = framePlan.indices.size();
    sample.draws = framePlan.draws.size();
    sample.planCacheHit = traversalSkipped || cameraOverlay || translationOverlay;
    sample.reuseKind = reuseDecision.kind;
    std::cerr << CoinRenderDiagnosticShell::formatActionPhase(sample) << '\n';
  }
  if (!useCachedPlan) {
    capturedPreflight.invalidate();
    this->lastValidPlan = retainStagedPixels ? std::move(resolvedPlan) : std::move(plan);
    this->hasLastValidPlan = true;
    this->capturedStereoStates = this->frameStereoStates;
    this->recordingLogValid = false;
  } else if (cameraOverlay || translationOverlay) {
    this->recordingLogValid = false;
  }
  if (cacheRoot) {
    this->cachedRoot = planCacheAllowed ? cacheRoot : NULL;
    this->cachedRootId = cacheRoot->getNodeId();
    if (cameraOverlay) {
      this->cameraOnlyDirty = false;
      this->clearTranslationProof();
    } else if (translationOverlay) this->commitTranslationOverlay();
    else if (!traversalSkipped)
      this->rememberFrameRoot(planCacheAllowed ? cacheRoot : NULL, qualifyCapturedCamera);
  }
  overlayScope.committed = true;
  this->setDiagnostic(CoinRenderDiagnosticShell::success());
}

SoCallbackAction::Response
CoinRenderActionP::textureImagePreCB(void * userdata, SoCallbackAction *, const SoNode * node)
{
  const auto * texture=static_cast<const SoTexture2 *>(node);
  SbVec2s size;int components;
  const auto * bytes=texture->image.getValue(size,components);
  // Distinguish a genuinely authored white 2x2 luminance image from Coin's
  // static pending-filename dummy. Reading this field does not traverse/mutate
  // the node or affect pre-callback PRUNE/override behavior.
  if(size[0]>0 && size[1]>0 && components>0)
    static_cast<CoinRenderActionP *>(userdata)->builder.registerAuthoredTextureImage(
      bytes, texture->enableCompressedTexture.getValue(), texture,
      texture->getImageUploadRevision());
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::textureVolumePreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  const auto * texture = static_cast<const SoTexture3 *>(node);
  SoState * state = action->getState();
  const int unit = SoTextureUnitElement::get(state);
  if (unit == 0 && SoTextureOverrideElement::getImageOverride(state))
    return SoCallbackAction::CONTINUE;
  SbVec3s size;
  int components = 0;
  const auto * bytes = texture->images.getValue(size, components);
  const bool pending = size == SbVec3s(0, 0, 0) && texture->images.isDefault() &&
      texture->filenames.getNum() > 0 && texture->filenames[0].getLength() != 0;
  // SoTexture3::doAction does not set the enabled element for callbacks.
  // Preserve the dimensional mode explicitly: SoMultiTextureImageElement also
  // stores ordinary 2D images with a depth of one.
  if (size != SbVec3s(0, 0, 0) || pending)
    SoMultiTextureEnabledElement::enableTexture3(state, const_cast<SoTexture3 *>(texture), unit);
  else
    SoMultiTextureEnabledElement::set(state, const_cast<SoTexture3 *>(texture), unit, FALSE);
  if (size[0] > 0 && size[1] > 0 && size[2] > 0 && components > 0)
    static_cast<CoinRenderActionP *>(userdata)->builder.registerAuthoredVolumeImage(
      bytes, texture->enableCompressedTexture.getValue(), texture, texture->getNodeId());
  return SoCallbackAction::CONTINUE;
}

static bool validShadowMapViewport(const SbViewportRegion &viewport, std::string &diagnostic)
{
  const auto &origin = viewport.getViewportOrigin();
  const auto &extent = viewport.getViewportSize();
  const auto &window = viewport.getWindowSize();
  for (unsigned axis = 0; axis < 2; ++axis) {
    const double pixelOrigin = double(origin[axis]) * window[axis];
    const double pixelExtent = double(extent[axis]) * window[axis];
    if (window[axis] <= 0 || !std::isfinite(pixelOrigin) || !std::isfinite(pixelExtent) ||
        extent[axis] < 0.0f || std::round(pixelOrigin) < std::numeric_limits<short>::min() ||
        std::round(pixelOrigin) > std::numeric_limits<short>::max() ||
        std::round(pixelExtent) > std::numeric_limits<short>::max()) {
      diagnostic =
          "shadowMapScene viewport is non-finite, negative or exceeds SbViewportRegion pixels";
      return false;
    }
  }
  return true;
}

SoCallbackAction::Response
CoinRenderActionP::textureUnitsPreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  // PRUNE skips this shape only; subsequent state nodes still traverse.
  if (CoinRenderFramePlanBuilder::isShapeInvisible(action)) return SoCallbackAction::PRUNE;
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->capturingShadowScene) {
    std::string diagnostic;
    if (!validShadowMapViewport(action->getViewportRegion(), diagnostic)) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
                                                         CoinRenderDiagnosticDomain::FRAME_PLAN,
                                                         SbString(diagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    const auto extent = action->getViewportRegion().getViewportSizePixels();
    if (!extent[0] || !extent[1])
      return SoCallbackAction::PRUNE;
  }
  if (node->getTypeId() == SoImage::getClassTypeId() ||
      node->getTypeId() == SoText2::getClassTypeId() ||
      node->getTypeId() == SoMarkerSet::getClassTypeId() ||
      node->getTypeId() == SoIndexedMarkerSet::getClassTypeId()) {
    p->builder.endShape();
    return SoCallbackAction::CONTINUE; // The action method captures after all pre callbacks.
  }
  SoState * state = action->getState();
  if (!p->capturingShadowScene && (SoShapeStyleElement::get(state)->getFlags() &
      SoShapeStyleElement::BBOXCMPLX)) return SoCallbackAction::CONTINUE;
  int last = -1;
  const SbBool * enabled = SoMultiTextureEnabledElement::getEnabledUnits(state, last);
  if (last >= static_cast<int>(COIN_RENDER_MAX_TEXTURE_UNITS)) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("At most eight texture units are supported")));
    return SoCallbackAction::ABORT;
  }
  if (SoTextureQualityElement::get(state) > 0.0f) {
    for (int unit = 0; unit <= last; ++unit) {
      if (enabled[unit] && SoMultiTextureCoordinateElement::getType(state, unit) ==
          SoMultiTextureCoordinateElement::TEXGEN) {
        p->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Hardware-only texture generators require a CPU coordinate callback")));
        return SoCallbackAction::ABORT;
      }
    }
  }
  p->builder.endShape();
  p->beginTranslationShape(action, node);
  // The installed capture callbacks include multiple inherited pre/post
  // observers. Their exact registration revision, rather than a single-list
  // count, proves that no external observer can be skipped by cube replay.
  // Restrict this admission to the concrete native generator: a C++ subclass
  // may override callback/generation while retaining Cube's runtime SoType.
  const bool internalCubeCapture = p->fastPathEnabled &&
    node->getTypeId() == SoCube::getClassTypeId() &&
    typeid(*node) == typeid(SoCube) &&
    p->master->callbackRegistrationRevision() == p->captureCallbackRevision;
  if (internalCubeCapture || p->master->hasSingleShapeCallbacks(node->getTypeId())) {
    p->builder.beginShape(action, node);
    if (p->fastPathEnabled && p->master->callbackRegistrationRevision() == p->captureCallbackRevision &&
        p->builder.replayNativeCube(action, const_cast<SoNode *>(node))) {
      p->endTranslationShape();
      p->builder.endShape();
      return SoCallbackAction::PRUNE;
    }
  }
  return SoCallbackAction::CONTINUE;
}

bool
CoinRenderActionP::captureBoundingBox(SoCallbackAction * action, SoNode * node)
{
  if (this->capturingShadowScene || !(SoShapeStyleElement::get(action->getState())->getFlags() &
      SoShapeStyleElement::BBOXCMPLX)) return false;
  // Match native returns that precede shouldGLRender, even if an otherwise
  // nonempty coordinate element could supply a box.
  if ((node->isOfType(SoIndexedFaceSet::getClassTypeId()) &&
       static_cast<SoIndexedFaceSet *>(node)->coordIndex.getNum() < 3) ||
      (node->isOfType(SoIndexedLineSet::getClassTypeId()) &&
       static_cast<SoIndexedLineSet *>(node)->coordIndex.getNum() < 2) ||
      (node->isOfType(SoIndexedPointSet::getClassTypeId()) &&
       static_cast<SoIndexedPointSet *>(node)->coordIndex.getNum() == 0) ||
      (node->isOfType(SoIndexedMarkerSet::getClassTypeId()) &&
       static_cast<SoIndexedMarkerSet *>(node)->coordIndex.getNum() == 0) ||
      (node->isOfType(SoPointSet::getClassTypeId()) &&
       static_cast<SoPointSet *>(node)->numPoints.getValue() == 0) ||
      (node->isOfType(SoFaceSet::getClassTypeId()) &&
       static_cast<SoFaceSet *>(node)->numVertices.getNum() == 1 &&
       static_cast<SoFaceSet *>(node)->numVertices[0] == 0)) return true;
  if (node->isOfType(SoImage::getClassTypeId())) {
    SbVec2s size; int components = 0;
    const auto * bytes = static_cast<SoImage *>(node)->image.getValue(size, components);
    if (!bytes || !size[0] || !size[1]) return true;
  }
  SoState * state = action->getState();
  state->push();
  const bool marker = node->isOfType(SoMarkerSet::getClassTypeId());
  if (marker) {
    SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
    SoMultiTextureEnabledElement::disableAll(state);
  }
  // PointSet reads VP coordinates in computeBBox but applies its material
  // only after shouldGLRender. Other vertex shapes apply the property first.
  if (node->isOfType(SoVertexShape::getClassTypeId()) &&
      (!node->isOfType(SoPointSet::getClassTypeId()) || marker)) {
    auto * property = static_cast<SoVertexShape *>(node)->vertexProperty.getValue();
    if (property) property->doAction(action);
  }
  const bool success = this->builder.captureBoundingBox(action, node);
  state->pop();
  if (!success) this->master->setTerminated(TRUE);
  return true;
}

void
CoinRenderActionP::observeBoundingBox(SoCallbackAction * action, SoNode * node)
{
  if (action->hasTerminated() || action->hasSingleShapeCallbacks(node->getTypeId())) return;
  const bool marker = node->isOfType(SoMarkerSet::getClassTypeId()) ||
                      node->isOfType(SoIndexedMarkerSet::getClassTypeId());
  SoState * state = action->getState();
  struct ObserverScope {
    CoinRenderActionP & owner;
    SoState * markerState;
    bool previous;
    ~ObserverScope() {
      if (markerState) markerState->pop();
      owner.boundingBoxObservers = previous;
    }
  } scope{*this, marker ? state : nullptr, this->boundingBoxObservers};
  if (marker) {
    state->push();
    SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
    SoMultiTextureEnabledElement::disableAll(state);
  }
  this->boundingBoxObservers = true;
  node->callback(action);
}

namespace {
void normalizeIndexedBindings(const SoNode * node, CoinRenderDirectGeometryView & view)
{
  // VRML1 PER_VERTEX means per coordinate, even when normalIndex is authored.
  // Both native indexed shapes replace that index stream with coordIndex.
  if (node->getNodeType() != SoNode::VRML1) return;
  if (view.normalBinding == SoNormalBindingElement::PER_VERTEX) {
    view.normalBinding = SoNormalBindingElement::PER_VERTEX_INDEXED;
    view.normalIndex = view.coordIndex;
  }
  // The native material binding has the same VRML1 coordinate-index rule;
  // an authored materialIndex is idle in this binding.
  if (view.materialBinding == SoMaterialBindingElement::PER_VERTEX) {
    view.materialBinding = SoMaterialBindingElement::PER_VERTEX_INDEXED;
    view.materialIndex = view.coordIndex;
  }
}

#ifdef HAVE_VRML97
// Validate only arrays dereferenced by native primitive generation. In
// particular unused index streams and light radius/beamWidth are not inputs.
bool validateVRMLGeometry(SoCallbackAction * action, SoNode * node, std::string & error)
{
  auto fail = [&]() { error = "Invalid consumed VRML geometry array, index or numeric value"; return false; };
  auto finite3 = [](const SbVec3f & v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
  };
  auto vectorNode = [&](const SoSFNode & field, SoType type) {
    return !field.getValue() || field.getValue()->isOfType(type);
  };
  auto indices = [&](const SoMFInt32 & stream, const SoVRMLCoordinate * coord) {
    for (int i = 0; i < stream.getNum(); ++i) {
      const int index = stream[i];
      if (index < -1 || (index >= 0 && (!coord || index >= coord->point.getNum() ||
                                      !finite3(coord->point[index])))) return false;
    }
    return true;
  };
  auto attribute = [&](const SoMFInt32 & coordinates, const SoMFInt32 & explicitIndices,
                       int count, bool perVertex, bool explicitUsed, auto finiteAt) {
    int part = 0;
    bool inPart = false;
    for (int i = 0; i < coordinates.getNum(); ++i) {
      if (coordinates[i] == -1) { if (inPart) ++part; inPart = false; continue; }
      int index = perVertex ? coordinates[i] : part;
      const int offset = perVertex ? i : part;
      if (explicitUsed) {
        if (offset >= explicitIndices.getNum()) return false;
        index = explicitIndices[offset];
      }
      if (index < 0 || index >= count || !finiteAt(index)) return false;
      inPart = true;
    }
    return true;
  };
  if (node->isOfType(SoVRMLBox::getClassTypeId())) {
    if (!finite3(static_cast<const SoVRMLBox *>(node)->size.getValue())) return fail();
  } else if (node->isOfType(SoVRMLSphere::getClassTypeId())) {
    if (!std::isfinite(static_cast<const SoVRMLSphere *>(node)->radius.getValue())) return fail();
  } else if (node->isOfType(SoVRMLCone::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLCone *>(node);
    if ((shape->side.getValue() || shape->bottom.getValue()) &&
        (!std::isfinite(shape->bottomRadius.getValue()) || !std::isfinite(shape->height.getValue()))) return fail();
  } else if (node->isOfType(SoVRMLCylinder::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLCylinder *>(node);
    if ((shape->side.getValue() || shape->bottom.getValue() || shape->top.getValue()) &&
        (!std::isfinite(shape->radius.getValue()) || !std::isfinite(shape->height.getValue()))) return fail();
  } else if (node->isOfType(SoVRMLText::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLText *>(node);
    if (!shape->string.getNum()) return true;
    if (!std::isfinite(shape->maxExtent.getValue())) return fail();
    for (int i = 0; i < shape->length.getNum() && i < shape->string.getNum(); ++i)
      if (!std::isfinite(shape->length[i])) return fail();
    if (!vectorNode(shape->fontStyle, SoVRMLFontStyle::getClassTypeId())) return fail();
    if (const auto * font = static_cast<const SoVRMLFontStyle *>(shape->fontStyle.getValue()))
      if (!std::isfinite(font->size.getValue()) || !std::isfinite(font->spacing.getValue())) return fail();
  } else if (node->isOfType(SoVRMLIndexedFaceSet::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLIndexedFaceSet *>(node);
    if (shape->coordIndex.getNum() < 3) return true;
    if (!vectorNode(shape->coord, SoVRMLCoordinate::getClassTypeId()) ||
        !vectorNode(shape->color, SoVRMLColor::getClassTypeId()) ||
        !vectorNode(shape->normal, SoVRMLNormal::getClassTypeId()) ||
        !vectorNode(shape->texCoord, SoVRMLTextureCoordinate::getClassTypeId())) return fail();
    const auto * coord = static_cast<const SoVRMLCoordinate *>(shape->coord.getValue());
    if (!coord) return true;
    if (!indices(shape->coordIndex, coord)) return fail();
    if (const auto * color = static_cast<const SoVRMLColor *>(shape->color.getValue())) {
      const bool explicitUsed = shape->colorIndex.getNum() && shape->colorIndex[0] >= 0;
      if (!attribute(shape->coordIndex, shape->colorIndex, color->color.getNum(),
          shape->colorPerVertex.getValue(), explicitUsed,
          [&](int i) { return finite3(color->color[i]); })) return fail();
    }
    if (const auto * normal = static_cast<const SoVRMLNormal *>(shape->normal.getValue())) {
      if (normal->vector.getNum()) {
        const bool explicitUsed = shape->normalIndex.getNum() && shape->normalIndex[0] >= 0;
        if (!attribute(shape->coordIndex, shape->normalIndex, normal->vector.getNum(),
            shape->normalPerVertex.getValue(), explicitUsed,
            [&](int i) { return finite3(normal->vector[i]); })) return fail();
      }
    }
    if (SoMultiTextureEnabledElement::get(action->getState(), 0))
      if (const auto * texture = static_cast<const SoVRMLTextureCoordinate *>(shape->texCoord.getValue())) {
        const bool explicitUsed = shape->texCoordIndex.getNum() && shape->texCoordIndex[0] >= 0;
        if (!attribute(shape->coordIndex, shape->texCoordIndex, texture->point.getNum(), true,
            explicitUsed, [&](int i) { return std::isfinite(texture->point[i][0]) &&
                                            std::isfinite(texture->point[i][1]); })) return fail();
      }
    if (!std::isfinite(shape->creaseAngle.getValue())) return fail();
  } else if (node->isOfType(SoVRMLIndexedLineSet::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLIndexedLineSet *>(node);
    if (shape->coordIndex.getNum() < 2) return true;
    if (!vectorNode(shape->coord, SoVRMLCoordinate::getClassTypeId()) ||
        !vectorNode(shape->color, SoVRMLColor::getClassTypeId())) return fail();
    const auto * coord = static_cast<const SoVRMLCoordinate *>(shape->coord.getValue());
    if (!coord) return true;
    if (!indices(shape->coordIndex, coord)) return fail();
    // Empty sections are legal for faces but native VRML line callbacks assert
    // when their first index is negative. Do not pass malformed strips through.
    for (int i = 0; i + 1 < shape->coordIndex.getNum(); ++i)
      if (shape->coordIndex[i] < 0 && (i == 0 || shape->coordIndex[i - 1] < 0)) return fail();
    if (const auto * color = static_cast<const SoVRMLColor *>(shape->color.getValue()))
      if (!attribute(shape->coordIndex, shape->colorIndex, color->color.getNum(),
          shape->colorPerVertex.getValue(), shape->colorIndex.getNum() != 0,
          [&](int i) { return finite3(color->color[i]); })) return fail();
  } else if (node->isOfType(SoVRMLPointSet::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLPointSet *>(node);
    if (!vectorNode(shape->coord, SoVRMLCoordinate::getClassTypeId()) ||
        !vectorNode(shape->color, SoVRMLColor::getClassTypeId())) return fail();
    const auto * coord = static_cast<const SoVRMLCoordinate *>(shape->coord.getValue());
    if (!coord) return true;
    const auto * color = static_cast<const SoVRMLColor *>(shape->color.getValue());
    for (int i = 0; i < coord->point.getNum(); ++i)
      if (!finite3(coord->point[i]) || (color && (i >= color->color.getNum() ||
                                               !finite3(color->color[i])))) return fail();
  } else if (node->isOfType(SoVRMLElevationGrid::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLElevationGrid *>(node);
    const int64_t x = shape->xDimension.getValue(), z = shape->zDimension.getValue();
    if (x < 2 || z < 2) return true;
    const int64_t points = x * z, faces = (x - 1) * (z - 1);
    if (points > INT32_MAX || points > shape->height.getNum() ||
        !std::isfinite(shape->xSpacing.getValue()) || !std::isfinite(shape->zSpacing.getValue())) return fail();
    for (int i = 0; i < points; ++i) if (!std::isfinite(shape->height[i])) return fail();
    if (!vectorNode(shape->color, SoVRMLColor::getClassTypeId()) ||
        !vectorNode(shape->normal, SoVRMLNormal::getClassTypeId()) ||
        !vectorNode(shape->texCoord, SoVRMLTextureCoordinate::getClassTypeId())) return fail();
    if (const auto * colors = static_cast<const SoVRMLColor *>(shape->color.getValue())) {
      const int64_t count = shape->colorPerVertex.getValue() ? points : faces;
      if (colors->color.getNum() < count) return fail();
      for (int i = 0; i < count; ++i) if (!finite3(colors->color[i])) return fail();
    }
    if (SoLazyElement::getLightModel(action->getState()) != SoLazyElement::BASE_COLOR)
      if (const auto * normals = static_cast<const SoVRMLNormal *>(shape->normal.getValue())) {
        const int64_t count = shape->normalPerVertex.getValue() ? points : faces;
        if (normals->vector.getNum() < count) return fail();
        for (int i = 0; i < count; ++i) if (!finite3(normals->vector[i])) return fail();
      }
    if (const auto * texture = static_cast<const SoVRMLTextureCoordinate *>(shape->texCoord.getValue())) {
      if (texture->point.getNum() < points) return fail();
      for (int i = 0; i < points; ++i)
        if (!std::isfinite(texture->point[i][0]) || !std::isfinite(texture->point[i][1])) return fail();
    }
  } else if (node->isOfType(SoVRMLExtrusion::getClassTypeId())) {
    const auto * shape = static_cast<const SoVRMLExtrusion *>(node);
    if (!shape->spine.getNum() || !shape->crossSection.getNum()) return true;
    if (shape->spine.getNum() < 2) return fail();
    for (int i = 0; i < shape->spine.getNum(); ++i) if (!finite3(shape->spine[i])) return fail();
    for (int i = 0; i < shape->crossSection.getNum(); ++i)
      if (!std::isfinite(shape->crossSection[i][0]) || !std::isfinite(shape->crossSection[i][1])) return fail();
    for (int i = 0; i < shape->scale.getNum() && i < shape->spine.getNum(); ++i)
      if (!std::isfinite(shape->scale[i][0]) || !std::isfinite(shape->scale[i][1])) return fail();
    for (int i = 0; i < shape->orientation.getNum() && i < shape->spine.getNum(); ++i)
      for (int c = 0; c < 4; ++c) if (!std::isfinite(shape->orientation[i].getValue()[c])) return fail();
    if (!std::isfinite(shape->creaseAngle.getValue())) return fail();
  }
  return true;
}
#endif

bool validateIndexedShapeGeometry(SoCallbackAction * action, SoNode * node,
                                 std::string & diagnostic)
{
  if (!node->isOfType(SoIndexedShape::getClassTypeId())) return true;
  const auto * shape = static_cast<const SoIndexedShape *>(node);
  const bool faces = node->isOfType(SoIndexedFaceSet::getClassTypeId());
  const bool lines = node->isOfType(SoIndexedLineSet::getClassTypeId());
  SoState * state = action->getState();
  state->push();
  struct StateScope { SoState * state; ~StateScope() { state->pop(); } } scope{state};
  // Match generatePrimitives: vertexProperty is applied before its arrays and
  // binding are read, including inherited override rules.
  if (shape->vertexProperty.getValue()) shape->vertexProperty.getValue()->doAction(action);
  const auto * coordinates = SoCoordinateElement::getInstance(state);
  const int count = coordinates ? coordinates->getNum() : 0;
  // Native primitive generation dereferences these indices before the builder
  // can validate emitted vertices. Validate the current fields after every
  // pre-callback, including the selected LINE replay and native fallbacks.
  for (int i = 0; i < shape->coordIndex.getNum(); ++i) {
    const int32_t index = shape->coordIndex[i];
    if (index < -1 || (index >= 0 && index >= count)) {
      diagnostic = "Indexed shape coordinate index out of bounds or invalid";
      return false;
    }
    if (index < 0) continue;
    if (coordinates->is3D()) {
      const SbVec3f & value = coordinates->get3(index);
      for (int component = 0; component < 3; ++component)
        if (!std::isfinite(value[component])) {
          diagnostic = "Non-finite consumed indexed shape coordinate";
          return false;
        }
    } else {
      const SbVec4f & value = coordinates->get4(index);
      for (int component = 0; component < 4; ++component)
        if (!std::isfinite(value[component])) {
          diagnostic = "Non-finite consumed indexed shape coordinate";
          return false;
        }
    }
  }
  if (!faces && !lines) return true;
  const auto * normals = SoNormalElement::getInstance(state);
  if (!normals || normals->getNum() == 0) return true;
  CoinRenderDirectGeometryView view;
  // Normal bounds depend on geometry presence, not coordinate values/dimension.
  view.positions = CoinRenderSpan<SbVec3f>(nullptr,
    size_t(SoCoordinateElement::getInstance(state)->getNum()));
  view.normals = CoinRenderSpan<SbVec3f>(normals->getArrayPtr(), size_t(normals->getNum()));
  view.coordIndex = CoinRenderSpan<int32_t>(shape->coordIndex.getValues(0), size_t(shape->coordIndex.getNum()));
  view.normalIndex = CoinRenderSpan<int32_t>(shape->normalIndex.getValues(0), size_t(shape->normalIndex.getNum()));
  view.normalBinding = SoNormalBindingElement::get(state);
  normalizeIndexedBindings(shape, view);
  return faces ? CoinRenderIndexedGeometryCore::validateFaceNormals(view, diagnostic)
               : CoinRenderIndexedGeometryCore::validateLineNormals(view, diagnostic);
}
}

bool CoinRenderActionP::acceptNode(SoNode * node)
{
  if (this->master->hasTerminated()) return false;
  this->master->setCurrentNode(node);
  if (this->master->abortNow()) return false;
  if (node->isOfType(SoShape::getClassTypeId())) this->builder.beginSortingOccurrence();
  return true;
}
void CoinRenderActionP::portableNodeMethod(SoAction * action, SoNode * node)
{
  auto * render = static_cast<CoinRenderAction *>(action);
  if (render->getPimpl()->technicalRttReplay && node->isOfType(SoCallback::getClassTypeId())) return;
  if (!render->pimpl->acceptNode(node)) return;
  SoNode::callbackS(action, node);
}

void
CoinRenderActionP::shapeContentMethod(SoAction * action, SoNode * node)
{
  auto * render = static_cast<CoinRenderAction *>(action);
  if (!render->pimpl->acceptNode(node)) return;
  render->invokePreCallbacks(node);
  if (render->getCurrentResponse() == SoCallbackAction::CONTINUE) {
#ifdef HAVE_VRML97
    if (node->getNodeType() == SoNode::VRML2 && !CoinRenderFramePlanBuilder::isShapeInvisible(render)) {
      std::string error;
      if (!validateVRMLGeometry(render, node, error)) {
        render->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(error.c_str())));
        render->setTerminated(TRUE);
        render->invokePostCallbacks(node);
        return;
      }
    }
#endif
#ifdef HAVE_VRML97
    const bool emptyVRMLFace = node->isOfType(SoVRMLIndexedFaceSet::getClassTypeId()) &&
      !static_cast<SoVRMLIndexedFaceSet *>(node)->coord.getValue();
    const bool emptyVRMLLine = node->isOfType(SoVRMLIndexedLineSet::getClassTypeId()) &&
      !static_cast<SoVRMLIndexedLineSet *>(node)->coord.getValue();
    if (emptyVRMLFace || emptyVRMLLine) { render->invokePostCallbacks(node); return; }
#endif
    const bool box = render->pimpl->captureBoundingBox(render, node);
    // All pre-callbacks have now run. Guard the native fallback as well as
    // subclasses and LINE replay, which intentionally bypass direct capture.
    const bool generatesPrimitives = !box || !render->hasSingleShapeCallbacks(node->getTypeId());
    std::string diagnostic;
    if (generatesPrimitives && !validateIndexedShapeGeometry(render, node, diagnostic)) {
      render->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString(diagnostic.c_str())));
      render->setTerminated(TRUE);
    } else if (!box) {
#ifdef HAVE_VRML97
      SoState * state = render->getState();
      const bool vrml = node->getNodeType() == SoNode::VRML2;
      uint32_t packed = 0;
      if (vrml) {
        state->push();
        SbBool ccw = TRUE, solid = TRUE;
        bool installHints = false;
        if (node->isOfType(SoVRMLIndexedFaceSet::getClassTypeId())) {
          installHints = true;
          auto * shape = static_cast<SoVRMLIndexedFaceSet *>(node);
          ccw = shape->ccw.getValue(); solid = shape->solid.getValue();
        } else if (node->isOfType(SoVRMLElevationGrid::getClassTypeId())) {
          installHints = true;
          auto * shape = static_cast<SoVRMLElevationGrid *>(node);
          ccw = shape->ccw.getValue(); solid = shape->solid.getValue();
          if (shape->color.getValue()) shape->color.getValue()->doAction(render);
        } else if (node->isOfType(SoVRMLExtrusion::getClassTypeId())) {
          installHints = true;
          auto * shape = static_cast<SoVRMLExtrusion *>(node);
          ccw = shape->ccw.getValue(); solid = shape->solid.getValue();
        }
        if (installHints && !(SoOverrideElement::getFlags(state) & SoOverrideElement::SHAPE_HINTS))
          SoShapeHintsElement::set(state, node,
            ccw ? SoShapeHintsElement::COUNTERCLOCKWISE : SoShapeHintsElement::CLOCKWISE,
            solid ? SoShapeHintsElement::SOLID : SoShapeHintsElement::UNKNOWN_SHAPE_TYPE,
            SoShapeHintsElement::FACE_TYPE_AS_IS);
        const bool line = node->isOfType(SoVRMLIndexedLineSet::getClassTypeId());
        const bool point = node->isOfType(SoVRMLPointSet::getClassTypeId());
        if (line || point) {
          SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
          SoMultiTextureEnabledElement::disableAll(state);
          SoNode * colors = line ? static_cast<SoVRMLIndexedLineSet *>(node)->color.getValue()
                                 : static_cast<SoVRMLPointSet *>(node)->color.getValue();
          if (!colors) {
            SbColor color = SoLazyElement::getEmissive(state);
            if (line) color += SoLazyElement::getDiffuse(state, 0);
            for (int c = 0; c < 3; ++c) color[c] = SbClamp(color[c], 0.0f, 1.0f);
            packed = color.getPackedValue();
            SoLazyElement::setPacked(state, node, 1, &packed, FALSE);
          } else if (point) colors->doAction(render);
        }
      }
      node->callback(render);
      if (vrml) state->pop();
#else
      node->callback(render);
#endif
    } else render->pimpl->observeBoundingBox(render, node);
  }
  render->invokePostCallbacks(node);
}

// VRML lights and fog have GL-only implementations in native Coin. Keep
// their interpretation here, scoped by the same Coin state stack as geometry.
void
CoinRenderActionP::vrmlStateMethod(SoAction * base, SoNode * node)
{
#ifdef HAVE_VRML97
  auto * action = static_cast<CoinRenderAction *>(base);
  if (!action->pimpl->acceptNode(node)) return;
  action->invokePreCallbacks(node);
  if (action->getCurrentResponse() == SoCallbackAction::CONTINUE) {
    SoState * state = action->getState();
    if (node->isOfType(SoVRMLMaterial::getClassTypeId())) {
      const auto * material = static_cast<const SoVRMLMaterial *>(node);
      bool finite = true;
      const uint32_t flags = SoOverrideElement::getFlags(state);
      const auto colorFinite = [&](const SoSFColor & field, uint32_t mask) {
        if (field.isIgnored() || (flags & mask)) return true;
        const SbColor & value = field.getValue();
        return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
      };
      finite = colorFinite(material->diffuseColor, SoOverrideElement::DIFFUSE_COLOR) &&
        colorFinite(material->emissiveColor, SoOverrideElement::EMISSIVE_COLOR) &&
        colorFinite(material->specularColor, SoOverrideElement::SPECULAR_COLOR);
      if (!material->diffuseColor.isIgnored() && !material->ambientIntensity.isIgnored() &&
          !(flags & SoOverrideElement::AMBIENT_COLOR))
        finite = finite && std::isfinite(material->ambientIntensity.getValue());
      if (!material->transparency.isIgnored() && !(flags & SoOverrideElement::TRANSPARENCY))
        finite = finite && std::isfinite(material->transparency.getValue());
      if (!material->shininess.isIgnored() && !(flags & SoOverrideElement::SHININESS))
        finite = finite && std::isfinite(material->shininess.getValue());
      if (!finite) {
        action->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Non-finite consumed VRML material")));
        action->setTerminated(TRUE);
      } else node->callback(action);
    } else if (node->isOfType(SoVRMLLight::getClassTypeId())) {
      action->pimpl->builder.recordVRMLLight(action, node);
    } else if (node->isOfType(SoVRMLPixelTexture::getClassTypeId()) ||
               node->isOfType(SoVRMLImageTexture::getClassTypeId())) {
      node->callback(action);
      const int unit = SoTextureUnitElement::get(state);
      if (unit >= 0 && unit < int(COIN_RENDER_MAX_TEXTURE_UNITS) &&
          SoMultiTextureEnabledElement::get(state, unit)) {
        SbVec2s size;
        int components;
        SoMultiTextureImageElement::Wrap s, t;
        SoMultiTextureImageElement::Model model;
        SbColor blend;
        const unsigned char * bytes = SoMultiTextureImageElement::get(
          state, unit, size, components, s, t, model, blend);
        const auto * texture = static_cast<const SoVRMLTexture *>(node);
        // Native VRML GL uses CLAMP_TO_EDGE for repeat=false; generic VRML
        // callbacks instead install CLAMP_TO_BORDER, which is not the GL model.
        SoMultiTextureImageElement::set(state, node, unit, size, components, bytes,
          texture->repeatS.getValue() ? SoMultiTextureImageElement::REPEAT : SoMultiTextureImageElement::CLAMP,
          texture->repeatT.getValue() ? SoMultiTextureImageElement::REPEAT : SoMultiTextureImageElement::CLAMP,
          model, blend);
        bool authored = node->isOfType(SoVRMLPixelTexture::getClassTypeId());
        if (!authored) {
          const auto * image = static_cast<const SoVRMLImageTexture *>(node)->getImage();
          authored = image && image->hasData();
        }
        if (authored) action->pimpl->builder.registerAuthoredTextureImage(bytes, false, nullptr, 0);
      }
    } else if (node->isOfType(SoVRMLBackground::getClassTypeId())) {
      auto * background = static_cast<SoVRMLBackground *>(node);
      bool finite = true;
      for (const auto * colors : {&background->skyColor, &background->groundColor})
        for (int i = 0; i < colors->getNum(); ++i)
          for (int c = 0; c < 3; ++c) finite = finite && std::isfinite((*colors)[i][c]);
      for (const auto * angles : {&background->skyAngle, &background->groundAngle})
        for (int i = 0; i < angles->getNum(); ++i) finite = finite && std::isfinite((*angles)[i]);
      if (!finite || (background->skyAngle.getNum() && !background->skyColor.getNum()) ||
          (background->groundAngle.getNum() && !background->groundColor.getNum())) {
        action->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Invalid VRML background colors or angles")));
        action->setTerminated(TRUE);
      } else {
        const size_t stereoCount = action->pimpl->frameStereoStates.size();
        background->coinRender(action);
        // The occurrence-local background camera is gone after capture.
        action->pimpl->frameStereoStates.resize(stereoCount);
      }
    } else if (node->isOfType(SoVRMLFog::getClassTypeId())) {
      const auto * fog = static_cast<const SoVRMLFog *>(node);
      const float visibility = fog->visibilityRange.getValue();
      const SbString & type = fog->fogType.getValue();
      // Native Coin uses its camera far plane when visibilityRange <= 0.
      // EXPONENTIAL maps to SMOKE (GL_EXP2), rather than Inventor's FOG.
      const int mode = type == "EXPONENTIAL" ? SoEnvironmentElement::SMOKE : SoEnvironmentElement::HAZE;
      SoEnvironmentElement::set(state, node,
        SoEnvironmentElement::getAmbientIntensity(state),
        SoEnvironmentElement::getAmbientColor(state),
        SoEnvironmentElement::getLightAttenuation(state), mode,
        fog->color.getValue(), visibility);
    } else {
      // The generic VRMLShape callback sets BASE_COLOR before its own push.
      // Enclose that callback so the setting cannot escape this occurrence.
      const auto * shape = static_cast<const SoVRMLShape *>(node);
      if (shape->appearance.getValue() &&
          !shape->appearance.getValue()->isOfType(SoVRMLAppearance::getClassTypeId())) {
        action->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("VRML Shape appearance must be an Appearance node")));
        action->setTerminated(TRUE);
      } else {
        state->push();
        node->callback(action);
        state->pop();
      }
    }
  }
  action->invokePostCallbacks(node);
#else
  node->callback(static_cast<SoCallbackAction *>(base));
#endif
}

void
CoinRenderActionP::markerContentMethod(SoAction * action, SoNode * node)
{
  // Custom subclasses retain their own virtual callback contract.
  if (node->getTypeId() != SoMarkerSet::getClassTypeId() &&
      node->getTypeId() != SoIndexedMarkerSet::getClassTypeId()) {
    shapeContentMethod(action, node);
    return;
  }
  auto * render = static_cast<CoinRenderAction *>(action);
  if (!render->pimpl->acceptNode(node)) return;
  render->invokePreCallbacks(node);
  if (render->getCurrentResponse() == SoCallbackAction::CONTINUE) {
    if (render->pimpl->captureBoundingBox(render, node)) {
      render->pimpl->observeBoundingBox(render, node);
      render->invokePostCallbacks(node);
      return;
    }
    const bool observers = !render->hasSingleShapeCallbacks(node->getTypeId());
    if (render->pimpl->builder.captureMarkerContent(render, node, observers) && observers) {
      // Native marker rendering disables texture-coordinate functions and
      // lighting. Retain inherited point observers in the same safe scope;
      // our point callback suppresses their legacy one-pixel geometry.
      SoState * state = render->getState();
      state->push();
      SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
      SoMultiTextureEnabledElement::disableAll(state);
      node->callback(render);
      state->pop();
    }
  }
  render->invokePostCallbacks(node);
}

void
CoinRenderActionP::screenContentMethod(SoAction * action, SoNode * node)
{
  // Subclasses retain their virtual callback and geometry contract.
  if (node->getTypeId() != SoImage::getClassTypeId() &&
      node->getTypeId() != SoText2::getClassTypeId()) {
    shapeContentMethod(action, node);
    return;
  }
  auto * render = static_cast<CoinRenderAction *>(action);
  if (!render->pimpl->acceptNode(node)) return;
  render->invokePreCallbacks(node);
  if (render->getCurrentResponse() == SoCallbackAction::CONTINUE) {
    if (render->pimpl->captureBoundingBox(render, node)) {
      render->pimpl->observeBoundingBox(render, node);
      render->invokePostCallbacks(node);
      return;
    }
    render->pimpl->builder.captureScreenContent(render, node);
    // Native image primitive observers still run. Our triangle callback
    // suppresses that legacy quad, leaving only the captured screen pixels.
    if (!render->hasSingleShapeCallbacks(node->getTypeId())) node->callback(render);
  }
  render->invokePostCallbacks(node);
}

SoCallbackAction::Response
CoinRenderActionP::shapePostCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * self = static_cast<CoinRenderActionP *>(userdata);
  self->builder.recordRasterNormal(action, node);
  self->builder.endShape();
  self->endTranslationShape();
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::rasterGeneratorPostCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  static_cast<CoinRenderActionP *>(userdata)->builder.recordRasterGenerator(action, node);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::rasterStatePostCB(void * userdata, SoCallbackAction * action, const SoNode *)
{
  // Native GL texgen callbacks are reapplied when a state stack is popped.
  static_cast<CoinRenderActionP *>(userdata)->builder.synchronizeRasterGenerators(action);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::textureCombinePreCB(void * userdata, SoCallbackAction* action, const SoNode* node)
{
  const int unit = SoTextureUnitElement::get(action->getState());
  if (unit < 0 || unit >= static_cast<int>(COIN_RENDER_MAX_TEXTURE_UNITS)) {
    auto* p = static_cast<CoinRenderActionP*>(userdata);
    p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("At most eight texture units are supported")));
    return SoCallbackAction::ABORT;
  }
  // Coin's generic callback implementation does not update this element.
  // Use the node's own field/override interpretation with our enabled state.
  const_cast<SoTextureCombine*>(static_cast<const SoTextureCombine*>(node))->doAction(action);
  return SoCallbackAction::CONTINUE;
}

void
CoinRenderActionP::alphaTestMethod(SoAction * action, SoNode * node)
{
  auto * render = static_cast<CoinRenderAction *>(action);
  if (!render->pimpl->acceptNode(node)) return;
  render->invokePreCallbacks(node);
  if (render->getCurrentResponse() == SoCallbackAction::CONTINUE) {
    if (!coin_render_capture_alpha_node(*static_cast<const SoAlphaTest *>(node), render->getState())) {
      render->pimpl->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("SoAlphaTest has invalid function or NaN reference")));
      render->setTerminated(TRUE);
    } else {
      // Preserve subclass callback behavior after interpreting inherited fields.
      // This virtual callback remains responsible for any subclass-specific state.
      node->callback(render);
    }
  }
  render->invokePostCallbacks(node);
}

// Match SoCamera::GLRender before its model-matrix step. Orthographic
// sources also become perspective frusta in stereo. Coin's callback already
// installed CROP's viewport; do not install the returned adjustment again.
static bool shadowStereoCamera(const SoCamera &camera, const SbViewportRegion &viewport,
                               SbViewVolume &volume, SbMatrix &view, SbMatrix &projection,
                               std::string &diagnostic)
{
  SbViewportRegion adjusted;
  volume = camera.getViewVolume(viewport, adjusted, SbMatrix::identity());
  const float nearDistance = volume.getNearDist();
  const float farDistance = nearDistance + volume.getDepth();
  const float width = volume.getWidth(), height = volume.getHeight();
  const float adjustment = camera.getStereoAdjustment(), balance = camera.getBalanceAdjustment();
  float focal = camera.focalDistance.getValue() * balance;
  if (!std::isfinite(nearDistance) || nearDistance <= 0.0f || !std::isfinite(farDistance) ||
      farDistance <= nearDistance || !std::isfinite(width) || width <= 0.0f ||
      !std::isfinite(height) || height <= 0.0f || !std::isfinite(adjustment) ||
      !std::isfinite(balance) || !std::isfinite(focal)) {
    diagnostic = "Stereo shadow-map camera requires a finite nondegenerate frustum";
    return false;
  }
  if (focal < nearDistance)
    focal = nearDistance;
  float offset = adjustment * 0.5f;
  if (camera.getStereoMode() == SoCamera::LEFT_VIEW)
    offset = -offset;
  SbVec3f right = volume.getProjectionDirection().cross(volume.getViewUp());
  const float axisLength = right.normalize();
  if (!std::isfinite(axisLength) || axisLength == 0.0f) {
    diagnostic = "Stereo shadow-map camera has no finite eye-separation axis";
    return false;
  }
  volume.getMatrices(view, projection);
  SbMatrix cameraTransform, inverseProjection;
  if (!coin_render_shadow_camera_math::inverse(view, cameraTransform) ||
      !coin_render_shadow_camera_math::inverse(projection, inverseProjection)) {
    diagnostic = "Stereo shadow-map camera has an invalid camera transform";
    return false;
  }
  const float halfWidth = width * 0.5f, halfHeight = height * 0.5f;
  const float shift = offset * nearDistance / focal;
  const float left = -halfWidth - shift, rightPlane = halfWidth - shift;
  if (!std::isfinite(shift) || !std::isfinite(left) || !std::isfinite(rightPlane) ||
      left >= rightPlane || halfHeight <= 0.0f) {
    diagnostic = "Stereo shadow-map camera skew is non-finite or degenerate";
    return false;
  }
  volume.frustum(left, rightPlane, -halfHeight, halfHeight, nearDistance, farDistance);
  volume.transform(cameraTransform);
  SbMatrix eyeTransform;
  eyeTransform.setTranslate(right * offset);
  volume.transform(eyeTransform);
  volume.getMatrices(view, projection);
  SbMatrix inverseView;
  if (!coin_render_shadow_camera_math::inverse(view, inverseView) ||
      !coin_render_shadow_camera_math::inverse(projection, inverseProjection)) {
    diagnostic = "Stereo shadow-map camera produces invalid view/projection matrices";
    return false;
  }
  return true;
}

SoCallbackAction::Response
CoinRenderActionP::cameraStereoPreCB(void * data, SoCallbackAction * action, const SoNode * node)
{
  auto * owner = static_cast<CoinRenderActionP *>(data);
  owner->stereoCameraViewport = SoViewportRegionElement::get(action->getState());
  const auto * camera = static_cast<const SoCamera *>(node);
  if (camera->getStereoMode() == SoCamera::MONOSCOPIC || owner->capturingShadowScene)
    return SoCallbackAction::CONTINUE;
  const int mapping = camera->viewportMapping.getValue();
  bool valid = mapping >= SoCamera::CROP_VIEWPORT_FILL_FRAME && mapping <= SoCamera::LEAVE_ALONE;
  valid = valid && (camera->getStereoMode() == SoCamera::LEFT_VIEW || camera->getStereoMode() == SoCamera::RIGHT_VIEW) &&
    std::isfinite(camera->nearDistance.getValue()) && camera->nearDistance.getValue() > 0 &&
    std::isfinite(camera->farDistance.getValue()) && camera->farDistance.getValue() > camera->nearDistance.getValue() &&
    std::isfinite(camera->aspectRatio.getValue()) && camera->aspectRatio.getValue() > 0 &&
    std::isfinite(camera->focalDistance.getValue()) && std::isfinite(camera->getStereoAdjustment()) &&
    std::isfinite(camera->getBalanceAdjustment());
  for (int i=0; i<3; ++i) valid = valid && std::isfinite(camera->position.getValue()[i]);
  const SbRotation rotation = camera->orientation.getValue();
  for (int i=0; i<4; ++i) valid = valid && std::isfinite(rotation.getValue()[i]);
  if (camera->isOfType(SoPerspectiveCamera::getClassTypeId())) {
    const float angle = static_cast<const SoPerspectiveCamera *>(camera)->heightAngle.getValue();
    valid = valid && std::isfinite(angle) && angle > 0 && angle < 3.14159265358979323846f;
  } else if (camera->isOfType(SoOrthographicCamera::getClassTypeId())) {
    const float height = static_cast<const SoOrthographicCamera *>(camera)->height.getValue();
    valid = valid && std::isfinite(height) && height > 0;
  }
  if (!valid) {
    owner->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Active stereo camera has invalid fields or settings")));
    return SoCallbackAction::ABORT;
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::cameraStereoPostCB(void * userdata, SoCallbackAction * action,
                                      const SoNode * node)
{
  auto * owner = static_cast<CoinRenderActionP *>(userdata);
  const auto * camera = static_cast<const SoCamera *>(node);
  owner->frameStereoStates.push_back({camera, static_cast<int>(camera->getStereoMode()),
    camera->getStereoAdjustment(), camera->getBalanceAdjustment()});
  if (owner->capturingShadowScene && camera->getStereoMode() == SoCamera::MONOSCOPIC)
    owner->builder.registerShadowMapCameraPose(camera, action->getModelMatrix(),
                                                 action->getViewingMatrix());
  if (camera->getStereoMode() == SoCamera::MONOSCOPIC) return SoCallbackAction::CONTINUE;
  if (owner->capturingShadowScene) {
    SbViewVolume volume;
    SbMatrix view, projection;
    std::string diagnostic;
    if (!shadowStereoCamera(*camera, action->getViewportRegion(), volume, view, projection,
                            diagnostic)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
                                                             CoinRenderDiagnosticDomain::FRAME_PLAN,
                                                             SbString(diagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    SoState *state = action->getState();
    SbBool identity;
    const SbMatrix &model = SoModelMatrixElement::get(state, identity);
    SbMatrix inverseModel;
    if (!coin_render_shadow_camera_math::inverse(model, inverseModel)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Stereo shadow-map camera has a non-invertible model transform")));
      return SoCallbackAction::ABORT;
    }
    if (!identity) {
      view.multRight(inverseModel);
      volume.transform(model);
    }
    SbMatrix inverseView;
    if (!coin_render_shadow_camera_math::inverse(view, inverseView)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Stereo shadow-map camera model/view product is non-finite or non-invertible")));
      return SoCallbackAction::ABORT;
    }
    SoNode *mutableCamera = const_cast<SoCamera *>(camera);
    SoViewVolumeElement::set(state, mutableCamera, volume);
    SoProjectionMatrixElement::set(state, mutableCamera, projection);
    SoViewingMatrixElement::set(state, mutableCamera, view);
    owner->builder.registerShadowMapCameraPose(camera, model, view);
    return SoCallbackAction::CONTINUE;
  }
  SbViewVolume volume;
  SbMatrix view, projection;
  std::string diagnostic;
  if (!shadowStereoCamera(*camera, owner->stereoCameraViewport, volume, view, projection, diagnostic)) {
    owner->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(diagnostic.c_str())));
    return SoCallbackAction::ABORT;
  }
  SoState * state = action->getState();
  SbBool identity;
  const SbMatrix & model = SoModelMatrixElement::get(state, identity);
  if (!identity) {
    SbMatrix inverseModel;
    if (!coin_render_shadow_camera_math::inverse(model, inverseModel)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("Stereo camera has a non-invertible model transform")));
      return SoCallbackAction::ABORT;
    }
    view.multRight(inverseModel);
    volume.transform(model);
  }
  SoNode * mutableCamera = const_cast<SoCamera *>(camera);
  SoViewVolumeElement::set(state, mutableCamera, volume);
  SoProjectionMatrixElement::set(state, mutableCamera, projection);
  SoViewingMatrixElement::set(state, mutableCamera, view);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::unsupportedEffectPreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  if (node->isOfType(SoCamera::getClassTypeId())) {
    auto * p = static_cast<CoinRenderActionP *>(userdata);
    if (!p->capturingShadowScene) return SoCallbackAction::CONTINUE;
    const SoType type = node->getTypeId();
    if (type != SoPerspectiveCamera::getClassTypeId() &&
        type != SoOrthographicCamera::getClassTypeId()) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("shadowMapScene camera type is not qualified")));
      return SoCallbackAction::ABORT;
    }
    std::string viewportDiagnostic;
    if (!validShadowMapViewport(action->getViewportRegion(), viewportDiagnostic)) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
                                                         CoinRenderDiagnosticDomain::FRAME_PLAN,
                                                         SbString(viewportDiagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    const auto *camera = static_cast<const SoCamera *>(node);
    const int mapping = camera->viewportMapping.getValue();
    const bool validMapping = mapping == SoCamera::ADJUST_CAMERA ||
                              mapping == SoCamera::LEAVE_ALONE ||
                              mapping == SoCamera::CROP_VIEWPORT_FILL_FRAME ||
                              mapping == SoCamera::CROP_VIEWPORT_LINE_FRAME ||
                              mapping == SoCamera::CROP_VIEWPORT_NO_FRAME;
    const SbVec3f position = camera->position.getValue();
    const SbRotation orientation = camera->orientation.getValue();
    const float *rotation = orientation.getValue();
    bool finiteParameters = std::isfinite(camera->aspectRatio.getValue()) &&
                            std::isfinite(camera->nearDistance.getValue()) &&
                            std::isfinite(camera->farDistance.getValue()) &&
                            std::isfinite(camera->focalDistance.getValue());
    for (int i = 0; i < 3; ++i) finiteParameters = finiteParameters && std::isfinite(position[i]);
    for (int i = 0; i < 4; ++i) finiteParameters = finiteParameters && std::isfinite(rotation[i]);
    bool valid = validMapping && finiteParameters && camera->aspectRatio.getValue() > 0.0f &&
                 camera->farDistance.getValue() > camera->nearDistance.getValue();
    if (type == SoPerspectiveCamera::getClassTypeId()) {
      const float angle = static_cast<const SoPerspectiveCamera *>(camera)->heightAngle.getValue();
      finiteParameters = finiteParameters && std::isfinite(angle);
      valid = valid && camera->nearDistance.getValue() > 0.0f &&
        std::isfinite(angle) && angle > 0.0f && angle < 3.14159265358979323846f;
    } else {
      const float height = static_cast<const SoOrthographicCamera *>(camera)->height.getValue();
      finiteParameters = finiteParameters && std::isfinite(height);
      valid = valid && std::isfinite(height) && height > 0.0f;
    }
    const auto stereo = camera->getStereoMode();
    if (stereo != SoCamera::MONOSCOPIC) {
      const float focal = camera->focalDistance.getValue() * camera->getBalanceAdjustment();
      finiteParameters = finiteParameters && std::isfinite(camera->getStereoAdjustment()) &&
                         std::isfinite(camera->getBalanceAdjustment()) && std::isfinite(focal);
      valid = valid && (stereo == SoCamera::LEFT_VIEW || stereo == SoCamera::RIGHT_VIEW) &&
              camera->nearDistance.getValue() > 0.0f &&
              std::isfinite(camera->getStereoAdjustment()) &&
              std::isfinite(camera->getBalanceAdjustment()) && std::isfinite(focal);
    }
    if (!valid) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(
        finiteParameters ? CoinRenderAction::UNSUPPORTED : CoinRenderAction::INVALID_SCENE,
        CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString(finiteParameters ? "shadowMapScene camera has unsupported or invalid parameters"
                                  : "shadowMapScene camera has non-finite parameters")));
      return SoCallbackAction::ABORT;
    }
    p->builder.registerShadowMapCamera(camera);
    return SoCallbackAction::CONTINUE;
  }

  const char * diagnostic = nullptr;
  if (node->isOfType(SoBumpMap::getClassTypeId())) {
    const auto * bump = static_cast<const SoBumpMap *>(node);
    SoBumpMapElement::set(action->getState(), const_cast<SoBumpMap *>(bump), nullptr);
    SbVec2s size;
    int components = 0;
    bump->image.getValue(size, components);
    if (size != SbVec2s(0, 0) || bump->filename.getValue().getLength() != 0)
      static_cast<CoinRenderActionP *>(userdata)->encounteredActiveBump = true;
    return SoCallbackAction::CONTINUE;
  } else if (node->isOfType(SoTextureCubeMap::getClassTypeId())) {
    auto * owner = static_cast<CoinRenderActionP *>(userdata);
    std::string error;
    if (!owner->builder.captureCubeNode(action, static_cast<const SoTextureCubeMap *>(node), error)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(error.c_str())));
      return SoCallbackAction::ABORT;
    }
    return SoCallbackAction::CONTINUE;
  } else if (node->isOfType(SoSceneTextureCubeMap::getClassTypeId())) {
    return SoCallbackAction::CONTINUE;
  } else if (node->isOfType(SoShaderProgram::getClassTypeId())) {
    auto *owner = static_cast<CoinRenderActionP *>(userdata);
    const auto found = owner->portableShaderModules.find(const_cast<SoShaderProgram *>(
      static_cast<const SoShaderProgram *>(node)));
    if (found != owner->portableShaderModules.end()) {
      bool active = false;
      std::string error;
      if (!coin_render_shader_active(*found->second, active, error)) {
        owner->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(error.c_str())));
        return SoCallbackAction::ABORT;
      }
      // An empty/inactive program replaces, rather than inherits, the previous
      // module. Separator restores its own preceding element afterwards.
      CoinRenderShaderModuleElement::set(action->getState(), active ? found->second :
        std::shared_ptr<const CoinRenderShaderRegistration>());
      if (!active && owner->builder.hasActiveShadowGroup()) {
        owner->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString("Empty SoShaderProgram disables the active shadow shader; this profile is unsupported")));
        return SoCallbackAction::ABORT;
      }
      // Resource and numeric proofs follow only at consumed active draws.
      return SoCallbackAction::CONTINUE;
    }
    bool active = false;
    std::string activationError;
    if (!coin_render_shader_active(static_cast<const SoShaderProgram *>(node), active, activationError)) {
      owner->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(activationError.c_str())));
      return SoCallbackAction::ABORT;
    }
    if (!active) {
      CoinRenderShaderModuleElement::set(action->getState(),
        std::shared_ptr<const CoinRenderShaderRegistration>());
      if (!owner->builder.hasActiveShadowGroup()) return SoCallbackAction::CONTINUE;
      // Empty programs remove Coin's generated shadow program, changing both
      // lighting/shadows and texture projection without changing ShadowStyle.
      diagnostic = "Empty SoShaderProgram disables the active shadow shader; this profile is unsupported";
    } else diagnostic = "SoShaderProgram has no portable shader contract";
  }
  if (!diagnostic) return SoCallbackAction::CONTINUE;
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  p->setDiagnostic(CoinRenderDiagnosticShell::action(
    CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
    SbString(diagnostic)));
  return SoCallbackAction::ABORT;
}

void
CoinRenderActionP::initCallbacks()
{
  this->master->addPreCallback(SoNode::getClassTypeId(), rttMutationPreCB, this);
  this->master->addTriangleCallback(SoShape::getClassTypeId(), triangleCB, this);
  this->master->addLineSegmentCallback(SoShape::getClassTypeId(), lineCB, this);
  this->master->addPointCallback(SoShape::getClassTypeId(), pointCB, this);

  this->master->addPreCallback(SoCamera::getClassTypeId(), cameraStereoPreCB, this);
  this->master->addPreCallback(SoCamera::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPostCallback(SoCamera::getClassTypeId(), cameraStereoPostCB, this);
  this->master->addPreCallback(SoTexture2::getClassTypeId(), textureImagePreCB, this);
  this->master->addPreCallback(SoTexture3::getClassTypeId(), textureVolumePreCB, this);
  this->master->addPostCallback(SoTextureCoordinateObject::getClassTypeId(), rasterGeneratorPostCB, this);
  this->master->addPostCallback(SoTextureCoordinatePlane::getClassTypeId(), rasterGeneratorPostCB, this);
  this->master->addPostCallback(SoTextureCoordinateEnvironment::getClassTypeId(), rasterGeneratorPostCB, this);
  this->master->addPostCallback(SoTextureCoordinateReflectionMap::getClassTypeId(), rasterGeneratorPostCB, this);
  this->master->addPostCallback(SoTextureCoordinateNormalMap::getClassTypeId(), rasterGeneratorPostCB, this);
  this->master->addPreCallback(SoShape::getClassTypeId(), textureUnitsPreCB, this);
  this->master->addPostCallback(SoShape::getClassTypeId(), shapePostCB, this);
  this->master->addPreCallback(SoTranslation::getClassTypeId(), translationPreCB, this);
  this->master->addPreCallback(SoTransform::getClassTypeId(), translationPreCB, this);
  this->master->addPreCallback(SoLight::getClassTypeId(), lightPreCB, this);
  this->master->addPreCallback(SoTextureCombine::getClassTypeId(), textureCombinePreCB, this);
  this->master->addPreCallback(SoBumpMap::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoTextureCubeMap::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoSceneTextureCubeMap::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoShadowGroup::getClassTypeId(), shadowGroupPreCB, this);
  this->master->addPostCallback(SoShadowGroup::getClassTypeId(), shadowGroupPostCB, this);
  this->master->addPreCallback(SoShadowStyle::getClassTypeId(), shadowStylePreCB, this);
  this->master->addPreCallback(SoShaderProgram::getClassTypeId(), unsupportedEffectPreCB, this);
  this->master->addPreCallback(SoDepthBuffer::getClassTypeId(), depthBufferPreCB, this);
  this->master->addPreCallback(SoAnnotation::getClassTypeId(), annotationPreCB, this);
  this->master->addPostCallback(SoAnnotation::getClassTypeId(), annotationPostCB, this);
  this->master->addPreCallback(SoSeparator::getClassTypeId(), managerLayerPreCB, this);
  this->master->addPostCallback(SoSeparator::getClassTypeId(), managerLayerPostCB, this);
  this->master->addPreCallback(SoSceneTexture2::getClassTypeId(), sceneTexturePreCB, this);
  this->master->addPreCallback(SoSceneTextureCubeMap::getClassTypeId(), sceneCubeTexturePreCB, this);
  this->master->addPreCallback(SoIndexedFaceSet::getClassTypeId(), indexedFaceSetPreCB, this);
  this->master->addPreCallback(SoIndexedLineSet::getClassTypeId(), indexedLineSetPreCB, this);
  this->master->addPostCallback(SoNode::getClassTypeId(), rasterStatePostCB, this);
  this->captureCallbackRevision = this->master->callbackRegistrationRevision();
}

void
CoinRenderActionP::triangleCB(void * userdata,
                               SoCallbackAction * action,
                               const SoPrimitiveVertex * v0,
                               const SoPrimitiveVertex * v1,
                               const SoPrimitiveVertex * v2)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->boundingBoxObservers) return;
  const auto type = action->getCurPathTail()->getTypeId();
  if (type == SoImage::getClassTypeId() || type == SoText2::getClassTypeId()) return;
  p->builder.addTriangle(action, v0, v1, v2);
}

void
CoinRenderActionP::lineCB(void * userdata,
                           SoCallbackAction * action,
                           const SoPrimitiveVertex * v0,
                           const SoPrimitiveVertex * v1)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->boundingBoxObservers) return;
  p->builder.addLine(action, v0, v1);
}

void
CoinRenderActionP::pointCB(void * userdata,
                            SoCallbackAction * action,
                            const SoPrimitiveVertex * vertex)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->boundingBoxObservers) return;
  const auto type = action->getCurPathTail()->getTypeId();
  if (type == SoMarkerSet::getClassTypeId() || type == SoIndexedMarkerSet::getClassTypeId()) return;
  p->builder.addPoint(action, vertex);
}

namespace {
struct CubeCameraOverride {
  const SoCamera * original;
  SoCamera * replacement;
  CoinRenderCameraSnapshot * captured;
  CoinRenderActionP * owner;
  static SoCallbackAction::Response post(void * data,SoCallbackAction * action,const SoNode * node) {
    auto * self=static_cast<CubeCameraOverride *>(data);
    if(node!=(self->original?static_cast<const SoNode *>(self->original):self->replacement))return SoCallbackAction::CONTINUE;
    if(self->replacement->getStereoMode()!=SoCamera::MONOSCOPIC) {
      // The ordinary post callback sees the original camera node. Rebuild its
      // stereo volume from the private face orientation before extracting the
      // GL reset matrix; no original camera fields are changed.
      self->replacement->doAction(action);
      if(CoinRenderActionP::cameraStereoPostCB(self->owner,action,self->replacement)!=SoCallbackAction::CONTINUE)
        return SoCallbackAction::ABORT;
    }
    SoState * state=action->getState();
    SbBool identity;
    const SbMatrix & model=SoModelMatrixElement::get(state,identity);
    SbMatrix view=SoViewingMatrixElement::get(state);
    if(!identity) {
      const float determinant=model.det4();
      if(!std::isfinite(determinant) || determinant==0) {
        self->owner->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
          CoinRenderDiagnosticDomain::FRAME_PLAN,SbString("Cube RTT camera path is non-invertible")));
        return SoCallbackAction::ABORT;
      }
      // Match SoGLViewingMatrixElement::getResetMatrix, including its exact
      // order. doAction stores V*M^-1; GL renders with reset M^-1*V so geometry
      // before and after SoResetTransform, and later local transforms, agree.
      view.multRight(model);
      view.multLeft(model.inverse());
      SoViewingMatrixElement::set(state,self->replacement,view);
    }
    self->captured->viewMatrix=view;
    self->captured->projectionMatrixCoin=SoProjectionMatrixElement::get(action->getState());
    self->captured->isPerspective=self->replacement->isOfType(SoPerspectiveCamera::getClassTypeId());
    self->captured->nearDistance=self->replacement->nearDistance.getValue();
    self->captured->farDistance=self->replacement->farDistance.getValue();
    self->captured->focalDistance=self->replacement->focalDistance.getValue();
    self->captured->aspectRatio=self->replacement->aspectRatio.getValue();
    return SoCallbackAction::CONTINUE;
  }
  static SoCallbackAction::Response apply(void * data, SoCallbackAction * action, const SoNode * node) {
    const auto * self=static_cast<const CubeCameraOverride *>(data);
    if (node!=self->original) return SoCallbackAction::CONTINUE;
    // Traverse only the private camera's fields at the original path/model
    // transform. No field, notification, connection or node ID of the scene
    // is modified, and all subsequent cameras retain their native behavior.
    self->replacement->doAction(action);
    return SoCallbackAction::PRUNE;
  }
};
SbRotation cubeRttOrientation(unsigned face) {
  const float pi=3.14159265358979323846f;
  switch(face) {
  case 0:return SbRotation(SbVec3f(0,1,0),-pi/2)*SbRotation(SbVec3f(1,0,0),pi);
  case 1:return SbRotation(SbVec3f(0,1,0), pi/2)*SbRotation(SbVec3f(1,0,0),pi);
  case 2:return SbRotation(SbVec3f(1,0,0), pi/2);
  case 3:return SbRotation(SbVec3f(1,0,0),-pi/2);
  case 4:return SbRotation(SbVec3f(0,1,0), pi)*SbRotation(SbVec3f(0,0,1),pi);
  default:return SbRotation(SbVec3f(0,0,1),pi);
  }
}
bool validCubeCamera(const SoCamera * camera) {
  if (!camera || !std::isfinite(camera->nearDistance.getValue()) ||
      !std::isfinite(camera->farDistance.getValue()) ||
      camera->farDistance.getValue()<=camera->nearDistance.getValue() ||
      !std::isfinite(camera->aspectRatio.getValue()) || camera->aspectRatio.getValue()<=0 ||
      !std::isfinite(camera->focalDistance.getValue())) return false;
  for(int c=0;c<3;++c)if(!std::isfinite(camera->position.getValue()[c]))return false;
  if(camera->isOfType(SoPerspectiveCamera::getClassTypeId())) {
    const float angle=static_cast<const SoPerspectiveCamera *>(camera)->heightAngle.getValue();
    if(!std::isfinite(angle) || angle<=0 || angle>=3.14159265358979323846f || camera->nearDistance.getValue()<=0)return false;
  }
  if(camera->isOfType(SoOrthographicCamera::getClassTypeId())) {
    const float height=static_cast<const SoOrthographicCamera *>(camera)->height.getValue();
    if(!std::isfinite(height)||height<=0)return false;
  }
  return true;
}
}

SoCallbackAction::Response
CoinRenderActionP::sceneCubeTexturePreCB(void * userdata,SoCallbackAction * action,const SoNode * node)
{
  auto * p=static_cast<CoinRenderActionP *>(userdata);
  const auto * texture=static_cast<const SoSceneTextureCubeMap *>(node);
  SoState * state=action?action->getState():NULL;
  const auto failWithStatus=[&](const std::string & text, CoinRenderAction::Status status) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(status,
      CoinRenderDiagnosticDomain::FRAME_PLAN,SbString(text.c_str())));
    return SoCallbackAction::ABORT;
  };
  const auto fail=[&](const std::string & text) {
    return failWithStatus(text, CoinRenderAction::UNSUPPORTED);
  };
  if(!state)return fail("Cube RTT has no traversal state");
  const int unit=SoTextureUnitElement::get(state);
  if(unit==0 && SoTextureOverrideElement::getImageOverride(state))return SoCallbackAction::CONTINUE;
  if(unit<0 || unit>=int(COIN_RENDER_MAX_TEXTURE_UNITS))return fail("Cube RTT supports units 0..7");
  CoinRenderTextureFilter filter;
  if(!CoinRenderTextureSamplingCore::quality(SoTextureQualityElement::get(state),filter))
    return fail("Cube RTT quality must be finite in [0,1]");
  if(!texture->scene.getValue() || SoTextureQualityElement::get(state)==0) {
    // Removal revokes the resource and inherited alpha classification.
    SoMultiTextureImageElement::set(state,const_cast<SoSceneTextureCubeMap *>(texture),unit,
      SbVec2s(0,0),0,NULL,SoMultiTextureImageElement::CLAMP,SoMultiTextureImageElement::CLAMP,
      SoMultiTextureImageElement::MODULATE,SbColor(0,0,0));
    SoMultiTextureEnabledElement::set(state,const_cast<SoSceneTextureCubeMap *>(texture),unit,FALSE);
    if(unit==0 && texture->isOverride())SoTextureOverrideElement::setImageOverride(state,TRUE);
    return SoCallbackAction::CONTINUE;
  }
  const int model=texture->model.getValue();
  if((model!=SoSceneTextureCubeMap::MODULATE && model!=SoSceneTextureCubeMap::REPLACE &&
      model!=SoSceneTextureCubeMap::DECAL && model!=SoSceneTextureCubeMap::BLEND) ||
     !coin_render_scene_texture_policy_supported(texture->transparencyFunction.getValue()) ||
     (texture->wrapS.getValue()!=SoSceneTextureCubeMap::REPEAT && texture->wrapS.getValue()!=SoSceneTextureCubeMap::CLAMP) ||
     (texture->wrapT.getValue()!=SoSceneTextureCubeMap::REPEAT && texture->wrapT.getValue()!=SoSceneTextureCubeMap::CLAMP) ||
     (texture->wrapR.getValue()!=SoSceneTextureCubeMap::REPEAT && texture->wrapR.getValue()!=SoSceneTextureCubeMap::CLAMP))
    return fail("Invalid cube RTT model, wrap or transparency policy");
  for(int c=0;c<3;++c)if(!std::isfinite(texture->backgroundColor.getValue()[c]) ||
      !std::isfinite(texture->blendColor.getValue()[c]))return fail("Non-finite cube RTT background/blend");
  const SbVec2s size=texture->size.getValue();
  CoinRenderTextureImageSnapshot description;
  if(size[0]!=size[1] || !CoinRenderCubeTextureCore::describe(uint32_t(size[0]),false,description))
    return fail("Cube RTT requires six square POT faces in 1..2048 within atlas limits");
  std::string diagnostic;
  if (!p->sceneTexturePlan) return fail("Missing cube RTT graph");
  const bool bumpProducer = p->sceneTextureBumpProducer ||
    (p->target && !p->planOnly && !p->asyncTicket && p->target->getPimpl()->supportsOpaqueBump(false));
  const int producerTransparency = SoShapeStyleElement::getTransparencyType(state);
  const auto context = rttCaptureContext(*p, state, producerTransparency, bumpProducer);
  uint64_t producerId = 0;
  bool dependencyCycle = CoinRenderRttCaptureMemo::activeSource(*p->sceneTexturePlan, texture->getNodeId());
  if (dependencyCycle) return failWithStatus("SoSceneTexture2 dependency cycle", CoinRenderAction::INVALID_SCENE);
  if (p->sceneTextureCaptureMemo &&
      !p->sceneTextureCaptureMemo->find(texture, context, *p->sceneTexturePlan, producerId, diagnostic, dependencyCycle))
    return failWithStatus(diagnostic, dependencyCycle ? CoinRenderAction::INVALID_SCENE : CoinRenderAction::UNSUPPORTED);
  if (!producerId) {
    CoinRenderRttCaptureMemo::Baseline baseline;
    const uint64_t captureEpoch = p->sceneTextureCaptureMemo ? p->sceneTextureCaptureMemo->epoch : 0;
    bool hasEffects = true;
    if (p->sceneTextureCaptureMemo && p->sceneTextureCaptureMemo->allowed)
      hasEffects = rttSceneHasEffects(texture, &baseline);
    if (hasEffects) { baseline.complete=false; baseline.nodes.clear(); }
    const size_t chargeBefore = p->sceneTexturePlan ? p->sceneTexturePlan->chargedBytes : 0;
    bool captureStable = true;
    if(!p->sceneTexturePlan->enter(texture->getNodeId(),SbVec2i32(size[0],size[1]),diagnostic,true)) return fail(diagnostic);
    struct GraphScope {CoinRenderRttPlan & graph;~GraphScope(){graph.leave();}} graphScope{*p->sceneTexturePlan};
    CoinRenderRttCaptureMemo::TraceScope trace(p->sceneTextureCaptureMemo.get(), texture->getNodeId(),
                                              p->sceneTexturePlan->activeSources.size());
    SoNode * scene=texture->scene.getValue();
    // Search/copy can invoke callbacks too. Release previous proofs before it,
    // so a callback's removals/destructors observe the original ownership.
    if (hasEffects && p->sceneTextureCaptureMemo) p->sceneTextureCaptureMemo->invalidate();
    CoinRenderGuardedTraversal<SoSearchAction> search;search.setType(SoCamera::getClassTypeId());search.setInterest(SoSearchAction::FIRST);search.apply(scene);
    const SoCamera * original=search.getPath()?static_cast<const SoCamera *>(search.getPath()->getTail()):NULL;
    SoCamera * camera=original?static_cast<SoCamera *>(original->copy(FALSE)):new SoPerspectiveCamera;
    camera->ref();
    SoSeparator * wrapper=NULL;
    struct CameraScope {SoCamera * camera;SoSeparator * & wrapper;~CameraScope(){if(wrapper)wrapper->unref();camera->unref();}} cameraScope{camera,wrapper};
    if(!original) {
      camera->position=SbVec3f(0,0,0);camera->nearDistance=.1f;camera->farDistance=100;
      static_cast<SoPerspectiveCamera *>(camera)->heightAngle=3.14159265358979323846f/2;
      wrapper=new SoSeparator;wrapper->ref();wrapper->addChild(camera);wrapper->addChild(scene);
    }
    if(original) {
      camera->setStereoMode(original->getStereoMode());
      camera->setStereoAdjustment(original->getStereoAdjustment());
      camera->setBalanceAdjustment(original->getBalanceAdjustment());
    }
    if(!validCubeCamera(camera))return fail("Invalid cube RTT camera parameters");
    CoinRenderRttProducer producer;producer.cube=true;producer.size=SbVec2i32(size[0],size[1]);
    producer.sourceRevision=texture->getNodeId();
    for(unsigned face=0;face<6;++face) {
      camera->orientation=cubeRttOrientation(face);
      CoinRenderAction child(SbViewportRegion(size[0],size[1]));
      child.pimpl->sceneTexturePlan=p->sceneTexturePlan;child.pimpl->planOnly=true;
      child.pimpl->sceneTextureCaptureMemo=p->sceneTextureCaptureMemo;
      child.pimpl->sceneTextureBumpProducer=bumpProducer;
      child.pimpl->executionOptions=p->executionOptions;
      child.pimpl->portableShaderModules=p->portableShaderModules;
      child.pimpl->inheritedTransparencyOverride=SoOverrideElement::getTransparencyTypeOverride(state);
      child.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(producerTransparency));
      child.setSortedLayersNumPasses(int(p->transparencyOptions.layers));child.setTransparencyBufferBudget(p->transparencyOptions.bufferBudget);
      const auto bg=texture->backgroundColor.getValue();child.setBackgroundColor(SbColor4f(bg[0],bg[1],bg[2],1));
      CoinRenderCameraSnapshot capturedCamera;
      CubeCameraOverride overrideCamera{original,camera,&capturedCamera,child.pimpl.operator->()};
      child.addPostCallback(SoCamera::getClassTypeId(),CubeCameraOverride::post,&overrideCamera);
      if(original)child.addPreCallback(SoCamera::getClassTypeId(),CubeCameraOverride::apply,&overrideCamera);
      child.apply(wrapper?static_cast<SoNode *>(wrapper):scene);
      if(child.getLastStatus()!=CoinRenderAction::SUCCESS)return fail(std::string("Cube RTT face capture: ")+child.getLastError().getString());
      captureStable = captureStable && !child.pimpl->builder.hasCoordinateFunctions();
      producer.face(face)=std::move(child.pimpl->lastValidPlan);producer.face(face).legacyBlendAlpha=true;
      if(producer.face(face).cameras.empty())producer.face(face).cameras.push_back(capturedCamera);
    }
    producer.capturedCubeFaces=6;
    if(!p->sceneTexturePlan->append(std::move(producer),producerId,diagnostic))return fail(diagnostic);
    if (p->sceneTextureCaptureMemo)
      p->sceneTextureCaptureMemo->remember(texture, context, baseline, trace.trace, captureEpoch, producerId,
                                          p->sceneTexturePlan->chargedBytes-chargeBefore, captureStable);
  }
  p->sceneTexturePixels.emplace_back(4,0);auto & marker=p->sceneTexturePixels.back();
  for(unsigned i=0;i<4;++i)marker[i]=uint8_t(producerId>>(i*8));
  SoMultiTextureImageElement::set(state,const_cast<SoSceneTextureCubeMap *>(texture),unit,SbVec2s(1,1),4,marker.data(),
    SoMultiTextureImageElement::CLAMP,SoMultiTextureImageElement::CLAMP,
    static_cast<SoMultiTextureImageElement::Model>(model),texture->blendColor.getValue());
  SbVec2s retained;int components;const auto * image=SoMultiTextureImageElement::getImage(state,unit,retained,components);
  if(!image||retained!=SbVec2s(1,1)||components!=4)return fail("Cube RTT marker was not retained");
  p->builder.registerSceneTexture(image,producerId,description.width,description.height,false,
    texture->transparencyFunction.getValue(),CoinRenderTextureFormat::RGBA8_LINEAR,uint32_t(size[0]));
  SoMultiTextureEnabledElement::enableCubeMap(state,const_cast<SoSceneTextureCubeMap *>(texture),unit);
  if(unit==0 && texture->isOverride())SoTextureOverrideElement::setImageOverride(state,TRUE);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::sceneTexturePreCB(void * userdata,
                                       SoCallbackAction * action,
                                       const SoNode * node)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  const SoSceneTexture2 * texture = static_cast<const SoSceneTexture2 *>(node);
  SoState * state = action ? action->getState() : NULL;
  if (!state) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::ACTION,
      SbString("SoSceneTexture2 has no traversal state")));
    return SoCallbackAction::ABORT;
  }
  if (SoTextureOverrideElement::getImageOverride(state)) {
    return SoCallbackAction::CONTINUE;
  }
  const int unit = SoTextureUnitElement::get(state);
  const int model = texture->model.getValue();
  const SoNode * transparency = texture->sceneTransparencyType.getValue();
  if (unit < 0 || unit >= static_cast<int>(COIN_RENDER_MAX_TEXTURE_UNITS) ||
      (texture->type.getValue() != SoSceneTexture2::RGBA8 && texture->type.getValue()!=SoSceneTexture2::RGBA16F) ||
      (model != SoSceneTexture2::MODULATE && model != SoSceneTexture2::REPLACE &&
       model != SoSceneTexture2::DECAL && model != SoSceneTexture2::BLEND) ||
      (texture->wrapS.getValue() != SoSceneTexture2::REPEAT &&
       texture->wrapS.getValue() != SoSceneTexture2::CLAMP) ||
      (texture->wrapT.getValue() != SoSceneTexture2::REPEAT &&
       texture->wrapT.getValue() != SoSceneTexture2::CLAMP) ||
      !coin_render_scene_texture_policy_supported(texture->transparencyFunction.getValue()) ||
      (transparency && !transparency->isOfType(SoTransparencyType::getClassTypeId()))) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
        SbString("SoSceneTexture2 supports units 0..7, RGBA8/RGBA16F, MODULATE/REPLACE/DECAL/BLEND, "
                 "REPEAT/CLAMP, NONE/ALPHA_BLEND/ALPHA_TEST and SoTransparencyType scene policy")));
    return SoCallbackAction::ABORT;
  }

  if (SoTextureQualityElement::get(state) <= 0.0f) {
    p->sceneTexturePixels.emplace_back(4, 0);
    SoMultiTextureImageElement::set(state, const_cast<SoSceneTexture2 *>(texture), unit,
      SbVec2s(1, 1), 4, p->sceneTexturePixels.back().data(),
      SoMultiTextureImageElement::REPEAT, SoMultiTextureImageElement::REPEAT,
      static_cast<SoMultiTextureImageElement::Model>(model), texture->blendColor.getValue());
    SbVec2s markerSize;
    int components;
    const auto * marker = SoMultiTextureImageElement::getImage(state, unit, markerSize, components);
    p->builder.registerSceneTexture(marker, 0, 1, 1, false,
                                    texture->transparencyFunction.getValue());
    SoMultiTextureEnabledElement::set(state, const_cast<SoSceneTexture2 *>(texture), unit, FALSE);
    return SoCallbackAction::CONTINUE;
  }

  const SbVec2s size = texture->size.getValue();
  // Disabled texturing returned above and does not consume a producer size.
  // Active nonpositive extents are malformed authorship; positive extents
  // still pass through the mode-specific capability/budget admission below.
  if (size[0] <= 0 || size[1] <= 0) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString("SoSceneTexture2 requires positive authored dimensions")));
    return SoCallbackAction::ABORT;
  }
  SoNode * scene = texture->scene.getValue();
  std::string diagnostic;
  const SbVec2i32 passSize(size[0], size[1]);
  const uint64_t sourceRevision = texture->getNodeId();
  const bool bumpProducer = p->target && !p->planOnly && !p->asyncTicket &&
    p->target->getPimpl()->supportsOpaqueBump(false);
  const int producerTransparency = transparency
    ? static_cast<const SoTransparencyType *>(transparency)->value.getValue()
    : SoShapeStyleElement::getTransparencyType(state);
  const auto context = rttCaptureContext(*p, state, producerTransparency, bumpProducer);
  uint64_t producerId = 0;
  std::vector<int> occurrencePath;
  if (p->technicalRttCapture) {
    const auto path = action->getCurPath()->fullPath();
    for (int i = 1; i < path.getLength(); ++i)
      occurrencePath.push_back(path.getIndexFromTail(path.getLength() - 1 - i));
  }
  if (p->technicalRttReplay)
    for (const auto & capture : p->technicalRttCaptures)
      if (capture.source == texture && capture.revision == texture->getNodeId() &&
          capture.context == context && capture.occurrencePath == occurrencePath) {
        producerId = capture.producer;
        break;
      }
  bool dependencyCycle = p->sceneTexturePlan &&
    CoinRenderRttCaptureMemo::activeSource(*p->sceneTexturePlan, sourceRevision);
  if (dependencyCycle) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::INVALID_SCENE,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString("SoSceneTexture2 dependency cycle")));
    return SoCallbackAction::ABORT;
  }
  if (!producerId && p->sceneTexturePlan && p->sceneTextureCaptureMemo &&
      !p->sceneTextureCaptureMemo->find(texture, context, *p->sceneTexturePlan, producerId, diagnostic, dependencyCycle)) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      dependencyCycle ? CoinRenderAction::INVALID_SCENE : CoinRenderAction::UNSUPPORTED,
      CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(diagnostic.c_str())));
    return SoCallbackAction::ABORT;
  }
  if (!producerId) {
    CoinRenderRttCaptureMemo::Baseline baseline;
    const uint64_t captureEpoch = p->sceneTextureCaptureMemo ? p->sceneTextureCaptureMemo->epoch : 0;
    bool hasEffects = true;
    if (p->sceneTextureCaptureMemo && p->sceneTextureCaptureMemo->allowed)
      hasEffects = rttSceneHasEffects(texture, &baseline);
    if (hasEffects) { baseline.complete=false; baseline.nodes.clear(); }
    const size_t chargeBefore = p->sceneTexturePlan ? p->sceneTexturePlan->chargedBytes : 0;
    if (!scene || !p->sceneTexturePlan ||
        !p->sceneTexturePlan->enter(sourceRevision, passSize, diagnostic)) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(
          CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
          SbString(diagnostic.empty() ? "SoSceneTexture2 requires a scene and dimensions in 1..2048"
                                      : diagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    struct ActiveTextureScope {
      CoinRenderRttPlan& graph;
      ~ActiveTextureScope() { graph.leave(); }
    } scope{*p->sceneTexturePlan};
    CoinRenderRttCaptureMemo::TraceScope trace(p->sceneTextureCaptureMemo.get(), sourceRevision,
                                              p->sceneTexturePlan->activeSources.size());
    const SbVec4f background = texture->backgroundColor.getValue();
    CoinRenderAction childAction(SbViewportRegion(size[0], size[1]));
    childAction.pimpl->sceneTexturePlan = p->sceneTexturePlan;
    childAction.pimpl->sceneTextureCaptureMemo = p->sceneTextureCaptureMemo;
    childAction.pimpl->planOnly = true;
    // The qualified backend executes the child into private RTT attachments.
    // The consumer may be either an offscreen target or a native window; its
    // capability guard still excludes asynchronous/direct-output targets.
    childAction.pimpl->sceneTextureBumpProducer = bumpProducer;
    childAction.pimpl->inheritedTransparencyOverride = SoOverrideElement::getTransparencyTypeOverride(state);
    childAction.pimpl->executionOptions = p->executionOptions;
    childAction.pimpl->portableShaderModules = p->portableShaderModules;
    // The producer inherits the traversal policy, including preceding Coin nodes.
    // Its explicit SoTransparencyType overrides this policy exactly as in CoinGL.
    childAction.setTransparencyType(static_cast<CoinRenderAction::TransparencyType>(producerTransparency));
    childAction.setSortedLayersNumPasses(static_cast<int>(p->transparencyOptions.layers));
    childAction.setTransparencyBufferBudget(p->transparencyOptions.bufferBudget);
    childAction.setBackgroundColor(
        SbColor4f(background[0], background[1], background[2], background[3]));
    childAction.apply(scene);
    if (childAction.getLastStatus() != CoinRenderAction::SUCCESS) {
      p->setDiagnostic(CoinRenderDiagnosticShell::withContext(
        childAction.getLastStatus(), childAction.pimpl->lastDiagnosticDomain,
        "SoSceneTexture2 subscene", childAction.getLastError()));
      return SoCallbackAction::ABORT;
    }
    CoinRenderRttProducer producer;
    producer.plan = std::move(childAction.pimpl->lastValidPlan);
    producer.plan.legacyBlendAlpha = true;
    producer.size = passSize;
    producer.sourceRevision = sourceRevision;
    producer.format=texture->type.getValue()==SoSceneTexture2::RGBA16F?CoinRenderTextureFormat::RGBA16_FLOAT:CoinRenderTextureFormat::RGBA8_LINEAR;
    if (!p->sceneTexturePlan->append(std::move(producer), producerId, diagnostic)) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
                                                         CoinRenderDiagnosticDomain::FRAME_PLAN,
                                                         SbString(diagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    if (p->sceneTextureCaptureMemo)
      p->sceneTextureCaptureMemo->remember(texture, context, baseline, trace.trace, captureEpoch, producerId,
                                          p->sceneTexturePlan->chargedBytes-chargeBefore,
                                          !childAction.pimpl->builder.hasCoordinateFunctions());
  }
  if (p->technicalRttCapture && !p->technicalRttReplay)
    p->technicalRttCaptures.push_back({texture, texture->getNodeId(), context, occurrencePath, producerId});
  p->sceneTexturePixels.emplace_back(4, 0);
  auto& marker = p->sceneTexturePixels.back();
  for (unsigned int i = 0; i < 4; ++i)
    marker[i] = static_cast<uint8_t>(producerId >> (i * 8));
  SoMultiTextureImageElement::set(
      state, const_cast<SoSceneTexture2*>(texture), unit, SbVec2s(1, 1), 4, marker.data(),
      static_cast<SoMultiTextureImageElement::Wrap>(texture->wrapS.getValue()),
      static_cast<SoMultiTextureImageElement::Wrap>(texture->wrapT.getValue()),
      static_cast<SoMultiTextureImageElement::Model>(model), texture->blendColor.getValue());
  SbVec2s markerSize;
  int markerComponents = 0;
  SoMultiTextureImageElement::Wrap ws, wt;
  SoMultiTextureImageElement::Model retainedModel;
  SbColor blend;
  const unsigned char* image =
      SoMultiTextureImageElement::get(state, unit, markerSize, markerComponents, ws, wt, retainedModel, blend);
  if (!image || markerSize != SbVec2s(1, 1) || markerComponents != 4) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::BACKEND_ERROR, CoinRenderDiagnosticDomain::ACTION,
        SbString("SoSceneTexture2 logical marker was not retained by traversal state")));
    return SoCallbackAction::ABORT;
  }
  p->builder.registerSceneTexture(image, producerId, uint32_t(size[0]), uint32_t(size[1]),
                                  false, texture->transparencyFunction.getValue(),
                                  texture->type.getValue()==SoSceneTexture2::RGBA16F?CoinRenderTextureFormat::RGBA16_FLOAT:CoinRenderTextureFormat::RGBA8_LINEAR);
  SoMultiTextureEnabledElement::set(state, const_cast<SoSceneTexture2 *>(texture), unit, TRUE);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::depthBufferPreCB(void *, SoCallbackAction * action, const SoNode * node)
{
  const SoDepthBuffer * depth = static_cast<const SoDepthBuffer *>(node);
  SoState * state = action->getState();
  SbBool test = depth->test.isIgnored()
    ? SoDepthBufferElement::getTestEnable(state) : depth->test.getValue();
  SbBool write = depth->write.isIgnored()
    ? SoDepthBufferElement::getWriteEnable(state) : depth->write.getValue();
  SoDepthBufferElement::DepthWriteFunction function = depth->function.isIgnored()
    ? SoDepthBufferElement::getFunction(state)
    : static_cast<SoDepthBufferElement::DepthWriteFunction>(depth->function.getValue());
  const SbVec2f range = depth->range.isIgnored()
    ? SoDepthBufferElement::getRange(state) : depth->range.getValue();
  SoDepthBufferElement::set(state, test, write, function, range);
  CoinRenderDepthPolicyElement::add(state, (depth->test.isIgnored() ? 0 : 1) |
    (depth->write.isIgnored() ? 0 : 2) | (depth->function.isIgnored() ? 0 : 4) |
    (depth->range.isIgnored() ? 0 : 8));
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::annotationPreCB(void * userdata, SoCallbackAction * action, const SoNode *)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->capturingShadowScene) return SoCallbackAction::PRUNE;
  action->getState()->push();
  // Coin/GL annotations render last with depth testing disabled. They do not
  // clear or write the scene depth; an explicit child DepthBuffer can override.
  SoDepthBufferElement::set(action->getState(), FALSE, FALSE,
    SoDepthBufferElement::getFunction(action->getState()),
    SoDepthBufferElement::getRange(action->getState()));
  p->builder.beginAnnotation(false);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::annotationPostCB(void * userdata, SoCallbackAction * action, const SoNode *)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (!p->capturingShadowScene) {
    p->builder.endAnnotation();
    action->getState()->pop();
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::managerLayerPreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->capturingShadowScene || p->replayingAnnotations) return SoCallbackAction::CONTINUE;
  const auto found = std::find_if(p->managerLayerBoundaries.begin(),
    p->managerLayerBoundaries.end(),
    [node](const std::tuple<const SoNode *, bool, int> & layer) {
      return std::get<0>(layer) == node;
    });
  if (found != p->managerLayerBoundaries.end()) {
    const int type = std::get<2>(*found);
    // This purpose belongs to a generated composition boundary, never to an
    // inferred combination of authored material, style or texture state.
    if (type == COIN_RENDER_MANAGER_HIGHLIGHT_LAYER) ++p->managerHighlightDepth;
    action->getState()->push();
    const int effectiveType = type >= 0 ? type : static_cast<int>(p->transparencyType);
    SoShapeStyleElement::setTransparencyType(action->getState(), effectiveType);
    SoLazyElement::setTransparencyType(action->getState(), effectiveType);
    SoOverrideElement::setTransparencyTypeOverride(action->getState(),
      const_cast<SoNode *>(node), effectiveType == SoGLRenderAction::SORTED_LAYERS_BLEND);
    p->builder.beginManagerLayer(std::get<1>(*found), type == COIN_RENDER_MANAGER_HIGHLIGHT_LAYER);
    p->managerCapturedLayers.push_back(p->builder.activeAnnotationLayer());
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::managerLayerPostCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->capturingShadowScene || p->replayingAnnotations) return SoCallbackAction::CONTINUE;
  const auto found = std::find_if(p->managerLayerBoundaries.begin(),
    p->managerLayerBoundaries.end(),
    [node](const std::tuple<const SoNode *, bool, int> & layer) {
      return std::get<0>(layer) == node;
    });
  if (found != p->managerLayerBoundaries.end()) {
    p->builder.capturePendingDepthClear(action);
    p->builder.endManagerLayer(std::get<2>(*found) == COIN_RENDER_MANAGER_HIGHLIGHT_LAYER);
    if (std::get<2>(*found) == COIN_RENDER_MANAGER_HIGHLIGHT_LAYER &&
        p->managerHighlightDepth) --p->managerHighlightDepth;
    action->getState()->pop();
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::shadowGroupPreCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  const auto * group = static_cast<const SoShadowGroup *>(node);
  // Selection replay still traverses the real path and its transforms, but
  // only the authored base contributes shadow groups, lights and map scenes.
  if (p->managerHighlightDepth || p->capturingShadowScene || !group->isActive.getValue())
    return SoCallbackAction::CONTINUE;
  CoinRenderGuardedTraversal<SoSearchAction> mapLightSearch;
  mapLightSearch.setType(SoLight::getClassTypeId());
  mapLightSearch.setInterest(SoSearchAction::ALL);
  mapLightSearch.apply(const_cast<SoShadowGroup *>(group));
  bool hasMapLight = false;
  for (int i = 0; i < mapLightSearch.getPaths().getLength(); ++i) {
    const SoNode * light = mapLightSearch.getPaths()[i]->getTail();
    if (!static_cast<const SoLight *>(light)->on.getValue()) continue;
    if (light->isOfType(SoSpotLight::getClassTypeId()) ||
        light->isOfType(SoShadowDirectionalLight::getClassTypeId())) {
      hasMapLight = true;
      break;
    }
  }
  if (!hasMapLight) return SoCallbackAction::CONTINUE;
  CoinRenderShadowGroupSnapshot snapshot;
  snapshot.sourceRevision = node->getNodeId();
  snapshot.intensity = group->intensity.getValue();
  snapshot.precision = group->precision.getValue();
  snapshot.quality = group->quality.getValue();
  snapshot.epsilon = group->epsilon.getValue();
  snapshot.threshold = group->threshold.getValue();
  snapshot.smoothBorder = group->smoothBorder.getValue();
  snapshot.shadowCachingEnabled = group->shadowCachingEnabled.getValue() != FALSE;
  snapshot.visibilityNearRadius = group->visibilityNearRadius.getValue();
  snapshot.visibilityRadius = group->visibilityRadius.getValue();
  snapshot.visibilityFlag = group->visibilityFlag.getValue();
  snapshot.parentGroupSlot = p->builder.activeShadowGroupSlot();
  snapshot.hasEntryCamera = true;
  snapshot.entryCamera = CoinRenderFramePlanBuilder::captureCamera(action);
  snapshot.entryModel = action->getModelMatrix();
  p->shadowStyleBeforeGroups.push_back(SoShadowStyleElement::get(action->getState()));
  SoShadowStyleElement::set(action->getState(), 3);
  p->builder.beginShadowGroup(snapshot);
  p->activeShadowGroupNodes.push_back(group);
  // Coin/GL renders light-owned scenes from the group-entry state, resetting
  // lazy material, enabled textures and model matrix in SoSceneTexture2.
  // Reuse ordinary captured casters for simple groups. Composition requires
  // its own map traversal because nested group resets and annotations affect
  // the main pass differently from SHADOWMAP.
  // Coin/GL renders the whole caster scene even when apply(path) selects
  // only a receiver. Reuse the independent map capture for this traversal.
  int pathIndexCount=0;
  const int * pathIndices=nullptr;
  bool separateMap = snapshot.parentGroupSlot != 0 ||
    p->master->callbackRegistrationRevision() != p->captureCallbackRevision ||
    (SoShapeStyleElement::get(action->getState())->getFlags() & SoShapeStyleElement::BBOXCMPLX) ||
    action->getPathCode(pathIndexCount,pathIndices) == SoAction::IN_PATH;
  CoinRenderGuardedTraversal<SoSearchAction> structure;
  structure.setType(SoComplexity::getClassTypeId());
  structure.setInterest(SoSearchAction::FIRST);
  structure.apply(const_cast<SoShadowGroup *>(group));
  separateMap = separateMap || structure.getPath() != nullptr;
  structure.reset();
  structure.setType(SoAnnotation::getClassTypeId());
  structure.setInterest(SoSearchAction::FIRST);
  structure.apply(const_cast<SoShadowGroup *>(group));
  separateMap = separateMap || structure.getPath() != nullptr;
  structure.reset();
  structure.setType(SoShadowGroup::getClassTypeId());
  structure.setInterest(SoSearchAction::ALL);
  structure.apply(const_cast<SoShadowGroup *>(group));
  for (int i = 0; i < structure.getPaths().getLength(); ++i) {
    const auto * child = static_cast<const SoShadowGroup *>(structure.getPaths()[i]->getTail());
    separateMap = separateMap || (child != group && child->isActive.getValue());
  }
  CoinRenderGuardedTraversal<SoSearchAction> search;
  search.setType(SoLight::getClassTypeId());
  search.setInterest(SoSearchAction::ALL);
  search.apply(const_cast<SoShadowGroup *>(group));
  for (int i = 0; i < search.getPaths().getLength(); ++i) {
    const SoNode * light = search.getPaths()[i]->getTail();
    SoNode * scene = nullptr;
    if (light->isOfType(SoShadowSpotLight::getClassTypeId()))
      scene = static_cast<const SoShadowSpotLight *>(light)->shadowMapScene.getValue();
    else if (light->isOfType(SoShadowDirectionalLight::getClassTypeId()))
      scene = static_cast<const SoShadowDirectionalLight *>(light)->shadowMapScene.getValue();
    if (!static_cast<const SoLight *>(light)->on.getValue()) continue;
    if (!scene && separateMap && (light->isOfType(SoSpotLight::getClassTypeId()) ||
                   light->isOfType(SoShadowDirectionalLight::getClassTypeId())))
      scene = const_cast<SoShadowGroup *>(group);
    if (!scene) continue;
    if (std::any_of(p->shadowSceneCaptures.begin(), p->shadowSceneCaptures.end(),
        [&](const ShadowSceneCapture & capture) {
          return capture.groupSlot == p->builder.activeShadowGroupSlot() &&
            capture.lightRevision == light->getNodeId();
        })) continue;
    uint32_t mapSize = 0;
    std::string mapDiagnostic;
    if (!coin_render_shadow_map_size(snapshot.precision, mapSize, mapDiagnostic)) {
      p->setDiagnostic(CoinRenderDiagnosticShell::action(CoinRenderAction::UNSUPPORTED,
                                                         CoinRenderDiagnosticDomain::FRAME_PLAN,
                                                         SbString(mapDiagnostic.c_str())));
      return SoCallbackAction::ABORT;
    }
    const SbViewportRegion mapViewport(static_cast<short>(mapSize), static_cast<short>(mapSize));
    CoinRenderFramePlanBuilder savedBuilder;
    std::swap(savedBuilder, p->builder);
    p->builder.beginFrame(p->backgroundColor, mapViewport);
    p->builder.configureTextures(p->executionOptions);
    auto captureGroup = snapshot;
    captureGroup.parentGroupSlot = 0;
    p->builder.beginShadowGroup(captureGroup);
    SoState * state = action->getState();
    const uint32_t inheritedPlanes = SoClipPlaneElement::getInstance(state)->getNum();
    state->push();
    // SoSceneTexture2 renders each light map with a square viewport. Camera
    // viewportMapping must see that aspect, not the observer's viewport.
    const SbViewportRegion observerViewport = action->getViewportRegion();
    action->setViewportRegion(mapViewport);
    SoViewportRegionElement::set(state, mapViewport);
    SoLazyElement::setToDefault(state);
    SoMultiTextureEnabledElement::disableAll(state);
    SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
    SoTextureQualityElement::set(state, 0.0f);
    SoMaterialBindingElement::set(state, nullptr, SoMaterialBindingElement::OVERALL);
    SoOverrideElement::setMaterialBindingOverride(state, nullptr, TRUE);
    SoOverrideElement::setLightModelOverride(state, nullptr, TRUE);
    SoTextureOverrideElement::setQualityOverride(state, TRUE);
    SoModelMatrixElement::set(state, const_cast<SoShadowGroup *>(group), SbMatrix::identity());
    p->capturingShadowScene = true;
    // GL flattens a ShadowGroup used as the custom scene.
    if (scene->isOfType(SoShadowGroup::getClassTypeId())) {
      const auto * sub = static_cast<const SoShadowGroup *>(scene);
      for (int child = 0; child < sub->getNumChildren(); ++child)
        action->switchToNodeTraversal(sub->getChild(child));
    } else action->switchToNodeTraversal(scene);
    p->capturingShadowScene = false;
    state->pop();
    action->setViewportRegion(observerViewport);
    action->setCurrentNode(const_cast<SoNode *>(node));
    ShadowSceneCapture capture;
    capture.groupSlot = savedBuilder.activeShadowGroupSlot();
    capture.lightRevision = light->getNodeId();
    capture.inheritedClipPlaneCount = inheritedPlanes;
    std::string error;
    p->builder.endShadowGroup();
    const bool ok = p->builder.build(capture.frame, &error, true);
    std::swap(savedBuilder, p->builder);
    if (!ok || action->hasTerminated()) {
      if (!action->hasTerminated()) p->setDiagnostic(CoinRenderDiagnosticShell::action(
        CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN, SbString(error.c_str())));
      return SoCallbackAction::ABORT;
    }
    for (auto &mapState : capture.frame.renderStates)
      mapState.shadowMapSize = mapSize;
    p->shadowSceneCaptures.push_back(std::move(capture));
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::shadowGroupPostCB(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  auto * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->managerHighlightDepth || p->capturingShadowScene ||
      !static_cast<const SoShadowGroup *>(node)->isActive.getValue())
    return SoCallbackAction::CONTINUE;
  if (p->activeShadowGroupNodes.empty() || p->activeShadowGroupNodes.back() != node)
    return SoCallbackAction::CONTINUE;
  p->builder.endShadowGroup();
  if (!p->activeShadowGroupNodes.empty()) p->activeShadowGroupNodes.pop_back();
  if (!p->shadowStyleBeforeGroups.empty()) {
    SoShadowStyleElement::set(action->getState(), p->shadowStyleBeforeGroups.back());
    p->shadowStyleBeforeGroups.pop_back();
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::shadowStylePreCB(void *, SoCallbackAction * action, const SoNode * node)
{
  const auto * style = static_cast<const SoShadowStyle *>(node);
  SoShadowStyleElement::set(action->getState(), const_cast<SoShadowStyle *>(style),
                            style->style.getValue());
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::lightPreCB(void * userdata,
                               SoCallbackAction * action,
                               const SoNode * node)
{
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (p->managerHighlightDepth || p->capturingShadowScene) return SoCallbackAction::CONTINUE;
  p->builder.recordLightAttenuation(action);
  if (!p->builder.hasActiveShadowGroup()) return SoCallbackAction::CONTINUE;
  const auto * light = static_cast<const SoLight *>(node);
  CoinRenderShadowLightSnapshot snapshot;
  snapshot.groupSlot = p->builder.activeShadowGroupSlot();
  snapshot.sourceRevision = node->getNodeId();
  snapshot.enabled = light->on.getValue() != FALSE;
  snapshot.color = light->color.getValue();
  snapshot.intensity = light->intensity.getValue();
  snapshot.attenuation = SoEnvironmentElement::getLightAttenuation(action->getState());
  snapshot.model = action->getModelMatrix();
  snapshot.modelViewAtLight = action->getModelMatrix() * action->getViewingMatrix();
  if (node->isOfType(SoSpotLight::getClassTypeId())) {
    const auto * spot = static_cast<const SoSpotLight *>(node);
    snapshot.type = CoinRenderLightType::SPOT;
    snapshot.shadowEligible = true;
    snapshot.position = spot->location.getValue();
    snapshot.direction = spot->direction.getValue();
    snapshot.cutOffAngle = spot->cutOffAngle.getValue();
    snapshot.dropOffRate = spot->dropOffRate.getValue();
    if (node->isOfType(SoShadowSpotLight::getClassTypeId())) {
      const auto * shadow = static_cast<const SoShadowSpotLight *>(node);
      snapshot.hasCustomScene = shadow->shadowMapScene.getValue() != nullptr;
      snapshot.nearDistance = shadow->nearDistance.getValue();
      snapshot.farDistance = shadow->farDistance.getValue();
    }
  } else if (node->isOfType(SoDirectionalLight::getClassTypeId())) {
    const auto * directional = static_cast<const SoDirectionalLight *>(node);
    snapshot.type = CoinRenderLightType::DIRECTIONAL;
    snapshot.direction = directional->direction.getValue();
    if (node->isOfType(SoShadowDirectionalLight::getClassTypeId())) {
      const auto * shadow = static_cast<const SoShadowDirectionalLight *>(node);
      snapshot.shadowEligible = true;
      snapshot.hasCustomScene = shadow->shadowMapScene.getValue() != nullptr;
      snapshot.maxShadowDistance = shadow->maxShadowDistance.getValue();
      snapshot.bboxCenter = shadow->bboxCenter.getValue();
      snapshot.bboxSize = shadow->bboxSize.getValue();
    }
  } else if (node->isOfType(SoPointLight::getClassTypeId())) {
    snapshot.type = CoinRenderLightType::POINT;
    snapshot.position = static_cast<const SoPointLight *>(node)->location.getValue();
  }
  if (snapshot.hasCustomScene && !p->activeShadowGroupNodes.empty()) {
    const SoNode * scene = nullptr;
    if (node->isOfType(SoShadowSpotLight::getClassTypeId()))
      scene = static_cast<const SoShadowSpotLight *>(node)->shadowMapScene.getValue();
    else if (node->isOfType(SoShadowDirectionalLight::getClassTypeId()))
      scene = static_cast<const SoShadowDirectionalLight *>(node)->shadowMapScene.getValue();
    if (scene) snapshot.customSceneNodeId = scene->getNodeId();
  }
  p->builder.recordShadowLight(snapshot);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
CoinRenderActionP::indexedFaceSetPreCB(void * userdata,
                                        SoCallbackAction * action,
                                        const SoNode * node)
{
  // PRUNE skips this shape only; subsequent state nodes still traverse.
  if (CoinRenderFramePlanBuilder::isShapeInvisible(action)) return SoCallbackAction::PRUNE;
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (!p->fastPathEnabled || p->master->callbackRegistrationRevision() != p->captureCallbackRevision ||
      (!p->capturingShadowScene && (SoShapeStyleElement::get(action->getState())->getFlags() &
                                   SoShapeStyleElement::BBOXCMPLX))) {
    return SoCallbackAction::CONTINUE;
  }

  // Preserve the original polygon details used by the common style resolver.
  if (CoinRenderFramePlanBuilder::polygonDrawStyle(action) != SoDrawStyle::FILLED)
    return SoCallbackAction::CONTINUE;

  // The direct path bypasses the node virtual callback implementation.
  // Subclasses may prepare traversal state there, so only prune the exact type.
  if (node->getTypeId() != SoIndexedFaceSet::getClassTypeId()) {
    return SoCallbackAction::CONTINUE;
  }

  const SoIndexedFaceSet * ifs = dynamic_cast<const SoIndexedFaceSet *>(node);
  if (!ifs) {
    return SoCallbackAction::CONTINUE;
  }

  SoState * state = action->getState();
  if (!state) {
    return SoCallbackAction::CONTINUE;
  }

  const SoVertexProperty * vp = static_cast<const SoVertexProperty *>(ifs->vertexProperty.getValue());
  if (vp) {
    state->push();
    const_cast<SoVertexProperty *>(vp)->doAction(action);
  }

  int lastTextureUnit = -1;
  SoMultiTextureEnabledElement::getEnabledUnits(state, lastTextureUnit);
  if (lastTextureUnit > 0) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  const SoCoordinateElement * coords = SoCoordinateElement::getInstance(state);
  if (!coords || !coords->is3D()) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  const SbVec3f * coordArray = coords->getArrayPtr3();
  int32_t numCoords = coords->getNum();
  if (!coordArray || numCoords <= 0) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  CoinRenderDirectGeometryView view;
  view.positions = CoinRenderSpan<SbVec3f>(coordArray, static_cast<size_t>(numCoords));

  const SoNormalElement * normElem = SoNormalElement::getInstance(state);
  if (normElem && normElem->getNum() > 0) {
    view.normals = CoinRenderSpan<SbVec3f>(normElem->getArrayPtr(), static_cast<size_t>(normElem->getNum()));
  }

  if (ifs->coordIndex.getNum() > 0) {
    view.coordIndex = CoinRenderSpan<int32_t>(ifs->coordIndex.getValues(0), static_cast<size_t>(ifs->coordIndex.getNum()));
  }
  if (ifs->normalIndex.getNum() > 0 && !(ifs->normalIndex.getNum() == 1 && ifs->normalIndex[0] == -1)) {
    view.normalIndex = CoinRenderSpan<int32_t>(ifs->normalIndex.getValues(0), static_cast<size_t>(ifs->normalIndex.getNum()));
  }
  if (ifs->materialIndex.getNum() > 0 && !(ifs->materialIndex.getNum() == 1 && ifs->materialIndex[0] == -1)) {
    view.materialIndex = CoinRenderSpan<int32_t>(ifs->materialIndex.getValues(0), static_cast<size_t>(ifs->materialIndex.getNum()));
  }
  if (ifs->textureCoordIndex.getNum() > 0 && !(ifs->textureCoordIndex.getNum() == 1 && ifs->textureCoordIndex[0] == -1)) {
    view.texCoordIndex = CoinRenderSpan<int32_t>(ifs->textureCoordIndex.getValues(0), static_cast<size_t>(ifs->textureCoordIndex.getNum()));
  }

  const SoMultiTextureCoordinateElement * tcElem = SoMultiTextureCoordinateElement::getInstance(state);
  if (tcElem) {
    SoMultiTextureCoordinateElement::CoordType ct = tcElem->getType(0);
    if (ct == SoMultiTextureCoordinateElement::EXPLICIT) {
      // The direct geometry view carries 2D coordinates. Native callbacks
      // preserve R and Q for explicit 3D/4D coordinates.
      if (tcElem->getDimension(0) != 2) {
        if (vp) state->pop();
        return SoCallbackAction::CONTINUE;
      }
      int32_t numTc = tcElem->getNum(0);
      if (numTc > 0) {
        const SbVec2f * tcPtr = tcElem->getArrayPtr2(0);
        if (tcPtr) {
          view.texcoords = CoinRenderSpan<SbVec2f>(tcPtr, static_cast<size_t>(numTc));
        }
      }
    }
  }

  view.materialBinding = SoMaterialBindingElement::get(state);
  view.normalBinding = SoNormalBindingElement::get(state);
  normalizeIndexedBindings(node, view);

  std::string err;
  CoinRenderFastPathResult res = p->builder.processIndexedFaceSet(action, view, const_cast<SoNode *>(node), &err);

  if (vp) {
    state->pop();
  }

  if (res == CoinRenderFastPathResult::SUCCESS_PRUNE) {
    return SoCallbackAction::PRUNE;
  } else if (res == CoinRenderFastPathResult::INVALID_SCENE) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Invalid scene in IndexedFaceSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else if (res == CoinRenderFastPathResult::UNSUPPORTED) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Unsupported feature in IndexedFaceSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else {
    return SoCallbackAction::CONTINUE;
  }
}

SoCallbackAction::Response
CoinRenderActionP::indexedLineSetPreCB(void * userdata,
                                        SoCallbackAction * action,
                                        const SoNode * node)
{
  // PRUNE skips this shape only; subsequent state nodes still traverse.
  if (CoinRenderFramePlanBuilder::isShapeInvisible(action)) return SoCallbackAction::PRUNE;
  CoinRenderActionP * p = static_cast<CoinRenderActionP *>(userdata);
  if (!p->fastPathEnabled || p->master->callbackRegistrationRevision() != p->captureCallbackRevision ||
      (!p->capturingShadowScene && (SoShapeStyleElement::get(action->getState())->getFlags() &
                                   SoShapeStyleElement::BBOXCMPLX))) {
    return SoCallbackAction::CONTINUE;
  }

  // Preserve callback semantics for subclasses; see indexedFaceSetPreCB().
  if (node->getTypeId() != SoIndexedLineSet::getClassTypeId()) {
    return SoCallbackAction::CONTINUE;
  }

  const SoIndexedLineSet * ils = dynamic_cast<const SoIndexedLineSet *>(node);
  if (!ils) {
    return SoCallbackAction::CONTINUE;
  }

  SoState * state = action->getState();
  if (!state) {
    return SoCallbackAction::CONTINUE;
  }

  const SoVertexProperty * vp = static_cast<const SoVertexProperty *>(ils->vertexProperty.getValue());
  if (vp) {
    state->push();
    const_cast<SoVertexProperty *>(vp)->doAction(action);
  }

  const SoCoordinateElement * coords = SoCoordinateElement::getInstance(state);
  if (!coords || !coords->is3D()) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  const SbVec3f * coordArray = coords->getArrayPtr3();
  int32_t numCoords = coords->getNum();
  if (!coordArray || numCoords <= 0) {
    if (vp) state->pop();
    return SoCallbackAction::CONTINUE;
  }

  CoinRenderDirectGeometryView view;
  view.positions = CoinRenderSpan<SbVec3f>(coordArray, static_cast<size_t>(numCoords));

  if (ils->coordIndex.getNum() > 0) {
    view.coordIndex = CoinRenderSpan<int32_t>(ils->coordIndex.getValues(0), static_cast<size_t>(ils->coordIndex.getNum()));
  }
  const auto* normals=SoNormalElement::getInstance(state);
  if(normals && normals->getNum()>0)
    view.normals=CoinRenderSpan<SbVec3f>(normals->getArrayPtr(),static_cast<size_t>(normals->getNum()));
  if(ils->normalIndex.getNum()>0 && !(ils->normalIndex.getNum()==1 && ils->normalIndex[0]==-1))
    view.normalIndex=CoinRenderSpan<int32_t>(ils->normalIndex.getValues(0),static_cast<size_t>(ils->normalIndex.getNum()));
  view.normalBinding=SoNormalBindingElement::get(state);
  if (ils->materialIndex.getNum() > 0 && !(ils->materialIndex.getNum() == 1 && ils->materialIndex[0] == -1)) {
    view.materialIndex = CoinRenderSpan<int32_t>(ils->materialIndex.getValues(0), static_cast<size_t>(ils->materialIndex.getNum()));
  }

  view.materialBinding = SoMaterialBindingElement::get(state);
  normalizeIndexedBindings(node, view);

  std::string err;
  CoinRenderFastPathResult res = p->builder.processIndexedLineSet(action, view, const_cast<SoNode *>(node), &err);

  if (vp) {
    state->pop();
  }

  if (res == CoinRenderFastPathResult::SUCCESS_PRUNE) {
    return SoCallbackAction::PRUNE;
  } else if (res == CoinRenderFastPathResult::INVALID_SCENE) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::INVALID_SCENE, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Invalid scene in IndexedLineSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else if (res == CoinRenderFastPathResult::UNSUPPORTED) {
    p->setDiagnostic(CoinRenderDiagnosticShell::action(
      CoinRenderAction::UNSUPPORTED, CoinRenderDiagnosticDomain::FRAME_PLAN,
      SbString(err.empty() ? "Unsupported feature in IndexedLineSet" : err.c_str())));
    return SoCallbackAction::ABORT;
  } else {
    return SoCallbackAction::CONTINUE;
  }
}
