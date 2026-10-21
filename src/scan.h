/*
 * scan.h - scan step: list the design units in a set of VHDL files.
 *
 * Each file is read, tokenized, summarized and its buffer freed again
 * (docs/DESIGN.md, R-SCALE-2). A summary keeps only names: entity,
 * architecture (and the entity it is of), package, package body,
 * configuration (and its entity), context, plus the library and use
 * clauses in front of each unit. Names are copied into an arena owned
 * by the ScanResult, each distinct name stored once, so nothing points
 * into a source buffer after the scan. Files are reloaded on demand
 * later, when a graph needs them.
 *
 * Names are kept as they are spelled at their first occurrence and are
 * identified by a Strtab id. Like the tokenizer's table, basic names
 * compare case-insensitively ("Foo" and "FOO" get one id) and extended
 * names (\Foo\) exactly, with their backslashes. Ids start at 1; 0 means
 * "none". Use clauses are stored as one dotted name, e.g.
 * "ieee.std_logic_1164.all", with whitespace and comments dropped.
 */
#ifndef VHD_SCAN_H
#define VHD_SCAN_H

#include <stddef.h>
#include <stdint.h>

#include "arena.h"
#include "strtab.h"

enum {
	SCAN_ENTITY = 1,
	SCAN_ARCHITECTURE,
	SCAN_PACKAGE,
	SCAN_PACKAGE_BODY,
	SCAN_PACKAGE_INST,     /* package p is new lib.generic_pkg ... */
	SCAN_CONFIGURATION,
	SCAN_CONTEXT
};

enum {
	SCAN_LIBRARY = 16,     /* library ieee; (own range: one name table) */
	SCAN_USE,              /* use ieee.std_logic_1164.all; */
	SCAN_CONTEXT_REF       /* context lib.ctx; */
};

typedef struct {
	uint32_t kind;     /* SCAN_ENTITY ... */
	uint32_t name;     /* unit name id */
	uint32_t of;       /* entity of an architecture or configuration,
			    * package of a body; 0 otherwise */
	uint32_t file;     /* index into ScanResult.file */
	uint32_t line;     /* 1-based line of the unit keyword */
} ScanUnit;

typedef struct {
	uint32_t kind;     /* SCAN_LIBRARY, SCAN_USE, SCAN_CONTEXT_REF */
	uint32_t name;     /* library name, or the dotted selected name */
	uint32_t unit;     /* index into ScanResult.unit of the unit it
			    * belongs to; (uint32_t)-1 if no unit followed */
} ScanRef;

typedef struct {
	const char *path;  /* copy, in the arena */
	uint64_t hash;     /* FNV-1a 64 of the file's bytes */
	size_t size;
	size_t nerr;       /* lexical errors (TK_ERROR tokens) */
	int failed;        /* 1 if the file could not be read */
} ScanFile;

typedef struct {
	Arena arena;       /* name and path bytes */
	Strtab names;      /* ids for names; entries point into the arena */
	ScanFile *file;
	size_t nfile, file_cap;
	ScanUnit *unit;
	size_t nunit, unit_cap;
	ScanRef *ref;
	size_t nref, ref_cap;
} ScanResult;

void scan_init(ScanResult *sr);
/* Frees everything the result owns: arena, name table and arrays. */
void scan_free(ScanResult *sr);

/* Summarize src[0..len) as file `path`. The buffer is only read during
 * the call; the caller may free or reuse it afterwards. Returns 0, or -1
 * on out-of-memory. Malformed VHDL is not an error: whatever units can
 * be recognized are recorded. */
int scan_buffer(ScanResult *sr, const char *path,
		const unsigned char *src, size_t len);

/* Read the file at path, summarize it and free its buffer. Returns 0,
 * or -1 on out-of-memory. A file that cannot be read is recorded with
 * failed = 1 and does not stop the scan. */
int scan_path(ScanResult *sr, const char *path);

/* NUL-terminated bytes of a name id ("" for 0); *len may be NULL. */
const char *scan_name(const ScanResult *sr, uint32_t id, size_t *len);
/* Id of a name, compared like VHDL identifiers; 0 if never seen. */
uint32_t scan_find(const ScanResult *sr, const char *name);

const char *scan_kind_name(uint32_t kind);

uint64_t scan_hash(const unsigned char *p, size_t len);

#endif
