/* lex.c - VHDL-2008 tokenizer; see lex.h. */
#include "lex.h"

#include <stdlib.h>
#include <string.h>

/* ---- tables ---------------------------------------------------------- */

static const char *const kw_text[] = {
#define X(name, text) text,
	LEX_KEYWORDS(X)
#undef X
};
#define NKW (sizeof kw_text / sizeof kw_text[0])

static const struct { uint32_t kind; const char *text; } delims[] = {
#define X(name, text) { TK_##name, text },
	LEX_DELIMS(X)
#undef X
};
#define NDELIM (sizeof delims / sizeof delims[0])

static const char *const kind_names[TK_COUNT_] = {
	"NONE", "ERROR", "WS", "LINE_COMMENT", "BLOCK_COMMENT",
	"IDENT", "EXT_IDENT", "DECIMAL", "BASED", "CHAR", "STRING",
	"BITSTRING", "TICK",
#define X(name, text) "'" text "'",
	LEX_DELIMS(X)
#undef X
	"KW_FIRST_",
#define X(name, text) text,
	LEX_KEYWORDS(X)
#undef X
};

/* ---- character classes (ASCII only; locale-independent) -------------- */

static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alpha(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static int is_space(int c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
	       c == '\v' || c == '\f';
}
/* Bytes >= 0x80 are accepted in identifiers so Latin-1 letters (legal in
 * VHDL) and other 8-bit text pass through as whole tokens. */
static int is_ident_start(int c) { return is_alpha(c) || c >= 0x80; }
static int is_ident_char(int c)
{
	return is_ident_start(c) || is_digit(c) || c == '_';
}
static int is_ext_digit(int c) { return is_digit(c) || is_alpha(c); }
static unsigned char fold(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c;
}

/* ---- public helpers -------------------------------------------------- */

int lex_is_trivia(uint32_t kind)
{
	return kind == TK_WS || kind == TK_LINE_COMMENT ||
	       kind == TK_BLOCK_COMMENT;
}

int lex_is_keyword(uint32_t kind)
{
	return kind >= TK_KW_FIRST && kind <= TK_KW_LAST;
}

const char *lex_kind_name(uint32_t kind)
{
	return kind < TK_COUNT_ ? kind_names[kind] : "?";
}

uint32_t lex_keyword(const unsigned char *p, size_t len)
{
	char buf[32];
	size_t i, lo = 0, hi = NKW;

	if (len == 0 || len >= sizeof buf)
		return 0;
	for (i = 0; i < len; i++) {
		if (!is_alpha(p[i]) && p[i] != '_')
			return 0;
		buf[i] = (char)fold(p[i]);
	}
	buf[len] = '\0';
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		int c = strcmp(buf, kw_text[mid]);
		if (c == 0)
			return (uint32_t)(TK_KW_FIRST + mid);
		if (c < 0)
			hi = mid;
		else
			lo = mid + 1;
	}
	return 0;
}

size_t lex_next(const LexFile *lf, size_t i)
{
	for (i++; i < lf->ntok; i++)
		if (!lex_is_trivia(lf->tok[i].kind))
			return i;
	return (size_t)-1;
}

size_t lex_prev(const LexFile *lf, size_t i)
{
	while (i-- > 0)
		if (!lex_is_trivia(lf->tok[i].kind))
			return i;
	return (size_t)-1;
}

/* Number of line breaks in a token (CRLF, LF and lone CR count once). */
static size_t line_breaks(const LexFile *lf, const Token *t)
{
	const unsigned char *p = lf->src + t->off;
	size_t i, n = 0;

	for (i = 0; i < t->len; i++) {
		if (p[i] == '\n')
			n++;
		else if (p[i] == '\r' && (i + 1 >= t->len || p[i + 1] != '\n'))
			n++;
	}
	return n;
}

/* Is comment token c the first thing on its line, ignoring blanks and
 * other comments? (A comment after code is that line's trailing comment.) */
static int starts_line(const LexFile *lf, size_t c)
{
	while (c-- > 0) {
		const Token *t = &lf->tok[c];

		if (t->kind == TK_WS && line_breaks(lf, t) > 0)
			return 1;
		if (t->kind != TK_WS && t->kind != TK_BLOCK_COMMENT)
			return 0;
	}
	return 1;
}

size_t lex_doc_comment(const LexFile *lf, size_t i, size_t *first)
{
	size_t j = i, start = i;

	while (j > 0) {
		const Token *t = &lf->tok[j - 1];

		if (t->kind == TK_WS) {
			if (line_breaks(lf, t) > 1)
				break;
			j--;
		} else if (t->kind == TK_LINE_COMMENT ||
			   t->kind == TK_BLOCK_COMMENT) {
			if (!starts_line(lf, j - 1))
				break; /* trailing comment of the line above */
			j--;
			start = j;
		} else {
			break;
		}
	}
	*first = start;
	return i - start;
}

