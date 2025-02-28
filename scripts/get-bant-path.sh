#!/usr/bin/env bash

# Print path to a bant binary. Can be provided by an environment variable
# or built from our dependency.

BAZEL=${BAZEL:-bazel}
BANT=${BANT:-needs-to-be-compiled-locally}

# Bant not given, compile from bzlmod dep.
if [ "${BANT}" = "needs-to-be-compiled-locally" ]; then
  "${BAZEL}" build --noshow_progress -c opt @bant//bant:bant 2>/dev/null
  BANT=$(realpath bazel-bin/external/bant*/bant/bant | head -1)
fi

echo $BANT
