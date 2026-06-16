#include "FurbleUI.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "Camera.h"
#include "CameraList.h"
#include "FurbleGPS.h"
#include "FurblePlatform.h"
#include "SyncGeoSleepLogo.h"
#include "Scan.h"
#include "WaveshareEPaper154.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

namespace {
constexpr char TAG[] = "ui";
constexpr size_t CAMERA_PAGE_LINES = 4;
enum class HomeAction { SCAN, SAVED, GPS, REMOTE, DISCONNECT, SLEEP };

bool homeCameraConnected() { return Furble::Control::getInstance().getState() == Furble::Control::STATE_ACTIVE; }

size_t homeItemCount(bool cameraConnected) { return cameraConnected ? 6 : 4; }

HomeAction homeActionAt(size_t index, bool cameraConnected) {
  if (!cameraConnected && index >= 3) return HomeAction::SLEEP;
  switch (index) {
    case 0:
      return HomeAction::SCAN;
    case 1:
      return HomeAction::SAVED;
    case 2:
      return HomeAction::GPS;
    case 3:
      return HomeAction::REMOTE;
    case 4:
      return HomeAction::DISCONNECT;
    default:
      return HomeAction::SLEEP;
  }
}

const char *homeActionLabel(HomeAction action) {
  switch (action) {
    case HomeAction::SCAN:
      return "Scan";
    case HomeAction::SAVED:
      return "Cameras";
    case HomeAction::GPS:
      return "GPS";
    case HomeAction::REMOTE:
      return "Remote";
    case HomeAction::DISCONNECT:
      return "Disconnect";
    case HomeAction::SLEEP:
    default:
      return "Sleep";
  }
}

void formatAge(char *buffer, size_t size, uint32_t now, uint32_t timestampMs) {
  if (timestampMs == 0) {
    std::snprintf(buffer, size, "--");
    return;
  }
  const uint32_t seconds = (now - timestampMs) / 1000;
  if (seconds < 100) {
    std::snprintf(buffer, size, "%lus", static_cast<unsigned long>(seconds));
  } else {
    std::snprintf(buffer, size, "%lum", static_cast<unsigned long>(seconds / 60));
  }
}

uint8_t luminanceFromRgb565(uint16_t color) {
  const uint8_t r = static_cast<uint8_t>(((color >> 11) & 0x1F) * 255 / 31);
  const uint8_t g = static_cast<uint8_t>(((color >> 5) & 0x3F) * 255 / 63);
  const uint8_t b = static_cast<uint8_t>((color & 0x1F) * 255 / 31);
  return static_cast<uint8_t>((30 * r + 59 * g + 11 * b) / 100);
}

const char *cameraTypeName(Furble::Camera::Type type) {
  using Type = Furble::Camera::Type;
  switch (type) {
    case Type::FUJIFILM_BASIC:
      return "Fuji";
    case Type::FUJIFILM_SECURE:
      return "FujiS";
    case Type::CANON_EOS_SMART:
      return "CanonS";
    case Type::CANON_EOS_REMOTE:
      return "CanonR";
    case Type::NIKON:
      return "Nikon";
    case Type::SONY:
      return "Sony";
    case Type::RICOH:
      return "Ricoh";
    case Type::FAUXNY:
      return "FauxNY";
    case Type::MOBILE_DEVICE:
    default:
      return "Camera";
  }
}

}  // namespace

