//! Texture metadata, profiles and coordinate resource validation.

use super::*;

// Transport validation only: chain generation belongs to Coin/Core.
pub(super) fn texture_levels(width:u32,height:u32,format:u32)->Result<Vec<(u32,u32,u64)>,(CoinWgpuStatus,String)> {
    texture_profile::levels(width,height,format)
}

pub(super) fn texture_metadata(image: &CoinWgpuTexture) -> u32 {
    image.reserved & !TEXTURE_DECLARED_OPAQUE
}

// Hardware sRGB decoding applies independently to each fetched atlas texel,
// before our explicit volume/cube spatial/LOD interpolation, and leaves alpha linear.
pub(super) fn texture_gpu_format(image: &CoinWgpuTexture)
    -> Result<wgpu::TextureFormat, (CoinWgpuStatus, String)> {
    if (image.format == 11 && texture_metadata(image) & TEXTURE_VOLUME_SRGB != 0) ||
       (image.format == 12 && texture_metadata(image) & TEXTURE_CUBE_SRGB != 0) {
        Ok(wgpu::TextureFormat::Rgba8UnormSrgb)
    } else {
        Ok(texture_profile::format(image.format)?.0)
    }
}

pub(super) fn validate_texture_declarations(textures: &[CoinWgpuTexture]) -> Result<(), String> {
    for image in textures {
        let allowed = match image.format {
            0 | 2..=10 => 0,
            1 => 31,
            11 => 0x03ff_ffff,
            12 => 1023,
            _ => return Err("Invalid texture format in declaration metadata".into()),
        };
        if texture_metadata(image) & !allowed != 0 {
            return Err("Texture metadata has unknown reserved bits".into());
        }
    }
    Ok(())
}

pub(super) fn gpu_texture_token(image: &CoinWgpuTexture) -> bool {
    image.format==1 || (image.format==12 && texture_metadata(image)&256!=0)
}

// Format 12 keeps a face-local chain in an RGBA8 2D transport atlas.
// reserved: log2(face size)[0..3], source components[4..6], mip flag[7], GPU token[8], sRGB[9].
pub(super) fn cube_atlas_metadata(t:&CoinWgpuTexture)->Result<Option<(u32,u32,bool)>,(CoinWgpuStatus,String)>{
    if t.format!=12{return Ok(None);}
    let exponent=texture_metadata(t)&15;let components=(texture_metadata(t)>>4)&7;let mips=texture_metadata(t)&128!=0;
    if texture_metadata(t) & !1023 != 0 || exponent>11 || !(1..=4).contains(&components){
        return Err((CoinWgpuStatus::InvalidArgument,"Invalid cube atlas metadata".into()));
    }
    let n=1u32<<exponent;let width=2*n;let mut size=n;let mut texels=0u64;
    loop{texels+=6*u64::from(size)*u64::from(size);if !mips||size==1{break;}size/=2;}
    let height=texels.div_ceil(u64::from(width));let bytes=u64::from(width)*height*4;
    if t.width!=width || u64::from(t.height)!=height || t.height>8192 || bytes>128*1024*1024 ||
       (if texture_metadata(t)&256!=0 {t.pixel_bytes_len!=0 || !t.pixels.is_null() || t.content_digest==0 || components!=4 || texture_metadata(t)&TEXTURE_CUBE_SRGB!=0}
        else {t.pixel_bytes_len!=bytes || t.pixels.is_null()}){
        return Err((CoinWgpuStatus::InvalidArgument,"Invalid cube atlas extent or pixels".into()));
    }
    if texture_metadata(t)&256!=0 {return Ok(Some((n,components,mips)));}
    let pixels=unsafe{std::slice::from_raw_parts(t.pixels,bytes as usize)};
    if pixels[texels as usize*4..].iter().any(|v|*v!=0){
        return Err((CoinWgpuStatus::InvalidArgument,"Nonzero cube atlas padding".into()));
    }
    Ok(Some((n,components,mips)))
}