/* ---- scanner --------------------------------------------------------- */

typedef struct {
	const unsigned char *s;
	size_t n;
	size_t pos;
} Scan;

static int at(const Scan *sc, size_t k)
{
	return sc->pos + k < sc->n ? sc->s[sc->pos + k] : -1;
}

static int push(LexFile *lf, uint32_t kind, size_t off, size_t len)
{
	if (lf->ntok == lf->cap) {
		size_t cap = lf->cap ? lf->cap * 2 : 256;
		Token *t = realloc(lf->tok, cap * sizeof *t);
		if (!t)
			return -1;
		lf->tok = t;
		lf->cap = cap;
	}
	lf->tok[lf->ntok].kind = kind;
	lf->tok[lf->ntok].off = (uint32_t)off;
	lf->tok[lf->ntok].len = (uint32_t)len;
	lf->ntok++;
	if (kind == TK_ERROR)
		lf->nerr++;
	return 0;
}

/* Length of a quoted run starting at i with the quote byte q, where a
 * doubled q stands for one q. Returns 0 if it is not closed before a line
 * break or the end of input. */
static size_t quoted(const Scan *sc, size_t i, unsigned char q)
{
	size_t j = i + 1;

	while (j < sc->n) {
		unsigned char c = sc->s[j];
		if (c == q) {
			if (j + 1 < sc->n && sc->s[j + 1] == q) {
				j += 2;
				continue;
			}
			return j + 1 - i;
		}
		if (c == '\n' || c == '\r')
			return 0;
		j++;
	}
	return 0;
}

/* Bytes up to (not including) the next line break, from i. */
static size_t to_eol(const Scan *sc, size_t i)
{
	size_t j = i;

	while (j < sc->n && sc->s[j] != '\n' && sc->s[j] != '\r')
		j++;
	return j - i;
}

/* Is s[i..i+len) a bit-string base specifier (b o x ub uo ux sb so sx d)? */
static int is_base_spec(const Scan *sc, size_t i, size_t len)
{
	int a = fold(sc->s[i]);
	int b = len == 2 ? fold(sc->s[i + 1]) : 0;

	if (len == 1)
		return a == 'b' || a == 'o' || a == 'x' || a == 'd';
	if (len == 2 && (a == 'u' || a == 's'))
		return b == 'b' || b == 'o' || b == 'x';
	return 0;
}

/* Run of digits and underscores from i (integer or based digits). */
static size_t digit_run(const Scan *sc, size_t i, int (*ok)(int))
{
	size_t j = i;

	while (j < sc->n && (ok(sc->s[j]) || sc->s[j] == '_'))
		j++;
	return j - i;
}

/* Optional exponent at i: [eE][+-]?digit... ; returns its length or 0. */
static size_t exponent(const Scan *sc, size_t i)
{
	size_t j = i;

	if (j >= sc->n || fold(sc->s[j]) != 'e')
		return 0;
	j++;
	if (j < sc->n && (sc->s[j] == '+' || sc->s[j] == '-'))
		j++;
	if (j >= sc->n || !is_digit(sc->s[j]))
		return 0;
	return j + digit_run(sc, j, is_digit) - i;
}

/* A number starting with a digit at pos: decimal, based or a bit string
 * with a length prefix (8x"FF"). Sets *kind and returns the length. */
static size_t number(const Scan *sc, uint32_t *kind)
{
	size_t i = sc->pos;
	size_t j = i + digit_run(sc, i, is_digit);
	size_t k;

	/* bit string with a length: 8x"FF", 12UB"..." */
	k = j;
	while (k < sc->n && is_alpha(sc->s[k]) && k - j < 3)
		k++;
	if (k > j && k - j <= 2 && k < sc->n && sc->s[k] == '"' &&
	    is_base_spec(sc, j, k - j)) {
		size_t q = quoted(sc, k, '"');
		if (q) {
			*kind = TK_BITSTRING;
			return k + q - i;
		}
	}

	/* based literal: 16#FF#, 2#1.1#e3 */
	if (j < sc->n && sc->s[j] == '#') {
		k = j + 1;
		k += digit_run(sc, k, is_ext_digit);
		if (k < sc->n && sc->s[k] == '.')
			k += 1 + digit_run(sc, k + 1, is_ext_digit);
		if (k > j + 1 && k < sc->n && sc->s[k] == '#') {
			k++;
			*kind = TK_BASED;
			return k + exponent(sc, k) - i;
		}
		/* not closed: fall back to a plain decimal before the '#' */
	}

	/* decimal: 1_000, 3.14, 1.0e-9 */
	if (j + 1 < sc->n && sc->s[j] == '.' && is_digit(sc->s[j + 1]))
		j += 1 + digit_run(sc, j + 1, is_digit);
	*kind = TK_DECIMAL;
	return j + exponent(sc, j) - i;
}

