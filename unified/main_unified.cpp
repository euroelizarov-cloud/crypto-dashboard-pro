#include <QApplication>
#include <QMainWindow>
#include <QWebSocket>
#include <QGridLayout>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QMenu>
#include <qactiongroup.h>
#include <QMenuBar>
#include <QMouseEvent>
#include <QDateTime>
#include <deque>
#include <optional>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <atomic>
#include <QDebug>
#include <QElapsedTimer>
#include <QMap>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QtGlobal>
#include <QLocale>

// Unified dashboard: switch between @trade and @ticker modes and tune performance

const QStringList CURRENCIES = {"BTC", "XRP", "BNB", "SOL", "DOGE", "XLM", "HBAR", "ETH", "APT", "TAO", "LAYER", "TON"};

enum class StreamMode { Trade, Ticker };

class PerformanceConfigDialog : public QDialog {
    Q_OBJECT
public:
    PerformanceConfigDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Performance Settings");
        QFormLayout* layout = new QFormLayout(this);

        animMs = new QSpinBox(); animMs->setRange(50, 5000); animMs->setValue(400);
        renderMs = new QSpinBox(); renderMs->setRange(8, 1000); renderMs->setValue(16);
        cacheMs = new QSpinBox(); cacheMs->setRange(50, 5000); cacheMs->setValue(300);
        volWindow = new QSpinBox(); volWindow->setRange(10, 20000); volWindow->setValue(800);
        maxPts = new QSpinBox(); maxPts->setRange(100, 10000); maxPts->setValue(800);

        layout->addRow("Animation (ms)", animMs);
        layout->addRow("Render interval (ms)", renderMs);
        layout->addRow("Cache update (ms)", cacheMs);
        layout->addRow("Volatility window", volWindow);
        layout->addRow("Max chart points", maxPts);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addRow(buttons);
    }

    int animationMs() const { return animMs->value(); }
    int renderIntervalMs() const { return renderMs->value(); }
    int cacheIntervalMs() const { return cacheMs->value(); }
    int volatilityWindowSize() const { return volWindow->value(); }
    int maxPointsCount() const { return maxPts->value(); }

private:
    QSpinBox *animMs, *renderMs, *cacheMs, *volWindow, *maxPts;
};

class DataWorker : public QObject {
    Q_OBJECT
public:
    explicit DataWorker(QObject *parent = nullptr)
        : QObject(parent), running(false), mode(StreamMode::Trade) {
        for (const QString& cur : CURRENCIES) msg_counter[cur] = 0;
        last_report_time = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    }

    void setMode(StreamMode m) { mode = m; }

public slots:
    void start() {
        if (running) return;
        running = true;
        connectWebSocket();
    }
    void stop() {
        running = false;
        if (webSocket && webSocket->state() != QAbstractSocket::UnconnectedState) webSocket->close();
    }

signals:
    void dataUpdated(const QString& currency, double price, double timestamp);
    void workerError(const QString& errorString);

private slots:
    void onConnected() { qDebug() << "WebSocket connected"; }
    void onDisconnected() { qDebug() << "WebSocket disconnected"; scheduleReconnect(); }
    void onError(QAbstractSocket::SocketError) { qDebug() << "WebSocket error:" << webSocket->errorString(); scheduleReconnect(); }

