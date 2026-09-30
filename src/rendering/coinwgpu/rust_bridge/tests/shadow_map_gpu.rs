use wgpu::util::DeviceExt;

#[test]
fn spot_moment_pass_writes_linear_distance() {
    let instance = wgpu::Instance::default();
    let require_gpu = std::env::var("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU").as_deref() == Ok("1");
    let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
        power_preference: wgpu::PowerPreference::LowPower,
        compatible_surface: None,
        force_fallback_adapter: false,
    }));
    let Some(adapter) = adapter else {
        assert!(!require_gpu, "wgpu adapter required for shadow-map test");
        eprintln!("Skipping shadow-map GPU test: no adapter");
        return;
    };
    let info = adapter.get_info();
    eprintln!(
        "Coin shadow-map test adapter: {} ({:?}), driver {}",
        info.name, info.backend, info.driver
    );
    let format = wgpu::TextureFormat::Rgba32Float;
    if !adapter
        .get_texture_format_features(format)
        .allowed_usages
        .contains(wgpu::TextureUsages::RENDER_ATTACHMENT)
    {
        assert!(
            !require_gpu,
            "RGBA32F render attachment required for shadow-map test"
        );
        eprintln!("Skipping shadow-map GPU test: RGBA32F render attachment unavailable");
        return;
    }
    let (device, queue) = pollster::block_on(adapter.request_device(
        &wgpu::DeviceDescriptor {
            label: Some("Coin shadow-map test"),
            required_features: wgpu::Features::empty(),
            required_limits: wgpu::Limits::default(),
            memory_hints: wgpu::MemoryHints::Performance,
        },
        None,
    ))
    .expect("wgpu device for shadow-map test");

    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_shadow.wgsl"),
        source: wgpu::ShaderSource::Wgsl(include_str!("../../shaders/coin_shadow.wgsl").into()),
    });
    let layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("Coin shadow uniforms"),
        entries: &[wgpu::BindGroupLayoutEntry {
            binding: 0,
            visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
            ty: wgpu::BindingType::Buffer {
                ty: wgpu::BufferBindingType::Uniform,
                has_dynamic_offset: false,
                min_binding_size: None,
            },
            count: None,
        }],
    });
    let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("Coin shadow-map test pipeline"),
        bind_group_layouts: &[&layout],
        push_constant_ranges: &[],
    });
    let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("Coin shadow-map test"),
        layout: Some(&pipeline_layout),
        vertex: wgpu::VertexState {
            module: &shader,
            entry_point: Some("vs_moments"),
            compilation_options: wgpu::PipelineCompilationOptions::default(),
            buffers: &[wgpu::VertexBufferLayout {
                array_stride: 12,
                step_mode: wgpu::VertexStepMode::Vertex,
                attributes: &[wgpu::VertexAttribute {
                    format: wgpu::VertexFormat::Float32x3,
                    offset: 0,
                    shader_location: 0,
                }],
            }],
        },
        fragment: Some(wgpu::FragmentState {
            module: &shader,
            entry_point: Some("fs_moments"),
            compilation_options: wgpu::PipelineCompilationOptions::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format,
                blend: None,
                write_mask: wgpu::ColorWrites::ALL,
            })],
        }),
        primitive: wgpu::PrimitiveState {
            cull_mode: None,
            ..Default::default()
        },
        depth_stencil: Some(wgpu::DepthStencilState {
            format: wgpu::TextureFormat::Depth32Float,
            depth_write_enabled: true,
            depth_compare: wgpu::CompareFunction::Less,
            stencil: wgpu::StencilState::default(),
            bias: wgpu::DepthBiasState::default(),
        }),
        multisample: wgpu::MultisampleState::default(),
        multiview: None,
        cache: None,
    });

    let identity: [f32; 16] = [
        1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
    ];
    let mut light_view = identity;
    light_view[12] = 0.6;
    let mut uniforms = Vec::from(identity);
    uniforms.extend(light_view);
    uniforms.extend([0.0, 1.0, 1.0, 0.0]); // near, far, spot
    let uniform = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some("Coin shadow test uniform"),
        contents: bytemuck::cast_slice(&uniforms),
        usage: wgpu::BufferUsages::UNIFORM,
    });
    let bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("Coin shadow test bindings"),
        layout: &layout,
        entries: &[wgpu::BindGroupEntry {
            binding: 0,
            resource: uniform.as_entire_binding(),
        }],
    });
    let vertices: [f32; 9] = [-0.8, -0.8, 0.5, 0.8, -0.8, 0.5, 0.0, 0.8, 0.5];
    let vertex = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some("Coin shadow test triangle"),
        contents: bytemuck::cast_slice(&vertices),
        usage: wgpu::BufferUsages::VERTEX,
    });
    let extent = wgpu::Extent3d {
        width: 64,
        height: 64,
        depth_or_array_layers: 1,
    };
    let moments = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("Coin shadow test moments"),
        size: extent,
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
        view_formats: &[],
    });
    let depth = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("Coin shadow test depth"),
        size: extent,
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Depth32Float,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
        view_formats: &[],
    });
    let readback = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("Coin shadow test readback"),
        size: 64 * 64 * 16,
        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
        mapped_at_creation: false,
    });
    let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label: Some("Coin shadow test encoder"),
    });
    {
        let color_view = moments.create_view(&Default::default());
        let depth_view = depth.create_view(&Default::default());
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("Coin shadow test pass"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: &color_view,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color::WHITE),
                    store: wgpu::StoreOp::Store,
                },
            })],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view: &depth_view,
                depth_ops: Some(wgpu::Operations {
                    load: wgpu::LoadOp::Clear(1.0),
                    store: wgpu::StoreOp::Store,
                }),
                stencil_ops: None,
            }),
            timestamp_writes: None,
            occlusion_query_set: None,
        });
        pass.set_pipeline(&pipeline);
        pass.set_bind_group(0, &bind_group, &[]);
        pass.set_vertex_buffer(0, vertex.slice(..));
        pass.draw(0..3, 0..1);
    }
    encoder.copy_texture_to_buffer(
        wgpu::TexelCopyTextureInfo {
            texture: &moments,
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
        extent,
    );
    queue.submit(Some(encoder.finish()));
    let slice = readback.slice(..);
    let (tx, rx) = std::sync::mpsc::channel();
    slice.map_async(wgpu::MapMode::Read, move |result| tx.send(result).unwrap());
    device.poll(wgpu::Maintain::Wait);
    rx.recv().unwrap().expect("map shadow moments");
    let data = slice.get_mapped_range();
    let index = (32 * 64 + 32) * 16;
    let mean = f32::from_ne_bytes(data[index..index + 4].try_into().unwrap());
    let square = f32::from_ne_bytes(data[index + 4..index + 8].try_into().unwrap());
    let expected = (0.6f32 * 0.6 + 0.5 * 0.5).sqrt();
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
