#ifndef COIN_RENDER_BUMP_OCCURRENCE_CASES_H
#define COIN_RENDER_BUMP_OCCURRENCE_CASES_H
// Independent R22.5 authoring. The shared mesh has a lit bump occurrence and
// an unlit occurrence, whose empty light state is an intentional input.
Module occurrenceModule(bool primary) {
  Module m;
  if (primary)
    m.expressions.push_back(operation(Op::PRIMARY_COLOR, Type::FLOAT4));
  else {
    declare(m, "occurrence color", Type::FLOAT4);
    m.expressions.push_back(uniform("occurrence color", Type::FLOAT4));
  }
  m.expressions.push_back(constant({{0, 0, 0, 1}}));
  m.expressions.push_back(constant({{1, 1, 1, 1}}));
  m.expressions.push_back(operation(Op::CLAMP, Type::FLOAT4, 0, 1, 2));
  m.output = 3;
  m.alphaMayVary = FALSE;
  return m;
}
struct OccurrenceScene {
  Scene &scene;
  bump_profile::Scene bump;
  unsigned variant;
  SoSeparator *branches[2]{};
  SoLightModel *bumpModel = new SoLightModel;
  SoSwitch *lights = new SoSwitch;
  SoSwitch *program = new SoSwitch;
  SoShadowGroup *group = nullptr;
  SoShadowDirectionalLight *mapped = nullptr;
  SoTranslation *caster = nullptr;
  SoCube *cube = nullptr;
  std::vector<SoDirectionalLight *> lamps;
  bool primary = false, adapted = true, reversed = false, casterOutside = false;
  Color value{{.13f, .57f, .29f, 1}};
  Module module = occurrenceModule(false);
  SoShaderParameter4f *parameter = nullptr;
  explicit OccurrenceScene(Scene &s, unsigned kind) : scene(s), variant(kind) {
    presentationGeometry(scene);
    scene.model->model = SoLightModel::BASE_COLOR;
    auto *geometry = new SoGroup;
    geometry->ref();
    geometry->addChild(scene.positions);
    geometry->addChild(scene.mesh);
    scene.root->removeChild(scene.positions);
    scene.root->removeChild(scene.mesh);
    bump.setNormal({{154, 110, 246}});
    for (unsigned lane = 0; lane < 2; ++lane) {
      auto *branch = new SoSeparator;
      branches[lane] = branch;
      SoGroup *owner = branch;
      if (!lane && variant == 2) {
        group = new SoShadowGroup;
        group->precision = .125f;
        group->quality = 1;
        group->epsilon = 0;
        group->threshold = 0;
        group->intensity = 1;
        branch->addChild(group);
        auto *style = new SoShadowStyle;
        style->style = SoShadowStyle::SHADOWED;
        group->addChild(style);
        owner = group;
      }
      if (!lane) {
        bumpModel->model = SoLightModel::PHONG;
        owner->addChild(bumpModel);
        owner->addChild(lights);
        lights->whichChild = SO_SWITCH_ALL;
        for (unsigned i = 0; i < (variant ? 4u : 1u); ++i) {
          SoDirectionalLight *lamp = nullptr;
          if (variant == 2 && i == 3) {
            mapped = new SoShadowDirectionalLight;
            mapped->bboxCenter.setValue(-.48f, 0, 0);
            mapped->bboxSize.setValue(2, 2, 8);
            auto *source = new SoSeparator;
            auto *style = new SoShadowStyle;
            style->style = SoShadowStyle::CASTS_SHADOW;
            source->addChild(style);
            caster = new SoTranslation;
            caster->translation.setValue(-.48f, 0, .75f);
            source->addChild(caster);
            cube = new SoCube;
            cube->width = cube->height = .4f;
            cube->depth = .5f;
            source->addChild(cube);
            mapped->shadowMapScene = source;
            lamp = mapped;
          } else
            lamp = new SoDirectionalLight;
          lamp->direction.setValue(variant ? 0 : -.6f, 0, variant ? -1 : -.8f);
          lamp->color.setValue(1, 1, 1);
          lamp->intensity = variant ? float(.1 + .05 * i) : 1.f;
          lamps.push_back(lamp);
          lights->addChild(lamp);
        }
      }
      auto *move = new SoTranslation;
      move->translation.setValue(lane ? .48f : -.48f, 0, 0);
      owner->addChild(move);
      auto *scale = new SoScale;
      scale->scaleFactor.setValue(.5f, .5f, .5f);
      owner->addChild(scale);
      if (!lane) {
        for (int i = 0; i < bump.root->getNumChildren(); ++i) {
          auto *node = bump.root->getChild(i);
          if (node != bump.camera && node != bump.light &&
              node != bump.positions && node != bump.mesh)
            owner->addChild(node);
        }
      } else {
        auto *material = new SoMaterial;
        material->diffuseColor.setValue(.2f, .6f, .4f);
        material->ambientColor.setValue(0, 0, 0);
        material->emissiveColor.setValue(0, 0, 0);
        material->specularColor.setValue(0, 0, 0);
        material->transparency = 0;
        owner->addChild(material);
        // Visiting a neutral declaration is not normalmap consumption.
        owner->addChild(new SoBumpMap);
        program->addChild(scene.program);
        program->whichChild = SO_SWITCH_ALL;
        owner->addChild(program);
      }
      owner->addChild(geometry);
      scene.root->addChild(branch);
    }
    geometry->unref();
    parameter = scene.parameter4("occurrence color", value);
  }
  void order(bool reverse) {
    if (reverse == reversed)
      return;
    branches[0]->ref();
    scene.root->removeChild(branches[0]);
    if (reverse)
      scene.root->addChild(branches[0]);
    else
      scene.root->insertChild(branches[0], scene.root->findChild(branches[1]));
    branches[0]->unref();
    reversed = reverse;
  }
  Color neighbor() const {
    return adapted && !primary ? value : Color{{.2f, .6f, .4f, 1}};
  }
  Color bumpColor(bool shadow) const {
    const double nx = 2. * bump.normal[0] / 255 - 1;
    const double nz = 2. * bump.normal[2] / 255 - 1;
    const double dot =
        std::max(0., std::min(1., variant ? nz : .6 * nx + .8 * nz));
    double total = 0;
    for (size_t i = 0; i < lamps.size(); ++i)
      if (lamps[i]->on.getValue() && !(shadow && i == 3))
        total += lamps[i]->intensity.getValue();
    return Color{{float(.8 * dot * total), float(.4 * dot * total),
                  float(.2 * dot * total), 1}};
  }
};
bool occurrenceDelivery(const OccurrenceScene &s, const CoinRenderFramePlan &f,
                        unsigned side) {
  std::string error;
  bool ok =
      check(f.isValid(&error), "occurrence frame common validation: " + error);
  std::vector<CoinRenderBumpGpuVertex> stream;
  if (!check(coin_render_build_bump_gpu_stream(f, stream, error),
             "occurrence complete bump stream: " + error))
    return false;
  static unsigned printedProfiles = 0;
  const unsigned profileBit = 1u << s.variant;
  const bool diagnose = (printedProfiles & profileBit) == 0;
  printedProfiles |= profileBit;
  if (diagnose)
    std::cout << "occurrence_delivery_packet variant=" << s.variant
              << " revision=" << f.revision << " core_draws=" << f.draws.size()
              << " states=" << f.renderStates.size()
              << " lighting_states=" << f.lightingStates.size()
              << " shadow_lights=" << f.shadowLights.size() << std::endl;
  std::vector<uint32_t> draws[2];
  for (uint32_t i = 0; i < f.draws.size(); ++i) {
    const auto &d = f.draws[i];
    if (d.shadowLightSlot || !d.geometry.indexCount)
      continue;
    if (d.renderStateSlot >= f.renderStates.size())
      return false;
    const auto &state = f.renderStates[d.renderStateSlot];
    const unsigned lane = state.model[3][0] > 0 ? 1 : 0;
    draws[lane].push_back(i);
    if (state.lightingSlot >= f.lightingStates.size())
      return false;
    const auto &light = f.lightingStates[state.lightingSlot].lights;
    if (diagnose) {
      std::cout << "occurrence_core_main variant=" << s.variant
                << " draw_index=" << i << " lane=" << lane
                << " source=" << d.sourceNodeId
                << " render_state_slot=" << d.renderStateSlot
                << " lighting_slot=" << state.lightingSlot
                << " light_model=" << uint32_t(state.lightModel)
                << " light_count=" << light.size()
                << " module_slot=" << state.portableShaderSlot
                << " shadow_group_slot=" << state.shadowGroupSlot
                << " shadow_light_slot=" << d.shadowLightSlot
                << " first_vertex=" << d.geometry.firstVertex
                << " vertex_count=" << d.geometry.vertexCount
                << " first_index=" << d.geometry.firstIndex
                << " index_count=" << d.geometry.indexCount << std::endl;
      for (size_t j = 0; j < light.size(); ++j)
        std::cout << "occurrence_core_light draw_index=" << i
                  << " lighting_slot=" << state.lightingSlot
                  << " local_light_index=" << j
                  << " source=" << light[j].sourceRevision
                  << " intensity=" << light[j].intensity
                  << " direction=" << light[j].direction[0] << ","
                  << light[j].direction[1] << "," << light[j].direction[2]
                  << std::endl;
    }
    ok &= check(d.sourceNodeId == s.scene.mesh->getNodeId() &&
                    state.lightModel == (lane ? CoinRenderLightModel::BASE_COLOR
                                              : CoinRenderLightModel::PHONG) &&
                    light.size() == (lane ? 0u : s.lamps.size()),
                "shared occurrences retain different models and genuinely "
                "local lights");
    if (!lane)
      for (size_t j = 0; j < light.size() && j < s.lamps.size(); ++j) {
        const auto *authored = s.lamps[j];
        const auto direction = authored->direction.getValue();
        const auto color = authored->color.getValue();
        const float power = authored->intensity.getValue();
        const double length = std::sqrt(double(direction[0]) * direction[0] +
                                        double(direction[1]) * direction[1] +
                                        double(direction[2]) * direction[2]);
        bool delivered =
            authored->on.getValue() != FALSE && length > 0 &&
            std::isfinite(length) &&
            light[j].type == CoinRenderLightType::DIRECTIONAL &&
            light[j].sourceRevision == authored->getNodeId() &&
            !std::memcmp(&light[j].intensity, &power, sizeof(power));
        for (unsigned c = 0; c < 3; ++c)
          delivered &=
              light[j].color[c] == color[c] &&
              std::abs(light[j].direction[c] -
                       (length > 0 ? direction[c] / length : 0)) < 1e-6;
        ok &=
            check(delivered, "each enabled authored DIR retains exact "
                             "identity/color/intensity and its view direction");
      }
    ok &= check(lane && s.adapted
                    ? portableCoreBinding(
                          f, d.renderStateSlot, s.module,
                          s.primary ? std::vector<float>()
                                    : std::vector<float>(s.value.begin(),
                                                         s.value.end()))
                    : state.portableShaderSlot == 0,
                "module ownership and exact payload remain occurrence-local");
    for (uint32_t j = 0; j < d.geometry.indexCount; ++j) {
      const auto index = f.indices[d.geometry.firstIndex + j];
      if (index >= stream.size())
        return false;
      ok &= check(stream[index].imageSlot == (lane ? 0u : 1u),
                  "only genuinely consumed draw indices address the normalmap");
      if (!lane) {
        const auto &vertex = f.vertices[index];
        const auto &tuple = stream[index];
        const double u = (vertex.position[0] + .75) / 1.515625;
        const double t = (vertex.position[1] + .7078125) / 1.4578125;
        ok &= check(std::abs(tuple.coord[0] - u) < 1e-5 &&
                        std::abs(tuple.coord[1] - t) < 1e-5 &&
                        tuple.coord[2] == 0 && tuple.coord[3] == 1 &&
                        std::abs(tuple.tangentS[0] - 1) < 1e-5 &&
                        std::abs(tuple.tangentS[1]) < 1e-5 &&
                        std::abs(tuple.tangentS[2]) < 1e-5 &&
                        std::abs(tuple.tangentT[0]) < 1e-5 &&
                        std::abs(tuple.tangentT[1] + 1) < 1e-5 &&
                        std::abs(tuple.tangentT[2]) < 1e-5,
                    "authorial axis-aligned UV and signed S/T basis stay "
                    "occurrence-local");
      }
    }
  }
  for (unsigned lane = 0; lane < 2; ++lane)
    ok &= check(portableCoreQuad(f, draws[lane], s.scene.positions),
                "each occurrence covers the whole authorial quad exactly once");
  CoinWgpuFfiFrame ffi;
  const bool packed = ffi.prepareBumpFrame(f, side, side, error);
  ok &= check(packed, "actual occurrence-aware bump FFI ingress: " + error);
  if (!packed)
    return false;
  const auto &v = ffi.getView();
  ok &= check(v.abi_version == 65 && v.bump_image_count == 1 &&
                  v.bump_vertex_count == stream.size() &&
                  v.portable_module_count == (s.adapted ? 1u : 0u),
              "ABI65 owns exact consumed normalmap and module resources");
  if (v.bump_image_count != 1 || v.bump_vertex_count != stream.size())
    return false;
  ok &= check(v.bump_images[0].width == 1 && v.bump_images[0].height == 1 &&
                  v.bump_images[0].rgb_len == 3 &&
                  std::equal(s.bump.normal.begin(), s.bump.normal.end(),
                             v.bump_images[0].rgb),
              "normalmap mutation/restoration retains exact source bytes");
  for (size_t i = 0; i < stream.size(); ++i)
    ok &=
        check(v.bump_vertices[i].image_slot == stream[i].imageSlot &&
                  !std::memcmp(v.bump_vertices[i].coord, stream[i].coord,
                               sizeof(stream[i].coord)) &&
                  !std::memcmp(v.bump_vertices[i].tangent_s, stream[i].tangentS,
                               sizeof(stream[i].tangentS)) &&
                  !std::memcmp(v.bump_vertices[i].tangent_t, stream[i].tangentT,
                               sizeof(stream[i].tangentT)),
              "all normalmap coordinate/TBN tuples survive FFI unchanged");
  std::vector<uint32_t> packedDraws[2];
  for (uint32_t i = 0; i < v.draw_count; ++i) {
    const auto &d = v.draws[i];
    const auto xy =
        portableClip(v, d.render_state_slot, v.indices[d.first_index]);
    const unsigned lane = xy[0] > 0 ? 1 : 0;
    packedDraws[lane].push_back(i);
    const auto &state = v.states[d.render_state_slot];
    if (diagnose) {
      const bool hasReceiver =
          v.shadow_receivers && d.render_state_slot < v.shadow_receiver_count;
      std::cout << "occurrence_ffi_main variant=" << s.variant
                << " draw_index=" << i << " lane=" << lane
                << " source=" << d.stable_node_id
                << " render_state_slot=" << d.render_state_slot
                << " light_model=" << state.light_model
                << " light_count=" << state.light_count
                << " module_slot=" << state.portable_shader_slot
                << " first_vertex=" << d.first_vertex
                << " vertex_count=" << d.vertex_count
                << " first_index=" << d.first_index
                << " index_count=" << d.index_count << " map_lighting_index="
                << (hasReceiver
                        ? v.shadow_receivers[d.render_state_slot].lighting_index
                        : -1)
                << " map_receiver_flags="
                << (hasReceiver
                        ? v.shadow_receivers[d.render_state_slot].receives
                        : 0u)
                << std::endl;
    }
    ok &= check(
        state.light_model == (lane ? 0u : 1u) &&
            state.light_count == (lane ? 0u : s.lamps.size()),
        "actual FFI preserves BASE_COLOR zero-light neighbor and lit bump");
    if (!lane && !draws[0].empty()) {
      // Match the authored occurrence, not an assumption that FFI state IDs
      // equal Core IDs after state duplication or composition scheduling.
      const auto &coreState =
          f.renderStates[f.draws[draws[0].front()].renderStateSlot];
      const auto &coreLights = f.lightingStates[coreState.lightingSlot].lights;
      for (size_t j = 0;
           j < coreLights.size() && j < state.light_count && j < s.lamps.size();
           ++j) {
        const auto &from = coreLights[j];
        const auto &to = state.lights[j];
        const bool identical =
            to.position_type[3] == float(uint32_t(from.type)) &&
            !std::memcmp(to.direction_cutoff, from.direction,
                         sizeof(from.direction)) &&
            !std::memcmp(to.color_intensity, from.color, sizeof(from.color)) &&
            !std::memcmp(&to.color_intensity[3], &from.intensity,
                         sizeof(from.intensity));
        ok &= check(identical,
                    "every consumed DIR retains exact Core-to-FFI "
                    "type/RGB/intensity/direction, including unmapped lights");
        if (diagnose || !identical)
          std::cout << "occurrence_ffi_light draw_index=" << i
                    << " render_state_slot=" << d.render_state_slot
                    << " local_light_index=" << j
                    << " authored_on=" << s.lamps[j]->on.getValue()
                    << " source=" << from.sourceRevision
                    << " core_type=" << uint32_t(from.type)
                    << " ffi_type=" << to.position_type[3]
                    << " core_rgb=" << from.color[0] << "," << from.color[1]
                    << "," << from.color[2]
                    << " ffi_rgb=" << to.color_intensity[0] << ","
                    << to.color_intensity[1] << "," << to.color_intensity[2]
                    << " core_intensity=" << from.intensity
                    << " ffi_intensity=" << to.color_intensity[3]
                    << " core_direction=" << from.direction[0] << ","
                    << from.direction[1] << "," << from.direction[2]
                    << " ffi_direction=" << to.direction_cutoff[0] << ","
                    << to.direction_cutoff[1] << "," << to.direction_cutoff[2]
                    << " exact=" << identical << std::endl;
      }
    }
    ok &= check(
        lane && s.adapted
            ? portableFfiBinding(v, d.render_state_slot, s.module,
                                 s.primary ? std::vector<float>()
                                           : std::vector<float>(s.value.begin(),
                                                                s.value.end()))
            : state.portable_shader_slot == 0,
        "every FFI draw retains the correct module association");
  }
  for (unsigned lane = 0; lane < 2; ++lane) {
    std::array<std::array<double, 2>, 4> corners;
    for (unsigned k = 0; k < 4; ++k)
      corners[k] = {{(lane ? .48 : -.48) + .5 * s.scene.positions->point[k][0],
                     .5 * s.scene.positions->point[k][1]}};
    ok &= check(portableFfiQuad(v, packedDraws[lane], corners),
                "FFI retains each occurrence's geometry and model transform");
  }
  CoinRenderShadowPlan plan;
  const bool planned = coin_render_plan_shadows(f, plan, error);
  ok &= check(planned && plan.passes.size() == (s.variant == 2 ? 1u : 0u),
              "only the authored directional map is planned: " + error);
  if (s.variant == 2 && planned && plan.passes.size() == 1) {
    const auto &pass = plan.passes[0];
    if (diagnose)
      std::cout << "occurrence_core_map pass_index=0"
                << " shadow_light_index=" << pass.lightSlot
                << " group_slot=" << pass.groupSlot
                << " map_size=" << pass.mapSize << " visible=" << pass.visible
                << " caster_draws=" << pass.casterDraws.size()
                << " receiver_draws=" << pass.receiverDraws.size() << std::endl;
    ok &= check(
        pass.visible && pass.mapSize == 256 &&
            pass.lightSlot < f.shadowLights.size() &&
            f.shadowLights[pass.lightSlot].sourceRevision ==
                s.mapped->getNodeId() &&
            !pass.casterDraws.empty() &&
            pass.receiverDraws.size() == draws[0].size(),
        "real last-light map has a nonempty caster and the bump receiver");
    for (auto d : draws[0]) {
      const auto slot = f.draws[d].renderStateSlot;
      if (diagnose)
        std::cout << "occurrence_core_map_receiver draw_index=" << d
                  << " render_state_slot=" << slot
                  << " lighting_slot=" << f.renderStates[slot].lightingSlot
                  << " map_lighting_index="
                  << (slot < pass.lightingIndexByState.size()
                          ? pass.lightingIndexByState[slot]
                          : -1)
                  << std::endl;
      ok &= check(
          std::find(pass.receiverDraws.begin(), pass.receiverDraws.end(), d) !=
                  pass.receiverDraws.end() &&
              slot < pass.lightingIndexByState.size() &&
              pass.lightingIndexByState[slot] == 3,
          "VSM attenuates exactly the last local directional contribution");
    }
    unsigned casterIndices = 0;
    for (auto d : pass.casterDraws) {
      ok &= check(d < f.draws.size(), "map caster draw index is valid");
      if (d >= f.draws.size())
        return false;
      const auto &draw = f.draws[d];
      if (diagnose)
        std::cout << "occurrence_core_caster draw_index=" << d
                  << " source=" << draw.sourceNodeId
                  << " render_state_slot=" << draw.renderStateSlot
                  << " lighting_slot="
                  << f.renderStates[draw.renderStateSlot].lightingSlot
                  << " shadow_light_slot=" << draw.shadowLightSlot
                  << " first_vertex=" << draw.geometry.firstVertex
                  << " vertex_count=" << draw.geometry.vertexCount
                  << " first_index=" << draw.geometry.firstIndex
                  << " index_count=" << draw.geometry.indexCount << std::endl;
      casterIndices += draw.geometry.indexCount;
      ok &= check(
          draw.sourceNodeId == s.cube->getNodeId() && draw.shadowLightSlot &&
              f.renderStates[draw.renderStateSlot].portableShaderSlot == 0,
          "map-only geometry remains an unadapted moments caster");
    }
    if (diagnose)
      for (uint64_t i = 0; i < v.shadow_caster_count; ++i) {
        const auto &caster = v.shadow_casters[i];
        std::cout << "occurrence_ffi_caster caster_index=" << i
                  << " render_state_slot=" << caster.render_state_slot
                  << " first_index=" << caster.first_index
                  << " index_count=" << caster.index_count
                  << " viewport=" << caster.viewport[0] << ","
                  << caster.viewport[1] << "," << caster.viewport[2] << ","
                  << caster.viewport[3] << std::endl;
      }
    ok &= check(casterIndices == 36 && v.shadow_caster_count > 0,
                "the complete authored cube reaches real shadow transport");
  }
  return ok;
}
bool occurrenceImage(const OccurrenceScene &s, const Frame &f, unsigned side,
                     unsigned samples, bool window, bool report = true) {
  const std::array<Color, 2> colors{{s.bumpColor(false), s.neighbor()}};
  if (s.variant != 2 || s.casterOutside)
    return preparationBasicImage(f, side, samples, colors, window, report);
  if (f.rgba.size() != size_t(side) * side * 4 ||
      (!window && f.depth.size() != size_t(side) * side))
    return false;
  const double margin = 2.1 * 2 / side;
  unsigned lit = 0, dark = 0, neighbor = 0, outside = 0, bad = 0, depthBad = 0;
  struct RoiDiagnostic {
    unsigned count = 0;
    int low[3] = {255, 255, 255}, high[3] = {0, 0, 0};
    double expectedLow[3], expectedHigh[3], maxDelta = 0;
    int sampleX = -1, sampleY = -1, badX = -1, badY = -1;
    Color sample{}, sampleExpected{}, badSample{}, badExpected{};
    RoiDiagnostic() {
      for (unsigned c = 0; c < 3; ++c) {
        expectedLow[c] = std::numeric_limits<double>::infinity();
        expectedHigh[c] = -std::numeric_limits<double>::infinity();
      }
    }
  } diagnostic[3]; // lit, dark, neighbor; no diagnostic-derived oracle values.
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      const double wx = (x + .5) * 2 / side - 1, wy = 1 - (y + .5) * 2 / side;
      const size_t p = size_t(y) * side + x;
      bool exterior = true;
      for (unsigned lane = 0; lane < 2; ++lane) {
        const double lx = (wx - (lane ? .48 : -.48)) / .5, ly = wy / .5;
        const bool inner = lx > -.75 + 2 * margin &&
                           lx < .765625 - 2 * margin &&
                           ly > -.7078125 + 2 * margin && ly < .75 - 2 * margin;
        exterior &= lx < -.75 - 2 * margin || lx > .765625 + 2 * margin ||
                    ly < -.7078125 - 2 * margin || ly > .75 + 2 * margin;
        if (!inner)
          continue;
        const bool deep = !lane && std::abs(wx + .48) < .2 - margin &&
                          std::abs(wy) < .2 - margin;
        const bool clear = lane || std::abs(wx + .48) > .2 + margin ||
                           std::abs(wy) > .2 + margin;
        if (!deep && !clear)
          continue;
        if (lane)
          ++neighbor;
        else if (deep)
          ++dark;
        else
          ++lit;
        const Color expected = lane ? s.neighbor() : s.bumpColor(deep);
        if (report) {
          auto &roi = diagnostic[lane ? 2 : deep ? 1 : 0];
          ++roi.count;
          bool rgbBad = false;
          Color actual{}, scaledExpected{};
          for (unsigned c = 0; c < 3; ++c) {
            const int value = f.rgba[4 * p + c];
            const double want = 255. * expected[c];
            roi.low[c] = std::min(roi.low[c], value);
            roi.high[c] = std::max(roi.high[c], value);
            roi.expectedLow[c] = std::min(roi.expectedLow[c], want);
            roi.expectedHigh[c] = std::max(roi.expectedHigh[c], want);
            roi.maxDelta = std::max(roi.maxDelta, std::abs(value - want));
            rgbBad |= std::abs(value - want) > 3;
            actual[c] = float(value);
            scaledExpected[c] = float(want);
          }
          if (roi.sampleX < 0) {
            roi.sampleX = int(x);
            roi.sampleY = int(y);
            roi.sample = actual;
            roi.sampleExpected = scaledExpected;
          }
          if (rgbBad && roi.badX < 0) {
            roi.badX = int(x);
            roi.badY = int(y);
            roi.badSample = actual;
            roi.badExpected = scaledExpected;
          }
        }
        for (unsigned c = 0; c < 3; ++c)
          bad += std::abs(f.rgba[4 * p + c] - 255. * expected[c]) > 3;
        bad += f.rgba[4 * p + 3] != 255;
        if (!window)
          depthBad += std::abs(f.depth[p] - 2.9 / 9.9) > 2e-5;
      }
      if (exterior) {
        ++outside;
        for (unsigned c = 0; c < 3; ++c)
          bad += f.rgba[4 * p + c] > 3;
        if (!window) {
          bad += f.rgba[4 * p + 3] != 0;
          depthBad += f.depth[p] != 1;
        }
      }
    }
  if (report)
    std::cout << "bump_occurrence_shadow lit=" << lit << " dark=" << dark
              << " neighbor=" << neighbor << " outside=" << outside
              << " bad=" << bad << " depth_bad=" << depthBad << std::endl;
  if (report && (bad || depthBad)) {
    const char *names[] = {"lit", "dark", "neighbor"};
    for (unsigned i = 0; i < 3; ++i) {
      const auto &roi = diagnostic[i];
      std::cout << "bump_occurrence_shadow_diagnostic roi=" << names[i]
                << " count=" << roi.count << " max_rgb_delta=" << roi.maxDelta;
      for (unsigned c = 0; c < 3; ++c)
        std::cout << " channel" << c << "_actual=" << roi.low[c] << ":"
                  << roi.high[c] << " channel" << c
                  << "_expected=" << roi.expectedLow[c] << ":"
                  << roi.expectedHigh[c];
      std::cout << " sample_xy=" << roi.sampleX << "," << roi.sampleY
                << " sample_actual=" << roi.sample[0] << "," << roi.sample[1]
                << "," << roi.sample[2]
                << " sample_expected=" << roi.sampleExpected[0] << ","
                << roi.sampleExpected[1] << "," << roi.sampleExpected[2]
                << " first_bad_xy=" << roi.badX << "," << roi.badY
                << " first_bad_actual=" << roi.badSample[0] << ","
                << roi.badSample[1] << "," << roi.badSample[2]
                << " first_bad_expected=" << roi.badExpected[0] << ","
                << roi.badExpected[1] << "," << roi.badExpected[2] << std::endl;
    }
  }
  return lit > 50 && dark > 8 && neighbor > 100 && outside > 500 && !bad &&
         !depthBad;
}
#ifdef COIN_PORTABLE_MODULE_RUST
// Mutations below bypass both Action and the C++ packer. Only the valid
// predecessor is packed; each malformed ABI65 view goes directly to Rust.
bool occurrenceRaw(Run &run, const OccurrenceScene &scene) {
  const auto publicBefore = run.read();
  if (!check(coin_wgpu_install_cpp_portable_core() == COIN_WGPU_OK,
             "raw occurrence uses the runtime's real portable callback"))
    return false;
  CoinWgpuFfiFrame owner;
  std::string diagnostic;
  if (!check(owner.prepareBumpFrame(run.audit->plan, run.extent, run.extent,
                                    diagnostic),
             "valid real mixed capture packs for raw occurrence ingress: " +
                 diagnostic))
    return false;
  const auto original = owner.getView();
  std::set<uint32_t> consumedStates, consumedVertices;
  uint32_t consumedDraw = UINT32_MAX;
  unsigned bumpCorners = 0, neighborCorners = 0;
  for (uint32_t i = 0; i < original.draw_count; ++i) {
    const auto &draw = original.draws[i];
    if (draw.render_state_slot >= original.state_count ||
        uint64_t(draw.first_index) + draw.index_count > original.index_count)
      return false;
    bool active = false;
    for (uint32_t j = 0; j < draw.index_count; ++j) {
      const auto vertex = original.indices[draw.first_index + j];
      if (vertex >= original.bump_vertex_count)
        return false;
      if (original.bump_vertices[vertex].image_slot) {
        active = true;
        consumedVertices.insert(vertex);
        ++bumpCorners;
      } else
        ++neighborCorners;
    }
    const auto &state = original.states[draw.render_state_slot];
    if (active) {
      consumedStates.insert(draw.render_state_slot);
      consumedDraw = i;
      if (!check(state.light_model == 1 &&
                     state.light_count == scene.lamps.size() &&
                     state.portable_shader_slot == 0,
                 "raw mutation identifies the real fixed bump consumer"))
        return false;
    } else if (!check(state.light_model == 0 && state.light_count == 0 &&
                          state.portable_shader_slot != 0,
                      "raw packet retains a valid zero-light BASE_COLOR module "
                      "neighbor"))
      return false;
  }
  if (!check(original.abi_version == 65 && consumedDraw != UINT32_MAX &&
                 !consumedVertices.empty() && bumpCorners == 6 &&
                 neighborCorners == 6,
             "raw mixed packet has complete independent consumed/unconsumed "
             "occurrences"))
    return false;
  Frame output;
  output.rgba.assign(size_t(run.extent) * run.extent * 4, 0x65);
  output.depth.assign(size_t(run.extent) * run.extent, .8765f);
  CoinWgpuTarget target{};
  target.width = target.height = run.extent;
  target.sample_count = run.samples;
  target.color_buffer = output.rgba.data();
  target.color_buffer_len = output.rgba.size();
  target.depth_buffer = output.depth.data();
  target.depth_buffer_len = output.depth.size();
  auto valid = [&](const char *label) {
    char error[2048] = {};
    const auto before = target.submission_serial;
    const auto status =
        coin_wgpu_submit(&target, &original, error, sizeof(error));
    output.serial = target.submission_serial;
    std::cout << "occurrence_raw phase=" << label << " status=" << status
              << " serial=" << before << "->" << output.serial
              << " error=" << error << std::endl;
    return check(status == COIN_WGPU_OK && output.serial > before,
                 "raw valid mixed occurrence makes a real GPU publication") &&
           check(occurrenceImage(scene, output, run.extent, run.samples, false),
                 "raw valid/recovery colors, alpha, depth have the independent "
                 "authorial oracle");
  };
  if (!valid("valid actual mixed packet"))
    return false;
  for (unsigned fault = 0; fault < 4; ++fault) {
    auto malformed = original;
    std::vector<CoinWgpuRenderState> states(
        original.states, original.states + original.state_count);
    std::vector<CoinWgpuDraw> draws(original.draws,
                                    original.draws + original.draw_count);
    std::vector<CoinWgpuBumpVertex> bump(original.bump_vertices,
                                         original.bump_vertices +
                                             original.bump_vertex_count);
    if (fault == 0 || fault == 1) {
      for (auto state : consumedStates)
        if (fault == 0)
          states[state].light_model = 0;
        else
          states[state].light_count = 0;
      malformed.states = states.data();
    }
    if (fault == 2) {
      bump[*consumedVertices.begin()].image_slot =
          uint32_t(original.bump_image_count + 1);
      malformed.bump_vertices = bump.data();
    }
    if (fault == 3) {
      draws[consumedDraw].first_index = UINT32_MAX;
      malformed.draws = draws.data();
    }
    // Keep the real revision and all unmodified payloads: validation cannot
    // trust a previous accepted packet and skip a malformed direct caller.
    const auto rgba = output.rgba;
    const auto depth = output.depth;
    const auto serial = target.submission_serial;
    CoinWgpuCacheStats before{}, after{};
    CoinWgpuPerformanceStats workBefore{}, workAfter{};
    coin_wgpu_get_cache_stats(&before);
    coin_wgpu_get_performance_stats(&workBefore);
    char error[2048] = {};
    const auto status =
        coin_wgpu_submit(&target, &malformed, error, sizeof(error));
    coin_wgpu_get_cache_stats(&after);
    coin_wgpu_get_performance_stats(&workAfter);
    std::cout << "occurrence_raw_refuse fault=" << fault << " status=" << status
              << " serial=" << serial << "->" << target.submission_serial
              << " device_submits=" << before.submission_serial << "->"
              << after.submission_serial
              << " pipelines=" << workBefore.pipeline_compilations << "->"
              << workAfter.pipeline_compilations
              << " uploads=" << workBefore.texture_uploads << "->"
              << workAfter.texture_uploads << " error=" << error << std::endl;
    if (!check((status == COIN_WGPU_INVALID_ARGUMENT ||
                status == COIN_WGPU_UNSUPPORTED) &&
                   error[0] && target.submission_serial == serial &&
                   output.rgba == rgba && output.depth == depth &&
                   before.submission_serial == after.submission_serial &&
                   workBefore.pipeline_compilations ==
                       workAfter.pipeline_compilations &&
                   workBefore.texture_uploads == workAfter.texture_uploads &&
                   before.cumulative_uploads == after.cumulative_uploads &&
                   before.cumulative_uploaded_bytes ==
                       after.cumulative_uploaded_bytes,
               "raw invalid consumed bump rejects before observable GPU work "
               "and preserves exact outputs"))
      return false;
    if (!valid("same raw target valid recovery"))
      return false;
  }
  const auto publicAfter = run.read();
  return check(
      publicAfter.serial == publicBefore.serial &&
          publicAfter.rgba == publicBefore.rgba &&
          publicAfter.depth == publicBefore.depth,
      "raw ingress and refusals leave the original public target untouched");
}
#endif
bool occurrenceLighting(Run &run, unsigned variant) {
  OccurrenceScene s(run.scene, variant);
  if (!run.bind(s.module))
    return false;
  Frame stable;
  bool first = true;
  auto draw = [&](const char *label, bool cached) {
    s.program->whichChild = s.adapted ? SO_SWITCH_ALL : SO_SWITCH_NONE;
    Frame frame;
    const std::string profile = variant == 0   ? "occurrence-legacy"
                                : variant == 1 ? "occurrence-extended"
                                               : "occurrence-shadow";
    const bool rendered =
        first ? preparationFirst(run, frame, s.scene.root, profile.c_str())
              : run.render(frame, label);
    first = false;
    if (!rendered)
      return false; // Keep the first cause, rather than cascading an unset
                    // stable frame.
    bool ok = occurrenceDelivery(s, run.audit->plan, run.extent);
    ok &= check(
        occurrenceImage(s, frame, run.extent, run.samples, run.window),
        "independent bump/BASE_COLOR colors, alpha, depth and sample coverage");
    ok &= check(
        preparationPhysical(run, frame,
                            [&](const Frame &physical) {
                              return occurrenceImage(s, physical, run.extent,
                                                     run.samples, true, false);
                            }),
        "current same-apply physical occurrence image within five seconds");
    if (cached)
      ok &= check(run.lastCompiles.empty(),
                  "parameter/light/normal edits reuse the same real module");
    if (ok)
      stable = std::move(frame);
    return ok;
  };
  if (!draw("cold local-light bump and unlit module", false))
    return false;
#ifdef COIN_PORTABLE_MODULE_RUST
  if (!run.window && run.samples == 1 && (variant == 0 || variant == 2) &&
      !occurrenceRaw(run, s))
    return false;
#endif
  s.bump.setNormal(variant ? std::array<uint8_t, 3>{{128, 128, 230}}
                           : std::array<uint8_t, 3>{{255, 128, 128}});
  if (!draw("normalmap B affects only its consumer", true))
    return false;
  s.bump.setNormal({{154, 110, 246}});
  if (!draw("normalmap A restoration", true))
    return false;
  const float power = s.lamps[0]->intensity.getValue();
  s.lamps[0]->intensity = power * .4f;
  if (!draw("local light edit leaves zero-light neighbor unlit", true))
    return false;
  s.lamps[0]->intensity = power;
  if (!draw("local light restoration", true))
    return false;
  s.value = Color{{.71f, .19f, .53f, 1}};
  s.parameter->value.setValue(s.value[0], s.value[1], s.value[2], s.value[3]);
  if (!draw("module color B leaves bump unchanged", true))
    return false;
  s.value = Color{{.13f, .57f, .29f, 1}};
  s.parameter->value.setValue(s.value[0], s.value[1], s.value[2], s.value[3]);
  if (!draw("module color A restoration", true))
    return false;
  s.scene.clearParameters();
  s.primary = true;
  s.module = occurrenceModule(true);
  if (!run.bind(s.module) ||
      !draw("PRIMARY observes unlit BASE_COLOR with zero lights", false))
    return false;
  s.adapted = false;
  if (!draw("ordinary BASE_COLOR still has zero real lights", true))
    return false;
  s.adapted = true;
  if (!draw("PRIMARY module restored", true))
    return false;
  s.order(true);
  if (!draw("unlit occurrence precedes local bump lights", true))
    return false;
  s.order(false);
  if (!draw("original occurrence order restored", true))
    return false;
  if (variant == 2) {
    s.casterOutside = true;
    s.caster->translation.setValue(3, 0, .75f);
    if (!draw("last map becomes neutral, attachments still use real MSAA",
              true))
      return false;
    s.casterOutside = false;
    s.caster->translation.setValue(-.48f, 0, .75f);
    if (!draw("last-light real shadow restored", true))
      return false;
  }
  auto refuse = [&](const char *label) {
    const auto entries = run.audit->entries;
    const Frame physical = run.lastPhysical;
    const bool rejected = run.refuse(stable, label);
    return check(run.audit->entries == entries,
                 "invalid consumed bump is rejected before root submission") &&
           untouched(run, stable, physical,
                     "exact invalid occurrence preservation") &&
           rejected;
  };
  s.bumpModel->model = SoLightModel::BASE_COLOR;
  if (!refuse("consumed bump cannot be BASE_COLOR"))
    return false;
  s.bumpModel->model = SoLightModel::PHONG;
  if (!draw("recover consumed PHONG on the same target", true))
    return false;
  s.lights->whichChild = SO_SWITCH_NONE;
  if (!refuse("consumed bump cannot have zero local lights"))
    return false;
  s.lights->whichChild = SO_SWITCH_ALL;
  if (!draw("recover the actual local light set", true))
    return false;
  const SbVec3f direction = s.lamps.back()->direction.getValue();
  s.lamps.back()->direction.setValue(std::numeric_limits<float>::quiet_NaN(), 0,
                                     -1);
  if (!refuse("last consumed local light cannot contain NaN"))
    return false;
  s.lamps.back()->direction = direction;
  if (!draw("last local light recovery", true))
    return false;
  if (run.window)
    for (unsigned extent : {96u, 64u}) {
      if (!check(run.resize(extent), "public occurrence window resize") ||
          !draw("current local state after resize", true))
        return false;
    }
  return true;
}
// This recorder exercises public capture and actual Core/FFI admission only.
// Its capability declarations select capture profiles; it performs no GPU work
// and supplies no image evidence. All image proofs use the real Run above.
class OccurrenceRecorder : public CoinRenderBackend {
public:
  CoinRenderFramePlan frame;
  uint64_t submits = 0;
  std::string error;
  bool isGpuBackend() const override { return true; }
  bool supportsOpaqueBump() const override { return true; }
  bool supportsOffscreenShadows() const override { return true; }
  CoinRenderBackendStatus getStatus() const override {
    return CoinRenderBackendStatus::SUCCESS;
  }
  CoinRenderBackendStatus prepare(CoinRenderTargetP &) override {
    return CoinRenderBackendStatus::SUCCESS;
  }
  CoinRenderSubmitResult submit(const CoinRenderFramePlan &f,
                                CoinRenderTargetP &) override {
    frame = f;
    return {CoinRenderBackendStatus::SUCCESS, "", ++submits};
  }
  void poll() override {}
  const std::string &getLastError() const override { return error; }
};
// Private CPU-only adversarial: a legal shared PHONG state does not imply
// shared bump consumption. Only disjoint vertex/index occurrences decide it.
bool occurrenceSharedState(const OccurrenceScene &scene,
                           const CoinRenderFramePlan &source) {
  std::string error;
  if (!check(source.bumpImages.size() == 1 && source.isValid(&error),
             "shared-state adversarial starts from a valid consumed source: " +
                 error))
    return false;
  const auto nodeId = scene.scene.root->getNodeId();
  const auto programId = scene.scene.program->getNodeId();
  const auto sourceIndices = source.indices;
  const auto sourceBump = source.bumpCoords;
  const auto sourcePixels = source.bumpImages.front().rgb;
  std::vector<uint32_t> sourceStateSlots;
  for (const auto &draw : source.draws)
    sourceStateSlots.push_back(draw.renderStateSlot);
  auto frame = source;
  frame.revision = 0;
  frame.portableShaders.clear();
  for (auto &state : frame.renderStates)
    state.portableShaderSlot = 0;
  std::vector<CoinRenderBumpGpuVertex> stream;
  if (!check(coin_render_build_bump_gpu_stream(frame, stream, error),
             "shared-state input has a valid sparse bump stream: " + error))
    return false;
  std::vector<uint32_t> mainDraws, bumpDraws, ordinaryDraws;
  std::set<uint32_t> vertices[2], indexPositions[2];
  uint32_t commonSlot = UINT32_MAX;
  for (uint32_t i = 0; i < frame.draws.size(); ++i) {
    const auto &draw = frame.draws[i];
    if (draw.shadowLightSlot || !draw.geometry.indexCount)
      continue;
    bool consumed = false, inactive = false;
    for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
      const auto at = draw.geometry.firstIndex + j;
      if (at >= frame.indices.size() || frame.indices[at] >= stream.size())
        return false;
      consumed |= stream[frame.indices[at]].imageSlot != 0;
      inactive |= stream[frame.indices[at]].imageSlot == 0;
    }
    if (!check(
            consumed != inactive,
            "authored occurrences do not mix bump consumption within a draw"))
      return false;
    const unsigned lane = consumed ? 0 : 1;
    mainDraws.push_back(i);
    (consumed ? bumpDraws : ordinaryDraws).push_back(i);
    if (consumed && commonSlot == UINT32_MAX)
      commonSlot = draw.renderStateSlot;
    for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
      const auto at = draw.geometry.firstIndex + j;
      indexPositions[lane].insert(at);
      vertices[lane].insert(frame.indices[at]);
    }
  }
  if (!check(commonSlot < frame.renderStates.size() && !ordinaryDraws.empty() &&
                 !bumpDraws.empty() && indexPositions[0].size() == 6 &&
                 indexPositions[1].size() == 6,
             "private input has two complete disjoint consumed/unconsumed "
             "occurrences"))
    return false;
  for (auto at : indexPositions[0])
    if (!check(!indexPositions[1].count(at),
               "input index ranges remain disjoint"))
      return false;
  for (auto vertex : vertices[0])
    if (!check(!vertices[1].count(vertex),
               "input vertex IDs distinguish bump from ordinary"))
      return false;
  for (auto i : mainDraws)
    frame.draws[i].renderStateSlot = commonSlot;
  if (!check(frame.isValid(&error),
             "same PHONG state is legal for both occurrences: " + error))
    return false;
  const auto &coreState = frame.renderStates[commonSlot];
  const auto &coreLights = frame.lightingStates[coreState.lightingSlot].lights;
  if (!check(coreState.lightModel == CoinRenderLightModel::PHONG &&
                 coreLights.size() == 4 && coreState.shadowGroupSlot != 0,
             "shared-state countercontrol preserves the four-light shadow "
             "profile"))
    return false;
  // Ordinary reference is a separate ordinary-only frame with the exact same
  // authorial state/geometry. Its pre-existing shading/transport policy must
  // survive the coexistence with a bump occurrence; no fixed slot IDs assumed.
  auto reference = frame;
  reference.bumpCoords.clear();
  reference.bumpTangents.clear();
  reference.bumpImages.clear();
  reference.bumpTransforms.clear();
  reference.draws.clear();
  for (uint32_t i = 0; i < frame.draws.size(); ++i)
    if (frame.draws[i].shadowLightSlot ||
        std::find(ordinaryDraws.begin(), ordinaryDraws.end(), i) !=
            ordinaryDraws.end())
      reference.draws.push_back(frame.draws[i]);
  CoinWgpuFfiFrame referenceOwner;
  if (!check(reference.isValid(&error) &&
                 referenceOwner.prepare(reference, 64, 64, error, false),
             "independent ordinary-only reference packs: " + error))
    return false;
  const auto &ordinary = referenceOwner.getView();
  std::array<std::array<double, 2>, 4> expectedCorners;
  for (unsigned c = 0; c < 4; ++c)
    expectedCorners[c] = {{-.48 + .5 * scene.scene.positions->point[c][0],
                           .5 * scene.scene.positions->point[c][1]}};
  std::vector<uint32_t> referenceDraws;
  for (uint32_t i = 0; i < ordinary.draw_count; ++i)
    referenceDraws.push_back(i);
  if (!check(portableFfiQuad(ordinary, referenceDraws, expectedCorners),
             "ordinary-only reference retains the independently authored "
             "shared geometry"))
    return false;

  if (!check(ordinary.draw_count > 0 && ordinary.shadow_map_size == 256,
             "ordinary reference retains geometry and the same real map"))
    return false;
  const uint32_t referenceSlot = ordinary.draws[0].render_state_slot;
  if (!check(referenceSlot < ordinary.state_count &&
                 ordinary.shadow_receivers &&
                 referenceSlot < ordinary.shadow_receiver_count,
             "ordinary reference has a valid receiver association"))
    return false;
  const auto &ordinaryState = ordinary.states[referenceSlot];
  const auto &ordinaryReceiver = ordinary.shadow_receivers[referenceSlot];
  bool ok = check(
      ordinaryState.light_count == coreLights.size() &&
          ordinaryReceiver.lighting_index == 3,
      "reference preserves all light entries and the last-light association");
  for (unsigned order = 0; order < 2; ++order) {
    if (order) {
      std::vector<CoinRenderDrawPacket> reversed;
      for (auto i : mainDraws)
        reversed.push_back(frame.draws[i]);
      std::reverse(reversed.begin(), reversed.end());
      for (size_t i = 0; i < mainDraws.size(); ++i)
        frame.draws[mainDraws[i]] = reversed[i];
    }
    CoinRenderShadowPlan plan;
    if (!check(coin_render_plan_shadows(frame, plan, error) &&
                   plan.passes.size() == 1,
               "shared-state map plans in either order: " + error))
      return false;
    const auto &pass = plan.passes[0];
    ok &=
        check(pass.receiverDraws.size() == mainDraws.size() &&
                  commonSlot < pass.lightingIndexByState.size() &&
                  pass.lightingIndexByState[commonSlot] == 3,
              "Core receiver membership covers both shared-state occurrences");
    for (auto i : mainDraws)
      ok &= check(frame.draws[i].renderStateSlot == commonSlot &&
                      std::find(pass.receiverDraws.begin(),
                                pass.receiverDraws.end(),
                                i) != pass.receiverDraws.end(),
                  "same source state and map receiver remain attached to each "
                  "occurrence");
    CoinWgpuFfiFrame owner;
    if (!check(owner.prepareBumpFrame(frame, 64, 64, error),
               "shared-state mixed packet packs in either order: " + error))
      return false;
    const auto &v = owner.getView();
    unsigned corners[2] = {0, 0};
    std::set<uint32_t> consumedPositions[2];
    std::vector<uint32_t> packedDraws[2];
    for (uint64_t i = 0; i < v.draw_count; ++i) {
      const auto &draw = v.draws[i];
      if (draw.render_state_slot >= v.state_count ||
          uint64_t(draw.first_index) + draw.index_count > v.index_count)
        return false;
      bool consumed = false, inactive = false;
      for (uint32_t j = 0; j < draw.index_count; ++j) {
        const auto vertex = v.indices[draw.first_index + j];
        if (vertex >= v.bump_vertex_count)
          return false;
        consumed |= v.bump_vertices[vertex].image_slot != 0;
        inactive |= v.bump_vertices[vertex].image_slot == 0;
      }
      if (!check(consumed != inactive,
                 "each actual packed draw has one consumption domain"))
        return false;
      const unsigned lane = consumed ? 0 : 1;
      packedDraws[lane].push_back(uint32_t(i));
      const auto &state = v.states[draw.render_state_slot];
      ok &= check(state.light_model == 1 &&
                      state.light_count == coreLights.size() &&
                      state.portable_shader_slot == 0,
                  "packed occurrences retain PHONG/four lights independently "
                  "of consumption");
      for (uint32_t j = 0; j < draw.index_count; ++j) {
        ok &= check(consumedPositions[lane].insert(draw.first_index + j).second,
                    "packed occurrence has no repeated index ranges");
        ++corners[lane];
      }
      if (!v.shadow_receivers ||
          draw.render_state_slot >= v.shadow_receiver_count)
        return false;
      const auto &receiver = v.shadow_receivers[draw.render_state_slot];
      ok &= check(receiver.lighting_index == 3 && (receiver.receives & 1u) &&
                      receiver.max_shadow_distance ==
                          ordinaryReceiver.max_shadow_distance &&
                      receiver.distance_falloff_coefficient ==
                          ordinaryReceiver.distance_falloff_coefficient,
                  "receiver association survives state duplication/rebasing "
                  "for both occurrences");
      for (size_t j = 0; j < coreLights.size() && j < state.light_count; ++j) {
        const auto &light = state.lights[j];
        const auto &from = coreLights[j];
        const auto &plain = ordinaryState.lights[j];
        const bool payload =
            consumed
                ? light.position_type[3] == float(uint32_t(from.type)) &&
                      !std::memcmp(light.color_intensity, from.color,
                                   sizeof(from.color)) &&
                      !std::memcmp(&light.color_intensity[3], &from.intensity,
                                   sizeof(from.intensity)) &&
                      !std::memcmp(light.direction_cutoff, from.direction,
                                   sizeof(from.direction))
                : !std::memcmp(light.position_type, plain.position_type,
                               sizeof(plain.position_type)) &&
                      !std::memcmp(light.direction_cutoff,
                                   plain.direction_cutoff,
                                   sizeof(plain.direction_cutoff)) &&
                      !std::memcmp(light.color_intensity, plain.color_intensity,
                                   sizeof(plain.color_intensity)) &&
                      !std::memcmp(light.attenuation_exponent,
                                   plain.attenuation_exponent,
                                   sizeof(plain.attenuation_exponent));
        ok &= check(payload, "bump keeps every light payload; ordinary keeps "
                             "its separate reference semantics");
        if (!payload)
          std::cout << "occurrence_shared_light order=" << order
                    << " consumed=" << consumed << " draw=" << i
                    << " local_light=" << j
                    << " actual_intensity=" << light.color_intensity[3]
                    << " core_intensity=" << from.intensity
                    << " ordinary_reference_intensity="
                    << plain.color_intensity[3] << std::endl;
      }
    }
    ok &= check(corners[0] == 6 && corners[1] == 6,
                "each shared-state occurrence retains exactly its own six "
                "packed indices");
    for (unsigned lane = 0; lane < 2; ++lane)
      ok &= check(portableFfiQuad(v, packedDraws[lane], expectedCorners),
                  "both consumption domains retain exact independent geometry "
                  "after rebase");
    std::cout << "occurrence_shared_state order=" << order
              << " input_shared_slot=" << commonSlot
              << " packed_states=" << v.state_count
              << " bump_corners=" << corners[0]
              << " ordinary_corners=" << corners[1] << std::endl;
    for (auto at : consumedPositions[0])
      ok &= check(!consumedPositions[1].count(at),
                  "packed bump/ordinary ranges remain disjoint");
  }
  ok &= check(source.indices == sourceIndices &&
                  source.bumpImages.front().rgb == sourcePixels &&
                  source.bumpCoords.size() == sourceBump.size() &&
                  scene.scene.root->getNodeId() == nodeId &&
                  scene.scene.program->getNodeId() == programId,
              "shared-state CPU adversarial leaves original source data and "
              "nodes untouched");
  for (size_t i = 0; i < source.draws.size(); ++i)
    ok &= check(source.draws[i].renderStateSlot == sourceStateSlots[i],
                "private cloning never changes an original draw binding");
  for (size_t i = 0; i < sourceBump.size(); ++i)
    ok &= check(
        source.bumpCoords[i].vertexIndex == sourceBump[i].vertexIndex &&
            source.bumpCoords[i].imageSlot == sourceBump[i].imageSlot &&
            source.bumpCoords[i].transformSlot == sourceBump[i].transformSlot &&
            !std::memcmp(source.bumpCoords[i].coord, sourceBump[i].coord,
                         sizeof(sourceBump[i].coord)),
        "private reordering never mutates original sparse bump inputs");
  return ok;
}
bool occurrenceCore() {
  bool ok = true;
  for (unsigned variant = 0; variant < 3; ++variant) {
    Scene scene;
    OccurrenceScene s(scene, variant);
    std::unique_ptr<CoinRenderTarget> target(
        CoinRenderTarget::createOffscreen(SbVec2i32(64, 64)));
    if (!check(bool(target), "CPU-only declaration target"))
      return false;
    auto *recorder = new OccurrenceRecorder;
    target->getPimpl()->backend.reset(recorder);
    CoinRenderAction action(SbViewportRegion(64, 64));
    action.setTransparencyType(CoinRenderAction::NONE);
    action.setRenderTarget(target.get());
    if (!check(action.setPortableShaderModule(scene.program, s.module),
               "CPU module association"))
      return false;
    const auto sourceId = scene.root->getNodeId();
    const auto programId = scene.program->getNodeId();
    action.apply(scene.root);
    const bool captured =
        check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  recorder->submits == 1,
              "CPU capture of local bump and truly zero-light BASE_COLOR");
    std::cout << "occurrence_core variant=" << variant
              << " status=" << action.getLastStatus()
              << " submits=" << recorder->submits
              << " error=" << action.getLastError().getString() << std::endl;
    ok &= check(scene.root->getNodeId() == sourceId &&
                    scene.program->getNodeId() == programId,
                "CPU capture never mutates its authorial source");
    ok &= captured;
    if (!captured) {
      action.setRenderTarget(nullptr);
      continue;
    }
    ok &= occurrenceDelivery(s, recorder->frame, 64);
    if (variant == 2)
      ok &= occurrenceSharedState(s, recorder->frame);
    const CoinRenderFramePlan good = recorder->frame;
    CoinWgpuFfiFrame ffi;
    std::string error;
    if (!check(ffi.prepareBumpFrame(good, 64, 64, error),
               "CPU valid occurrence FFI predecessor: " + error))
      return false;
    std::set<uint32_t> bumpStates;
    for (const auto &d : good.draws)
      if (!d.shadowLightSlot &&
          good.renderStates[d.renderStateSlot].model[3][0] < 0)
        bumpStates.insert(d.renderStateSlot);
    ok &= check(!bumpStates.empty(),
                "CPU negatives address actually consumed bump occurrences");
    uint32_t bumpDraw = UINT32_MAX;
    for (uint32_t i = 0; i < good.draws.size(); ++i)
      if (!good.draws[i].shadowLightSlot &&
          bumpStates.count(good.draws[i].renderStateSlot)) {
        bumpDraw = i;
        break;
      }
    if (bumpDraw == UINT32_MAX) {
      action.setRenderTarget(nullptr);
      return false;
    }
    for (unsigned fault = 0; fault < 4; ++fault) {
      auto bad = good;
      bad.revision = 0; // Edited private IR has no immutable capture identity.
      for (auto slot : bumpStates) {
        auto &state = bad.renderStates[slot];
        if (fault == 0)
          state.lightModel = CoinRenderLightModel::BASE_COLOR;
        if (fault == 1)
          bad.lightingStates[state.lightingSlot].lights.clear();
      }
      if (fault == 2)
        for (auto &coord : bad.bumpCoords)
          if (coord.imageSlot) {
            coord.imageSlot = uint32_t(bad.bumpImages.size() + 1);
            break;
          }
      if (fault == 3)
        bad.draws[bumpDraw].geometry.firstIndex =
            uint32_t(bad.indices.size() + 1);
      const auto previous = ffi.getView();
      error.clear();
      const bool rejected = !ffi.prepareBumpFrame(bad, 64, 64, error);
      const auto after = ffi.getView();
      ok &= check(
          rejected && !error.empty() &&
              after.frame_revision == previous.frame_revision &&
              after.states == previous.states &&
              after.bump_vertices == previous.bump_vertices &&
              after.portable_modules == previous.portable_modules,
          "actual private malformed consumed bump rejects transactionally");
      ok &= check(ffi.prepareBumpFrame(good, 64, 64, error),
                  "private occurrence recovery");
    }
    const auto before = recorder->submits;
    s.bumpModel->model = SoLightModel::BASE_COLOR;
    action.apply(scene.root);
    ok &= check(
        action.getLastStatus() != CoinRenderAction::SUCCESS &&
            recorder->submits == before,
        "CPU consumed BASE_COLOR bump rejects before recorder submission");
    s.bumpModel->model = SoLightModel::PHONG;
    action.apply(scene.root);
    ok &=
        check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  recorder->submits == before + 1,
              "CPU restores the actual consumer without changing its neighbor");
    action.setRenderTarget(nullptr);
  }
  return ok;
}
#endif
