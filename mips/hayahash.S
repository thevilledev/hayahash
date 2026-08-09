# hayahash64 and hayahash128 - MIPS64 assembly port of the reference C implementation
# (hayahash.h at the repository root). See that header for the full
# design notes.
#
# Bit-exact with hayahash64() for every input and seed. Loads are
# little-endian by construction (byte assembly), so the digest does
# not depend on host endianness. Targets the MIPS64 n64 ABI:
#
#   uint64_t hayahash64(const void *key, ptrdiff_t len, uint64_t seed);
#     $a0 = key, $a1 = len, $a2 = seed, result in $v0
#   hayahash128_t hayahash128(const void *key, ptrdiff_t len, uint64_t seed);
#     same arguments, result in $v0 (lo) and $v1 (hi)
#
# The compact dispatch shape is used (same absorb sequence as the Go,
# Zig, Java, and wasm ports): 8-lane bulk loop for len >= 320, then the
# 4-lane mid loop and overlapping tail. Architecture-specific length
# tiers from the C header are intentionally omitted; they do not change
# the digest.
#
# This is free and unencumbered software released into the public
# domain. For more information, please refer to <https://unlicense.org/>

	.set	noreorder
	.set	noat
	.set	nomacro

	.section .text.hayahash64, "ax", @progbits
	.align	3
	.globl	hayahash64
	.type	hayahash64, @function
	.globl	hayahash128
	.type	hayahash128, @function

# ---------------------------------------------------------------------------
# Constants (materialized at entry; 64-bit immediates need lui/ori/dsll)
# ---------------------------------------------------------------------------
# K  = 0x9E3779B97F4A7C15
# M1 = 0x3C79AC492BA7B653
# M2 = 0x1C69B3F74AC4AE35

# ---------------------------------------------------------------------------
# Register map (after prologue; n64 names: $t0-$t3, $t8-$t9, $a0-$a7)
#   $s0  p            current read pointer
#   $s1  l            remaining length
#   $s2  s            seed ^ K
#   $s3  K
#   $s4  h0
#   $s5  h1
#   $s6  h2
#   $s7  h3
#   $t0  wp           previous stripe
#   $fp  key          original key base (for overlapping end reads)
#   stack 0($sp)      original length n
# Temps: $t1-$t3, $t8-$t9, $at, $a3-$a7 (h4..h7 live in $a4..$a7 / stack)
# ---------------------------------------------------------------------------

# Stack frame (n64, 16-byte aligned):
#   0($sp)  n
#   8($sp)  saved $ra
#  16($sp)  saved $fp
#  24($sp)  saved $s0
#  32($sp)  saved $s1
#  40($sp)  saved $s2
#  48($sp)  saved $s3
#  56($sp)  saved $s4
#  64($sp)  saved $s5
#  72($sp)  saved $s6
#  80($sp)  saved $s7
#  88($sp)  bulk spill h4
#  96($sp)  bulk spill h5
# 104($sp)  bulk spill h6
# 112($sp)  bulk spill h7
# 120($sp)  wide-output flag
# Frame size 128

	.equ	FRM, 128
	.equ	OFF_N, 0
	.equ	OFF_RA, 8
	.equ	OFF_FP, 16
	.equ	OFF_S0, 24
	.equ	OFF_S1, 32
	.equ	OFF_S2, 40
	.equ	OFF_S3, 48
	.equ	OFF_S4, 56
	.equ	OFF_S5, 64
	.equ	OFF_S6, 72
	.equ	OFF_S7, 80
	.equ	OFF_H4, 88
	.equ	OFF_H5, 96
	.equ	OFF_H6, 104
	.equ	OFF_H7, 112
	.equ	OFF_WIDE, 120

# Load an unsigned little-endian 64-bit word from base+off into rd.
# Clobbers $t8, $t9, $at. rd may alias base.
.macro	LOAD64LE rd, base, off
	lbu	$t8, \off+0(\base)
	lbu	$t9, \off+1(\base)
	lbu	$at, \off+2(\base)
	dsll	$t9, $t9, 8
	or	$t8, $t8, $t9
	lbu	$t9, \off+3(\base)
	dsll	$at, $at, 16
	or	$t8, $t8, $at
	lbu	$at, \off+4(\base)
	dsll	$t9, $t9, 24
	or	$t8, $t8, $t9
	lbu	$t9, \off+5(\base)
	dsll	$at, $at, 32
	or	$t8, $t8, $at
	lbu	$at, \off+6(\base)
	dsll	$t9, $t9, 40
	or	$t8, $t8, $t9
	lbu	$t9, \off+7(\base)
	dsll	$at, $at, 48
	or	$t8, $t8, $at
	dsll	$t9, $t9, 56
	or	\rd, $t8, $t9
