//! Render target texture ownership and cube assembly.

use super::*;

#[no_mangle]
pub extern "C" fn coin_wgpu_submit(
    target: *mut CoinWgpuTarget,
    frame: *const CoinWgpuFrameView,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    coin_wgpu_submit_internal(target, frame, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), error_buf, error_buf_len)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_submit_with_depth_history(
    target: *mut CoinWgpuTarget, frame: *const CoinWgpuFrameView, history: *mut u64,
    error_buf: *mut std::os::raw::c_char, error_buf_len: usize,
) -> CoinWgpuStatus {
    if history.is_null() || (history as usize) % std::mem::align_of::<u64>() != 0 {
        set_error(error_buf, error_buf_len, "Null or misaligned target depth history token");
        return CoinWgpuStatus::InvalidArgument;
    }
    coin_wgpu_submit_internal(target, frame, std::ptr::null_mut(), std::ptr::null_mut(),
        history, error_buf, error_buf_len)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_release_depth_history(history: u64) {
    if let Ok(mut guard) = RUNTIME_CTX.lock() {
        if let Some(runtime) = guard.as_mut() {
            if let Some(device) = runtime.device_state.as_mut() {
                device.target_depth_histories.remove(&history);
            }
            for device in runtime.extra_devices.values_mut() {
                device.target_depth_histories.remove(&history);
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn coin_wgpu_submit_texture(
    target: *mut CoinWgpuTarget,
    frame: *const CoinWgpuFrameView,
    out_token: *mut u64,
    error_buf: *mut std::os::raw::c_char,
    error_buf_len: usize,
) -> CoinWgpuStatus {
    if out_token.is_null() || (out_token as usize) % std::mem::align_of::<u64>() != 0 {
        set_error(error_buf, error_buf_len, "Null or misaligned RTT output token pointer");
        return CoinWgpuStatus::InvalidArgument;
    }
    unsafe { *out_token = 0; }
    coin_wgpu_submit_internal(target, frame, std::ptr::null_mut(), out_token, std::ptr::null_mut(), error_buf, error_buf_len)
}

pub(super) fn cube_rtt_layout(n:u32,mips:bool)->Result<(u32,u32),(CoinWgpuStatus,String)> {
    if n==0 || n>2048 || !n.is_power_of_two(){return Err((CoinWgpuStatus::InvalidArgument,"Invalid cube RTT face extent".into()));}
    let width=2*n;let mut extent=n;let mut texels=0u64;
    loop {texels+=6*u64::from(extent)*u64::from(extent);if !mips||extent==1{break;}extent/=2;}
    let height=texels.div_ceil(u64::from(width));
    if height>8192 || u64::from(width)*height*4>128*1024*1024 {
        return Err((CoinWgpuStatus::Unsupported,"Cube RTT atlas exceeds 128 MiB".into()));
    }
    Ok((width,height as u32))
}

pub(super) fn admit_cube_rtt(ctx:&DeviceState,n:u32,mips:bool,samples:u32)->Result<(u32,u32),(CoinWgpuStatus,String)> {
    let (width,height)=cube_rtt_layout(n,mips)?;
    if !matches!(samples,1|2|4) || width>ctx.device.limits().max_texture_dimension_2d || height>ctx.device.limits().max_texture_dimension_2d {
        return Err((CoinWgpuStatus::Unsupported,"Cube RTT samples or atlas extent unavailable".into()));
    }
    let color=ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba8Unorm);
    let depth=ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Depth32Float);
    let required=if samples==2 {wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X2}
        else if samples==4 {wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4} else {wgpu::TextureFormatFeatureFlags::empty()};
    if !color.allowed_usages.contains(wgpu::TextureUsages::RENDER_ATTACHMENT|wgpu::TextureUsages::TEXTURE_BINDING|
        wgpu::TextureUsages::COPY_SRC|wgpu::TextureUsages::COPY_DST) ||
       !color.flags.contains(required) || !depth.flags.contains(required) {
        return Err((CoinWgpuStatus::Unsupported,"Cube RTT color/depth copy or sample format unavailable".into()));
    }
    Ok((width,height))
}

#[no_mangle]
pub extern "C" fn coin_wgpu_preflight_cube_rtt(n:u32,mips:u32,samples:u32)->CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if mips>1{return CoinWgpuStatus::InvalidArgument;}
        let Ok(guard)=RUNTIME_CTX.lock() else{return CoinWgpuStatus::BackendError;};
        let Some(ctx)=guard.as_ref().and_then(|runtime|runtime.device_state.as_ref()) else{return CoinWgpuStatus::NotReady;};
        match admit_cube_rtt(ctx,n,mips!=0,samples){Ok(_)=>CoinWgpuStatus::Ok,Err((status,_))=>status}
    }).unwrap_or(CoinWgpuStatus::BackendError)
}

