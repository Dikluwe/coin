    use super::*;

    #[test]
    fn captured_casters_write_front_moments() {
        let required = std::env::var("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU").as_deref() == Ok("1");
        let instance = wgpu::Instance::default();
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::LowPower,
            compatible_surface: None,
            force_fallback_adapter: false,
        }));
        let Some(adapter) = adapter else {
            assert!(!required, "shadow-map GPU adapter required");
            eprintln!("Skipping captured shadow caster test: no adapter");
            return;
        };
        let info = adapter.get_info();
        eprintln!(
            "Coin captured shadow caster adapter: {} ({:?}), driver {}",
            info.name, info.backend, info.driver
        );
        if !adapter
            .get_texture_format_features(wgpu::TextureFormat::Rgba32Float)
            .allowed_usages
            .contains(wgpu::TextureUsages::RENDER_ATTACHMENT)
        {
            assert!(!required, "RGBA32F render attachment required");
            return;
        }
        let (device, queue) = pollster::block_on(adapter.request_device(
            &wgpu::DeviceDescriptor {
                label: Some("Coin captured shadow caster test"),
                required_features: wgpu::Features::empty(),
                required_limits: wgpu::Limits::default(),
                memory_hints: wgpu::MemoryHints::Performance,
            },
            None,
        ))
        .expect("shadow-map GPU device");
        let mut vertices = vec![CoinWgpuVertex::zeroed(); 6];
        for (index, coordinates) in [
            [-0.8, -0.8, 0.5],
            [0.8, -0.8, 0.5],
            [0.0, 0.8, 0.5],
            [-0.8, -0.8, 0.8],
            [0.8, -0.8, 0.8],
            [0.0, 0.8, 0.8],
        ]
        .iter()
        .enumerate()
        {
            vertices[index].position = *coordinates;
        }
        let identity = [
            1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
        ];
        let mut light_view = identity;
        light_view[12] = 0.6;
        let caster = CoinWgpuShadowDraw {
            first_index: 0,
            index_count: 6,
            render_state_slot: 0,
            reserved: 0,
            model_view: light_view,
            model_view_projection: identity,
        };
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("Coin shadow caster test"),
        });
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.model_view = identity;
        state.clip_plane_count = 1;
        state.clip_planes[0] = [1.0, 0.0, 0.0, 0.0];
        let map = encode_moments(
            &device,
            &mut encoder,
            &vertices,
            &[0, 1, 2, 3, 4, 5],
            &[caster],
            &[state],
            64,
            0.1,
            1.1,
            1,
        )
        .expect("encode captured casters");
        let readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Coin captured shadow readback"),
            size: 64 * 64 * 16,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: &map._moments,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &readback,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(64 * 16),
                    rows_per_image: Some(64),
                },
            },
            wgpu::Extent3d {
                width: 64,
                height: 64,
                depth_or_array_layers: 1,
            },
        );
        queue.submit(Some(encoder.finish()));
        let slice = readback.slice(..);
        let (tx, rx) = std::sync::mpsc::channel();
        slice.map_async(wgpu::MapMode::Read, move |result| tx.send(result).unwrap());
        device.poll(wgpu::Maintain::Wait);
        rx.recv().unwrap().expect("map captured shadow moments");
        let data = slice.get_mapped_range();
        let offset = (32 * 64 + 32) * 16;
        let mean = f32::from_ne_bytes(data[offset..offset + 4].try_into().unwrap());
        let square = f32::from_ne_bytes(data[offset + 4..offset + 8].try_into().unwrap());
        let clipped_offset = (32 * 64 + 24) * 16;
        let kept_offset = (32 * 64 + 40) * 16;
        let clipped_mean = f32::from_ne_bytes(data[clipped_offset..clipped_offset + 4].try_into().unwrap());
        let kept_mean = f32::from_ne_bytes(data[kept_offset..kept_offset + 4].try_into().unwrap());
        assert!(clipped_mean > 0.99, "clipped moments were written: {clipped_mean}");
        assert!(kept_mean < 0.99, "retained caster disappeared: {kept_mean}");
        let expected = (0.6f32 * 0.6 + 0.5 * 0.5).sqrt() - 0.1;
        assert!(
            (mean - expected).abs() < 0.02,
            "mean={mean}, expected={expected}"
        );
        assert!(
            (square - expected * expected).abs() < 0.03,
            "square={square}, expected={}",
            expected * expected
        );
    }
