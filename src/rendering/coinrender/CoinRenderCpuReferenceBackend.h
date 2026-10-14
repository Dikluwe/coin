#include <Inventor/CoinRenderExport.h>
#ifndef COIN_RENDER_CPU_REFERENCE_BACKEND_H
#define COIN_RENDER_CPU_REFERENCE_BACKEND_H

#include "rendering/coinrender/CoinRenderBackend.h"
#include <string>

class COIN_RENDER_DLL_API CoinRenderCpuReferenceBackend : public CoinRenderBackend {
public:
  CoinRenderCpuReferenceBackend();
  virtual ~CoinRenderCpuReferenceBackend();

  bool isGpuBackend() const override { return false; }
  CoinRenderBackendStatus getStatus() const override;
  CoinRenderBackendStatus prepare(CoinRenderTargetP & target) override;
  CoinRenderSubmitResult submit(const CoinRenderFramePlan & frame, CoinRenderTargetP & target) override;
  void poll() override {}
  const std::string & getLastError() const override { return this->lastError; }

private:
  CoinRenderBackendStatus status;
  std::string lastError;
};

#endif // !COIN_RENDER_CPU_REFERENCE_BACKEND_H
