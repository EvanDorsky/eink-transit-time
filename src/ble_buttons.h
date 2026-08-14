#pragma once

#include <cstdint>
#include <functional>

// BLE central that discovers, connects to, and stays connected to any number
// of ble-button peripherals (XIAO firmware in ~/src/ble-button). Runs on its
// own FreeRTOS task; onPress fires on the NimBLE host task, so keep it short
// and thread-safe (e.g. bump an atomic and handle it elsewhere).
void bleButtonsBegin(std::function<void(const char *addr, uint32_t count)> onPress);
