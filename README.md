[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Continuous Integration](https://github.com/lromor/fpga-assembler/workflows/ci/badge.svg)](https://github.com/lromor/fpga-assembler/actions/workflows/ci.yml)

[fasm-spec]: https://fasm.readthedocs.io/en/stable/#
[bazel]: https://bazel.build/
[counter-example]: https://github.com/chipsalliance/f4pga-examples/blob/13f11197b33dae1cde3bf146f317d63f0134eacf/xc7/counter_test/counter.v

# fpga-assembler

This command-line tool converts [FASM][fasm-spec] files into bitstreams, simplifying the assembly of human-readable FPGA configurations into the binary formats needed to program various FPGAs.

At this stage, it can generate a set of frames in the same manner as [fasm2frames](https://github.com/chipsalliance/f4pga-xc-fasm/blob/25dc605c9c0896204f0c3425b52a332034cf5e5c/xc_fasm/fasm2frames.py).
It has been tested with the Artix-7 [counter example][counter-example], where it produces identical frames—at approximately 10 times the speed.

## Usage

First, install [Bazel][bazel] and ensure you have a basic C/C++ toolchain set up. Then run:

``
bazel run -c opt //fpga:fpga-as -- --prjxray_db_path=/some/path/prjxray-db/artix7 --part=xc7a35tcsg324-1 < /some/path.fasm
``
