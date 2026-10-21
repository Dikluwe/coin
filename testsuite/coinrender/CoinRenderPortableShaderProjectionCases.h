#ifndef COIN_PORTABLE_SHADER_PROJECTION_CASES_H
#define COIN_PORTABLE_SHADER_PROJECTION_CASES_H
#include "CoinRenderPortableShaderShadowCases.h"
// The same public image/UV/matrix/program occurs in two independently authored
// roots on one target. Shadow reception supplies DIRECT_ST; ordinary geometry
// supplies PROJECTIVE. Only SAMPLE performs the policy-dependent division.
Color projectionTexel(const std::vector<uint8_t> &pixels, double s, double t) {
  s -= std::floor(s);
  t -= std::floor(t);
  const unsigned x = unsigned(std::floor(s * 2)),
                 y = unsigned(std::floor(t * 2));
  Color color;
  for (unsigned c = 0; c < 4; ++c)
    color[c] = pixels[(y * 2 + x) * 4 + c] / 255.f;
  return color;
}
Module projectionSample(bool uniformCoordinate, bool effectiveCoordinate,
                        float constantQ = 1) {
  Module m;
  if (uniformCoordinate) {
    declare(m, "homogeneous", Type::FLOAT4);
    m.expressions.push_back(uniform("homogeneous", Type::FLOAT4));
  } else if (effectiveCoordinate) {
    auto coord = operation(Op::TEXCOORD, Type::FLOAT4);
    coord.textureUnit = 0;
    m.expressions.push_back(coord);
  } else
    m.expressions.push_back(constant({{.3f, .44f, .09f, constantQ}}));
  auto sample = operation(Op::SAMPLE_2D, Type::FLOAT4, 0);
  sample.textureUnit = 0;
  m.expressions.push_back(sample);
  m.output = 1;
  return m;
}
Module projectionCoordinateColor() {
  Module m;
  auto coord = operation(Op::TEXCOORD, Type::FLOAT4);
  coord.textureUnit = 0;
  m.expressions = {coord,
                   constant({{.2f, .1f, 1, .05f}}),
                   operation(Op::DOT, Type::FLOAT1, 0, 1),
                   constant({{.7f, .5f, .9f, 0}}),
                   operation(Op::MULTIPLY, Type::FLOAT4, 2, 3),
                   constant({{.1f, .2f, .15f, 1}}),
                   operation(Op::ADD, Type::FLOAT4, 4, 5)};
  m.output = 6;
  return m;
}
Color projectionCoordinateExpected(float q) {
  // (.14,.26,0,1) times the authorial matrix gives(.3,.44,.09,q).
  const float d = .3f * .2f + .44f * .1f + .09f + q * .05f;
  return {{.1f + .7f * d, .2f + .5f * d, .15f + .9f * d, 1}};
}
bool projectionDelivery(Run &run, SbUniqueId source, SoCoordinate3 *positions,
                        bool direct, float q,
                        const std::vector<uint8_t> &pixels,
                        const Module &expected,
                        const std::vector<float> &parameters) {
  const auto &f = run.audit->plan;
  std::vector<uint32_t> sourceDraws;
  std::set<uint32_t> consumed;
  bool ok = true;
  for (uint32_t i = 0; i < f.draws.size(); ++i) {
    const auto &d = f.draws[i];
    if (d.sourceNodeId != source || d.shadowLightSlot)
      continue;
    sourceDraws.push_back(i);
    if (!check(d.renderStateSlot < f.renderStates.size(),
               "projection source state exists"))
      return false;
    const auto &s = f.renderStates[d.renderStateSlot];
    const auto t = coin_render_texture_unit(s, 0);
    consumed.insert(s.portableShaderSlot);
    ok &=
        check(portableCoreBinding(f, d.renderStateSlot, expected, parameters) &&
                  s.textureProjection ==
                      (direct ? CoinRenderTextureProjection::DIRECT_ST
                              : CoinRenderTextureProjection::PROJECTIVE) &&
                  t.enabled && t.imageSlot < f.textures.size() &&
                  t.samplerSlot < f.samplers.size(),
              "every source triangle resolves exact "
              "module/signature/parameters and effective projection");
    if (t.imageSlot >= f.textures.size() || t.samplerSlot >= f.samplers.size())
      return false;
    ok &= check(
        f.textures[t.imageSlot].width == 2 &&
            f.textures[t.imageSlot].height == 2 &&
            f.textures[t.imageSlot].pixelsRgba == pixels &&
            f.samplers[t.samplerSlot].filter ==
                CoinRenderTextureFilter::NEAREST &&
            f.samplers[t.samplerSlot].wrapS == CoinRenderTextureWrap::REPEAT &&
            f.samplers[t.samplerSlot].wrapT == CoinRenderTextureWrap::REPEAT &&
            t.matrix[0][0] == 2 && t.matrix[1][1] == 1.5f &&
            t.matrix[2][2] == .5f && t.matrix[3][0] == .02f &&
            t.matrix[3][1] == .05f && t.matrix[3][2] == .09f &&
            t.matrix[3][3] == q,
        "all occurrences retain asymmetric image, sampler and authorial matrix "
        "exactly once");
  }
  ok &= check(portableCoreQuad(f, sourceDraws, positions) &&
                  consumed.size() == f.portableShaders.size(),
              "all actual receiver draws consume exact disjoint six-index "
              "authored quad and complete module table");
  CoinWgpuFfiFrame ffi;
  std::string error;
  const bool packed = ffi.prepare(f, 64, 64, error, false);
  ok &= check(packed, "effective projection reaches FFI: " + error);
  if (!packed)
    return false;
  const auto &v = ffi.getView();
  std::vector<uint32_t> packedDraws;
  std::set<uint32_t> packedSlots;
  for (uint32_t i = 0; i < v.draw_count; ++i) {
    const auto state = v.draws[i].render_state_slot;
    if (state >= v.state_count) {
      ok &= check(false, "packed projection state exists");
      continue;
    }
    if (!v.states[state].portable_shader_slot)
      continue;
    packedDraws.push_back(i);
    packedSlots.insert(v.states[state].portable_shader_slot);
    ok &= check(portableFfiBinding(v, state, expected, parameters) &&
                    v.states[state].texture_projection == (direct ? 1u : 0u) &&
                    v.states[state].has_texture &&
                    v.states[state].texture_matrix[15] == q,
                "all packed triangle bindings preserve authorial module and "
                "homogeneous P07 policy");
  }
  std::array<std::array<double, 2>, 4> corners;
  for (unsigned i = 0; i < 4; ++i)
    corners[i] = {{positions->point[i][0] / (direct ? 2. : 1.),
                   positions->point[i][1] / (direct ? 2. : 1.)}};
  ok &= check(v.abi_version == 63 && portableFfiQuad(v, packedDraws, corners) &&
                  packedSlots.size() == v.portable_module_count,
              "packed module occurrences retain complete disjoint quad and "
              "exactly consumed payloads");
  if (!ok) {
    shadowModuleDumpFrame(f, "projectionDelivery failure", source);
    shadowModuleDumpFfi(v, "projectionDelivery failure");
  }
  return ok;
}

