/* strtab.c - interned identifier table; see strtab.h. */
#include "strtab.h"

#include <stdlib.h>
#include <string.h>

static unsigned char fold(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c;
}

/* Extended identifiers start with a backslash and are case-sensitive. */
static int is_ext(const unsigned char *p, size_t len)
{
	return len > 0 && p[0] == '\\';
}

static uint32_t hash_bytes(const unsigned char *p, size_t len)
{
	uint32_t h = 2166136261u; /* FNV-1a */
	int ext = is_ext(p, len);
	size_t i;

	for (i = 0; i < len; i++) {
		h ^= ext ? p[i] : fold(p[i]);
		h *= 16777619u;
	}
	return h;
}

static int same(const unsigned char *a, size_t alen,
		const unsigned char *b, size_t blen)
{
	size_t i;

	if (alen != blen)
		return 0;
	if (is_ext(a, alen))
		return memcmp(a, b, alen) == 0;
	for (i = 0; i < alen; i++)
		if (fold(a[i]) != fold(b[i]))
			return 0;
	return 1;
}

void strtab_init(Strtab *st)
{
	memset(st, 0, sizeof *st);
}

void strtab_free(Strtab *st)
{
	free(st->ent);
	free(st->slot);
	strtab_init(st);
}

static size_t find_slot(const Strtab *st, const unsigned char *p, size_t len,
			uint32_t h)
{
	size_t mask = st->nslot - 1;
	size_t i = h & mask;

	for (;;) {
		uint32_t id = st->slot[i];
		const StrEntry *e = &st->ent[id];

		if (id == 0 || (e->hash == h && same(e->p, e->len, p, len)))
			return i;
		i = (i + 1) & mask;
	}
}

static int grow_slots(Strtab *st)
{
	size_t nslot = st->nslot ? st->nslot * 2 : 64;
	uint32_t *slot = calloc(nslot, sizeof *slot);
	size_t id;

	if (!slot)
		return -1;
	free(st->slot);
	st->slot = slot;
	st->nslot = nslot;
	for (id = 1; id < st->n; id++) {
		const StrEntry *e = &st->ent[id];
		size_t i = find_slot(st, e->p, e->len, e->hash);
		st->slot[i] = (uint32_t)id;
	}
	return 0;
}

uint32_t strtab_intern(Strtab *st, const unsigned char *p, size_t len)
{
	uint32_t h;
	size_t i;

	if (len > UINT32_MAX)
		return 0;
	if (st->n == 0) { /* reserve id 0 */
		st->cap = 64;
		st->ent = calloc(st->cap, sizeof *st->ent);
		if (!st->ent)
			return 0;
		st->n = 1;
	}
	/* keep the load factor at or below 1/2 */
	if ((st->n + 1) * 2 > st->nslot && grow_slots(st) != 0)
		return 0;

	h = hash_bytes(p, len);
	i = find_slot(st, p, len, h);
	if (st->slot[i])
		return st->slot[i];

	if (st->n == st->cap) {
		StrEntry *ent = realloc(st->ent, st->cap * 2 * sizeof *ent);
		if (!ent)
			return 0;
		st->ent = ent;
		st->cap *= 2;
	}
	st->ent[st->n].p = p;
	st->ent[st->n].len = (uint32_t)len;
	st->ent[st->n].hash = h;
	st->slot[i] = (uint32_t)st->n;
	return (uint32_t)st->n++;
}

uint32_t strtab_find(const Strtab *st, const char *p, size_t len)
{
	const unsigned char *q = (const unsigned char *)p;

	if (st->nslot == 0)
		return 0;
	return st->slot[find_slot(st, q, len, hash_bytes(q, len))];
}

const unsigned char *strtab_bytes(const Strtab *st, uint32_t id, size_t *len)
{
	if (id == 0 || id >= st->n) {
		*len = 0;
		return NULL;
	}
	*len = st->ent[id].len;
	return st->ent[id].p;
}

size_t strtab_count(const Strtab *st)
{
	return st->n ? st->n - 1 : 0;
}
