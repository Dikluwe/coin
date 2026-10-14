    use super::*;

    #[test]
    fn gpu_material_table_preserves_all_source_bits_and_zero_gpu_padding() {
        assert_eq!(std::mem::size_of::<CoinWgpuMaterial>(), 72);
        assert_eq!(std::mem::size_of::<GpuMaterial>(), 80);
        let mut materials = vec![CoinWgpuMaterial::zeroed(); 4097];
        for (index, material) in materials.iter_mut().enumerate() {
            material.ambient = [index as f32, -0.0, 0.125, 0.25];
            material.diffuse = [0.5, 0.625, 0.75, 1.0];
            material.specular = [0.875, 1.0, -0.0, 0.0];
            material.emission = [0.0, 0.03125, 0.0625, index as f32];
            material.shininess = index as f32 / 4096.0;
            material.transparency = -0.0;
        }
        let packed = pack_gpu_materials(&materials);
        let bytes: &[u8] = bytemuck::cast_slice(&packed);
        assert_eq!(bytes.len(), 4097 * 80);
        for (index, material) in materials.iter().enumerate() {
            let mut expected = [0.0_f32; 20];
            expected[0..4].copy_from_slice(&material.ambient);
            expected[4..8].copy_from_slice(&material.diffuse);
            expected[8..12].copy_from_slice(&material.specular);
            expected[12..16].copy_from_slice(&material.emission);
            expected[16] = material.shininess;
            expected[17] = material.transparency;
            assert_eq!(&bytes[index * 80..(index + 1) * 80], bytemuck::cast_slice(&expected));
        }
        assert!(pack_gpu_materials(&[]).is_empty());
    }
