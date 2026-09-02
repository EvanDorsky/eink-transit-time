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


// ---------------- weather ----------------
//
// Icons are drawn with GFX primitives rather than bitmaps: on a 1-bit panel
// simple filled shapes read better at this size than dithered art, and it
// keeps the flash cost at zero.

static const int16_t WX_COL_W = 132; // right column reserved in renderArrivals

static void wxSun(Adafruit_GFX &g, int16_t cx, int16_t cy, int16_t r) {
  g.fillCircle(cx, cy, r, K_BLACK);
  for (int i = 0; i < 8; i++) {
    float a = i * 0.7853981634f; // pi/4
    float c = cosf(a), s2 = sinf(a);
    int16_t x0 = cx + (int16_t)((r + 3) * c), y0 = cy + (int16_t)((r + 3) * s2);
    int16_t x1 = cx + (int16_t)((r + 8) * c), y1 = cy + (int16_t)((r + 8) * s2);
    g.drawLine(x0, y0, x1, y1, K_BLACK);
    g.drawLine(x0 + 1, y0, x1 + 1, y1, K_BLACK); // 2px stroke
  }
}

static void wxMoon(Adafruit_GFX &g, int16_t cx, int16_t cy, int16_t r) {
  g.fillCircle(cx, cy, r, K_BLACK);
  g.fillCircle(cx + r / 2 + 2, cy - r / 3, r, K_WHITE); // bite out a crescent
}

// Cloud body; cx/cy is the centre of the flat underside
static void wxCloud(Adafruit_GFX &g, int16_t cx, int16_t cy) {
  g.fillCircle(cx - 11, cy - 5, 8, K_BLACK);
  g.fillCircle(cx + 1, cy - 10, 11, K_BLACK);
  g.fillCircle(cx + 13, cy - 5, 8, K_BLACK);
  g.fillRect(cx - 11, cy - 6, 25, 7, K_BLACK);
}

static void wxDrops(Adafruit_GFX &g, int16_t cx, int16_t cy, int n) {
  for (int i = 0; i < n; i++) {
    int16_t x = cx - 12 + i * 11;
    g.drawLine(x + 3, cy, x - 1, cy + 8, K_BLACK);
    g.drawLine(x + 4, cy, x, cy + 8, K_BLACK);
  }
}

static void wxFlakes(Adafruit_GFX &g, int16_t cx, int16_t cy, int n) {
  for (int i = 0; i < n; i++) {
    int16_t x = cx - 12 + i * 11, y = cy + 4;
    g.drawLine(x - 3, y, x + 3, y, K_BLACK);
    g.drawLine(x, y - 3, x, y + 3, K_BLACK);
    g.drawLine(x - 2, y - 2, x + 2, y + 2, K_BLACK);
    g.drawLine(x - 2, y + 2, x + 2, y - 2, K_BLACK);
  }
}

static void wxBolt(Adafruit_GFX &g, int16_t cx, int16_t cy) {
  g.fillTriangle(cx + 4, cy - 1, cx - 6, cy + 11, cx + 1, cy + 10, K_BLACK);
  g.fillTriangle(cx + 4, cy - 1, cx + 1, cy + 10, cx + 7, cy + 3, K_BLACK);
}

static void wxLines(Adafruit_GFX &g, int16_t cx, int16_t cy, bool hook) {
  for (int i = 0; i < 3; i++) {
    int16_t y = cy - 8 + i * 8;
    int16_t half = (i == 1) ? 16 : 12;
    g.fillRect(cx - half, y, half * 2, 3, K_BLACK);
    if (hook) g.fillRect(cx + half - 2, y - 4, 3, 8, K_BLACK);
  }
}

// HA weather state string -> icon. Unknown conditions fall back to a cloud.
static void drawWeatherIcon(Adafruit_GFX &gfx, const char *cond, int16_t cx,
                            int16_t cy) {
  bool night = strcmp(cond, "clear-night") == 0;
  if (strcmp(cond, "sunny") == 0) { wxSun(gfx, cx, cy, 11); return; }
  if (night) { wxMoon(gfx, cx, cy, 13); return; }
  if (strcmp(cond, "partlycloudy") == 0) {
    wxSun(gfx, cx + 8, cy - 9, 7);
    wxCloud(gfx, cx - 2, cy + 10);
    return;
  }
  if (strcmp(cond, "fog") == 0) { wxLines(gfx, cx, cy, false); return; }
  if (strcmp(cond, "windy") == 0 || strcmp(cond, "windy-variant") == 0) {
    wxLines(gfx, cx, cy, true);
    return;
  }
  if (strcmp(cond, "exceptional") == 0) {
    gfx.fillRect(cx - 3, cy - 14, 6, 18, K_BLACK);
    gfx.fillRect(cx - 3, cy + 8, 6, 6, K_BLACK);
    return;
  }
  // everything else is cloud-based
  wxCloud(gfx, cx, cy + 2);
  if (strcmp(cond, "rainy") == 0) wxDrops(gfx, cx, cy + 5, 3);
  else if (strcmp(cond, "pouring") == 0) wxDrops(gfx, cx, cy + 5, 4);
  else if (strcmp(cond, "snowy") == 0) wxFlakes(gfx, cx, cy + 4, 3);
  else if (strcmp(cond, "snowy-rainy") == 0) {
    wxDrops(gfx, cx - 6, cy + 5, 1);
    wxFlakes(gfx, cx + 6, cy + 4, 1);
  } else if (strcmp(cond, "hail") == 0) {
    for (int i = 0; i < 3; i++)
      gfx.fillCircle(cx - 11 + i * 11, cy + 9, 3, K_BLACK);
  } else if (strcmp(cond, "lightning") == 0 ||
             strcmp(cond, "lightning-rainy") == 0)
    wxBolt(gfx, cx, cy + 4);
}

