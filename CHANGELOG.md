# Changelog
# Changelog

## v1.1.2 — 2025-10-04

### Added
- History persistence module with two backends: JSON Lines (.jsonl) and SQLite (.sqlite/.db).
- App menu “История” with actions: Save, Load, Clear, Backend toggle, and Auto-save every N minutes.
- Optional auto-load on startup (settings groundwork added; file selection available in menu).
- Multi-Compare X axis switched to time (`QDateTimeAxis`) with HH:mm:ss labels.
 - Sensitivity subsystem per speedometer: needle gain (1x/1.5x/2x/3x/5x), auto-collapse window, spike expand, min window width; per-widget persistence via QSettings.
 - Compare chart intervals: 30m/1h/2h/4h/12h/24h/48h; “Auto” step to cap points/series.

### Improved
- `DynamicSpeedometerCharts::replaceHistory` to restore buffers and recompute bounds/volatility.
- User notifications on save/load/clear; validation of loaded history (symbols, monotonic timestamps, numeric values).
 - Adaptive resampling in compare chart to keep memory/CPU low for long windows.
 - Left-click tooltip on compare: nearest series at cursor time with symbol/price; robust selection across series.
 - Documentation updated: README (v1.1.2 features), USAGE (sensitivity & history), ARCHITECTURE (1.1.2 modules).

### Fixed
- About dialog and CMake versions synced to 1.1.2.
 - Duplicate axis labels in compare chart fixed by persistent axes lifecycle and series cleanup.
 - Tooltip earlier showing only once/first series now correctly selects nearest series and updates per click.

## v1.1.0 — 2025-10-04

### Added
- Timestamped history with metadata for widgets (HistoryPoint: ts, value, source kind, provider, market, seq).
- Multi-Compare: interpolation mode (Hold-last or Linear) and optional provider lag compensation; settings persisted.

### Improved
- More accurate min/max bounds and volatility calculations based on structured history points.
- Widgets now tag points with TRADE/TICKER source kind automatically when mode changes.

### Fixed
- Build restored after migrating history internals; About dialog and project versions bumped to v1.1.0.

## v0.5.0 — 2025-09-22

### Added
- Computed widgets (pseudo tickers): @AVG, @ALT_AVG, @MEDIAN, @SPREAD
- Binance↔Bybit price difference widget: @DIFF:SYMBOL[:Linear|Spot]
- Right-click menu to insert computed widgets without typing

### Improved
- Separate compare workers for strict market diff subscriptions (no Bybit fallback)

### Notes
- Computed widgets use fixed 0..100 scaling and update live from existing widgets/streams.

## v0.4.0 — 2025-09-22

### Added
- New speedometer styles: Segment Bar, Dual Arc
- Chart options per widget: toggle grid lines and axis labels
- Provider switching (Binance / Bybit) with UI badges
- Bybit dual-market fallback (Linear/Spot) and per-symbol tracking
- Grid layout menu with presets from 1x1 to 7x7 and TOP50 auto-fill
- Python-like scaling mode with runtime tuning

### Improved
- Modern Scale style now uses a clean segmented indicator for better readability
- Numerous UI refinements and stability improvements

### Fixed
- Robust teardown and reconnection when switching providers/markets
- Improved JSON parsing and ping/pong handling for Bybit streams

