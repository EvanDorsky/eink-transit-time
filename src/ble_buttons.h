#pragma once

#include <cstdint>
#include <functional>

// Passive BLE scanner for ble-button beacons (XIAO firmware in
// ~/src/ble-button): buttons deep-sleep until pressed, then broadcast a
// short advertisement burst with a press counter. No connections are made.
// onPress fires on the NimBLE host task, so keep it short and thread-safe
// (e.g. bump an atomic and handle it elsewhere).
void bleButtonsBegin(std::function<void(const char *addr, uint32_t count)> onPress);

// ---- SP-1 beacon remote (~/src/beacon-sp-1) ----
//
// Wire format: beacon-sp-1/bluetooth/broadcast-format.md. The CYW20706 module
// app broadcasts non-connectable manufacturer-specific data: company 0xFFFF +
// a 9-byte state payload (version=1, seq, buttons u16 LE, 4 faders u8, battery
// u8). No service UUID; the state is in the PRIMARY advertisement.
//
// The SP-1 is not a heartbeat: it advertises only in a ~short burst on activity
// (button/fader), then powers the radio down. Every advert carries the ABSOLUTE
// full state; seq bumps on each significant change and repeats on keepalives.

// Button bit indices in Sp1State.buttons (bit i set = button i held).
enum {
  SP1_BTN_PLAY = 0,
  SP1_BTN_TRACK1 = 1,
  SP1_BTN_TRACK2 = 2,
  SP1_BTN_TRACK3 = 3,
  SP1_BTN_TRACK4 = 4,
  SP1_BTN_VOL_UP = 5,
  SP1_BTN_VOL_DN = 6,
  SP1_BTN_FWD = 7,
  SP1_BTN_RWD = 8,
  SP1_BTN_COUNT = 9,
  // (the "••" function/power button is local-only and is NOT broadcast)
};

struct Sp1State {
  uint8_t seq;       // increments on each significant change; wraps mod 256
  uint16_t buttons;  // bit i = button i held (SP1_BTN_*)
  uint8_t fader[4];  // 0..255
  uint8_t battery;   // 0..100, or 0xFF = not yet measured
};

// Fires once per NEW state (deduped by seq) on the NimBLE host task:
//   pressed  = buttons that went 0->1 vs the previous received state,
//   released = buttons that went 1->0.
// Because BLE is lossy and the payload is absolute, a press+release inside a
// gap of missed adverts can be missed; a burst sends many copies so it's rare.
// Keep the callback short and thread-safe (bump an atomic; act on the HomeSpan
// poll task). Call before bleButtonsBegin so the scanner is configured in time.
void bleSp1Begin(std::function<void(const Sp1State &st, uint16_t pressed,
                                    uint16_t released)>
                     onState);
