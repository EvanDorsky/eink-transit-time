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

# Commute-mode display font: chars 32-67 only (space..C — digits, comma,
# dash, route letters). 64pt is the ceiling — GFXglyph yOffset is int8_t,
# so glyphs taller than ~128px overflow; renderCommute scales 2x instead.
FONTCONVERT_FACE=1 FONTCONVERT_NAME=HelveticaBold \
  "$BIN" Helvetica.ttc 64 32 67 > "$OUT_DIR/HelveticaBold64pt7b.h"
echo "generated $OUT_DIR/HelveticaBold64pt7b.h"

# Commute bullet letter: route letters only (A-C) at 85pt, the largest
# glyph that still fits a GFXfont's int8_t offsets
# commute-mode minute list ("22,28,34") beside the side column: comma+digits
FONTCONVERT_FACE=1 FONTCONVERT_NAME=HelveticaBold \
  "$BIN" Helvetica.ttc 54 44 57 > "$OUT_DIR/HelveticaBold54pt7b.h"
echo "generated $OUT_DIR/HelveticaBold54pt7b.h"
FONTCONVERT_FACE=1 FONTCONVERT_NAME=HelveticaBold \
  "$BIN" Helvetica.ttc 85 65 67 > "$OUT_DIR/HelveticaBold85pt7b.h"
echo "generated $OUT_DIR/HelveticaBold85pt7b.h"
