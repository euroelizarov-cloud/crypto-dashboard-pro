# Crypto Dashboard Pro — Usage Guide

This guide covers the most important runtime controls and customization options.

## Views
- Left-click a widget to cycle modes: `speedometer → line_chart → btc_ratio → speedometer`.
- Right-click a widget to open the context menu.

## Grid Layouts (1x1 … 7x7)
- Menu: View → Grid → choose any layout from 1x1 up to 7x7.
- Auto-fill (TOP50): When enabled, the grid will populate empty cells with the top tickers list.
- Reflow: Widgets will reflow automatically when grid size changes.

## Provider Switching (Binance / Bybit)
- Menu: Settings → Provider → Binance / Bybit.
- Bybit dual-market fallback: The app attempts Linear and Spot connections automatically per symbol and shows the active market badge.
- Market/Provider badges: Top-right of each widget shows the current provider and market (e.g., `Bybit • Linear`).

## Chart Options
- Right-click → Chart Options:
  - Show grid: toggles the grid lines on charts.
  - Show axis labels: toggles axis labels visibility.

## Speedometer Styles
- Right-click → Style:
  - Classic, NeonGlow, Minimal, ModernTicks, Classic Pro, Gauge, Modern Scale
  - Segment Bar: bold segmented arc with gaps, minimal labels.
  - Dual Arc: outer arc for value, inner arc for volatility.

## Scaling Modes
- Settings → Scaling:
  - Adaptive: adjusts to recent data range automatically.
  - Fixed: enforce fixed min/max.
  - Manual: set custom min/max; expands if price goes outside.
  - Python-like: compresses window gradually, enforces a minimal width relative to price; expands when price breaks out.

## Thresholds
- Settings → Thresholds: Enable and set Warn/Danger levels. Gauges colorize accordingly.

## Performance Tuning
- Settings → Performance: Adjust animation duration, render/cache intervals, history size, and raw cache limit.

## Tips
- Use Minimal or Gauge styles for the cleanest look.
- Use Segment Bar when you want strong progress indication without needles.
- Dual Arc is helpful to correlate current value with volatility.
- For charts, hide grid and labels for a distraction-free canvas.
