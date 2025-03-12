#!/usr/bin/env bash
set -u  # only use variables once assigned
set -e  # error out on error.

FORMAT_OUT=${TMPDIR:-/tmp}/clang-format-diff.out

CLANG_FORMAT=${CLANG_FORMAT:-clang-format}
BUILDIFIER=${BUILDIFIER:-buildifier}
RUNNING_IN_CI=${RUNNING_IN_CI:-0}

${CLANG_FORMAT} --version

# Run on all files.

find fpga/ -name "*.h" -o -name "*.cc" | xargs -P2 ${CLANG_FORMAT} -i

# If we have buildifier installed, use that on BUILD files
if command -v ${BUILDIFIER} >/dev/null; then
  echo "Run $(buildifier --version)"
  ${BUILDIFIER} -lint=fix MODULE.bazel $(find . -name BUILD -o -name "*.bzl")
fi

# If in CI, we want to report changes, as we don't expect them
if [ "$RUNNING_IN_CI" -eq 1 ]; then
    # Check if we got any diff
    git diff > ${FORMAT_OUT}

    if [ -s ${FORMAT_OUT} ]; then
	echo "Style not matching"
	echo "Run"
	echo "  scripts/run-clang-format.sh"
	echo "and amend PR."
	echo "-------------------------------------------------"
	echo
	cat ${FORMAT_OUT}
	exit 1
    fi
fi

exit 0
