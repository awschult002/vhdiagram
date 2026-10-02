/*
 * lex.h - VHDL-2008 tokenizer (part of the headless vhdiagram core).
 *
 * The source is treated as raw bytes. Nothing is normalized: line endings
 * and encodings are left exactly as they are. Every byte of the input
 * belongs to exactly one token, including whitespace and comments
 * ("trivia"), so concatenating all tokens in order reproduces the input
 * byte for byte, and any token range can be spliced by the writer.
 *
 * A token is a kind, an offset and a length into the caller's buffer,
 * and nothing else. One flat array per file; no per-token allocation.
 * Keywords get their own kind (TK_KW_*), so callers never compare text
 * to recognise them. Identifiers can optionally be interned (see
 * strtab.h); the ids live in a parallel array, not in the token.
 */
#ifndef VHD_LEX_H
#define VHD_LEX_H

#include <stddef.h>
#include <stdint.h>

#include "strtab.h"

/* VHDL-2008 reserved words, in strcmp order (lookup is a binary search;
 * tests/lex_test.c checks the order). */
#define LEX_KEYWORDS(X) \
	X(ABS, "abs") X(ACCESS, "access") X(AFTER, "after") X(ALIAS, "alias") \
	X(ALL, "all") X(AND, "and") X(ARCHITECTURE, "architecture") \
	X(ARRAY, "array") X(ASSERT, "assert") X(ASSUME, "assume") \
	X(ASSUME_GUARANTEE, "assume_guarantee") X(ATTRIBUTE, "attribute") \
	X(BEGIN, "begin") X(BLOCK, "block") X(BODY, "body") \
	X(BUFFER, "buffer") X(BUS, "bus") X(CASE, "case") \
	X(COMPONENT, "component") X(CONFIGURATION, "configuration") \
	X(CONSTANT, "constant") X(CONTEXT, "context") X(COVER, "cover") \
	X(DEFAULT, "default") X(DISCONNECT, "disconnect") X(DOWNTO, "downto") \
	X(ELSE, "else") X(ELSIF, "elsif") X(END, "end") X(ENTITY, "entity") \
	X(EXIT, "exit") X(FAIRNESS, "fairness") X(FILE, "file") X(FOR, "for") \
	X(FORCE, "force") X(FUNCTION, "function") X(GENERATE, "generate") \
	X(GENERIC, "generic") X(GROUP, "group") X(GUARDED, "guarded") \
	X(IF, "if") X(IMPURE, "impure") X(IN, "in") X(INERTIAL, "inertial") \
	X(INOUT, "inout") X(IS, "is") X(LABEL, "label") X(LIBRARY, "library") \
	X(LINKAGE, "linkage") X(LITERAL, "literal") X(LOOP, "loop") \
	X(MAP, "map") X(MOD, "mod") X(NAND, "nand") X(NEW, "new") \
	X(NEXT, "next") X(NOR, "nor") X(NOT, "not") X(NULL, "null") \
	X(OF, "of") X(ON, "on") X(OPEN, "open") X(OR, "or") \
	X(OTHERS, "others") X(OUT, "out") X(PACKAGE, "package") \
	X(PARAMETER, "parameter") X(PORT, "port") X(POSTPONED, "postponed") \
	X(PROCEDURE, "procedure") X(PROCESS, "process") \
	X(PROPERTY, "property") X(PROTECTED, "protected") X(PURE, "pure") \
	X(RANGE, "range") X(RECORD, "record") X(REGISTER, "register") \
	X(REJECT, "reject") X(RELEASE, "release") X(REM, "rem") \
	X(REPORT, "report") X(RESTRICT, "restrict") \
	X(RESTRICT_GUARANTEE, "restrict_guarantee") X(RETURN, "return") \
	X(ROL, "rol") X(ROR, "ror") X(SELECT, "select") \
	X(SEQUENCE, "sequence") X(SEVERITY, "severity") X(SHARED, "shared") \
	X(SIGNAL, "signal") X(SLA, "sla") X(SLL, "sll") X(SRA, "sra") \
	X(SRL, "srl") X(STRONG, "strong") X(SUBTYPE, "subtype") \
	X(THEN, "then") X(TO, "to") X(TRANSPORT, "transport") \
	X(TYPE, "type") X(UNAFFECTED, "unaffected") X(UNITS, "units") \
	X(UNTIL, "until") X(USE, "use") X(VARIABLE, "variable") \
	X(VMODE, "vmode") X(VPROP, "vprop") X(VUNIT, "vunit") \
	X(WAIT, "wait") X(WHEN, "when") X(WHILE, "while") X(WITH, "with") \
	X(XNOR, "xnor") X(XOR, "xor")

