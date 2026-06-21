# SyncGeo Firmware

SyncGeo 是面向 Waveshare ESP32-S3 1.54 英寸电子墨水屏开发板的便携式相机蓝牙遥控与 GPS 地理信息同步固件。本仓库是 furble 的硬件专用移植版本，当前聚焦于理光相机（Ricoh GR 系列）遥控、GPS 写入和低功耗使用体验。

> 历史说明：`README_PHASE12.md` 记录的是 phase 1/2 的早期 Waveshare 移植验证内容，并已被后续 phase 文档取代。当前 README 汇总的是当前代码状态。

## 当前状态

- 硬件目标：Waveshare `ESP32-S3-ePaper-1.54` 与 `ESP32-S3-Touch-ePaper-1.54`
- 构建系统：PlatformIO + ESP-IDF
- UI：200x200 黑白电子墨水屏界面，中文文本，内置中文 LVGL 字体子集
- 相机支持：仅保留理光相机支持
- GPS：支持 ATGM336H-5N NMEA 数据解析，并通过理光 BLE GATT 路径写入地理信息
- 输入：实体 REC/PWR 按键；Touch 版本额外支持 FT6336 触摸
- 电源：电池电量显示、空闲休眠、深睡唤醒、BLE/GPS 场景下的电源锁

## 支持硬件

### ESP32-S3-ePaper-1.54

非触摸版本，使用实体按键完成菜单、扫描、连接、遥控、GPS 开关和休眠操作。

PlatformIO 环境：

```powershell
platformio run -e esp32-s3-epaper154
```

### ESP32-S3-Touch-ePaper-1.54

触摸版本，保留实体按键作为备用输入，同时支持点击菜单项、返回标题栏、确认框和遥控操作。

PlatformIO 环境：

```powershell
platformio run -e esp32-s3-touch-epaper154
```

## 主要功能

- 扫描并连接理光 BLE 相机
- 保存已配对/已连接相机
- 遥控拍摄
  - `Shutter`：立即拍摄
  - `2s Shot`：理光 2 秒延迟拍摄
- 空闲后进入已连接低功耗状态，按键/触摸遥控可唤醒并继续发送命令
- GPS 页面显示 NMEA/GNSS 状态、卫星数、经纬度、高度和 UTC 时间
- GPS 有效时通过 `Control::updateGPS()` 分发到相机目标，并由 `Ricoh::updateGeoData()` 写入理光 BLE 特征
- 低电量提醒、严重低电量休眠、手动休眠、空闲休眠
- 电子墨水屏休眠 logo：`SyncGeo`
- 中文 UI 字体子集：`src/FurbleUIFontZh14.c`

## UI 操作概览

### 首页菜单

未连接相机时常见菜单：

- `扫描`
- `相机列表`
- `定位`
- `休眠`

连接相机后会出现：

- `遥控器`
- `断开连接`

### 实体按键

- REC 单击：确认/进入/快门，具体行为取决于当前页面
- REC 双击或长按：返回上级页面
- PWR 单击：移动光标/第二动作/刷新，具体行为取决于当前页面
- PWR 双击：在遥控页触发断开确认
- PWR 长按：手动休眠

### Touch 版本

- 点击菜单行进入功能页
- 点击标题栏返回或取消
- 遥控页点击 `Shutter` 或 `2秒延迟`
- 确认页点击 `是` / `否`

## GPS / ATGM336H-5N 接线

默认固件使用 UART NMEA 数据：

| ATGM336H-5N | Waveshare ESP32-S3 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| TXD | GPIO44 / RXD |
| RXD | GPIO43 / TXD，可选 |

默认串口参数：

- `9600` baud
- `8N1`
- 常见 NMEA 语句：`GNGGA`、`GNRMC`、`GNZDA`、`GPTXT`

当前 `GPS_PWR_PIN = -1`，表示未配置外部 GPS 电源门控 GPIO；如果硬件增加 MOSFET 电源控制，需要在代码中指定有效 GPIO。

## Touch 硬件映射

Touch 版本使用 FT6336：

| 功能 | GPIO / 参数 |
| --- | --- |
| EPD DC | GPIO10 |
| EPD CS | GPIO11 |
| EPD SCK | GPIO12 |
| EPD MOSI | GPIO13 |
| EPD RST | GPIO9 |
| EPD BUSY | GPIO8 |
| EPD PWR | GPIO6 |
| FT6336 INT | GPIO21 |
| FT6336 RST | GPIO7 |
| FT6336 SDA | GPIO47 |
| FT6336 SCL | GPIO48 |
| FT6336 I2C 地址 | `0x38` |

## 构建

非触摸版本：

```powershell
platformio run -e esp32-s3-epaper154
```

触摸版本：

```powershell
platformio run -e esp32-s3-touch-epaper154
```

## 烧录

将 `COM12` 替换为实际串口。

非触摸版本：

```powershell
platformio run -e esp32-s3-epaper154 -t upload --upload-port COM12
```

触摸版本：

```powershell
platformio run -e esp32-s3-touch-epaper154 -t upload --upload-port COM12
```

串口监视：

```powershell
platformio device monitor -b 115200 --port COM12
```

## 中文字体

中文 UI 使用 LVGL 自定义字体子集：

- 字体源文件：`src/FurbleUIFontZh14.c`
- 生成脚本：`tools/generate_ui_font.js`
- CMake 引用：`src/CMakeLists.txt`

如果新增或修改中文 UI 文本，请重新生成字体：

```powershell
node tools\generate_ui_font.js
```

生成脚本会从 `src/FurbleUI.cpp` 中提取 `UI_Text` 的中文字符，并使用 LVGL 的 `SourceHanSansSC-Normal.otf` 生成 14px 字体子集。

## 项目结构

| 路径 | 说明 |
| --- | --- |
| `src/` | 应用入口、UI、平台层、GPS、设置 |
| `include/` | 应用头文件、硬件映射、logo 资源 |
| `lib/furble/` | BLE 相机抽象、理光协议、扫描与连接列表 |
| `lib/preferences/` | ESP-IDF 下的 Preferences 兼容实现 |
| `tools/` | 字体生成等辅助脚本 |
| `assets/` | 资源文件 |
| `docs/` | 额外文档 |
| `README_PHASE*.md` | 历史阶段说明 |

## 重要实现说明

- 当前只保留 Ricoh 相机支持；历史保存的非 Ricoh NVS 记录会被忽略。
- `Settings::RECONNECT` 是无限重连设置，不等同于已移除的 reconnect scan 模式。
- `.pio/`、`managed_components/`、`outputs/` 属于构建/生成产物，不应作为主要源码维护入口。
- Touch 和非 Touch 使用不同 PlatformIO 环境，烧录时需要确认环境选择正确。
- 如果触摸无响应，请优先确认烧录的是 `esp32-s3-touch-epaper154`，并检查串口日志中 FT6336 probe 结果。

## 相关文档

- [Waveshare ESP32-S3 ePaper 1.54 硬件资料](https://docs.waveshare.net/ESP32-S3-ePaper-1.54/?variant=ESP32-S3-Touch-ePaper-1.54)
- `README_PHASE12.md`：phase 1/2 早期硬件移植记录
- `README_PHASE3.md`：扫描/连接/遥控基础流程
- `README_PHASE4.md`：电源与电池体验
- `README_PHASE5.md`：电子墨水屏 UI 与 SyncGeo logo
- `README_PHASE6.md`：GPS 页面和 UI 调整
- `README_PHASE7_TOUCH.md`：Touch 版本支持
