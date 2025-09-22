#include "DataWorker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDateTime>
#include <QRandomGenerator>
#include <QDebug>
#include <QSet>
#include <QHash>

static inline double jsonToDouble(const QJsonValue& v) {
    if (v.isDouble()) return v.toDouble();
    if (v.isString()) return v.toString().toDouble();
    if (v.isNull() || v.isUndefined()) return 0.0;
    auto var = v.toVariant();
    bool ok=false; double d = var.toString().toDouble(&ok); if (ok) return d;
    return var.toDouble();
}

DataWorker::DataWorker(QObject* parent) : QObject(parent) {
    last_report_time = QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

void DataWorker::setMode(StreamMode m) {
    mode = m;
    qDebug() << "[DataWorker] setMode ->" << (mode==StreamMode::Trade?"TRADE":"TICKER");
}

void DataWorker::setProvider(DataProvider p) {
    provider = p;
    qDebug() << "[DataWorker] setProvider ->" << (provider==DataProvider::Binance?"Binance":"Bybit");
    if (running) {
        reconnectPlanned = true;
        if (webSocket) { webSocket->abort(); webSocket->deleteLater(); webSocket = nullptr; }
        startPingWatchdog(false);
        QTimer::singleShot(0, this, &DataWorker::connectWebSocket);
    }
}

void DataWorker::start() {
    if (running) return;
    running = true;
    qDebug() << "[DataWorker] start: provider=" << (provider==DataProvider::Binance?"Binance":"Bybit")
             << ", mode=" << (mode==StreamMode::Trade?"TRADE":"TICKER")
             << ", currencies=" << subscribedCurrencies;
    connectWebSocket();
}

void DataWorker::stop() {
    running = false;
    startPingWatchdog(false);
    qDebug() << "[DataWorker] stop";
    if (webSocket) {
        webSocket->abort(); webSocket->deleteLater(); webSocket = nullptr;
    }
}

void DataWorker::setCurrencies(const QStringList& list) {
    subscribedCurrencies = list;
    for (const QString& c : subscribedCurrencies) if (!msg_counter.contains(c)) msg_counter[c]=0;
    qDebug() << "[DataWorker] setCurrencies ->" << subscribedCurrencies;
    if (running) {
        reconnectPlanned = true;
        if (webSocket) { webSocket->abort(); webSocket->deleteLater(); webSocket=nullptr; }
        startPingWatchdog(false);
        QTimer::singleShot(0, this, &DataWorker::connectWebSocket);
    }
}

void DataWorker::onConnected() {
    reconnectAttempt = 0; lastPongMs = nowMs(); lastMsgMs = nowMs(); startPingWatchdog(true);
    qDebug() << "[DataWorker] connected to" << (provider==DataProvider::Binance?"Binance":"Bybit");
    if (provider == DataProvider::Bybit) {
        const QStringList args = buildBybitArgs();
        // Send on primary market first according to preference
        const BybitMarket primary = (bybitPreference==BybitPreference::LinearFirst) ? BybitMarket::Linear : BybitMarket::Spot;
        sendBybitSubscriptions(webSocket, args, primary);
    }
}

void DataWorker::onDisconnected() {
    qDebug() << "[DataWorker] disconnected";
    if (webSocket) {
        qDebug() << "[DataWorker] close code/reason" << int(webSocket->closeCode()) << webSocket->closeReason();
    }
    startPingWatchdog(false);
    if (reconnectPlanned) {
        reconnectPlanned = false; // we'll reconnect via queued connectWebSocket already
        return;
    }
    scheduleReconnect();
}

void DataWorker::onError(QAbstractSocket::SocketError err) {
    qDebug() << "[DataWorker] socket error:" << int(err) << (webSocket? webSocket->errorString():QString());
    startPingWatchdog(false);
    if (reconnectPlanned) {
        reconnectPlanned = false;
        return;
    }
    scheduleReconnect();
}

void DataWorker::processMessage(const QString& message) {
    if (!running) return;
    if (message == "ping") { if (webSocket && webSocket->state()==QAbstractSocket::ConnectedState) webSocket->sendTextMessage("pong"); return; }
    QJsonParseError jerr; QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &jerr); if (jerr.error != QJsonParseError::NoError) return; if (!doc.isObject()) return; QJsonObject root = doc.object();
    if (root.value("op").toString() == "ping") { QJsonObject pong; pong["op"] = "pong"; if (root.contains("ts")) pong["ts"] = root.value("ts"); QJsonDocument d(pong); if (webSocket && webSocket->state()==QAbstractSocket::ConnectedState) webSocket->sendTextMessage(QString::fromUtf8(d.toJson(QJsonDocument::Compact))); lastPongMs = nowMs(); return; }
    if (root.value("op").toString() == "pong") { lastPongMs = nowMs(); return; }
    // Bybit subscription ack / error diagnostics
    if (provider == DataProvider::Bybit && root.contains("op") && root.value("op").toString() == "subscribe") {
        bool success = root.value("success").toBool(true);
        QString retMsg = root.value("ret_msg").toString();
        QJsonObject req = root.value("request").toObject();
        qDebug() << "Bybit subscribe ack: success=" << success << ", ret_msg=" << retMsg
                 << ", request=" << QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact));
        handleBybitSubscribeAck(root);
        // don't return; allow further parsing if data present, but most acks have no data
    }
    double price=0.0, timestamp=0.0; QString currency;
    if (provider == DataProvider::Bybit) { static int rawDbg = 0; if (rawDbg < 10) { qDebug() << "Bybit raw topic/type:" << root.value("topic").toString() << root.value("type").toString(); qDebug() << "Bybit raw:" << message.left(500); rawDbg++; } }
    if (provider == DataProvider::Binance) {
        if (!root.contains("data")) return; QJsonObject data = root["data"].toObject(); QString symbol = data.value("s").toString(); if (symbol.isEmpty()) return; currency = symbol.left(symbol.length()-4).toUpper(); if (mode==StreamMode::Trade) { price = jsonToDouble(data.value("p")); timestamp = jsonToDouble(data.value("T"))/1000.0; } else { price = jsonToDouble(data.value("c")); timestamp = jsonToDouble(data.value("E"))/1000.0; }
    } else {
        QString topic = root.value("topic").toString(); QJsonValue dataVal = root.value("data"); QJsonArray arr; QJsonObject obj; if (dataVal.isArray()) arr = dataVal.toArray(); else if (dataVal.isObject()) obj = dataVal.toObject(); QString symbol;
        if (!arr.isEmpty()) { QJsonObject e = arr.last().toObject(); symbol = e.value("symbol").toString(); if (symbol.isEmpty()) symbol = e.value("s").toString(); if (mode==StreamMode::Trade) { price = jsonToDouble(e.value("p")); timestamp = jsonToDouble(e.value("T"))/1000.0; } else { price = e.contains("lastPrice") ? jsonToDouble(e.value("lastPrice")) : jsonToDouble(e.value("lp")); double tsRoot = jsonToDouble(root.value("ts")); double tsData = jsonToDouble(e.value("ts")); timestamp = ((tsRoot>0? tsRoot : tsData))/1000.0; } }
        else if (!obj.isEmpty()) { symbol = obj.value("symbol").toString(); if (symbol.isEmpty()) symbol = obj.value("s").toString(); if (mode==StreamMode::Trade) { price = jsonToDouble(obj.value("p")); timestamp = jsonToDouble(obj.value("T"))/1000.0; } else { price = obj.contains("lastPrice") ? jsonToDouble(obj.value("lastPrice")) : jsonToDouble(obj.value("lp")); double tsRoot = jsonToDouble(root.value("ts")); double tsData = jsonToDouble(obj.value("ts")); timestamp = ((tsRoot>0? tsRoot : tsData))/1000.0; } }
        if (symbol.isEmpty() && !topic.isEmpty()) { auto parts = topic.split('.'); if (parts.size()>=2) symbol = parts.last(); }
        if (symbol.isEmpty()) return; currency = symbol.left(symbol.length()-4).toUpper();
    }
    if (price<=0 || timestamp<=0 || currency.isEmpty()) return;
    if (provider == DataProvider::Bybit) { static QHash<QString,int> dbgCount; int &c = dbgCount[currency]; if (c < 2) { qDebug() << "Bybit tick" << currency << "price" << price << "ts" << timestamp; c++; } }
    if (msg_counter.contains(currency)) msg_counter[currency]++;
    double now = QDateTime::currentMSecsSinceEpoch()/1000.0; if (now - last_report_time >= 10) { double interval = now - last_report_time; const auto list = subscribedCurrencies; for (const QString& cur : list) { qDebug() << QString("  %1: %2 msg/s").arg(cur).arg(msg_counter[cur]/interval,0,'f',2); msg_counter[cur]=0; } last_report_time = now; }
    lastMsgMs = nowMs();
    emit dataUpdated(currency, price, timestamp);
    // market tag: if topic contains tickers.* from Spot fallback, label Spot; otherwise Linear for Bybit; Binance as-is
    QString providerName = (provider==DataProvider::Binance) ? "Binance" : "Bybit";
    QString marketName = "";
    if (provider==DataProvider::Bybit) {
        BybitMarket m = lastSubMarket.value(currency, (bybitPreference==BybitPreference::LinearFirst)?BybitMarket::Linear:BybitMarket::Spot);
        marketName = (m==BybitMarket::Linear) ? "Linear" : "Spot";
    }
    emit dataTick(currency, price, timestamp, providerName, marketName);
}

