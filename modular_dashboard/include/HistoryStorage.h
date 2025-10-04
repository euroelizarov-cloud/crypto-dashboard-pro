#pragma once
#include <QObject>
#include <QMap>
#include <QString>
#include <QVector>
#include <QVariant>
#include <QDateTime>
#include <optional>
#include "DynamicSpeedometerCharts.h"

// Unified DTO for persistence (covers all params to restore)
struct HistoryRecord {
    QString symbol;           // ticker
    QString provider;         // e.g., Binance, Bybit
    QString market;           // e.g., Linear, Spot
    QString source;           // TRADE/TICKER
    quint64 seq = 0;          // sequence
    double ts = 0.0;          // seconds since epoch
    double value = 0.0;       // normalized price/value (0..100 typically)
};

// Structured snapshot per symbol for save/load
struct HistoryBundle {
    QString symbol;
    QString provider;
    QString market;
    QString source;
    QVector<HistoryRecord> points;
};

class HistoryStorage : public QObject {
    Q_OBJECT
public:
    enum class Backend { Jsonl, SQLite };
    explicit HistoryStorage(QObject* parent=nullptr);
    void setBackend(Backend b) { backend_ = b; }
    Backend backend() const { return backend_; }

    // Collect current history from widgets into bundles
    static QVector<HistoryBundle> collect(const QMap<QString, DynamicSpeedometerCharts*>& widgets);

    // Save/load API
    // path: file path for JSONL or SQLite db path
    // Returns true/false and error message on failure
    bool save(const QVector<HistoryBundle>& bundles, const QString& path, QString* error);
    bool load(QVector<HistoryBundle>* outBundles, const QString& path, QString* error);
    // Clear given backend storage (for SQLite); for Jsonl this truncates file
    bool clear(const QString& path, QString* error);
private:
    Backend backend_ = Backend::Jsonl;

    // JSONL helpers (newline-delimited JSON, one record per line)
    bool saveJsonl(const QVector<HistoryBundle>& bundles, const QString& path, QString* error);
    bool loadJsonl(QVector<HistoryBundle>* outBundles, const QString& path, QString* error);
    bool clearJsonl(const QString& path, QString* error);

    // SQLite helpers (single table history_records)
    bool saveSql(const QVector<HistoryBundle>& bundles, const QString& path, QString* error);
    bool loadSql(QVector<HistoryBundle>* outBundles, const QString& path, QString* error);
    bool clearSql(const QString& path, QString* error);
};
