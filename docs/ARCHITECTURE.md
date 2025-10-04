# Crypto Dashboard Pro — Technical Design and Implementation Guide (v1.0.0)

This document provides a complete, end-to-end description of the Crypto Dashboard Pro project: requirements, architecture, data flows, concurrency model, modules, APIs, data structures, algorithms, persistence, UI/UX, deployment, testing, and operations. It is meant to enable any developer or LLM to reconstruct, maintain, and extend the project with confidence.

Version: 1.1.2
Date: 2025-10-04

---

## 1. Product Overview

Crypto Dashboard Pro is a real-time, modular desktop application built with C++17 and Qt 6, designed for monitoring, analyzing, and visualizing cryptocurrency markets. It features per-symbol speedometer widgets with multiple visualization styles, volume overlays, indicators, and anomalies; a market-wide overview gauge; and a multi-symbol normalized comparison chart. Configuration is persistent and manageable both per-widget and globally.

Core capabilities:
- Realtime WebSocket ingestion (Binance, Bybit) of trades/tickers.
- Per-symbol dynamic speedometer widgets with autoscaling that preserves extremes.
- Volume visualization: side bar and fuel modes; 2nd needle for volume.
- Global menu to control per-widget options in bulk.
- Market overview tool producing an aggregate strength/confidence gauge.
- Multi-symbol comparison tool aligning and normalizing series on one chart.
- Theming system with modern tick styles and multiple frame styles.

---

## 2. Requirements and Scope

### 2.1 Functional Requirements
- Stream real-time quotes from supported providers (Binance, Bybit) using WebSocket.
- Maintain per-symbol history with timestamps for resampling and analytics.
- Visualize each symbol using a dynamic speedometer + optional time series chart.
- Autoscaling modes: legacy, history-window preserving extremes, KiloCoder-like.
- Volume integrations: bar (sidebar), fuel, or second needle mode.
- Per-widget indicators: RSI, MACD, Bollinger Bands; anomaly detection modes.
- Global menu to apply settings across all widgets.
- Market Overview window:
  - Computes aggregated trend/strength/confidence over adjustable intervals.
  - Allows weighting (Equal, InverseVolatility), include/exclude BTC and other assets.
- Multi-Compare window:
  - Aligns different symbols on a common time grid.
  - Normalization modes: FromStart%, MinMax [0..1], Z-Score.
  - Smoothing (EMA), auto-refresh, and selectable assets.
- Persistent settings via QSettings.

### 2.2 Non-Functional Requirements
- Stability: Clean shutdown without hangs; QPainter state safety.
- Performance: Handle frequent ticks without UI stalls; efficient resampling.
- Extensibility: Modular components with clear interfaces.
- Portability: macOS primary; Qt6 cross-platform foundation.
- Observability: Debug logging for data flow and state changes.

---

## 3. Architecture

The system follows a modular, event-driven architecture leveraging Qt's signals/slots and object ownership model.

High-level components:
- MainWindow: App shell, menus, layout/grid of widgets, routing actions.
- DataWorker: WebSocket manager and message dispatcher (TRADE/TICKER modes).
- DynamicSpeedometerCharts: Per-symbol widget combining speedometer, overlays, and context menu.
- MarketAnalyzer: Aggregates symbol time series into market snapshot metrics.
- MarketGaugeWidget: Visual gauge to render MarketAnalyzer output.
- MarketOverviewWindow: Hosts the market gauge and settings panel.
- MultiCompareWindow: Normalized multi-symbol chart with resampling and UI controls.
- ThemeManager + frame drawing: Styles, colors, and frame styles.

### 3.1 Process & Thread Model
- UI thread: All Qt widgets and painting.
- DataWorker: QWebSocket operations run in UI thread but use asynchronous event loop. Optional moveToThread can be added for heavy parsing.
- Timers: QTimer-driven periodic tasks (auto-refresh, analyzer intervals).

### 3.2 Communication
- Signals/slots for data propagation:
  - DataWorker::quoteReceived(symbol, price, ts, vol,...)
  - MainWindow::handleData(...) updates widgets and forwards to MarketAnalyzer.
  - MarketAnalyzer::snapshotReady(MarketSnapshot) to MarketOverviewWindow.
- QSettings for configuration persistence.

---

## 4. Data Model

### 4.1 Per-symbol History
- Structure: deque or QVector of pairs {timestamp_ms: double, value: double}
- Access: DynamicSpeedometerCharts::historySnapshot() returns QVector<QPair<double,double>>
- Windowing: Fixed or configurable window seconds for autoscaling and analytics.

### 4.2 Market Analyzer State
- Per symbol: rolling buffers for prices, volatility estimates.
- Aggregate metrics: regression slopes, consensus, strength [0..1], confidence [0..1], label.

