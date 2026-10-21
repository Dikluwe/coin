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
    snapshot: Option<(BindGroupLayout, RenderPipeline)>,
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

const SNAPSHOT: &str = r#"
@group(0) @binding(0) var source_depth: SOURCE_TYPE;
@vertex fn vs_main(@builtin(vertex_index) index:u32)->@builtin(position) vec4<f32> {
    let x=f32((index<<1u)&2u);let y=f32(index&2u);
    return vec4<f32>(x*2.0-1.0,y*2.0-1.0,0.0,1.0);
}
@fragment fn fs_main(@builtin(position) pixel:vec4<f32>,@builtin(sample_index) sample_index:u32)->@builtin(frag_depth) f32 {
    let value=textureLoad(source_depth,vec2<i32>(pixel.xy),i32(sample_index));return SOURCE_LOAD;
}
"#;

pub(super) fn adapter_supported(adapter: &Adapter, device: &Device, samples: u32) -> bool {
    if !matches!(samples, 1 | 2 | 4) || !device_supported(device) { return false; }
    if samples == 1 { return true; }
    if !adapter.get_downlevel_capabilities().flags.contains(wgpu::DownlevelFlags::MULTISAMPLED_SHADING) { return false; }
    if samples == 2 && !device.features().contains(Features::TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES) { return false; }
    let count = if samples == 2 { TextureFormatFeatureFlags::MULTISAMPLE_X2 } else { TextureFormatFeatureFlags::MULTISAMPLE_X4 };
    [TextureFormat::Rgba16Float, TextureFormat::R8Unorm, TextureFormat::Depth32Float].into_iter().all(|format| {
        let caps = adapter.get_texture_format_features(format);
        caps.allowed_usages.contains(TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING) && caps.flags.contains(count)
    })
}

impl Peeling {
    pub fn new(
        device: &Device,
        width: u32,
        height: u32,
        format: TextureFormat,
        layers: usize,
        gl_depth_loads: bool,
    ) -> Self {
        Self::new_samples(device, width, height, format, layers, gl_depth_loads, 1)
    }

