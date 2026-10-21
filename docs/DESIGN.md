# Design notes

How the core is built, and why. Requirements are in [`REQUIREMENTS.md`](REQUIREMENTS.md). The Scribe keeps this file in step with team decisions.

## Tokenizer (`core/tokenizer`)

- Files are read as raw bytes. Line endings and encoding are never changed.
- A token is a kind, an offset and a length into the file's buffer, and nothing else: no string copies. Each file has one flat token array, with no per-token `malloc`.
- Case is folded only when comparing keywords and identifiers. It's never folded in storage.
- VHDL-2008 syntax is supported: `/* */` comments, bit strings such as `8x"FF"`, matching operators such as `?=`, and `<< >>` external names.
- **The apostrophe rule:** a tick directly after an identifier, `)`, `]` or the keyword `all` is an attribute or qualified-expression tick, as in `clk'event`, `a'range` and `std_logic'('1')`. Any other tick starts a character literal such as `'1'`. Reserved words other than `all` don't count as identifiers, so in `else'0'` the `'0'` is a literal.
- **Why the tick rule still matters:** only entities, ports, generics and maps are parsed for meaning (R-IN-10), but the extractor still has to tokenize process bodies correctly to find where they end. If `clk'event` or `std_logic'('1')` were misread, the extractor would lose its place in the file.
- The tick has to come *directly* after the identifier, so `a '1'`, with a space, is a character literal.
- A file being edited is read once into a `const` buffer that stays loaded while its graph is open. Its token table points into that buffer, and the writer splices output from it.
- VHDL projects can have hundreds of files (R-SCALE-2). For the list of available nodes, each file is tokenized, scanned, and freed. The tokenizer itself doesn't change for this. See [Scan](#scan-corescan-r-in-2-r-in-3-r-scale-2) for what's kept.
- **Test invariant:** joining the tokens back together reproduces the file byte for byte. Edge-case tests include `x'('1')`, `'''`, `else'0'`, `string'("01")`, and a stray `'` inside a `--` comment.

## Scan (`core/scan`, R-IN-2, R-IN-3, R-SCALE-2)

The scan step lists the design units in every file. It's a token walk over the non-trivia tokens, not a parser.

- **Memory:** names live in one bump arena (64 KB blocks, 8-byte aligned, freed all at once). Each name is copied into it once: a string table maps the bytes to an id, and a name already seen reuses its id. File paths are copied into the same arena. `scan_path` reads a file, scans it, and frees the file's buffer as soon as that file is scanned, so only names stay in memory. Each file keeps its size, its FNV-1a 64-bit hash and its tokenizer error count. A file that can't be read is recorded as failed, with no units.
- **What it records:** entity, architecture (with its entity), package, package body (with its package), package instantiation (`package p is new ...`), configuration (with its entity) and context declaration. Each unit has its kind, name, file and the line of its keyword. `library`, `use` and `context` reference clauses are recorded as dotted names (`ieee.std_logic_1164.all`) and linked to the unit that follows them.
- **Where a unit starts:** a unit keyword starts a unit only outside every unit. So instantiations (`u : entity work.e`), bindings (`use entity work.e`), generic packages and packages nested in an architecture are never taken for units. `context c is` starts a unit; `context` without `is` after its name is a context reference.
- **Where a unit ends:** inside a unit the walker keeps a depth, the number of open constructs that close with their own `end`. It never counts `is`: type, subtype, alias and attribute declarations have `is` and no `end`. Every `end` closes one construct, and an `end` at depth 0 ends the unit whatever follows it (`end;`, `end entity;`, `end rtl;`). The openers, each counted only when not directly after `end`:
  - `process`, `block`, `record`, `units`, `protected`, `loop`;
  - `component`, except after `:` (a component instantiation);
  - `if` and `case`, both as sequential statements and as generates;
  - `generate` of a for-generate only (the `generate` of an if, case, elsif or else generate belongs to the `if` or `case` already counted);
  - function and procedure bodies: `is` not followed by `new` or `<>`, and not `is name` closing a generic item;
  - a nested `package p is` (not `is new`) and `package body p is`;
  - in a configuration only, `for` (block and component configurations close with `end for`). Outside a configuration, `end for` closes nothing.
