#ifndef COIN_PORTABLE_SHADER_SHADOW_CASES_H
#define COIN_PORTABLE_SHADER_SHADOW_CASES_H
// Passive diagnostics only: table ordinals are printed before any existing
// gate or early return. The assertions and rendering flow are unchanged.
void shadowModuleDumpFrame(const CoinRenderFramePlan &f, const char *phase,
                           SbUniqueId receiver, SbUniqueId caster = 0) {
  std::cout << "portable_shadow_delivery phase=" << phase
            << " revision=" << f.revision << " expected_receiver=" << receiver
            << " expected_caster=" << caster << " draws=" << f.draws.size()
            << " states=" << f.renderStates.size()
            << " modules=" << f.portableShaders.size()
            << " groups=" << f.shadowGroups.size()
            << " lights=" << f.shadowLights.size() << std::endl;
  for (size_t i = 0; i < f.portableShaders.size(); ++i) {
    const auto &instance = f.portableShaders[i];
    const auto &m = instance.module;
    std::cout << "portable_shadow_module slot=" << i + 1
              << " version=" << m.version
              << " alpha_may_vary=" << m.alphaMayVary
              << " ir_alpha=" << instance.ir.alphaMayVary
              << " expressions=" << m.expressions.size()
              << " output=" << m.output << " uniforms=" << m.uniforms.size()
              << " parameters=" << instance.parameters.size()
              << " texture_mask=" << instance.ir.textureMask
              << " coordinate_mask=" << instance.ir.coordinateMask;
    if (!m.expressions.empty()) {
      const auto &e = m.expressions[0];
      std::cout << " first_op=" << uint32_t(e.op)
                << " first_type=" << uint32_t(e.type)
                << " first_value=" << e.value[0] << ',' << e.value[1] << ','
                << e.value[2] << ',' << e.value[3];
    }
    std::cout << std::endl;
  }
  for (size_t i = 0; i < f.draws.size(); ++i) {
    const auto &d = f.draws[i];
    std::cout << "portable_shadow_draw index=" << i
              << " source=" << d.sourceNodeId
              << " source_revision=" << d.sourceRevision
              << " shadow_light_slot=" << d.shadowLightSlot
              << " state_slot=" << d.renderStateSlot
              << " indices=" << d.geometry.indexCount;
    if (d.renderStateSlot < f.renderStates.size()) {
      const auto &state = f.renderStates[d.renderStateSlot];
      std::cout << " module_slot=" << state.portableShaderSlot
                << " group=" << state.shadowGroupSlot
                << " style=" << state.shadowStyle
                << " projection=" << unsigned(state.textureProjection)
                << " material_slot=" << state.materialSlot
                << " lighting_slot=" << state.lightingSlot
                << " transparent_material=" << state.transparentMaterial
                << " transparent_texture=" << state.transparentTexture
                << " raster=" << state.rasterPixels
                << " map_size=" << state.shadowMapSize;
    }
    std::cout << std::endl;
  }
}
void shadowModuleDumpPasses(const CoinRenderShadowPlan &plan) {
  for (size_t i = 0; i < plan.passes.size(); ++i) {
    const auto &p = plan.passes[i];
    std::cout << "portable_shadow_pass index=" << i << " visible=" << p.visible
              << " light_index=" << p.lightSlot << " group=" << p.groupSlot
              << " map_size=" << p.mapSize << " near=" << p.nearDistance
              << " far=" << p.farDistance << " vsm_far=" << p.vsmFarDistance
              << " epsilon=" << p.epsilon << " threshold=" << p.threshold
              << " per_fragment=" << p.perFragmentLighting
              << " caster_count=" << p.casterDraws.size()
              << " receiver_count=" << p.receiverDraws.size() << " casters=";
    for (auto d : p.casterDraws)
      std::cout << d << ',';
    std::cout << " receivers=";
    for (auto d : p.receiverDraws)
      std::cout << d << ',';
    std::cout << " lighting_by_state=";
    for (auto light : p.lightingIndexByState)
      std::cout << light << ',';
    std::cout << std::endl;
  }
}
void shadowModuleDumpFfi(const CoinWgpuFrameView &v, const char *phase) {
  std::cout << "portable_shadow_ffi phase=" << phase << " abi=" << v.abi_version
            << " modules=" << v.portable_module_count
            << " states=" << v.state_count << " map_size=" << v.shadow_map_size
            << " casters=" << v.shadow_caster_count
            << " receivers=" << v.shadow_receiver_count << std::endl;
  for (uint64_t i = 0; i < v.state_count; ++i)
    std::cout << "portable_shadow_ffi_state index=" << i
              << " module_slot=" << v.states[i].portable_shader_slot
              << " projection=" << v.states[i].texture_projection
              << " has_texture=" << v.states[i].has_texture
              << " matrix_q=" << v.states[i].texture_matrix[15] << std::endl;
}
// Reuse the qualified authorial rectangle, custom cube caster, directional
// bbox and epsilon=threshold=0. This test's ordinary Phong/AST expectations
// below are independent of the bump fixture's pow64/texturing oracle.
bool shadowModuleImage(const Frame &f, const bump_shadow::Scene &scene,
                       bool constantOutput, bool casterOutside,
                       Color constantColor, unsigned n = 64,
                       bool opaqueWindow = false, bool report = true) {
  if (f.rgba.size() != size_t(n) * n * 4 ||
      (!opaqueWindow && f.depth.size() != size_t(n) * n))
    return false;
  const double margin = 2.1 * 4 / n;
  unsigned lit = 0, occluded = 0, outside = 0, bad = 0, depthBad = 0;
  int maximum = 0;
  for (unsigned y = 0; y < n; ++y)
    for (unsigned x = 0; x < n; ++x) {
      const double wx = (x + .5) * 4 / n - 2;
      const double wy = 2 - (y + .5) * 4 / n;
      const size_t p = size_t(y) * n + x;
      const bool inside = wx > -1.4 + margin && wx < 1.3 - margin &&
                          wy > -1.3 + margin && wy < 1.35 - margin;
      const bool exterior = wx < -1.4 - margin || wx > 1.3 + margin ||
                            wy < -1.3 - margin || wy > 1.35 + margin;
      if (exterior) {
        ++outside;
        for (unsigned c = 0; c < (opaqueWindow ? 3u : 4u); ++c)
          bad += f.rgba[4 * p + c] > (c == 3 ? 1 : 3);
        if (!opaqueWindow)
          depthBad += f.depth[p] != 1;
      }
      if (!inside)
        continue;
      const bool deep = std::abs(wx) < .18 && std::abs(wy) < .18;
      const bool clear = std::max(std::abs(wx), std::abs(wy)) > .65;
      if (!deep && !clear)
        continue;
      occluded += deep;
      lit += clear;
      const double visibility = deep && !casterOutside ? 0 : 1;
      for (unsigned c = 0; c < 3; ++c) {
        // N=L=+Z; shininess=0 makes pow(dot(N,H),0)=1. Ambient and
        // emission are zero. Specular belongs to PRIMARY before the module,
        // including the same visibility as diffuse, not after the AST.
        const double primary = visibility * (scene.diffuse[c] + scene.spec[c]) *
                               scene.lamp[c] * scene.intensity;
        const double expected =
            255 * (constantOutput ? constantColor[c] : primary);
        const int delta = int(std::abs(f.rgba[4 * p + c] - expected));
        maximum = std::max(maximum, delta);
        if (std::abs(f.rgba[4 * p + c] - expected) > 3 && bad++ < 4 && report)
          std::cerr << "portable_module_shadow_first xy=" << x << ',' << y
                    << " channel=" << c << " V=" << visibility
                    << " expected=" << expected
                    << " actual=" << unsigned(f.rgba[4 * p + c]) << std::endl;
      }
      bad += f.rgba[4 * p + 3] != 255;
      if (!opaqueWindow)
        depthBad += std::abs(f.depth[p] - 3. / 8) > 2e-5;
    }
  if (report)
    std::cout << "portable_module_shadow_image constant=" << constantOutput
              << " caster_outside=" << casterOutside << " lit=" << lit
              << " occluded=" << occluded << " exterior=" << outside
              << " bad=" << bad << " max=" << maximum
              << " depth_bad=" << depthBad << std::endl;
  const bool valid =
      lit > 500 && occluded > 20 && outside > 100 && !bad && !depthBad;
  return report
             ? check(
                   valid,
                   "independent ordinary Phong+V0/V1 and complete module color")
             : valid;
}
// A constant module isolates the single consumer resolve. VSM maps remain
// active and consumed, but cannot change the constant's RGB at the silhouette.
bool shadowModuleCoverage(const Frame &f, unsigned side, unsigned samples,
                          Color color, bool opaqueWindow, bool report = true) {
  if (f.rgba.size() != size_t(side) * side * 4)
    return false;
  unsigned full = 0, empty = 0, partial = 0, odd = 0, bad = 0, depthBad = 0;
  for (size_t p = 0; p < size_t(side) * side; ++p) {
    double best = std::numeric_limits<double>::infinity();
    unsigned selected = samples + 1;
    for (unsigned k = 0; k <= samples; ++k) {
      bool match = true;
      double score = 0;
      for (unsigned c = 0; c < 3; ++c) {
        const double delta = f.rgba[p * 4 + c] - 255. * color[c] * k / samples;
        match &= std::abs(delta) <= 3;
        score += delta * delta;
      }
      if (!opaqueWindow) {
        const double delta = f.rgba[p * 4 + 3] - 255. * k / samples;
        match &= std::abs(delta) <= 1;
        score += delta * delta;
      }
      if (match && score < best) {
        best = score;
        selected = k;
      }
    }
    bad += selected > samples;
    full += selected == samples;
    empty += selected == 0;
    partial += selected > 0 && selected < samples;
    odd += selected < samples && (selected & 1);
    if (!opaqueWindow) {
      if (f.depth.size() != size_t(side) * side)
        return false;
      depthBad += f.depth[p] != 1 && std::abs(f.depth[p] - 3. / 8) > 2e-5;
    }
  }
  if (report)
    std::cout << "portable_module_shadow_coverage extent=" << side
              << " samples=" << samples << " full=" << full
              << " empty=" << empty << " partial=" << partial << " odd=" << odd
              << " bad=" << bad << " depth_bad=" << depthBad << std::endl;
  return full > 100 && empty > 100 && !bad && !depthBad &&
         (samples == 1 || partial > 0) && (samples != 4 || odd > 0);
}
bool shadowModulePresentation(Run &run, const Frame &readback,
                              const bump_shadow::Scene &scene,
                              bool constantOutput, bool casterOutside,
                              Color color) {
  if (!run.window)
    return true;
  const auto begin = std::chrono::steady_clock::now();
  unsigned attempts = 0, maximum = 0;
  do {
    ++attempts;
    Frame shown;
    if (!run.physical(shown) || shown.rgba.size() != readback.rgba.size())
      continue;
    maximum = 0;
    for (size_t i = 0; i < shown.rgba.size(); ++i)
      if (i % 4 < 3)
        maximum = std::max(maximum, unsigned(std::abs(int(shown.rgba[i]) -
                                                      int(readback.rgba[i]))));
    if (maximum <= 3 &&
        shadowModuleImage(shown, scene, constantOutput, casterOutside, color,
                          run.extent, true, false) &&
        (!constantOutput || shadowModuleCoverage(shown, run.extent, run.samples,
                                                 color, true, false)) &&
        std::chrono::steady_clock::now() - begin <= std::chrono::seconds(5)) {
      run.lastPhysical = std::move(shown);
      std::cout << "portable_module_shadow_physical attempts=" << attempts
                << " max_delta=" << maximum << " present_ms="
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - begin)
                       .count()
                << std::endl;
      return true;
    }
  } while (std::chrono::steady_clock::now() - begin < std::chrono::seconds(5));
  std::cerr << "portable_module_shadow_physical timeout attempts=" << attempts
            << " max_delta=" << maximum << std::endl;
  return false;
}
bool shadowModuleDelivery(const CoinRenderFramePlan &frame,
                          const bump_shadow::Scene &scene, SoCube *cube,
                          bool adapted, Op op, Color constantColor,
                          unsigned extent = 64) {
  shadowModuleDumpFrame(frame, "shadowModuleDelivery", scene.mesh->getNodeId(),
                        cube->getNodeId());
  CoinRenderShadowPlan shadows;
  std::string error;
  const bool planned = coin_render_plan_shadows(frame, shadows, error);
  shadowModuleDumpPasses(shadows);
  bool ok = check(
      planned && shadows.passes.size() == 1 && frame.shadowLights.size() == 1,
      "actual captured frame retains one eligible map/light: " + error);
  if (!planned || shadows.passes.size() != 1 || frame.shadowLights.size() != 1)
    return false;
  const auto &pass = shadows.passes[0];
  const auto &light = frame.shadowLights[0];
  ok &=
      check(pass.visible && pass.mapSize == 256 && pass.lightSlot == 0 &&
                pass.groupSlot == 1 && pass.epsilon == 0 &&
                pass.threshold == 0 && light.enabled && light.shadowEligible &&
                light.hasCustomScene && light.mapSceneCaptured &&
                light.sourceRevision == scene.directional->getNodeId() &&
                light.customSceneNodeId ==
                    scene.directional->shadowMapScene.getValue()->getNodeId(),
            "authored VSM light, custom map and visibility policy retained");
  std::vector<uint32_t> receiverDraws;
  for (uint32_t i = 0; i < frame.draws.size(); ++i)
    if (frame.draws[i].sourceNodeId == scene.mesh->getNodeId() &&
        !frame.draws[i].shadowLightSlot)
      receiverDraws.push_back(i);
  const std::set<uint32_t> receivers(receiverDraws.begin(),
                                     receiverDraws.end());
  const std::set<uint32_t> plannedReceivers(pass.receiverDraws.begin(),
                                            pass.receiverDraws.end());
  ok &= check(!receiverDraws.empty() && receivers == plannedReceivers &&
                  plannedReceivers.size() == pass.receiverDraws.size() &&
                  portableCoreQuad(frame, receiverDraws, scene.positions),
              "all receiver draws cover exactly the two disjoint authored "
              "triangles and VSM associations");
  Module expected;
  expected.expressions.push_back(
      op == Op::CONSTANT ? constant(constantColor)
                         : operation(Op::PRIMARY_COLOR, Type::FLOAT4));
  if (op == Op::CONSTANT)
    expected.alphaMayVary = FALSE;
  std::set<uint32_t> used;
  for (auto index : receiverDraws) {
    const auto &draw = frame.draws[index];
    if (!check(draw.renderStateSlot < frame.renderStates.size(),
               "receiver state exists"))
      return false;
    const auto &state = frame.renderStates[draw.renderStateSlot];
    ok &= check(state.shadowGroupSlot == 1 && (state.shadowStyle & 2) &&
                    state.depthTest && state.depthWrite &&
                    state.depthRange[0] == 0 && state.depthRange[1] == 1 &&
                    (state.depthFunction == CoinRenderDepthFunction::LESS ||
                     state.depthFunction == CoinRenderDepthFunction::LEQUAL),
                "every receiver occurrence retains shadow and depth ownership");
    if (adapted) {
      ok &=
          check(portableCoreBinding(frame, draw.renderStateSlot, expected, {}),
                "every receiver resolves its own valid slot to exact authored "
                "AST/signature/parameters");
      used.insert(state.portableShaderSlot);
    } else
      ok &= check(state.portableShaderSlot == 0,
                  "ordinary predecessor remains fixed texturing");
    if (!check(state.materialSlot < frame.materials.size(),
               "receiver material exists"))
      return false;
    const auto &material = frame.materials[state.materialSlot];
    ok &= check(material.shininess == 0 && material.transparency == 0,
                "every receiver retains opaque zero-exponent material");
    for (unsigned c = 0; c < 3; ++c)
      ok &= check(
          std::abs(material.diffuse[c] - scene.diffuse[c]) < 1e-6 &&
              std::abs(material.specular[c] - scene.spec[c]) < 1e-6 &&
              material.ambient[c] == 0 && material.emission[c] == 0,
          "nonzero diffuse/specular retained on all triangle occurrences");
  }
  ok &= check(used.size() == frame.portableShaders.size(),
              "captured module payloads are exactly the consumed slots");
  SbVec3f lower(1e20f, 1e20f, 1e20f), upper(-1e20f, -1e20f, -1e20f);
  std::set<uint32_t> casterIndices;
  ok &= check(!pass.casterDraws.empty(), "moment caster remains present");
  for (auto index : pass.casterDraws) {
    if (!check(index < frame.draws.size(), "caster draw exists"))
      return false;
    const auto &draw = frame.draws[index];
    if (!check(draw.renderStateSlot < frame.renderStates.size(),
               "caster state exists"))
      return false;
    const auto &state = frame.renderStates[draw.renderStateSlot];
    ok &= check(draw.sourceNodeId == cube->getNodeId() &&
                    draw.shadowLightSlot == 1 && !state.portableShaderSlot &&
                    draw.geometry.indexCount % 3 == 0,
                "every original cube occurrence remains an unadapted "
                "moments/depth caster");
    const auto &g = draw.geometry;
    if (!check(uint64_t(g.firstIndex) + g.indexCount <= frame.indices.size(),
               "caster index range exists"))
      return false;
    for (unsigned i = 0; i < g.indexCount; ++i) {
      const auto at = g.firstIndex + i;
      ok &=
          check(casterIndices.insert(at).second, "caster ranges are disjoint");
      if (!check(frame.indices[at] < frame.vertices.size(),
                 "caster vertex exists"))
        return false;
      const auto &v = frame.vertices[frame.indices[at]];
      SbVec3f world;
      state.model.multVecMatrix(
          SbVec3f(v.position[0], v.position[1], v.position[2]), world);
      for (unsigned c = 0; c < 3; ++c) {
        lower[c] = std::min(lower[c], world[c]);
        upper[c] = std::max(upper[c], world[c]);
      }
    }
  }
  ok &= check(casterIndices.size() == 36,
              "moment caster retains all 36 original indices");
  const float x = scene.caster->translation.getValue()[0];
  const float lo[] = {x - .35f, -.35f, .5f}, hi[] = {x + .35f, .35f, 1};
  for (unsigned c = 0; c < 3; ++c)
    ok &= check(std::abs(lower[c] - lo[c]) < 1e-5 &&
                    std::abs(upper[c] - hi[c]) < 1e-5,
                "caster retains complete authorial 3D bounds and depth extent");
  CoinWgpuFfiFrame ffi;
  const bool packed = ffi.prepare(frame, extent, extent, error, false);
  ok &= check(packed, "module and VSM coexist in FFI: " + error);
  if (!packed)
    return false;
  const auto &v = ffi.getView();
  shadowModuleDumpFfi(v, "shadowModuleDelivery");
  ok &= check(v.abi_version == COIN_WGPU_ABI_VERSION &&
                  v.texture_sampling_policy ==
                      uint32_t(frame.textureSamplingPolicy) &&
                  v.shadow_map_size == 256 && v.shadow_caster_count > 0,
              "ABI67 retains real moment resources and sampling policy");
  unsigned casterCount = 0;
  for (uint64_t i = 0; i < v.shadow_caster_count; ++i) {
    casterCount += v.shadow_casters[i].index_count;
    const auto state = v.shadow_casters[i].render_state_slot;
    ok &= check(state < v.state_count && !v.states[state].portable_shader_slot,
                "packed moment caster never executes the receiver module");
  }
  ok &= check(casterCount == 36,
              "FFI retains complete original cube caster geometry");
  std::vector<uint32_t> packedReceivers;
  std::set<uint32_t> packedSlots;
  for (uint32_t i = 0; i < v.draw_count; ++i) {
    const auto state = v.draws[i].render_state_slot;
    if (state >= v.state_count) {
      ok &= check(false, "FFI draw state exists");
      continue;
    }
    if (!v.states[state].portable_shader_slot)
      continue;
    packedReceivers.push_back(i);
    packedSlots.insert(v.states[state].portable_shader_slot);
    ok &= check(portableFfiBinding(v, state, expected, {}) &&
                    state < v.shadow_receiver_count &&
                    (v.shadow_receivers[state].receives & 1u) &&
                    v.shadow_receivers[state].lighting_index == 0,
                "every packed receiver draw retains exact module and VSM light "
                "association");
  }
  if (adapted) {
    std::array<std::array<double, 2>, 4> corners;
    for (unsigned i = 0; i < 4; ++i)
      corners[i] = {{scene.positions->point[i][0] / 2.,
                     scene.positions->point[i][1] / 2.}};
    ok &= check(portableFfiQuad(v, packedReceivers, corners) &&
                    packedSlots.size() == v.portable_module_count,
                "all packed module draws cover the complete quad and consume "
                "exactly their module payloads");
  } else
    ok &= check(v.portable_module_count == 0 && packedReceivers.empty(),
                "fixed predecessor has no module payload");
  return ok;
}

