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
    .{ .len = 0, .seed = 0x0000000000000000, .want = 0x68AC507CF298CA3F },
    .{ .len = 0, .seed = 0x9E3779B97F4A7C15, .want = 0xC4F85F43D5A9985E },
    .{ .len = 0, .seed = 0xDEADBEEFCAFEBABE, .want = 0x7EDC9F1B603B7337 },
    .{ .len = 1, .seed = 0x0000000000000000, .want = 0x7EC9660A48395D15 },
    .{ .len = 1, .seed = 0x9E3779B97F4A7C15, .want = 0x4D49AADD61BED986 },
    .{ .len = 1, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8E456FA77805E810 },
    .{ .len = 2, .seed = 0x0000000000000000, .want = 0x3AE1E83A68B10976 },
    .{ .len = 2, .seed = 0x9E3779B97F4A7C15, .want = 0xE27ACD9CD85250AE },
    .{ .len = 2, .seed = 0xDEADBEEFCAFEBABE, .want = 0xB64D67091596299D },
    .{ .len = 3, .seed = 0x0000000000000000, .want = 0x10E8B8FEA2D42E52 },
    .{ .len = 3, .seed = 0x9E3779B97F4A7C15, .want = 0x377E32D405528932 },
    .{ .len = 3, .seed = 0xDEADBEEFCAFEBABE, .want = 0xDCC0CB335DC1DE4B },
    .{ .len = 4, .seed = 0x0000000000000000, .want = 0x3FF33333AEEA0226 },
    .{ .len = 4, .seed = 0x9E3779B97F4A7C15, .want = 0x7BB1267AF5779B6B },
    .{ .len = 4, .seed = 0xDEADBEEFCAFEBABE, .want = 0x321409D41F3F0597 },
    .{ .len = 5, .seed = 0x0000000000000000, .want = 0x37EE1F8B5A98B84B },
    .{ .len = 5, .seed = 0x9E3779B97F4A7C15, .want = 0xBEF801DDD997C630 },
    .{ .len = 5, .seed = 0xDEADBEEFCAFEBABE, .want = 0x169255793C689422 },
    .{ .len = 6, .seed = 0x0000000000000000, .want = 0x7C024E9BC939E745 },
    .{ .len = 6, .seed = 0x9E3779B97F4A7C15, .want = 0xDBE499DF16AF6C98 },
    .{ .len = 6, .seed = 0xDEADBEEFCAFEBABE, .want = 0x38A42A135D5BFBC6 },
    .{ .len = 7, .seed = 0x0000000000000000, .want = 0x8D33EEB37AEA4269 },
    .{ .len = 7, .seed = 0x9E3779B97F4A7C15, .want = 0x5E24209A2FD00B2C },
    .{ .len = 7, .seed = 0xDEADBEEFCAFEBABE, .want = 0xF47AB25F56BDC3D7 },
    .{ .len = 8, .seed = 0x0000000000000000, .want = 0xA7E6D3110DA23914 },
    .{ .len = 8, .seed = 0x9E3779B97F4A7C15, .want = 0xCCB63E92CE9F688B },
    .{ .len = 8, .seed = 0xDEADBEEFCAFEBABE, .want = 0x18665C87C237153D },
    .{ .len = 9, .seed = 0x0000000000000000, .want = 0x09C8DFCA0C41DA5C },
    .{ .len = 9, .seed = 0x9E3779B97F4A7C15, .want = 0xE75FFD2D1E883756 },
    .{ .len = 9, .seed = 0xDEADBEEFCAFEBABE, .want = 0xE5EE2BB71F19DE1B },
    .{ .len = 10, .seed = 0x0000000000000000, .want = 0xE28B66FB1E4CB4EA },
    .{ .len = 10, .seed = 0x9E3779B97F4A7C15, .want = 0x2A004FEA465884CE },
    .{ .len = 10, .seed = 0xDEADBEEFCAFEBABE, .want = 0x0418428D16CA9A24 },
    .{ .len = 11, .seed = 0x0000000000000000, .want = 0x12778E6F25C1D32A },
    .{ .len = 11, .seed = 0x9E3779B97F4A7C15, .want = 0x48188C4BED9A1E46 },
    .{ .len = 11, .seed = 0xDEADBEEFCAFEBABE, .want = 0xAD02CB6AE9D55E5B },
    .{ .len = 12, .seed = 0x0000000000000000, .want = 0x48D755EBE2679385 },
    .{ .len = 12, .seed = 0x9E3779B97F4A7C15, .want = 0x011E9FD88E5940FE },
    .{ .len = 12, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8180264C7B1768A9 },
    .{ .len = 13, .seed = 0x0000000000000000, .want = 0x381A61980D756222 },
    .{ .len = 13, .seed = 0x9E3779B97F4A7C15, .want = 0x64B7FC904BBC58B3 },
    .{ .len = 13, .seed = 0xDEADBEEFCAFEBABE, .want = 0x64C9FE2B9160C2CA },
    .{ .len = 14, .seed = 0x0000000000000000, .want = 0xC71A2DB50E6448EB },
    .{ .len = 14, .seed = 0x9E3779B97F4A7C15, .want = 0x32011605ED340D8C },
    .{ .len = 14, .seed = 0xDEADBEEFCAFEBABE, .want = 0x3A8F543C0F65C501 },
    .{ .len = 15, .seed = 0x0000000000000000, .want = 0x9A8920A57F119D6B },
    .{ .len = 15, .seed = 0x9E3779B97F4A7C15, .want = 0x5572E81BAB3953FF },
    .{ .len = 15, .seed = 0xDEADBEEFCAFEBABE, .want = 0x7E1EB5F7F4A597F0 },
    .{ .len = 16, .seed = 0x0000000000000000, .want = 0xE1AF813939BA1A9E },
    .{ .len = 16, .seed = 0x9E3779B97F4A7C15, .want = 0x72EF22A0197AC7E6 },
    .{ .len = 16, .seed = 0xDEADBEEFCAFEBABE, .want = 0x15DDB774F1BECBF7 },
    .{ .len = 17, .seed = 0x0000000000000000, .want = 0xEB0531E9E3A3BEBE },
    .{ .len = 17, .seed = 0x9E3779B97F4A7C15, .want = 0x0F6DFA98935233F7 },
    .{ .len = 17, .seed = 0xDEADBEEFCAFEBABE, .want = 0x3F070CC2B4422BA0 },
    .{ .len = 20, .seed = 0x0000000000000000, .want = 0xC311E14FF31FB2BF },
    .{ .len = 20, .seed = 0x9E3779B97F4A7C15, .want = 0xAC8DC0FD5673D897 },
    .{ .len = 20, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8B748E4515D7C27F },
    .{ .len = 24, .seed = 0x0000000000000000, .want = 0x9A64D93E28CB5DA0 },
    .{ .len = 24, .seed = 0x9E3779B97F4A7C15, .want = 0xDD2D9A95B8088061 },
    .{ .len = 24, .seed = 0xDEADBEEFCAFEBABE, .want = 0xBE8112B0F103E6C5 },
    .{ .len = 31, .seed = 0x0000000000000000, .want = 0x95D2421945AEC7A1 },
    .{ .len = 31, .seed = 0x9E3779B97F4A7C15, .want = 0xCD63F6F92AE5BA34 },
    .{ .len = 31, .seed = 0xDEADBEEFCAFEBABE, .want = 0x1E915F729DA2021A },
    .{ .len = 32, .seed = 0x0000000000000000, .want = 0xCBD35DAB7AD91CE4 },
    .{ .len = 32, .seed = 0x9E3779B97F4A7C15, .want = 0x4E5482C9BC55AC72 },
    .{ .len = 32, .seed = 0xDEADBEEFCAFEBABE, .want = 0xEFBFF5D3A7172762 },
    .{ .len = 33, .seed = 0x0000000000000000, .want = 0x134D1F8689BF729C },
    .{ .len = 33, .seed = 0x9E3779B97F4A7C15, .want = 0x02F60A6383C9BEA7 },
    .{ .len = 33, .seed = 0xDEADBEEFCAFEBABE, .want = 0x51DE032C8DA94D2F },
    .{ .len = 47, .seed = 0x0000000000000000, .want = 0x854A0E1FB80DC713 },
    .{ .len = 47, .seed = 0x9E3779B97F4A7C15, .want = 0x9C87C14BFAD5F65D },
    .{ .len = 47, .seed = 0xDEADBEEFCAFEBABE, .want = 0x8DA40D3A16F8FBF1 },
    .{ .len = 48, .seed = 0x0000000000000000, .want = 0x6B6B8CAA3DDB2A68 },
    .{ .len = 48, .seed = 0x9E3779B97F4A7C15, .want = 0xFEFAE7ADD93696A6 },
    .{ .len = 48, .seed = 0xDEADBEEFCAFEBABE, .want = 0xF3DECF00052380B1 },
    .{ .len = 63, .seed = 0x0000000000000000, .want = 0x7FD21B276D3862D5 },
    .{ .len = 63, .seed = 0x9E3779B97F4A7C15, .want = 0xF8571E24784C85B0 },
    .{ .len = 63, .seed = 0xDEADBEEFCAFEBABE, .want = 0x68B11FACBCA125F5 },
    .{ .len = 64, .seed = 0x0000000000000000, .want = 0x8D2CE2017D1ECCEB },
    .{ .len = 64, .seed = 0x9E3779B97F4A7C15, .want = 0x257D3EE25843F04B },
    .{ .len = 64, .seed = 0xDEADBEEFCAFEBABE, .want = 0x71AA83B0D836F52D },
    .{ .len = 65, .seed = 0x0000000000000000, .want = 0xA521C43309772CDE },
    .{ .len = 65, .seed = 0x9E3779B97F4A7C15, .want = 0xFCD59327E5C4F6DD },
    .{ .len = 65, .seed = 0xDEADBEEFCAFEBABE, .want = 0x2D7D45F44C1829D0 },
    .{ .len = 96, .seed = 0x0000000000000000, .want = 0x0E456A468AC7355B },
    .{ .len = 96, .seed = 0x9E3779B97F4A7C15, .want = 0xE5F760FC0C083B17 },
    .{ .len = 96, .seed = 0xDEADBEEFCAFEBABE, .want = 0xD3B493D06042DC09 },
    .{ .len = 127, .seed = 0x0000000000000000, .want = 0x4907F10A034954D1 },
    .{ .len = 127, .seed = 0x9E3779B97F4A7C15, .want = 0x1D907A46A134AB8E },
    .{ .len = 127, .seed = 0xDEADBEEFCAFEBABE, .want = 0x350DB49C244548A7 },
    .{ .len = 128, .seed = 0x0000000000000000, .want = 0xEECEEE2B8790729D },
    .{ .len = 128, .seed = 0x9E3779B97F4A7C15, .want = 0x8DED815CA5788588 },
    .{ .len = 128, .seed = 0xDEADBEEFCAFEBABE, .want = 0xC9FF25BFDE22A5C7 },
    .{ .len = 191, .seed = 0x0000000000000000, .want = 0xB9E354ABAF76CDA3 },
    .{ .len = 191, .seed = 0x9E3779B97F4A7C15, .want = 0x8AB844BBF8DF6893 },
    .{ .len = 191, .seed = 0xDEADBEEFCAFEBABE, .want = 0x0A1934AE61772E91 },
    .{ .len = 192, .seed = 0x0000000000000000, .want = 0x0503FD18DB80FFFF },
    .{ .len = 192, .seed = 0x9E3779B97F4A7C15, .want = 0xEDCACF5231FFEDF9 },
    .{ .len = 192, .seed = 0xDEADBEEFCAFEBABE, .want = 0xE6357993D5CAFBD4 },
    .{ .len = 255, .seed = 0x0000000000000000, .want = 0x1D0EE105FC8EE266 },
    .{ .len = 255, .seed = 0x9E3779B97F4A7C15, .want = 0x9EADEAF2612E6B65 },
    .{ .len = 255, .seed = 0xDEADBEEFCAFEBABE, .want = 0x674B0232E3BA8AFB },
    .{ .len = 319, .seed = 0x0000000000000000, .want = 0x8F078F3394AC0EEB },
    .{ .len = 319, .seed = 0x9E3779B97F4A7C15, .want = 0x0672714B8B89EAF4 },
    .{ .len = 319, .seed = 0xDEADBEEFCAFEBABE, .want = 0x66F3ECD10E74B602 },
    .{ .len = 320, .seed = 0x0000000000000000, .want = 0xF4BCF4FA135AABFE },
    .{ .len = 320, .seed = 0x9E3779B97F4A7C15, .want = 0x6F86504F4C61F014 },
    .{ .len = 320, .seed = 0xDEADBEEFCAFEBABE, .want = 0xF0563F11BE6D85C7 },
    .{ .len = 321, .seed = 0x0000000000000000, .want = 0x68868A120FB9CEF6 },
    .{ .len = 321, .seed = 0x9E3779B97F4A7C15, .want = 0x58B97AFD4ADA0656 },
    .{ .len = 321, .seed = 0xDEADBEEFCAFEBABE, .want = 0xEDF253CD5A32819C },
    .{ .len = 383, .seed = 0x0000000000000000, .want = 0x762CF976C6FFBA80 },
    .{ .len = 383, .seed = 0x9E3779B97F4A7C15, .want = 0x738E8B85886F0EAF },
    .{ .len = 383, .seed = 0xDEADBEEFCAFEBABE, .want = 0x368281E467A93A0E },
    .{ .len = 512, .seed = 0x0000000000000000, .want = 0xDFBF7FC9292FF7FF },
    .{ .len = 512, .seed = 0x9E3779B97F4A7C15, .want = 0x153A2FC22ACBAEA6 },
    .{ .len = 512, .seed = 0xDEADBEEFCAFEBABE, .want = 0x2E9B7D29F46F8552 },
    .{ .len = 1023, .seed = 0x0000000000000000, .want = 0x2578244C81138967 },
    .{ .len = 1023, .seed = 0x9E3779B97F4A7C15, .want = 0xB2C3356B389D297D },
    .{ .len = 1023, .seed = 0xDEADBEEFCAFEBABE, .want = 0x125EAA99FBAE5C4D },
    .{ .len = 1024, .seed = 0x0000000000000000, .want = 0x951BE6CF3BC7CF43 },
    .{ .len = 1024, .seed = 0x9E3779B97F4A7C15, .want = 0x3460EEC64AA4799F },
    .{ .len = 1024, .seed = 0xDEADBEEFCAFEBABE, .want = 0xC93D1F81FD51336A },
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
    try std.testing.expectEqual(0x65F2AC15, @as(u32, @truncate(total)));
}
