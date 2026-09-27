load("@rules_rust//rust:defs.bzl", "rust_shared_library")
load("@rules_rust_wasm_bindgen//:defs.bzl", "rust_wasm_bindgen")

package(
    default_applicable_licenses = ["//fpga:license"],
    default_visibility = ["//:__subpackages__"],
)

# Only ever built for wasm: rust_wasm_bindgen transitions to it, and this
# marker keeps a plain `bazel build //...` on a host platform from trying to
# link a browser renderer against the host toolchain.
rust_shared_library(
    name = "fabric_view",
    srcs = [
        "lib.rs",
        "model.rs",
        "palette.rs",
        "render.rs",
        "scene.rs",
    ],
    compile_data = ["shader.wgsl"],
    crate_name = "fabric_view",
    target_compatible_with = ["@platforms//cpu:wasm32"],
    deps = [
        "@viewer_crates//:bytemuck",
        "@viewer_crates//:console_error_panic_hook",
        "@viewer_crates//:console_log",
        "@viewer_crates//:log",
        "@viewer_crates//:serde",
        "@viewer_crates//:serde_json",
        "@viewer_crates//:wasm-bindgen",
        "@viewer_crates//:wasm-bindgen-futures",
        "@viewer_crates//:web-sys",
        "@viewer_crates//:wgpu",
    ],
)

# "web" emits an ES module the page loads directly, with no bundler step.
rust_wasm_bindgen(
    name = "bundle",
    target = "web",
    wasm_file = ":fabric_view",
)
