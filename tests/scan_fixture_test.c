/*
 * scan_fixture_test.c - run the scan over tests/fixtures/scan/ and
 * compare each file's units with the "-- expect:" lines in it.
 * The list of fixtures and their status (pass / xfail) is in MANIFEST.
 *
 * Usage: scan_fixture_test [fixture-dir]  (default: tests/fixtures/scan)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scan.h"

#define MAXU 64

static int read_expect(const char *path, char exp[][512], int *n)
{
	FILE *f = fopen(path, "rb");
	char line[512];

	*n = 0;
	if (!f)
		return -1;
	while (fgets(line, sizeof line, f) && *n < MAXU) {
		size_t l = strlen(line);
		while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
			line[--l] = 0;
		if (strncmp(line, "-- expect: ", 11) == 0)
			snprintf(exp[(*n)++], 512, "%s", line + 11);
	}
	fclose(f);
	return 0;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "tests/fixtures/scan";
	char mpath[1024], line[1024], path[1200];
	static char exp[MAXU][512], act[MAXU][512];
	FILE *m;
	int npass = 0, nxfail = 0, nbad = 0;

	snprintf(mpath, sizeof mpath, "%s/MANIFEST", dir);
	m = fopen(mpath, "rb");
	if (!m) {
		printf("scan fixtures: cannot open %s\n", mpath);
		return 1;
	}
	while (fgets(line, sizeof line, m)) {
		char name[256], status[16];
		ScanResult sr;
		int ne, na = 0, ok, i;
		size_t u;

		if (line[0] == '#' || sscanf(line, "%255s %15s", name, status) != 2)
			continue;
		snprintf(path, sizeof path, "%s/%s", dir, name);
		if (read_expect(path, exp, &ne) || ne == 0) {
			printf("FAIL  %s: no expectations\n", name);
			nbad++;
			continue;
		}
		scan_init(&sr);
		if (scan_path(&sr, path) || sr.file[0].failed) {
			printf("FAIL  %s: scan failed\n", name);
			nbad++;
			scan_free(&sr);
			continue;
		}
		for (u = 0; u < sr.nunit && na < MAXU; u++)
			snprintf(act[na++], 512, "%u %s %s",
				 (unsigned)sr.unit[u].line,
				 scan_kind_name(sr.unit[u].kind),
				 scan_name(&sr, sr.unit[u].name, NULL));
		scan_free(&sr);
		ok = na == ne;
		for (i = 0; ok && i < ne; i++)
			ok = strcmp(exp[i], act[i]) == 0;
		if (strcmp(status, "xfail") == 0) {
			printf("%s %s\n", ok ? "XPASS" : "xfail", name);
			if (ok)
				nbad++;   /* fixed: drop the xfail mark */
			else
				nxfail++;
		} else {
			printf("%s %s\n", ok ? "ok   " : "FAIL ", name);
			if (ok)
				npass++;
			else
				nbad++;
		}
		if (!ok) {
			for (i = 0; i < ne; i++)
				printf("        expected: %s\n", exp[i]);
			for (i = 0; i < na; i++)
				printf("        actual:   %s\n", act[i]);
		}
	}
	fclose(m);
	printf("scan fixtures: %d passed, %d expected failures, %d bad\n",
	       npass, nxfail, nbad);
	return nbad ? 1 : 0;
}
