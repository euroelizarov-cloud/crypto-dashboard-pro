# 📊 Crypto Dashboard Pro
[![Latest Release](https://img.shields.io/github/v/release/euroelizarov-cloud/crypto-dashboard-pro?sort=semver&label=latest)](https://github.com/euroelizarov-cloud/crypto-dashboard-pro/releases/latest)

Modern real-time cryptocurrency dashboard built with Qt6 (Widgets + Charts + WebSockets) and C++. Version **v1.1.2** adds history persistence (JSONL/SQLite), time-aligned compare charts, interactive tooltips, performance-friendly auto resampling, and a powerful per-widget Sensitivity system (needle gain + auto-collapse).

## ✨ Features

### 🎛️ **Advanced Speedometer Styles (9)**
- Classic — Traditional with colored threshold zones
- NeonGlow — Futuristic neon glow & radial aura
- Minimal — Lightweight arcs & needle
- ModernTicks — Contemporary tick grid
- Classic Pro — Detailed professional dial
- Gauge — Simplified gauge variant
- Modern Scale — Segmented linear-feel arc
- Segment Bar — Discrete progress segments with gaps
- Dual Arc — Value + inner volatility arc

### 🎨 **Rich Color Themes (15+ themes)**
- **Dark** - Classic dark theme
- **Light** - Clean light theme
- **Blue/Green/Purple/Orange/Red** - Colorful variants
- **Cyber/Ocean/Forest** - Atmospheric themes
- **Tokyo Night** - Dark theme with purple-pink accents
- **Cyberpunk** - Neon blue and purple colors
- **Pastel** - Soft pastel tones
- **Retro** - Vintage orange-brown colors
- **Minimal White** - Pure white minimalism

### ⚙️ **Smart Auto-Scaling & Sensitivity**
- Adaptive — EWMA-like soft tracking of bounds
- Fixed — Static 0–100 mapping
- Manual — Sticky bounds expand only if breached
- Python-like — Gradually compressing dynamic window with minimum width rules
- Sensitivity (new in v1.1.2):
	- Needle gain: 1x/1.5x/2x/3x/5x for more responsive needles
	- Auto-collapse: window auto-shrinks around current price with speed presets
	- Spike expand: instant window expansion on price spikes to avoid clipping
	- Minimum window width: guard against over-collapse (per-widget)
- Per-widget independent scaling & overrides

### 🔧 **Advanced Features**
- Real-time WebSocket (Binance + Bybit) with resilient reconnect
- Bybit dual-market fallback (Linear ↔ Spot preference + live badge)
- Provider & market badges per widget
- Pseudo / computed tickers (see below) with automatic scaling & badges
- Technical Indicators: RSI, MACD (12/26/9), Bollinger Bands (20/2)
- Anomaly Detection Modes (RSI OB/OS, MACD cross, BB breakout, Z-Score, Vol spike, Composite, RSI divergence, MACD histogram surge, Clustered Z-score, Volatility regime shift)
- Transition animations (Flip, Slide, Crossfade, Zoom+Blur) – toggleable
- Per‑widget overlays: Volatility %, Change %
- Threshold highlighting (Warn / Danger)
- Performance tuning dialog (animation, render/cache cadence, history, raw cache)
- Persistent settings (QSettings)
- Context menu power tools (styles, indicators, anomalies, computed insert)
- Chart options: grid & axis labels toggles
- History persistence (new): Save/Load/Clear history via JSONL or SQLite, auto-save timer, optional auto-load on startup
- Compare chart (new): Time axis, adaptive ticks & format, left-click tooltip with nearest series & price at time
- Intervals (new): 30m/1h/2h/4h/12h/24h/48h + Auto step to cap points/series (~≤2500)
- Grid presets 1×1 … 7×7 with TOP50 auto-fill

## 🚀 **Live Data Support**
- Default basket: BTC, XRP, BNB, SOL, DOGE, XLM, HBAR, ETH, APT, TAO, LAYER, TON (auto-extend via TOP50 when enlarging grid)
- Real-time Trade or Ticker streams (menu selectable)
- Dual-provider (Binance, Bybit) + per-symbol market resolution
- Compare workers for @DIFF pseudo tickers (Binance vs Bybit Linear/Spot)

## 🛠️ **Technical Stack**
- **Qt6** (Core, GUI, Widgets, WebSockets, Charts)
- **C++17** with modern features
- **CMake/Ninja** build system
- **QSettings** for configuration persistence
- **Modular architecture** with clean separation

## 📦 **Project Structure**
```
dashboard/
├── modular_dashboard/           # Main application
│   ├── include/                # Header files
│   │   ├── MainWindow.h
│   │   ├── DynamicSpeedometerCharts.h
│   │   ├── ThemeManager.h
│   │   └── DataWorker.h
│   └── src/                    # Implementation files
│       ├── main.cpp
│       ├── MainWindow.cpp
│       ├── DynamicSpeedometerCharts.cpp
│       ├── ThemeManager.cpp
│       └── DataWorker.cpp
├── CMakeLists.txt              # Build configuration
└── build/                      # Build output (ignored)

Grid presets and auto-fill from TOP50 are managed in `MainWindow`.
```

## ⬇️ Download

Latest macOS bundle: see the **Releases** page.

Direct link (v1.1.2):
https://github.com/euroelizarov-cloud/crypto-dashboard-pro/releases/download/v1.1.2/crypto-dashboard-pro-v1.1.2-macos.zip

## 🔨 **Building**

### Prerequisites
- Qt6 (with WebSockets and Charts modules)
- CMake 3.16+
- Modern C++ compiler (C++17 support)

### Build Steps
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target modular_dashboard -j8
```

### Run
```bash
./build_mod/modular_dashboard.app/Contents/MacOS/modular_dashboard
```

## 🎯 **Usage**

### Basic Operation
1. **Launch** the application
2. **Switch provider**: Settings → Provider → Binance or Bybit (fallback logic auto-handles Bybit Linear/Spot)
3. **Use grid layouts**: View → Grid → 1x1 … 7x7; auto-fill with TOP50 if enabled
4. **Toggle chart features**: Right-click widget → Chart Options → Show grid / Show axis labels
5. **Customize** styles via Settings or right-click context menus

### Customization
- **Global Styles**: Settings → Speedometer Style
- **Per-Widget**: Right-click any speedometer → Choose style
- **Themes**: Settings → Theme
- **Auto-scaling**: Settings → Auto-scaling (Adaptive / Fixed / Manual / Python-like)
- **Thresholds**: Settings → Thresholds
- **Chart options**: Right-click → Chart Options

### Computed Widgets (Pseudo Tickers)
Right-click → Computed to insert without typing.

| Token | Meaning |
|-------|---------|
| `@AVG` | Average normalized value of all real tickers |
| `@ALT_AVG` | Average excluding BTC |
| `@MEDIAN` | Median normalized value |
| `@SPREAD` | Range (max − min) mapped to 0..100 |
| `@TOP10_AVG` | Average of TOP10 symbols (intersection with tracked) |
| `@VOL_AVG` | Average volatility % mapped to 0..100 (soft clamp) |
| `@BTC_DOM` | BTC dominance proxy vs basket (capped) |
| `@Z_SCORE:SYMBOL` | Z-score vs basket distribution mapped to 0..100 |
| `@DIFF:SYMBOL[:Linear|Spot]` | % diff Bybit (market) vs Binance centered at 50 |

All computed widgets show badge “Computed • <MODE>” and use fixed scaling.

### Performance Tuning
- **Animation timing**: Settings → Performance → Animation delays
- **Cache limits**: Settings → Performance → Memory limits
- **Data retention**: Settings → Performance → History size

## 🔍 **Architecture Highlights**

### Modular Design
- **MainWindow**: Application orchestration and UI management
- **DynamicSpeedometerCharts**: Individual speedometer widgets with multiple styles
- **ThemeManager**: Centralized theme and color management
- **DataWorker**: Background WebSocket data handling with threading

### Key Design Patterns
- **Strategy Pattern**: Multiple speedometer rendering styles
- **Observer Pattern**: Real-time data updates
- **Factory Pattern**: Theme and color scheme creation
- **Command Pattern**: Settings persistence and restoration

### Performance Optimizations
- **Efficient painting**: Optimized QPainter usage for smooth animations
- **Memory management**: Configurable cache limits and data circular buffers
- **Threading**: Background data processing with Qt's signal-slot system
- **Resource pooling**: Reusable painter objects and brush caching

## 📈 **Development Timeline**
| Phase | Status | Highlights |
|-------|--------|------------|
| 1 | ✅ | Core charts + data plumbing |
| 2 | ✅ | Custom speedometer renderer base |
| 3 | ✅ | Multi-style + theming system |
| 4 | ✅ | Scaling modes & thresholds |
| 5 | ✅ | Provider switch + Bybit dual-market fallback |
| 6 | ✅ | Grid 1×1..7×7 + TOP50 auto-fill |
| 7 | ✅ | Segment Bar & Dual Arc styles, chart toggles |
| 8 | ✅ | Pseudo tickers & compare workers |
| 9 | ✅ | Technical indicators (RSI, MACD, BB) |
| 10 | ✅ | Transition animations & anomaly detectors v1 |
| 11 | ✅ | Advanced anomalies (divergence, histogram surge, clustered Z, regime shift) |

## 🆕 What's New in v1.1.2
Enhancements
- History persistence with JSONL/SQLite backends; UI “История”: Save/Load/Clear, backend toggle, auto-save
- Time-based compare chart (QDateTimeAxis), adaptive tick count and label formats
- Left-click tooltip on compare: nearest series at cursor time with symbol/price
- Intervals: 30m/1h/2h/4h/12h/24h/48h and “Auto” resampling step to keep charts snappy
- Per-widget Sensitivity: needle gain, auto-collapse window, spike expand, minimum window width

Fixes & Polishing
- Duplicate axis labels in compare fixed via persistent axes and series cleanup
- Adaptive resampling to cap points/series and reduce memory/CPU usage
- Console logging for history ops (save/load/clear)

## 🤝 **Contributing**
This is a personal project showcasing modern Qt development practices. Feel free to explore the code and adapt it for your own projects.

## 📄 **License**
Private project - All rights reserved.

## 🙏 **Acknowledgments**
- **Binance API** for real-time cryptocurrency data
- **Qt Framework** for excellent cross-platform development tools
- **Modern C++** community for best practices and patterns

---
*Built with ❤️ and Qt6*