# Arty A7-35T smoke tests

These tests program an Arty A7-35T and use a Raspberry Pi to check its I/O.

## Wiring and channel names

Connect the Arty's JA connector to JA on the Pi's Pmod HAT, following
[PiCi's wiring](https://github.com/CarlFK/pici/wiki/Wiring-Diagram).
JA1 means pin 1 of that connector; JA2 means pin 2.

| Arty signal | FPGA package pin | Pi GPIO (BCM) | Pi header pin | Direction |
| --- | --- | --- | --- | --- |
| [JA1][arty-ja1] | G13 | [GPIO8][hat-pinout] | 24 | FPGA → Pi |
| [JA2][arty-ja2] | B11 | [GPIO10][hat-pinout] | 19 | Pi → FPGA |
| [LD4][arty-ld4] | H5 | — | — | Onboard LED; visual check only |

GPIO8 is the Pi signal name; it is on physical header pin 24. The C++ trace
uses `8`. Likewise, GPIO10 is on header pin 19, and the trace uses `10`.
On FPGAs Online's `pi3`, both signals belong to `/dev/gpiochip0`. Check the
GPIO device before using a different Pi.

Use 3.3 V signals and common ground. Disable SPI or any other peripheral
using GPIO8 or GPIO10. On the HAT, JA2/JB2, JA3/JB3, and JA4/JB4 share wires;
do not drive the corresponding JB pins separately. Pmod pins 5 and 11 are
ground; 6 and 12 are 3.3 V. PiCi leaves JD disconnected.

[arty-ja1]: https://github.com/Digilent/digilent-xdc/blob/c8644a9fe31cc803789643124545d56b3bef4600/Arty-A7-35-Master.xdc#L43
[arty-ja2]: https://github.com/Digilent/digilent-xdc/blob/c8644a9fe31cc803789643124545d56b3bef4600/Arty-A7-35-Master.xdc#L44
[arty-ld4]: https://github.com/Digilent/digilent-xdc/blob/c8644a9fe31cc803789643124545d56b3bef4600/Arty-A7-35-Master.xdc#L31
[hat-pinout]: https://digilent.com/reference/_media/reference/add-ons/pmod-hat/pmod-hat-adapter_rm.pdf#page=4

## Tests

| FASM file | Runner argument | Check |
| --- | --- | --- |
| `gpio-low.fasm` | `low` | JA1 is low |
| `gpio-high.fasm` | `high` | JA1 is high |
| `loopback.fasm` | `loopback` | Drive JA2 with `__--_-__`; JA1 follows |
| `led-on.fasm` | — | LD4 lights up; no Pi check |

Each file comments its FASM features. Programming below uses SRAM and leaves
flash unchanged.

## Build and run

Build on your workstation, from the repository root. `nix develop` supplies
the build tools. The Pi needs `openFPGALoader`, Linux 5.10 or newer, and GPIO
device permissions for the user running the test.

```sh
bazel build -c opt //fpga:fpga-as
bazel-bin/fpga/fpga-as --prjxray_db_path="$HOME/prjxray-db/artix7" \
  --part=xc7a35tcsg324-1 < tests/hardware/arty-a7-35t/loopback.fasm > loopback.bit
bazel build -c opt \
  --platforms=//tests/hardware/arty-a7-35t:raspberry_pi \
  //tests/hardware/arty-a7-35t:hardware_test
```

Copy `loopback.bit` and `bazel-bin/tests/hardware/arty-a7-35t/hardware_test`
to the Pi. Then run there:

```sh
flock -w 60 /tmp/fpga-as-ci.lock bash -e <<'TEST'
openFPGALoader -b arty --freq 1000000 -m loopback.bit
./hardware_test loopback /dev/gpiochip0
TEST
```

Program the matching bitstream before each test. To check constant levels,
run low, high, then low again. The runner prints the captured trace and
`[PASS]` or `[FAIL]`; failures return a nonzero exit status. Driven GPIOs
return to inputs when the test finishes.

## Channel traces

In `hardware_test.cc`, `inputs` are driven by the Pi and `expect` are read by
the Pi. Each row contains a channel name, a GPIO number, and a sequence:

```cpp
{.name = "loopback",
 .inputs = {{"JA2", 10, "__--_-__"}},
 .expect = {{"JA1",  8, "__--_-__"}}},
```

`_` means low and `-` means high. Each column is one step: drive the inputs,
then wait up to one second for all outputs to match. Rows must have the same
length and use different GPIOs. These tests check levels, not precise timing.
Use `./hardware_test --list` to preview the traces without accessing GPIOs.

## GitHub Actions

PRs run the usual checks and build the hardware fixtures and Pi executable.
They do not access the board. To test a PR before merging, open **Actions →
Hardware → Run workflow**, select the PR's branch, and start the run. This
manual action runs low, high, low, and loopback on the Pi. Hardware tests
are optional; keep `Hardware / Test` out of required merge checks.

The workflow must first be present on `main` for the Run workflow button to
appear. Configure these repository settings:

| Setting | Type | Value |
| --- | --- | --- |
| `FPGAS_ONLINE_SSH_PASSWORD` | Secret | Pi login password |
| `FPGAS_ONLINE_SSH_CONFIG` | Secret | SSH configuration below |
| `FPGAS_ONLINE_KNOWN_HOSTS` | Secret | Pi host key verified with the provider |
| `FPGAS_ONLINE_GPIO_CHIP` | Variable | `/dev/gpiochip0` on pi3 |

SSH configuration for pi3:

```sshconfig
Host arty
  HostName ps1.fpgas.online
  Port 10322
  User pi
  PreferredAuthentications password
  PubkeyAuthentication no
  NumberOfPasswordPrompts 1
  StrictHostKeyChecking yes
  ConnectTimeout 15
```

Only one hardware job runs at a time across all branches, with one pending
run. A new request replaces the pending run without interrupting the active
one. On the Pi, `/tmp/fpga-as-ci.lock` stays locked for the entire programming
and testing sequence. Local tests must use the same lock, as above. Other
visitors can still bypass it.

Uploads are temporary; each run sends them again. Fork PRs need a reviewed
branch in this repository before running hardware tests with its secrets.

[The hardware run on pi3 passed all four checks](https://github.com/lromor/fpga-assembler/actions/runs/41388797008).
For the hardware code's license, see [COPYING](../../../COPYING).
