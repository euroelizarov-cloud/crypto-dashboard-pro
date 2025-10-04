# Crypto Dashboard Pro — Usage Guide (v0.6.0)

This guide covers runtime controls, transitions, indicators, pseudo tickers, anomaly alerts, scaling & sensitivity, history, and compare charts up to version 1.1.2.

## New in 1.1.2
- History menu (“История”): Save/Load/Clear, backend (JSONL/SQLite), auto-save interval, optional auto-load on startup
- Compare chart: time axis, adaptive ticks, left-click tooltip for nearest series and price
- Intervals: 30m/1h/2h/4h/12h/24h/48h; Auto step for performance
- Sensitivity (per-widget): needle gain, auto-collapse, spike expand, minimum window width

### Sensitivity Menu (per widget)
Right-click a speedometer → “Чувствительность”
- Needle Gain: 1x/1.5x/2x/3x/5x — boosts reaction near mid-scale
- Auto-collapse: enable and set speed (Soft/Normal/Fast) — shrinks window around current price
- Spike expand: enable and set threshold (0.4/0.8/1.5/3%) — temporary widen on spikes
- Min window width: guard to avoid over-collapse (0.05–1.0%)

All settings persist per widget via QSettings.

## Views
- Left-click a widget to cycle modes: `speedometer → line_chart → btc_ratio → speedometer`.
- Right-click a widget to open the context menu.

### Transitions
Right-click → Transitions

- Enable animations – master switch
- Types: None | Flip | Slide | Crossfade | Zoom+Blur
  - Flip: 3D card flip simulation
  - Slide: horizontal content slide
  - Crossfade: alpha blend
  - Zoom+Blur: zoom-out blur of old → fade new

If a transition misbehaves, temporarily set Type = None (debug safety).

## Grid Layouts (1x1 … 7x7)
- Menu: Settings → Grid (presets or manual rows/cols)
- Auto-fill: Expanding the grid pulls additional symbols from an internal TOP50 list (no duplicates)
- Reflow: Layout rebuilds after size changes; per-widget settings persist

## Provider Switching (Binance / Bybit)
- Settings → Provider → Binance / Bybit
- Bybit preference (Linear → Spot or Spot → Linear) chooses primary; automatic fallback if unsupported
- Each widget shows badge: `Provider • Market` or computed mode descriptor

## Chart Options
- Right-click → Chart Options:
  - Show grid: toggles the grid lines on charts.
  - Show axis labels: toggles axis labels visibility.

## Speedometer Styles (Right-click → Style)
Classic | NeonGlow | Minimal | ModernTicks | Classic Pro | Gauge | Modern Scale | Segment Bar | Dual Arc

- Segment Bar – segmented progress (2–3% arc segments) with gaps
- Dual Arc – outer = value, inner = volatility (scaled)

## Indicators
Right-click → Indicators

- RSI (14) – overlays separate RSI axis on chart
- MACD (12,26,9) – MACD and signal lines, own axis
- Bollinger Bands (20,2) – upper & lower envelope

Indicators persist per widget via settings.

## Anomaly Alerts (Right-click → Аномалии)
- Показывать значок аномалии – toggles detection badge
- Modes:
  - Выкл – off
  - RSI OB/OS – RSI >=70 or <=30
  - MACD cross – signal crossover
  - BB breakout – price beyond bands
  - Z-Score – |Z| >= 2 over rolling window
  - Всплеск волатильности – return spike vs median
  - Композитный – combined (RSI extreme OR MACD cross OR BB breakout)
  - RSI дивергенция – price higher high + RSI lower high (bearish) / converse (bullish)
  - MACD histogram surge – absolute histogram jump > ~2.5× recent median
  - Кластерный Z-Score – average of multiple Z-scores exceeds threshold
  - Calm↔Volatile смена режима – volatility regime shift (step change)

Badges display compact codes (e.g., RSI↑, MACD↓, BB↑, Z↓, VOL, DIV-, HIST, CZ↑, VOL↑) on the speedometer view.

## Computed / Pseudo Tickers
Right-click → Computed

| Token | Description |
|-------|-------------|
| @AVG | Mean normalized value |
| @ALT_AVG | Mean excluding BTC |
| @MEDIAN | Median normalized value |
| @SPREAD | Range (max−min) normalized |
| @TOP10_AVG | Mean over TOP10 set |
| @VOL_AVG | Average volatility mapped 0..100 |
| @BTC_DOM | BTC dominance proxy (capped) |
| @Z_SCORE:SYMBOL | Symbol Z vs basket (-> 0..100 with 50 mid) |
| @DIFF:SYMBOL[:Linear|Spot] | % diff Bybit vs Binance centered at 50 |

All computed tickers use fixed 0..100 scaling & show badge `Computed • MODE`.

## Scaling Modes (Settings → Auto-scaling)
- Adaptive – drifting bounds (EWMA style) (default)
- Fixed – static 0..100 range
- Manual – user-defined sticky bounds (expand only on breach)
- Python-like – compress bounds each update, enforce minimal width, expand on breakout

Per-widget scaling: pseudo tickers automatically force Fixed 0..100.

## Thresholds
- Settings → Thresholds: Enable and set Warn/Danger levels. Gauges colorize accordingly.

## Performance Tuning
Settings → Performance

Parameters:
- Animation (ms)
- Render interval (UI refresh cadence)
- Cache update (chart data recache)
- Volatility window size
- Max chart points (sampling cap)
- Raw cache size (history retention)
- Python-like scaling parameters (init span, compression factors, min width)

## Overlays
Right-click:
- Show volatility overlay – displays rolling volatility
- Show change overlay – displays recent % change

## Tips
- Minimal / Gauge for dense grids
- Segment Bar for dashboard / TV displays
- Dual Arc to visually pair momentum + volatility
- Turn off grid & axis labels for cinematic charts

## Troubleshooting
| Issue | Action |
|-------|--------|
| Transition flicker | Disable animations, re-enable after restart |
| No data for a symbol | Check provider selection & Bybit market preference |
| Computed widget static | Ensure at least one real symbol updates |
| High CPU | Increase render interval or reduce max points |

## Upcoming
- Global policy menu (batch anomaly/indicator toggles)
- Physics overlay (Box2D visual effects)
- Export/import settings profile
