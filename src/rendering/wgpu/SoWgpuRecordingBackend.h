#include <Inventor/CoinWgpuExport.h>
#ifndef COIN_SOWGPURECORDINGBACKEND_H
#define COIN_SOWGPURECORDINGBACKEND_H

#include "rendering/wgpu/SoWgpuBackend.h"
#include <string>

class COIN_WGPU_DLL_API SoWgpuRecordingBackend : public SoWgpuBackend {
public:
  SoWgpuRecordingBackend();
  ~SoWgpuRecordingBackend() override;

  bool isGpuBackend() const override { return false; }
  BackendStatus getStatus() const override;
  BackendStatus prepare(SoWgpuRenderTargetP & target) override;
  SubmitResult submit(const FramePlan & frame,
                      SoWgpuRenderTargetP & target) override;
  void poll() override;
  const std::string & getLastError() const override;

  std::string recordToString(const FramePlan & frame);
  const std::string & getLastRecordingLog() const;

private:
  std::string lastError;
  std::string lastRecordingLog;
  uint64_t submissionSerial{0};
};

#endif // !COIN_SOWGPURECORDINGBACKEND_H
