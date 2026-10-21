#!/usr/bin/env python3
"""Independent design-unit counter, used to cross-check src/scan.c.

Deliberately a different method from scan.c (which counts nesting depth):
text is stripped of comments, strings, char literals and extended
identifiers (newlines kept, so lines stay right), split into words, and
a unit is recognised only *outside* a unit. A unit ends at
    end [<unit kind>] [<unit name>] ;
where the optional name, if present, must be the unit's own name, and a
bare "end;" counts only if it is the last "end" before the next
top-level header or the end of the file (resolved by looking ahead).

Usage: scan_check.py [--compare DUMP] < file-list
Prints "path<TAB>line<TAB>kind<TAB>name<TAB>of" per unit, like scan_dump.
With --compare, diffs against scan_dump output and exits 1 on mismatch.
"""
import re, sys

TOK = re.compile(r"""
   (?P<nl>\r\n|\n|\r)
 | (?P<lc>--[^\r\n]*)
 | (?P<bc>/\*.*?\*/)
 | (?P<str>"(?:[^"\r\n]|"")*")
 | (?P<ext>\\(?:[^\\\r\n]|\\\\)*\\)
 | (?P<word>[A-Za-z][A-Za-z0-9_]*)
 | (?P<tick>')
 | (?P<num>[0-9][0-9_]*)
 | (?P<sym>[;().:,])
 | (?P<other>.)
""", re.S | re.X)

KINDS = {"entity", "architecture", "package", "configuration", "context"}


def tokens(text):
    """(text, line) of interesting tokens; words lower-cased."""
    out, line, prev = [], 1, None
    for m in TOK.finditer(text):
        g = m.lastgroup
        s = m.group()
        if g == "bc":
            line += len(re.findall(r"\r\n|\n|\r", s))
            continue
        if g == "nl":
            line += 1
            continue
        if g in ("lc", "other", "num"):
            if g != "other" or not s.isspace():
                prev = g
            continue
        if g == "tick":
            # char literal 'x' unless it follows a name / ) (attribute)
            if prev not in ("word", ")") and text[m.end():m.end() + 2][1:2] == "'":
                prev = "char"
                out.append(("'c", line))
                continue
            prev = "tick"
            continue
        if g == "word":
            out.append((s.lower(), line, s))
            prev = "word"
            continue
        if g == "ext":
            out.append((s, line, s))
            prev = "word"
            continue
        if g == "str":
            prev = "str"
            continue
        out.append((s, line))
        prev = s
    return out


def strip_chars(text):
    # Remove char literals like ';' or '-' before tokenizing so they never
    # look like delimiters; keep attribute ticks (x'range) intact.
    return re.sub(r"(?<![A-Za-z0-9_)\]])'[^\r\n]'", "' '", text)


def units(path):
    with open(path, "rb") as f:
        text = f.read().decode("latin-1")
    toks = [t for t in tokens(strip_chars(text)) if t[0] != "'c"]
    w = [t[0] for t in toks]
    res, i, n = [], 0, len(toks)
    stmt_start = True

    def header(i):
        if not stmt_start:
            return None
        a = w[i:i + 5] + [""] * 5
        if a[0] == "entity" and a[2] == "is":
            return ("entity", 1, "")
        if a[0] == "context" and a[2] == "is":
            return ("context", 1, "")
        if a[0] in ("architecture", "configuration") and a[2] == "of" and a[4] == "is":
            return (a[0], 1, toks[i + 3][2])
        if a[0] == "package" and a[1] == "body" and a[3] == "is":
            return ("package body", 2, toks[i + 2][2])
        if a[0] == "package" and a[2] == "is":
            if a[3] == "new":
                return ("package instance", 1, "")
            return ("package", 1, "")
        return None

    while i < n:
        h = header(i)
        if not h:
            stmt_start = w[i] == ";"
            i += 1
            continue
        kind, ni, of = h
        name = toks[i + ni][2]
        res.append((toks[i][1], kind, name, of))
        lname = name.lower() if not name.startswith("\\") else name
        if kind == "package instance":
            while i < n and w[i] != ";":
                i += 1
            i += 1
            stmt_start = True
            continue
        # find the end of the unit
        j = i + ni + 1
        last_bare = None
        end_at = None
        while j < n:
            if w[j] == "end":
                k = j + 1
                kw = kind.split()
                if w[k:k + len(kw)] == kw:
                    k += len(kw)
                if k < n and w[k] == lname:
                    k += 1
                if k < n and w[k] == ";" and (k > j + 1):
                    end_at = k
                    break
                if k == j + 1 and k < n and w[k] == ";":
                    last_bare = k
            # a top-level-looking header after a bare end: unit ended there
            if last_bare is not None and w[j - 1] == ";" and w[j] in (
                    "library", "use", "entity", "architecture", "package",
                    "configuration", "context") and _hdr(w, j):
                end_at = last_bare
                break
            j += 1
        if end_at is None:
            end_at = last_bare if last_bare is not None else n - 1
        i = end_at + 1
        stmt_start = True
    return res


def _hdr(w, j):
    a = w[j:j + 5] + [""] * 5
    if a[0] in ("library",):
        return True
    if a[0] == "use" and a[1] not in ("entity", "configuration", "open"):
        return False  # use clauses also occur inside units
    if a[0] in ("entity", "context") and a[2] == "is":
        return True
    if a[0] in ("architecture", "configuration") and a[2] == "of":
        return True
    if a[0] == "package" and (a[1] == "body" or a[2] == "is"):
        return True
    return False


def main():
    args = sys.argv[1:]
    cmp = None
    if args[:1] == ["--compare"]:
        cmp = args[1]
    paths = [l.strip() for l in sys.stdin if l.strip()]
    mine = []
    for p in paths:
        for line, kind, name, of in units(p):
            mine.append("%s\t%d\t%s\t%s\t%s" % (p, line, kind, name, of))
    if cmp is None:
        print("\n".join(mine))
        return 0
    with open(cmp, encoding="latin-1") as f:
        theirs = [l.rstrip("\r\n") for l in f if l.strip()]
    norm = lambda l: l.lower()
    a, b = set(map(norm, mine)), set(map(norm, theirs))
    only_scan = [l for l in theirs if norm(l) not in a]
    only_chk = [l for l in mine if norm(l) not in b]
    for l in only_scan:
        print("scan only:    " + l)
    for l in only_chk:
        print("checker only: " + l)
    print("scan compare: %d files, scan %d units, checker %d units, "
          "%d disagreements" % (len(paths), len(theirs), len(mine),
                                len(only_scan) + len(only_chk)))
    return 1 if only_scan or only_chk else 0


if __name__ == "__main__":
    sys.exit(main())
