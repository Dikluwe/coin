#include <Inventor/CoinRenderExport.h>
#ifndef COIN_RENDER_RECORDING_BACKEND_H
#define COIN_RENDER_RECORDING_BACKEND_H

#include "rendering/coinrender/CoinRenderBackend.h"
#include <string>

class COIN_RENDER_DLL_API CoinRenderRecordingBackend : public CoinRenderBackend {
public:
  CoinRenderRecordingBackend();
  ~CoinRenderRecordingBackend() override;

  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override;
  CoinRenderBackendStatus prepare(CoinRenderTargetP & target) override;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame,
                      CoinRenderTargetP & target) override;
  void poll() override;
  const std::string & getLastError() const override;

  std::string recordToString(const CoinRenderFramePlan & frame) const;
  const std::string & getLastRecordingLog() const;

private:
  std::string lastError;
  std::string lastRecordingLog;
  uint64_t submissionSerial{0};
};

#endif // !COIN_RENDER_RECORDING_BACKEND_H
