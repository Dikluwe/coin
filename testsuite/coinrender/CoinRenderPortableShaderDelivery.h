#ifndef COIN_PORTABLE_SHADER_DELIVERY_H
#define COIN_PORTABLE_SHADER_DELIVERY_H
// Narrow delivery witnesses for these authored indexed quads, not a plan
// equivalence oracle. Rendering still has independent RGB/depth expectations.
uint32_t portableBits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}
unsigned portableInputs(Op op) {
  switch (op) {
  case Op::ADD:
  case Op::SUBTRACT:
  case Op::MULTIPLY:
  case Op::DIVIDE:
  case Op::MINIMUM:
  case Op::MAXIMUM:
  case Op::DOT:
    return 2;
  case Op::CLAMP:
    return 3;
  case Op::SAMPLE_2D:
    return 1;
  default:
    return 0;
  }
}
bool portableModuleEqual(const Module &a, const Module &b) {
  if (a.version != b.version || a.output != b.output ||
      a.alphaMayVary != b.alphaMayVary ||
      a.uniforms.size() != b.uniforms.size() ||
      a.expressions.size() != b.expressions.size())
    return false;
  for (size_t i = 0; i < a.uniforms.size(); ++i)
    if (a.uniforms[i].name != b.uniforms[i].name ||
        a.uniforms[i].type != b.uniforms[i].type)
      return false;
  for (size_t i = 0; i < a.expressions.size(); ++i) {
    const auto &x = a.expressions[i], &y = b.expressions[i];
    if (x.op != y.op || x.type != y.type)
      return false;
    for (unsigned j = 0; j < portableInputs(x.op); ++j)
      if (x.inputs[j] != y.inputs[j])
        return false;
    if (x.op == Op::CONSTANT)
      for (unsigned j = 0; j < uint32_t(x.type); ++j)
        if (portableBits(x.value[j]) != portableBits(y.value[j]))
          return false;
    if (x.op == Op::UNIFORM && x.uniformName != y.uniformName)
      return false;
    if ((x.op == Op::TEXCOORD || x.op == Op::SAMPLE_2D) &&
        x.textureUnit != y.textureUnit)
      return false;
  }
  return true;
}
bool portableParametersEqual(const float *a, size_t count,
                             const std::vector<float> &b) {
  if (count != b.size() || (count && !a))
    return false;
  for (size_t i = 0; i < count; ++i)
    if (portableBits(a[i]) != portableBits(b[i]))
      return false;
  return true;
}
bool portableCoreBinding(const CoinRenderFramePlan &f, uint32_t state,
                         const Module &expected,
                         const std::vector<float> &parameters) {
  if (state >= f.renderStates.size())
    return false;
  const auto slot = f.renderStates[state].portableShaderSlot;
  if (!slot || slot > f.portableShaders.size())
    return false;
  const auto &instance = f.portableShaders[slot - 1];
  return portableModuleEqual(instance.module, expected) &&
         portableParametersEqual(instance.parameters.data(),
                                 instance.parameters.size(), parameters);
}
bool portableFfiBinding(const CoinWgpuFrameView &v, uint32_t state,
                        const Module &expected,
                        const std::vector<float> &parameters) {
  if (state >= v.state_count || !v.states)
    return false;
  const auto slot = v.states[state].portable_shader_slot;
  if (!slot || slot > v.portable_module_count || !v.portable_modules)
    return false;
  const auto &packed = v.portable_modules[slot - 1];
  if (!packed.signature || !packed.signature_bytes || !packed.nodes)
    return false;
  Module decoded;
  CoinRenderShaderIr ir;
  std::string error;
  if (!coin_render_shader_decode(packed.signature, packed.signature_bytes,
                                 decoded, ir, error) ||
      !portableModuleEqual(decoded, expected) ||
      packed.node_count != ir.nodes.size() || packed.output != ir.output ||
      packed.alpha_may_vary != unsigned(ir.alphaMayVary) ||
      !portableParametersEqual(packed.parameters, packed.parameter_count,
                               parameters))
    return false;
  for (size_t i = 0; i < ir.nodes.size(); ++i) {
    const auto &a = packed.nodes[i];
    const auto &b = ir.nodes[i];
    if (a.op != uint32_t(b.op) || a.type != uint32_t(b.type))
      return false;
    for (unsigned j = 0; j < portableInputs(b.op); ++j)
      if (a.inputs[j] != b.inputs[j])
        return false;
    if (b.op == Op::CONSTANT)
      for (unsigned j = 0; j < uint32_t(b.type); ++j)
        if (portableBits(a.value[j]) != portableBits(b.value[j]))
          return false;
    if ((b.op == Op::UNIFORM || b.op == Op::TEXCOORD ||
         b.op == Op::SAMPLE_2D) &&
        a.resource != b.resource)
      return false;
  }
  return true;
}
bool portableTriangle(std::array<unsigned, 3> ids, unsigned (&counts)[2]) {
  unsigned first = 0;
  for (unsigned i = 1; i < 3; ++i)
    if (ids[i] < ids[first])
      first = i;
  const std::array<unsigned, 3> ordered = {
      {ids[first], ids[(first + 1) % 3], ids[(first + 2) % 3]}};
  if (ordered == std::array<unsigned, 3>{{0, 1, 2}}) {
    ++counts[0];
    return true;
  }
  if (ordered == std::array<unsigned, 3>{{0, 2, 3}}) {
    ++counts[1];
    return true;
  }
  return false;
}
bool portableCoreQuad(const CoinRenderFramePlan &f,
                      const std::vector<uint32_t> &draws,
                      const SoCoordinate3 *positions) {
  std::set<uint32_t> consumed;
  unsigned triangles[2] = {0, 0};
  for (auto index : draws) {
    if (index >= f.draws.size())
      return false;
    const auto &d = f.draws[index];
    const auto &g = d.geometry;
    if (d.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        g.indexCount % 3 ||
        uint64_t(g.firstIndex) + g.indexCount > f.indices.size())
      return false;
    for (unsigned i = 0; i < g.indexCount; i += 3) {
      std::array<unsigned, 3> ids;
      for (unsigned k = 0; k < 3; ++k) {
        const auto at = g.firstIndex + i + k;
        if (!consumed.insert(at).second || f.indices[at] >= f.vertices.size())
          return false;
        const auto &p = f.vertices[f.indices[at]].position;
        ids[k] = 4;
        for (unsigned c = 0; c < 4; ++c) {
          const auto &a = positions->point[c];
          if (p[0] == a[0] && p[1] == a[1] && p[2] == a[2])
            ids[k] = c;
        }
        if (ids[k] == 4)
          return false;
      }
      if (!portableTriangle(ids, triangles))
        return false;
    }
  }
  return consumed.size() == 6 && triangles[0] == 1 && triangles[1] == 1;
}
std::array<double, 2> portableClip(const CoinWgpuFrameView &v, uint32_t state,
                                   uint32_t vertex) {
  std::array<double, 2> invalid = {
      {std::numeric_limits<double>::infinity(), 0}};
  if (state >= v.state_count || vertex >= v.vertex_count)
    return invalid;
  const auto &p = v.vertices[vertex].position;
  const auto *m = v.states[state].model_view_projection;
  double clip[4] = {m[12], m[13], m[14], m[15]};
  for (unsigned c = 0; c < 4; ++c)
    for (unsigned r = 0; r < 3; ++r)
      clip[c] += p[r] * double(m[r * 4 + c]);
  if (!std::isfinite(clip[3]) || clip[3] == 0)
    return invalid;
  return {{clip[0] / clip[3], clip[1] / clip[3]}};
}
bool portableFfiQuad(const CoinWgpuFrameView &v,
                     const std::vector<uint32_t> &draws,
                     const std::array<std::array<double, 2>, 4> &corners) {
  std::set<uint32_t> consumed;
  unsigned triangles[2] = {0, 0};
  for (auto index : draws) {
    if (index >= v.draw_count)
      return false;
    const auto &d = v.draws[index];
    if (d.topology != 0 || d.index_count % 3 ||
        uint64_t(d.first_index) + d.index_count > v.index_count)
      return false;
    for (unsigned i = 0; i < d.index_count; i += 3) {
      std::array<unsigned, 3> ids;
      for (unsigned k = 0; k < 3; ++k) {
        const auto at = d.first_index + i + k;
        if (!consumed.insert(at).second)
          return false;
        const auto xy = portableClip(v, d.render_state_slot, v.indices[at]);
        ids[k] = 4;
        for (unsigned c = 0; c < 4; ++c)
          if (std::abs(xy[0] - corners[c][0]) < 1e-5 &&
              std::abs(xy[1] - corners[c][1]) < 1e-5)
            ids[k] = c;
        if (ids[k] == 4)
          return false;
      }
      if (!portableTriangle(ids, triangles))
        return false;
    }
  }
  return consumed.size() == 6 && triangles[0] == 1 && triangles[1] == 1;
}
#endif
