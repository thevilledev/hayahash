/* C declaration for the MIPS64 assembly hayahash64 in hayahash.S.
 *
 * This is free and unencumbered software released into the public domain.
 * For more information, please refer to <https://unlicense.org/>
 */

#ifndef HAYAHASH_MIPS_H
#define HAYAHASH_MIPS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bit-exact with the reference hayahash64() in ../hayahash.h. */
uint64_t hayahash64(const void *key, ptrdiff_t len, uint64_t seed);

#ifdef __cplusplus
}
#endif

#endif /* HAYAHASH_MIPS_H */
