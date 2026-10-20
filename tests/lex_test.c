/*
 * lex_test.c - table-driven tests for the VHDL-2008 tokenizer.
 *
 * Every case is also checked for the core invariant: the tokens tile the
 * input exactly (contiguous, non-empty), so joining them reproduces the
 * input byte for byte.
 *
 * Usage: lex_test [fixture-dir]   (default: tests/fixtures)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex.h"
#include "strtab.h"

static int ntests, nfail;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
		ok = 0; \
	} \
} while (0)

static void report(const char *name, int ok)
{
	ntests++;
	if (!ok) {
		nfail++;
		printf("FAIL %s\n", name);
	}
}

/* The invariant: tokens are non-empty, contiguous and cover the input,
 * and joining their bytes gives back the input exactly. */
static int tiles_input(const LexFile *lf)
{
	size_t i, off = 0;
	unsigned char *buf = malloc(lf->src_len + 1);
	int ok = buf != NULL;

	for (i = 0; ok && i < lf->ntok; i++) {
		const Token *t = &lf->tok[i];
		if (t->len == 0 || t->off != off) {
			ok = 0;
			break;
		}
		memcpy(buf + off, lf->src + t->off, t->len);
		off += t->len;
	}
	ok = ok && off == lf->src_len &&
	     memcmp(buf, lf->src, lf->src_len) == 0;
	free(buf);
	return ok;
}

static void print_tokens(const LexFile *lf)
{
	size_t i;

	for (i = 0; i < lf->ntok; i++) {
		const Token *t = &lf->tok[i];
		printf("    [%u+%u] %-14s '%.*s'\n", (unsigned)t->off,
		       (unsigned)t->len, lex_kind_name(t->kind), (int)t->len,
		       (const char *)lf->src + t->off);
	}
}

/* ---- table-driven token cases --------------------------------------- */

typedef struct {
	uint32_t kind;
	const char *text;
} Exp;

typedef struct {
	const char *name;
	const char *src;
	int trivia;      /* 1: expect every token, 0: skip trivia */
	Exp exp[32];     /* terminated by { TK_NONE } */
} Case;

#define E(k, t) { TK_##k, t }
#define KW(k, t) { TK_KW_##k, t }