    pub fn new_samples(device: &Device, width: u32, height: u32, format: TextureFormat,
                       layers: usize, gl_depth_loads: bool, samples: u32) -> Self {
        let texture = |format, usage, label| {
            device.create_texture(&TextureDescriptor {
                label: Some(label),
                size: Extent3d {
                    width,
                    height,
                    depth_or_array_layers: 1,
                },
                mip_level_count: 1,
                sample_count: samples,
                dimension: TextureDimension::D2,
                format,
                usage,
                view_formats: &[],
            })
        };
        let opaque = texture(
            TextureFormat::Depth32Float,
            TextureUsages::TEXTURE_BINDING | if gl_depth_loads || samples > 1 {
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
                        multisampled: samples > 1,
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
                        multisampled: samples > 1,
                    },
                    count: None,
                },
                BindGroupLayoutEntry {
                    binding: 2,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Texture {
                        sample_type: TextureSampleType::Float { filterable: false },
                        view_dimension: TextureViewDimension::D2,
                        multisampled: samples > 1,
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
        let composite = if samples > 1 {
            COMPOSITE.replace("texture_2d<f32>", "texture_multisampled_2d<f32>")
                .replace("texture_depth_2d", "texture_depth_multisampled_2d")
                .replace("pixel:vec4<f32>", "pixel:vec4<f32>,@builtin(sample_index) sample_index:u32")
                .replace(",0)", ",i32(sample_index))")
        } else { COMPOSITE.to_owned() };
        let shader = device.create_shader_module(ShaderModuleDescriptor {
            label: Some("Coin peel compositor"),
            source: ShaderSource::Wgsl(super::shader_profile::depth_load_profile(&composite, gl_depth_loads).into()),
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
                multisample: MultisampleState { count: samples, ..Default::default() },
                multiview: None,
                cache: None,
            })
        };
        let snapshot = (samples > 1).then(|| {
            let layout = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
                label: Some("Coin opaque MSAA depth snapshot"),
                entries: &[BindGroupLayoutEntry { binding: 0, visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Texture { sample_type: if gl_depth_loads {
                        TextureSampleType::Float { filterable: false }
                    } else { TextureSampleType::Depth }, view_dimension: TextureViewDimension::D2,
                        multisampled: true }, count: None }],
            });
            let source = SNAPSHOT.replace("SOURCE_TYPE", if gl_depth_loads {
                "texture_multisampled_2d<f32>"
            } else { "texture_depth_multisampled_2d" })
                .replace("SOURCE_LOAD", if gl_depth_loads { "value.r" } else { "value" });
            let shader = device.create_shader_module(ShaderModuleDescriptor {
                label: Some("Coin opaque per-sample depth snapshot"), source: ShaderSource::Wgsl(source.into()),
            });
            let pipeline_layout = device.create_pipeline_layout(&PipelineLayoutDescriptor {
                label: Some("Coin MSAA snapshot layout"), bind_group_layouts: &[&layout], push_constant_ranges: &[],
            });
            let pipeline = device.create_render_pipeline(&RenderPipelineDescriptor {
                label: Some("Coin MSAA snapshot"), layout: Some(&pipeline_layout),
                vertex: VertexState { module: &shader, entry_point: Some("vs_main"), buffers: &[], compilation_options: Default::default() },
                fragment: Some(FragmentState { module: &shader, entry_point: Some("fs_main"), targets: &[], compilation_options: Default::default() }),
                primitive: PrimitiveState::default(), depth_stencil: Some(DepthStencilState {
                    format: TextureFormat::Depth32Float, depth_write_enabled: true, depth_compare: CompareFunction::Always,
                    stencil: Default::default(), bias: Default::default() }),
                multisample: MultisampleState { count: samples, ..Default::default() }, multiview: None, cache: None,
            });
            (layout, pipeline)
        });
        Self {
            opaque,
            opaque_view,
            colors,
            depths,
            masks,
            bindings,
            color_pipeline: pipeline(false),
            depth_pipeline: pipeline(true),
            snapshot,
        }
    }

    pub fn snapshot_depth(&self, device: &Device, encoder: &mut CommandEncoder, source: &TextureView) {
        let (layout, pipeline) = self.snapshot.as_ref().unwrap();
        let binding = device.create_bind_group(&BindGroupDescriptor {
            label: Some("Coin opaque depth source"), layout,
            entries: &[BindGroupEntry { binding: 0, resource: BindingResource::TextureView(source) }],
        });
        let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
            label: Some("Coin opaque depth per-sample snapshot"), color_attachments: &[],
            depth_stencil_attachment: Some(RenderPassDepthStencilAttachment { view: &self.opaque_view,
                depth_ops: Some(Operations { load: LoadOp::Clear(1.0), store: StoreOp::Store }), stencil_ops: None }),
            timestamp_writes: None, occlusion_query_set: None,
        });
        pass.set_pipeline(pipeline); pass.set_bind_group(0, &binding, &[]); pass.draw(0..3, 0..1);
    }

    pub fn composite(
        &self,
        encoder: &mut CommandEncoder,
        color: &TextureView,
        depth: &TextureView,
        timestamp: Option<&QuerySet>,
    ) {
        self.composite_samples(encoder, color, depth, None, timestamp);
    }

    pub fn composite_samples(&self, encoder: &mut CommandEncoder, color: &TextureView,
        depth: &TextureView, resolve: Option<&TextureView>, timestamp: Option<&QuerySet>) {
        let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
            label: Some("Coin bounded source-over composite"),
            color_attachments: &[Some(RenderPassColorAttachment {
                view: color,
                resolve_target: resolve,
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
    validate_request_samples(layers, reserved, budget, width, height, enabled, 1)
}

pub(super) fn validate_request_samples(layers: u32, reserved: u32, budget: u64,
    width: u32, height: u32, enabled: bool, samples: u32) -> Result<(), (super::CoinWgpuStatus, String)> {
    use super::CoinWgpuStatus;
    if !matches!(samples, 1 | 2 | 4) {
        return Err((CoinWgpuStatus::Unsupported, "Peeling requires exact 1/2/4 samples".into()));
    }
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
        .and_then(|pixels| pixels.checked_mul(u64::from(layers) * 16 + 8)).and_then(|bytes| bytes.checked_mul(u64::from(samples)))
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
#[path = "../../../../../testsuite/coinwgpu/rust/unit/peeling_tests.rs"]
mod tests;
