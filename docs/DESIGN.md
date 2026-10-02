# Design notes

How the core is built, and why. Requirements are in [`REQUIREMENTS.md`](REQUIREMENTS.md). The Scribe keeps this file in step with team decisions.

## Tokenizer (`core/tokenizer`)

- Files are read as raw bytes. Line endings and encoding are never changed.
- A token is a kind, an offset and a length into the file's buffer, and nothing else: no string copies. Each file has one flat token array, with no per-token `malloc`.
- Case is folded only when comparing keywords and identifiers. It's never folded in storage.
- VHDL-2008 syntax is supported: `/* */` comments, bit strings such as `8x"FF"`, matching operators such as `?=`, and `<< >>` external names.
- **The apostrophe rule:** a tick directly after an identifier, `)`, `]` or the keyword `all` is an attribute or qualified-expression tick, as in `clk'event`, `a'range` and `std_logic'('1')`. Any other tick starts a character literal such as `'1'`. Reserved words other than `all` don't count as identifiers, so in `else'0'` the `'0'` is a literal.
- **Test invariant:** joining the tokens back together reproduces the file byte for byte. Edge-case tests include `x'('1')`, `'''`, `else'0'`, `string'("01")`, and a stray `'` inside a `--` comment.

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

## Test corpus (R-TEST-1, open)

The proposal is two open-source VHDL-2008 projects pinned to fixed commits: [neorv32](https://github.com/stnolting/neorv32), a real hierarchy with records in packages, and [Open Logic](https://github.com/open-logic/open-logic), which has many generics and component instantiations. Alex's own ~30 files would make the final acceptance test.
