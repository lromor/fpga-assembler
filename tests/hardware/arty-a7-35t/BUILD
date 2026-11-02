load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "hardware_test",
    testonly = True,
    srcs = ["hardware_test.cc"],
    applicable_licenses = ["//tests/hardware:license"],
    tags = ["manual"],
    target_compatible_with = ["@platforms//os:linux"],
    deps = ["//tests/hardware:gpio"],
)