- **`is new`:** a package or subprogram instantiation is not a body and opens nothing.
- **Attributes:** an attribute declaration or specification is skipped up to its `;` outside parentheses, so the entity class in `attribute a of f : function is ...;` opens nothing.
- **Generate alternatives (VHDL-2008):** a bare `end;` or `end label;` followed by `elsif`, `else`, `when` or `end generate` closes nothing. It ends a generate alternative, and the generate stays open until its own `end generate`.
- **Verification:**
  - `tests/scan_test.c`: 19 tests, covering these forms, names outliving the source buffer, arena growth, malformed and truncated input, and a missing file.
  - Fixtures in `tests/fixtures/scan` (listed in `MANIFEST`, all `pass`) for attribute specifications, case in a process, and if/case generates.
  - Corpus (R-TEST-1): the 181 hdl-modules files at `8142d3a` give 358 units, with 0 disagreements against the independent checker `tests/scan_check.py`, and run clean under ASan and UBSan.
- **Known limits:**
  - Clauses after the last unit in a file aren't attached to any unit.
  - The unit's library isn't recorded yet. That waits on `-L lib=dir` (R-IN-10, R-IN-11).
  - Generics, ports and header comments are left to the extractor.
  - It's a token walk, so it relies on well-formed VHDL. Unbalanced code can put a unit's end in the wrong place; a unit with no `end` still gets recorded.

## Libraries (R-IN-11, R-OUT-5)

- hdl-modules puts each module in its own library and instantiates with explicit prefixes such as `entity common.handshake_pipeline` (51 of these) and `entity fifo.fifo` (11). VHDL has no library-to-directory mapping, so the tool takes one from a repeatable `-L lib=dir` flag. Entities with no mapping are in `work`.
- The entity table is keyed on (library, entity name).
- Resolution: an explicit library plus a `-L` mapping is exact. Otherwise the entity name alone is matched: one match is used, and two or more give a warning listing the files and a greyed-out node. A wrong silent pick would open the wrong ports.
- The writer copies an existing instance's library name from the `const` source buffer and never retypes it. A new instance uses its `-L` library or `work`, and adds `library x;` to the parent's context clause if it's missing.

## Saving (writer)

*Status: designed, not yet implemented. There's no writer code yet.*

- **Change detection:** a file is hashed with FNV-1a 64-bit every time it's loaded for editing and again just before a write. If the hash doesn't match the summary, the tool warns and rescans that file before splicing, so it never splices into stale offsets. There's no size-and-mtime shortcut, because FAT and some network shares record times only to the nearest 2 seconds.
- **Atomic replace:** the new bytes go to `foo.vhd.tmp` in the same directory and are synced. The tool then checks the original's hash and renames the temp file over the original.
- **Symlinks:** the link is resolved first, and the target is the file that gets written.
- **Permissions:** the original's mode is copied onto the temp file before the rename.
- **Locked files:** if a file is locked (Windows sharing violation), the tool names it, leaves the original untouched and deletes the `.tmp`.
- **Batch save, in two phases:**
  1. Write and sync every `.tmp` and check every original's hash.
  2. Rename only if all of phase 1 passed. Otherwise delete all temp files.
  - Renames run in dependency order, leaf entities first and parents last, so a partial failure leaves unused new children, never a parent pointing at a missing file. The tool reports which files were saved and which weren't.
- **File operations table:** the writer never calls the OS directly. It takes a table of six operations: open, write, sync, rename, remove and sync directory. The code that orders the save never mentions a platform and has no `#ifdef`.

  | Operation | Linux | Windows |
  |---|---|---|
  | sync | `fsync` | `FlushFileBuffers` |
  | rename | `rename` | `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING \| MOVEFILE_WRITE_THROUGH)` |
  | sync directory | `fsync` on the directory | does nothing |

