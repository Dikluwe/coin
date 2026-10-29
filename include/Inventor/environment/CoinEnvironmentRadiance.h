#ifndef COIN_ENVIRONMENT_RADIANCE_H
#define COIN_ENVIRONMENT_RADIANCE_H
#include <Inventor/CoinEnvironmentExport.h>
#include <Inventor/SbByteBuffer.h>
#include <Inventor/SbString.h>
#include <Inventor/SbVec2i32.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbRotation.h>
#include <Inventor/system/inttypes.h>

/** CPU scene-linear RGB radiance. Rows run from top to bottom. Cubes use
 * CoinEnvironment's +X,-X,+Y,-Y,+Z,-Z convention; panoramas use +Z at the north
 * pole, +X at u=.5 and +Y at u=.75. Pixels are native-endian RGBA float32.
 * Source orientation is applied during preparation, never by graphics calls. */
class COIN_ENVIRONMENT_DLL_API CoinEnvironmentRadiance {
public:
  enum Projection { EQUIRECTANGULAR=0, CUBE=1 };
  enum ColorSpace { LINEAR_SRGB=1 };
  Projection projection=EQUIRECTANGULAR;
  ColorSpace colorSpace=LINEAR_SRGB;
  SbVec2i32 dimensions=SbVec2i32(0,0);
  SbRotation sourceToWorld;
  SbByteBuffer pixels[6];
  uint64_t memoryBytes() const;
  bool validate(SbString & diagnostic, uint64_t memoryBudget=UINT64_C(256)*1024*1024) const;
  SbVec3f sample(const SbVec3f & worldDirection) const;
  static bool supportsOpenExr();
  // Imports accept Radiance RGBE and optional OpenEXR scanline RGB(A) half/float.
  // Imports are atomic. RGBE EXPOSURE/COLORCORR factors are removed once to
  // recover original radiance; caller scale is applied once. Missing primaries
  // use the explicit linear-sRGB import profile. Other primaries are rejected.
  bool readHdrPanorama(const SbString & filename, SbString & diagnostic,
    float scale=1, uint64_t memoryBudget=UINT64_C(256)*1024*1024);
  bool readHdrFaces(const SbString filenames[6], SbString & diagnostic,
    float scale=1, uint64_t memoryBudget=UINT64_C(256)*1024*1024);
};

struct CoinEnvironmentBuildOptions {
  uint32_t faceSize=64, lutSize=64;
  uint32_t filterSamples=256, lutSamples=2048, shSamples=65536;
  uint64_t payloadBudgetBytes=UINT64_C(64)*1024*1024;
  uint64_t workingBudgetBytes=UINT64_C(256)*1024*1024;
};
#endif
