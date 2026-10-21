//! Present already-resolved linear RGBA8 texels; scene MSAA never uses the WSI format.
pub(super) struct Presentation {
    pub texture: wgpu::Texture,
    pub view: wgpu::TextureView,
    pub format: wgpu::TextureFormat,
    pipeline: wgpu::RenderPipeline,
    bindings: wgpu::BindGroup,
}

pub(super) fn linear_format(format: wgpu::TextureFormat) -> Option<wgpu::TextureFormat> {
    match format {
        wgpu::TextureFormat::Rgba8Unorm | wgpu::TextureFormat::Rgba8UnormSrgb =>
            Some(wgpu::TextureFormat::Rgba8Unorm),
        wgpu::TextureFormat::Bgra8Unorm | wgpu::TextureFormat::Bgra8UnormSrgb =>
            Some(wgpu::TextureFormat::Bgra8Unorm),
        _ => None,
    }
}

impl Presentation {
    pub fn new(device: &wgpu::Device, width: u32, height: u32,
               format: wgpu::TextureFormat) -> Self {
        Self::with_source_format(device, width, height, format, wgpu::TextureFormat::Rgba8Unorm)
    }

    // Match the swapchain's scene quantization for a private single-sample capture.
    pub fn with_source_format(device: &wgpu::Device, width: u32, height: u32,
                              format: wgpu::TextureFormat, source_format: wgpu::TextureFormat) -> Self {
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Coin window resolved RGBA8"),
            size: wgpu::Extent3d {width, height, depth_or_array_layers: 1},
            mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
            format: source_format,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let view = texture.create_view(&Default::default());
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Coin window exact texel presentation"),
            source: wgpu::ShaderSource::Wgsl(r#"
@group(0) @binding(0) var resolved_color: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
    var p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0),
        vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    return vec4<f32>(p[i], 0.0, 1.0);
}
@fragment fn fs(@builtin(position) p: vec4<f32>) -> @location(0) vec4<f32> {
    return textureLoad(resolved_color, vec2<i32>(p.xy), 0);
}
"#.into()),
        });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Coin window resolved texel copy"), layout: None,
            vertex: wgpu::VertexState {module: &shader, entry_point: Some("vs"),
                buffers: &[], compilation_options: Default::default()},
            fragment: Some(wgpu::FragmentState {module: &shader, entry_point: Some("fs"),
                targets: &[Some(wgpu::ColorTargetState {format, blend: None,
                    write_mask: wgpu::ColorWrites::ALL})], compilation_options: Default::default()}),
            primitive: Default::default(), depth_stencil: None,
            multisample: Default::default(), multiview: None, cache: None,
        });
        let bindings = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("Coin window resolved texels"), layout: &pipeline.get_bind_group_layout(0),
            entries: &[wgpu::BindGroupEntry {binding: 0,
                resource: wgpu::BindingResource::TextureView(&view)}],
        });
        Self {texture, view, format, pipeline, bindings}
    }

    pub fn encode(&self, device: &wgpu::Device, target: &wgpu::TextureView) -> wgpu::CommandBuffer {
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("Coin window resolved presentation"),
        });
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Coin window resolved presentation"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: target, resolve_target: None,
                    ops: wgpu::Operations {load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),
                        store: wgpu::StoreOp::Store},
                })],
                depth_stencil_attachment: None, timestamp_writes: None, occlusion_query_set: None,
            });
            pass.set_pipeline(&self.pipeline);
            pass.set_bind_group(0, &self.bindings, &[]);
            pass.draw(0..3, 0..1);
        }
        encoder.finish()
    }
}
