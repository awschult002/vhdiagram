/* arena.c - simple bump allocator; see arena.h. */
#include "arena.h"

#include <stdlib.h>
#include <string.h>

#define ARENA_ALIGN 8
#define ARENA_BLOCK_SIZE (64 * 1024)

struct ArenaBlock {
	ArenaBlock *next;  /* older block */
	size_t cap;        /* usable bytes in data[] */
	size_t pos;        /* next free byte in data[] */
	double data[1];    /* aligned storage; really cap bytes */
};

void arena_init(Arena *a)
{
	memset(a, 0, sizeof *a);
}

void arena_free(Arena *a)
{
	ArenaBlock *b = a->head;

	while (b) {
		ArenaBlock *next = b->next;
		free(b);
		b = next;
	}
	arena_init(a);
}

static ArenaBlock *new_block(size_t min)
{
	size_t cap = min > ARENA_BLOCK_SIZE ? min : ARENA_BLOCK_SIZE;
	ArenaBlock *b;

	if (cap > (size_t)-1 - sizeof *b)
		return NULL;
	b = malloc(sizeof *b + cap);
	if (!b)
		return NULL;
	b->next = NULL;
	b->cap = cap;
	b->pos = 0;
	return b;
}

void *arena_alloc(Arena *a, size_t n)
{
	ArenaBlock *b = a->head;
	size_t pad;
	unsigned char *p;

	if (n > (size_t)-1 - ARENA_ALIGN)
		return NULL;
	pad = b ? (ARENA_ALIGN - b->pos % ARENA_ALIGN) % ARENA_ALIGN : 0;
	if (!b || pad + n > b->cap - b->pos) {
		b = new_block(n);
		if (!b)
			return NULL;
		b->next = a->head;
		a->head = b;
		a->nblock++;
		pad = 0;
	}
	p = (unsigned char *)b->data + b->pos + pad;
	b->pos += pad + n;
	a->used += pad + n;
	return p;
}

char *arena_strdup(Arena *a, const void *p, size_t len)
{
	char *s;

	if (len == (size_t)-1)
		return NULL;
	s = arena_alloc(a, len + 1);
	if (!s)
		return NULL;
	if (len)
		memcpy(s, p, len);
	s[len] = '\0';
	return s;
}
