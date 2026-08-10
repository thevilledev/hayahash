//! Streaming conformance: every split of an input must produce the
//! one-shot digest, and digesting must not consume the state.

const std = @import("std");
const hayahash = @import("hayahash");

const K: u64 = 0x9E3779B97F4A7C15;

/// The shared portable input fill used by the KAT tables and
/// test_vectors/.
fn patternA(out: []u8) void {
    for (out, 0..) |*b, i| {
        b.* = @truncate((@as(u64, i) *% K +% 0x2545F4914F6CDD1D) >> 56);
    }
}

/// Chunk sizes that straddle the 448-byte buffer, the 128-byte keep
/// floor and the 64-byte block.
const splits = [_]usize{ 1, 7, 64, 127, 448, 449, std.math.maxInt(usize) };

fn feed(h: *hayahash.Hasher, data: []const u8, chunk: usize) void {
    var i: usize = 0;
    while (i < data.len) {
        const n = @min(chunk, data.len - i);
        h.update(data[i .. i + n]);
        i += n;
    }
}

test "streaming matches one-shot" {
    const allocator = std.testing.allocator;
    const seeds = [_]u64{ 0, K, 0xDEADBEEFCAFEBABE };
    const extra = [_]usize{ 895, 896, 897, 1023, 1024, 1025, 4096, 20000, 131073 };

    const buf = try allocator.alloc(u8, 131073);
    defer allocator.free(buf);
    patternA(buf);

    for (seeds) |seed| {
        var n: usize = 0;
        while (n <= 640) : (n += 1) {
            try checkLength(buf[0..n], seed);
        }
        for (extra) |m| try checkLength(buf[0..m], seed);
    }
}

fn checkLength(data: []const u8, seed: u64) !void {
    const want64 = hayahash.hayahash64(data, seed);
    const want128 = hayahash.hayahash128(data, seed);
    for (splits) |chunk| {
        var h = hayahash.Hasher.init(seed);
        feed(&h, data, chunk);
        try std.testing.expectEqual(want64, h.digest64());
        try std.testing.expectEqual(want128.lo, h.digest128().lo);
        try std.testing.expectEqual(want128.hi, h.digest128().hi);
        try std.testing.expectEqual(want64, h.digest128().lo);
    }
}

test "digest is non-destructive" {
    const allocator = std.testing.allocator;
    const total: usize = 2000;
    const data = try allocator.alloc(u8, total);
    defer allocator.free(data);
    patternA(data);

    for ([_]usize{ 0, 1, 63, 64, 447, 448, 449, 1000, total }) |cut| {
        var h = hayahash.Hasher.init(7);
        h.update(data[0..cut]);
        const first = h.digest64();
        try std.testing.expectEqual(first, h.digest64());
        try std.testing.expectEqual(hayahash.hayahash64(data[0..cut], 7), first);
        try std.testing.expectEqual(first, h.digest128().lo);
        // Continue absorbing from the same state.
        h.update(data[cut..]);
        try std.testing.expectEqual(hayahash.hayahash64(data, 7), h.digest64());
    }
}

test "empty and zero-length updates" {
    var h = hayahash.Hasher.init(0);
    try std.testing.expectEqual(hayahash.hayahash64("", 0), h.digest64());
    h.update("");
    try std.testing.expectEqual(hayahash.hayahash64("", 0), h.digest64());
    try std.testing.expectEqual(@as(u64, 0), h.len());

    const allocator = std.testing.allocator;
    const data = try allocator.alloc(u8, 500);
    defer allocator.free(data);
    patternA(data);
    h.update(data[0..200]);
    h.update("");
    h.update(data[200..]);
    try std.testing.expectEqual(hayahash.hayahash64(data, 0), h.digest64());
    try std.testing.expectEqual(@as(u64, 500), h.len());
}

test "reset restarts with a new seed" {
    const allocator = std.testing.allocator;
    const data = try allocator.alloc(u8, 1000);
    defer allocator.free(data);
    patternA(data);

    var h = hayahash.Hasher.init(0xABCD);
    h.update(data);
    h.reset(1);
    try std.testing.expectEqual(@as(u64, 0), h.len());
    h.update(data[0..10]);
    try std.testing.expectEqual(hayahash.hayahash64(data[0..10], 1), h.digest64());
}

// Pins the "streaming equality samples" section of
// test_vectors/v0.5.0.txt, which until now no port consumed.
// pattern_a input, seed 0, absorbed one byte at a time.
test "published streaming vectors" {
    const vectors = [_]struct { len: usize, want: u64 }{
        .{ .len = 0, .want = 0x68AC507CF298CA3F },
        .{ .len = 5, .want = 0x37EE1F8B5A98B84B },
        .{ .len = 10, .want = 0xE28B66FB1E4CB4EA },
        .{ .len = 15, .want = 0x9A8920A57F119D6B },
        .{ .len = 20, .want = 0xC311E14FF31FB2BF },
        .{ .len = 25, .want = 0xC27FDE4AC86CCE54 },
        .{ .len = 30, .want = 0x16CC1E65CA2CB4F3 },
        .{ .len = 35, .want = 0x1C6522BDC246DA12 },
        .{ .len = 40, .want = 0xD110128D567CB9F8 },
    };
    var buf: [40]u8 = undefined;
    patternA(&buf);
    for (vectors) |v| {
        var h = hayahash.Hasher.init(0);
        for (buf[0..v.len]) |b| h.update(&[_]u8{b});
        try std.testing.expectEqual(v.want, h.digest64());
    }
}