static const Case cases[] = {
	{ "dash-dash inside a string", "s := \"a--b\"; -- c", 1, {
		E(IDENT, "s"), E(WS, " "), E(ASSIGN, ":="), E(WS, " "),
		E(STRING, "\"a--b\""), E(SEMI, ";"), E(WS, " "),
		E(LINE_COMMENT, "-- c") } },
	{ "block comment containing --", "/* a -- b */x", 1, {
		E(BLOCK_COMMENT, "/* a -- b */"), E(IDENT, "x") } },
	{ "block comments do not nest", "/* a /* b */ c */", 1, {
		E(BLOCK_COMMENT, "/* a /* b */"), E(WS, " "), E(IDENT, "c"),
		E(WS, " "), E(STAR, "*"), E(SLASH, "/") } },
	{ "block comment across CRLF lines", "a/*1\r\n2*/b", 1, {
		E(IDENT, "a"), E(BLOCK_COMMENT, "/*1\r\n2*/"),
		E(IDENT, "b") } },
	{ "line comment containing /*", "-- /* x\ny", 1, {
		E(LINE_COMMENT, "-- /* x"), E(WS, "\n"), E(IDENT, "y") } },
	{ "line comment stops before CR", "--x\r\ny", 1, {
		E(LINE_COMMENT, "--x"), E(WS, "\r\n"), E(IDENT, "y") } },
	{ "comment at end of input", "a --", 1, {
		E(IDENT, "a"), E(WS, " "), E(LINE_COMMENT, "--") } },
	{ "unterminated block comment", "a /* b\n c", 0, {
		E(IDENT, "a"), E(ERROR, "/* b\n c") } },
	{ "strings with doubled quotes", "\"a\"\"b\" \"\" \"\"\"\"", 0, {
		E(STRING, "\"a\"\"b\""), E(STRING, "\"\""),
		E(STRING, "\"\"\"\"") } },
	{ "unterminated string stops at line end", "\"abc\nx", 1, {
		E(ERROR, "\"abc"), E(WS, "\n"), E(IDENT, "x") } },
	{ "extended identifiers", "\\my sig\\ \\a\\\\b\\ \\Foo\\", 0, {
		E(EXT_IDENT, "\\my sig\\"), E(EXT_IDENT, "\\a\\\\b\\"),
		E(EXT_IDENT, "\\Foo\\") } },
	{ "extended identifier with --", "\\a--b\\;", 0, {
		E(EXT_IDENT, "\\a--b\\"), E(SEMI, ";") } },
	{ "unterminated extended identifier", "\\abc\nd", 0, {
		E(ERROR, "\\abc"), E(IDENT, "d") } },
	{ "mixed-case keywords and identifiers", "ENTITY Foo_Bar Is", 0, {
		KW(ENTITY, "ENTITY"), E(IDENT, "Foo_Bar"), KW(IS, "Is") } },
	{ "end entity foo;", "end entity foo;", 1, {
		KW(END, "end"), E(WS, " "), KW(ENTITY, "entity"), E(WS, " "),
		E(IDENT, "foo"), E(SEMI, ";") } },
	{ "tick: attribute after identifier", "clk'event a'range", 0, {
		E(IDENT, "clk"), E(TICK, "'"), E(IDENT, "event"),
		E(IDENT, "a"), E(TICK, "'"), KW(RANGE, "range") } },
	{ "tick: a'b' style", "a'b'", 0, {
		E(IDENT, "a"), E(TICK, "'"), E(IDENT, "b"), E(TICK, "'") } },
	{ "tick: char literal in parens", "f('b')", 0, {
		E(IDENT, "f"), E(LPAREN, "("), E(CHAR, "'b'"),
		E(RPAREN, ")") } },
	{ "tick: x'('0') qualified expression", "x'('0')", 0, {
		E(IDENT, "x"), E(TICK, "'"), E(LPAREN, "("), E(CHAR, "'0'"),
		E(RPAREN, ")") } },
	{ "tick: std_logic'('1')", "std_logic'('1')", 0, {
		E(IDENT, "std_logic"), E(TICK, "'"), E(LPAREN, "("),
		E(CHAR, "'1'"), E(RPAREN, ")") } },
	{ "tick: string'(\"01\")", "string'(\"01\")", 0, {
		E(IDENT, "string"), E(TICK, "'"), E(LPAREN, "("),
		E(STRING, "\"01\""), E(RPAREN, ")") } },
	{ "tick: ''' is a char literal", "c = '''", 0, {
		E(IDENT, "c"), E(EQ, "="), E(CHAR, "'''") } },
	{ "tick: else'0' is a char literal", "else'0'", 0, {
		KW(ELSE, "else"), E(CHAR, "'0'") } },
	{ "tick: after ) ] and all", "f(x)'length g[bit]'x p.all'length", 0, {
		E(IDENT, "f"), E(LPAREN, "("), E(IDENT, "x"), E(RPAREN, ")"),
		E(TICK, "'"), E(IDENT, "length"),
		E(IDENT, "g"), E(LBRACKET, "["), E(IDENT, "bit"),
		E(RBRACKET, "]"), E(TICK, "'"), E(IDENT, "x"),
		E(IDENT, "p"), E(DOT, "."), KW(ALL, "all"), E(TICK, "'"),
		E(IDENT, "length") } },
	{ "tick: after extended identifier", "\\t\\'('1')", 0, {
		E(EXT_IDENT, "\\t\\"), E(TICK, "'"), E(LPAREN, "("),
		E(CHAR, "'1'"), E(RPAREN, ")") } },
	{ "tick: others => '0'", "(others => '0')", 0, {
		E(LPAREN, "("), KW(OTHERS, "others"), E(ARROW, "=>"),
		E(CHAR, "'0'"), E(RPAREN, ")") } },
	{ "tick: stray tick in a -- comment", "a -- it's\n'1'", 1, {
		E(IDENT, "a"), E(WS, " "), E(LINE_COMMENT, "-- it's"),
		E(WS, "\n"), E(CHAR, "'1'") } },
	{ "tick: only directly after the prefix", "a '1' f(x) 'b'", 0, {
		E(IDENT, "a"), E(CHAR, "'1'"), E(IDENT, "f"), E(LPAREN, "("),
		E(IDENT, "x"), E(RPAREN, ")"), E(CHAR, "'b'") } },
	{ "tick: stray tick is an error", "= ' x", 0, {
		E(EQ, "="), E(ERROR, "'"), E(IDENT, "x") } },
	{ "bit strings", "x\"FF\" b\"1010\" o\"7\" d\"12\" X\"f_f\"", 0, {
		E(BITSTRING, "x\"FF\""), E(BITSTRING, "b\"1010\""),
		E(BITSTRING, "o\"7\""), E(BITSTRING, "d\"12\""),
		E(BITSTRING, "X\"f_f\"") } },
	{ "bit strings, 2008 forms", "8x\"FF\" UX\"0F\" sx\"F\" 12SB\"1\" 3d\"5\"", 0, {
		E(BITSTRING, "8x\"FF\""), E(BITSTRING, "UX\"0F\""),
		E(BITSTRING, "sx\"F\""), E(BITSTRING, "12SB\"1\""),
		E(BITSTRING, "3d\"5\"") } },
	{ "not bit strings", "xy\"1\" x \"1\" 8 x\"1\"", 0, {
		E(IDENT, "xy"), E(STRING, "\"1\""), E(IDENT, "x"),
		E(STRING, "\"1\""), E(DECIMAL, "8"), E(BITSTRING, "x\"1\"") } },
	{ "based literals", "16#FF# 2#1010_1010# 16#F.F#E+2 8#7#e1", 0, {
		E(BASED, "16#FF#"), E(BASED, "2#1010_1010#"),
		E(BASED, "16#F.F#E+2"), E(BASED, "8#7#e1") } },
	{ "decimal literals", "1_000 3.14 1.0e-9 1E6 2.5E+3", 0, {
		E(DECIMAL, "1_000"), E(DECIMAL, "3.14"), E(DECIMAL, "1.0e-9"),
		E(DECIMAL, "1E6"), E(DECIMAL, "2.5E+3") } },
	{ "decimal edge cases", "10ns 1e 7 downto 0", 0, {
		E(DECIMAL, "10"), E(IDENT, "ns"), E(DECIMAL, "1"),
		E(IDENT, "e"), E(DECIMAL, "7"), KW(DOWNTO, "downto"),
		E(DECIMAL, "0") } },
	{ "simple delimiters", "& ( ) * + , - . / : ; < = > ` | [ ] ? @", 0, {
		E(AMP, "&"), E(LPAREN, "("), E(RPAREN, ")"), E(STAR, "*"),
		E(PLUS, "+"), E(COMMA, ","), E(MINUS, "-"), E(DOT, "."),
		E(SLASH, "/"), E(COLON, ":"), E(SEMI, ";"), E(LT, "<"),
		E(EQ, "="), E(GT, ">"), E(BACKTICK, "`"), E(BAR, "|"),
		E(LBRACKET, "["), E(RBRACKET, "]"), E(QUESTION, "?"),
		E(AT, "@") } },
	{ "compound delimiters", "=> <= := /= >= ** <> ?? ?= ?/= ?< ?<= ?> ?>= << >>", 0, {
		E(ARROW, "=>"), E(LE, "<="), E(ASSIGN, ":="), E(NE, "/="),
		E(GE, ">="), E(POW, "**"), E(BOX, "<>"), E(COND, "??"),
		E(MEQ, "?="), E(MNE, "?/="), E(MLT, "?<"), E(MLE, "?<="),
		E(MGT, "?>"), E(MGE, "?>="), E(DLT, "<<"), E(DGT, ">>") } },
	{ "delimiters without spaces", "a<=<<signal .t.s:bit>>;b?/=c", 0, {
		E(IDENT, "a"), E(LE, "<="), E(DLT, "<<"), KW(SIGNAL, "signal"),
		E(DOT, "."), E(IDENT, "t"), E(DOT, "."), E(IDENT, "s"),
		E(COLON, ":"), E(IDENT, "bit"), E(DGT, ">>"), E(SEMI, ";"),
		E(IDENT, "b"), E(MNE, "?/="), E(IDENT, "c") } },
	{ "CRLF keeps exact offsets", "a\r\n\r\n  b\r\n", 1, {
		E(IDENT, "a"), E(WS, "\r\n\r\n  "), E(IDENT, "b"),
		E(WS, "\r\n") } },
	{ "Latin-1 byte inside a comment", "-- caf\xE9\r\nx", 1, {
		E(LINE_COMMENT, "-- caf\xE9"), E(WS, "\r\n"), E(IDENT, "x") } },
	{ "Latin-1 bytes in string and char literals", "\"\xB0\" '\xE9'", 0, {
		E(STRING, "\"\xB0\""), E(CHAR, "'\xE9'") } },
	{ "unknown bytes are single errors", "a $ b", 0, {
		E(IDENT, "a"), E(ERROR, "$"), E(IDENT, "b") } },
};

