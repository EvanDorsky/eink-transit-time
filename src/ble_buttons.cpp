#include "ble_buttons.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <map>

// Must match ~/src/ble-button/src/main.cpp
static const char *BUTTON_SERVICE_UUID = "b6f5d0a0-6f21-4f2e-9e6b-1b7a3c5d9e10";

// Buttons are fire-and-forget beacons: deep asleep until pressed, then a
// short burst of non-connectable advertisements carrying a press counter,
// then back to sleep. We never connect — just listen. A press is "the
// counter for this address changed" (or a button we've never heard),
// which dedups the ~15 copies of each burst.
static std::function<void(const char *, uint32_t)> pressHandler;

// peer address -> last seen press counter + when (only touched on the host
// task). Dedup is time-boxed: the counter repeats within one ~2s burst, but
// it can also legitimately repeat across boots (the button's RTC counter
// resets on power loss or reset-button boots), so an identical counter seen
// well after the burst ended is a new press, not a duplicate.
struct Seen {
  uint32_t count;
  uint32_t ms;
};
static std::map<uint64_t, Seen> lastSeen;
static const uint32_t BURST_DEDUP_MS = 10000;

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    if (!dev->isAdvertisingService(NimBLEUUID(BUTTON_SERVICE_UUID))) return;

    // Manufacturer data: 0xFFFF (test company id) + uint32 LE press count
    std::string mfg = dev->getManufacturerData();
    if (mfg.size() < 6 || (uint8_t)mfg[0] != 0xFF || (uint8_t)mfg[1] != 0xFF)
      return;
    uint32_t count;
    memcpy(&count, mfg.data() + 2, 4);

    uint64_t key = uint64_t(dev->getAddress());
    uint32_t now = millis();
    auto it = lastSeen.find(key);
    if (it != lastSeen.end() && it->second.count == count &&
        now - it->second.ms < BURST_DEDUP_MS) {
      it->second.ms = now; // still the same burst
      return;
    }

    lastSeen[key] = {count, now};
    if (pressHandler)
      pressHandler(dev->getAddress().toString().c_str(), count);
  }

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    NimBLEDevice::getScan()->start(10000);
  }
};
static ScanCallbacks scanCallbacks;

// If a scan restart ever fails (e.g. momentary resource pressure), onScanEnd
// won't fire again and listening would die silently — this watchdog revives it
static void scanWatchdog(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (!scan->isScanning()) {
      Serial.println("[ble] scan stopped, restarting");
      scan->start(10000);
    }
  }
}

void bleButtonsBegin(std::function<void(const char *, uint32_t)> onPress) {
  pressHandler = onPress;
  NimBLEDevice::init("");

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, false);
  scan->setActiveScan(false);        // beacons don't do scan responses
  scan->setDuplicateFilter(false);   // repeat presses reuse the same packet
  // 15% duty: WiFi/HomeKit needs the airtime under coex, and the button's
  // 2s advertising burst gives us ~6 windows to catch at least one packet
  scan->setInterval(300);
  scan->setWindow(45);
  scan->start(10000);
  xTaskCreate(scanWatchdog, "bleScanWdt", 2048, nullptr, 1, nullptr);
  Serial.println("[ble] listening for button beacons");
}
