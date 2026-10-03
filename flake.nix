{
  description = "fpga-assembler";

  inputs = {
    nixpkgs = {
      url = "github:NixOS/nixpkgs/nixos-unstable";
    };

    flake-parts = {
      url = "github:hercules-ci/flake-parts";
      inputs.nixpkgs-lib.follows = "nixpkgs";
    };

    treefmt-nix = {
      url = "github:numtide/treefmt-nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
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
    inputs@{ flake-parts, nixpkgs, ... }:
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
              programs.clang-format.enable = true;
              # Need to figure out how to run hzeller script for faster
              # checks.
              #programs.clang-tidy.enable = true;
            };
            devShells.default =
              with pkgs;
              mkShell {
                packages = [
                  git
                  bazel_9
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
                  valgrind
                  # Bazel build currently broken.
                  # Uncomment once resolved.
                  #perf_data_converter

                  # FPGA utils.
                  openfpgaloader
                ];

                CLANG_TIDY = "${clang-tools}/bin/clang-tidy";
                CLANG_FORMAT = "${clang-tools}/bin/clang-format";
              };

            # Package fpga-assembler.
            packages.default =
              let
                bazelPackage = pkgs.callPackage (
                  nixpkgs + "/pkgs/by-name/ba/bazel_9/build-support/bazelPackage.nix"
                ) { };
                registry = pkgs.fetchFromGitHub {
                  owner = "bazelbuild";
                  repo = "bazel-central-registry";
                  rev = "ab7d440f336aa430267ed208c983b13306b27486";
                  hash = "sha256-6EkkVY/rDJ/kQImv9yRenF9XEcFWp1XfPscbvJpX72w=";
                };
              in
              (bazelPackage {
                name = "fpga-as";
                version = "0.0.1";
                src = pkgs.nix-gitignore.gitignoreSourcePure [ ] ./.;
                bazel = pkgs.bazel_9;
                inherit registry;
                # Include test dependencies in the offline cache.
                targets = [ "//..." ];
                commandArgs = [
                  "-c"
                  "opt"
                  "--spawn_strategy=standalone"
                ];
                nativeBuildInputs = [ pkgs.git ];
                env.SSL_CERT_FILE = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";
                bazelRepoCacheFOD = {
                  outputHash =
                    {
                      x86_64-linux = "sha256-T+ojVs+OXKCmmjvBO5WN8gLQ7/AkHCmdRWyG6bmBsc4=";
                    }
                    .${system} or (throw "No hash for system: ${system}");
                  outputHashAlgo = "sha256";
                };
                installPhase = ''
                  runHook preInstall
                  install -D --strip bazel-bin/fpga/fpga-as "$out/bin/fpga-as"
                  runHook postInstall
                '';
              }).overrideAttrs
                {
                  pname = "fpga-as";
                  preBuild = ''
                    echo "build --jobs=$NIX_BUILD_CORES" >> .bazelrc
                  '';
                  postPatch = ''
                    patchShebangs scripts/create-workspace-status.sh
                  '';
                  doCheck = true;
                  checkPhase = ''
                    runHook preCheck

                    # Bazel's bundled test scripts need Nix interpreter paths.
                    installBase=$(${pkgs.bazel_9}/bin/bazel --batch info install_base)
                    patchShebangs "$installBase"
                    ${pkgs.bazel_9}/bin/bazel --batch test //... -c opt \
                      --registry=file://${registry} \
                      --repository_cache=repo_cache \
                      --repo_contents_cache= \
                      --spawn_strategy=standalone \
                      --test_output=errors

                    runHook postCheck
                  '';
                  meta = {
                    description = "Tool to convert FASM to FPGA bitstream.";
                    homepage = "https://github.com/lromor/fpga-assembler";
                    license = pkgs.lib.licenses.asl20;
                    platforms = pkgs.lib.platforms.linux;
                  };
                };
          };
      };
}
