// Physical backend control for R27.3: two passes share one 4x attachment.
// The first stores red everywhere without resolving. The second loads that
// attachment, draws green into the left half only, and resolves once at end.
#[test]
#[ignore = "requires a selected physical GPU; run with --ignored"]
fn four_sample_load_scissor_preserves_outside_on_final_resolve() {
    use wgpu::TextureFormatFeatureFlags as Flags;
    let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
        backends: wgpu::Backends::from_env().unwrap_or(wgpu::Backends::all()),
        ..Default::default()
    });
    let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
        power_preference: wgpu::PowerPreference::HighPerformance,
        compatible_surface: None,
        force_fallback_adapter: false,
    })).expect("physical MSAA adapter");
    let info = adapter.get_info();
    eprintln!("msaa_partial_scissor backend={:?} vendor={:#x} device={:#x} type={:?} name={}",
        info.backend, info.vendor, info.device, info.device_type, info.name);
    assert_ne!(info.device_type, wgpu::DeviceType::Cpu, "GPU execution is required");
    let features = adapter.get_texture_format_features(wgpu::TextureFormat::Rgba8Unorm);
    assert!(features.flags.contains(Flags::MULTISAMPLE_X4), "RGBA8 must support 4x");
    let (device, queue) = pollster::block_on(adapter.request_device(
        &wgpu::DeviceDescriptor::default(), None)).expect("MSAA device");
    let extent = wgpu::Extent3d { width: 8, height: 8, depth_or_array_layers: 1 };
    let msaa = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("partial-scissor 4x color"), size: extent,
        mip_level_count: 1, sample_count: 4, dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Rgba8Unorm,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT, view_formats: &[],
    });
    let resolved = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("partial-scissor final resolve"), size: extent,
        mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Rgba8Unorm,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
        view_formats: &[],
    });
    let msaa_view = msaa.create_view(&Default::default());
    let resolved_view = resolved.create_view(&Default::default());
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("partial-scissor fullscreen green"),
        source: wgpu::ShaderSource::Wgsl(r#"
@vertex fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
  let p = array<vec2<f32>, 3>(vec2<f32>(-1.0,-1.0),
                              vec2<f32>(3.0,-1.0), vec2<f32>(-1.0,3.0));
  return vec4<f32>(p[index], 0.0, 1.0);
}
@fragment fn fs() -> @location(0) vec4<f32> {
  return vec4<f32>(0.0,1.0,0.0,1.0);
}
"#.into()),
    });
    let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("partial-scissor 4x green"), layout: None,
        vertex: wgpu::VertexState { module: &shader, entry_point: Some("vs"),
            buffers: &[], compilation_options: Default::default() },
        primitive: Default::default(), depth_stencil: None,
        multisample: wgpu::MultisampleState { count: 4, ..Default::default() },
        fragment: Some(wgpu::FragmentState { module: &shader, entry_point: Some("fs"),
            targets: &[Some(wgpu::ColorTargetState {
                format: wgpu::TextureFormat::Rgba8Unorm,
                blend: None, write_mask: wgpu::ColorWrites::ALL,
            })], compilation_options: Default::default() }),
        multiview: None, cache: None,
    });
    let readback = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("partial-scissor readback"), size: 8 * 256,
        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
        mapped_at_creation: false,
    });
    let mut encoder = device.create_command_encoder(&Default::default());
    {
        let _pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("red first pass; no resolve"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: &msaa_view, resolve_target: None,
                ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color {
                    r: 1.0, g: 0.0, b: 0.0, a: 1.0,
                }), store: wgpu::StoreOp::Store },
            })], depth_stencil_attachment: None,
            timestamp_writes: None, occlusion_query_set: None,
        });
    }
    {
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("green scissored second pass; final resolve"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: &msaa_view, resolve_target: Some(&resolved_view),
                ops: wgpu::Operations { load: wgpu::LoadOp::Load,
                    store: wgpu::StoreOp::Store },
            })], depth_stencil_attachment: None,
            timestamp_writes: None, occlusion_query_set: None,
        });
        pass.set_pipeline(&pipeline);
        pass.set_scissor_rect(0, 0, 4, 8);
        pass.draw(0..3, 0..1);
    }
    encoder.copy_texture_to_buffer(resolved.as_image_copy(),
        wgpu::TexelCopyBufferInfo { buffer: &readback,
            layout: wgpu::TexelCopyBufferLayout { offset: 0,
                bytes_per_row: Some(256), rows_per_image: Some(8) } }, extent);
    queue.submit([encoder.finish()]);
    let (sender, receiver) = std::sync::mpsc::channel();
    readback.slice(..).map_async(wgpu::MapMode::Read, move |status| {
        let _ = sender.send(status);
    });
    let _ = device.poll(wgpu::Maintain::Wait);
    receiver.recv_timeout(std::time::Duration::from_secs(20))
        .expect("MSAA readback callback").expect("MSAA readback result");
    let mapped = readback.slice(..).get_mapped_range();
    for y in 0..8usize {
        for x in 0..8usize {
            let offset = y * 256 + x * 4;
            let observed = &mapped[offset..offset + 4];
            let expected: &[u8] = if x < 4 { &[0,255,0,255] } else { &[255,0,0,255] };
            assert_eq!(observed, expected,
                "partial resolve must preserve the loaded color outside scissor at ({x},{y})");
        }
    }
    drop(mapped);
    readback.unmap();
}
