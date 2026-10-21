/*
 * scan_test.c - tests for the scan step (design-unit summaries).
 *
 * Each case scans one or more in-memory buffers, frees or overwrites
 * them, and checks the units and context clauses that were recorded.
 *
 * Usage: scan_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scan.h"

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

/* Scan a copy of text from the heap, then wipe and free that copy, so
 * any pointer left into it would show up as garbage or an ASan error. */
static int scan_text(ScanResult *sr, const char *path, const char *text)
{
	size_t len = strlen(text);
	unsigned char *buf = malloc(len ? len : 1);
	int rc;

	if (!buf)
		return -1;
	memcpy(buf, text, len);
	rc = scan_buffer(sr, path, buf, len);
	memset(buf, '#', len);
	free(buf);
	return rc;
}

/* Does unit u have this kind, name and "of" name (NULL for none)? */
static int unit_is(const ScanResult *sr, size_t u, uint32_t kind,
		   const char *name, const char *of)
{
	const ScanUnit *x;

	if (u >= sr->nunit)
		return 0;
	x = &sr->unit[u];
	if (x->kind != kind || strcmp(scan_name(sr, x->name, NULL), name) != 0)
		return 0;
	if (!of)
		return x->of == 0;
	return strcmp(scan_name(sr, x->of, NULL), of) == 0;
}

static void print_units(const ScanResult *sr)
{
	size_t i;

	for (i = 0; i < sr->nunit; i++)
		printf("    unit %s '%s' of '%s' line %u\n",
		       scan_kind_name(sr->unit[i].kind),
		       scan_name(sr, sr->unit[i].name, NULL),
		       scan_name(sr, sr->unit[i].of, NULL),
		       (unsigned)sr->unit[i].line);
	for (i = 0; i < sr->nref; i++)
		printf("    ref %s '%s' -> unit %d\n",
		       scan_kind_name(sr->ref[i].kind),
		       scan_name(sr, sr->ref[i].name, NULL),
		       (int)sr->ref[i].unit);
}

static void test_entity_architecture(void)
{
	ScanResult sr;
	int ok = 1;

	scan_init(&sr);
	CHECK(scan_text(&sr, "counter.vhd",
		"library ieee;\n"
		"use ieee.std_logic_1164.all, ieee.numeric_std.all;\n"
		"\n"
		"entity Counter is\n"
		"  generic (W : natural := 8);\n"
		"  port (clk : in std_logic; q : out unsigned(W-1 downto 0));\n"
		"end entity Counter;\n"
		"\n"
		"architecture rtl of counter is\n"
		"  signal r : unsigned(W-1 downto 0);\n"
		"begin\n"
		"  process (clk) begin\n"
		"    if rising_edge(clk) and clk'event then r <= r + 1; end if;\n"
		"  end process;\n"
		"  q <= r;\n"
		"end architecture;\n") == 0, "scan failed");
	CHECK(sr.nunit == 2, "nunit %u", (unsigned)sr.nunit);
	CHECK(unit_is(&sr, 0, SCAN_ENTITY, "Counter", NULL), "entity");
	CHECK(unit_is(&sr, 1, SCAN_ARCHITECTURE, "rtl", "Counter"), "arch");
	CHECK(sr.nunit == 2 && sr.unit[0].line == 4 && sr.unit[1].line == 9,
	      "lines");
	/* "counter" and "Counter" are one name, spelled as first seen */
	CHECK(sr.nunit == 2 && sr.unit[1].of == sr.unit[0].name, "same id");
	CHECK(scan_find(&sr, "COUNTER") == sr.unit[0].name, "find folds");
	CHECK(sr.nref == 3, "nref %u", (unsigned)sr.nref);
	CHECK(sr.nref == 3 && sr.ref[0].kind == SCAN_LIBRARY &&
	      strcmp(scan_name(&sr, sr.ref[0].name, NULL), "ieee") == 0 &&
	      sr.ref[1].kind == SCAN_USE &&
	      strcmp(scan_name(&sr, sr.ref[1].name, NULL),
		     "ieee.std_logic_1164.all") == 0 &&
	      strcmp(scan_name(&sr, sr.ref[2].name, NULL),
		     "ieee.numeric_std.all") == 0 &&
	      sr.ref[2].unit == 0, "refs");
	CHECK(sr.nfile == 1 && strcmp(sr.file[0].path, "counter.vhd") == 0 &&
	      sr.file[0].nerr == 0, "file");
	if (!ok)
		print_units(&sr);
	scan_free(&sr);
	report("entity+architecture", ok);
}

