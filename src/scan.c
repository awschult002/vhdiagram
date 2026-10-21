/*
 * scan.c - scan step: list the design units in VHDL files; see scan.h.
 *
 * This is a token walk, not a parser. Outside any unit only context
 * clauses (library, use, context references) and unit headers are
 * looked at; a unit keyword starts a unit only there, so instantiations
 * ("u : entity work.e"), bindings ("use entity work.e",
 * "use configuration work.c"), generic packages ("package p is new
 * ... generic map (<>)") and packages nested in an architecture are
 * never mistaken for units.
 *
 * Inside a unit the walker keeps a depth: the number of open constructs
 * that close with their own "end". It does not count "is" (type,
 * subtype, alias and attribute declarations have "is" and no "end").
 * Openers, each counted when not directly after "end":
 *   process, block, record, units, protected, component (not after ":"),
 *   if / case (sequential statements, or if/case generate), loop,
 *   generate (for-generate only: the "generate" of an if/case/elsif/else
 *   generate belongs to the if or case already counted),
 *   function/procedure bodies ("is" not followed by "new" or "<>"),
 *   nested "package p is" (not "is new") and "package body p is",
 *   and, in a configuration only, "for" (block and component
 *   configurations close with "end for").
 * Every "end" closes one construct; an "end" at depth 0 ends the unit,
 * whatever follows it (end; end entity; end rtl; ...). Outside a
 * configuration, "end for" (an optional end of a configuration
 * specification) closes nothing. Known gap: the optional "end alt;" of a
 * 2008 generate alternative is not modelled.
 */
#include "scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex.h"

#define NO_UNIT ((uint32_t)-1)

void scan_init(ScanResult *sr)
{
	memset(sr, 0, sizeof *sr);
	arena_init(&sr->arena);
	strtab_init(&sr->names);
}

void scan_free(ScanResult *sr)
{
	strtab_free(&sr->names);
	arena_free(&sr->arena);
	free(sr->file);
	free(sr->unit);
	free(sr->ref);
	scan_init(sr);
}

uint64_t scan_hash(const unsigned char *p, size_t len)
{
	uint64_t h = UINT64_C(14695981039346656037); /* FNV-1a 64 */
	size_t i;

	for (i = 0; i < len; i++) {
		h ^= p[i];
		h *= UINT64_C(1099511628211);
	}
	return h;
}

/* Make room for one more element in a growable array. */
static int reserve(void **arr, size_t *cap, size_t n, size_t elem)
{
	size_t new_cap;
	void *p;

	if (n < *cap)
		return 0;
	new_cap = *cap ? *cap * 2 : 16;
	if (new_cap > (size_t)-1 / elem)
		return -1;
	p = realloc(*arr, new_cap * elem);
	if (!p)
		return -1;
	*arr = p;
	*cap = new_cap;
	return 0;
}

/* Id of p[0..len), copying the bytes into the arena the first time the
 * name is seen. Returns 0 on out-of-memory. */
static uint32_t store_name(ScanResult *sr, const unsigned char *p, size_t len)
{
	uint32_t id = strtab_find(&sr->names, (const char *)p, len);
	char *copy;

	if (id)
		return id;
	copy = arena_strdup(&sr->arena, p, len);
	if (!copy)
		return 0;
	return strtab_intern(&sr->names, (const unsigned char *)copy, len);
}

const char *scan_name(const ScanResult *sr, uint32_t id, size_t *len)
{
	size_t n;
	const unsigned char *p = strtab_bytes(&sr->names, id, &n);

	if (len)
		*len = n;
	return p ? (const char *)p : "";
}

uint32_t scan_find(const ScanResult *sr, const char *name)
{
	return strtab_find(&sr->names, name, strlen(name));
}

const char *scan_kind_name(uint32_t kind)
{
	switch (kind) {
	case SCAN_ENTITY:        return "entity";
	case SCAN_ARCHITECTURE:  return "architecture";
	case SCAN_PACKAGE:       return "package";
	case SCAN_PACKAGE_BODY:  return "package body";
	case SCAN_PACKAGE_INST:  return "package instance";
	case SCAN_CONFIGURATION: return "configuration";
	case SCAN_CONTEXT:       return "context";
	case SCAN_LIBRARY:       return "library";
	case SCAN_USE:           return "use";
	case SCAN_CONTEXT_REF:   return "context reference";
	}
	return "?";
}

/* ---- walking the tokens ---------------------------------------------- */

/* State for one file. Only non-trivia tokens matter, so their indices
 * are collected once and the walker moves over that list. */
