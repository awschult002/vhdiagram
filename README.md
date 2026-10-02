# vhdiagram

A desktop node editor for stitching existing VHDL entities together (Linux and Windows).

Give it a top-level VHDL file. It scans that file's directory recursively, lists every entity it finds, and opens the top as a node graph. Entities are nodes, ports and generics come from the VHDL, and connections are signals. Saving writes one file per entity and leaves untouched entities alone.

It does not compile, simulate or verify VHDL.

Status: requirements interview in progress. See `docs/`.

## Build

    make        # build ./vhdiagram
    make test   # run tests
