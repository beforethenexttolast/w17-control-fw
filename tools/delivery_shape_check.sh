#!/usr/bin/env bash
#
# Delivery-image shape guard (grand review 2026-09-03, finding safety-3).
#
# The repo asserts in FIVE places that the DELIVERED image (env:esp32dev) links
# no bench console, no BT show-off code and no simulation feeder, and calls that
# "ELF-verified":
#   * platformio.ini, [env:esp32dev]'s lib_ignore note ("the pure console libs
#     (compiled but never linked, ELF-verified)");
#   * platformio.ini, [env:esp32dev_btshowoff]'s header ("links zero btpad/BT
#     code (ELF-verified alongside the console invariant)");
#   * CLAUDE.md and AGENTS.md, section "Delivery vs tuning builds (stable
#     invariant)" ("Delivery ... links no console parser (ELF-verified)");
#   * docs/ROADMAP.md, D8's "CF-1 remediation" paragraph ("ELF-verified: 0
#     console symbols/strings in esp32dev").
# (Cited by section, not by line: these files move, and a stale line number in
# the guard that exists to make claims true would be its own small joke.)
#
# Until this script existed nothing automated enforced any of it: the
# verification was a one-off human `nm` spot-check recorded in
# docs/D8_BENCH_BRINGUP.md Phase 11a (step 7, "Flash plain delivery firmware"),
# i.e. a point-in-time claim, not a regression guard. This script is that guard,
# in the same spirit as tools/link2_copy_check.sh: it only READS build output,
# never writes anything.
#
# Scope -- what this does and does not prove:
#   DOES     prove that the named quarantined code is absent from a BUILT
#            delivery ELF: no console/btpad/Bluepad32/sim symbol survived to the
#            link, and no sim/console banner string is in the image.
#   DOES NOT prove the firmware is otherwise correct, nor that the ELF it scanned
#            is the one that gets flashed. Pair it with the D8 Phase 11a step-7
#            ritual ("re-flash the delivery image and attach this check's
#            output").
#
# Anti-vacuity (why the positive controls are not optional -- and why EACH
# scanner is proven separately): a scanner that finds nothing because its
# patterns are wrong, its `nm` is the host one, or its ELF path is stale, PASSES
# silently and looks exactly like a clean build. So the same scans, with the same
# patterns and the same nm, are also run over builds that MUST trip them:
#   * esp32dev_tuning (the bench console env) must produce console:: SYMBOL hits
#     AND a [tune] STRING hit;
#   * esp32dev_sim (the Wokwi sim env) -- when present -- must produce
#     simfeeder:: SYMBOL hits AND a [sim] STRING hit.
# The two scanners are asserted INDEPENDENTLY, never as a union, because a union
# hides the one failure that matters most here: with a working `strings` and a
# useless `nm` (host nm, wrong binary, empty output) the string hits alone would
# satisfy both controls -- while the BT half of the quarantine has NO string
# detector at all (Bluepad32/BTstack put no banner in .rodata), so `nm` is its
# ONLY detector. The 2026-09-03 review demonstrated the hole: this script with
# `--nm /usr/bin/true --elf .pio/build/esp32dev_btshowoff/firmware.elf` exited 0
# and certified a full Bluepad32/BTstack image as clean. That same command now
# exits 4.
# If a control build fails to trip EITHER scanner, this script fails with exit 4
# (VACUOUS) instead of reporting a green delivery image, because at that point it
# has proven nothing about the delivery image either.
#
# Usage:
#   tools/delivery_shape_check.sh [--elf PATH] [--console-control PATH]
#                                 [--sim-control PATH] [--nm PATH]
#                                 [--strict] [-q|--quiet]
#
#   --elf PATH              Delivery ELF to check.
#                           Default: .pio/build/esp32dev/firmware.elf
#   --console-control PATH  Build that MUST contain console symbols.
#                           Default: .pio/build/esp32dev_tuning/firmware.elf
#   --sim-control PATH      Build that MUST contain sim markers.
#                           Default: .pio/build/esp32dev_sim/firmware.elf
#   --nm PATH               Cross `nm` to use. Default: W17_NM, else
#                           xtensa-esp32-elf-nm on PATH, else the PlatformIO
#                           toolchain copy under ~/.platformio/packages.
#   --strict                A missing control build is a HARD FAILURE (exit 4)
#                           rather than a reported skip. Use this in CI, which
#                           builds every env before calling us. Also settable as
#                           W17_DELIVERY_CHECK_STRICT=1.
#
# Exit codes (deliberately distinct -- "the image is dirty" and "I could not
# check" are different failures and CI must be able to tell them apart):
#   0  delivery ELF is clean AND every available control tripped the scanner
#   1  SHAPE VIOLATION: quarantined code is present in the delivery image
#   2  CANNOT CHECK: delivery ELF or a working cross-nm is missing
#   3  usage error
#   4  VACUOUS: a control build did not trip one of the two scanners (or, under
#      --strict, is missing), so a "clean" result would not mean anything
#
# Falsification recipe (how to see it go red, no source edit needed):
#   pio run -e esp32dev_tuning
#   tools/delivery_shape_check.sh --elf .pio/build/esp32dev_tuning/firmware.elf
#   => exit 1, listing the console:: symbols. That is the injected-regression
#      proof the fix-wave verdict asks for: a delivery ELF carrying console code
#      is exactly what the tuning ELF is.
#
#   tools/delivery_shape_check.sh --nm /usr/bin/true
#   => exit 4 (VACUOUS): a `nm` that prints nothing trips no symbol match on the
#      console control, so the run is refused instead of reporting a clean image.