static void test_package_body(void)
{
	ScanResult sr;
	int ok = 1;

	scan_init(&sr);
	CHECK(scan_text(&sr, "pkg.vhd",
		"package util_pkg is\n"
		"  function clog2(n : natural) return natural;\n"
		"  type rec_t is record a : bit; end record;\n"
		"end package;\n"
		"package body util_pkg is\n"
		"  function clog2(n : natural) return natural is\n"
		"  begin return 0; end;\n"
		"  procedure p is begin null; end procedure p;\n"
		"end package body util_pkg;\n"
		"package int_pkg is new work.generic_pkg generic map (N => 4);\n")
	      == 0, "scan failed");
	CHECK(sr.nunit == 3, "nunit %u", (unsigned)sr.nunit);
	CHECK(unit_is(&sr, 0, SCAN_PACKAGE, "util_pkg", NULL), "package");
	CHECK(unit_is(&sr, 1, SCAN_PACKAGE_BODY, "util_pkg", "util_pkg"),
	      "body");
	CHECK(unit_is(&sr, 2, SCAN_PACKAGE_INST, "int_pkg", NULL), "inst");
	if (!ok)
		print_units(&sr);
	scan_free(&sr);
	report("package+package body", ok);
}

static void test_multiple_units(void)
{
	ScanResult sr;
	int ok = 1;

	scan_init(&sr);
	CHECK(scan_text(&sr, "multi.vhd",
		"context proj_ctx is\n"
		"  library ieee; use ieee.std_logic_1164.all;\n"
		"end context;\n"
		"library work; context work.proj_ctx;\n"
		"entity a is end;\n"
		"entity b is port (x : in bit); end entity b;\n"
		"architecture s of b is\n"
		"begin\n"
		"  u0 : entity work.a;\n"
		"  g : for i in 0 to 1 generate begin end generate;\n"
		"end s;\n"
		"configuration cfg of b is\n"
		"  for s for u0 : a use entity work.a; end for; end for;\n"
		"end configuration;\n") == 0, "scan failed");
	CHECK(sr.nunit == 5, "nunit %u", (unsigned)sr.nunit);
	CHECK(unit_is(&sr, 0, SCAN_CONTEXT, "proj_ctx", NULL), "context");
	CHECK(unit_is(&sr, 1, SCAN_ENTITY, "a", NULL), "a");
	CHECK(unit_is(&sr, 2, SCAN_ENTITY, "b", NULL), "b");
	CHECK(unit_is(&sr, 3, SCAN_ARCHITECTURE, "s", "b"), "arch");
	CHECK(unit_is(&sr, 4, SCAN_CONFIGURATION, "cfg", "b"), "cfg");
	/* clauses inside the context declaration belong to it, not to a */
	CHECK(sr.nref == 2 && sr.ref[0].kind == SCAN_LIBRARY &&
	      sr.ref[0].unit == 1 && sr.ref[1].kind == SCAN_CONTEXT_REF &&
	      strcmp(scan_name(&sr, sr.ref[1].name, NULL),
		     "work.proj_ctx") == 0 && sr.ref[1].unit == 1, "refs");
	if (!ok)
		print_units(&sr);
	scan_free(&sr);
	report("multiple units in one file", ok);
}

static void test_comments_strings(void)
{
	ScanResult sr;
	int ok = 1;

	scan_init(&sr);
	CHECK(scan_text(&sr, "c.vhd",
		"-- entity fake1 is\n"
		"/* architecture fake2 of x is\n"
		"   package fake3 is */\n"
		"entity real is end entity;\n"
		"architecture r of real is\n"
		"  constant s : string := \"entity fake4 is\";\n"
		"  constant c : character := 'e';\n"
		"begin\n"
		"  assert false report \"package body fake5 is\" severity note;\n"
		"end architecture; -- entity fake6 is\n") == 0, "scan failed");
	CHECK(sr.nunit == 2, "nunit %u", (unsigned)sr.nunit);
	CHECK(unit_is(&sr, 0, SCAN_ENTITY, "real", NULL), "entity");
	CHECK(unit_is(&sr, 1, SCAN_ARCHITECTURE, "r", "real"), "arch");
	CHECK(scan_find(&sr, "fake1") == 0 && scan_find(&sr, "fake4") == 0,
	      "fakes");
	if (!ok)
		print_units(&sr);
	scan_free(&sr);
	report("keywords in comments and strings", ok);
}

static void test_extended_identifiers(void)
{
	ScanResult sr;
	int ok = 1;

	scan_init(&sr);
	CHECK(scan_text(&sr, "x.vhd",
		"entity \\like this\\ is end entity \\like this\\;\n"
		"entity \\Like This\\ is end;\n"
		"architecture \\a\\\\b\\ of \\like this\\ is begin end;\n")
	      == 0, "scan failed");
	CHECK(sr.nunit == 3, "nunit %u", (unsigned)sr.nunit);
	CHECK(unit_is(&sr, 0, SCAN_ENTITY, "\\like this\\", NULL), "ext 1");
	CHECK(unit_is(&sr, 1, SCAN_ENTITY, "\\Like This\\", NULL), "ext 2");
	CHECK(sr.nunit == 3 && sr.unit[0].name != sr.unit[1].name,
	      "extended names are case-sensitive");
	CHECK(unit_is(&sr, 2, SCAN_ARCHITECTURE, "\\a\\\\b\\", "\\like this\\"),
	      "doubled backslash");
	CHECK(scan_find(&sr, "\\LIKE THIS\\") == 0, "no folding");
	if (!ok)
		print_units(&sr);
	scan_free(&sr);
	report("extended identifiers", ok);
}

