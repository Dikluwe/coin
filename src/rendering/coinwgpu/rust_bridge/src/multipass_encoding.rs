//! Multipass admission and sample replay encoding.

use super::*;

// Private multipass metadata is validated before any adapter acquisition,
// resource/cache mutation or producer command. It never dereferences a payload
// until the common pointer/count/alignment proof succeeds.
pub(super) fn validate_multipass_metadata(frame:&CoinWgpuFrameView,
    error:*mut std::os::raw::c_char, error_len:usize) -> Result<(),CoinWgpuStatus> {
    let invalid=|message:&str| {set_error(error,error_len,message);CoinWgpuStatus::InvalidArgument};
    if !(1..=16).contains(&frame.multipass_count) || frame.multipass_reserved!=0 {
        return Err(invalid("Invalid multipass count or reserved field"));
    }
    if frame.multipass_matrix_count==0 {
        if !frame.multipass_matrices.is_null() {return Err(invalid("Unexpected multipass matrix pointer"));}
    } else {
        let expected=frame.state_count.checked_mul(u64::from(frame.multipass_count)).and_then(|n|n.checked_mul(16));
        if frame.multipass_count==1 || expected!=Some(frame.multipass_matrix_count) {
            return Err(invalid("Invalid multipass matrix payload count"));
        }
        let values=validate_slice(frame.multipass_matrices,frame.multipass_matrix_count,"multipass matrices",error,error_len)?;
        if values.iter().any(|value|!value.is_finite()) {return Err(invalid("Non-finite multipass matrix payload"));}
    }
    if frame.multipass_clear_color_count==0 {
        if !frame.multipass_clear_colors.is_null() { return Err(invalid("Unexpected snapshot clear pointer")); }
    } else {
        if frame.multipass_clear_color_count!=u64::from(frame.multipass_count)*4 {
            return Err(invalid("Invalid snapshot clear payload count"));
        }
        let values=validate_slice(frame.multipass_clear_colors,frame.multipass_clear_color_count,
            "snapshot clears",error,error_len)?;
        if values.iter().any(|value|!value.is_finite()) { return Err(invalid("Non-finite snapshot clear color")); }
    }

    if frame.multipass_count>1 {
        let minimum=multipass::bytes(frame.width,frame.height)
            .and_then(|n|frame.multipass_matrix_count.checked_mul(4).and_then(|m|n.checked_add(m)));
        if minimum.is_none_or(|bytes|bytes>frame.multipass_budget_bytes) {
            set_error(error,error_len,"Multipass accumulation and projection payload exceed budget");
            return Err(CoinWgpuStatus::OutOfMemory);
        }
    }
    Ok(())
}

// Validate the resolved draw source: either structurally checked caller slices
// or the immutable snapshot proven by camera_base. Camera patches intentionally
// omit caller geometry while retaining the snapshot's counts and draw masks.
pub(super) fn validate_multipass_draws(frame:&CoinWgpuFrameView, draws:&[CoinWgpuDraw],
    error:*mut std::os::raw::c_char, error_len:usize) -> Result<(),CoinWgpuStatus> {
    let invalid=|message:&str| {set_error(error,error_len,message);CoinWgpuStatus::InvalidArgument};
    if !(1..=16).contains(&frame.multipass_count) || draws.len() as u64!=frame.draw_count {
        return Err(invalid("Invalid multipass draw count"));
    }
    for draw in draws {
        let mask=(draw.composition_flags>>15)&0xffff;
        if mask>>frame.multipass_count!=0 || (frame.multipass_count>1 &&
            draw.composition_flags&0x80000000!=0 && frame.multipass_matrix_count==0) {
            return Err(invalid("Invalid multipass draw mask or missing raster matrices"));
        }
    }
    Ok(())
}

// Raw ABI callers receive the same simultaneous-resource admission as the
// C++ connector. All slices used here were structurally validated by ingress.
pub(super) fn multipass_pending_bytes(runtime: Option<&RuntimeContext>) -> Option<u64> {
    runtime.map_or(Some(0), |runtime| runtime.pending_readbacks.values()
        .chain(runtime.retired_readbacks.iter()).try_fold(0u64,|sum,job|
            sum.checked_add(job.color.size())?.checked_add(job.depth.as_ref().map_or(0,|depth|depth.size()))))
}