    void processMessage(const QString& message) {
        if (!running) return;
        QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
        if (!doc.isObject()) return;
        QJsonObject root = doc.object();
        if (!root.contains("data")) return;
        QJsonObject data = root["data"].toObject();

        QString symbol = data.value("s").toString();
        if (symbol.isEmpty()) return;
        QString currency = symbol.left(symbol.length() - 4).toUpper();
        double price = 0.0;
        double timestamp = 0.0;
        if (mode == StreamMode::Trade) {
            price = data.value("p").toString().toDouble();
            timestamp = data.value("T").toDouble() / 1000.0;
        } else {
            price = data.value("c").toString().toDouble();
            timestamp = data.value("E").toDouble() / 1000.0;
        }
        if (price <= 0 || timestamp <= 0) return;

        if (msg_counter.contains(currency)) msg_counter[currency]++;
        double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        if (now - last_report_time >= 10) {
            double interval = now - last_report_time;
            qDebug() << (mode == StreamMode::Trade ? "TRADE" : "TICKER") << "messages per second:";
            for (const QString& cur : CURRENCIES) {
                qDebug() << QString("  %1: %2 msg/s").arg(cur).arg(msg_counter[cur] / interval, 0, 'f', 2);
                msg_counter[cur] = 0;
            }
            last_report_time = now;
        }
        emit dataUpdated(currency, price, timestamp);
    }

private:
    void connectWebSocket() {
        if (!running) return;
        if (webSocket) webSocket->deleteLater();
        webSocket = new QWebSocket();
        connect(webSocket, &QWebSocket::connected, this, &DataWorker::onConnected);
        connect(webSocket, &QWebSocket::disconnected, this, &DataWorker::onDisconnected);
        connect(webSocket, &QWebSocket::errorOccurred, this, &DataWorker::onError);
        connect(webSocket, &QWebSocket::textMessageReceived, this, &DataWorker::processMessage);

        QStringList streams;
        const QString suffix = (mode == StreamMode::Trade) ? "@trade" : "@ticker";
        for (const QString& cur : CURRENCIES) streams << QString("%1usdt%2").arg(cur.toLower(), suffix);
        const QString url = QString("wss://stream.binance.com:9443/stream?streams=%1").arg(streams.join('/'));
        qDebug() << "Connecting to" << url;
        webSocket->open(QUrl(url));
    }
    void scheduleReconnect() {
        if (!running) return;
        QTimer::singleShot(2000, this, &DataWorker::connectWebSocket);
    }

    QWebSocket *webSocket = nullptr;
    QMap<QString, int> msg_counter;
    double last_report_time;
    std::atomic<bool> running;
    StreamMode mode;
};

class DynamicSpeedometer : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double value READ getValue WRITE setValue NOTIFY valueChanged)
public:
    explicit DynamicSpeedometer(const QString& currency, QWidget* parent = nullptr)
        : QWidget(parent), currency(currency), _value(0), modeView("speedometer"),
          minInit(0.9995), maxInit(1.0005), volatilityWindow(800),
          minFactor(1.00001), maxFactor(0.99999),
          maxPoints(800), sampleMethod(0), cacheSize(864000),
          currentScale("5m"), showAxisLabels(false), showTooltips(false),
          smoothLines(false), trendColors(false), logScale(false),
          highlightLast(true), showGrid(false), volatility(0.0),
          btcPrice(0), cachedMinVal(std::nullopt), cachedMaxVal(std::nullopt),
          dataNeedsRedraw(false)
    {
        timeScales = {{"1m", 60}, {"5m", 300}, {"15m", 900}, {"30m", 1800}, {"1h", 3600}, {"4h", 14400}, {"24h", 86400}};
        setMinimumSize(100, 100);
        animation = new QPropertyAnimation(this, "value", this);
        animation->setDuration(400);
        connect(animation, &QPropertyAnimation::valueChanged, this, [this]() {
            if (modeView == "speedometer") update();
        });

        setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this, &QWidget::customContextMenuRequested, this, &DynamicSpeedometer::showContextMenu);
        setMouseTracking(true);

        cachedProcessedHistory.reserve(maxPoints);
        cachedProcessedBtcRatio.reserve(maxPoints);

        renderTimer = new QTimer(this);
        renderTimer->setSingleShot(false);
        connect(renderTimer, &QTimer::timeout, this, &DynamicSpeedometer::onRenderTimeout);
        renderTimer->start(16);

        cacheUpdateTimer = new QTimer(this);
        cacheUpdateTimer->setSingleShot(false);
        connect(cacheUpdateTimer, &QTimer::timeout, this, &DynamicSpeedometer::cacheChartData);
        cacheUpdateTimer->start(300);
    }

    void updateData(double price, double timestamp, double btcPrice = 0) {
        history.push_back({timestamp, price});
        if (history.size() > static_cast<size_t>(cacheSize)) history.pop_front();
        if (currency == "BTC") {
            this->btcPrice = price; btcRatioHistory.push_back({timestamp, 1.0});
        } else if (btcPrice > 0) {
            btcRatioHistory.push_back({timestamp, price / btcPrice});
        }
        if (btcRatioHistory.size() > static_cast<size_t>(cacheSize)) btcRatioHistory.pop_front();
        updateVolatility();
        updateBounds(price);

        double scaled = 50;
        if (cachedMinVal && cachedMaxVal && cachedMaxVal.value() > cachedMinVal.value()) {
            scaled = (price - cachedMinVal.value()) / (cachedMaxVal.value() - cachedMinVal.value()) * 100;
            scaled = std::clamp(scaled, 0.0, 100.0);
        }
        animation->stop();
        animation->setStartValue(_value);
        animation->setEndValue(scaled);
        animation->start();
        dataNeedsRedraw = true;
    }

    // Performance controls
    void applyPerformance(int animMs, int renderMs, int cacheMs, int volWindowSize, int maxPts) {
        animation->setDuration(animMs);
        renderTimer->setInterval(std::max(1, renderMs));
        cacheUpdateTimer->setInterval(std::max(10, cacheMs));
        volatilityWindow = volWindowSize;
        maxPoints = maxPts;
        cachedProcessedHistory.reserve(maxPoints);
        cachedProcessedBtcRatio.reserve(maxPoints);
        cacheChartData();
        update();
    }

