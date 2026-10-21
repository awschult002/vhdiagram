/*
 * corpus_run.c - run the tokenizer over a list of real VHDL files and
 * check invariants on each one.
 *
 * Usage: corpus_run [-v] < file-list     (one path per line)
 *
 * Per file:
 *   1. lex_file succeeds (no crash, no OOM).
 *   2. No TK_ERROR tokens (lf.nerr == 0 and no token of kind TK_ERROR).
 *   3. Tokens tile the input: off[0] == 0, off[i+1] == off[i] + len[i],
 *      every len > 0, the last one ends at src_len, and concatenating the
 *      token spans reproduces the file byte for byte.
 *   4. Line/col: recomputed per token from the bytes (LF, CRLF and lone
 *      CR each end a line); a token never starts in the middle of a CRLF
 *      pair, and lines/cols advance monotonically.
 *   5. Tick sanity (docs/DESIGN.md apostrophe rule): a TK_TICK comes
 *      directly after an identifier, ')', ']' or 'all'; a TK_CHAR never
 *      does. Ticks/char literals where the token directly before is
 *      trivia but the last non-trivia token is one of those are counted
 *      separately ("lookback-sensitive") because the pending lookback
 *      rule would classify them differently.
 *
 * Exit status is nonzero if any file fails 1-4 or the strict part of 5.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex.h"

static int verbose;

static unsigned char *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf = NULL;
	long n;

	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 &&
	    fseek(f, 0, SEEK_SET) == 0) {
		buf = malloc((size_t)n + 1);
		if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
			free(buf);
			buf = NULL;
		}
		*len = (size_t)n;
	}
	fclose(f);
	return buf;
}

static int tick_ok_prev(uint32_t k)
{
	return k == TK_IDENT || k == TK_EXT_IDENT || k == TK_RPAREN ||
	       k == TK_RBRACKET || k == TK_KW_ALL;
}

static void where(const LexFile *lf, size_t i, unsigned line, unsigned col,
		  const char *path, const char *what)
{
	const Token *t = &lf->tok[i];
	unsigned n = t->len > 40 ? 40 : t->len;
	unsigned j;

	printf("  %s:%u:%u: %s: %s \"", path, line, col, what,
	       lex_kind_name(t->kind));
	for (j = 0; j < n; j++) {
		unsigned char c = lf->src[t->off + j];
		if (c == '\n') fputs("\\n", stdout);
		else if (c == '\r') fputs("\\r", stdout);
		else putchar(c);
	}
	printf("%s\"\n", t->len > n ? "..." : "");
}

static size_t nfiles, nfail, ntok_total, nbytes, nticks, nchars, nlookback;

static int check(const char *path)
{
	size_t len = 0, i, pos = 0, li;
	unsigned char *src = read_file(path, &len), *re;
	LexFile lf;
	unsigned line = 1, col = 1, pline = 0, pcol = 0;
	int bad = 0;

	if (!src) {
		printf("FAIL %s: cannot read\n", path);
		return 1;
	}
	memset(&lf, 0, sizeof lf);
	if (lex_file(&lf, src, len, NULL) != 0) {
		printf("FAIL %s: lex_file returned -1\n", path);
		free(src);
		return 1;
	}
	nbytes += len;
	ntok_total += lf.ntok;
	re = malloc(len + 1);
	for (i = 0; i < lf.ntok; i++) {
		const Token *t = &lf.tok[i];
		size_t k;

		if (t->len == 0 || t->off != pos || t->off + t->len > len) {
			printf("  %s: token %lu bad span off=%u len=%u "
			       "expected off=%lu\n", path, (unsigned long)i,
			       t->off, t->len, (unsigned long)pos);
			bad = 1;
			break;
		}
		if (t->off > 0 && src[t->off - 1] == '\r' &&
		    src[t->off] == '\n') {
			where(&lf, i, line, col, path, "token splits CRLF");
			bad = 1;
		}
		if (line < pline || (line == pline && col <= pcol)) {
			where(&lf, i, line, col, path, "line/col not monotonic");
			bad = 1;
		}
		pline = line;
		pcol = col;
		if (t->kind == TK_ERROR) {
			where(&lf, i, line, col, path, "error token");
			bad = 1;
		}
		if (t->kind == TK_TICK || t->kind == TK_CHAR) {
			uint32_t direct = i ? lf.tok[i - 1].kind : TK_NONE;
			size_t p = lex_prev(&lf, i);
			uint32_t last = p == (size_t)-1 ? TK_NONE
							: lf.tok[p].kind;
			int strict = tick_ok_prev(direct);

			if (t->kind == TK_TICK)
				nticks++;
			else
				nchars++;
			if ((t->kind == TK_TICK) != strict) {
				where(&lf, i, line, col, path,
				      "tick rule violated");
				bad = 1;
			}
			if (lex_is_trivia(direct) && tick_ok_prev(last)) {
				nlookback++;
				if (verbose)
					where(&lf, i, line, col, path,
					      "lookback-sensitive");
			}
		}
		memcpy(re + pos, src + t->off, t->len);
		pos += t->len;
		for (k = t->off; k < (size_t)t->off + t->len; k++) {
			if (src[k] == '\n' ||
			    (src[k] == '\r' && (k + 1 >= len ||
						src[k + 1] != '\n'))) {
				line++;
				col = 1;
			} else if (src[k] != '\r') {
				col++;
			}
		}
	}
	if (!bad && (pos != len || (len && memcmp(re, src, len) != 0))) {
		printf("  %s: round trip mismatch (%lu of %lu bytes)\n", path,
		       (unsigned long)pos, (unsigned long)len);
		bad = 1;
	}
	if (lf.nerr && !bad) {
		printf("  %s: nerr=%lu\n", path, (unsigned long)lf.nerr);
		bad = 1;
	}
	li = lf.ntok;
	printf("%s %s (%lu bytes, %lu tokens)\n", bad ? "FAIL" : "ok  ", path,
	       (unsigned long)len, (unsigned long)li);
	free(re);
	lex_free(&lf);
	free(src);
	return bad;
}

int main(int argc, char **argv)
{
	char path[4096];

	if (argc > 1 && strcmp(argv[1], "-v") == 0)
		verbose = 1;
	while (fgets(path, sizeof path, stdin)) {
		size_t n = strlen(path);
		while (n && (path[n - 1] == '\n' || path[n - 1] == '\r'))
			path[--n] = 0;
		if (!n)
			continue;
		nfiles++;
		nfail += check(path) != 0;
	}
	printf("corpus: %lu files, %lu bytes, %lu tokens, %lu ticks, "
	       "%lu char literals, %lu lookback-sensitive; "
	       "%lu passed, %lu failed\n",
	       (unsigned long)nfiles, (unsigned long)nbytes,
	       (unsigned long)ntok_total, (unsigned long)nticks,
	       (unsigned long)nchars, (unsigned long)nlookback,
	       (unsigned long)(nfiles - nfail), (unsigned long)nfail);
	return nfail || !nfiles ? 1 : 0;
}
