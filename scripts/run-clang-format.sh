#!/usr/bin/env bash
set -u  # only use variables once assigned
set -e  # error out on error.

FORMAT_OUT=${TMPDIR:-/tmp}/clang-format-diff.out

CLANG_FORMAT=${CLANG_FORMAT:-clang-format}
BUILDIFIER=${BUILDIFIER:-buildifier}

${CLANG_FORMAT} --version

# Run on all files.

find fpga/ -name "*.h" -o -name "*.cc" | xargs -P2 ${CLANG_FORMAT} -i

# If we have buildifier installed, use that on BUILD files
if command -v ${BUILDIFIER} >/dev/null; then
  echo "Run $(buildifier --version)"
  ${BUILDIFIER} -lint=fix MODULE.bazel $(find . -name BUILD -o -name "*.bzl")
fi

# Check if we got any diff
git diff > ${FORMAT_OUT}

if [ -s ${FORMAT_OUT} ]; then
   echo "Style not matching"
   echo "Run"
   echo "  .github/bin/run-clang-format.sh"
   echo "-------------------------------------------------"
   echo
   cat ${FORMAT_OUT}
   exit 1
fi

exit 0