/* Delimiters (the tick is handled separately). Longest match wins. */
#define LEX_DELIMS(X) \
	X(AMP, "&") X(LPAREN, "(") X(RPAREN, ")") X(STAR, "*") X(PLUS, "+") \
	X(COMMA, ",") X(MINUS, "-") X(DOT, ".") X(SLASH, "/") X(COLON, ":") \
	X(SEMI, ";") X(LT, "<") X(EQ, "=") X(GT, ">") X(BACKTICK, "`") \
	X(BAR, "|") X(LBRACKET, "[") X(RBRACKET, "]") X(QUESTION, "?") \
	X(AT, "@") \
	X(ARROW, "=>") X(POW, "**") X(ASSIGN, ":=") X(NE, "/=") X(GE, ">=") \
	X(LE, "<=") X(BOX, "<>") X(COND, "??") X(MEQ, "?=") X(MLT, "?<") \
	X(MGT, "?>") X(DLT, "<<") X(DGT, ">>") \
	X(MNE, "?/=") X(MLE, "?<=") X(MGE, "?>=")

enum {
	TK_NONE = 0,       /* never produced; handy as a table terminator */
	TK_ERROR,          /* bytes that do not form a valid token */
	/* trivia */
	TK_WS,             /* spaces, tabs, CR, LF, VT, FF */
	TK_LINE_COMMENT,   /* "--" up to (not including) CR or LF */
	TK_BLOCK_COMMENT,  /* "/" "*" ... "*" "/", not nested */
	/* names and literals */
	TK_IDENT,          /* basic identifier (case-insensitive) */
	TK_EXT_IDENT,      /* \extended identifier\ (case-sensitive) */
	TK_DECIMAL,        /* 1_000, 3.14, 1.0e-9 */
	TK_BASED,          /* 16#FF#, 2#1.1#e3 */
	TK_CHAR,           /* 'a' */
	TK_STRING,         /* "a""b" */
	TK_BITSTRING,      /* x"FF", 8ux"F", d"12" */
	TK_TICK,           /* attribute / qualified-expression tick */
#define X(name, text) TK_##name,
	LEX_DELIMS(X)
#undef X
	TK_KW_FIRST_,
#define X(name, text) TK_KW_##name,
	LEX_KEYWORDS(X)
#undef X
	TK_COUNT_
};

#define TK_KW_FIRST (TK_KW_FIRST_ + 1)
#define TK_KW_LAST  (TK_COUNT_ - 1)

typedef struct {
	uint32_t kind;
	uint32_t off;  /* byte offset into the source buffer */
	uint32_t len;  /* byte length, always > 0 */
} Token;

typedef struct {
	const unsigned char *src;  /* borrowed; must outlive the LexFile */
	size_t src_len;
	Token *tok;
	size_t ntok;
	size_t cap;
	uint32_t *sym;   /* parallel to tok when interning, else NULL; 0 = none */
	size_t nerr;     /* number of TK_ERROR tokens */
} LexFile;

/* Tokenize src[0..len). If st is non-NULL, every TK_IDENT and TK_EXT_IDENT
 * is interned into st and its id stored in lf->sym[i]. Returns 0 on
 * success, -1 on out-of-memory or an input larger than 4 GiB. Lexical
 * errors are not fatal: they become TK_ERROR tokens and are counted. */
int lex_file(LexFile *lf, const unsigned char *src, size_t len, Strtab *st);
void lex_free(LexFile *lf);

int lex_is_trivia(uint32_t kind);
int lex_is_keyword(uint32_t kind);
const char *lex_kind_name(uint32_t kind);

/* Keyword kind for the given bytes (case-insensitive), or 0. */
uint32_t lex_keyword(const unsigned char *p, size_t len);

/* Index of the next / previous non-trivia token, or (size_t)-1. */
size_t lex_next(const LexFile *lf, size_t i);
size_t lex_prev(const LexFile *lf, size_t i);

/* The comment block directly above token i: consecutive comment tokens
 * separated only by whitespace holding at most one line break, and with
 * at most one line break between the last comment and token i. Sets
 * *first to the first comment token and returns how many tokens (comments
 * and the whitespace between them) the block spans; 0 if there is none.
 * A comment that follows code on its line is not part of the block. */
size_t lex_doc_comment(const LexFile *lf, size_t i, size_t *first);

#endif
