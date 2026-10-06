//! Concrete bounded depth peeling. Coin decisions arrive in the draw plan.
use wgpu::*;

pub(super) struct Peeling {
    pub opaque: Texture,
    pub opaque_view: TextureView,
    pub colors: Vec<TextureView>,
    pub depths: Vec<TextureView>,
    pub masks: Vec<TextureView>,
    bindings: Vec<BindGroup>,
    color_pipeline: RenderPipeline,
    depth_pipeline: RenderPipeline,
}

const COMPOSITE: &str = r#"
@group(0) @binding(0) var layer_color: texture_2d<f32>;
@group(0) @binding(1) var layer_depth: texture_depth_2d;
@group(0) @binding(2) var layer_mask: texture_2d<f32>;
@vertex fn vs_main(@builtin(vertex_index) index:u32)->@builtin(position) vec4<f32> {
    let x=f32((index<<1u)&2u);let y=f32(index&2u);
    return vec4<f32>(x*2.0-1.0,y*2.0-1.0,0.0,1.0);
}
@fragment fn fs_color(@builtin(position) pixel:vec4<f32>)->@location(0) vec4<f32> {
    return textureLoad(layer_color,vec2<i32>(pixel.xy),0);
}
@fragment fn fs_depth(@builtin(position) pixel:vec4<f32>)->@builtin(frag_depth) f32 {
    let xy=vec2<i32>(pixel.xy);
    if(textureLoad(layer_mask,xy,0).r<0.5){discard;}
    return textureLoad(layer_depth,xy,0);
}
"#;

