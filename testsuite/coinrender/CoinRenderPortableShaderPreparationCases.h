#ifndef COIN_PORTABLE_SHADER_PREPARATION_CASES_H
#define COIN_PORTABLE_SHADER_PREPARATION_CASES_H
#include "CoinRenderPortableShaderDelivery.h"
#include "CoinRenderPortableShaderPresentationCases.h"
#include "CoinRenderPortableShaderShadowCases.h"
Module preparationModule(bool primary) {
  Module module;
  declare(module, "prepared rgba", Type::FLOAT4);
  module.expressions.push_back(uniform("prepared rgba", Type::FLOAT4));
  if (primary) {
    module.expressions.push_back(operation(Op::PRIMARY_COLOR, Type::FLOAT4));
    module.expressions.push_back(operation(Op::MULTIPLY, Type::FLOAT4, 1, 0));
    module.expressions.push_back(constant({{0, 0, 0, 1}}));
    module.expressions.push_back(operation(Op::ADD, Type::FLOAT4, 2, 3));
    module.output = 4;
  }
  module.alphaMayVary = TRUE;
  return module;
}
bool preparationFirst(Run &run, Frame &frame, SoNode *root,
                      const char *profile) {
  bool ok =
      check(run.target->getLastSubmissionSerial() == 0 && !run.audit->entries,
            "first scene apply uses a genuinely unsubmitted target");
  std::cout << "portable_preparation_first_begin profile=" << profile
            << " samples=" << run.samples
            << " serial=" << run.target->getLastSubmissionSerial()
            << " entries=" << run.audit->entries << std::endl;
  const bool rendered =
      run.render(frame, "cold first portable preparation", root);
  std::cout << "portable_preparation_first_done profile=" << profile
            << " samples=" << run.samples
            << " status=" << run.action.getLastStatus()
            << " serial=" << frame.serial << " entries=" << run.audit->entries
            << std::endl;
  return rendered && ok;
}
// Narrow witness for the authored one shadow receiver or two shared-quad
// occurrences below. Every emitted triangle retains its own real association.
bool preparationDelivery(Run &run, SoIndexedFaceSet *mesh,
                         SoCoordinate3 *positions, const bool (&active)[2],
                         bool two, const Module &module, Color value,
                         bool expectedBump = false) {
  const auto &f = run.audit->plan;
  std::vector<uint32_t> draws[2];
  const std::vector<float> parameters =
      module.uniforms.empty() ? std::vector<float>()
                              : std::vector<float>(value.begin(), value.end());
  bool ok = check(f.textureSamplingPolicy ==
                      run.target->getOptions().textureSamplingPolicy,
                  "current capture retains the requested sampling policy");
  for (uint32_t i = 0; i < f.draws.size(); ++i) {
    const auto &draw = f.draws[i];
    if (draw.shadowLightSlot || !draw.geometry.indexCount)
      continue;
    ok &=
        check(draw.sourceNodeId == mesh->getNodeId(),
              "every executed main surface is an authored receiver occurrence");
    if (draw.sourceNodeId != mesh->getNodeId())
      continue;
    if (draw.renderStateSlot >= f.renderStates.size())
      return false;
    const auto &state = f.renderStates[draw.renderStateSlot];
    const unsigned lane = two && state.model[3][0] > 0 ? 1 : 0;
    draws[lane].push_back(i);
    ok &= check(active[lane] ? portableCoreBinding(f, draw.renderStateSlot,
                                                   module, parameters)
                             : !state.portableShaderSlot,
                "module and fixed occurrences have their exact independent "
                "Core bindings");
    if (state.viewportSlot >= f.viewports.size())
      return false;
    const auto &viewport = f.viewports[state.viewportSlot];
    ok &=
        check(viewport.x == 0 && viewport.y == 0 &&
                  viewport.width == run.extent && viewport.height == run.extent,
              "actual occurrence retains the full current target viewport");
  }
  for (unsigned lane = 0; lane < (two ? 2u : 1u); ++lane)
    ok &= check(
        portableCoreQuad(f, draws[lane], positions),
        "shared source occurrence covers the complete disjoint authored quad");
  const unsigned instances = active[0] || (two && active[1]) ? 1 : 0;
  ok &= check(f.portableShaders.size() == instances,
              "only consumed semantic module instances are captured");
  CoinWgpuFfiFrame ffi;
  std::string error;
  const bool prepared =
      expectedBump ? ffi.prepareBumpFrame(f, run.extent, run.extent, error)
                   : ffi.prepare(f, run.extent, run.extent, error, false);
  ok &= check(prepared,
              "real preparation delivery packs transactionally: " + error);
  if (!prepared)
    return false;
  const auto &v = ffi.getView();
  std::vector<uint32_t> packed[2];
  for (uint32_t i = 0; i < v.draw_count; ++i) {
    const auto &draw = v.draws[i];
    if (draw.render_state_slot >= v.state_count || !draw.index_count ||
        uint64_t(draw.first_index) + draw.index_count > v.index_count)
      return false;
    const auto xy =
        portableClip(v, draw.render_state_slot, v.indices[draw.first_index]);
    const unsigned lane = two && xy[0] > 0 ? 1 : 0;
    packed[lane].push_back(i);
    ok &= check(
        active[lane]
            ? portableFfiBinding(v, draw.render_state_slot, module, parameters)
            : !v.states[draw.render_state_slot].portable_shader_slot,
        "every actual FFI triangle retains module/fixed ownership and "
        "parameter bits");
  }
  for (unsigned lane = 0; lane < (two ? 2u : 1u); ++lane) {
    std::array<std::array<double, 2>, 4> corners;
    for (unsigned p = 0; p < 4; ++p)
      corners[p] = {
          {two ? (lane ? .48 : -.48) + .5 * positions->point[p][0]
               : positions->point[p][0] / 2.,
           two ? .5 * positions->point[p][1] : positions->point[p][1] / 2.}};
    ok &= check(portableFfiQuad(v, packed[lane], corners),
                "FFI geometry coverage and occurrence transform remain exact");
  }
  return check(v.abi_version == 63 && v.portable_module_count == instances,
               "ABI63 transports only the actual consumed module payloads") &&
         ok;
}
bool preparationBasicImage(const Frame &frame, unsigned side, unsigned samples,
                           const std::array<Color, 2> &colors, bool window,
                           bool report = true) {
  if (frame.rgba.size() != size_t(side) * side * 4 ||
      (!window && frame.depth.size() != size_t(side) * side))
    return false;
  const double margin = 2.1 * 2 / side;
  unsigned inside[2] = {0, 0}, outside = 0, full = 0, empty = 0, partial = 0,
           odd = 0;
  unsigned bad = 0, depthBad = 0;
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      const size_t pixel = size_t(y) * side + x;
      const double wx = (x + .5) * 2 / side - 1, wy = 1 - (y + .5) * 2 / side;
      bool exterior = true;
      for (unsigned lane = 0; lane < 2; ++lane) {
        const double lx = (wx - (lane ? .48 : -.48)) / .5, ly = wy / .5;
        const bool inner =
            lx > -.75 + margin / .5 && lx < .765625 - margin / .5 &&
            ly > -.7078125 + margin / .5 && ly < .75 - margin / .5;
        exterior &= lx < -.75 - margin / .5 || lx > .765625 + margin / .5 ||
                    ly < -.7078125 - margin / .5 || ly > .75 + margin / .5;
        if (inner) {
          ++inside[lane];
          for (unsigned c = 0; c < 3; ++c)
            bad += std::abs(frame.rgba[4 * pixel + c] -
                            255. * colors[lane][c]) > 3;
          bad += frame.rgba[4 * pixel + 3] != 255;
          if (!window) {
            depthBad += std::abs(frame.depth[pixel] - 2.9 / 9.9) > 2e-5;
          }
        }
      }
      if (exterior) {
        ++outside;
        for (unsigned c = 0; c < 3; ++c)
          bad += frame.rgba[4 * pixel + c] > 3;
        if (!window) {
          bad += frame.rgba[4 * pixel + 3] != 0;
          depthBad += frame.depth[pixel] != 1;
        }
      }
      // Each separated rectangle resolves once against black. No cross-GPU
      // sample locations are assumed; only the actual admissible k/N levels.
      const auto &color = colors[wx > 0 ? 1 : 0];
      double best = std::numeric_limits<double>::infinity();
      unsigned chosen = samples + 1;
      for (unsigned k = 0; k <= samples; ++k) {
        bool valid = true;
        double error = 0;
        for (unsigned c = 0; c < 3; ++c) {
          const double d =
              frame.rgba[4 * pixel + c] - 255. * color[c] * k / samples;
          valid &= std::abs(d) <= 3;
          error += d * d;
        }
        if (!window) {
          const double d = frame.rgba[4 * pixel + 3] - 255. * k / samples;
          valid &= std::abs(d) <= 1;
          error += d * d;
        }
        if (valid && error < best) {
          best = error;
          chosen = k;
        }
      }
      bad += chosen > samples;
      full += chosen == samples;
      empty += chosen == 0;
      partial += chosen > 0 && chosen < samples;
      odd += chosen < samples && (chosen & 1);
      if (!window)
        depthBad += frame.depth[pixel] != 1 &&
                    std::abs(frame.depth[pixel] - 2.9 / 9.9) > 2e-5;
    }
  if (report)
    std::cout << "portable_preparation_image inside=" << inside[0] << ','
              << inside[1] << " outside=" << outside << " partial=" << partial
              << " odd=" << odd << " bad=" << bad << " depth_bad=" << depthBad
              << std::endl;
  return inside[0] > 100 && inside[1] > 100 && outside > 500 && full > 100 &&
         empty > 500 && !bad && !depthBad && (samples == 1 || partial > 0) &&
         (samples != 4 || odd > 0);
}
bool preparationPhysical(Run &run, const Frame &readback,
                         const std::function<bool(const Frame &)> &oracle) {
  if (!run.window)
    return true;
  const auto start = std::chrono::steady_clock::now();
  unsigned attempts = 0, maximum = 0;
  do {
    Frame shown;
    ++attempts;
    if (!run.physical(shown) || shown.rgba.size() != readback.rgba.size())
      continue;
    maximum = 0;
    for (size_t i = 0; i < shown.rgba.size(); ++i)
      if (i % 4 < 3)
        maximum = std::max(maximum, unsigned(std::abs(int(shown.rgba[i]) -
                                                      int(readback.rgba[i]))));
    if (maximum <= 3 && oracle(shown) &&
        std::chrono::steady_clock::now() - start <= std::chrono::seconds(5)) {
      run.lastPhysical = std::move(shown);
      std::cout << "portable_preparation_present attempts=" << attempts
                << " max_delta=" << maximum << " present_ms="
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count()
                << std::endl;
      return true;
    }
  } while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
  std::cerr << "portable_preparation_present timeout attempts=" << attempts
            << " max_delta=" << maximum << std::endl;
  return false;
}
bool preparationBasic(Run &run, const std::string &profile) {
  auto &scene = run.scene;
  presentationGeometry(scene);
  auto *geometry = new SoGroup;
  geometry->ref();
  geometry->addChild(scene.positions);
  geometry->addChild(scene.mesh);
  scene.root->removeChild(scene.positions);
  scene.root->removeChild(scene.mesh);
  const Color material[2] = {Color{{.2f, .6f, .4f, 1}},
                             Color{{.8f, .3f, .5f, 1}}};
  SoSwitch *programs[2];
  for (unsigned lane = 0; lane < 2; ++lane) {
    auto *branch = new SoSeparator;
    auto *translation = new SoTranslation;
    translation->translation.setValue(lane ? .48f : -.48f, 0, 0);
    auto *scale = new SoScale;
    scale->scaleFactor.setValue(.5f, .5f, .5f);
    auto *mat = new SoMaterial;
    mat->diffuseColor.setValue(material[lane][0], material[lane][1],
                               material[lane][2]);
    auto *program = new SoSwitch;
    program->addChild(scene.program);
    programs[lane] = program;
    branch->addChild(translation);
    branch->addChild(scale);
    branch->addChild(mat);
    branch->addChild(program);
    branch->addChild(geometry);
    scene.root->addChild(branch);
  }
  geometry->unref();
  bool primary = profile == "primary";
  Color value =
      primary ? Color{{.7f, .6f, .8f, 0}} : Color{{.13f, .57f, .29f, 1}};
  const bool literalFirst = profile == "module";
  SoShaderParameter4f *parameter =
      literalFirst ? nullptr : scene.parameter4("prepared rgba", value);
  Module module = literalFirst ? literal(value) : preparationModule(primary);
  if (literalFirst)
    module.alphaMayVary = FALSE;
  bool ok = run.bind(module), first = true;
  bool active[2] = {profile != "ordinary",
                    profile != "ordinary" && profile != "mixed"};
  Frame stable;
  auto draw = [&](const char *label, bool cached) {
    std::array<Color, 2> colors;
    for (unsigned lane = 0; lane < 2; ++lane) {
      programs[lane]->whichChild =
          active[lane] ? SO_SWITCH_ALL : SO_SWITCH_NONE;
      colors[lane] = material[lane];
      if (active[lane]) {
        colors[lane] = value;
        if (primary)
          for (unsigned c = 0; c < 3; ++c)
            colors[lane][c] *= material[lane][c];
        colors[lane][3] = 1;
      }
    }
    Frame frame;
    const bool rendered =
        first ? preparationFirst(run, frame, scene.root, profile.c_str())
              : run.render(frame, label);
    first = false;
    bool current = rendered;
    if (rendered) {
      current &= preparationDelivery(run, scene.mesh, scene.positions, active,
                                     true, module, value);
      current &=
          check(preparationBasicImage(frame, run.extent, run.samples, colors,
                                      run.window),
                "independent cold/mixed RGB, depth and actual sample coverage");
      current &=
          check(preparationPhysical(run, frame,
                                    [&](const Frame &physical) {
                                      return preparationBasicImage(
                                          physical, run.extent, run.samples,
                                          colors, true, false);
                                    }),
                "first/current whole physical image and readback agree within "
                "five seconds");
    }
    if (cached)
      current &=
          check(run.lastCompiles.empty(),
                "same module and parameter edits reuse compiled variants");
    if (current)
      stable = std::move(frame);
    ok &= current;
  };
  draw("cold authorial first profile", false);
  if (literalFirst) {
    parameter = scene.parameter4("prepared rgba", value);
    module = preparationModule(false);
    ok &= run.bind(module);
  }
  active[0] = active[1] = true;
  draw("all module occurrences preserve shared geometry",
       !literalFirst && profile != "ordinary");
  if (literalFirst || profile == "ordinary")
    ok &= check(
        !run.lastCompiles.empty(),
        "first consumed UNIFORM AST/signature compiles its own real variant");
  value = primary ? Color{{.35f, .8f, .45f, 0}} : Color{{.71f, .19f, .53f, 1}};
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  draw("uniform-only visible mutation", true);
  value = primary ? Color{{.7f, .6f, .8f, 0}} : Color{{.13f, .57f, .29f, 1}};
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  draw("parameter A/B/A on same target", true);
  active[1] = false;
  draw("module and ordinary shared-shape occurrences coexist", false);
  active[0] = false;
  draw("ordinary-only transition retains both material contexts", false);
  active[0] = active[1] = true;
  draw("return from ordinary to actual module", true);
  primary = !primary;
  module = preparationModule(primary);
  value = primary ? Color{{.7f, .6f, .8f, 0}} : Color{{.13f, .57f, .29f, 1}};
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  ok &= run.bind(module);
  draw("constant versus parameterized PRIMARY replaces only surface color",
       false);
  value = primary ? Color{{.35f, .8f, .45f, 0}} : Color{{.71f, .19f, .53f, 1}};
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  draw("second AST parameter-only edit", true);
  const Frame before = stable, physical = run.lastPhysical;
  auto invalid = parameter->value.getValue();
  invalid[1] = std::numeric_limits<float>::quiet_NaN();
  parameter->value = invalid;
  ok &= run.refuse(
      before, "invalid cold-profile binding preserves current publication");
  ok &= untouched(
      run, before, physical,
      "cold-profile refusal retains exact buffers/serial/physical image");
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  draw("valid recovery has current delivery without recompilation", true);
  if (run.window)
    for (unsigned side : {96u, 64u}) {
      ok &= check(run.resize(side), "public preparation window resize");
      draw("one current apply after extent mutation", true);
    }
  return ok;
}
bool preparationShadowImage(const Frame &frame, const bump_shadow::Scene &scene,
                            bool active, bool primary, Color value,
                            bool outsideCaster, unsigned side, bool window,
                            bool report = true) {
  if (frame.rgba.size() != size_t(side) * side * 4 ||
      (!window && frame.depth.size() != size_t(side) * side))
    return false;
  const double margin = 2.1 * 4 / side;
  unsigned lit = 0, dark = 0, exterior = 0, bad = 0, depthBad = 0;
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      const double wx = (x + .5) * 4 / side - 2, wy = 2 - (y + .5) * 4 / side;
      const size_t p = size_t(y) * side + x;
      const bool inside = wx > -1.4 + margin && wx < 1.3 - margin &&
                          wy > -1.3 + margin && wy < 1.35 - margin;
      const bool out = wx < -1.4 - margin || wx > 1.3 + margin ||
                       wy < -1.3 - margin || wy > 1.35 + margin;
      if (out) {
        ++exterior;
        for (unsigned c = 0; c < 3; ++c)
          bad += frame.rgba[4 * p + c] > 3;
        if (!window) {
          bad += frame.rgba[4 * p + 3] != 0;
          depthBad += frame.depth[p] != 1;
        }
      }
      const bool deep = std::abs(wx) < .18 && std::abs(wy) < .18;
      const bool clear = std::max(std::abs(wx), std::abs(wy)) > .65;
      if (!inside || (!deep && !clear))
        continue;
      lit += clear;
      dark += deep;
      const double visibility = deep && !outsideCaster ? 0 : 1;
      for (unsigned c = 0; c < 3; ++c) {
        // The qualified authorial plane has N=L=+Z, shininess=0 and no
        // ambient/emission. PRIMARY includes VSM and nonzero Phong specular.
        double expected = visibility * (scene.diffuse[c] + scene.spec[c]) *
                          scene.lamp[c] * scene.intensity;
        if (active)
          expected = primary ? expected * value[c] : value[c];
        bad += std::abs(frame.rgba[4 * p + c] - 255 * expected) > 3;
      }
      bad += frame.rgba[4 * p + 3] != 255;
      if (!window) {
        depthBad += std::abs(frame.depth[p] - 3. / 8) > 2e-5;
      }
    }
  if (report)
    std::cout << "portable_preparation_shadow lit=" << lit << " deep=" << dark
              << " exterior=" << exterior << " active=" << active
              << " primary=" << primary << " bad=" << bad
              << " depth_bad=" << depthBad << std::endl;
  return lit > 500 && dark > 8 && exterior > 500 && !bad && !depthBad;
}
bool preparationShadowDelivery(Run &run, const bump_shadow::Scene &scene,
                               SoCube *cube, bool active, const Module &module,
                               Color value) {
  const bool adapted[2] = {active, false};
  bool ok = preparationDelivery(run, scene.mesh, scene.positions, adapted,
                                false, module, value);
  const auto &frame = run.audit->plan;
  CoinRenderShadowPlan plan;
  std::string error;
  const bool planned = coin_render_plan_shadows(frame, plan, error);
  ok &= check(planned && plan.passes.size() == 1 &&
                  frame.shadowLights.size() == 1,
              "one actual authorial map/light is delivered: " + error);
  if (!planned || plan.passes.size() != 1 || frame.shadowLights.size() != 1)
    return false;
  const auto &pass = plan.passes[0];
  const auto &light = frame.shadowLights[0];
  ok &= check(
      pass.visible && pass.mapSize == 256 && pass.lightSlot == 0 &&
          pass.groupSlot == 1 && pass.epsilon == 0 && pass.threshold == 0 &&
          light.enabled && light.shadowEligible && light.hasCustomScene &&
          light.mapSceneCaptured &&
          light.sourceRevision == scene.directional->getNodeId(),
      "real VSM visibility, policy, identity and custom map remain intact");
  std::set<uint32_t> receivers;
  for (uint32_t i = 0; i < frame.draws.size(); ++i)
    if (!frame.draws[i].shadowLightSlot &&
        frame.draws[i].sourceNodeId == scene.mesh->getNodeId())
      receivers.insert(i);
  ok &= check(receivers == std::set<uint32_t>(pass.receiverDraws.begin(),
                                              pass.receiverDraws.end()) &&
                  !receivers.empty(),
              "all receiver triangles retain actual map association");
  unsigned casterIndices = 0;
  for (auto index : pass.casterDraws) {
    if (index >= frame.draws.size())
      return false;
    const auto &draw = frame.draws[index];
    casterIndices += draw.geometry.indexCount;
    if (draw.renderStateSlot >= frame.renderStates.size())
      return false;
    ok &= check(
        draw.sourceNodeId == cube->getNodeId() && draw.shadowLightSlot == 1 &&
            !frame.renderStates[draw.renderStateSlot].portableShaderSlot,
        "the module cannot replace caster moments or geometry");
  }
  ok &= check(casterIndices == 36,
              "all 36 original cube caster indices remain consumed");
  CoinWgpuFfiFrame ffi;
  const bool packed = ffi.prepare(frame, run.extent, run.extent, error, false);
  ok &=
      check(packed, "actual separate moment-caster transport packs: " + error);
  if (!packed)
    return false;
  const auto &view = ffi.getView();
  unsigned packedCasterIndices = 0;
  for (uint64_t i = 0; i < view.shadow_caster_count; ++i) {
    const auto &caster = view.shadow_casters[i];
    packedCasterIndices += caster.index_count;
    ok &= check(caster.render_state_slot < view.state_count &&
                    !view.states[caster.render_state_slot].portable_shader_slot,
                "FFI moments retain their separate non-module caster state");
  }
  return check(view.shadow_map_size == 256 && packedCasterIndices == 36,
               "real FFI moment resources survive without a fixed receiver "
               "executor") &&
         ok;
}
bool preparationShadow(Run &run) {
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
  if (!check(cube != nullptr, "qualified custom caster exists"))
    return false;
  auto *program = new SoSwitch;
  program->addChild(run.scene.program);
  scene.group->insertChild(program, scene.group->findChild(scene.positions));
  Color value = {{.2f, .55f, .75f, 1}};
  SoShaderParameter4f *parameter = nullptr;
  bool active = true, primary = false, outsideCaster = false, first = true;
  Module module = literal(value);
  module.alphaMayVary = FALSE;
  bool ok = run.bind(module);
  Frame stable;
  auto draw = [&](const char *label, bool cached) {
    program->whichChild = active ? SO_SWITCH_ALL : SO_SWITCH_NONE;
    Frame frame;
    const bool rendered =
        first ? preparationFirst(run, frame, scene.root, "shadow")
              : run.render(frame, label, scene.root);
    first = false;
    bool current = rendered;
    if (rendered) {
      current &=
          preparationShadowDelivery(run, scene, cube, active, module, value);
      current &=
          check(preparationShadowImage(frame, scene, active, primary, value,
                                       outsideCaster, run.extent, run.window),
                "independent VSM/Phong/AST/depth oracle");
      if (active && !primary)
        current &=
            check(shadowModuleCoverage(frame, run.extent, run.samples, value,
                                       run.window),
                  "real MSAA coverage survives module-only shadow preparation");
      current &= check(
          preparationPhysical(
              run, frame,
              [&](const Frame &physical) {
                return preparationShadowImage(physical, scene, active, primary,
                                              value, outsideCaster, run.extent,
                                              true, false) &&
                       (!(active && !primary) ||
                        shadowModuleCoverage(physical, run.extent, run.samples,
                                             value, true, false));
              }),
          "first/current physical shadow/module image matches same-apply "
          "readback");
    }
    if (cached)
      current &= check(run.lastCompiles.empty(),
                       "shadow module parameter edits keep compiled variants");
    if (current)
      stable = std::move(frame);
    ok &= current;
  };
  draw("cold module-only shadow constant", false);
  parameter = run.scene.parameter4("prepared rgba", value);
  module = preparationModule(false);
  ok &= run.bind(module);
  draw(
      "parameterized constant follows literal without fixed surface dependency",
      false);
  ok &=
      check(!run.lastCompiles.empty(),
            "new shadow UNIFORM AST/signature compiles after literal CONSTANT");
  value = {{.65f, .24f, .4f, 1}};
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  draw("constant parameter mutation remains unlit with real map", true);
  value = {{.2f, .55f, .75f, 1}};
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  draw("constant current A/B/A", true);
  primary = true;
  module = preparationModule(true);
  ok &= run.bind(module);
  value = {{.7f, .6f, .8f, 0}};
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  draw("PRIMARY includes Phong specular and real VSM before parameter gain",
       false);
  scene.caster->translation.setValue(3, 0, .75f);
  outsideCaster = true;
  draw("only moving caster changes the actual PRIMARY visibility", true);
  scene.caster->translation.setValue(0, 0, .75f);
  outsideCaster = false;
  draw("caster A/B/A preserves map and module delivery", true);
  active = false;
  draw("ordinary receiver after module still consumes full Phong/VSM", false);
  active = true;
  draw("module returns after ordinary receiver", true);
  value = {{.35f, .8f, .45f, 0}};
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  draw("PRIMARY gain edit does not recompile", true);
  const Frame before = stable, physical = run.lastPhysical;
  auto *depth = new SoDepthBuffer;
  depth->test = TRUE;
  depth->write = TRUE;
  depth->function = SoDepthBuffer::LEQUAL;
  depth->range.setValue(std::numeric_limits<float>::quiet_NaN(), 1);
  scene.group->insertChild(depth, scene.group->findChild(scene.positions));
  if (run.window)
    ok &= check(run.target->requestWindowReadbackRGBA(),
                "window refusal also has explicit same-apply readback request");
  ok &= run.refuse(before, "invalid shadow-module depth is atomic", scene.root);
  ok &= untouched(
      run, before, physical,
      "shadow-module refusal keeps old exact publication/physical image");
  depth->range.setValue(0, 1);
  draw("same target recovery has current delivery and image", true);
  if (run.window)
    for (unsigned side : {96u, 64u}) {
      ok &=
          check(run.resize(side), "public shadow preparation extent mutation");
      draw("current shadow-module image after resize", true);
    }
  return ok;
}
// A fixed R26 directional bump occurrence and a distinct adapted occurrence
// share the actual mesh. No module is applied to the bump occurrence. Uniform
// scaling leaves the normalized directional light unchanged in object space.
bool preparationBumpDelivery(Run &run, bump_profile::Scene &bump,
                             bool bumpEnabled, bool adapted,
                             const Module &module, Color value) {
  const bool active[2] = {false, adapted};
  bool ok = preparationDelivery(run, run.scene.mesh, run.scene.positions,
                                active, true, module, value, bumpEnabled);
  const auto &frame = run.audit->plan;
  ok &=
      check(frame.shadowGroups.empty() && frame.shadowLights.empty() &&
                !coin_render_uses_extended_bump(frame),
            "legacy bump has no shadow-map groups or lights and no extended profile");
  std::vector<CoinRenderBumpGpuVertex> stream;
  std::string error;
  const bool built = coin_render_build_bump_gpu_stream(frame, stream, error);
  ok &= check(built, "real mixed bump stream is well formed: " + error);
  if (!built)
    return false;
  CoinWgpuFfiFrame ffi;
  const bool packed =
      bumpEnabled ? ffi.prepareBumpFrame(frame, run.extent, run.extent, error)
                  : ffi.prepare(frame, run.extent, run.extent, error, false);
  ok &= check(packed, "actual mixed bump/module FFI packet: " + error);
  if (!packed)
    return false;
  const auto &view = ffi.getView();
  unsigned corners[2] = {0, 0};
  for (uint32_t i = 0; i < view.draw_count; ++i)
    ok &= check(view.states[view.draws[i].render_state_slot].light_model == 1,
                "both actual FFI occurrences retain authored PHONG");
  if (bumpEnabled) {
    ok &= check(frame.bumpImages.size() == 1 && view.bump_image_count == 1 &&
                    stream.size() == view.bump_vertex_count,
                "one consumed normalmap and its complete stream reach FFI");
    if (frame.bumpImages.size() != 1 || view.bump_image_count != 1 ||
        stream.size() != view.bump_vertex_count)
      return false;
    const auto &image = view.bump_images[0];
    ok &=
        check(image.width == 1 && image.height == 1 && image.rgb_len == 3 &&
                  std::equal(bump.normal.begin(), bump.normal.end(), image.rgb),
              "current authored normal RGB survives transport exactly");
    for (size_t i = 0; i < stream.size(); ++i) {
      const auto &a = stream[i];
      const auto &b = view.bump_vertices[i];
      ok &=
          check(a.imageSlot == b.image_slot &&
                    !std::memcmp(a.coord, b.coord, sizeof(a.coord)) &&
                    !std::memcmp(a.tangentS, b.tangent_s, sizeof(a.tangentS)) &&
                    !std::memcmp(a.tangentT, b.tangent_t, sizeof(a.tangentT)),
                "every Core bump coordinate/tangent tuple reaches FFI");
    }
  }
  if (!bumpEnabled)
    ok &= check(frame.bumpImages.empty() && stream.empty() &&
                    view.bump_image_count == 0 && view.bump_vertex_count == 0,
                "empty authored normalmap leaves no consumed bump resource");
  for (const auto &draw : frame.draws) {
    if (draw.shadowLightSlot || !draw.geometry.indexCount)
      continue;
    const auto &state = frame.renderStates[draw.renderStateSlot];
    ok &= check(state.lightModel == CoinRenderLightModel::PHONG &&
                    state.lightingSlot < frame.lightingStates.size() &&
                    frame.lightingStates[state.lightingSlot].lights.size() == 1 &&
                    frame.lightingStates[state.lightingSlot].lights[0].type ==
                        CoinRenderLightType::DIRECTIONAL,
                "each actual occurrence has qualified PHONG and the authored DIR");
    const unsigned lane = state.model[3][0] > 0 ? 1 : 0;
    for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
      const auto index = frame.indices[draw.geometry.firstIndex + j];
      if (bumpEnabled) {
        if (index >= stream.size())
          return false;
        ok &= check(stream[index].imageSlot == (lane == 0 ? 1u : 0u),
                    "only the fixed occurrence consumes the normalmap");
      }
      ++corners[lane];
    }
  }
  return check(corners[0] == 6 && corners[1] == 6,
               "both fixed and adapted occurrences actually execute six "
               "corners") &&
         ok;
}