// Icon above a "lo-hi" range, right-aligned so the block hugs the corner.
// Returns nothing; safe to call with a null/invalid WeatherInfo.
static void drawWeather(Adafruit_GFX &gfx, const WeatherInfo *wx, int16_t right,
                        int16_t top) {
  if (!wx || !wx->valid) return;
  char temps[16];
  snprintf(temps, sizeof(temps), "%d-%d", wx->lo, wx->hi);

  gfx.setFont(&HelveticaBold14pt7b);
  gfx.setTextColor(K_BLACK);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  gfx.getTextBounds(temps, 0, 0, &tbx, &tby, &tbw, &tbh);

  const int16_t degR = 3;
  int16_t total = (int16_t)tbw + degR * 2 + 4;
  int16_t cx = right - total / 2;

  drawWeatherIcon(gfx, wx->cond, cx, top + 20);

  int16_t baseline = top + 44 + (int16_t)tbh;
  gfx.setCursor(right - total - tbx, baseline);
  gfx.print(temps);
  // degree ring, drawn rather than typed: the font has no degree glyph
  gfx.drawCircle(right - degR, baseline - (int16_t)tbh + degR, degR, K_BLACK);
  gfx.drawCircle(right - degR, baseline - (int16_t)tbh + degR, degR - 1,
                 K_BLACK);
}

// ---------------- side column: rain + notices ----------------
//
// The reserved right column reads top-to-bottom as weather / rain outlook /
// notification. The two lower zones are only drawn in arrivals mode.

static const int16_t RAIN_ZONE_TOP = 92;   // icon+label ~80px tall
static const int16_t NOTE_ZONE_TOP = 190;  // title / bar / text / dots

// Centred text helper for the column; uses the current font
static void printCentered(Adafruit_GFX &gfx, const char *s, int16_t cx,
                          int16_t baseline) {
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  gfx.getTextBounds(s, 0, 0, &tbx, &tby, &tbw, &tbh);
  gfx.setCursor(cx - (int16_t)tbw / 2 - tbx, baseline);
  gfx.print(s);
}

// Umbrella: half-disc canopy with a scalloped edge and a J handle
static void drawUmbrella(Adafruit_GFX &g, int16_t cx, int16_t cy) {
  const int16_t r = 22;
  g.fillCircle(cx, cy, r, K_BLACK);
  g.fillRect(cx - r - 1, cy + 1, r * 2 + 3, r + 2, K_WHITE);
  for (int i = -1; i <= 1; i++) g.fillCircle(cx + i * 15, cy + 4, 5, K_WHITE);
  g.fillRect(cx - 1, cy - r - 4, 3, 5, K_BLACK); // tip
  g.fillRect(cx - 1, cy, 3, 20, K_BLACK);        // shaft
  g.drawCircle(cx - 5, cy + 20, 5, K_BLACK);     // hook (bottom half only)
  g.drawCircle(cx - 5, cy + 20, 4, K_BLACK);
  g.fillRect(cx - 11, cy + 10, 12, 10, K_WHITE);
  g.fillRect(cx - 1, cy, 3, 21, K_BLACK);
}

// Middle zone: umbrella over the hour rain starts ("6pm", or "now");
// nothing at all when the next 12h are dry
static void drawRain(Adafruit_GFX &gfx, const WeatherInfo *wx, int16_t cx) {
  if (!wx || !wx->valid || wx->rainIn < 0) return;
  drawUmbrella(gfx, cx, RAIN_ZONE_TOP + 26);
  const char *label =
      (wx->rainIn == 0 || !wx->rainAt[0]) ? "now" : wx->rainAt;
  gfx.setFont(&HelveticaBold14pt7b);
  gfx.setTextColor(K_BLACK);
  printCentered(gfx, label, cx, RAIN_ZONE_TOP + 76);
}

