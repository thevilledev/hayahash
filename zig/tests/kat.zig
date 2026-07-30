//! Conformance tests against the C reference implementation.
//!
//! The vector table is generated from `hayahash.h`; regenerate with
//! the same key-byte formula if the algorithm ever changes on purpose.

const std = @import("std");
const hayahash = @import("hayahash");

/// Deterministic key material shared with the C generator:
/// `byte(i) = (i * K + 0x2545F4914F6CDD1D) >> 56`.
fn byteAt(i: u64) u8 {
    return @truncate(((i *% 0x9E3779B97F4A7C15) +% 0x2545F4914F6CDD1D) >> 56);
}

const Vector = struct { len: usize, seed: u64, want: u64 };

/// (input length, seed, expected digest), generated from the C
/// reference on a little-endian host. Lengths cover every dispatch
/// path: empty, 1..3 byte, 4..7 byte, 8..16 byte, tail-only, mid-loop,
/// and bulk-loop (>= 320) inputs, including all boundary values.
const vectors = [_]Vector{
    .{ .len = 0, .seed = 0x0000000000000000, .want = 0xC4F85F43D5A9985E },
    .{ .len = 0, .seed = 0x9E3779B97F4A7C15, .want = 0x68AC507CF298CA3F },
    .{ .len = 0, .seed = 0xDEADBEEFCAFEBABE, .want = 0x4B9E2D31E2F3BE1A },
    .{ .len = 1, .seed = 0x0000000000000000, .want = 0x01A6BDFC8C3D62DB },
    .{ .len = 1, .seed = 0x9E3779B97F4A7C15, .want = 0x9D50F14E915849CE },
    .{ .len = 1, .seed = 0xDEADBEEFCAFEBABE, .want = 0xCCCC62AC48D47C24 },
    .{ .len = 2, .seed = 0x0000000000000000, .want = 0xDADF7DA717BFE154 },
    .{ .len = 2, .seed = 0x9E3779B97F4A7C15, .want = 0xC89A133687D95091 },
    .{ .len = 2, .seed = 0xDEADBEEFCAFEBABE, .want = 0x30F0C4A46F6AD806 },
    .{ .len = 3, .seed = 0x0000000000000000, .want = 0xEA21F17856742557 },
    .{ .len = 3, .seed = 0x9E3779B97F4A7C15, .want = 0xE58B6749C35ABE5C },
    .{ .len = 3, .seed = 0xDEADBEEFCAFEBABE, .want = 0x6D72E08B53427EE0 },
    .{ .len = 4, .seed = 0x0000000000000000, .want = 0x7C58CF3C18F9B496 },
    .{ .len = 4, .seed = 0x9E3779B97F4A7C15, .want = 0x99AAF1833279AA9D },
    .{ .len = 4, .seed = 0xDEADBEEFCAFEBABE, .want = 0xA1B22F8158CFD63F },
    .{ .len = 5, .seed = 0x0000000000000000, .want = 0x517724B9853C566C },
    .{ .len = 5, .seed = 0x9E3779B97F4A7C15, .want = 0x250ACB4274262E38 },
    .{ .len = 5, .seed = 0xDEADBEEFCAFEBABE, .want = 0x31760EE78A0023EF },
    .{ .len = 6, .seed = 0x0000000000000000, .want = 0xE1D10A4FBBE1DBC7 },
    .{ .len = 6, .seed = 0x9E3779B97F4A7C15, .want = 0xC93F66BF5463FFF0 },
    .{ .len = 6, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8C20FD5F52DCE3CF },
    .{ .len = 7, .seed = 0x0000000000000000, .want = 0x56A3989BDDD7CBE5 },
    .{ .len = 7, .seed = 0x9E3779B97F4A7C15, .want = 0xD7D254EE5D533404 },
    .{ .len = 7, .seed = 0xDEADBEEFCAFEBABE, .want = 0xC1D3AB0CC35936B9 },
    .{ .len = 8, .seed = 0x0000000000000000, .want = 0xDFD74858772B0E91 },
    .{ .len = 8, .seed = 0x9E3779B97F4A7C15, .want = 0x9BB51F7D28C94379 },
    .{ .len = 8, .seed = 0xDEADBEEFCAFEBABE, .want = 0x600552A3FF515FAE },
    .{ .len = 9, .seed = 0x0000000000000000, .want = 0x0D246B6B3127404E },
    .{ .len = 9, .seed = 0x9E3779B97F4A7C15, .want = 0x96A02B773AF52564 },
    .{ .len = 9, .seed = 0xDEADBEEFCAFEBABE, .want = 0x691BF2C6A9EEF2E0 },
    .{ .len = 10, .seed = 0x0000000000000000, .want = 0x940CED4143CCCB2B },
    .{ .len = 10, .seed = 0x9E3779B97F4A7C15, .want = 0x6D653DB391FBCACC },
    .{ .len = 10, .seed = 0xDEADBEEFCAFEBABE, .want = 0x24169BD3021086AC },
    .{ .len = 11, .seed = 0x0000000000000000, .want = 0x00F4C1EB06392F40 },
    .{ .len = 11, .seed = 0x9E3779B97F4A7C15, .want = 0xA239C14889BA224A },
    .{ .len = 11, .seed = 0xDEADBEEFCAFEBABE, .want = 0x29D070B34AF489CE },
    .{ .len = 12, .seed = 0x0000000000000000, .want = 0x35985EE7C1DFC292 },
    .{ .len = 12, .seed = 0x9E3779B97F4A7C15, .want = 0xC65A041E835E8250 },
    .{ .len = 12, .seed = 0xDEADBEEFCAFEBABE, .want = 0x310DFF6EFCE0F6BD },
    .{ .len = 13, .seed = 0x0000000000000000, .want = 0xFEA35C4B388EC02B },
    .{ .len = 13, .seed = 0x9E3779B97F4A7C15, .want = 0xAA3CEFED2E20869E },
    .{ .len = 13, .seed = 0xDEADBEEFCAFEBABE, .want = 0xF478BC0F8259144C },
    .{ .len = 14, .seed = 0x0000000000000000, .want = 0x08524E7E5AA2CE93 },
    .{ .len = 14, .seed = 0x9E3779B97F4A7C15, .want = 0x5D380960C8653731 },
    .{ .len = 14, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8887680638A97434 },
    .{ .len = 15, .seed = 0x0000000000000000, .want = 0xE82F8F2F83E24DC3 },
    .{ .len = 15, .seed = 0x9E3779B97F4A7C15, .want = 0xA55CC72365471962 },
    .{ .len = 15, .seed = 0xDEADBEEFCAFEBABE, .want = 0xAF5A8128D9513D53 },
    .{ .len = 16, .seed = 0x0000000000000000, .want = 0xC6C3B656C926EF2B },
    .{ .len = 16, .seed = 0x9E3779B97F4A7C15, .want = 0x348B4B44E949A4A5 },
    .{ .len = 16, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8B444516C3FCE921 },
    .{ .len = 17, .seed = 0x0000000000000000, .want = 0xF2097D97DDE357AE },
    .{ .len = 17, .seed = 0x9E3779B97F4A7C15, .want = 0x9632041CD63DB454 },
    .{ .len = 17, .seed = 0xDEADBEEFCAFEBABE, .want = 0x1723899582C3EA6F },
    .{ .len = 20, .seed = 0x0000000000000000, .want = 0x755BAF1F2B76C412 },
    .{ .len = 20, .seed = 0x9E3779B97F4A7C15, .want = 0x3618DCCEF36B9B12 },
    .{ .len = 20, .seed = 0xDEADBEEFCAFEBABE, .want = 0xD1DD986517B516B5 },
    .{ .len = 24, .seed = 0x0000000000000000, .want = 0xA9EEF61768886934 },
    .{ .len = 24, .seed = 0x9E3779B97F4A7C15, .want = 0x3E37447D706F68DC },
    .{ .len = 24, .seed = 0xDEADBEEFCAFEBABE, .want = 0x7FF1AB972FE165C1 },
    .{ .len = 31, .seed = 0x0000000000000000, .want = 0xA4EBC213A72566E4 },
    .{ .len = 31, .seed = 0x9E3779B97F4A7C15, .want = 0x8C97E8FB408409EB },
    .{ .len = 31, .seed = 0xDEADBEEFCAFEBABE, .want = 0x00FF9DB9173A7670 },
    .{ .len = 32, .seed = 0x0000000000000000, .want = 0x4CB8942324250253 },
    .{ .len = 32, .seed = 0x9E3779B97F4A7C15, .want = 0xB42703DE5802CE94 },
    .{ .len = 32, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8262A44DEEF5BB9B },
    .{ .len = 33, .seed = 0x0000000000000000, .want = 0x10F0BCA1DF4D619E },
    .{ .len = 33, .seed = 0x9E3779B97F4A7C15, .want = 0xCB48982F8FA4275A },
    .{ .len = 33, .seed = 0xDEADBEEFCAFEBABE, .want = 0x9D20E623D2DABD98 },
    .{ .len = 47, .seed = 0x0000000000000000, .want = 0x6B7DAAB465ACE8D7 },
    .{ .len = 47, .seed = 0x9E3779B97F4A7C15, .want = 0x838C7FD540E2137B },
    .{ .len = 47, .seed = 0xDEADBEEFCAFEBABE, .want = 0x2872D293429994C9 },
    .{ .len = 48, .seed = 0x0000000000000000, .want = 0x49107ADCDE8E3130 },
    .{ .len = 48, .seed = 0x9E3779B97F4A7C15, .want = 0x464BA3B8EC8AC2C2 },
    .{ .len = 48, .seed = 0xDEADBEEFCAFEBABE, .want = 0xDDE8583E92A44E9A },
    .{ .len = 63, .seed = 0x0000000000000000, .want = 0xCEA6DA6E3F61096F },
    .{ .len = 63, .seed = 0x9E3779B97F4A7C15, .want = 0x8A345448769249D7 },
    .{ .len = 63, .seed = 0xDEADBEEFCAFEBABE, .want = 0x5F7D616C0C270A64 },
    .{ .len = 64, .seed = 0x0000000000000000, .want = 0x448F9774A82C2543 },
    .{ .len = 64, .seed = 0x9E3779B97F4A7C15, .want = 0xCC07C69DFC7C4835 },
    .{ .len = 64, .seed = 0xDEADBEEFCAFEBABE, .want = 0x874B418FD0A7DD92 },
    .{ .len = 65, .seed = 0x0000000000000000, .want = 0xD6C623C771B90A0C },
    .{ .len = 65, .seed = 0x9E3779B97F4A7C15, .want = 0xE212A3C24E1EC790 },
    .{ .len = 65, .seed = 0xDEADBEEFCAFEBABE, .want = 0xD8AC399BE6418F77 },
    .{ .len = 96, .seed = 0x0000000000000000, .want = 0x2D31F3DECB602FD3 },
    .{ .len = 96, .seed = 0x9E3779B97F4A7C15, .want = 0x140B1122BD6AFC19 },
    .{ .len = 96, .seed = 0xDEADBEEFCAFEBABE, .want = 0x96154215E49D4615 },
    .{ .len = 127, .seed = 0x0000000000000000, .want = 0xB719FF6021114CD8 },
    .{ .len = 127, .seed = 0x9E3779B97F4A7C15, .want = 0x961A22A3478552E7 },
    .{ .len = 127, .seed = 0xDEADBEEFCAFEBABE, .want = 0xF3B18B0FFD884B22 },
    .{ .len = 128, .seed = 0x0000000000000000, .want = 0x968B305502DBA431 },
    .{ .len = 128, .seed = 0x9E3779B97F4A7C15, .want = 0x471A48CF9B92DC42 },
    .{ .len = 128, .seed = 0xDEADBEEFCAFEBABE, .want = 0x0752440B83EE4A3D },
    .{ .len = 191, .seed = 0x0000000000000000, .want = 0xF3FF82570D2D8256 },
    .{ .len = 191, .seed = 0x9E3779B97F4A7C15, .want = 0xF44D7D59B268EE48 },
    .{ .len = 191, .seed = 0xDEADBEEFCAFEBABE, .want = 0x9FC1503D455244E6 },
    .{ .len = 192, .seed = 0x0000000000000000, .want = 0x8B2F53A8D1592DE0 },
    .{ .len = 192, .seed = 0x9E3779B97F4A7C15, .want = 0x079AF5C58C9F34D8 },
    .{ .len = 192, .seed = 0xDEADBEEFCAFEBABE, .want = 0x34FF2D0B06CD24A1 },
    .{ .len = 255, .seed = 0x0000000000000000, .want = 0xE47C9AEC5C83F47A },
    .{ .len = 255, .seed = 0x9E3779B97F4A7C15, .want = 0x609491AF8062780A },
    .{ .len = 255, .seed = 0xDEADBEEFCAFEBABE, .want = 0x3EBBFF793A025A3A },
    .{ .len = 319, .seed = 0x0000000000000000, .want = 0x0E3593E08BED4F79 },
    .{ .len = 319, .seed = 0x9E3779B97F4A7C15, .want = 0xCF66BCA2AE1CFE7A },
    .{ .len = 319, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8E3415FC76D03904 },
    .{ .len = 320, .seed = 0x0000000000000000, .want = 0x8C8BFEE038F4D9BC },
    .{ .len = 320, .seed = 0x9E3779B97F4A7C15, .want = 0xC826AE9965DDA128 },
    .{ .len = 320, .seed = 0xDEADBEEFCAFEBABE, .want = 0x768A97E86095A1F5 },
    .{ .len = 321, .seed = 0x0000000000000000, .want = 0xC975DEB2B0A4D083 },
    .{ .len = 321, .seed = 0x9E3779B97F4A7C15, .want = 0x7BAC7E4CF1E08199 },
    .{ .len = 321, .seed = 0xDEADBEEFCAFEBABE, .want = 0x110D6A9104190DA9 },
    .{ .len = 383, .seed = 0x0000000000000000, .want = 0xEF1113151DEAE6ED },
    .{ .len = 383, .seed = 0x9E3779B97F4A7C15, .want = 0x5D88576FE05458DE },
    .{ .len = 383, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8D9CD7C2CB9E80E7 },
    .{ .len = 512, .seed = 0x0000000000000000, .want = 0x6EC25257921A6924 },
    .{ .len = 512, .seed = 0x9E3779B97F4A7C15, .want = 0x6803A7F94C922829 },
    .{ .len = 512, .seed = 0xDEADBEEFCAFEBABE, .want = 0xA07F4EF94A4B5CA8 },
    .{ .len = 1023, .seed = 0x0000000000000000, .want = 0x97408D1577DE153A },
    .{ .len = 1023, .seed = 0x9E3779B97F4A7C15, .want = 0xB2ED4FCC76F3C87E },
    .{ .len = 1023, .seed = 0xDEADBEEFCAFEBABE, .want = 0xF2B98444F1CB4582 },
    .{ .len = 1024, .seed = 0x0000000000000000, .want = 0xE0A1CB1C1C0BAC68 },
    .{ .len = 1024, .seed = 0x9E3779B97F4A7C15, .want = 0xA5CEFA133BEE637C },
    .{ .len = 1024, .seed = 0xDEADBEEFCAFEBABE, .want = 0xD0AE6DF69F1413A3 },
};

test "matches C reference" {
    var buf: [1024]u8 = undefined;
    for (&buf, 0..) |*b, i| b.* = byteAt(i);
    for (vectors) |v| {
        try std.testing.expectEqual(v.want, hayahash.hayahash64(buf[0..v.len], v.seed));
    }
}

// SMHasher3's self-test: hash the key prefix of length `i` with seed
// `256 - i` for i in 0..=255, concatenating the little-endian digests
// (key byte `i` is set to `i` after round `i`), then hash that buffer
// with seed 0. The low 32 bits must match the registered verification
// value.
test "smhasher3 verification value" {
    var key = [_]u8{0} ** 256;
    var hashes: [256 * 8]u8 = undefined;
    for (0..256) |i| {
        const h = hayahash.hayahash64(key[0..i], 256 - i);
        std.mem.writeInt(u64, hashes[i * 8 ..][0..8], h, .little);
        key[i] = @intCast(i);
    }
    const total = hayahash.hayahash64(&hashes, 0);
    try std.testing.expectEqual(0x6B558D9D, @as(u32, @truncate(total)));
}