signals:
    void valueChanged(double newValue);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (modeView == "speedometer") drawSpeedometer(painter);
        else if (modeView == "line_chart") drawLineChart(painter, cachedProcessedHistory, false);
        else drawLineChart(painter, cachedProcessedBtcRatio, true);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (modeView == "speedometer") modeView = "line_chart";
            else if (modeView == "line_chart") modeView = "btc_ratio";
            else modeView = "speedometer";
            update();
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if ((modeView == "line_chart" || modeView == "btc_ratio") && showTooltips) {
            double x = event->pos().x();
            int w = width(), margin = 30;
            const auto& values = (modeView == "btc_ratio") ? cachedProcessedBtcRatio : cachedProcessedHistory;
            if (!values.empty()) {
                int idx = static_cast<int>((x - margin) * (values.size() - 1) / std::max(1, w - 2 * margin));
                if (idx >= 0 && idx < static_cast<int>(values.size())) {
                    setToolTip(modeView == "btc_ratio" ?
                                   QString("Ratio: %1 BTC").arg(values[idx], 0, 'f', 8) :
                                   QString("Price: $%1").arg(values[idx], 0, 'f', 2));
                }
            }
        }
    }

private slots:
    void onRenderTimeout() {
        if (dataNeedsRedraw && (modeView == "line_chart" || modeView == "btc_ratio")) {
            dataNeedsRedraw = false;
            update();
        }
    }
    void showContextMenu(const QPoint& pos) {
        QMenu menu;
        QMenu* scaleMenu = menu.addMenu("Time Scale");
        for (const QString& scale : timeScales.keys()) {
            QAction* action = scaleMenu->addAction(scale);
            connect(action, &QAction::triggered, this, [this, scale]() { setTimeScale(scale); });
        }
        menu.exec(mapToGlobal(pos));
    }