static int run_case(const Case *c)
{
	LexFile lf;
	size_t i, e = 0;
	int ok = 1;

	if (lex_file(&lf, (const unsigned char *)c->src, strlen(c->src),
		     NULL) != 0) {
		printf("  FAIL lex_file returned an error\n");
		return 0;
	}
	CHECK(tiles_input(&lf), "tokens do not tile the input");
	for (i = 0; ok && i < lf.ntok; i++) {
		const Token *t = &lf.tok[i];
		const Exp *x = &c->exp[e];

		if (!c->trivia && lex_is_trivia(t->kind))
			continue;
		CHECK(x->kind != TK_NONE, "unexpected extra token %s",
		      lex_kind_name(t->kind));
		if (!ok)
			break;
		CHECK(t->kind == x->kind && t->len == strlen(x->text) &&
		      memcmp(c->src + t->off, x->text, t->len) == 0,
		      "token %u: got %s '%.*s', want %s '%s'", (unsigned)e,
		      lex_kind_name(t->kind), (int)t->len, c->src + t->off,
		      lex_kind_name(x->kind), x->text);
		e++;
	}
	CHECK(!ok || c->exp[e].kind == TK_NONE, "missing token %s '%s'",
	      lex_kind_name(c->exp[e].kind), c->exp[e].text);
	if (!ok)
		print_tokens(&lf);
	lex_free(&lf);
	return ok;
}

