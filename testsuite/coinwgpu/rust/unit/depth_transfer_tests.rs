    use super::*;

    #[test]
    fn exact_depth_shader_translates_to_glsl() {
        super::super::shader_profile::tests::assert_glsl(super::SHADER, "fs_main");
        super::super::shader_profile::tests::assert_glsl(super::SHADER, "fs_depth");
        super::super::shader_profile::tests::assert_glsl(super::MSAA_DEPTH_SHADER, "fs_main");
    }

    #[test]
    fn multisample_depth_glsl_honors_backend_bounds_policy() {
        use naga::{back::glsl, proc::{BoundsCheckPolicies, BoundsCheckPolicy},
            valid::{Capabilities, ValidationFlags, Validator}};
        let module = naga::front::wgsl::parse_str(MSAA_DEPTH_SHADER).unwrap();
        let info = Validator::new(ValidationFlags::all(), Capabilities::all())
            .validate(&module).unwrap();
        for (version, policy, query_expected) in [
            (glsl::Version::Embedded { version: 310, is_webgl: false }, BoundsCheckPolicy::Unchecked, false),
            (glsl::Version::Desktop(430), BoundsCheckPolicy::ReadZeroSkipWrite, true),
        ] {
            let options = glsl::Options { version, ..Default::default() };
            let pipeline = glsl::PipelineOptions { shader_stage: naga::ShaderStage::Fragment,
                entry_point: "fs_main".into(), multiview: None };
            let mut output = String::new();
            glsl::Writer::new(&mut output, &module, &info, &options, &pipeline,
                BoundsCheckPolicies { image_load: policy, ..Default::default() })
                .unwrap().write().unwrap();
            assert!(output.contains("texelFetch("));
            assert_eq!(output.contains("textureSamples("), query_expected,
                "MSAA depth loads must respect the backend image policy: {output}");
        }
    }

    #[test]
    fn multisample_depth_resolves_nearest_sample_vulkan() {
        multisample_depth_resolves_nearest_sample(wgpu::Backends::VULKAN, wgpu::Backend::Vulkan);
    }

    #[test]
    fn multisample_depth_resolves_nearest_sample_gl() {
        multisample_depth_resolves_nearest_sample(wgpu::Backends::GL, wgpu::Backend::Gl);
    }

    fn multisample_depth_resolves_nearest_sample(backend: wgpu::Backends, expected_backend: wgpu::Backend) {
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: backend,
            ..Default::default()
        });
        let Some(adapter) = pollster::block_on(instance.request_adapter(&RequestAdapterOptions::default())) else {
            assert_ne!(std::env::var("COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU").as_deref(), Ok("1"), "GPU required");
            return;
        };
        if !adapter.features().contains(Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES) {
            eprintln!("skipping multisample depth: adapter-specific format features unavailable");
            return;
        }
        let depth_features = adapter.get_texture_format_features(TextureFormat::Depth32Float);
        if !depth_features.flags.contains(TextureFormatFeatureFlags::MULTISAMPLE_X2 | TextureFormatFeatureFlags::MULTISAMPLE_X4) {
            eprintln!("skipping multisample depth: Depth32Float lacks 2x or 4x support");
            return;
        }
        let (device, queue) = pollster::block_on(adapter.request_device(&DeviceDescriptor {
            required_features: Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES,
            ..Default::default()
        }, None)).unwrap();
        assert_eq!(adapter.get_info().backend, expected_backend);
        eprintln!("depth MSAA physical adapter: {:?}", adapter.get_info());
        for samples in [2, 4] {
        for selected_sample in 0..samples {
        let depth = device.create_texture(&TextureDescriptor {
            label: Some("Depth multisample regression"),
            size: Extent3d { width: 4, height: 4, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: samples, dimension: TextureDimension::D2,
            format: TextureFormat::Depth32Float,
            usage: TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        });
        let view = depth.create_view(&TextureViewDescriptor::default());
        let shader = device.create_shader_module(ShaderModuleDescriptor {
            label: Some("Depth per-sample regression"),
            source: ShaderSource::Wgsl(format!("{}\n@fragment fn fs_sample() -> @builtin(frag_depth) f32 {{ return {}; }}", SHADER, 0.2f32 + selected_sample as f32 * 0.1).into()),
        });
        let pipeline = device.create_render_pipeline(&RenderPipelineDescriptor {
            label: Some("Depth per-sample regression"), layout: None,
            vertex: VertexState { module: &shader, entry_point: Some("vs_main"), buffers: &[], compilation_options: Default::default() },
            fragment: Some(FragmentState { module: &shader, entry_point: Some("fs_sample"), targets: &[], compilation_options: Default::default() }),
            primitive: Default::default(), depth_stencil: Some(DepthStencilState {
                format: TextureFormat::Depth32Float, depth_write_enabled: true, depth_compare: CompareFunction::Always,
                stencil: Default::default(), bias: Default::default(),
            }), multisample: MultisampleState { count: samples, mask: 1u64 << selected_sample,
                ..Default::default() }, multiview: None, cache: None,
        });
        let mut encoder = device.create_command_encoder(&CommandEncoderDescriptor::default());
        {
            let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
                label: None, color_attachments: &[],
                depth_stencil_attachment: Some(RenderPassDepthStencilAttachment {
                    view: &view, depth_ops: Some(Operations { load: LoadOp::Clear(1.0), store: StoreOp::Store }), stencil_ops: None,
                }), timestamp_writes: None, occlusion_query_set: None,
            });
            pass.set_scissor_rect(0, 0, 2, 4);
            pass.set_pipeline(&pipeline); pass.draw(0..3, 0..1);
        }
        let transfer = if expected_backend == wgpu::Backend::Gl {
            DepthTransfer::new_gl(&device, false)
        } else {
            DepthTransfer::new(&device, false)
        };
        let output = transfer.encode_msaa(&device, &mut encoder, &view, 4, 4, samples);
        assert_eq!(output.format(), TextureFormat::Rgba8Unorm,
                   "{samples}x depth resolve must use a portable RGBA8 attachment");
        let buffer = device.create_buffer(&BufferDescriptor {
            label: None, size: 256 * 4, usage: BufferUsages::COPY_DST | BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        encoder.copy_texture_to_buffer(output.as_image_copy(), TexelCopyBufferInfo {
            buffer: &buffer, layout: TexelCopyBufferLayout { offset: 0, bytes_per_row: Some(256), rows_per_image: Some(4) },
        }, output.size());
        queue.submit([encoder.finish()]);
        let (tx, rx) = std::sync::mpsc::channel();
        buffer.slice(..).map_async(MapMode::Read, move |status| tx.send(status).unwrap());
        device.poll(Maintain::Wait); rx.recv().unwrap().unwrap();
        let mapped = buffer.slice(..).get_mapped_range();
        for y in 0..4 { for x in 0..4 {
            let offset = (y * 256 + x * 4) as usize;
            let actual = f32::from_ne_bytes(mapped[offset..offset+4].try_into().unwrap());
            let expected: f32 = if x < 2 { 0.2 + selected_sample as f32 * 0.1 } else { 1.0 };
            assert_eq!(&mapped[offset..offset+4], &expected.to_ne_bytes(),
                       "{samples}x sample={selected_sample} packed depth bytes at ({x},{y}) backend={expected_backend:?}");
            assert_eq!(actual, expected,
                       "{samples}x sample={selected_sample} depth at ({x},{y}) = {actual}, expected {expected}, backend={expected_backend:?}");
        }}
        drop(mapped);
        buffer.unmap();
        }
        }
    }

    #[test]
    fn conversion_preserves_depth_bits_and_padded_rows() {
        let instance = crate::gpu_test_instance();
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
        let snapshot_transfer = if adapter.get_info().backend == wgpu::Backend::Gl {
            DepthTransfer::new_gl(&device, true)
        } else {
            DepthTransfer::new(&device, true)
        };
        snapshot_transfer.copy_depth(&device, &mut encoder, &view, &snapshot_view);
        let readback_transfer = if adapter.get_info().backend == wgpu::Backend::Gl {
            DepthTransfer::new_gl(&device, false)
        } else {
            DepthTransfer::new(&device, false)
        };
        let output = readback_transfer.encode(&device, &mut encoder, &snapshot_view, width, height);
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
