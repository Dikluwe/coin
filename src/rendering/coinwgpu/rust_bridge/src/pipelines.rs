//! Standard render pipelines and polygon depth bias.

use super::*;

pub(super) fn polygon_depth_bias(st: &CoinWgpuRenderState, topology: u32) -> Result<wgpu::DepthBiasState, String> {
    if !st.polygon_offset_factor.is_finite() || !st.polygon_offset_units.is_finite() || !st.polygon_offset_slope_bias.is_finite()
        || (st.polygon_offset_max_depth_bits != 0 &&
            !(0.0..=1.0).contains(&f32::from_bits(st.polygon_offset_max_depth_bits - 1)))
        || st.polygon_offset_styles & !7 != 0
        || !matches!(st.polygon_offset_primitive_style, 1 | 2 | 4) {
        return Err("Invalid polygon offset".to_string());
    }
    let style = match topology {
        1 => 2, 2 => 4, _ => st.polygon_offset_primitive_style,
    };
    Ok(if st.polygon_offset_enabled != 0 && st.polygon_offset_styles & style != 0 {
        wgpu::DepthBiasState {
            constant: st.polygon_offset_units.round() as i32,
            slope_scale: st.polygon_offset_factor,
            clamp: 0.0,
        }
    } else { wgpu::DepthBiasState::default() })
}

// D32Float precision is an Infra property. The Core's original-face slope
// is constant per draw; Core captures the original face maximum before masking.
pub(super) fn d32_depth_quantum(max_depth: f32) -> f32 {
    let exponent = (max_depth.to_bits() >> 23) & 0xff;
    if exponent == 0 { f32::from_bits(1) }
    else { 2.0f32.powi(exponent as i32 - 127 - 23) }
}

pub(super) fn resolved_polygon_bias(st: &CoinWgpuRenderState, draw: &CoinWgpuDraw,
                         vertices: &[CoinWgpuVertex], indices: &[u32]) -> Result<f32, String> {
    if st.polygon_offset_factor != 0.0 {
        return Err("Resolved polygon slope bias requires zero GPU slope factor".to_string());
    }
    let resolved_maximum = if st.polygon_offset_max_depth_bits != 0 {
        let depth = f32::from_bits(st.polygon_offset_max_depth_bits - 1);
        if !(0.0..=1.0).contains(&depth) {
            return Err("Invalid original polygon depth".to_string());
        }
        Some(depth)
    } else { None };
    let mut max_depth = 0.0f32;
    for index in &indices[draw.first_index as usize..(draw.first_index + draw.index_count) as usize] {
        let p = vertices[*index as usize].position;
        let m = &st.model_view_projection;
        let z = m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14];
        let w = m[3]*p[0] + m[7]*p[1] + m[11]*p[2] + m[15];
        if !z.is_finite() || !w.is_finite() {
            return Err("Invalid clip depth for resolved polygon bias".to_string());
        }
        // Core measures the clipped original face. Its retained triangle can
        // still have raw corners at or behind the eye: raster clipping handles
        // them, and they must not replace that visible-face depth metric.
        if resolved_maximum.is_some() { continue; }
        if w <= 0.0 {
            return Err("Invalid clip depth for resolved polygon bias".to_string());
        }
        let depth = st.depth_range[0] + (z/w) * (st.depth_range[1]-st.depth_range[0]);
        max_depth = max_depth.max(depth.clamp(0.0, 1.0));
    }
    if let Some(depth) = resolved_maximum {
        max_depth = depth;
    }
    let bias = st.polygon_offset_slope_bias + st.polygon_offset_units * d32_depth_quantum(max_depth);
    if !bias.is_finite() { return Err("Invalid resolved polygon depth bias".to_string()); }
    Ok(bias)
}

// A depth-only fullscreen triangle, clipped to the annotation viewport. Loading
// depth here preserves the rest of the target; color is never attached.
pub(super) fn annotation_depth_clear_pipeline(device: &wgpu::Device, sample_count: u32) -> wgpu::RenderPipeline {
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("Coin Annotation Depth Clear Shader"),
        source: wgpu::ShaderSource::Wgsl(std::borrow::Cow::Borrowed(r#"
            @vertex fn vs_main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
                let positions = array<vec2<f32>, 3>(
                    vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
                return vec4<f32>(positions[index], 1.0, 1.0);
            }
        "#)),
    });
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("Coin Annotation Depth Clear Pipeline"),
        layout: None,
        vertex: wgpu::VertexState {
            module: &shader, entry_point: Some("vs_main"), buffers: &[],
            compilation_options: wgpu::PipelineCompilationOptions::default(),
        },
        fragment: None,
        primitive: wgpu::PrimitiveState::default(),
        depth_stencil: Some(wgpu::DepthStencilState {
            format: wgpu::TextureFormat::Depth32Float,
            depth_write_enabled: true, depth_compare: wgpu::CompareFunction::Always,
            stencil: wgpu::StencilState::default(), bias: wgpu::DepthBiasState::default(),
        }),
        multisample: wgpu::MultisampleState { count: sample_count, ..Default::default() },
        multiview: None, cache: None,
    })
}

