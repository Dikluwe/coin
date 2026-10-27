    use super::*;

    #[test]
    fn shadow_alpha_surface_ingress_rejects_malformed_state_before_device() {
        let surface_id = u64::MAX - 1; // nonzero, deliberately absent runtime surface
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.alpha_test_function = 7;
        state.alpha_test_reference = 0.25;
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        frame.multipass_count = 1;
        frame.multipass_reserved = 0;
        frame.multipass_budget_bytes = 256 * 1024 * 1024;
        frame.multipass_matrices = std::ptr::null();
        frame.multipass_matrix_count = 0;
        frame.abi_version = COIN_WGPU_ABI_VERSION;
        frame.struct_size = std::mem::size_of::<CoinWgpuFrameView>() as u32;
        frame.width = 4;
        frame.height = 4;
        frame.states = &state;
        frame.state_count = 1;
        let serial_before = coin_wgpu_surface_submission_serial(surface_id);
        assert_eq!(serial_before, 0);
        for (function, reference) in [
            (9, 0.25), (u32::MAX, 0.25),
            (7, f32::NAN), (7, f32::INFINITY), (7, f32::NEG_INFINITY),
            (7, -0.01), (7, 1.01),
        ] {
            state.alpha_test_function = function;
            state.alpha_test_reference = reference;
            let mut error = [0 as std::os::raw::c_char; 256];
            let status = coin_wgpu_surface_submit(
                surface_id, &frame, error.as_mut_ptr(), error.len());
            let diagnostic = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }
                .to_string_lossy();
            assert_eq!(status, CoinWgpuStatus::InvalidArgument,
                "function={function} reference={reference} diagnostic={diagnostic}");
            assert!(diagnostic.contains("alpha"), "{diagnostic}");
            assert_eq!(coin_wgpu_surface_submission_serial(surface_id), serial_before);
        }
        state.alpha_test_function = 7;
        state.alpha_test_reference = 0.25;
        let mut error = [0 as std::os::raw::c_char; 256];
        let _ = coin_wgpu_surface_submit(
            surface_id, &frame, error.as_mut_ptr(), error.len());
        let diagnostic = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }
            .to_string_lossy();
        assert!(!diagnostic.contains("alpha"),
            "valid active alpha was rejected by alpha ingress: {diagnostic}");
        assert_eq!(coin_wgpu_surface_submission_serial(surface_id), serial_before);
    }

    #[test]
    fn cube_atlas_direct_submit_validates_before_same_revision_reuse() {
        // Exercise the Rust submit boundary itself: the C++ frame packer is not
        // involved, and every adversarial submit reuses the published revision.
        let mut device = 0u64;
        let mut error = [0 as std::os::raw::c_char; 512];
        assert_eq!(coin_wgpu_device_create(&mut device, error.as_mut_ptr(), error.len()),
            CoinWgpuStatus::Ok);
        assert_ne!(device, 0);
        let mut color = [0x5au8; 4 * 4 * 4];
        let mut depth = [0.375f32; 4 * 4];
        let mut target = CoinWgpuTarget {
            width: 4, height: 4, color_buffer: color.as_mut_ptr(),
            color_buffer_len: color.len() as u64,
            depth_buffer: depth.as_mut_ptr(), depth_buffer_len: depth.len() as u64,
            submission_serial: 0, device_id: device, sample_count: 1,
        };
        let mut vertices = [CoinWgpuVertex::zeroed(); 3];
        vertices[0].position = [-0.75, -0.75, 0.0];
        vertices[1].position = [0.75, -0.75, 0.0];
        vertices[2].position = [0.0, 0.75, 0.0];
        for vertex in &mut vertices { vertex.texcoord = [1.0, 0.0, 0.0, 1.0]; }
        let indices = [0u32, 1, 2];
        let mut draw = CoinWgpuDraw::zeroed();
        draw.topology = 0;
        draw.vertex_count = 3;
        draw.index_count = 3;
        draw.stable_node_id = 0x2402;
        draw.source_revision = 1;
        let mut material: CoinWgpuMaterial = unsafe { std::mem::zeroed() };
        material.diffuse = [1.0; 4];
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.color_write = 1;
        state.polygon_offset_primitive_style = 1;
        for diagonal in [0, 5, 10, 15] {
            state.model_view[diagonal] = 1.0;
            state.model_view_projection[diagonal] = 1.0;
            state.normal_matrix[diagonal] = 1.0;
        }
        state.has_texture = 1;
        state.texture_model = 1; // REPLACE: the sampled cube color is observable.
        state.texture_matrix[0] = 1.0;
        state.texture_matrix[5] = 1.0;
        state.texture_matrix[10] = 1.0;
        state.texture_matrix[15] = 1.0;
        state.depth_range = [0.0, 1.0];
        let sampler = CoinWgpuSampler { wrap_s: 1, wrap_t: 1, filter: 1,
            reserved: 1 << 4 }; // Cube also clamps R; low nibble stays anisotropy-1=0.
        // Six 4x4 faces, three lower mip levels: 126 texels in an 8x16
        // physical atlas. Its last two texels are required zero padding.
        let mut pixels = [0u8; 8 * 16 * 4];
        for texel in pixels[..126 * 4].chunks_exact_mut(4) {
            texel.copy_from_slice(&[47, 139, 211, 255]);
        }
        let mut texture = CoinWgpuTexture {
            width: 8, height: 16, format: 12,
            reserved: 2 | (4 << 4) | (1 << 7),
            content_digest: 0x24_02_01,
            pixels: pixels.as_ptr(), pixel_bytes_len: pixels.len() as u64,
        };
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        frame.multipass_count = 1;
        frame.multipass_reserved = 0;
        frame.multipass_budget_bytes = 256 * 1024 * 1024;
        frame.multipass_matrices = std::ptr::null();
        frame.multipass_matrix_count = 0;
        frame.abi_version = COIN_WGPU_ABI_VERSION;
        frame.struct_size = std::mem::size_of::<CoinWgpuFrameView>() as u32;
        frame.frame_revision = 41;
        frame.width = 4;
        frame.height = 4;
        frame.clear_color = [0.1, 0.2, 0.3, 1.0];
        frame.vertices = vertices.as_ptr(); frame.vertex_count = vertices.len() as u64;
        frame.indices = indices.as_ptr(); frame.index_count = indices.len() as u64;
        frame.draws = &draw; frame.draw_count = 1;
        frame.materials = &material; frame.material_count = 1;
        frame.states = &state; frame.state_count = 1;
        frame.textures = &texture; frame.texture_count = 1;
        frame.samplers = &sampler; frame.sampler_count = 1;
        let submit = |target: &mut CoinWgpuTarget, frame: &CoinWgpuFrameView| {
            let mut message = [0 as std::os::raw::c_char; 512];
            let status = coin_wgpu_submit(target, frame, message.as_mut_ptr(), message.len());
            let diagnostic = unsafe { std::ffi::CStr::from_ptr(message.as_ptr()) }
                .to_string_lossy().into_owned();
            (status, diagnostic)
        };
        let (status, diagnostic) = submit(&mut target, &frame);
        assert_eq!(status, CoinWgpuStatus::Ok, "valid cube: {diagnostic}");
        // Interior of the authored triangle in the top-down 4x4 readback.
        let interior = (2 * 4 + 1) * 4;
        for (channel, expected) in [47u8, 139, 211].iter().enumerate() {
            assert!((i16::from(color[interior + channel]) - i16::from(*expected)).abs() <= 2,
                "cube sample channel={channel} actual={} expected={expected}",
                color[interior + channel]);
        }
        assert_eq!(color[interior + 3], 255);
        let stable_color = color;
        let stable_depth = depth;
        let stable_serial = target.submission_serial;
        assert!(stable_serial > 0);

        let mut reject = |label: &str, texture: &CoinWgpuTexture,
                          vertices: &[CoinWgpuVertex; 3]| {
            // The C ABI view is a borrowed pointer aggregate; copying its
            // bytes leaves every backing fixture alive for this call.
            let mut candidate = unsafe { std::ptr::read(&frame) };
            candidate.textures = texture;
            candidate.vertices = vertices.as_ptr();
            let (status, diagnostic) = submit(&mut target, &candidate);
            assert_eq!(status, CoinWgpuStatus::InvalidArgument,
                "{label}: {diagnostic}");
            assert_eq!(target.submission_serial, stable_serial, "{label}: serial");
            assert_eq!(color, stable_color, "{label}: RGBA");
            assert_eq!(depth, stable_depth, "{label}: depth");
        };
        texture.reserved |= 1 << 8;
        reject("reserved high bit", &texture, &vertices);
        texture.reserved &= !(1 << 8);
        texture.width = 4;
        reject("physical width", &texture, &vertices);
        texture.width = 8;
        texture.height = 15;
        reject("physical height", &texture, &vertices);
        texture.height = 16;
        texture.pixel_bytes_len -= 4;
        reject("truncated bytes", &texture, &vertices);
        texture.pixel_bytes_len += 4;
        pixels[126 * 4] = 1;
        texture.pixels = pixels.as_ptr();
        reject("nonzero atlas padding", &texture, &vertices);
        pixels[126 * 4] = 0;
        let mut invalid_vertices = vertices;
        for vertex in &mut invalid_vertices { vertex.texcoord = [0.0; 4]; }
        reject("zero cube direction", &texture, &invalid_vertices);
        invalid_vertices = vertices;
        invalid_vertices[1].texcoord[2] = f32::NAN;
        reject("nonfinite cube direction", &texture, &invalid_vertices);
        drop(reject);
        // Cube lookup uses STR after the matrix; finite Q does not divide it.
        for q in [0.0, -1.0] {
            for vertex in &mut vertices { vertex.texcoord[3] = q; }
            let (status, diagnostic) = submit(&mut target, &frame);
            assert_eq!(status, CoinWgpuStatus::Ok, "cube q={q}: {diagnostic}");
        }
        assert_eq!(coin_wgpu_device_destroy(device), CoinWgpuStatus::Ok);
    }
    #[test]
    fn mip_payload_layout_limits() {
        assert_eq!(texture_levels(4,2,2).unwrap(),vec![(4,2,0),(2,1,32),(1,1,40)]);
        assert_eq!(texture_levels(1,1,2).unwrap(),vec![(1,1,0)]);
        assert_eq!(texture_levels(3,5,0).unwrap(),vec![(3,5,0)]);
        // Format 11 is a single physical RGBA8 atlas; its spatial 3D mip
        // levels are packed inside this one 2D payload.
        assert_eq!(texture_levels(4,4,11).unwrap(),vec![(4,4,0)]);
        assert_eq!(texture_levels(8,12,11).unwrap(),vec![(8,12,0)]);
        // Format 12 transports one physical RGBA8 cube atlas. Spatial levels
        // are encoded by its reserved metadata and checked at submit ingress.
        assert_eq!(texture_levels(4,4,12).unwrap(),vec![(4,4,0)]);
        assert_eq!(texture_levels(8,16,12).unwrap(),vec![(8,16,0)]);
        assert_eq!(8 * 16 * 4, 512);
        for &(w,h,f) in &[(8192,8192,2),(0,8,2),(8193,1,0),(4,4,13)] {
            assert!(texture_levels(w,h,f).is_err());
        }
    }

    #[test]
    fn viewport_defaults_empty_and_bounds() {
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        assert_eq!(resolved_viewport(&state, 64, 32).unwrap(), [0,0,64,32]);
        state.viewport = [0,0,0,1];
        assert_eq!(resolved_viewport(&state, 64, 32).unwrap(), [0,0,0,1]);
        state.viewport = [2,3,60,29];
        assert_eq!(resolved_viewport(&state, 64, 32).unwrap(), [2,3,60,29]);
        for invalid in [[-1,0,32,32],[0,0,65,32],[0,0,0,2],[0,0,32,-1],
                        [i32::MAX,0,i32::MAX,32]] {
            state.viewport = invalid;
            assert!(resolved_viewport(&state, 64, 32).is_err());
        }
    }


    #[test]
    fn abi62_shadow_scissor_domain_is_checked_without_gpu_resources() {
        assert_eq!(std::mem::size_of::<CoinWgpuShadowDraw>(), 160);
        let mut caster: CoinWgpuShadowDraw = unsafe { std::mem::zeroed() };
        let offset = std::ptr::addr_of!(caster.viewport) as usize
            - std::ptr::addr_of!(caster) as usize;
        assert_eq!(offset, 144, "append-only scissor layout");
        caster.index_count = 3;
        for diagonal in [0, 5, 10, 15] {
            caster.model_view[diagonal] = 1.0;
            caster.model_view_projection[diagonal] = 1.0;
        }
        let mut receiver: CoinWgpuShadowReceiver = unsafe { std::mem::zeroed() };
        for diagonal in [0, 5, 10, 15] {
            receiver.model_view[diagonal] = 1.0;
            receiver.model_view_projection[diagonal] = 1.0;
        }
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        frame.multipass_count = 1;
        frame.multipass_reserved = 0;
        frame.multipass_budget_bytes = 256 * 1024 * 1024;
        frame.multipass_matrices = std::ptr::null();
        frame.multipass_matrix_count = 0;
        frame.abi_version = 70; // Independent current protocol assertion.
        frame.struct_size = std::mem::size_of::<CoinWgpuFrameView>() as u32;
        frame.shadow_map_size = 64;
        frame.shadow_kind = 1;
        frame.shadow_near_distance = 1.0;
        frame.shadow_far_distance = 10.0;
        frame.shadow_epsilon = 0.00001;
        frame.shadow_threshold = 0.1;
        frame.shadow_casters = &caster;
        frame.shadow_caster_count = 1;
        frame.shadow_receivers = &receiver;
        frame.shadow_receiver_count = 1;
        // This entry point validates borrowed metadata; it has no device,
        // target, acquisition or allocation argument and performs no encoding.
        for viewport in [[0, 0, 64, 64], [7, 11, 31, 29], [0, 0, 0, 0],
                         [64, 64, 0, 0], [64, 0, 0, 64], [0, 64, 64, 0]] {
            caster.viewport = viewport;
            let mut error = [0 as std::os::raw::c_char; 256];
            assert!(validate_shadow_frame(&frame, 1, false, error.as_mut_ptr(), error.len()).is_ok(),
                    "legal full/subrect/empty scissor {viewport:?}");
        }
        for viewport in [[-1, 0, 64, 64], [0, -1, 64, 64], [0, 0, -1, 64],
                         [0, 0, 64, -1], [63, 0, 2, 64], [0, 63, 64, 2],
                         [65, 0, 0, 0], [0, 65, 0, 0],
                         [i32::MAX, 0, i32::MAX, 1], [0, i32::MAX, 1, i32::MAX]] {
            caster.viewport = viewport;
            let mut error = [0 as std::os::raw::c_char; 256];
            assert!(matches!(validate_shadow_frame(&frame, 1, false, error.as_mut_ptr(), error.len()),
                             Err(CoinWgpuStatus::InvalidArgument)),
                    "invalid/overflow/unclipped scissor {viewport:?}");
            assert_ne!(error[0], 0);
        }
        caster.viewport = [7, 11, 31, 29];
        for reserved in [1u32, 2, 0x80000000, u32::MAX] {
            caster.reserved = reserved;
            let mut error = [0 as std::os::raw::c_char; 256];
            assert!(matches!(validate_shadow_frame(&frame, 1, false, error.as_mut_ptr(), error.len()),
                             Err(CoinWgpuStatus::InvalidArgument)),
                    "unknown flags {reserved:x} cannot alias viewport semantics");
        }
        caster.reserved = 0;
        let mut error = [0 as std::os::raw::c_char; 256];
        assert!(validate_shadow_frame(&frame, 1, false, error.as_mut_ptr(), error.len()).is_ok(),
                "valid scissor recovery after malformed metadata");
    }
