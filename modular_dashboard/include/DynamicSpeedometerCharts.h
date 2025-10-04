#pragma once
#include <QWidget>
#include <QPropertyAnimation>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <deque>
#include <optional>
#include <vector>
#include <QMap>
#include <QString>
#include "TransitionOverlay.h"
#include <QPointer>
#include <QVector>

struct SpeedometerColors {
    QColor background;     // main widget background
    QColor arcBase;        // base arc color
    QColor needleNormal;   // needle color when no thresholds
    QColor zoneGood;       // green/safe zone
    QColor zoneWarn;       // yellow/warning zone  
    QColor zoneDanger;     // red/danger zone
    QColor text;           // text color
    QColor glow;           // glow/highlight effect
};

class DynamicSpeedometerCharts : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double value READ getValue WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(double volatility READ getVolatility NOTIFY volatilityChanged)
public:
    enum class SpeedometerStyle { Classic, NeonGlow, Minimal, ModernTicks, Circle, Gauge, Ring, SegmentBar, DualArc };
    enum class ScalingMode { Fixed, Adaptive, Manual, PythonLike, OldSchoolAdaptive, OldSchoolPythonLike, KiloCoderLike };
    struct ScalingSettings { ScalingMode mode = ScalingMode::Adaptive; double fixedMin = 0.0; double fixedMax = 100.0; int windowSize = 0; double paddingPct = 0.01; };
    explicit DynamicSpeedometerCharts(const QString& currency, QWidget* parent=nullptr);
    void applyPerformance(int animMs, int renderMs, int cacheMs, int volWindowSize, int maxPts, int rawCacheSize);
    void setRawCacheSize(int sz);
    void updateData(double price, double timestamp, double btcPrice = 0);
    void setCurrencyName(const QString& name);
    // Source kind for new history points (e.g., "TRADE" or "TICKER")
    void setSourceKind(const QString& kind) { currentSourceKind = kind; setProperty("sourceKind", currentSourceKind); }
    // Retention for raw history buffer in seconds (older points trimmed periodically)
    void setHistoryRetentionSeconds(double sec) { historyRetentionSec = std::clamp(sec, 60.0, 7*24*3600.0); }
    void setSpeedometerStyle(SpeedometerStyle s) { style = s; if (modeView=="speedometer") update(); }
    SpeedometerStyle speedometerStyle() const { return style; }
    struct Thresholds { bool enabled=false; int warn=70; int danger=90; };
    void applyThresholds(const Thresholds& t) { thresholds = t; if (modeView=="speedometer") update(); }
    void applyThemeColors(const SpeedometerColors& colors) { themeColors = colors; if (modeView=="speedometer") update(); }
    void applyScaling(const ScalingSettings& s) {
        auto oldMode = scalingSettings.mode;
        auto oldWindow = scalingSettings.windowSize;
        auto oldPadding = scalingSettings.paddingPct;
        scalingSettings = s;
        if (s.mode != oldMode || s.windowSize != oldWindow || s.paddingPct != oldPadding) { cachedMinVal.reset(); cachedMaxVal.reset(); }
        if (!history.empty()) { updateBounds(history.back().value); }
        if (modeView=="speedometer") update();
    }
    ScalingSettings scaling() const { return scalingSettings; }
    // Runtime tuning for Python-like scaling behavior
    void setPythonScalingParams(double initSpanPct, double minCompress, double maxCompress, double minWidthPct);
    // Chart options (affect line chart modes)
    void setChartOptions(bool grid, bool axisLabels) { showGrid = grid; showAxisLabels = axisLabels; updateChartSeries(); }
    void setSpeedometerColors(const QColor& primary, const QColor& secondary, const QColor& text, const QColor& background) {
        themeColors.zoneGood = primary; themeColors.arcBase = secondary; themeColors.text = text; themeColors.background = background;
        if (modeView=="speedometer") update();
    }
    void setThresholds(bool enabled, double warnValue, double dangerValue) {
        thresholds.enabled = enabled; thresholds.warn = warnValue; thresholds.danger = dangerValue;
        if (modeView=="speedometer") update();
    }
    // Volume visualization settings
    enum class VolumeVis { Off=0, Bar=1, Needle=2, Sidebar=3, Fuel=4 };
    void setVolumeVis(VolumeVis v) { volVis = v; if (modeView=="speedometer") update(); }
    VolumeVis volumeVis() const { return volVis; }
    void setVolumeVisByKey(const QString& key);
    // Global-control helpers (public setters for per-widget options)
    void setFrameStyleByName(const QString& name);
    void setSidebarWidthMode(int mode);
    void setSidebarOutline(bool enabled);
    void setSidebarBrightnessPct(int pct);
    void setRSIEnabled(bool enabled);
    void setMACDEnabled(bool enabled);
    void setBBEnabled(bool enabled);
    void setAnomalyEnabled(bool enabled);
    // key in {off,rsi,macd,bb,zscore,vol,comp,rsi_div,macd_hist,clustered_z,vol_regime}
    void setAnomalyModeByKey(const QString& key);
    void setOverlayVolatility(bool enabled);
    void setOverlayChange(bool enabled);
    void updateVolume(double volBase24h, double volQuote24h, double volIncrement, double ts) {
        // Compute increment: prefer per-trade increment (TRADE mode); fallback to 24h deltas per second (TICKER mode)
        double prevBase = lastVolBase;
        double prevQuote = lastVolQuote;
        double prevTs = lastVolTs;
        double inc = std::max(0.0, volIncrement);
        if (inc <= 0.0 && prevTs > 0.0 && ts > prevTs) {
            double dt = ts - prevTs;
            if (dt > 0.05) {
                double dBase = std::max(0.0, volBase24h - prevBase);
                double dQuote = std::max(0.0, volQuote24h - prevQuote);
                // Use the larger of base/quote deltas; normalize to per-second rate
                inc = std::max(dBase, dQuote) / dt;
            }
        }
        // Maintain a simple EMA of activity and a decaying max for normalization (0..1)
        volEma = volEma * 0.90 + inc * 0.10;
        volEmaMax = std::max(volEmaMax * 0.995, volEma);
        volNorm = (volEmaMax > 1e-12) ? std::clamp(volEma / volEmaMax, 0.0, 1.0) : 0.0;
        // Update last observed fields after using previous values for delta calc
        lastVolBase = volBase24h; lastVolQuote = volQuote24h; lastVolIncr = volIncrement; lastVolTs = ts;
        dataNeedsRedraw = true; if (modeView=="speedometer") update(); }
    // Market badge API
    void setMarketBadge(const QString& provider, const QString& market) {
        providerName = provider; marketName = market;
        setProperty("providerName", providerName);
        setProperty("marketName", marketName);
        if (modeView=="speedometer") update();
    }
    void setUnsupportedReason(const QString& reason) { unsupportedMsg = reason; if (modeView=="speedometer") update(); }
    // History snapshot (timestamp, price) in seconds
    QVector<QPair<double,double>> historySnapshot() const;
    // Extended history snapshot with metadata
    struct HistoryPoint { double ts=0.0; double value=0.0; QString source; QString provider; QString market; quint64 seq=0; };
    QVector<HistoryPoint> historySnapshotEx() const {
        QVector<HistoryPoint> out; out.reserve(int(history.size()));
        for (const auto& p : history) out.push_back(p);
        return out;
    }
    // Replace raw history with provided points (assumed sorted by ts)
    void replaceHistory(const QVector<HistoryPoint>& pts) {
        history.clear();
        for (const auto& p : pts) history.push_back(p);
        // Recompute cached metrics and redraw
        if (!history.empty()) {
            updateBounds(history.back().value);
            updateVolatility();
        }
        dataNeedsRedraw = true;
        if (modeView=="speedometer") update(); else updateChartSeries();
    }
