# Requirements

Collected from the customer interview with Alex. The Scribe maintains this file and the decision log below. Each requirement has an ID so tests can cite it. **Open** items are waiting on an answer.

## Purpose

Make it easier to stitch existing VHDL modules together. You open a design, see its entities as nodes, wire them up visually, and write the result back as VHDL.

**Out of scope:** checking syntax, compiling, verifying and simulating.

## Platform and build

- **R-PLAT-1** Desktop application for Linux and Windows.
- **R-PLAT-2** Written in C, built with a clean `Makefile`.
- **R-PLAT-3** Immediate-mode GUI with GPU support: cimgui with imnodes. The C++ is vendored and pinned in `third_party/`, behind the cimgui C interface. Nuklear's node editor was judged too limited.
- **R-PLAT-4** Hosted on GitHub, with CI test runners and release builds for both platforms. *Status: CI and tag-triggered release workflows are set up.*

## Input and scanning

- **R-IN-1** The input is the VHDL file at the top of the hierarchy. A directory as input is also mentioned. *Open: which directory is the starting point when given a file?*
- **R-IN-2** Scan the containing directory recursively for VHDL files and parse every entity found.
- **R-IN-3** Every entity found goes into the list of available nodes.
- **R-IN-4** The editor opens with the node graph of the top-level entity.
- **R-IN-5** Each entity's comment description header is read and shown with its node. *Open: is there a fixed header format, or just the comment block directly above `entity`?*
- **R-IN-6** Both direct instantiation and component instantiation are read.
- **R-IN-10** Only entities, generics, ports, component declarations, instances with their generic and port maps, and signal declarations are parsed for meaning. Everything else, such as process bodies and attributes like `clk'event`, is only tokenized so the extractor can skip over it.
- **R-IN-11** Library resolution:
  - The entity table is keyed on the pair of library and entity name.
  - A repeatable `-L lib=dir` flag maps a library to a directory, as build tools do. Entities with no mapping are in `work`.
  - When an instance names its library and a `-L` mapping covers it, it resolves exactly.
  - Otherwise it matches on entity name alone. If exactly one entity matches, that's used. If two or more match, the tool warns, names every matching file, and greys the node out instead of picking one.
- **R-IN-7** Sources are VHDL-2008. The tokenizer handles 2008 syntax, including `/* */` comments.
- *Open: when the tool finds VHDL it doesn't understand, should it warn and make that file read-only, or do the best it can?*
- **R-IN-8** Only the top file's directory and the directories below it are read. Vendor and library paths are never read.
- **R-IN-9** An entity that's instantiated but has no declaration anywhere in that tree (vendor primitives, generated IP) is shown as a greyed-out node. It can't be edited or opened. If a `component` declaration for it exists, the node shows those ports read-only. A node shows no ports only when there's no declaration anywhere. Its whole port map is copied through byte for byte.

## Nodes and connections

- **R-NODE-1** Each node is a VHDL entity.
- **R-NODE-2** A node's ports are the entity's VHDL ports.
- **R-NODE-3** A node's parameters are the entity's generics.
- **R-NODE-4** Records are supported, and packages are read for record types. *Open: is a record port one wire carrying the whole record, or does it expand into its fields?*
- **R-NODE-5** Recursive nodes are supported: opening a hierarchical node shows its own internal graph. Recursive `generate` (an entity instantiating itself) is not supported.
- **R-NODE-6** Only entities and their instances are nodes. Processes, `generate` statements and `block` statements are not shown in the editor.
- **R-CONN-1** A connection is a signal in the generated source code.
- **R-CONN-2** For now, a port map connects a whole port to a whole signal. Slices and conversion functions aren't supported yet; they may come later as their own node type. Until then, an association the tool can't model (a slice or a conversion function) stays on its port as locked text and is copied through byte for byte, never dropped or rewritten. *Open: are record fields, constants or `open` used in port maps?*
- **R-NODE-7** When a port's width depends on a generic, the node shows the width expression as text and doesn't compute it.
- **R-NODE-8** A file may hold more than one entity, and those entities are supported. *Open: is a file with several entities written back as one file, or split?*
- **R-NODE-9** Each entity has exactly one architecture. Entities with more than one architecture aren't supported.

## Editing

- **R-EDIT-1** Drag and drop nodes onto the graph from the available list.
- **R-EDIT-2** Connect node ports to each other.
- **R-EDIT-3** Create new custom entities, open them, and add to them recursively.
- **R-EDIT-4** A type mismatch or width mismatch on a connection is flagged, but allowed. If the user keeps it, it's written out as drawn.
- **R-EDIT-5** Node positions are not saved. On load, an automatic layout spreads the nodes out reasonably, and the user can move them. An **Auto layout** button reruns the layout.

## Output

- **R-OUT-1** Writes one or more VHDL files: one entity per node and one file per entity.
- **R-OUT-2** Creates new files or overwrites existing ones.
- **R-OUT-3** A file whose entity was not edited is never touched. *Open: does "untouched" mean byte-for-byte identical? In an edited file, does everything outside the changed parts have to survive exactly, including formatting, line endings and encoding?*
- **R-OUT-4** In an edited architecture, only what's necessary is rewritten: instances, port maps and signal declarations. Processes, `generate` statements, `block` statements and everything else are copied through unchanged.
- **R-OUT-5** Output always uses direct instantiation (`entity lib.foo`). An existing instance keeps the library name already in the file, such as `common.` or `fifo.`, copied from the original text. A new instance uses the library its entity is mapped to with `-L` (R-IN-11), or `work` when there's no mapping. If the parent file has no `library` clause for that library, the writer adds one to its context clause. That's the only change made outside the edited entity's instances and signals. *Amended 2026-10-01 from "always `entity work.x`", confirmed by Alex.*

