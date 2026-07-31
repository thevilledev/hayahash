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
    .{ .len = 32, .seed = 0x0000000000000000, .want = 0x9733F484B26360E2 },
    .{ .len = 32, .seed = 0x9E3779B97F4A7C15, .want = 0x5536B4D8B2CDC730 },
    .{ .len = 32, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8715CE33BCA2379A },
    .{ .len = 33, .seed = 0x0000000000000000, .want = 0xA745A3E83613C58C },
    .{ .len = 33, .seed = 0x9E3779B97F4A7C15, .want = 0x553FF7D4FC5D9DED },
    .{ .len = 33, .seed = 0xDEADBEEFCAFEBABE, .want = 0x4A2C6EF5DC5531C2 },
    .{ .len = 47, .seed = 0x0000000000000000, .want = 0x273936D61142DC44 },
    .{ .len = 47, .seed = 0x9E3779B97F4A7C15, .want = 0xE5310D240EEF34A6 },
    .{ .len = 47, .seed = 0xDEADBEEFCAFEBABE, .want = 0x6F1E75D18841E6A1 },
    .{ .len = 48, .seed = 0x0000000000000000, .want = 0x14FE95D8C0BBFF90 },
    .{ .len = 48, .seed = 0x9E3779B97F4A7C15, .want = 0x12363278FA0C626C },
    .{ .len = 48, .seed = 0xDEADBEEFCAFEBABE, .want = 0x7CEDB4152693BAD0 },
    .{ .len = 63, .seed = 0x0000000000000000, .want = 0xA2D251874D348D01 },
    .{ .len = 63, .seed = 0x9E3779B97F4A7C15, .want = 0x16A48C318E8B07E0 },
    .{ .len = 63, .seed = 0xDEADBEEFCAFEBABE, .want = 0xB1E770FD7BF42E61 },
    .{ .len = 64, .seed = 0x0000000000000000, .want = 0x363A32091A09FCBB },
    .{ .len = 64, .seed = 0x9E3779B97F4A7C15, .want = 0xC4E3781C47EFC9D1 },
    .{ .len = 64, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8D03A93F23DE3191 },
    .{ .len = 65, .seed = 0x0000000000000000, .want = 0x45F1EE618787FF12 },
    .{ .len = 65, .seed = 0x9E3779B97F4A7C15, .want = 0x04985102207489F3 },
    .{ .len = 65, .seed = 0xDEADBEEFCAFEBABE, .want = 0xF363E94BF76C2046 },
    .{ .len = 96, .seed = 0x0000000000000000, .want = 0x4E69ED01F30C31C8 },
    .{ .len = 96, .seed = 0x9E3779B97F4A7C15, .want = 0xC30A61FB3B222764 },
    .{ .len = 96, .seed = 0xDEADBEEFCAFEBABE, .want = 0x58AF9E68DB1EE021 },
    .{ .len = 127, .seed = 0x0000000000000000, .want = 0x0F0FE54BC082E22A },
    .{ .len = 127, .seed = 0x9E3779B97F4A7C15, .want = 0xE3E8106C41EA1694 },
    .{ .len = 127, .seed = 0xDEADBEEFCAFEBABE, .want = 0xCE05BE2901F0D305 },
    .{ .len = 128, .seed = 0x0000000000000000, .want = 0x2A2679B53F1A4841 },
    .{ .len = 128, .seed = 0x9E3779B97F4A7C15, .want = 0x300390CFA9B638B4 },
    .{ .len = 128, .seed = 0xDEADBEEFCAFEBABE, .want = 0x53F276CABAE36EEC },
    .{ .len = 191, .seed = 0x0000000000000000, .want = 0xA92BA7E80F7062F9 },
    .{ .len = 191, .seed = 0x9E3779B97F4A7C15, .want = 0xEB0B5C65254F5714 },
    .{ .len = 191, .seed = 0xDEADBEEFCAFEBABE, .want = 0x139A18908F2CA7EA },
    .{ .len = 192, .seed = 0x0000000000000000, .want = 0xB7E79CDB9B877B00 },
    .{ .len = 192, .seed = 0x9E3779B97F4A7C15, .want = 0xE792EDE59093081E },
    .{ .len = 192, .seed = 0xDEADBEEFCAFEBABE, .want = 0x1803DF1A8DEC4887 },
    .{ .len = 255, .seed = 0x0000000000000000, .want = 0xEBC48B87E0CBC931 },
    .{ .len = 255, .seed = 0x9E3779B97F4A7C15, .want = 0xF93BDA5AEEEBF508 },
    .{ .len = 255, .seed = 0xDEADBEEFCAFEBABE, .want = 0xB1C14189418FFEA4 },
    .{ .len = 319, .seed = 0x0000000000000000, .want = 0xC09297A1F1D64937 },
    .{ .len = 319, .seed = 0x9E3779B97F4A7C15, .want = 0xA41DA5A41F8DCF12 },
    .{ .len = 319, .seed = 0xDEADBEEFCAFEBABE, .want = 0xEC77E71BE598CE5C },
    .{ .len = 320, .seed = 0x0000000000000000, .want = 0xEE3764436C14457A },
    .{ .len = 320, .seed = 0x9E3779B97F4A7C15, .want = 0xAA5826FA7260FEDA },
    .{ .len = 320, .seed = 0xDEADBEEFCAFEBABE, .want = 0x949A1075C6520355 },
    .{ .len = 321, .seed = 0x0000000000000000, .want = 0xE29605C4E053B57E },
    .{ .len = 321, .seed = 0x9E3779B97F4A7C15, .want = 0x0220A8E06A7488FD },
    .{ .len = 321, .seed = 0xDEADBEEFCAFEBABE, .want = 0x671ED25D1C2E46C6 },
    .{ .len = 383, .seed = 0x0000000000000000, .want = 0xE35440F8E70E9DA9 },
    .{ .len = 383, .seed = 0x9E3779B97F4A7C15, .want = 0xD7EFC77EF2D2BB7B },
    .{ .len = 383, .seed = 0xDEADBEEFCAFEBABE, .want = 0x06BC8E5A39E7105A },
    .{ .len = 512, .seed = 0x0000000000000000, .want = 0xE3E6F3E6AEB93BDB },
    .{ .len = 512, .seed = 0x9E3779B97F4A7C15, .want = 0x33AEB18CA7D3E3B0 },
    .{ .len = 512, .seed = 0xDEADBEEFCAFEBABE, .want = 0xE5F56F3152DD7519 },
    .{ .len = 1023, .seed = 0x0000000000000000, .want = 0x9FA8B5EF83709862 },
    .{ .len = 1023, .seed = 0x9E3779B97F4A7C15, .want = 0x8B29A20DD57D53BA },
    .{ .len = 1023, .seed = 0xDEADBEEFCAFEBABE, .want = 0x5CFF532D7767AC0E },
    .{ .len = 1024, .seed = 0x0000000000000000, .want = 0xEFF14C6ADC7654F1 },
    .{ .len = 1024, .seed = 0x9E3779B97F4A7C15, .want = 0x8D6553C8DCA3F2C0 },
    .{ .len = 1024, .seed = 0xDEADBEEFCAFEBABE, .want = 0x1FA36EEB572C3496 },
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
    try std.testing.expectEqual(0x99B3876F, @as(u32, @truncate(total)));
}
