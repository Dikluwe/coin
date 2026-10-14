//! Preserve Depth32Float readback and snapshots without native depth copies.
//! A non-filterable float binding may read a depth view (WebGPU binding rules).
//! Unlike texture_depth_2d, it also supports textureLoad in Naga's GLSL backend.
use wgpu::*;

const SHADER: &str = r#"
@group(0) @binding(0) var depth: texture_2d<f32>;
@vertex fn vs_main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
    let x = f32((index << 1u) & 2u); let y = f32(index & 2u);
    return vec4<f32>(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
}
@fragment fn fs_main(@builtin(position) pixel: vec4<f32>) -> @location(0) f32 {
    return textureLoad(depth, vec2<i32>(pixel.xy), 0).r;
}
@fragment fn fs_depth(@builtin(position) pixel: vec4<f32>) -> @builtin(frag_depth) f32 {
    return textureLoad(depth, vec2<i32>(pixel.xy), 0).r;
}
"#;

pub(super) struct DepthTransfer {
    layout: BindGroupLayout,
    pipeline: RenderPipeline,
}

impl DepthTransfer {
    pub fn new(device: &Device, depth_output: bool) -> Self {
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
                    format: TextureFormat::R32Float, blend: None, write_mask: ColorWrites::ALL })] } }),
            primitive: PrimitiveState::default(),
            depth_stencil: depth_output.then_some(DepthStencilState {
                format: TextureFormat::Depth32Float, depth_write_enabled: true,
                depth_compare: CompareFunction::Always, stencil: Default::default(), bias: Default::default(),
            }),
            multisample: Default::default(), multiview: None, cache: None,
        });
        Self { layout, pipeline }
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
            format: TextureFormat::R32Float,
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
                ops: Operations { load: LoadOp::Clear(Color::BLACK), store: StoreOp::Store },
            })],
            depth_stencil_attachment: None, timestamp_writes: None, occlusion_query_set: None,
        });
        pass.set_pipeline(&self.pipeline);
        pass.set_bind_group(0, &binding, &[]);
        pass.draw(0..3, 0..1);
        drop(pass);
        output
    }
}

#[cfg(test)]
#[path = "../../../../../testsuite/coinwgpu/rust/unit/depth_transfer_tests.rs"]
mod tests;