pub(super) fn get_or_create_pipeline<'a>(
    ctx: &'a DeviceState,
    profile: &SurfaceSamplingProfile,
    topology: u32,
    color_format: wgpu::TextureFormat,
    depth_format: wgpu::TextureFormat,
    sample_count: u32,
    cull_face: Option<wgpu::Face>,
    front_face: wgpu::FrontFace,
    blend: bool,
    additive: bool,
    legacy_blend_alpha: bool,
    peel: bool,
    depth_write: bool,
    color_write: u32,
    depth_compare: wgpu::CompareFunction,
    depth_bias: wgpu::DepthBiasState,
    resolved_depth_bias: bool,
    eight_shadows: bool,
    shadow_profile: bool,
    weighted: bool,
    textured: bool,
    instanced: bool,
) -> Result<wgpu::RenderPipeline, String> {
    let mut map = profile.pipelines.lock().map_err(|e| e.to_string())?;

    // Lines and Points have culling disabled by specification
    let (primitive_topology, selected_shader, effective_cull) = match topology {
        1 => (wgpu::PrimitiveTopology::LineList, &profile.line_shader_module, None),
        2 => (wgpu::PrimitiveTopology::PointList, &profile.point_shader_module, None),
        _ => (wgpu::PrimitiveTopology::TriangleList,
            if instanced { &profile.instanced_shader } else if eight_shadows { &profile.shadow_eight_shader } else if shadow_profile { &profile.shadow_shader } else { &profile.shader_module }, cull_face),
    };

    let key = PipelineKey {
        instanced, eight_shadows, shadow_profile, weighted, topology, textured,
        color_format,
        depth_format,
        sample_count,
        cull_mode: effective_cull,
        front_face,
        blend,
        additive,
        legacy_blend_alpha,
        peel,
        depth_write,
        color_write,
        depth_compare,
        resolved_depth_bias,
        depth_bias_constant: depth_bias.constant,
        depth_bias_slope_bits: depth_bias.slope_scale.to_bits(),
    };
    if let Some(p) = map.get(&key) {
        ctx.pipeline_hits.fetch_add(1, Ordering::Relaxed);
        return Ok(p.clone());
    }

    let vertex_buffer_layout = wgpu::VertexBufferLayout {
        array_stride: std::mem::size_of::<CoinWgpuVertex>() as wgpu::BufferAddress,
        step_mode: wgpu::VertexStepMode::Vertex,
        attributes: &[
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32x3,
                offset: 0,
                shader_location: 0,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32x3,
                offset: 12,
                shader_location: 1,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32x4,
                offset: 24,
                shader_location: 2,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Uint32,
                offset: 40,
                shader_location: 3,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32,
                offset: 44,
                shader_location: 4,
            },
            wgpu::VertexAttribute {
                format: wgpu::VertexFormat::Float32,
                offset: 48,
                shader_location: 5,
            },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 52, shader_location: 6 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 68, shader_location: 7 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 84, shader_location: 8 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 100, shader_location: 9 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 116, shader_location: 10 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 132, shader_location: 11 },
            wgpu::VertexAttribute { format: wgpu::VertexFormat::Float32x4, offset: 148, shader_location: 12 },
        ],
    };

    let pipeline_label = match topology {
        1 => "Coin Line Render Pipeline",
        2 => "Coin Point Render Pipeline",
        _ => "Coin Standard Render Pipeline",
    };

    let mut color_targets = vec![Some(wgpu::ColorTargetState {
        format: color_format,
        blend: Some(if blend {
            wgpu::BlendState {
                color: wgpu::BlendComponent {
                    src_factor: wgpu::BlendFactor::SrcAlpha,
                    dst_factor: if additive {
                        wgpu::BlendFactor::One
                    } else {
                        wgpu::BlendFactor::OneMinusSrcAlpha
                    },
                    operation: wgpu::BlendOperation::Add,
                },
                alpha: wgpu::BlendComponent {
                    src_factor: if additive || legacy_blend_alpha {
                        wgpu::BlendFactor::SrcAlpha
                    } else {
                        wgpu::BlendFactor::One
                    },
                    dst_factor: if additive {
                        wgpu::BlendFactor::One
                    } else {
                        wgpu::BlendFactor::OneMinusSrcAlpha
                    },
                    operation: wgpu::BlendOperation::Add,
                },
            }
        } else {
            wgpu::BlendState::REPLACE
        }),
        write_mask: if color_write == 1 { wgpu::ColorWrites::ALL }
                    else { wgpu::ColorWrites::from_bits_truncate(color_write & 0x0f) },
    })];
    if weighted {
        let additive=wgpu::BlendComponent { src_factor:wgpu::BlendFactor::One,
            dst_factor:wgpu::BlendFactor::One,operation:wgpu::BlendOperation::Add };
        color_targets[0].as_mut().unwrap().blend=Some(wgpu::BlendState{color:additive,alpha:additive});
        let reveal=wgpu::BlendComponent { src_factor:wgpu::BlendFactor::Zero,
            dst_factor:wgpu::BlendFactor::OneMinusSrc,operation:wgpu::BlendOperation::Add };
        color_targets.push(Some(wgpu::ColorTargetState{format:wgpu::TextureFormat::R16Float,
            blend:Some(wgpu::BlendState{color:reveal,alpha:reveal}),write_mask:wgpu::ColorWrites::ALL}));
    }
    if peel {
        color_targets.push(Some(wgpu::ColorTargetState {
            format: wgpu::TextureFormat::R8Unorm,
            blend: None,
            write_mask: wgpu::ColorWrites::ALL,
        }));
    }
    // wgpu-hal 24's GL program cache keys module IDs/entry points but omits
    // override constants. Give textured/untextured variants distinct modules
    // so switching profiles cannot reuse the first profile's GL program.
    let selected_shader = if !profile.bump_receiver && !instanced && !textured && ctx.adapter.get_info().backend == wgpu::Backend::Gl {
        let mut profiles = profile.untextured_shaders.lock().map_err(|e| e.to_string())?;
        profiles.entry((topology, shadow_profile, eight_shadows)).or_insert_with(|| {
            let source = match topology {
                1 | 2 => WGSL_PRIMITIVE_SHADER.to_owned(),
                _ if eight_shadows => if profile.shadow_multitexture { shadow_receiver::eight_map_multitexture_source() } else { shadow_receiver::eight_map_source() },
                _ if shadow_profile => if profile.shadow_multitexture { shadow_receiver::four_map_multitexture_source() } else { shadow_receiver::four_map_source() },
                _ => shader_profile::without_shadows(WGSL_SHADER),
            };
            ctx.device.create_shader_module(wgpu::ShaderModuleDescriptor {
                label: Some("Coin untextured shader profile"),
                source: wgpu::ShaderSource::Wgsl(shader_profile::depth_load_profile(
                    &shader_profile::sample_depth_profile(&shader_profile::peeling_msaa_profile(&shader_profile::texture_specialization(
                        &shader_profile::sampling_profile(&format!("{}{}", source, weighted::FRAGMENT), profile.policy != SamplingPolicy::Native)), profile.multisampled_peel), profile.sample_depth), true).into()),
            })
        }).clone()
    } else { selected_shader.clone() };
    let constants = HashMap::from([("coin_textures_enabled".to_owned(), if textured { 1.0 } else { 0.0 })]);
    let pipeline = ctx
        .device
        .create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some(pipeline_label),
            layout: Some(if instanced { &profile.instanced_pipeline_layout } else if eight_shadows { &profile.shadow_eight_pipeline_layout } else { &profile.pipeline_layout }),
            vertex: wgpu::VertexState {
                module: &selected_shader,
                entry_point: Some("vs_main"),
                buffers: &[vertex_buffer_layout],
                compilation_options: wgpu::PipelineCompilationOptions { constants: &constants, ..Default::default() },
            },
            fragment: Some(wgpu::FragmentState {
                module: &selected_shader,
                entry_point: Some(if weighted { "fs_weighted" } else if peel {
                    "fs_peel"
                } else if resolved_depth_bias {
                    "fs_depth_bias"
                } else {
                    "fs_main"
                }),
                targets: &color_targets,
                compilation_options: wgpu::PipelineCompilationOptions { constants: &constants, ..Default::default() },
            }),
            primitive: wgpu::PrimitiveState {
                topology: primitive_topology,
                strip_index_format: None,
                front_face,
                cull_mode: effective_cull,
                polygon_mode: wgpu::PolygonMode::Fill,
                unclipped_depth: false,
                conservative: false,
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: depth_format,
                depth_write_enabled: depth_write,
                depth_compare,
                stencil: wgpu::StencilState::default(),
                bias: depth_bias,
            }),
            multisample: wgpu::MultisampleState { count: sample_count, ..Default::default() },
            multiview: None,
            cache: None,
        });

    map.insert(key, pipeline.clone());
    ctx.pipeline_compilations.fetch_add(1, Ordering::Relaxed);
    Ok(pipeline)
}
