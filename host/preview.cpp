// Host-side preview: renders the display layout into a 1-bit canvas using
// the exact same render.cpp + font bitmaps as the firmware, writes a PGM.
//   preview [north-row] [south-row] [out.pgm]
//   preview --message "text" [out.pgm]
#include <stdio.h>
#include <string.h>
#include <Adafruit_GFX.h>
#include "render.h"

static const int W = 792, H = 272;

int main(int argc, char **argv) {
  GFXcanvas1 canvas(W, H);
  const char *out = "preview.pgm";

  if (argc >= 3 && strcmp(argv[1], "--message") == 0) {
    renderMessage(canvas, argv[2]);
    if (argc > 3) out = argv[3];
  } else {
    const char *north = argc > 1 ? argv[1] : "3,8,15";
    const char *south = argc > 2 ? argv[2] : "5,12";
    const char *bus = argc > 3 ? argv[3] : "7,22";
    renderArrivals(canvas, north, south, bus);
    if (argc > 4) out = argv[4];
  }

  FILE *f = fopen(out, "wb");
  if (!f) {
    perror(out);
    return 1;
  }
  fprintf(f, "P5\n%d %d\n255\n", W, H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      fputc(canvas.getPixel(x, y) ? 255 : 0, f);
  fclose(f);
  fprintf(stderr, "wrote %s\n", out);
  return 0;
}
