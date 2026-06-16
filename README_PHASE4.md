# furble ESP32-S3-ePaper-1.54 phase4

第四阶段在 phase3 已验证的相机扫描、匹配、连接、快门/延迟快门链路外侧，补齐电源与电池体验。

## 新增内容

- 电池 ADC 读取优先使用 ESP-IDF ADC curve fitting 校准；校准不可用时回退到 raw 12-bit 换算。
- UI 电量缓存只采样一次电压，再按 3.20/3.40/3.70/3.90/4.20V 曲线换算百分比。
- 低电量（<=15%）弹出提示；临界电量（<=5%）自动进入深睡。
- 5 分钟无按键且不在扫描/连接中时自动深睡。
- 手动/菜单/自动深睡前显示睡眠原因、唤醒方式和电量，并保留在 e-paper 上。
- 深睡前停止 BLE 扫描并断开已连接相机，避免相机端残留连接。

## 构建

```powershell
platformio run -e esp32-s3-epaper154
```

## 烧录

```powershell
platformio run -e esp32-s3-epaper154 -t upload
```

## 测试建议

1. 正常启动后首页显示 `furble phase4` 与电量。
2. 扫描、连接、REC 快门、PWR 对焦仍保持 phase3 行为。
3. 长按 PWR：屏幕显示 Sleeping，再进入深睡；按 REC 或 PWR 可唤醒。
4. 进入 Sleep 菜单项：同样显示睡眠提示页并入睡。
5. 保持空闲约 5 分钟：自动显示 `Idle timeout` 并入睡。
6. 若电池低于阈值：<=15% 提示低电，<=5% 自动保护入睡。