/* The apostrophe rule (docs/DESIGN.md): a tick directly after an
 * identifier, ')', ']' or the keyword 'all' is an attribute or qualified
 * expression tick. "Directly" means the previous token, with no
 * whitespace or comment in between. Any other tick starts a character
 * literal. */
static int tick_after(const LexFile *lf)
{
	uint32_t k;

	if (lf->ntok == 0)
		return 0;
	k = lf->tok[lf->ntok - 1].kind;
	return k == TK_IDENT || k == TK_EXT_IDENT || k == TK_RPAREN ||
	       k == TK_RBRACKET || k == TK_KW_ALL;
}

/* Scan one token at sc->pos. Returns its length (> 0) and sets *kind. */
static size_t next_token(const LexFile *lf, const Scan *sc, uint32_t *kind)
{
	size_t i = sc->pos, j, d, best = 0;
	int c = at(sc, 0), c1 = at(sc, 1);

	if (is_space(c)) {
		j = i;
		while (j < sc->n && is_space(sc->s[j]))
			j++;
		*kind = TK_WS;
		return j - i;
	}
	if (c == '-' && c1 == '-') {
		*kind = TK_LINE_COMMENT;
		return to_eol(sc, i);
	}
	if (c == '/' && c1 == '*') {
		for (j = i + 2; j + 1 < sc->n; j++) {
			if (sc->s[j] == '*' && sc->s[j + 1] == '/') {
				*kind = TK_BLOCK_COMMENT;
				return j + 2 - i;
			}
		}
		*kind = TK_ERROR; /* unterminated: swallow the rest */
		return sc->n - i;
	}
	if (is_ident_start(c)) {
		j = i;
		while (j < sc->n && is_ident_char(sc->s[j]))
			j++;
		/* bit string without a length: x"FF", UB"01", d"12" */
		if (j < sc->n && sc->s[j] == '"' && j - i <= 2 &&
		    is_base_spec(sc, i, j - i)) {
			size_t q = quoted(sc, j, '"');
			if (q) {
				*kind = TK_BITSTRING;
				return j + q - i;
			}
		}
		*kind = lex_keyword(sc->s + i, j - i);
		if (!*kind)
			*kind = TK_IDENT;
		return j - i;
	}
	if (is_digit(c))
		return number(sc, kind);
	if (c == '"' || c == '\\') {
		d = quoted(sc, i, (unsigned char)c);
		if (d) {
			*kind = c == '"' ? TK_STRING : TK_EXT_IDENT;
			return d;
		}
		*kind = TK_ERROR; /* unterminated: up to the line break */
		return to_eol(sc, i);
	}
	if (c == '\'') {
		if (tick_after(lf)) {
			*kind = TK_TICK;
			return 1;
		}
		if (c1 != -1 && c1 != '\n' && c1 != '\r' &&
		    at(sc, 2) == '\'') {
			*kind = TK_CHAR;
			return 3;
		}
		*kind = TK_ERROR;
		return 1;
	}
	for (d = 0; d < NDELIM; d++) {
		size_t len = strlen(delims[d].text);
		if (len > best && i + len <= sc->n &&
		    memcmp(sc->s + i, delims[d].text, len) == 0) {
			best = len;
			*kind = delims[d].kind;
		}
	}
	if (best)
		return best;
	*kind = TK_ERROR;
	return 1;
}

int lex_file(LexFile *lf, const unsigned char *src, size_t len, Strtab *st)
{
	Scan sc;
	size_t i;

	memset(lf, 0, sizeof *lf);
	lf->src = src;
	lf->src_len = len;
	if (len > UINT32_MAX)
		return -1;

	sc.s = src;
	sc.n = len;
	sc.pos = 0;
	while (sc.pos < sc.n) {
		uint32_t kind = TK_ERROR;
		size_t n = next_token(lf, &sc, &kind);

		if (n == 0) { /* cannot happen; guard against a stuck scanner */
			kind = TK_ERROR;
			n = 1;
		}
		if (push(lf, kind, sc.pos, n) != 0)
			goto oom;
		sc.pos += n;
	}

	if (st) {
		lf->sym = calloc(lf->ntok ? lf->ntok : 1, sizeof *lf->sym);
		if (!lf->sym)
			goto oom;
		for (i = 0; i < lf->ntok; i++) {
			const Token *t = &lf->tok[i];
			if (t->kind != TK_IDENT && t->kind != TK_EXT_IDENT)
				continue;
			lf->sym[i] = strtab_intern(st, src + t->off, t->len);
			if (!lf->sym[i])
				goto oom;
		}
	}
	return 0;
oom:
	lex_free(lf);
	return -1;
}

void lex_free(LexFile *lf)
{
	free(lf->tok);
	free(lf->sym);
	lf->tok = NULL;
	lf->sym = NULL;
	lf->ntok = lf->cap = lf->nerr = 0;
}
