#pragma once
#include <Adafruit_GFX.h>

// Pure drawing code, shared between the firmware (GxEPD2 panel) and the
// host-side preview tool (GFXcanvas1). Must not depend on anything but
// Adafruit_GFX.
void renderArrivals(Adafruit_GFX &gfx, const char *routeLetter,
                    const char *northRow, const char *southRow,
                    const char *busWestRow, const char *busEastRow);
void renderMessage(Adafruit_GFX &gfx, const char *msg);
