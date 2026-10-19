#!/bin/sh
# Regenerate GFX font headers from Helvetica.ttc (face 1 = Bold).
# Requires freetype (pkg-config freetype2) and the Adafruit GFX library
# checkout in .pio/libdeps (pulled by `pio run`).
set -e
cd "$(dirname "$0")/.."

GFX_DIR=".pio/libdeps/crowpanel_epaper_579/Adafruit GFX Library"
OUT_DIR="include/fonts"
BIN="$(mktemp -d)/fontconvert"

cc scripts/fontconvert.c -o "$BIN" -I"$GFX_DIR" $(pkg-config --cflags --libs freetype2)
mkdir -p "$OUT_DIR"

for size in 11 14 20 22 26 32; do
  FONTCONVERT_FACE=1 FONTCONVERT_NAME=HelveticaBold \
    "$BIN" Helvetica.ttc "$size" > "$OUT_DIR/HelveticaBold${size}pt7b.h"
  echo "generated $OUT_DIR/HelveticaBold${size}pt7b.h"
done

# commute-mode minute list ("22,28,34") beside the side column: comma+digits
FONTCONVERT_FACE=1 FONTCONVERT_NAME=HelveticaBold \
  "$BIN" Helvetica.ttc 54 44 57 > "$OUT_DIR/HelveticaBold54pt7b.h"
echo "generated $OUT_DIR/HelveticaBold54pt7b.h"
# Commute bullet: the route's letter or number at 85pt, the largest glyph
# that still fits a GFXfont's int8_t offsets. Chars 48-90 = 0-9 and A-Z, so
# any subway route (TRANSIT_ROUTE_*) can be shown; the few punctuation marks
# in between cost almost nothing
FONTCONVERT_FACE=1 FONTCONVERT_NAME=HelveticaBold \
  "$BIN" Helvetica.ttc 85 48 90 > "$OUT_DIR/HelveticaBold85pt7b.h"
echo "generated $OUT_DIR/HelveticaBold85pt7b.h"
