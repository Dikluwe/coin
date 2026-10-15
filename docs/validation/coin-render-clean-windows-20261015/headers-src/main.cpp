#include <Inventor/CoinRenderExport.h>
#include <Inventor/CoinWgpuExport.h>
#include <Inventor/actions/CoinBgfxAction.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoBGFXRenderAction.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>
#include <Inventor/rendering/SoWgpuRenderManagerAdapter.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/rendering/SoWgpuSceneManager.h>
int main(){CoinRenderOptions o;return o.textureSamplingPolicy==COIN_RENDER_SAMPLING_NATIVE?0:1;}