namespace Furble {

UI::UI() {
  lv_init();
  lv_tick_set_cb(tickCb);

  m_display = lv_display_create(WIDTH, HEIGHT);
  lv_display_set_default(m_display);
  lv_display_set_color_format(m_display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(m_display, displayFlush);

  m_buffer1 = heap_caps_aligned_alloc(64, BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  m_buffer2 = heap_caps_aligned_alloc(64, BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (m_buffer1 == nullptr || m_buffer2 == nullptr) {
    ESP_LOGE(TAG, "LVGL draw buffer allocation failed");
    abort();
  }
  lv_display_set_buffers(m_display, m_buffer1, m_buffer2, BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  m_lastActivityMs = Platform::getInstance().tick();
  createScreen();
}

uint32_t UI::tickCb() { return Platform::getInstance().tick(); }

void UI::displayFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  const int32_t width = area->x2 - area->x1 + 1;
  const int32_t height = area->y2 - area->y1 + 1;
  const uint16_t *pixels = reinterpret_cast<const uint16_t *>(px_map);
  Platform &platform = Platform::getInstance();

  for (int32_t y = 0; y < height; ++y) {
    const int32_t displayY = area->y1 + y;
    if (displayY < 0 || displayY >= HEIGHT) continue;

    for (int32_t x = 0; x < width; ++x) {
      const int32_t displayX = area->x1 + x;
      if (displayX < 0 || displayX >= WIDTH) continue;

      const uint16_t color = pixels[y * width + x];
      const bool black = luminanceFromRgb565(color) < 160;
      platform.displayDrawPixel(static_cast<uint16_t>(displayX), static_cast<uint16_t>(displayY), black);
    }
  }

  if (lv_display_flush_is_last(disp)) {
    platform.displayRefreshPartial();
  }
  lv_display_flush_ready(disp);
}

void UI::scanCallback(void *param) {
  auto *ui = static_cast<UI *>(param);
  if (ui != nullptr) ui->m_scanHits.fetch_add(1);
}

void UI::createScreen() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
  lv_obj_set_style_text_color(screen, lv_color_black(), 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  m_title = lv_label_create(screen);
  lv_label_set_text(m_title, "SyncGeo");
  lv_obj_set_size(m_title, 200, 28);
  lv_label_set_long_mode(m_title, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(m_title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(m_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(m_title, 5, 0);
  lv_obj_align(m_title, LV_ALIGN_TOP_MID, 0, 0);

  m_headerRight = lv_label_create(screen);
  lv_label_set_text(m_headerRight, "");
  lv_obj_set_width(m_headerRight, 64);
  lv_label_set_long_mode(m_headerRight, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(m_headerRight, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(m_headerRight, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(m_headerRight, LV_ALIGN_TOP_RIGHT, -16, 12);
  lv_obj_add_flag(m_headerRight, LV_OBJ_FLAG_HIDDEN);

  m_headerLine = lv_obj_create(screen);
  lv_obj_set_size(m_headerLine, 168, 1);
  lv_obj_align(m_headerLine, LV_ALIGN_TOP_LEFT, 16, 42);
  lv_obj_clear_flag(m_headerLine, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(m_headerLine, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(m_headerLine, lv_color_black(), 0);
  lv_obj_set_style_border_width(m_headerLine, 0, 0);
  lv_obj_set_style_pad_all(m_headerLine, 0, 0);
  lv_obj_add_flag(m_headerLine, LV_OBJ_FLAG_HIDDEN);

  for (size_t i = 0; i < LINE_COUNT; ++i) {
    const int y = 40 + static_cast<int>(i) * 23;

    m_cards[i] = lv_obj_create(screen);
    lv_obj_set_size(m_cards[i], 168, 21);
    lv_obj_align(m_cards[i], LV_ALIGN_TOP_LEFT, 16, y);
    lv_obj_clear_flag(m_cards[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(m_cards[i], 8, 0);
    lv_obj_set_style_border_width(m_cards[i], 1, 0);
    lv_obj_set_style_pad_all(m_cards[i], 0, 0);
    lv_obj_add_flag(m_cards[i], LV_OBJ_FLAG_HIDDEN);

    m_lines[i] = lv_label_create(screen);
    lv_obj_set_width(m_lines[i], 156);
    lv_label_set_long_mode(m_lines[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(m_lines[i], &lv_font_montserrat_14, 0);
    lv_obj_align(m_lines[i], LV_ALIGN_TOP_LEFT, 22, y + 3);
  }

  m_footer = lv_label_create(screen);
  lv_obj_set_width(m_footer, 192);
  lv_label_set_long_mode(m_footer, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(m_footer, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(m_footer, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(m_footer, LV_ALIGN_BOTTOM_MID, 0, -2);

  render("ready");
}

void UI::setLine(size_t index, const char *text) {
  if (index < LINE_COUNT) {
    lv_label_set_text(m_lines[index], text ? text : "");
    m_lineTouchable[index] = text != nullptr && text[0] != '\0';
  }
}

void UI::setLine(size_t index, const std::string &text) { setLine(index, text.c_str()); }

void UI::setTitleBar(const char *title, bool inverted) {
  (void)inverted;
  lv_obj_clear_flag(m_headerRight, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(m_headerLine, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(m_footer, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(m_footer, "");
  lv_obj_set_size(m_title, 96, 28);
  lv_obj_align(m_title, LV_ALIGN_TOP_LEFT, 16, 12);
  lv_obj_set_style_text_align(m_title, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_pad_top(m_title, 0, 0);
  lv_obj_set_style_pad_left(m_title, 0, 0);
  lv_label_set_text(m_title, title ? title : "");
  lv_obj_set_style_bg_opa(m_title, LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_color(m_title, lv_color_black(), 0);

  char battery[16];
  if (m_batteryPercent >= 0) {
    std::snprintf(battery, sizeof(battery), "%d%%", m_batteryPercent);
  } else {
    std::snprintf(battery, sizeof(battery), "--");
  }
  lv_label_set_text(m_headerRight, battery);
}

void UI::setMenuHeader() {
  setTitleBar("menu", false);
}

void UI::layoutRows(int startY, int step, int cardHeight) {
  for (size_t i = 0; i < LINE_COUNT; ++i) {
    const int y = startY + static_cast<int>(i) * step;
    const int labelOffset = cardHeight >= 24 ? 4 : 3;
    m_lineRects[i].x = 16;
    m_lineRects[i].y = static_cast<int16_t>(y);
    m_lineRects[i].w = 168;
    m_lineRects[i].h = static_cast<int16_t>(cardHeight);
    lv_obj_set_size(m_cards[i], 168, cardHeight);
    lv_obj_align(m_cards[i], LV_ALIGN_TOP_LEFT, 16, y);
    lv_obj_set_width(m_lines[i], 156);
    lv_obj_align(m_lines[i], LV_ALIGN_TOP_LEFT, 22, y + labelOffset);
  }
}

void UI::layoutGrid(size_t startIndex, size_t count, int startX, int startY, int cardWidth, int cardHeight, int gapX,
                    int gapY) {
  if (startIndex >= LINE_COUNT) return;
  const size_t maxCount = std::min(count, LINE_COUNT - startIndex);
  for (size_t offset = 0; offset < maxCount; ++offset) {
    const size_t i = startIndex + offset;
    const int col = static_cast<int>(offset % 2);
    const int row = static_cast<int>(offset / 2);
    const int x = startX + col * (cardWidth + gapX);
    const int y = startY + row * (cardHeight + gapY);
    const int labelOffset = std::max(4, (cardHeight - 17) / 2);

    m_lineRects[i].x = static_cast<int16_t>(x);
    m_lineRects[i].y = static_cast<int16_t>(y);
    m_lineRects[i].w = static_cast<int16_t>(cardWidth);
    m_lineRects[i].h = static_cast<int16_t>(cardHeight);
    lv_obj_set_size(m_cards[i], cardWidth, cardHeight);
    lv_obj_align(m_cards[i], LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_width(m_lines[i], cardWidth - 12);
    lv_obj_align(m_lines[i], LV_ALIGN_TOP_LEFT, x + 6, y + labelOffset);
  }
}

void UI::setLineStyle(size_t index, LineStyle style, lv_text_align_t align) {
  if (index >= LINE_COUNT) return;

  lv_obj_set_style_text_align(m_lines[index], align, 0);
  lv_obj_set_style_text_color(m_lines[index], style == LineStyle::ACTIVE ? lv_color_white() : lv_color_black(), 0);

  if (style == LineStyle::PLAIN) {
    lv_obj_add_flag(m_cards[index], LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_clear_flag(m_cards[index], LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_opa(m_cards[index], LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(m_cards[index], style == LineStyle::ACTIVE ? lv_color_black() : lv_color_white(), 0);
  lv_obj_set_style_border_color(m_cards[index], lv_color_black(), 0);
  lv_obj_set_style_border_width(m_cards[index], style == LineStyle::ACTIVE ? 0 : 1, 0);
}

void UI::clearLineStyles(bool resetLayout) {
  if (resetLayout) layoutRows(48, 22, 20);
  for (size_t i = 0; i < LINE_COUNT; ++i) {
    setLineStyle(i, LineStyle::PLAIN);
    setLine(i, "");
    m_lineTouchable[i] = false;
  }
}

int UI::hitLine(const WaveshareEPaper154::TouchEvent &event) const {
  for (size_t i = 0; i < LINE_COUNT; ++i) {
    if (!m_lineTouchable[i]) continue;
    const HitRect &rect = m_lineRects[i];
    const int16_t x0 = std::max<int16_t>(0, rect.x - 8);
    const int16_t y0 = std::max<int16_t>(0, rect.y - 7);
    const int16_t x1 = std::min<int16_t>(WIDTH - 1, rect.x + rect.w + 8);
    const int16_t y1 = std::min<int16_t>(HEIGHT - 1, rect.y + rect.h + 7);
    if (event.x >= x0 && event.x <= x1 && event.y >= y0 && event.y <= y1) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool UI::isHeaderTap(const WaveshareEPaper154::TouchEvent &event) const { return event.y <= 44; }

std::string UI::batteryText() const {
  char buffer[24];
  if (m_batteryVoltage >= 0.0f && m_batteryPercent >= 0) {
    std::snprintf(buffer, sizeof(buffer), "%.2fV %d%%", m_batteryVoltage, m_batteryPercent);
  } else {
    std::snprintf(buffer, sizeof(buffer), "battery --");
  }
  return std::string(buffer);
}

void UI::updateBatteryCache(bool force) {
  const uint32_t now = Platform::getInstance().tick();
  if (!force && m_lastBatteryMs != 0 && now - m_lastBatteryMs < BATTERY_RENDER_MS) return;
  m_lastBatteryMs = now;

  Platform &platform = Platform::getInstance();
  m_batteryVoltage = platform.batteryVoltage();
  m_batteryPercent = (m_batteryVoltage >= 0.0f) ? platform.batteryPercentFromVoltage(m_batteryVoltage) : -1;
  if (m_batteryPercent > LOW_BATTERY_PERCENT) m_lowBatteryWarned = false;
}

void UI::render(const char *eventText) {
  (void)eventText;
  updateBatteryCache(false);

  switch (m_state) {
    case State::HOME:
      renderHome();
      break;
    case State::SCANNING:
      renderScanning();
      break;
    case State::CAMERA_LIST:
      renderCameraList();
      break;
    case State::CONNECTING:
      renderConnecting();
      break;
    case State::REMOTE:
      renderRemote();
      break;
    case State::GPS:
      renderGPS();
      break;
    case State::MESSAGE:
      renderMessage();
      break;
    case State::CONFIRM:
      renderConfirm();
      break;
  }

  Platform::getInstance().displayClear(true);
  lv_obj_invalidate(lv_screen_active());
  m_lastRenderMs = Platform::getInstance().tick();
}

void UI::renderHome() {
  setMenuHeader();
  clearLineStyles(false);
  const bool cameraConnected = homeCameraConnected();
  const size_t itemCount = homeItemCount(cameraConnected);
  if (m_homeCursor >= itemCount) m_homeCursor = itemCount - 1;
  if (itemCount >= 6) {
    layoutGrid(0, itemCount, 14, 52, 82, 36, 8, 8);
  } else {
    layoutGrid(0, itemCount, 14, 58, 82, 42, 8, 14);
  }

  for (size_t i = 0; i < LINE_COUNT; ++i) {
    if (i < itemCount) {
      setLine(i, homeActionLabel(homeActionAt(i, cameraConnected)));
      setLineStyle(i, i == m_homeCursor ? LineStyle::ACTIVE : LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
    }
  }

  lv_label_set_text(m_footer, "");
  lv_obj_add_flag(m_footer, LV_OBJ_FLAG_HIDDEN);
}

void UI::renderScanning() {
  setTitleBar("< scan", true);
  clearLineStyles();
  const uint32_t elapsed = Platform::getInstance().tick() - m_scanStartedMs;
  char line[96];

  setLine(0, "looking for camera");
  setLineStyle(0, LineStyle::ACTIVE, LV_TEXT_ALIGN_CENTER);
  std::snprintf(line, sizeof(line), "time %lus / %lus", static_cast<unsigned long>(elapsed / 1000),
                static_cast<unsigned long>(SCAN_MS / 1000));
  setLine(1, line);
  setLineStyle(1, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  std::snprintf(line, sizeof(line), "matches %u", static_cast<unsigned>(m_scanHits.load()));
  setLine(2, line);
  setLineStyle(2, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  setLine(3, "pairing mode");
  setLineStyle(3, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
  setLine(4, "keep awake");
  setLineStyle(4, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
  setLine(5, "Cancel");
  setLineStyle(5, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(m_footer, "tap cancel");
}

void UI::renderCameraList() {
  setTitleBar(m_listFromScan ? "< scan results" : "< cameras", true);
  clearLineStyles(false);
  layoutRows(54, 34, 30);
  const size_t count = CameraList::size();
  if (count == 0) {
    setLine(0, "no cameras found");
    setLineStyle(0, LineStyle::ACTIVE, LV_TEXT_ALIGN_CENTER);
    setLine(1, m_listFromScan ? "try scan again" : "use Scan first");
    setLineStyle(1, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
  } else {
    const size_t start = (m_listCursor / CAMERA_PAGE_LINES) * CAMERA_PAGE_LINES;
    for (size_t i = 0; i < CAMERA_PAGE_LINES; ++i) {
      const size_t index = start + i;
      if (index < count) {
        setLine(i, cameraLabel(index));
        setLineStyle(i, index == m_listCursor ? LineStyle::ACTIVE : LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
      }
    }
  }
  lv_label_set_text(m_footer, "");
  lv_obj_add_flag(m_footer, LV_OBJ_FLAG_HIDDEN);
}

void UI::renderConnecting() {
  setTitleBar("< connecting", true);
  clearLineStyles();
  auto &control = Control::getInstance();
  Camera *camera = control.getConnectingCamera();
  char line[96];

  setLine(0, m_selectedCamera ? m_selectedCamera->getName().c_str() : "camera");
  setLineStyle(0, LineStyle::ACTIVE, LV_TEXT_ALIGN_CENTER);
  std::snprintf(line, sizeof(line), "state %s", controlStateName(control.getState()));
  setLine(1, line);
  setLineStyle(1, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  if (camera != nullptr) {
    std::snprintf(line, sizeof(line), "progress %u%%", camera->getConnectProgress());
  } else {
    std::snprintf(line, sizeof(line), "progress wait");
  }
  setLine(2, line);
  setLineStyle(2, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  setLine(3, "may take 30 sec");
  setLineStyle(3, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
  setLine(4, "keep camera awake");
  setLineStyle(4, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
  setLine(5, "Cancel");
  setLineStyle(5, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(m_footer, "tap cancel");
}

void UI::renderRemote() {
  setTitleBar("< remote", true);
  clearLineStyles(false);
  layoutRows(48, 22, 20);
  auto &control = Control::getInstance();
  char line[96];

  std::snprintf(line, sizeof(line), "%s  %u target", controlStateName(control.getState()),
                static_cast<unsigned>(control.getTargets().size()));
  setLine(0, line);
  setLineStyle(0, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  m_lineTouchable[0] = false;
  layoutGrid(1, 4, 14, 78, 82, 38, 8, 14);
  setLine(1, "Shutter");
  setLineStyle(1, LineStyle::ACTIVE, LV_TEXT_ALIGN_CENTER);
  setLine(2, remotePowerLabel());
  setLineStyle(2, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  setLine(3, "Back");
  setLineStyle(3, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  setLine(4, "Disconnect");
  setLineStyle(4, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(m_footer, "buttons still work");
}

void UI::renderGPS() {
  setTitleBar("< gps", true);
  clearLineStyles(false);
  layoutRows(46, 25, 23);

  GPS &gps = GPS::getInstance();
  const GPS::Snapshot snapshot = gps.snapshot();
  char line[96];
  char age[16];
  const uint32_t now = Platform::getInstance().tick();

  std::snprintf(line, sizeof(line), "GPS  %s", snapshot.enabled ? "ON" : "OFF");
  setLine(0, line);
  setLineStyle(0, LineStyle::ACTIVE, LV_TEXT_ALIGN_CENTER);

  if (!snapshot.enabled) {
    setLine(1, "Tap to turn on");
    setLineStyle(1, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
    setLine(2, "ATGM336H-5N");
    setLineStyle(2, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
    setLine(3, "RX IO44  9600");
    setLineStyle(3, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
    setLine(4, "TX not required");
    setLineStyle(4, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(m_footer, "tap < back");
    m_lastGpsRenderMs = now;
    return;
  }

  if (!snapshot.hasFix) {
    formatAge(age, sizeof(age), now, snapshot.lastSentenceMs);
    setLine(1, snapshot.lastSentenceMs == 0 ? "waiting NMEA" : "searching fix");
    setLineStyle(1, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
    std::snprintf(line, sizeof(line), "NMEA age %s", age);
    setLine(2, line);
    setLineStyle(2, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
    std::snprintf(line, sizeof(line), "sat %u  fix %u", snapshot.satellites, snapshot.fixQuality);
    setLine(3, line);
    setLineStyle(3, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
    setLine(4, snapshot.antenna[0] ? snapshot.antenna : "keep antenna open");
    setLineStyle(4, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
    setLine(5, "outdoors helps");
    setLineStyle(5, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(m_footer, "tap < back");
    m_lastGpsRenderMs = now;
    return;
  }

  formatAge(age, sizeof(age), now, snapshot.lastFixMs);
  std::snprintf(line, sizeof(line), "age %s  sat %u", age, snapshot.satellites);
  setLine(1, line);
  setLineStyle(1, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  std::snprintf(line, sizeof(line), "lat %.6f", snapshot.latitude);
  setLine(2, line);
  setLineStyle(2, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  std::snprintf(line, sizeof(line), "lon %.6f", snapshot.longitude);
  setLine(3, line);
  setLineStyle(3, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  std::snprintf(line, sizeof(line), "alt %.1fm", snapshot.altitude);
  setLine(4, line);
  setLineStyle(4, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
  if (snapshot.validDate && snapshot.validTime) {
    std::snprintf(line, sizeof(line), "%02u-%02u-%02u %02u:%02u:%02u", snapshot.year % 100,
                  snapshot.month, snapshot.day, snapshot.hour, snapshot.minute, snapshot.second);
  } else {
    std::snprintf(line, sizeof(line), "UTC --");
  }
  setLine(5, line);
  setLineStyle(5, LineStyle::PLAIN, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(m_footer, "tap < back");
  m_lastGpsRenderMs = now;
}

void UI::renderMessage() {
  setTitleBar("SyncGeo", true);
  clearLineStyles();
  setLine(1, m_message);
  setLineStyle(1, LineStyle::ACTIVE, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(m_footer, "wait...");
}

void UI::renderConfirm() {
  setTitleBar("< confirm", true);
  clearLineStyles(false);
  layoutRows(58, 42, 36);

  setLine(0, m_confirmPrompt.empty() ? "confirm?" : m_confirmPrompt);
  setLineStyle(0, LineStyle::ACTIVE, LV_TEXT_ALIGN_CENTER);
  setLine(1, "Yes");
  setLineStyle(1, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  setLine(2, "No");
  setLineStyle(2, LineStyle::CARD, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(m_footer, "");
}

std::string UI::cameraLabel(size_t index) const {
  Camera *camera = CameraList::get(index);
  if (camera == nullptr) return "Camera";
  std::string label = cameraTypeName(camera->getType());
  label += " ";
  if (!camera->getName().empty()) {
    label += camera->getName();
  } else {
    char addr[24];
    std::snprintf(addr, sizeof(addr), "%s", std::string(camera->getAddress()).c_str());
    label += addr;
  }
  return label;
}

const char *UI::remotePowerLabel() const {
  Camera *camera = m_selectedCamera;
  const auto &targets = Control::getInstance().getTargets();
  if (camera == nullptr && !targets.empty()) camera = targets.front()->getCamera();
  if (camera == nullptr) return "Focus --";

  switch (camera->getType()) {
    case Camera::Type::RICOH:
      return "2s Shot";
    case Camera::Type::CANON_EOS_SMART:
    case Camera::Type::NIKON:
      return "Focus --";
    default:
      return "Focus";
  }
}

const char *UI::controlStateName(Control::state_t state) const {
  switch (state) {
    case Control::STATE_IDLE:
      return "idle";
    case Control::STATE_CONNECT:
      return "connect";
    case Control::STATE_CONNECTING:
      return "connecting";
    case Control::STATE_CONNECT_FAILED:
      return "failed";
    case Control::STATE_ACTIVE:
      return "active";
    case Control::STATE_DISCONNECTING:
      return "disconnect";
    default:
      return "?";
  }
}

void UI::resetActivity() { m_lastActivityMs = Platform::getInstance().tick(); }

void UI::drawSleepLogo() {
  Platform &platform = Platform::getInstance();
  platform.displayClear(true);

  constexpr int bytesPerRow = (SYNCGEO_SLEEP_LOGO_WIDTH + 7) / 8;
  const int x0 = (WIDTH - SYNCGEO_SLEEP_LOGO_WIDTH) / 2;
  const int y0 = (HEIGHT - SYNCGEO_SLEEP_LOGO_HEIGHT) / 2;

  for (int y = 0; y < SYNCGEO_SLEEP_LOGO_HEIGHT; ++y) {
    for (int x = 0; x < SYNCGEO_SLEEP_LOGO_WIDTH; ++x) {
      const uint8_t byte = SYNCGEO_SLEEP_LOGO[y * bytesPerRow + (x / 8)];
      const bool black = ((byte >> (7 - (x & 0x07))) & 0x01) != 0;
      if (black) platform.displayDrawPixel(static_cast<uint16_t>(x0 + x), static_cast<uint16_t>(y0 + y), true);
    }
  }

  platform.displayRefreshPartial();
}

void UI::renderSleep(const char *reason) {
  (void)reason;
  updateBatteryCache(true);
  drawSleepLogo();
  vTaskDelay(pdMS_TO_TICKS(400));
}

void UI::prepareSleep(const char *reason) {
  if (m_sleeping) return;
  m_sleeping = true;
  ESP_LOGI(TAG, "Preparing deep sleep: %s", reason ? reason : "sleep");

  stopScan();
  renderSleep(reason);

  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_IDLE || !control.getTargets().empty()) {
    ESP_LOGI(TAG, "Disconnecting camera before sleep");
    control.disconnect();
  }

  Platform::getInstance().powerOff();
}

void UI::pollPower() {
  if (m_sleeping) return;

  Platform &platform = Platform::getInstance();
  const uint32_t now = platform.tick();
  const bool shouldSampleBattery = (m_lastBatteryMs == 0) || (now - m_lastBatteryMs >= BATTERY_RENDER_MS);
  bool rendered = false;

  if (shouldSampleBattery) {
    updateBatteryCache(true);

    if (m_batteryPercent >= 0 && m_batteryPercent <= CRITICAL_BATTERY_PERCENT) {
      char reason[64];
      std::snprintf(reason, sizeof(reason), "Critical battery %d%%", m_batteryPercent);
      prepareSleep(reason);
      return;
    }

    if (m_batteryPercent >= 0 && m_batteryPercent <= LOW_BATTERY_PERCENT && !m_lowBatteryWarned) {
      m_lowBatteryWarned = true;
      if (m_state != State::MESSAGE) {
        char warning[64];
        std::snprintf(warning, sizeof(warning), "Low battery %d%%", m_batteryPercent);
        showMessage(warning, m_state);
        rendered = true;
      }
    }

    if (!rendered && m_state != State::SCANNING && m_state != State::CONNECTING) {
      render();
      rendered = true;
    }
  }

  if (m_state != State::SCANNING && m_state != State::CONNECTING && m_lastActivityMs != 0 &&
      now - m_lastActivityMs >= INACTIVITY_SLEEP_MS) {
    prepareSleep("Idle timeout");
  }
}

void UI::startScan() {
  disconnectAndHome("Preparing scan");
  CameraList::clear();
  auto &scan = Scan::getInstance();
  scan.clear();
  m_scanHits = 0;
  m_scanStartedMs = Platform::getInstance().tick();
  m_listFromScan = true;
  m_listCursor = 0;
  m_state = State::SCANNING;
  scan.start(scanCallback, this);
  render();
}

void UI::stopScan() {
  auto &scan = Scan::getInstance();
  if (scan.isActive()) scan.stop();
}

void UI::goHome() {
  stopScan();
  m_state = State::HOME;
  render();
}

void UI::showSavedList() {
  disconnectAndHome("Loading saved");
  CameraList::load();
  m_listFromScan = false;
  m_listCursor = 0;
  m_state = State::CAMERA_LIST;
  render();
}

void UI::connectSelectedCamera() {
  if (CameraList::size() == 0 || m_listCursor >= CameraList::size()) {
    showMessage("No camera selected");
    return;
  }

  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_IDLE || !control.getTargets().empty()) {
    control.disconnect();
  }

  m_selectedCamera = CameraList::get(m_listCursor);
  m_pendingSave = m_listFromScan;
  control.addActive(m_selectedCamera);
  control.connectAll(false);
  m_state = State::CONNECTING;
  render();
}

void UI::pollScan() {
  if (m_state != State::SCANNING) return;

  const uint32_t now = Platform::getInstance().tick();
  if (now - m_scanStartedMs >= SCAN_MS) {
    stopScan();
    m_state = State::CAMERA_LIST;
    m_listCursor = 0;
    render();
    return;
  }

  if (now - m_lastRenderMs >= SCAN_RENDER_MS) render();
}

void UI::pollConnection() {
  if (m_state != State::CONNECTING) return;

  auto &control = Control::getInstance();
  const auto state = control.getState();
  if (state == Control::STATE_ACTIVE) {
    if (m_pendingSave && m_selectedCamera != nullptr) {
      CameraList::save(m_selectedCamera);
      m_pendingSave = false;
    }
    m_state = State::REMOTE;
    render();
    return;
  }

  if (state == Control::STATE_CONNECT_FAILED) {
    m_pendingSave = false;
    showMessage("Connect failed", State::CAMERA_LIST);
    return;
  }

  if (Platform::getInstance().tick() - m_lastRenderMs >= CONNECT_RENDER_MS) render();
}

void UI::pollGPS() {
  if (m_state != State::GPS) return;

  const uint32_t now = Platform::getInstance().tick();
  if (m_lastGpsRenderMs == 0 || now - m_lastGpsRenderMs >= GPS_RENDER_MS) render();
}

void UI::sendPulse(Control::cmd_t press, Control::cmd_t release) {
  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_ACTIVE) {
    showMessage("Not connected", State::HOME);
    return;
  }
  control.sendCommand(press);
  vTaskDelay(pdMS_TO_TICKS(100));
  control.sendCommand(release);
}

void UI::disconnectAndHome(const char *message) {
  stopScan();
  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_IDLE || !control.getTargets().empty()) {
    control.disconnect();
  }
  m_selectedCamera = nullptr;
  m_pendingSave = false;
  if (message != nullptr && std::strlen(message) > 0) ESP_LOGI(TAG, "%s", message);
  m_state = State::HOME;
}

void UI::activateHomeAction(size_t index, bool cameraConnected) {
  switch (homeActionAt(index, cameraConnected)) {
    case HomeAction::SCAN:
      startScan();
      break;
    case HomeAction::SAVED:
      showSavedList();
      break;
    case HomeAction::GPS:
      m_state = State::GPS;
      render();
      break;
    case HomeAction::REMOTE:
      if (cameraConnected) {
        m_state = State::REMOTE;
        render();
      } else {
        showMessage("No active camera");
      }
      break;
    case HomeAction::DISCONNECT:
      showConfirm("disconnect?", ConfirmAction::DISCONNECT, State::HOME);
      break;
    case HomeAction::SLEEP:
      showConfirm("sleep?", ConfirmAction::SLEEP, State::HOME);
      break;
    default:
      break;
  }
}

void UI::showConfirm(const char *prompt, ConfirmAction action, State backState) {
  m_confirmPrompt = prompt ? prompt : "confirm?";
  m_confirmAction = action;
  m_beforeConfirm = backState;
  m_state = State::CONFIRM;
  render();
}

void UI::runConfirmAction() {
  const ConfirmAction action = m_confirmAction;
  m_confirmAction = ConfirmAction::NONE;
  m_confirmPrompt.clear();

  switch (action) {
    case ConfirmAction::DISCONNECT:
      disconnectAndHome("Disconnected");
      showMessage("Disconnected");
      break;
    case ConfirmAction::SLEEP:
      prepareSleep("Confirmed sleep");
      break;
    case ConfirmAction::NONE:
    default:
      m_state = m_beforeConfirm;
      render();
      break;
  }
}

void UI::showMessage(const char *message, State next) { showMessage(std::string(message ? message : ""), next); }

void UI::showMessage(const std::string &message, State next) {
  m_message = message;
  m_afterMessage = next;
  m_messageUntilMs = Platform::getInstance().tick() + MESSAGE_MS;
  m_state = State::MESSAGE;
  render();
}

void UI::handleHomeButton(bool rec, bool pwr, bool recBack, bool pwrLong) {
  if (pwrLong) {
    prepareSleep("Manual sleep");
    return;
  }
  const bool cameraConnected = homeCameraConnected();
  const size_t itemCount = homeItemCount(cameraConnected);
  if (m_homeCursor >= itemCount) m_homeCursor = itemCount - 1;
  if (pwr) {
    m_homeCursor = (m_homeCursor + 1) % itemCount;
    render();
    return;
  }
  if (recBack) {
    m_homeCursor = 0;
    render();
    return;
  }
  if (!rec) return;
  activateHomeAction(m_homeCursor, cameraConnected);
}

void UI::handleScanningButton(bool recBack, bool pwrLong) {
  if (pwrLong) {
    prepareSleep("Manual sleep");
    return;
  }
  if (recBack) {
    stopScan();
    m_state = State::HOME;
    render();
  }
}

void UI::handleListButton(bool rec, bool pwr, bool recBack, bool pwrLong) {
  if (pwrLong) {
    prepareSleep("Manual sleep");
    return;
  }
  if (recBack) {
    m_state = State::HOME;
    render();
    return;
  }
  const size_t count = CameraList::size();
  if (pwr && count > 0) {
    m_listCursor = (m_listCursor + 1) % count;
    render();
    return;
  }
  if (rec && count > 0) connectSelectedCamera();
}

void UI::handleConnectingButton(bool recBack, bool pwrLong) {
  if (pwrLong) {
    prepareSleep("Manual sleep");
    return;
  }
  if (recBack && Control::getInstance().getState() == Control::STATE_CONNECT_FAILED) {
    m_state = State::CAMERA_LIST;
    render();
  }
}

void UI::handleRemoteButton(bool rec, bool pwr, bool recBack, bool pwrDouble, bool pwrLong) {
  if (pwrLong) {
    prepareSleep("Manual sleep");
    return;
  }
  if (pwrDouble) {
    showConfirm("disconnect?", ConfirmAction::DISCONNECT, State::REMOTE);
    return;
  }
  if (recBack) {
    m_state = State::HOME;
    render();
    return;
  }
  if (rec) {
    sendPulse(Control::CMD_SHUTTER_PRESS, Control::CMD_SHUTTER_RELEASE);
    render();
    return;
  }
  if (pwr) {
    sendPulse(Control::CMD_FOCUS_PRESS, Control::CMD_FOCUS_RELEASE);
    render();
  }
}

void UI::handleGPSButton(bool rec, bool pwr, bool recBack, bool pwrLong) {
  if (pwrLong) {
    prepareSleep("Manual sleep");
    return;
  }
  if (recBack) {
    m_state = State::HOME;
    render();
    return;
  }
  if (rec) {
    GPS &gps = GPS::getInstance();
    const bool next = !gps.isEnabled();
    gps.setEnabled(next);
    showMessage(next ? "GPS enabled" : "GPS disabled", State::GPS);
    return;
  }
  if (pwr) render();
}

void UI::handleConfirmButton(bool rec, bool pwr, bool recBack, bool pwrLong) {
  if (pwrLong) {
    prepareSleep("Manual sleep");
    return;
  }
  if (rec) {
    runConfirmAction();
    return;
  }
  if (pwr || recBack) {
    m_confirmAction = ConfirmAction::NONE;
    m_confirmPrompt.clear();
    m_state = m_beforeConfirm;
    render();
  }
}

void UI::handleHomeTouch(const WaveshareEPaper154::TouchEvent &event) {
  const bool cameraConnected = homeCameraConnected();
  const size_t itemCount = homeItemCount(cameraConnected);
  const int line = hitLine(event);
  if (line < 0 || static_cast<size_t>(line) >= itemCount) return;
  m_homeCursor = static_cast<size_t>(line);
  activateHomeAction(m_homeCursor, cameraConnected);
}

void UI::handleScanningTouch(const WaveshareEPaper154::TouchEvent &event) {
  if (!isHeaderTap(event) && hitLine(event) != 5) return;
  stopScan();
  goHome();
}

void UI::handleListTouch(const WaveshareEPaper154::TouchEvent &event) {
  if (isHeaderTap(event)) {
    goHome();
    return;
  }

  const int line = hitLine(event);
  if (line < 0) return;

  const size_t count = CameraList::size();
  if (count == 0) {
    goHome();
    return;
  }

  const size_t start = (m_listCursor / CAMERA_PAGE_LINES) * CAMERA_PAGE_LINES;
  const size_t index = start + static_cast<size_t>(line);
  if (index >= count) return;

  m_listCursor = index;
  connectSelectedCamera();
}

void UI::handleConnectingTouch(const WaveshareEPaper154::TouchEvent &event) {
  if (!isHeaderTap(event) && hitLine(event) != 5) return;
  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_IDLE || !control.getTargets().empty()) control.disconnect();
  m_state = State::CAMERA_LIST;
  render();
}

void UI::handleRemoteTouch(const WaveshareEPaper154::TouchEvent &event) {
  if (isHeaderTap(event)) {
    m_state = State::HOME;
    render();
    return;
  }

  const int line = hitLine(event);
  switch (line) {
    case 1:
      sendPulse(Control::CMD_SHUTTER_PRESS, Control::CMD_SHUTTER_RELEASE);
      render();
      break;
    case 2:
      if (std::strcmp(remotePowerLabel(), "Focus --") == 0) {
        showMessage("No focus action", State::REMOTE);
      } else {
        sendPulse(Control::CMD_FOCUS_PRESS, Control::CMD_FOCUS_RELEASE);
        render();
      }
      break;
    case 3:
      m_state = State::HOME;
      render();
      break;
    case 4:
      showConfirm("disconnect?", ConfirmAction::DISCONNECT, State::REMOTE);
      break;
    default:
      break;
  }
}

void UI::handleGPSTouch(const WaveshareEPaper154::TouchEvent &event) {
  if (isHeaderTap(event)) {
    m_state = State::HOME;
    render();
    return;
  }

  const int line = hitLine(event);
  GPS &gps = GPS::getInstance();
  const bool enabled = gps.isEnabled();
  if (line == 0 || (!enabled && line == 1)) {
    const bool next = !enabled;
    gps.setEnabled(next);
    showMessage(next ? "GPS enabled" : "GPS disabled", State::GPS);
  }
}

void UI::handleConfirmTouch(const WaveshareEPaper154::TouchEvent &event) {
  const int line = hitLine(event);
  if (line == 1) {
    runConfirmAction();
    return;
  }
  if (line == 2 || isHeaderTap(event)) {
    m_confirmAction = ConfirmAction::NONE;
    m_confirmPrompt.clear();
    m_state = m_beforeConfirm;
    render();
  }
}

void UI::handleTouch() {
  Platform &platform = Platform::getInstance();
  if (!platform.touchAvailable()) return;

  const auto event = platform.popTouchEvent();
  if (event.type == WaveshareEPaper154::TouchEventType::NONE) return;
  resetActivity();

  switch (m_state) {
    case State::HOME:
      handleHomeTouch(event);
      break;
    case State::SCANNING:
      handleScanningTouch(event);
      break;
    case State::CAMERA_LIST:
      handleListTouch(event);
      break;
    case State::CONNECTING:
      handleConnectingTouch(event);
      break;
    case State::REMOTE:
      handleRemoteTouch(event);
      break;
    case State::GPS:
      handleGPSTouch(event);
      break;
    case State::MESSAGE:
      m_state = m_afterMessage;
      render();
      break;
    case State::CONFIRM:
      handleConfirmTouch(event);
      break;
  }
}

void UI::handleButtons() {
  Platform &platform = Platform::getInstance();
  using Button = WaveshareEPaper154::Button;
  using Event = WaveshareEPaper154::ButtonEvent;

  const Event recEvent = platform.popButtonEvent(Button::REC);
  const Event pwrEvent = platform.popButtonEvent(Button::PWR);
  const bool recSingle = recEvent == Event::SINGLE;
  const bool recBack = recEvent == Event::DOUBLE || recEvent == Event::LONG;
  const bool pwrSingle = pwrEvent == Event::SINGLE;
  const bool pwrDouble = pwrEvent == Event::DOUBLE;
  const bool pwrLong = pwrEvent == Event::LONG;

  if (recEvent == Event::NONE && pwrEvent == Event::NONE) return;
  resetActivity();

  switch (m_state) {
    case State::HOME:
      handleHomeButton(recSingle, pwrSingle || pwrDouble, recBack, pwrLong);
      break;
    case State::SCANNING:
      handleScanningButton(recBack, pwrLong);
      break;
    case State::CAMERA_LIST:
      handleListButton(recSingle, pwrSingle || pwrDouble, recBack, pwrLong);
      break;
    case State::CONNECTING:
      handleConnectingButton(recBack, pwrLong);
      break;
    case State::REMOTE:
      handleRemoteButton(recSingle, pwrSingle, recBack, pwrDouble, pwrLong);
      break;
    case State::GPS:
      handleGPSButton(recSingle, pwrSingle || pwrDouble, recBack, pwrLong);
      break;
    case State::MESSAGE:
      m_state = m_afterMessage;
      render();
      break;
    case State::CONFIRM:
      handleConfirmButton(recSingle, pwrSingle || pwrDouble, recBack, pwrLong);
      break;
  }
}

void UI::task() {
  ESP_LOGI(TAG, "Starting e-paper phase6 UI task");
  render();

  while (true) {
    Platform &platform = Platform::getInstance();
    platform.update();
    handleTouch();
    handleButtons();
    pollScan();
    pollConnection();
    pollGPS();

    if (m_state == State::MESSAGE && platform.tick() >= m_messageUntilMs) {
      m_state = m_afterMessage;
      render();
    }

    pollPower();

    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace Furble
