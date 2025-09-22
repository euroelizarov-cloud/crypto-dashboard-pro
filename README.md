# 📊 Crypto Dashboard Pro  
[![Latest Release](https://img.shields.io/github/v/release/euroelizarov-cloud/crypto-dashboard-pro?sort=semver&label=latest)](https://github.com/euroelizarov-cloud/crypto-dashboard-pro/releases/latest)

Modern real-time cryptocurrency dashboard built with Qt6 and C++. Features advanced speedometer-style visualizations with multiple themes and customizable displays.

## ✨ Features

### 🎛️ **Advanced Speedometer Styles**
- **Classic** - Traditional speedometer with red needle tip
- **NeonGlow** - Futuristic neon lighting effects
- **Minimal** - Clean minimalist design
- **ModernTicks** - Contemporary tick marks
- **Classic Pro** - Professional traditional style with detailed markings
- **Gauge** - Clean gauge with value indicators
- **Modern Scale** - Contemporary linear-style scale design with segmented progress
- **Segment Bar** - Bold segmented arc with clean center labels
- **Dual Arc** - Outer arc shows value, inner arc shows volatility

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

### ⚙️ **Smart Auto-Scaling**
- **Adaptive** - Automatic scaling based on data range
- **Fixed (0-100)** - Fixed percentage range
- **Manual** - Custom min/max bounds
- **Per-widget** - Individual scaling per cryptocurrency

### 🔧 **Advanced Features**
- **Real-time WebSocket** connection to Binance & Bybit APIs
- **Bybit dual-market fallback** - Linear/Spot auto-fallback per symbol
- **Provider & market badges** - Visible on each widget
- **Per-widget customization** - Individual styles and settings
- **Threshold highlighting** - Color-coded warning zones
- **Performance monitoring** - Built-in profiling and optimization
- **Persistent settings** - All customizations saved automatically
- **Context menus** - Right-click for quick style changes
- **Memory management** - Configurable cache limits and data retention
- **Chart options** - Toggle grid and axis labels in chart modes

## 🚀 **Live Data Support**
- **Top tickers**: BTC, ETH, XRP, BNB, SOL, DOGE, XLM, HBAR, APT, TAO, LAYER, TON (extendable)
- **Real-time updates** from Binance & Bybit
- **Trade/Ticker modes** with automatic fallback
- **Bybit dual-market logic** with per-symbol tracking
- **Performance metrics** showing update rates

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

- macOS app (v0.4.0): https://github.com/euroelizarov-cloud/crypto-dashboard-pro/releases/download/v0.4.0/modular_dashboard-v0.4.0-macos.zip

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
- ✅ **Phase 1**: Basic dashboard with Qt Charts integration
- ✅ **Phase 2**: Custom speedometer visualizations
- ✅ **Phase 3**: Multiple styles and themes
- ✅ **Phase 4**: Advanced customization and scaling
- ✅ **Phase 5**: Provider switching, Bybit dual-market fallback, market badges
- ✅ **Phase 6**: Grid layouts 1x1…7x7 with TOP50 auto-fill
- ✅ **Phase 7**: New styles (Segment Bar, Dual Arc), chart grid/labels toggles

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