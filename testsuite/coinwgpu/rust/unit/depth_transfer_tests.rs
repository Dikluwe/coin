    use super::*;

    #[test]
    fn exact_depth_shader_translates_to_glsl() {
        super::super::shader_profile::tests::assert_glsl(super::SHADER, "fs_main");
        super::super::shader_profile::tests::assert_glsl(super::SHADER, "fs_depth");
    }

    #[test]
    fn conversion_preserves_depth_bits_and_padded_rows() {
        let instance = Instance::new(&InstanceDescriptor {
            backends: Backends::from_env().unwrap_or(Backends::all()), ..Default::default()
        });
        let Some(adapter) = pollster::block_on(instance.request_adapter(&RequestAdapterOptions::default())) else {
            assert_ne!(std::env::var("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU").as_deref(), Ok("1"), "GPU required");
            return;
        };
        eprintln!("Depth conversion adapter: {:?}", adapter.get_info());
        let (device, queue) = pollster::block_on(adapter.request_device(&DeviceDescriptor::default(), None)).unwrap();
        // Adjacent floats detect quantization; odd dimensions detect row padding
        // and asymmetric values detect a misplaced texel or inverted row.
        let bits = [0u32, 0x3e800001, 0x3eaaaaab, 0x3f000001, 0x3f7fffff, 0x3f800000];
        let width = 67; let height = 5; let pitch = 512;
        let depth = device.create_texture(&TextureDescriptor {
            label: Some("Depth conversion regression"),
            size: Extent3d { width, height, depth_or_array_layers: 1 }, mip_level_count: 1,
            sample_count: 1, dimension: TextureDimension::D2, format: TextureFormat::Depth32Float,
            usage: TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING, view_formats: &[],
        });
        let view = depth.create_view(&TextureViewDescriptor::default());
        let source = format!("{}\n@fragment fn fs_pattern(@builtin(position) p:vec4<f32>)->@builtin(frag_depth) f32 {{
            let bits=array<u32,6>({}); return bitcast<f32>(bits[(u32(p.x)+u32(p.y)*67u)%6u]); }}",
            SHADER, bits.iter().map(|v| format!("{v}u")).collect::<Vec<_>>().join(","));
        let shader = device.create_shader_module(ShaderModuleDescriptor {
            label: None, source: ShaderSource::Wgsl(source.into()),
        });
        let pipeline = device.create_render_pipeline(&RenderPipelineDescriptor {
            label: None, layout: None,
            vertex: VertexState { module: &shader, entry_point: Some("vs_main"), buffers: &[], compilation_options: Default::default() },
            fragment: Some(FragmentState { module: &shader, entry_point: Some("fs_pattern"), targets: &[], compilation_options: Default::default() }),
            primitive: Default::default(), depth_stencil: Some(DepthStencilState {
                format: TextureFormat::Depth32Float, depth_write_enabled: true, depth_compare: CompareFunction::Always,
                stencil: Default::default(), bias: Default::default(),
            }), multisample: Default::default(), multiview: None, cache: None,
        });
        let mut encoder = device.create_command_encoder(&CommandEncoderDescriptor::default());
        {
            let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
                label: None, color_attachments: &[],
                depth_stencil_attachment: Some(RenderPassDepthStencilAttachment {
                    view: &view, depth_ops: Some(Operations { load: LoadOp::Clear(1.0), store: StoreOp::Store }), stencil_ops: None,
                }), timestamp_writes: None, occlusion_query_set: None,
            });
            pass.set_pipeline(&pipeline); pass.draw(0..3, 0..1);
        }
        let snapshot = device.create_texture(&TextureDescriptor {
            label: Some("Depth snapshot regression"), size: depth.size(), mip_level_count: 1,
            sample_count: 1, dimension: TextureDimension::D2, format: TextureFormat::Depth32Float,
            usage: TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING, view_formats: &[],
        });
        let snapshot_view = snapshot.create_view(&TextureViewDescriptor::default());
        DepthTransfer::new(&device, true).copy_depth(&device, &mut encoder, &view, &snapshot_view);
        let output = DepthTransfer::new(&device, false).encode(&device, &mut encoder, &snapshot_view, width, height);
        let buffer = device.create_buffer(&BufferDescriptor {
            label: None, size: u64::from(pitch * height), usage: BufferUsages::COPY_DST | BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        encoder.copy_texture_to_buffer(output.as_image_copy(), TexelCopyBufferInfo {
            buffer: &buffer, layout: TexelCopyBufferLayout { offset: 0, bytes_per_row: Some(pitch), rows_per_image: Some(height) },
        }, output.size());
        queue.submit([encoder.finish()]);
        let (tx, rx) = std::sync::mpsc::channel();
        buffer.slice(..).map_async(MapMode::Read, move |status| tx.send(status).unwrap());
        device.poll(Maintain::Wait); rx.recv().unwrap().unwrap();
        let mapped = buffer.slice(..).get_mapped_range();
        for y in 0..height { for x in 0..width {
            let offset = (y * pitch + x * 4) as usize;
            let actual = u32::from_ne_bytes(mapped[offset..offset+4].try_into().unwrap());
            assert_eq!(actual, bits[((x+y*width)%6) as usize], "depth bits at ({x},{y})");
        }}
    }