.endm

# Load an unsigned little-endian 32-bit word (zero-extended) into rd.
# Clobbers $t8, $t9, $at.
.macro	LOAD32LE rd, base, off
	lbu	$t8, \off+0(\base)
	lbu	$t9, \off+1(\base)
	lbu	$at, \off+2(\base)
	dsll	$t9, $t9, 8
	or	$t8, $t8, $t9
	lbu	$t9, \off+3(\base)
	dsll	$at, $at, 16
	or	$t8, $t8, $at
	dsll	$t9, $t9, 24
	or	\rd, $t8, $t9
.endm

# rd = rotl64(rs, n). Clobbers $t8, $t9. Safe when rd == rs.
.macro	ROTL64 rd, rs, n
	dsll	$t8, \rs, \n
	dsrl	$t9, \rs, 64-\n
	or	\rd, $t8, $t9
.endm

# rd = (rs * rt) mod 2^64. Clobbers HI/LO.
.macro	MUL64 rd, rs, rt
	dmultu	\rs, \rt
	mflo	\rd
.endm

# inj(w) = w ^ rotl(w,21) ^ rotl(w,41). rd may equal rs.
# Clobbers $t8, $t9, $a3.
.macro	INJ rd, rs
	ROTL64	$a3, \rs, 21
	xor	$a3, \rs, $a3
	ROTL64	$t8, \rs, 41
	xor	\rd, $a3, $t8
.endm

# inj2(w) = w ^ rotl(w,11) ^ rotl(w,50).
.macro	INJ2 rd, rs
	ROTL64	$a3, \rs, 11
	xor	$a3, \rs, $a3
	ROTL64	$t8, \rs, 50
	xor	\rd, $a3, $t8
.endm

# Absorb one stripe: h = (h ^ (w + rotl(wp, 27))) * K; wp = w.
# Loads w from base+off. Clobbers $t1, $t8, $t9, $at, HI/LO.
.macro	STRIPE h, base, off
	LOAD64LE $t1, \base, \off
	ROTL64	$t8, $t0, 27		# rotl(wp, 27)
	daddu	$t8, $t1, $t8		# w + rotl(wp, 27)
	xor	\h, \h, $t8
	MUL64	\h, \h, $s3
	move	$t0, $t1		# wp = w
.endm

# h = (h + inj(load64le(base+off))) * K
.macro	ABSORB_INJP h, base, off
	LOAD64LE $t1, \base, \off
	INJ	$t1, $t1
	daddu	\h, \h, $t1
	MUL64	\h, \h, $s3
.endm

# Bijective finalizer used by the high output word. Clobbers
# $t1, $t3, $a4, HI/LO.
.macro	FMIX128 rd
	dsrl	$t1, \rd, 30
	xor	\rd, \rd, $t1
	# N1 = 0xFF51AFD7ED558CCD
	lui	$t3, 0xFF51
	ori	$t3, $t3, 0xAFD7
	dsll	$t3, $t3, 32
	lui	$a4, 0xED55
	ori	$a4, $a4, 0x8CCD
	# lui sign-extends on MIPS64; clear those upper bits before the
	# low half is combined with 0xFF51AFD7.
	dsll	$a4, $a4, 32
	dsrl	$a4, $a4, 32
	or	$t3, $t3, $a4
	MUL64	\rd, \rd, $t3
	dsrl	$t1, \rd, 31
	xor	\rd, \rd, $t1
	# N2 = 0xC4CEB9FE1A85EC53
	lui	$t3, 0xC4CE
	ori	$t3, $t3, 0xB9FE
	dsll	$t3, $t3, 32
	lui	$a4, 0x1A85
	ori	$a4, $a4, 0xEC53
	or	$t3, $t3, $a4
	MUL64	\rd, \rd, $t3
	dsrl	$t1, \rd, 33
	xor	\rd, \rd, $t1
