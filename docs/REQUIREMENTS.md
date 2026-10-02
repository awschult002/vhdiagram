# Requirements

Collected from the customer interview with Alex. The Scribe maintains this file and the decision log below. Each requirement has an ID so tests can cite it. **Open** items are waiting on an answer.

## Purpose

Make it easier to stitch existing VHDL modules together. You open a design, see its entities as nodes, wire them up visually, and write the result back as VHDL.

**Out of scope:** checking syntax, compiling, verifying and simulating.

## Platform and build

- **R-PLAT-1** Desktop application for Linux and Windows.
- **R-PLAT-2** Written in C, built with a clean `Makefile`.
- **R-PLAT-3** Immediate-mode GUI with GPU support, for example Dear ImGui. *Open: is C++ under a C interface (cimgui + imnodes) acceptable, or does it have to be pure C (for example Nuklear)?*
- **R-PLAT-4** Hosted on GitHub, with CI test runners and release builds for both platforms. *Status: CI and tag-triggered release workflows are set up.*

## Input and scanning

- **R-IN-1** The input is the VHDL file at the top of the hierarchy. A directory as input is also mentioned. *Open: which directory is the starting point when given a file?*
- **R-IN-2** Scan the containing directory recursively for VHDL files and parse every entity found.
- **R-IN-3** Every entity found goes into the list of available nodes.
- **R-IN-4** The editor opens with the node graph of the top-level entity.
- **R-IN-5** Each entity's comment description header is read and shown with its node. *Open: is there a fixed header format, or just the comment block directly above `entity`?*

## Nodes and connections

- **R-NODE-1** Each node is a VHDL entity.
- **R-NODE-2** A node's ports are the entity's VHDL ports.
- **R-NODE-3** A node's parameters are the entity's generics.
- **R-NODE-4** Records are supported. *Open: should the tool parse packages for record types, and should a record be connected as a whole or by individual fields?*
- **R-NODE-5** Recursive nodes are supported: you can open a node to see its own internal graph. *Open: does this also cover an entity that instantiates itself through `generate`?*
- **R-CONN-1** A connection is a signal in the generated source code.

## Editing

- **R-EDIT-1** Drag and drop nodes onto the graph from the available list.
- **R-EDIT-2** Connect node ports to each other.
- **R-EDIT-3** Create new custom entities, open them, and add to them recursively.
- *Open: should the tool flag obvious problems such as a width mismatch or two drivers on one signal, or write out whatever was drawn?*
- *Open: where should node positions be stored: a sidecar file, or structured comments in the VHDL?*

## Output

- **R-OUT-1** Writes one or more VHDL files: one entity per node and one file per entity.
- **R-OUT-2** Creates new files or overwrites existing ones.
- **R-OUT-3** A file whose entity was not edited is never touched.
- *Open: in an edited architecture that also has processes, concurrent assignments or `generate`, should only the instances, port maps and signal declarations be rewritten, with everything else kept exactly as written?*
- *Open: direct instantiation (`entity work.foo`), component declarations, or both?*

## Scale

- *Open: how many files and entities are in a typical project?*

## Decision log

| Date | Decision | By |
|---|---|---|
| 2026-10-01 | Project started. C, Linux and Windows, immediate-mode GPU GUI, no verify or simulate. | Alex |
| 2026-10-01 | Public repo `awschult002/vhdiagram`, with CI on Ubuntu and Windows (MSYS2 gcc) and tag-triggered releases. | Alex, set up by Chief of Staff |
