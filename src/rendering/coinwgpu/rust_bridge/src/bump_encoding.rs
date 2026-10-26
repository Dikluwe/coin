//! Bump map uploads, validation and opaque draw encoding.

use super::*;

pub(super) fn bump_payload_present(frame: &CoinWgpuFrameView) -> bool {
    frame.bump_vertex_count != 0 || !frame.bump_vertices.is_null() ||
        frame.bump_image_count != 0 || !frame.bump_images.is_null()
}

// ABI60 reserves bits 0..3 for anisotropy-minus-one and bit 4 for
// wrap-R. R addressing has no effect on a 2D base image, but remains valid.
pub(super) fn bump_base_anisotropy(settings: &CoinWgpuSampler)
    -> Result<u16, (CoinWgpuStatus, String)> {
    if settings.wrap_s > 1 || settings.wrap_t > 1 || settings.filter > 3 ||
        settings.reserved & !31 != 0 {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid bump base sampler bits".into()));
    }
    texture_profile::anisotropy(settings.reserved & 15, settings.filter)
}

pub(super) fn upload_bump_base_image(device: &wgpu::Device, queue: &wgpu::Queue,
    source: &CoinWgpuTexture, settings: &CoinWgpuSampler,
) -> Result<GpuBumpBaseImage, (CoinWgpuStatus, String)> {
    let anisotropy = bump_base_anisotropy(settings)?;
    let levels = texture_levels(source.width, source.height, source.format)?;
    let texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("Coin bump base image"),
        size: wgpu::Extent3d { width: source.width, height: source.height, depth_or_array_layers: 1 },
        mip_level_count: levels.len() as u32, sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: if matches!(source.format, 3 | 4) {
            wgpu::TextureFormat::Rgba8UnormSrgb
        } else { wgpu::TextureFormat::Rgba8Unorm },
        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
        view_formats: &[],
    });
    let pixels = unsafe { std::slice::from_raw_parts(source.pixels, source.pixel_bytes_len as usize) };
    for (level, &(width, height, offset)) in levels.iter().enumerate() {
        let bytes = u64::from(width) * u64::from(height) * 4;
        queue.write_texture(wgpu::TexelCopyTextureInfo {
            texture: &texture, mip_level: level as u32,
            origin: wgpu::Origin3d::ZERO, aspect: wgpu::TextureAspect::All,
        }, &pixels[offset as usize..(offset + bytes) as usize],
            wgpu::TexelCopyBufferLayout { offset: 0, bytes_per_row: Some(width * 4),
                rows_per_image: Some(height) },
            wgpu::Extent3d { width, height, depth_or_array_layers: 1 });
    }
    let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
    let address = |wrap| if wrap == 0 { wgpu::AddressMode::Repeat } else { wgpu::AddressMode::ClampToEdge };
    let filter = if settings.filter == 0 { wgpu::FilterMode::Nearest } else { wgpu::FilterMode::Linear };
    let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
        address_mode_u: address(settings.wrap_s), address_mode_v: address(settings.wrap_t),
        mag_filter: filter,
        min_filter: if settings.filter == 2 { wgpu::FilterMode::Nearest } else { filter },
        mipmap_filter: if settings.filter >= 2 { wgpu::FilterMode::Linear } else { wgpu::FilterMode::Nearest },
        lod_max_clamp: if settings.filter >= 2 { 32.0 } else { 0.0 },
        anisotropy_clamp: anisotropy,
        ..Default::default()
    });
    Ok(GpuBumpBaseImage { _texture: texture, view, sampler })
}

pub(super) fn multipass_upload_buffer(device:&wgpu::Device, descriptor:&wgpu::util::BufferInitDescriptor<'_>) -> wgpu::Buffer {
    use wgpu::util::DeviceExt;
    MULTIPASS_UPLOADS.with(|slot| {
        let mut slot=slot.borrow_mut();
        if let Some(replay)=slot.as_mut() {
            let key=(descriptor.usage.bits(),descriptor.contents.to_vec());
            replay.buffers.entry(key).or_insert_with(||device.create_buffer_init(descriptor)).clone()
        } else { device.create_buffer_init(descriptor) }
    })
}