.endm

hayahash64:
	move	$a3, $zero
	b	.Lentry
	nop

hayahash128:
	daddiu	$a3, $zero, 1

.Lentry:
	daddiu	$sp, $sp, -FRM
	sd	$ra, OFF_RA($sp)
	sd	$fp, OFF_FP($sp)
	sd	$s0, OFF_S0($sp)
	sd	$s1, OFF_S1($sp)
	sd	$s2, OFF_S2($sp)
	sd	$s3, OFF_S3($sp)
	sd	$s4, OFF_S4($sp)
	sd	$s5, OFF_S5($sp)
	sd	$s6, OFF_S6($sp)
	sd	$s7, OFF_S7($sp)

	move	$fp, $a0			# key base
	move	$s0, $a0			# p
	move	$s1, $a1			# l = len
	sd	$a1, OFF_N($sp)			# n
	sd	$a3, OFF_WIDE($sp)

	# K = 0x9E3779B97F4A7C15
	lui	$s3, 0x9E37
	ori	$s3, $s3, 0x79B9
	dsll	$s3, $s3, 32
	lui	$t1, 0x7F4A
	ori	$t1, $t1, 0x7C15
	or	$s3, $s3, $t1

	# s = seed ^ K. The length is absorbed in the finalizer instead
	# (through a multiply against state), so the digest is a pure
	# function of (seed, bytes-so-far); lenmix = len * K is
	# recomputed at its two use sites from $s1 / the saved n.
	xor	$s2, $a2, $s3

	# ---- short path: len <= 16 ----
	sltiu	$t1, $s1, 17
	beqz	$t1, .Lmid_setup
	nop

	# a, b in $s4, $s5
	beqz	$s1, .Lshort_empty
	nop
	sltiu	$t1, $s1, 4
	bnez	$t1, .Lshort_1_3
	nop
	sltiu	$t1, $s1, 8
	bnez	$t1, .Lshort_4_7
	nop

	# len >= 8: a = load64le(p), b = load64le(p + len - 8)
	LOAD64LE $s4, $s0, 0
	daddiu	$t1, $s1, -8
	daddu	$t1, $s0, $t1
	LOAD64LE $s5, $t1, 0
	b	.Lshort_mix
	nop

.Lshort_4_7:
	LOAD32LE $s4, $s0, 0
	daddiu	$t1, $s1, -4
	daddu	$t1, $s0, $t1
	LOAD32LE $s5, $t1, 0
	b	.Lshort_mix
	nop

.Lshort_1_3:
	# a = p[0]; b = p[l>>1]<<8 | p[l-1]<<16
	lbu	$s4, 0($s0)
	dsrl	$t1, $s1, 1
	daddu	$t1, $s0, $t1
	lbu	$t1, 0($t1)
	daddiu	$t2, $s1, -1
	daddu	$t2, $s0, $t2
	lbu	$t2, 0($t2)
	dsll	$t1, $t1, 8
	dsll	$t2, $t2, 16
	or	$s5, $t1, $t2
	b	.Lshort_mix
	nop

.Lshort_empty:
	move	$s4, $zero
	move	$s5, $zero