private:
    void setTimeScale(const QString& scale) {
        if (timeScales.contains(scale)) {
            currentScale = scale;
            cacheChartData();
            update();
        }
    }
    void cacheChartData() {
        cachedProcessedHistory = processHistory(false);
        cachedProcessedBtcRatio = processHistory(true);
        dataNeedsRedraw = true;
    }
    std::vector<double> processHistory(bool useBtcRatio) {
        double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        double timeWindow = timeScales[currentScale];
        double cutoff = now - timeWindow;
        const auto& source = useBtcRatio ? btcRatioHistory : history;
        std::vector<std::pair<double, double>> filtered;
        filtered.reserve(source.size());
        for (const auto& it : source) if (it.first >= cutoff) filtered.push_back(it);

        if (filtered.size() <= static_cast<size_t>(maxPoints)) {
            std::vector<double> result; result.reserve(filtered.size());
            for (const auto& kv : filtered) result.push_back(kv.second);
            return result;
        }
        std::vector<double> samples; samples.reserve(maxPoints);
        size_t step = std::max<size_t>(1, filtered.size() / maxPoints);
        for (size_t i = 0; i < filtered.size(); i += step) {
            auto start = filtered.begin() + i;
            auto end = filtered.begin() + std::min(i + step, filtered.size());
            if (sampleMethod == 0) samples.push_back((end-1)->second);
            else if (sampleMethod == 1) {
                double sum = 0.0; size_t c = 0; for (auto it = start; it != end; ++it) { sum += it->second; ++c; }
                samples.push_back(c ? sum / c : (end-1)->second);
            } else {
                double mx = (start != end) ? start->second : 0.0; for (auto it = start; it != end; ++it) mx = std::max(mx, it->second);
                samples.push_back(mx);
            }
        }
        if (samples.size() > static_cast<size_t>(maxPoints)) samples.resize(maxPoints);
        return samples;
    }
    void updateVolatility() {
        if (history.size() < 2) { volatility = 0.0; return; }
        size_t window = std::min(static_cast<size_t>(volatilityWindow), history.size());
        std::vector<double> prices; prices.reserve(window);
        for (auto it = history.end() - window; it != history.end(); ++it) prices.push_back(it->second);
        std::vector<double> rets; rets.reserve(prices.size() - 1);
        for (size_t i = 1; i < prices.size(); ++i) rets.push_back(prices[i-1] ? std::abs((prices[i]-prices[i-1])/prices[i-1]) : 0.0);
        volatility = rets.empty() ? 0.0 : (std::accumulate(rets.begin(), rets.end(), 0.0) / rets.size()) * 100.0;
    }
    void updateBounds(double price) {
        if (!cachedMinVal || !cachedMaxVal) { cachedMinVal = price * minInit; cachedMaxVal = price * maxInit; return; }
        cachedMinVal = cachedMinVal.value() * minFactor + price * (1.0 - minFactor);
        cachedMaxVal = cachedMaxVal.value() * maxFactor + price * (1.0 - maxFactor);
        if (price < cachedMinVal.value()) cachedMinVal = price;
        if (price > cachedMaxVal.value()) cachedMaxVal = price;
        double range = cachedMaxVal.value() - cachedMinVal.value();
        double epsilon = std::max(1e-10, range * 1e-5);
        if (range < epsilon) {
            double center = (cachedMaxVal.value() + cachedMinVal.value()) / 2.0;
            cachedMinVal = center - epsilon/2.0; cachedMaxVal = center + epsilon/2.0;
        }
    }
    void drawSpeedometer(QPainter& painter) {
        int w = width(), h = height();
        int size = std::min(w, h) - 20;
        QRect rect((w - size) / 2, (h - size) / 2, size, size);
        painter.fillRect(0, 0, w, h, QColor(40, 44, 52));
        painter.setPen(QPen(QColor(100, 100, 100), 4));
        painter.drawArc(rect, 45 * 16, 270 * 16);

        if (cachedMinVal && cachedMaxVal) {
            struct Zone { double start, end; QColor color; };
            std::vector<Zone> zones = {{0, 70, QColor(76, 175, 80)}, {70, 90, QColor(255, 193, 7)}, {90, 100, QColor(244, 67, 54)}};
            for (const auto& z : zones) {
                painter.setPen(QPen(z.color, 8));
                double span = (z.end - z.start) * 270 / 100; double angle = 45 + (270 * z.start / 100);
                painter.drawArc(rect, static_cast<int>(angle * 16), static_cast<int>(span * 16));
            }
        }
        double angle = 45 + (270 * _value / 100);
        painter.setPen(QPen(Qt::white, 2));
        painter.translate(w / 2, h / 2); painter.rotate(angle);
        painter.drawLine(0, 0, size / 2 - 15, 0);
        painter.setBrush(QColor(240, 67, 54, 190)); painter.drawEllipse(QPoint(size / 2 - 15, 0), 18, 8);
        painter.resetTransform();

        painter.setFont(QFont("Arial", 8)); painter.setPen(Qt::yellow);
        painter.drawText(QRect(1, h - 50, w - 10, 20), Qt::AlignLeft | Qt::AlignBottom, QString("Trades: %1").arg(history.size()));
        painter.setPen(Qt::white); painter.setFont(QFont("Arial", 21, QFont::Bold));
        painter.drawText(QRect(0, h / 2 - 50, w, 20), Qt::AlignCenter, currency);
        if (cachedMinVal && cachedMaxVal && !history.empty()) {
            double currentPrice = history.back().second;
            painter.setFont(QFont("Arial", 21, QFont::Bold));
            painter.drawText(QRect(0, h / 2 + 20, w, 20), Qt::AlignCenter, QLocale().toString(currentPrice, 'f', 3));
        }
        painter.setFont(QFont("Arial", 6));
        painter.drawText(QRect(1, 1, w - 10, 20), Qt::AlignLeft | Qt::AlignTop, QString("Volatility: %1%\n").arg(volatility, 0, 'f', 4));
        painter.drawText(QRect(1, h - 30, w - 10, 30), Qt::AlignLeft | Qt::AlignBottom,
                         cachedMinVal ? QString("MAX: %1\nMIN: %2").arg(cachedMaxVal.value(), 0, 'f', 4).arg(cachedMinVal.value(), 0, 'f', 4) : "");
    }
    void drawLineChart(QPainter& painter, const std::vector<double>& values, bool useBtcRatio) {
        int w = width(), h = height(), margin = 30; painter.fillRect(0, 0, w, h, QColor(40, 44, 52));
        if (values.empty()) return;
        double minValChart = *std::min_element(values.begin(), values.end());
        double maxValChart = *std::max_element(values.begin(), values.end());
        if (maxValChart == minValChart) maxValChart += useBtcRatio ? 1e-8 : 1e-4;
        if (showGrid) {
            painter.setPen(QPen(QColor(80, 80, 80, 50), 1, Qt::DotLine));
            for (int i = 1; i < 5; ++i) painter.drawLine(margin, h - margin - (i * (h - 2 * margin) / 5), w - margin, h - margin - (i * (h - 2 * margin) / 5));
        }
        painter.setPen(QPen(QColor(100, 100, 100), 1)); painter.drawLine(margin, h - margin, w - margin, h - margin); painter.drawLine(margin, margin, margin, h - margin);
        if (showAxisLabels) {
            painter.setPen(Qt::white); painter.setFont(QFont("Arial", 8));
            for (int i = 0; i < 5; ++i) {
                double y = h - margin - (i * (h - 2 * margin) / 4);
                double value = logScale ? minValChart + std::exp(i * std::log(maxValChart - minValChart + 1) / 4) - 1 : minValChart + (i * (maxValChart - minValChart) / 4);
                painter.drawText(margin - 25, y - 5, 20, 10, Qt::AlignRight, QString("%1").arg(value, 0, 'f', useBtcRatio ? 8 : 2));
            }
        }
        std::vector<QPointF> points; points.reserve(values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            double x = margin + i * (w - 2 * margin) / std::max<size_t>(1, values.size() - 1);
            double range = std::max(1e-10, maxValChart - minValChart);
            double normalizedValue = logScale ? (std::log(values[i] - minValChart + 1) / std::log(range + 1)) : ((values[i] - minValChart) / range);
            double y = h - margin - normalizedValue * (h - 2 * margin);
            points.emplace_back(x, y);
        }
        if (points.empty()) return;
        QPainterPath fillPath; fillPath.moveTo(points[0]); for (const auto& p : points) fillPath.lineTo(p);
        fillPath.lineTo(w - margin, h - margin); fillPath.lineTo(margin, h - margin);
        QLinearGradient gradient(0, margin, 0, h - margin); gradient.setColorAt(0, QColor(33, 150, 243, 80)); gradient.setColorAt(1, QColor(33, 150, 243, 5));
        painter.setBrush(gradient); painter.setPen(Qt::NoPen); painter.drawPath(fillPath);
        QLinearGradient lineGradient(0, 0, w, 0);
        if (trendColors && values.size() > 1) {
            double trend = values.back() - values.front();
            if (trend > 0) { lineGradient.setColorAt(0, QColor(76, 175, 80)); lineGradient.setColorAt(1, QColor(33, 150, 243)); }
            else { lineGradient.setColorAt(0, QColor(244, 67, 54)); lineGradient.setColorAt(1, QColor(33, 150, 243)); }
        } else { lineGradient.setColorAt(0, QColor(33, 150, 243)); lineGradient.setColorAt(1, QColor(76, 175, 80)); }
        painter.setPen(QPen(lineGradient, 2));
        if (smoothLines && points.size() > 1) {
            QPainterPath path; path.moveTo(points[0]);
            for (size_t i = 1; i < points.size(); ++i) { double midX = (points[i-1].x() + points[i].x()) / 2; path.quadTo(points[i-1], QPointF(midX, points[i].y())); }
            painter.drawPath(path);
        } else {
            painter.drawPolyline(points.data(), static_cast<int>(points.size()));
        }
        if (highlightLast && !points.empty()) { painter.setBrush(QColor(255, 255, 255, 100)); painter.setPen(Qt::NoPen); painter.drawEllipse(points.back(), 5, 5); }
        painter.setPen(Qt::white); painter.setFont(QFont("Arial", 16));
        painter.drawText(10, 20, (useBtcRatio ? QString("%1/BTC | %2").arg(currency, currentScale) : QString("%1 | %2").arg(currency, currentScale)));
        painter.drawText(QRect(margin, h - 25, w - 2 * margin, 20), Qt::AlignCenter, QString("%1").arg(values.back(), 0, 'f', useBtcRatio ? 8 : 2));
    }

    double getValue() const { return _value; }
    void setValue(double newValue) { if (qFuzzyCompare(_value, newValue)) return; _value = newValue; emit valueChanged(newValue); }

    QString currency; double _value; QString modeView; QPropertyAnimation* animation;
    QTimer* renderTimer; QTimer* cacheUpdateTimer;
    double minInit, maxInit, minFactor, maxFactor; int volatilityWindow, maxPoints, sampleMethod, cacheSize;
    QMap<QString, int> timeScales; QString currentScale; bool showAxisLabels, showTooltips, smoothLines, trendColors, logScale, highlightLast, showGrid;
    std::deque<std::pair<double, double>> history, btcRatioHistory; double volatility; double btcPrice; std::optional<double> cachedMinVal, cachedMaxVal;
    std::vector<double> cachedProcessedHistory; std::vector<double> cachedProcessedBtcRatio; bool dataNeedsRedraw;
};