pub(super) fn upload_bump_payload(device: &wgpu::Device, queue: &wgpu::Queue,
                       payload: &ValidatedBumpPayload) -> Result<GpuBumpPayload, String> {
    if let Some(cached)=MULTIPASS_UPLOADS.with(|slot|slot.borrow().as_ref().and_then(|replay|replay.bump.clone())) {
        return Ok(cached);
    }
    if payload.vertices.is_empty() || payload.images.is_empty() {
        return Err("Empty bump GPU payload".to_string());
    }
    if payload.images.iter().any(|image|
        image.width > device.limits().max_texture_dimension_2d ||
        image.height > device.limits().max_texture_dimension_2d) {
        return Err("Bump image exceeds GPU texture dimension limit".to_string());
    }
    let vertex_bytes = bytemuck::cast_slice(&payload.vertices);
    let vertices = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("Coin bump vertex stream"), size: vertex_bytes.len() as u64,
        usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST |
            wgpu::BufferUsages::COPY_SRC,
        mapped_at_creation: false,
    });
    queue.write_buffer(&vertices, 0, vertex_bytes);
    let mut images = Vec::with_capacity(payload.images.len());
    for source in &payload.images {
        let mut rgba = Vec::with_capacity(source.rgb.len() / 3 * 4);
        for rgb in source.rgb.chunks_exact(3) {
            rgba.extend_from_slice(&[rgb[0], rgb[1], rgb[2], 255]);
        }
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Coin bump normal image"),
            size: wgpu::Extent3d { width: source.width, height: source.height,
                depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST |
                wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        queue.write_texture(texture.as_image_copy(), &rgba,
            wgpu::TexelCopyBufferLayout { offset: 0,
                bytes_per_row: Some(source.width * 4), rows_per_image: Some(source.height) },
            texture.size());
        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            address_mode_u: if source.clamp_s { wgpu::AddressMode::ClampToEdge }
                else { wgpu::AddressMode::Repeat },
            address_mode_v: if source.clamp_t { wgpu::AddressMode::ClampToEdge }
                else { wgpu::AddressMode::Repeat },
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default()
        });
        images.push(GpuBumpImage { texture, view, sampler });
    }
    let result=GpuBumpPayload { vertices, images };
    MULTIPASS_UPLOADS.with(|slot| {
        if let Some(replay)=slot.borrow_mut().as_mut() { replay.bump=Some(result.clone()); }
    });
    Ok(result)
}

#[allow(dead_code)]
pub(super) fn bump_uniforms_from_state(state: &CoinWgpuRenderState,
                            material: &CoinWgpuMaterial) -> Result<BumpUniforms, String> {
    if state.light_count != 1 ||
        !matches!(state.lights[0].position_type[3], 0.0 | 1.0 | 2.0) ||
        state.model_view[3] != 0.0 || state.model_view[7] != 0.0 ||
        state.model_view[11] != 0.0 || state.model_view[15] != 1.0 ||
        material.transparency != 0.0 || material.diffuse[3] != 1.0 {
        return Err("Bump lighting state is outside the initial opaque one-light profile".to_string());
    }
    let matrices_finite = state.model_view.iter().chain(state.model_view_projection.iter())
        .chain(state.normal_matrix.iter()).all(|v| v.is_finite());
    let light = &state.lights[0];
    if !matrices_finite || !light.position_type.iter().all(|v| v.is_finite()) ||
        !light.direction_cutoff.iter().all(|v| v.is_finite()) ||
        !light.color_intensity.iter().all(|v| v.is_finite()) ||
        !material.diffuse.iter().chain(material.specular.iter()).all(|v| v.is_finite()) ||
        !material.shininess.is_finite() || !(0.0..=1.0).contains(&material.shininess) {
        return Err("Nonfinite or invalid bump lighting state".to_string());
    }
    // normal_matrix is inverse(model_view linear)^T. Its transpose converts a
    // view-space direction back to the object's S/T/N input space.
    let inverse_direction = |v: [f32; 3]| -> [f32; 3] {
        let n = &state.normal_matrix;
        [n[0]*v[0] + n[1]*v[1] + n[2]*v[2],
         n[4]*v[0] + n[5]*v[1] + n[6]*v[2],
         n[8]*v[0] + n[9]*v[1] + n[10]*v[2]]
    };
    let translation = [state.model_view[12], state.model_view[13], state.model_view[14]];
    let view_light = [light.position_type[0], light.position_type[1],
                      light.position_type[2]];
    let object_light = if light.position_type[3] == 0.0 {
        inverse_direction([-light.direction_cutoff[0], -light.direction_cutoff[1],
                           -light.direction_cutoff[2]])
    } else {
        inverse_direction([view_light[0] - translation[0],
                           view_light[1] - translation[1],
                           view_light[2] - translation[2]])
    };
    let object_eye = inverse_direction([-translation[0], -translation[1], -translation[2]]);
    if !object_light.iter().chain(object_eye.iter()).all(|v| v.is_finite()) {
        return Err("Bump object-space light or eye overflows".to_string());
    }
    let intensity = light.color_intensity[3];
    if intensity < 0.0 {
        return Err("Negative bump light intensity".to_string());
    }
    let mut diffuse = [0.0; 4];
    let mut specular = material.specular;
    for axis in 0..3 {
        diffuse[axis] = material.diffuse[axis] * light.color_intensity[axis] * intensity;
    }
    diffuse[3] = 1.0;
    specular[3] = 1.0;
    if !diffuse.iter().all(|v| v.is_finite()) {
        return Err("Bump diffuse light product overflows".to_string());
    }
    Ok(BumpUniforms {
        model_view_projection: state.model_view_projection,
        light_position_type: [object_light[0], object_light[1], object_light[2],
                              light.position_type[3]],
        diffuse, eye_position: [object_eye[0], object_eye[1], object_eye[2], 1.0],
        specular, shininess: [material.shininess, 0.0, 0.0, 0.0],
    })
}

