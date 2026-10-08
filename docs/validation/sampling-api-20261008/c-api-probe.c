#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
  CoinRenderCapabilities caps = {0};
  caps.struct_size=sizeof(caps);caps.version=COIN_RENDER_CAPABILITIES_VERSION;
  caps.backend=COIN_RENDER_EXPERIMENTAL_RECORDING;
  caps.implemented_sampling_policies=COIN_RENDER_SAMPLING_POLICY_NATIVE|COIN_RENDER_SAMPLING_POLICY_PORTABLE;
  caps.available_sampling_policies=caps.implemented_sampling_policies;
  caps.qualified_sampling_policies=COIN_RENDER_SAMPLING_POLICY_PORTABLE;
  struct CoinRenderSamplingSelection choice=coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_PORTABLE,1);
  assert(choice.reason==COIN_RENDER_SELECTION_SUPPORTED && choice.policy==COIN_RENDER_SAMPLING_PORTABLE);
  assert(coin_render_select_sampling_policy(&caps,COIN_RENDER_SAMPLING_NATIVE,1).reason==COIN_RENDER_SELECTION_UNQUALIFIED_PROFILE);
  printf("C11 exported API passed: version=%u size=%zu v3prefix=%zu\n",caps.version,sizeof(caps),offsetof(CoinRenderCapabilities,implemented_sampling_policies));
  return 0;
}
