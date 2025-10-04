#include "HistoryStorage.h"
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTextStream>
#include <QDir>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QDebug>

HistoryStorage::HistoryStorage(QObject* parent) : QObject(parent) {}

QVector<HistoryBundle> HistoryStorage::collect(const QMap<QString, DynamicSpeedometerCharts*>& widgets) {
    QVector<HistoryBundle> out; out.reserve(widgets.size());
    for (auto it = widgets.begin(); it != widgets.end(); ++it) {
        auto* w = it.value(); if (!w) continue;
        HistoryBundle b; b.symbol = it.key();
        b.provider = w->property("providerName").toString();
        b.market = w->property("marketName").toString();
        b.source = w->property("sourceKind").toString();
        for (const auto& p : w->historySnapshotEx()) {
            HistoryRecord r; r.symbol = b.symbol; r.provider=b.provider; r.market=b.market; r.source=p.source; r.seq=p.seq; r.ts=p.ts; r.value=p.value; b.points.push_back(r);
        }
        out.push_back(b);
    }
    return out;
}

bool HistoryStorage::save(const QVector<HistoryBundle>& bundles, const QString& path, QString* error) {
    qInfo() << "HistoryStorage::save backend=" << (backend_==Backend::SQLite? "SQLite":"JSONL") << "path=" << path << "bundles=" << bundles.size();
    bool ok = backend_==Backend::SQLite ? saveSql(bundles, path, error) : saveJsonl(bundles, path, error);
    if (!ok) qWarning() << "HistoryStorage::save FAILED:" << (error? *error : QString("unknown error"));
    else qInfo() << "HistoryStorage::save OK";
    return ok;
}

bool HistoryStorage::load(QVector<HistoryBundle>* outBundles, const QString& path, QString* error) {
    qInfo() << "HistoryStorage::load backend=" << (backend_==Backend::SQLite? "SQLite":"JSONL") << "path=" << path;
    bool ok = backend_==Backend::SQLite ? loadSql(outBundles, path, error) : loadJsonl(outBundles, path, error);
    if (!ok) qWarning() << "HistoryStorage::load FAILED:" << (error? *error : QString("unknown error"));
    else qInfo() << "HistoryStorage::load OK bundles=" << (outBundles? outBundles->size() : 0);
    return ok;
}

bool HistoryStorage::clear(const QString& path, QString* error) {
    qInfo() << "HistoryStorage::clear backend=" << (backend_==Backend::SQLite? "SQLite":"JSONL") << "path=" << path;
    bool ok = backend_==Backend::SQLite ? clearSql(path, error) : clearJsonl(path, error);
    if (!ok) qWarning() << "HistoryStorage::clear FAILED:" << (error? *error : QString("unknown error"));
    else qInfo() << "HistoryStorage::clear OK";
    return ok;
}

// JSONL implementation
static QJsonObject toJson(const HistoryRecord& r) {
    QJsonObject o; o["symbol"]=r.symbol; o["provider"]=r.provider; o["market"]=r.market; o["source"]=r.source;
    o["seq"]=double(r.seq); o["ts"]=r.ts; o["value"]=r.value; return o;
}

bool HistoryStorage::saveJsonl(const QVector<HistoryBundle>& bundles, const QString& path, QString* error) {
    QFile f(path); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) { if (error) *error=f.errorString(); return false; }
    QTextStream ts(&f); ts.setEncoding(QStringConverter::Utf8);
    qint64 count=0;
    for (const auto& b : bundles) {
        for (const auto& r : b.points) {
            QJsonObject o = toJson(r);
            QJsonDocument doc(o); ts << doc.toJson(QJsonDocument::Compact) << '\n';
            ++count;
        }
    }
    qInfo() << "HistoryStorage::saveJsonl wrote records:" << count;
    return true;
}

bool HistoryStorage::loadJsonl(QVector<HistoryBundle>* outBundles, const QString& path, QString* error) {
    outBundles->clear();
    QFile f(path); if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { if (error) *error=f.errorString(); return false; }
    QMap<QString, HistoryBundle> bySymbol;
    QTextStream ts(&f); ts.setEncoding(QStringConverter::Utf8);
    int lineNo=0; int recs=0; while (!ts.atEnd()) {
        const QString line = ts.readLine(); ++lineNo; if (line.trimmed().isEmpty()) continue;
        QJsonParseError pe; QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) { if (error) *error = QString("JSON parse error at line %1: %2").arg(lineNo).arg(pe.errorString()); return false; }
        QJsonObject o = doc.object();
        HistoryRecord r; r.symbol=o.value("symbol").toString(); r.provider=o.value("provider").toString(); r.market=o.value("market").toString(); r.source=o.value("source").toString();
        r.seq = quint64(o.value("seq").toDouble()); r.ts=o.value("ts").toDouble(); r.value=o.value("value").toDouble();
        if (r.symbol.isEmpty() || r.ts<=0.0) { if (error) *error = QString("Invalid record at line %1").arg(lineNo); return false; }
        auto& b = bySymbol[r.symbol]; if (b.symbol.isEmpty()) { b.symbol=r.symbol; b.provider=r.provider; b.market=r.market; b.source=r.source; }
        b.points.push_back(r);
        ++recs;
    }
    for (auto it=bySymbol.begin(); it!=bySymbol.end(); ++it) outBundles->push_back(it.value());
    qInfo() << "HistoryStorage::loadJsonl read records:" << recs << "bundles:" << outBundles->size();
    return true;
}

