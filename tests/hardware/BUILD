load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_license//rules:license.bzl", "license")

license(
    name = "license",
    license_kinds = ["@rules_license//licenses/spdx:GPL-2.0-only"],
    license_text = "//:LICENSES/GPL-2.0-only.txt",
    visibility = ["//tests/hardware:__subpackages__"],
)

cc_library(
    name = "gpio",
    testonly = True,
    hdrs = ["gpio.h"],
    applicable_licenses = [":license"],
    tags = ["manual"],
    target_compatible_with = ["@platforms//os:linux"],
    visibility = ["//tests/hardware:__subpackages__"],
)
