// Independent manual diagnostic: registry wgpu 24; no Coin, bridge or EGL.
use std::{collections::{BTreeMap, VecDeque}, ffi::{CStr, CString}, os::raw::{c_int,c_ulong,c_void},
    ptr::NonNull, sync::{Arc, OnceLock, atomic::{AtomicBool,AtomicU64,AtomicUsize,Ordering}},
    time::{Duration,Instant}, process::ExitCode};
use raw_window_handle::{RawDisplayHandle,RawWindowHandle,XlibDisplayHandle,XlibWindowHandle};

static LOG_ERRORS:AtomicUsize=AtomicUsize::new(0);
static WGPU_ERRORS:AtomicUsize=AtomicUsize::new(0);
static TEARDOWN_ERRORS:AtomicUsize=AtomicUsize::new(0);
static LAYER_ACTIVE:AtomicBool=AtomicBool::new(false);
static SYNC_ACTIVE:AtomicBool=AtomicBool::new(false);
static CLOCK:OnceLock<Instant>=OnceLock::new();
const SYNC:&str="VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT";
fn stamp()->u128{CLOCK.get_or_init(Instant::now).elapsed().as_micros()}
struct Logger;
static LOGGER:Logger=Logger;
impl log::Log for Logger {
    fn enabled(&self,m:&log::Metadata<'_>)->bool{m.level()<=log::Level::Info}
    fn log(&self,r:&log::Record<'_>){
        if !self.enabled(r.metadata()){return;}
        if r.level()==log::Level::Error{LOG_ERRORS.fetch_add(1,Ordering::SeqCst);}
        let message=format!("{}",r.args());
        if message.contains("Khronos Validation Layer Active"){
            LAYER_ACTIVE.store(true,Ordering::SeqCst);
            // Khronos 1.3.275 reports the canonical enable without _EXT.
            // Accept precisely either report token; the requested env still
            // requires SYNC with _EXT and all other evidence remains mandatory.
            if message.split(|c:char|!(c.is_ascii_alphanumeric()||c=='_')).any(|token|
                token==SYNC||token=="VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION"){
                SYNC_ACTIVE.store(true,Ordering::SeqCst);
            }
        }
        if r.level()<=log::Level::Warn || r.target().contains("vulkan") {
            eprintln!("b95_wgpu log us={} level={} target={} message={}",stamp(),r.level(),r.target(),message);
        }
    }
    fn flush(&self){}
}
#[repr(C)]
#[derive(Default,Debug,Clone,Copy)]
struct Attrs {
    valid:c_int,x:c_int,y:c_int,width:c_int,height:c_int,depth:c_int,map_state:c_int,
    override_redirect:c_int,bits_per_pixel:c_int,bytes_per_line:c_int,
    window:c_ulong,red_mask:c_ulong,green_mask:c_ulong,blue_mask:c_ulong,
}
extern "C" {
    fn b95_x11_create(side:u32)->*mut c_void;
    fn b95_x11_display(owner:*mut c_void)->*mut c_void;
    fn b95_x11_window(owner:*mut c_void)->c_ulong;
    fn b95_x11_screen(owner:*mut c_void)->c_int;
    fn b95_x11_attributes(owner:*mut c_void,out:*mut Attrs)->c_int;
    fn b95_x11_resize(owner:*mut c_void,side:u32)->c_int;
    fn b95_x11_capture(owner:*mut c_void,side:u32,rgba:*mut u8,bytes:usize,attrs:*mut Attrs)->c_int;
    fn b95_x11_close(owner:*mut c_void)->c_int;
    fn b95_x11_errors()->u32;
}
#[derive(Debug)]
struct Failure {code:u8,detail:String}
type Result<T>=std::result::Result<T,Failure>;
fn fail(code:u8,detail:impl Into<String>)->Failure{Failure{code,detail:detail.into()}}
struct Window(NonNull<c_void>);
impl Window {
    fn new()->Result<Self>{let ptr=NonNull::new(unsafe{b95_x11_create(96)}).ok_or_else(||fail(1,"X11 create/map failed"))?;
        let window=Self(ptr);window.attributes(96)?;Ok(window)}
    fn attributes(&self,side:u32)->Result<Attrs>{let mut attrs=Attrs::default();
        if unsafe{b95_x11_attributes(self.0.as_ptr(),&mut attrs)}!=1||attrs.map_state!=2||attrs.width!=side as i32||attrs.height!=side as i32 {
            return Err(fail(1,format!("X11 not viewable at authored dimensions: {attrs:?}")));}
        Ok(attrs)}
    fn resize(&self,side:u32)->Result<()>{if unsafe{b95_x11_resize(self.0.as_ptr(),side)}!=1{return Err(fail(1,"X11 resize failed"));}
        println!("b95_wgpu resize us={} attrs={:?}",stamp(),self.attributes(side)?);Ok(())}
    fn capture(&self,side:u32)->Result<(Vec<u8>,Attrs)>{let mut rgba=vec![0;side as usize*side as usize*4];let mut attrs=Attrs::default();
        if unsafe{b95_x11_capture(self.0.as_ptr(),side,rgba.as_mut_ptr(),rgba.len(),&mut attrs)}!=1 {
            return Err(fail(1,format!("physical capture unavailable: {attrs:?}")));}
        Ok((rgba,attrs))}
}
impl Drop for Window {fn drop(&mut self){if unsafe{b95_x11_close(self.0.as_ptr())}!=1{TEARDOWN_ERRORS.fetch_add(1,Ordering::SeqCst);}}}
struct Drain(wgpu::Device);
impl Drop for Drain {fn drop(&mut self){
    let outcome=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||self.0.poll(wgpu::Maintain::Wait).is_queue_empty()));
    match outcome {Ok(empty)=>println!("b95_wgpu teardown_poll us={} completed=1 empty_on_entry={empty}",stamp()),
        Err(_)=>{TEARDOWN_ERRORS.fetch_add(1,Ordering::SeqCst);eprintln!("b95_wgpu teardown_poll panicked");}}
}}
fn validation_environment()->Result<()> {
    let layers=std::env::var("VK_INSTANCE_LAYERS").unwrap_or_default();
    let enables=std::env::var("VK_LAYER_ENABLES").unwrap_or_default();
    println!("b95_wgpu validation_environment VK_INSTANCE_LAYERS={layers:?} VK_LAYER_ENABLES={enables:?}");
    if !layers.split(|c:char|c==':'||c==','||c==';'||c.is_whitespace()).any(|v|v=="VK_LAYER_KHRONOS_validation")||!enables.contains(SYNC){
        return Err(fail(4,"require VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation and VK_LAYER_ENABLES containing synchronization validation"));}
    Ok(())
}
fn validation_instance(instance:&wgpu::Instance)->Result<()> {
    // Read-only registry HAL metadata; no manually created/destroyed Vulkan
    // objects, no nested callbacks or vendor implementation paths.
    let hal=unsafe{instance.as_hal::<wgpu::hal::api::Vulkan>()}.ok_or_else(||fail(4,"Vulkan HAL unavailable"))?;
    let shared=hal.shared_instance();let layer=CString::new("VK_LAYER_KHRONOS_validation").unwrap();
    let layers=unsafe{shared.entry().enumerate_instance_layer_properties()}.map_err(|e|fail(4,format!("query validation layers: {e:?}")))?;
    let present=layers.iter().any(|p|unsafe{CStr::from_ptr(p.layer_name.as_ptr())}==layer.as_c_str());
    let extensions=unsafe{shared.entry().enumerate_instance_extension_properties(Some(layer.as_c_str()))}
        .map_err(|e|fail(4,format!("query validation features: {e:?}")))?;
    let features=extensions.iter().any(|p|unsafe{CStr::from_ptr(p.extension_name.as_ptr())}.to_bytes()==b"VK_EXT_validation_features");
    let debug=shared.extensions().iter().any(|v|v.to_bytes()==b"VK_EXT_debug_utils");
    let maps=std::fs::read_to_string("/proc/self/maps").map_err(|e|fail(4,format!("validation library maps: {e}")))?;
    let loaded=maps.lines().any(|l|l.contains("VkLayer_khronos_validation"));
    println!("b95_wgpu validation_evidence layer_available={present} validation_features={features} debug_callback={debug} library_loaded={loaded} active_callback={} sync_callback={}",
        LAYER_ACTIVE.load(Ordering::SeqCst),SYNC_ACTIVE.load(Ordering::SeqCst));
    if !(present&&features&&debug&&loaded&&LAYER_ACTIVE.load(Ordering::SeqCst)&&SYNC_ACTIVE.load(Ordering::SeqCst)){
        return Err(fail(4,"real Khronos + synchronization validation evidence missing; no skipped PASS"));}
    Ok(())
}
const SHADER:&str=r#"
@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4<f32> {
    var p=array<vec2<f32>,3>(vec2<f32>(-1.0,-1.0),vec2<f32>(3.0,-1.0),vec2<f32>(-1.0,3.0));
    return vec4<f32>(p[i],0.0,1.0);
}
@fragment fn red()->@location(0) vec4<f32>{return vec4<f32>(204.0/255.0,20.0/255.0,10.0/255.0,1.0);}
@fragment fn yellow()->@location(0) vec4<f32>{return vec4<f32>(204.0/255.0,204.0/255.0,0.0,1.0);}
"#;
fn pipeline(device:&wgpu::Device,shader:&wgpu::ShaderModule,format:wgpu::TextureFormat,entry:&str)->wgpu::RenderPipeline {
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label:Some(entry),layout:None,
        vertex:wgpu::VertexState{module:shader,entry_point:Some("vs"),compilation_options:Default::default(),buffers:&[]},
        fragment:Some(wgpu::FragmentState{module:shader,entry_point:Some(entry),compilation_options:Default::default(),
            targets:&[Some(wgpu::ColorTargetState{format,blend:None,write_mask:wgpu::ColorWrites::ALL})]}),
        primitive:Default::default(),depth_stencil:None,multisample:wgpu::MultisampleState{count:1,..Default::default()},multiview:None,cache:None,
    })
}
fn background(scene:u32)->[u8;3]{if scene==2{[0,0,0]}else{[10,20,204]}}
fn pixel(scene:u32,side:u32,x:u32,y:u32)->[u8;3]{let inset=(side+5)/10;
    if scene!=1&&x>=inset&&x<side-inset&&y>=inset&&y<side-inset {if scene==0{[204,20,10]}else{[204,204,0]}}else{background(scene)}}