static void test_names_outlive_buffer(void)
{
	ScanResult sr;
	const char *text = "library mylib;\nentity keep_me is end;\n"
			   "architecture arch of keep_me is begin end;\n";
	size_t len = strlen(text);
	unsigned char *buf = malloc(len);
	int ok = buf != NULL;

	scan_init(&sr);
	if (buf) {
		memcpy(buf, text, len);
		CHECK(scan_buffer(&sr, "keep.vhd", buf, len) == 0, "scan");
		memset(buf, 'Z', len);  /* overwrite, then free */
		free(buf);
	}
	CHECK(unit_is(&sr, 0, SCAN_ENTITY, "keep_me", NULL), "entity");
	CHECK(unit_is(&sr, 1, SCAN_ARCHITECTURE, "arch", "keep_me"), "arch");
	CHECK(sr.nref == 1 &&
	      strcmp(scan_name(&sr, sr.ref[0].name, NULL), "mylib") == 0,
	      "library");
	CHECK(scan_hash((const unsigned char *)text, len) == sr.file[0].hash,
	      "hash");
	scan_free(&sr);
	report("names outlive the source buffer", ok);
}

static void test_arena_growth(void)
{
	ScanResult sr;
	char text[256];
	char path[64];
	size_t i, n = 3000;
	int ok = 1;

	scan_init(&sr);
	for (i = 0; i < n && ok; i++) {
		sprintf(path, "dir/file_%04u.vhd", (unsigned)i);
		sprintf(text, "library lib_%u;\n"
			"entity entity_with_a_fairly_long_name_%u is end;\n"
			"architecture rtl of entity_with_a_fairly_long_name_%u "
			"is begin end;\n",
			(unsigned)(i % 10), (unsigned)i, (unsigned)i);
		CHECK(scan_text(&sr, path, text) == 0, "scan %u", (unsigned)i);
	}
	CHECK(sr.nfile == n && sr.nunit == 2 * n && sr.nref == n, "counts");
	CHECK(sr.arena.nblock > 1, "arena did not grow (%u blocks)",
	      (unsigned)sr.arena.nblock);
	/* each distinct name stored once: n entities + rtl + 10 libraries */
	CHECK(strtab_count(&sr.names) == n + 1 + 10, "names %u",
	      (unsigned)strtab_count(&sr.names));
	CHECK(unit_is(&sr, 2 * 1234, SCAN_ENTITY,
		      "entity_with_a_fairly_long_name_1234", NULL), "early unit");
	CHECK(strcmp(sr.file[0].path, "dir/file_0000.vhd") == 0 &&
	      strcmp(sr.file[n - 1].path, "dir/file_2999.vhd") == 0, "paths");
	CHECK(sr.unit[2 * n - 1].file == n - 1, "file index");
	scan_free(&sr);
	report("arena growth across many files", ok);
}

static void test_malformed(void)
{
	static const char *const cases[] = {
		"", "entity", "entity foo", "entity foo is", "entity is",
		"architecture", "architecture a of", "architecture a of e",
		"package body", "package body p", "package", "configuration c of",
		"library", "library ;", "library a,", "use", "use a.", "use a.b.",
		"context", "context a", "end", "end;", "end entity foo;",
		"entity foo is port (a : in bit", "\"unterminated entity x is",
		"/* unterminated entity x is", "\\unterminated ext",
		"entity \\bad", "entity e is end; architecture",
		"@@@ entity e is ` ~ end;",
	};
	size_t i;
	int ok = 1;

	for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		ScanResult sr;
		scan_init(&sr);
		CHECK(scan_text(&sr, "bad.vhd", cases[i]) == 0, "case %u",
		      (unsigned)i);
		scan_free(&sr);
	}
	{
		ScanResult sr;
		scan_init(&sr);
		CHECK(scan_text(&sr, "trunc.vhd",
			"entity e is port (a : in bit);\n"
			"architecture rtl of e is begin process begin") == 0,
		      "truncated");
		CHECK(unit_is(&sr, 0, SCAN_ENTITY, "e", NULL) &&
		      unit_is(&sr, 1, SCAN_ARCHITECTURE, "rtl", "e"),
		      "units before truncation");
		CHECK(sr.file[0].nerr == 0, "nerr");
		scan_free(&sr);
	}
	report("malformed and truncated input", ok);
}

static void test_missing_file(void)
{
	ScanResult sr;
	int ok = 1;

	scan_init(&sr);
	CHECK(scan_path(&sr, "tests/fixtures/does_not_exist.vhd") == 0, "rc");
	CHECK(sr.nfile == 1 && sr.file[0].failed == 1 && sr.nunit == 0,
	      "recorded as failed");
	scan_free(&sr);
	report("unreadable file", ok);
}

int main(void)
{
	test_entity_architecture();
	test_package_body();
	test_multiple_units();
	test_comments_strings();
	test_extended_identifiers();
	test_names_outlive_buffer();
	test_arena_growth();
	test_malformed();
	test_missing_file();
	printf("scan_test: %d/%d passed\n", ntests - nfail, ntests);
	return nfail ? 1 : 0;
}