pub(super) fn admit_multipass_resources(frame:&CoinWgpuFrameView, samples:u32,
    states:&[CoinWgpuRenderState], textures:&[CoinWgpuTexture],
    order:&[composition::CompositionItem], shadow:Option<&ShadowFrame>,
    window:bool, readback:bool, depth:bool, pending:Option<u64>)
    -> Result<(),(CoinWgpuStatus,String)> {
    if frame.multipass_count==1 {return Ok(());}
    let bytes=(|| -> Option<u64> {
        let width=u64::from(frame.width); let height=u64::from(frame.height);
        let mut total=pending?;
        let mut charge=|w:u64,h:u64,b:u64,n:u64| -> Option<()> {
            total=total.checked_add(w.checked_mul(h)?.checked_mul(b)?.checked_mul(n)?)?;
            Some(())
        };
        let color=if frame.texture_output_flags&1!=0 {8}else{4};
        charge(width,height,32,1)?;
        charge(width,height,color+4,u64::from(samples))?;
        if samples>1 {charge(width,height,color+4,1)?;}
        charge(frame.multipass_matrix_count,1,4,1)?;
        if window {charge(width,height,4,1)?;}
        if readback {
            let pitch=width.checked_mul(4)?.checked_add(255)? & !255;
            charge(pitch,height,1,if depth {2}else{1})?;
            if depth && samples>1 {charge(width,height,4,1)?;}
        }
        if frame.texture_output_flags&2!=0 {
            let mut w=width;let mut h=height;
            while w>1 || h>1 {w=(w/2).max(1);h=(h/2).max(1);charge(w,h,color,1)?;}
        }
        if order.iter().any(|item|item.peel) {
            charge(width,height,4,u64::from(samples))?;
            charge(width,height,13,u64::from(samples).checked_mul(u64::from(frame.sorted_layers_passes))?)?;
        }
        if order.iter().any(|item|item.weighted) {charge(width,height,10,u64::from(samples))?;}
        if let Some(shadow)=shadow {
            let sizes:Vec<_>=std::iter::once(shadow.map_size).chain(shadow.second.iter()
                .chain(shadow.third.iter()).chain(shadow.fourth.iter()).chain(shadow.extra.iter())
                .map(|pass|pass.map_size)).collect();
            for &size in &sizes {charge(u64::from(size),u64::from(size),20,1)?;}
            let size=u64::from(*sizes.iter().max()?);
            charge(size,size,16,u64::from(shadow::array_layer_count(sizes.len() as u32)))?;
        }
        let mut tokens=std::collections::HashSet::new();
        for image in textures.iter().filter(|image|gpu_texture_token(image)) {
            if !tokens.insert(image.content_digest) {continue;}
            let format=if image.format==1 {texture_metadata(image)>>2}else{0};
            let bpp=if format==2 {8}else{4};
            charge(u64::from(image.width),u64::from(image.height),bpp,1)?;
            if image.format==1 && texture_metadata(image)&2!=0 {
                let mut w=u64::from(image.width);let mut h=u64::from(image.height);
                while w>1 || h>1 {w=(w/2).max(1);h=(h/2).max(1);charge(w,h,bpp,1)?;}
            }
        }
        for state in states.iter().filter(|state|state.raster_texture_source_slot!=0) {
            let image=textures.get(state.texture_slot as usize)?;
            charge(u64::from(image.width),u64::from(image.height),16,1)?;
        }
        Some(total)
    })();
    if bytes.is_none_or(|bytes|bytes>frame.multipass_budget_bytes) {
        Err((CoinWgpuStatus::OutOfMemory,"Multipass simultaneous scene, composition, shadow, RTT and staging resources exceed budget".into()))
    } else {Ok(())}
}

