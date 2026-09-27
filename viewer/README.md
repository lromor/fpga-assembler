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
tile, coloured by what the tile is, with the interconnect and the scaffolding
dimmed so the columns of logic, memory, DSP and IO stand out. A resolved FASM
line rings the tiles it touches and draws crosshairs through them, so a single
changed tile is findable at full-die zoom.

The inset in the corner is the selected tile's own window into the bitstream:
one column per frame, one row per bit, over the rectangle `tilegrid.json`
gives the tile. The bits the line drives are lit -- amber for a bit it sets,
blue for one the database clears with a leading `!`, because a feature that
half clears is a feature that half works.

The panel on the right spells the same thing out in words: the tile, its
sites, its bit window, and per feature the frame address, word and bit of
every bit that moved. A LUT `INIT` also gets its 64 bits drawn out, and an
interconnect feature is named the way the database means it -- prjxray writes
a pip as `destination.source`, which is the one piece of the notation a
reader is likely to have backwards.

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
so what the viewer draws is what the assembler would write. `fpga/ffi`
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
