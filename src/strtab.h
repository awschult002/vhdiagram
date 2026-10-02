/*
 * strtab.h - interned identifier table.
 *
 * Basic identifiers are interned case-insensitively ("Foo", "FOO" and
 * "foo" share one id); extended identifiers (\Foo\) are compared exactly,
 * and the backslashes keep them apart from basic ones. Folding happens
 * only in hashing and comparison: entries point at the original bytes of
 * the first occurrence and nothing is copied, so the source buffers must
 * outlive the table. Ids start at 1; 0 means "none".
 */
#ifndef VHD_STRTAB_H
#define VHD_STRTAB_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	const unsigned char *p;
	uint32_t len;
	uint32_t hash;
} StrEntry;

typedef struct {
	StrEntry *ent;     /* ent[0] is unused so ids start at 1 */
	size_t n;          /* number of entries including ent[0] */
	size_t cap;
	uint32_t *slot;    /* open-addressing hash of ids, 0 = empty */
	size_t nslot;      /* power of two */
} Strtab;

void strtab_init(Strtab *st);
void strtab_free(Strtab *st);

/* Intern p[0..len); returns its id, or 0 on out-of-memory. */
uint32_t strtab_intern(Strtab *st, const unsigned char *p, size_t len);
/* Look up without inserting; returns 0 if absent. */
uint32_t strtab_find(const Strtab *st, const char *p, size_t len);

/* Original bytes of the first occurrence of id. */
const unsigned char *strtab_bytes(const Strtab *st, uint32_t id, size_t *len);
size_t strtab_count(const Strtab *st);

#endif
