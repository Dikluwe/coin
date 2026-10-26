    use super::*;

    #[test]
    fn bump_draws_route_one_image_per_indexed_draw() {
        let vertex = |slot| CoinWgpuBumpVertex { image_slot: slot,
            coord: [0.0,0.0,0.0,1.0], tangent_s: [1.0,0.0,0.0],
            tangent_t: [0.0,1.0,0.0] };
        let payload = ValidatedBumpPayload {
            vertices: vec![vertex(1), vertex(1), vertex(1),
                           vertex(2), vertex(2), vertex(2),
                           vertex(0), vertex(0), vertex(0)],
            images: (0..2).map(|_| ValidatedBumpImage { width: 1, height: 1,
                clamp_s: false, clamp_t: false, rgb: vec![128,128,255] }).collect(),
        };
        let mut draws = [CoinWgpuDraw::zeroed(); 3];
        for (i, draw) in draws.iter_mut().enumerate() {
            draw.first_index = (i * 3) as u32;
            draw.index_count = 3;
        }
        let indices: Vec<u32> = (0..9).collect();
        assert_eq!(bump_image_slots_for_draws(&payload, &indices, &draws).unwrap(),
                   [Some(0), Some(1), None]);
        let mut mixed = indices.clone();
        mixed[2] = 3;
        assert!(bump_image_slots_for_draws(&payload, &mixed, &draws).is_err());
        draws[0].first_index = 8;
        assert!(bump_image_slots_for_draws(&payload, &indices, &draws).is_err());
    }

    #[test]
    fn bump_lighting_state_converts_view_to_object_space() {
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.model_view = [2.0,0.0,0.0,0.0, 0.0,3.0,0.0,0.0,
                            0.0,0.0,4.0,0.0, 10.0,20.0,30.0,1.0];
        state.normal_matrix = [0.5,0.0,0.0,0.0, 0.0,1.0/3.0,0.0,0.0,
                               0.0,0.0,0.25,0.0, 0.0,0.0,0.0,1.0];
        state.model_view_projection[15] = 1.0;
        state.light_count = 1;
        state.lights[0].position_type = [12.0,23.0,34.0,1.0];
        state.lights[0].color_intensity = [1.0,1.0,1.0,1.0];
        let mut material = CoinWgpuMaterial::zeroed();
        material.diffuse = [0.8,0.2,0.1,1.0];
        material.specular = [0.5,0.25,0.125,1.0];
        material.shininess = 0.5;
        let uniforms = bump_uniforms_from_state(&state, &material).unwrap();
        assert_eq!(uniforms.light_position_type, [1.0,1.0,1.0,1.0]);
        assert!((uniforms.eye_position[0] + 5.0).abs() < 1e-6);
        assert!((uniforms.eye_position[1] + 20.0/3.0).abs() < 1e-6);
        assert!((uniforms.eye_position[2] + 7.5).abs() < 1e-6);
        assert_eq!(uniforms.diffuse, [0.8,0.2,0.1,1.0]);
        state.lights[0].position_type = [0.0,0.0,0.0,0.0];
        state.lights[0].direction_cutoff = [0.0,0.0,-4.0,0.0];
        assert_eq!(bump_uniforms_from_state(&state, &material).unwrap()
                   .light_position_type, [0.0,0.0,1.0,0.0]);
        state.lights[0].position_type[3] = 0.5;
        assert!(bump_uniforms_from_state(&state, &material).is_err());
        state.lights[0].position_type[3] = 1.0;
        state.lights[0].color_intensity[3] = -1.0;
        assert!(bump_uniforms_from_state(&state, &material).is_err());
    }

    #[test]
    fn optional_bump_payload_cannot_be_silently_ignored() {
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        assert!(!bump_payload_present(&frame));
        frame.bump_vertex_count = 1;
        assert!(bump_payload_present(&frame));
        frame.bump_vertex_count = 0;
        let vertex = CoinWgpuBumpVertex::zeroed();
        frame.bump_vertices = &vertex;
        assert!(bump_payload_present(&frame));
        frame.bump_vertices = std::ptr::null();
        frame.bump_image_count = 1;
        assert!(bump_payload_present(&frame));
    }

    #[test]
    fn bump_payload_validates_and_owns_image_bytes() {
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        let vertex = CoinWgpuBumpVertex {
            coord: [0.25, 0.5, 0.0, 1.0], tangent_s: [1.0, 0.0, 0.0],
            tangent_t: [0.0, 1.0, 0.0], image_slot: 1,
        };
        let mut rgb = [128u8, 128, 255];
        let mut image = CoinWgpuBumpImage {
            width: 1, height: 1, clamp_s: 0, clamp_t: 1,
            rgb: rgb.as_ptr(), rgb_len: 3,
        };
        frame.vertex_count = 1;
        frame.bump_vertices = &vertex;
        frame.bump_vertex_count = 1;
        frame.bump_images = &image;
        frame.bump_image_count = 1;
        let owned = validate_bump_payload(&frame, std::ptr::null_mut(), 0)
            .unwrap().unwrap();
        rgb[0] = 0;
        assert_eq!(owned.vertices[0].image_slot, 1);
        assert_eq!(owned.images[0].rgb, [128, 128, 255]);
        assert!(owned.images[0].clamp_t);
        image.rgb_len = 2;
        frame.bump_images = &image;
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
        image.rgb_len = 3;
        frame.bump_vertex_count = 2;
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
        frame.bump_vertex_count = 1;
        let bad_slot = CoinWgpuBumpVertex { image_slot: 2, ..vertex };
        frame.bump_vertices = &bad_slot;
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
        let bad_coord = CoinWgpuBumpVertex { coord: [0.0, 0.0, 0.0, 0.0], ..vertex };
        frame.bump_vertices = &bad_coord;
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
        frame.bump_vertices = &vertex;
        image.clamp_s = 2;
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
        image.clamp_s = 0;
        frame.bump_images = std::ptr::null();
        assert!(matches!(validate_bump_payload(&frame, std::ptr::null_mut(), 0),
            Err(CoinWgpuStatus::InvalidArgument)));
    }

    #[test]
    #[ignore = "requires a selected physical GPU; run with --ignored"]
    fn bump_payload_uploads_exact_vertices_and_rgb() {
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        let vertex = CoinWgpuBumpVertex {
            coord: [0.25, 0.5, 0.0, 1.0], tangent_s: [1.0, 0.0, 0.0],
            tangent_t: [0.0, 1.0, 0.0], image_slot: 1,
        };
        let rgb = [128u8, 128, 255, 240, 128, 128];
        let image = CoinWgpuBumpImage {
            width: 2, height: 1, clamp_s: 1, clamp_t: 0,
            rgb: rgb.as_ptr(), rgb_len: rgb.len() as u64,
        };
        frame.vertex_count = 1;
        frame.bump_vertices = &vertex;
        frame.bump_vertex_count = 1;
        frame.bump_images = &image;
        frame.bump_image_count = 1;
        let owned = validate_bump_payload(&frame, std::ptr::null_mut(), 0)
            .unwrap().unwrap();
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::from_env().unwrap_or(wgpu::Backends::all()),
            ..Default::default()
        });
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: None, force_fallback_adapter: false,
        })).expect("physical bump upload adapter");
        eprintln!("bump_upload_adapter {:?}", adapter.get_info());
        let (device, queue) = pollster::block_on(adapter.request_device(
            &wgpu::DeviceDescriptor::default(), None)).expect("bump upload device");
        let oversized = ValidatedBumpPayload {
            vertices: owned.vertices.clone(),
            images: vec![ValidatedBumpImage {
                width: device.limits().max_texture_dimension_2d + 1,
                height: 1, clamp_s: false, clamp_t: false, rgb: Vec::new(),
            }],
        };
        assert!(upload_bump_payload(&device, &queue, &oversized).is_err());
        let gpu = upload_bump_payload(&device, &queue, &owned).expect("bump upload");
        let image_bytes = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump image readback"), size: 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let vertex_bytes = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump vertex readback"), size: 44,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
        encoder.copy_texture_to_buffer(gpu.images[0].texture.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &image_bytes,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(1) } },
            wgpu::Extent3d { width: 2, height: 1, depth_or_array_layers: 1 });
        encoder.copy_buffer_to_buffer(&gpu.vertices, 0, &vertex_bytes, 0, 44);
        queue.submit([encoder.finish()]);
        let map = |buffer: &wgpu::Buffer| {
            let (sender, receiver) = std::sync::mpsc::channel();
            buffer.slice(..).map_async(wgpu::MapMode::Read, move |result| {
                let _ = sender.send(result);
            });
            let _ = device.poll(wgpu::Maintain::Wait);
            receiver.recv_timeout(std::time::Duration::from_secs(20))
                .expect("bump upload readback callback").expect("bump upload readback");
            let bytes = buffer.slice(..).get_mapped_range().to_vec();
            buffer.unmap();
            bytes
        };
        let pixels = map(&image_bytes);
        let vertices = map(&vertex_bytes);
        eprintln!("bump_upload_pixels {:?}", &pixels[..8]);
        assert_eq!(&pixels[..8], &[128, 128, 255, 255, 240, 128, 128, 255]);
        assert_eq!(&vertices[..], bytemuck::bytes_of(&vertex));
    }

    #[test]
    #[ignore = "requires a selected physical GPU; run with --ignored"]
    fn bump_abi_payload_renders_with_dedicated_shader() {
        let bump_vertex = CoinWgpuBumpVertex {
            coord: [0.5, 0.5, 0.0, 1.0], tangent_s: [1.0, 0.0, 0.0],
            tangent_t: [0.0, 1.0, 0.0], image_slot: 1,
        };
        let bump_vertices = [bump_vertex, bump_vertex, bump_vertex,
            CoinWgpuBumpVertex { image_slot: 2, ..bump_vertex },
            CoinWgpuBumpVertex { image_slot: 2, ..bump_vertex },
            CoinWgpuBumpVertex { image_slot: 2, ..bump_vertex }];
        let rgb = [128u8, 128, 255];
        let inverted_rgb = [128u8, 128, 0];
        let images = [CoinWgpuBumpImage {
            width: 1, height: 1, clamp_s: 0, clamp_t: 0,
            rgb: rgb.as_ptr(), rgb_len: 3,
        }, CoinWgpuBumpImage {
            width: 1, height: 1, clamp_s: 0, clamp_t: 0,
            rgb: inverted_rgb.as_ptr(), rgb_len: 3,
        }];
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        frame.vertex_count = 6;
        frame.bump_vertices = bump_vertices.as_ptr();
        frame.bump_vertex_count = 6;
        frame.bump_images = images.as_ptr();
        frame.bump_image_count = 2;
        let owned = validate_bump_payload(&frame, std::ptr::null_mut(), 0)
            .unwrap().unwrap();
        let indices = [0u32, 1, 2, 3, 4, 5];
        let draw = CoinWgpuDraw { topology: 0, first_index: 0, index_count: 3,
            ..CoinWgpuDraw::zeroed() };
        let inverted_draw = CoinWgpuDraw { first_index: 3, ..draw };
        let slots = bump_image_slots_for_draws(&owned, &indices, &[draw, inverted_draw])
            .expect("two image slots");
        assert_eq!(slots, [Some(0), Some(1)]);
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::from_env().unwrap_or(wgpu::Backends::all()),
            ..Default::default()
        });
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: None, force_fallback_adapter: false,
        })).expect("physical bump draw adapter");
        eprintln!("bump_abi_draw_adapter {:?}", adapter.get_info());
        let (device, queue) = pollster::block_on(adapter.request_device(
            &wgpu::DeviceDescriptor::default(), None)).expect("bump draw device");
        let gpu = upload_bump_payload(&device, &queue, &owned).expect("bump draw upload");
        let pipeline = create_bump_pipeline(&device, wgpu::TextureFormat::Rgba8Unorm,
                                            None, "fs_main", None);
        let base_pipeline = create_bump_base_pipeline(&device,
            wgpu::TextureFormat::Rgba8Unorm, None);
        let specular_pipeline = create_bump_pipeline(&device,
            wgpu::TextureFormat::Rgba8Unorm, None, "fs_specular", None);
        let additive_specular_pipeline = create_bump_pipeline(&device,
            wgpu::TextureFormat::Rgba8Unorm, None, "fs_specular",
            Some(wgpu::BlendState {
                color: wgpu::BlendComponent {
                    src_factor: wgpu::BlendFactor::One,
                    dst_factor: wgpu::BlendFactor::One,
                    operation: wgpu::BlendOperation::Add,
                },
                alpha: wgpu::BlendComponent {
                    src_factor: wgpu::BlendFactor::One,
                    dst_factor: wgpu::BlendFactor::One,
                    operation: wgpu::BlendOperation::Add,
                },
            }));
        let mut vertices = [CoinWgpuVertex::zeroed(); 6];
        vertices[0].position = [-1.0, -1.0, 0.0];
        vertices[1].position = [3.0, -1.0, 0.0];
        vertices[2].position = [-1.0, 3.0, 0.0];
        vertices[3] = vertices[0];
        vertices[4] = vertices[1];
        vertices[5] = vertices[2];
        for vertex in &mut vertices {
            vertex.normal = [0.0, 0.0, 1.0];
            vertex.texcoord = [0.5,0.5,0.0,1.0];
        }
        let positions = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI main vertices"),
            size: std::mem::size_of_val(&vertices) as u64,
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        queue.write_buffer(&positions, 0, bytemuck::cast_slice(&vertices));
        let index_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI indices"), size: std::mem::size_of_val(&indices) as u64,
            usage: wgpu::BufferUsages::INDEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        queue.write_buffer(&index_buffer, 0, bytemuck::cast_slice(&indices));
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.model_view = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                            0.0,0.0,1.0,0.0, 0.0,0.0,-5.0,1.0];
        state.model_view_projection = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                                       0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0];
        state.normal_matrix = state.model_view_projection;
        state.light_model = 1; // Consumed bump uses the PHONG contract.
        state.light_count = 1;
        state.lights[0].position_type = [0.0,0.0,0.0,0.0];
        state.lights[0].direction_cutoff = [0.0,0.0,-1.0,0.0];
        state.lights[0].color_intensity = [1.0,1.0,1.0,1.0];
        let mut material = CoinWgpuMaterial::zeroed();
        material.diffuse = [0.8,0.2,0.1,1.0];
        material.specular = [0.5,0.25,0.125,1.0];
        material.shininess = 0.5;
        let uniforms = bump_uniforms_from_state(&state, &material)
            .expect("bump state uniforms");
        let uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI uniforms"), size: std::mem::size_of_val(&uniforms) as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        queue.write_buffer(&uniform_buffer, 0, bytemuck::bytes_of(&uniforms));
        let bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI image"), layout: &pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&gpu.images[0].view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&gpu.images[0].sampler) },
            ],
        });
        let inverted_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI second image"),
            layout: &pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&gpu.images[slots[1].unwrap()].view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&gpu.images[slots[1].unwrap()].sampler) },
            ],
        });
        let specular_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI specular image"),
            layout: &specular_pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&gpu.images[0].view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&gpu.images[0].sampler) },
            ],
        });
        let additive_specular_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI additive specular image"),
            layout: &additive_specular_pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&gpu.images[0].view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&gpu.images[0].sampler) },
            ],
        });
        let base_image = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("bump ABI opaque base image"),
            size: wgpu::Extent3d { width: 1, height: 1, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        queue.write_texture(base_image.as_image_copy(), &[128,128,128,255],
            wgpu::TexelCopyBufferLayout { offset: 0, bytes_per_row: Some(4),
                rows_per_image: Some(1) }, base_image.size());
        let base_image_view = base_image.create_view(&wgpu::TextureViewDescriptor::default());
        let base_sampler = device.create_sampler(&wgpu::SamplerDescriptor::default());
        let base_uniforms = BumpBaseUniforms { texture_matrix: [
            1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
            0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0,
        ] };
        let base_uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI base matrix"),
            size: std::mem::size_of_val(&base_uniforms) as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        queue.write_buffer(&base_uniform_buffer, 0, bytemuck::bytes_of(&base_uniforms));
        let base_bump_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI base normal map"),
            layout: &base_pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&gpu.images[0].view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&gpu.images[0].sampler) },
            ],
        });
        let base_texture_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("bump ABI base color image"),
            layout: &base_pipeline.get_bind_group_layout(1),
            entries: &[
                wgpu::BindGroupEntry { binding: 0,
                    resource: base_uniform_buffer.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1,
                    resource: wgpu::BindingResource::TextureView(&base_image_view) },
                wgpu::BindGroupEntry { binding: 2,
                    resource: wgpu::BindingResource::Sampler(&base_sampler) },
            ],
        });
        let target = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("bump ABI color"),
            size: wgpu::Extent3d { width: 8, height: 8, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let target_view = target.create_view(&wgpu::TextureViewDescriptor::default());
        let readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI color readback"), size: 8 * 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let specular_readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI specular readback"), size: 8 * 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let composed_readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI composed readback"), size: 8 * 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let base_readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI opaque base readback"), size: 8 * 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let inverted_readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("bump ABI second image readback"), size: 8 * 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("bump ABI diffuse"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target_view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None, timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&pipeline);
            pass.set_bind_group(0, &bind, &[]);
            pass.set_vertex_buffer(0, positions.slice(..));
            pass.set_vertex_buffer(1, gpu.vertices.slice(..));
            pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(draw.first_index..draw.first_index + draw.index_count, 0, 0..1);
        }
        encoder.copy_texture_to_buffer(target.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &readback,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("bump ABI specular"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target_view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None, timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&specular_pipeline);
            pass.set_bind_group(0, &specular_bind, &[]);
            pass.set_vertex_buffer(0, positions.slice(..));
            pass.set_vertex_buffer(1, gpu.vertices.slice(..));
            pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(draw.first_index..draw.first_index + draw.index_count, 0, 0..1);
        }
        encoder.copy_texture_to_buffer(target.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &specular_readback,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("bump ABI diffuse and additive specular"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target_view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None, timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&pipeline);
            pass.set_bind_group(0, &bind, &[]);
            pass.set_vertex_buffer(0, positions.slice(..));
            pass.set_vertex_buffer(1, gpu.vertices.slice(..));
            pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(draw.first_index..draw.first_index + draw.index_count, 0, 0..1);
            pass.set_pipeline(&additive_specular_pipeline);
            pass.set_bind_group(0, &additive_specular_bind, &[]);
            pass.draw_indexed(draw.first_index..draw.first_index + draw.index_count, 0, 0..1);
        }
        encoder.copy_texture_to_buffer(target.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &composed_readback,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("bump ABI opaque base texture"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target_view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None, timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&base_pipeline);
            pass.set_bind_group(0, &base_bump_bind, &[]);
            pass.set_bind_group(1, &base_texture_bind, &[]);
            pass.set_vertex_buffer(0, positions.slice(..));
            pass.set_vertex_buffer(1, gpu.vertices.slice(..));
            pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(draw.first_index..draw.first_index + draw.index_count, 0, 0..1);
        }
        encoder.copy_texture_to_buffer(target.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &base_readback,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("bump ABI second normal image"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target_view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None, timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&pipeline);
            pass.set_bind_group(0, &inverted_bind, &[]);
            pass.set_vertex_buffer(0, positions.slice(..));
            pass.set_vertex_buffer(1, gpu.vertices.slice(..));
            pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(inverted_draw.first_index..inverted_draw.first_index +
                              inverted_draw.index_count, 0, 0..1);
        }
        encoder.copy_texture_to_buffer(target.as_image_copy(),
            wgpu::TexelCopyBufferInfo { buffer: &inverted_readback,
                layout: wgpu::TexelCopyBufferLayout { offset: 0,
                    bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
        queue.submit([encoder.finish()]);
        let (sender, receiver) = std::sync::mpsc::channel();
        readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::Maintain::Wait);
        receiver.recv_timeout(std::time::Duration::from_secs(20))
            .expect("bump ABI color callback").expect("bump ABI color readback");
        let mapped = readback.slice(..).get_mapped_range();
        let offset = 4 * 256 + 4 * 4;
        let pixel: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
        drop(mapped);
        readback.unmap();
        eprintln!("bump_abi_draw_pixel {:?}", pixel);
        assert!(pixel[0].abs_diff(204) <= 1 && pixel[1].abs_diff(51) <= 1 &&
            pixel[2].abs_diff(26) <= 1 && pixel[3] == 255);
        let (sender, receiver) = std::sync::mpsc::channel();
        composed_readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::Maintain::Wait);
        receiver.recv_timeout(std::time::Duration::from_secs(20))
            .expect("bump composed callback").expect("bump composed readback");
        let mapped = composed_readback.slice(..).get_mapped_range();
        let composed: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
        drop(mapped);
        composed_readback.unmap();
        eprintln!("bump_abi_composed_pixel {:?}", composed);
        assert!(composed[0] == 255 && composed[1].abs_diff(115) <= 1 &&
            composed[2].abs_diff(57) <= 1 && composed[3] == 255);
        let (sender, receiver) = std::sync::mpsc::channel();
        base_readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::Maintain::Wait);
        receiver.recv_timeout(std::time::Duration::from_secs(20))
            .expect("bump base callback").expect("bump base readback");
        let mapped = base_readback.slice(..).get_mapped_range();
        let base_pixel: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
        drop(mapped);
        base_readback.unmap();
        eprintln!("bump_abi_base_pixel {:?}", base_pixel);
        assert!(base_pixel[0].abs_diff(102) <= 1 &&
            base_pixel[1].abs_diff(26) <= 1 &&
            base_pixel[2].abs_diff(13) <= 1 && base_pixel[3] == 255);
        let (sender, receiver) = std::sync::mpsc::channel();
        inverted_readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::Maintain::Wait);
        receiver.recv_timeout(std::time::Duration::from_secs(20))
            .expect("bump second image callback").expect("bump second image readback");
        let mapped = inverted_readback.slice(..).get_mapped_range();
        let inverted: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
        drop(mapped);
        inverted_readback.unmap();
        eprintln!("bump_abi_second_image_pixel {:?}", inverted);
        assert_eq!(inverted, [0,0,0,255]);
        let (sender, receiver) = std::sync::mpsc::channel();
        specular_readback.slice(..).map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::Maintain::Wait);
        receiver.recv_timeout(std::time::Duration::from_secs(20))
            .expect("bump ABI specular callback").expect("bump ABI specular readback");
        let mapped = specular_readback.slice(..).get_mapped_range();
        let specular: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
        drop(mapped);
        specular_readback.unmap();
        eprintln!("bump_abi_specular_pixel {:?}", specular);
        for (actual, expected) in specular.iter().zip([127u8,64,32,254]) {
            assert!(actual.abs_diff(expected) <= 1, "specular ABI pixel {specular:?}");
        }
        state.depth_test = 1;
        state.depth_write = 1;
        state.depth_function = 3;
        // Private ABI: 0 disables channel writes; 1 explicitly enables RGBA.
        state.color_write = 1;
        state.viewport = [0,0,8,8];
        let depth = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("bump encoder depth"),
            size: target.size(), mip_level_count: 1, sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Depth32Float,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });
        let depth_view = depth.create_view(&wgpu::TextureViewDescriptor::default());
        let pipeline_cache = Mutex::new(HashMap::new());
        let read_encoder_pixel = |draw_vertices: &[CoinWgpuVertex], draws: &[CoinWgpuDraw],
                                  st: CoinWgpuRenderState,
                                  textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler]| {
            let command = encode_bump_opaque_frame(&device, &queue, Some(&pipeline_cache), None, draw_vertices,
                &indices, draws, &[st], &[material], textures, samplers, &owned, &target_view,
                wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0])
                .expect("bump frame encoder");
            let output = device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("bump frame encoder readback"), size: 8 * 256,
                usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
                mapped_at_creation: false,
            });
            let mut copy = device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
            copy.copy_texture_to_buffer(target.as_image_copy(),
                wgpu::TexelCopyBufferInfo { buffer: &output,
                    layout: wgpu::TexelCopyBufferLayout { offset: 0,
                        bytes_per_row: Some(256), rows_per_image: Some(8) } }, target.size());
            queue.submit([command, copy.finish()]);
            let (sender, receiver) = std::sync::mpsc::channel();
            output.slice(..).map_async(wgpu::MapMode::Read, move |result| {
                let _ = sender.send(result);
            });
            let _ = device.poll(wgpu::Maintain::Wait);
            receiver.recv_timeout(std::time::Duration::from_secs(20))
                .expect("bump encoder callback").expect("bump encoder readback");
            let mapped = output.slice(..).get_mapped_range();
            let pixel: [u8; 4] = mapped[offset..offset + 4].try_into().unwrap();
            drop(mapped);
            output.unmap();
            pixel
        };
        let encoded = read_encoder_pixel(&vertices, &[draw], state, &[], &[]);
        eprintln!("bump_frame_encoded_pixel {:?}", encoded);
        assert_eq!(encoded, [255,115,57,255]);
        let encoded_two = read_encoder_pixel(&vertices, &[draw, inverted_draw], state, &[], &[]);
        eprintln!("bump_frame_two_draws_pixel {:?}", encoded_two);
        assert_eq!(encoded_two, [0,0,0,255]);
        let mut textured = state;
        textured.has_texture = 1;
        textured.texture_matrix = base_uniforms.texture_matrix;
        let base_rgba = [128u8,128,128,255];
        let base_source = CoinWgpuTexture { width: 1, height: 1, format: 0, reserved: 0,
            content_digest: 0, pixels: base_rgba.as_ptr(), pixel_bytes_len: 4 };
        let base_settings = CoinWgpuSampler { wrap_s: 0, wrap_t: 0, filter: 0, reserved: 0 };
        let encoded_base = read_encoder_pixel(&vertices, &[draw], textured, &[base_source], &[base_settings]);
        eprintln!("bump_frame_textured_pixel {:?}", encoded_base);
        for (actual, expected) in encoded_base.iter().zip([229u8,90,45,255]) {
            assert!(actual.abs_diff(expected) <= 1, "textured bump encoder {encoded_base:?}");
        }
        let srgb_source = CoinWgpuTexture { format: 3, ..base_source };
        let encoded_srgb = read_encoder_pixel(&vertices, &[draw], textured, &[srgb_source], &[base_settings]);
        eprintln!("bump_frame_srgb_base_pixel {:?}", encoded_srgb);
        // 128 sRGB decodes to 0.21586 linear. The diffuse contribution
        // changes; the independently lit specular contribution does not.
        for (actual, expected) in encoded_srgb.iter().zip([171u8,75,37,255]) {
            assert!(actual.abs_diff(expected) <= 2, "sRGB bump base {encoded_srgb:?}");
        }
        let repeated_base = encode_bump_opaque_frame(&device, &queue,
            Some(&pipeline_cache), None, &vertices, &indices, &[draw,draw], &[textured],
            &[material], &[base_source], &[base_settings], &owned, &target_view,
            wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0])
            .expect("two draws sharing a bump base image");
        drop(repeated_base);
        LAST_BUMP_BASE_IMAGE_UPLOADS.with(|count| assert_eq!(count.get(), 1,
            "two draws sharing texture and sampler upload the base image once"));
        eprintln!("bump_frame_shared_base draws=2 image_uploads=1");
        let base_cache = Mutex::new(BumpBaseCache::default());
        for serial in 1..=2 {
            let command = encode_bump_opaque_frame(&device, &queue,
                Some(&pipeline_cache), Some((&base_cache, serial, serial - 1)),
                &vertices, &indices, &[draw], &[textured], &[material],
                &[base_source], &[base_settings], &owned, &target_view,
                wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0])
                .expect("stable bump base across frames");
            queue.submit([command]);
        }
        let changed_rgba = [64u8,128,128,255];
        let changed_source = CoinWgpuTexture { pixels: changed_rgba.as_ptr(), ..base_source };
        let command = encode_bump_opaque_frame(&device, &queue,
            Some(&pipeline_cache), Some((&base_cache, 3, 2)),
            &vertices, &indices, &[draw], &[textured], &[material],
            &[changed_source], &[base_settings], &owned, &target_view,
            wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0])
            .expect("changed bytes with unchanged digest invalidate bump base");
        queue.submit([command]);
        let _ = device.poll(wgpu::Maintain::Wait);
        let cache = base_cache.lock().unwrap();
        assert_eq!(cache.uploads, 2);
        assert_eq!(cache.hits, 1);
        eprintln!("bump_base_cross_frame_cache uploads={} hits={}", cache.uploads, cache.hits);
        drop(cache);
        let command = encode_bump_opaque_frame(&device, &queue,
            Some(&pipeline_cache), Some((&base_cache, 4, 3)),
            &vertices, &indices, &[draw], &[textured], &[material],
            &[srgb_source], &[base_settings], &owned, &target_view,
            wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0])
            .expect("sRGB and linear base must have different cache entries");
        queue.submit([command]);
        assert_eq!(base_cache.lock().unwrap().uploads, 3,
            "sRGB reinterpretation must upload a separate texture");
        let large_pixels = vec![128u8; 512 * 512 * 4];
        let mut large_pixels = large_pixels;
        for alpha in large_pixels.iter_mut().skip(3).step_by(4) { *alpha = 255; }
        let large_source = CoinWgpuTexture { width: 512, height: 512,
            content_digest: 0xBACE, pixels: large_pixels.as_ptr(),
            pixel_bytes_len: large_pixels.len() as u64, ..base_source };
        let timing_cache = Mutex::new(BumpBaseCache::default());
        let mut uncached_base_ms = Vec::new();
        let mut cached_base_ms = Vec::new();
        let mut serial = 1;
        for cached in [false, true, true, false] {
            for _ in 0..12 {
                let start = std::time::Instant::now();
                let cache = if cached { Some((&timing_cache, serial, serial - 1)) }
                    else { None };
                let command = encode_bump_opaque_frame(&device, &queue,
                    Some(&pipeline_cache), cache, &vertices, &indices, &[draw],
                    &[textured], &[material], &[large_source], &[base_settings],
                    &owned, &target_view, wgpu::TextureFormat::Rgba8Unorm,
                    &depth_view, [8,8], [0.0,0.0,0.0,1.0])
                    .expect("large bump base timing frame");
                queue.submit([command]);
                let _ = device.poll(wgpu::Maintain::Wait);
                let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;
                if cached { cached_base_ms.push(elapsed_ms); }
                else { uncached_base_ms.push(elapsed_ms); }
                serial += 1;
            }
        }
        uncached_base_ms.sort_by(f64::total_cmp);
        cached_base_ms.sort_by(f64::total_cmp);
        let timing_counts = timing_cache.lock().unwrap();
        eprintln!("bump_base_cache_bench image=512x512 rgba8 samples=24 uncached_median_ms={:.6} cached_median_ms={:.6} uploads={} hits={}",
            uncached_base_ms[12], cached_base_ms[12],
            timing_counts.uploads, timing_counts.hits);
        drop(timing_counts);
        let mip_pixels = [128u8,128,128,255, 128,128,128,255,
                          128,128,128,255, 128,128,128,255, 128,128,128,255];
        let mip_source = CoinWgpuTexture { width: 2, height: 2, format: 2, reserved: 0,
            content_digest: 0, pixels: mip_pixels.as_ptr(), pixel_bytes_len: 20 };
        let mip_settings = CoinWgpuSampler { wrap_s: 0, wrap_t: 0, filter: 2, reserved: 0 };
        let encoded_mip = read_encoder_pixel(&vertices, &[draw], textured, &[mip_source], &[mip_settings]);
        eprintln!("bump_frame_mipmapped_pixel {:?}", encoded_mip);
        for (actual, expected) in encoded_mip.iter().zip([229u8,90,45,255]) {
            assert!(actual.abs_diff(expected) <= 1, "mipmapped bump encoder {encoded_mip:?}");
        }
        let srgb_mip_source = CoinWgpuTexture { format: 4, ..mip_source };
        let encoded_srgb_mip = read_encoder_pixel(&vertices, &[draw], textured,
            &[srgb_mip_source], &[mip_settings]);
        eprintln!("bump_frame_srgb_mip_pixel {:?}", encoded_srgb_mip);
        for (actual, expected) in encoded_srgb_mip.iter().zip([171u8,75,37,255]) {
            assert!(actual.abs_diff(expected) <= 2,
                "mipmapped sRGB bump base {encoded_srgb_mip:?}");
        }
        for factor in [2u32,4,8,16] {
            let anisotropic = CoinWgpuSampler { filter: 3, reserved: factor - 1,
                ..mip_settings };
            let pixel = read_encoder_pixel(&vertices, &[draw], textured,
                &[mip_source], &[anisotropic]);
            for (actual, expected) in pixel.iter().zip([229u8,90,45,255]) {
                assert!(actual.abs_diff(expected) <= 1,
                    "anisotropic bump factor {factor}: {pixel:?}");
            }
        }
        eprintln!("bump_base_anisotropy factors=2,4,8,16 pixel=[229,90,45,255]");
        let mut stripe_level = vec![0u8; 256 * 256 * 4];
        for y in 0..256 {
            for x in 0..256 {
                let offset = (y * 256 + x) * 4;
                stripe_level[offset] = if (y / 8) % 2 == 0 { 255 } else { 0 };
                stripe_level[offset + 3] = 255;
            }
        }
        let mut stripe_chain = Vec::new();
        let mut side = 256usize;
        loop {
            stripe_chain.extend_from_slice(&stripe_level);
            if side == 1 { break; }
            let next_side = side / 2;
            let mut next = vec![0u8; next_side * next_side * 4];
            for y in 0..next_side {
                for x in 0..next_side {
                    for channel in 0..4 {
                        let sum: u32 = [(0,0),(1,0),(0,1),(1,1)].iter()
                            .map(|(dx,dy)| u32::from(stripe_level[
                                (((2*y+dy)*side + 2*x+dx)*4)+channel]))
                            .sum();
                        next[(y*next_side+x)*4+channel] = ((sum+2)/4) as u8;
                    }
                }
            }
            stripe_level = next;
            side = next_side;
        }
        let stripe_source = CoinWgpuTexture { width: 256, height: 256,
            format: 2, content_digest: 0x5a71, pixels: stripe_chain.as_ptr(),
            pixel_bytes_len: stripe_chain.len() as u64, ..base_source };
        let mut patterned_vertices = vertices;
        patterned_vertices[0].texcoord = [0.0,0.0,0.0,1.0];
        patterned_vertices[1].texcoord = [2.0,0.0,0.0,1.0];
        patterned_vertices[2].texcoord = [0.0,0.25,0.0,1.0];
        let mut stripe_samples = [Vec::<u8>::new(), Vec::<u8>::new()];
        for (index, factor) in [1u32,8].iter().enumerate() {
            let settings = CoinWgpuSampler { filter: 3, reserved: factor-1,
                ..mip_settings };
            for step in 0..8 {
                let mut translated = textured;
                translated.texture_matrix[13] = step as f32 * 4.0 / 256.0;
                let pixel = read_encoder_pixel(&patterned_vertices, &[draw],
                    translated, &[stripe_source], &[settings]);
                stripe_samples[index].push(pixel[0]);
            }
        }
        eprintln!("bump_base_anisotropy_stripes factor1={:?} factor8={:?}",
            stripe_samples[0], stripe_samples[1]);
        let span = |values: &[u8]| values.iter().max().unwrap() - values.iter().min().unwrap();
        assert!(span(&stripe_samples[0]) <= 4 && span(&stripe_samples[1]) >= 64,
            "anisotropic filtering must retain the oblique stripe signal");
        for rejected in [CoinWgpuSampler { filter: 3, reserved: 2, ..mip_settings },
                         CoinWgpuSampler { filter: 2, reserved: 1, ..mip_settings }] {
            assert!(encode_bump_opaque_frame(&device, &queue,
                Some(&pipeline_cache), None, &vertices, &indices, &[draw],
                &[textured], &[material], &[mip_source], &[rejected],
                &owned, &target_view, wgpu::TextureFormat::Rgba8Unorm,
                &depth_view, [8,8], [0.0,0.0,0.0,1.0]).is_err(),
                "invalid anisotropy must reject before submission");
        }
        assert_eq!(pipeline_cache.lock().unwrap().len(), 1,
            "untextured, textured and mipmapped draws reuse one pipeline set");
        let mut no_color = state;
        no_color.color_write = 0;
        let encoded_no_color = read_encoder_pixel(&vertices, &[draw], no_color, &[], &[]);
        eprintln!("bump_frame_zero_write_pixel {:?}", encoded_no_color);
        assert_eq!(encoded_no_color, [0,0,0,255],
            "zero channel writes preserve the authorial opaque black clear");
        let encoded_restored = read_encoder_pixel(&vertices, &[draw], state, &[], &[]);
        assert_eq!(encoded_restored, [255,115,57,255],
            "restoring RGBA writes cannot reuse the zero-mask cached pipeline");
        {
            let cache = pipeline_cache.lock().unwrap();
            let all = BumpPipelineKey { color_format: wgpu::TextureFormat::Rgba8Unorm,
                depth_compare: wgpu::CompareFunction::LessEqual, sample_count: 1,
                color_writes: wgpu::ColorWrites::ALL.bits() };
            let none = BumpPipelineKey { color_format: wgpu::TextureFormat::Rgba8Unorm,
                depth_compare: wgpu::CompareFunction::LessEqual, sample_count: 1,
                color_writes: wgpu::ColorWrites::empty().bits() };
            assert!(cache.contains_key(&all) && cache.contains_key(&none),
                "color write ALL and NONE require their own exact pipeline keys");
            assert_eq!(cache.len(), 2,
                "the mask contracontrol adds only NONE to the original shared ALL pipeline");
        }
        let mut uncached_ms = Vec::new();
        let mut cached_ms = Vec::new();
        // Measure CPU encoding only. Alternating blocks limits drift, and the
        // test deliberately makes no cross-driver latency assertion.
        for cached in [false, true, true, false] {
            for _ in 0..16 {
                let start = std::time::Instant::now();
                let command = encode_bump_opaque_frame(&device, &queue,
                    if cached { Some(&pipeline_cache) } else { None }, None, &vertices,
                    &indices, &[draw], &[state], &[material], &[], &[], &owned,
                    &target_view, wgpu::TextureFormat::Rgba8Unorm, &depth_view,
                    [8,8], [0.0,0.0,0.0,1.0]).expect("bump encoder timing");
                let elapsed = start.elapsed().as_secs_f64() * 1000.0;
                drop(command);
                if cached { cached_ms.push(elapsed); } else { uncached_ms.push(elapsed); }
            }
        }
        uncached_ms.sort_by(f64::total_cmp);
        cached_ms.sort_by(f64::total_cmp);
        eprintln!("bump_pipeline_cache_bench encoding_only samples=32 uncached_median_ms={:.6} cached_median_ms={:.6}",
            uncached_ms[16], cached_ms[16]);
        let mut uncached_submit_ms = Vec::new();
        let mut cached_submit_ms = Vec::new();
        for cached in [true, false, false, true] {
            for _ in 0..16 {
                let start = std::time::Instant::now();
                let command = encode_bump_opaque_frame(&device, &queue,
                    if cached { Some(&pipeline_cache) } else { None }, None, &vertices,
                    &indices, &[draw], &[state], &[material], &[], &[], &owned,
                    &target_view, wgpu::TextureFormat::Rgba8Unorm, &depth_view,
                    [8,8], [0.0,0.0,0.0,1.0]).expect("bump submit timing");
                queue.submit([command]);
                let _ = device.poll(wgpu::Maintain::Wait);
                let elapsed = start.elapsed().as_secs_f64() * 1000.0;
                if cached { cached_submit_ms.push(elapsed); }
                else { uncached_submit_ms.push(elapsed); }
            }
        }
        uncached_submit_ms.sort_by(f64::total_cmp);
        cached_submit_ms.sort_by(f64::total_cmp);
        eprintln!("bump_pipeline_cache_bench encode_submit_wait samples=32 uncached_median_ms={:.6} cached_median_ms={:.6}",
            uncached_submit_ms[16], cached_submit_ms[16]);
        let mut emissive = material;
        emissive.emission[0] = 0.1;
        emissive.ambient[1] = 0.9;
        assert!(encode_bump_opaque_frame(&device, &queue, None, None, &vertices,
            &indices, &[draw], &[state], &[emissive], &[], &[], &owned, &target_view,
            wgpu::TextureFormat::Rgba8Unorm, &depth_view, [8,8], [0.0,0.0,0.0,1.0]).is_ok());
    }

    #[test]
    #[ignore = "requires a selected physical GPU; run with --ignored"]
    fn bump_offscreen_ffi_submission_renders_pixel() {
        let mut vertices = [CoinWgpuVertex::zeroed(); 3];
        vertices[0].position = [-1.0,-1.0,0.0];
        vertices[1].position = [3.0,-1.0,0.0];
        vertices[2].position = [-1.0,3.0,0.0];
        for vertex in &mut vertices { vertex.normal = [0.0,0.0,1.0]; }
        let indices = [0u32,1,2];
        let draw = CoinWgpuDraw { topology: 0, first_index: 0, index_count: 3,
            render_state_slot: 0, ..CoinWgpuDraw::zeroed() };
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.model_view = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                            0.0,0.0,1.0,0.0, 0.0,0.0,-5.0,1.0];
        state.model_view_projection = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                                       0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0];
        state.normal_matrix = state.model_view_projection;
        state.light_model = 1; // Consumed bump uses the PHONG contract.
        state.light_count = 1;
        state.lights[0].position_type = [0.0,0.0,0.0,0.0];
        state.lights[0].direction_cutoff = [0.0,0.0,-1.0,0.0];
        state.lights[0].color_intensity = [1.0,1.0,1.0,1.0];
        state.depth_test = 1;
        state.depth_write = 1;
        state.depth_function = 3;
        // Private ABI: 0 disables channel writes; 1 explicitly enables RGBA.
        state.color_write = 1;
        state.viewport = [0,0,8,8];
        let mut material = CoinWgpuMaterial::zeroed();
        material.diffuse = [0.8,0.2,0.1,1.0];
        material.specular = [0.5,0.25,0.125,1.0];
        material.shininess = 0.5;
        let bump_vertex = CoinWgpuBumpVertex { coord: [0.5,0.5,0.0,1.0],
            tangent_s: [1.0,0.0,0.0], tangent_t: [0.0,1.0,0.0], image_slot: 1 };
        let bump_vertices = [bump_vertex; 3];
        let rgb = [128u8,128,255];
        let image = CoinWgpuBumpImage { width: 1, height: 1, clamp_s: 0,
            clamp_t: 0, rgb: rgb.as_ptr(), rgb_len: 3 };
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        frame.multipass_count = 1; // ABI 69 requires an explicit single pass.
        frame.abi_version = COIN_WGPU_ABI_VERSION;
        frame.struct_size = std::mem::size_of::<CoinWgpuFrameView>() as u32;
        frame.vertices = vertices.as_ptr(); frame.vertex_count = 3;
        frame.indices = indices.as_ptr(); frame.index_count = 3;
        frame.draws = &draw; frame.draw_count = 1;
        frame.materials = &material; frame.material_count = 1;
        frame.states = &state; frame.state_count = 1;
        frame.bump_vertices = bump_vertices.as_ptr(); frame.bump_vertex_count = 3;
        frame.bump_images = &image; frame.bump_image_count = 1;
        frame.width = 8; frame.height = 8;
        frame.clear_color = [0.0,0.0,0.0,1.0];
        frame.sorted_layers_passes = 1;
        let mut pixels = [0u8; 8 * 8 * 4];
        let mut depth = [0.0f32; 8 * 8];
        let mut target = CoinWgpuTarget { width: 8, height: 8,
            color_buffer: pixels.as_mut_ptr(), color_buffer_len: pixels.len() as u64,
            depth_buffer: depth.as_mut_ptr(), depth_buffer_len: depth.len() as u64,
            submission_serial: 0, device_id: 0, sample_count: 1 };
        let mut error = [0 as std::os::raw::c_char; 512];
        // BASE_COLOR with consumed bump remains unsupported; correcting the
        // positive PHONG fixture must not erase this negative contract.
        state.light_model = 0;
        pixels.fill(0x65);
        depth.fill(-17.0);
        let rejected = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
        assert_eq!(rejected, CoinWgpuStatus::Unsupported);
        assert_eq!(target.submission_serial, 0);
        assert!(pixels.iter().all(|&value| value == 0x65));
        assert!(depth.iter().all(|&value| value == -17.0));
        state.light_model = 1;
        let status = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
        let message = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy();
        assert_eq!(status, CoinWgpuStatus::Ok, "{message}");
        let offset = (4 * 8 + 4) * 4;
        let pixel: [u8; 4] = pixels[offset..offset+4].try_into().unwrap();
        eprintln!("bump_offscreen_ffi_pixel {:?}", pixel);
        assert_eq!(pixel, [255,115,57,255]);
        assert!(target.submission_serial > 0);
        let before_zero_write = target.submission_serial;
        state.color_write = 0;
        pixels.fill(0x65);
        let status = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
        let message = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy();
        assert_eq!(status, CoinWgpuStatus::Ok, "{message}");
        let no_color: [u8; 4] = pixels[offset..offset+4].try_into().unwrap();
        eprintln!("bump_offscreen_ffi_zero_write_pixel {:?}", no_color);
        assert_eq!(no_color, [0,0,0,255],
            "zero channel writes preserve authorial clear rather than stale color");
        assert!(target.submission_serial > before_zero_write);
        let before_write_recovery = target.submission_serial;
        state.color_write = 1;
        let status = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
        let message = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy();
        assert_eq!(status, CoinWgpuStatus::Ok, "{message}");
        let recovered: [u8; 4] = pixels[offset..offset+4].try_into().unwrap();
        assert_eq!(recovered, [255,115,57,255],
            "same target restores the complete diffuse plus specular result");
        assert!(target.submission_serial > before_write_recovery);
        let first_serial = target.submission_serial;
        state.depth_function = 2;
        pixels.fill(0);
        let status = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
        let message = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy();
        assert_eq!(status, CoinWgpuStatus::Ok, "{message}");
        let less_pixel: [u8; 4] = pixels[offset..offset+4].try_into().unwrap();
        eprintln!("bump_offscreen_ffi_less_pixel {:?}", less_pixel);
        assert_eq!(less_pixel, [255,115,57,255]);
        assert!(target.submission_serial > first_serial);
    }

    #[test]
    #[ignore = "requires a selected physical GPU; run with --ignored"]
    fn msaa_offscreen_ffi_resolves_partial_edge() {
        let mut vertices = [CoinWgpuVertex::zeroed(); 3];
        vertices[0].position = [-1.0, -1.0, 0.0];
        vertices[1].position = [1.0, -1.0, 0.0];
        vertices[2].position = [-1.0, 0.83, 0.0];
        for vertex in &mut vertices { vertex.normal = [0.0, 0.0, 1.0]; }
        let indices = [0u32, 1, 2];
        let draw = CoinWgpuDraw { topology: 0, first_index: 0, index_count: 3,
            render_state_slot: 0, ..CoinWgpuDraw::zeroed() };
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.model_view = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                            0.0,0.0,1.0,0.0, 0.0,0.0,-5.0,1.0];
        state.model_view_projection = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                                       0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0];
        state.normal_matrix = state.model_view_projection;
        state.viewport = [0, 0, 32, 32];
        state.depth_test = 1;
        state.depth_write = 1;
        state.depth_function = 3;
        state.color_write = 1;
        state.polygon_offset_primitive_style = 1;
        let mut material = CoinWgpuMaterial::zeroed();
        material.diffuse = [1.0, 0.0, 0.0, 1.0];
        material.emission = [1.0, 0.0, 0.0, 1.0];
        let mut frame: CoinWgpuFrameView = unsafe { std::mem::zeroed() };
        frame.multipass_count = 1; // ABI 69 requires an explicit single pass.
        frame.abi_version = COIN_WGPU_ABI_VERSION;
        frame.struct_size = std::mem::size_of::<CoinWgpuFrameView>() as u32;
        frame.vertices = vertices.as_ptr(); frame.vertex_count = 3;
        frame.indices = indices.as_ptr(); frame.index_count = 3;
        frame.draws = &draw; frame.draw_count = 1;
        frame.materials = &material; frame.material_count = 1;
        frame.states = &state; frame.state_count = 1;
        frame.width = 32; frame.height = 32;
        frame.clear_color = [0.0, 0.0, 0.0, 0.0];
        frame.sorted_layers_passes = 1;
        let mut captures = Vec::new();
        for sample_count in [1, 2, 4] {
            let mut pixels = vec![0u8; 32 * 32 * 4];
            let mut target = CoinWgpuTarget { width: 32, height: 32,
                color_buffer: pixels.as_mut_ptr(), color_buffer_len: pixels.len() as u64,
                depth_buffer: std::ptr::null_mut(), depth_buffer_len: 0,
                submission_serial: 0, device_id: 0, sample_count };
            let mut error = [0 as std::os::raw::c_char; 512];
            let status = coin_wgpu_submit(&mut target, &frame, error.as_mut_ptr(), error.len());
            let message = unsafe { std::ffi::CStr::from_ptr(error.as_ptr()) }.to_string_lossy();
            assert_eq!(status, CoinWgpuStatus::Ok, "{sample_count}x: {message}");
            assert!(target.submission_serial > 0);
            let partial = pixels.chunks_exact(4).filter(|pixel| pixel[3] > 0 && pixel[3] < 255).count();
            eprintln!("msaa_offscreen sample_count={sample_count} partial_alpha={partial}");
            captures.push((pixels, partial));
        }
        assert_eq!(captures[0].1, 0);
        assert!(captures[1].1 > 0 && captures[2].1 > 0);
        assert_ne!(captures[0].0, captures[2].0);
    }
