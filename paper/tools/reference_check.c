/*
 * Direct checks for the C reference described by the working paper.
 *
 * The vectors cover every input-length regime and both sides of the
 * 320-byte bulk threshold. The final check reproduces the direct-call
 * verification procedure used by the language-port tests in this
 * repository. This is not a replacement for SMHasher3.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "hayahash.h"

struct vector {
	size_t len;
	uint64_t seed;
	uint64_t expected;
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

int main(void)
{
	static const struct vector vectors[] = {
		{    0, UINT64_C(0x0000000000000000), UINT64_C(0xC4F85F43D5A9985E) },
		{    1, UINT64_C(0x0000000000000000), UINT64_C(0x01A6BDFC8C3D62DB) },
		{    3, UINT64_C(0xDEADBEEFCAFEBABE), UINT64_C(0x6D72E08B53427EE0) },
		{    4, UINT64_C(0x0000000000000000), UINT64_C(0x7C58CF3C18F9B496) },
		{    8, UINT64_C(0x9E3779B97F4A7C15), UINT64_C(0x9BB51F7D28C94379) },
		{   16, UINT64_C(0xDEADBEEFCAFEBABE), UINT64_C(0x8B444516C3FCE921) },
		{   17, UINT64_C(0x0000000000000000), UINT64_C(0xA90B41F0B4D835FB) },
		{   31, UINT64_C(0xDEADBEEFCAFEBABE), UINT64_C(0xD0B87A0842A08418) },
		{   32, UINT64_C(0x0000000000000000), UINT64_C(0x242FD3F914303C1D) },
		{  191, UINT64_C(0x9E3779B97F4A7C15), UINT64_C(0x2F01EEFE05F61681) },
		{  192, UINT64_C(0xDEADBEEFCAFEBABE), UINT64_C(0x2C05041A563EFC0A) },
		{  319, UINT64_C(0x0000000000000000), UINT64_C(0xFB6F356631C62298) },
		{  320, UINT64_C(0x9E3779B97F4A7C15), UINT64_C(0x24747E138240D684) },
		{  321, UINT64_C(0xDEADBEEFCAFEBABE), UINT64_C(0xAA4EFD9BA8E0C810) },
		{  383, UINT64_C(0x0000000000000000), UINT64_C(0x6FBC1354616B9257) },
		{  512, UINT64_C(0x9E3779B97F4A7C15), UINT64_C(0x143C166BDE64236D) },
		{ 1024, UINT64_C(0xDEADBEEFCAFEBABE), UINT64_C(0xF00E1771AB6A3869) },
	};
	uint8_t key[1024];
	int failed = 0;

	for (size_t i = 0; i < sizeof(key); i++)
		key[i] = byte_at(i);

	for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
		uint64_t got = hayahash64(key, (ptrdiff_t)vectors[i].len,
		                         vectors[i].seed);
		if (got != vectors[i].expected) {
			fprintf(stderr,
			        "vector %zu failed: len=%zu seed=%016" PRIx64
			        " got=%016" PRIx64 " expected=%016" PRIx64 "\n",
			        i, vectors[i].len, vectors[i].seed, got,
			        vectors[i].expected);
			failed = 1;
		}
	}

	uint32_t verification = direct_verification_value();
	if (verification != UINT32_C(0xF3C4A9B4)) {
		fprintf(stderr,
		        "direct verification failed: got=%08" PRIx32
		        " expected=f3c4a9b4\n",
		        verification);
		failed = 1;
	}

	if (failed)
		return 1;

	printf("%zu known-answer vectors: passed\n",
	       sizeof(vectors) / sizeof(vectors[0]));
	printf("direct-interface verification value: 0x%08" PRIX32 "\n",
	       verification);
	return 0;
}

