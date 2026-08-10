// Fuzz the file-name escaper that builds hayasum's output lines.
//
// A checksum list is parsed line by line, so a name carrying a newline
// must never be able to forge an extra line. The input is used as a
// file name (truncated at the first NUL, since that is what execve
// hands a program) and rendered through hayasum_print_digest.
//
// Invariants checked on every input:
//   - the line contains exactly one newline, at the very end, and no
//     carriage return anywhere
//   - the digest field is 16 or 32 lowercase hex digits followed by
//     exactly two spaces
//   - unescaping the name field reproduces the original name byte for
//     byte, so the encoding is lossless and injective
//   - the leading-backslash flag is set exactly when the name field
//     differs from the raw name

#define _GNU_SOURCE 1

#define HAYASUM_NO_MAIN 1
#include "../hayasum.c"

#define CHECK(cond)                                                        \
	do {                                                               \
		if (!(cond)) {                                             \
			fprintf(stderr, "%s:%d: check failed: %s\n",       \
				__FILE__, __LINE__, #cond);                \
			abort();                                           \
		}                                                          \
	} while (0)

static int is_lower_hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	size_t namelen, hexlen, fieldlen, i, pos, outlen;
	char *name, *line = NULL, *back;
	uint64_t lo = 0, hi;
	int bits, escaped;
	FILE *mem;

	// A file name ends at the first NUL. The leading bytes also pick
	// the digest width and value, so both output shapes are covered
	// without a second target.
	for (namelen = 0; namelen < size && data[namelen] != '\0'; namelen++)
		;
	name = (char *)malloc(namelen + 1);
	CHECK(name != NULL);
	if (namelen != 0)
		memcpy(name, data, namelen);
	name[namelen] = '\0';

	bits = (size != 0 && (data[0] & 1) != 0) ? 128 : 64;
	for (i = 0; i < size && i < 8; i++)
		lo = (lo << 8) | data[i];
	hi = ~lo;

	mem = open_memstream(&line, &outlen);
	CHECK(mem != NULL);
	hayasum_print_digest(mem, lo, hi, bits, name);
	CHECK(fclose(mem) == 0);
	CHECK(line != NULL);
	// The escaper never emits a NUL, so the stream length and the C
	// string length have to agree.
	CHECK(strlen(line) == outlen);

	pos = 0;
	escaped = 0;
	if (line[pos] == '\\') {
		escaped = 1;
		pos++;
	}
	hexlen = bits == 128 ? 32 : 16;
	CHECK(outlen >= pos + hexlen + 3);
	for (i = 0; i < hexlen; i++)
		CHECK(is_lower_hex(line[pos + i]));
	pos += hexlen;
	CHECK(line[pos] == ' ' && line[pos + 1] == ' ');
	pos += 2;

	CHECK(line[outlen - 1] == '\n');
	CHECK(memchr(line, '\n', outlen - 1) == NULL);
	CHECK(memchr(line, '\r', outlen) == NULL);

	// The name field runs from pos to the terminating newline.
	fieldlen = outlen - 1 - pos;
	back = (char *)malloc(fieldlen + 1);
	CHECK(back != NULL);
	for (i = 0; pos < outlen - 1; i++) {
		char c = line[pos++];

		if (c != '\\') {
			back[i] = c;
			continue;
		}
		CHECK(pos < outlen - 1);
		switch (line[pos++]) {
		case '\\':
			back[i] = '\\';
			break;
		case 'n':
			back[i] = '\n';
			break;
		case 'r':
			back[i] = '\r';
			break;
		default:
			CHECK(0); // no other escape may be produced
		}
	}
	back[i] = '\0';
	CHECK(i == namelen);
	CHECK(memcmp(back, name, namelen) == 0);

	// Flagged exactly when the field is not the name verbatim.
	CHECK(escaped == (fieldlen != namelen));
	CHECK(escaped == hayasum_label_needs_escape(name));

	free(back);
	free(line);
	free(name);
	return 0;
}
