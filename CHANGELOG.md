# Changelog
# Changelog

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