// Pure shared command encoder function strictly common to offscreen and window targets
pub(super) fn encode_scene_samples(
    ctx: &DeviceState, frame: &CoinWgpuFrameView, size: [u32;2],
    vertices: &[CoinWgpuVertex], indices: &[u32], draws: &[CoinWgpuDraw],
    order: &[composition::CompositionItem], materials: &[CoinWgpuMaterial],
    states: &[CoinWgpuRenderState], modules: &[portable::Module],
    textures: &[CoinWgpuTexture], samplers: &[CoinWgpuSampler],
    bump: Option<&ValidatedBumpPayload>, shadow: Option<&ShadowFrame>,
    camera_geometry: Option<&Arc<ValidatedGeometry>>,
    instances: &[CoinWgpuInstance], instance_ranges: &[CoinWgpuInstanceRange],
    timestamp: Option<&wgpu::QuerySet>, color: &wgpu::TextureView,
    resolved: Option<&wgpu::TextureView>, format: wgpu::TextureFormat,
    depth: &wgpu::TextureView, depth_texture: &wgpu::Texture, samples: u32,
) -> Result<(Vec<wgpu::CommandBuffer>,Option<InstancedGpuBuffers>),(CoinWgpuStatus,String)> {
    let count=frame.multipass_count;
    if !(1..=16).contains(&count) || frame.multipass_reserved!=0 {
        return Err((CoinWgpuStatus::InvalidArgument,"Invalid multipass request".into()));
    }
    if bump.is_some() && (!instances.is_empty() || !instance_ranges.is_empty()
        || order.iter().any(|item|item.peel||item.weighted) || frame.rasterization_policy!=0) {
        return Err((CoinWgpuStatus::Unsupported,"Bump frame cannot combine instancing or composition".into()));
    }
    let matrix_count=(states.len() as u64).checked_mul(u64::from(count)).and_then(|n|n.checked_mul(16))
        .ok_or((CoinWgpuStatus::InvalidArgument,"Multipass matrix count overflows".into()))?;
    let matrices=if frame.multipass_matrix_count==0 {
        if !frame.multipass_matrices.is_null() {
            return Err((CoinWgpuStatus::InvalidArgument,"Unexpected multipass matrix pointer".into()));
        }
        &[][..]
    } else {
        if count==1 || frame.multipass_matrix_count!=matrix_count {
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid multipass matrix payload count".into()));
        }
        validate_slice(frame.multipass_matrices,frame.multipass_matrix_count,
            "multipass matrices",std::ptr::null_mut(),0)
            .map_err(|status|(status,"Invalid multipass matrix pointer/count/alignment".into()))?
    };
    if matrices.iter().any(|value|!value.is_finite()) || draws.iter().any(|draw|
        ((draw.composition_flags>>15)&0xffff)>>count != 0) {
        return Err((CoinWgpuStatus::InvalidArgument,"Invalid multipass matrices or sample mask".into()));
    }
    let clear_colors = if frame.multipass_clear_color_count == 0 {
        if !frame.multipass_clear_colors.is_null() {
            return Err((CoinWgpuStatus::InvalidArgument,"Unexpected snapshot clear pointer".into()));
        }
        &[][..]
    } else {
        if frame.multipass_clear_color_count != u64::from(count)*4 {
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid snapshot clear count".into()));
        }
        validate_slice(frame.multipass_clear_colors,frame.multipass_clear_color_count,
            "snapshot clears",std::ptr::null_mut(),0)
            .map_err(|status|(status,"Invalid snapshot clear pointer/count/alignment".into()))?
    };
    if clear_colors.iter().any(|value|!value.is_finite()) {
        return Err((CoinWgpuStatus::InvalidArgument,"Non-finite snapshot clear color".into()));
    }
    let sample_states = |sample:u32| {
        if matrices.is_empty() { multipass::jitter_states(states,count,sample) }
        else {
            let mut output=states.to_vec();
            for (slot,state) in output.iter_mut().enumerate() {
                let first=(sample as usize*states.len()+slot)*16;
                state.model_view_projection.copy_from_slice(&matrices[first..first+16]);
            }
            output
        }
    };
    let encode = |sample_states:&[CoinWgpuRenderState], sample_draws:&[CoinWgpuDraw],
                  sample_order:&[composition::CompositionItem], shared, single:bool, sample_clear:[f32;4]| {
        if let Some(payload)=bump {
            encode_mixed_bump_frame(ctx,Some(&ctx.bump_pipelines),
                Some((&ctx.bump_base_cache,ctx.last_submitted_serial.load(Ordering::SeqCst)+1,
                    ctx.completed_serial.load(Ordering::SeqCst))),vertices,indices,sample_draws,sample_states,
                modules,materials,textures,samplers,payload,shadow,color,resolved,format,depth,
                size,sample_clear,samples,depth_texture,frame.texture_sampling_policy,shared)
        } else {
            encode_frame_impl(ctx,if single{frame.frame_revision}else{0},sample_clear,
                size[0],size[1],vertices,indices,sample_draws,sample_order,materials,sample_states,modules,textures,
                samplers,shadow,if single{camera_geometry}else{None},instances,instance_ranges,
                if single{timestamp}else{None},color,resolved,format,depth,depth_texture,samples,
                frame.sorted_layers_passes,frame.texture_sampling_policy,frame.rasterization_policy,
                frame.stereo_mode==0,None,shared).and_then(|(command,instances)| {
                    let mut commands=Vec::new();
                    if frame.stereo_mode!=0 {
                        commands.push(stereo::encode(ctx,frame,size,color,format,depth,samples)?);
                    }
                    commands.push(command);
                    Ok((commands,instances))
                })
        }
    };
    if count==1 {return encode(states,draws,order,None,true,frame.clear_color);}
    let required=multipass::bytes(size[0],size[1])
        .ok_or((CoinWgpuStatus::OutOfMemory,"Multipass accumulation extent overflows".into()))?;
    if required>frame.multipass_budget_bytes {
        return Err((CoinWgpuStatus::OutOfMemory,"Multipass accumulation exceeds its buffer budget".into()));
    }
    let usage=wgpu::TextureUsages::STORAGE_BINDING|wgpu::TextureUsages::TEXTURE_BINDING;
    if !ctx.adapter.get_texture_format_features(wgpu::TextureFormat::Rgba32Float).allowed_usages.contains(usage) {
        return Err((CoinWgpuStatus::Unsupported,"Multipass RGBA32F accumulation unavailable".into()));
    }
    // Validate every translated matrix before issuing any sample commands.
    for sample in 0..count {
        if sample_states(sample).iter()
            .any(|state|state.model_view_projection.iter().any(|value|!value.is_finite())) {
            return Err((CoinWgpuStatus::InvalidArgument,"Multipass jitter projection overflows".into()));
        }
    }
    let _uploads=MultipassUploadScope::begin();
    let programs=ctx.multipass_programs.get(&ctx.device,format)
        .map_err(|error|(CoinWgpuStatus::BackendError,error))?;
    let accumulator=multipass::Accumulation::new(&ctx.device,size[0],size[1],count,programs);
    let shared_shadow=if let Some(shadow)=shadow {
        if fault_state::current()==FAULT_SHADOW_MAP_ALLOC {
            fault_state::clear();
            return Err((CoinWgpuStatus::OutOfMemory,"Injected shadow-map attachment allocation failure".into()));
        }
        let mut encoder=ctx.device.create_command_encoder(&Default::default());
        let mut maps=Vec::new();
        maps.push(shadow::encode_moments(&ctx.device,&mut encoder,vertices,indices,
            shadow.casters,states,shadow.map_size,shadow.near,shadow.far,shadow.kind,
            frame.rasterization_policy==1).map_err(|error|(CoinWgpuStatus::InvalidArgument,error))?);
        for pass in shadow.second.iter().chain(shadow.third.iter()).chain(shadow.fourth.iter()).chain(shadow.extra.iter()) {
            maps.push(shadow::encode_moments(&ctx.device,&mut encoder,vertices,indices,
                pass.casters,states,pass.map_size,pass.near,pass.far,pass.kind,
                frame.rasterization_policy==1).map_err(|error|(CoinWgpuStatus::InvalidArgument,error))?);
        }
        let refs:Vec<_>=maps.iter().collect();
        let array=shadow::encode_array(&ctx.device,&mut encoder,&refs)
            .map_err(|error|(CoinWgpuStatus::Unsupported,error))?;
        ctx.queue.submit([encoder.finish()]);
        Some((maps,array))
    } else {None};
    let sample_view=resolved.unwrap_or(color);
    for sample in 0..count {
        let states_for_sample=sample_states(sample);
        let selected_draws:Vec<_>=draws.iter().filter_map(|draw| {
            let mask=(draw.composition_flags>>15)&0xffff;
            if mask==0 || mask&(1u32<<sample)!=0 {Some(*draw)}
            else if draw.clear_depth_before!=0 || draw.composition_flags&64!=0 {
                let mut barrier=*draw;
                barrier.index_count=0; barrier.vertex_count=0;
                Some(barrier)
            } else {None}
        }).collect();
        let selected_order=composition::order(vertices,indices,&selected_draws,materials,&states_for_sample,textures)?;
        let sample_clear=if clear_colors.is_empty(){frame.clear_color}else{
            let index=sample as usize*4;
            [clear_colors[index],clear_colors[index+1],clear_colors[index+2],clear_colors[index+3]]
        };
        let (mut commands,_)=encode(&states_for_sample,&selected_draws,&selected_order,shared_shadow.as_ref(),false,sample_clear)?;
        commands.push(accumulator.accumulate(&ctx.device,sample_view,sample));
        // A later encoder may update retained uniform buffers. Ordered private
        // submits make those writes follow this sample's draw commands; none
        // acquires/presents, publishes a serial or performs a readback.
        ctx.queue.submit(commands);
    }
    Ok((vec![accumulator.resolve(&ctx.device,sample_view)],None))
}