- **Durability:** each temp file is synced before its rename. `fflush` alone isn't enough. On Linux the directory is synced after the renames.
- **Batch invariant:** for files f1..fm, renames run leaf-first in order f1..fm. After any failure, f1..fk are the new files and the rest are byte-for-byte unchanged, where k is the count the save reports. No temp file is left behind, with one exception: if remove fails after the rename, the file counts as fully replaced, the save reports an error, and the next save overwrites the leftover temp.
- **Tests:**
  - Failure injection: a version of the table fails on the Nth call to any operation, including sync. Both runners fail every step of each phase and check the batch invariant.
  - The Windows rename is checked to pass `MOVEFILE_WRITE_THROUGH`.
  - A fixture that saves a parent before its child must fail.
  - A file changed after the scan is detected when opened.
  - With one graph open over hdl-modules, the number of loaded buffers equals the number of files behind that graph, not 181.
  - Symlinks, file mode and a locked file each get a test.
  - On Windows, a real file-lock test runs on top of these (Linux has no mandatory locks): with the parent locked, saving a change that adds a new child leaves the child's file written and the parent byte-for-byte unchanged.

## Associations the tool can't model

- A slice or conversion-function association stays on its port as locked text, copied through byte for byte.
- A greyed-out node's port map is one opaque range, copied through byte for byte. If there's a `component` declaration for it, its ports are shown read-only.
- **Test (edit one wire):** a parent with a slice association and a vendor instance gets one new wire, and the diff shows only that wire.

## Mismatch flags (R-EDIT-4)

- Type and width mismatches are flagged, and the connection is written out as drawn.
- When a width depends on a generic, the port shows the expression as text (R-NODE-7), and the width check reports **unknown** instead of guessing. A false mismatch would teach the user to ignore real ones.
- **Test:** an 8-bit to 16-bit wire is flagged, written out exactly as drawn, and flagged again after reloading.

## Auto layout (R-EDIT-5)

The layout is layered (the Sugiyama method), flowing left to right, in four passes:

1. **Directed graph.** Each net becomes edges from its `out` driver to each `in` reader. `inout` nets are ignored for layout.
2. **Break cycles.** A depth-first search flips each edge that closes a loop, for layout only. Feedback such as an FSM wired to its datapath is normal.
3. **Columns.** A node's column is 1 plus the longest path to it. The top entity's `in` ports go in a fixed left column and its `out` ports in a fixed right column.
4. **Order within columns.** About four passes down and back up sort each column by barycenter, the average position of a node's neighbors in the next column. Nodes are then spaced by their pixel height.

**Determinism (Linux and Windows have to give the same result):**

- `qsort` isn't stable, and glibc and MSVCRT order ties differently. So the comparator itself breaks ties: barycenter first, then instance name.
- A barycenter is stored as the sum of positions \(S\) and the neighbor count \(c\), and two nodes are compared by cross-multiplying, \(S_a c_b\) against \(S_b c_a\). There's no division or floating point.
- A node with no neighbors in the next column keeps its current position as its sort key, stored as \(S = \text{pos}, c = 1\). Storing it as an empty sum with \(c = 0\) would make both cross products 0, so the node would tie with every other node and the order would fall back to `qsort`. A fixture with one isolated node tests this.
- **Tests:** the same graph gives the same coordinates on both CI runners, which are diffed against each other, and the result never has more crossings than the unsorted order.

## Test corpus (R-TEST-1)

[hdl-modules](https://github.com/hdl-modules/hdl-modules) pinned at `8142d3a`: 181 `.vhd` files, 84 synthesizable, all LF. It has record packages (`axi_stream_pkg`), `component` instantiation in the bus-model test code, and a real greyed-out case in `fifo36e2_wrapper` (`unisim`). It has no duplicate entity names, so that case is a fixture written by hand. The earlier proposal was two open-source VHDL-2008 projects pinned to fixed commits: [neorv32](https://github.com/stnolting/neorv32), a real hierarchy with records in packages, and [Open Logic](https://github.com/open-logic/open-logic), which has many generics and component instantiations. Alex's own ~30 files would make the final acceptance test.