bool projectionPolicies(Run &run) {
  bump_shadow::Scene shadow(1, true);
  shadow.bumpStage->whichChild = SO_SWITCH_NONE;
  shadow.paint(false);
  shadow.shine = 0;
  shadow.setSpec({{.24, .12, .3}});
  shadow.quality->textureQuality = .1f;
  const std::vector<uint8_t> pixels = {30, 210, 70,  255, 220, 60,  160, 255,
                                       95, 120, 235, 255, 180, 200, 20,  255};
  shadow.base->wrapS = shadow.base->wrapT = SoTexture2::REPEAT;
  shadow.base->image.setValue(SbVec2s(2, 2), 4, pixels.data());
  for (unsigned i = 0; i < 4; ++i)
    shadow.baseUv->point.set1Value(i, SbVec2f(.14f, .26f));
  auto *matrix = new SoTextureMatrixTransform;
  SbMatrix authored = SbMatrix::identity();
  authored[0][0] = 2;
  authored[1][1] = 1.5f;
  authored[2][2] = .5f;
  authored[3][0] = .02f;
  authored[3][1] = .05f;
  authored[3][2] = .09f;
  matrix->matrix = authored;
  shadow.group->insertChild(matrix, shadow.group->findChild(shadow.positions));
  shadow.group->insertChild(run.scene.program,
                            shadow.group->findChild(shadow.positions));
  run.scene.attach(true);
  for (SoNode *node :
       {static_cast<SoNode *>(shadow.quality),
        static_cast<SoNode *>(shadow.base),
        static_cast<SoNode *>(shadow.baseUv), static_cast<SoNode *>(matrix)})
    run.scene.root->insertChild(node,
                                run.scene.root->findChild(run.scene.program));
  auto *coordinate =
      run.scene.parameter4("homogeneous", {{.3f, .44f, .09f, .5f}});
  bool ok = true;
  Module currentModule;
  auto bindModule = [&](const Module &module) {
    currentModule = module;
    return run.bind(module);
  };
  for (bool direct : {true, false}) {
    SoNode *root = direct ? static_cast<SoNode *>(shadow.root)
                          : static_cast<SoNode *>(run.scene.root);
    const auto mesh =
        direct ? shadow.mesh->getNodeId() : run.scene.mesh->getNodeId();
    auto setQ = [&](float q) {
      SbMatrix m = authored;
      m[3][3] = q;
      matrix->matrix = m;
    };
    auto draw = [&](Color expected, Frame &f, float q, const char *label) {
      const bool rendered = run.render(f, label, root);
      std::vector<float> parameters;
      if (!currentModule.uniforms.empty()) {
        const auto &value = coordinate->value.getValue();
        for (unsigned c = 0; c < 4; ++c)
          parameters.push_back(value[c]);
      }
      return rendered &&
             projectionDelivery(run, mesh,
                                direct ? shadow.positions : run.scene.positions,
                                direct, q, pixels, currentModule, parameters) &&
             (direct ? shadowModuleImage(f, shadow, true, false, expected)
                     : image(f, expected));
    };
    Frame stable, changed, back;
    setQ(1);
    run.scene.fragment->parameter.set1Value(0, coordinate);
    ok &= bindModule(projectionSample(true, false));
    coordinate->value.setValue(.3f, .44f, .09f, .5f);
    ok &= draw(
        projectionTexel(pixels, direct ? .3 : .6, direct ? .44 : .88), stable,
        1, "uniform homogeneous Q=.5 follows actual DIRECT/PROJECTIVE policy");
    coordinate->value.setValue(.3f, .44f, .09f, 0);
    if (direct)
      ok &= draw(projectionTexel(pixels, .3, .44), changed, 1,
                 "DIRECT_ST samples finite uniform Q0 without division");
    else
      ok &= run.refuse(stable,
                       "PROJECTIVE uniform SAMPLE Q0 refuses atomically", root);
    coordinate->value.setValue(.3f, .44f, .09f, -1);
    ok &= draw(projectionTexel(pixels, direct ? .3 : -.3, direct ? .44 : -.44),
               changed, 1,
               "finite negative Q remains valid and keeps projective sign");
    coordinate->value.setValue(.3f, .44f, .09f, .5f);
    ok &= draw(projectionTexel(pixels, direct ? .3 : .6, direct ? .44 : .88),
               back, 1, "same-target homogeneous parameter A/B/A after Q0");
    run.scene.fragment->parameter.setNum(0);
    ok &= bindModule(projectionCoordinateColor());
    for (float q : {.5f, 0.f, -1.f}) {
      setQ(q);
      ok &= draw(
          projectionCoordinateExpected(q), stable, q,
          "TEXCOORD observes effective STRQ before any division, including Q0");
    }
    ok &= bindModule(projectionSample(false, true));
    setQ(.5f);
    ok &= draw(projectionTexel(pixels, direct ? .3 : .6, direct ? .44 : .88),
               stable, .5f, "TEXCOORD to SAMPLE uses matrix exactly once");
    setQ(0);
    if (direct)
      ok &= draw(projectionTexel(pixels, .3, .44), changed, 0,
                 "DIRECT_ST TEXCOORD SAMPLE permits captured Q0");
    else
      ok &= run.refuse(
          stable, "PROJECTIVE TEXCOORD SAMPLE Q0 refuses atomically", root);
    setQ(-1);
    ok &= draw(projectionTexel(pixels, direct ? .3 : -.3, direct ? .44 : -.44),
               back, -1, "negative effective Q divides only in PROJECTIVE");
    setQ(0);
    ok &= bindModule(projectionSample(false, false, 1));
    ok &= draw(projectionTexel(pixels, .3, .44), stable, 0,
               "constant SAMPLE Q1 ignores unused captured Q0 and does not "
               "reapply matrix");
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()}) {
      setQ(invalid);
      ok &= run.refuse(stable,
                       "nonfinite authorial coordinate matrix remains invalid "
                       "in both policies",
                       root);
    }
    setQ(1);
    ok &= draw(projectionTexel(pixels, .3, .44), stable, 1,
               "nonfinite matrix recovery retains valid constant SAMPLE");
    Module zero = projectionSample(false, false, 0);
    ok &= validation(zero, true,
                     "pure constant Q0 AST has a legal DIRECT_ST execution");
    ok &= bindModule(zero);
    if (direct)
      ok &= draw(projectionTexel(pixels, .3, .44), changed, 1,
                 "DIRECT_ST actually executes constant-coordinate SAMPLE Q0");
    else
      ok &= run.refuse(
          stable, "PROJECTIVE rejects the same otherwise-valid constant Q0 AST",
          root);
    ok &= bindModule(projectionSample(false, false, 1));
    ok &= draw(projectionTexel(pixels, .3, .44), back, 1,
               "constant-coordinate Q0 recovery on the same target");
  }
  return ok;
}
#endif
