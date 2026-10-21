/* scan.c - scan step: list the design units in VHDL files; see scan.h. */
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
	size_t unit_kw;      /* keyword kind that opened the current unit */
	size_t unit_name_tok;
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

/* Do code tokens i and j spell the same VHDL name? */
static int same_name(const Walk *w, size_t i, size_t j)
{
	size_t ilen, jlen, k;
	const unsigned char *a = text_at(w, i, &ilen);
	const unsigned char *b = text_at(w, j, &jlen);

	if (ilen != jlen)
		return 0;
	if (kind_at(w, i) == TK_EXT_IDENT || kind_at(w, j) == TK_EXT_IDENT)
		return kind_at(w, i) == kind_at(w, j) &&
		       memcmp(a, b, ilen) == 0;
	for (k = 0; k < ilen; k++) {
		unsigned char x = a[k], y = b[k];
		if (x >= 'A' && x <= 'Z')
			x = (unsigned char)(x - 'A' + 'a');
		if (y >= 'A' && y <= 'Z')
			y = (unsigned char)(y - 'A' + 'a');
		if (x != y)
			return 0;
	}
	return 1;
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
	w->unit_kw = kind_at(w, kw_tok);
	w->unit_name_tok = name_tok;
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
		if (id && !w->in_unit && add_ref(w, kind, id))
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

/* Starts a design unit after the end of a bare "end;"? */
static int starts_unit(uint32_t kind)
{
	return kind == TK_NONE || kind == TK_KW_LIBRARY ||
	       kind == TK_KW_USE || kind == TK_KW_CONTEXT ||
	       kind == TK_KW_ENTITY || kind == TK_KW_ARCHITECTURE ||
	       kind == TK_KW_PACKAGE || kind == TK_KW_CONFIGURATION;
}

/* At an "end": does it close the current unit? Accepted forms are
 * "end [unit-keyword] [name] ;" where the keyword or name matches the
 * unit, or a bare "end ;" followed by the start of another unit or the
 * end of the file. Nested "end process;", "end if;" do not match. */
static int ends_unit(const Walk *w, size_t i)
{
	size_t j = i + 1;
	int matched = 0;

	if (kind_at(w, j) == (uint32_t)w->unit_kw) {
		matched = 1;
		j++;
		if (w->unit_kw == TK_KW_PACKAGE && kind_at(w, j) == TK_KW_BODY)
			j++;
	}
	if (is_name(kind_at(w, j))) {
		if (!same_name(w, j, w->unit_name_tok))
			return 0;
		matched = 1;
		j++;
	}
	if (kind_at(w, j) != TK_SEMI)
		return 0;
	return matched || starts_unit(kind_at(w, j + 1));
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
		/* "u : entity work.e" never has "is" after the name */
		if (i > 0 && kind_at(w, i - 1) == TK_COLON)
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

	while (i < w->ncode) {
		uint32_t k = kind_at(w, i);
		int r;

		if (k == TK_KW_LIBRARY || k == TK_KW_USE ||
		    (k == TK_KW_CONTEXT && kind_at(w, i + 2) != TK_KW_IS)) {
			uint32_t kind = k == TK_KW_LIBRARY ? SCAN_LIBRARY :
					k == TK_KW_USE ? SCAN_USE :
					SCAN_CONTEXT_REF;
			/* "use entity work.e" in a configuration is no clause */
			if (k == TK_KW_USE && !is_name(kind_at(w, i + 1))) {
				i++;
				continue;
			}
			if (context_item(w, &i, kind))
				return -1;
			continue;
		}
		if (w->in_unit && k == TK_KW_END && ends_unit(w, i)) {
			w->in_unit = 0;
			i++;
			continue;
		}
		/* A unit header is accepted even inside a unit, so a unit
		 * whose end was not recognized cannot hide the next one. */
		r = unit_header(w, i);
		if (r < 0)
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
