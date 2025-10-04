#pragma once
#include <QObject>
#include <QString>
#include <QSet>
#include <QMap>
#include <deque>

struct MarketSnapshot {
    // Index in [-1,1]: -1 strong drop, 0 neutral/sideways, +1 strong rise
    double index = 0.0;
    // Strength of the aggregated trend [0,1]
    double strength = 0.0;
    // Confidence [0,1] based on consensus and dispersion
    double confidence = 0.0;
    // Brief comment like "все падает", "рост", "нестабильно" etc.
    QString label;
};

class MarketAnalyzer : public QObject {
    Q_OBJECT
public:
    enum class Weighting { Equal, InverseVolatility };
    struct Config {
        int windowSeconds = 900; // default 15m
        bool includeBTC = true;
        Weighting weighting = Weighting::Equal;
        QSet<QString> excluded; // uppercase symbols to exclude
    };
    explicit MarketAnalyzer(QObject* parent=nullptr);
    void setConfig(const Config& c);
    const Config& config() const { return cfg; }
    // Called from UI thread for each symbol update
    void updateSymbol(const QString& symbol, double ts, double normalized01_100, double volatilityPct);
    // Clear all time series
    void reset();
signals:
    void snapshotUpdated(const MarketSnapshot& snapshot);
private:
    struct Sample { double t; double v; };
    struct Series {
        std::deque<Sample> points; // normalized [0..100]
        // Running stats for slope (EMA mean/var)
        double emaSlope = 0.0;
        double emaVar = 0.0;
        bool seeded = false;
        double lastTs = 0.0;
        double lastV = 50.0;
        double lastVolatility = 0.0; // %
    };
    QMap<QString, Series> series; // key upper symbol
    Config cfg;
    // Helpers
    static double regressionSlope(const std::deque<Sample>& pts);
    void trimOld(std::deque<Sample>& pts, double now, int windowSec);
    void computeAndEmit();
};
