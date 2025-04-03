{
  description = "fpga-assembler";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };
      in {
        devShells.default =
          let
            bazel = pkgs.bazel_7;

            # There is too much volatility between even micro-versions of
            # newer clang-format. Use slightly older version for now.
            clang_for_formatting = pkgs.llvmPackages_17.clang-tools;

            # clang tidy: use latest.
            clang_for_tidy = pkgs.llvmPackages_18.clang-tools;
          in with pkgs; pkgs.mkShell{
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
            ];

            CLANG_TIDY="${clang_for_tidy}/bin/clang-tidy";
            CLANG_FORMAT="${clang_for_formatting}/bin/clang-format";
          };
      }
    );
}
