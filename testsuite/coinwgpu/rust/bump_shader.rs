//! R26 shader execution probe. It bypasses the CoinRender frame ABI and does
//! not qualify B12 or the full CoinGL bump compositing contract.
use bytemuck::{Pod, Zeroable};
use std::sync::mpsc;
use std::time::Duration;
use wgpu::*;

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct Vertex {
    position: [f32; 3], normal: [f32; 3], coord: [f32; 4],
    tangent_s: [f32; 3], tangent_t: [f32; 3],
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct Uniforms {
    mvp: [f32; 16], light: [f32; 4], diffuse: [f32; 4],
    eye_position: [f32; 4], specular: [f32; 4], shininess: [f32; 4],
}

fn image(device: &Device, queue: &Queue, pipeline: &RenderPipeline,
         vertices: &Buffer, normal_view: &TextureView, sampler: &Sampler,
         light: [f32; 4]) -> [[u8; 4]; 2] {
    let uniform = Uniforms {
        mvp: [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
              0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0],
        light, diffuse: [0.8,0.2,0.1,1.0],
        eye_position: [0.0,0.0,5.0,1.0], specular: [0.5,0.25,0.125,1.0],
        shininess: [0.5,0.0,0.0,0.0],
    };
    let uniforms = device.create_buffer(&BufferDescriptor {
        label: Some("bump uniforms"), size: std::mem::size_of::<Uniforms>() as u64,
        usage: BufferUsages::UNIFORM | BufferUsages::COPY_DST, mapped_at_creation: false,
    });
    queue.write_buffer(&uniforms, 0, bytemuck::bytes_of(&uniform));
    let bind = device.create_bind_group(&BindGroupDescriptor {
        label: Some("bump diffuse resources"), layout: &pipeline.get_bind_group_layout(0),
        entries: &[
            BindGroupEntry {binding: 0, resource: uniforms.as_entire_binding()},
            BindGroupEntry {binding: 1, resource: BindingResource::TextureView(normal_view)},
            BindGroupEntry {binding: 2, resource: BindingResource::Sampler(sampler)},
        ],
    });
    let color = device.create_texture(&TextureDescriptor {
        label: Some("bump diffuse image"), size: Extent3d {width: 8,height: 8,depth_or_array_layers: 1},
        mip_level_count: 1, sample_count: 1, dimension: TextureDimension::D2,
        format: TextureFormat::Rgba8Unorm,
        usage: TextureUsages::RENDER_ATTACHMENT | TextureUsages::COPY_SRC, view_formats: &[],
    });
    let view = color.create_view(&TextureViewDescriptor::default());
    let readback = device.create_buffer(&BufferDescriptor {
        label: Some("bump diffuse readback"), size: 8 * 256,
        usage: BufferUsages::COPY_DST | BufferUsages::MAP_READ, mapped_at_creation: false,
    });
    let mut encoder = device.create_command_encoder(&CommandEncoderDescriptor::default());
    {
        let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
            label: Some("bump diffuse pass"),
            color_attachments: &[Some(RenderPassColorAttachment {
                view: &view, resolve_target: None,
                ops: Operations {load: LoadOp::Clear(Color::BLACK), store: StoreOp::Store},
            })],
            depth_stencil_attachment: None, timestamp_writes: None, occlusion_query_set: None,
        });
        pass.set_pipeline(pipeline);
        pass.set_bind_group(0, &bind, &[]);
        pass.set_vertex_buffer(0, vertices.slice(..));
        pass.draw(0..3, 0..1);
    }
    encoder.copy_texture_to_buffer(color.as_image_copy(), TexelCopyBufferInfo {
        buffer: &readback,
        layout: TexelCopyBufferLayout {offset: 0, bytes_per_row: Some(256), rows_per_image: Some(8)},
    }, color.size());
    queue.submit([encoder.finish()]);
    let (tx, rx) = mpsc::channel();
    readback.slice(..).map_async(MapMode::Read, move |status| { let _ = tx.send(status); });
    let _ = device.poll(Maintain::Wait);
    rx.recv_timeout(Duration::from_secs(20)).expect("bump readback callback").expect("bump readback");
    let mapped = readback.slice(..).get_mapped_range();
    let center = 4 * 256 + 4 * 4;
    let corner = 1 * 256 + 1 * 4;
    let pixels = [mapped[center..center + 4].try_into().unwrap(),
                  mapped[corner..corner + 4].try_into().unwrap()];
    drop(mapped);
    readback.unmap();
    pixels
}

