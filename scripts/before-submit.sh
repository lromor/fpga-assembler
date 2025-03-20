#!/usr/bin/env bash
#
# Good to run before each submit to catch or repair issues.

cd $(dirname $0)/..

set -e

. <(scripts/run-build-cleaner.sh ...)  # auto-repair issues
scripts/run-clang-format.sh
bazel build -c opt ...
bazel test -c opt ...
scripts/make-compilation-db.sh
sh scripts/run-clang-tidy-cached.cc
