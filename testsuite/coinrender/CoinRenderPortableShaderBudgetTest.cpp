#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "CoinRenderPortableShaderModuleCases.h"
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderRttCore.h"
#include "rendering/coinrender/CoinRenderShaderModuleCore.h"
#include "rendering/coinwgpu/CoinWgpuFfiFrame.h"
#include <cstring>
#include <limits>

// The sole new internal interface in the completion contract. Keep this
// executable separate so public/GPU cases also link against the old baseline.
COIN_RENDER_DLL_API bool coin_render_shader_resource_bytes(const CoinRenderFramePlan &, uint64_t &, std::string &);
namespace {
using namespace portable_module_test;
constexpr uint64_t budget = UINT64_C(32) * 1024 * 1024;
CoinRenderShaderInstance named(size_t length) {
  CoinRenderShaderInstance s;
  s.module = literal({{.23f,.51f,.79f,1}});
  const std::string name(length, 'q');
  declare(s.module, name.c_str(), Type::FLOAT1);
  std::string error;
  check(coin_render_shader_ir(s.module,s.ir,error), "bounded authored signature lowers: " + error);
  s.parameters = {.625f};
  return s;
}
uint64_t logical(const CoinRenderShaderInstance &s) {
  return 64 + 40 * uint64_t(s.ir.nodes.size()) + 4 * uint64_t(s.parameters.size()) +
    uint64_t(coin_render_shader_key(s.module,s.ir).size());
}
bool count(CoinRenderFramePlan &f, uint64_t expected, const std::string &label) {
  uint64_t actual = UINT64_C(0xbad12345);
  std::string error;
  return check(coin_render_shader_resource_bytes(f,actual,error) && actual==expected,
    label + " exact logical bytes=" + std::to_string(actual) + " error=" + error);
}
bool refuse(CoinRenderFramePlan &f, const std::string &label) {
  const uint64_t sentinel=UINT64_C(0x123456789abcdef0);
  uint64_t bytes=sentinel;
  std::string error;
  const bool accepted=coin_render_shader_resource_bytes(f,bytes,error);
  return check(!accepted && bytes==sentinel && !error.empty(), label + " rejects transactionally: " + error);
}
bool resources() {
  CoinRenderFramePlan f;
  bool ok=count(f,0,"ordinary frame costs zero");
  auto s=named(23);
  const uint64_t one=logical(s);
  f.portableShaders.push_back(s);
  ok &= count(f,one,"one stored module includes descriptor, IR, parameters and signature");
  f.renderStates.resize(1); f.renderStates[0].portableShaderSlot=1;
  f.draws.resize(1024);
  ok &= count(f,one,"many draws referencing same slot count once");
  f.portableShaders.push_back(s);
  ok &= count(f,2*one,"identical signatures in distinct stored slots count twice");
  f.portableShaders[1].parameters[0]=.875f;
  ok &= count(f,2*one,"parameter contents cannot merge stored instances");
  f.portableShaders[1].ir.nodes[0].type=static_cast<Type>(999);
  ok &= refuse(f,"malformed unconsumed second module is still validated");
  f.portableShaders[1]=s;
  f.portableShaders[1].parameters.clear();
  ok &= refuse(f,"missing actual payload cannot undercharge parameter bytes");
  f.portableShaders[1]=s;
  f.portableShaders[1].ir.nodes.push_back(f.portableShaders[1].ir.nodes[0]);
  ok &= refuse(f,"contradictory IR cannot lower reported memory");
  // 512 real, legal signatures produce precisely 32 MiB. This is the
  // production threshold, not a reduced test-only budget.
  const auto unit=named(65364);
  ok &= check(logical(unit)==65536,"independent ABI65 byte arithmetic for 64KiB instance");
  f=CoinRenderFramePlan{}; f.portableShaders.assign(512,unit);
  ok &= count(f,budget,"32MiB inclusive exact table boundary");
  f.portableShaders.back()=named(65365);
  ok &= refuse(f,"32MiB plus one byte");
  std::string diagnostic;
  ok &= check(!f.isValid(&diagnostic) && !diagnostic.empty(),
    "real FramePlan ingress rejects oversized otherwise-valid table");
  CoinWgpuFfiFrame ffi;
  const auto prior=ffi.getView();
  diagnostic.clear();
  ok &= check(!ffi.prepare(f,32,32,diagnostic,false) && !diagnostic.empty() &&
    !std::memcmp(&prior,&ffi.getView(),sizeof(prior)),
    "real FFI ingress rejects aggregate before replacing its output");
  f.portableShaders.back()=unit;
  ok &= count(f,budget,"exact boundary recovers after refusal");
  // A graph containing individually admissible tables must charge all frames
  // and every cube face independently, including equal signatures.
  CoinRenderRttPlan graph(COIN_RENDER_SCENE_TEXTURE_DIRECT);
  CoinRenderFramePlan root;
  root.portableShaders.assign(256,unit);
  CoinRenderRttProducer producer;
  producer.size=SbVec2i32(1,1); producer.sourceRevision=1; producer.plan.portableShaders.assign(256,unit);
  graph.producers.push_back(producer);
  diagnostic.clear();
  ok &= check(graph.validate(root,diagnostic),"root plus producer exactly32MiB is admitted: "+diagnostic);
  graph.producers[0].plan.portableShaders.back()=named(65365);
  diagnostic.clear();
  ok &= check(!graph.validate(root,diagnostic) && !diagnostic.empty(),
    "graph sum32MiB+1 rejects despite each frame independently below32MiB");
  root.portableShaders.clear(); graph.producers.clear();
  producer.plan.portableShaders.assign(86,unit); producer.cube=true; producer.capturedCubeFaces=6;
  for (auto &face:producer.cubeFaces) face=producer.plan;
  graph.producers.push_back(std::move(producer));
  diagnostic.clear();
  ok &= check(!graph.validate(root,diagnostic) && !diagnostic.empty(),
    "all six producer faces coexist; equal module signatures are not graph-deduplicated");
  std::cout << "portable_shader_budget complete=" << ok << std::endl;
  return ok;
}
}
int main(int argc,char **) {
  if(argc!=1)return 2;
  SoDB::init(); CoinRenderAction::initClass();
  return resources()?0:1;
}