#[allow(dead_code)]
pub(super) fn create_bump_pipeline(device: &wgpu::Device, color_format: wgpu::TextureFormat,
                        depth_stencil: Option<wgpu::DepthStencilState>,
                        fragment_entry: &str,
                        blend: Option<wgpu::BlendState>) -> wgpu::RenderPipeline {
    create_bump_pipeline_samples(device, color_format, depth_stencil,
        fragment_entry, blend, 1, wgpu::ColorWrites::ALL)
}

pub(super) fn create_bump_pipeline_samples(device: &wgpu::Device, color_format: wgpu::TextureFormat,
                                depth_stencil: Option<wgpu::DepthStencilState>,
                                fragment_entry: &str, blend: Option<wgpu::BlendState>,
                                sample_count: u32, color_writes: wgpu::ColorWrites) -> wgpu::RenderPipeline {
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_bump.wgsl"),
        source: wgpu::ShaderSource::Wgsl(include_str!("../../shaders/coin_bump.wgsl").into()),
    });
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("Coin bump lighting pipeline"), layout: None,
        vertex: wgpu::VertexState {
            module: &shader, entry_point: Some("vs_main"),
            compilation_options: Default::default(),
            buffers: &[
                wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<CoinWgpuVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Vertex,
                    attributes: &wgpu::vertex_attr_array![0 => Float32x3, 1 => Float32x3],
                },
                wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<CoinWgpuBumpVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Vertex,
                    attributes: &wgpu::vertex_attr_array![2 => Float32x4, 3 => Float32x3,
                        4 => Float32x3],
                },
            ],
        },
        fragment: Some(wgpu::FragmentState {
            module: &shader, entry_point: Some(fragment_entry),
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format: color_format,
                blend, write_mask: color_writes,
            })],
        }),
        primitive: wgpu::PrimitiveState { cull_mode: None, ..Default::default() },
        depth_stencil,
        multisample: wgpu::MultisampleState { count: sample_count, ..Default::default() },
        multiview: None, cache: None,
    })
}

pub(super) fn create_bump_base_pipeline(device: &wgpu::Device, color_format: wgpu::TextureFormat,
                             depth_stencil: Option<wgpu::DepthStencilState>) -> wgpu::RenderPipeline {
    create_bump_base_pipeline_samples(device, color_format, depth_stencil, 1, wgpu::ColorWrites::ALL)
}

pub(super) fn create_bump_base_pipeline_samples(device: &wgpu::Device, color_format: wgpu::TextureFormat,
                                     depth_stencil: Option<wgpu::DepthStencilState>,
                                     sample_count: u32, color_writes: wgpu::ColorWrites) -> wgpu::RenderPipeline {
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_bump.wgsl base texture"),
        source: wgpu::ShaderSource::Wgsl(include_str!("../../shaders/coin_bump.wgsl").into()),
    });
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("Coin bump base texture pipeline"), layout: None,
        vertex: wgpu::VertexState {
            module: &shader, entry_point: Some("vs_base"),
            compilation_options: Default::default(),
            buffers: &[
                wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<CoinWgpuVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Vertex,
                    attributes: &[
                        wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x3,
                            offset: 0, shader_location: 0 },
                        wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x3,
                            offset: 12, shader_location: 1 },
                        wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4,
                            offset: 24, shader_location: 5 },
                    ],
                },
                wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<CoinWgpuBumpVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Vertex,
                    attributes: &wgpu::vertex_attr_array![2 => Float32x4, 3 => Float32x3,
                        4 => Float32x3],
                },
            ],
        },
        fragment: Some(wgpu::FragmentState {
            module: &shader, entry_point: Some("fs_base"),
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format: color_format, blend: None, write_mask: color_writes,
            })],
        }),
        primitive: wgpu::PrimitiveState { cull_mode: None, ..Default::default() },
        depth_stencil,
        multisample: wgpu::MultisampleState { count: sample_count, ..Default::default() },
        multiview: None, cache: None,
    })
}

