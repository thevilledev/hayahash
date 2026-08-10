/*
 * Streaming conformance: every split of an input must produce the
 * one-shot digest, and digesting must not consume the state.
 */

import XCTest
@testable import Hayahash

final class HasherTests: XCTestCase {
    // The shared portable input fill used by the KAT tables and
    // test_vectors/: byte(i) = (i*K + 0x2545F4914F6CDD1D) >> 56.
    private static func patternA(_ n: Int) -> [UInt8] {
        (0..<n).map { i in
            let w = UInt64(i) &* 0x9E3779B97F4A7C15 &+ 0x2545F4914F6CDD1D
            return UInt8(truncatingIfNeeded: w >> 56)
        }
    }

    // Mirrors tests/hash128.c and the other ports: the interesting
    // splits are the ones that straddle the 448-byte buffer, the
    // 128-byte keep floor and the 64-byte block.
    private static var splits: [(name: String, next: (Int, Int) -> Int)] {
        [
            (name: "bytewise", next: { _, _ in 1 }),
            (name: "7", next: { _, _ in 7 }),
            (name: "64", next: { _, _ in 64 }),
            (name: "127", next: { _, _ in 127 }),
            (name: "448", next: { _, _ in 448 }),
            (name: "449", next: { _, _ in 449 }),
            (name: "whole", next: { _, remaining in remaining }),
            (name: "varying", next: { i, _ in 1 + (i * 31 + 7) % 193 }),
        ]
    }

    /// Absorbs `data` in the chunk sizes `next` asks for. Feeds
    /// pointers into the original buffer, so this also covers the
    /// `UnsafeRawBufferPointer` input path.
    private static func feed(
        seed: UInt64,
        _ data: [UInt8],
        _ next: (Int, Int) -> Int
    ) -> Hayahash.Hasher {
        // Resolve the chunk boundaries up front so the pointer closure
        // below captures plain values only.
        var bounds: [Int] = []
        var off = 0
        var i = 0
        while off < data.count {
            off += min(next(i, data.count - off), data.count - off)
            bounds.append(off)
            i += 1
        }

        var h = Hayahash.Hasher(seed: seed)
        data.withUnsafeBytes { p in
            var start = 0
            for end in bounds {
                h.update(UnsafeRawBufferPointer(rebasing: p[start..<end]))
                start = end
            }
        }
        return h
    }

    func testStreamingMatchesOneShot() {
        let seeds: [UInt64] = [0, 0x9E3779B97F4A7C15, 0xDEADBEEFCAFEBABE]
        // Every length through 640 covers the short path, the mid path,
        // the 320-byte bulk threshold, the 448-byte buffer and the
        // first refill.
        var lengths = Array(0...640)
        // Then the sizes where the buffer/keep arithmetic changes shape.
        lengths += [
            895, 896, 897, 1023, 1024, 1025, 1343, 1344, 1345,
            4095, 4096, 4097, 20000, 65536, 131073,
        ]

        for seed in seeds {
            for n in lengths {
                let input = Self.patternA(n)
                let want64 = Hayahash.hash64(input, seed: seed)
                let want128 = Hayahash.hash128(input, seed: seed)
                for split in Self.splits {
                    let h = Self.feed(seed: seed, input, split.next)
                    let ctx = "len=\(n) seed=0x\(String(seed, radix: 16))"
                        + " split=\(split.name)"
                    XCTAssertEqual(h.digest64(), want64, ctx)
                    XCTAssertEqual(h.digest128(), want128, ctx)
                    XCTAssertEqual(h.length, UInt64(n), ctx)
                    XCTAssertEqual(h.seed, seed, ctx)
                }
            }
        }
    }

    func testDigestDoesNotConsumeTheState() {
        let total = 2000
        let data = Self.patternA(total)
        for cut in [0, 1, 63, 64, 447, 448, 449, 512, 1000, total] {
            var h = Hayahash.Hasher(seed: 7)
            h.update(Array(data[0..<cut]))
            let first = h.digest64()
            XCTAssertEqual(first, h.digest64(), "repeated digest differs at cut=\(cut)")
            XCTAssertEqual(first, Hayahash.hash64(Array(data[0..<cut]), seed: 7), "cut=\(cut)")
            XCTAssertEqual(h.digest128().lo, first, "cut=\(cut)")

            h.update(Array(data[cut...]))
            XCTAssertEqual(h.digest64(), Hayahash.hash64(data, seed: 7), "continued at cut=\(cut)")
        }
    }