bool preparationBumpMixed(Run &run) {
  auto &scene = run.scene;
  // Legacy bump admission applies to all main occurrences, including adapted
  // surfaces: the constant module replaces their PHONG result after lighting.
  scene.model->model = SoLightModel::PHONG;
  presentationGeometry(scene);
  bump_profile::Scene bump;
  bump.setNormal({{154, 110, 246}});
  auto *geometry = new SoGroup;
  geometry->ref();
  geometry->addChild(scene.positions);
  geometry->addChild(scene.mesh);
  scene.root->removeChild(scene.positions);
  scene.root->removeChild(scene.mesh);
  scene.root->addChild(bump.light);
  SoSeparator *branches[2];
  SoSwitch *program = nullptr;
  for (unsigned lane = 0; lane < 2; ++lane) {
    auto *branch = new SoSeparator;
    branches[lane] = branch;
    auto *translation = new SoTranslation;
    translation->translation.setValue(lane ? .48f : -.48f, 0, 0);
    auto *scale = new SoScale;
    scale->scaleFactor.setValue(.5f, .5f, .5f);
    branch->addChild(translation);
    branch->addChild(scale);
    if (lane == 0) {
      auto *phong = new SoLightModel;
      phong->model = SoLightModel::PHONG;
      branch->addChild(phong);
      // Reuse public, qualified R26 node authorship, not a captured frame or
      // renderer result. Keep its directional light outside both Separators.
      for (int i = 0; i < bump.root->getNumChildren(); ++i) {
        auto *node = bump.root->getChild(i);
        if (node != bump.camera && node != bump.light &&
            node != bump.positions && node != bump.mesh)
          branch->addChild(node);
      }
    } else {
      auto *mat = new SoMaterial;
      mat->diffuseColor.setValue(.2f, .6f, .4f);
      mat->ambientColor.setValue(0, 0, 0);
      mat->emissiveColor.setValue(0, 0, 0);
      mat->specularColor.setValue(0, 0, 0);
      mat->transparency = 0;
      branch->addChild(mat);
      auto *normal = new SoNormal;
      normal->vector.setValue(0, 0, 1);
      branch->addChild(normal);
      auto *binding = new SoNormalBinding;
      binding->value = SoNormalBinding::OVERALL;
      branch->addChild(binding);
      program = new SoSwitch;
      program->addChild(scene.program);
      branch->addChild(program);
    }
    branch->addChild(geometry);
    scene.root->addChild(branch);
  }
  geometry->unref();
  Module module;
  declare(module, "prepared rgba", Type::FLOAT4);
  module.expressions = {
      uniform("prepared rgba", Type::FLOAT4),
      constant({{0, 0, 0, 1}}), constant({{1, 1, 1, 1}}),
      operation(Op::CLAMP, Type::FLOAT4, 0, 1, 2)};
  module.output = 3;
  // RGB parameters stay in [0,1]; equal alpha bounds structurally prove one
  // independently of their values, so the legacy opaque bump domain applies.
  module.alphaMayVary = FALSE;
  Color value{{.13f, .57f, .29f, 1}};
  auto *parameter = scene.parameter4("prepared rgba", value);
  bool ok = run.bind(module), first = true, adapted = true, bumpEnabled = true;
  Frame stable;
  auto draw = [&](const char *label, bool cached) {
    program->whichChild = adapted ? SO_SWITCH_ALL : SO_SWITCH_NONE;
    // N is the sampled RGB expanded to [-1,1], without renormalization.
    // Axis-aligned UVs give S=+X,T=-Y. L=(.6,0,.8), already unit length;
    // zero specular/ambient/emission makes the independent RGB constant.
    double factor = .8;
    if (bumpEnabled)
      factor =
          std::max(0., std::min(1., (2. * bump.normal[0] / 255 - 1) * .6 +
                                        (2. * bump.normal[2] / 255 - 1) * .8));
    std::array<Color, 2> colors{
        {Color{{float(.8 * factor), float(.4 * factor), float(.2 * factor), 1}},
         // Ordinary PHONG has geometric N=+Z and unit L=(.6,0,.8):
         // diffuse * .8; the active constant module remains unlit.
         adapted ? value : Color{{.16f, .48f, .32f, 1}}}};
    Frame frame;
    const bool rendered =
        first ? preparationFirst(run, frame, scene.root, "bump-mixed")
              : run.render(frame, label);
    first = false;
    bool current = rendered;
    if (rendered) {
      current &= preparationBumpDelivery(run, bump, bumpEnabled, adapted,
                                         module, value);
      current &= check(
          preparationBasicImage(frame, run.extent, run.samples, colors,
                                run.window),
          "fixed bump plus module has analytic RGB/depth and real coverage");
      current &=
          check(preparationPhysical(run, frame,
                                    [&](const Frame &shown) {
                                      return preparationBasicImage(
                                          shown, run.extent, run.samples,
                                          colors, true, false);
                                    }),
                "current mixed bump/module physical image within five seconds");
    }
    if (cached)
      current &= check(
          run.lastCompiles.empty(),
          "fixed bump state/parameter edits do not recompile the same module");
    if (current)
      stable = std::move(frame);
    ok &= current;
  };
  draw("cold fixed bump plus actual module", false);
  bump.setNormal({{255, 128, 128}});
  draw("normalmap mutation affects only fixed bump", true);
  bump.setNormal({{154, 110, 246}});
  draw("normalmap restoration with current delivery", true);
  value = Color{{.71f, .19f, .53f, 1}};
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  draw("module parameter edit leaves bump intact", true);
  value = Color{{.13f, .57f, .29f, 1}};
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  draw("parameter restoration with current delivery", true);
  branches[0]->ref();
  scene.root->removeChild(branches[0]);
  scene.root->addChild(branches[0]);
  branches[0]->unref();
  draw("module then fixed bump reverses occurrence order", true);
  branches[0]->ref();
  scene.root->removeChild(branches[0]);
  scene.root->insertChild(branches[0], scene.root->findChild(branches[1]));
  branches[0]->unref();
  draw("fixed bump then module restores occurrence order", true);
  adapted = false;
  draw("fixed bump and ordinary retain their executing programs", true);
  bumpEnabled = false;
  bump.bump->image.setValue(SbVec2s(0, 0), 3, nullptr);
  draw("ordinary-only transition clears the normalmap resource", true);
  bumpEnabled = true;
  bump.setNormal({{154, 110, 246}});
  draw("fixed bump returns after ordinary-only execution", true);
  adapted = true;
  draw("return from ordinary to the cached module", true);
  const Frame before = stable, physical = run.lastPhysical;
  parameter->value.setValue(value[0], std::numeric_limits<float>::quiet_NaN(),
                            value[2], value[3]);
  ok &= run.refuse(before, "invalid adapted binding alongside fixed bump");
  ok &= untouched(run, before, physical,
                  "mixed bump refusal retains exact buffers/serial/physical");
  parameter->value.setValue(value[0], value[1], value[2], value[3]);
  draw("same-target mixed recovery has new delivery", true);
  return ok;
}
#endif
