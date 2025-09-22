#pragma once
#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QMap>
#include <QStringList>
#include <QSet>
#include <atomic>

enum class StreamMode { Trade, Ticker };
enum class DataProvider { Binance, Bybit };
enum class BybitMarket { Spot, Linear };
enum class BybitPreference { LinearFirst, SpotFirst };

class DataWorker : public QObject {
    Q_OBJECT
public:
    explicit DataWorker(QObject* parent=nullptr);
    // These change internal socket state; ensure they run in the worker thread
    Q_INVOKABLE void setMode(StreamMode m);
    Q_INVOKABLE void setProvider(DataProvider p);
    Q_INVOKABLE void setBybitPreference(BybitPreference pref) { bybitPreference = pref; }
    Q_INVOKABLE void setAllowBybitFallback(bool allow) { allowBybitFallback = allow; }
public slots:
    void start();
    void stop();
    void setCurrencies(const QStringList& list);
signals:
    void dataUpdated(const QString& currency, double price, double timestamp);
    void workerError(const QString& errorString);
    // New: enriched tick with provider/market for UI badges
    void dataTick(const QString& currency, double price, double timestamp, const QString& providerName, const QString& marketName);
    void unsupportedSymbol(const QString& currency, const QString& reason);
private slots:
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError);
    void processMessage(const QString& message);
private:
    void connectWebSocket();
    void scheduleReconnect();
    void startPingWatchdog(bool enable);
    static qint64 nowMs();
    QStringList buildBybitArgs() const; // helper for Bybit subscriptions
    void sendBybitSubscriptions(QWebSocket* target, const QStringList& args, BybitMarket market);
    void handleBybitSubscribeAck(const QJsonObject& root);
private:
    // Primary socket (Binance or Bybit primary market)
    QWebSocket* webSocket=nullptr;
    // Bybit alternate market socket (created only for Bybit provider)
    QWebSocket* bybitAlt=nullptr;
    QMap<QString,int> msg_counter;
    double last_report_time=0.0;
    std::atomic<bool> running=false;
    StreamMode mode=StreamMode::Trade;
    DataProvider provider=DataProvider::Binance;
    QStringList subscribedCurrencies;
    int reconnectAttempt=0;
    QTimer* pingTimer=nullptr; QTimer* watchdogTimer=nullptr; qint64 lastPongMs=0; qint64 lastMsgMs=0;
    const int pingIntervalMs = 10000; // 10s
    const int watchdogIntervalMs = 5000; // 5s
    const int pongTimeoutMs = 20000; // 20s
    const int idleTimeoutMs = 30000; // 30s
    bool reconnectPlanned = false; // prevent double reconnects when we intentionally restart socket
    QSet<QString> invalidSymbolsBybit; // uppercase currency codes unsupported on Bybit both markets
    QSet<QString> invalidLinearBybit;  // uppercase currency codes unsupported on Linear
    QSet<QString> invalidSpotBybit;    // uppercase currency codes unsupported on Spot
    BybitPreference bybitPreference = BybitPreference::LinearFirst;
    bool allowBybitFallback = true; // when false, do not fall back to alternate Bybit market
    // Track where we last attempted a subscription for a symbol
    QHash<QString, BybitMarket> lastSubMarket;
};
