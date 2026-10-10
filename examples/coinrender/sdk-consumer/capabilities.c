#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <stddef.h>
#include <stdio.h>
_Static_assert(COIN_RENDER_SAMPLING_NATIVE == 0, "native policy ordinal stays zero");
_Static_assert(sizeof(CoinRenderCapabilities) == 584, "v4 public capability ABI");
_Static_assert(offsetof(CoinRenderCapabilities, implemented_sampling_policies) == 536, "v3 prefix ABI");
int main(void) {
  printf("installed_C11_capabilities_v4_size=%zu v3_prefix=%zu native_policy=%d\n",
    sizeof(CoinRenderCapabilities), offsetof(CoinRenderCapabilities, implemented_sampling_policies), COIN_RENDER_SAMPLING_NATIVE);
  return 0;
}
