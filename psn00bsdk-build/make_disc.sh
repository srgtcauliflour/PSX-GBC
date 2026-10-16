#!/bin/bash
# Build a PS1 disc image (psx-gbc.bin/.cue) containing PSXGBC.EXE plus any ROMs you give it.
# Usage:  ./make_disc.sh game1.gb game2.gbc ...        (ROMs are copied in as 8.3 names GAME1.GB, GAME2.GBC, ...)
# Needs: build/psxgbc.exe (see STATUS.md "How to rebuild") and `mkpsxiso` on PATH
#        (or set MKPSXISO=/path/to/mkpsxiso). ROMs must be <= 1.5MB and are NOT part of this repo.
set -e
cd "$(dirname "$0")"
MKPSXISO="${MKPSXISO:-mkpsxiso}"
[ -f build/psxgbc.exe ] || { echo "build/psxgbc.exe missing - build the PS1 executable first"; exit 1; }
[ $# -ge 1 ] || { echo "usage: $0 rom1.gb [rom2.gbc ...]"; exit 1; }
rm -rf disc_roms && mkdir disc_roms
files=""; n=0
for rom in "$@"; do
  n=$((n+1)); ext="${rom##*.}"; ext="$(echo "$ext" | tr a-z A-Z)"
  [ "$ext" = GB ] || [ "$ext" = GBC ] || { echo "skipping $rom (not .gb/.gbc)"; n=$((n-1)); continue; }
  cp "$rom" "disc_roms/GAME$n.$ext"
  files="$files\n\t\t\t<file name=\"GAME$n.$ext\" type=\"data\" source=\"disc_roms/GAME$n.$ext\" />"
done
python3 - "$files" <<'PY'
import sys,re
x=open('iso.xml').read()
x=re.sub(r'\s*<file name="GAME[^"]*"[^>]*/>','',x)
x=x.replace('</directory_tree>',sys.argv[1].replace('\\n','\n').replace('\\t','\t')+'\n\t\t</directory_tree>')
open('iso_custom.xml','w').write(x)
PY
"$MKPSXISO" -y iso_custom.xml
echo "Wrote psx-gbc.bin / psx-gbc.cue with $n ROM(s). (disc_roms/ and iso_custom.xml are local scratch; do not commit your ROMs.)"
