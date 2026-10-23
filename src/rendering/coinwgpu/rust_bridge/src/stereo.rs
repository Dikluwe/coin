//! Completed views are composed before the authored monoscopic foreground.
//! No authored resource slot is merged with another view's slot namespace.
use super::{CoinWgpuFrameView, CoinWgpuStatus, DeviceState};
use wgpu::util::DeviceExt;

pub(super) fn encode(ctx:&DeviceState, frame:&CoinWgpuFrameView, size:[u32;2],
    color:&wgpu::TextureView, format:wgpu::TextureFormat, depth:&wgpu::TextureView,
    samples:u32) -> Result<wgpu::CommandBuffer,(CoinWgpuStatus,String)> {
    let invalid=|message:&str|(CoinWgpuStatus::InvalidArgument,message.to_string());
    if !(1..=4).contains(&frame.stereo_mode) || (frame.stereo_mode!=4 &&
        (frame.stereo_tokens[0]==frame.stereo_tokens[1] || frame.stereo_extent.iter().any(|n|*n<=0))) {
        return Err(invalid("Invalid stereo completed-view composition"));
    }
    let registry=ctx.rtt_textures.lock().unwrap();
    let lookup=|token:u64| {
        registry.active.get(&token).filter(|view|view.width==size[0] && view.height==size[1])
            .and_then(|view|view.completed_view.as_ref()).filter(|view|view.2==samples)
            .ok_or_else(||invalid("Stereo view token is stale, from another device, or has mismatched samples/extent"))
    };
    let left=lookup(frame.stereo_tokens[0])?;
    let right=lookup(frame.stereo_tokens[1])?;
    let texture=if samples>1 {"texture_multisampled_2d<f32>"}else{"texture_2d<f32>"};
    // Match DepthTransfer: Naga's GLSL writer cannot lower a single-sample
    // depth-typed textureLoad. A non-filterable float view reads the same
    // Depth32Float texel without comparison or conversion. Multisampled
    // depth keeps its typed binding and the existing GLSL texelFetch path.
    let depth_texture=if samples>1 {"texture_depth_multisampled_2d"}else{"texture_2d<f32>"};
    let depth_channel=if samples>1 {""}else{".r"};
    let sample_argument=if samples>1 {", @builtin(sample_index) sample:u32"}else{""};
    let index=if samples>1 {"i32(sample)"}else{"0"};
    let source=format!(r#"
@group(0) @binding(0) var left_color: {texture};
@group(0) @binding(1) var right_color: {texture};
@group(0) @binding(2) var left_depth: {depth_texture};
@group(0) @binding(3) var right_depth: {depth_texture};
@group(0) @binding(4) var<uniform> parameters: array<vec4<i32>,3>;
@vertex fn vertex(@builtin(vertex_index) i:u32)->@builtin(position) vec4<f32> {{
 let x=f32((i<<1u)&2u); let y=f32(i&2u);
 return vec4<f32>(2.0*x-1.0,1.0-2.0*y,0.0,1.0);
}}
struct Output {{ @location(0) color:vec4<f32>, @builtin(frag_depth) depth:f32 }}
@fragment fn fragment(@builtin(position) position:vec4<f32>{sample_argument})->Output {{
 let pixel=vec2<i32>(position.xy);
 let native_pixel=vec2<i32>(pixel.x,parameters[1].y-1-pixel.y);
 let local=native_pixel-parameters[0].yz;
 let inside=all(local>=vec2<i32>(0)) && all(local<vec2<i32>(parameters[0].w,parameters[1].x));
 let mode=parameters[0].x;
 let left_phase=inside && select((local.x&1)==1,(local.y&1)==1,mode==2);
 let l=textureLoad(left_color,pixel,{index}); let r=textureLoad(right_color,pixel,{index});
 let ld=textureLoad(left_depth,pixel,{index}){depth_channel}; let rd=textureLoad(right_depth,pixel,{index}){depth_channel};
 var output:Output;
 output.color=select(r,l,left_phase); output.depth=select(rd,ld,left_phase);
 if(mode==1) {{ output.color=vec4<f32>(l.r,r.gba); output.depth=rd; }}
 if(mode==4) {{ output.color=vec4<f32>(bitcast<vec3<f32>>(parameters[2].xyz),l.a); output.depth=1.0; }}
 return output;
}}
"#);
    let shader=ctx.device.create_shader_module(wgpu::ShaderModuleDescriptor{
        label:Some("Coin completed stereo views"),source:wgpu::ShaderSource::Wgsl(source.into())});
    let entry=|binding,depth|wgpu::BindGroupLayoutEntry {binding,visibility:wgpu::ShaderStages::FRAGMENT,
        ty:wgpu::BindingType::Texture {sample_type:if depth && samples>1{wgpu::TextureSampleType::Depth}else{wgpu::TextureSampleType::Float{filterable:false}},
            view_dimension:wgpu::TextureViewDimension::D2,multisampled:samples>1},count:None};
    let layout=ctx.device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor{
        label:Some("Coin stereo view layout"),entries:&[entry(0,false),entry(1,false),entry(2,true),entry(3,true),
            wgpu::BindGroupLayoutEntry{binding:4,visibility:wgpu::ShaderStages::FRAGMENT,
                ty:wgpu::BindingType::Buffer{ty:wgpu::BufferBindingType::Uniform,has_dynamic_offset:false,min_binding_size:None},count:None}]});
    let pipeline_layout=ctx.device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor{
        label:Some("Coin stereo view layout"),bind_group_layouts:&[&layout],push_constant_ranges:&[]});
    let pipeline=ctx.device.create_render_pipeline(&wgpu::RenderPipelineDescriptor{
        label:Some("Coin stereo completed-view transfer"),layout:Some(&pipeline_layout),
        vertex:wgpu::VertexState{module:&shader,entry_point:Some("vertex"),compilation_options:Default::default(),buffers:&[]},
        primitive:Default::default(),depth_stencil:Some(wgpu::DepthStencilState{format:wgpu::TextureFormat::Depth32Float,
            depth_write_enabled:true,depth_compare:wgpu::CompareFunction::Always,stencil:Default::default(),bias:Default::default()}),
        multisample:wgpu::MultisampleState{count:samples,..Default::default()},
        fragment:Some(wgpu::FragmentState{module:&shader,entry_point:Some("fragment"),compilation_options:Default::default(),
            targets:&[Some(wgpu::ColorTargetState{format,blend:None,write_mask:wgpu::ColorWrites::ALL})]}),multiview:None,cache:None});
    let parameters=[frame.stereo_mode as i32,frame.stereo_origin[0],frame.stereo_origin[1],frame.stereo_extent[0],
        frame.stereo_extent[1],size[1] as i32,0,0,
        frame.clear_color[0].to_bits() as i32,frame.clear_color[1].to_bits() as i32,frame.clear_color[2].to_bits() as i32,0];
    let uniform=ctx.device.create_buffer_init(&wgpu::util::BufferInitDescriptor{
        label:Some("Coin stereo manager phase"),contents:bytemuck::cast_slice(&parameters),usage:wgpu::BufferUsages::UNIFORM});
    let bindings=ctx.device.create_bind_group(&wgpu::BindGroupDescriptor{label:Some("Coin completed stereo views"),layout:&layout,
        entries:&[wgpu::BindGroupEntry{binding:0,resource:wgpu::BindingResource::TextureView(&left.0)},
            wgpu::BindGroupEntry{binding:1,resource:wgpu::BindingResource::TextureView(&right.0)},
            wgpu::BindGroupEntry{binding:2,resource:wgpu::BindingResource::TextureView(&left.1)},
            wgpu::BindGroupEntry{binding:3,resource:wgpu::BindingResource::TextureView(&right.1)},
            wgpu::BindGroupEntry{binding:4,resource:uniform.as_entire_binding()}]});
    let mut encoder=ctx.device.create_command_encoder(&wgpu::CommandEncoderDescriptor{label:Some("Coin stereo transfer")});
    {
        let mut pass=encoder.begin_render_pass(&wgpu::RenderPassDescriptor{label:Some("Coin stereo completed views"),
            color_attachments:&[Some(wgpu::RenderPassColorAttachment{view:color,resolve_target:None,
                ops:wgpu::Operations{load:wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),store:wgpu::StoreOp::Store}})],
            depth_stencil_attachment:Some(wgpu::RenderPassDepthStencilAttachment{view:depth,
                depth_ops:Some(wgpu::Operations{load:wgpu::LoadOp::Clear(1.0),store:wgpu::StoreOp::Store}),stencil_ops:None}),
            timestamp_writes:None,occlusion_query_set:None});
        pass.set_pipeline(&pipeline);pass.set_bind_group(0,&bindings,&[]);pass.draw(0..3,0..1);
    }
    Ok(encoder.finish())
}
