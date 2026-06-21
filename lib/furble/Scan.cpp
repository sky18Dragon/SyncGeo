#include <NimBLEAdvertisedDevice.h>
#include <NimBLEScan.h>

#include "Device.h"
#include "FurblePlatform.h"
#include "Scan.h"

// log tag
const char *LOG_TAG = FURBLE_STR;

namespace Furble {

Scan &Scan::getInstance(void) {
  static Scan instance;

  if (instance.m_Scan == nullptr) {
    instance.m_Server = NimBLEDevice::createServer();
    instance.m_Advertising = instance.m_Server->getAdvertising();
    instance.m_Advertising->setName(FURBLE_STR);
    instance.m_Advertising->setAppearance(HID_GENERIC_REMOTE);
    instance.m_Advertising->enableScanResponse(true);

    instance.m_Scan = NimBLEDevice::getScan();
    instance.m_Scan->setActiveScan(true);
    instance.m_Scan->setInterval(6553);
    instance.m_Scan->setWindow(6553);
  }

  return instance;
}

/**
 * BLE Advertisement callback.
 */
void Scan::onResult(const NimBLEAdvertisedDevice *pDevice) {
  if (CameraList::match(pDevice)) {
    ESP_LOGI(LOG_TAG, "RSSI(%s) = %d", pDevice->getName().c_str(), pDevice->getRSSI());
    if (m_ScanResultCallback != nullptr) {
      (m_ScanResultCallback)(m_ScanResultPrivateData);
    }
  }
};

void Scan::startPairingScan(std::function<void(void *)> scanCallback, void *scanPrivateData) {
  // D2: 30s active scan, interval=160, window=160 — 100% duty cycle in 100ms windows
  // Guard against nested scans — stop any existing scan first
  if (m_lockAcquired) stop();

  ESP_LOGI(LOG_TAG, "Scan: pairing mode (active, 30s, interval=160, window=160)");
  m_Mode = Mode::PAIRING;
  m_Advertising->start(0, nullptr);
  m_Scan->setScanCallbacks(this);
  m_Scan->setActiveScan(true);
  m_Scan->setDuplicateFilter(false);
  m_Scan->setInterval(160);
  m_Scan->setWindow(160);

  m_ScanResultCallback = scanCallback;
  m_ScanResultPrivateData = scanPrivateData;
  m_Scan->start(30, false);

  Platform::getInstance().acquire(Platform::PowerLock::BLE_SCAN);
  m_lockAcquired = true;
}

void Scan::start(std::function<void(void *)> scanCallback, void *scanPrivateData) {
  if (m_lockAcquired) stop();

  m_Mode = Mode::NONE;
  m_Advertising->start(0, nullptr);
  m_Scan->setScanCallbacks(this);

  m_ScanResultCallback = scanCallback;
  m_ScanResultPrivateData = scanPrivateData;
  m_Scan->start(0, false);

  Platform::getInstance().acquire(Platform::PowerLock::BLE_SCAN);
  m_lockAcquired = true;
}

void Scan::stop(void) {
  m_Advertising->stop();
  m_Scan->stop();
  m_ScanResultPrivateData = nullptr;
  m_ScanResultCallback = nullptr;
  m_Mode = Mode::NONE;
  // Release BLE_SCAN lock independently of mode to prevent lock leak
  if (m_lockAcquired) {
    Platform::getInstance().release(Platform::PowerLock::BLE_SCAN);
    m_lockAcquired = false;
  }
}

bool Scan::isActive(void) const {
  return m_Scan->isScanning();
}

Scan::Mode Scan::getMode(void) const {
  return m_Mode;
}

void Scan::clear(void) {
  m_Scan->clearResults();
}
}  // namespace Furble
