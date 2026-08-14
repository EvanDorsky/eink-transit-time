#include "render.h"
#include "fonts/HelveticaBold14pt7b.h"
#include "fonts/HelveticaBold20pt7b.h"
#include "fonts/HelveticaBold22pt7b.h"
#include "fonts/HelveticaBold26pt7b.h"

// Same values as GxEPD_BLACK / GxEPD_WHITE, redefined here so this file
// compiles on the host without GxEPD2
static const uint16_t K_BLACK = 0x0000;
static const uint16_t K_WHITE = 0xFFFF;

// Four data rows, vertically centered on the 272px panel (equal breathing
// room top and bottom): 26pt digits with a small inline "min" at the margin
static const int16_t ROW_BASELINES[] = {61, 123, 185, 247};
static const int16_t LABEL_X = 78;

// MTA route bullet: filled disc with the route letter knocked out in white
static void drawRouteBullet(Adafruit_GFX &gfx, const char *letter, int16_t cx,
                            int16_t cy, int16_t r) {
  gfx.fillCircle(cx, cy, r, K_BLACK);
  gfx.setFont(&HelveticaBold22pt7b);
  gfx.setTextColor(K_WHITE);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  gfx.getTextBounds(letter, 0, 0, &tbx, &tby, &tbw, &tbh);
  gfx.setCursor(cx - tbw / 2 - tbx, cy - tbh / 2 - tby);
  gfx.print(letter);
  gfx.setTextColor(K_BLACK);
}

// Left label plus big right-aligned minute numbers with a small "min"
// underneath, like the real countdown clocks
static void printArrivalRow(Adafruit_GFX &gfx, const char *label,
                            const char *nums, int16_t baselineY) {
  gfx.setFont(&HelveticaBold26pt7b);
  gfx.setCursor(LABEL_X, baselineY);
  gfx.print(label);

  if (!nums[0]) return;
  int16_t tbx, tby;
  uint16_t tbw, tbh;

  // small "min" sits at the right margin on the shared baseline
  gfx.setFont(&HelveticaBold14pt7b);
  gfx.getTextBounds("min", 0, 0, &tbx, &tby, &tbw, &tbh);
  int16_t minX = gfx.width() - 16 - tbw - tbx;
  gfx.setCursor(minX, baselineY);
  gfx.print("min");

  gfx.setFont(&HelveticaBold26pt7b);
  gfx.getTextBounds(nums, 0, 0, &tbx, &tby, &tbw, &tbh);
  gfx.setCursor(minX - 8 - tbw - tbx, baselineY);
  gfx.print(nums);
}

void renderArrivals(Adafruit_GFX &gfx, const char *routeLetter,
                    const char *northRow, const char *southRow,
                    const char *busWestRow, const char *busEastRow) {
  gfx.fillScreen(K_WHITE);
  gfx.setTextColor(K_BLACK);

  // subway rows carry the route bullet, centered on the digit caps
  drawRouteBullet(gfx, routeLetter, 42, ROW_BASELINES[0] - 18, 26);
  drawRouteBullet(gfx, routeLetter, 42, ROW_BASELINES[1] - 18, 26);

  printArrivalRow(gfx, "Manhattan", northRow, ROW_BASELINES[0]);
  printArrivalRow(gfx, "Euclid", southRow, ROW_BASELINES[1]);
  printArrivalRow(gfx, "B25/26 Dtwn", busWestRow, ROW_BASELINES[2]);
  printArrivalRow(gfx, "B25/26 East", busEastRow, ROW_BASELINES[3]);
}

void renderMessage(Adafruit_GFX &gfx, const char *msg) {
  gfx.fillScreen(K_WHITE);
  gfx.setTextColor(K_BLACK);
  gfx.setFont(&HelveticaBold26pt7b);
  gfx.setCursor(14, 140);
  gfx.print(msg);
}
