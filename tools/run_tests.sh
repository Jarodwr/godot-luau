#!/bin/sh
# Runs every headless test suite against the built addon, as CI does.
# Exits non-zero if any suite fails.
#
#     GODOT_BIN=/path/to/godot tools/run_tests.sh
set -u
G="${GODOT_BIN:-godot}"
cd "$(dirname "$0")/.."
logs="${TEST_LOGS:-$PWD/test-logs}"  # each suite's full output
mkdir -p "$logs"
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
	printf '%s\n' "$out" > "$logs/$name.log"
	if [ $code -eq 0 ] && ! printf '%s\n' "$out" | grep -q '^FAIL' && { [ -z "$expected" ] || printf '%s\n' "$out" | grep -qF -- "$expected"; }; then
		echo "ok   $name"
	else
		echo "FAIL $name (exit $code); the end of its output ($logs/$name.log):"
		printf '%s\n' "$out" | tail -40 | sed 's/^/    /'
		failed="$failed $name"
	fi
}

# The editor's import registers global classes (class_name) for the suites.
# A fresh project loads the extension only when the scan finds it, and Godot
# 4.7.2 crashes quitting after such a late load (any extension: seen with
# fennel-gdextension too), so the extension is listed beforehand, as it is
# after a project's first editor session.
mkdir -p demo/.godot
[ -f demo/.godot/extension_list.cfg ] || printf 'res://addons/godot_luau/godot_luau.gdextension\n' > demo/.godot/extension_list.cfg
if ! (cd demo && limit 300 "$G" --headless --import --path . > "$logs/import.log" 2>&1); then
	echo "FAIL import; the end of its output ($logs/import.log):"
	tail -40 "$logs/import.log" | sed 's/^/    /'
	failed="$failed import"
fi

suite checks "failures: 0" --script checks.gd
suite smoke "check = 76" --script smoke.gd
suite fennel "fennel mover position" --script fennel_smoke.gd
suite global_class "failures: 0" --script global_class.gd
suite hot_reload "failures: 0" --script hot_reload.gd

for script in check_errors check_editor; do
	if GODOT_BIN="$G" limit 600 "tools/$script.sh" >"$logs/$script.log" 2>&1; then
		echo "ok   $script"
	else
		echo "FAIL $script; the end of its output ($logs/$script.log):"
		tail -40 "$logs/$script.log" | sed 's/^/    /'
		failed="$failed $script"
	fi
done

if [ -n "$failed" ]; then
	echo "failed:$failed"
	exit 1
fi
echo "all suites passed"