void DataWorker::connectWebSocket() {
    if (!running) return;
    if (webSocket) { webSocket->abort(); webSocket->deleteLater(); webSocket=nullptr; }
    webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(webSocket, &QWebSocket::connected, this, &DataWorker::onConnected);
    connect(webSocket, &QWebSocket::disconnected, this, &DataWorker::onDisconnected);
    connect(webSocket, &QWebSocket::errorOccurred, this, &DataWorker::onError);
    connect(webSocket, &QWebSocket::textMessageReceived, this, &DataWorker::processMessage);
    connect(webSocket, &QWebSocket::pong, this, [this](quint64, const QByteArray&){ lastPongMs = nowMs(); });
    // Log SSL errors if any (macOS often blocks by corporate proxy/root)
    QObject::connect(webSocket, &QWebSocket::sslErrors, this, [this](const QList<QSslError>& errs){
        qDebug() << "[DataWorker] SSL errors:";
        for (const auto& e : errs) qDebug() << "   *" << e.errorString();
    });
    if (provider == DataProvider::Binance) {
        QStringList streams; const QString suffix = (mode==StreamMode::Trade)? "@trade" : "@ticker"; const auto& list = subscribedCurrencies; for (const QString& cur : list) streams << QString("%1usdt%2").arg(cur.toLower(), suffix);
        const QString url = QString("wss://stream.binance.com:9443/stream?streams=%1").arg(streams.join('/'));
        qDebug() << "[DataWorker] opening Binance WS:" << url;
        webSocket->open(QUrl(url));
    } else {
        // Bybit primary according to preference
        const bool linearPrimary = (bybitPreference==BybitPreference::LinearFirst);
        const QString primaryUrl = linearPrimary ? QStringLiteral("wss://stream.bybit.com/v5/public/linear?compress=false")
                                                 : QStringLiteral("wss://stream.bybit.com/v5/public/spot?compress=false");
        qDebug() << "[DataWorker] opening Bybit" << (linearPrimary?"Linear":"Spot") << "WS:" << primaryUrl;
        webSocket->open(QUrl(primaryUrl));
        connect(webSocket, &QWebSocket::binaryMessageReceived, this, [this,linearPrimary](const QByteArray& bin){ static int cnt = 0; if (cnt++ < 3) qDebug() << (linearPrimary?"Bybit Linear":"Bybit Spot") << "binary frame (ignored, expect compress=false): size" << bin.size(); });
        // Prepare alternate socket (the other market) for on-demand fallback
        if (!bybitAlt) {
            bybitAlt = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
            connect(bybitAlt, &QWebSocket::connected, this, [this](){ qDebug() << "[DataWorker] connected to Bybit alternate market"; });
            connect(bybitAlt, &QWebSocket::disconnected, this, [this](){ qDebug() << "[DataWorker] disconnected Bybit alternate market"; });
            connect(bybitAlt, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e){ qDebug() << "[DataWorker] Bybit alt market error:" << int(e) << bybitAlt->errorString(); });
            connect(bybitAlt, &QWebSocket::textMessageReceived, this, &DataWorker::processMessage);
            connect(bybitAlt, &QWebSocket::pong, this, [this](quint64, const QByteArray&){ lastPongMs = nowMs(); });
            QObject::connect(bybitAlt, &QWebSocket::sslErrors, this, [this](const QList<QSslError>& errs){ qDebug() << "[DataWorker] SSL errors (Bybit alt):"; for (const auto& e : errs) qDebug() << "   *" << e.errorString(); });
        }
    }
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
            connect(pingTimer, &QTimer::timeout, this, [this](){
                auto doPing = [](QWebSocket* s){ if (!s) return; if (s->state()!=QAbstractSocket::ConnectedState) return; s->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"op","ping"},{"ts",QDateTime::currentMSecsSinceEpoch()}}).toJson(QJsonDocument::Compact))); };
                auto doWsPing = [](QWebSocket* s){ if (!s) return; if (s->state()!=QAbstractSocket::ConnectedState) return; s->ping("ka"); };
                if (provider == DataProvider::Bybit) { doPing(webSocket); doPing(bybitAlt); }
                else { doWsPing(webSocket); }
            });
        }
        if (!watchdogTimer) {
            watchdogTimer = new QTimer(this);
            connect(watchdogTimer, &QTimer::timeout, this, [this](){
                qint64 now = nowMs(); bool pongStale = (provider==DataProvider::Bybit) ? false : ((now - lastPongMs) > pongTimeoutMs); bool idle = (now - lastMsgMs) > idleTimeoutMs;
                if (pongStale || idle) {
                    if (webSocket) { webSocket->close(); webSocket->deleteLater(); webSocket=nullptr; }
                    if (bybitAlt) { bybitAlt->close(); bybitAlt->deleteLater(); bybitAlt=nullptr; }
                    startPingWatchdog(false); scheduleReconnect();
                }
            });
        }
        lastPongMs = nowMs(); lastMsgMs = nowMs(); pingTimer->start(pingIntervalMs); watchdogTimer->start(watchdogIntervalMs);
    } else {
        if (pingTimer) pingTimer->stop(); if (watchdogTimer) watchdogTimer->stop();
    }
}

