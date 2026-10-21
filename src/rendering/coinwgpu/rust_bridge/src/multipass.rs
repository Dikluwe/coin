//! Private, single-publication multipass color accumulation. Depth is never
//! sampled or resolved here: the scene encoder retains the final pass depth.
use wgpu::util::DeviceExt;
use std::sync::{Arc,Mutex};
use std::collections::HashMap;

const ACCUMULATE: &str = r#"
@group(0) @binding(0) var scene: texture_2d<f32>;
@group(0) @binding(1) var previous: texture_2d<f32>;
@group(0) @binding(2) var output_sum: texture_storage_2d<rgba32float, write>;
@group(0) @binding(3) var<uniform> parameters: vec4<u32>;
@compute @workgroup_size(8,8)
fn accumulate(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= parameters.z || id.y >= parameters.w) { return; }
    let pixel = vec2<i32>(id.xy);
    var sum = textureLoad(scene, pixel, 0);
    if (parameters.x != 0u) { sum += textureLoad(previous, pixel, 0); }
    textureStore(output_sum, pixel, sum);
}
"#;
const RESOLVE: &str = r#"
@group(0) @binding(0) var accumulated: texture_2d<f32>;
@group(0) @binding(1) var<uniform> parameters: vec4<u32>;
@vertex fn vertex(@builtin(vertex_index) index:u32) -> @builtin(position) vec4<f32> {
    let x = f32((index << 1u) & 2u);
    let y = f32(index & 2u);
    return vec4<f32>(x*2.0-1.0, 1.0-y*2.0, 0.0, 1.0);
}
@fragment fn fragment(@builtin(position) position:vec4<f32>) -> @location(0) vec4<f32> {
    return textureLoad(accumulated, vec2<i32>(position.xy), 0) / f32(parameters.y);
}
"#;

pub(crate) fn bytes(width:u32, height:u32) -> Option<u64> {
    // Two alternating RGBA32F sums. Scene/MSAA/feature attachments are charged
    // by their existing owner; they are reused across every sample.
    u64::from(width).checked_mul(u64::from(height))?.checked_mul(32)
}

