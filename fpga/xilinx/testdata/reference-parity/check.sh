#!/usr/bin/env bash
# Check fpga-as against the reference chain's configuration frames.
#
# Each case directory holds an input.fasm and, where the reference chain
# (fasm2frames from openXC7/prjxray) assembles it, the gold.frames it produced.
# This runs fpga-as on the same input with the same database and compares the
# configuration bits.
#
# The database is not committed (it is hundreds of MB), so set PRJXRAY_DB_DIR
# to the annotated prjxray-db -- the toolchain-nix devshell exports it.  Without
# it the check reports skipped and passes, so it is safe to call anywhere.
#
# Usage: [PRJXRAY_DB_DIR=/path/to/prjxray-db] [FPGA_AS=/path/to/fpga-as] check.sh
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
FPGA_AS=${FPGA_AS:-fpga-as}
DB=${PRJXRAY_DB_DIR:-}

if [ -z "$DB" ]; then
    echo "skipped: export PRJXRAY_DB_DIR to the annotated prjxray-db to run this"
    exit 0
fi
if [ ! -x "$FPGA_AS" ] && ! command -v "$FPGA_AS" > /dev/null; then
    echo "skipped: fpga-as not found (set FPGA_AS)"
    exit 0
fi

tmpdir=$(mktemp -d) || exit 1
trap 'rm -rf "$tmpdir"' EXIT

pass=0
fail=0
while read -r name part family expect; do
    case "$name" in '' | '#'*) continue ;; esac
    input="$here/$name/input.fasm"
    if [ ! -f "$input" ]; then
        echo "FAIL $name: no input.fasm"
        fail=$((fail + 1))
        continue
    fi
    out="$tmpdir/$name"
    if ! "$FPGA_AS" --part="$part" --prjxray_db_path="$DB/$family" \
        --dump_frames_file="$out.frames" "$input" > "$out.bit" 2> "$out.err"; then
        if [ "${expect%%:*}" = error ] && grep -q "${expect#error:}" "$out.err"; then
            printf 'ok   %-32s expected failure: %s\n' "$name" "$(head -1 "$out.err")"
            pass=$((pass + 1))
            continue
        fi
        printf 'FAIL %-32s unexpected exit, expected "%s":\n' "$name" "$expect"
        sed 's/^/       /' "$out.err" | head -5
        fail=$((fail + 1))
        continue
    fi
    if [ "${expect%%:*}" = error ]; then
        printf 'FAIL %-32s expected an error, but a bitstream was written\n' "$name"
        fail=$((fail + 1))
        continue
    fi
    : >> "$out.frames"
    if [ "$expect" = no-frames ]; then
        if [ -s "$out.frames" ]; then
            printf 'FAIL %-32s expected no configuration bits\n' "$name"
            fail=$((fail + 1))
            continue
        fi
        printf 'ok   %-32s no bits, as in the reference\n' "$name"
        pass=$((pass + 1))
        continue
    fi
    # Compare like the gist's comparer does: a frame is only interesting if it
    # sets a bit.  fpga-as dumps frames whose words all end up zero (they were
    # touched during assembly), the reference's dump is dense, the gold is
    # neither -- so drop all-zero frames on both sides.
    filter() {
        awk '{ n = split($2, w, ","); for (i = 1; i <= n; i++)
                   if (w[i] != "0x00000000") { print; break } }' "$1" |
            tr 'A-F' 'a-f' | sort
    }
    filter "$here/$name/gold.frames" > "$out.gold"
    filter "$out.frames" > "$out.got"
    if cmp -s "$out.gold" "$out.got"; then
        printf 'ok   %-32s %s frames match the reference\n' "$name" "$(grep -c . "$out.gold")"
        pass=$((pass + 1))
    else
        printf 'FAIL %-32s frames differ from the reference:\n' "$name"
        diff "$out.gold" "$out.got" | head -6 | sed 's/^/       /'
        fail=$((fail + 1))
    fi
done < "$here/cases.tsv"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