signals:
    void valueChanged(double newValue);
    void volatilityChanged(double newVolatility);
    void requestRename(const QString& currentTicker);
    void requestChangeTicker(const QString& oldName, const QString& newName);
    void styleSelected(const QString& currency, const QString& styleName);
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
private slots:
    void onRenderTimeout();
    void setTimeScale(const QString& scale);
private:
    void setModeView(const QString& mv);
    void cacheChartData();
    std::vector<double> processHistory(bool useBtcRatio);
    void updateVolatility();
    void updateBounds(double price);
    void drawSpeedometer(QPainter& p);
    void updateChartSeries();
    double getValue() const { return _value; }
    void setValue(double v) { if (qFuzzyCompare(_value, v)) return; _value=v; emit valueChanged(v); }
    double getVolatility() const { return volatility; }
    QString buildComputedTooltip(const QString& token) const;
    // Indicator calculators (return same-length vectors or empty if insufficient)
    static std::vector<double> computeRSI(const std::vector<double>& values, int period = 14);
    struct MACDOut { std::vector<double> macd; std::vector<double> signal; };
    static MACDOut computeMACD(const std::vector<double>& values, int fast=12, int slow=26, int signal=9);
    struct BBOut { std::vector<double> upper; std::vector<double> lower; };
    static BBOut computeBollinger(const std::vector<double>& values, int period=20, double k=2.0);
    // Anomaly detection
    enum class AnomalyMode { Off, RSIOverboughtOversold, MACDCross, BollingerBreakout, ZScore, VolSpike, Composite,
                             RSIDivergence, MACDHistSurge, ClusteredZ, VolRegimeShift };
    static double computeZScore(const std::vector<double>& values, int window=50);
