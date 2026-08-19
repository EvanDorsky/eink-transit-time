#include "ble_buttons.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <map>
#include <string>

// Must match ~/src/ble-button/src/main.cpp
static const char *BUTTON_SERVICE_UUID = "b6f5d0a0-6f21-4f2e-9e6b-1b7a3c5d9e10";

// SP-1 beacon remote (~/src/beacon-sp-1, bluetooth/broadcast-format.md): the
// CYW20706 module app broadcasts manufacturer data = company 0xFFFF + a 9-byte
// state payload (version, seq, buttons u16 LE, 4 faders, battery), in the
// PRIMARY advertisement, with NO service UUID. Company 0xFFFF is shared with
// the XIAO ble-button beacons below, so we disambiguate by payload length (11
// bytes incl. company vs the button's 6) + the version byte.
static const uint16_t SP1_COMPANY_ID = 0xFFFF;
static const uint8_t SP1_STATE_VER = 0x01;
static const size_t SP1_STATE_LEN = 9; // payload after the 2 company-id bytes

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

// SP-1 decode + edge state (host task only). Every advert carries the absolute
// full state; we dedup on seq (keepalives repeat it) and diff the button bitmap
// against the last accepted state to turn holds into press/release edges.
static std::function<void(const Sp1State &, uint16_t, uint16_t)> sp1Handler;
static uint16_t sp1PrevButtons = 0;
static int sp1LastSeq = -1;
static bool sp1HaveState = false;

// Decode one advertisement's manufacturer data (which includes the 2 company
// bytes) into st. Returns false if it isn't an SP-1 state packet.
static bool sp1Decode(const std::string &mfr, Sp1State *st) {
  if (mfr.size() < 2 + SP1_STATE_LEN) return false;
  uint16_t company = (uint8_t)mfr[0] | ((uint16_t)(uint8_t)mfr[1] << 8);
  if (company != SP1_COMPANY_ID) return false;
  const uint8_t *p = (const uint8_t *)mfr.data() + 2;
  if (p[0] != SP1_STATE_VER) return false; // version gate (0xFFFF is shared)
  st->seq = p[1];
  st->buttons = (uint16_t)(p[2] | (p[3] << 8));
  st->fader[0] = p[4];
  st->fader[1] = p[5];
  st->fader[2] = p[6];
  st->fader[3] = p[7];
  st->battery = p[8];
  return true;
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    std::string mfr = dev->getManufacturerData();

    // SP-1 beacon remote: full-state manufacturer data (checked first; its
    // 11-byte + version signature can't collide with the 6-byte button beacon).
    Sp1State st;
    if (sp1Decode(mfr, &st)) {
      if (sp1HaveState && (int)st.seq == sp1LastSeq) return; // keepalive/repeat
      uint16_t prev = sp1HaveState ? sp1PrevButtons : 0;
      uint16_t changed = (uint16_t)(prev ^ st.buttons);
      uint16_t pressed = (uint16_t)(changed & st.buttons); // 0 -> 1
      uint16_t released = (uint16_t)(changed & prev);       // 1 -> 0
      sp1PrevButtons = st.buttons;
      sp1LastSeq = st.seq;
      sp1HaveState = true;
      if (sp1Handler) sp1Handler(st, pressed, released);
      return;
    }

    // XIAO ble-button beacons: service UUID + company 0xFFFF + uint32 LE count.
    if (!dev->isAdvertisingService(NimBLEUUID(BUTTON_SERVICE_UUID))) return;
    if (mfr.size() < 6 || (uint8_t)mfr[0] != 0xFF || (uint8_t)mfr[1] != 0xFF)
      return;
    uint32_t count;
    memcpy(&count, mfr.data() + 2, 4);

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

void bleSp1Begin(std::function<void(const Sp1State &, uint16_t, uint16_t)>
                     onState) {
  sp1Handler = onState;
}

void bleButtonsBegin(std::function<void(const char *, uint32_t)> onPress) {
  pressHandler = onPress;
  NimBLEDevice::init("");

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, false);
  // Passive scan: both the SP-1 (manufacturer data) and the ble-button beacons
  // put everything in the PRIMARY advertisement, so we never need to send scan
  // requests — which also frees airtime for WiFi/HomeKit under BT/WiFi coex.
  scan->setActiveScan(false);
  scan->setDuplicateFilter(false); // repeat presses reuse the same packet
  // 60% duty: the display is wall-powered, so scan hard — a ~2s burst is
  // ~20 adverts and at 60% a full-burst miss is vanishingly rare. WiFi coex
  // still preempts windows while TLS fetches are on the air; that (not the
  // duty cycle) is the remaining cause of missed bursts.
  scan->setInterval(100);
  scan->setWindow(60);
  scan->start(10000);
  xTaskCreate(scanWatchdog, "bleScanWdt", 2048, nullptr, 1, nullptr);
  Serial.println("[ble] listening for button beacons");
}
