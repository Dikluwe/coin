use crate::{CoinWgpuRenderState, CoinWgpuShadowDraw, CoinWgpuVertex};
use bytemuck::{Pod, Zeroable};
use wgpu::util::DeviceExt;

#[repr(C)]
#[derive(Copy, Clone, Pod, Zeroable)]
struct ShadowUniforms {
    model_view_projection: [f32; 16],
    model_view: [f32; 16],
    near_far_kind: [f32; 4],
    clip_model_view: [f32; 16],
    clip_planes: [[f32; 4]; 8],
    raster_viewport: [f32; 4],
    alpha_test: [f32; 4],
}

#[derive(Clone)]
pub(crate) struct ShadowMap {
    pub view: wgpu::TextureView,
    moments: wgpu::Texture,
    size: u32,
    _depth: wgpu::Texture,
}

#[derive(Clone)]
pub(crate) struct ShadowArray {
    pub view: wgpu::TextureView,
    _texture: wgpu::Texture,
}

// wgpu-hal/GLES infers the native target from allocation shape and does not
// reinterpret it when creating a view. A singleton becomes TEXTURE_2D, while
// a square multiple of six becomes a cube image. Keep this explicitly typed
// D2Array allocation unambiguous on every adapter; unused layers are padding.
pub(crate) fn array_layer_count(maps: u32) -> u32 {
    let layers=maps.max(2);
    if layers%6==0 {layers+1} else {layers}
}

pub(crate) fn encode_array(device: &wgpu::Device, encoder: &mut wgpu::CommandEncoder,
    maps: &[&ShadowMap]) -> Result<ShadowArray, String> {
    let size = maps.iter().map(|map| map.size).max().unwrap_or(1);
    let layers = array_layer_count(maps.len() as u32);
    if size > device.limits().max_texture_dimension_2d || layers > device.limits().max_texture_array_layers {
        return Err("Shadow moment array exceeds device extent/layer limits".into());
    }
    let texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("Coin independent shadow moment layers"),
        size: wgpu::Extent3d { width: size, height: size, depth_or_array_layers: layers },
        mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Rgba32Float,
        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
        view_formats: &[],
    });
    for (layer, map) in maps.iter().enumerate() {
        encoder.copy_texture_to_texture(
            wgpu::TexelCopyTextureInfo { texture: &map.moments, mip_level: 0,
                origin: wgpu::Origin3d::ZERO, aspect: wgpu::TextureAspect::All },
            wgpu::TexelCopyTextureInfo { texture: &texture, mip_level: 0,
                origin: wgpu::Origin3d { x: 0, y: 0, z: layer as u32 }, aspect: wgpu::TextureAspect::All },
            wgpu::Extent3d { width: map.size, height: map.size, depth_or_array_layers: 1 });
    }
    let view = texture.create_view(&wgpu::TextureViewDescriptor {
        dimension: Some(wgpu::TextureViewDimension::D2Array), ..Default::default()
    });
    Ok(ShadowArray { view, _texture: texture })
}

// The Core/FFI payload already owns the Coin decisions. This function only
// allocates attachments and records indexed triangle draws in their source order.
// Every caster carries its own clipped map-pixel scissor. Validate before
// attachments or a surface are acquired; never reinterpret unknown flags.
pub(crate) fn validate_caster_viewports(casters: &[CoinWgpuShadowDraw], map_size: u32)
    -> Result<(), String> {
    for draw in casters {
        let [x, y, width, height] = draw.viewport;
        if draw.reserved != 0 || x < 0 || y < 0 || width < 0 || height < 0 ||
            i64::from(x) + i64::from(width) > i64::from(map_size) ||
            i64::from(y) + i64::from(height) > i64::from(map_size) {
            return Err("Shadow caster has an invalid map viewport or reserved flags".into());
        }
    }
    Ok(())
}

