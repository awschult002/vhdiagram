/*
 * arena.h - simple bump allocator.
 *
 * Memory comes from a chain of blocks. Allocations never move, so a
 * pointer stays valid until arena_free(), which releases everything at
 * once. There is no per-allocation free.
 */
#ifndef VHD_ARENA_H
#define VHD_ARENA_H

#include <stddef.h>

typedef struct ArenaBlock ArenaBlock;

typedef struct {
	ArenaBlock *head;   /* newest block; allocations come from here */
	size_t nblock;      /* number of blocks, for tests and stats */
	size_t used;        /* bytes handed out, including alignment padding */
} Arena;

void arena_init(Arena *a);
void arena_free(Arena *a);

/* n bytes aligned to 8, or NULL on out-of-memory. n == 0 is allowed. */
void *arena_alloc(Arena *a, size_t n);

/* Copy p[0..len) and add a NUL terminator; NULL on out-of-memory. */
char *arena_strdup(Arena *a, const void *p, size_t len);

#endif
