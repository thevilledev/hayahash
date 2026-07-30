// Minimal <string.h> for the wasm32-freestanding build. hayahash.h
// only needs the memcpy prototype; the fixed 4/8-byte copies it makes
// lower to single wasm load instructions, and shim.c defines a real
// memcpy in case the compiler emits a call anyway.
#pragma once
typedef __SIZE_TYPE__ size_t;
void *memcpy(void *dst, const void *src, size_t n);