private:
    // Sensitivity tuning (new)
    // Needle gain: amplifies normalized [0..1] displacement around 0.5 -> 0.5 + (x-0.5)*gain
    double needleGain = 1.0;
    // Auto-collapse dynamic range around current price each tick (disabled by default)
    bool autoCollapseEnabled = false;
    // Each tick: range *= autoCollapseFactor (0.99..0.9999). Lower -> more aggressive collapse
    double autoCollapseFactor = 0.995;
    // On spikes (abs return >= spikeExpandThreshold), instant expand: range *= autoExpandFactor
    bool spikeExpandEnabled = true;
    double autoExpandFactor = 1.08;
    // Minimum range as a fraction of abs(price) to avoid zero-width windows
    double minRangePctOfPrice = 0.0005; // 0.05%
    // Spike threshold as fraction of price
    double spikeExpandThreshold = 0.004; // 0.4%
    double lastPriceForCollapse = 0.0;
    void loadSensitivityPrefs();
    void saveSensitivityPrefs() const;
    // Appearance: per-widget frame/border around the perimeter
    enum class FrameStyle { None=0, Minimal=1, Dashed=2, Glow=3 };
    FrameStyle frameStyle = FrameStyle::None;

    // Sidebar volume (VolumeVis::Sidebar) tuning
    // 0=Auto (based on widget size), 1=Narrow, 2=Medium, 3=Wide
    int sidebarWidthMode = 0;
    // Outline stroke for sidebar bar element
    bool sidebarOutline = true;
    // Brightness percentage 50..150 (100=default)
    int sidebarBrightnessPct = 100;

    // Transition helpers
    TransitionOverlay::Type transitionType = TransitionOverlay::Flip; // default
    bool transitionsEnabled = true;
    bool transitionActive = false; // true while overlay animation runs
    QPointer<TransitionOverlay> activeOverlay; // currently running overlay
    void animateViewSwitch(const QString& nextMode);
    QString currency; double _value=0; QString modeView="speedometer"; QPropertyAnimation* animation=nullptr;
    QTimer* renderTimer=nullptr; QTimer* cacheUpdateTimer=nullptr; int volatilityWindow=800, maxPoints=800, sampleMethod=0, cacheSize=20000; QMap<QString,int> timeScales; QString currentScale="5m";
    bool showAxisLabels=false, showTooltips=false, smoothLines=false, trendColors=false, logScale=false, highlightLast=true, showGrid=true; 
    bool showVolOverlay=false, showChangeOverlay=false;
    // Indicator toggles
    bool showRSI=false, showMACD=false, showBB=false;
    // Anomaly state
    bool showAnomalyBadge=false; AnomalyMode anomalyMode = AnomalyMode::Off; bool anomalyActive=false; QString anomalyLabel;
    double volatility=0.0; double btcPrice=0.0; std::optional<double> cachedMinVal, cachedMaxVal; bool dataNeedsRedraw=false;
    std::deque<HistoryPoint> history, btcRatioHistory; std::vector<double> cachedProcessedHistory, cachedProcessedBtcRatio;
    QChart* chart=nullptr; QChartView* chartView=nullptr; QLineSeries* seriesNormal=nullptr; QLineSeries* seriesRatio=nullptr; 
    // Indicators series
    QLineSeries* rsiSeries=nullptr; QLineSeries* macdSeries=nullptr; QLineSeries* macdSignalSeries=nullptr; QLineSeries* bbUpperSeries=nullptr; QLineSeries* bbLowerSeries=nullptr;
    // Axes
    QValueAxis* axisX=nullptr; QValueAxis* axisY=nullptr; QValueAxis* axisRSI=nullptr; QValueAxis* axisMACD=nullptr;
    SpeedometerStyle style = SpeedometerStyle::Classic; Thresholds thresholds{}; SpeedometerColors themeColors{}; ScalingSettings scalingSettings{};
    // Adaptive (EWMA-like) params
    double minInit=0.9995, maxInit=1.0005, minFactor=1.00001, maxFactor=0.99999;
    // Python-like scaling params (compressing window each tick)
    double pyInitSpanPct=0.005;        // ±0.5%
    double pyMinCompress=1.000001;     // min *= 1.000001
    double pyMaxCompress=0.999999;     // max *= 0.999999
    double pyMinWidthPct=0.0001;       // at least price*0.01%
    // KiloCoder Like scaling params (hierarchical window analysis)
    int shortWindowSize=50, mediumWindowSize=200, longWindowSize=800;
    double shortAlpha=0.1, mediumAlpha=0.05, longAlpha=0.02;
    std::optional<double> globalMin, globalMax;
    std::optional<double> shortMin, shortMax, mediumMin, mediumMax, longMin, longMax;
    double basePadding=0.01;
    // Market badge state
    QString providerName;
    QString marketName;
    QString unsupportedMsg; // non-empty => show warning badge
    // Volume state
    VolumeVis volVis = VolumeVis::Off;
    double lastVolBase=0.0, lastVolQuote=0.0, lastVolIncr=0.0; double lastVolTs=0.0;
    // Smoothed activity and normalization for volume overlays
    double volEma = 0.0;     // EMA of incremental trade volume
    double volEmaMax = 1e-9; // decaying maximum for normalization
    double volNorm = 0.0;    // normalized 0..1 activity level
    // History retention and metadata
    double historyRetentionSec = 48*3600.0; // keep ~48h of raw points by default
    QString currentSourceKind = ""; // "TRADE" or "TICKER" for new points
    quint64 seqCounter = 0;
};
