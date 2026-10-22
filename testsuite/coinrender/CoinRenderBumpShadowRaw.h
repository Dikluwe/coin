#ifndef COIN_RENDER_BUMP_SHADOW_RAW_H
#define COIN_RENDER_BUMP_SHADOW_RAW_H
#if defined(COIN_BUMP_SHADOW_TEST_RUST)
// Declaration supplied as the common A/B interface by C. No ABI struct mirror.
extern "C" CoinWgpuStatus
coin_wgpu_preflight_bump_receiver(uint32_t samples, uint64_t vertices,
                                  uint64_t materials, uint32_t image_axis,
                                  uint32_t sampling_policy);

namespace bump_shadow {
inline bool raw(Scene &scene, Harness &h, unsigned samples) {
  const auto frame = h.audit->plan;
  CoinWgpuFfiFrame owner;
  std::string diagnostic;
  if (!check(owner.prepareBumpFrame(frame, side, side, diagnostic),
             "raw combined public capture packs " + diagnostic))
    return false;
  const auto original = owner.getView();
  bool ok =
      check(original.abi_version == COIN_WGPU_ABI_VERSION && original.bump_image_count == 1 &&
                original.shadow_map_size && original.shadow_receiver_count,
            "raw positive contains actual simultaneous packets");
  const uint32_t policy =
      samplingTestPolicy == COIN_RENDER_SAMPLING_PORTABLE ? 1u : 0u;
  ok &= check(coin_wgpu_preflight_bump_receiver(samples, original.vertex_count,
                                                original.material_count, 1,
                                                policy) == COIN_WGPU_OK,
              "real receiver preflight admits exact current cardinalities");
  for (unsigned fault = 0; fault < 5; ++fault) {
    const auto status = coin_wgpu_preflight_bump_receiver(
        fault == 0 ? 3 : samples,
        fault == 1 ? UINT64_MAX : original.vertex_count,
        fault == 2 ? UINT64_MAX : original.material_count,
        fault == 3 ? UINT32_MAX : 1, fault == 4 ? 999 : policy);
    std::cout << "raw_receiver_preflight fault=" << fault
              << " status=" << status << '\n';
    ok &= check(status != COIN_WGPU_OK,
                "invalid domain/overflow/physical limit refuses in preflight");
  }
  Frame output;
  output.rgba.assign(side * side * 4, 0x5a);
  output.depth.assign(side * side, .987f);
  CoinWgpuTarget target{};
  target.width = target.height = side;
  target.sample_count = samples;
  target.color_buffer = output.rgba.data();
  target.color_buffer_len = output.rgba.size();
  target.depth_buffer = output.depth.data();
  target.depth_buffer_len = output.depth.size();
  char error[2048] = {};
  auto submit = [&](const CoinWgpuFrameView &view, const char *phase) {
    error[0] = 0;
    const auto before = target.submission_serial;
    const auto status = coin_wgpu_submit(&target, &view, error, sizeof(error));
    std::cout << "raw_combined phase=" << phase << " status=" << status
              << " serial=" << before << ',' << target.submission_serial
              << " error=" << error << '\n';
    return check(status == COIN_WGPU_OK && target.submission_serial > before,
                 "valid raw combined submission");
  };
  ok &= submit(original, "original") && oracle(scene, output);
  const auto stableRGBA = output.rgba;
  const auto stableDepth = output.depth;
  uint32_t receiverState = UINT32_MAX;
  for (uint64_t d = 0; d < original.draw_count; ++d) {
    const auto &draw = original.draws[d];
    for (uint64_t v = draw.first_vertex;
         v < uint64_t(draw.first_vertex) + draw.vertex_count; ++v)
      if (v < original.bump_vertex_count &&
          original.bump_vertices[v].image_slot == 1) {
        receiverState = draw.render_state_slot;
        break;
      }
    if (receiverState != UINT32_MAX)
      break;
  }
  if (!check(
          receiverState < original.state_count &&
              original.states[receiverState].light_count == 1,
          "raw mutations target the actual authored bump receiver state/light"))
    return false;
  for (unsigned fault = 0; fault < 12; ++fault) {
    auto malformed = original;
    std::vector<CoinWgpuBumpVertex> bump(original.bump_vertices,
                                         original.bump_vertices +
                                             original.bump_vertex_count);
    std::vector<CoinWgpuBumpImage> images(
        original.bump_images, original.bump_images + original.bump_image_count);
    std::vector<CoinWgpuVertex> vertices(
        original.vertices, original.vertices + original.vertex_count);
    std::vector<CoinWgpuSampler> samplers(
        original.samplers, original.samplers + original.sampler_count);
    std::vector<CoinWgpuRenderState> states(
        original.states, original.states + original.state_count);
    if (fault == 0) {
      bump[0].image_slot = uint32_t(images.size() + 1);
      malformed.bump_vertices = bump.data();
    }
    if (fault == 1) {
      --images[0].rgb_len;
      malformed.bump_images = images.data();
    }
    if (fault == 2) {
      bump[0].coord[0] = std::numeric_limits<float>::quiet_NaN();
      malformed.bump_vertices = bump.data();
    }
    if (fault == 3) {
      vertices[0].extra_texcoords[6][2] =
          std::numeric_limits<float>::infinity();
      malformed.vertices = vertices.data();
    }
    if (fault == 4) {
      samplers[0].reserved |= 32u;
      malformed.samplers = samplers.data();
    }
    if (fault == 5) {
      states[receiverState].lights[0].color_intensity[3] =
          std::numeric_limits<float>::quiet_NaN();
      malformed.states = states.data();
    }
    if (fault == 6) {
      malformed.bump_image_count = 0;
    }
    if (fault == 7) {
      states[receiverState].fog_mode = 1;
      states[receiverState].fog_start = 0;
      states[receiverState].fog_end = 10;
      malformed.states = states.data();
    }
    if (fault == 8) {
      states[receiverState].clip_plane_count = 1;
      states[receiverState].clip_planes[0][0] = 1;
      malformed.states = states.data();
    }
    if (fault == 9) {
      states[receiverState].depth_range[0] = .2f;
      states[receiverState].depth_range[1] = .8f;
      malformed.states = states.data();
    }
    if (fault == 10)
      malformed.rasterization_policy = 1;
    std::vector<CoinWgpuMaterial> materials(
        original.materials, original.materials + original.material_count);
    if (fault == 11) {
      for (auto &material : materials) {
        material.transparency = .5f;
        material.diffuse[3] = .5f;
      }
      malformed.materials = materials.data();
    }
    const auto serial = target.submission_serial;
    error[0] = 0;
    const auto status =
        coin_wgpu_submit(&target, &malformed, error, sizeof(error));
    std::cout << "raw_combined_malformed fault=" << fault
              << " status=" << status << " error=" << error << '\n';
    ok &= check((status == COIN_WGPU_INVALID_ARGUMENT ||
                 status == COIN_WGPU_UNSUPPORTED) &&
                    error[0] && target.submission_serial == serial &&
                    output.rgba == stableRGBA && output.depth == stableDepth,
                "private malformed packet refuses atomically without "
                "old-color/depth/serial corruption");
    ok &= submit(original, "malformed recovery") &&
          check(output.rgba == stableRGBA && output.depth == stableDepth,
                "private malformed recovery exact within same device");
  }
  // ABI62 appends the already-clipped top-left map scissor to each caster.
  // Reuse the current revision throughout: cached packets must still validate
  // flags and bounds before changing publication or encoding a shadow pass.
  if (!check(original.shadow_caster_count && original.shadow_map_size >= 16,
             "raw scissor fixture contains real map casters"))
    return false;
  const int32_t mapSide = int32_t(original.shadow_map_size);
  auto scissorView = original;
  std::vector<CoinWgpuShadowDraw> scissorCasters(
      original.shadow_casters,
      original.shadow_casters + original.shadow_caster_count);
  scissorView.shadow_casters = scissorCasters.data();
  auto setScissor = [&](int32_t x, int32_t y, int32_t width, int32_t height) {
    for (auto &caster : scissorCasters) {
      caster.reserved = 0;
      caster.viewport[0] = x;
      caster.viewport[1] = y;
      caster.viewport[2] = width;
      caster.viewport[3] = height;
    }
  };
  auto scissorRecovery = [&]() {
    return submit(original, "raw ABI67 scissor original recovery") &&
           oracle(scene, output) &&
           check(output.rgba == stableRGBA && output.depth == stableDepth,
                 "raw scissor mutation restores original color/depth exactly");
  };
  for (unsigned fault = 0; fault < 11; ++fault) {
    setScissor(0, 0, mapSide, mapSide);
    auto &caster = scissorCasters[0];
    if (fault == 0)
      caster.reserved = 1;
    if (fault == 1)
      caster.reserved = 0x80000000u;
    if (fault == 2)
      caster.viewport[0] = -1;
    if (fault == 3)
      caster.viewport[1] = -1;
    if (fault == 4)
      caster.viewport[2] = -1;
    if (fault == 5)
      caster.viewport[3] = -1;
    if (fault == 6) {
      caster.viewport[0] = INT32_MAX;
      caster.viewport[2] = INT32_MAX;
    }
    if (fault == 7) {
      caster.viewport[1] = INT32_MAX;
      caster.viewport[3] = INT32_MAX;
    }
    if (fault == 8) {
      caster.viewport[0] = mapSide - 1;
      caster.viewport[2] = 2;
    }
    if (fault == 9) {
      caster.viewport[1] = mapSide - 1;
      caster.viewport[3] = 2;
    }
    if (fault == 10) {
      caster.viewport[0] = mapSide + 1;
      caster.viewport[2] = 0;
    }
    const auto serial = target.submission_serial;
    error[0] = 0;
    const auto status =
        coin_wgpu_submit(&target, &scissorView, error, sizeof(error));
    std::cout << "raw_ABI67_scissor_invalid fault=" << fault
              << " status=" << status << " error=" << error << '\n';
    ok &= check(
        status == COIN_WGPU_INVALID_ARGUMENT && error[0] &&
            target.submission_serial == serial && output.rgba == stableRGBA &&
            output.depth == stableDepth &&
            scissorView.frame_revision == original.frame_revision,
        "unknown flags/invalid scissor refuse atomically at fixed revision");
    ok &= scissorRecovery();
  }
  auto obsolete = original;
  obsolete.abi_version = 61;
  const auto oldSerial = target.submission_serial;
  error[0] = 0;
  ok &= check(coin_wgpu_submit(&target, &obsolete, error, sizeof(error)) ==
                      COIN_WGPU_INVALID_ARGUMENT &&
                  error[0] && target.submission_serial == oldSerial &&
                  output.rgba == stableRGBA && output.depth == stableDepth,
              "obsolete ABI61 cannot alias the appended scissor layout");
  ok &= scissorRecovery();
  setScissor(0, 0, mapSide, mapSide);
  ok &= submit(scissorView, "raw explicit full-map scissor") &&
        oracle(scene, output);
  ok &= scissorRecovery();
  setScissor(0, 0, mapSide / 2, mapSide);
  if (submit(scissorView, "raw half-map scissor independently observable")) {
    unsigned left = 0, right = 0, bad = 0;
    for (unsigned y = 0; y < side; ++y)
      for (unsigned x = 0; x < side; ++x) {
        const double wx = (x + .5) * 4 / side - 2;
        const double wy = 2 - (y + .5) * 4 / side;
        if (std::abs(wy) >= .14 || std::abs(wx) <= .08 || std::abs(wx) >= .16)
          continue;
        const bool shadow = wx < 0;
        left += shadow;
        right += !shadow;
        const auto expected = scene.predicted(x, y, shadow ? 0 : 1);
        const size_t pixel = size_t(y) * side + x;
        for (unsigned channel = 0; channel < 3; ++channel)
          bad += std::abs(int(output.rgba[4 * pixel + channel]) -
                          int(std::lround(expected[channel]))) > 3;
        bad += output.rgba[4 * pixel + 3] != 255;
        bad += std::abs(output.depth[pixel] - .375f) > 1e-6;
      }
    std::cout << "raw_ABI67_half_scissor left=" << left << " right=" << right
              << " bad=" << bad << '\n';
    ok &= check(left >= 8 && right >= 8 && bad == 0,
                "half-map scissor preserves left shadow and clears right "
                "before sampling");
  } else {
    ok = false;
  }
  ok &= scissorRecovery();
  for (unsigned boundary : {0u, 1u}) {
    setScissor(boundary ? mapSide : 0, boundary ? mapSide : 0, 0, 0);
    ok &= submit(scissorView, "raw legal empty map scissor") &&
          oracle(scene, output, true, false);
    ok &= scissorRecovery();
  }
  const auto originalNormal = scene.normal;
  const std::array<uint8_t, 3> variants[] = {{{210, 145, 205}},
                                             {{80, 200, 240}}};
  for (unsigned mutation = 0; mutation < 2; ++mutation) {
    auto changed = original;
    std::vector<CoinWgpuBumpImage> images(
        original.bump_images, original.bump_images + original.bump_image_count);
    const auto color = variants[mutation];
    std::vector<uint8_t> rgb(color.begin(), color.end());
    if (mutation)
      rgb.insert(rgb.end(), color.begin(), color.end());
    images[0].rgb = rgb.data();
    images[0].rgb_len = rgb.size();
    if (mutation)
      images[0].width = 2;
    changed.bump_images = images.data();
    scene.setNormal(color);
    ok &= check(changed.frame_revision == original.frame_revision,
                "raw valid payload/dimension mutation retains exact frame "
                "revision") &&
          submit(changed, mutation ? "same revision valid image dimensions"
                                   : "same revision valid image bytes") &&
          oracle(scene, output);
    scene.setNormal(originalNormal);
    ok &= submit(original, "same revision original recovery") &&
          oracle(scene, output) &&
          check(output.rgba == stableRGBA && output.depth == stableDepth,
                "cache proof never exposes stale changed/original normal data");
  }
  uint32_t slot = UINT32_MAX;
  for (uint64_t i = 0; i < original.state_count; ++i)
    if (original.states[i].extra_textures[6].enabled) {
      slot = original.states[i].extra_textures[6].texture_slot;
      break;
    }
  if (!check(slot < original.texture_count &&
                 original.textures[slot].format == 11 &&
                 original.textures[slot].pixels &&
                 original.textures[slot].pixel_bytes_len,
             "raw unit seven owns real CPU-authored volume atlas bytes"))
    return false;
  for (unsigned mutation = 0; mutation < 2; ++mutation) {
    auto changed = original;
    std::vector<CoinWgpuTexture> textures(
        original.textures, original.textures + original.texture_count);
    const RGB authored = mutation ? RGB{{180, 40, 90}} : RGB{{36, 80, 120}};
    std::vector<uint8_t> bytes;
    if (!mutation) {
      bytes.assign(textures[slot].pixels,
                   textures[slot].pixels + textures[slot].pixel_bytes_len);
      // Source alpha is deliberately 255 for every logical voxel and zero
      // for padding. Preserve padding and all metadata exactly.
      for (size_t p = 0; p < bytes.size(); p += 4)
        if (bytes[p + 3] == 255)
          for (unsigned c = 0; c < 3; ++c)
            bytes[p + c] = uint8_t(authored[c]);
    } else {
      // A second fully valid logical volume: 1x1x1, no lower levels/padding.
      bytes = {uint8_t(authored[0]), uint8_t(authored[1]), uint8_t(authored[2]),
               255};
      textures[slot].width = textures[slot].height = 1;
      textures[slot].reserved = 0; // exact (X-1,Y-1,Z-1), no mips.
    }
    textures[slot].pixels = bytes.data();
    textures[slot].pixel_bytes_len = bytes.size();
    changed.textures = textures.data();
    scene.customTexel[7] = authored;
    for (auto &v : scene.customTexel[7]) {
      v /= 255;
      if (scene.srgb)
        v = bump_profile::decode(v);
    }
    ok &=
        check(changed.frame_revision == original.frame_revision &&
                  textures[slot].content_digest ==
                      original.textures[slot].content_digest,
              "volume payload/dimension mutation keeps exact revision AND "
              "digest") &&
        submit(changed, mutation ? "same revision/digest second logical volume"
                                 : "same revision/digest volume payload") &&
        oracle(scene, output);
    scene.customTexel[7] = {{-1, -1, -1}};
    ok &= submit(original, "same revision/digest volume A/B/A") &&
          oracle(scene, output) &&
          check(output.rgba == stableRGBA && output.depth == stableDepth,
                "volume cache license validates effective payload and full "
                "metadata before reuse");
  }

  // Preserve declared NONE alpha as DATA. RGB depends on it through public
  // combine operands; forcing alpha=1 is therefore observably incorrect.
  Frame publicBefore;
  h.target->readbackRGBA(publicBefore.rgba);
  h.target->readbackDepth(publicBefore.depth);
  auto *authoring = new SoGroup;
  auto *select = new SoTextureUnit;
  select->unit = 3;
  authoring->addChild(select);
  auto *combine = new SoTextureCombine;
  authoring->addChild(combine);
  combine->rgbOperation = SoTextureCombine::MODULATE;
  const int args[] = {SoTextureCombine::PREVIOUS, SoTextureCombine::TEXTURE};
  combine->rgbSource.setValues(0, 2, args);
  combine->rgbOperand.set1Value(0, SoTextureCombine::SRC_COLOR);
  combine->alphaOperation = SoTextureCombine::REPLACE;
  combine->alphaSource.set1Value(0, SoTextureCombine::PREVIOUS);
  auto *unitZero = new SoTextureUnit;
  unitZero->unit = 0;
  authoring->addChild(unitZero);
  scene.group->insertChild(authoring, scene.group->findChild(scene.mesh));
  for (bool inverse : {false, true}) {
    combine->rgbOperand.set1Value(1, inverse
                                         ? SoTextureCombine::ONE_MINUS_SRC_ALPHA
                                         : SoTextureCombine::SRC_ALPHA);
    scene.customTexel[3] = inverse ? RGB{{0, 0, 0}} : RGB{{1, 1, 1}};
    Frame publicOpaque;
    ok &= h.render(scene.root, publicOpaque,
                   "public opaque alpha-operand source for raw NONE") &&
          oracle(scene, publicOpaque);
    CoinWgpuFfiFrame alphaOwner;
    ok &= check(
        alphaOwner.prepareBumpFrame(h.audit->plan, side, side, diagnostic),
        "authored alpha combine transports without private instruction "
        "mirroring");
    const auto alphaOriginal = alphaOwner.getView();
    uint32_t state = UINT32_MAX;
    for (uint64_t d = 0; d < alphaOriginal.draw_count; ++d) {
      const auto &draw = alphaOriginal.draws[d];
      for (uint64_t v = draw.first_vertex;
           v < uint64_t(draw.first_vertex) + draw.vertex_count; ++v)
        if (v < alphaOriginal.bump_vertex_count &&
            alphaOriginal.bump_vertices[v].image_slot == 1) {
          state = draw.render_state_slot;
          break;
        }
      if (state != UINT32_MAX)
        break;
    }
    if (!check(state < alphaOriginal.state_count,
               "raw alpha operand resolves actual receiver state"))
      return false;
    const uint32_t textureSlot =
        alphaOriginal.states[state].extra_textures[2].texture_slot;
    if (!check(
            textureSlot < alphaOriginal.texture_count &&
                alphaOriginal.textures[textureSlot].format == 0 &&
                !(alphaOriginal.textures[textureSlot].reserved & 0x80000000u),
            "normal public SoTexture2 never gains the RTT NONE declaration"))
      return false;
    auto view = alphaOriginal;
    std::vector<CoinWgpuTexture> textures(alphaOriginal.textures,
                                          alphaOriginal.textures +
                                              alphaOriginal.texture_count);
    uint8_t rgba[] = {51, 102, 179, 64};
    textures[textureSlot].pixels = rgba;
    textures[textureSlot].pixel_bytes_len = 4;
    textures[textureSlot].width = textures[textureSlot].height = 1;
    view.textures = textures.data();
    auto refuse = [&](const char *phase) {
      const auto beforeColor = output.rgba;
      const auto beforeDepth = output.depth;
      const auto serial = target.submission_serial;
      error[0] = 0;
      const auto status =
          coin_wgpu_submit(&target, &view, error, sizeof(error));
      std::cout << "raw_NONE_refusal phase=" << phase << " status=" << status
                << " error=" << error << '\n';
      return check((status == COIN_WGPU_INVALID_ARGUMENT ||
                    status == COIN_WGPU_UNSUPPORTED) &&
                       error[0] && serial == target.submission_serial &&
                       beforeColor == output.rgba &&
                       beforeDepth == output.depth,
                   "raw NONE scope/domain refusal preserves all published "
                   "bytes/depth/serial");
    };
    textures[textureSlot].reserved = 0;
    ok &= refuse("alpha64 ordinary image without declaration");
    textures[textureSlot].reserved = 0x80000000u;
    for (unsigned alpha : {64u, 191u}) {
      rgba[3] = uint8_t(alpha);
      const double factor = inverse ? 1 - alpha / 255. : alpha / 255.;
      scene.customTexel[3] = {{factor, factor, factor}};
      ok &= check(view.frame_revision == alphaOriginal.frame_revision &&
                      textures[textureSlot].content_digest ==
                          alphaOriginal.textures[textureSlot].content_digest,
                  "raw NONE alpha mutation keeps frame revision and texture "
                  "digest exact") &&
            submit(view, inverse ? "NONE preserves ONE_MINUS_SRC_ALPHA"
                                 : "NONE preserves SRC_ALPHA") &&
            oracle(scene, output);
    }
    textures[textureSlot].reserved |= 0x40000000u;
    ok &= refuse("unknown bit30 alongside valid NONE bit31");
    textures[textureSlot].reserved = 0;
    ok &= refuse("declaration removed with same revision/digest/pixels");
    scene.customTexel[3] = inverse ? RGB{{0, 0, 0}} : RGB{{1, 1, 1}};
    ok &=
        submit(alphaOriginal, "ordinary opaque image after NONE cache entry") &&
        oracle(scene, output) &&
        check(
            output.rgba == publicOpaque.rgba &&
                output.depth == publicOpaque.depth,
            "NONE cache state never leaks into ordinary SoTexture2 transport");
  }
  scene.group->removeChild(authoring);
  scene.customTexel[3] = {{-1, -1, -1}};
  Frame publicRestored;
  ok &= h.render(scene.root, publicRestored,
                 "public alpha-operand scope A/B/A") &&
        oracle(scene, publicRestored) && exact(publicBefore, publicRestored);
  ok &= submit(original, "raw original after NONE scope/cache controls") &&
        oracle(scene, output) &&
        check(output.rgba == stableRGBA && output.depth == stableDepth,
              "raw NONE scope restores original packet exactly");
  // A GPU token is not itself evidence of opacity. This source has an
  // alpha-zero clear outside its geometry, and submit_texture has no CPU
  // output buffers. Only the explicit NONE declaration admits this resource.
  CoinWgpuTarget producerTarget{};
  producerTarget.width = producerTarget.height = side;
  producerTarget.sample_count = samples;
  uint64_t token = 0;
  error[0] = 0;
  const auto producerStatus = coin_wgpu_submit_texture(
      &producerTarget, &original, &token, error, sizeof(error));
  std::cout << "raw_GPU_NONE_source status=" << producerStatus
            << " token=" << token
            << " generation=" << coin_wgpu_default_device_generation()
            << " error=" << error << '\n';
  ok &= check(producerStatus == COIN_WGPU_OK && token &&
                  coin_wgpu_default_device_generation() &&
                  !producerTarget.color_buffer && !producerTarget.depth_buffer,
              "actual same-device combined source produces GPU token without "
              "CPU readback");
  if (token) {
    auto gpuView = original;
    std::vector<CoinWgpuTexture> gpuTextures(
        original.textures, original.textures + original.texture_count);
    std::vector<CoinWgpuVertex> gpuVertices(
        original.vertices, original.vertices + original.vertex_count);
    const uint32_t slot =
        original.states[receiverState].extra_textures[2].texture_slot;
    if (check(slot < gpuTextures.size(),
              "GPU NONE adversary resolves actual unit-three binding")) {
      auto &texture = gpuTextures[slot];
      texture.width = texture.height = side;
      texture.format = 1;
      texture.reserved = 0;
      texture.pixels = nullptr;
      texture.pixel_bytes_len = 0;
      texture.content_digest = token;
      // The four nearest source texels around (.5,.5) lie strictly inside
      // the closed caster shadow, where epsilon=threshold=0 implies V=0.
      // Both diffuse and specular are therefore exactly zero independently
      // of the normal map. No observed output is used as the expected texel.
      for (auto &vertex : gpuVertices) {
        vertex.extra_texcoords[2][0] = .5f;
        vertex.extra_texcoords[2][1] = .5f;
        vertex.extra_texcoords[2][2] = 0;
        vertex.extra_texcoords[2][3] = 1;
      }
      gpuView.vertices = gpuVertices.data();
      gpuView.textures = gpuTextures.data();
      auto refuseGpu = [&](const char *phase) {
        const auto oldColor = output.rgba;
        const auto oldDepth = output.depth;
        const auto serial = target.submission_serial;
        error[0] = 0;
        const auto status =
            coin_wgpu_submit(&target, &gpuView, error, sizeof(error));
        std::cout << "raw_GPU_NONE_refusal phase=" << phase
                  << " status=" << status << " error=" << error << '\n';
        return check((status == COIN_WGPU_INVALID_ARGUMENT ||
                      status == COIN_WGPU_UNSUPPORTED) &&
                         error[0] && target.submission_serial == serial &&
                         output.rgba == oldColor && output.depth == oldDepth,
                     "unproven/stale GPU opacity refuses before publication "
                     "and cached reuse");
      };
      ok &= refuseGpu("real token without opacity proof or NONE");
      texture.reserved = 0x80000000u;
      scene.customTexel[3] = {{0, 0, 0}};
      ok &= check(gpuView.frame_revision == original.frame_revision &&
                      texture.content_digest == token,
                  "GPU NONE positive changes only declaration at fixed "
                  "revision/token") &&
            submit(gpuView, "real token with authorial NONE") &&
            oracle(scene, output);
      texture.reserved = 0;
      ok &=
          refuseGpu("NONE removed at same revision and token after cache hit");
      texture.reserved = 0x80000000u;
      coin_wgpu_release_texture(token);
      token = 0;
      ok &= refuseGpu("released real token with NONE remains stale");
      scene.customTexel[3] = {{-1, -1, -1}};
      ok &= submit(original, "GPU NONE original resource recovery") &&
            oracle(scene, output) &&
            check(output.rgba == stableRGBA && output.depth == stableDepth,
                  "GPU NONE rejection and retirement recover original "
                  "color/depth exactly");
    } else {
      ok = false;
    }
    if (token)
      coin_wgpu_release_texture(token);
  }
  return ok;
}
} // namespace bump_shadow
#endif
#endif
