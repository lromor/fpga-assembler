load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(
    default_applicable_licenses = ["//fpga:license"],
    default_visibility = ["//:__subpackages__"],
)

cc_library(
    name = "fabric-view",
    srcs = [
        "fabric-view.cc",
    ],
    hdrs = [
        "fabric-view.h",
    ],
    deps = [
        "//fpga:database",
        "//fpga:database-parsers",
        "//fpga:fasm-parser",
        "//fpga:memory-mapped-file",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)

filegroup(
    name = "testdata",
    srcs = glob(["testdata/**"]),
)

cc_test(
    name = "fabric-view_test",
    srcs = [
        "fabric-view_test.cc",
    ],
    data = glob(["testdata/**"]),
    deps = [
        ":fabric-view",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
