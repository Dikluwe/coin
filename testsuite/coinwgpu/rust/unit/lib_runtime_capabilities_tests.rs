    use super::*;
    #[test]
    fn rejects_invalid_output_without_a_probe() {
        assert_eq!(
            coin_wgpu_query_runtime_capabilities(std::ptr::null_mut(), 56),
            CoinWgpuStatus::InvalidArgument
        );
        let mut caps = CoinWgpuRuntimeCapabilities::default();
        assert_eq!(
            coin_wgpu_query_runtime_capabilities(&mut caps, 55),
            CoinWgpuStatus::InvalidArgument
        );
        assert_eq!(caps.struct_size, 0);
    }
    #[test]
    fn format_bits_keep_sampling_and_storage_access_separate() {
        let features = wgpu::TextureFormatFeatures {
            allowed_usages: wgpu::TextureUsages::RENDER_ATTACHMENT
                | wgpu::TextureUsages::STORAGE_BINDING,
            flags: wgpu::TextureFormatFeatureFlags::MULTISAMPLE_X4
                | wgpu::TextureFormatFeatureFlags::STORAGE_READ_ONLY,
        };
        assert_eq!(portable_format_features(features), 2 | 4 | 8);
    }
