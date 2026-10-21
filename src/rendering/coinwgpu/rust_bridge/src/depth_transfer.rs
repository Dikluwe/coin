//! Preserve Depth32Float readback and snapshots without native depth copies.
//! A non-filterable float binding may read a depth view (WebGPU binding rules).
//! Unlike texture_depth_2d, it also supports textureLoad in Naga's GLSL backend.
//! RGBA8 output carries the original IEEE-754 bytes; R32Float is not a color
//! attachment on every adapter. Staging rows are decoded as little-endian f32.
use wgpu::*;

const CLEAR_DEPTH_BYTES: Color = Color { r: 0.0, g: 0.0, b: 128.0 / 255.0, a: 63.0 / 255.0 };

const SHADER: &str = r#"
@group(0) @binding(0) var depth: texture_2d<f32>;
@vertex fn vs_main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
    let x = f32((index << 1u) & 2u); let y = f32(index & 2u);
    return vec4<f32>(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
}
fn pack_depth(value: f32) -> vec4<f32> {
    let bits = bitcast<u32>(value);
    return vec4<f32>(f32(bits & 255u), f32((bits >> 8u) & 255u),
        f32((bits >> 16u) & 255u), f32((bits >> 24u) & 255u)) / 255.0;
}
@fragment fn fs_main(@builtin(position) pixel: vec4<f32>) -> @location(0) vec4<f32> {
    return pack_depth(textureLoad(depth, vec2<i32>(pixel.xy), 0).r);
}
@fragment fn fs_depth(@builtin(position) pixel: vec4<f32>) -> @builtin(frag_depth) f32 {
    return textureLoad(depth, vec2<i32>(pixel.xy), 0).r;
}
"#;

// A depth-typed load is required on every backend: treating Depth32Float as
// a sampled color texture returns zero on Mesa/OpenGL. The local Naga GLSL
// patch lowers multisampled depth loads to texelFetch(sampler2DMS, ...).x.
const MSAA_DEPTH_SHADER: &str = r#"
@group(0) @binding(0) var depth: texture_depth_multisampled_2d;
@group(0) @binding(1) var<uniform> sample_count: vec4<u32>;
@vertex fn vs_main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
    let x = f32((index << 1u) & 2u); let y = f32(index & 2u);
    return vec4<f32>(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
}
fn pack_depth(value: f32) -> vec4<f32> {
    let bits = bitcast<u32>(value);
    return vec4<f32>(f32(bits & 255u), f32((bits >> 8u) & 255u),
        f32((bits >> 16u) & 255u), f32((bits >> 24u) & 255u)) / 255.0;
}
@fragment fn fs_main(@builtin(position) pixel: vec4<f32>) -> @location(0) vec4<f32> {
    var nearest = 1.0;
    for (var sample = 0; sample < i32(sample_count.x); sample = sample + 1) {
        nearest = min(nearest, textureLoad(depth, vec2<i32>(pixel.xy), sample));
    }
    return pack_depth(nearest);
}
"#;

pub(super) struct DepthTransfer {
    layout: BindGroupLayout,
    pipeline: RenderPipeline,
    msaa_layout: BindGroupLayout,
    msaa_pipeline: RenderPipeline,
}

impl DepthTransfer {
    pub fn new(device: &Device, depth_output: bool) -> Self {
        Self::new_with_backend(device, depth_output, false)
    }

    pub fn new_gl(device: &Device, depth_output: bool) -> Self {
        Self::new_with_backend(device, depth_output, true)
    }

