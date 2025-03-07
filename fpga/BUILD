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
    name = "database-parsers",
    srcs = [
        "database-parsers.cc",
    ],
    hdrs = [
        "database-parsers.h",
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
    name = "database-parsers_test",
    srcs = [
        "database-parsers_test.cc",
    ],
    data = [
        "//fpga:testdata",
    ],
    deps = [
        ":database-parsers",
        ":memory-mapped-file",
        "@abseil-cpp//absl/strings:string_view",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
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
        ":database-parsers",
        ":memory-mapped-file",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/strings:strings",
        "@rapidjson",
    ],
)

cc_test(
    name = "database_test",
    srcs = [
        "database_test.cc",
    ],
    deps = [
        ":database",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "assembler",
    srcs = [
        "assembler.cc",
    ],
    deps = [
        ":database",
        ":database-parsers",
        ":fasm-parser",
        ":memory-mapped-file",
        "@abseil-cpp//absl/cleanup:cleanup",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:strings",
    ],
)
