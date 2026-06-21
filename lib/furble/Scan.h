#ifndef SCAN_H
#define SCAN_H

#include <vector>

#include <NimBLEScan.h>

#include "CameraList.h"
#include "FurbleTypes.h"

#ifndef FURBLE_VERSION
#define FURBLE_VERSION "unknown"
#endif

namespace Furble {
/**
 * BLE advertisement scanning class.
 *
 * Works in conjunction with Furble::Device class.
 */
class Scan: public NimBLEScanCallbacks {
 public:
  // Power optimization: scan mode ownership tracking (D2)
  enum class Mode : uint8_t {
    NONE = 0,
    PAIRING,
  };

  static Scan &getInstance(void);

  Scan(Scan const &) = delete;
  Scan(Scan &&) = delete;
  Scan &operator=(Scan const &) = delete;
  Scan &operator=(Scan &&) = delete;

  /**
   * Start pairing scan: 30s active scan with full duty cycle in 100ms windows.
   * For user-initiated camera discovery.
   */
  void startPairingScan(std::function<void(void *)> scanCallback, void *scanPrivateData);

  /**
   * Start the scan for BLE advertisements with a callback function when a
   * matching reseult is encountered.
   */
  void start(std::function<void(void *)> scanCallback, void *scanResultPrivateData);


  /**
   * Stop the scan.
   */
  void stop(void);

  /**
   * Scanning is active.
   */
  bool isActive(void) const;

  /**
   * Get current scan mode.
   */
  Mode getMode(void) const;

  /**
   * Clear the scan list.
   */
  void clear(void);

  void onResult(const NimBLEAdvertisedDevice *pDevice) override;

 private:
  Scan() {};

  static constexpr uint16_t HID_GENERIC_REMOTE = 0x180;

  NimBLEServer *m_Server = nullptr;
  NimBLEAdvertising *m_Advertising = nullptr;
  NimBLEScan *m_Scan = nullptr;
  std::function<void(void *)> m_ScanResultCallback;
  void *m_ScanResultPrivateData = nullptr;

  // Power optimization: scan mode and parameter save/restore
  Mode m_Mode = Mode::NONE;
  bool m_lockAcquired = false;  // independent of mode — prevents lock leak
};

}  // namespace Furble

#endif
