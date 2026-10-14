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
}

pub(crate) struct ShadowMap {
    pub view: wgpu::TextureView,
    _moments: wgpu::Texture,
    _depth: wgpu::Texture,
}

// The Core/FFI payload already owns the Coin decisions. This function only
// allocates attachments and records indexed triangle draws in their source order.
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
) -> Result<ShadowMap, String> {
    if !map_size.is_power_of_two()
        || map_size > 2048
        || !near.is_finite()
        || !far.is_finite()
        || near <= 0.0
        || far <= near
        || kind > 1
    {
        return Err("Invalid wgpu shadow-map dimensions or depth range".into());
    }
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
        _moments: moments,
        _depth: depth,
    })
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/shadow_tests.rs"]
mod tests;
