// Minimal <string.h> for the wasm32-freestanding build. hayahash.h
// needs the memcpy prototype (its fixed 4/8-byte copies lower to
// single wasm load instructions) and, for the streaming state's
// buffer management, memmove; shim.c defines both in case the
// compiler emits real calls, and the linker drops them when unused.
#pragma once
typedef __SIZE_TYPE__ size_t;
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