// Bottom zone: one notice at a time. Title, optional progress bar, detail
// line, then pager dots if there is more than one notice in rotation.
static void drawNotice(Adafruit_GFX &gfx, const Notice *note, int16_t cx,
                       int16_t colW) {
  if (!note || !note->title[0]) return;
  gfx.setTextColor(K_BLACK);
  int16_t y = NOTE_ZONE_TOP + 20;
  gfx.setFont(&HelveticaBold14pt7b);
  printCentered(gfx, note->title, cx, y);
  y += 10;

  if (note->pct >= 0) {
    const int16_t w = colW - 24, h = 12;
    int16_t x0 = cx - w / 2;
    gfx.drawRect(x0, y, w, h, K_BLACK);
    gfx.drawRect(x0 + 1, y + 1, w - 2, h - 2, K_BLACK);
    int16_t fill = (int16_t)((w - 6) * (note->pct > 100 ? 100 : note->pct) / 100);
    if (fill > 0) gfx.fillRect(x0 + 3, y + 3, fill, h - 6, K_BLACK);
    y += h + 6;
  }

  if (note->text[0]) {
    gfx.setFont(&HelveticaBold11pt7b);
    printCentered(gfx, note->text, cx, y + 16);
    y += 22;
  }

  if (note->count > 1) {
    int16_t dy = gfx.height() - 8;
    int16_t x = cx - (note->count - 1) * 5;
    for (int i = 0; i < note->count; i++, x += 10) {
      if (i == note->idx) gfx.fillCircle(x, dy, 3, K_BLACK);
      else gfx.drawCircle(x, dy, 3, K_BLACK);
    }
  }
}

// Left label plus big right-aligned minute numbers with a small "min"
// underneath, like the real countdown clocks
static void printArrivalRow(Adafruit_GFX &gfx, const char *label,
                            const char *nums, int16_t baselineY,
                            int16_t rightMargin) {
  gfx.setFont(&HelveticaBold26pt7b);
  gfx.setCursor(LABEL_X, baselineY);
  gfx.print(label);

  if (!nums[0]) return;
  int16_t tbx, tby;
  uint16_t tbw, tbh;

  // small "min" sits at the right margin on the shared baseline
  gfx.setFont(&HelveticaBold14pt7b);
  gfx.getTextBounds("min", 0, 0, &tbx, &tby, &tbw, &tbh);
  int16_t minX = gfx.width() - rightMargin - tbw - tbx;
  gfx.setCursor(minX, baselineY);
  gfx.print("min");

  gfx.setFont(&HelveticaBold26pt7b);
  gfx.getTextBounds(nums, 0, 0, &tbx, &tby, &tbw, &tbh);
  gfx.setCursor(minX - 8 - tbw - tbx, baselineY);
  gfx.print(nums);
}

void renderArrivals(Adafruit_GFX &gfx, const char *routeNorth,
                    const char *routeSouth, const char *northRow,
                    const char *southRow, const char *busWestRow,
                    const char *busEastRow, const WeatherInfo *wx,
                    const Notice *note) {
  gfx.fillScreen(K_WHITE);
  gfx.setTextColor(K_BLACK);

  // subway rows carry their own route bullet — the two directions can be
  // on different lines around the C's service boundary
  drawRouteBullet(gfx, routeNorth, 42, ROW_BASELINES[0] - 18, 26);
  drawRouteBullet(gfx, routeSouth, 42, ROW_BASELINES[1] - 18, 26);

  // bus rows carry the shared B25/B26 badge in the same column
  drawBusBadge(gfx, 42, ROW_BASELINES[2] - 18);
  drawBusBadge(gfx, 42, ROW_BASELINES[3] - 18);

  // Arrival rows stop short of the reserved weather column when weather is
  // available, and reclaim the full width when it isn't
  int16_t margin = (wx && wx->valid) ? WX_COL_W : 16;
  printArrivalRow(gfx, "Manhattan", northRow, ROW_BASELINES[0], margin);
  printArrivalRow(gfx, "Euclid", southRow, ROW_BASELINES[1], margin);
  printArrivalRow(gfx, "Downtown", busWestRow, ROW_BASELINES[2], margin);
  printArrivalRow(gfx, "Eastbound", busEastRow, ROW_BASELINES[3], margin);

  int16_t right = gfx.width() - 16;
  drawWeather(gfx, wx, right, 8);
  if (wx && wx->valid) {
    int16_t cx = right - (WX_COL_W - 16) / 2;
    drawRain(gfx, wx, cx);
    drawNotice(gfx, note, cx, WX_COL_W - 16);
  }
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
                   const char *minutesText, const WeatherInfo *wx) {
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

  drawWeather(gfx, wx, gfx.width() - 16, 4);
}
