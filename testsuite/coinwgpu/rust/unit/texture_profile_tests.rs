    use super::*;
    #[test]
    fn npot_complete_chain() {
        assert_eq!(
            levels(3, 5, 2).unwrap(),
            vec![(3, 5, 0), (1, 2, 60), (1, 1, 68)]
        );
        assert_eq!(
            levels(4, 2, 2).unwrap(),
            vec![(4, 2, 0), (2, 1, 32), (1, 1, 40)]
        );
    }
    #[test]
    fn format_layouts_and_alignment() {
        assert_eq!(
            levels(4, 4, 8).unwrap(),
            vec![(4, 4, 0), (2, 2, 16), (1, 1, 32)]
        );
        assert_eq!(levels(1, 1, 5).unwrap(), vec![(1, 1, 0)]);
        assert_eq!(level_bytes(1, 1, wgpu::TextureFormat::Rgba16Float), 8);
        assert!(levels(3, 4, 7).is_err());
        assert!(levels(0, 1, 0).is_err());
        assert!(levels(8192, 8192, 5).is_err());
        let (atlas_format, _) = format(11).expect("format 11 is a physical RGBA8 volume atlas");
        assert_eq!(atlas_format, wgpu::TextureFormat::Rgba8Unorm);
        assert_eq!(levels(4, 4, 11).unwrap(), vec![(4, 4, 0)]);
        assert_eq!(levels(8, 12, 11).unwrap(), vec![(8, 12, 0)]);
        assert_eq!(level_bytes(8, 12, atlas_format), 8 * 12 * 4);
        let (cube_format, _) = format(12).expect("format 12 is a physical RGBA8 cube atlas");
        assert_eq!(cube_format, wgpu::TextureFormat::Rgba8Unorm);
        assert_eq!(levels(8, 16, 12).unwrap(), vec![(8, 16, 0)]);
        assert_eq!(level_bytes(8, 16, cube_format), 8 * 16 * 4);
        assert!(format(13).is_err());
    }
    #[test]
    fn anisotropy_is_explicit_and_validated() {
        for n in [1, 2, 4, 8, 16] {
            assert_eq!(anisotropy(n - 1, 3).unwrap(), n as u16);
        }
        for n in [0u32, 3, 32] {
            assert!(anisotropy(n.wrapping_sub(1), 3).is_err());
        }
        assert!(anisotropy(15, 2).is_err());
    }
