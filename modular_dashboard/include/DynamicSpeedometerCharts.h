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
public:
    enum class SpeedometerStyle { Classic, NeonGlow, Minimal, ModernTicks, Circle, Gauge, Ring, SegmentBar, DualArc };
    enum class ScalingMode { Fixed, Adaptive, Manual, PythonLike };
    struct ScalingSettings { ScalingMode mode = ScalingMode::Adaptive; double fixedMin = 0.0; double fixedMax = 100.0; };
    explicit DynamicSpeedometerCharts(const QString& currency, QWidget* parent=nullptr);
    void applyPerformance(int animMs, int renderMs, int cacheMs, int volWindowSize, int maxPts, int rawCacheSize);
    void setRawCacheSize(int sz);
    void updateData(double price, double timestamp, double btcPrice = 0);
    void setCurrencyName(const QString& name);
    void setSpeedometerStyle(SpeedometerStyle s) { style = s; if (modeView=="speedometer") update(); }
    SpeedometerStyle speedometerStyle() const { return style; }
    struct Thresholds { bool enabled=false; int warn=70; int danger=90; };
    void applyThresholds(const Thresholds& t) { thresholds = t; if (modeView=="speedometer") update(); }
    void applyThemeColors(const SpeedometerColors& colors) { themeColors = colors; if (modeView=="speedometer") update(); }
    void applyScaling(const ScalingSettings& s) {
        auto oldMode = scaling.mode;
        scaling = s;
        if (s.mode != oldMode) { cachedMinVal.reset(); cachedMaxVal.reset(); }
        if (!history.empty()) { updateBounds(history.back().second); }
        if (modeView=="speedometer") update();
    }
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
    // Market badge API
    void setMarketBadge(const QString& provider, const QString& market) {
        providerName = provider; marketName = market;
        setProperty("providerName", providerName);
        setProperty("marketName", marketName);
        if (modeView=="speedometer") update();
    }
    void setUnsupportedReason(const QString& reason) { unsupportedMsg = reason; if (modeView=="speedometer") update(); }
signals:
    void valueChanged(double newValue);
    void requestRename(const QString& currentTicker);
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
private:
    QString currency; double _value=0; QString modeView="speedometer"; QPropertyAnimation* animation=nullptr;
    QTimer* renderTimer=nullptr; QTimer* cacheUpdateTimer=nullptr; int volatilityWindow=800, maxPoints=800, sampleMethod=0, cacheSize=20000; QMap<QString,int> timeScales; QString currentScale="5m";
    bool showAxisLabels=false, showTooltips=false, smoothLines=false, trendColors=false, logScale=false, highlightLast=true, showGrid=true; double volatility=0.0; double btcPrice=0.0; std::optional<double> cachedMinVal, cachedMaxVal; bool dataNeedsRedraw=false;
    std::deque<std::pair<double,double>> history, btcRatioHistory; std::vector<double> cachedProcessedHistory, cachedProcessedBtcRatio;
    QChart* chart=nullptr; QChartView* chartView=nullptr; QLineSeries* seriesNormal=nullptr; QLineSeries* seriesRatio=nullptr; QValueAxis* axisX=nullptr; QValueAxis* axisY=nullptr;
    SpeedometerStyle style = SpeedometerStyle::Classic; Thresholds thresholds{}; SpeedometerColors themeColors{}; ScalingSettings scaling{};
    // Adaptive (EWMA-like) params
    double minInit=0.9995, maxInit=1.0005, minFactor=1.00001, maxFactor=0.99999;
    // Python-like scaling params (compressing window each tick)
    double pyInitSpanPct=0.005;        // ±0.5%
    double pyMinCompress=1.000001;     // min *= 1.000001
    double pyMaxCompress=0.999999;     // max *= 0.999999
    double pyMinWidthPct=0.0001;       // at least price*0.01%
    // Market badge state
    QString providerName;
    QString marketName;
    QString unsupportedMsg; // non-empty => show warning badge
};
