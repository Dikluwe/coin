// Included within antecedents. One Image node, byte array and inherited
// texture generators are shared; each execution occurrence owns its raw UV.
SoNormal *historyNormal(SoSwitch *toggle, const SbVec3f &value) {
  auto *branch = static_cast<SoSeparator *>(toggle->getChild(0));
  SoNormal *normal = nullptr;
  for (int i = 0; i < branch->getNumChildren(); ++i) {
    auto *node = branch->getChild(i);
    if (node->isOfType(SoNormal::getClassTypeId())) {
      normal = static_cast<SoNormal *>(node); normal->vector.setValue(value);
    }
    if (node->isOfType(SoCoordinate3::getClassTypeId()))
      static_cast<SoCoordinate3 *>(node)->point.setValue(0, -20, 0);
  }
  return normal;
}
bool occurrenceDelivery(const Scene &scene, const CoinRenderFramePlan &plan,
                        const std::array<std::array<float, 4>, 2> &expected, bool overlap) {
  CoinWgpuFfiFrame ffi; std::string error;
  if (!check(ffi.prepare(plan, side, side, error, false), "shared Image private transport: " + error)) return false;
  std::vector<uint32_t> slots;
  for (const auto &draw : plan.draws)
    if (draw.sourceNodeId == scene.image->getNodeId() &&
        std::find(slots.begin(), slots.end(), draw.renderStateSlot) == slots.end()) slots.push_back(draw.renderStateSlot);
  if (!check(slots.size() == 2, "same Image has two independently owned raster occurrence states")) return false;
  std::vector<std::pair<uint32_t,uint32_t>> occurrences;
  for(const auto &draw:plan.draws)if(draw.sourceNodeId==scene.image->getNodeId())
    occurrences.emplace_back(draw.frameNodeOrdinal,draw.renderStateSlot);
  std::sort(occurrences.begin(),occurrences.end());
  occurrences.erase(std::unique(occurrences.begin(),occurrences.end()),occurrences.end());
  bool found[2]{}, ok = check(occurrences.size()==2 && occurrences[0].first!=occurrences[1].first,
                             "shared Image is identified by two actual frame occurrence ordinals");
  if(occurrences.size()!=2)return false;
  for(uint32_t slot:slots)
    if(!check(slot<plan.renderStates.size() && plan.renderStates[slot].rasterTextureSourceSlot<plan.renderStates.size(),
              "shared Image occurrence/source state is addressable"))return false;
  for(unsigned n=0;n<2;++n)for(unsigned c=0;c<4;++c)
    ok &= check(plan.renderStates[occurrences[n].second].rasterTextureCoordinates[7][c]==expected[n][c],
                "normal-derived raw coordinate belongs to the correct frameNodeOrdinal");
  if(overlap){
    const auto &a=plan.renderStates[plan.renderStates[slots[0]].rasterTextureSourceSlot];
    const auto &b=plan.renderStates[plan.renderStates[slots[1]].rasterTextureSourceSlot];
    const auto ua=coin_render_texture_unit(a,7),ub=coin_render_texture_unit(b,7);
    ok &= check(a.model==b.model && a.cameraSlot==b.cameraSlot && ua.imageSlot==ub.imageSlot &&
                ua.samplerSlot==ub.samplerSlot && ua.matrix==ub.matrix,
                "overlapped shared Image has identical model/camera/inherited texture state");
  }
  for (uint32_t slot : slots) {
    if (!check(slot < plan.renderStates.size() && slot < ffi.getView().state_count, "shared occurrence Core/FFI state")) return false;
    const auto &state = plan.renderStates[slot]; const auto &packed = ffi.getView().states[slot];
    ok &= check(state.rasterPixels && state.rasterTextureSourceSlot < plan.renderStates.size() &&
                packed.raster_texture_source_slot == state.rasterTextureSourceSlot + 1,
                "each shared Image occurrence owns a valid inherited source state");
    for (unsigned n = 0; n < 2; ++n) {
      bool matches = true;
      for (unsigned c = 0; c < 4; ++c) {
        matches &= state.rasterTextureCoordinates[7][c] == expected[n][c];
        ok &= check(packed.raster_texture_coordinates[7][c] == state.rasterTextureCoordinates[7][c],
                    "shared occurrence raw UV is preserved exactly through FFI");
      }
      found[n] |= matches;
    }
  }
  return check(found[0] && found[1], "both independently emitted normals reach the same Image in one frame") && ok;
}
bool occurrenceImage(const Frame &frame, bool secondZ, bool overlap) {
  if (!check(frame.rgba.size() == size_t(side * side * 4) && frame.depth.size() == size_t(side * side),
             "shared Image complete framebuffer")) return false;
  // NORMAL_MAP and authored matrix: X selects texel(1,0), Y(0,1), Z(0,0).
  const int expected[2][3] = {{70,30,55},{20,secondZ ? 30 : 70,secondZ ? 40 : 55}};
  unsigned bad = 0;
  for (int occurrence = overlap ? 1 : 0; occurrence < 2; ++occurrence)
    for (int y = 24; y < 40; ++y) for (int x = overlap ? 24 : 8 + occurrence * 32; x < (overlap ? 40 : 24 + occurrence * 32); ++x) {
      const size_t p = size_t(y * side + x);
      for (int c = 0; c < 3; ++c) bad += std::abs(int(frame.rgba[4 * p + c]) - expected[occurrence][c]) > 1;
      bad += frame.rgba[4 * p + 3] != 255;
      bad += std::abs(frame.depth[p] - float(9. / 29.)) > 1e-6f;
    }
  return check(bad == 0, "both shared Image footprints have independently authored RGB/alpha/depth");
}
bool sharedOccurrences(bool overlap) {
  Harness h; if (!h.target) return false;
  Scene scene(2, 7, true, false, true);
  scene.model->translation.setValue(0,0,0); scene.model->rotation.setValue(SbRotation::identity());
  scene.model->scaleFactor.setValue(1,1,1);
  scene.image->ref(); scene.root->removeChild(scene.image);
  if(overlap){auto *depth=new SoDepthBuffer;depth->function=SoDepthBuffer::ALWAYS;scene.root->addChild(depth);}
  SoNormal *second = nullptr; SoSwitch *array = pointHistory(true);
  for (unsigned n = 0; n < 2; ++n) {
    auto *branch = new SoSeparator; auto *move = new SoTransform;
    move->translation.setValue(overlap ? 0 : n ? 16 : -16, 0, 0); branch->addChild(move);
    auto *emit = pointHistory(false); emit->whichChild = SO_SWITCH_ALL;
    auto *normal = historyNormal(emit, n ? SbVec3f(0,1,0) : SbVec3f(1,0,0));
    if (n) second = normal;
    branch->addChild(emit); if (n) branch->addChild(array);
    branch->addChild(scene.image); scene.root->addChild(branch);
  }
  scene.image->unref();
  if (!check(second != nullptr, "second OVERALL emitter exists")) return false;
  auto draw = [&](bool secondZ, const char *label) {
    const std::array<std::array<float,4>,2> expected = {{{{1,0,0,2}},{{0,secondZ ? 0.f : 1.f,secondZ ? 1.f : 0.f,2}}}};
    return h.apply(scene,label) && occurrenceDelivery(scene,h.backend->plan,expected,overlap) && occurrenceImage(h.read(),secondZ,overlap);
  };
  if (!draw(false,"shared Image X/Y first apply")) return false;
  const Frame a = h.read();
  second->vector.setValue(0,0,1); bool ok = draw(true,"shared Image X/Z");
  second->vector.setValue(0,1,0); ok &= draw(false,"shared Image X/Y restored");
  Frame back = h.read(); ok &= check(a.rgba == back.rgba && a.depth == back.depth,"shared Image full A/B/A exact");
  array->whichChild = SO_SWITCH_ALL;
  ok &= h.refuse(scene,"Unknown in second shared occurrence refuses before either Image publishes");
  array->whichChild = SO_SWITCH_NONE; ok &= draw(false,"shared Image first valid recovery");
  back=h.read();ok &= check(a.rgba==back.rgba && a.depth==back.depth,"shared Image refusal preserves exact recovery");
  Scene retained(2,7,true,false,true);
  const bool persistent=h.apply(retained,"last executed shared occurrence retains normalY");ok &= persistent;
  if(persistent)ok &= delivery(retained,h.backend->plan,false,{{0,1,0}});
  return ok;
}
