#ifndef COIN_ENVIRONMENT_H
#define COIN_ENVIRONMENT_H
#include <Inventor/CoinEnvironmentExport.h>
#include <Inventor/SbByteBuffer.h>
#include <Inventor/SbColor.h>
#include <Inventor/SbString.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/lists/SbList.h>
#include <Inventor/system/inttypes.h>
#include <Inventor/environment/CoinEnvironmentRadiance.h>

// Experimental renderer-independent GGX environment payload. Uses Coin base types.
// No renderer dependency, graphics API or device handles. Version 1 preserves .cibl bytes.
struct CoinEnvironmentSection {
  uint32_t kind=0, face=0, level=0, width=0, height=0;
  float roughness=0;
  uint64_t offset=0, bytes=0;
};
class COIN_ENVIRONMENT_DLL_API CoinEnvironment {
public:
  enum Source { STUDIO=0, ASYMMETRIC=1, WHITE=2, AXIS=3, IMPORTED=4, CAPTURED=5 };
  enum Kind { SOURCE=1, SPECULAR=2, LUT=3, IRRADIANCE=4 };
  static const uint32_t version=1, model=1, orientation=1;
  static const uint64_t budget=UINT64_C(64)*1024*1024;
  static constexpr float roughnessFloor=.045f;
  uint32_t size=0, levels=0, lutSize=0, source=0;
  uint32_t bundleVersion=1, sourceLevels=1;
  uint32_t filterSamples=0, lutSamples=0, shSamples=0;
  uint64_t identity=0;
  SbList<CoinEnvironmentSection> sections;
  SbByteBuffer payload;
  SbVec3f irradiance[9];
  bool read(const SbString & file, SbString & diagnostic);
  // Admit the candidate payload and descriptors before allocating its payload.
  // The caller accounts for any existing snapshots separately.
  bool read(const SbString & file, SbString & diagnostic, uint64_t candidateBudgetBytes);
  bool write(const SbString & file, SbString & diagnostic) const;
  // Complete validation also applies to in-memory candidates. Failure never
  // permits a target to upload mutated or structurally inconsistent data.
  bool validate(SbString & diagnostic) const;
  // Runtime identity includes interpretation as well as payload. The v1
  // serialized payload checksum remains unchanged for byte compatibility.
  uint64_t contentIdentity() const;
  static bool bake(Source source, uint32_t size, uint32_t lutSize,
    uint32_t filterSamples, uint32_t lutSamples, CoinEnvironment & result, SbString & diagnostic);
  static bool build(const CoinEnvironmentRadiance & radiance,
    const CoinEnvironmentBuildOptions & options, CoinEnvironment & result,
    SbString & diagnostic, Source provenance=IMPORTED);
  static bool bakeFiltered(Source source,const CoinEnvironmentBuildOptions & options,
    CoinEnvironment & result,SbString & diagnostic);
  // Offline preparation from one radiance source. Resolutions double from
  // minimumFaceSize to options.faceSize. LUT/SH are shared mathematically;
  // every GGX chain retains its own roughness parameterization.
  // payloadBudgetBytes limits the complete set, workingBudgetBytes includes
  // existing output and candidates. Failure conserves the previous set.
  static bool prepareVariants(const CoinEnvironmentRadiance & radiance,
    const CoinEnvironmentBuildOptions & options,uint32_t minimumFaceSize,
    SbList<CoinEnvironment> & result,SbString & diagnostic,Source provenance=IMPORTED);
  static SbVec3f direction(uint32_t face, float u, float v);
  static SbVec3f radiance(Source source, const SbVec3f & direction);
  static SbVec2f dfg(float noV, float roughness, uint32_t samples);
  // Directional A/B albedo of correlated anisotropic GGX, with Schlick Fresnel
  // split as F0*A+B. view is expressed in the tangent/bitangent/normal frame.
  // Explicit CPU preparation; no environment mutation or renderer dependency.
  // Invalid parameters leave result unchanged and provide a diagnostic.
  static bool anisotropicDfg(const SbVec3f & view, float roughness,
    float strength, uint32_t samples, SbVec2f & result, SbString & diagnostic);
  static float diffuseFresnel(float noV, uint32_t samples);
  static void basis(const SbVec3f &, float result[9]);
  const uint8_t * data(const CoinEnvironmentSection & s) const;
  SbByteBuffer albedoAverage() const; // one RGBA16F texel per roughness row
  uint64_t gpuBytes() const { return payload.size(); }
private:
  static bool buildPrepared(const CoinEnvironmentRadiance & radiance,
    const CoinEnvironmentBuildOptions & options,CoinEnvironment & result,
    SbString & diagnostic,Source provenance,const CoinEnvironment * common);
};
#endif
