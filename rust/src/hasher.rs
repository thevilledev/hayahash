//! [`Hasher`]/[`BuildHasher`] glue so hayahash64 can back the
//! standard hash-map and hash-set types.
//!
//! Compiled with the `std` feature (default) or the `hashbrown`
//! feature. The map/set aliases sit on `std::collections` by default
//! and on hashbrown when its feature is enabled; hashbrown takes
//! precedence when both are on, so enabling it behaves the same on
//! std and no_std builds.

use alloc::vec::Vec;
use core::fmt;
use core::hash::{BuildHasher, Hasher};

#[cfg(feature = "hashbrown")]
use hashbrown::{HashMap as BaseHashMap, HashSet as BaseHashSet};
#[cfg(all(feature = "std", not(feature = "hashbrown")))]
use std::collections::{HashMap as BaseHashMap, HashSet as BaseHashSet};

use crate::hayahash64;

/// Inline write buffer capacity. Typical `HashMap` keys stay within
/// this and never touch the heap; larger concatenated writes spill.
///
/// Kept small on purpose: the hasher doubles as the [`BuildHasher`]
/// that maps store inline, and larger buffers measured slower per
/// lookup without covering meaningfully more real keys.
const INLINE_CAP: usize = 64;

/// Buffering [`Hasher`] and [`BuildHasher`] producing [`hayahash64`]
/// digests with a fixed seed.
///
/// hayahash64 premixes the total input length into its state before
/// absorbing any bytes, so it cannot hash a stream incrementally.
/// Instead, [`write`](Hasher::write) appends to an internal buffer and
/// [`finish`](Hasher::finish) hashes the buffered bytes, so the digest
/// always equals [`hayahash64`] of the concatenated writes.
///
/// Writes are buffered inline up to 64 bytes, so ordinary hash-map
/// keys do not allocate. Only larger concatenated inputs spill to the
/// heap. When hashing raw byte strings, calling [`hayahash64`]
/// directly is still cheaper.
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
#[derive(Clone)]
pub struct HayaHasher {
    seed: u64,
    len: usize,
    inline: [u8; INLINE_CAP],
    heap: Vec<u8>,
}

impl HayaHasher {
    /// Creates an empty hasher (and hasher builder) with the given
    /// seed.
    #[must_use]
    pub const fn new(seed: u64) -> Self {
        Self {
            seed,
            len: 0,
            inline: [0; INLINE_CAP],
            heap: Vec::new(),
        }
    }

    #[inline]
    fn buffer(&self) -> &[u8] {
        if self.heap.is_empty() {
            &self.inline[..self.len]
        } else {
            &self.heap
        }
    }
}

impl Default for HayaHasher {
    #[inline]
    fn default() -> Self {
        Self::new(0)
    }
}

impl fmt::Debug for HayaHasher {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("HayaHasher")
            .field("seed", &self.seed)
            .field("buffer", &self.buffer())
            .finish()
    }
}

impl Hasher for HayaHasher {
    #[inline]
    fn finish(&self) -> u64 {
        hayahash64(self.buffer(), self.seed)
    }

    #[inline]
    fn write(&mut self, bytes: &[u8]) {
        if bytes.is_empty() {
            return;
        }
        if !self.heap.is_empty() {
            self.heap.extend_from_slice(bytes);
            return;
        }
        let new_len = self.len + bytes.len();
        if new_len <= INLINE_CAP {
            self.inline[self.len..new_len].copy_from_slice(bytes);
            self.len = new_len;
            return;
        }
        self.heap.reserve(new_len);
        self.heap.extend_from_slice(&self.inline[..self.len]);
        self.heap.extend_from_slice(bytes);
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
