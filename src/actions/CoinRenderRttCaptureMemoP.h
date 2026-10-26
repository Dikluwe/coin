#ifndef COIN_RENDER_RTT_CAPTURE_MEMO_P_H
#define COIN_RENDER_RTT_CAPTURE_MEMO_P_H

#include "actions/CoinRenderActionIncludesP.h"

inline bool rttNodeHasKnownTraversal(const SoNode * node)
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

#endif
