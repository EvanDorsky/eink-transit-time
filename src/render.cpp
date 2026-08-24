#include "render.h"
#include "fonts/HelveticaBold11pt7b.h"
#include "fonts/HelveticaBold14pt7b.h"
#include "fonts/HelveticaBold20pt7b.h"
#include "fonts/HelveticaBold22pt7b.h"
#include "fonts/HelveticaBold26pt7b.h"
#include "fonts/HelveticaBold64pt7b.h"
#include "fonts/HelveticaBold85pt7b.h"

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

// Bus route badge: filled rounded rect matching the route bullet's visual
// weight, both route names stacked and knocked out in white
static void drawBusBadge(Adafruit_GFX &gfx, int16_t cx, int16_t cy) {
  const int16_t w = 60, h = 52, r = 10;
  gfx.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, K_BLACK);
  gfx.setFont(&HelveticaBold11pt7b);
  gfx.setTextColor(K_WHITE);
  const char *lines[2] = {"B25", "B26"};
  const int16_t centers[2] = {(int16_t)(cy - 12), (int16_t)(cy + 12)};
  for (int i = 0; i < 2; i++) {
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    gfx.getTextBounds(lines[i], 0, 0, &tbx, &tby, &tbw, &tbh);
    gfx.setCursor(cx - tbw / 2 - tbx, centers[i] - tbh / 2 - tby);
    gfx.print(lines[i]);
  }
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

  // bus rows carry the shared B25/B26 badge in the same column
  drawBusBadge(gfx, 42, ROW_BASELINES[2] - 18);
  drawBusBadge(gfx, 42, ROW_BASELINES[3] - 18);

  printArrivalRow(gfx, "Manhattan", northRow, ROW_BASELINES[0]);
  printArrivalRow(gfx, "Euclid", southRow, ROW_BASELINES[1]);
  printArrivalRow(gfx, "Downtown", busWestRow, ROW_BASELINES[2]);
  printArrivalRow(gfx, "Eastbound", busEastRow, ROW_BASELINES[3]);
}

void renderMessage(Adafruit_GFX &gfx, const char *msg) {
  gfx.fillScreen(K_WHITE);
  gfx.setTextColor(K_BLACK);
  gfx.setFont(&HelveticaBold26pt7b);
  gfx.setCursor(14, 140);
  gfx.print(msg);
}

// Commute mode: nothing but a giant route bullet and the minutes until the
// next three trains ("7,12,19"), sized for the 792x272 panel. The 64pt face
// is the biggest a GFXfont can hold (int8_t glyph offsets); at 1x the worst
// case "22,28,34" still fits beside the bullet.
void renderCommute(Adafruit_GFX &gfx, const char *routeLetter,
                   const char *minutesText) {
  gfx.fillScreen(K_WHITE);
  gfx.setTextColor(K_BLACK);

  const int16_t cy = gfx.height() / 2; // 136
  // r=105 keeps the 85pt letter at the same ~0.60 letter/disc height
  // ratio as the small arrival-row bullets
  const int16_t bulletR = 105;
  const int16_t bulletCx = 20 + bulletR;

  // route bullet at nearly full panel height, letter knocked out in white
  gfx.fillCircle(bulletCx, cy, bulletR, K_BLACK);
  gfx.setFont(&HelveticaBold85pt7b);
  gfx.setTextColor(K_WHITE);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  gfx.getTextBounds(routeLetter, 0, 0, &tbx, &tby, &tbw, &tbh);
  gfx.setCursor(bulletCx - tbw / 2 - tbx, cy - tbh / 2 - tby);
  gfx.print(routeLetter);
  gfx.setTextColor(K_BLACK);

  const char *text = minutesText[0] ? minutesText : "-";

  // minute list (up to "22,28,34") centered between bullet and right margin.
  // Vertical centering uses digit metrics only — commas descend below the
  // baseline, and including them in the box pushes the digits above center.
  gfx.setFont(&HelveticaBold64pt7b);
  gfx.getTextBounds("0", 0, 0, &tbx, &tby, &tbw, &tbh);
  int16_t baseline = cy - (int16_t)tbh / 2 - tby;
  gfx.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
  int16_t left = bulletCx + bulletR;
  int16_t right = gfx.width() - 20;
  gfx.setCursor(left + (right - left - (int16_t)tbw) / 2 - tbx, baseline);
  gfx.print(text);
}
