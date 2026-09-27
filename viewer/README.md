# Fabric viewer

A 2D viewer for the Xilinx fabrics the assembler supports. You give it a FASM
line; it shows you which tile that line configures, where that tile sits on
the die, and which configuration bits change.

```
bazel run -c opt //viewer/server -- \
  --prjxray-db-path=/some/path/prjxray-db \
  --preload=artix7/xc7a35tcsg324-1
```

Then open <http://127.0.0.1:8080>.

`--prjxray-db-path` is the root that holds the family directories
(`artix7`, `kintex7`, `spartan7`, `zynq7`), not one family. It also reads
`PRJXRAY_DB_PATH`. `--preload` is optional and only moves the cost of the
first parse off the first page load.

## What it shows

The canvas draws the fabric as prjxray's tile grid lays it out: one cell per
tile at its `grid_x`, `grid_y`. Each tile type gets its own shade of its
category's colour, so the dozen kinds of interconnect and the half dozen
kinds of CLB are told apart without losing the category at a glance; tiles
with no configuration bits of their own are dimmed, because they are the
scaffolding between the columns that do the work. The panel on the left
lists every tile type in the part with its count, and clicking one isolates
it. Zoom in far enough and the tiles label themselves -- the type first,
then the full name once the cell is wide enough to hold it without running
into its neighbour.

A resolved FASM line rings the tiles it touches and draws crosshairs through
them, so a single changed tile is findable at full-die zoom.

### Three coordinate systems

The one thing worth knowing before reading a fabric is that a tile has
three sets of numbers, and they do not agree:

  * **grid** (`grid_x`, `grid_y` in `tilegrid.json`) is the position on the
    die, unique per tile. It is what this viewer draws.
  * **the tile name** (`CLBLM_R_X33Y38`) is numbered within the tile type,
    so types reuse each other's indices: on `xc7a50t`, `X0Y0` names five
    different tiles, among them `INT_L_X0Y0` and `LIOB33_SING_X0Y0`.
  * **the site name** (`SLICE_X52Y38`) is numbered within the site type
    across the whole device. `CLBLM_R_X33Y38` holds `SLICE_X52Y38` and
    `SLICE_X53Y38`: the Y agrees with the tile, the X does not.

Selecting a tile spells its three out.

The inset in the corner is the selected tile's own window into the bitstream:
one column per frame, one row per bit, over the rectangle `tilegrid.json`
gives the tile. The bits the line drives are lit -- amber for a bit it sets,
blue for one the database clears with a leading `!`, because a feature that
half clears is a feature that half works.

The panel on the right spells the same thing out in words: the tile, its
sites, its bit window, and per feature the frame address, word and bit of
every bit that moved. A LUT `INIT` also gets its 64 bits drawn out, and an
interconnect feature is named the way the database means it -- prjxray
writes a pip as `destination.source`, because the database stores a block of
bits per destination signal and one pattern within it per source that can
drive it, which is the one piece of the notation a reader is likely to have
backwards.

## Reference

The right panel has a Reference tab. It explains how a FASM line turns into
frame bits. It also gives a glossary of the 7-series terms the rest of the
viewer uses: tile, tile type, site, interconnect tile, PIP, pseudo PIP,
frame, word, frame address, configuration bus, column, segment, bit block,
segbits file, LUT and INIT, clock region, the three sets of coordinates,
and FASM. A term in a resolved feature links into it, so "pseudo PIP" in a
result is one click from what a pseudo PIP is.

The text follows ASD-STE100 Simplified Technical English: short sentences,
plain words, and one name for one thing. The viewer used to say "inset" and
"bit window", which it never defined. The square in the corner is now just
that, and the part of the bitstream that belongs to a tile is a **bit
block**, after the `bits` entry in `tilegrid.json` and `BitsBlock` in the
C++.

The knowledge comes from [Project X-Ray][prjxray]. It documented the
7-series bitstream format and it makes the database this tool reads. The
wording here is this repository's own. Every number was checked two times:
against that documentation, and against the code here. The frame address
was checked against `fpga/xilinx/arch-xc7-frame.h`. The feature lookup was
checked against `fpga/database.cc`. Project X-Ray uses the ISC license, and
`fpga/xilinx/` carries code from it under that license. The content lives
in `viewer/static/reference.js`, apart from the code that draws it.

## How it is put together

```
fpga/ffi/        a C ABI over fpga::PartDatabase and fasm::Parse
viewer/fabric/   a safe Rust wrapper over that ABI
viewer/server/   axum: loads databases, answers the browser
viewer/web/      wgpu, compiled to wasm: draws the canvas
viewer/static/   the page around the canvas
```

The point of the C ABI is that nothing here re-implements the database. A
FASM line entered in the browser is resolved by `fpga::PartDatabase::
ConfigBits` -- the same call `//fpga:fpga-as` makes to assemble a bitstream --
so the bits the viewer draws for a line are the bits the assembler would set
for it.

It is not a whole assembly, and does not claim to be. `fpga-as` also injects
the configuration the reference implementation adds around a design (see
`fpga/injected-features.h`) and pads out every frame of a touched bit block;
the viewer shows what the lines in front of you do, and nothing else. `fpga/ffi`
exposes `fpga_view_abi_sizeof` for exactly this reason: the hand-written Rust
binding asserts its struct layouts against the C++ ones the first time a
handle is opened, rather than trusting that the two descriptions of the same
struct have not drifted.

The renderer draws one thing: an instanced rectangle. Tiles, bit cells,
highlight rings and crosshairs are all that primitive with a different
camera, which is why the fabric map and the bit inset share a pipeline and
differ only by a uniform and a viewport.

## Working on it

```
bazel test //fpga/ffi/... //viewer/...   # the ABI and the wrapper
bazel build //viewer/web:bundle          # the wasm, via wasm-bindgen
```

The browser needs WebGPU or WebGL2; the renderer asks for WebGPU and falls
back, and it keeps to the WebGL2 limits so the fallback is not a different
program.

Third party crates are declared in `MODULE.bazel` and resolved by
crate_universe, so there is no `Cargo.toml` to keep in step with the Bazel
build. `wasm-bindgen` is pinned because it refuses to run against a CLI of a
different version, and the CLI comes from `rules_rust_wasm_bindgen`; the two
versions have to be bumped together.

The Nix flake packages `fpga-as` and deliberately builds only `//fpga/...`:
putting the viewer in it would pull a Rust toolchain and the whole crate
universe into a fixed-output derivation. The viewer is built and tested by
the Bazel CI instead.

[prjxray]: https://github.com/f4pga/prjxray
