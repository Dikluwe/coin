// R27 physical probe: format/sample support, color resolve, depth attachment,
// and observable partial coverage. No production target is enabled here.
#[test]
#[ignore = "requires a selected physical GPU; run with --ignored"]
fn rgba8_depth32_resolve_1_2_4_samples() {
    use wgpu::TextureFormatFeatureFlags as Flags;
    let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
        backends: wgpu::Backends::from_env().unwrap_or(wgpu::Backends::all()),
        ..Default::default()
    });
    let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
        power_preference: wgpu::PowerPreference::HighPerformance,
        compatible_surface: None, force_fallback_adapter: false,
    })).expect("MSAA physical adapter");
    eprintln!("msaa_probe_adapter {:?}", adapter.get_info());
    let color_features = adapter.get_texture_format_features(wgpu::TextureFormat::Rgba8Unorm);
    let depth_features = adapter.get_texture_format_features(wgpu::TextureFormat::Depth32Float);
    for (samples, flag) in [(2, Flags::MULTISAMPLE_X2), (4, Flags::MULTISAMPLE_X4)] {
        assert!(color_features.flags.contains(flag) && depth_features.flags.contains(flag),
            "RGBA8 and Depth32Float must both support {samples} samples");
    }
    assert!(adapter.features().contains(
        wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES),
        "2x requires texture adapter-specific format features");
    let (device, queue) = pollster::block_on(adapter.request_device(
        &wgpu::DeviceDescriptor {
            required_features: wgpu::Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES,
            ..Default::default()
        }, None)).expect("MSAA device");
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("Coin R27 resolve probe"),
        source: wgpu::ShaderSource::Wgsl(r#"
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
  let xy = array<vec2<f32>,3>(vec2<f32>(-0.9,-0.9),
                             vec2<f32>(0.9,-0.9), vec2<f32>(-0.9,0.4));
  return vec4<f32>(xy[i], 0.5, 1.0);
}
@fragment fn fs() -> @location(0) vec4<f32> { return vec4<f32>(1.0,0.0,0.0,1.0); }
"#.into()),
    });
    let mut partial_counts = Vec::new();
    for samples in [1u32,2,4] {
        let resolved = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Coin R27 resolved RGBA8"),
            size: wgpu::Extent3d { width: 16, height: 16, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let resolved_view = resolved.create_view(&Default::default());
        let multisampled = (samples > 1).then(|| device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Coin R27 multisampled RGBA8"),
            size: resolved.size(), mip_level_count: 1, sample_count: samples,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        }));
        let multisampled_view = multisampled.as_ref().map(|texture|
            texture.create_view(&Default::default()));
        let depth = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Coin R27 multisampled depth"),
            size: resolved.size(), mip_level_count: 1, sample_count: samples,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Depth32Float,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });
        let depth_view = depth.create_view(&Default::default());
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Coin R27 sample-count probe"),
            layout: None,
            vertex: wgpu::VertexState { module: &shader, entry_point: Some("vs"),
                buffers: &[], compilation_options: Default::default() },
            primitive: Default::default(),
            depth_stencil: Some(wgpu::DepthStencilState {
                format: wgpu::TextureFormat::Depth32Float,
                depth_write_enabled: true, depth_compare: wgpu::CompareFunction::Less,
                stencil: Default::default(), bias: Default::default(),
            }),
            multisample: wgpu::MultisampleState { count: samples, ..Default::default() },
            fragment: Some(wgpu::FragmentState { module: &shader, entry_point: Some("fs"),
                targets: &[Some(wgpu::ColorTargetState {
                    format: wgpu::TextureFormat::Rgba8Unorm,
                    blend: None, write_mask: wgpu::ColorWrites::ALL,
                })], compilation_options: Default::default() }),
            multiview: None, cache: None,
        });
        let readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Coin R27 resolved readback"), size: 16 * 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let mut encoder = device.create_command_encoder(&Default::default());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Coin R27 color/depth pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: multisampled_view.as_ref().unwrap_or(&resolved_view),
                    resolve_target: multisampled_view.as_ref().map(|_| &resolved_view),
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: &depth_view,
                    depth_ops: Some(wgpu::Operations {
                        load: wgpu::LoadOp::Clear(1.0), store: wgpu::StoreOp::Store,
                    }), stencil_ops: None,
                }),
                timestamp_writes: None, occlusion_query_set: None,
            });
            pass.set_pipeline(&pipeline);
            pass.draw(0..3, 0..1);
        }
        encoder.copy_texture_to_buffer(resolved.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &readback,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(16) } },
            resolved.size());
        queue.submit([encoder.finish()]);
        let (sender, receiver) = std::sync::mpsc::channel();
        readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::Maintain::Wait);
        receiver.recv_timeout(std::time::Duration::from_secs(20))
            .expect("MSAA map callback").expect("MSAA map result");
        let mapped = readback.slice(..).get_mapped_range();
        let mut partial = 0usize;
        let mut red_sum = 0u64;
        let mut solid = 0usize;
        for row in 0..16 {
            for column in 0..16 {
                let offset = row * 256 + column * 4;
                let red = mapped[offset];
                red_sum += u64::from(red);
                partial += usize::from(red > 0 && red < 255);
                solid += usize::from(red == 255);
                assert_eq!(mapped[offset + 3], 255, "opaque clear/fragment alpha");
            }
        }
        drop(mapped);
        readback.unmap();
        eprintln!("msaa_probe samples={samples} partial={partial} solid={solid} red_sum={red_sum}");
        assert!(solid > 0 && red_sum > 0, "triangle must be visible");
        partial_counts.push(partial);
    }
    assert_eq!(partial_counts[0], 0, "one-sample output has binary coverage");
    assert!(partial_counts[1] > 0 && partial_counts[2] > 0,
        "2x and 4x resolve must expose partial coverage");
}
