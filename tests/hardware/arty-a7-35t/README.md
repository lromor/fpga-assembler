# Arty A7-35T smoke test

The FASM fixtures configure GPIO levels and a JA2-to-JA1 loopback for
[PiCi's wiring](https://github.com/CarlFK/pici/wiki/Wiring-Diagram).
Every feature is commented. The four-feature `led-on.fasm` was checked on a
local Arty A7-35T. Low/high/low and the full loopback trace passed on FPGAs
Online pi3 (Raspberry Pi 4, Arty A7-35T) on 2026-11-02.

Run these commands from the repository root. Build the assembler and runner
with Bazel (`nix develop` supplies the toolchain):

```sh
bazel build -c opt //fpga:fpga-as //tests/hardware/arty-a7-35t:hardware_test
bazel-bin/fpga/fpga-as --prjxray_db_path="$HOME/prjxray-db/artix7" \
    --part=xc7a35tcsg324-1 < tests/hardware/arty-a7-35t/loopback.fasm > /tmp/loopback.bit
openFPGALoader -b arty --freq 1000000 -m /tmp/loopback.bit
bazel-bin/tests/hardware/arty-a7-35t/hardware_test loopback /dev/gpiochipN
```

Replace `/dev/gpiochipN` with the Pi's header GPIO controller. The runner
requires Linux GPIO character-device API v2 (Linux 5.10+) and permission to
open that device and request its lines. Chip numbering may vary: select the
controller and verify its line offsets against the board mapping. Offsets are
not physical header pin numbers. Bitstreams may be assembled elsewhere and
copied to the board.
Program the matching fixture before each invocation:

| Fixture | Runner argument | Behavior |
| --- | --- | --- |
| `gpio-low.fasm` | `low` | JA1 must read low |
| `gpio-high.fasm` | `high` | JA1 must read high |
| `loopback.fasm` | `loopback` | Drive JA2 with `__--_-__`; JA1 must follow |
| `led-on.fasm` | — | Hold LD4 (H5) high for a visual check |

For the constant-level smoke test, program and check low, then high, then low
again. Programming uses SRAM; flash is not changed.

## Wiring and channel names

`JA1` and `JA2` mean pins 1 and 2 of the Arty's JA Pmod connector. They
connect to the same numbered pins on the Pi's Pmod HAT, as shown in
[PiCi's harness mapping](https://github.com/CarlFK/pici/wiki/Wiring-Diagram).
The names below link to Digilent's exact FPGA pin assignments; the GPIO links
show the HAT-to-Pi mapping on page 4 of its manual.

| Channel | Arty connector and FPGA package pin | Pi BCM GPIO / chip line offset | Pi 40-pin header position | Role in these tests |
| --- | --- | --- | --- | --- |
| [JA1][arty-ja1] | JA pin 1, package G13 (`ja[0]` in the XDC) | [GPIO8 / offset 8][hat-pinout] | 24 | FPGA drives; Pi samples (`expect`) |
| [JA2][arty-ja2] | JA pin 2, package B11 (`ja[1]` in the XDC) | [GPIO10 / offset 10][hat-pinout] | 19 | Pi drives (`inputs`); FPGA receives |
| [LD4][arty-ld4] | Onboard LED, package H5 (`led[0]` in the XDC) | — | — | FPGA drives; visual check only |

[arty-ja1]: https://github.com/Digilent/digilent-xdc/blob/c8644a9fe31cc803789643124545d56b3bef4600/Arty-A7-35-Master.xdc#L43
[arty-ja2]: https://github.com/Digilent/digilent-xdc/blob/c8644a9fe31cc803789643124545d56b3bef4600/Arty-A7-35-Master.xdc#L44
[arty-ld4]: https://github.com/Digilent/digilent-xdc/blob/c8644a9fe31cc803789643124545d56b3bef4600/Arty-A7-35-Master.xdc#L31
[hat-pinout]: https://digilent.com/reference/_media/reference/add-ons/pmod-hat/pmod-hat-adapter_rm.pdf#page=4

The loopback path is Pi GPIO10/header 19 → HAT JA2 → Arty JA2/B11 →
FPGA routing → Arty JA1/G13 → HAT JA1 → Pi GPIO8/header 24. The XDC's
port arrays start at zero; physical connector positions start at one.
The `8` and `10` in the C++ traces are chip line offsets, never header positions
`24` and `19`. On the verified pi3 host, `/dev/gpiochip0` is the
`pinctrl-bcm2711` controller and its offsets match these BCM GPIO numbers.

Use common ground and 3.3 V logic. The HAT's Pmod ground pins are 5 and 11;
pins 6 and 12 carry 3.3 V. JA2/JB2, JA3/JB3, and JA4/JB4 share nets on the
HAT, so they cannot be driven independently. JD is disconnected in PiCi's
harness. Coordinate board use with the operator and configure the test pins
for GPIO use. The runner requests only the listed lines from the kernel;
it does not rewrite device-tree overlays or fall back to register access.
Missing devices, permission errors, invalid offsets, busy lines, and failed
GPIO operations fail the test. A failed read is never treated as a low level.

Device-tree overlays can assign pins to peripherals or reserve GPIO lines.
The kernel's ownership checks decide whether a request is allowed. Some older
Pi pinctrl drivers do not enforce exclusivity between GPIO and alternate pin
functions ([kernel documentation](https://docs.kernel.org/driver-api/pin-control.html)),
so a successful request alone does not prove the pinmux is safe.
Keep SPI/I²C/UART disabled on test pins; do not rely only on `/dev` existing.

## Channel traces

A `ChannelTrace` names a channel, its chip line offset, and a wave. Tests assign
traces to `inputs` or `expect`; the trace itself has no fixed role. Add rows to
either group to describe several channels together:

```cpp
{.name = "loopback",
 .inputs = {{"JA2", 10, "__--_-__"}},
 .expect = {{"JA1",  8, "__--_-__"}}},
```

Each row is a channel and each column is a logical step. `_` means low and
`-` means high. Waves must have equal, nonzero lengths and use distinct GPIOs.
Each column holds its inputs and waits up to one second for all expected
outputs to match in the same sample. These are steady-level checks, not pulse
timing measurements.

`hardware_test --list` renders the configured traces without GPIO access.
A run prints inputs, expectations, captured output waves, and `[PASS]` or
`[FAIL]`. A failed wave stops at the mismatching column. Only the channels in
the test are configured. Output direction and initial value are set together.
Normal completion checks that driven pins return to inputs; error cleanup
also attempts this before releasing the request. Cleanup errors are reported.
Failure returns a nonzero exit status.

The **Hardware / Test** GitHub job assembles these fixtures, cross-compiles a
static ARM executable with Bazel, and uploads both to FPGAs Online over SSH.
It programs and checks low, high, low, then loopback. The Pi needs
`openFPGALoader`, `flock`, and GPIO v2 access; it does not need Bazel. The CI
binary targets ARMv7 Linux on a Pi 2-4, including PiCi's 32-bit OS.

Cross-compile from the existing Nix development shell with:

```sh
bazel build -c opt \
  --platforms=//tests/hardware/arty-a7-35t:raspberry_pi \
  //tests/hardware/arty-a7-35t:hardware_test
```

The registered [hermetic LLVM toolchain](https://github.com/hermeticbuild/hermetic-llvm)
supplies the compiler, Linux headers, and static musl runtime. No system cross
compiler or target sysroot is needed. The same command runs in Ubuntu CI;
ordinary native builds keep their existing toolchain.

Choose an available A7-35T with the wiring above and configure these repository
secrets: `FPGAS_ONLINE_SSH_PASSWORD`, `FPGAS_ONLINE_KNOWN_HOSTS`, and
`FPGAS_ONLINE_SSH_CONFIG`. Set repository variable `FPGAS_ONLINE_GPIO_CHIP`
to the verified header controller's path, for example `/dev/gpiochip0`.
The workflow requires this setting; it does not guess the chip. Use the board's
password and obtain its host key through the provider. Password login needs no
changes to the Pi's boot image. Each run uploads its artifacts again, so a Pi
reboot does not require reinstalling our test files or an SSH public key.
Use this SSH configuration with the host and forwarded port from the board's page:

```sshconfig
Host arty
  HostName ps1.fpgas.online
  Port 10222
  User pi
  PreferredAuthentications password
  PubkeyAuthentication no
  NumberOfPasswordPrompts 1
  StrictHostKeyChecking yes
  ConnectTimeout 15
```

The job runs on pull requests, main pushes, and manual dispatch. Missing
configuration, unreachable hardware, programming errors, GPIO access errors, and mismatches
fail the job. Fork pull requests have no access to these secrets; run a
reviewed branch in this repository to exercise hardware. GitHub serializes
this repository's runs, and `flock` serializes users of the same Pi lock.
Carl has agreed that we can assume no other users for now. There is no
service-wide queue: these locks coordinate our runs, but do not exclude other
visitors. Revisit board allocation with the operator if contention appears.

The GPIO abstraction is adapted from Henner Zeller's
[rpi-rgb-led-matrix GPIO code](https://github.com/hzeller/rpi-rgb-led-matrix/blob/3da7a3bdfca4637bb79ff038bc2bb26965197db5/lib/gpio.cc).
Its abstraction now uses Linux GPIO v2 requests for direction, reads, and
masked writes, with no exceptions or external GPIO library. Copyright and GPL-2.0 license are kept
in [`gpio.h`](../gpio.h) and [the GPL license text](../../../LICENSES/GPL-2.0-only.txt).
Root [`COPYING`](../../../COPYING) describes which files use each license.
The standalone hardware runner uses GPL-2.0-only; it does not link the assembler.