pub(crate) fn encode_moments(
    device: &wgpu::Device,
    encoder: &mut wgpu::CommandEncoder,
    vertices: &[CoinWgpuVertex],
    indices: &[u32],
    casters: &[CoinWgpuShadowDraw],
    states: &[CoinWgpuRenderState],
    map_size: u32,
    near: f32,
    far: f32,
    kind: u32,
    canonical_coverage: bool,
) -> Result<ShadowMap, String> {
    if !map_size.is_power_of_two()
        || map_size > 2048
        || !near.is_finite()
        || !far.is_finite()
        || (kind == 1 && near <= 0.0)
        || far <= near
        || kind > 1
    {
        return Err("Invalid wgpu shadow-map dimensions or depth range".into());
    }
    validate_caster_viewports(casters, map_size)?;
    if vertices.is_empty() || indices.is_empty() {
        return Err("Shadow casters require indexed geometry".into());
    }
    for (slot, draw) in casters.iter().enumerate() {
        let end = (draw.first_index as usize)
            .checked_add(draw.index_count as usize)
            .ok_or_else(|| format!("Shadow caster {slot} index range overflows"))?;
        if draw.index_count == 0
            || draw.render_state_slot as usize >= states.len()
            || states[draw.render_state_slot as usize].clip_plane_count > 8
            || states[draw.render_state_slot as usize].alpha_test_function > 8
            || !states[draw.render_state_slot as usize].alpha_test_reference.is_finite()
            || !(0.0..=1.0).contains(&states[draw.render_state_slot as usize].alpha_test_reference)
            || draw.index_count % 3 != 0
            || end > indices.len()
            || indices[draw.first_index as usize..end]
                .iter()
                .any(|&index| index as usize >= vertices.len())
            || !draw
                .model_view
                .iter()
                .chain(draw.model_view_projection.iter())
                .all(|v| v.is_finite())
        {
            return Err(format!(
                "Shadow caster {slot} has invalid geometry or matrices"
            ));
        }
    }

    let extent = wgpu::Extent3d {
        width: map_size,
        height: map_size,
        depth_or_array_layers: 1,
    };
    let moments = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("Coin spot shadow moments"),
        size: extent,
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Rgba32Float,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT
            | wgpu::TextureUsages::TEXTURE_BINDING
            | wgpu::TextureUsages::COPY_SRC,
        view_formats: &[],
    });
    let depth = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("Coin spot shadow depth"),
        size: extent,
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Depth32Float,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
        view_formats: &[],
    });
    let view = moments.create_view(&Default::default());
    let depth_view = depth.create_view(&Default::default());
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("coin_shadow.wgsl"),
        source: wgpu::ShaderSource::Wgsl(include_str!("../../shaders/coin_shadow.wgsl").into()),
    });
    let binding_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("Coin shadow uniform layout"),
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
        label: Some("Coin shadow pipeline layout"),
        bind_group_layouts: &[&binding_layout],
        push_constant_ranges: &[],
    });
    let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("Coin shadow moments pipeline"),
        layout: Some(&pipeline_layout),
        vertex: wgpu::VertexState {
            module: &shader,
            entry_point: Some("vs_moments"),
            compilation_options: Default::default(),
            buffers: &[wgpu::VertexBufferLayout {
                array_stride: std::mem::size_of::<CoinWgpuVertex>() as u64,
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
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format: wgpu::TextureFormat::Rgba32Float,
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
            stencil: Default::default(),
            bias: Default::default(),
        }),
        multisample: Default::default(),
        multiview: None,
        cache: None,
    });
    let vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some("Coin shadow vertices"),
        contents: bytemuck::cast_slice(vertices),
        usage: wgpu::BufferUsages::VERTEX,
    });
    let index_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some("Coin shadow indices"),
        contents: bytemuck::cast_slice(indices),
        usage: wgpu::BufferUsages::INDEX,
    });
    let mut bindings = Vec::with_capacity(casters.len());
    for draw in casters {
        let state = &states[draw.render_state_slot as usize];
        let uniforms = ShadowUniforms {
            model_view_projection: draw.model_view_projection,
            model_view: draw.model_view,
            near_far_kind: [near, far, kind as f32, state.clip_plane_count as f32],
            clip_model_view: state.model_view,
            clip_planes: state.clip_planes,
            raster_viewport: if canonical_coverage { [0.0, 0.0, map_size as f32, map_size as f32] } else { [0.0; 4] },
            alpha_test: [state.alpha_test_function as f32, state.alpha_test_reference, 0.0, 0.0],
        };
        let buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Coin shadow draw uniform"),
            contents: bytemuck::bytes_of(&uniforms),
            usage: wgpu::BufferUsages::UNIFORM,
        });
        let binding = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("Coin shadow draw binding"),
            layout: &binding_layout,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                resource: buffer.as_entire_binding(),
            }],
        });
        bindings.push((buffer, binding));
    }
    {
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("Coin spot shadow moments"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: &view,
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
        pass.set_vertex_buffer(0, vertex_buffer.slice(..));
        pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
        for (slot, draw) in casters.iter().enumerate() {
            let [x, y, width, height] = draw.viewport;
            if width == 0 || height == 0 { continue; }
            pass.set_scissor_rect(x as u32, y as u32, width as u32, height as u32);
            pass.set_bind_group(0, &bindings[slot].1, &[]);
            pass.draw_indexed(
                draw.first_index..draw.first_index + draw.index_count,
                0,
                0..1,
            );
        }
    }
    Ok(ShadowMap {
        view,
        moments,
        size: map_size,
        _depth: depth,
    })
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/shadow_tests.rs"]
mod tests;

#[cfg(test)]
mod coverage_layout_tests {
    #[test]
    fn shadow_coverage_uniform_matches_wgsl_layout() {
        use naga::valid::{Capabilities, ValidationFlags, Validator};
        let source = include_str!("../../shaders/coin_shadow.wgsl");
        let module = naga::front::wgsl::parse_str(source).expect("shadow coverage WGSL");
        Validator::new(ValidationFlags::all(), Capabilities::all()).validate(&module).expect("shadow coverage validation");
        let ty = module.types.iter().find(|(_, ty)| ty.name.as_deref() == Some("ShadowUniforms")).unwrap().1;
        if let naga::TypeInner::Struct { ref members, span } = ty.inner {
            assert_eq!(span as usize, std::mem::size_of::<super::ShadowUniforms>());
            // Seven vec4-aligned fields: three matrices, near/far/kind,
            // eight clip planes, viewport, and C07 alpha-test state. The
            // final vec4 extended the previous 352-byte layout to 368.
            assert_eq!(span, 368);
            assert_eq!(span % 16, 0);
            let offsets: Vec<u32> = members.iter().map(|member| member.offset).collect();
            assert_eq!(offsets, [0, 64, 128, 144, 208, 336, 352]);
            assert!(offsets.iter().all(|offset| offset % 16 == 0));
        } else { panic!("shadow uniforms must be a struct"); }
    }
}
