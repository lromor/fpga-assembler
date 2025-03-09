load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_license//rules:license.bzl", "license")

package(
    default_applicable_licenses = [":license"],
    default_visibility = ["//:__subpackages__"],
)

license(
    name = "license",
    package_name = "xilinx",
    license_kind = "@rules_license//licenses/spdx:ISC",
)

cc_library(
    name = "big-endian-span",
    hdrs = [
        "big-endian-span.h",
    ],
    deps = [
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "big-endian-span_test",
    srcs = [
        "big-endian-span_test.cc",
    ],
    deps = [
        ":big-endian-span",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "bit-ops",
    hdrs = [
        "bit-ops.h",
    ],
)

cc_test(
    name = "bit-ops_test",
    srcs = [
        "bit-ops_test.cc",
    ],
    deps = [
        ":bit-ops",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "configuration-packet",
    hdrs = [
        "configuration-packet.h",
    ],
    deps = [
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "arch-xc7-defs",
    srcs = [
        "arch-xc7-defs.cc",
    ],
    hdrs = [
        "arch-xc7-defs.h",
    ],
)

cc_library(
    name = "arch-xc7-configuration-packet",
    srcs = [
        "arch-xc7-configuration-packet.cc",
    ],
    hdrs = [
        "arch-xc7-configuration-packet.h",
    ],
    deps = [
        ":arch-xc7-defs",
        ":bit-ops",
        ":configuration-packet",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "arch-xc7-configuration-packet_test",
    srcs = [
        "arch-xc7-configuration-packet_test.cc",
    ],
    deps = [
        ":arch-xc7-configuration-packet",
        ":arch-xc7-defs",
        ":bit-ops",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "arch-xc7-frame-address",
    srcs = [
        "arch-xc7-frame-address.cc",
    ],
    hdrs = [
        "arch-xc7-frame-address.h",
    ],
    deps = [
        ":arch-xc7-defs",
        ":bit-ops",
    ],
)

cc_library(
    name = "arch-xc7-part",
    srcs = [
        "arch-xc7-part.cc",
    ],
    hdrs = [
        "arch-xc7-part.h",
    ],
    deps = [
        ":arch-xc7-defs",
        ":arch-xc7-frame-address",
    ],
)

cc_test(
    name = "arch-xc7-part_test",
    srcs = [
        "arch-xc7-part_test.cc",
    ],
    deps = [
        ":arch-xc7-defs",
        ":arch-xc7-frame-address",
        ":arch-xc7-part",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
