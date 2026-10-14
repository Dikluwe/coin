    use super::copy_color_rows;

    #[test]
    fn contiguous_rows_copy_exactly() {
        let src: Vec<u8> = (0..512).map(|n| (n % 251) as u8).collect();
        let mut dst = vec![0; 512];
        copy_color_rows(&mut dst, &src, 64, 2, 256);
        assert_eq!(dst, src);
    }

    #[test]
    fn padded_rows_skip_padding() {
        let mut src = vec![0xee; 1024];
        for row in 0..2 {
            for col in 0..260 {
                src[row * 512 + col] = ((row + col) % 251) as u8;
            }
        }
        let mut dst = vec![0; 520];
        copy_color_rows(&mut dst, &src, 65, 2, 512);
        for row in 0..2 {
            assert_eq!(&dst[row * 260..(row + 1) * 260],
                &src[row * 512..row * 512 + 260]);
        }
    }
