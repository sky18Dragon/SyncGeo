# Review: fix idle remote and reconnect scan

## Result
- `UI::sendPulse()` now accepts `STATE_CONNECTED_IDLE` in addition to `STATE_ACTIVE`, allowing Control's existing idle wake-up path to receive shutter/focus commands.
- Removed the unused reconnect scan call from `Control::connectAll()`.
- Removed `Scan::startReconnectScan()` and the `Scan::Mode::RECONNECT` enum entry.
- Confirmed `src/FurbleUIFontZh14.c` and `tools/generate_ui_font.js` exist and staged them for version control.

## Verification
- `git diff --check`: passed.
- Focused scan API check: no `startReconnectScan` / `Mode::RECONNECT` remains in Scan or Control files.
- `platformio run -e esp32-s3-touch-epaper154`: SUCCESS.
- `platformio run -e esp32-s3-epaper154`: SUCCESS.

## Notes
- `Settings::RECONNECT` remains intentionally: it is the infinite reconnect user setting, not the removed reconnect scan mode.
- CCG external double-model review could not be run because `~/.claude/bin/codeagent-wrapper` and the referenced prompt files are not present in this environment.