    func testEmptyAndZeroLengthUpdates() {
        var h = Hayahash.Hasher()
        let empty = [UInt8]()
        XCTAssertEqual(h.digest64(), Hayahash.hash64(empty, seed: 0))
        h.update(empty)
        XCTAssertEqual(h.digest64(), Hayahash.hash64(empty, seed: 0))
        XCTAssertEqual(h.length, 0)

        // A zero-length update in the middle of a stream, on both sides
        // of the point where the state commits to the bulk path.
        let data = Self.patternA(1000)
        h.update(Array(data[0..<200]))
        h.update(empty)
        h.update(Array(data[200..<800]))
        h.update(empty)
        h.update(Array(data[800...]))
        XCTAssertEqual(h.digest64(), Hayahash.hash64(data, seed: 0))
        XCTAssertEqual(h.length, 1000)
    }

    func testResetKeepsOrReplacesTheSeed() {
        let data = Self.patternA(1000)
        var h = Hayahash.Hasher(seed: 0xABCD)
        h.update(data)

        h.reset()
        XCTAssertEqual(h.length, 0)
        XCTAssertEqual(h.seed, 0xABCD)
        h.update(Array(data[0..<10]))
        XCTAssertEqual(h.digest64(), Hayahash.hash64(Array(data[0..<10]), seed: 0xABCD))

        h.reset(seed: 1)
        XCTAssertEqual(h.seed, 1)
        XCTAssertEqual(h.length, 0)
        h.update(Array(data[0..<10]))
        XCTAssertEqual(h.digest64(), Hayahash.hash64(Array(data[0..<10]), seed: 1))
    }

    // A Hasher is a value type, so copying one forks the state. That is
    // the Swift spelling of the explicit `copy()` the other ports need.
    func testCopyingForksTheState() {
        let data = Self.patternA(1200)
        for prefix in [0, 100, 448, 700] {
            var base = Hayahash.Hasher(seed: 42)
            base.update(Array(data[0..<prefix]))

            var left = base
            var right = base
            left.update(Array(data[prefix..<900]))
            right.update(Array(data[prefix...]))

            XCTAssertEqual(
                left.digest64(),
                Hayahash.hash64(Array(data[0..<900]), seed: 42),
                "prefix=\(prefix)"
            )
            XCTAssertEqual(
                right.digest64(),
                Hayahash.hash64(data, seed: 42),
                "prefix=\(prefix)"
            )
            // The original is untouched by either fork.
            XCTAssertEqual(base.length, UInt64(prefix), "prefix=\(prefix)")
            XCTAssertEqual(
                base.digest64(),
                Hayahash.hash64(Array(data[0..<prefix]), seed: 42),
                "prefix=\(prefix)"
            )
        }
    }

    func testDefaultSeedIsZero() {
        var explicitSeed = Hayahash.Hasher(seed: 0)
        var implicitSeed = Hayahash.Hasher()
        let data = Self.patternA(700)
        explicitSeed.update(data)
        implicitSeed.update(data)
        XCTAssertEqual(explicitSeed.digest64(), implicitSeed.digest64())
    }

    // Pins the "streaming equality samples" section of
    // test_vectors/v0.5.0.txt.
    func testPublishedStreamingVectors() {
        let vectors: [(Int, UInt64)] = [
            (0, 0x68AC507CF298CA3F),
            (5, 0x37EE1F8B5A98B84B),
            (10, 0xE28B66FB1E4CB4EA),
            (15, 0x9A8920A57F119D6B),
            (20, 0xC311E14FF31FB2BF),
            (25, 0xC27FDE4AC86CCE54),
            (30, 0x16CC1E65CA2CB4F3),
            (35, 0x1C6522BDC246DA12),
            (40, 0xD110128D567CB9F8),
        ]
        for (len, want) in vectors {
            var h = Hayahash.Hasher(seed: 0)
            for byte in Self.patternA(len) {
                h.update([byte])
            }
            XCTAssertEqual(h.digest64(), want, "len=\(len)")
        }
    }
}
