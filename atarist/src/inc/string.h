/* string.h for the libc-less Atari ST build: only what the cores use (st_lib.c) */
#ifndef ST_STRING_H
#define ST_STRING_H
#include <stddef.h>
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#endif