pub(super) fn validate_cube_resources(states:&[CoinWgpuRenderState],textures:&[CoinWgpuTexture],samplers:&[CoinWgpuSampler])
    ->Result<(),(CoinWgpuStatus,String)>{
    validate_spatial_resources(states,textures,samplers)?;
    for image in textures{cube_atlas_metadata(image)?;}
    if !textures.iter().any(|t|t.format==12){return Ok(());}
    for state in states{for unit in 0..8{
        let (enabled,image,sampler,model)=if unit==0{(state.has_texture,state.texture_slot,state.sampler_slot,state.texture_model)}
            else{let t=&state.extra_textures[unit-1];(t.enabled,t.texture_slot,t.sampler_slot,t.model)};
        if enabled==0{continue;}
        let image=textures.get(image as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Cube image slot out of bounds".into()))?;
        if image.format!=12{continue;}
        let (_,components,mips)=cube_atlas_metadata(image)?.unwrap();
        let matrix=if unit==0{&state.texture_matrix}else{&state.extra_textures[unit-1].matrix};
        if state.texture_projection>1 || state.portable_shader_slot!=0 || !matrix.iter().all(|v|v.is_finite()) {
            return Err((CoinWgpuStatus::InvalidArgument,"Unsupported cube shader or invalid coordinate matrix".into()));
        }
        let sampler=samplers.get(sampler as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Cube sampler slot out of bounds".into()))?;
        if sampler.wrap_s!=1||sampler.wrap_t!=1||sampler.reserved!=16||sampler.filter>3||model>3||(model==2&&components<3)||(sampler.filter>=2&&!mips){
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid cube sampler or model".into()));
        }
    }}
    Ok(())
}

pub(super) fn validate_cube_draws(states:&[CoinWgpuRenderState],textures:&[CoinWgpuTexture],vertices:&[CoinWgpuVertex],
    indices:&[u32],draws:&[CoinWgpuDraw])->Result<(),(CoinWgpuStatus,String)>{
    if !textures.iter().any(|t|t.format==11||t.format==12){return Ok(());}
    for draw in draws{
        let state=states.get(draw.render_state_slot as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Cube draw state out of bounds".into()))?;
        let cube=(0..8).any(|unit|{
            let (enabled,slot)=if unit==0{(state.has_texture,state.texture_slot)}else{let t=&state.extra_textures[unit-1];(t.enabled,t.texture_slot)};
            enabled!=0&&textures.get(slot as usize).is_some_and(|t|t.format==11||t.format==12)
        });
        if !cube{continue;}
        let end=draw.first_index.checked_add(draw.index_count).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Cube draw index overflow".into()))?;
        let primitive=match draw.topology{0=>3,1=>2,2=>1,_=>return Err((CoinWgpuStatus::InvalidArgument,"Invalid cube topology".into()))};
        if draw.index_count%primitive!=0{return Err((CoinWgpuStatus::InvalidArgument,"Incomplete cube primitive".into()));}
        let range=indices.get(draw.first_index as usize..end as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Cube draw indices out of bounds".into()))?;
        validate_texture_coordinates_with_images(state,draw.topology,vertices,range,textures).map_err(|message|(CoinWgpuStatus::InvalidArgument,message))?;
    }
    Ok(())
}

pub(super) fn spatial_atlas_metadata(t: &CoinWgpuTexture)
    -> Result<Option<(u32,u32,u32,bool)>,(CoinWgpuStatus,String)> {
    if t.format != 11 { return Ok(None); }
    if texture_metadata(t) & !0x03ff_ffff != 0 { return Err((CoinWgpuStatus::InvalidArgument,
        "Spatial atlas metadata has reserved bits".into())); }
    let w=(texture_metadata(t) & 255)+1;
    let h=((texture_metadata(t) >> 8) & 255)+1;
    let d=((texture_metadata(t) >> 16) & 255)+1;
    let mips=texture_metadata(t) & (1<<24) != 0;
    let mut columns=1u32;
    while columns*columns<d { columns*=2; }
    let mut rows=0u32;
    let (mut lw,mut lh,mut ld)=(w,h,d);
    loop {
        rows=rows.checked_add(lh*((ld+columns-1)/columns)).ok_or_else(||
            (CoinWgpuStatus::Unsupported,"Spatial atlas row count overflow".into()))?;
        if !mips || (lw==1 && lh==1 && ld==1) { break; }
        lw=(lw/2).max(1); lh=(lh/2).max(1); ld=(ld/2).max(1);
    }
    if t.width != w*columns || t.height != rows ||
       t.width > 8192 || t.height > 8192 ||
       u64::from(t.width)*u64::from(t.height)*4 > 128*1024*1024 ||
       t.pixels.is_null() || t.pixel_bytes_len != u64::from(t.width)*u64::from(t.height)*4 {
        return Err((CoinWgpuStatus::InvalidArgument,
            "Spatial atlas physical extent or byte budget mismatch".into()));
    }
    Ok(Some((w,h,d,mips)))
}

// Validate every transported volume before target acquisition or cache changes,
// including unused resources and padding that must never become authored data.
pub(super) fn validate_spatial_resources(states:&[CoinWgpuRenderState],textures:&[CoinWgpuTexture],samplers:&[CoinWgpuSampler])
    ->Result<(),(CoinWgpuStatus,String)>{
    for image in textures {
        if let Some((mut w,mut h,mut d,mips))=spatial_atlas_metadata(image)? {
            let mut columns=1;
            while columns*columns<d {columns*=2;}
            let pixels=unsafe{std::slice::from_raw_parts(image.pixels,image.pixel_bytes_len as usize)};
            let mut base=0;
            loop {
                let rows=(d+columns-1)/columns;
                for row in 0..rows {
                    let used=(d-row*columns).min(columns)*w;
                    for y in 0..h {
                        let start=((base+row*h+y)*image.width+used) as usize*4;
                        let end=((base+row*h+y+1)*image.width) as usize*4;
                        if pixels[start..end].iter().any(|v|*v!=0) {
                            return Err((CoinWgpuStatus::InvalidArgument,"Nonzero spatial atlas padding".into()));
                        }
                    }
                }
                base+=h*rows;
                if !mips || (w==1&&h==1&&d==1) {break;}
                w=(w/2).max(1);h=(h/2).max(1);d=(d/2).max(1);
            }
        }
    }
    if !textures.iter().any(|t|t.format==11) {return Ok(());}
    for sampler in samplers {
        if sampler.wrap_s>1 || sampler.wrap_t>1 || sampler.filter>3 || sampler.reserved&!31!=0 ||
           texture_profile::anisotropy(sampler.reserved&15,sampler.filter).is_err() {
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid spatial frame sampler".into()));
        }
    }
    for state in states {for unit in 0..8 {
        let (enabled,image,sampler,model)=if unit==0 {(state.has_texture,state.texture_slot,state.sampler_slot,state.texture_model)}
            else {let layer=&state.extra_textures[unit-1];(layer.enabled,layer.texture_slot,layer.sampler_slot,layer.model)};
        if enabled==0 {continue;}
        let image=textures.get(image as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Volume frame image slot out of bounds".into()))?;
        if image.format!=11 {continue;}
        let sampler=samplers.get(sampler as usize).ok_or_else(||(CoinWgpuStatus::InvalidArgument,"Volume sampler slot out of bounds".into()))?;
        if sampler.wrap_s>1 || sampler.wrap_t>1 || sampler.filter>3 || sampler.reserved&!16!=0 || model>3 ||
           (sampler.filter>=2 && texture_metadata(image)&(1<<24)==0) {
            return Err((CoinWgpuStatus::InvalidArgument,"Invalid volume sampler or model".into()));
        }
    }}
    Ok(())
}

pub(super) fn admit_spatial_resources(ctx:&DeviceState,textures:&[CoinWgpuTexture])->Result<(),(CoinWgpuStatus,String)>{
    for image in textures.iter().filter(|image|image.format==12) {
        let (n,_,mips)=cube_atlas_metadata(image)?.unwrap();
        if image.width>ctx.device.limits().max_texture_dimension_2d || image.height>ctx.device.limits().max_texture_dimension_2d {
            return Err((CoinWgpuStatus::Unsupported,"Cube atlas exceeds GPU texture dimension limit".into()));
        }
        if gpu_texture_token(image) {
            let registry=ctx.rtt_textures.lock().unwrap();
            let entry=registry.active.get(&image.content_digest).ok_or_else(||
                (CoinWgpuStatus::InvalidArgument,"Cube token is stale or from another device/generation".into()))?;
            if entry.width!=image.width || entry.height!=image.height || entry.cube_face_size!=n ||
                entry.cube_mipmapped!=mips || entry.format!=0 || entry.mip_count!=1 {
                return Err((CoinWgpuStatus::InvalidArgument,"Cube token metadata mismatch".into()));
            }
        }
    }

    if textures.iter().any(|t|t.format==11 && (t.width>ctx.device.limits().max_texture_dimension_2d ||
        t.height>ctx.device.limits().max_texture_dimension_2d)) {
        return Err((CoinWgpuStatus::Unsupported,"Spatial atlas exceeds GPU texture dimension limit".into()));
    }
    Ok(())
}

pub(super) fn resolved_viewport(state: &CoinWgpuRenderState, width: u32, height: u32)
    -> Result<[i32; 4], (CoinWgpuStatus, String)> {
    let viewport = if state.viewport[2] == 0 && state.viewport[3] == 0 {
        [0, 0, width as i32, height as i32]
    } else { state.viewport };
    // Core's empty scissor. Keep the legacy all-zero full-target default.
    if viewport == [0, 0, 0, 1] { return Ok(viewport); }
    let right = i64::from(viewport[0]) + i64::from(viewport[2]);
    let bottom = i64::from(viewport[1]) + i64::from(viewport[3]);
    if viewport[0] < 0 || viewport[1] < 0 || viewport[2] <= 0 || viewport[3] <= 0
        || right > i64::from(width) || bottom > i64::from(height) {
        return Err((CoinWgpuStatus::InvalidArgument,
            format!("Invalid viewport {:?}", viewport)));
    }
    Ok(viewport)
}