.Lshort_mix:
	# x = (inj(a) ^ s ^ K) * K
	INJ	$t1, $s4
	xor	$t1, $t1, $s2
	xor	$t1, $t1, $s3
	MUL64	$t1, $t1, $s3
	move	$s4, $t1			# preserve x for hayahash128
	# y = (inj2(b) ^ rotl(s, 23) ^ (K >> 19)) * M1
	INJ2	$t2, $s5
	ROTL64	$t3, $s2, 23
	xor	$t2, $t2, $t3
	dsrl	$t3, $s3, 19
	xor	$t2, $t2, $t3
	# M1 = 0x3C79AC492BA7B653
	lui	$t3, 0x3C79
	ori	$t3, $t3, 0xAC49
	dsll	$t3, $t3, 32
	lui	$a4, 0x2BA7
	ori	$a4, $a4, 0xB653
	or	$t3, $t3, $a4
	MUL64	$t2, $t2, $t3
	# return fmix(rotl(x, 27) ^ y ^ len * K); l = len is intact in
	# $s1 on the short path and K is in $s3.
	ROTL64	$t1, $s4, 27
	xor	$s5, $t1, $t2			# u
	MUL64	$a4, $s1, $s3
	xor	$s5, $s5, $a4
	move	$v0, $s5
	# fmix (moremur)
	dsrl	$t1, $v0, 27
	xor	$v0, $v0, $t1
	MUL64	$v0, $v0, $t3		# * M1
	dsrl	$t1, $v0, 33
	xor	$v0, $v0, $t1
	# M2 = 0x1C69B3F74AC4AE35
	lui	$t3, 0x1C69
	ori	$t3, $t3, 0xB3F7
	dsll	$t3, $t3, 32
	lui	$a4, 0x4AC4
	ori	$a4, $a4, 0xAE35
	or	$t3, $t3, $a4
	MUL64	$v0, $v0, $t3
	dsrl	$t1, $v0, 27
	xor	$v0, $v0, $t1
	ld	$t1, OFF_WIDE($sp)
	beqz	$t1, .Lepilogue
	nop
	# hi = fmix128(x + rotl(u, 32))
	ROTL64	$t1, $s5, 32
	daddu	$v1, $s4, $t1
	FMIX128 $v1
	b	.Lepilogue
	nop

# ---- mid/long setup: len > 16 ----
.Lmid_setup:
	# h0 = s ^ K
	xor	$s4, $s2, $s3
	# h1 = rotl(s, 17) + (K << 21)
	ROTL64	$s5, $s2, 17
	dsll	$t1, $s3, 21
	daddu	$s5, $s5, $t1
	# h2 = rotl(s, 34) ^ (K >> 13)
	ROTL64	$s6, $s2, 34
	dsrl	$t1, $s3, 13
	xor	$s6, $s6, $t1
	# h3 = rotl(s, 51) + (K << 42)
	ROTL64	$s7, $s2, 51
	dsll	$t1, $s3, 42
	daddu	$s7, $s7, $t1
	move	$t0, $zero			# wp = 0

	# bulk when l >= 320
	sltiu	$t1, $s1, 320
	bnez	$t1, .Lmid_loop
	nop

# ---- 8-lane bulk loop ----
.Lbulk:
	# h4 = s + (K >> 27)
	dsrl	$t1, $s3, 27
	daddu	$a4, $s2, $t1
	# h5 = rotl(s, 13) ^ (K << 9)
	ROTL64	$a5, $s2, 13
	dsll	$t1, $s3, 9
	xor	$a5, $a5, $t1
	# h6 = rotl(s, 26) + (K >> 40)
	ROTL64	$a6, $s2, 26
	dsrl	$t1, $s3, 40
	daddu	$a6, $a6, $t1
	# h7 = rotl(s, 39) ^ (K << 30)
	ROTL64	$a7, $s2, 39
	dsll	$t1, $s3, 30
	xor	$a7, $a7, $t1
	sd	$a4, OFF_H4($sp)
	sd	$a5, OFF_H5($sp)
	sd	$a6, OFF_H6($sp)
	sd	$a7, OFF_H7($sp)

.Lbulk_loop:
	STRIPE	$s4, $s0, 0
	STRIPE	$s5, $s0, 8
	STRIPE	$s6, $s0, 16
	STRIPE	$s7, $s0, 24
	ld	$a4, OFF_H4($sp)
	STRIPE	$a4, $s0, 32
	sd	$a4, OFF_H4($sp)
	ld	$a5, OFF_H5($sp)
	STRIPE	$a5, $s0, 40
	sd	$a5, OFF_H5($sp)
	ld	$a6, OFF_H6($sp)
	STRIPE	$a6, $s0, 48
	sd	$a6, OFF_H6($sp)
	ld	$a7, OFF_H7($sp)
	STRIPE	$a7, $s0, 56
	sd	$a7, OFF_H7($sp)
	# checkpoint: h0 += wp
	daddu	$s4, $s4, $t0
	daddiu	$s0, $s0, 64
	daddiu	$s1, $s1, -64
	sltiu	$t1, $s1, 64
	beqz	$t1, .Lbulk_loop
	nop

	# Fold upper lanes: hi = (hi ^ rotl(h{i+4}, r)) * K
	ld	$a4, OFF_H4($sp)
	ROTL64	$t1, $a4, 11
	xor	$s4, $s4, $t1
	MUL64	$s4, $s4, $s3
	ld	$a5, OFF_H5($sp)
	ROTL64	$t1, $a5, 19
	xor	$s5, $s5, $t1
	MUL64	$s5, $s5, $s3
	ld	$a6, OFF_H6($sp)
	ROTL64	$t1, $a6, 31
	xor	$s6, $s6, $t1
	MUL64	$s6, $s6, $s3
	ld	$a7, OFF_H7($sp)
	ROTL64	$t1, $a7, 47
	xor	$s7, $s7, $t1
	MUL64	$s7, $s7, $s3