bool shadows(Run &run) {
  bump_shadow::Scene scene(1, true);
  scene.bumpStage->whichChild = SO_SWITCH_NONE;
  scene.paint(false);
  scene.shine = 0;
  scene.setSpec({{.24, .12, .3}});
  auto *map =
      static_cast<SoSeparator *>(scene.directional->shadowMapScene.getValue());
  SoCube *cube = nullptr;
  for (int i = 0; i < map->getNumChildren(); ++i)
    if (map->getChild(i)->isOfType(SoCube::getClassTypeId()))
      cube = static_cast<SoCube *>(map->getChild(i));
  if (!check(cube != nullptr, "qualified custom cube caster exists"))
    return false;
  const Color color = {{.2f, .55f, .75f, 1}};
  Frame plain, constant, primary, moved, back;
  auto draw = [&](Frame &frame, bool adapted, Op op, bool constantOutput,
                  bool casterOutside, const char *label) {
    const auto sourceId = scene.root->getNodeId();
    const auto casterId = cube->getNodeId();
    const bool rendered = run.render(frame, label, scene.root);
    return rendered &&
           check(scene.root->getNodeId() == sourceId &&
                     cube->getNodeId() == casterId,
                 "shadow apply does not mutate authored source/caster") &&
           check(run.audit->plan.textureSamplingPolicy ==
                     run.target->getOptions().textureSamplingPolicy,
                 "actual captured sampling policy matches immutable target") &&
           shadowModuleDelivery(run.audit->plan, scene, cube, adapted, op,
                                color, run.extent) &&
           shadowModuleImage(frame, scene, constantOutput, casterOutside, color,
                             run.extent, run.window) &&
           (!constantOutput ||
            check(shadowModuleCoverage(frame, run.extent, run.samples, color,
                                       run.window),
                  "constant receiver retains actual single-resolve sample "
                  "coverage")) &&
           check(shadowModulePresentation(run, frame, scene, constantOutput,
                                          casterOutside, color),
                 "current physical VSM/module image and same-apply readback "
                 "within five seconds");
  };
  bool ok = draw(plain, false, Op::PRIMARY_COLOR, false, false,
                 "ordinary Phong/specular shadow predecessor");
  scene.group->insertChild(run.scene.program,
                           scene.group->findChild(scene.positions));
  Module fixed = literal(color);
  fixed.alphaMayVary = FALSE;
  ok &= run.bind(fixed);
  ok &= draw(constant, true, Op::CONSTANT, true, false,
             "constant receiver stays constant in lit and occluded interiors");
  Module readPrimary;
  readPrimary.expressions.push_back(operation(Op::PRIMARY_COLOR, Type::FLOAT4));
  ok &= run.bind(readPrimary);
  ok &= draw(primary, true, Op::PRIMARY_COLOR, false, false,
             "PRIMARY includes VSM attenuated diffuse and nonzero specular");
  scene.caster->translation.setValue(3, 0, .75f);
  ok &=
      draw(moved, true, Op::PRIMARY_COLOR, false, true,
           "moving only caster changes PRIMARY shadow into full illumination");
  scene.caster->translation.setValue(0, 0, .75f);
  ok &= draw(back, true, Op::PRIMARY_COLOR, false, false,
             "caster A/B/A restores current PRIMARY and original depth");
  ok &= run.bind(fixed);
  ok &= draw(
      back, true, Op::CONSTANT, true, false,
      "constant A/B/A remains independent of full Phong shadow contribution");
  const Frame stable = back, physical = run.lastPhysical;
  auto *depth = new SoDepthBuffer;
  depth->test = TRUE;
  depth->write = TRUE;
  depth->function = SoDepthBuffer::LEQUAL;
  depth->range.setValue(std::numeric_limits<float>::quiet_NaN(), 1);
  scene.group->insertChild(depth, scene.group->findChild(scene.positions));
  if (run.window)
    ok &= check(run.target->requestWindowReadbackRGBA(),
                "readback request also exercises invalid shadow-module apply");
  ok &= run.refuse(stable,
                   "invalid shadow/module depth range retains old publication",
                   scene.root);
  ok &= untouched(run, stable, physical,
                  "refusal preserves complete old VSM/module publication");
  depth->range.setValue(0, 1);
  ok &= draw(
      back, true, Op::CONSTANT, true, false,
      "same-target shadow/module recovery has new delivery and current image");
  if (run.window) {
    for (unsigned extent : {96u, 64u}) {
      ok &= check(run.resize(extent),
                  "public shadow-module window extent mutation");
      ok &= draw(back, true, Op::CONSTANT, true, false,
                 "one current shadow-module publication after physical resize");
    }
  }
  return ok;
}
#endif
