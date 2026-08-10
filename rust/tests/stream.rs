//! Streaming conformance: every split of an input must produce the
//! one-shot digest, and finalizing must not disturb the state.

use hayahash::{hayahash128, hayahash64, Digest};

const K: u64 = 0x9E37_79B9_7F4A_7C15;

/// The shared portable input fill used by the KAT tables and
/// `test_vectors/`.
fn pattern_a(n: usize) -> Vec<u8> {
    (0..n)
        .map(|i| {
            (((i as u64)
                .wrapping_mul(K)
                .wrapping_add(0x2545_F491_4F6C_DD1D))
                >> 56) as u8
        })
        .collect()
}

/// Chunk sizes chosen to straddle the 448-byte buffer, the 128-byte
/// keep floor and the 64-byte block.
fn split_sizes(i: usize, remaining: usize) -> [(&'static str, usize); 8] {
    [
        ("bytewise", 1),
        ("7", 7),
        ("64", 64),
        ("127", 127),
        ("448", 448),
        ("449", 449),
        ("whole", remaining.max(1)),
        ("varying", 1 + (i * 31 + 7) % 193),
    ]
}

fn feed(d: &mut Digest, input: &[u8], pattern: usize) {
    let mut rest = input;
    let mut i = 0usize;
    while !rest.is_empty() {
        let n = split_sizes(i, rest.len())[pattern].1.min(rest.len());
        d.update(&rest[..n]);
        rest = &rest[n..];
        i += 1;
    }
}

#[test]
fn streaming_matches_one_shot() {
    let seeds = [0u64, K, 0xDEAD_BEEF_CAFE_BABE];
    let mut lengths: Vec<usize> = (0..=640).collect();
    lengths.extend_from_slice(&[
        895, 896, 897, 1023, 1024, 1025, 1343, 1344, 1345, 4095, 4096, 4097, 20000, 65536, 131073,
    ]);

    for &seed in &seeds {
        for &n in &lengths {
            let input = pattern_a(n);
            let want64 = hayahash64(&input, seed);
            let want128 = hayahash128(&input, seed);
            for pattern in 0..8 {
                let name = split_sizes(0, 1)[pattern].0;
                let mut d = Digest::new(seed);
                feed(&mut d, &input, pattern);
                assert_eq!(
                    d.finish64(),
                    want64,
                    "finish64 len={n} seed={seed:#x} split={name}"
                );
                let got = d.finish128();
                assert_eq!(
                    got, want128,
                    "finish128 len={n} seed={seed:#x} split={name}"
                );
                assert_eq!(got.lo, want64, "lo != finish64 len={n} split={name}");
            }
        }
    }
}

#[test]
fn digest_is_non_destructive() {
    let total = 2000;
    let input = pattern_a(total);
    for cut in [0usize, 1, 63, 64, 447, 448, 449, 1000, total] {
        let mut d = Digest::new(7);
        d.update(&input[..cut]);
        let first = d.finish64();
        assert_eq!(
            first,
            d.finish64(),
            "repeated finish64 differs at cut={cut}"
        );
        assert_eq!(first, hayahash64(&input[..cut], 7), "cut={cut}");
        assert_eq!(d.finish128().lo, first, "finish128 disagrees at cut={cut}");
        // Continue absorbing from the same state.
        d.update(&input[cut..]);
        assert_eq!(
            d.finish64(),
            hayahash64(&input, 7),
            "continued at cut={cut}"
        );
    }
}

#[test]
fn empty_and_zero_length_updates() {
    let mut d = Digest::new(0);
    assert!(d.is_empty());
    assert_eq!(d.finish64(), hayahash64(b"", 0));
    d.update(b"");
    assert_eq!(d.finish64(), hayahash64(b"", 0));
    assert!(d.is_empty());

    let input = pattern_a(500);
    d.update(&input[..200]);
    d.update(b"");
    d.update(&input[200..]);
    assert_eq!(d.finish64(), hayahash64(&input, 0));
    assert_eq!(d.len(), 500);
    assert!(!d.is_empty());
}

#[test]
fn reset_restarts_with_new_seed() {
    let input = pattern_a(1000);
    let mut d = Digest::new(0xABCD);
    d.update(&input);
    d.reset(1);
    d.update(&input[..10]);
    assert_eq!(d.finish64(), hayahash64(&input[..10], 1));
}

/// Pins the "streaming equality samples" section of
/// `test_vectors/v0.5.0.txt`, which until now no port consumed.
/// pattern_a input, seed 0, absorbed one byte at a time.
#[test]
fn published_streaming_vectors() {
    const VECTORS: [(usize, u64); 9] = [
        (0, 0x68AC_507C_F298_CA3F),
        (5, 0x37EE_1F8B_5A98_B84B),
        (10, 0xE28B_66FB_1E4C_B4EA),
        (15, 0x9A89_20A5_7F11_9D6B),
        (20, 0xC311_E14F_F31F_B2BF),
        (25, 0xC27F_DE4A_C86C_CE54),
        (30, 0x16CC_1E65_CA2C_B4F3),
        (35, 0x1C65_22BD_C246_DA12),
        (40, 0xD110_128D_567C_B9F8),
    ];
    for (len, want) in VECTORS {
        let mut d = Digest::new(0);
        for b in pattern_a(len) {
            d.update(&[b]);
        }
        assert_eq!(d.finish64(), want, "len={len} bytewise");
    }
}
