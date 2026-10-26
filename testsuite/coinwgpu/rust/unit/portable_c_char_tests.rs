use super::*;

    #[test]
    fn slice_accepts_empty_null_and_valid_data() {
        assert!(slice::<u32>(std::ptr::null(), 0, "items").unwrap().is_empty());
        let values = [17u32, 29];
        assert_eq!(slice(values.as_ptr(), 2, "items").unwrap(), &values);
    }

    #[test]
    fn slice_preserves_invalid_pointer_and_size_errors() {
        let (status, message) = slice::<u32>(std::ptr::null(), 1, "items").unwrap_err();
        assert_eq!(status, CoinWgpuStatus::InvalidArgument);
        assert_eq!(message, "Null items pointer with non-zero count");
        let (status, message) = slice::<u32>(1usize as *const u32, 1, "items").unwrap_err();
        assert_eq!(status, CoinWgpuStatus::InvalidArgument);
        assert!(message.starts_with("Misaligned items pointer:"));
        let value = 0u64;
        let (status, message) = slice(&value, u64::MAX, "items").unwrap_err();
        assert_eq!(status, CoinWgpuStatus::InvalidArgument);
        assert!(message.contains("items"));
        assert!(message.contains("overflow") || message.contains("range"));
    }

    #[test]
    fn slice_decodes_utf8_and_terminates_truncated_errors() {
        let (_, message) = slice::<u8>(std::ptr::null(), 1, "prédios").unwrap_err();
        assert_eq!(message, "Null prédios pointer with non-zero count");
        let (_, message) = slice::<u8>(std::ptr::null(), 1, &"x".repeat(600)).unwrap_err();
        assert_eq!(message.len(), 511);
        assert_eq!(message, format!("Null {}", "x".repeat(506)));
    }

    // Each packet selects its own behavior, so tests can share the single installed
    // callback without a mutable global mode or dependence on test scheduling.
    unsafe extern "C" fn core_fixture(
        packet: *const CoinWgpuPortableModule,
        _primary: *const CoinWgpuPortableRange,
        _coordinates: *const CoinWgpuPortableRange,
        _samples: *const CoinWgpuPortableRange,
        _projection: u32,
        output: *mut c_char,
        output_len: u64,
        required: *mut u64,
        error: *mut c_char,
        error_len: u64,
    ) -> CoinWgpuStatus {
        match (*packet).output {
            0 => {
                *required = 4;
                if output_len >= 4 {
                    std::ptr::copy_nonoverlapping(b"ok!\0".as_ptr(), output.cast::<u8>(), 4);
                }
                CoinWgpuStatus::Ok
            }
            1 => {
                set_error(error, error_len as usize, "Falha: prédios inválidos");
                CoinWgpuStatus::InvalidArgument
            }
            2 => {
                set_error(error, error_len as usize, &"x".repeat(700));
                CoinWgpuStatus::BackendError
            }
            3 => CoinWgpuStatus::Unsupported,
            4 => { *required = 0; CoinWgpuStatus::Ok }
            5 => { *required = FUNCTION_BYTES + 2; CoinWgpuStatus::Ok }
            6 => { *required = FUNCTION_BYTES + 1; CoinWgpuStatus::Ok }
            _ => unreachable!(),
        }
    }

    fn packet(mode: u32) -> CoinWgpuPortableModule {
        assert_eq!(coin_wgpu_install_portable_core_v2(Some(core_fixture)), CoinWgpuStatus::Ok);
        CoinWgpuPortableModule {
            nodes: std::ptr::null(), node_count: 0,
            parameters: std::ptr::null(), parameter_count: 0,
            output: mode, alpha_may_vary: 0,
            signature: std::ptr::null(), signature_bytes: 0,
        }
    }

    #[test]
    fn call_preserves_success_size_and_output() {
        assert_eq!(call(&packet(0), None, &mut []).unwrap(), 4);
        let mut output = [0xff; 8];
        assert_eq!(call(&packet(0), None, &mut output).unwrap(), 4);
        assert_eq!(&output[..4], b"ok!\0");
        assert_eq!(&output[4..], &[0xff; 4]);
        assert_eq!(call(&packet(6), None, &mut []).unwrap(), (FUNCTION_BYTES + 1) as usize);
    }

    #[test]
    fn call_preserves_utf8_error_status_and_empty_diagnostic() {
        let (status, message) = call(&packet(1), None, &mut []).unwrap_err();
        assert_eq!(status, CoinWgpuStatus::InvalidArgument);
        assert_eq!(message, "Falha: prédios inválidos");
        let (status, message) = call(&packet(3), None, &mut []).unwrap_err();
        assert_eq!(status, CoinWgpuStatus::Unsupported);
        assert_eq!(message, "");
    }

    #[test]
    fn call_preserves_terminated_error_at_buffer_limit() {
        let (status, message) = call(&packet(2), None, &mut []).unwrap_err();
        assert_eq!(status, CoinWgpuStatus::BackendError);
        assert_eq!(message, "x".repeat(511));
    }

    #[test]
    fn call_rejects_zero_and_over_budget_source_lengths() {
        for mode in [4, 5] {
            let (status, message) = call(&packet(mode), None, &mut []).unwrap_err();
            assert_eq!(status, CoinWgpuStatus::InvalidArgument);
            assert_eq!(message, "Portable shader Core function exceeds 262144 byte source budget or has invalid length");
        }
    }
