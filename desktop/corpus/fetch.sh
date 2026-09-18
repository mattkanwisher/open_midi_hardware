#!/bin/sh
# fetch.sh - put the corpus in desktop/corpus/files/, from pinned commits.
#
#   ./desktop/corpus/fetch.sh              # fetch what is missing, verify all
#   ./desktop/corpus/fetch.sh --list       # what the manifest says, fetch nothing
#   ./desktop/corpus/fetch.sh --force      # re-download even what is present
#
# WHY A SCRIPT AND NOT COMMITTED FILES. The root .gitignore says it: "Upstream
# clones are read here, never committed ... Each workstream records the commit
# hash it used in its own README so the reading is reproducible." bench/vendor
# and boot/vendor work that way and so does this. The repository records WHAT
# it read and WHERE, and the bytes stay with their owners.
#
# There is a second reason here, specific to this directory. Two of the seven
# files are GPL-2.0-only. Committing them would put GPL'd data inside a tree
# that is otherwise 0BSD, which is a licensing decision nobody asked for and
# which a fetch script makes unnecessary. MANIFEST.tsv carries the copyright
# notices that both licences require to travel with the work, and this script
# downloads each source repository's own licence text next to the files.
#
# NONE OF THIS IS MT-32 MATERIAL. Every file is General MIDI written for a
# game's software mixer or an OPL card. It is here for POLYPHONY, LENGTH and
# EVENT RATE -- see corpus/README.md, which is blunt about what that does and
# does not tell you.
#
# SPDX-License-Identifier: 0BSD

set -e
cd "$(dirname "$0")"
HERE=$(pwd)
FILES=$HERE/files
MANIFEST=$HERE/MANIFEST.tsv
RAW=https://raw.githubusercontent.com

LIST=0
FORCE=0
for a in "$@"; do
    case $a in
        --list)  LIST=1;;
        --force) FORCE=1;;
        -h|--help)
            sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'
            exit 0;;
        *) echo "fetch.sh: unknown option $a"; exit 2;;
    esac
done

[ -f "$MANIFEST" ] || { echo "fetch.sh: no $MANIFEST"; exit 2; }

# --------------------------------------------------------------- the tools --

if command -v sha256sum >/dev/null 2>&1; then
    sha256() { sha256sum "$1" | cut -d' ' -f1; }
elif command -v shasum >/dev/null 2>&1; then
    sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
else
    echo "fetch.sh: no sha256sum and no shasum -- cannot verify, refusing"
    exit 2
fi

if command -v curl >/dev/null 2>&1; then
    get() { curl -fsSL --max-time 120 -o "$2" "$1"; }
elif command -v wget >/dev/null 2>&1; then
    get() { wget -q -T 120 -O "$2" "$1"; }
else
    echo "fetch.sh: neither curl nor wget"; exit 2
fi

mkdir -p "$FILES"

# ------------------------------------------------------------------- work --

ok=0; failed=0; fetched=0

# The manifest is tab separated with a header row and '#' comments. Read it
# with the field separator set explicitly so a name or a note containing a
# space cannot split a column.
grep -v '^#' "$MANIFEST" | tail -n +2 | while IFS='	' read -r \
        name repo commit path bytes want licence licence_file copyright note; do
    [ -n "$name" ] || continue
    dst=$FILES/$name.mid
    url=$RAW/$repo/$commit/$path

    if [ "$FORCE" = 0 ] && [ -f "$dst" ]; then
        got=$(sha256 "$dst")
        if [ "$got" = "$want" ]; then
            [ "$LIST" = 1 ] || printf '  %-9s ok (already here)\n' "$name"
            continue
        fi
        printf '  %-9s present but WRONG sha256 -- refetching\n' "$name"
    fi

    if [ "$LIST" = 1 ]; then
        printf '  %-9s %-9s %7s B  %s\n     %s\n' \
               "$name" "$licence" "$bytes" "$repo@$(echo "$commit" | cut -c1-12)" "$note"
        continue
    fi

    printf '  %-9s ' "$name"
    if ! get "$url" "$dst"; then
        echo "FETCH FAILED  $url"
        rm -f "$dst"
        continue
    fi
    got=$(sha256 "$dst")
    if [ "$got" != "$want" ]; then
        echo "SHA256 MISMATCH -- refusing to keep it"
        echo "     want $want"
        echo "     got  $got"
        rm -f "$dst"
        continue
    fi
    echo "ok ($bytes B, $licence)"
done

[ "$LIST" = 1 ] && exit 0

# The licence text of each source repository, beside the files it covers. Both
# BSD-3-Clause and GPL-2.0-only require the notice to travel with the work.
echo
echo "licence texts:"
grep -v '^#' "$MANIFEST" | tail -n +2 | awk -F'\t' '{print $2"\t"$3"\t"$8}' |
    sort -u | while IFS='	' read -r repo commit licence_file; do
    [ -n "$repo" ] || continue
    out=$FILES/LICENCE.$(echo "$repo" | tr '/' '.').txt
    if [ -f "$out" ]; then
        printf '  %-28s already here\n' "$(basename "$out")"
        continue
    fi
    printf '  %-28s ' "$(basename "$out")"
    if get "$RAW/$repo/$commit/$licence_file" "$out"; then
        echo "ok"
    else
        echo "FAILED ($licence_file from $repo@$commit)"
        rm -f "$out"
    fi
done

# ------------------------------------------------------------------ report --

echo
have=$(ls "$FILES"/*.mid 2>/dev/null | wc -l | tr -d ' ')
want=$(grep -v '^#' "$MANIFEST" | tail -n +2 | grep -c .)
echo "$have of $want files in $FILES"
if [ "$have" != "$want" ]; then
    echo
    echo "Something did not arrive. raw.githubusercontent.com is the only"
    echo "host this needs; if it is unreachable, clone the repositories named"
    echo "in MANIFEST.tsv at the pinned commits and copy the paths by hand."
    exit 1
fi
cat <<'NOTE'

What you have is General MIDI written for games, not MT-32 material, and no
amount of it tells you how this project sounds. It is here because it is dense,
long and legally redistributable. Read corpus/README.md before quoting anything
measured from it.

    python3 desktop/corpus/scan.py desktop/corpus/files/*.mid
    ./desktop/ab.sh --corpus map24 --roms ~/mt32roms
NOTE
