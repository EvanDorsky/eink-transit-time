// Minimal Print shim for host builds; just enough for Adafruit_GFX text.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *buffer, size_t size) {
    size_t n = 0;
    while (size--)
      if (write(*buffer++)) n++;
      else break;
    return n;
  }
  size_t write(const char *str) {
    return str ? write((const uint8_t *)str, strlen(str)) : 0;
  }
  size_t print(const char *str) { return write(str); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t println(const char *str) { return write(str) + write((uint8_t)'\n'); }
};
