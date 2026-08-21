#!/usr/bin/env bash
#
# Bulk-extract every LFD in a TIE Fighter resource directory through
# filmextract, producing the per-film manifest tree selected through the
# application's `--resource-root` option.
#
# Usage: tools/film/extract-all-lfds.sh <resource-dir> <output-dir>
#                                       [extra-filmextract-args...]
#
# By default emits BC7+zstd KTX2 (runtime asset) AND PNG (authoring
# artefact) at 4K-corrected aspect. Pass --no-zstd / --bc7-quality
# uber / etc. through and they'll forward to every invocation.
#
# Examples:
#   tools/film/extract-all-lfds.sh tie-collector/RESOURCE remaster/
#   tools/film/extract-all-lfds.sh ~/tie/RESOURCE out/ --bc7-quality uber
#
# Per-LFD overrides (palette + extra LFDs needed for correct rendering)
# come from tools/film/lfd-palettes.yaml — see that file for the schema and
# rationale. EMPIRE.LFD is also chained as `--extra` for every other
# LFD because it holds the canonical `standard` palette; if the YAML's
# matched rule already lists it, the duplicate is dropped.

set -euo pipefail

if [ "$#" -lt 2 ]; then
    sed -n '4,21p' "$0" >&2
    exit 2
fi

RES_DIR="$1"; shift
OUT_DIR="$1"; shift
PASS_THROUGH=("$@")

if [ ! -d "$RES_DIR" ]; then
    echo "extract-all-lfds: $RES_DIR is not a directory" >&2
    exit 1
fi

# Locate filmextract relative to this script. Falls back to PATH.
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
FILMEXTRACT="$ROOT/build/tools/film/filmextract"
if [ ! -x "$FILMEXTRACT" ]; then
    if command -v filmextract >/dev/null 2>&1; then
        FILMEXTRACT=$(command -v filmextract)
    else
        echo "extract-all-lfds: filmextract not found at $FILMEXTRACT" >&2
        echo "  build it first: cmake --build build --target filmextract" >&2
        exit 1
    fi
fi

OVERRIDES_FILE="$SCRIPT_DIR/lfd-palettes.yaml"

# Parse lfd-palettes.yaml once at startup → tab-separated rules table.
# Columns: <glob>\t<palette-or-dash>\t<extras-comma-separated-or-dash>.
# The parser is a separate Python file (lfd-palettes-parse.py) rather
# than an inline heredoc because bash 3.2 (macOS default) has a known
# parse bug with quoted heredocs inside `$( ... )` command
# substitution.
OVERRIDES_TABLE=$(python3 "$SCRIPT_DIR/lfd-palettes-parse.py" "$OVERRIDES_FILE")

# Look up the first matching override rule for $1 (LFD basename).
# Emits three lines on stdout: PALETTE, EXTRAS_CSV, MATCHED (1/0).
# Bash's `case` evaluates shell globs against the basename — fnmatch
# semantics, sufficient for `BATTLE*.LFD` etc.
match_rule() {
    local base="$1"
    local matched=0 palette="-" extras="-"
    if [ -n "$OVERRIDES_TABLE" ]; then
        while IFS=$'\t' read -r glob pal ext; do
            [ -z "$glob" ] && continue
            case "$base" in
                $glob)
                    matched=1; palette="$pal"; extras="$ext"; break;;
            esac
        done <<<"$OVERRIDES_TABLE"
    fi
    printf '%s\n%s\n%s\n' "$palette" "$extras" "$matched"
}

mkdir -p "$OUT_DIR"

# Build the --extra list for an LFD: dedup against EMPIRE (always
# included). `extras_csv` is comma-separated basenames from the
# YAML override; "-" means none.
build_extras() {
    local extras_csv="$1"
    local out=()
    # EMPIRE.LFD always — canonical shared-assets pool.
    if [ -f "$RES_DIR/EMPIRE.LFD" ]; then
        out+=("--extra" "$RES_DIR/EMPIRE.LFD")
    fi
    if [ "$extras_csv" != "-" ]; then
        local IFS=,
        for name in $extras_csv; do
            [ "$name" = "EMPIRE.LFD" ] && continue       # already in
            local p="$RES_DIR/$name"
            if [ -f "$p" ]; then
                out+=("--extra" "$p")
            else
                echo "  warning: extras lists $name but $p missing — skipping" >&2
            fi
        done
    fi
    printf '%s\n' "${out[@]+"${out[@]}"}"
}

run_one() {
    local lfd="$1"
    local base
    base=$(basename "$lfd")

    # Resolve overrides.
    local rule_out palette extras_csv matched
    rule_out=$(match_rule "$base")
    palette=$(sed -n '1p'   <<<"$rule_out")
    extras_csv=$(sed -n '2p'<<<"$rule_out")
    matched=$(sed -n '3p'   <<<"$rule_out")

    # Extras → --extra args (handles EMPIRE inclusion + dedup).
    local extras_args=()
    while IFS= read -r line; do
        [ -z "$line" ] && continue
        extras_args+=("$line")
    done < <(build_extras "$extras_csv")

    # Palette override?
    local pal_args=()
    if [ "$palette" != "-" ]; then
        pal_args=("--palette" "$palette")
    fi

    # Per-LFD passthrough plus the script-wide PASS_THROUGH array.
    # The `${arr[@]+"${arr[@]}"}` idiom lets empty arrays expand to
    # nothing under `set -u` (bash 3.2 / macOS default).
    local args=("$lfd" "$OUT_DIR" --bc7 --scale --atlas \
                ${pal_args[@]+"${pal_args[@]}"} \
                ${extras_args[@]+"${extras_args[@]}"} \
                ${PASS_THROUGH[@]+"${PASS_THROUGH[@]}"})

    if [ "$matched" = "1" ]; then
        echo "==> filmextract $base   [palette=$palette, extras=$extras_csv]"
    else
        echo "==> filmextract $base"
    fi
    "$FILMEXTRACT" "${args[@]}"
}

# 1. EMPIRE first (own films + the shared-assets pool every other
#    LFD references). EMPIRE doesn't chain into itself; build_extras
#    handles the self-skip.
if [ -f "$RES_DIR/EMPIRE.LFD" ]; then
    "$FILMEXTRACT" "$RES_DIR/EMPIRE.LFD" "$OUT_DIR" --bc7 --scale --atlas \
        ${PASS_THROUGH[@]+"${PASS_THROUGH[@]}"}
    echo "==> filmextract EMPIRE.LFD"
fi

# 2. Every other LFD with EMPIRE chained + per-LFD overrides applied.
shopt -s nullglob
for lfd in "$RES_DIR"/*.LFD "$RES_DIR"/*.lfd; do
    base=$(basename "$lfd")
    if [ "$base" = "EMPIRE.LFD" ] || [ "$base" = "empire.lfd" ]; then
        continue
    fi
    run_one "$lfd"
done

echo "==> done. Output tree at $OUT_DIR"
