# Reference parity

The reference chain this assembler replaces is `fasm2frames` + `xc7frames2bit`
from `openXC7/prjxray`. These cases are the inputs where `fpga-as` used to
disagree with it, kept as the "gold standard" to check against:

| case | part | what it pins down |
| --- | --- | --- |
| `01-missing-feature` | `xc7a35tfgg484-2` | a feature no database defines must fail with a message, not terminate the process |
| `02-zynq7-required-features` | `xc7z010clg400-1` | the part database's `required_features.fasm` has to be read and emitted |
| `03-pudcb-pullup-opt-in` | `xc7a35tcsg324-1` | the PUDC_B pull-up is injected only when asked for (`--emit_pudc_b_pullup`) |
| `04-wide-feature-value` | `xc7a35tfgg484-2` | an 83-bit value on a range wider than 64 bits resolves correctly |
| `06-alias-abort` | `xc7k325tffg900-2` | a pseudo PIP is looked up in the tile's own database, and is a no-op, not a crash |

Each directory holds the input `input.fasm` and — where the reference assembles
it — `gold.frames`, the reference's configuration frames, canonicalised to the
frames that are not all zero. `cases.tsv` says which part, which family and what
is expected.

## Running the check

```sh
nix develop /path/to/toolchain-nix           # exports PRJXRAY_DB_DIR
FPGA_AS=bazel-bin/fpga/fpga-as fpga/xilinx/testdata/reference-parity/check.sh
```

The database is not committed — it is hundreds of MB — so the check reads
`PRJXRAY_DB_DIR` (the annotated `prjxray-db`, the same revision the toolchain
pins) and reports `skipped` when it is unset, which keeps it safe to call from
anywhere. `FPGA_AS` defaults to `fpga-as` on `PATH`.

## Why these cases

They came out of comparing both chains over the 21 `demo-projects` designs that
carry a `.fasm`: before the fix 19 of them produced a different configuration
and 2 terminated without a bitstream at all; after it, all 20 designs the
reference can assemble are frame-identical, and the 21st fails on a database gap
that the reference hits too.

The raw evidence — the failed bitstreams, the payload byte diffs, the stderr of
both builds — is in a gist:
<https://gist.github.com/hansfbaier/20a7b7da75d2bf8fc28ad92936d620e2>.

One case is deliberately **not** checked here: the same `RXCDR_CFG[82:0]`
feature carrying the value from `demo-projects/litex-sata-alientek-davincipro/litex_pcie.fasm`
still disagrees with the reference in two frames (`0x2129e`, `0x2129f`). It is
open, and it is in the gist with the bit positions; a failing check in the tree
would only be noise until it is understood.
