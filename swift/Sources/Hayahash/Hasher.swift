/*
 * Incremental hashing: absorb input in pieces, digest at any point.
 *
 * This is free and unencumbered software released into the public
 * domain. For more information, please refer to https://unlicense.org/
 */

import Foundation

extension Hayahash {
    /// A streaming hayahash state.
    ///
    /// The digest equals `Hayahash.hash64` or `Hayahash.hash128` over the
    /// concatenation of everything passed to ``update(_:)``, for every
    /// split of that input:
    ///
    /// ```swift
    /// var h = Hayahash.Hasher(seed: 7)
    /// h.update(Array("hello ".utf8))
    /// h.update(Array("world".utf8))
    /// let digest = h.digest64()
    /// ```
    ///
    /// Digesting does not consume the state, so absorbing may continue
    /// afterwards. This is a value type: copying a hasher forks its
    /// state, which is how you fork a common prefix.
    ///
    /// It is nested under ``Hayahash`` rather than declared at the top
    /// level so it cannot collide with the standard library's `Hasher`
    /// in code that also writes `Hashable` conformances by hand.
    public struct Hasher: Sendable {
        /// Streaming buffer size. Totals below it stay buffered so short
        /// and mid inputs take the one-shot dispatch at digest time,
        /// exactly as the C reference does.
        private static let bufCap = 448

        /// The floor the buffer is drained to. The digest-time mid/tail
        /// phase reaches back up to 16 bytes before the current pointer,
        /// so the buffer has to retain more than that.
        private static let keep = 128

        /// The nine words the absorb loop carries: the eight lanes plus
        /// the previous stripe. Passed and returned by value so
        /// absorbing out of `buf` never needs a second access to `self`.
        private struct Lanes: Sendable {
            var h0: UInt64
            var h1: UInt64
            var h2: UInt64
            var h3: UInt64
            var h4: UInt64
            var h5: UInt64
            var h6: UInt64
            var h7: UInt64
            var wp: UInt64
        }

        private var lanes: Lanes
        private var buf: [UInt8]
        private var seedValue: UInt64
        private var total: UInt64
        private var nbuf: Int
        private var bulk: Bool

        /// Creates an empty state seeded with `seed`.
        public init(seed: UInt64 = 0) {
            let k = Hayahash.k
            let s = seed ^ k
            lanes = Lanes(
                h0: s ^ k,
                h1: Hayahash.rotl(s, 17) &+ (k << 21),
                h2: Hayahash.rotl(s, 34) ^ (k >> 13),
                h3: Hayahash.rotl(s, 51) &+ (k << 42),
                h4: s &+ (k >> 27),
                h5: Hayahash.rotl(s, 13) ^ (k << 9),
                h6: Hayahash.rotl(s, 26) &+ (k >> 40),
                h7: Hayahash.rotl(s, 39) ^ (k << 30),
                wp: 0
            )
            buf = [UInt8](repeating: 0, count: Self.bufCap)
            seedValue = seed
            total = 0
            nbuf = 0
            bulk = false
        }

        /// The seed this state was created or last reset with.
        public var seed: UInt64 { seedValue }

        /// Number of bytes absorbed so far.
        public var length: UInt64 { total }

        /// Discards absorbed input and restarts with a new seed.
        public mutating func reset(seed: UInt64) {
            self = Hasher(seed: seed)
        }

        /// Discards absorbed input, keeping the current seed.
        public mutating func reset() {
            reset(seed: seedValue)
        }

        /// Absorbs `data`. Any contiguous byte source works: `[UInt8]`,
        /// `Data`, or an `UnsafeRawBufferPointer`.
        public mutating func update(_ data: some ContiguousBytes) {
            data.withUnsafeBytes { self.absorb($0) }
        }

