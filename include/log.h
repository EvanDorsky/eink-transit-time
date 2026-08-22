#pragma once

// Defined in main.cpp; logs to USB serial + UDP broadcast :5555
__attribute__((format(printf, 1, 2))) void logLine(const char *fmt, ...);

#define LOGB(fmt, ...) logLine(fmt, ##__VA_ARGS__)
