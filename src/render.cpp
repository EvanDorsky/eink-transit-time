#include "render.h"
#include "fonts/HelveticaBold14pt7b.h"
#include "fonts/HelveticaBold20pt7b.h"
#include "fonts/HelveticaBold26pt7b.h"

// Same values as GxEPD_BLACK / GxEPD_WHITE, redefined here so this file
// compiles on the host without GxEPD2
static const uint16_t K_BLACK = 0x0000;
static const uint16_t K_WHITE = 0xFFFF;

static const char *ROUTE_LETTER = "C";
static const char *STATION_NAME = "home station";

// Three data rows between the header rule (y=74) and the panel bottom (272):
// 26pt digits with the small "min" tag tucked under their right edge
static const int16_t ROW_BASELINES[] = {119, 183, 246};
static const int16_t MIN_TAG_DROP = 24;

// MTA route bullet: filled disc with the route letter knocked out in white
static void drawRouteBullet(Adafruit_GFX &gfx, int16_t cx, int16_t cy,
                            int16_t r) {
  gfx.fillCircle(cx, cy, r, K_BLACK);
  gfx.setFont(&HelveticaBold20pt7b);
  gfx.setTextColor(K_WHITE);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  gfx.getTextBounds(ROUTE_LETTER, 0, 0, &tbx, &tby, &tbw, &tbh);
  gfx.setCursor(cx - tbw / 2 - tbx, cy - tbh / 2 - tby);
  gfx.print(ROUTE_LETTER);
}

// Big right-aligned minute numbers with a small "min" tucked underneath,
// like the real countdown clocks
static void printArrivalRow(Adafruit_GFX &gfx, const char *label,
                            const char *nums, int16_t baselineY) {
  gfx.setFont(&HelveticaBold26pt7b);
  gfx.setCursor(14, baselineY);
  gfx.print(label);

  if (!nums[0]) return;
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  gfx.getTextBounds(nums, 0, 0, &tbx, &tby, &tbw, &tbh);
  gfx.setCursor(gfx.width() - 16 - tbw - tbx, baselineY);
  gfx.print(nums);

  gfx.setFont(&HelveticaBold14pt7b);
  gfx.getTextBounds("min", 0, 0, &tbx, &tby, &tbw, &tbh);
  gfx.setCursor(gfx.width() - 16 - tbw - tbx, baselineY + MIN_TAG_DROP);
  gfx.print("min");
}

void renderArrivals(Adafruit_GFX &gfx, const char *northRow,
                    const char *southRow, const char *busRow) {
  gfx.fillScreen(K_WHITE);

  drawRouteBullet(gfx, 38, 38, 28);

  gfx.setTextColor(K_BLACK);
  gfx.setFont(&HelveticaBold20pt7b);
  gfx.setCursor(82, 51);
  gfx.print(STATION_NAME);

  gfx.fillRect(10, 74, gfx.width() - 20, 3, K_BLACK);

  printArrivalRow(gfx, "Manhattan", northRow, ROW_BASELINES[0]);
  printArrivalRow(gfx, "Euclid", southRow, ROW_BASELINES[1]);
  printArrivalRow(gfx, "B25/26 Dtwn", busRow, ROW_BASELINES[2]);
}

void renderMessage(Adafruit_GFX &gfx, const char *msg) {
  gfx.fillScreen(K_WHITE);
  gfx.setTextColor(K_BLACK);
  gfx.setFont(&HelveticaBold20pt7b);
  gfx.setCursor(14, 140);
  gfx.print(msg);
}