pub(crate) struct Programs {
    compute: wgpu::ComputePipeline,
    resolve: wgpu::RenderPipeline,
}
impl Programs {
    fn new(device:&wgpu::Device, output:wgpu::TextureFormat) -> Self {
        let compute_shader=device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label:Some("Coin multipass sum"),source:wgpu::ShaderSource::Wgsl(ACCUMULATE.into()),
        });
        let texture_entry = |binding, visibility| wgpu::BindGroupLayoutEntry {
            binding, visibility, ty:wgpu::BindingType::Texture {
                sample_type:wgpu::TextureSampleType::Float {filterable:false},
                view_dimension:wgpu::TextureViewDimension::D2,multisampled:false,
            },count:None,
        };
        let uniform_entry = |binding, visibility| wgpu::BindGroupLayoutEntry {
            binding,visibility,ty:wgpu::BindingType::Buffer {
                ty:wgpu::BufferBindingType::Uniform,has_dynamic_offset:false,min_binding_size:None,
            },count:None,
        };
        let compute_bindings=device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label:Some("Coin multipass compute layout"),entries:&[
                texture_entry(0,wgpu::ShaderStages::COMPUTE),texture_entry(1,wgpu::ShaderStages::COMPUTE),
                wgpu::BindGroupLayoutEntry {binding:2,visibility:wgpu::ShaderStages::COMPUTE,
                    ty:wgpu::BindingType::StorageTexture {access:wgpu::StorageTextureAccess::WriteOnly,
                        format:wgpu::TextureFormat::Rgba32Float,view_dimension:wgpu::TextureViewDimension::D2},count:None},
                uniform_entry(3,wgpu::ShaderStages::COMPUTE),
            ],
        });
        let compute_layout=device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label:Some("Coin multipass compute layout"),bind_group_layouts:&[&compute_bindings],push_constant_ranges:&[],
        });
        let compute=device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label:Some("Coin multipass sum"),layout:Some(&compute_layout),module:&compute_shader,
            entry_point:Some("accumulate"),compilation_options:Default::default(),cache:None,
        });
        let resolve_shader=device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label:Some("Coin multipass mean"),source:wgpu::ShaderSource::Wgsl(RESOLVE.into()),
        });
        let resolve_bindings=device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label:Some("Coin multipass resolve layout"),entries:&[
                texture_entry(0,wgpu::ShaderStages::FRAGMENT),uniform_entry(1,wgpu::ShaderStages::FRAGMENT),
            ],
        });
        let resolve_layout=device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label:Some("Coin multipass resolve layout"),bind_group_layouts:&[&resolve_bindings],push_constant_ranges:&[],
        });
        let resolve=device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label:Some("Coin multipass mean"),layout:Some(&resolve_layout),
            vertex:wgpu::VertexState { module:&resolve_shader,entry_point:Some("vertex"),
                compilation_options:Default::default(),buffers:&[] },
            primitive:Default::default(),depth_stencil:None,multisample:Default::default(),
            fragment:Some(wgpu::FragmentState {module:&resolve_shader,entry_point:Some("fragment"),
                compilation_options:Default::default(),targets:&[Some(wgpu::ColorTargetState {
                    format:output,blend:None,write_mask:wgpu::ColorWrites::ALL,
                })]}),multiview:None,cache:None,
        });
        Self {compute,resolve}
    }
}
#[derive(Default)]
pub(crate) struct Cache(Mutex<HashMap<wgpu::TextureFormat,Arc<Programs>>>);
impl Cache {
    pub(crate) fn get(&self,device:&wgpu::Device,format:wgpu::TextureFormat) -> Result<Arc<Programs>,String> {
        let mut entries=self.0.lock().unwrap();
        if let Some(programs)=entries.get(&format) { return Ok(programs.clone()); }
        let programs=Arc::new(Programs::new(device,format));
        // Pipeline creation can load native compiler modules lazily. Keep the
        // process-exit cleanup ahead of handlers installed by those modules.
        super::register_runtime_exit()?;
        entries.insert(format,programs.clone());
        Ok(programs)
    }
}
pub(crate) struct Accumulation {
    _textures: [wgpu::Texture;2],
    views: [wgpu::TextureView;2],
    programs: Arc<Programs>,
    width:u32,
    height:u32,
    count:u32,
}
impl Accumulation {
    pub(crate) fn new(device:&wgpu::Device, width:u32, height:u32,
                      count:u32, programs:Arc<Programs>) -> Self {
        let create = || device.create_texture(&wgpu::TextureDescriptor {
            label:Some("Coin multipass RGBA32F sum"),
            size:wgpu::Extent3d {width,height,depth_or_array_layers:1},
            mip_level_count:1,sample_count:1,dimension:wgpu::TextureDimension::D2,
            format:wgpu::TextureFormat::Rgba32Float,
            usage:wgpu::TextureUsages::STORAGE_BINDING|wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats:&[],
        });
        let textures=[create(),create()];
        let views=[textures[0].create_view(&Default::default()),textures[1].create_view(&Default::default())];
        Self {_textures:textures,views,programs,width,height,count}
    }
    pub(crate) fn accumulate(&self, device:&wgpu::Device, scene:&wgpu::TextureView,
                            sample:u32) -> wgpu::CommandBuffer {
        let data=[sample,self.count,self.width,self.height];
        let uniform=device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label:Some("Coin multipass sample"),contents:bytemuck::cast_slice(&data),usage:wgpu::BufferUsages::UNIFORM,
        });
        let output=(sample&1) as usize;
        let binding=device.create_bind_group(&wgpu::BindGroupDescriptor {
            label:Some("Coin multipass sum inputs"),layout:&self.programs.compute.get_bind_group_layout(0),
            entries:&[
                wgpu::BindGroupEntry {binding:0,resource:wgpu::BindingResource::TextureView(scene)},
                wgpu::BindGroupEntry {binding:1,resource:wgpu::BindingResource::TextureView(&self.views[1-output])},
                wgpu::BindGroupEntry {binding:2,resource:wgpu::BindingResource::TextureView(&self.views[output])},
                wgpu::BindGroupEntry {binding:3,resource:uniform.as_entire_binding()},
            ],
        });
        let mut encoder=device.create_command_encoder(&Default::default());
        {
            let mut pass=encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {label:Some("Coin multipass sum"),timestamp_writes:None});
            pass.set_pipeline(&self.programs.compute);pass.set_bind_group(0,&binding,&[]);
            pass.dispatch_workgroups(self.width.div_ceil(8),self.height.div_ceil(8),1);
        }
        encoder.finish()
    }
    pub(crate) fn resolve(&self, device:&wgpu::Device, output:&wgpu::TextureView) -> wgpu::CommandBuffer {
        let data=[0u32,self.count,self.width,self.height];
        let uniform=device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label:Some("Coin multipass count"),contents:bytemuck::cast_slice(&data),usage:wgpu::BufferUsages::UNIFORM,
        });
        let binding=device.create_bind_group(&wgpu::BindGroupDescriptor {
            label:Some("Coin multipass final sum"),layout:&self.programs.resolve.get_bind_group_layout(0),
            entries:&[
                wgpu::BindGroupEntry {binding:0,resource:wgpu::BindingResource::TextureView(&self.views[((self.count-1)&1) as usize])},
                wgpu::BindGroupEntry {binding:1,resource:uniform.as_entire_binding()},
            ],
        });
        let mut encoder=device.create_command_encoder(&Default::default());
        {
            let mut pass=encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label:Some("Coin multipass final mean"),color_attachments:&[Some(wgpu::RenderPassColorAttachment {
                    view:output,resolve_target:None,ops:wgpu::Operations {load:wgpu::LoadOp::Load,store:wgpu::StoreOp::Store},
                })],depth_stencil_attachment:None,occlusion_query_set:None,timestamp_writes:None,
            });
            pass.set_pipeline(&self.programs.resolve);pass.set_bind_group(0,&binding,&[]);pass.draw(0..3,0..1);
        }
        encoder.finish()
    }
}

