#!/usr/bin/env bash
#
# Unity test-registration guard (grand review 2026-09-03, finding correctness-4).
#
# Unity has no auto-discovery: a test only runs if some RUN_TEST(name) line names
# it. Writing the function and forgetting the registration is silent -- the suite
# stays green and the count still goes UP, because the other tests in the file
# are new too. That is how test_armgate_neutral_while_switch_off_does_not_prearm
# (test/test_channels/test_main.cpp) sat unregistered: 12 ArmGate test functions,
# 11 RUN_TEST lines, 330 green.
#
# This script compares, per suite file, the set of `void test_*()` DEFINITIONS
# against the set of RUN_TEST() REGISTRATIONS and fails on any difference in
# either direction. It only reads sources.
#
# What it looks at, and why (the 2026-09-03 review demonstrated all four holes
# this now closes):
#   * Comments and `#if 0` blocks are BLANKED before either grep. A
#     `// RUN_TEST(test_b);` or a RUN_TEST inside `#if 0` used to read as a live
#     registration -- i.e. a deliberately disabled test looked registered, which
#     is the dangerous direction: the case never runs and nothing says so.
#   * A definition is `void test_x(` at the start of a line, optionally indented
#     and optionally `static` -- `static void test_x()` and an indented
#     `  void test_x()` were both invisible before, so an unregistered test could
#     hide behind either.
#   * A line ending in `;` is a forward declaration, not a definition, and is
#     dropped -- the old regex counted `void test_a();` as a second definition of
#     test_a and reported a phantom orphan.
# Known and deliberate limits: a `//` inside a string literal blanks the rest of
# that line (no test source here has one), and a registration made through a
# wrapper macro (RUN_GROUP(test_b) expanding to RUN_TEST) reads as unregistered.
# That last one fails LOUD (exit 1), which is the safe direction; this repo
# registers every test with a literal RUN_TEST line.
# Two more (2026-09-03 re-verify, R5), both silent rather than loud: a RUN_TEST
# inside an inactive `#ifdef`/`#ifndef` still counts as a live registration --
# only a bare `#if 0` is stripped as dead code above -- so a build where that
# guard macro is undefined actually runs one FEWER test than this script
# reports; and a definition split across lines (`void` on one line, `test_x()`
# on the next) matches neither the definition grep nor the "forward
# declaration" exclusion, so a test written that way is invisible rather than
# flagged as a mismatch. Neither is closed here. The backstop for both is the
# CI step that compares this script's `--print-total` against `pio test`'s own
# reported case count: a real divergence between the two fails loudly even
# when this script's own per-file diff stays silent (the split-line case only
# trips that backstop if the invisible definition is ALSO registered
# elsewhere -- a split-line test that is both unregistered and undetected
# stays a genuine blind spot).
#
# Usage: tools/test_registration_check.sh [-q|--quiet] [--print-total] [PATH ...]
#        PATH defaults to every test/test_*/test_main.cpp in the repo.
#
#   --print-total  Print ONLY the number of registered tests on stdout (implies
#                  quiet). CI compares it with the case count `pio test`
#                  actually reports, so the two numbers can never drift apart
#                  silently -- today they agree, and that agreement is the
#                  strongest evidence there is no live blind spot left.
#
# Exit codes:
#   0  every defined test is registered exactly once, and every registration
#      names a defined test
#   1  MISMATCH (unregistered test, duplicate registration, or a registration
#      with no definition)
#   2  CANNOT CHECK (no suite files found where they were expected)
#   3  usage error

set -u

QUIET=0
PRINT_TOTAL=0
FILES=""

while [ $# -gt 0 ]; do
    case "$1" in
        -q|--quiet) QUIET=1; shift ;;
        --print-total) PRINT_TOTAL=1; QUIET=1; shift ;;
        -h|--help) sed -n '3,64p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "error: unknown argument '$1' (try --help)" >&2; exit 3 ;;
        *) FILES="$FILES $1"; shift ;;
    esac
done

say() { [ "$QUIET" -eq 1 ] || printf '%s\n' "$*"; }