fn analytic(scene:u32,side:u32)->Vec<u8>{let mut result=Vec::with_capacity(side as usize*side as usize*4);
    for y in 0..side{for x in 0..side{result.extend_from_slice(&pixel(scene,side,x,y));result.push(255);}}result}
fn differences(expected:&[u8],observed:&[u8])->usize {
    if expected.len()!=observed.len(){return expected.len()/4;}
    expected.chunks_exact(4).zip(observed.chunks_exact(4)).filter(|(a,b)|a[..3]!=b[..3]).count()
}
fn fingerprint(pixels:&[u8])->u64{let mut hash=14695981039346656037u64;
    for p in pixels.chunks_exact(4){for c in &p[..3]{hash^=u64::from(*c);hash=hash.wrapping_mul(1099511628211);}}hash}
fn histogram(pixels:&[u8])->BTreeMap<[u8;3],usize>{let mut colors=BTreeMap::new();for p in pixels.chunks_exact(4){*colors.entry([p[0],p[1],p[2]]).or_insert(0)+=1;}colors}
#[derive(Debug)]
struct History {frame:u32,serial:u64,scene:u32,side:u32,fingerprint:u64}
fn physical(window:&Window,side:u32,frame:u32,serial:u64,scene:u32,expected:&[u8],history:&VecDeque<History>)->Result<()> {
    let start=Instant::now();let mut attempts=0u32;let mut first=Vec::new();let mut last;
    println!("b95_wgpu observe_begin us={} frame={frame} serial={serial} scene={scene} extent={side}x{side}",stamp());
    loop {
        let (pixels,attrs)=window.capture(side)?;attempts+=1;
        if attempts==1{first=pixels.clone();}
        let delta=differences(expected,&pixels);last=pixels;
        if delta==0{println!("b95_wgpu observe_end us={} frame={frame} serial={serial} physical=1 attempts={attempts} ms={:.3} attrs={attrs:?}",stamp(),start.elapsed().as_secs_f64()*1000.0);return Ok(());}
        if start.elapsed()>=Duration::from_secs(5){
            let observed=fingerprint(&last);
            eprintln!("b95_wgpu observe_end us={} frame={frame} serial={serial} physical=0 attempts={attempts} ms={:.3} changed_pixels={delta} attrs={attrs:?} expected_fingerprint={} first_fingerprint={} last_fingerprint={observed}",
                stamp(),start.elapsed().as_secs_f64()*1000.0,fingerprint(expected),fingerprint(&first));
            eprintln!("b95_wgpu colors expected={:?} first={:?} last={:?}",histogram(expected),histogram(&first),histogram(&last));
            let mut reports=0;for (i,(a,b)) in expected.chunks_exact(4).zip(last.chunks_exact(4)).enumerate(){if a[..3]!=b[..3]&&reports<8 {
                reports+=1;eprintln!("b95_wgpu physical_difference xy={},{} expected={:?} observed={:?}",i%side as usize,i/side as usize,&a[..3],&b[..3]);}}
            for old in history{eprintln!("b95_wgpu history frame={} serial={} scene={} extent={} fingerprint={} exact_observed_match={}",old.frame,old.serial,old.scene,old.side,old.fingerprint,old.side==side&&old.fingerprint==observed);}
            return Err(fail(3,"GPU analytically correct, current physical RGB not observed in five seconds"));
        }
        // XGetImage observations only: no device poll, configure, acquire,
        // queue operation or second render during this observation window.
        std::thread::sleep(Duration::from_millis(2));
    }
}
fn capture_buffer(device:&wgpu::Device,side:u32)->wgpu::Buffer {
    let row=(side*4+255)/256*256;
    device.create_buffer(&wgpu::BufferDescriptor{label:Some("surface MAP_READ"),size:u64::from(row)*u64::from(side),usage:wgpu::BufferUsages::MAP_READ|wgpu::BufferUsages::COPY_DST,mapped_at_creation:false})
}
fn run(frames:u32,repeat:u32)->Result<()> {
    validation_environment()?;
    // Created first, destroyed last: the unsafe raw handles remain valid until
    // every surface/instance/device owner has completed normal teardown.
    let window=Window::new()?;
    let flags=wgpu::InstanceFlags::VALIDATION|wgpu::InstanceFlags::DEBUG;
    println!("b95_wgpu instance_flags={flags:?} flags_bits={:#x} backend=Vulkan upstream=24.0.5 actual_swapchain_images=unobserved",flags.bits());
    let instance=wgpu::Instance::new(&wgpu::InstanceDescriptor{backends:wgpu::Backends::VULKAN,flags,backend_options:Default::default()});
    validation_instance(&instance)?;
    let display=NonNull::new(unsafe{b95_x11_display(window.0.as_ptr())}).ok_or_else(||fail(1,"null native Display"))?;
    let xid=unsafe{b95_x11_window(window.0.as_ptr())};let screen=unsafe{b95_x11_screen(window.0.as_ptr())};
    let surface=unsafe{instance.create_surface_unsafe(wgpu::SurfaceTargetUnsafe::RawHandle {
        raw_display_handle:RawDisplayHandle::Xlib(XlibDisplayHandle::new(Some(display),screen)),
        raw_window_handle:RawWindowHandle::Xlib(XlibWindowHandle::new(xid)),
    })}.map_err(|e|fail(1,format!("create surface: {e}")))?;
    let adapter=instance.enumerate_adapters(wgpu::Backends::VULKAN).into_iter().find(|adapter|{
        let info=adapter.get_info();info.vendor==0x10de&&info.device!=0&&info.backend==wgpu::Backend::Vulkan&&info.device_type!=wgpu::DeviceType::Cpu&&adapter.is_surface_supported(&surface)
    }).ok_or_else(||fail(1,"real NVIDIA Vulkan surface adapter unavailable; no fallback"))?;
    let info=adapter.get_info();let name=info.name.to_lowercase();
    if name.is_empty()||["llvmpipe","softpipe","swiftshader","software"].iter().any(|word|name.contains(*word)){return Err(fail(1,"nonphysical adapter identity"));}
    println!("b95_wgpu adapter={info:?} Display={display:?} XID={xid} screen={screen}");
    let caps=surface.get_capabilities(&adapter);let usage=wgpu::TextureUsages::RENDER_ATTACHMENT|wgpu::TextureUsages::COPY_SRC;
    let format=[wgpu::TextureFormat::Bgra8Unorm,wgpu::TextureFormat::Rgba8Unorm].into_iter().find(|f|caps.formats.contains(f)).ok_or_else(||fail(1,"linear RGBA8/BGRA8 surface unavailable"))?;
    if !caps.usages.contains(usage)||!caps.present_modes.contains(&wgpu::PresentMode::Fifo)||!caps.alpha_modes.contains(&wgpu::CompositeAlphaMode::Opaque){return Err(fail(1,format!("required exact surface capabilities unavailable: {caps:?}")));}
    let (device,queue)=pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor{label:Some("B95 stock device"),required_features:wgpu::Features::empty(),required_limits:wgpu::Limits::default(),memory_hints:Default::default()},None))
        .map_err(|e|fail(1,format!("request device: {e}")))?;
    device.on_uncaptured_error(Box::new(|error|{WGPU_ERRORS.fetch_add(1,Ordering::SeqCst);eprintln!("b95_wgpu uncaptured us={} error={error}",stamp());}));
    let _drain=Drain(device.clone());
    let mut config=wgpu::SurfaceConfiguration{usage,format,width:96,height:96,present_mode:wgpu::PresentMode::Fifo,desired_maximum_frame_latency:2,alpha_mode:wgpu::CompositeAlphaMode::Opaque,view_formats:vec![]};
    surface.configure(&device,&config);
    let shader=device.create_shader_module(wgpu::ShaderModuleDescriptor{label:Some("B95 analytic rectangles"),source:wgpu::ShaderSource::Wgsl(SHADER.into())});
    let red=pipeline(&device,&shader,format,"red");let yellow=pipeline(&device,&shader,format,"yellow");
    let mut side=96;let mut readback=capture_buffer(&device,side);let completed=Arc::new(AtomicU64::new(0));let mut history=VecDeque::new();
    println!("b95_wgpu configure format={format:?} usage={usage:?} mode=FIFO desired_latency=2 actual_image_count=unobserved surface_samples=1 attrs={:?}",window.attributes(side)?);
    for frame in 0..frames {
        if frame==8{side=128;window.resize(side)?;config.width=side;config.height=side;surface.configure(&device,&config);readback=capture_buffer(&device,side);}
        let serial=u64::from(frame)+1;let scene=(frame/repeat)%3;let expected=analytic(scene,side);let inset=(side+5)/10;
        let acquired=surface.get_current_texture().map_err(|e|fail(1,format!("single acquire frame={frame}: {e:?}")))?;
        if acquired.texture.sample_count()!=1||acquired.texture.size()!=(wgpu::Extent3d{width:side,height:side,depth_or_array_layers:1}){return Err(fail(2,"actual surface sample count or extent differs"));}
        println!("b95_wgpu acquired us={} frame={frame} serial={serial} scene={scene} extent={side}x{side} suboptimal={} texture={:?}",stamp(),acquired.suboptimal,acquired.texture);
        let view=acquired.texture.create_view(&Default::default());let bg=background(scene);
        let mut scene_encoder=device.create_command_encoder(&wgpu::CommandEncoderDescriptor{label:Some("scene command buffer")});
        {let attachments=[Some(wgpu::RenderPassColorAttachment{view:&view,resolve_target:None,ops:wgpu::Operations{load:wgpu::LoadOp::Clear(wgpu::Color{r:f64::from(bg[0])/255.0,g:f64::from(bg[1])/255.0,b:f64::from(bg[2])/255.0,a:1.0}),store:wgpu::StoreOp::Store}})];
            let mut pass=scene_encoder.begin_render_pass(&wgpu::RenderPassDescriptor{label:Some("direct surface1x"),color_attachments:&attachments,depth_stencil_attachment:None,timestamp_writes:None,occlusion_query_set:None});
            if scene!=1{pass.set_pipeline(if scene==0{&red}else{&yellow});pass.set_scissor_rect(inset,inset,side-2*inset,side-2*inset);pass.draw(0..3,0..1);}}
        let row=(side*4+255)/256*256;let mut capture=device.create_command_encoder(&wgpu::CommandEncoderDescriptor{label:Some("capture command buffer")});
        capture.copy_texture_to_buffer(wgpu::TexelCopyTextureInfo{texture:&acquired.texture,mip_level:0,origin:wgpu::Origin3d::ZERO,aspect:wgpu::TextureAspect::All},
            wgpu::TexelCopyBufferInfo{buffer:&readback,layout:wgpu::TexelCopyBufferLayout{offset:0,bytes_per_row:Some(row),rows_per_image:Some(side)}},wgpu::Extent3d{width:side,height:side,depth_or_array_layers:1});
        let submission=queue.submit([scene_encoder.finish(),capture.finish()]);let completion=completed.clone();queue.on_submitted_work_done(move||{completion.store(serial,Ordering::SeqCst);});
        println!("b95_wgpu submit us={} frame={frame} serial={serial} command_buffers=2 submission={submission:?}",stamp());
        let (tx,rx)=std::sync::mpsc::channel();readback.slice(..).map_async(wgpu::MapMode::Read,move|result|{let _=tx.send(result);});
        let empty=device.poll(wgpu::Maintain::Wait).is_queue_empty();
        rx.try_recv().map_err(|e|fail(2,format!("map callback absent after poll Wait: {e}")))?.map_err(|e|fail(1,format!("MAP_READ: {e}")))?;
        let mut current=vec![0;expected.len()];
        {let mapped=readback.slice(..).get_mapped_range();for y in 0..side{for x in 0..side{let from=(y*row+x*4) as usize;let to=((y*side+x)*4) as usize;
            current[to..to+4].copy_from_slice(&mapped[from..from+4]);if format==wgpu::TextureFormat::Bgra8Unorm{current.swap(to,to+2);}}}}
        readback.unmap();let delta=differences(&expected,&current);let done=completed.load(Ordering::SeqCst);
        println!("b95_wgpu readback us={} frame={frame} serial={serial} completed={done} poll_empty_on_entry={empty} pixels={} changed_RGB_pixels={delta} analytic_hist={:?} actual_hist={:?}",stamp(),side*side,histogram(&expected),histogram(&current));
        if done!=serial||delta!=0{return Err(fail(2,"GPU completion/readback does not match independent all-pixel analytic oracle"));}
        if LOG_ERRORS.load(Ordering::SeqCst)>0||WGPU_ERRORS.load(Ordering::SeqCst)>0{return Err(fail(4,"validation/error callback before present"));}
        println!("b95_wgpu present_begin us={} frame={frame} serial={serial}",stamp());acquired.present();
        println!("b95_wgpu present_end us={} frame={frame} serial={serial}",stamp());
        physical(&window,side,frame,serial,scene,&expected,&history)?;
        // Logging-only history is computed after successful physical observation.
        if history.len()==8{history.pop_front();}history.push_back(History{frame,serial,scene,side,fingerprint:fingerprint(&expected)});
    }
    println!("b95_wgpu requested_frames_complete frames={frames} completed={}",completed.load(Ordering::SeqCst));
    Ok(()) // All locals, including Drain, surface, instance and X11, drop here.
}
fn options()->Result<(u32,u32)>{let mut frames=256;let mut repeat=2;let mut args=std::env::args().skip(1);
    while let Some(arg)=args.next(){let target=match arg.as_str(){"--frames"=>&mut frames,"--repeat"=>&mut repeat,_=>return Err(fail(64,"usage: B95WgpuStalePresent [--frames 9..100000] [--repeat 1..256]"))};
        *target=args.next().ok_or_else(||fail(64,"missing numeric argument"))?.parse().map_err(|_|fail(64,"invalid number"))?;}
    if !(9..=100000).contains(&frames)||!(1..=256).contains(&repeat){return Err(fail(64,"invalid finite frame/repeat count"));}Ok((frames,repeat))}
