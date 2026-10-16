#!/bin/sh
# Checks the addon's fennel-ls settings (docs/adr/0061) with `fennel-ls check`
# (fennel-ls-rs 0.2.2 or later): every defnode script in demo/ checks clean, and
# each mistake in demo/types/mistakes.fnl is reported.
#
#     FENNEL_LS=/path/to/fennel-ls tools/check_lsp.sh
set -u
cd "$(dirname "$0")/../demo"
fls="${FENNEL_LS:-fennel-ls}"
failures=0
expect() {  # expect OUTPUT TEXT
	if printf '%s\n' "$1" | grep -qF -- "$2"; then
		echo "ok   $2"
	else
		echo "FAIL $2"
		failures=$((failures + 1))
	fi
}

# Scripts Godot loads (types/ is ignored by Godot, errors/ are broken on purpose)
clean=$(find . -name '*.fnl' -not -path './errors/*' -not -path './types/*' -not -path './.godot/*' -not -path './addons/*' | sort)
out=$("$fls" check $clean 2>&1)
if [ -z "$out" ]; then
	echo "ok   $(printf '%s\n' "$clean" | wc -l | tr -d ' ') demo scripts have no warnings"
else
	echo "FAIL demo scripts:"
	printf '%s\n' "$out"
	failures=$((failures + 1))
fi

out=$("$fls" check types/mistakes.fnl 2>&1)
expect "$out" "mistakes.fnl:4:8: warning: \`unused-local\` is defined but never used"
expect "$out" "mistakes.fnl:8:18: warning: unknown identifier \`SPEDE\`"
expect "$out" "mistakes.fnl:10:20: warning: unknown identifier \`ARMR\`"
expect "$out" "mistakes.fnl:11:23: warning: unknown identifier \`Bsae\`"
expect "$out" "mistakes.fnl:13:24: warning: parameter \`spare\` is unused"
expect "$out" "mistakes.fnl:15:22: warning: unknown identifier \`helth\`"
expect "$out" "mistakes.fnl:15:29: warning: unknown identifier \`by\`"
count=$(printf '%s\n' "$out" | grep -c 'warning:')
expect "warnings: $count" "warnings: 7"

echo "failures: $failures"
exit $failures