### 4.3 Multi-Compare Series
- Resampled on a grid: t0..tn with step S (e.g., 5–60s), last-known carry-forward.
- Normalized values:
  - FromStart%: (x_t - x_0) / x_0 * 100
  - MinMax01: (x_t - min) / (max - min)
  - Z-Score: (x_t - mean) / std

---

## 5. Key Algorithms

### 5.1 Autoscaling (History-based preserving extremes)
- Maintain rolling max/min over a window with padding.
- If new extremes occur, adjust bounds but decaying memory prevents jitter.
- Legacy and KiloCoder-like modes are selectable.

Pseudocode:
```
window = N seconds
padding = p%
cur_min, cur_max = rolling extremes in window
if cur_max == cur_min: expand epsilon
low  = cur_min * (1 - padding)
high = cur_max * (1 + padding)
```

### 5.2 Market Overview Aggregation
- For each symbol, compute slope via linear regression on recent window.
- Weight series by config (equal or inverse volatility).
- Aggregate to a consensus index; derive strength and confidence via variance.

### 5.3 Resampling & Normalization for Multi-Compare
- Align series on common grid; fill with last-known; drop leading empties.
- Compute stats per series then normalize per selected mode.

---

## 6. Modules

### 6.1 DynamicSpeedometerCharts
- Responsibilities: render speedometer, chart, overlays, context, and history.
- Public setters for global control:
  - setFrameStyleByName, setSidebarWidthMode, setSidebarOutline, setSidebarBrightnessPct
  - setRSIEnabled, setMACDEnabled, setBBEnabled
  - setAnomalyEnabled, setAnomalyModeByKey
  - setOverlayVolatility, setOverlayChange
  - setVolumeVisByKey
  - historySnapshot()
- Drawing: Uses QPainter; ensures save/restore symmetry to avoid leaks.
- Volume modes: Sidebar (right vertical bar), Fuel tank, Second needle.
- Anomalies: Composite anomaly visualization modes.

### 6.2 DataWorker
- WebSocket connections for Binance/Bybit.
- Modes: TRADE or TICKER, with 24h fallback normalization for TICKER.
- Parses JSON frames; emits typed updates to MainWindow.
- Reconnect/backoff and clean disconnect handling.

### 6.3 MainWindow
- Builds menus: Widgets (global controls), Tools (Overview, Multi-Compare), Help.
- Creates and arranges DynamicSpeedometerCharts per symbol.
- Forwards data to widgets and MarketAnalyzer.
- Persist settings using QSettings.
- About dialog reflects app version (v1.1.2).

### 6.4 MarketAnalyzer, MarketGaugeWidget, MarketOverviewWindow
- Analyzer manages per-symbol buffers, computes snapshot (trend, strength, confidence, label).
- Gauge renders gradient arc, needle, bars with labels.
- Window hosts settings: interval, weights, include BTC, exclusion list.

### 6.5 MultiCompareWindow
- UI controls: interval, normalization mode, resample step, smoothing (EMA), auto-refresh, select all/none.
- Data source: DynamicSpeedometerCharts::historySnapshot().
- Plots multiple QLineSeries with QValueAxis x/y, dynamic min/max from aggregated normalized values.

---

## 7. UI/UX and Theming
- ThemeManager controls palettes, modern “KiloCode Modern Ticks” multi-arc style.
- Frame styles for widgets (flat, glow, etc.).
- Tool windows accessible from Tools menu; per-widget context menus for local options.

---

## 8. Persistence
- QSettings keys for:
  - Global widget options (volume modes, overlays, frames, indicators, anomalies).
  - Tool windows’ parameters (overview, optionally compare window – future enhancement).
- Per-widget state applied on construction and updated via global menu actions.

---

## 9. Error Handling & Stability
- Graceful disconnects for WebSockets with close codes logged.
- Painter state safety: ensure painter.save/painter.restore pairs.
- Robust defaults when data arrays are empty; guard against division by zero in normalization.

---

## 10. Logging & Observability
- qDebug() tracing for DataWorker state transitions, connections, and streams.
- Logs for provider/mode changes, currency list updates.
- Optional: add category-based logging and levels.

---

## 11. Performance & Scalability
- Efficient time-series storage (deque/QVector) and bounded windows.
- Resampling uses last-known carry-forward to avoid sparse artifacts.
- UI updates are event-driven; heavy work remains minimal on the UI thread.
- Potential future: move parsing to worker thread if needed; back-pressure via throttling.

---

## 12. Security
- Outbound WebSocket connections to public endpoints; no secrets are stored.
- No local database or credentials. QSettings stores only visual/config preferences.

---

## 13. Testing Strategy
- Manual smoke tests: build, run, WS connect, Tools windows open and render.
- Unit-test candidates (future): analyzer math, normalization functions, resampler.
- Visual regression: screenshots of widgets under stable sample data streams.

---

## 14. Build, Packaging, and Deployment
- Build system: CMake + Ninja, Qt 6.9.1.
- Targets: modular_dashboard.app and related demo apps.
- Packaging: zip the .app bundle for distribution.
- Example artifact: build/Qt_6_9_1_for_macOS-Debug/modular_dashboard-099.zip

