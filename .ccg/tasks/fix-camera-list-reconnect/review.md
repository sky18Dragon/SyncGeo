# Review: fix saved camera reconnect

## Result
- Saved camera list reconnect now calls `Control::connectAll(!m_listFromScan)`:
  - scan result connection remains a normal finite connection attempt;
  - saved-list reconnect uses Control's infinite reconnect/backoff path.
- `CameraList::load()` now skips saved Ricoh entries whose BLE bond is missing and records a skipped count.
- Saved-list UI displays ASCII message `Pair lost; scan` when all saved entries were skipped due to missing bonds, avoiding new Chinese glyph generation.

## Verification
- `git diff --check`: passed.
- `platformio run -e esp32-s3-touch-epaper154`: SUCCESS.
- `platformio run -e esp32-s3-epaper154`: SUCCESS.

## Notes
- This fixes the observed `pairType=saved` + `bonded(before)=no` stale saved-entry path by preventing attempts against unbonded saved records.
- CCG external double-model review could not be run because `~/.claude/bin/codeagent-wrapper` and referenced prompt files are unavailable in this environment.
