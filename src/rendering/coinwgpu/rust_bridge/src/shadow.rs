use crate::{CoinWgpuShadowDraw, CoinWgpuVertex};
use bytemuck::{Pod, Zeroable};
use wgpu::util::DeviceExt;

#[repr(C)]
#[derive(Copy, Clone, Pod, Zeroable)]
struct ShadowUniforms {
    model_view_projection: [f32; 16],
    model_view: [f32; 16],
    near_far_kind: [f32; 4],
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
    map_size: u32,
    near: f32,
    far: f32,
    kind: u32,
) -> Result<ShadowMap, String> {
    if casters.is_empty()
        || !map_size.is_power_of_two()
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
        let uniforms = ShadowUniforms {
            model_view_projection: draw.model_view_projection,
            model_view: draw.model_view,
            near_far_kind: [near, far, kind as f32, 0.0],
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
mod tests {
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
        let map = encode_moments(
            &device,
            &mut encoder,
            &vertices,
            &[0, 1, 2, 3, 4, 5],
            &[caster],
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
}
