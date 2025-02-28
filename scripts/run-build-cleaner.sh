#!/usr/bin/env bash

set -u

readonly BANT_EXIT_ON_DWYU_ISSUES=3

# Which bazel and bant to use can be chosen by environment variables
BAZEL=${BAZEL:-bazel}
BANT=$($(dirname $0)/get-bant-path.sh)

# Run depend-on-what-you-use build-cleaner.
# Print buildifier commands to fix if needed.
"${BANT}" dwyu "$@"

BANT_EXIT=$?
if [ ${BANT_EXIT} -eq ${BANT_EXIT_ON_DWYU_ISSUES} ]; then
  cat >&2 <<EOF

Build dependency issues found, the following one-liner will fix it. Amend PR.

source <(scripts/run-build-cleaner.sh $@)
EOF
fi

exit $BANT_EXIT
