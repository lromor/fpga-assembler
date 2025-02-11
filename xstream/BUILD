load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "fasm-parser",
    hdrs = [
        "fasm-parser.h",
    ],
)

cc_test(
    name = "fasm-parser_test",
    srcs = [
        "fasm-parser_test.cc",
    ],
    deps = [
        ":fasm-parser",
    ],
)

cc_library(
    name = "memory-mapped-file",
    srcs = [
        "memory-mapped-file.cc",
        "memory-mapped-file.h",
    ],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "database",
    srcs = [
        "database.cc",
    ],
    hdrs = [
        "database.h",
    ],
    deps = [
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/strings:strings",
        "@rapidjson",
    ],
)

filegroup(
    name = "testdata",
    srcs = glob(["testdata/*"]),
)

cc_test(
    name = "database_test",
    srcs = [
        "database_test.cc",
    ],
    data = [
        "//xstream:testdata",
    ],
    deps = [
        ":database",
        ":memory-mapped-file",
        "@abseil-cpp//absl/strings:string_view",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "banks-tiles-registry",
    srcs = [
        "banks-tiles-registry.cc",
    ],
    hdrs = [
        "banks-tiles-registry.h",
    ],
    deps = [
        ":database",
        ":memory-mapped-file",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/strings:strings",
    ],
)

cc_test(
    name = "banks-tiles-registry_test",
    srcs = [
        "banks-tiles-registry_test.cc",
    ],
    deps = [
        ":banks-tiles-registry",
        ":database",
        "@abseil-cpp//absl/strings:string_view",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "xstream",
    srcs = [
        "xstream.cc",
    ],
    deps = [
        ":fasm-parser",
        ":database",
        ":memory-mapped-file",
        ":banks-tiles-registry",
        "@abseil-cpp//absl/strings:strings",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/flags:parse",
    ],
)