pub(super) fn create_bump_pipelines(device: &wgpu::Device, color_format: wgpu::TextureFormat,
                         depth_compare: wgpu::CompareFunction,
                         sample_count: u32, color_writes: wgpu::ColorWrites) -> BumpPipelines {
    let depth_write = Some(wgpu::DepthStencilState {
        format: wgpu::TextureFormat::Depth32Float, depth_write_enabled: true,
        depth_compare, stencil: Default::default(), bias: Default::default(),
    });
    let depth_read = Some(wgpu::DepthStencilState {
        format: wgpu::TextureFormat::Depth32Float, depth_write_enabled: false,
        depth_compare: wgpu::CompareFunction::LessEqual,
        stencil: Default::default(), bias: Default::default(),
    });
    BumpPipelines {
        base: create_bump_base_pipeline_samples(device, color_format, depth_write.clone(), sample_count, color_writes),
        diffuse: create_bump_pipeline_samples(device, color_format, depth_write, "fs_main", None,
            sample_count, color_writes),
        specular: create_bump_pipeline_samples(device, color_format, depth_read, "fs_specular",
            Some(wgpu::BlendState {
                color: wgpu::BlendComponent { src_factor: wgpu::BlendFactor::One,
                    dst_factor: wgpu::BlendFactor::One, operation: wgpu::BlendOperation::Add },
                alpha: wgpu::BlendComponent { src_factor: wgpu::BlendFactor::One,
                    dst_factor: wgpu::BlendFactor::One, operation: wgpu::BlendOperation::Add },
            }), sample_count, color_writes),
    }
}

// Dedicated encoder for the first opaque bump profile. It is deliberately
// separate from the standard shader: that shader has no free interpolators
// for the captured S/T basis. Callers must preflight every draw before this
// function changes a target.
pub(super) fn encode_bump_opaque_frame(
    device: &wgpu::Device, queue: &wgpu::Queue,
    pipeline_cache: Option<&Mutex<HashMap<BumpPipelineKey, Arc<BumpPipelines>>>>,
    base_cache: Option<(&Mutex<BumpBaseCache>, u64, u64)>,
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState], materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload, color_view: &wgpu::TextureView,
    color_format: wgpu::TextureFormat, depth_view: &wgpu::TextureView,
    target_size: [u32; 2], clear_color: [f32; 4],
) -> Result<wgpu::CommandBuffer, (CoinWgpuStatus, String)> {
    encode_bump_opaque_frame_samples(device, queue, pipeline_cache, base_cache,
        vertices, indices, draws, states, materials, textures, samplers, payload,
        color_view, None, color_format, depth_view, target_size, clear_color, 1)
}

pub(super) fn validate_bump_draw_profile(
    device: &wgpu::Device, vertices: &[CoinWgpuVertex], indices: &[u32],
    draws: &[CoinWgpuDraw], states: &[CoinWgpuRenderState], materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler], target_size: [u32; 2],
    consumed: &[bool],
) -> Result<(Vec<BumpUniforms>, Option<u32>), (CoinWgpuStatus, String)> {
    let unsupported = |msg: &str| (CoinWgpuStatus::Unsupported, msg.to_string());
    let mut uniforms = Vec::with_capacity(draws.len());
    let mut depth_function = None;
    if consumed.len() != draws.len() {
        return Err((CoinWgpuStatus::InvalidArgument, "Invalid bump occurrence count".into()));
    }
    for (draw, active) in draws.iter().zip(consumed) {
        let st = states.get(draw.render_state_slot as usize)
            .ok_or((CoinWgpuStatus::InvalidArgument, "Bump state slot out of bounds".into()))?;
        let material = materials.get(st.material_slot as usize)
            .ok_or((CoinWgpuStatus::InvalidArgument, "Bump material slot out of bounds".into()))?;
        if st.extra_textures.iter().any(|unit| unit.enabled != 0) ||
            st.fog_mode != 0 || st.depth_test != 1 || st.depth_write != 1 ||
            !matches!(st.depth_function, 2 | 3) || st.cull_mode != 0 ||
            (st.viewport != [0,0,0,0] &&
             st.viewport != [0,0,target_size[0] as i32,target_size[1] as i32]) ||
            draw.composition_flags != 0 ||
            draw.clear_depth_before != 0 {
            return Err(unsupported(&format!("Bump frame needs opaque, full-target depth rendering (fog={}, depth={}/{}/{}, cull={}, viewport={:?}, composition={}, clear_depth={})",
                st.fog_mode, st.depth_test, st.depth_write, st.depth_function,
                st.cull_mode, st.viewport, draw.composition_flags, draw.clear_depth_before)));
        }
        if st.has_texture != 0 {
            if st.texture_model != 0 || st.texture_projection != 0 {
                return Err(unsupported("Bump base texture requires projective MODULATE"));
            }
            let texture = textures.get(st.texture_slot as usize)
                .ok_or((CoinWgpuStatus::InvalidArgument, "Bump base texture slot out of bounds".into()))?;
            let sampler = samplers.get(st.sampler_slot as usize)
                .ok_or((CoinWgpuStatus::InvalidArgument, "Bump base sampler slot out of bounds".into()))?;
            let levels = texture_levels(texture.width, texture.height, texture.format)?;
            let byte_count = levels.last().and_then(|&(width, height, offset)|
                u64::from(width).checked_mul(u64::from(height))
                    .and_then(|pixels| pixels.checked_mul(4))
                    .and_then(|bytes| offset.checked_add(bytes)));
            if !matches!(texture.format, 0 | 2 | 3 | 4) || texture.width == 0 || texture.height == 0 ||
                texture.width > device.limits().max_texture_dimension_2d ||
                texture.height > device.limits().max_texture_dimension_2d ||
                byte_count != Some(texture.pixel_bytes_len) ||
                texture.pixel_bytes_len > 48 * 1024 * 1024 || texture.pixels.is_null() ||
                sampler.wrap_s > 1 || sampler.wrap_t > 1 || sampler.filter > 3 ||
                (sampler.filter >= 2 && !matches!(texture.format, 2 | 4)) ||
                bump_base_anisotropy(sampler).is_err() ||
                !st.texture_matrix.iter().all(|v| v.is_finite()) {
                return Err(unsupported(&format!("Bump base image or sampler is outside opaque RGBA8 profile (format={}, size={}x{}, bytes={}, wraps={}/{}, filter={}, anisotropy={})",
                    texture.format, texture.width, texture.height, texture.pixel_bytes_len,
                    sampler.wrap_s, sampler.wrap_t, sampler.filter, sampler.reserved)));
            }
            let pixels = unsafe { std::slice::from_raw_parts(texture.pixels,
                texture.pixel_bytes_len as usize) };
            if pixels.chunks_exact(4).any(|rgba| rgba[3] != 255) {
                return Err(unsupported("Bump base image alpha needs transparency composition"));
            }
            let start = draw.first_index as usize;
            let end = start + draw.index_count as usize;
            validate_texture_coordinates_with_images(st, draw.topology, vertices, &indices[start..end], textures)
                .map_err(|e| (CoinWgpuStatus::InvalidArgument, e))?;
        }
        if depth_function.is_some_and(|function| function != st.depth_function) {
            return Err(unsupported("Bump draws with different depth functions need separate pipelines"));
        }
        depth_function = Some(st.depth_function);
        if *active {
            if st.light_model != 1 { return Err(unsupported("Consumed bump requires PHONG")); }
            uniforms.push(bump_uniforms_from_state(st, material)
                .map_err(|e| (CoinWgpuStatus::Unsupported, e))?);
        }
    }
    Ok((uniforms, depth_function))
}