set -u

STRICT="${W17_DELIVERY_CHECK_STRICT:-0}"
QUIET=0
ELF=""
CONSOLE_CONTROL=""
SIM_CONTROL=""
NM="${W17_NM:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --elf) shift
               if [ $# -eq 0 ]; then echo "error: --elf needs a PATH" >&2; exit 3; fi
               ELF="$1"; shift ;;
        --console-control) shift
               if [ $# -eq 0 ]; then echo "error: --console-control needs a PATH" >&2; exit 3; fi
               CONSOLE_CONTROL="$1"; shift ;;
        --sim-control) shift
               if [ $# -eq 0 ]; then echo "error: --sim-control needs a PATH" >&2; exit 3; fi
               SIM_CONTROL="$1"; shift ;;
        --nm) shift
               if [ $# -eq 0 ]; then echo "error: --nm needs a PATH" >&2; exit 3; fi
               NM="$1"; shift ;;
        --strict) STRICT=1; shift ;;
        -q|--quiet) QUIET=1; shift ;;
        -h|--help) sed -n '3,94p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "error: unknown argument '$1' (try --help)" >&2; exit 3 ;;
    esac
done

say() { [ "$QUIET" -eq 1 ] || printf '%s\n' "$*"; }

# Repo root = parent of the directory holding this script, so it works from any cwd.
SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(dirname -- "$SCRIPT_DIR")"

[ -n "$ELF" ] || ELF="$REPO_ROOT/.pio/build/esp32dev/firmware.elf"
[ -n "$CONSOLE_CONTROL" ] || CONSOLE_CONTROL="$REPO_ROOT/.pio/build/esp32dev_tuning/firmware.elf"
[ -n "$SIM_CONTROL" ] || SIM_CONTROL="$REPO_ROOT/.pio/build/esp32dev_sim/firmware.elf"

# --- the quarantine patterns -------------------------------------------------
#
# Symbols (nm -C, demangled). Namespaces are the contract:
#   console:: / console_hal_esp32::  the bench tuning console (lib/console,
#       lib/console_hal_esp32) -- compiled in every env by the LDF, linked only
#       under -DW17_TUNING_CONSOLE.
#   btpad:: / btpad_hal_esp32::      the BT show-off prototype (never a delivery
#       target; docs/bt_showoff_design.md 2.1).
#   luepad / btstack / uni_platform  Bluepad32's own core + BTstack symbols, so
#       the check does not depend on our wrapper alone (matches the pattern set
#       D8 Phase 11a step 7 prescribes; "luepad" avoids B/b case questions).
#   simfeeder:: / simpad::           the scripted sim feeders (src/SimCrsfFeeder,
#       src/SimPadFeeder), delivery-lineage source gated by -DW17_SIM_*.
SYMBOL_PATTERNS='console::|console_hal_esp32::|btpad::|btpad_hal_esp32::|luepad|btstack|uni_platform|simfeeder::|simpad::'

# Strings. The sim feeders and the console announce themselves on the serial
# port with these tags; a string check catches a build where the code was
# inlined away into an unnamed symbol but its banner survived in .rodata.
STRING_PATTERNS='\[sim\]|\[simpad\]|\[tune\]'

# --- locate a cross nm -------------------------------------------------------
if [ -z "$NM" ]; then
    if command -v xtensa-esp32-elf-nm > /dev/null 2>&1; then
        NM="$(command -v xtensa-esp32-elf-nm)"
    else
        # PlatformIO installs the cross toolchain under ~/.platformio/packages.
        # The package name has changed across releases (toolchain-xtensa-esp32,
        # toolchain-xtensa32, ...), so glob the family rather than one name; the
        # BINARY name is the stable part.
        for candidate in "$HOME"/.platformio/packages/toolchain-*/bin/xtensa-esp32-elf-nm; do
            if [ -x "$candidate" ]; then NM="$candidate"; break; fi
        done
    fi
fi
if [ -z "$NM" ] || [ ! -x "$NM" ]; then
    echo "delivery-shape-check: CANNOT CHECK -- no xtensa-esp32-elf-nm found." >&2
    echo "  Build once with PlatformIO (which installs the toolchain), put it on" >&2
    echo "  PATH, or pass --nm PATH / set W17_NM." >&2
    exit 2
fi