bool HistoryStorage::clearJsonl(const QString& path, QString* error) {
    QFile f(path); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) { if (error) *error=f.errorString(); return false; }
    return true;
}

// SQLite implementation
static bool ensureTable(QSqlDatabase& db, QString* err) {
    QSqlQuery q(db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS history_records (symbol TEXT, provider TEXT, market TEXT, source TEXT, seq INTEGER, ts REAL, value REAL)")) { if (err) *err=q.lastError().text(); return false; }
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_symbol_ts ON history_records(symbol, ts)")) { if (err) *err=q.lastError().text(); return false; }
    return true;
}

bool HistoryStorage::saveSql(const QVector<HistoryBundle>& bundles, const QString& path, QString* error) {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "hist_save"); db.setDatabaseName(path);
    if (!db.open()) { if (error) *error = db.lastError().text(); return false; }
    if (!ensureTable(db, error)) { db.close(); QSqlDatabase::removeDatabase("hist_save"); return false; }
    QSqlQuery q(db); db.transaction();
    q.prepare("INSERT INTO history_records(symbol,provider,market,source,seq,ts,value) VALUES(?,?,?,?,?,?,?)");
    qint64 count=0;
    for (const auto& b : bundles) for (const auto& r : b.points) { q.addBindValue(r.symbol); q.addBindValue(r.provider); q.addBindValue(r.market); q.addBindValue(r.source); q.addBindValue(QVariant::fromValue<qulonglong>(r.seq)); q.addBindValue(r.ts); q.addBindValue(r.value); if (!q.exec()) { if (error) *error=q.lastError().text(); db.rollback(); db.close(); QSqlDatabase::removeDatabase("hist_save"); return false; } ++count; }
    db.commit(); db.close(); QSqlDatabase::removeDatabase("hist_save"); return true;
}

bool HistoryStorage::loadSql(QVector<HistoryBundle>* outBundles, const QString& path, QString* error) {
    outBundles->clear();
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "hist_load"); db.setDatabaseName(path);
    if (!db.open()) { if (error) *error=db.lastError().text(); return false; }
    if (!ensureTable(db, error)) { db.close(); QSqlDatabase::removeDatabase("hist_load"); return false; }
    QMap<QString, HistoryBundle> bySymbol;
    QSqlQuery q(db);
    if (!q.exec("SELECT symbol,provider,market,source,seq,ts,value FROM history_records ORDER BY symbol, ts")) { if (error) *error=q.lastError().text(); db.close(); QSqlDatabase::removeDatabase("hist_load"); return false; }
    qint64 recs=0; while (q.next()) {
        HistoryRecord r; r.symbol=q.value(0).toString(); r.provider=q.value(1).toString(); r.market=q.value(2).toString(); r.source=q.value(3).toString(); r.seq=q.value(4).toULongLong(); r.ts=q.value(5).toDouble(); r.value=q.value(6).toDouble();
        if (r.symbol.isEmpty() || r.ts<=0.0) { if (error) *error = QString("Invalid row for symbol '%1'").arg(r.symbol); db.close(); QSqlDatabase::removeDatabase("hist_load"); return false; }
        auto& b = bySymbol[r.symbol]; if (b.symbol.isEmpty()) { b.symbol=r.symbol; b.provider=r.provider; b.market=r.market; b.source=r.source; }
        b.points.push_back(r);
        ++recs;
    }
    db.close(); QSqlDatabase::removeDatabase("hist_load");
    for (auto it=bySymbol.begin(); it!=bySymbol.end(); ++it) outBundles->push_back(it.value());
    qInfo() << "HistoryStorage::loadSql read records:" << recs << "bundles:" << outBundles->size();
    return true;
}

bool HistoryStorage::clearSql(const QString& path, QString* error) {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "hist_clear"); db.setDatabaseName(path);
    if (!db.open()) { if (error) *error=db.lastError().text(); return false; }
    if (!ensureTable(db, error)) { db.close(); QSqlDatabase::removeDatabase("hist_clear"); return false; }
    QSqlQuery q(db);
    if (!q.exec("DELETE FROM history_records")) { if (error) *error=q.lastError().text(); db.close(); QSqlDatabase::removeDatabase("hist_clear"); return false; }
    db.close(); QSqlDatabase::removeDatabase("hist_clear"); return true;
}
