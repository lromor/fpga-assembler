{
  description = "fpga-assembler";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/e643668fd71b949c53f8626614b21ff71a07379d";
    flake-parts.url = "github:hercules-ci/flake-parts";

    treefmt-nix.url = "github:numtide/treefmt-nix";
  };

  nixConfig = {
    extra-substituters = [
      "https://fpga-assembler.cachix.org"
    ];
    extra-trusted-public-keys = [
      "fpga-assembler.cachix.org-1:yp4kNzY1Hru9BlaoE025RuBaL/sX6Ou2abo5L8SHk0I="
    ];
  };

  outputs =
    inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake
      {
        inherit inputs;
      }
      {
        imports = [
          inputs.treefmt-nix.flakeModule
        ];

        systems = [
          "aarch64-darwin"
          "aarch64-linux"
          "x86_64-darwin"
          "x86_64-linux"
        ];

        perSystem =
          { pkgs, system, ... }:
          {
            treefmt = {
              projectRootFile = "flake.nix";
              programs.yamlfmt.enable = true;
              programs.nixfmt.enable = true;
            };
            devShells.default =
              with pkgs;
              mkShell {
                packages = [
                  git
                  bazel_7
                  jdk
                  bash
                  gdb

                  # For clang-tidy and clang-format.
                  clang-tools

                  # For buildifier, buildozer.
                  bazel-buildtools
                  bant

                  # Profiling and sanitizers.
                  perf
                  pprof
                  perf_data_converter
                  valgrind

                  # FPGA utils.
                  openfpgaloader
                ];

                CLANG_TIDY = "${clang-tools}/bin/clang-tidy";
                CLANG_FORMAT = "${clang-tools}/bin/clang-format";
              };

            # Package fpga-assembler.
            packages.default =
              (pkgs.callPackage (
                {
                  buildBazelPackage,
                  stdenv,
                  fetchFromGitHub,
                  lib,
                  nix-gitignore,
                }:
                let
                  system = stdenv.hostPlatform.system;
                  registry = fetchFromGitHub {
                    owner = "bazelbuild";
                    repo = "bazel-central-registry";
                    rev = "c0249a798b9f367d0844c73c4c310350b0b15ade";
                    hash = "sha256-vSs4XMe3wnfS8G7kj561rVgUqmpMLuL3qN+X21LFqy8=";
                  };
                in
                with pkgs;
                buildBazelPackage {
                  pname = "fpga-as";

                  version = "0.0.1";

                  src = nix-gitignore.gitignoreSourcePure [ ] ./.;

                  bazelFlags = [
                    "--registry"
                    "file://${registry}"
                  ];

                  postPatch = ''
                    patchShebangs scripts/create-workspace-status.sh
                  '';

                  fetchAttrs = {
                    hash =
                      {
                        aarch64-linux = "sha256-E4VHjDa0qkHmKUNpTBfJi7dhMLcd1z5he+p31/XvUl8=";
                        x86_64-linux = "sha256-9jvyo0qlzPHYvs/Uou5j0bUU3oq/SPVDAq9ydlOBr2k=";
                      }
                      .${system} or (throw "No hash for system: ${system}");
                  };

                  removeRulesCC = false;
                  removeLocalConfigCc = false;
                  removeLocalConfigSh = false;

                  nativeBuildInputs = [
                    jdk
                    git
                    bash
                    # Convenient tool to enter into the sandbox and start debugging.
                    # breakpointHook
                  ];

                  bazel = bazel_7;

                  bazelBuildFlags = [ "-c opt" ];
                  bazelTestTargets = [ "//..." ];
                  bazelTargets = [ "//fpga:fpga-as" ];

                  buildAttrs = {
                    installPhase = ''
                      install -D --strip bazel-bin/fpga/fpga-as "$out/bin/fpga-as"
                    '';
                  };

                  meta = {
                    description = "Tool to convert FASM to FPGA bitstream.";
                    homepage = "https://github.com/lromor/fpga-assembler";
                    license = lib.licenses.asl20;
                    platforms = lib.platforms.linux;
                  };
                }
              ) { }).overrideAttrs
                (
                  final: prev: {
                    # Fixup the deps so they always contain correrct
                    # shebangs paths pointing to the store.
                    deps = prev.deps.overrideAttrs (
                      final: prev: {
                        installPhase = ''
                          patchShebangs $bazelOut/external
                        ''
                        + prev.installPhase;
                      }
                    );
                  }
                );
          };
      };
}
