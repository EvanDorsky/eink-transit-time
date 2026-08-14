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

// peer address -> last seen press counter (only touched on the host task)
static std::map<uint64_t, uint32_t> lastCounter;

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
    auto it = lastCounter.find(key);
    if (it != lastCounter.end() && it->second == count) return; // same burst

    lastCounter[key] = count;
    if (pressHandler)
      pressHandler(dev->getAddress().toString().c_str(), count);
  }

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    NimBLEDevice::getScan()->start(10000);
  }
};
static ScanCallbacks scanCallbacks;

void bleButtonsBegin(std::function<void(const char *, uint32_t)> onPress) {
  pressHandler = onPress;
  NimBLEDevice::init("");

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, false);
  scan->setActiveScan(false);        // beacons don't do scan responses
  scan->setDuplicateFilter(false);   // repeat presses reuse the same packet
  scan->setInterval(100);
  scan->setWindow(60); // 60% duty: reliable catch, leaves airtime for WiFi
  scan->start(10000);
  Serial.println("[ble] listening for button beacons");
}