class CryptoDashboard : public QMainWindow {
    Q_OBJECT
public:
    CryptoDashboard() : btcPrice(0), streamMode(StreamMode::Trade) {
        QWidget* central = new QWidget(this); setCentralWidget(central); QGridLayout* layout = new QGridLayout(central); layout->setSpacing(10);
        int row = 0, col = 0; for (const QString& currency : CURRENCIES) { auto* s = new DynamicSpeedometer(currency); speedometers[currency] = s; layout->addWidget(s, row, col); if (++col >= 4) { col = 0; ++row; } }
        setStyleSheet("background: #202429;"); setWindowTitle("Crypto Dashboard (Unified)"); resize(1600, 900);

        // Menus
        QMenu* modeMenu = menuBar()->addMenu("Mode");
        QAction* tradeAct = modeMenu->addAction("TRADE stream"); tradeAct->setCheckable(true); tradeAct->setChecked(true);
        QAction* tickerAct = modeMenu->addAction("TICKER stream"); tickerAct->setCheckable(true);
        QActionGroup* modeGroup = new QActionGroup(this); modeGroup->addAction(tradeAct); modeGroup->addAction(tickerAct); modeGroup->setExclusive(true);
        connect(tradeAct, &QAction::triggered, this, [this]() { switchMode(StreamMode::Trade); });
        connect(tickerAct, &QAction::triggered, this, [this]() { switchMode(StreamMode::Ticker); });

        QMenu* settingsMenu = menuBar()->addMenu("Settings");
        QAction* perfAct = settingsMenu->addAction("Performance...");
        connect(perfAct, &QAction::triggered, this, &CryptoDashboard::openPerformanceDialog);

        // Worker thread
        dataWorker = new DataWorker(); dataWorker->setMode(streamMode);
        workerThread = new QThread(this); dataWorker->moveToThread(workerThread);
        connect(workerThread, &QThread::started, dataWorker, &DataWorker::start);
        connect(dataWorker, &DataWorker::dataUpdated, this, &CryptoDashboard::handleData);
        connect(workerThread, &QThread::finished, dataWorker, &QObject::deleteLater);
        workerThread->start();
    }
    ~CryptoDashboard() {
        dataWorker->stop(); workerThread->quit(); workerThread->wait();
    }

private slots:
    void switchMode(StreamMode m) {
        // Restart worker in the new mode
        streamMode = m;
        QMetaObject::invokeMethod(dataWorker, [this, m]() {
            dataWorker->stop(); dataWorker->setMode(m); dataWorker->start();
        }, Qt::QueuedConnection);
        setWindowTitle(QString("Crypto Dashboard (Unified) — %1").arg(m == StreamMode::Trade ? "TRADE" : "TICKER"));
    }

    void openPerformanceDialog() {
        PerformanceConfigDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            for (auto* s : speedometers) {
                s->applyPerformance(dlg.animationMs(), dlg.renderIntervalMs(), dlg.cacheIntervalMs(), dlg.volatilityWindowSize(), dlg.maxPointsCount());
            }
        }
    }

    void handleData(const QString& currency, double price, double timestamp) {
        if (speedometers.contains(currency)) {
            if (currency == "BTC") btcPrice = price;
            QMetaObject::invokeMethod(this, [this, currency, price, timestamp]() {
                speedometers[currency]->updateData(price, timestamp, btcPrice);
            }, Qt::QueuedConnection);
        }
    }

private:
    QMap<QString, DynamicSpeedometer*> speedometers;
    DataWorker* dataWorker; QThread* workerThread; double btcPrice; StreamMode streamMode;
};

#include "main_unified.moc"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    qApp->setStyleSheet("QWidget { background-color: #121212; color: white; font-family: Segoe UI, sans-serif; }");
    CryptoDashboard w; w.show();
    return app.exec();
}
