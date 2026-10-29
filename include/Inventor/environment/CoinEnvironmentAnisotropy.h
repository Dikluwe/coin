#ifndef COIN_ENVIRONMENT_ANISOTROPY_H
#define COIN_ENVIRONMENT_ANISOTROPY_H
#include <Inventor/CoinEnvironmentExport.h>
#include <Inventor/SbByteBuffer.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbString.h>
#include <Inventor/system/inttypes.h>

struct CoinEnvironmentAnisotropyOptions {
  uint32_t viewSize=32,roughnessSize=32,strengthSize=8,azimuthSize=16,samples=512;
  uint64_t payloadBudgetBytes=UINT64_C(16)*1024*1024;
  // Includes a retained output while a complete candidate is prepared/read.
  uint64_t workingBudgetBytes=UINT64_C(32)*1024*1024;
};
// Renderer-independent lighting product. A/B are the directional Schlick
// coefficients; Z=A+B is unit-Fresnel energy. W is one. All texels RGBA16F LE.
// The atlas uses strength tiles across X, azimuth tiles across Y. Each tile
// is viewSize by roughnessSize, with endpoint grids in all four dimensions.
class COIN_ENVIRONMENT_DLL_API CoinEnvironmentAnisotropy {
public:
  uint32_t viewSize=0,roughnessSize=0,strengthSize=0,azimuthSize=0,samples=0;
  uint64_t identity=0;
  SbByteBuffer directional,average;
  uint32_t atlasWidth() const {return viewSize*strengthSize;}
  uint32_t atlasHeight() const {return roughnessSize*azimuthSize;}
  uint64_t bytes() const {return directional.size()+average.size();}
  uint64_t contentIdentity() const;
  bool validate(SbString & diagnostic) const;
  static bool prepare(const CoinEnvironmentAnisotropyOptions &,
    CoinEnvironmentAnisotropy & result,SbString & diagnostic);
  bool read(const SbString &,SbString & diagnostic,
    uint64_t workingBudgetBytes=UINT64_C(32)*1024*1024);
  bool write(const SbString &,SbString & diagnostic) const;
  // Consumers validate a frozen product once. These queries check layout and
  // arguments without rescanning the payload. Invalid inputs preserve result.
  bool evaluate(const SbVec3f & tangentView,float roughness,float strength,
    SbVec2f & result,SbString & diagnostic) const;
  bool evaluateAverage(float roughness,float strength,SbVec2f & result,
    SbString & diagnostic) const;
};
#endif
