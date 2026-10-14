#[test]
fn rtt_capacity_has_an_exact_saturating_boundary() {
    assert_eq!(rtt_remaining_capacity(0), 64);
    assert_eq!(rtt_remaining_capacity(63), 1);
    assert_eq!(rtt_remaining_capacity(64), 0);
    assert_eq!(rtt_remaining_capacity(65), 0);
}
