#include "MarketAnalyzer.h"
#include <QtGlobal>
#include <cmath>
#include <algorithm>

MarketAnalyzer::MarketAnalyzer(QObject* parent) : QObject(parent) {}

void MarketAnalyzer::setConfig(const Config& c) {
    cfg = c;
    computeAndEmit();
}

void MarketAnalyzer::reset() {
    series.clear();
    computeAndEmit();
}

void MarketAnalyzer::updateSymbol(const QString& sym, double ts, double normalized01_100, double volatilityPct) {
    QString key = sym.toUpper();
    if (!cfg.includeBTC && key=="BTC") return;
    if (cfg.excluded.contains(key)) return;
    auto& s = series[key];
    s.lastTs = ts; s.lastV = normalized01_100; s.lastVolatility = std::max(0.0, volatilityPct);
    s.points.push_back({ts, normalized01_100});
    trimOld(s.points, ts, cfg.windowSeconds);
    // Update EMA of slope per series for uncertainty estimation
    if (s.points.size() >= 4) {
        double slope = regressionSlope(s.points); // in units per second of normalized value
        double alpha = 0.2; // responsiveness
        if (!s.seeded) { s.emaSlope = slope; s.emaVar = 0.0; s.seeded = true; }
        else {
            double prev = s.emaSlope;
            s.emaSlope = (1-alpha)*s.emaSlope + alpha*slope;
            double diff = slope - prev;
            // Simple EW variance of slope changes
            s.emaVar = (1-alpha)*s.emaVar + alpha*diff*diff;
        }
    }
    computeAndEmit();
}

double MarketAnalyzer::regressionSlope(const std::deque<Sample>& pts) {
    int n = int(pts.size()); if (n < 2) return 0.0;
    // Normalize time to start at 0 to improve numeric stability
    double t0 = pts.front().t;
    double sumT=0.0, sumV=0.0, sumTT=0.0, sumTV=0.0;
    for (const auto& p : pts) {
        double dt = p.t - t0; // seconds
        sumT += dt; sumV += p.v; sumTT += dt*dt; sumTV += dt*p.v;
    }
    double denom = n*sumTT - sumT*sumT; if (std::abs(denom) < 1e-9) return 0.0;
    double slope = (n*sumTV - sumT*sumV) / denom; // units per second
    // Convert to per-minute to be more interpretable
    return slope * 60.0;
}

void MarketAnalyzer::trimOld(std::deque<Sample>& pts, double now, int windowSec) {
    while (!pts.empty() && (now - pts.front().t) > windowSec) pts.pop_front();
}

void MarketAnalyzer::computeAndEmit() {
    if (series.isEmpty()) { emit snapshotUpdated({0.0,0.0,0.0, QObject::tr("недостаточно данных")}); return; }
    // Aggregate per-series slope and consensus
    double weightedSlopeSum = 0.0;
    double weightSum = 0.0;
    int agreeCount = 0, total=0;
    double meanAbsSlope = 0.0;
    // Use center at 50 to estimate direction if few points
    for (auto it = series.begin(); it != series.end(); ++it) {
        const auto& s = it.value();
        if (s.points.size() < 3) continue;
        double slope = regressionSlope(s.points); // per minute
        double w = 1.0;
        if (cfg.weighting == Weighting::InverseVolatility) {
            // more weight to calmer assets
            w = 1.0 / std::max(1e-6, (0.5 + s.lastVolatility));
        }
        weightedSlopeSum += w * slope;
        weightSum += w;
        meanAbsSlope += std::abs(slope);
        ++total;
    }
    if (weightSum <= 0.0 || total==0) { emit snapshotUpdated({0.0,0.0,0.0, QObject::tr("недостаточно данных")}); return; }
    double aggSlope = weightedSlopeSum / weightSum; // per minute
    meanAbsSlope /= total;
    // Normalize slope to [-1,1] using a soft scale: 0.5 units/min ~ strong trend
    double scale = 0.5; // units of normalized value per minute considered strong
    double index = std::clamp(aggSlope / scale, -1.5, 1.5);
    index = std::clamp(index, -1.0, 1.0);
    // Consensus: count series whose sign matches aggregate
    for (auto it = series.begin(); it != series.end(); ++it) {
        const auto& s = it.value(); if (s.points.size()<3) continue;
        double slope = regressionSlope(s.points);
        if ((aggSlope>=0 && slope>=0) || (aggSlope<0 && slope<0)) ++agreeCount;
    }
    double consensus = double(agreeCount) / double(std::max(1,total));
    // Strength derived from meanAbsSlope
    double strength = std::clamp(meanAbsSlope/scale, 0.0, 1.0);
    // Confidence combines consensus and inverse dispersion via slope variance
    double disp = 0.0; int nvar=0;
    for (auto it = series.begin(); it != series.end(); ++it) { if (it.value().seeded) { disp += it.value().emaVar; ++nvar; } }
    double avgVar = (nvar>0? disp/nvar : 0.0);
    // Map variance to [0,1] low variance -> 1
    double varScore = 1.0 / (1.0 + avgVar*20.0);
    double confidence = std::clamp(0.5*consensus + 0.5*varScore, 0.0, 1.0);
    // Label
    QString label;
    if (confidence < 0.35) label = QObject::tr("рынок нестабилен");
    else if (index > 0.6) label = QObject::tr("сильный рост");
    else if (index > 0.2) label = QObject::tr("рост");
    else if (index < -0.6) label = QObject::tr("сильный спад");
    else if (index < -0.2) label = QObject::tr("спад");
    else label = QObject::tr("боковое движение");
    emit snapshotUpdated({index, strength, confidence, label});
}