pub(super) fn encode_bump_opaque_frame_samples(
    device: &wgpu::Device, queue: &wgpu::Queue,
    pipeline_cache: Option<&Mutex<HashMap<BumpPipelineKey, Arc<BumpPipelines>>>>,
    base_cache: Option<(&Mutex<BumpBaseCache>, u64, u64)>,
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState], materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload, color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>, color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView, target_size: [u32; 2], clear_color: [f32; 4],
    sample_count: u32,
) -> Result<wgpu::CommandBuffer, (CoinWgpuStatus, String)> {
    if resolve_view.is_some() != (sample_count > 1) {
        return Err((CoinWgpuStatus::InvalidArgument,
            "Bump MSAA attachments require matching sample count and color resolve".into()));
    }
    encode_bump_opaque_run(device, queue, pipeline_cache, base_cache, vertices,
        indices, draws, states, materials, textures, samplers, payload, color_view,
        resolve_view, color_format, depth_view, target_size, clear_color, sample_count, true)
}

pub(super) fn encode_bump_opaque_run(
    device: &wgpu::Device, queue: &wgpu::Queue,
    pipeline_cache: Option<&Mutex<HashMap<BumpPipelineKey, Arc<BumpPipelines>>>>,
    base_cache: Option<(&Mutex<BumpBaseCache>, u64, u64)>,
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    states: &[CoinWgpuRenderState], materials: &[CoinWgpuMaterial],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
    payload: &ValidatedBumpPayload, color_view: &wgpu::TextureView,
    resolve_view: Option<&wgpu::TextureView>, color_format: wgpu::TextureFormat,
    depth_view: &wgpu::TextureView, target_size: [u32; 2], clear_color: [f32; 4],
    sample_count: u32, clear_attachments: bool,
) -> Result<wgpu::CommandBuffer, (CoinWgpuStatus, String)> {
    use wgpu::util::DeviceExt;
    let unsupported = |msg: &str| (CoinWgpuStatus::Unsupported, msg.to_string());
    if !matches!(sample_count, 1 | 2 | 4) || (resolve_view.is_some() && sample_count == 1) {
        return Err((CoinWgpuStatus::InvalidArgument,
            "Bump MSAA attachments require matching sample count and color resolve".into()));
    }
    if draws.is_empty() || payload.vertices.len() != vertices.len() ||
        !clear_color.iter().all(|v| v.is_finite()) {
        return Err((CoinWgpuStatus::InvalidArgument,
            "Invalid bump frame geometry or clear color".into()));
    }
    let slots = bump_image_slots_for_draws(payload, indices, draws)
        .map_err(|e| (CoinWgpuStatus::InvalidArgument, e))?;
    if slots.iter().any(Option::is_none) {
        return Err(unsupported("Mixed bump and non-bump draws need the normal base pass"));
    }
    let (uniforms, depth_function) = validate_bump_draw_profile(device, vertices, indices,
        draws, states, materials, textures, samplers, target_size,
        &slots.iter().map(Option::is_some).collect::<Vec<_>>())?;
    let gpu = upload_bump_payload(device, queue, payload)
        .map_err(|e| (CoinWgpuStatus::Unsupported, e))?;
    let main_vertices = multipass_upload_buffer(device, &wgpu::util::BufferInitDescriptor {
        label: Some("Coin bump positions/normals"),
        contents: bytemuck::cast_slice(vertices), usage: wgpu::BufferUsages::VERTEX,
    });
    let index_buffer = multipass_upload_buffer(device, &wgpu::util::BufferInitDescriptor {
        label: Some("Coin bump indices"),
        contents: bytemuck::cast_slice(indices), usage: wgpu::BufferUsages::INDEX,
    });
    let depth_compare = if depth_function == Some(2) {
        wgpu::CompareFunction::Less
    } else { wgpu::CompareFunction::LessEqual };
    // Channel writes belong to each occurrence, including adjacent draws in
    // one bump run. Keep the matching auto-layout pipelines and bindgroups
    // together; a cached ALL pipeline cannot stand in for a partial mask.
    let mut pipelines = Vec::with_capacity(draws.len());
    let mut run_pipelines = HashMap::<u32, Arc<BumpPipelines>>::new();
    for draw in draws {
        let encoded = states[draw.render_state_slot as usize].color_write;
        let color_writes = if encoded == 1 { wgpu::ColorWrites::ALL }
            else { wgpu::ColorWrites::from_bits_truncate(encoded & 0x0f) };
        if let Some(pipeline) = run_pipelines.get(&color_writes.bits()) {
            pipelines.push(pipeline.clone());
            continue;
        }
        let key = BumpPipelineKey { color_format, depth_compare, sample_count,
            color_writes: color_writes.bits() };
        let pipeline = if let Some(cache) = pipeline_cache {
            let mut cache = cache.lock().map_err(|e|
                (CoinWgpuStatus::BackendError, format!("Bump pipeline cache lock failed: {e}")))?;
            cache.entry(key).or_insert_with(|| Arc::new(create_bump_pipelines(
                device, color_format, depth_compare, sample_count, color_writes))).clone()
        } else { Arc::new(create_bump_pipelines(device, color_format, depth_compare,
                                               sample_count, color_writes)) };
        run_pipelines.insert(color_writes.bits(), pipeline.clone());
        pipelines.push(pipeline);
    }
    let mut uniform_buffers = Vec::with_capacity(uniforms.len());
    for uniform in &uniforms {
        uniform_buffers.push(multipass_upload_buffer(device, &wgpu::util::BufferInitDescriptor {
            label: Some("Coin bump uniforms"), contents: bytemuck::bytes_of(uniform),
            usage: wgpu::BufferUsages::UNIFORM,
        }));
    }
    let make_binds = |specular: bool| {
        draws.iter().enumerate().map(|(i, _)| {
            let pipeline = if specular { &pipelines[i].specular } else { &pipelines[i].diffuse };
            let image = &gpu.images[slots[i].unwrap()];
            device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("Coin bump draw image"), layout: &pipeline.get_bind_group_layout(0),
                entries: &[
                    wgpu::BindGroupEntry { binding: 0,
                        resource: uniform_buffers[i].as_entire_binding() },
                    wgpu::BindGroupEntry { binding: 1,
                        resource: wgpu::BindingResource::TextureView(&image.view) },
                    wgpu::BindGroupEntry { binding: 2,
                        resource: wgpu::BindingResource::Sampler(&image.sampler) },
                ],
            })
        }).collect::<Vec<_>>()
    };
    let diffuse_binds = make_binds(false);
    let specular_binds = make_binds(true);
    let mut base_resources = Vec::with_capacity(draws.len());
    let mut base_images = Vec::<GpuBumpBaseImage>::new();
    let mut image_slots = HashMap::<(u32,u32),usize>::new();
    for draw in draws {
        let st = &states[draw.render_state_slot as usize];
        if st.has_texture == 0 { base_resources.push(None); continue; }
        let key = (st.texture_slot, st.sampler_slot);
        let image_index = if let Some(&index) = image_slots.get(&key) { index } else {
            let source = &textures[st.texture_slot as usize];
            let settings = &samplers[st.sampler_slot as usize];
            let pixels = unsafe { std::slice::from_raw_parts(source.pixels, source.pixel_bytes_len as usize) };
            let image = if let Some((cache, serial, completed)) = base_cache {
                let mut cache = cache.lock().map_err(|e|
                    (CoinWgpuStatus::BackendError, format!("Bump base cache lock failed: {e}")))?;
                cache.retired.retain(|(_, retired_at)| *retired_at > completed);
                let cache_key = BumpBaseKey {
                    width: source.width, height: source.height, format: source.format,
                    digest: source.content_digest, wrap_s: settings.wrap_s,
                    wrap_t: settings.wrap_t, filter: settings.filter, anisotropy: settings.reserved & 15,
                };
                let hit = cache.entries.get_mut(&cache_key).and_then(|entry| {
                    if entry.pixels_copy == pixels {
                        entry.last_used_serial = serial;
                        Some(entry.image.clone())
                    } else { None }
                });
                if let Some(image) = hit {
                    cache.hits += 1;
                    image
                } else {
                    let image = upload_bump_base_image(device, queue, source, settings)?;
                    let old = cache.entries.insert(cache_key, BumpBaseCacheEntry {
                        image: image.clone(), pixels_copy: pixels.to_vec(),
                        last_used_serial: serial,
                    });
                    if let Some(old) = old {
                        cache.retired.push((old.image, old.last_used_serial));
                    }
                    cache.uploads += 1;
                    image
                }
            } else { upload_bump_base_image(device, queue, source, settings)? };
            let index = base_images.len();
            base_images.push(image);
            image_slots.insert(key, index);
            index
        };
        let uniform = multipass_upload_buffer(device, &wgpu::util::BufferInitDescriptor {
            label: Some("Coin bump base transform"),
            contents: bytemuck::bytes_of(&BumpBaseUniforms { texture_matrix: st.texture_matrix }),
            usage: wgpu::BufferUsages::UNIFORM,
        });
        base_resources.push(Some(GpuBumpBaseDraw { image_index, uniform }));
    }
    if let Some((cache, serial, _)) = base_cache {
        let mut cache = cache.lock().map_err(|e|
            (CoinWgpuStatus::BackendError, format!("Bump base cache lock failed: {e}")))?;
        let stale: Vec<_> = cache.entries.iter().filter(|(_, entry)|
            serial.saturating_sub(entry.last_used_serial) > 8)
            .map(|(key, _)| key.clone()).collect();
        for key in stale {
            if let Some(entry) = cache.entries.remove(&key) {
                cache.retired.push((entry.image, entry.last_used_serial));
            }
        }
    }
    #[cfg(test)]
    LAST_BUMP_BASE_IMAGE_UPLOADS.with(|count| count.set(base_images.len()));
    let base_binds = draws.iter().enumerate().map(|(i, _)| {
        base_resources[i].as_ref().map(|resource| {
            let base_pipeline = &pipelines[i].base;
            let image = &gpu.images[slots[i].unwrap()];
            let base_image = &base_images[resource.image_index];
            let group0 = device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("Coin bump textured draw"), layout: &base_pipeline.get_bind_group_layout(0),
                entries: &[
                    wgpu::BindGroupEntry { binding: 0, resource: uniform_buffers[i].as_entire_binding() },
                    wgpu::BindGroupEntry { binding: 1, resource: wgpu::BindingResource::TextureView(&image.view) },
                    wgpu::BindGroupEntry { binding: 2, resource: wgpu::BindingResource::Sampler(&image.sampler) },
                ],
            });
            let group1 = device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("Coin bump base draw"), layout: &base_pipeline.get_bind_group_layout(1),
                entries: &[
                    wgpu::BindGroupEntry { binding: 0, resource: resource.uniform.as_entire_binding() },
                    wgpu::BindGroupEntry { binding: 1, resource: wgpu::BindingResource::TextureView(&base_image.view) },
                    wgpu::BindGroupEntry { binding: 2, resource: wgpu::BindingResource::Sampler(&base_image.sampler) },
                ],
            });
            (group0, group1)
        })
    }).collect::<Vec<_>>();
    let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label: Some("Coin opaque bump frame"),
    });
    {
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("Coin opaque bump draws"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: color_view, resolve_target: resolve_view,
                ops: wgpu::Operations { load: if clear_attachments { wgpu::LoadOp::Clear(wgpu::Color {
                    r: clear_color[0] as f64, g: clear_color[1] as f64,
                    b: clear_color[2] as f64, a: clear_color[3] as f64 }) } else { wgpu::LoadOp::Load },
                    store: wgpu::StoreOp::Store },
            })],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view: depth_view, depth_ops: Some(wgpu::Operations {
                    load: if clear_attachments { wgpu::LoadOp::Clear(1.0) } else { wgpu::LoadOp::Load }, store: wgpu::StoreOp::Store }),
                stencil_ops: None,
            }), timestamp_writes: None, occlusion_query_set: None,
        });
        pass.set_vertex_buffer(0, main_vertices.slice(..));
        pass.set_vertex_buffer(1, gpu.vertices.slice(..));
        pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
        for (i, draw) in draws.iter().enumerate() {
            let range = draw.first_index..draw.first_index + draw.index_count;
            if let Some((group0, group1)) = &base_binds[i] {
                pass.set_pipeline(&pipelines[i].base);
                pass.set_bind_group(0, group0, &[]);
                pass.set_bind_group(1, group1, &[]);
            } else {
                pass.set_pipeline(&pipelines[i].diffuse);
                pass.set_bind_group(0, &diffuse_binds[i], &[]);
            }
            pass.draw_indexed(range.clone(), 0, 0..1);
            pass.set_pipeline(&pipelines[i].specular);
            pass.set_bind_group(0, &specular_binds[i], &[]);
            pass.draw_indexed(range, 0, 0..1);
        }
    }
    Ok(encoder.finish())
}

