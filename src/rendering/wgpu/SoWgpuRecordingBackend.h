#ifndef COIN_SOWGPURECORDINGBACKEND_H
#define COIN_SOWGPURECORDINGBACKEND_H

#include "rendering/wgpu/SoWgpuBackend.h"
#include <string>

class SoWgpuRecordingBackend : public SoWgpuBackend {
public:
  SoWgpuRecordingBackend();
  ~SoWgpuRecordingBackend() override;

  bool isGpuBackend() const override { return false; }
  BackendStatus getStatus() const override;
  BackendStatus prepare(SoWgpuRenderTargetP & target) override;
  BackendStatus submit(const FramePlan & frame,
                       SoWgpuRenderTargetP & target) override;
  void poll() override;
  const std::string & getLastError() const override;

  std::string recordToString(const FramePlan & frame);
  const std::string & getLastRecordingLog() const;

private:
  std::string lastError;
  std::string lastRecordingLog;
};

#endif // !COIN_SOWGPURECORDINGBACKEND_H
