// Host-side preview: renders the display layout into a 1-bit canvas using
// the exact same render.cpp + font bitmaps as the firmware, writes a PGM.
//   preview [north] [south] [bus-west] [bus-east] [route-letter] [out.pgm]
//   preview --message "text" [out.pgm]
//   preview --commute MINUTES [ROUTE] [out.pgm]
#include <stdio.h>
#include <string.h>
#include <Adafruit_GFX.h>
#include "render.h"
#include <stdlib.h>

static const int W = 792, H = 272;

int main(int argc, char **argv) {
  GFXcanvas1 canvas(W, H);
  const char *out = "preview.pgm";

  // Weather comes from MQTT on the device; for previews take it from the
  // environment (WX_COND= to blank it out entirely)
  WeatherInfo wx = {};
  const char *wc = getenv("WX_COND");
  snprintf(wx.cond, sizeof(wx.cond), "%s", wc ? wc : "partlycloudy");
  wx.lo = getenv("WX_LO") ? atoi(getenv("WX_LO")) : 63;
  wx.hi = getenv("WX_HI") ? atoi(getenv("WX_HI")) : 81;
  wx.valid = wx.cond[0] != 0;
  // RAIN_IN=hours (0 = now, unset/-1 = dry), RAIN_AT=label ("6pm")
  wx.rainIn = getenv("RAIN_IN") ? atoi(getenv("RAIN_IN")) : -1;
  snprintf(wx.rainAt, sizeof(wx.rainAt), "%s",
           getenv("RAIN_AT") ? getenv("RAIN_AT") : "");

  // Bottom-zone notice: NOTE_TITLE / NOTE_TEXT / NOTE_PCT / NOTE_IDX / NOTE_COUNT
  Notice note = {};
  snprintf(note.title, sizeof(note.title), "%s",
           getenv("NOTE_TITLE") ? getenv("NOTE_TITLE") : "");
  snprintf(note.text, sizeof(note.text), "%s",
           getenv("NOTE_TEXT") ? getenv("NOTE_TEXT") : "");
  note.pct = getenv("NOTE_PCT") ? atoi(getenv("NOTE_PCT")) : -1;
  note.idx = getenv("NOTE_IDX") ? atoi(getenv("NOTE_IDX")) : 0;
  note.count = getenv("NOTE_COUNT") ? atoi(getenv("NOTE_COUNT")) : 1;

  if (argc >= 3 && strcmp(argv[1], "--message") == 0) {
    renderMessage(canvas, argv[2]);
    if (argc > 3) out = argv[3];
  } else if (argc >= 3 && strcmp(argv[1], "--commute") == 0) {
    // preview --commute MINUTES [ROUTE] [out.pgm]
    const char *route = argc > 3 ? argv[3] : "C";
    renderCommute(canvas, route, argv[2], &wx);
    if (argc > 4) out = argv[4];
  } else {
    const char *north = argc > 1 ? argv[1] : "3,8,15";
    const char *south = argc > 2 ? argv[2] : "5,12";
    const char *busWest = argc > 3 ? argv[3] : "7,22";
    const char *busEast = argc > 4 ? argv[4] : "4,31";
    const char *route = argc > 5 ? argv[5] : "C";
    renderArrivals(canvas, route, route, north, south, busWest, busEast, &wx,
                   &note);
    if (argc > 6) out = argv[6];
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
