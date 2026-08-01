package hayahash_test

import (
	"encoding/binary"
	"os"
	"testing"

	hayahash "github.com/thevilledev/hayahash/go"
)

func TestDifferentialConformance(t *testing.T) {
	path := os.Getenv("HAYAHASH_CORPUS")
	if path == "" {
		t.Skip("HAYAHASH_CORPUS is unset; skipping nightly differential corpus")
	}
	corpus, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read differential corpus: %v", err)
	}
	offset := 0
	take := func(n int) []byte {
		if n < 0 || offset > len(corpus)-n {
			t.Fatalf("truncated differential corpus at byte %d", offset)
		}
		value := corpus[offset : offset+n]
		offset += n
		return value
	}
	readU32 := func() uint32 { return binary.LittleEndian.Uint32(take(4)) }
	readU64 := func() uint64 { return binary.LittleEndian.Uint64(take(8)) }

	if magic := string(take(8)); magic != "HAYAFZ01" {
		t.Fatalf("invalid differential corpus magic %q", magic)
	}
	caseCount := readU32()
	prngSeed := readU64()
	for caseIndex := uint32(0); caseIndex < caseCount; caseIndex++ {
		length := int(readU32())
		hashSeed := readU64()
		expected := readU64()
		input := take(length)
		if actual := hayahash.Hash64(input, hashSeed); actual != expected {
			t.Fatalf(
				"case=%d len=%d hash_seed=%#016x corpus_prng_seed=%#016x: got %#016x, want %#016x",
				caseIndex, length, hashSeed, prngSeed, actual, expected,
			)
		}
	}
	if offset != len(corpus) {
		t.Fatalf("%d trailing bytes in differential corpus", len(corpus)-offset)
	}
	t.Logf("Go matched %d C-reference cases (corpus PRNG seed=%#016x)", caseCount, prngSeed)
}
