#!/bin/sh
# Runs every headless test suite against the built addon, as CI does.
# Exits non-zero if any suite fails.
#
#     GODOT_BIN=/path/to/godot tools/run_tests.sh
set -u
G="${GODOT_BIN:-godot}"
cd "$(dirname "$0")/.."
failed=""

limit() {  # limit SECONDS COMMAND...: stop a hung suite
	if timeout --version >/dev/null 2>&1; then  # GNU's (Windows has its own timeout)
		timeout "$@"
	else
		perl -e 'alarm shift; exec @ARGV' "$@"
	fi
}

# suite NAME EXPECTED-TEXT ARGS...: runs Godot in demo/ with ARGS; passes when
# it exits 0, prints no "FAIL", and prints EXPECTED-TEXT (if given)
suite() {
	name=$1
	expected=$2
	shift 2
	out=$(cd demo && limit 300 "$G" --headless --path . "$@" 2>&1)
	code=$?
	if [ $code -eq 0 ] && ! printf '%s\n' "$out" | grep -q '^FAIL' && { [ -z "$expected" ] || printf '%s\n' "$out" | grep -qF -- "$expected"; }; then
		echo "ok   $name"
	else
		echo "FAIL $name (exit $code)"
		printf '%s\n' "$out" | grep -E '^FAIL|ERROR|crash' | head -20
		failed="$failed $name"
	fi
}

# The editor's scan registers global classes (class_name) for the suites
(cd demo && limit 300 "$G" --headless --editor --quit --path . >/dev/null 2>&1)

suite checks "failures: 0" --script checks.gd
suite smoke "check = 76" --script smoke.gd
suite fennel "fennel mover position" --script fennel_smoke.gd
suite global_class "failures: 0" --script global_class.gd
suite hot_reload "failures: 0" --script hot_reload.gd

for script in check_errors check_editor; do
	if GODOT_BIN="$G" limit 600 "tools/$script.sh" >/tmp/godot_luau_$script.log 2>&1; then
		echo "ok   $script"
	else
		echo "FAIL $script"
		grep '^FAIL' /tmp/godot_luau_$script.log | head -20
		failed="$failed $script"
	fi
done

if [ -n "$failed" ]; then
	echo "failed:$failed"
	exit 1
fi
echo "all suites passed"