Release 1.0.0:
- Update CMake project versions to 1.1.2.

## 1.1.2 Additions

### HistoryStorage
- Backends: JSONL and SQLite
- API: save/load/clear/collect
- Validation: symbol format, monotonic timestamps, numeric values
- Logging: qInfo/qWarning for operations with counts/backends/paths
- UI: MainWindow adds “История” menu (Save/Load/Auto-save/Backend)

### Compare Chart (Multi-Compare)
- Persistent axes: QDateTimeAxis (X), QValueAxis (Y)
- Adaptive tick count and time label formats based on visible time window
- Left-click tooltip: nearest series at cursor time, shows symbol + price
- Intervals: 30m/1h/2h/4h/12h/24h/48h; Auto step to keep ≤~2500 points/series

### Sensitivity Subsystem (DynamicSpeedometerCharts)
- Needle gain (1x…5x): amplifies normalized displacement around mid-scale
- Auto-collapse: window gradually shrinks around current price with speed presets
- Spike expand: one-shot expansion on large returns to avoid clipping
- Min range: fraction of |price| guards against zero width
- Persistence: per-widget QSettings; menu under “Чувствительность”
- Update About dialog to v1.0.0.
- Rebuild and package as modular_dashboard-1.0.0.zip (see below).

---

## 15. CI/CD (Future Work)
- GitHub Actions for macOS: Qt setup, cmake configure, build, and artifact upload.
- Matrix for Debug/Release builds; notarization pipeline (macOS) as a later step.

---

## 16. Metrics & Monitoring (Future)
- FPS/paint time, tick rate, WS reconnects, per-window latency stats.
- In-app overlay or simple metrics endpoint for scraping (not implemented).

---

## 17. Documentation & Code Style
- Code comments document key data flows and algorithms.
- Prefer small, focused classes and clear slots/signals.
- Style: clang-format compatible; consider adding config and CI check.

---

## 18. Design Principles & Patterns
- Event-driven UI; Observer pattern via Qt signals/slots.
- Model-View separation: data collection vs. rendering widgets.
- Modular windows for tools (Overview, Compare) decouple analytics from UI.

---

## 19. File/Folder Structure (Key Parts)
- CMakeLists.txt (root)
- modular_dashboard/
  - CMakeLists.txt
  - include/
    - MainWindow.h
    - DynamicSpeedometerCharts.h
    - MarketAnalyzer.h
    - MarketGaugeWidget.h
    - MarketOverviewWindow.h
    - MultiCompareWindow.h
  - src/
    - MainWindow.cpp
    - DynamicSpeedometerCharts.cpp
    - MarketAnalyzer.cpp
    - MarketGaugeWidget.cpp
    - MarketOverviewWindow.cpp
    - MultiCompareWindow.cpp
  - resources/ (themes, icons – if any)
- docs/ARCHITECTURE.md (this file)

---

## 20. Pseudocode Snippets

### 20.1 DataWorker
```
connectWS(provider, mode, symbols):
  url = buildUrl(provider, mode, symbols)
  socket.open(url)

onMessage(msg):
  obj = parseJSON(msg)
  for each tick in obj:
    emit quoteReceived(symbol, price, ts, vol)
```

### 20.2 MainWindow Data Handling
```
onQuote(symbol, price, ts, vol):
  widget = widgets[symbol]
  widget.update(price, ts, vol)
  analyzer.updateSymbol(symbol, price, ts)
```

### 20.3 MultiCompare Resampling & Normalization
```
resample(series, step):
  grid = [t0, t0+step, ... tn]
  last = NaN
  for t in grid:
    v = last_known_value_before(series, t)
    if v exists: last = v
    out.append((t, last))

normalize(mode, series):
  if mode==FromStart: base = first_non_nan; return (x-base)/base*100
  if mode==MinMax: min,max = stats; return (x-min)/(max-min)
  if mode==ZScore: mu,sigma = stats; return (x-mu)/sigma
```

---

## 21. Known Limitations & Next Steps
- Compare window settings are not persisted yet.
- Deprecation warnings in QtCharts axis getters; refactor to managed axes.
- Unit tests are minimal; add math tests for analyzers and normalizers.

---

## 22. How to Build & Run

Prerequisites: Qt 6.9.x, CMake, Ninja.

Build & run locally:
1) Configure with CMake and build (Debug/Release).
2) Start modular_dashboard.app.
3) Use Tools menu to access Overview and Compare windows.

Packaging:
- Zip the .app bundle for distribution (see artifact example above).

---

## 23. Changelog

- 1.0.0 (2025-10-04)
  - Finalized Multi-Compare tool; Market Overview stabilized.
  - Global widgets control menu; improved autoscaling preserving extremes.
  - Modern themes and ticks; volume modes and overlays; stability fixes.

---

## 24. License

Proprietary — internal use.
