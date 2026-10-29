#ifndef COIN_ENVIRONMENT_EXR_P_H
#define COIN_ENVIRONMENT_EXR_P_H
#include <Inventor/environment/CoinEnvironmentRadiance.h>
bool coin_environment_read_exr(const SbString & file,SbVec2i32 & dimensions,
  SbByteBuffer & pixels,float scale,uint64_t memoryBudget,SbString & diagnostic);
#endif
