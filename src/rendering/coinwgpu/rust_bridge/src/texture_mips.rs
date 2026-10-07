//! GPU-only RTT mip generation. No staging, map, readback or CPU image fallback.
use crate::CoinWgpuStatus;
pub fn encode(
    device: &wgpu::Device,
    texture: &wgpu::Texture,
    format: wgpu::TextureFormat,
    count: u32,
    width: u32,
    height: u32,
    separate_source: bool,
) -> Result<wgpu::CommandBuffer, (CoinWgpuStatus, String)> {
    let storage = match format {
        wgpu::TextureFormat::Rgba8Unorm => "rgba8unorm",
        wgpu::TextureFormat::Rgba16Float => "rgba16float",
        _ => {
            return Err((
                CoinWgpuStatus::Unsupported,
                "Unsupported direct RTT mip format".into(),
            ))
        }
    };
    let shader = shader_source(storage);
    let module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("Coin direct RTT mip box"),
        source: wgpu::ShaderSource::Wgsl(shader.into()),
    });
    let pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
        label: Some("Coin direct RTT mip box"),
        layout: None,
        module: &module,
        entry_point: Some("main"),
        compilation_options: Default::default(),
        cache: None,
    });
    let layout = pipeline.get_bind_group_layout(0);
    let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
        label: Some("Coin RTT exact texel sampler"),
        ..Default::default()
    });
    let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label: Some("Coin RTT complete mip chain"),
    });
    let (mut w, mut h) = (width, height);
    for level in 1..count {
        let (source_width, source_height) = (w, h);
        w = (w / 2).max(1);
        h = (h / 2).max(1);
        let view = |mip| {
            texture.create_view(&wgpu::TextureViewDescriptor {
                base_mip_level: mip,
                mip_level_count: Some(1),
                ..Default::default()
            })
        };
        // OpenGL view/base-level aliasing is avoided by a GPU-only copy into
        // a single-level texture. No map, CPU readback or image staging.
        let scratch = separate_source.then(|| {
            device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Coin RTT mip source isolation"),
                size: wgpu::Extent3d {
                    width: source_width,
                    height: source_height,
                    depth_or_array_layers: 1,
                },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format,
                usage: wgpu::TextureUsages::COPY_DST | wgpu::TextureUsages::TEXTURE_BINDING,
                view_formats: &[],
            })
        });
        let source = if let Some(scratch) = scratch.as_ref() {
            encoder.copy_texture_to_texture(
                wgpu::TexelCopyTextureInfo {
                    texture,
                    mip_level: level - 1,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                wgpu::TexelCopyTextureInfo {
                    texture: scratch,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                wgpu::Extent3d {
                    width: source_width,
                    height: source_height,
                    depth_or_array_layers: 1,
                },
            );
            scratch.create_view(&wgpu::TextureViewDescriptor::default())
        } else {
            view(level - 1)
        };
        let output = view(level);
        let bindings = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("Coin RTT mip pair"),
            layout: &layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&source),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&output),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::Sampler(&sampler),
                },
            ],
        });
        let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
            label: Some("Coin RTT downsample level"),
            timestamp_writes: None,
        });
        pass.set_pipeline(&pipeline);
        pass.set_bind_group(0, &bindings, &[]);
        pass.dispatch_workgroups(w.div_ceil(8), h.div_ceil(8), 1);
    }
    Ok(encoder.finish())
}

fn shader_source(storage: &str) -> String {
    format!(
        r#"
@group(0) @binding(0) var source:texture_2d<f32>;
@group(0) @binding(2) var exactSampler:sampler;
@group(0) @binding(1) var output:texture_storage_2d<{storage},write>;
fn power2(v:u32)->bool {{return v>0u && (v&(v-1u))==0u;}}
@compute @workgroup_size(8,8)
fn main(@builtin(global_invocation_id) id:vec3<u32>) {{
    let dst=textureDimensions(output);if any(id.xy>=dst) {{return;}}
    let src=textureDimensions(source);
    let lo=id.xy*src;let hi=(id.xy+vec2<u32>(1))*src;
    var sum=vec4<f32>(0);var weight=0.0;
    for(var y=lo.y/dst.y;y<(hi.y+dst.y-1u)/dst.y;y++) {{
        let wy=min(hi.y,(y+1u)*dst.y)-max(lo.y,y*dst.y);
        for(var x=lo.x/dst.x;x<(hi.x+dst.x-1u)/dst.x;x++) {{
            let wx=min(hi.x,(x+1u)*dst.x)-max(lo.x,x*dst.x);
            var color=textureSampleLevel(source,exactSampler,(vec2<f32>(f32(x),f32(y))+vec2<f32>(0.5))/vec2<f32>(src),0.0);
            if "{storage}"=="rgba8unorm" {{color=round(color*255.0);}}
            let w=f32(wx)*f32(wy);sum+=color*w;weight+=w;
        }}
    }}
    var color=sum/weight;
    if "{storage}"=="rgba8unorm" {{
        let rounding=select(0.5,0.0,(src.x==1u || src.y==1u) && power2(src.x) && power2(src.y));
        color=floor(color+vec4<f32>(rounding+0.000001))/255.0;
    }}
    textureStore(output,vec2<i32>(id.xy),color);
}}
"#
    )
    .replace(
        &format!("\"{storage}\"==\"rgba8unorm\""),
        if storage == "rgba8unorm" {
            "true"
        } else {
            "false"
        },
    )
}

#[cfg(test)]
mod tests {
    #[test]
    fn both_output_shaders_validate() {
        for storage in ["rgba8unorm", "rgba16float"] {
            let module = naga::front::wgsl::parse_str(&super::shader_source(storage)).unwrap();
            naga::valid::Validator::new(
                naga::valid::ValidationFlags::all(),
                naga::valid::Capabilities::all(),
            )
            .validate(&module)
            .unwrap();
        }
    }
}
