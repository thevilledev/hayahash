/* Nightly differential conformance against a randomized C-reference corpus. */

import Foundation
import XCTest
@testable import Hayahash

final class DifferentialTests: XCTestCase {
    func testRandomizedCReferenceCorpus() throws {
        guard let path = ProcessInfo.processInfo.environment["HAYAHASH_CORPUS"], !path.isEmpty else {
            fputs("HAYAHASH_CORPUS is unset; skipping nightly differential corpus\n", stderr)
            return
        }

        let corpus = try Data(contentsOf: URL(fileURLWithPath: path))
        var cursor = 0

        func requireRemaining(_ bytes: Int) {
            XCTAssertGreaterThanOrEqual(bytes, 0)
            XCTAssertLessThanOrEqual(cursor + bytes, corpus.count, "truncated differential corpus at byte \(cursor)")
        }

        func readUInt32() -> UInt32 {
            requireRemaining(4)
            let b0 = UInt32(corpus[cursor])
            let b1 = UInt32(corpus[cursor + 1]) << 8
            let b2 = UInt32(corpus[cursor + 2]) << 16
            let b3 = UInt32(corpus[cursor + 3]) << 24
            cursor += 4
            return b0 | b1 | b2 | b3
        }

        func readUInt64() -> UInt64 {
            requireRemaining(8)
            let b0 = UInt64(corpus[cursor])
            let b1 = UInt64(corpus[cursor + 1]) << 8
            let b2 = UInt64(corpus[cursor + 2]) << 16
            let b3 = UInt64(corpus[cursor + 3]) << 24
            let b4 = UInt64(corpus[cursor + 4]) << 32
            let b5 = UInt64(corpus[cursor + 5]) << 40
            let b6 = UInt64(corpus[cursor + 6]) << 48
            let b7 = UInt64(corpus[cursor + 7]) << 56
            cursor += 8
            return b0 | b1 | b2 | b3 | b4 | b5 | b6 | b7
        }

        requireRemaining(8)
        let magic = String(decoding: corpus[cursor..<(cursor + 8)], as: UTF8.self)
        cursor += 8
        XCTAssertEqual(magic, "HAYAFZ01")

        let caseCount = readUInt32()
        let prngSeed = readUInt64()

        for caseIndex in 0..<caseCount {
            let length = Int(readUInt32())
            let hashSeed = readUInt64()
            let expected = readUInt64()
            requireRemaining(length)
            let inputOffset = cursor
            let actual = corpus.withUnsafeBytes { buf in
                Hayahash.hash64(
                    UnsafeRawBufferPointer(rebasing: buf[inputOffset..<(inputOffset + length)]),
                    seed: hashSeed
                )
            }
            cursor = inputOffset + length
            XCTAssertEqual(
                actual,
                expected,
                "case=\(caseIndex) len=\(length) hash_seed=0x\(String(hashSeed, radix: 16)) corpus_prng_seed=0x\(String(prngSeed, radix: 16))"
            )
        }

        XCTAssertEqual(cursor, corpus.count, "trailing bytes in differential corpus")
        fputs(
            "Swift matched \(caseCount) C-reference cases (corpus PRNG seed=0x\(String(prngSeed, radix: 16)))\n",
            stderr
        )
    }
}
