/*
 * Direct checks for the C reference described by the working paper.
 *
 * The vectors cover every input-length regime and both sides of the
 * 320-byte bulk threshold, for both digest widths. Every vector also
 * asserts the width invariant hayahash128().lo == hayahash64(). The
 * final checks reproduce the direct-call verification procedure used
 * by the language-port tests in this repository, for each width. This
 * is not a replacement for SMHasher3.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "hayahash.h"

struct vector {
	size_t len;
	uint64_t seed;
	uint64_t expected;
	uint64_t expected_hi;
};

static uint8_t byte_at(size_t i)
{
	uint64_t x = (uint64_t)i * UINT64_C(0x9E3779B97F4A7C15);
	x += UINT64_C(0x2545F4914F6CDD1D);
	return (uint8_t)(x >> 56);
}

static uint32_t direct_verification_value(void)
{
	uint8_t key[256] = { 0 };
	uint8_t hashes[256 * 8];

	for (size_t i = 0; i < 256; i++) {
		uint64_t h = hayahash64(key, (ptrdiff_t)i, 256 - i);
		for (size_t j = 0; j < 8; j++)
			hashes[i * 8 + j] = (uint8_t)(h >> (8 * j));
		key[i] = (uint8_t)i;
	}
	return (uint32_t)hayahash64(hashes, sizeof(hashes), 0);
}

static uint32_t direct_verification_value_128(void)
{
	uint8_t key[256] = { 0 };
	uint8_t hashes[256 * 16];

	for (size_t i = 0; i < 256; i++) {
		hayahash128_t h = hayahash128(key, (ptrdiff_t)i, 256 - i);
		for (size_t j = 0; j < 8; j++) {
			hashes[i * 16 + j] = (uint8_t)(h.lo >> (8 * j));
			hashes[i * 16 + 8 + j] = (uint8_t)(h.hi >> (8 * j));
		}
		key[i] = (uint8_t)i;
	}
	return (uint32_t)hayahash128(hashes, sizeof(hashes), 0).lo;
}

int main(void)
{
	static const struct vector vectors[] = {
		{    0, UINT64_C(0x0000000000000000),
		        UINT64_C(0x68AC507CF298CA3F),
		        UINT64_C(0xACE2141F6BA30868) },
		{    1, UINT64_C(0x0000000000000000),
		        UINT64_C(0x7EC9660A48395D15),
		        UINT64_C(0x3CF0E5F69813C72D) },
		{    3, UINT64_C(0xDEADBEEFCAFEBABE),
		        UINT64_C(0xDCC0CB335DC1DE4B),
		        UINT64_C(0xC51357A09E6BDD3D) },
		{    4, UINT64_C(0x0000000000000000),
		        UINT64_C(0x3FF33333AEEA0226),
		        UINT64_C(0xFB1361E3305035B6) },
		{    8, UINT64_C(0x9E3779B97F4A7C15),
		        UINT64_C(0xCCB63E92CE9F688B),
		        UINT64_C(0xC4551B563450BE68) },
		{   16, UINT64_C(0xDEADBEEFCAFEBABE),
		        UINT64_C(0x15DDB774F1BECBF7),
		        UINT64_C(0xAC114579C9178912) },
		{   17, UINT64_C(0x0000000000000000),
		        UINT64_C(0xEB0531E9E3A3BEBE),
		        UINT64_C(0x8D3299B49DD5F0B4) },
		{   31, UINT64_C(0xDEADBEEFCAFEBABE),
		        UINT64_C(0x1E915F729DA2021A),
		        UINT64_C(0x9C8A8838ED1C2B91) },
		{   32, UINT64_C(0x0000000000000000),
		        UINT64_C(0xCBD35DAB7AD91CE4),
		        UINT64_C(0x46001BDFFAD28749) },
		{  191, UINT64_C(0x9E3779B97F4A7C15),
		        UINT64_C(0x8AB844BBF8DF6893),
		        UINT64_C(0x9491545C5BFA8D1B) },
		{  192, UINT64_C(0xDEADBEEFCAFEBABE),
		        UINT64_C(0xE6357993D5CAFBD4),
		        UINT64_C(0x80184A79A6D39EDD) },
		{  319, UINT64_C(0x0000000000000000),
		        UINT64_C(0x8F078F3394AC0EEB),
		        UINT64_C(0x3DF43DA730823B08) },
		{  320, UINT64_C(0x9E3779B97F4A7C15),
		        UINT64_C(0x6F86504F4C61F014),
		        UINT64_C(0xDD90FF3299ACC673) },
		{  321, UINT64_C(0xDEADBEEFCAFEBABE),
		        UINT64_C(0xEDF253CD5A32819C),
		        UINT64_C(0x569732D51AE7BF5B) },
		{  383, UINT64_C(0x0000000000000000),
		        UINT64_C(0x762CF976C6FFBA80),
		        UINT64_C(0xBB395DD49DC33393) },
		{  512, UINT64_C(0x9E3779B97F4A7C15),
		        UINT64_C(0x153A2FC22ACBAEA6),
		        UINT64_C(0xD28621741168C5E8) },
		{ 1024, UINT64_C(0xDEADBEEFCAFEBABE),
		        UINT64_C(0xC93D1F81FD51336A),
		        UINT64_C(0xB47A82E46FF97BD4) },
	};
	uint8_t key[1024];
	int failed = 0;

	for (size_t i = 0; i < sizeof(key); i++)
		key[i] = byte_at(i);

	for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
		uint64_t got = hayahash64(key, (ptrdiff_t)vectors[i].len,
		                         vectors[i].seed);
		hayahash128_t got128 = hayahash128(key,
		                                   (ptrdiff_t)vectors[i].len,
		                                   vectors[i].seed);
		if (got != vectors[i].expected) {
			fprintf(stderr,
			        "vector %zu failed: len=%zu seed=%016" PRIx64
			        " got=%016" PRIx64 " expected=%016" PRIx64 "\n",
			        i, vectors[i].len, vectors[i].seed, got,
			        vectors[i].expected);
			failed = 1;
		}
		if (got128.lo != got) {
			fprintf(stderr,
			        "vector %zu width invariant failed: len=%zu"
			        " lo=%016" PRIx64 " h64=%016" PRIx64 "\n",
			        i, vectors[i].len, got128.lo, got);
			failed = 1;
		}
		if (got128.hi != vectors[i].expected_hi) {
			fprintf(stderr,
			        "vector %zu hi failed: len=%zu seed=%016" PRIx64
			        " got=%016" PRIx64 " expected=%016" PRIx64 "\n",
			        i, vectors[i].len, vectors[i].seed, got128.hi,
			        vectors[i].expected_hi);
			failed = 1;
		}
	}

	uint32_t verification = direct_verification_value();
	if (verification != UINT32_C(0x65F2AC15)) {
		fprintf(stderr,
		        "direct verification failed: got=%08" PRIx32
		        " expected=65f2ac15\n",
		        verification);
		failed = 1;
	}

	uint32_t verification128 = direct_verification_value_128();
	if (verification128 != UINT32_C(0x3F0411F4)) {
		fprintf(stderr,
		        "direct 128-bit verification failed: got=%08" PRIx32
		        " expected=3f0411f4\n",
		        verification128);
		failed = 1;
	}

	if (failed)
		return 1;

	printf("%zu known-answer vectors (both widths): passed\n",
	       sizeof(vectors) / sizeof(vectors[0]));
	printf("direct-interface verification value: 0x%08" PRIX32 "\n",
	       verification);
	printf("direct-interface 128-bit verification value: 0x%08" PRIX32 "\n",
	       verification128);
	return 0;
}
