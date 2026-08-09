//! Nightly differential conformance against a randomized C-reference corpus.

const std = @import("std");
const hayahash = @import("hayahash");

const CorpusReader = struct {
    bytes: []const u8,
    offset: usize = 0,

    fn take(self: *CorpusReader, len: usize) ![]const u8 {
        const end = std.math.add(usize, self.offset, len) catch
            return error.InvalidDifferentialCorpus;
        if (end > self.bytes.len) return error.InvalidDifferentialCorpus;
        defer self.offset = end;
        return self.bytes[self.offset..end];
    }

    fn readU32(self: *CorpusReader) !u32 {
        const bytes = try self.take(4);
        return std.mem.readInt(u32, bytes[0..4], .little);
    }

    fn readU64(self: *CorpusReader) !u64 {
        const bytes = try self.take(8);
        return std.mem.readInt(u64, bytes[0..8], .little);
    }
};

test "randomized C-reference corpus" {
    const path = std.testing.environ.getAlloc(
        std.testing.allocator,
        "HAYAHASH_CORPUS",
    ) catch |err| switch (err) {
        error.EnvironmentVariableMissing => return,
        else => return err,
    };
    defer std.testing.allocator.free(path);

    const corpus = try std.Io.Dir.cwd().readFileAlloc(
        std.testing.io,
        path,
        std.testing.allocator,
        .limited(512 * 1024 * 1024),
    );
    defer std.testing.allocator.free(corpus);

    var reader: CorpusReader = .{ .bytes = corpus };
    try std.testing.expectEqualStrings("HAYAFZ02", try reader.take(8));
    const case_count = try reader.readU32();
    const prng_seed = try reader.readU64();
    for (0..case_count) |case_index| {
        const len: usize = try reader.readU32();
        const hash_seed = try reader.readU64();
        const expected_lo = try reader.readU64();
        const expected_hi = try reader.readU64();
        const input = try reader.take(len);
        const actual = hayahash.hayahash128(input, hash_seed);
        if (actual.lo != expected_lo or actual.hi != expected_hi) {
            std.debug.print(
                "case={d} len={d} hash_seed=0x{x:0>16} " ++
                    "corpus_prng_seed=0x{x:0>16}\n",
                .{ case_index, len, hash_seed, prng_seed },
            );
            try std.testing.expectEqual(expected_lo, actual.lo);
            try std.testing.expectEqual(expected_hi, actual.hi);
        }
        try std.testing.expectEqual(hayahash.hayahash64(input, hash_seed), actual.lo);
    }

    try std.testing.expectEqual(corpus.len, reader.offset);
}