        private mutating func absorb(_ data: UnsafeRawBufferPointer) {
            if data.isEmpty {
                return
            }
            total &+= UInt64(data.count)

            var off = 0
            var remaining = data.count

            if !bulk {
                // Undecided between the one-shot finish and the bulk
                // path: totals up to bufCap-1 stay buffered.
                if nbuf + remaining < Self.bufCap {
                    // `at:` is read into a local first: passing a
                    // property of `self` to a mutating method of `self`
                    // is an overlapping access.
                    let at = nbuf
                    copyIn(data, off, remaining, at: at)
                    nbuf += remaining
                    return
                }
                // Total is now >= 448 > bulkMin: commit to the bulk path.
                bulk = true
            }

            while true {
                // Buffer at its floor with plenty incoming: drain the
                // floor, then stream whole blocks straight from the
                // caller's bytes, leaving a [keep, keep+63]-byte
                // remainder for the buffer.
                if nbuf == Self.keep && remaining > Self.bufCap {
                    let direct = (remaining - Self.keep) & ~63
                    absorbBuffered(Self.keep)
                    lanes = Self.absorbBlocks(data, off, direct, lanes)
                    off += direct
                    remaining -= direct
                    nbuf = 0
                }
                let at = nbuf
                let take = min(Self.bufCap - at, remaining)
                copyIn(data, off, take, at: at)
                nbuf += take
                off += take
                remaining -= take
                if nbuf < Self.bufCap {
                    break
                }
                // Buffer full: consume whole blocks down to the keep
                // floor and shift the remainder to the front.
                let consume = (nbuf - Self.keep) & ~63
                absorbBuffered(consume)
                nbuf -= consume
                for i in 0..<nbuf {
                    let byte = buf[consume &+ i]
                    buf[i] = byte
                }
            }
        }

        /// Copies `count` bytes at `from` in `data` to offset `dst` of
        /// the internal buffer.
        private mutating func copyIn(
            _ data: UnsafeRawBufferPointer, _ from: Int, _ count: Int, at dst: Int
        ) {
            if count == 0 {
                return
            }
            let src = UnsafeRawBufferPointer(rebasing: data[from..<(from + count)])
            buf.withUnsafeMutableBytes { b in
                UnsafeMutableRawBufferPointer(rebasing: b[dst..<(dst + count)])
                    .copyMemory(from: src)
            }
        }

        /// Absorbs `count` bytes from the front of the internal buffer.
        /// `count` must be a multiple of 64.
        private mutating func absorbBuffered(_ count: Int) {
            let bytes = buf
            let start = lanes
            var next = start
            bytes.withUnsafeBytes { p in
                next = Self.absorbBlocks(p, 0, count, start)
            }
            lanes = next
        }