pub(super) fn validate_bump_payload(
    frame: &CoinWgpuFrameView,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> Result<Option<ValidatedBumpPayload>, CoinWgpuStatus> {
    if !bump_payload_present(frame) { return Ok(None); }
    let invalid = |message: &str| {
        set_error(error_buf, error_buf_len, message);
        CoinWgpuStatus::InvalidArgument
    };
    if frame.bump_vertex_count == 0 || frame.bump_image_count == 0 ||
        frame.bump_vertex_count != frame.vertex_count || frame.bump_image_count > 4096 {
        return Err(invalid("Bump payload count does not match emitted geometry or images"));
    }
    let vertices = validate_slice(frame.bump_vertices, frame.bump_vertex_count,
        "bump vertices", error_buf, error_buf_len)?;
    let images = validate_slice(frame.bump_images, frame.bump_image_count,
        "bump images", error_buf, error_buf_len)?;
    if vertices.iter().any(|v| v.image_slot > frame.bump_image_count as u32 ||
        v.coord.iter().chain(v.tangent_s.iter()).chain(v.tangent_t.iter())
            .any(|x| !x.is_finite()) || (v.image_slot != 0 && v.coord[3] == 0.0)) {
        return Err(invalid("Invalid bump vertex slot, coordinate or tangent"));
    }
    let mut total_bytes = 0u64;
    let mut owned_images = Vec::with_capacity(images.len());
    for image in images {
        let expected = u64::from(image.width).checked_mul(u64::from(image.height))
            .and_then(|pixels| pixels.checked_mul(3));
        if image.width == 0 || image.height == 0 || image.width > 32767 ||
            image.height > 32767 || image.clamp_s > 1 || image.clamp_t > 1 ||
            expected != Some(image.rgb_len) || image.rgb_len > 48 * 1024 * 1024 ||
            image.rgb.is_null() {
            return Err(invalid("Invalid bump image dimensions, flags or bytes"));
        }
        total_bytes = total_bytes.checked_add(image.rgb_len)
            .ok_or_else(|| invalid("Bump image byte count overflow"))?;
        if total_bytes > 256 * 1024 * 1024 {
            return Err(invalid("Bump images exceed the transport budget"));
        }
        let bytes = validate_slice(image.rgb, image.rgb_len,
            "bump RGB bytes", error_buf, error_buf_len)?;
        owned_images.push(ValidatedBumpImage {
            width: image.width, height: image.height,
            clamp_s: image.clamp_s != 0, clamp_t: image.clamp_t != 0,
            rgb: bytes.to_vec(),
        });
    }
    Ok(Some(ValidatedBumpPayload { vertices: vertices.to_vec(), images: owned_images }))
}

// The bump image is bound per draw, while its slot travels per vertex in the
// captured plan. Resolve that boundary before encoding; interpolating slots
// across a primitive would silently sample the wrong normal image.
pub(super) fn bump_image_slots_for_draws(payload: &ValidatedBumpPayload, indices: &[u32],
                              draws: &[CoinWgpuDraw]) -> Result<Vec<Option<usize>>, String> {
    let mut slots = Vec::with_capacity(draws.len());
    for (draw_number, draw) in draws.iter().enumerate() {
        if draw.topology != 0 || draw.index_count == 0 || draw.index_count % 3 != 0 {
            return Err(format!("Bump draw {draw_number} requires indexed triangles"));
        }
        let start = draw.first_index as usize;
        let end = start.checked_add(draw.index_count as usize)
            .ok_or_else(|| format!("Bump draw {draw_number} index range overflows"))?;
        let draw_indices = indices.get(start..end)
            .ok_or_else(|| format!("Bump draw {draw_number} index range is out of bounds"))?;
        let first = payload.vertices.get(draw_indices[0] as usize)
            .ok_or_else(|| format!("Bump draw {draw_number} vertex is out of bounds"))?
            .image_slot;
        if first as usize > payload.images.len() {
            return Err(format!("Bump draw {draw_number} image slot is out of bounds"));
        }
        for &index in draw_indices {
            let vertex = payload.vertices.get(index as usize)
                .ok_or_else(|| format!("Bump draw {draw_number} vertex is out of bounds"))?;
            if vertex.image_slot != first {
                return Err(format!("Bump draw {draw_number} mixes normal image slots"));
            }
        }
        slots.push(if first == 0 { None } else { Some(first as usize - 1) });
    }
    Ok(slots)
}
