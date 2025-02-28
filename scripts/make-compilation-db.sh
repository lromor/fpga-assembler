#!/usr/bin/env bash

set -u
set -e

# Which bazel and bant to use. If unset, defaults are used.
BAZEL=${BAZEL:-bazel}
BANT=$($(dirname $0)/get-bant-path.sh)

# Important to run with --remote_download_outputs=all to make sure generated
# files are actually visible locally in case a remote cache (that includes
# --disk_cache) is used ( https://github.com/hzeller/bazel-gen-file-issue )
BAZEL_OPTS="-c opt --remote_download_outputs=all"

# Tickle some build targets to fetch all dependencies and generate files,
# so that they can be seen by the users of the compilation db.
"${BAZEL}" build ${BAZEL_OPTS} @googletest//:has_absl @rapidjson

# Create compilation DB. Command 'compilation-db' creates a huge *.json file,
# but compile_flags.txt is perfectly sufficient and easier for tools to use.
"${BANT}" compile-flags > compile_flags.txt

# If there are two styles of comp-dbs, tools might have issues. Warn user.
if [ -r compile_commands.json ]; then
  echo -e "\n\033[1;31mSuggest to remove old compile_commands.json to not interfere with compile_flags.txt\033[0m\n"
fi