        /// The 8-lane bulk loop over `count` bytes at `off`, which must
        /// be a multiple of 64.
        ///
        /// Static, taking and returning the lane state by value, so a
        /// caller can absorb out of its own `buf` without a second,
        /// overlapping access to `self`.
        private static func absorbBlocks(
            _ p: UnsafeRawBufferPointer, _ off: Int, _ count: Int, _ st: Lanes
        ) -> Lanes {
            let k = Hayahash.k
            var h0 = st.h0
            var h1 = st.h1
            var h2 = st.h2
            var h3 = st.h3
            var h4 = st.h4
            var h5 = st.h5
            var h6 = st.h6
            var h7 = st.h7
            var wp = st.wp
            var i = off
            let end = off &+ count
            while i < end {
                var w = Hayahash.load64(p, i)
                h0 = (h0 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                wp = w
                w = Hayahash.load64(p, i &+ 8)
                h1 = (h1 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                wp = w
                w = Hayahash.load64(p, i &+ 16)
                h2 = (h2 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                wp = w
                w = Hayahash.load64(p, i &+ 24)
                h3 = (h3 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                wp = w
                w = Hayahash.load64(p, i &+ 32)
                h4 = (h4 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                wp = w
                w = Hayahash.load64(p, i &+ 40)
                h5 = (h5 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                wp = w
                w = Hayahash.load64(p, i &+ 48)
                h6 = (h6 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                wp = w
                w = Hayahash.load64(p, i &+ 56)
                h7 = (h7 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                wp = w
                // Checkpoint the raw-word chain once per block so a
                // 64-stripe rotation orbit cannot hide a difference
                // until it returns to the same lane.
                h0 = h0 &+ wp
                i &+= 64
            }
            return Lanes(
                h0: h0, h1: h1, h2: h2, h3: h3,
                h4: h4, h5: h5, h6: h6, h7: h7, wp: wp
            )
        }

        /// Returns the 64-bit digest of everything absorbed so far,
        /// without consuming the state.
        public func digest64() -> UInt64 {
            if !bulk {
                return Hayahash.hash64(buf, offset: 0, length: nbuf, seed: seedValue)
            }
            let t = finish()
            var x = (seedValue ^ Hayahash.k) ^ t.0 ^ Hayahash.rotl(t.1, 29)
            x ^= x >> 37
            x &*= Hayahash.k
            return x ^ (x >> 32)
        }

        /// Returns both digest words for everything absorbed so far,
        /// without consuming the state. `lo` is exactly ``digest64()``.
        public func digest128() -> Hash128 {
            if !bulk {
                return Hayahash.hash128(buf, offset: 0, length: nbuf, seed: seedValue)
            }
            let t = finish()
            let s = seedValue ^ Hayahash.k
            var x = s ^ t.0 ^ Hayahash.rotl(t.1, 29)
            x ^= x >> 37
            x &*= Hayahash.k
            return Hash128(
                lo: x ^ (x >> 32),
                hi: Hayahash.fmix128(
                    Hayahash.rotl(s, 32) ^ (t.1 &+ Hayahash.rotl(t.0, 47))
                )
            )
        }

        /// Continues the long path over the buffered remainder: the
        /// leftover whole blocks, then the same fold, mid round, wall
        /// and tail as the one-shot. Reads the state without mutating it.
        private func finish() -> (UInt64, UInt64) {
            let k = Hayahash.k
            let count = nbuf
            let lenmix = total &* k
            let start = lanes
            let bytes = buf
            var t0: UInt64 = 0
            var t1: UInt64 = 0

            bytes.withUnsafeBytes { p in
                let whole = count & ~63
                let st = Self.absorbBlocks(p, 0, whole, start)
                var wp = st.wp
                var off = whole
                var l = count - whole

                // Fold the upper lanes in, exactly as the one-shot does
                // when it leaves the bulk loop.
                var h0 = (st.h0 ^ Hayahash.rotl(st.h4, 11)) &* k
                var h1 = (st.h1 ^ Hayahash.rotl(st.h5, 19)) &* k
                var h2 = (st.h2 ^ Hayahash.rotl(st.h6, 31)) &* k
                var h3 = (st.h3 ^ Hayahash.rotl(st.h7, 47)) &* k

                // 0..63 bytes are left, so at most one mid round runs.
                if l >= 32 {
                    var w = Hayahash.load64(p, off)
                    h0 = (h0 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                    wp = w
                    w = Hayahash.load64(p, off &+ 8)
                    h1 = (h1 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                    wp = w
                    w = Hayahash.load64(p, off &+ 16)
                    h2 = (h2 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                    wp = w
                    w = Hayahash.load64(p, off &+ 24)
                    h3 = (h3 ^ (w &+ Hayahash.rotl(wp, 27))) &* k
                    wp = w
                    off &+= 32
                    l -= 32
                }

                h0 = h0 &+ Hayahash.rotl(wp, 27)
                if l > 16 {
                    h0 = (h0 &+ Hayahash.inj(Hayahash.load64(p, off))) &* k
                    h1 = (h1 &+ Hayahash.inj(Hayahash.load64(p, off &+ 8))) &* k
                }
                // The last 16 bytes of the stream. keep >= 128
                // guarantees this reach-back stays inside the buffer
                // even when l is small.
                if l > 0 {
                    h2 = (h2 &+ Hayahash.inj(Hayahash.load64(p, count &- 16))) &* k
                    h3 = (h3 &+ Hayahash.inj(Hayahash.load64(p, count &- 8))) &* k
                }

                t0 = (h0 ^ Hayahash.rotl(h1, 13) ^ lenmix) &* k
                t1 = (h2 ^ Hayahash.rotl(h3, 33)) &* k
            }

            return (t0, t1)
        }
    }
}
