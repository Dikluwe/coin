//! Direct wgpu lifecycle reproducer: no CoinRender runtime or FFI is involved.
//! Opt in with --ignored; each case requires a working physical GPU profile.
use std::sync::mpsc::{self, Receiver, Sender};
use std::time::Duration;
use wgpu::*;

const WORKERS: usize = 2;
const ROUNDS: usize = 8;

fn rendezvous(tx: &Sender<(usize, usize, usize)>, rx: &Receiver<()>,
              worker: usize, round: usize, phase: usize) {
    tx.send((worker, round, phase)).expect("lifecycle coordinator");
    rx.recv_timeout(Duration::from_secs(20)).expect("lifecycle phase timed out");
}

fn exercise(explicit_unmap: bool, lockstep: bool, render: bool, shared_instance: bool) {
    let common_instance = shared_instance.then(|| Instance::new(&InstanceDescriptor {
        backends: Backends::from_env().unwrap_or(Backends::all()),
        ..Default::default()
    }));
    let (events_tx, events_rx) = mpsc::channel();
    let mut gates = Vec::new();
    let mut workers = Vec::new();
    for worker in 0..WORKERS {
        let (gate_tx, gate_rx) = mpsc::channel();
        gates.push(gate_tx);
        let events = events_tx.clone();
        let common_instance = common_instance.clone();
        workers.push(std::thread::spawn(move || {
            for round in 0..ROUNDS {
                if lockstep || round == 0 {rendezvous(&events, &gate_rx, worker, round, 0);}
                {
                    let instance = common_instance.clone().unwrap_or_else(|| Instance::new(&InstanceDescriptor {
                        backends: Backends::from_env().unwrap_or(Backends::all()),
                        ..Default::default()
                    }));
                    let adapter = pollster::block_on(instance.request_adapter(&RequestAdapterOptions {
                        power_preference: if worker == 0 {PowerPreference::LowPower} else {PowerPreference::HighPerformance},
                        compatible_surface: None, force_fallback_adapter: false,
                    })).expect("GPU required for the explicit lifecycle probe");
                    eprintln!("lifecycle_adapter worker={worker} round={round} info={:?}",adapter.get_info());
                    let (device, queue) = pollster::block_on(adapter.request_device(
                        &DeviceDescriptor::default(),None)).expect("lifecycle device");
                    let expected = (0..8u32).flat_map(|word|
                        (0x13000000u32 | ((worker as u32)<<20) | ((round as u32)<<8) | word).to_le_bytes()).collect::<Vec<_>>();
                    let source = device.create_buffer(&BufferDescriptor {
                        label: Some("lifecycle source"),size:32,
                        usage:BufferUsages::COPY_DST|BufferUsages::COPY_SRC,mapped_at_creation:false,
                    });
                    let readback = device.create_buffer(&BufferDescriptor {
                        label: Some("lifecycle readback"),size:32,
                        usage:BufferUsages::COPY_DST|BufferUsages::MAP_READ,mapped_at_creation:false,
                    });
                    queue.write_buffer(&source,0,&expected);
                    let mut encoder = device.create_command_encoder(&CommandEncoderDescriptor::default());
                    encoder.copy_buffer_to_buffer(&source,0,&readback,0,32);
                    queue.submit([encoder.finish()]);
                    let (mapped_tx,mapped_rx) = mpsc::channel();
                    readback.slice(..).map_async(MapMode::Read,move |status| { let _=mapped_tx.send(status); });
                    let _=device.poll(Maintain::Wait);
                    mapped_rx.recv_timeout(Duration::from_secs(20)).expect("map callback").expect("map result");
                    let mapped=readback.slice(..).get_mapped_range();
                    assert_eq!(&mapped[..],&expected[..],"worker {worker} round {round} bytes");
                    drop(mapped);
                    if explicit_unmap {readback.unmap();}
                    // Both devices finish their work before simultaneous destruction.
                    // The mapped-drop variant leaves the buffer mapped, as the old tests do.
                    if render {render_probe(&device,&queue,worker,round);}
                    if lockstep {rendezvous(&events,&gate_rx,worker,round,1);}
                }
                if lockstep {rendezvous(&events,&gate_rx,worker,round,2);}
            }
        }));
    }
    drop(events_tx);
    for round in 0..(if lockstep {ROUNDS} else {1}) {
        for phase in 0..(if lockstep {3} else {1}) {
            let mut arrived=[false;WORKERS];
            for _ in 0..WORKERS {
                let (worker,received_round,received_phase)=events_rx.recv_timeout(Duration::from_secs(20)).expect("worker phase timed out");
                assert_eq!((received_round,received_phase),(round,phase));
                assert!(!arrived[worker],"duplicate worker phase");arrived[worker]=true;
            }
            for gate in &gates {gate.send(()).expect("worker gate");}
        }
    }
    for worker in workers {worker.join().expect("lifecycle worker panicked");}
    eprintln!("lifecycle_complete explicit_unmap={explicit_unmap} workers={WORKERS} rounds={ROUNDS} devices={} lockstep={lockstep} render={render}",WORKERS*ROUNDS);
}

