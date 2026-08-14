#include "ble_buttons.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <deque>

// Must match ~/src/ble-button/src/main.cpp
static const char *BUTTON_SERVICE_UUID = "b6f5d0a0-6f21-4f2e-9e6b-1b7a3c5d9e10";
static const char *BUTTON_CHAR_UUID = "b6f5d0a1-6f21-4f2e-9e6b-1b7a3c5d9e10";

static std::function<void(const char *, uint32_t)> pressHandler;

// Buttons seen by the scan but not yet connected. Filled from the scan
// callback (NimBLE host task), drained by the worker task — connecting from
// inside the scan callback itself is not supported.
static std::deque<NimBLEAddress> pendingConnects;
static SemaphoreHandle_t pendingMutex;

static bool isPending(const NimBLEAddress &addr) {
  for (const auto &a : pendingConnects)
    if (a == addr) return true;
  return false;
}

static void onNotify(NimBLERemoteCharacteristic *chr, uint8_t *data,
                     size_t len, bool isNotify) {
  if (len < 4) return;
  uint32_t count;
  memcpy(&count, data, 4);
  if (pressHandler)
    pressHandler(chr->getClient()->getPeerAddress().toString().c_str(), count);
}

class ClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *client, int reason) override {
    Serial.printf("[ble] button %s disconnected (reason %d)\n",
                  client->getPeerAddress().toString().c_str(), reason);
    // Worker task keeps the scan running; the button re-advertises and
    // gets picked up again like a new device.
  }
};
static ClientCallbacks clientCallbacks;

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    if (!dev->isAdvertisingService(NimBLEUUID(BUTTON_SERVICE_UUID))) return;

    NimBLEAddress addr = dev->getAddress();
    NimBLEClient *existing = NimBLEDevice::getClientByPeerAddress(addr);
    if (existing && existing->isConnected()) return;

    xSemaphoreTake(pendingMutex, portMAX_DELAY);
    if (!isPending(addr)) {
      pendingConnects.push_back(addr);
      Serial.printf("[ble] found button %s (%s)\n", addr.toString().c_str(),
                    dev->getName().c_str());
    }
    xSemaphoreGive(pendingMutex);
  }

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    // Short sessions: each restart clears the duplicate cache, so a button
    // that changed state (rebooted, reflashed) is reported again
    NimBLEDevice::getScan()->start(10000);
  }
};
static ScanCallbacks scanCallbacks;

static bool connectButton(const NimBLEAddress &addr) {
  NimBLEClient *client = NimBLEDevice::getClientByPeerAddress(addr);
  if (!client) client = NimBLEDevice::getDisconnectedClient();
  if (!client) {
    if (NimBLEDevice::getCreatedClientCount() >=
        CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
      Serial.println("[ble] max connections reached, ignoring new button");
      return false;
    }
    client = NimBLEDevice::createClient();
  }
  client->setClientCallbacks(&clientCallbacks, false);
  client->setConnectTimeout(5000);

  if (!client->connect(addr)) {
    Serial.printf("[ble] connect to %s failed\n", addr.toString().c_str());
    return false;
  }

  NimBLERemoteService *service = client->getService(BUTTON_SERVICE_UUID);
  NimBLERemoteCharacteristic *chr =
      service ? service->getCharacteristic(BUTTON_CHAR_UUID) : nullptr;
  if (!chr || !chr->canNotify() || !chr->subscribe(true, onNotify)) {
    Serial.printf("[ble] %s lacks button characteristic, dropping\n",
                  addr.toString().c_str());
    client->disconnect();
    return false;
  }

  Serial.printf("[ble] button %s connected and subscribed\n",
                addr.toString().c_str());
  return true;
}

static void bleWorker(void *) {
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(200);
  scan->setWindow(60); // modest duty cycle, leaves airtime for WiFi
  scan->start(10000);
  Serial.println("[ble] scanning for buttons");

  for (;;) {
    NimBLEAddress addr;
    bool have = false;
    xSemaphoreTake(pendingMutex, portMAX_DELAY);
    if (!pendingConnects.empty()) {
      addr = pendingConnects.front();
      pendingConnects.pop_front();
      have = true;
    }
    xSemaphoreGive(pendingMutex);

    if (have) {
      scan->stop();
      connectButton(addr);
    }
    if (!scan->isScanning()) scan->start(10000);
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void bleButtonsBegin(std::function<void(const char *, uint32_t)> onPress) {
  pressHandler = onPress;
  pendingMutex = xSemaphoreCreateMutex();
  NimBLEDevice::init("");
  xTaskCreate(bleWorker, "bleButtons", 4096, nullptr, 1, nullptr);
}
