#include <Inventor/CoinRenderExport.h>
#ifndef SOWGPURENDERTARGETP_H
#define SOWGPURENDERTARGETP_H

#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderFrameReuseCore.h"
#include <Inventor/SbVec2i32.h>
#include "rendering/coinrender/CoinRenderBackend.h"
#include <vector>
#include <cstdint>
#include <string>
#include <memory>

class CoinRenderFramePreflight;
struct CoinRenderCompositionTransferTrace;

class COIN_RENDER_DLL_API CoinRenderTargetP {
public:
  enum TargetKind {
    KIND_OFFSCREEN = 0,
    KIND_WINDOW = 1
  };

  CoinRenderTargetP(const SbVec2i32 & sz = SbVec2i32(0, 0));
  ~CoinRenderTargetP();

  static CoinRenderBackendRuntime & backendRuntime();
  static bool compiledBackendInitializesCpuDepthBuffer();
  // Non-owning, process-lifetime Infra service, independent of executor ownership.
  CoinRenderBackendRuntime * runtime;
  bool capabilityProbeOnly = false;
  CoinRenderOptions options;
  std::string optionsDiagnostic;
  TargetKind kind{KIND_OFFSCREEN};
  CoinRenderTarget::Status status{CoinRenderTarget::TARGET_READY};
  SbVec2i32 size{0, 0};
  std::vector<uint8_t> colorBuffer; // RGBA8 (offscreen)
  bool depthReadbackEnabled{true}; // Output policy; depth testing remains enabled.
  bool directTextureOutput{false}; // Private child pass: no CPU readback.
  uint64_t directTextureToken{0};
  std::vector<float> depthBuffer;   // Depth [0, 1] (offscreen software / CPU fallback)
  uint32_t generation{0};
  static uint64_t allocateResourceOwnerId();
  uint64_t resourceOwnerId{0};
  uint64_t resourceGeneration{0};
  static std::unique_ptr<CoinRenderBackend> createBackend();
  static bool isGpuBackendAvailable();
  // Admission uses implementation facts without preparing a GPU/device.
  bool supportsOffscreenShadows(bool asynchronous) const;
  CoinRenderBackendStatus prepareBackend();
  CoinRenderSubmitResult preflightSubmission(bool asynchronous);
  void deviceLost();
  CoinRenderDeviceDomain preparedDomain;

  // Reusable candidate storage; never exposed while a submission is pending.
  std::vector<uint8_t> spareColorBuffer;
  std::vector<float> spareDepthBuffer;
  void detachedFromAction();
  bool synchronousReadbackValid{true};
  bool borrowedReadbackValid{false}; // Only after a successful synchronous frame.

  // Window surface specific members
  CoinRenderNativeSurfaceDescriptor nativeDesc{};
  uint64_t surfaceId{0};
  bool suspended{false};
  bool needsReconfigure{false};
  bool windowReadbackRequested{false};
  std::string lastError;

  bool initWindow(const CoinRenderNativeSurfaceDescriptor & desc, const SbVec2i32 & fbSize);
  bool resize(const SbVec2i32 & newSize);
  void clear(float r, float g, float b, float a, float depthVal = 1.0f);
  void readbackRGBA(std::vector<uint8_t> & outRgba) const;
  void readbackDepth(std::vector<float> & outDepth) const;
  uint64_t lastSubmissionSerial{0};
  uint64_t lastValidatedPlanRevision{0};

  // Preflight validation according to Onda 1 profile (Section 4.6)
  static CoinRenderFrameExecutionResult validateProfile(const CoinRenderFramePlan& frame,
                                                        const SbVec2i32& targetSize,
                                                        bool deferUnresolvedAlpha = false);
  static bool validateProfile(const CoinRenderFramePlan & frame, std::string & outDiagnostic);

  // Render execution for target (offscreen or window)
  CoinRenderFrameExecutionResult executeFrame(const CoinRenderFramePlan & frame);
  CoinRenderFrameExecutionResult executeFrame(const CoinRenderFramePlan & frame,
                                    const CoinRenderFrameReuseDecision & reuse);
  CoinRenderFrameExecutionResult executeFrame(const CoinRenderFramePlan & frame,
                                    const CoinRenderFrameReuseDecision & reuse,
                                    const CoinRenderFramePreflight * capturedPreflight);
  CoinRenderFrameExecutionResult executeFrameAsync(const CoinRenderFramePlan & frame,
                                         CoinRenderReadbackTicket & outTicket);
  CoinRenderFrameExecutionResult executeFrameAsync(const CoinRenderFramePlan & frame,
                                         CoinRenderReadbackTicket & outTicket,
                                         const CoinRenderFrameReuseDecision & reuse);
  CoinRenderFrameExecutionResult executeFrameAsync(const CoinRenderFramePlan & frame,
                                         CoinRenderReadbackTicket & outTicket,
                                         const CoinRenderFrameReuseDecision & reuse,
                                         const CoinRenderFramePreflight * capturedPreflight);

  std::unique_ptr<CoinRenderBackend> backend;
  // Available only while a freshly validated frame is being submitted.
  const CoinRenderFramePreflight * submissionPreflight(const CoinRenderFramePlan & frame) const;

private:
  const CoinRenderFramePreflight * activePreflight = nullptr;
  static CoinRenderFrameExecutionResult validateProfileInternal(const CoinRenderFramePlan & frame,
      const SbVec2i32 & targetSize, bool deferUnresolvedAlpha,
      CoinRenderFramePreflight * preflight,
      const CoinRenderFramePreflight * capturedPreflight = nullptr,
      bool allowCompositionBorrow = false,
      CoinRenderCompositionTransferTrace * transfers = nullptr);
  CoinRenderFrameExecutionResult executeFrameInternal(const CoinRenderFramePlan & frame,
                                             CoinRenderReadbackTicket * outTicket,
                                             const CoinRenderFrameReuseDecision & reuse,
                                             const CoinRenderFramePreflight * capturedPreflight = nullptr);
};

#endif // !SOWGPURENDERTARGETP_H