#[test]
#[ignore = "explicit GPU concurrency/lifecycle stress; run with --ignored"]
fn concurrent_explicit_unmap_and_drop() {exercise(true,true,false,false);}

#[test]
#[ignore = "explicit GPU concurrency/lifecycle stress; run with --ignored"]
fn concurrent_mapped_buffer_drop() {exercise(false,true,false,false);}

fn render_probe(device:&Device,queue:&Queue,worker:usize,round:usize) {
    let shader=device.create_shader_module(ShaderModuleDescriptor {
        label:Some("lifecycle shader"),source:ShaderSource::Wgsl(r#"
        @vertex fn vs_main(@builtin(vertex_index) i:u32)->@builtin(position) vec4<f32> {
            let p=array<vec2<f32>,3>(vec2<f32>(-1.0,-1.0),vec2<f32>(3.0,-1.0),vec2<f32>(-1.0,3.0));
            return vec4<f32>(p[i],0.5,1.0);
        }
        @fragment fn fs_main()->@location(0) vec4<f32> {return vec4<f32>(1.0,0.0,0.0,1.0);}
        "#.into()),
    });
    let pipeline=device.create_render_pipeline(&RenderPipelineDescriptor {
        label:Some("lifecycle render"),layout:None,
        vertex:VertexState {module:&shader,entry_point:Some("vs_main"),buffers:&[],compilation_options:Default::default()},
        fragment:Some(FragmentState {module:&shader,entry_point:Some("fs_main"),targets:&[Some(ColorTargetState {
            format:TextureFormat::Rgba8Unorm,blend:None,write_mask:ColorWrites::ALL})],compilation_options:Default::default()}),
        primitive:Default::default(),depth_stencil:None,multisample:Default::default(),multiview:None,cache:None,
    });
    let color=device.create_texture(&TextureDescriptor {
        label:Some("lifecycle image"),size:Extent3d {width:8,height:8,depth_or_array_layers:1},
        mip_level_count:1,sample_count:1,dimension:TextureDimension::D2,format:TextureFormat::Rgba8Unorm,
        usage:TextureUsages::RENDER_ATTACHMENT|TextureUsages::COPY_SRC,view_formats:&[],
    });
    let view=color.create_view(&TextureViewDescriptor::default());
    let readback=device.create_buffer(&BufferDescriptor {label:None,size:8*256,
        usage:BufferUsages::COPY_DST|BufferUsages::MAP_READ,mapped_at_creation:false});
    let mut encoder=device.create_command_encoder(&CommandEncoderDescriptor::default());
    {
        let mut pass=encoder.begin_render_pass(&RenderPassDescriptor {
            label:None,color_attachments:&[Some(RenderPassColorAttachment {view:&view,resolve_target:None,
                ops:Operations {load:LoadOp::Clear(Color {r:0.0,g:0.0,b:1.0,a:1.0}),store:StoreOp::Store}})],
            depth_stencil_attachment:None,timestamp_writes:None,occlusion_query_set:None,
        });
        pass.set_pipeline(&pipeline);pass.draw(0..3,0..1);
    }
    encoder.copy_texture_to_buffer(color.as_image_copy(),TexelCopyBufferInfo {buffer:&readback,
        layout:TexelCopyBufferLayout {offset:0,bytes_per_row:Some(256),rows_per_image:Some(8)}},color.size());
    queue.submit([encoder.finish()]);
    let (tx,rx)=mpsc::channel();readback.slice(..).map_async(MapMode::Read,move|status|{let _=tx.send(status);});
    let _=device.poll(Maintain::Wait);rx.recv_timeout(Duration::from_secs(20)).expect("image map callback").expect("image map");
    let mapped=readback.slice(..).get_mapped_range();let expected=[255u8,0,0,255].repeat(8);
    for y in 0..8 {assert_eq!(&mapped[y*256..y*256+32],&expected[..],"worker {worker} round {round} row {y}");}
    drop(mapped);readback.unmap();
    eprintln!("lifecycle_render_verified worker={worker} round={round} pixels=64");
}

#[test]
#[ignore = "explicit GPU create/drop overlap with shader execution; run with --ignored"]
fn concurrent_free_running_render_and_drop() {exercise(false,false,true,false);}

#[test]
#[ignore = "explicit GPU shared Instance with concurrent logical Device lifecycles"]
fn concurrent_shared_instance_render_and_drop() {exercise(false,false,true,true);}
