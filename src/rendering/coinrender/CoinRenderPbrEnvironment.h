#ifndef COIN_RENDER_PBR_ENVIRONMENT_H
#define COIN_RENDER_PBR_ENVIRONMENT_H
#include <Inventor/CoinRenderExport.h>
#include <Inventor/SbByteBuffer.h>
#include <Inventor/SbColor.h>
#include <Inventor/SbString.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/lists/SbList.h>
#include <Inventor/system/inttypes.h>

// Private, versioned CPU payload. No device handles, nodes or parallel math types.
struct CoinRenderPbrEnvironmentSection {
  uint32_t kind=0, face=0, level=0, width=0, height=0;
  float roughness=0;
  uint64_t offset=0, bytes=0;
};
class COIN_RENDER_DLL_API CoinRenderPbrEnvironment {
public:
  enum Source { STUDIO=0, ASYMMETRIC=1, WHITE=2, AXIS=3 };
  enum Kind { SOURCE=1, SPECULAR=2, LUT=3, IRRADIANCE=4 };
  static const uint32_t version=1, model=1, orientation=1;
  static const uint64_t budget=UINT64_C(64)*1024*1024;
  static constexpr float roughnessFloor=.045f;
  uint32_t size=0, levels=0, lutSize=0, source=0;
  uint32_t filterSamples=0, lutSamples=0, shSamples=0;
  uint64_t identity=0;
  SbList<CoinRenderPbrEnvironmentSection> sections;
  SbByteBuffer payload;
  SbVec3f irradiance[9];
  bool read(const SbString & file, SbString & diagnostic);
  bool write(const SbString & file, SbString & diagnostic) const;
  static bool bake(Source source, uint32_t size, uint32_t lutSize,
    uint32_t filterSamples, uint32_t lutSamples, CoinRenderPbrEnvironment & result, SbString & diagnostic);
  static SbVec3f direction(uint32_t face, float u, float v);
  static SbVec3f radiance(Source source, const SbVec3f & direction);
  static SbVec2f dfg(float noV, float roughness, uint32_t samples);
  static float diffuseFresnel(float noV, uint32_t samples);
  static void basis(const SbVec3f &, float result[9]);
  const uint8_t * data(const CoinRenderPbrEnvironmentSection & s) const;
  SbByteBuffer albedoAverage() const; // one RGBA16F texel per roughness row
  uint64_t gpuBytes() const { return payload.size(); }
};
#endif
