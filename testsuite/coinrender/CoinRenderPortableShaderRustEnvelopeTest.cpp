#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
namespace {
uint64_t declared=262145;
unsigned queries=0,fills=0;
CoinWgpuStatus hostile(const CoinWgpuPortableModule *,const CoinWgpuPortableRange *,
 const CoinWgpuPortableRange *,const CoinWgpuPortableRange *,uint32_t,char *output,uint64_t capacity,
 uint64_t *required,char *error,uint64_t errorSize) {
  *required=declared;
  if(!output && !capacity){++queries;return COIN_WGPU_OK;}
  ++fills;
  // A deliberate second-call stop keeps every case CPU-only and bounded.
  const char text[]="independent fill sentinel";
  if(error && errorSize){size_t n=sizeof(text)<errorSize?sizeof(text):size_t(errorSize);std::memcpy(error,text,n);error[n-1]=0;}
  return COIN_WGPU_INVALID_ARGUMENT;
}
bool check(bool value,const char *message){if(!value)std::cerr<<"portable_rust_envelope FAIL "<<message<<std::endl;return value;}
CoinWgpuStatus prepare(const CoinWgpuFrameView &frame,char (&error)[512]) {
  queries=fills=0;std::memset(error,0,sizeof(error));
  return coin_wgpu_prepare_portable_frame(&frame,1,error,sizeof(error));
}
}
int main(int argc,char **) {
  if(argc!=1)return 2;
  bool ok=check(coin_wgpu_install_portable_core_v2(&hostile)==COIN_WGPU_OK,"isolated trusted hostile callback installed");
  CoinWgpuPortableNode node{};node.type=4;
  const uint8_t tiny[4]={1,0,0,0};
  CoinWgpuPortableModule module{};module.nodes=&node;module.node_count=1;module.signature=tiny;module.signature_bytes=4;
  CoinWgpuFrameView frame{};frame.abi_version=65;frame.struct_size=sizeof(frame);frame.portable_modules=&module;frame.portable_module_count=1;
  char error[512];
  for(uint64_t length:{UINT64_C(65537),UINT64_C(0x100000000),UINT64_MAX}) {
    module.signature_bytes=length;
    const auto status=prepare(frame,error);
    ok &= check(status==COIN_WGPU_INVALID_ARGUMENT && error[0] && !queries && !fills,
      "oversized/negative raw signature length rejected before slice allocation/read/callback");
  }
  module.signature_bytes=4;
  for(uint64_t length:{UINT64_C(262144),UINT64_C(262145),UINT64_C(262146),UINT64_MAX}) {
    declared=length;
    const auto status=prepare(frame,error);
    const bool legal=length<=262145;
    ok &= check(status!=COIN_WGPU_OK && error[0] && queries==1 && fills==unsigned(legal),
      "source function boundary excludes NUL and rejects excessive required before fill");
    if(legal)ok &= check(std::strstr(error,"independent fill sentinel")!=nullptr,"inclusive function boundary reaches second callback");
  }
  std::vector<uint8_t> signature(65536,0x65);
  module.signature=signature.data();module.signature_bytes=signature.size();
  std::vector<CoinWgpuPortableModule> modules(512,module);
  frame.portable_modules=modules.data();frame.portable_module_count=modules.size();declared=4;
  const auto status=prepare(frame,error);
  ok &= check(status!=COIN_WGPU_OK && error[0] && !queries && !fills,
    "aggregate32MiB checked before any per-module callback; distinct equal slots counted independently");
  frame.portable_modules=&module;frame.portable_module_count=1;declared=262145;
  const auto recovered=prepare(frame,error);
  ok &= check(recovered==COIN_WGPU_INVALID_ARGUMENT && queries==1 && fills==1 &&
    std::strstr(error,"independent fill sentinel"),"valid total recovers after aggregate refusal");
  std::cout<<"portable_rust_envelope complete="<<ok<<" gpu_initialization=0"<<std::endl;
  return ok?0:1;
}