# ---- 4-lane mid loop ----
.Lmid_loop:
	sltiu	$t1, $s1, 32
	bnez	$t1, .Lwall
	nop
	STRIPE	$s4, $s0, 0
	STRIPE	$s5, $s0, 8
	STRIPE	$s6, $s0, 16
	STRIPE	$s7, $s0, 24
	daddiu	$s0, $s0, 32
	daddiu	$s1, $s1, -32
	b	.Lmid_loop
	nop

.Lwall:
	# h0 += rotl(wp, 27)
	ROTL64	$t1, $t0, 27
	daddu	$s4, $s4, $t1

	# when l > 16, absorb next 16 bytes into h0/h1
	sltiu	$t1, $s1, 17
	bnez	$t1, .Ltail_end
	nop
	ABSORB_INJP $s4, $s0, 0
	ABSORB_INJP $s5, $s0, 8

.Ltail_end:
	# when l > 0, overlapping last-16 reads into h2/h3
	beqz	$s1, .Lfold
	nop
	ld	$t2, OFF_N($sp)			# n
	daddiu	$t1, $t2, -16
	daddu	$t1, $fp, $t1			# key + n - 16
	ABSORB_INJP $s6, $t1, 0
	daddiu	$t1, $t2, -8
	daddu	$t1, $fp, $t1			# key + n - 8
	ABSORB_INJP $s7, $t1, 0

.Lfold:
	# t0 = (h0 ^ rotl(h1, 13) ^ n * K) * K; n reloaded from the
	# frame since $s1 counted down through the loops.
	ROTL64	$t1, $s5, 13
	xor	$t1, $s4, $t1
	ld	$t2, OFF_N($sp)
	MUL64	$t2, $t2, $s3
	xor	$t1, $t1, $t2
	MUL64	$t1, $t1, $s3
	move	$s4, $t1			# preserve t0
	# t1 = (h2 ^ rotl(h3, 33)) * K
	ROTL64	$t2, $s7, 33
	xor	$t2, $s6, $t2
	MUL64	$t2, $t2, $s3
	move	$s5, $t2			# preserve t1
	# x = s ^ t0 ^ rotl(t1, 29)
	ROTL64	$t2, $s5, 29
	xor	$v0, $s2, $s4
	xor	$v0, $v0, $t2
	# long_fmix: x ^= x>>37; x *= K; x ^= x>>32
	dsrl	$t1, $v0, 37
	xor	$v0, $v0, $t1
	MUL64	$v0, $v0, $s3
	dsrl	$t1, $v0, 32
	xor	$v0, $v0, $t1
	ld	$t1, OFF_WIDE($sp)
	beqz	$t1, .Lepilogue
	nop
	# hi = fmix128(rotl(s,32) ^ (t1 + rotl(t0,47)))
	ROTL64	$v1, $s2, 32
	ROTL64	$t2, $s4, 47
	daddu	$t2, $s5, $t2
	xor	$v1, $v1, $t2
	FMIX128 $v1

.Lepilogue:
	ld	$ra, OFF_RA($sp)
	ld	$fp, OFF_FP($sp)
	ld	$s0, OFF_S0($sp)
	ld	$s1, OFF_S1($sp)
	ld	$s2, OFF_S2($sp)
	ld	$s3, OFF_S3($sp)
	ld	$s4, OFF_S4($sp)
	ld	$s5, OFF_S5($sp)
	ld	$s6, OFF_S6($sp)
	ld	$s7, OFF_S7($sp)
	jr	$ra
	daddiu	$sp, $sp, FRM

	.size	hayahash64, .-hayahash64
	.size	hayahash128, .-hayahash128