typedef struct {
	ScanResult *sr;
	const LexFile *lf;
	size_t *code;        /* indices of non-trivia tokens */
	size_t ncode;
	uint32_t file;
	size_t first_ref;    /* refs from here on wait for the next unit */
	int in_unit;         /* inside a unit, between its header and end */
	int in_config;       /* the current unit is a configuration */
	size_t depth;        /* open constructs inside the unit, see above */
	size_t skip_gen;     /* "generate" already counted by its if/case */
} Walk;

static uint32_t kind_at(const Walk *w, size_t i)
{
	return i < w->ncode ? w->lf->tok[w->code[i]].kind : TK_NONE;
}

static int is_name(uint32_t kind)
{
	return kind == TK_IDENT || kind == TK_EXT_IDENT;
}

static const unsigned char *text_at(const Walk *w, size_t i, size_t *len)
{
	const Token *t = &w->lf->tok[w->code[i]];

	*len = t->len;
	return w->lf->src + t->off;
}

static uint32_t name_at(Walk *w, size_t i)
{
	size_t len;
	const unsigned char *p = text_at(w, i, &len);

	return store_name(w->sr, p, len);
}

static uint32_t line_of(const Walk *w, size_t i)
{
	const unsigned char *p = w->lf->src;
	size_t end = w->lf->tok[w->code[i]].off, k;
	uint32_t line = 1;

	for (k = 0; k < end; k++)
		if (p[k] == '\n' || (p[k] == '\r' && (k + 1 >= end ||
						      p[k + 1] != '\n')))
			line++;
	return line;
}

static int add_unit(Walk *w, uint32_t kind, size_t name_tok, uint32_t of,
		    size_t kw_tok)
{
	ScanResult *sr = w->sr;
	ScanUnit *u;
	uint32_t name = name_at(w, name_tok);
	size_t r;

	if (!name || reserve((void **)&sr->unit, &sr->unit_cap, sr->nunit,
			     sizeof *sr->unit))
		return -1;
	u = &sr->unit[sr->nunit];
	u->kind = kind;
	u->name = name;
	u->of = of;
	u->file = w->file;
	u->line = line_of(w, kw_tok);
	/* the context clause just before the unit belongs to it */
	for (r = w->first_ref; r < sr->nref; r++)
		sr->ref[r].unit = (uint32_t)sr->nunit;
	w->first_ref = sr->nref;
	sr->nunit++;

	w->in_unit = 1;
	w->in_config = kind == SCAN_CONFIGURATION;
	w->depth = 0;
	return 0;
}

static int add_ref(Walk *w, uint32_t kind, uint32_t name)
{
	ScanResult *sr = w->sr;
	ScanRef *r;

	if (!name || reserve((void **)&sr->ref, &sr->ref_cap, sr->nref,
			     sizeof *sr->ref))
		return -1;
	r = &sr->ref[sr->nref++];
	r->kind = kind;
	r->name = name;
	r->unit = NO_UNIT;
	return 0;
}

/* Read a selected name (a.b.c, a.b.all) starting at *i. Stores it as
 * one dotted name and leaves *i after it. Returns the id, 0 if there is
 * no name at *i, or (uint32_t)-1 on out-of-memory. */
static uint32_t selected_name(Walk *w, size_t *i)
{
	size_t start = *i, j = *i, len = 0, k;
	unsigned char *buf, *out;
	uint32_t id;

	if (!is_name(kind_at(w, j)))
		return 0;
	for (;;) {
		size_t n;
		text_at(w, j, &n);
		len += n;
		j++;
		if (kind_at(w, j) != TK_DOT)
			break;
		k = kind_at(w, j + 1);
		if (!is_name(k) && k != TK_KW_ALL)
			break;
		len++;
		j++;
	}
	*i = j;
	buf = malloc(len);
	if (!buf)
		return (uint32_t)-1;
	out = buf;
	for (k = start; k < j; k++) {
		size_t n;
		const unsigned char *p = text_at(w, k, &n);
		memcpy(out, p, n);
		out += n;
	}
	id = store_name(w->sr, buf, len);
	free(buf);
	return id ? id : (uint32_t)-1;
}

/* library a, b;  use a.b.all, c.d;  context a.b; */
static int context_item(Walk *w, size_t *i, uint32_t kind)
{
	(*i)++;
	while (*i < w->ncode && kind_at(w, *i) != TK_SEMI) {
		uint32_t id = selected_name(w, i);

		if (id == (uint32_t)-1)
			return -1;
		if (id && add_ref(w, kind, id))
			return -1;
		if (!id || kind_at(w, *i) != TK_COMMA) {
			if (!id)
				(*i)++;
			continue;
		}
		(*i)++;
	}
	return 0;
}