    fn new_with_backend(device: &Device, depth_output: bool, _gl_backend: bool) -> Self {
        let layout = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("Coin depth readback"),
            entries: &[BindGroupLayoutEntry {
                binding: 0, visibility: ShaderStages::FRAGMENT,
                ty: BindingType::Texture {
                    sample_type: TextureSampleType::Float { filterable: false },
                    view_dimension: TextureViewDimension::D2, multisampled: false,
                }, count: None,
            }],
        });
        let pipeline_layout = device.create_pipeline_layout(&PipelineLayoutDescriptor {
            label: Some("Coin depth readback"), bind_group_layouts: &[&layout],
            push_constant_ranges: &[],
        });
        let shader = device.create_shader_module(ShaderModuleDescriptor {
            label: Some("Coin exact depth readback"), source: ShaderSource::Wgsl(SHADER.into()),
        });
        let pipeline = device.create_render_pipeline(&RenderPipelineDescriptor {
            label: Some("Coin exact depth readback"), layout: Some(&pipeline_layout),
            vertex: VertexState { module: &shader, entry_point: Some("vs_main"), buffers: &[],
                compilation_options: Default::default() },
            fragment: Some(FragmentState { module: &shader,
                entry_point: Some(if depth_output { "fs_depth" } else { "fs_main" }),
                compilation_options: Default::default(),
                targets: if depth_output { &[] } else { &[Some(ColorTargetState {
                    format: TextureFormat::Rgba8Unorm, blend: None, write_mask: ColorWrites::ALL })] } }),
            primitive: PrimitiveState::default(),
            depth_stencil: depth_output.then_some(DepthStencilState {
                format: TextureFormat::Depth32Float, depth_write_enabled: true,
                depth_compare: CompareFunction::Always, stencil: Default::default(), bias: Default::default(),
            }),
            multisample: Default::default(), multiview: None, cache: None,
        });
        let msaa_layout = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("Coin multisample depth resolve"),
            entries: &[BindGroupLayoutEntry {
                binding: 0, visibility: ShaderStages::FRAGMENT,
                ty: BindingType::Texture {
                    sample_type: TextureSampleType::Depth,
                    view_dimension: TextureViewDimension::D2, multisampled: true,
                }, count: None,
            }, BindGroupLayoutEntry {
                binding: 1, visibility: ShaderStages::FRAGMENT,
                ty: BindingType::Buffer { ty: BufferBindingType::Uniform,
                    has_dynamic_offset: false, min_binding_size: None }, count: None,
            }],
        });
        let msaa_pipeline_layout = device.create_pipeline_layout(&PipelineLayoutDescriptor {
            label: Some("Coin multisample depth resolve"), bind_group_layouts: &[&msaa_layout],
            push_constant_ranges: &[],
        });
        let msaa_shader = device.create_shader_module(ShaderModuleDescriptor {
            label: Some("Coin multisample depth resolve"),
            source: ShaderSource::Wgsl(MSAA_DEPTH_SHADER.into()),
        });
        let msaa_pipeline = device.create_render_pipeline(&RenderPipelineDescriptor {
            label: Some("Coin multisample depth resolve"), layout: Some(&msaa_pipeline_layout),
            vertex: VertexState { module: &msaa_shader, entry_point: Some("vs_main"), buffers: &[],
                compilation_options: Default::default() },
            fragment: Some(FragmentState { module: &msaa_shader, entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(ColorTargetState { format: TextureFormat::Rgba8Unorm,
                    blend: None, write_mask: ColorWrites::ALL })] }),
            primitive: PrimitiveState::default(), depth_stencil: None,
            multisample: Default::default(), multiview: None, cache: None,
        });
        Self { layout, pipeline, msaa_layout, msaa_pipeline }
    }

    pub fn copy_depth(&self, device: &Device, encoder: &mut CommandEncoder,
                      source: &TextureView, destination: &TextureView) {
        let binding = device.create_bind_group(&BindGroupDescriptor {
            label: Some("Coin depth snapshot"), layout: &self.layout,
            entries: &[BindGroupEntry { binding: 0, resource: BindingResource::TextureView(source) }],
        });
        let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
            label: Some("Coin exact depth snapshot"), color_attachments: &[],
            depth_stencil_attachment: Some(RenderPassDepthStencilAttachment {
                view: destination, depth_ops: Some(Operations { load: LoadOp::Clear(1.0), store: StoreOp::Store }),
                stencil_ops: None,
            }), timestamp_writes: None, occlusion_query_set: None,
        });
        pass.set_pipeline(&self.pipeline); pass.set_bind_group(0, &binding, &[]);
        pass.draw(0..3, 0..1);
    }

    pub fn encode(&self, device: &Device, encoder: &mut CommandEncoder,
                  depth: &TextureView, width: u32, height: u32) -> Texture {
        let output = device.create_texture(&TextureDescriptor {
            label: Some("Coin transferable float depth"),
            size: Extent3d { width, height, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: TextureDimension::D2,
            format: TextureFormat::Rgba8Unorm,
            usage: TextureUsages::RENDER_ATTACHMENT | TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let view = output.create_view(&TextureViewDescriptor::default());
        let binding = device.create_bind_group(&BindGroupDescriptor {
            label: Some("Coin depth readback"), layout: &self.layout,
            entries: &[BindGroupEntry { binding: 0, resource: BindingResource::TextureView(depth) }],
        });
        let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
            label: Some("Coin exact depth readback"),
            color_attachments: &[Some(RenderPassColorAttachment {
                view: &view, resolve_target: None,
                ops: Operations { load: LoadOp::Clear(CLEAR_DEPTH_BYTES), store: StoreOp::Store },
            })],
            depth_stencil_attachment: None, timestamp_writes: None, occlusion_query_set: None,
        });
        pass.set_pipeline(&self.pipeline);
        pass.set_bind_group(0, &binding, &[]);
        pass.draw(0..3, 0..1);
        drop(pass);
        output
    }

    pub fn encode_msaa(&self, device: &Device, encoder: &mut CommandEncoder,
                       depth: &TextureView, width: u32, height: u32,
                       samples: u32) -> Texture {
        let output = device.create_texture(&TextureDescriptor {
            label: Some("Coin resolved multisample depth"),
            size: Extent3d { width, height, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: TextureDimension::D2,
            format: TextureFormat::Rgba8Unorm,
            usage: TextureUsages::RENDER_ATTACHMENT | TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let view = output.create_view(&TextureViewDescriptor::default());
        let sample_buffer = device.create_buffer(&BufferDescriptor {
            label: Some("Coin multisample depth sample count"), size: 16,
            usage: BufferUsages::UNIFORM, mapped_at_creation: true,
        });
        sample_buffer.slice(..).get_mapped_range_mut()[..4].copy_from_slice(&samples.to_ne_bytes());
        sample_buffer.unmap();
        let binding = device.create_bind_group(&BindGroupDescriptor {
            label: Some("Coin multisample depth resolve"), layout: &self.msaa_layout,
            entries: &[BindGroupEntry { binding: 0, resource: BindingResource::TextureView(depth) },
                BindGroupEntry { binding: 1, resource: sample_buffer.as_entire_binding() }],
        });
        let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
            label: Some("Coin multisample depth resolve"),
            color_attachments: &[Some(RenderPassColorAttachment {
                view: &view, resolve_target: None,
                ops: Operations { load: LoadOp::Clear(CLEAR_DEPTH_BYTES), store: StoreOp::Store },
            })],
            depth_stencil_attachment: None, timestamp_writes: None, occlusion_query_set: None,
        });
        pass.set_pipeline(&self.msaa_pipeline);
        pass.set_bind_group(0, &binding, &[]);
        pass.draw(0..3, 0..1);
        drop(pass);
        output
    }
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/depth_transfer_tests.rs"]
mod tests;
