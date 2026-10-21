/*
 * scan_dump.c - scan files and print their design units, one per line:
 *     path<TAB>line<TAB>kind<TAB>name<TAB>of
 * Every file is scanned first (scan_path frees each buffer), and only
 * then are names printed, so the output also shows that names are read
 * from the arena and not from freed source buffers (run under ASan).
 *
 * Usage: scan_dump [path...]   (no paths: read one path per line on stdin)
 */
#include <stdio.h>
#include <string.h>

#include "scan.h"

static int scan_one(ScanResult *sr, const char *path)
{
	if (scan_path(sr, path)) {
		fprintf(stderr, "scan_dump: out of memory on %s\n", path);
		return 1;
	}
	if (sr->file[sr->nfile - 1].failed) {
		fprintf(stderr, "scan_dump: cannot read %s\n", path);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	ScanResult sr;
	char path[4096];
	size_t i;
	int bad = 0;

	scan_init(&sr);
	if (argc > 1) {
		for (i = 1; i < (size_t)argc; i++)
			bad |= scan_one(&sr, argv[i]);
	} else {
		while (fgets(path, sizeof path, stdin)) {
			size_t n = strlen(path);
			while (n && (path[n - 1] == '\n' || path[n - 1] == '\r'))
				path[--n] = 0;
			if (n)
				bad |= scan_one(&sr, path);
		}
	}
	for (i = 0; i < sr.nunit; i++) {
		const ScanUnit *u = &sr.unit[i];
		printf("%s\t%u\t%s\t%s\t%s\n", sr.file[u->file].path,
		       (unsigned)u->line, scan_kind_name(u->kind),
		       scan_name(&sr, u->name, NULL),
		       scan_name(&sr, u->of, NULL));
	}
	scan_free(&sr);
	return bad;
}