/* From an if, case, elsif or else at i: the "generate" that comes
 * before any "then", "is", "loop" or ";" (outside parentheses), or
 * (size_t)-1 if the statement is not a generate. */
static size_t generate_ahead(const Walk *w, size_t i)
{
	size_t paren = 0;

	for (i++; i < w->ncode; i++) {
		uint32_t k = kind_at(w, i);

		if (k == TK_LPAREN)
			paren++;
		else if (k == TK_RPAREN && paren > 0)
			paren--;
		else if (paren > 0)
			continue;
		else if (k == TK_KW_GENERATE)
			return i;
		else if (k == TK_KW_THEN || k == TK_KW_IS || k == TK_KW_LOOP ||
			 k == TK_SEMI)
			break;
	}
	return (size_t)-1;
}

/* From a function or procedure at i: is this a body? A declaration
 * ends in ";" (or ")" in a generic list) before any "is"; "is new" is an
 * instantiation and "is <>" a generic default. */
static int subprogram_body(const Walk *w, size_t i)
{
	size_t paren = 0;

	for (i++; i < w->ncode; i++) {
		uint32_t k = kind_at(w, i);

		if (k == TK_LPAREN) {
			paren++;
		} else if (k == TK_RPAREN) {
			if (paren == 0)
				return 0;
			paren--;
		} else if (paren == 0 && k == TK_SEMI) {
			return 0;
		} else if (paren == 0 && k == TK_KW_IS) {
			k = kind_at(w, i + 1);
			if (k == TK_KW_NEW || k == TK_BOX)
				return 0;
			/* "is default_name" closing a generic item */
			k = kind_at(w, i + 2);
			if (is_name(kind_at(w, i + 1)) &&
			    (k == TK_SEMI || k == TK_RPAREN))
				return 0;
			return 1;
		}
	}
	return 0;
}

/* Does code token i open a construct that closes with "end"? */
static int opens_block(Walk *w, size_t i)
{
	uint32_t k = kind_at(w, i);
	uint32_t prev = i > 0 ? kind_at(w, i - 1) : TK_NONE;

	if (prev == TK_KW_END)
		return 0;
	switch (k) {
	case TK_KW_PROCESS:
	case TK_KW_BLOCK:
	case TK_KW_RECORD:
	case TK_KW_UNITS:
	case TK_KW_PROTECTED:
	case TK_KW_LOOP:
		return 1;
	case TK_KW_COMPONENT:
		return prev != TK_COLON;
	case TK_KW_IF:
	case TK_KW_CASE:
		w->skip_gen = generate_ahead(w, i);
		return 1;
	case TK_KW_ELSIF:
	case TK_KW_ELSE:
		w->skip_gen = generate_ahead(w, i);
		return 0;
	case TK_KW_GENERATE:
		return i != w->skip_gen;
	case TK_KW_FUNCTION:
	case TK_KW_PROCEDURE:
		return subprogram_body(w, i);
	case TK_KW_PACKAGE:
		if (kind_at(w, i + 1) == TK_KW_BODY)
			return 1;
		return kind_at(w, i + 2) == TK_KW_IS &&
		       kind_at(w, i + 3) != TK_KW_NEW;
	case TK_KW_FOR:
		return w->in_config;
	}
	return 0;
}

/* Inside a unit: track depth and notice the unit's end. */
static void inside_unit(Walk *w, size_t i)
{
	if (kind_at(w, i) == TK_KW_END) {
		if (kind_at(w, i + 1) == TK_KW_FOR && !w->in_config)
			return;
		if (w->depth == 0)
			w->in_unit = 0;
		else
			w->depth--;
		return;
	}
	if (opens_block(w, i))
		w->depth++;
}

/* Recognize a unit header at code token i. Returns 1 if one was added,
 * 0 if not, -1 on out-of-memory. */
