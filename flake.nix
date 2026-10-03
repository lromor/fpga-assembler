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
                  bazel_8
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
                # The older buildBazelPackage helper is incompatible with Bazel 8.
                bazelDerivation = pkgs.callPackage (
                  nixpkgs + "/pkgs/by-name/ba/bazel_8/build-support/bazelDerivation.nix"
                ) { };
                registry = pkgs.fetchFromGitHub {
                  owner = "bazelbuild";
                  repo = "bazel-central-registry";
                  rev = "6873d34b26b6b294a80c7e4bd2cda1926fdfcc4d";
                  hash = "sha256-iMjT8jar5x2JYl9OJoGrjljxtK0elwMsOYa1xnNVe6M=";
                };
                common = {
                  src = pkgs.nix-gitignore.gitignoreSourcePure [ ] ./.;
                  bazel = pkgs.bazel_8;
                  inherit registry;
                  targets = [ "//..." ];
                  nativeBuildInputs = [ pkgs.git ];
                  env.SSL_CERT_FILE = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";
                  bazelPreBuild = ''
                    echo "build --jobs=$NIX_BUILD_CORES" >> .bazelrc
                  '';
                };
                repoCache = bazelDerivation (
                  common
                  // {
                    name = "fpga-as-repo-cache";
                    command = "build";
                    commandArgs = [
                      "--nobuild"
                      "--repository_cache=repo_cache"
                    ];
                    installPhase = ''
                      mkdir -p "$out"
                      cp -r repo_cache "$out/repo_cache"
                    '';
                    dontFixup = true;
                    outputHashMode = "recursive";
                    outputHashAlgo = "sha256";
                    outputHash =
                      {
                        x86_64-linux = "sha256-ysf1IEOVgxkjAokq2/IUb6IEJj0aCVzWsoRoeHnoiyE=";
                      }
                      .${system} or (throw "No hash for system: ${system}");
                  }
                );
              in
              bazelDerivation (
                common
                // {
                  pname = "fpga-as";
                  version = "0.0.1";
                  bazelRepoCache = repoCache;
                  targets = [ "//fpga:fpga-as" ];
                  command = "build";
                  commandArgs = [
                    "-c"
                    "opt"
                    "--spawn_strategy=standalone"
                  ];
                  postPatch = ''
                    patchShebangs scripts/create-workspace-status.sh
                  '';
                  doCheck = true;
                  checkPhase = ''
                    runHook preCheck

                    # Bazel's bundled test scripts need Nix interpreter paths.
                    installBase=$(${pkgs.bazel_8}/bin/bazel --batch info install_base)
                    patchShebangs "$installBase"
                    ${pkgs.bazel_8}/bin/bazel --batch test //... -c opt \
                      --registry=file://${registry} \
                      --repository_cache=repo_cache \
                      --spawn_strategy=standalone \
                      --test_output=errors

                    runHook postCheck
                  '';
                  installPhase = ''
                    runHook preInstall
                    install -D --strip bazel-bin/fpga/fpga-as "$out/bin/fpga-as"
                    runHook postInstall
                  '';

                  meta = {
                    description = "Tool to convert FASM to FPGA bitstream.";
                    homepage = "https://github.com/lromor/fpga-assembler";
                    license = pkgs.lib.licenses.asl20;
                    platforms = pkgs.lib.platforms.linux;
                  };
                }
              );
          };
      };
}
