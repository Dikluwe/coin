    use super::*;
    #[test]
    fn mip_payload_layout_limits() {
        assert_eq!(texture_levels(4,2,2).unwrap(),vec![(4,2,0),(2,1,32),(1,1,40)]);
        assert_eq!(texture_levels(1,1,2).unwrap(),vec![(1,1,0)]);
        assert_eq!(texture_levels(3,5,0).unwrap(),vec![(3,5,0)]);
        for &(w,h,f) in &[(8192,8192,2),(0,8,2),(8193,1,0),(4,4,11)] {
            assert!(texture_levels(w,h,f).is_err());
        }
    }

    #[test]
    fn viewport_defaults_empty_and_bounds() {
        let mut state: CoinWgpuRenderState = unsafe { std::mem::zeroed() };
        assert_eq!(resolved_viewport(&state, 64, 32).unwrap(), [0,0,64,32]);
        state.viewport = [0,0,0,1];
        assert_eq!(resolved_viewport(&state, 64, 32).unwrap(), [0,0,0,1]);
        state.viewport = [2,3,60,29];
        assert_eq!(resolved_viewport(&state, 64, 32).unwrap(), [2,3,60,29]);
        for invalid in [[-1,0,32,32],[0,0,65,32],[0,0,0,2],[0,0,32,-1],
                        [i32::MAX,0,i32::MAX,32]] {
            state.viewport = invalid;
            assert!(resolved_viewport(&state, 64, 32).is_err());
        }
    }
