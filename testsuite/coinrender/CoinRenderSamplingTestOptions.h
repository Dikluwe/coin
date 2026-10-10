#ifndef COIN_RENDER_SAMPLING_TEST_OPTIONS_H
#define COIN_RENDER_SAMPLING_TEST_OPTIONS_H
#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include <string>
#include <iostream>
static CoinRenderTextureSamplingPolicy samplingTestPolicy = COIN_RENDER_SAMPLING_NATIVE;
static void configureSamplingTest(int argc, char** argv) {
  std::cout << std::unitbuf;
  for (int i=1; i<argc; ++i) if (std::string(argv[i]) == "--portable-sampling")
    samplingTestPolicy = COIN_RENDER_SAMPLING_PORTABLE;
  std::cout << "sampling_test_policy=" << samplingTestPolicy << '\n';
}
static CoinRenderOptions samplingTestOptions() {
  std::string diagnostic;
  auto options = CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if (!diagnostic.empty()) options.renderer = static_cast<CoinRenderRenderer>(999);
  options.textureSamplingPolicy = samplingTestPolicy;
  return options;
}
static CoinRenderTarget* samplingTestOffscreen(const SbVec2i32& size) {
  return CoinRenderTarget::createOffscreen(size, samplingTestOptions());
}
static CoinRenderTarget* samplingTestOffscreen(const SbVec2i32& size, CoinRenderOptions options) {
  options.textureSamplingPolicy = samplingTestPolicy;
  return CoinRenderTarget::createOffscreen(size, options);
}
#endif
