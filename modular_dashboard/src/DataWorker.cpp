#include "DataWorker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QDateTime>
#include <QRandomGenerator>
#include <QDebug>

DataWorker::DataWorker(QObject* parent) : QObject(parent) {
    last_report_time = QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

void DataWorker::setMode(StreamMode m) { mode = m; }

void DataWorker::start() {
    if (running) return; running = true; connectWebSocket();
}

void DataWorker::stop() {
    running = false; startPingWatchdog(false);
    if (webSocket && webSocket->state() != QAbstractSocket::UnconnectedState) webSocket->close();
}

void DataWorker::setCurrencies(const QStringList& list) {
    subscribedCurrencies = list;
    for (const QString& c : subscribedCurrencies) if (!msg_counter.contains(c)) msg_counter[c]=0;
    if (running) {
        if (webSocket) { webSocket->close(); webSocket->deleteLater(); webSocket=nullptr; }
        startPingWatchdog(false); connectWebSocket();
    }
}

void DataWorker::onConnected() {
    reconnectAttempt = 0; lastPongMs = nowMs(); lastMsgMs = nowMs(); startPingWatchdog(true);
}

void DataWorker::onDisconnected() { startPingWatchdog(false); scheduleReconnect(); }

void DataWorker::onError(QAbstractSocket::SocketError) { startPingWatchdog(false); scheduleReconnect(); }

void DataWorker::processMessage(const QString& message) {
    if (!running) return;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return; QJsonObject root = doc.object(); if (!root.contains("data")) return; QJsonObject data = root["data"].toObject();
    QString symbol = data.value("s").toString(); if (symbol.isEmpty()) return; QString currency = symbol.left(symbol.length()-4).toUpper();
    double price=0.0, timestamp=0.0;
    if (mode==StreamMode::Trade) { price = data.value("p").toString().toDouble(); timestamp = data.value("T").toDouble()/1000.0; }
    else { price = data.value("c").toString().toDouble(); timestamp = data.value("E").toDouble()/1000.0; }
    if (price<=0 || timestamp<=0) return;
    if (msg_counter.contains(currency)) msg_counter[currency]++;
    double now = QDateTime::currentMSecsSinceEpoch()/1000.0;
    if (now - last_report_time >= 10) {
        double interval = now - last_report_time; const auto list = subscribedCurrencies;
        for (const QString& cur : list) { qDebug() << QString("  %1: %2 msg/s").arg(cur).arg(msg_counter[cur]/interval,0,'f',2); msg_counter[cur]=0; }
        last_report_time = now;
    }
    lastMsgMs = nowMs(); emit dataUpdated(currency, price, timestamp);
}

void DataWorker::connectWebSocket() {
    if (!running) return;
    if (webSocket) { webSocket->deleteLater(); webSocket=nullptr; }
    webSocket = new QWebSocket();
    connect(webSocket, &QWebSocket::connected, this, &DataWorker::onConnected);
    connect(webSocket, &QWebSocket::disconnected, this, &DataWorker::onDisconnected);
    connect(webSocket, &QWebSocket::errorOccurred, this, &DataWorker::onError);
    connect(webSocket, &QWebSocket::textMessageReceived, this, &DataWorker::processMessage);
    connect(webSocket, &QWebSocket::pong, this, [this](quint64, const QByteArray&){ lastPongMs = nowMs(); });
    QStringList streams; const QString suffix = (mode==StreamMode::Trade)? "@trade" : "@ticker";
    const auto& list = subscribedCurrencies;
    for (const QString& cur : list) streams << QString("%1usdt%2").arg(cur.toLower(), suffix);
    const QString url = QString("wss://stream.binance.com:9443/stream?streams=%1").arg(streams.join('/'));
    webSocket->open(QUrl(url));
}

void DataWorker::scheduleReconnect() {
    if (!running) return;
    int base=1000; int delay = base * (1 << std::min(reconnectAttempt, 5)); delay = std::min(delay, 30000);
    delay += int(QRandomGenerator::global()->bounded(500)); reconnectAttempt++;
    QTimer::singleShot(delay, this, &DataWorker::connectWebSocket);
}

void DataWorker::startPingWatchdog(bool enable) {
    if (enable) {
        if (!pingTimer) {
            pingTimer = new QTimer(this);
            connect(pingTimer, &QTimer::timeout, this, [this](){ if (webSocket && webSocket->state()==QAbstractSocket::ConnectedState) webSocket->ping("ka"); });
        }
        if (!watchdogTimer) {
            watchdogTimer = new QTimer(this);
            connect(watchdogTimer, &QTimer::timeout, this, [this](){
                qint64 now = nowMs(); bool pongStale = (now - lastPongMs) > pongTimeoutMs; bool idle = (now - lastMsgMs) > idleTimeoutMs;
                if (pongStale || idle) { if (webSocket) { webSocket->close(); webSocket->deleteLater(); webSocket=nullptr; } startPingWatchdog(false); scheduleReconnect(); }
            });
        }
        lastPongMs = nowMs(); lastMsgMs = nowMs(); pingTimer->start(pingIntervalMs); watchdogTimer->start(watchdogIntervalMs);
    } else {
        if (pingTimer) pingTimer->stop(); if (watchdogTimer) watchdogTimer->stop();
    }
}

qint64 DataWorker::nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