/* ---- other tests ------------------------------------------------------ */

static int test_keyword_table(void)
{
	int ok = 1;
	uint32_t k;

	for (k = TK_KW_FIRST; k < TK_KW_LAST; k++)
		CHECK(strcmp(lex_kind_name(k), lex_kind_name(k + 1)) < 0,
		      "keywords out of order: %s, %s", lex_kind_name(k),
		      lex_kind_name(k + 1));
	for (k = TK_KW_FIRST; k <= TK_KW_LAST; k++) {
		const char *s = lex_kind_name(k);
		CHECK(lex_keyword((const unsigned char *)s, strlen(s)) == k,
		      "lookup of %s failed", s);
	}
	CHECK(lex_keyword((const unsigned char *)"EnTiTy", 6) == TK_KW_ENTITY,
	      "case-insensitive lookup");
	CHECK(lex_keyword((const unsigned char *)"entit", 5) == 0, "prefix");
	CHECK(lex_keyword((const unsigned char *)"std_logic", 9) == 0,
	      "non-keyword");
	CHECK(strcmp(lex_kind_name(TK_TICK), "TICK") == 0 &&
	      strcmp(lex_kind_name(TK_MGE), "'?>='") == 0,
	      "kind names out of step with the enum");
	return ok;
}

static int test_interning(void)
{
	static const char src[] =
		"Foo foo FOO \\Foo\\ \\foo\\ \\Foo\\ bar entity";
	Strtab st;
	LexFile lf;
	uint32_t id[8];
	size_t i, n = 0;
	int ok = 1;

	strtab_init(&st);
	if (lex_file(&lf, (const unsigned char *)src, sizeof src - 1,
		     &st) != 0)
		return 0;
	for (i = 0; i < lf.ntok && n < 8; i++)
		if (lf.tok[i].kind == TK_IDENT ||
		    lf.tok[i].kind == TK_EXT_IDENT)
			id[n++] = lf.sym[i];
	CHECK(n == 7, "expected 7 identifiers, got %u", (unsigned)n);
	if (ok) {
		size_t len;
		const unsigned char *p = strtab_bytes(&st, id[1], &len);

		CHECK(id[0] && id[0] == id[1] && id[1] == id[2],
		      "basic identifiers fold case");
		CHECK(id[3] == id[5] && id[3] != id[4],
		      "extended identifiers are case-sensitive");
		CHECK(id[0] != id[3], "\\Foo\\ differs from Foo");
		CHECK(strtab_count(&st) == 4, "4 distinct names, got %u",
		      (unsigned)strtab_count(&st));
		/* storage keeps the original bytes of the first occurrence */
		CHECK(p == (const unsigned char *)src && len == 3,
		      "entry points at the original bytes");
		CHECK(strtab_find(&st, "FOO", 3) == id[0], "find folded");
		CHECK(strtab_find(&st, "\\FOO\\", 5) == 0, "find exact");
		CHECK(strtab_find(&st, "entity", 6) == 0,
		      "keywords are not interned");
	}
	lex_free(&lf);
	strtab_free(&st);
	return ok;
}

