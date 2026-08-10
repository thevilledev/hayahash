package hayahash

import (
	"encoding/binary"
	"hash"
	"testing"
)

// splitPatterns mirrors tests/hash128.c: every split of the same input
// has to produce the one-shot digest, so the interesting cases are the
// ones that straddle the 448-byte buffer, the 128-byte keep floor, and
// the 64-byte block.
var splitPatterns = []struct {
	name string
	next func(i, remaining int) int
}{
	{"bytewise", func(int, int) int { return 1 }},
	{"7", func(int, int) int { return 7 }},
	{"64", func(int, int) int { return 64 }},
	{"127", func(int, int) int { return 127 }},
	{"448", func(int, int) int { return 448 }},
	{"449", func(int, int) int { return 449 }},
	{"whole", func(_, remaining int) int { return remaining }},
	{"varying", func(i, _ int) int { return 1 + (i*31+7)%193 }},
}

func writeSplit(t *testing.T, d *Digest, in []byte, next func(i, remaining int) int) {
	t.Helper()
	for i := 0; len(in) > 0; i++ {
		n := next(i, len(in))
		if n > len(in) {
			n = len(in)
		}
		got, err := d.Write(in[:n])
		if err != nil {
			t.Fatalf("Write returned %v", err)
		}
		if got != n {
			t.Fatalf("Write returned %d, want %d", got, n)
		}
		in = in[n:]
	}
}

// patternA is the shared portable input fill used by the KAT tables and
// test_vectors/.
func patternA(n int) []byte {
	b := make([]byte, n)
	for i := range b {
		b[i] = byte((uint64(i)*k + 0x2545F4914F6CDD1D) >> 56)
	}
	return b
}

func TestStreamingMatchesOneShot(t *testing.T) {
	seeds := []uint64{0, k, 0xDEADBEEFCAFEBABE}
	// Every length through 640 covers the short path, the mid path, the
	// 320-byte bulk threshold, the 448-byte buffer and the first refill.
	lengths := make([]int, 0, 700)
	for n := 0; n <= 640; n++ {
		lengths = append(lengths, n)
	}
	// Then the sizes where the buffer/keep arithmetic changes shape.
	lengths = append(lengths,
		895, 896, 897, 1023, 1024, 1025, 1343, 1344, 1345,
		4095, 4096, 4097, 20000, 65536, 131073)

	for _, seed := range seeds {
		for _, n := range lengths {
			in := patternA(n)
			want64 := Hash64(in, seed)
			want128 := Hash128(in, seed)
			for _, sp := range splitPatterns {
				d := New(seed)
				writeSplit(t, d, in, sp.next)
				if got := d.Sum64(); got != want64 {
					t.Fatalf("Sum64 len=%d seed=%#x split=%s: got %#016x want %#016x",
						n, seed, sp.name, got, want64)
				}
				got128 := d.Sum128()
				if got128 != want128 {
					t.Fatalf("Sum128 len=%d seed=%#x split=%s: got %+v want %+v",
						n, seed, sp.name, got128, want128)
				}
				if got128.Lo != want64 {
					t.Fatalf("Sum128().Lo != Sum64 at len=%d seed=%#x split=%s",
						n, seed, sp.name)
				}
			}
		}
	}
}

func TestDigestIsNonDestructive(t *testing.T) {
	// Finalizing must not disturb the state: digest, keep writing,
	// digest again, and both have to match the one-shot of the prefix.
	const total = 2000
	in := patternA(total)
	d := New(7)
	for _, cut := range []int{0, 1, 63, 64, 447, 448, 449, 1000, total} {
		d.Reset64(7)
		_, _ = d.Write(in[:cut])
		first := d.Sum64()
		second := d.Sum64()
		if first != second {
			t.Fatalf("cut=%d: repeated Sum64 differs: %#x vs %#x", cut, first, second)
		}
		if want := Hash64(in[:cut], 7); first != want {
			t.Fatalf("cut=%d: got %#016x want %#016x", cut, first, want)
		}
		if wide := d.Sum128(); wide.Lo != first {
			t.Fatalf("cut=%d: Sum128 after Sum64 disagrees", cut)
		}
		// Continue absorbing from the same state.
		_, _ = d.Write(in[cut:])
		if got, want := d.Sum64(), Hash64(in, 7); got != want {
			t.Fatalf("cut=%d: continued digest got %#016x want %#016x", cut, got, want)
		}
	}
}

