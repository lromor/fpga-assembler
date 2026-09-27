load("@rules_rust//rust:defs.bzl", "rust_binary")

package(
    default_applicable_licenses = ["//fpga:license"],
    default_visibility = ["//:__subpackages__"],
)

rust_binary(
    name = "server",
    srcs = [
        "api.rs",
        "main.rs",
        "state.rs",
    ],
    crate_name = "fpga_fabric_viewer",
    data = [
        "//viewer/static",
        "//viewer/web:bundle",
    ],
    deps = [
        "//viewer/fabric",
        "@viewer_crates//:axum",
        "@viewer_crates//:clap",
        "@viewer_crates//:serde",
        "@viewer_crates//:tokio",
        "@viewer_crates//:tower-http",
        "@viewer_crates//:tracing",
        "@viewer_crates//:tracing-subscriber",
    ],
)