static int unit_header(Walk *w, size_t i)
{
	uint32_t k = kind_at(w, i);
	uint32_t of;

	switch (k) {
	case TK_KW_ENTITY:      /* entity e is */
	case TK_KW_CONTEXT:     /* context c is */
		if (!is_name(kind_at(w, i + 1)) || kind_at(w, i + 2) != TK_KW_IS)
			return 0;
		return add_unit(w, k == TK_KW_ENTITY ? SCAN_ENTITY : SCAN_CONTEXT,
				i + 1, 0, i) ? -1 : 1;
	case TK_KW_ARCHITECTURE:     /* architecture a of e is */
	case TK_KW_CONFIGURATION:    /* configuration c of e is */
		if (!is_name(kind_at(w, i + 1)) || kind_at(w, i + 2) != TK_KW_OF ||
		    !is_name(kind_at(w, i + 3)) || kind_at(w, i + 4) != TK_KW_IS)
			return 0;
		of = name_at(w, i + 3);
		if (!of)
			return -1;
		return add_unit(w, k == TK_KW_ARCHITECTURE ? SCAN_ARCHITECTURE :
				SCAN_CONFIGURATION, i + 1, of, i) ? -1 : 1;
	case TK_KW_PACKAGE:
		if (kind_at(w, i + 1) == TK_KW_BODY) {  /* package body p is */
			if (!is_name(kind_at(w, i + 2)) ||
			    kind_at(w, i + 3) != TK_KW_IS)
				return 0;
			of = name_at(w, i + 2);
			if (!of)
				return -1;
			return add_unit(w, SCAN_PACKAGE_BODY, i + 2, of, i) ? -1 : 1;
		}
		if (!is_name(kind_at(w, i + 1)) || kind_at(w, i + 2) != TK_KW_IS)
			return 0;
		return add_unit(w, kind_at(w, i + 3) == TK_KW_NEW ?
				SCAN_PACKAGE_INST : SCAN_PACKAGE,
				i + 1, 0, i) ? -1 : 1;
	}
	return 0;
}

static int walk(Walk *w)
{
	size_t i = 0;

	w->skip_gen = (size_t)-1;
	while (i < w->ncode) {
		uint32_t k = kind_at(w, i);

		if (w->in_unit) {
			inside_unit(w, i);
			i++;
			continue;
		}
		if (k == TK_KW_LIBRARY || k == TK_KW_USE ||
		    (k == TK_KW_CONTEXT && kind_at(w, i + 2) != TK_KW_IS)) {
			uint32_t kind = k == TK_KW_LIBRARY ? SCAN_LIBRARY :
					k == TK_KW_USE ? SCAN_USE :
					SCAN_CONTEXT_REF;
			if (context_item(w, &i, kind))
				return -1;
			continue;
		}
		if (unit_header(w, i) < 0)
			return -1;
		i++;
	}
	return 0;
}

static int add_file(ScanResult *sr, const char *path)
{
	ScanFile *f;
	const char *copy;

	if (reserve((void **)&sr->file, &sr->file_cap, sr->nfile,
		    sizeof *sr->file))
		return -1;
	copy = arena_strdup(&sr->arena, path, strlen(path));
	if (!copy)
		return -1;
	f = &sr->file[sr->nfile++];
	memset(f, 0, sizeof *f);
	f->path = copy;
	return 0;
}

int scan_buffer(ScanResult *sr, const char *path,
		const unsigned char *src, size_t len)
{
	LexFile lf;
	Walk w;
	ScanFile *f;
	size_t i;
	int rc = -1;

	if (add_file(sr, path))
		return -1;
	f = &sr->file[sr->nfile - 1];
	f->hash = scan_hash(src, len);
	f->size = len;
	if (lex_file(&lf, src, len, NULL) != 0) {
		lex_free(&lf);
		return -1;
	}
	f->nerr = lf.nerr;

	memset(&w, 0, sizeof w);
	w.sr = sr;
	w.lf = &lf;
	w.file = (uint32_t)(sr->nfile - 1);
	w.first_ref = sr->nref;
	w.code = malloc((lf.ntok ? lf.ntok : 1) * sizeof *w.code);
	if (w.code) {
		for (i = 0; i < lf.ntok; i++)
			if (!lex_is_trivia(lf.tok[i].kind))
				w.code[w.ncode++] = i;
		rc = walk(&w);
	}
	free(w.code);
	lex_free(&lf);
	return rc;
}

int scan_path(ScanResult *sr, const char *path)
{
	FILE *fp = fopen(path, "rb");
	unsigned char *buf = NULL;
	size_t len = 0, cap = 0;
	int rc;

	if (fp) {
		for (;;) {
			size_t got;
			if (len == cap) {
				size_t new_cap = cap ? cap * 2 : 64 * 1024;
				unsigned char *p = realloc(buf, new_cap);
				if (!p) {
					free(buf);
					fclose(fp);
					return -1;
				}
				buf = p;
				cap = new_cap;
			}
			got = fread(buf + len, 1, cap - len, fp);
			len += got;
			if (got == 0)
				break;
		}
		if (ferror(fp)) {
			free(buf);
			buf = NULL;
		}
		fclose(fp);
	}
	if (!buf) {
		if (add_file(sr, path))
			return -1;
		sr->file[sr->nfile - 1].failed = 1;
		return 0;
	}
	rc = scan_buffer(sr, path, buf, len);
	free(buf);   /* only names stay in memory */
	return rc;
}