qint64 DataWorker::nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QStringList DataWorker::buildBybitArgs() const {
    QStringList args; const auto& list = subscribedCurrencies;
    if (mode==StreamMode::Trade) { for (const auto& cur : list) args << QString("publicTrade.%1USDT").arg(cur.toUpper()); }
    else { for (const auto& cur : list) args << QString("tickers.%1USDT").arg(cur.toUpper()); }
    return args;
}

void DataWorker::sendBybitSubscriptions(QWebSocket* target, const QStringList& args, BybitMarket market) {
    if (!target || target->state()!=QAbstractSocket::ConnectedState) return;
    const int chunk = 10; // bybit may limit per subscribe size; be conservative
    int batchIdx = 0;
    for (int i=0; i<args.size(); i+=chunk, ++batchIdx) {
        const QStringList slice = args.mid(i, chunk);
        QJsonObject sub; sub["op"] = "subscribe"; QJsonArray a; for (const auto& s : slice) a.append(s); sub["args"] = a;
        const QString payload = QString::fromUtf8(QJsonDocument(sub).toJson(QJsonDocument::Compact));
        const int totalBatches = (args.size()+chunk-1)/chunk;
        qDebug() << (market==BybitMarket::Linear?"Bybit Linear":"Bybit Spot") << "subscribe (chunk)" << (i/chunk+1) << "/" << totalBatches << ":" << payload;
        // Spread out subscription messages to avoid server disconnects
        QTimer::singleShot(150 * batchIdx, this, [this, payload, target](){
            if (target && target->state()==QAbstractSocket::ConnectedState) target->sendTextMessage(payload);
        });
        // Track last attempted market for included symbols
        for (const auto& s : slice) {
            QString topic = s;
            QString sym = topic.contains('.') ? topic.section('.',1,1) : topic;
            if (sym.endsWith("USDT")) sym.chop(4);
            lastSubMarket[sym] = market;
        }
    }
}

