#ifndef CAMERALIST_H
#define CAMERALIST_H

#include <Preferences.h>
#include <memory>

#include "Camera.h"

namespace Furble {

class CameraList {
 public:
  CameraList();
  ~CameraList();
  /**
   * Save camera to connection list.
   */
  static void save(const Furble::Camera *camera);

  /**
   * Remove camera from connection list.
   */
  static void remove(Furble::Camera *camera);

  /**
   * Load previously connected devices.
   */
  static void load(void);

  /**
   * Count saved entries skipped by the last load because BLE bond was missing.
   */
  static size_t getLastLoadSkippedUnbondedCount(void);

  /**
   * Get number of saved connections.
   */
  static size_t getSaveCount(void);

  /**
   * Add matching devices to the list.
   *
   * @return true if device matches
   */
  static bool match(const NimBLEAdvertisedDevice *pDevice);


  /**
   * Number of connectable devices.
   */
  static size_t size(void);

  /**
   * Clear connectable devices.
   */
  static void clear(void);

  /**
   * Get last added entry.
   */
  static Furble::Camera *last(void);

  /**
   * Retrieve device by index.
   */
  static Furble::Camera *get(size_t n);

 private:
  typedef struct {
    char name[16];
    Camera::Type type;
  } index_entry_t;

  static void fillSaveEntry(index_entry_t &entry, const Camera *camera);
  static std::vector<index_entry_t> load_index(void);
  static void save_index(std::vector<index_entry_t> &index);
  static void add_index(std::vector<index_entry_t> &index, index_entry_t &entry);

  /**
   * List of connectable devices.
   */
  static std::vector<std::unique_ptr<Furble::Camera>> m_ConnectList;
  static size_t m_LastLoadSkippedUnbonded;

  static Preferences m_Prefs;
};
}  // namespace Furble

#endif