// Coin's native tables and f32 operation order; never indexed for count1.
pub(crate) fn jitter_states(states:&[super::CoinWgpuRenderState], count:u32, sample:u32)
    -> Vec<super::CoinWgpuRenderState> {
    const J2: [f32;4] = [0.25, 0.77, 0.75, 0.25];
    const J3: [f32;6] = [0.5033922635, 0.8317967229, 0.7806016275, 0.2504380877, 0.2261828938, 0.4131553612];
    const J4: [f32;8] = [0.375, 0.25, 0.125, 0.75, 0.875, 0.25, 0.625, 0.75];
    const J5: [f32;10] = [0.5, 0.5, 0.3, 0.1, 0.7, 0.9, 0.9, 0.3, 0.1, 0.7];
    const J6: [f32;12] = [0.4646464646, 0.4646464646, 0.1313131313, 0.7979797979, 0.5353535353, 0.8686868686, 0.8686868686, 0.5353535353, 0.7979797979, 0.1313131313, 0.2020202020, 0.2020202020];
    const J8: [f32;16] = [0.5625, 0.4375, 0.0625, 0.9375, 0.3125, 0.6875, 0.6875, 0.8125, 0.8125, 0.1875, 0.9375, 0.5625, 0.4375, 0.0625, 0.1875, 0.3125];
    const J9: [f32;18] = [0.5, 0.5, 0.1666666666, 0.9444444444, 0.5, 0.1666666666, 0.5, 0.8333333333, 0.1666666666, 0.2777777777, 0.8333333333, 0.3888888888, 0.1666666666, 0.6111111111, 0.8333333333, 0.7222222222, 0.8333333333, 0.0555555555];
    const J12: [f32;24] = [0.4166666666, 0.625, 0.9166666666, 0.875, 0.25, 0.375, 0.4166666666, 0.125, 0.75, 0.125, 0.0833333333, 0.125, 0.75, 0.625, 0.25, 0.875, 0.5833333333, 0.375, 0.9166666666, 0.375, 0.0833333333, 0.625, 0.583333333, 0.875];
    const J16: [f32;32] = [0.375, 0.4375, 0.625, 0.0625, 0.875, 0.1875, 0.125, 0.0625, 0.375, 0.6875, 0.875, 0.4375, 0.625, 0.5625, 0.375, 0.9375, 0.625, 0.3125, 0.125, 0.5625, 0.125, 0.8125, 0.375, 0.1875, 0.875, 0.9375, 0.875, 0.6875, 0.125, 0.3125, 0.625, 0.8125];
    let table:&[f32]=match count {2=>&J2,3=>&J3,4=>&J4,5=>&J5,6=>&J6,
        7|8=>&J8,9=>&J9,10..=12=>&J12,13..=16=>&J16,_=>return states.to_vec()};
    let mut output=states.to_vec();
    for state in &mut output {
        if state.viewport[2]<=0 || state.viewport[3]<=0 {continue;}
        let dx=(table[sample as usize*2]-0.5)*2.0/state.viewport[2] as f32;
        let dy=(table[sample as usize*2+1]-0.5)*2.0/state.viewport[3] as f32;
        // Postmultiply by a clip translation, preserving the original W/depth.
        // FFI matrices are column-major; each X/Y row gains displacement*W.
        for column in 0..4 {
            let w=state.model_view_projection[column*4+3];
            state.model_view_projection[column*4]+=dx*w;
            state.model_view_projection[column*4+1]+=dy*w;
        }
    }
    output
}
