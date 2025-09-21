#pragma once
#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QMap>
#include <QStringList>
#include <atomic>

enum class StreamMode { Trade, Ticker };

class DataWorker : public QObject {
    Q_OBJECT
public:
    explicit DataWorker(QObject* parent=nullptr);
    void setMode(StreamMode m);
public slots:
    void start();
    void stop();
    void setCurrencies(const QStringList& list);
signals:
    void dataUpdated(const QString& currency, double price, double timestamp);
    void workerError(const QString& errorString);
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
private:
    QWebSocket* webSocket=nullptr;
    QMap<QString,int> msg_counter;
    double last_report_time=0.0;
    std::atomic<bool> running=false;
    StreamMode mode=StreamMode::Trade;
    QStringList subscribedCurrencies;
    int reconnectAttempt=0;
    QTimer* pingTimer=nullptr; QTimer* watchdogTimer=nullptr; qint64 lastPongMs=0; qint64 lastMsgMs=0;
    const int pingIntervalMs = 10000; // 10s
    const int watchdogIntervalMs = 5000; // 5s
    const int pongTimeoutMs = 20000; // 20s
    const int idleTimeoutMs = 30000; // 30s
};