#[no_mangle]
pub extern "C" fn coin_wgpu_assemble_cube_rtt(faces:*const u64,n:u32,mips:u32,generation:u64,
    out_token:*mut u64,error_buf:*mut std::os::raw::c_char,error_buf_len:usize)->CoinWgpuStatus {
    std::panic::catch_unwind(|| {
        if out_token.is_null() || (out_token as usize)%std::mem::align_of::<u64>()!=0 || mips>1 {
            set_error(error_buf,error_buf_len,"Invalid cube RTT output pointer or mip flag");return CoinWgpuStatus::InvalidArgument;
        }
        unsafe{*out_token=0;}
        let faces=match validate_slice(faces,6,"cube RTT face tokens",error_buf,error_buf_len){Ok(v)=>v,Err(status)=>return status};
        let Ok(mut guard)=RUNTIME_CTX.lock() else{set_error(error_buf,error_buf_len,"Cube RTT runtime lock failed");return CoinWgpuStatus::BackendError;};
        let Some(ctx)=guard.as_mut().and_then(|runtime|runtime.device_state.as_mut()) else{set_error(error_buf,error_buf_len,"Cube RTT default device is not prepared");return CoinWgpuStatus::NotReady;};
        // The public connector domain uses raw generation + 1, preserving
        // zero as unavailable while accepting the first raw generation (0).
        // Match the existing query's checked epoch conversion on overflow.
        if generation==0 || ctx.generation.checked_add(1)!=Some(generation) {
            set_error(error_buf,error_buf_len,"Cube RTT belongs to another device generation");return CoinWgpuStatus::DeviceLost;
        }
        let (width,height)=match admit_cube_rtt(ctx,n,mips!=0,1){Ok(v)=>v,Err((status,message))=>{set_error(error_buf,error_buf_len,&message);return status;}};
        let mut registry=ctx.rtt_textures.lock().unwrap();
        if registry.active.len()>=64{set_error(error_buf,error_buf_len,"Cube RTT atlas exceeds retained texture capacity");return CoinWgpuStatus::OutOfMemory;}
        let mut sources=Vec::with_capacity(6);
        let levels=if mips!=0 {n.ilog2()+1}else{1};
        for (face,&token) in faces.iter().enumerate() {
            if token==0 || faces[..face].contains(&token) {
                set_error(error_buf,error_buf_len,"Cube RTT faces alias or have a zero token");return CoinWgpuStatus::InvalidArgument;
            }
            let Some(source)=registry.active.get(&token) else {
                set_error(error_buf,error_buf_len,"Cube RTT face token is stale or belongs to another device");return CoinWgpuStatus::InvalidArgument;
            };
            if source.width!=n || source.height!=n || source.format!=0 || source.cube_face_size!=0 || source.mip_count<levels {
                set_error(error_buf,error_buf_len,"Cube RTT face token metadata mismatch");return CoinWgpuStatus::InvalidArgument;
            }
            sources.push(source.texture.clone());
        }
        let texture=ctx.device.create_texture(&wgpu::TextureDescriptor {
            label:Some("Coin cube RTT canonical atlas"),size:wgpu::Extent3d{width,height,depth_or_array_layers:1},
            mip_level_count:1,sample_count:1,dimension:wgpu::TextureDimension::D2,format:wgpu::TextureFormat::Rgba8Unorm,
            usage:wgpu::TextureUsages::RENDER_ATTACHMENT|wgpu::TextureUsages::TEXTURE_BINDING|wgpu::TextureUsages::COPY_SRC|wgpu::TextureUsages::COPY_DST,
            view_formats:&[],
        });
        let view=texture.create_view(&Default::default());
        let mut encoder=ctx.device.create_command_encoder(&wgpu::CommandEncoderDescriptor{label:Some("Coin six-face cube RTT assembly")});
        {
            let _clear=encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label:Some("Coin cube atlas zero padding"),
                color_attachments:&[Some(wgpu::RenderPassColorAttachment{view:&view,resolve_target:None,
                    ops:wgpu::Operations{load:wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),store:wgpu::StoreOp::Store}})],
                depth_stencil_attachment:None,timestamp_writes:None,occlusion_query_set:None,
            });
        }
        let mut level_offset=0u64;let mut extent=n;
        for level in 0..levels {
            for (face,source) in sources.iter().enumerate() {
                for y in 0..extent {
                    let mut x=0;
                    while x<extent {
                        let linear=level_offset+face as u64*u64::from(extent)*u64::from(extent)+u64::from(y)*u64::from(extent)+u64::from(x);
                        let dx=(linear%u64::from(width)) as u32;let dy=(linear/u64::from(width)) as u32;
                        let count=(extent-x).min(width-dx);
                        encoder.copy_texture_to_texture(
                            wgpu::TexelCopyTextureInfo{texture:source,mip_level:level,origin:wgpu::Origin3d{x,y:extent-1-y,z:0},aspect:wgpu::TextureAspect::All},
                            wgpu::TexelCopyTextureInfo{texture:&texture,mip_level:0,origin:wgpu::Origin3d{x:dx,y:dy,z:0},aspect:wgpu::TextureAspect::All},
                            wgpu::Extent3d{width:count,height:1,depth_or_array_layers:1});
                        x+=count;
                    }
                }
            }
            level_offset+=6*u64::from(extent)*u64::from(extent);extent=(extent/2).max(1);
        }
        let token=NEXT_RTT_TOKEN.fetch_add(1,Ordering::SeqCst);
        if token==0{set_error(error_buf,error_buf_len,"Cube RTT token space exhausted");return CoinWgpuStatus::OutOfMemory;}
        let serial=GLOBAL_SUBMISSION_SERIAL.fetch_add(1,Ordering::SeqCst);
        LAST_SUBMITTED_SERIAL.store(serial,Ordering::SeqCst);
        let local_serial=ctx.last_submitted_serial.fetch_add(1,Ordering::SeqCst)+1;
        ctx.queue.submit([encoder.finish()]);
        let completed=ctx.completed_serial.clone();
        ctx.queue.on_submitted_work_done(move||{completed.fetch_max(local_serial,Ordering::SeqCst);GLOBAL_COMPLETED_SERIAL.fetch_max(serial,Ordering::SeqCst);});
        registry.active.insert(token,RttTexture{texture,view,width,height,opaque:false,format:0,mip_count:1,cube_face_size:n,cube_mipmapped:mips!=0,completed_view:None});
        unsafe{*out_token=token;}CoinWgpuStatus::Ok
    }).unwrap_or_else(|_|{set_error(error_buf,error_buf_len,"Cube RTT assembly panicked");CoinWgpuStatus::BackendError})
}

