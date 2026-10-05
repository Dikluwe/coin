#ifndef COIN_WGPU_FFI_FRAME_H
#define COIN_WGPU_FFI_FRAME_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include "rendering/coinwgpu/CoinWgpuShadowFrame.h"

#include <cstdint>
#include <string>
#include <vector>

// Private Infra owner for the storage referenced by CoinWgpuFrameView.
class CoinRenderFramePreflight;

class CoinWgpuFfiFrame {
public:
  CoinWgpuFfiFrame();
  bool prepare(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height,
               std::string & outDiagnostic, bool allowInstancing = true);
  bool prepare(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height,
               const CoinRenderFrameReuseDecision & reuse,
               std::string & outDiagnostic,
               const CoinRenderFramePreflight * preflight = nullptr,
               bool allowInstancing = true);
  const CoinWgpuFrameView & getView() const;
  const CoinWgpuShadowFrame & getShadowFrame() const;
  bool reusedLastPrepare() const;
  CoinRenderFrameReuseKind lastPrepareKind() const;
  // Private Infra diagnostics; these do not change the C/Rust frame ABI.
  bool incrementalOpaqueLastPrepare() const;
  size_t opaqueRangesRebakedLastPrepare() const;
  size_t opaqueVerticesRebakedLastPrepare() const;
  size_t opaqueHashedRangesLastPrepare() const;

private:
  struct BakeMatrices { float modelView[16], normal[16]; };
  struct OpaqueRange {
    size_t drawIndex;
    CoinRenderGeometryRange geometry;
    uint32_t stateSlot, drawOrdinal;
  };
  CoinWgpuFfiFrame(const CoinWgpuFfiFrame &);
  CoinWgpuFfiFrame & operator=(const CoinWgpuFfiFrame &);
  bool packStates(const CoinRenderFramePlan & frame, CoinWgpuShadowFrame & shadow,
                  uint32_t targetWidth, uint32_t targetHeight,
                  std::string & outDiagnostic,
                  const CoinRenderFramePreflight * preflight);
  bool packState(const CoinRenderFramePlan & frame,
                 const CoinRenderRenderStateSnapshot & source,
                 uint32_t targetWidth, uint32_t targetHeight,
                 CoinWgpuRenderState & destination, std::string & outDiagnostic);
  bool tryEarlyOpaqueBatch(const CoinRenderFramePlan & frame,
                          uint32_t width, uint32_t height,
                          const CoinRenderFramePreflight * preflight);
  bool tryOpaqueInstancing(const CoinRenderFramePlan & frame,
                          uint32_t width, uint32_t height,
                          const CoinRenderFramePreflight * preflight);
  void bindView(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height);
  void batchOpaqueTriangles(const CoinRenderFramePlan & frame);
  void rememberOpaqueCamera(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height);
  bool patchOpaqueCamera(const CoinRenderFramePlan & frame, uint32_t width, uint32_t height,
                         const CoinRenderFrameReuseDecision & reuse);

  uint64_t packedRevision;
  bool reused;
  bool opaqueBatched;
  bool opaqueInstanced = false;
  bool opaqueDiagonalLowered = false;
  size_t opaqueHashedRanges = 0;
  double opaqueInstancePositionBound = 0;
  bool opaqueCameraPatchable;
  bool opaqueGeometryPatchable;
  SbMatrix opaqueCameraAnchor;
  CoinWgpuRenderState opaqueCameraState;
  size_t opaqueSourceVertices, opaqueSourceIndices, opaqueSourceDraws, opaqueSourceStates;
  uint32_t opaqueTargetWidth, opaqueTargetHeight;
  float opaqueClearColor[4];
  CoinRenderFrameReuseKind prepareKind;
  CoinWgpuFrameView view;
  CoinWgpuShadowFrame shadowFrame;
  std::vector<CoinWgpuShadowPassView> extraShadowPassViews;
  std::vector<CoinWgpuVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<CoinWgpuDraw> draws;
  std::vector<CoinWgpuMaterial> materials;
  std::vector<CoinWgpuRenderState> states;
  std::vector<CoinWgpuInstance> instances;
  std::vector<CoinWgpuInstanceRange> instanceRanges;
  std::vector<CoinWgpuTexture> textures;
  std::vector<CoinWgpuSampler> samplers;
  std::vector<std::vector<uint8_t> > texturePixels;
  // A bounded exact-content cache over captured values, never Coin node IDs.
  // The large expanded output remains in vertices/indices across full packs.
  bool opaqueIncrementalValid = false;
  bool opaqueIncrementalCandidate = false;
  bool opaqueIncrementalUsed = false;
  size_t opaqueRebakedRanges = 0, opaqueRebakedVertices = 0;
  uint32_t opaqueIncrementalWidth = 0, opaqueIncrementalHeight = 0;
  SbMatrix opaqueIncrementalView, opaqueIncrementalProjection;
  CoinWgpuRenderState opaqueIncrementalState{};
  std::vector<BakeMatrices> bakeMatrices, opaquePreviousMatrices;
  std::vector<OpaqueRange> opaqueRanges;
  std::vector<uint32_t> opaqueStateMaterialSlots;
  std::vector<CoinRenderVertexSnapshot> opaqueInputVertices;
  std::vector<uint32_t> opaqueInputIndices;
  std::vector<CoinRenderMaterialSnapshot> opaqueInputMaterials;
};

#endif // !COIN_WGPU_FFI_FRAME_H
