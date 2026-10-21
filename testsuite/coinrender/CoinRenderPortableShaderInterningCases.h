#ifndef COIN_PORTABLE_SHADER_INTERNING_CASES_H
#define COIN_PORTABLE_SHADER_INTERNING_CASES_H
#include "CoinRenderPortableShaderDelivery.h"
// Diagnostics are defined later with the VSM focal; no dependency on its flow.
void shadowModuleDumpFrame(const CoinRenderFramePlan &, const char *, SbUniqueId, SbUniqueId);
void shadowModuleDumpFfi(const CoinWgpuFrameView &, const char *);
bool interningImage(const Frame &f, const std::array<Color, 2> &expected) {
  if (f.rgba.size() != 64 * 64 * 4 || f.depth.size() != 64 * 64)
    return false;
  unsigned inside[2] = {0, 0}, outside = 0, bad = 0, depthBad = 0;
  const double margin = 2.1 * 2 / 64;
  for (unsigned y = 0; y < 64; ++y)
    for (unsigned x = 0; x < 64; ++x) {
      const double wx = (x + .5) * 2 / 64 - 1, wy = 1 - (y + .5) * 2 / 64;
      const size_t p = size_t(y) * 64 + x;
      bool exterior = true;
      for (unsigned lane = 0; lane < 2; ++lane) {
        const double center = lane ? .48 : -.48;
        const double lx = (wx - center) / .5, ly = wy / .5;
        const bool in = lx > -.75 + margin / .5 && lx < .75 - margin / .5 &&
                        ly > -.71875 + margin / .5 && ly < .75 - margin / .5;
        exterior &= lx < -.75 - margin / .5 || lx > .75 + margin / .5 ||
                    ly < -.71875 - margin / .5 || ly > .75 + margin / .5;
        if (in) {
          ++inside[lane];
          for (unsigned c = 0; c < 4; ++c)
            bad += std::abs(f.rgba[p * 4 + c] - 255. * expected[lane][c]) >
                   (c == 3 ? 1 : 3);
          depthBad += std::abs(f.depth[p] - 2.9 / 9.9) > 2e-5;
        }
      }
      if (exterior) {
        ++outside;
        for (unsigned c = 0; c < 4; ++c)
          bad += f.rgba[p * 4 + c] > (c == 3 ? 1 : 3);
        depthBad += f.depth[p] != 1;
      }
    }
  std::cout << "portable_module_intern_image inside=" << inside[0] << ','
            << inside[1] << " outside=" << outside << " bad=" << bad
            << " depth_bad=" << depthBad << std::endl;
  return check(inside[0] > 100 && inside[1] > 100 && outside > 500 && !bad &&
                   !depthBad,
               "two independent authorial uniform colors and depth");
}
bool interningDelivery(Run &run, const Module &module,
                       const std::array<std::vector<float>, 2> &parameters,
                       unsigned unique) {
  const auto &f = run.audit->plan;
  std::vector<uint32_t> lanes[2];
  std::set<uint32_t> slots;
  bool ok = true;
  for (uint32_t i = 0; i < f.draws.size(); ++i) {
    const auto &d = f.draws[i];
    if (d.sourceNodeId != run.scene.mesh->getNodeId() || d.shadowLightSlot)
      continue;
    if (!check(d.renderStateSlot < f.renderStates.size(),
               "shared shape state exists"))
      return false;
    const auto &state = f.renderStates[d.renderStateSlot];
    const unsigned lane = state.model[3][0] < 0 ? 0 : 1;
    lanes[lane].push_back(i);
    slots.insert(state.portableShaderSlot);
    SbMatrix model = SbMatrix::identity();
    model[0][0] = model[1][1] = model[2][2] = .5f;
    model[3][0] = lane ? .48f : -.48f;
    for (unsigned r = 0; r < 4; ++r)
      for (unsigned c = 0; c < 4; ++c)
        ok &= check(state.model[r][c] == model[r][c],
                    "occurrence retains exact authored scale/translation");
    ok &= check(
        portableCoreBinding(f, d.renderStateSlot, module, parameters[lane]),
        "each triangle resolves correct 4KiB signature and bit-exact "
        "occurrence parameters");
  }
  for (unsigned lane = 0; lane < 2; ++lane)
    ok &= check(portableCoreQuad(f, lanes[lane], run.scene.positions),
                "all triangles cover this occurrence once without depending on "
                "draw count");
  size_t logical = 0;
  for (const auto &instance : f.portableShaders) {
    logical += instance.parameters.size();
    ok &= check(instance.parameters.size() == 1024 &&
                    instance.module.uniforms.size() == 256,
                "full logical signature remains 4KiB without truncation");
  }
  ok &= check(f.portableShaders.size() == unique && slots.size() == unique &&
                  logical == size_t(unique) * 1024,
              "identical active payloads share one frame instance; "
              "bit-distinct values never merge");
  CoinWgpuFfiFrame ffi;
  std::string error;
  const bool prepared = ffi.prepare(f, 64, 64, error, false);
  ok &= check(prepared, "real interning transport packs: " + error);
  if (!prepared)
    return false;
  const auto &v = ffi.getView();
  std::vector<uint32_t> packed[2];
  std::set<uint32_t> packedSlots;
  for (uint32_t i = 0; i < v.draw_count; ++i) {
    const auto &d = v.draws[i];
    if (d.render_state_slot >= v.state_count ||
        uint64_t(d.first_index) + d.index_count > v.index_count ||
        !d.index_count) {
      ok &= check(false, "packed shared geometry range/state valid");
      continue;
    }
    const auto xy =
        portableClip(v, d.render_state_slot, v.indices[d.first_index]);
    if (!std::isfinite(xy[0]) || xy[0] == 0) {
      ok &= check(false, "separated clip-space occurrence witness");
      continue;
    }
    const unsigned lane = xy[0] < 0 ? 0 : 1;
    packed[lane].push_back(i);
    packedSlots.insert(v.states[d.render_state_slot].portable_shader_slot);
    ok &= check(
        portableFfiBinding(v, d.render_state_slot, module, parameters[lane]),
        "every packed triangle retains exact occurrence signature/parameters "
        "including signed zero");
  }
  for (unsigned lane = 0; lane < 2; ++lane) {
    std::array<std::array<double, 2>, 4> corners;
    for (unsigned i = 0; i < 4; ++i)
      corners[i] = {
          {(lane ? .48 : -.48) + .5 * run.scene.positions->point[i][0],
           .5 * run.scene.positions->point[i][1]}};
    ok &= check(portableFfiQuad(v, packed[lane], corners),
                "packed occurrence retains both disjoint authored triangles");
  }
  size_t packedLogical = 0;
  for (uint64_t i = 0; i < v.portable_module_count; ++i)
    packedLogical += v.portable_modules[i].parameter_count;
  ok &= check(v.abi_version == 63 && v.portable_module_count == unique &&
                  packedSlots.size() == unique &&
                  packedLogical == size_t(unique) * 1024,
              "ABI63 delivers only unique logical 4KiB payloads, never one "
              "copy per triangle or node");
  if (!ok) {
    shadowModuleDumpFrame(f, "interning failure", run.scene.mesh->getNodeId(), 0);
    shadowModuleDumpFfi(v, "interning failure");
  }
  return ok;
}
bool interning(Run &run) {
  auto &scene = run.scene;
  auto *geometry = new SoGroup;
  geometry->ref();
  geometry->addChild(scene.positions);
  geometry->addChild(scene.mesh);
  scene.root->removeChild(scene.positions);
  scene.root->removeChild(scene.mesh);
  auto *other = new SoShaderProgram;
  other->ref();
  auto *fragment = new SoFragmentShader;
  fragment->sourceType = SoShaderObject::GLSL_PROGRAM;
  fragment->sourceProgram = "void main(){gl_FragColor=vec4(0.0,1.0,0.0,1.0);}";
  fragment->isActive = TRUE;
  other->shaderObject.set1Value(0, fragment);
  Module module;
  module.expressions = {uniform("field0", Type::FLOAT4),
                        uniform("field255", Type::FLOAT4),
                        operation(Op::ADD, Type::FLOAT4, 0, 1)};
  module.output = 2;
  std::array<std::vector<float>, 2> parameters;
  std::vector<SoShaderParameter4f *> right;
  for (unsigned i = 0; i < 256; ++i) {
    const std::string name = "field" + std::to_string(i);
    declare(module, name.c_str(), Type::FLOAT4);
    Color value = {
        {float(i % 7) * .01f, float(i % 11) * .01f, float(i % 13) * .01f, .5f}};
    if (i == 0)
      value = {{.1f, .2f, .3f, .25f}};
    if (i == 255)
      value = {{.6f, .35f, .2f, .75f}};
    if (i == 128)
      value[0] = 0.f;
    scene.parameter4(name, value);
    auto *parameter = new SoShaderParameter4f;
    parameter->name = name.c_str();
    parameter->value.setValue(value[0], value[1], value[2], value[3]);
    fragment->parameter.set1Value(i, parameter);
    right.push_back(parameter);
    for (unsigned lane = 0; lane < 2; ++lane)
      for (auto component : value)
        parameters[lane].push_back(component);
  }
  bool ok = run.bind(module) &&
            check(run.action.setPortableShaderModule(other, module),
                  "distinct public programs register identical logical "
                  "module/signature");
  SoSeparator *branches[2];
  for (unsigned lane = 0; lane < 2; ++lane) {
    auto *branch = new SoSeparator;
    branch->ref();
    branches[lane] = branch;
    auto *translation = new SoTranslation;
    translation->translation.setValue(lane ? .48f : -.48f, 0, 0);
    branch->addChild(translation);
    auto *scale = new SoScale;
    scale->scaleFactor.setValue(.5f, .5f, .5f);
    branch->addChild(scale);
    auto *material = new SoMaterial;
    material->diffuseColor.setValue(lane ? .8f : .2f, lane ? .2f : .4f,
                                    lane ? .3f : .7f);
    material->transparency = 0;
    branch->addChild(material);
    branch->addChild(lane ? other : scene.program);
    branch->addChild(geometry);
    scene.root->addChild(branch);
  }
  std::array<Color, 2> colors = {
      {{{.7f, .55f, .5f, 1}}, {{.7f, .55f, .5f, 1}}}};
  auto draw = [&](unsigned unique, const char *label) {
    Frame frame;
    return run.render(frame, label) && interningImage(frame, colors) &&
           interningDelivery(run, module, parameters, unique);
  };
  ok &= draw(
      1,
      "two nodes and multiple triangles share one identical full 4KiB payload");
  auto zero = right[128]->value.getValue();
  zero[0] = -0.f;
  right[128]->value = zero;
  parameters[1][128 * 4] = -0.f;
  ok &= draw(
      2,
      "bit-exact minus zero cannot merge with plus zero despite identical RGB");
  ok &= check(run.lastCompiles.empty(),
              "signed-zero parameter edit does not compile shader code");
  zero[0] = 0.f;
  right[128]->value = zero;
  parameters[1][128 * 4] = 0.f;
  ok &= draw(1, "restored plus-zero payload shares one instance again");
  right[255]->value.setValue(.7f, .25f, .35f, .75f);
  const Color changed = {{.7f, .25f, .35f, .75f}};
  for (unsigned c = 0; c < 4; ++c)
    parameters[1][255 * 4 + c] = changed[c];
  colors[1] = {{.8f, .45f, .65f, 1}};
  ok &= draw(
      2,
      "last field changes only its occurrence with complete current payload");
  ok &= check(run.lastCompiles.empty(),
              "distinct uniform values reuse the same AST pipeline");
  scene.root->removeChild(branches[0]);
  scene.root->removeChild(branches[1]);
  scene.root->addChild(branches[1]);
  scene.root->addChild(branches[0]);
  ok &= draw(
      2,
      "reversed occurrence order keeps distinct signed payload associations");
  right[255]->value.setValue(.6f, .35f, .2f, .75f);
  parameters[1] = parameters[0];
  colors[1] = colors[0];
  ok &= draw(1, "complete A/B/A payload restoration resumes unique sharing");
  for (auto *branch : branches)
    branch->unref();
  other->unref();
  geometry->unref();
  return ok;
}
#endif
