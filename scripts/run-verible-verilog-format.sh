#!/usr/bin/env bash
set -u  # only use variables once assigned
set -e  # error out on error.

FORMAT_OUT=${TMPDIR:-/tmp}/verible-verilog-format-diff.out

VERIBLE_VERILOG_FORMAT=${VERIBLE_VERILOG_FORMAT:-verible-verilog-format}
RUNNING_IN_CI=${RUNNING_IN_CI:-0}

echo "verilble-verilog-format " `${VERIBLE_VERILOG_FORMAT} --version | head -n 1`

# Run on all files clang-format and verible-verilog-format.

find pici/ -name "*.v" -o -name "*.sv" | xargs -P2 ${VERIBLE_VERILOG_FORMAT} --inplace

# If in CI, we want to report changes, as we don't expect them
if [ "$RUNNING_IN_CI" -eq 1 ]; then
    # Check if we got any diff
    git diff > ${FORMAT_OUT}

    if [ -s ${FORMAT_OUT} ]; then
	echo "Style not matching"
	echo "Run"
	echo "  scripts/run-verible-verilog-format.sh"
	echo "and amend PR."
	echo "-------------------------------------------------"
	echo
	cat ${FORMAT_OUT}
	exit 1
    fi
fi

exit 0
