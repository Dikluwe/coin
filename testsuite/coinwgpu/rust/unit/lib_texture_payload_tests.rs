    use super::*;
    fn projective_fixture() -> (CoinWgpuRenderState, [CoinWgpuVertex; 3]) {
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        state.has_texture = 1;
        for i in 0..4 { state.texture_matrix[i * 5] = 1.0; }
        let mut vertices = [CoinWgpuVertex::zeroed(); 3];
        for vertex in &mut vertices { vertex.texcoord = [0.5, 0.25, 0.0, 1.0]; }
        (state, vertices)
    }

    #[test]
    fn rejects_singular_crossing_nonfinite_and_overflowing_projective_coordinates() {
        let (mut state, mut vertices) = projective_fixture();
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        vertices[1].texcoord[3] = 0.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        vertices[1].texcoord[3] = -1.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        assert!(validate_texture_coordinates(&state, 1, &vertices, &[0, 1]).is_err());
        assert!(validate_texture_coordinates(&state, 2, &vertices, &[0, 1, 2]).is_ok(),
            "each point owns its own q sign");
        for vertex in &mut vertices { vertex.texcoord[3] = -2.0; }
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        vertices[0].texcoord[3] = f32::MIN_POSITIVE * 0.5;
        assert!(validate_texture_coordinates(&state, 2, &vertices, &[0]).is_err());
        vertices[0].texcoord = [f32::MAX, 0.25, 0.0, 0.5];
        assert!(validate_texture_coordinates(&state, 2, &vertices, &[0]).is_err());
        vertices[0].texcoord = [f32::MAX, 0.25, 0.0, 1.0];
        state.texture_matrix[0] = 2.0;
        assert!(validate_texture_coordinates(&state, 2, &vertices, &[0]).is_err());
        vertices[0].texcoord = [0.5, 0.25, f32::NAN, 1.0];
        assert!(validate_texture_coordinates(&state, 2, &vertices, &[0]).is_err());
        vertices[0] = CoinWgpuVertex::zeroed();
        state.has_texture = 0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok(),
            "inactive unit zero q is valid dormant transport");
    }

    #[test]
    fn transforms_r_into_q_and_validates_upper_unit_independently() {
        let (mut state, mut vertices) = projective_fixture();
        // q' = r + q. Raw positive q cannot qualify a zero/crossing transformed q.
        state.texture_matrix[11] = 1.0;
        vertices[1].texcoord[2] = -1.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        vertices[1].texcoord[2] = -2.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        for vertex in &mut vertices { vertex.texcoord[2] = -3.0; }
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        state.has_texture = 0;
        let upper = &mut state.extra_textures[6];
        upper.enabled = 1;
        for i in 0..4 { upper.matrix[i * 5] = 1.0; }
        upper.matrix[11] = 1.0;
        for vertex in &mut vertices { vertex.extra_texcoords[6] = [0.5, 0.25, 0.0, 1.0]; }
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        vertices[2].extra_texcoords[6][2] = -1.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        state.extra_textures[6].enabled = 0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
    }

    #[test]
    fn direct_st_policy_permits_singular_q_but_requires_finite_st_and_raw_payload() {
        let (mut state, mut vertices) = projective_fixture();
        vertices[0].texcoord[3] = 0.0;
        vertices[1].texcoord[3] = -1.0;
        vertices[2].texcoord[3] = f32::MIN_POSITIVE * 0.5;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        let projective = state;
        state.texture_projection = 1;
        assert!(valid_texture_payload(&state));
        assert!(!same_camera_independent_state(&projective, &state),
            "projection policy participates in state reuse even without a shadow map");
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        // Native direct ST does not sample R/Q, so a finite input whose Q row
        // overflows remains valid when the transformed S/T rows stay finite.
        vertices[0].texcoord = [0.5, 0.25, f32::MAX, 1.0];
        state.texture_matrix[11] = 2.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        vertices[0].texcoord[0] = f32::MAX;
        state.texture_matrix[0] = 2.0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        vertices[0].texcoord = [0.5, 0.25, f32::NAN, 1.0];
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        vertices[0].texcoord = [0.5, 0.25, 0.0, 1.0];
        state.has_texture = 0;
        state.extra_textures[6].enabled = 1;
        for i in 0..4 { state.extra_textures[6].matrix[i * 5] = 1.0; }
        for vertex in &mut vertices { vertex.extra_texcoords[6] = [0.5, 0.25, 0.0, 0.0]; }
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_ok());
        state.texture_projection = 0;
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err());
        state.texture_projection = 2;
        state.extra_textures[6].enabled = 0;
        assert!(!valid_texture_payload(&state));
        assert!(validate_texture_coordinates(&state, 0, &vertices, &[0, 1, 2]).is_err(),
            "unknown projection policy is rejected even without active texture units");
    }

    #[test]
    fn rejects_malformed_programs_and_extra_unit_values() {
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        assert!(valid_texture_payload(&state));
        let valid = [[1.0, 7.0, 5.0, 0.0], [0.0, 15.0, 4.0, 4.0],
            [8.0, 15.0, 11.0, 2.0], [0.2, 0.4, 0.6, 0.8]];
        state.texture_combines[7] = valid;
        assert!(valid_texture_payload(&state));
        for (r, c, value) in [(0, 0, 2.0), (0, 1, 7.5), (0, 2, 6.0),
            (1, 0, 16.0), (2, 0, 7.0), (1, 3, 3.0), (3, 0, -0.1), (3, 2, f32::NAN)] {
            state.texture_combines[7] = valid;
            state.texture_combines[7][r][c] = value;
            assert!(!valid_texture_payload(&state));
        }
        state.texture_combines[7] = valid;
        state.extra_textures[6].enabled = 1;
        assert!(valid_texture_payload(&state));
        state.extra_textures[6].matrix[12] = f32::INFINITY;
        assert!(!valid_texture_payload(&state));
    }

    fn npot_transport(w:u32,h:u32,d:u32,mips:bool)->(CoinWgpuTexture,Vec<u8>) {
        let mut columns=1;while columns*columns<d {columns*=2;}
        let (mut x,mut y,mut z)=(w,h,d);let mut rows=0;
        loop {rows+=y*((z+columns-1)/columns);if !mips||(x==1&&y==1&&z==1){break;}x=(x/2).max(1);y=(y/2).max(1);z=(z/2).max(1);}
        let pixels=vec![0x7bu8;(u64::from(w*columns)*u64::from(rows)*4) as usize];
        let texture=CoinWgpuTexture {width:w*columns,height:rows,format:11,
            reserved:(w-1)|((h-1)<<8)|((d-1)<<16)|if mips {1<<24}else{0},
            content_digest:717,pixels:pixels.as_ptr(),pixel_bytes_len:pixels.len() as u64};
        (texture,pixels)
    }
    #[test]
    fn npot_exact_axis_minus_one_metadata_and_floor_chain_ingress() {
        for (w,h,d) in [(3,5,7),(7,3,5),(5,7,3),(255,1,1),(1,255,1),(1,1,255),(256,256,241),(1,1,1),(2,4,8)] {
            for mips in [false,true] {
                let (t,_pixels)=npot_transport(w,h,d,mips);
                // DECLARED_OPAQUE is an authorial policy, orthogonal to all
                // atlas axes, explicit mip levels, digest and payload bytes.
                let source=t;
                for declaration in [0u32, 1u32<<31] {
                    let t=CoinWgpuTexture{reserved:source.reserved|declaration,..source};
                    assert_eq!(t.pixels,source.pixels);
                    assert_eq!(t.pixel_bytes_len,source.pixel_bytes_len);
                    assert_eq!(t.content_digest,source.content_digest);
                    assert_eq!(spatial_atlas_metadata(&t).unwrap(),Some((w,h,d,mips)));
                    assert_eq!(t.reserved&255,w-1);assert_eq!((t.reserved>>8)&255,h-1);
                    assert_eq!((t.reserved>>16)&255,d-1);assert_eq!((t.reserved>>24)&1,u32::from(mips));
                    let levels=texture_levels(t.width,t.height,t.format).unwrap();
                    assert_eq!(levels.len(),1,"physical atlas must have no hardware mip chain");
                    let mut bad=t;bad.width+=1;assert!(spatial_atlas_metadata(&bad).is_err());
                    bad=t;bad.height+=1;assert!(spatial_atlas_metadata(&bad).is_err());
                    bad=t;bad.width=0;assert!(spatial_atlas_metadata(&bad).is_err());
                    bad=t;bad.height=0;assert!(spatial_atlas_metadata(&bad).is_err());
                    bad=t;bad.pixels=std::ptr::null();assert!(spatial_atlas_metadata(&bad).is_err(),"null atlas payload");
                    bad=t;bad.pixel_bytes_len-=1;assert!(spatial_atlas_metadata(&bad).is_err(),"short atlas payload");
                    bad=t;bad.pixel_bytes_len+=1;assert!(spatial_atlas_metadata(&bad).is_err(),"oversized atlas payload");
                    bad=t;bad.pixel_bytes_len=0;assert!(spatial_atlas_metadata(&bad).is_err(),"empty atlas payload");
                    for bit in 25..31 {bad=t;bad.reserved|=1<<bit;assert!(spatial_atlas_metadata(&bad).is_err(),"reserved bit {bit}");}
                    assert_eq!(spatial_atlas_metadata(&t).unwrap(),Some((w,h,d,mips)),"decoder recovery after malformed metadata");
                    assert_eq!(texture_levels(t.width,t.height,t.format).unwrap(),
                               texture_levels(source.width,source.height,source.format).unwrap(),
                               "declaration leaves physical chain unchanged");
                    assert_eq!(unsafe {std::slice::from_raw_parts(t.pixels,t.pixel_bytes_len as usize)},
                               _pixels.as_slice(),"declaration preserves every raw byte");
                }
            }
        }
    }
    #[test]
    fn npot_metadata_rejects_legacy_log2_and_mip_flag_aliases() {
        let (t,_pixels)=npot_transport(3,5,7,true);
        for reserved in [0x1222,0x111,0x1000,0x02060502,0x80000000,0xffffffff] {
            let bad=CoinWgpuTexture{reserved,..t};assert!(spatial_atlas_metadata(&bad).is_err(),"legacy/malformed {reserved:x}");
        }
        let mut changed=t;changed.reserved ^=1<<24;assert!(spatial_atlas_metadata(&changed).is_err(),"mip bit changes physical extent admission");
        for (w,h,d) in [(256,1,1),(1,256,1),(1,1,256)] {
            let (t,_pixels)=npot_transport(w,h,d,false);assert_eq!(spatial_atlas_metadata(&t).unwrap(),Some((w,h,d,false)));
        }
    }

    #[test]
    fn npot_volume_projective_r_is_checked_for_every_topology_and_texture_unit() {
        let (image,_pixels)=npot_transport(3,5,7,false);
        for topology in [0,1,2] {for unit in [0usize,7] {
            let (mut st,mut vertices)=projective_fixture();st.has_texture=0;
            if unit==0 {st.has_texture=1;}else {st.extra_textures[unit-1].enabled=1;for i in 0..4 {st.extra_textures[unit-1].matrix[i*5]=1.0;}}
            for v in &mut vertices {if unit==0 {v.texcoord=[0.2,0.3,0.9,1.0];}else {v.extra_texcoords[unit-1]=[0.2,0.3,0.9,1.0];}}
            let count=match topology {1=>2,2=>1,_=>3};let indices=&[0u32,1,2][..count];
            assert!(validate_texture_coordinates_with_images(&st,topology,&vertices,indices,&[image]).is_ok());
            let stable=vertices;let coordinate=if unit==0 {&mut vertices[0].texcoord}else{&mut vertices[0].extra_texcoords[unit-1]};coordinate[2]=f32::NAN;
            assert!(validate_texture_coordinates_with_images(&st,topology,&vertices,indices,&[image]).is_err());
            vertices=stable;let coordinate=if unit==0 {&mut vertices[0].texcoord}else{&mut vertices[0].extra_texcoords[unit-1]};coordinate[2]=f32::MAX;coordinate[3]=0.5;
            assert!(validate_texture_coordinates_with_images(&st,topology,&vertices,indices,&[image]).is_err(),"volume R/Q overflow topology {topology} unit {unit}");
            vertices=stable;let coordinate=if unit==0 {&mut vertices[0].texcoord}else{&mut vertices[0].extra_texcoords[unit-1]};coordinate[2]=f32::MAX;
            let matrix=if unit==0 {&mut st.texture_matrix}else{&mut st.extra_textures[unit-1].matrix};matrix[10]=2.0;
            assert!(validate_texture_coordinates_with_images(&st,topology,&vertices,indices,&[image]).is_err(),"volume transformed R overflow topology {topology} unit {unit}");
            if unit==0 {st.texture_matrix[10]=1.0;}else {st.extra_textures[unit-1].matrix[10]=1.0;}
            vertices=stable;let coordinate=if unit==0 {&mut vertices[0].texcoord}else{&mut vertices[0].extra_texcoords[unit-1]};coordinate[3]=0.0;
            assert!(validate_texture_coordinates_with_images(&st,topology,&vertices,indices,&[image]).is_err());
            assert!(validate_texture_coordinates_with_images(&st,topology,&stable,indices,&[image]).is_ok(),"NPOT coordinates recover after invalid payload");
        }}
    }
