{ pkgs ? import <nixpkgs> {} }:
let
  bazel = pkgs.bazel_7;

  # There is too much volatility between even micro-versions of
  # newer clang-format. Use slightly older version for now.
  clang_for_formatting = pkgs.llvmPackages_17.clang-tools;

  # clang tidy: use latest.
  clang_for_tidy = pkgs.llvmPackages_18.clang-tools;
in
pkgs.mkShell {
  name = "fpga-assembler";
  packages = with pkgs; [
    git
    bazel
    jdk
    bash
    gdb

    # For clang-tidy and clang-format.
    clang_for_formatting
    clang_for_tidy

    # For buildifier, buildozer.
    bazel-buildtools
    bant

    # Profiling and sanitizers.
    linuxPackages_latest.perf
    pprof
    perf_data_converter
    valgrind

    # FPGA utils.
    openfpgaloader
    verible
  ];

  CLANG_TIDY="${clang_for_tidy}/bin/clang-tidy";
  CLANG_FORMAT="${clang_for_formatting}/bin/clang-format";
}