# scan_elf PATH -> prints matching lines, returns 0 when at least one matched.
scan_elf() {
    "$NM" -C "$1" 2>/dev/null | grep -E "$SYMBOL_PATTERNS"
}
scan_strings() {
    strings "$1" 2>/dev/null | grep -E "$STRING_PATTERNS"
}

# --- the delivery image ------------------------------------------------------
if [ ! -f "$ELF" ]; then
    echo "delivery-shape-check: CANNOT CHECK -- no delivery ELF at $ELF" >&2
    echo "  Build it first: pio run -e esp32dev" >&2
    exit 2
fi

symbol_hits="$(scan_elf "$ELF")"
string_hits="$(scan_strings "$ELF")"

if [ -n "$symbol_hits" ] || [ -n "$string_hits" ]; then
    echo "delivery-shape-check: DELIVERY SHAPE VIOLATION" >&2
    echo "  ELF: $ELF" >&2
    echo "" >&2
    if [ -n "$symbol_hits" ]; then
        echo "  Quarantined SYMBOLS linked into the delivery image:" >&2
        printf '%s\n' "$symbol_hits" | sed 's/^/    /' >&2
    fi
    if [ -n "$string_hits" ]; then
        echo "  Quarantined STRINGS present in the delivery image:" >&2
        printf '%s\n' "$string_hits" | sed 's/^/    /' >&2
    fi
    echo "" >&2
    echo "The gift image (env:esp32dev) must link NO bench console, NO BT show-off" >&2
    echo "code and NO sim feeder -- that quarantine is what platformio.ini's env" >&2
    echo "comments, CLAUDE.md/AGENTS.md 'Delivery vs tuning builds' and ROADMAP D8" >&2
    echo "call 'ELF-verified'. Move the code behind its build flag, or" >&2
    echo "change the invariant deliberately (and everywhere) first." >&2
    exit 1
fi

# --- anti-vacuity controls ---------------------------------------------------
vacuous=0

# check_control NAME PATH EXPECT_SYMBOL_REGEX EXPECT_STRING_REGEX
#
# Both scanners must trip, and they are judged SEPARATELY. Union logic would let
# a live `strings` cover for a dead `nm` -- see the anti-vacuity note in the
# header: the BT quarantine has no string detector, so a silently useless `nm`
# would disarm it while this script still printed OK.
check_control() {
    name="$1"; path="$2"; expect_sym="$3"; expect_str="$4"
    if [ ! -f "$path" ]; then
        if [ "$STRICT" -eq 1 ]; then
            echo "delivery-shape-check: VACUOUS -- $name control build missing at $path" >&2
            echo "  --strict was given (CI builds every env before calling this), so an" >&2
            echo "  unverifiable scanner is a failure, not a skip." >&2
            vacuous=1
        else
            say "delivery-shape-check: NOTE -- $name control not built ($path);"
            say "  the scanner itself is unproven this run. CI runs with --strict."
        fi
        return
    fi
    sym_hits="$(scan_elf "$path" | grep -E "$expect_sym")"
    str_hits="$(scan_strings "$path" | grep -E "$expect_str")"

    if [ -z "$sym_hits" ]; then
        echo "delivery-shape-check: VACUOUS -- the $name control tripped no SYMBOL match." >&2
        echo "  $path yields no /$expect_sym/ from '$NM -C', so the symbol scanner is" >&2
        echo "  broken (host nm, wrong nm, wrong patterns, or a stale ELF). The symbol" >&2
        echo "  scan is the ONLY detector for the Bluepad32/BTstack half of the" >&2
        echo "  quarantine, so a clean delivery result above proves NOTHING about it." >&2
        vacuous=1
    fi
    if [ -z "$str_hits" ]; then
        echo "delivery-shape-check: VACUOUS -- the $name control tripped no STRING match." >&2
        echo "  $path yields no /$expect_str/ from 'strings', so the string scanner is" >&2
        echo "  broken or missing (binutils absent on this runner). It is the detector" >&2
        echo "  that survives a build where the code was inlined into unnamed symbols," >&2
        echo "  so a clean delivery result above is only half-proven." >&2
        vacuous=1
    fi
    if [ -n "$sym_hits" ] && [ -n "$str_hits" ]; then
        say "delivery-shape-check: control OK -- $name build trips BOTH scanners ($(printf '%s\n' "$sym_hits" | wc -l | tr -d ' ') symbol, $(printf '%s\n' "$str_hits" | wc -l | tr -d ' ') string hits)"
    fi
}

check_control "console (esp32dev_tuning)" "$CONSOLE_CONTROL" 'console::' '\[tune\]'
check_control "sim (esp32dev_sim)" "$SIM_CONTROL" 'simfeeder::' '\[sim\]'

if [ "$vacuous" -ne 0 ]; then
    exit 4
fi

say "delivery-shape-check: OK -- no console/btpad/Bluepad32/sim symbol or banner in"
say "  $ELF"
exit 0