static int test_strtab_grows(void)
{
	Strtab st;
	char (*names)[16] = malloc(5000 * sizeof *names);
	uint32_t i;
	int ok = names != NULL;

	strtab_init(&st);
	for (i = 0; ok && i < 5000; i++) {
		sprintf(names[i], "n%u", (unsigned)i);
		CHECK(strtab_intern(&st, (unsigned char *)names[i],
				    strlen(names[i])) == i + 1, "id %u", i);
	}
	for (i = 0; ok && i < 5000; i++)
		CHECK(strtab_find(&st, names[i], strlen(names[i])) == i + 1,
		      "find %u", i);
	strtab_free(&st);
	free(names);
	return ok;
}

static int test_doc_comment(void)
{
	static const char src[] =
		"x; -- trailing\r\n"
		"-- unrelated\r\n"
		"\r\n"
		"-- Header line 1\r\n"
		"  /* block */ -- line 2\r\n"
		"entity e is end;\r\n"
		"entity f is end;\r\n";
	LexFile lf;
	size_t i, first, n, ent[2], ne = 0;
	int ok = 1;

	if (lex_file(&lf, (const unsigned char *)src, sizeof src - 1,
		     NULL) != 0)
		return 0;
	for (i = 0; i < lf.ntok; i++)
		if (lf.tok[i].kind == TK_KW_ENTITY && ne < 2)
			ent[ne++] = i;
	CHECK(ne == 2, "two entities");
	if (ok) {
		const Token *a, *b;

		n = lex_doc_comment(&lf, ent[0], &first);
		CHECK(n > 0, "header found");
		a = &lf.tok[first];
		b = &lf.tok[ent[0] - 1];
		CHECK(a->kind == TK_LINE_COMMENT &&
		      memcmp(src + a->off, "-- Header line 1", 16) == 0,
		      "header starts at line 1, got '%.*s'", (int)a->len,
		      src + a->off);
		CHECK(b->kind == TK_WS, "whitespace before entity");
		/* the header itself is an exact byte range of the source */
		CHECK(a->off == 32, "header offset %u", (unsigned)a->off);
		n = lex_doc_comment(&lf, ent[1], &first);
		CHECK(n == 0, "no header on the second entity");
		n = lex_doc_comment(&lf, 2, &first);
		CHECK(n == 0, "trailing comment is not a header");
	}
	lex_free(&lf);
	return ok;
}

static int test_crlf_offsets(void)
{
	static const char src[] = "a\r\n-- c\r\nb;\r\n";
	LexFile lf;
	int ok = 1;

	if (lex_file(&lf, (const unsigned char *)src, sizeof src - 1,
		     NULL) != 0)
		return 0;
	CHECK(lf.ntok == 7, "ntok %u", (unsigned)lf.ntok);
	if (ok) {
		CHECK(lf.tok[2].kind == TK_LINE_COMMENT &&
		      lf.tok[2].off == 3 && lf.tok[2].len == 4,
		      "comment at 3+4");
		CHECK(lf.tok[4].kind == TK_IDENT && lf.tok[4].off == 9,
		      "b at 9");
		CHECK(lf.tok[5].kind == TK_SEMI && lf.tok[5].off == 10,
		      "; at 10");
	}
	lex_free(&lf);
	return ok;
}

static unsigned char *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb"); /* binary: no CRLF translation */
	unsigned char *buf = NULL;
	long n;

	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 &&
	    fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t)n + 1))) {
		if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
			free(buf);
			buf = NULL;
		}
		*len = (size_t)n;
	}
	fclose(f);
	return buf;
}

