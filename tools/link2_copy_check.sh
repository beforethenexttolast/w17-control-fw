#!/usr/bin/env bash
#
# link2 cross-repo drift guard (audit R06, decided 2026-07-25).
#
# This repo OWNS the link2 protocol (docs/link2_protocol.md + lib/link2/).
# w17-soundlight-fw carries a VERBATIM COPY of the shared subset, deliberately
# omitting src/Link2Sender.cpp (control-side only). The copy is permanent by
# decision, so it is guarded rather than shared: this script diffs the shared
# files and reports drift. It NEVER writes to either repo.
#
# Scope -- what this does and does not prove:
#   DOES     catch the two checked-out copies diverging, byte for byte.
#   DOES NOT prove the wire format itself is correct: that is pinned hermetically
#            by `pio test -e native` (test_golden_frame_bytes fixes the exact 14
#            bytes; test_crc_matches_crsf_implementation pins the CRC against
#            lib/crsf). Both layers are needed; neither substitutes.
#
# Two tiers, on purpose:
#   FATAL     the 4 shared CODE files. These are compiled on both boards, so
#             byte-identity IS the correct invariant and any difference is a bug.
#   REPORTED  docs/link2_protocol.md, which soundlight-fw also carries as a copy.
#             Its NORMATIVE content (field table, lengths, CRC, timeout rule) must
#             not drift -- but byte-identity is the WRONG bar for it, because the
#             canonical copy legitimately carries this-repo-specific prose (the
#             ownership section names this very script, which does not exist over
#             there). A plain diff cannot tell normative drift from local
#             commentary, so this tier reports and leaves the judgement to a human
#             instead of forcing the tooling prose to be mirrored.
#
# Usage:
#   tools/link2_copy_check.sh [--strict] [--sibling PATH] [-q|--quiet]
#
#   --strict          A missing/unreadable sibling is a HARD FAILURE (exit 2).
#                     Use this in soundlight-fw's CI, where the sibling is always
#                     present and "absent => pass" would make the guard
#                     decoration. Also settable as W17_LINK2_CHECK_STRICT=1.
#   --sibling PATH    Path to the w17-soundlight-fw checkout.
#                     Default: ../w17-soundlight-fw relative to this repo.
#                     Also settable as W17_SOUNDLIGHT_DIR.
#
# Exit codes (deliberately distinct -- "drifted" and "couldn't check" are
# different failures and CI should be able to tell them apart):
#   0  shared CODE files identical, or sibling absent in non-strict mode (skipped).
#      A reported-tier doc difference does NOT change the exit code.
#   1  DRIFT: at least one shared code file differs, or is missing on the copy side
#   2  COULD NOT CHECK: sibling absent/unreadable, and --strict was given
#   3  usage error

set -u

STRICT="${W17_LINK2_CHECK_STRICT:-0}"
SIBLING="${W17_SOUNDLIGHT_DIR:-}"
QUIET=0

while [ $# -gt 0 ]; do
    case "$1" in
        --strict) STRICT=1; shift ;;
        --sibling) shift
                   if [ $# -eq 0 ]; then echo "error: --sibling needs a PATH" >&2; exit 3; fi
                   SIBLING="$1"; shift ;;
        -q|--quiet) QUIET=1; shift ;;
        -h|--help) sed -n '3,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "error: unknown argument '$1' (try --help)" >&2; exit 3 ;;
    esac
done

say() { [ "$QUIET" -eq 1 ] || printf '%s\n' "$*"; }

# Repo root = parent of the directory holding this script, so the script works
# from any cwd.
SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(dirname -- "$SCRIPT_DIR")"
[ -n "$SIBLING" ] || SIBLING="$REPO_ROOT/../w17-soundlight-fw"

LOCAL_LINK2="$REPO_ROOT/lib/link2"
COPY_LINK2="$SIBLING/lib/link2"

# The shared subset. src/Link2Sender.cpp is intentionally NOT listed: it is
# control-side only and must not exist on the copy side.
SHARED_FILES="
include/link2/Link2Frame.hpp
include/link2/Link2Codec.hpp
src/Link2Codec.cpp
library.json
"

