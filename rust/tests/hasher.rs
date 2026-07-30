//! Behavioral tests for the `Hasher`/`BuildHasher` glue and the
//! map/set aliases. Digest conformance against the C reference lives
//! in `kat.rs`; these tests only need `hayahash64` as the oracle.
#![cfg(any(feature = "std", feature = "hashbrown"))]

use core::hash::{BuildHasher, Hasher};

use hayahash::{hayahash64, HayaHashMap, HayaHashSet, HayaHasher};

#[test]
fn hasher_matches_one_shot_across_split_writes() {
    let data = b"the quick brown fox jumps over the lazy dog";
    for seed in [0u64, 1, 0x9E37_79B9_7F4A_7C15, u64::MAX] {
        let want = hayahash64(data, seed);
        for split in [0, 1, 7, 8, 16, data.len()] {
            let mut h = HayaHasher::new(seed);
            h.write(&data[..split]);
            h.write(&data[split..]);
            assert_eq!(h.finish(), want, "seed={seed:#018X} split={split}");
        }
    }
}

#[test]
fn empty_hasher_matches_empty_input() {
    assert_eq!(HayaHasher::new(3).finish(), hayahash64(b"", 3));
    assert_eq!(HayaHasher::default().finish(), hayahash64(b"", 0));
}

#[test]
fn finish_does_not_consume_buffered_bytes() {
    let mut h = HayaHasher::new(9);
    h.write(b"abc");
    let first = h.finish();
    assert_eq!(h.finish(), first);
    h.write(b"def");
    assert_eq!(h.finish(), hayahash64(b"abcdef", 9));
}

#[test]
fn seed_changes_output() {
    let mut a = HayaHasher::new(0);
    let mut b = HayaHasher::new(1);
    a.write(b"same bytes");
    b.write(b"same bytes");
    assert_ne!(a.finish(), b.finish());
}

#[test]
fn build_hasher_is_seeded_and_starts_empty() {
    let builder = HayaHasher::new(42);
    assert_eq!(builder.hash_one("hello"), builder.hash_one("hello"));
    assert_eq!(
        builder.hash_one("hello"),
        HayaHasher::new(42).hash_one("hello")
    );
    assert_ne!(
        builder.hash_one("hello"),
        HayaHasher::new(43).hash_one("hello")
    );
    // Bytes written to the builder itself must not leak into the
    // hashers it builds.
    let mut dirty = HayaHasher::new(42);
    dirty.write(b"leftover");
    assert_eq!(dirty.hash_one("hello"), builder.hash_one("hello"));
}

#[test]
fn map_basics() {
    let mut map: HayaHashMap<String, i32> = HayaHashMap::default();
    map.insert("hello".to_string(), 42);
    assert_eq!(map.get("hello"), Some(&42));
    assert_eq!(map.remove("hello"), Some(42));
    assert!(map.is_empty());
}

#[test]
fn map_survives_growth_and_rehashing() {
    let mut map = HayaHashMap::with_hasher(HayaHasher::new(42));
    for i in 0..1000u32 {
        map.insert(i.to_string(), i);
    }
    assert_eq!(map.len(), 1000);
    for i in 0..1000u32 {
        assert_eq!(map.get(&i.to_string()), Some(&i), "key {i}");
    }
}

#[test]
fn set_basics() {
    let mut set: HayaHashSet<String> = HayaHashSet::default();
    assert!(set.insert("hello".to_string()));
    assert!(!set.insert("hello".to_string()));
    assert!(set.contains("hello"));
    assert!(set.remove("hello"));
    assert!(set.is_empty());
}