func TestResetKeepsSeed(t *testing.T) {
	in := patternA(1000)
	d := New(0xABCD)
	_, _ = d.Write(in)
	d.Reset()
	_, _ = d.Write(in[:10])
	if got, want := d.Sum64(), Hash64(in[:10], 0xABCD); got != want {
		t.Fatalf("after Reset got %#016x want %#016x", got, want)
	}
	d.Reset64(1)
	_, _ = d.Write(in[:10])
	if got, want := d.Sum64(), Hash64(in[:10], 1); got != want {
		t.Fatalf("after Reset64 got %#016x want %#016x", got, want)
	}
}

func TestEmptyAndZeroLengthWrites(t *testing.T) {
	d := New(0)
	if got, want := d.Sum64(), Hash64(nil, 0); got != want {
		t.Fatalf("empty digest got %#016x want %#016x", got, want)
	}
	// Zero-length writes must be inert, including after real input.
	_, _ = d.Write(nil)
	_, _ = d.Write([]byte{})
	if got, want := d.Sum64(), Hash64(nil, 0); got != want {
		t.Fatalf("after empty writes got %#016x want %#016x", got, want)
	}
	in := patternA(500)
	_, _ = d.Write(in[:200])
	_, _ = d.Write(nil)
	_, _ = d.Write(in[200:])
	_, _ = d.Write([]byte{})
	if got, want := d.Sum64(), Hash64(in, 0); got != want {
		t.Fatalf("interleaved empty writes got %#016x want %#016x", got, want)
	}
}

// TestPublishedStreamingVectors pins the "streaming equality samples"
// section of test_vectors/v0.5.0.txt, which until now no port consumed.
// pattern_a input, seed 0, absorbed one byte at a time.
func TestPublishedStreamingVectors(t *testing.T) {
	vectors := []struct {
		len  int
		want uint64
	}{
		{0, 0x68AC507CF298CA3F},
		{5, 0x37EE1F8B5A98B84B},
		{10, 0xE28B66FB1E4CB4EA},
		{15, 0x9A8920A57F119D6B},
		{20, 0xC311E14FF31FB2BF},
		{25, 0xC27FDE4AC86CCE54},
		{30, 0x16CC1E65CA2CB4F3},
		{35, 0x1C6522BDC246DA12},
		{40, 0xD110128D567CB9F8},
	}
	for _, v := range vectors {
		d := New(0)
		for _, b := range patternA(v.len) {
			_, _ = d.Write([]byte{b})
		}
		if got := d.Sum64(); got != v.want {
			t.Errorf("len=%d bytewise: got %016X want %016X", v.len, got, v.want)
		}
	}
}

func TestHashHash64Interface(t *testing.T) {
	var h hash.Hash64 = New(7)
	in := patternA(700)
	_, _ = h.Write(in)
	if h.Size() != 8 {
		t.Fatalf("Size = %d, want 8", h.Size())
	}
	if h.BlockSize() != 64 {
		t.Fatalf("BlockSize = %d, want 64", h.BlockSize())
	}
	want := Hash64(in, 7)
	if got := h.Sum64(); got != want {
		t.Fatalf("Sum64 = %#016x, want %#016x", got, want)
	}
	// Sum appends big-endian and leaves the state alone.
	sum := h.Sum([]byte("prefix"))
	if string(sum[:6]) != "prefix" {
		t.Fatalf("Sum did not append to the given slice: %q", sum)
	}
	if got := binary.BigEndian.Uint64(sum[6:]); got != want {
		t.Fatalf("Sum bytes = %#016x, want %#016x", got, want)
	}
	if got := h.Sum64(); got != want {
		t.Fatalf("Sum mutated the state")
	}
}
