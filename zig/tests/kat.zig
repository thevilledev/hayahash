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
    .{ .len = 17, .seed = 0x0000000000000000, .want = 0xA90B41F0B4D835FB },
    .{ .len = 17, .seed = 0x9E3779B97F4A7C15, .want = 0xEB9CF0A2219A8A79 },
    .{ .len = 17, .seed = 0xDEADBEEFCAFEBABE, .want = 0x2B0BCB27D533F29D },
    .{ .len = 20, .seed = 0x0000000000000000, .want = 0xD71BDC0ADCA8ADF8 },
    .{ .len = 20, .seed = 0x9E3779B97F4A7C15, .want = 0x1DED18E5D2C6641D },
    .{ .len = 20, .seed = 0xDEADBEEFCAFEBABE, .want = 0x50E620AA5892FC5E },
    .{ .len = 24, .seed = 0x0000000000000000, .want = 0x6982F8BDFE69F930 },
    .{ .len = 24, .seed = 0x9E3779B97F4A7C15, .want = 0xFB5432EF4D039215 },
    .{ .len = 24, .seed = 0xDEADBEEFCAFEBABE, .want = 0xB7A4906D498A9857 },
    .{ .len = 31, .seed = 0x0000000000000000, .want = 0xB01CADA4E3595061 },
    .{ .len = 31, .seed = 0x9E3779B97F4A7C15, .want = 0xBB07CE100EF1ACE7 },
    .{ .len = 31, .seed = 0xDEADBEEFCAFEBABE, .want = 0xD0B87A0842A08418 },
    .{ .len = 32, .seed = 0x0000000000000000, .want = 0x242FD3F914303C1D },
    .{ .len = 32, .seed = 0x9E3779B97F4A7C15, .want = 0x343BA63D7E4FC2F6 },
    .{ .len = 32, .seed = 0xDEADBEEFCAFEBABE, .want = 0x66D7EF7DA4627549 },
    .{ .len = 33, .seed = 0x0000000000000000, .want = 0xE87DE7C9F18E3D9D },
    .{ .len = 33, .seed = 0x9E3779B97F4A7C15, .want = 0x548C908A58A6CDF1 },
    .{ .len = 33, .seed = 0xDEADBEEFCAFEBABE, .want = 0xC115EA8FBA551490 },
    .{ .len = 47, .seed = 0x0000000000000000, .want = 0x696428DD947DF322 },
    .{ .len = 47, .seed = 0x9E3779B97F4A7C15, .want = 0x39BE535C74585AC5 },
    .{ .len = 47, .seed = 0xDEADBEEFCAFEBABE, .want = 0x52811A1D01562807 },
    .{ .len = 48, .seed = 0x0000000000000000, .want = 0x1059B4E24C8CFDF0 },
    .{ .len = 48, .seed = 0x9E3779B97F4A7C15, .want = 0x643BA76560D01C09 },
    .{ .len = 48, .seed = 0xDEADBEEFCAFEBABE, .want = 0xA537F4069657C426 },
    .{ .len = 63, .seed = 0x0000000000000000, .want = 0x4471E159CA7F1AA1 },
    .{ .len = 63, .seed = 0x9E3779B97F4A7C15, .want = 0xE979866C4F557390 },
    .{ .len = 63, .seed = 0xDEADBEEFCAFEBABE, .want = 0xB026D08EC12753F1 },
    .{ .len = 64, .seed = 0x0000000000000000, .want = 0x62BC7D33C15657E9 },
    .{ .len = 64, .seed = 0x9E3779B97F4A7C15, .want = 0x0078437E7D379478 },
    .{ .len = 64, .seed = 0xDEADBEEFCAFEBABE, .want = 0x765229403E95673B },
    .{ .len = 65, .seed = 0x0000000000000000, .want = 0xA57F09711AE70C77 },
    .{ .len = 65, .seed = 0x9E3779B97F4A7C15, .want = 0xA5F941F9895FAF8D },
    .{ .len = 65, .seed = 0xDEADBEEFCAFEBABE, .want = 0x74F19F9600A6C10F },
    .{ .len = 96, .seed = 0x0000000000000000, .want = 0x2186FDC93E032C9C },
    .{ .len = 96, .seed = 0x9E3779B97F4A7C15, .want = 0x4164E090EA0C5DF9 },
    .{ .len = 96, .seed = 0xDEADBEEFCAFEBABE, .want = 0x967410100E8C7C8A },
    .{ .len = 127, .seed = 0x0000000000000000, .want = 0xDAA46CC2E67CF5C3 },
    .{ .len = 127, .seed = 0x9E3779B97F4A7C15, .want = 0x1D78B9BCE14CF66F },
    .{ .len = 127, .seed = 0xDEADBEEFCAFEBABE, .want = 0x7CC9599ACC50F32B },
    .{ .len = 128, .seed = 0x0000000000000000, .want = 0xD4C430490D0CE9D1 },
    .{ .len = 128, .seed = 0x9E3779B97F4A7C15, .want = 0xAE0594746A45D322 },
    .{ .len = 128, .seed = 0xDEADBEEFCAFEBABE, .want = 0xC777BD88BF800192 },
    .{ .len = 191, .seed = 0x0000000000000000, .want = 0xAB4EB4F6A214AB26 },
    .{ .len = 191, .seed = 0x9E3779B97F4A7C15, .want = 0x2F01EEFE05F61681 },
    .{ .len = 191, .seed = 0xDEADBEEFCAFEBABE, .want = 0x0AC11D2D060A6D39 },
    .{ .len = 192, .seed = 0x0000000000000000, .want = 0xBD4668FD0E37A0D8 },
    .{ .len = 192, .seed = 0x9E3779B97F4A7C15, .want = 0xB33BD21C20D7F1EE },
    .{ .len = 192, .seed = 0xDEADBEEFCAFEBABE, .want = 0x2C05041A563EFC0A },
    .{ .len = 255, .seed = 0x0000000000000000, .want = 0x3F35C5137D9DDD92 },
    .{ .len = 255, .seed = 0x9E3779B97F4A7C15, .want = 0xB3450BEEA6A88C0E },
    .{ .len = 255, .seed = 0xDEADBEEFCAFEBABE, .want = 0x10EEAFB383803642 },
    .{ .len = 319, .seed = 0x0000000000000000, .want = 0xFB6F356631C62298 },
    .{ .len = 319, .seed = 0x9E3779B97F4A7C15, .want = 0x82A92D2F3C0D3FC2 },
    .{ .len = 319, .seed = 0xDEADBEEFCAFEBABE, .want = 0xC187B939C37F8EC7 },
    .{ .len = 320, .seed = 0x0000000000000000, .want = 0xAAB4DE0105C41715 },
    .{ .len = 320, .seed = 0x9E3779B97F4A7C15, .want = 0x24747E138240D684 },
    .{ .len = 320, .seed = 0xDEADBEEFCAFEBABE, .want = 0xD908512F166E3CD2 },
    .{ .len = 321, .seed = 0x0000000000000000, .want = 0xD58DE26140651F72 },
    .{ .len = 321, .seed = 0x9E3779B97F4A7C15, .want = 0xAE8B28A6EF04CC35 },
    .{ .len = 321, .seed = 0xDEADBEEFCAFEBABE, .want = 0xAA4EFD9BA8E0C810 },
    .{ .len = 383, .seed = 0x0000000000000000, .want = 0x6FBC1354616B9257 },
    .{ .len = 383, .seed = 0x9E3779B97F4A7C15, .want = 0x09A754CB4921EAB0 },
    .{ .len = 383, .seed = 0xDEADBEEFCAFEBABE, .want = 0xF1548B3D85DA380E },
    .{ .len = 512, .seed = 0x0000000000000000, .want = 0x2968882331191FDB },
    .{ .len = 512, .seed = 0x9E3779B97F4A7C15, .want = 0x143C166BDE64236D },
    .{ .len = 512, .seed = 0xDEADBEEFCAFEBABE, .want = 0x5E7F7FF6C918FE7F },
    .{ .len = 1023, .seed = 0x0000000000000000, .want = 0xDA47A412C97502BE },
    .{ .len = 1023, .seed = 0x9E3779B97F4A7C15, .want = 0x003D27583FDEE215 },
    .{ .len = 1023, .seed = 0xDEADBEEFCAFEBABE, .want = 0x80F98032FDB103FD },
    .{ .len = 1024, .seed = 0x0000000000000000, .want = 0xD3594D0A25CB043B },
    .{ .len = 1024, .seed = 0x9E3779B97F4A7C15, .want = 0xED0E2941D2F3D593 },
    .{ .len = 1024, .seed = 0xDEADBEEFCAFEBABE, .want = 0xF00E1771AB6A3869 },
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
    try std.testing.expectEqual(0xF3C4A9B4, @as(u32, @truncate(total)));
}