# --- sanity: our own side must be intact before we can compare anything ------
if [ ! -d "$LOCAL_LINK2" ]; then
    echo "link2-copy-check: CANNOT CHECK -- canonical lib not found at $LOCAL_LINK2" >&2
    exit 2
fi

# --- sibling presence --------------------------------------------------------
if [ ! -d "$COPY_LINK2" ]; then
    if [ "$STRICT" -eq 1 ]; then
        echo "link2-copy-check: CANNOT CHECK -- no soundlight-fw copy at $COPY_LINK2" >&2
        echo "  --strict was given, so this is a failure. Check out w17-soundlight-fw" >&2
        echo "  beside this repo, or pass --sibling PATH / set W17_SOUNDLIGHT_DIR." >&2
        exit 2
    fi
    say "link2-copy-check: SKIPPED -- no soundlight-fw copy at $COPY_LINK2"
    say "  (non-strict mode: nothing to compare, not treated as a failure."
    say "   Use --strict in CI, where the sibling must always be present.)"
    exit 0
fi

# --- compare -----------------------------------------------------------------
drifted=""
missing=""

for rel in $SHARED_FILES; do
    ours="$LOCAL_LINK2/$rel"
    theirs="$COPY_LINK2/$rel"

    if [ ! -f "$ours" ]; then
        # A shared file vanished from the OWNER side: real drift, and the more
        # alarming direction, so report it rather than silently skipping.
        missing="$missing $rel(canonical)"
        continue
    fi
    if [ ! -f "$theirs" ]; then
        missing="$missing $rel(copy)"
        continue
    fi
    if ! diff -u "$ours" "$theirs" > /dev/null 2>&1; then
        drifted="$drifted $rel"
    fi
done

# --- reported tier: the protocol document -----------------------------------
DOC_REL="docs/link2_protocol.md"
OUR_DOC="$REPO_ROOT/$DOC_REL"
THEIR_DOC="$SIBLING/$DOC_REL"
if [ -f "$OUR_DOC" ] && [ -f "$THEIR_DOC" ]; then
    if ! diff -u "$OUR_DOC" "$THEIR_DOC" > /dev/null 2>&1; then
        say "link2-copy-check: REPORT -- $DOC_REL differs from the soundlight copy."
        say "  Not a failure (see the two-tier note at the top of this script), but"
        say "  CHECK IT BY HAND: if any normative part drifted -- field table, lengths,"
        say "  CRC, the 500 ms staleness rule -- re-sync soundlight's copy. If the only"
        say "  difference is this repo's local prose, nothing to do."
        say "  Review with: diff -u '$OUR_DOC' '$THEIR_DOC'"
    fi
elif [ -f "$OUR_DOC" ] && [ ! -f "$THEIR_DOC" ]; then
    say "link2-copy-check: REPORT -- the copy has no $DOC_REL (informational)."
fi

# Informational only: the copy must not carry the control-side sender. Not a
# drift failure (it is not a shared file), but it means someone copied too much.
if [ -f "$COPY_LINK2/src/Link2Sender.cpp" ]; then
    say "link2-copy-check: NOTE -- the copy carries src/Link2Sender.cpp, which is"
    say "  control-side only. Harmless to the wire format, but board #2 will"
    say "  compile it as dead code. Consider removing it there."
fi

if [ -n "$missing" ] || [ -n "$drifted" ]; then
    echo "link2-copy-check: DRIFT DETECTED" >&2
    echo "  canonical: $LOCAL_LINK2" >&2
    echo "  copy:      $COPY_LINK2" >&2
    for rel in $missing; do
        echo "  MISSING  $rel" >&2
    done
    for rel in $drifted; do
        echo "  DIFFERS  $rel" >&2
    done
    for rel in $drifted; do
        echo "" >&2
        echo "--- diff: $rel (canonical vs copy) ---" >&2
        diff -u "$LOCAL_LINK2/$rel" "$COPY_LINK2/$rel" >&2 || true
    done
    echo "" >&2
    echo "This repo owns the protocol (docs/link2_protocol.md). Land the change" >&2
    echo "HERE first, then re-copy the shared subset into soundlight-fw." >&2
    exit 1
fi

say "link2-copy-check: OK -- all 4 shared link2 code files identical"
say "  canonical: $LOCAL_LINK2"
say "  copy:      $COPY_LINK2"
exit 0
