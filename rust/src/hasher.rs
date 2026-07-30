//! [`Hasher`]/[`BuildHasher`] glue so hayahash64 can back the
//! standard hash-map and hash-set types.
//!
//! Compiled with the `std` feature (default) or the `hashbrown`
//! feature. The map/set aliases sit on `std::collections` by default
//! and on hashbrown when its feature is enabled; hashbrown takes
//! precedence when both are on, so enabling it behaves the same on
//! std and no_std builds.

use alloc::vec::Vec;
use core::hash::{BuildHasher, Hasher};

#[cfg(feature = "hashbrown")]
use hashbrown::{HashMap as BaseHashMap, HashSet as BaseHashSet};
#[cfg(all(feature = "std", not(feature = "hashbrown")))]
use std::collections::{HashMap as BaseHashMap, HashSet as BaseHashSet};

use crate::hayahash64;

/// Buffering [`Hasher`] and [`BuildHasher`] producing [`hayahash64`]
/// digests with a fixed seed.
///
/// hayahash64 premixes the total input length into its state before
/// absorbing any bytes, so it cannot hash a stream incrementally.
/// Instead, [`write`](Hasher::write) appends to an internal buffer and
/// [`finish`](Hasher::finish) hashes the buffered bytes, so the digest
/// always equals [`hayahash64`] of the concatenated writes. This
/// costs one small heap allocation per hashed key; when hashing raw
/// byte strings, calling [`hayahash64`] directly is cheaper.
///
/// The [`BuildHasher`] implementation makes the type double as a
/// hash-map hasher builder: built hashers start empty with the same
/// seed. [`Default`] uses seed 0.
///
/// Note that digests observed through the [`Hasher`] trait depend on
/// how a type implements [`Hash`](core::hash::Hash) (integers, for
/// example, hash their native-endian bytes), so unlike raw
/// [`hayahash64`] calls they are not guaranteed to be identical
/// across platforms.
///
/// ```
/// use core::hash::{BuildHasher, Hasher};
/// use hayahash::{hayahash64, HayaHasher};
///
/// // As a Hasher: digest equals the one-shot function.
/// let mut h = HayaHasher::new(7);
/// h.write(b"hello ");
/// h.write(b"world");
/// assert_eq!(h.finish(), hayahash64(b"hello world", 7));
///
/// // As a BuildHasher, seeding is deterministic.
/// let b = HayaHasher::new(7);
/// assert_eq!(b.hash_one("key"), HayaHasher::new(7).hash_one("key"));
/// ```
#[derive(Debug, Clone, Default)]
pub struct HayaHasher {
    seed: u64,
    buffer: Vec<u8>,
}

impl HayaHasher {
    /// Creates an empty hasher (and hasher builder) with the given
    /// seed.
    #[must_use]
    pub const fn new(seed: u64) -> Self {
        Self {
            seed,
            buffer: Vec::new(),
        }
    }
}

impl Hasher for HayaHasher {
    #[inline]
    fn finish(&self) -> u64 {
        hayahash64(&self.buffer, self.seed)
    }

    #[inline]
    fn write(&mut self, bytes: &[u8]) {
        self.buffer.extend_from_slice(bytes);
    }
}

impl BuildHasher for HayaHasher {
    type Hasher = HayaHasher;

    #[inline]
    fn build_hasher(&self) -> HayaHasher {
        HayaHasher::new(self.seed)
    }
}

/// A `HashMap` hashing its keys with hayahash64.
///
/// Backed by `std::collections::HashMap`, or by `hashbrown::HashMap`
/// when the `hashbrown` feature is enabled.
///
/// ```
/// use hayahash::{HayaHashMap, HayaHasher};
///
/// let mut map: HayaHashMap<String, i32> = HayaHashMap::default();
/// map.insert("hello".to_string(), 42);
/// assert_eq!(map.get("hello"), Some(&42));
///
/// // Or with an explicit seed:
/// let mut map = HayaHashMap::with_hasher(HayaHasher::new(42));
/// map.insert("hello".to_string(), 1);
/// assert_eq!(map.get("hello"), Some(&1));
/// ```
pub type HayaHashMap<K, V> = BaseHashMap<K, V, HayaHasher>;

/// A `HashSet` hashing its items with hayahash64.
///
/// Backed by `std::collections::HashSet`, or by `hashbrown::HashSet`
/// when the `hashbrown` feature is enabled.
///
/// ```
/// use hayahash::HayaHashSet;
///
/// let mut set: HayaHashSet<&str> = HayaHashSet::default();
/// set.insert("hello");
/// assert!(set.contains("hello"));
/// ```
pub type HayaHashSet<T> = BaseHashSet<T, HayaHasher>;