impl Peeling {
    pub fn new(
        device: &Device,
        width: u32,
        height: u32,
        format: TextureFormat,
        layers: usize,
        gl_depth_loads: bool,
    ) -> Self {
        let texture = |format, usage, label| {
            device.create_texture(&TextureDescriptor {
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
                usage,
                view_formats: &[],
            })
        };
        let opaque = texture(
            TextureFormat::Depth32Float,
            TextureUsages::TEXTURE_BINDING | if gl_depth_loads {
                TextureUsages::RENDER_ATTACHMENT
            } else { TextureUsages::COPY_DST },
            "Coin opaque depth snapshot",
        );
        let opaque_view = opaque.create_view(&TextureViewDescriptor::default());
        let mut colors = Vec::new();
        let mut depths = Vec::new();
        let mut masks = Vec::new();
        for _ in 0..layers {
            colors.push(
                texture(
                    TextureFormat::Rgba16Float,
                    TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING,
                    "Coin peeled color",
                )
                .create_view(&TextureViewDescriptor::default()),
            );
            depths.push(
                texture(
                    TextureFormat::Depth32Float,
                    TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING,
                    "Coin peeled depth",
                )
                .create_view(&TextureViewDescriptor::default()),
            );
            masks.push(
                texture(
                    TextureFormat::R8Unorm,
                    TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING,
                    "Coin peeled depth-write mask",
                )
                .create_view(&TextureViewDescriptor::default()),
            );
        }
        let layout = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("Coin peel compositor"),
            entries: &[
                BindGroupLayoutEntry {
                    binding: 0,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Texture {
                        sample_type: TextureSampleType::Float { filterable: false },
                        view_dimension: TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                BindGroupLayoutEntry {
                    binding: 1,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Texture {
                        sample_type: if gl_depth_loads { TextureSampleType::Float { filterable: false } }
                            else { TextureSampleType::Depth },
                        view_dimension: TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                BindGroupLayoutEntry {
                    binding: 2,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Texture {
                        sample_type: TextureSampleType::Float { filterable: false },
                        view_dimension: TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
            ],
        });
        let bindings = (0..layers)
            .map(|i| {
                device.create_bind_group(&BindGroupDescriptor {
                    label: Some("Coin peel compositor layer"),
                    layout: &layout,
                    entries: &[
                        BindGroupEntry {
                            binding: 0,
                            resource: BindingResource::TextureView(&colors[i]),
                        },
                        BindGroupEntry {
                            binding: 1,
                            resource: BindingResource::TextureView(&depths[i]),
                        },
                        BindGroupEntry {
                            binding: 2,
                            resource: BindingResource::TextureView(&masks[i]),
                        },
                    ],
                })
            })
            .collect();
        let shader = device.create_shader_module(ShaderModuleDescriptor {
            label: Some("Coin peel compositor"),
            source: ShaderSource::Wgsl(super::shader_profile::depth_load_profile(COMPOSITE, gl_depth_loads).into()),
        });
        let pipeline_layout = device.create_pipeline_layout(&PipelineLayoutDescriptor {
            label: Some("Coin peel compositor"),
            bind_group_layouts: &[&layout],
            push_constant_ranges: &[],
        });
        let pipeline = |depth: bool| {
            device.create_render_pipeline(&RenderPipelineDescriptor {
                label: Some("Coin peel composite pipeline"),
                layout: Some(&pipeline_layout),
                vertex: VertexState {
                    module: &shader,
                    entry_point: Some("vs_main"),
                    buffers: &[],
                    compilation_options: Default::default(),
                },
                fragment: Some(FragmentState {
                    module: &shader,
                    entry_point: Some(if depth { "fs_depth" } else { "fs_color" }),
                    targets: &[Some(ColorTargetState {
                        format,
                        blend: if depth {
                            None
                        } else {
                            Some(BlendState {
                                color: BlendComponent {
                                    src_factor: BlendFactor::SrcAlpha,
                                    dst_factor: BlendFactor::OneMinusSrcAlpha,
                                    operation: BlendOperation::Add,
                                },
                                alpha: BlendComponent {
                                    src_factor: BlendFactor::One,
                                    dst_factor: BlendFactor::OneMinusSrcAlpha,
                                    operation: BlendOperation::Add,
                                },
                            })
                        },
                        write_mask: if depth {
                            ColorWrites::empty()
                        } else {
                            ColorWrites::ALL
                        },
                    })],
                    compilation_options: Default::default(),
                }),
                primitive: PrimitiveState::default(),
                depth_stencil: Some(DepthStencilState {
                    format: TextureFormat::Depth32Float,
                    depth_write_enabled: depth,
                    depth_compare: CompareFunction::Always,
                    stencil: Default::default(),
                    bias: Default::default(),
                }),
                multisample: Default::default(),
                multiview: None,
                cache: None,
            })
        };
        Self {
            opaque,
            opaque_view,
            colors,
            depths,
            masks,
            bindings,
            color_pipeline: pipeline(false),
            depth_pipeline: pipeline(true),
        }
    }

    pub fn composite(
        &self,
        encoder: &mut CommandEncoder,
        color: &TextureView,
        depth: &TextureView,
        timestamp: Option<&QuerySet>,
    ) {
        let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
            label: Some("Coin bounded source-over composite"),
            color_attachments: &[Some(RenderPassColorAttachment {
                view: color,
                resolve_target: None,
                ops: Operations {
                    load: LoadOp::Load,
                    store: StoreOp::Store,
                },
            })],
            depth_stencil_attachment: Some(RenderPassDepthStencilAttachment {
                view: depth,
                depth_ops: Some(Operations {
                    load: LoadOp::Load,
                    store: StoreOp::Store,
                }),
                stencil_ops: None,
            }),
            timestamp_writes: timestamp.map(|query_set| RenderPassTimestampWrites {
                query_set,
                beginning_of_pass_write_index: None,
                end_of_pass_write_index: Some(1),
            }),
            occlusion_query_set: None,
        });
        for i in (0..self.bindings.len()).rev() {
            pass.set_bind_group(0, &self.bindings[i], &[]);
            pass.set_pipeline(&self.color_pipeline);
            pass.draw(0..3, 0..1);
            pass.set_pipeline(&self.depth_pipeline);
            pass.draw(0..3, 0..1);
        }
    }
}

pub(super) fn device_supported(device: &Device) -> bool {
    let features = device.features();
    let uses = TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING;
    let rgba = TextureFormat::Rgba16Float.guaranteed_format_features(features);
    let depth = TextureFormat::Depth32Float.guaranteed_format_features(features);
    let mask = TextureFormat::R8Unorm.guaranteed_format_features(features);
    device.limits().max_color_attachments >= 2
        && rgba.allowed_usages.contains(uses)
        && rgba.flags.contains(TextureFormatFeatureFlags::BLENDABLE)
        && depth
            .allowed_usages
            .contains(uses | TextureUsages::COPY_SRC | TextureUsages::COPY_DST)
        && mask.allowed_usages.contains(uses)
}

// Validate concrete allocation size even for direct private ABI callers.
pub(super) fn validate_request(
    layers: u32,
    reserved: u32,
    budget: u64,
    width: u32,
    height: u32,
    enabled: bool,
) -> Result<(), (super::CoinWgpuStatus, String)> {
    use super::CoinWgpuStatus;
    if reserved != 0 {
        return Err((
            CoinWgpuStatus::InvalidArgument,
            "Reserved transparency bits must be zero".into(),
        ));
    }
    if !enabled {
        return Ok(());
    }
    if !(1..=8).contains(&layers) || budget == 0 {
        return Err((
            CoinWgpuStatus::Unsupported,
            "Peeling requires 1..8 layers and a nonzero buffer budget".into(),
        ));
    }
    let required = u64::from(width)
        .checked_mul(u64::from(height))
        .and_then(|pixels| pixels.checked_mul(u64::from(layers) * 13 + 4))
        .ok_or((
            CoinWgpuStatus::Unsupported,
            "Peeling allocation size overflows".into(),
        ))?;
    if required > budget {
        return Err((
            CoinWgpuStatus::Unsupported,
            "Peeling attachment budget exceeded".into(),
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    #[test]
    fn rejects_unbounded_and_over_budget_transport() {
        use super::validate_request;
        for layers in [0, 9, u32::MAX] {
            assert!(validate_request(layers, 0, u64::MAX, 64, 64, true).is_err());
        }
        assert!(validate_request(8, 0, u64::MAX, u32::MAX, u32::MAX, true).is_err());
        assert!(validate_request(8, 0, 0, 64, 64, true).is_err());
        assert!(validate_request(8, 1, u64::MAX, 64, 64, true).is_err());
        let physical_bytes = 64 * 64 * (8 * 13 + 4);
        assert!(validate_request(8, 0, physical_bytes - 1, 64, 64, true).is_err());
        assert!(validate_request(8, 0, physical_bytes, 64, 64, true).is_ok());
        assert!(validate_request(0, 0, 0, 64, 64, false).is_ok());
    }
    #[test]
    fn compositor_validates() {
        let module = naga::front::wgsl::parse_str(super::COMPOSITE).unwrap();
        naga::valid::Validator::new(
            naga::valid::ValidationFlags::all(),
            naga::valid::Capabilities::all(),
        )
        .validate(&module)
        .unwrap();
        super::super::shader_profile::tests::assert_glsl(
            &super::super::shader_profile::depth_load_profile(super::COMPOSITE, true), "fs_depth");
    }
}
