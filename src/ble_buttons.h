#pragma once

#include <cstdint>
#include <functional>

// Passive BLE scanner for ble-button beacons (XIAO firmware in
// ~/src/ble-button): buttons deep-sleep until pressed, then broadcast a
// short advertisement burst with a press counter. No connections are made.
// onPress fires on the NimBLE host task, so keep it short and thread-safe
// (e.g. bump an atomic and handle it elsewhere).
void bleButtonsBegin(std::function<void(const char *addr, uint32_t count)> onPress);