#[test]
#[ignore = "requires a selected physical GPU; run with --ignored"]
fn bump_lighting_front_reverse_and_point() {
    let instance = Instance::new(&InstanceDescriptor {
        backends: Backends::from_env().unwrap_or(Backends::all()), ..Default::default()
    });
    let adapter = pollster::block_on(instance.request_adapter(&RequestAdapterOptions {
        power_preference: PowerPreference::HighPerformance,
        compatible_surface: None, force_fallback_adapter: false,
    })).expect("physical bump adapter");
    eprintln!("bump_shader_adapter {:?}", adapter.get_info());
    let (device, queue) = pollster::block_on(adapter.request_device(
        &DeviceDescriptor::default(), None)).expect("bump device");
    let shader = device.create_shader_module(ShaderModuleDescriptor {
        label: Some("coin_bump.wgsl"),
        source: ShaderSource::Wgsl(include_str!("../../../src/rendering/coinwgpu/shaders/coin_bump.wgsl").into()),
    });
    let make_pipeline = |fragment_entry| device.create_render_pipeline(&RenderPipelineDescriptor {
        label: Some("bump lighting pipeline"), layout: None,
        vertex: VertexState {
            module: &shader, entry_point: Some("vs_main"), compilation_options: Default::default(),
            buffers: &[VertexBufferLayout {
                array_stride: std::mem::size_of::<Vertex>() as u64,
                step_mode: VertexStepMode::Vertex,
                attributes: &vertex_attr_array![0 => Float32x3, 1 => Float32x3,
                    2 => Float32x4, 3 => Float32x3, 4 => Float32x3],
            }],
        },
        fragment: Some(FragmentState {
            module: &shader, entry_point: Some(fragment_entry), compilation_options: Default::default(),
            targets: &[Some(ColorTargetState {
                format: TextureFormat::Rgba8Unorm, blend: None, write_mask: ColorWrites::ALL,
            })],
        }),
        primitive: PrimitiveState {cull_mode: None, ..Default::default()},
        depth_stencil: None, multisample: Default::default(), multiview: None, cache: None,
    });
    let pipeline = make_pipeline("fs_main");
    let specular_pipeline = make_pipeline("fs_specular");
    let vertex = Vertex {
        position: [-1.0,-1.0,0.0], normal: [0.0,0.0,1.0], coord: [0.5,0.5,0.0,1.0],
        tangent_s: [1.0,0.0,0.0], tangent_t: [0.0,1.0,0.0],
    };
    let vertices = device.create_buffer(&BufferDescriptor {
        label: Some("bump triangle"), size: (3 * std::mem::size_of::<Vertex>()) as u64,
        usage: BufferUsages::VERTEX | BufferUsages::COPY_DST, mapped_at_creation: false,
    });
    let mut triangle = [vertex; 3];
    triangle[1].position = [3.0,-1.0,0.0];
    triangle[2].position = [-1.0,3.0,0.0];
    queue.write_buffer(&vertices, 0, bytemuck::cast_slice(&triangle));
    let normal = device.create_texture(&TextureDescriptor {
        label: Some("one-pixel normal map"), size: Extent3d {width: 1,height: 1,depth_or_array_layers: 1},
        mip_level_count: 1, sample_count: 1, dimension: TextureDimension::D2,
        format: TextureFormat::Rgba8Unorm,
        usage: TextureUsages::TEXTURE_BINDING | TextureUsages::COPY_DST, view_formats: &[],
    });
    queue.write_texture(normal.as_image_copy(), &[128,128,255,255],
        TexelCopyBufferLayout {offset: 0, bytes_per_row: Some(4), rows_per_image: Some(1)},
        normal.size());
    let normal_view = normal.create_view(&TextureViewDescriptor::default());
    let sampler = device.create_sampler(&SamplerDescriptor::default());
    let front = image(&device, &queue, &pipeline, &vertices, &normal_view, &sampler,
                      [0.0,0.0,1.0,0.0]);
    let reverse = image(&device, &queue, &pipeline, &vertices, &normal_view, &sampler,
                        [0.0,0.0,-1.0,0.0]);
    let point = image(&device, &queue, &pipeline, &vertices, &normal_view, &sampler,
                      [0.0,0.0,2.0,1.0]);
    let specular_front = image(&device, &queue, &specular_pipeline, &vertices,
                               &normal_view, &sampler, [0.0,0.0,1.0,0.0]);
    let specular_reverse = image(&device, &queue, &specular_pipeline, &vertices,
                                 &normal_view, &sampler, [0.0,0.0,-1.0,0.0]);
    let specular_angled = image(&device, &queue, &specular_pipeline, &vertices,
                                &normal_view, &sampler, [1.0,0.0,1.0,0.0]);
    eprintln!("bump_shader_pixels front={:?} reverse={:?} point_center={:?} point_corner={:?}",
              front[0], reverse[0], point[0], point[1]);
    for (actual, expected) in front[0].iter().zip([204u8,51,26,255]) {
        assert!(actual.abs_diff(expected) <= 1, "front pixel {:?}", front[0]);
    }
    assert_eq!(reverse[0], [0,0,0,255]);
    assert!(point[0][0] > point[1][0] && point[1][0] > reverse[0][0],
            "point-light diffuse must vary with vertex position: {point:?}");
    eprintln!("bump_shader_specular front={:?} reverse={:?} angled={:?}",
              specular_front[0], specular_reverse[0], specular_angled[0]);
    assert!(specular_front[0][0] > 80 && specular_front[0][1] > 40 &&
            specular_front[0][2] > 20 && specular_reverse[0][0] < 5,
            "legacy half-vector specular must respond to light direction");
    assert!(specular_angled[0][0] > 5 && specular_angled[0][0] < 40,
            "angled light must use the legacy shininess*64 exponent");
}
