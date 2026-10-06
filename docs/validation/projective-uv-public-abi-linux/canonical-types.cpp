#include <Inventor/C/basic.h>
#include <Inventor/elements/SoLazyElement.h>
#include <type_traits>
static_assert(COIN_MAJOR_VERSION==4,"Coin 4 headers required");
static_assert(std::is_same<SoLazyElement::LightModel,SoLazyElement::CoinRenderLightModel>::value,"experimental name must alias canonical type");
static_assert(SoLazyElement::BASE_COLOR==0&&SoLazyElement::PHONG==1,"existing enum values preserved");
int main(){return 0;}