## Testing

- **R-TEST-1** The reference corpus for the no-change round-trip test (load, change nothing, write, diff is empty) is [hdl-modules](https://github.com/hdl-modules/hdl-modules) pinned at commit `8142d3a`: 181 `.vhd` files, 84 of them synthesizable, all with LF line endings. Its AXI and AXI-Stream modules are the inspiration for this tool. Alex's own files would make the final acceptance test.
- **R-TEST-2** Fixtures written by hand cover what hdl-modules doesn't have, such as two `fifo` entities in different directories. Without `-L`, the tool warns, lists both files and greys the node out. With `-L fifo=a/`, the instance resolves to `a/fifo.vhd`.
- **R-TEST-3** Edit one wire in an `axi` module: every `common.` and `fifo.` prefix survives unchanged.

## Scale

- **R-SCALE-1** The tool itself should stay small, about 30 C source files. The VHDL projects it opens can have hundreds of files. Parsing should be simple, reliable and easy to extend, not clever. *Corrected 2026-10-01: "about 30 files" meant the C codebase, not the VHDL input.*
- **R-SCALE-2** Only the file or files behind the graph being edited stay loaded in memory. Building the list of available nodes reads each file, keeps a small summary (entity name, library, generics, ports, header comment and source path), and then frees the file.

## Proposed design (team consensus)

Details are in [`DESIGN.md`](DESIGN.md).

- A headless core library, with the GUI as a thin layer on top. The whole core runs in CI without a GPU.
- A tokenizer plus an extractor, not a full VHDL parser. It recognizes entity, generic, port, component, signal, instance and record declarations, and keeps everything else (processes, `generate` and `block` bodies) as opaque byte ranges that are copied through unchanged.
- The writer splices only the changed ranges. Source is handled as raw bytes: line endings and encoding are never changed. Each file is hashed when it's parsed. Moving a node never marks it as edited, and an untouched file is never opened for writing.
- Signals are first-class net objects, each with one driver and any number of readers. The GUI draws a net as a fan of links.
- The instance hierarchy is checked for cycles when it loads. A self-instantiating entity appears as a back-reference node that can't be expanded.
- First CI gates: no-op round trip, a single edit, idempotence, tokenizer edge cases, and the cycle check. The corpus is marked `-text` in `.gitattributes`.

## Decision log

| Date | Decision | By |
|---|---|---|
| 2026-10-01 | Project started. C, Linux and Windows, immediate-mode GPU GUI, no verify or simulate. | Alex |
| 2026-10-01 | Public repo `awschult002/vhdiagram`, with CI on Ubuntu and Windows (MSYS2 gcc) and tag-triggered releases. | Alex, set up by Chief of Staff |
| 2026-10-01 | Processes, `generate` and `block` statements aren't modeled and are copied through byte for byte. Only instances, port maps and signal declarations are rewritten. | Alex |
| 2026-10-01 | Both direct and component instantiation are read; output always uses direct instantiation (library rule amended below). | Alex |
| 2026-10-01 | Recursive nodes mean opening a node into its internal graph. Recursive `generate` isn't supported. | Alex |
| 2026-10-01 | Type and width mismatches are flagged but allowed, and written out as drawn. | Alex |
| 2026-10-01 | Node positions aren't saved. Automatic layout on load, plus an Auto layout button. | Alex |
| 2026-10-01 | GUI is cimgui with imnodes, chosen over Nuklear because Nuklear's node editor is too limited. | Alex |
| 2026-10-01 | Parsing should be simple, reliable and extensible, not clever. | Alex |
| 2026-10-01 | VHDL-2008 is the target standard. | Alex |
| 2026-10-01 | No slices or conversion functions in port maps for now. They may become their own node type later. | Alex |
| 2026-10-01 | A width that depends on a generic is shown as its expression text, not computed. | Alex |
| 2026-10-01 | More than one entity per file is supported. More than one architecture per entity isn't. | Alex |
| 2026-10-01 | Only the top directory and below are read, never vendor paths. Entities with no declaration found there become greyed-out nodes that can't be edited or opened. | Alex |
| 2026-10-01 | Only entities, ports, generics and maps are parsed for meaning. Everything else is skipped as tokens. | Alex |
| 2026-10-01 | An association the tool can't model, and a greyed-out node's port map, are kept as locked text and copied through byte for byte. A greyed-out node with a `component` declaration shows those ports read-only. | Team (Tester, Dev, Senior) |
| 2026-10-01 | hdl-modules, with its AXI and AXI-Stream modules, is the inspiration for the tool. | Alex |
| 2026-10-01 | The test corpus is hdl-modules pinned at `8142d3a`. | Team (Tester), following Alex |
| 2026-10-01 | Existing library prefixes are kept. New instances take their library from `-L`, or `work`. Entities are keyed by library and name, and an ambiguous name greys the node out with a warning. Amends R-OUT-5. | Team (Tester, Dev, Senior, Oracle), confirmed by Alex |
| 2026-10-01 | "About 30 files" means the C codebase. VHDL projects can have hundreds of files. Only the graph being edited stays loaded; the node list keeps summaries only. | Alex |