fn main()->ExitCode {
    if log::set_logger(&LOGGER).is_err(){eprintln!("b95_wgpu logger unavailable");return ExitCode::from(4);}
    log::set_max_level(log::LevelFilter::Info);
    let (frames,repeat)=match options(){Ok(v)=>v,Err(e)=>{eprintln!("{}",e.detail);return ExitCode::from(e.code);}};
    let outcome=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||run(frames,repeat)));
    let mut code=match outcome{Ok(Ok(()))=>0,Ok(Err(e))=>{eprintln!("b95_wgpu failure code={} detail={}",e.code,e.detail);e.code},Err(_)=>{eprintln!("b95_wgpu panic/invalid run");4}};
    // This runs only after every resource from run(), including on a physical
    // timeout, has undergone normal RAII teardown. Invalid runs never become B95.
    let logs=LOG_ERRORS.load(Ordering::SeqCst);let uncaptured=WGPU_ERRORS.load(Ordering::SeqCst);
    let teardown=TEARDOWN_ERRORS.load(Ordering::SeqCst);let xerrors=unsafe{b95_x11_errors()};
    if logs>0||uncaptured>0||teardown>0||xerrors>0{code=4;}
    println!("b95_wgpu result={code} frames={frames} repeat={repeat} logger_ERROR={logs} uncaptured={uncaptured} teardown_errors={teardown} X11_errors={xerrors} validation_active={} sync_active={} all_run_resources_dropped=1",
        LAYER_ACTIVE.load(Ordering::SeqCst),SYNC_ACTIVE.load(Ordering::SeqCst));
    ExitCode::from(code)
}
