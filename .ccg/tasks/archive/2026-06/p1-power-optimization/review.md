# Review: p1-power-optimization

## Scope
- Added GPS power gating APIs and sampling-window integration.
- Added UI render dirty flag and idle/GPS loop delay reduction.

## External model review
- Skipped: `~/.claude/bin/codeagent-wrapper` is not present in this environment, so required CCG gemini/claude external review could not be invoked.

## Local verification
- `pio run` passed for:
  - `esp32-s3-epaper154`
  - `esp32-s3-touch-epaper154`

## Findings
- Critical: none from local build.
- Warning: GPS snapshot freshness in `Control::STATE_GPS_SAMPLE` is still governed by existing control logic; this change only gates hardware on the sampling state as requested.
- Info: `.ccg/spec` was absent, so no project spec updates were applicable.