void DataWorker::handleBybitSubscribeAck(const QJsonObject& root) {
    bool success = root.value("success").toBool(true);
    if (success) return;
    const QString retMsg = root.value("ret_msg").toString();
    // Example: "Invalid symbol :[tickers.TAOUSDT]"
    static QRegularExpression re("\\[(.*?)\\]");
    QRegularExpressionMatch m = re.match(retMsg);
    if (!m.hasMatch()) return;
    const QString inside = m.captured(1); // e.g. tickers.TAOUSDT
    QString sym = inside;
    if (sym.contains('.')) sym = sym.section('.', 1, 1);
    if (sym.endsWith("USDT")) sym.chop(4);
    sym = sym.toUpper();
    if (!sym.isEmpty()) {
        // If Linear rejects symbol, attempt to subscribe it on Spot
        // Decide which market rejected based on lastSubMarket
        BybitMarket last = lastSubMarket.value(sym, (bybitPreference==BybitPreference::LinearFirst)?BybitMarket::Linear:BybitMarket::Spot);
        if (allowBybitFallback && last == BybitMarket::Linear && !invalidLinearBybit.contains(sym)) {
            invalidLinearBybit.insert(sym);
            qDebug() << "[DataWorker] Bybit Linear does not support" << sym << ", trying Spot.";
            if (bybitAlt && bybitAlt->state()==QAbstractSocket::UnconnectedState) {
                const QString spotUrl = QStringLiteral("wss://stream.bybit.com/v5/public/spot?compress=false");
                qDebug() << "[DataWorker] opening Bybit Spot WS:" << spotUrl;
                bybitAlt->open(QUrl(spotUrl));
            }
            auto doAltSub = [this, sym]() {
                if (!bybitAlt || bybitAlt->state()!=QAbstractSocket::ConnectedState) return;
                QStringList args;
                if (mode==StreamMode::Trade) args << QString("publicTrade.%1USDT").arg(sym);
                else args << QString("tickers.%1USDT").arg(sym);
                sendBybitSubscriptions(bybitAlt, args, BybitMarket::Spot);
            };
            if (bybitAlt && bybitAlt->state()==QAbstractSocket::ConnectedState) doAltSub(); else QTimer::singleShot(400, this, doAltSub);
            return;
        }
        // If Spot also rejects, mark fully invalid
        if (!allowBybitFallback && last == BybitMarket::Linear) {
            // In strict mode, just mark unsupported for this worker
            invalidLinearBybit.insert(sym);
            qDebug() << "[DataWorker] Bybit Linear unsupported for" << sym << " (strict, no fallback).";
            return;
        }
        if (!invalidSymbolsBybit.contains(sym)) {
            invalidSymbolsBybit.insert(sym);
            qDebug() << "[DataWorker] Bybit Spot also invalid for" << sym << ": fully filtering";
            emit unsupportedSymbol(sym, QStringLiteral("Bybit: unsupported on both Linear and Spot"));
        }
    }
}
