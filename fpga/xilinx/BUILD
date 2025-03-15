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
    name = "arch-xc7-frame",
    srcs = [
        "arch-xc7-frame.cc",
    ],
    hdrs = [
        "arch-xc7-frame.h",
    ],
    deps = [
        ":bit-ops",
    ],
)

cc_test(
    name = "arch-xc7-frame_test",
    srcs = [
        "arch-xc7-frame_test.cc",
    ],
    deps = [
        ":arch-xc7-frame",
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
    name = "arch-xc7-configuration-packet",
    srcs = [
        "arch-xc7-configuration-packet.cc",
    ],
    hdrs = [
        "arch-xc7-configuration-packet.h",
    ],
    deps = [
        ":arch-xc7-frame",
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
        ":arch-xc7-frame",
        ":bit-ops",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
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
        ":arch-xc7-frame",
    ],
)

cc_test(
    name = "arch-xc7-part_test",
    srcs = [
        "arch-xc7-part_test.cc",
    ],
    deps = [
        ":arch-xc7-frame",
        ":arch-xc7-part",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "arch-types",
    hdrs = [
        "arch-types.h",
    ],
    deps = [
        ":arch-xc7-configuration-packet",
        ":arch-xc7-frame",
        ":arch-xc7-part",
    ],
)

cc_library(
    name = "frames",
    hdrs = [
        "frames.h",
    ],
    deps = [
        ":arch-types",
    ],
)

cc_test(
    name = "frames-xc7_test",
    srcs = [
        "frames-xc7_test.cc",
    ],
    deps = [
        ":arch-types",
        ":arch-xc7-frame",
        ":frames",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "configuration",
    srcs = [
        "configuration.cc",
    ],
    hdrs = [
        "configuration.h",
    ],
    deps = [
        ":arch-types",
        ":arch-xc7-configuration-packet",
        ":bit-ops",
        ":configuration-packet",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "configuration-xc7_test",
    srcs = [
        "configuration-xc7_test.cc",
    ],
    deps = [
        ":arch-types",
        ":arch-xc7-frame",
        ":configuration",
        ":configuration-packet",
        ":frames",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