#[no_mangle]
pub extern "C" fn coin_wgpu_release_texture(token: u64) {
    let _ = std::panic::catch_unwind(|| {
        if token == 0 { return; }
        if let Ok(mut guard) = RUNTIME_CTX.lock() {
            if let Some(runtime) = guard.as_mut() {
                if let Some(ctx) = runtime.device_state.as_mut() {
                    if let Ok(mut textures) = ctx.rtt_textures.lock() {
                        if let Some(entry) = textures.active.remove(&token) {
                            let serial = ctx.last_submitted_serial.load(Ordering::SeqCst);
                            if ctx.completed_serial.load(Ordering::SeqCst) < serial {
                                textures.retired.push(RetiredRttTexture {
                                    resource: entry,
                                    retired_at_serial: serial,
                                });
                            }
                        }
                    }
                }
                for ctx in runtime.extra_devices.values() {
                    if let Ok(mut textures) = ctx.rtt_textures.lock() {
                        if let Some(entry) = textures.active.remove(&token) {
                            let serial = ctx.last_submitted_serial.load(Ordering::SeqCst);
                            if ctx.completed_serial.load(Ordering::SeqCst) < serial {
                                textures.retired.push(RetiredRttTexture {
                                    resource: entry,
                                    retired_at_serial: serial,
                                });
                            }
                            break;
                        }
                    }
                }
            }
        }
    });
}
