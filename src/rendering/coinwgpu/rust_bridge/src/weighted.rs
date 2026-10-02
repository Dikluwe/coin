//! Weighted accumulation is an explicit extension. Coin scheduling stays in Core.
use wgpu::*;

pub(super) const FRAGMENT: &str = r#"
struct WeightedOutput {
    @location(0) accumulation: vec4<f32>,
    @location(1) revealage: f32,
    @builtin(frag_depth) depth: f32,
};
@fragment fn fs_weighted(input: VertexOutput) -> WeightedOutput {
    let color = fragment_color(input);
    let alpha = clamp(color.a, 0.0, 1.0);
    if (alpha <= 0.0) { discard; }
    let depth = clamp(mix(u.clip_meta.z, u.clip_meta.w, input.clip_position.z) + u.clip_meta.y, 0.0, 1.0);
    let weight = clamp(alpha * 8.0 + 0.01, 0.01, 8.0) *
                 clamp(pow(1.0 - depth, 3.0) * 16.0 + 0.1, 0.1, 16.0);
    var output: WeightedOutput;
    output.accumulation = vec4<f32>(color.rgb * alpha, alpha) * weight;
    output.revealage = alpha;
    output.depth = depth;
    return output;
}
"#;

const COMPOSITE: &str = r#"
@group(0) @binding(0) var accumulation: texture_2d<f32>;
@group(0) @binding(1) var revealage: texture_2d<f32>;
@vertex fn vs_main(@builtin(vertex_index) index:u32)->@builtin(position) vec4<f32> {
    let x=f32((index<<1u)&2u);let y=f32(index&2u);
    return vec4<f32>(x*2.0-1.0,y*2.0-1.0,0.0,1.0);
}
@fragment fn fs_main(@builtin(position) pixel:vec4<f32>)->@location(0) vec4<f32> {
    let xy=vec2<i32>(pixel.xy);
    let sum=textureLoad(accumulation,xy,0);
    let reveal=clamp(textureLoad(revealage,xy,0).r,0.0,1.0);
    return vec4<f32>(sum.rgb / max(sum.a,0.00001),1.0-reveal);
}
"#;

pub(super) struct Weighted {
    pub accumulation: TextureView,
    pub revealage: TextureView,
    binding: BindGroup,
    pipeline: RenderPipeline,
}

impl Weighted {
    pub fn new(device: &Device, width: u32, height: u32, format: TextureFormat) -> Self {
        let texture = |format, label| {
            device
                .create_texture(&TextureDescriptor {
                    label: Some(label),
                    size: Extent3d {
                        width,
                        height,
                        depth_or_array_layers: 1,
                    },
                    mip_level_count: 1,
                    sample_count: 1,
                    dimension: TextureDimension::D2,
                    format,
                    usage: TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING,
                    view_formats: &[],
                })
                .create_view(&TextureViewDescriptor::default())
        };
        let accumulation = texture(TextureFormat::Rgba16Float, "Coin weighted accumulation");
        let revealage = texture(TextureFormat::R16Float, "Coin weighted revealage");
        let entries: Vec<_> = (0..2)
            .map(|binding| BindGroupLayoutEntry {
                binding,
                visibility: ShaderStages::FRAGMENT,
                ty: BindingType::Texture {
                    sample_type: TextureSampleType::Float { filterable: false },
                    view_dimension: TextureViewDimension::D2,
                    multisampled: false,
                },
                count: None,
            })
            .collect();
        let layout = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("Coin weighted composite bindings"),
            entries: &entries,
        });
        let binding = device.create_bind_group(&BindGroupDescriptor {
            label: Some("Coin weighted composite"),
            layout: &layout,
            entries: &[
                BindGroupEntry {
                    binding: 0,
                    resource: BindingResource::TextureView(&accumulation),
                },
                BindGroupEntry {
                    binding: 1,
                    resource: BindingResource::TextureView(&revealage),
                },
            ],
        });
        let pipeline_layout = device.create_pipeline_layout(&PipelineLayoutDescriptor {
            label: Some("Coin weighted composite layout"),
            bind_group_layouts: &[&layout],
            push_constant_ranges: &[],
        });
        let shader = device.create_shader_module(ShaderModuleDescriptor {
            label: Some("Coin weighted composite shader"),
            source: ShaderSource::Wgsl(COMPOSITE.into()),
        });
        let pipeline = device.create_render_pipeline(&RenderPipelineDescriptor {
            label: Some("Coin weighted composite pipeline"),
            layout: Some(&pipeline_layout),
            vertex: VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                buffers: &[],
                compilation_options: PipelineCompilationOptions::default(),
            },
            fragment: Some(FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                targets: &[Some(ColorTargetState {
                    format,
                    blend: Some(BlendState::ALPHA_BLENDING),
                    write_mask: ColorWrites::ALL,
                })],
                compilation_options: PipelineCompilationOptions::default(),
            }),
            primitive: PrimitiveState::default(),
            depth_stencil: None,
            multisample: MultisampleState::default(),
            multiview: None,
            cache: None,
        });
        Self {
            accumulation,
            revealage,
            binding,
            pipeline,
        }
    }
    pub fn composite(
        &self,
        encoder: &mut CommandEncoder,
        color: &TextureView,
        timestamp: Option<&QuerySet>,
    ) {
        let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
            label: Some("Coin weighted resolve"),
            color_attachments: &[Some(RenderPassColorAttachment {
                view: color,
                resolve_target: None,
                ops: Operations {
                    load: LoadOp::Load,
                    store: StoreOp::Store,
                },
            })],
            depth_stencil_attachment: None,
            timestamp_writes: timestamp.map(|query_set| RenderPassTimestampWrites {
                query_set,
                beginning_of_pass_write_index: None,
                end_of_pass_write_index: Some(1),
            }),
            occlusion_query_set: None,
        });
        pass.set_pipeline(&self.pipeline);
        pass.set_bind_group(0, &self.binding, &[]);
        pass.draw(0..3, 0..1);
    }
}

pub(super) fn device_supported(device: &Device) -> bool {
    let required = TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING;
    device.limits().max_color_attachments >= 2
        && [TextureFormat::Rgba16Float, TextureFormat::R16Float]
            .iter()
            .all(|format| {
                let features = format.guaranteed_format_features(device.features());
                features.allowed_usages.contains(required)
                    && features
                        .flags
                        .contains(TextureFormatFeatureFlags::BLENDABLE)
            })
}

#[cfg(test)]
mod tests {
    #[test]
    fn shaders_validate() {
        for source in [
            super::COMPOSITE.to_owned(),
            format!(
                "{}{}",
                include_str!("../../shaders/coin_standard.wgsl"),
                super::FRAGMENT
            ),
        ] {
            let module = naga::front::wgsl::parse_str(&source).unwrap();
            naga::valid::Validator::new(
                naga::valid::ValidationFlags::all(),
                naga::valid::Capabilities::all(),
            )
            .validate(&module)
            .unwrap();
        }
    }
}
