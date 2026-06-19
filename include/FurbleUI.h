#ifndef FURBLE_UI_H
#define FURBLE_UI_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include <lvgl.h>

#include "FurbleControl.h"
#include "WaveshareEPaper154.h"

namespace Furble {

class Camera;

class UI {
 public:
  UI();
  void task();

 private:
  enum class State { HOME, SCANNING, CAMERA_LIST, CONNECTING, REMOTE, GPS, MESSAGE, CONFIRM };
  enum class LineStyle { PLAIN, CARD, ACTIVE };
  enum class ConfirmAction { NONE, DISCONNECT, SLEEP };

  struct HitRect {
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;
  };

  static constexpr int WIDTH = 200;
  static constexpr int HEIGHT = 200;
  static constexpr int DRAW_LINES = 20;
  static constexpr size_t BUFFER_SIZE = WIDTH * DRAW_LINES * sizeof(uint16_t);
  static constexpr size_t LINE_COUNT = 6;
  static constexpr uint32_t SCAN_MS = 30000;  // Match pairing scan 30s duration
  static constexpr uint32_t SCAN_RENDER_MS = 2000;
  static constexpr uint32_t CONNECT_RENDER_MS = 1000;
  static constexpr uint32_t GPS_RENDER_MS = 1000;
  static constexpr uint32_t MESSAGE_MS = 2200;
  static constexpr uint32_t BATTERY_RENDER_MS = 30000;
  static constexpr uint32_t INACTIVITY_SLEEP_MS = 5 * 60 * 1000;
  static constexpr int LOW_BATTERY_PERCENT = 15;
  static constexpr int CRITICAL_BATTERY_PERCENT = 5;

  static uint32_t tickCb();
  static void displayFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
  static void scanCallback(void *param);

  void createScreen();
  void render(const char *eventText = nullptr);
  void renderHome();
  void renderScanning();
  void renderCameraList();
  void renderConnecting();
  void renderRemote();
  void renderGPS();
  void renderMessage();
  void renderConfirm();
  void renderSleep(const char *reason);
  void drawSleepLogo();

  void handleButtons();
  void handleTouch();
  void handleHomeButton(bool rec, bool pwr, bool recBack, bool pwrLong);
  void handleScanningButton(bool recBack, bool pwrLong);
  void handleListButton(bool rec, bool pwr, bool recBack, bool pwrLong);
  void handleConnectingButton(bool recBack, bool pwrLong);
  void handleRemoteButton(bool rec, bool pwr, bool recBack, bool pwrDouble, bool pwrLong);
  void handleGPSButton(bool rec, bool pwr, bool recBack, bool pwrLong);
  void handleConfirmButton(bool rec, bool pwr, bool recBack, bool pwrLong);
  void handleHomeTouch(const WaveshareEPaper154::TouchEvent &event);
  void handleScanningTouch(const WaveshareEPaper154::TouchEvent &event);
  void handleListTouch(const WaveshareEPaper154::TouchEvent &event);
  void handleConnectingTouch(const WaveshareEPaper154::TouchEvent &event);
  void handleRemoteTouch(const WaveshareEPaper154::TouchEvent &event);
  void handleGPSTouch(const WaveshareEPaper154::TouchEvent &event);
  void handleConfirmTouch(const WaveshareEPaper154::TouchEvent &event);

  void startScan();
  void stopScan();
  void goHome();
  void showSavedList();
  void connectSelectedCamera();
  void activateHomeAction(size_t index, bool cameraConnected);
  void pollScan();
  void pollConnection();
  void pollGPS();
  void pollPower();
  void resetActivity();
  void prepareSleep(const char *reason);
  void sendPulse(Control::cmd_t press, Control::cmd_t release);
  void disconnectAndHome(const char *message);
  void showConfirm(const char *prompt, ConfirmAction action, State backState);
  void runConfirmAction();
  void showMessage(const char *message, State next = State::HOME);
  void showMessage(const std::string &message, State next = State::HOME);
  void setTitleBar(const char *title, bool inverted = true);
  void setMenuHeader();
  void layoutRows(int startY, int step, int cardHeight);
  void layoutGrid(size_t startIndex, size_t count, int startX, int startY, int cardWidth, int cardHeight, int gapX,
                  int gapY);
  void setLine(size_t index, const char *text);
  void setLine(size_t index, const std::string &text);
  void setLineStyle(size_t index, LineStyle style, lv_text_align_t align = LV_TEXT_ALIGN_LEFT);
  void clearLineStyles(bool resetLayout = true);
  int hitLine(const WaveshareEPaper154::TouchEvent &event) const;
  bool isHeaderTap(const WaveshareEPaper154::TouchEvent &event) const;
  std::string batteryText() const;
  void updateBatteryCache(bool force = false);
  std::string cameraLabel(size_t index) const;
  const char *remotePowerLabel() const;
  const char *controlStateName(Control::state_t state) const;

  lv_display_t *m_display = nullptr;
  void *m_buffer1 = nullptr;
  void *m_buffer2 = nullptr;

  lv_obj_t *m_title = nullptr;
  lv_obj_t *m_headerRight = nullptr;
  lv_obj_t *m_headerLine = nullptr;
  std::array<lv_obj_t *, LINE_COUNT> m_cards = {};
  std::array<lv_obj_t *, LINE_COUNT> m_lines = {};
  std::array<HitRect, LINE_COUNT> m_lineRects = {};
  std::array<bool, LINE_COUNT> m_lineTouchable = {};
  lv_obj_t *m_footer = nullptr;

  State m_state = State::HOME;
  State m_afterMessage = State::HOME;
  State m_beforeConfirm = State::HOME;
  ConfirmAction m_confirmAction = ConfirmAction::NONE;
  size_t m_homeCursor = 0;
  size_t m_listCursor = 0;
  bool m_listFromScan = false;
  bool m_pendingSave = false;
  Camera *m_selectedCamera = nullptr;

  std::atomic<size_t> m_scanHits {0};
  uint32_t m_scanStartedMs = 0;
  uint32_t m_lastRenderMs = 0;
  uint32_t m_lastGpsRenderMs = 0;
  uint32_t m_messageUntilMs = 0;

  int m_batteryPercent = -1;
  float m_batteryVoltage = -1.0f;
  uint32_t m_lastBatteryMs = 0;
  uint32_t m_lastActivityMs = 0;
  bool m_lowBatteryWarned = false;
  bool m_sleeping = false;
  bool m_renderDirty = true;
  std::string m_message;
  std::string m_confirmPrompt;
};

}  // namespace Furble

#endif
