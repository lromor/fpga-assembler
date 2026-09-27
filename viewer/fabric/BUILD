load("@rules_rust//rust:defs.bzl", "rust_library", "rust_test")

package(
    default_applicable_licenses = ["//fpga:license"],
    default_visibility = ["//:__subpackages__"],
)

rust_library(
    name = "fabric",
    srcs = [
        "lib.rs",
        "sys.rs",
    ],
    crate_name = "fabric",
    # The C ABI is linked in, not a Rust dependency.
    link_deps = ["//fpga/ffi:fabric-view"],
    deps = ["@viewer_crates//:serde"],
)

rust_test(
    name = "fabric_test",
    crate = ":fabric",
    data = ["//fpga/ffi:testdata"],
)