# strip_dead_code FILE -- prints the file with every // comment, /* */ comment
# and `#if 0 ... #endif` block replaced by blank space, so the greps below see
# only code that actually compiles. Line count is preserved (blank lines are
# emitted for dropped lines), which keeps any future line-numbered message
# honest. An `#else`/`#elif` at the top level of an `#if 0` ends the dead
# region: that branch IS compiled.
strip_dead_code() {
    awk '
    BEGIN { inblock = 0; dead = 0; nest = 0 }
    {
        line = $0
        out = ""
        i = 1
        n = length(line)
        while (i <= n) {
            rest = substr(line, i)
            if (inblock) {
                p = index(rest, "*/")
                if (p == 0) { i = n + 1 } else { inblock = 0; i = i + p + 1 }
            } else {
                p = index(rest, "/*")
                q = index(rest, "//")
                if (q > 0 && (p == 0 || q < p)) { out = out substr(rest, 1, q - 1); i = n + 1 }
                else if (p > 0) { out = out substr(rest, 1, p - 1); inblock = 1; i = i + p + 1 }
                else { out = out rest; i = n + 1 }
            }
        }
        line = out
        if (dead) {
            if (line ~ /^[ \t]*#[ \t]*(if|ifdef|ifndef)/) { nest++; print ""; next }
            if (line ~ /^[ \t]*#[ \t]*endif/) { if (nest > 0) nest--; else dead = 0; print ""; next }
            if (nest == 0 && line ~ /^[ \t]*#[ \t]*(else|elif)/) { dead = 0; print ""; next }
            print ""; next
        }
        if (line ~ /^[ \t]*#[ \t]*if[ \t]+0[ \t]*$/) { dead = 1; nest = 0; print ""; next }
        print line
    }' "$1"
}

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(dirname -- "$SCRIPT_DIR")"

if [ -z "$FILES" ]; then
    FILES="$(find "$REPO_ROOT/test" -name 'test_main.cpp' 2>/dev/null | sort)"
fi
if [ -z "$FILES" ]; then
    echo "test-registration-check: CANNOT CHECK -- no test/**/test_main.cpp found" >&2
    exit 2
fi

fail=0
checked=0
total_tests=0

for f in $FILES; do
    if [ ! -f "$f" ]; then
        echo "test-registration-check: CANNOT CHECK -- no such file: $f" >&2
        exit 2
    fi
    checked=$((checked + 1))

    # Both greps run over the file with comments and `#if 0` blocks blanked, so
    # a disabled RUN_TEST cannot read as a live one (see the header).
    code="$(strip_dead_code "$f")"

    # Definitions: `void test_name(` starting a line, optionally indented and
    # optionally `static`. A line ending in `;` is a declaration, not a
    # definition, and is dropped.
    defs="$(printf '%s\n' "$code" \
            | grep -E '^[[:space:]]*(static[[:space:]]+)?void[[:space:]]+test_[A-Za-z0-9_]+[[:space:]]*\(' \
            | grep -vE ';[[:space:]]*$' \
            | sed -E 's/^[[:space:]]*(static[[:space:]]+)?void[[:space:]]+(test_[A-Za-z0-9_]+).*/\2/' \
            | sort)"
    regs="$(printf '%s\n' "$code" \
            | grep -oE 'RUN_TEST[[:space:]]*\([[:space:]]*test_[A-Za-z0-9_]+' \
            | sed -E 's/.*\([[:space:]]*//' | sort)"

    ndefs="$(printf '%s' "$defs" | grep -c . || true)"
    nregs="$(printf '%s' "$regs" | grep -c . || true)"
    total_tests=$((total_tests + nregs))

    missing="$(comm -23 <(printf '%s\n' "$defs") <(printf '%s\n' "$regs" | sort -u))"
    unknown="$(comm -13 <(printf '%s\n' "$defs") <(printf '%s\n' "$regs" | sort -u))"
    dupes="$(printf '%s\n' "$regs" | uniq -d)"

    if [ -n "$missing" ] || [ -n "$unknown" ] || [ -n "$dupes" ]; then
        fail=1
        echo "test-registration-check: MISMATCH in $f ($ndefs defined, $nregs registered)" >&2
        for t in $missing; do
            echo "  DEFINED BUT NEVER RUN   $t" >&2
        done
        for t in $unknown; do
            echo "  REGISTERED, NOT DEFINED $t" >&2
        done
        for t in $dupes; do
            echo "  REGISTERED TWICE        $t" >&2
        done
    else
        say "test-registration-check: OK   $ndefs/$nregs  $f"
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "" >&2
    echo "Unity runs only what RUN_TEST() names. A defined-but-unregistered test is" >&2
    echo "invisible: the suite stays green and the case never executes. Add the" >&2
    echo "RUN_TEST line (or delete the function deliberately)." >&2
    exit 1
fi

if [ "$PRINT_TOTAL" -eq 1 ]; then
    printf '%s\n' "$total_tests"
    exit 0
fi

say "test-registration-check: OK -- $checked suites, $total_tests registered tests, no orphans"
exit 0
