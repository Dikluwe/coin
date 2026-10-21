#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderPortableShaderModuleCases.h"
#include "rendering/coinrender/CoinRenderShaderModuleCore.h"
#include <cstring>

// Compile the actual C++ callback under test-local installer names. The capture
// only records its function pointer; it does not replace any runtime callback.
#define coin_wgpu_install_cpp_portable_core envelope_install_cpp_core
#define coin_wgpu_install_portable_core_v2 envelope_capture_core
#include "rendering/coinwgpu/CoinWgpuPortableCore.cpp"
#undef coin_wgpu_install_portable_core_v2
#undef coin_wgpu_install_cpp_portable_core
static CoinWgpuPortableCoreLowerV2 callback=nullptr;
extern "C" CoinWgpuStatus envelope_capture_core(CoinWgpuPortableCoreLowerV2 f) {
  callback=f;return COIN_WGPU_OK;
}
namespace {
using namespace portable_module_test;
Module named(size_t length,bool consumed=true) {
  Module m;
  const std::string name(length,'x');
  declare(m,name.c_str(),Type::FLOAT4);
  m.expressions.push_back(consumed?uniform(name.c_str(),Type::FLOAT4):constant({{.2f,.4f,.6f,1}}));
  return m;
}
bool envelopes() {
  bool ok=check(envelope_install_cpp_core()==COIN_WGPU_OK && callback,"real C++ lowering callback captured without runtime");
  if(!ok)return false;
  Scene scene;
  CoinRenderAction action(SbViewportRegion(32,32));
  for (bool consumed:{true,false}) {
    // Authored independently from the ABI66 schema: 8 header + 8 uniform
    // descriptor + 24 IR header + 12 UNIFORM or 24 CONSTANT bytes.
    const size_t fixed=consumed?52:64;
    for(size_t limit:{size_t(65535),size_t(65536),size_t(65537)}) {
      Module m=named(limit-fixed,consumed);
      const bool expected=limit<=65536;
      ok &= validation(m,expected,"canonical envelope adjacent byte="+std::to_string(limit));
      CoinRenderShaderIr ir;
      std::string error;
      const bool lowered=coin_render_shader_ir(m,ir,error);
      ok &= check(lowered==expected,"Core lowering enforces same inclusive signature envelope");
      if(!lowered)continue;
      const auto signature=coin_render_shader_key(m,ir);
      ok &= check(signature.size()==limit,"canonical whole signature has independently calculated exact size");
      Module decoded; CoinRenderShaderIr decodedIr;
      ok &= check(coin_render_shader_decode(reinterpret_cast<const uint8_t*>(signature.data()),signature.size(),decoded,decodedIr,error),
        "decoder accepts inclusive boundary and long individual uniform name");
      ok &= check(action.setPortableShaderModule(scene.program,m),"public setter accepts complete legal envelope");
    }
  }
  Module retained=literal({{.19f,.37f,.83f,1}});
  ok &= check(action.setPortableShaderModule(scene.program,retained),"predecessor setter accepted");
  ok &= check(!action.setPortableShaderModule(scene.program,named(65536)),"huge-name replacement is rejected");
  // Valid storage is intentionally only four bytes. All over-envelope lengths
  // must fail before reading it, including raw negative lengths cast to u64.
  alignas(8) const uint8_t tiny[4]={1,0,0,0};
  for(uint64_t length:{UINT64_C(65537),UINT64_C(0x100000000),UINT64_MAX}) {
    Module out=retained; CoinRenderShaderIr ir;
    std::string error;
    coin_render_shader_ir(retained,ir,error);
    const auto original=coin_render_shader_key(out,ir);
    ok &= check(!coin_render_shader_decode(tiny,size_t(length),out,ir,error) && !error.empty() &&
      coin_render_shader_key(out,ir)==original,"C++ decoder refuses oversized length before reading and preserves outputs");
    CoinWgpuPortableNode node{};node.op=uint32_t(Op::CONSTANT);node.type=4;
    const float values[4]={.19f,.37f,.83f,1};std::memcpy(node.value,values,sizeof(values));
    CoinWgpuPortableModule packet{};packet.nodes=&node;packet.node_count=1;packet.alpha_may_vary=1;
    packet.signature=tiny;packet.signature_bytes=length;
    char output[16];std::memset(output,0x35,sizeof(output));char prior[16];std::memcpy(prior,output,sizeof(output));
    uint64_t required=UINT64_C(0x12345678);char diagnostic[512]={};
    const auto status=callback(&packet,nullptr,nullptr,nullptr,0,output,sizeof(output),&required,diagnostic,sizeof(diagnostic));
    ok &= check(status==COIN_WGPU_INVALID_ARGUMENT && diagnostic[0] && required==UINT64_C(0x12345678) &&
      !std::memcmp(output,prior,sizeof(output)),"raw C++ callback rejects length before read, preserves source/required");
  }
  // A raw IR mutation with identical signature must not become a cache hit.
  CoinRenderShaderIr retainedIr;std::string rawError;coin_render_shader_ir(retained,retainedIr,rawError);
  const auto retainedKey=coin_render_shader_key(retained,retainedIr);
  CoinWgpuPortableNode rawNode{};rawNode.op=uint32_t(Op::CONSTANT);rawNode.type=4;
  std::memcpy(rawNode.value,retained.expressions[0].value.data(),sizeof(rawNode.value));
  CoinWgpuPortableModule raw{};raw.nodes=&rawNode;raw.node_count=1;raw.alpha_may_vary=1;
  raw.signature=reinterpret_cast<const uint8_t*>(retainedKey.data());raw.signature_bytes=retainedKey.size();
  uint64_t required=0;char diagnostic[512]={};
  ok &= check(callback(&raw,nullptr,nullptr,nullptr,0,nullptr,0,&required,diagnostic,sizeof(diagnostic))==COIN_WGPU_OK && required>1 && required<=262145,
    "valid real callback source length is function-only and bounded");
  const auto priorRequired=required;char unchanged[32];std::memset(unchanged,0x31,sizeof(unchanged));
  rawNode.value[0]=.91f;
  ok &= check(callback(&raw,nullptr,nullptr,nullptr,0,unchanged,sizeof(unchanged),&required,diagnostic,sizeof(diagnostic))==COIN_WGPU_INVALID_ARGUMENT &&
    required==priorRequired && unchanged[0]==0x31,"same signature cannot license changed consumed IR or modify previous source metadata");
  rawNode.value[0]=retained.expressions[0].value[0];
  required=0;
  ok &= check(callback(&raw,nullptr,nullptr,nullptr,0,nullptr,0,&required,diagnostic,sizeof(diagnostic))==COIN_WGPU_OK,
    "real callback valid/invalid/valid recovery without cached contamination");
  // Malformed name length inside a bounded buffer must not allocate/read from
  // the advertised extent; a valid decode remains available afterwards.
  auto m=named(31);CoinRenderShaderIr ir;std::string error;coin_render_shader_ir(m,ir,error);
  auto key=coin_render_shader_key(m,ir);
  Module out=retained;CoinRenderShaderIr outIr;coin_render_shader_ir(out,outIr,error);
  const auto old=coin_render_shader_key(out,outIr);
  for(unsigned i=12;i<16;++i)key[i]=char(0xff);
  ok &= check(!coin_render_shader_decode(reinterpret_cast<const uint8_t*>(key.data()),key.size(),out,outIr,error) &&
    !error.empty() && coin_render_shader_key(out,outIr)==old,"malformed embedded name length preserves decoder output");
  key=coin_render_shader_key(m,ir);key.push_back(0);
  ok &= check(!coin_render_shader_decode(reinterpret_cast<const uint8_t*>(key.data()),key.size(),out,outIr,error),"trailing signature byte is not ignored");
  key=coin_render_shader_key(m,ir);
  ok &= check(coin_render_shader_decode(reinterpret_cast<const uint8_t*>(key.data()),key.size(),out,outIr,error),"decoder recovers after malformed/oversized calls");
  std::cout<<"portable_shader_envelope complete="<<ok<<std::endl;return ok;
}
}
int main(int argc,char **) {if(argc!=1)return 2;SoDB::init();CoinRenderAction::initClass();return envelopes()?0:1;}
