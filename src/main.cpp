#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold24pt7b.h>

// CrowPanel 5.79" pinout (dual-SSD1683 panel, GDEY0579T93)
#define EPD_POWER 7
#define EPD_MOSI 11
#define EPD_SCK 12
#define EPD_CS 45
#define EPD_DC 46
#define EPD_RST 47
#define EPD_BUSY 48

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT>
    display(GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

void setup() {
  Serial.begin(115200);

  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, HIGH);
  delay(100);

  // Must run before display.init(); GxEPD2's own SPI.begin() is then a no-op
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.init(115200);
  display.setRotation(0); // landscape, 792x272

  display.setFont(&FreeMonoBold24pt7b);
  display.setTextColor(GxEPD_BLACK);

  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds("hello world", 0, 0, &tbx, &tby, &tbw, &tbh);
  uint16_t x = (display.width() - tbw) / 2 - tbx;
  uint16_t y = (display.height() - tbh) / 2 - tby;

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(x, y);
    display.print("hello world");
  } while (display.nextPage());

  display.hibernate();
  Serial.println("displayed hello world");
}

void loop() {
  delay(1000);
}
