// Minimal Arduino shim so Adafruit_GFX compiles on the host for the
// preview tool. Only what GFX actually uses.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef bool boolean;
typedef uint8_t byte;

#define PROGMEM
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define DEG_TO_RAD 0.017453292519943295769236907684886
#define radians(deg) ((deg) * DEG_TO_RAD)

static inline void yield() {}

class __FlashStringHelper;

#include <string>
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string &s) : std::string(s) {}
};