static int test_fixture(const char *dir)
{
	char path[1024];
	unsigned char *src;
	size_t len = 0, i, first, ncrlf = 0, nent = 0;
	LexFile lf;
	Strtab st;
	int ok = 1;

	snprintf(path, sizeof path, "%s/crlf_latin1.vhd", dir);
	src = read_file(path, &len);
	CHECK(src != NULL, "cannot read %s", path);
	if (!src)
		return 0;
	for (i = 0; i + 1 < len; i++)
		ncrlf += src[i] == '\r' && src[i + 1] == '\n';
	CHECK(ncrlf == 14, "fixture should have 14 CRLFs (got %u); "
	      "check .gitattributes -text", (unsigned)ncrlf);

	strtab_init(&st);
	CHECK(lex_file(&lf, src, len, &st) == 0, "lex_file");
	CHECK(tiles_input(&lf), "fixture round trip");
	CHECK(lf.nerr == 0, "%u error tokens", (unsigned)lf.nerr);
	if (lf.nerr)
		print_tokens(&lf);

	/* first comment keeps the Latin-1 bytes and stops before CR */
	CHECK(lf.ntok > 0 && lf.tok[0].kind == TK_LINE_COMMENT &&
	      src[lf.tok[0].off + lf.tok[0].len] == '\r' &&
	      memchr(src, 0xE9, lf.tok[0].len) != NULL,
	      "Latin-1 header comment");

	for (i = 0; i < lf.ntok; i++) {
		if (lf.tok[i].kind != TK_KW_ENTITY || lex_prev(&lf, i) !=
		    (size_t)-1)
			continue;
		nent++;
		CHECK(lex_doc_comment(&lf, i, &first) == 4 && first == 0,
		      "two-line header above the entity");
	}
	CHECK(nent == 1, "entity keyword at top of file");
	CHECK(strtab_find(&st, "COUNTER", 7) != 0 &&
	      strtab_find(&st, "\\My Sig\\", 8) != 0, "interned names");
	lex_free(&lf);
	strtab_free(&st);
	free(src);
	return ok;
}

/* Round-trip property: random byte soup built from VHDL fragments and
 * arbitrary bytes always tiles exactly. Uses a fixed LCG, not rand(), so
 * every platform sees the same inputs. */
static int test_round_trip_property(void)
{
	static const char *const frag[] = {
		"--", "/*", "*/", "\"", "\"\"", "'", "''", "\\", "\\\\",
		"x\"", "8x\"FF\"", "16#", "#", "1_0", ".", "e+", "E", "\r\n",
		"\n", "\r", " ", "\t", "entity", "Foo", "all", ")", "]", "(",
		"<=", "?/=", "?", "<<", ">>", "=>", "\xE9", "\xFF", "\0",
	};
	uint32_t seed = 12345;
	unsigned char buf[512];
	int iter, ok = 1;

	for (iter = 0; ok && iter < 20000; iter++) {
		size_t n = 0, target;
		LexFile lf;

		seed = seed * 1103515245u + 12345u;
		target = (seed >> 16) % sizeof buf;
		while (n < target) {
			seed = seed * 1103515245u + 12345u;
			if ((seed >> 16) % 4 == 0) {
				buf[n++] = (unsigned char)(seed >> 8);
			} else {
				size_t f = (seed >> 16) % (sizeof frag /
							  sizeof frag[0]);
				size_t fl = frag[f][0] ? strlen(frag[f]) : 1;
				if (n + fl > target)
					break;
				memcpy(buf + n, frag[f], fl);
				n += fl;
			}
		}
		if (lex_file(&lf, buf, n, NULL) != 0) {
			CHECK(0, "lex_file failed at iteration %d", iter);
			break;
		}
		CHECK(tiles_input(&lf), "round trip failed at iteration %d",
		      iter);
		lex_free(&lf);
	}
	return ok;
}

static int test_empty(void)
{
	LexFile lf;
	int ok = 1;

	CHECK(lex_file(&lf, (const unsigned char *)"", 0, NULL) == 0 &&
	      lf.ntok == 0, "empty input gives no tokens");
	lex_free(&lf);
	return ok;
}

int main(int argc, char **argv)
{
	const char *fixtures = argc > 1 ? argv[1] : "tests/fixtures";
	size_t i;

	for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
		report(cases[i].name, run_case(&cases[i]));
	report("keyword table", test_keyword_table());
	report("identifier interning", test_interning());
	report("string table growth", test_strtab_grows());
	report("doc comment above entity", test_doc_comment());
	report("CRLF offsets", test_crlf_offsets());
	report("CRLF + Latin-1 fixture", test_fixture(fixtures));
	report("round-trip property (20000 random inputs)",
	       test_round_trip_property());
	report("empty input", test_empty());

	printf("lex: %d/%d tests passed\n", ntests - nfail, ntests);
	return nfail ? 1 : 0;
}
