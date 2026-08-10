//! Incremental hashing: absorb input in pieces, digest at any point.
//!
//! `Hasher` produces exactly `hayahash64`/`hayahash128` of the
//! concatenation of everything written, for every split of that input.

const std = @import("std");
const root = @import("root.zig");

const K: u64 = 0x9E3779B97F4A7C15;

/// Streaming buffer size. Totals below it stay buffered so short and
/// mid inputs take the one-shot dispatch at digest time, exactly as
/// the C reference does.
const buf_cap: usize = 448;

/// The floor the buffer is drained to. The digest-time mid/tail phase
/// reaches back up to 16 bytes before the current pointer, so the
/// buffer has to retain more than that.
const keep: usize = 128;

inline fn rotl(x: u64, comptime n: u6) u64 {
    return std.math.rotl(u64, x, n);
}

inline fn load64le(p: []const u8, i: usize) u64 {
    return std.mem.readInt(u64, p[i..][0..8], .little);
}

/// A streaming hayahash state.
///
/// The digest equals the one-shot function over the concatenation of
/// every `update`, for any split. Digesting does not consume the
/// state, so absorbing may continue afterwards.
///
/// Not safe for concurrent use.
pub const Hasher = struct {
    h: [8]u64,
    wp: u64,
    seed: u64,
    total: u64,
    nbuf: usize,
    bulk: bool,
    buf: [buf_cap]u8,

    /// Returns an empty state seeded with `seed`.
    pub fn init(seed: u64) Hasher {
        const s = seed ^ K;
        return .{
            .h = .{
                s ^ K,
                rotl(s, 17) +% (K << 21),
                rotl(s, 34) ^ (K >> 13),
                rotl(s, 51) +% (K << 42),
                s +% (K >> 27),
                rotl(s, 13) ^ (K << 9),
                rotl(s, 26) +% (K >> 40),
                rotl(s, 39) ^ (K << 30),
            },
            .wp = 0,
            .seed = seed,
            .total = 0,
            .nbuf = 0,
            .bulk = false,
            .buf = undefined,
        };
    }

    /// Discards absorbed input and restarts with a new seed.
    pub fn reset(self: *Hasher, seed: u64) void {
        self.* = Hasher.init(seed);
    }

    /// Number of bytes absorbed so far.
    pub fn len(self: *const Hasher) u64 {
        return self.total;
    }

    /// Absorbs `data`.
    pub fn update(self: *Hasher, data: []const u8) void {
        var p = data;
        if (p.len == 0) return;
        self.total +%= p.len;

        if (!self.bulk) {
            // Undecided between the one-shot finish and the bulk path:
            // totals up to buf_cap-1 stay buffered.
            if (self.nbuf + p.len < buf_cap) {
                @memcpy(self.buf[self.nbuf..][0..p.len], p);
                self.nbuf += p.len;
                return;
            }
            // Total is now >= 448 > bulk_min: commit to the bulk path.
            self.bulk = true;
        }

        while (true) {
            // Buffer at its floor with plenty incoming: drain the
            // floor, then stream whole blocks straight from the
            // caller's slice, leaving a [keep, keep+63]-byte remainder.
            if (self.nbuf == keep and p.len > buf_cap) {
                const direct = (p.len - keep) & ~@as(usize, 63);
                self.blocks(self.buf[0..keep]);
                self.blocks(p[0..direct]);
                p = p[direct..];
                self.nbuf = 0;
            }
            const take = @min(buf_cap - self.nbuf, p.len);
            @memcpy(self.buf[self.nbuf..][0..take], p[0..take]);
            self.nbuf += take;
            p = p[take..];
            if (self.nbuf < buf_cap) break;
            // Buffer full: consume whole blocks down to the keep floor.
            const consume = (self.nbuf - keep) & ~@as(usize, 63);
            self.blocks(self.buf[0..consume]);
            self.nbuf -= consume;
            std.mem.copyForwards(u8, self.buf[0..self.nbuf], self.buf[consume..][0..self.nbuf]);
        }
    }

    /// Absorbs `p`, whose length must be a multiple of 64.
    fn blocks(self: *Hasher, p: []const u8) void {
        var h = self.h;
        var wp = self.wp;
        var off: usize = 0;
        while (off < p.len) : (off += 64) {
            inline for (0..8) |lane| {
                const w = load64le(p, off + lane * 8);
                h[lane] = (h[lane] ^ (w +% rotl(wp, 27))) *% K;
                wp = w;
            }
            // Checkpoint the raw-word chain once per block so a
            // 64-stripe rotation orbit cannot hide a difference until
            // it returns to the same lane.
            h[0] +%= wp;
        }
        self.h = h;
        self.wp = wp;
    }

    /// Returns the 64-bit digest of everything absorbed so far,
    /// without consuming the state.
    pub fn digest64(self: *const Hasher) u64 {
        if (!self.bulk) {
            return root.hayahash64(self.buf[0..@intCast(self.total)], self.seed);
        }
        const t = self.tail();
        var x = t.s ^ t.t0 ^ rotl(t.t1, 29);
        x ^= x >> 37;
        x *%= K;
        return x ^ (x >> 32);
    }

    /// Returns both digest words, without consuming the state. `lo` is
    /// exactly `digest64`.
    pub fn digest128(self: *const Hasher) root.Digest128 {
        if (!self.bulk) {
            return root.hayahash128(self.buf[0..@intCast(self.total)], self.seed);
        }
        const t = self.tail();
        var x = t.s ^ t.t0 ^ rotl(t.t1, 29);
        x ^= x >> 37;
        x *%= K;
        return .{
            .lo = x ^ (x >> 32),
            .hi = root.internal.fmix128Fn(rotl(t.s, 32) ^ (t.t1 +% rotl(t.t0, 47))),
        };
    }

    const Tail = struct { t0: u64, t1: u64, s: u64 };

    /// Continues the long path over the buffered remainder: the
    /// leftover whole blocks, then the same fold, mid round, wall and
    /// tail as the one-shot. Reads the state without mutating it.
    fn tail(self: *const Hasher) Tail {
        const lenmix = self.total *% K;
        const s = self.seed ^ K;
        var h = self.h;
        var wp = self.wp;
        const buf = self.buf[0..self.nbuf];
        var off: usize = 0;
        var l = self.nbuf;

        while (l >= 64) {
            inline for (0..8) |lane| {
                const w = load64le(buf, off + lane * 8);
                h[lane] = (h[lane] ^ (w +% rotl(wp, 27))) *% K;
                wp = w;
            }
            h[0] +%= wp;
            off += 64;
            l -= 64;
        }
        h[0] = (h[0] ^ rotl(h[4], 11)) *% K;
        h[1] = (h[1] ^ rotl(h[5], 19)) *% K;
        h[2] = (h[2] ^ rotl(h[6], 31)) *% K;
        h[3] = (h[3] ^ rotl(h[7], 47)) *% K;

        if (l >= 32) {
            inline for (0..4) |lane| {
                const w = load64le(buf, off + lane * 8);
                h[lane] = (h[lane] ^ (w +% rotl(wp, 27))) *% K;
                wp = w;
            }
            off += 32;
            l -= 32;
        }

        h[0] +%= rotl(wp, 27);
        if (l > 16) {
            h[0] = (h[0] +% root.internal.injpFn(buf, off)) *% K;
            h[1] = (h[1] +% root.internal.injpFn(buf, off + 8)) *% K;
        }
        // The last 16 bytes of the stream. keep >= 128 guarantees this
        // reach-back stays inside the buffer even when l is small.
        if (l > 0) {
            h[2] = (h[2] +% root.internal.injpFn(buf, self.nbuf - 16)) *% K;
            h[3] = (h[3] +% root.internal.injpFn(buf, self.nbuf - 8)) *% K;
        }

        return .{
            .t0 = (h[0] ^ rotl(h[1], 13) ^ lenmix) *% K,
            .t1 = (h[2] ^ rotl(h[3], 33)) *% K,
            .s = s,
        };
    }
};
