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

const QStringList CURRENCIES = {"BTC", "XRP", "BNB", "SOL", "DOGE", "XLM", "HBAR", "ETH", "APT", "TAO", "LAYER", "TON"};

// --- Profiler ---
// (Код профайлера оставлен без изменений из File 1 для отладки, если нужно)
class Profiler {
public:
    struct Stats {
        qint64 total_time_ns = 0;
        qint64 max_time_ns = 0;
        qint64 min_time_ns = std::numeric_limits<qint64>::max();
        int count = 0;
        QString name;
        double avg_ms() const { return count > 0 ? (total_time_ns / 1e6) / count : 0.0; }
        double total_ms() const { return total_time_ns / 1e6; }
        double max_ms() const { return max_time_ns / 1e6; }
        double min_ms() const { return min_time_ns / 1e6; }
    };
    void record(const QString& name, qint64 elapsed_ns) {
        QMutexLocker locker(&mutex);
        auto& stat = stats[name];
        stat.name = name;
        stat.total_time_ns += elapsed_ns;
        stat.count++;
        if (elapsed_ns > stat.max_time_ns) stat.max_time_ns = elapsed_ns;
        if (elapsed_ns < stat.min_time_ns) stat.min_time_ns = elapsed_ns;
    }
    void printStats() {
        QMutexLocker locker(&mutex);
        qDebug() << "\n========== PROFILER STATS ==========";
        for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) {
            const QString& key = it.key();
            const Stats& s = it.value();
            qDebug().noquote() << QString("%1: Total: %2 ms, Avg: %3 ms, Max: %4 ms, Min: %5 ms, Count: %6")
                                      .arg(s.name, -35)
                                      .arg(s.total_ms(), 10, 'f', 3)
                                      .arg(s.avg_ms(), 8, 'f', 3)
                                      .arg(s.max_ms(), 8, 'f', 3)
                                      .arg(s.min_ms() == std::numeric_limits<qint64>::max() ? 0.0 : s.min_ms(), 8, 'f', 3)
                                      .arg(s.count, 8);
        }
        qDebug() << "====================================\n";
        QFile file("profiler_stats.txt");
        if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
            QTextStream out(&file);
            out << "\n========== PROFILER STATS ==========\n";
            out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
            for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) {
                const QString& key = it.key();
                const Stats& s = it.value();
                out << QString("%1: Total: %2 ms, Avg: %3 ms, Max: %4 ms, Min: %5 ms, Count: %6\n")
                           .arg(s.name, -35)
                           .arg(s.total_ms(), 10, 'f', 3)
                           .arg(s.avg_ms(), 8, 'f', 3)
                           .arg(s.max_ms(), 8, 'f', 3)
                           .arg(s.min_ms() == std::numeric_limits<qint64>::max() ? 0.0 : s.min_ms(), 8, 'f', 3)
                           .arg(s.count, 8);
            }
            out << "====================================\n";
            file.close();
        }
    }
    void reset() {
        QMutexLocker locker(&mutex);
        stats.clear();
    }

private:
    QMap<QString, Stats> stats;
    QMutex mutex;
};
Profiler g_profiler;

#define PROFILE_SCOPE(name) \
QElapsedTimer __profiler_timer; \
    __profiler_timer.start(); \
    auto __profiler_lambda = [&]() { \
              g_profiler.record(name, __profiler_timer.nsecsElapsed()); \
      }; \
    auto __profiler_guard = qScopeGuard(__profiler_lambda);

// --- DataWorker ---
// (Код DataWorker оставлен без изменений из File 1)
class DataWorker : public QObject {
    Q_OBJECT
public:
    DataWorker(QObject *parent = nullptr) : QObject(parent), running(false) {
        for (const QString& cur : CURRENCIES) {
            msg_counter[cur] = 0;
        }
        last_report_time = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    }
    void start() {
        if (running) return;
        running = true;
        connectWebSocket();
    }
    void stop() {
        running = false;
        if (webSocket && webSocket->state() != QAbstractSocket::UnconnectedState) {
            webSocket->close();
        }
    }

signals:
    void dataUpdated(const QString& currency, double price, double timestamp);
    void workerError(const QString& errorString);

private slots:
    void onConnected() {
        qDebug() << "WebSocket connected";
    }
    void onDisconnected() {
        qDebug() << "WebSocket disconnected";
        scheduleReconnect();
    }
    void onError(QAbstractSocket::SocketError error) {
        Q_UNUSED(error)
        qDebug() << "WebSocket error:" << webSocket->errorString();
        scheduleReconnect();
    }
    void processMessage(const QString& message) {
        PROFILE_SCOPE("DataWorker::processMessage");
        if (!running) return;
        QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
        if (!doc.isObject()) return;
        QJsonObject root = doc.object();
        if (!root.contains("data")) return;
        QJsonObject data = root["data"].toObject();
        QString symbol = data["s"].toString();
        QString currency = symbol.left(symbol.length() - 4).toUpper();
        double price = data["c"].toString().toDouble();
        double timestamp = data["E"].toDouble() / 1000;
        if (msg_counter.contains(currency)) {
            msg_counter[currency]++;
        }
        double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        if (now - last_report_time >= 10) {
            double interval = now - last_report_time;
            qDebug() << "WebSocket messages per second:";
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
        PROFILE_SCOPE("DataWorker::connectWebSocket");
        if (!running) return;
        if (webSocket) {
            webSocket->deleteLater();
        }
        webSocket = new QWebSocket();
        connect(webSocket, &QWebSocket::connected, this, &DataWorker::onConnected);
        connect(webSocket, &QWebSocket::disconnected, this, &DataWorker::onDisconnected);
        connect(webSocket, &QWebSocket::errorOccurred, this, &DataWorker::onError);
        connect(webSocket, &QWebSocket::textMessageReceived, this, &DataWorker::processMessage);
        QStringList streams;
        for (const QString& cur : CURRENCIES) {
            streams << QString("%1usdt@ticker").arg(cur.toLower());
        }
        QString url = QString("wss://stream.binance.com:9443/stream?streams=%1").arg(streams.join('/'));
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
};

// --- ChartConfig ---
// (Код ChartConfig оставлен без изменений из File 1)
class ChartConfig : public QDialog {
    Q_OBJECT
public:
    ChartConfig(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Chart Settings");
        QFormLayout* layout = new QFormLayout(this);
        cacheSize = new QSpinBox();
        cacheSize->setRange(100, 86400);
        cacheSize->setValue(43200);
        layout->addRow("Max stored points:", cacheSize);
        sampleMethod = new QComboBox();
        sampleMethod->addItems({"Last", "Average", "Maximum"});
        layout->addRow("Sampling method:", sampleMethod);
        showAxisLabels = new QCheckBox("Show axis labels");
        showAxisLabels->setChecked(true);
        layout->addRow(showAxisLabels);
        showTooltips = new QCheckBox("Show tooltips");
        showTooltips->setChecked(true);
        layout->addRow(showTooltips);
        smoothLines = new QCheckBox("Smooth lines");
        smoothLines->setChecked(true);
        layout->addRow(smoothLines);
        trendColors = new QCheckBox("Trend colors");
        trendColors->setChecked(true);
        layout->addRow(trendColors);
        logScale = new QCheckBox("Logarithmic scale");
        logScale->setChecked(false);
        layout->addRow(logScale);
        highlightLast = new QCheckBox("Highlight last point");
        highlightLast->setChecked(true);
        layout->addRow(highlightLast);
        showGrid = new QCheckBox("Show grid");
        showGrid->setChecked(true);
        layout->addRow(showGrid);
        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addRow(buttons);
        setLayout(layout);
    }
    QMap<QString, QVariant> getConfig() const {
        return {
            {"cache_size", cacheSize->value()},
            {"sample_method", sampleMethod->currentIndex()},
            {"show_axis_labels", showAxisLabels->isChecked()},
            {"show_tooltips", showTooltips->isChecked()},
            {"smooth_lines", smoothLines->isChecked()},
            {"trend_colors", trendColors->isChecked()},
            {"log_scale", logScale->isChecked()},
            {"highlight_last", highlightLast->isChecked()},
            {"show_grid", showGrid->isChecked()}
        };
    }

private:
    QSpinBox* cacheSize;
    QComboBox* sampleMethod;
    QCheckBox* showAxisLabels;
    QCheckBox* showTooltips;
    QCheckBox* smoothLines;
    QCheckBox* trendColors;
    QCheckBox* logScale;
    QCheckBox* highlightLast;
    QCheckBox* showGrid;
};

// --- DynamicSpeedometer ---
class DynamicSpeedometer : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double value READ getValue WRITE setValue NOTIFY valueChanged)

public:
    DynamicSpeedometer(const QString& currency, QWidget* parent = nullptr)
        : QWidget(parent), currency(currency), _value(0), mode("speedometer"),
        minInit(0.995), maxInit(1.005), volatilityWindow(400),
        minFactor(1.000001), maxFactor(0.999999),
        maxPoints(500), sampleMethod(0), cacheSize(86400),
        currentScale("24h"), showAxisLabels(false), showTooltips(false),
        smoothLines(false), trendColors(false), logScale(false),
        highlightLast(true), showGrid(false), volatility(0.0),
        btcPrice(0), cachedMinVal(std::nullopt), cachedMaxVal(std::nullopt),
        dataNeedsRedraw(false) { // Инициализируем флаг
        timeScales = {{"5m", 300}, {"15m", 900}, {"30m", 1800}, {"1h", 3600}, {"4h", 14400}, {"24h", 86400}};
        setMinimumSize(100, 100);

        animation = new QPropertyAnimation(this, "value", this);
        animation->setDuration(200); // Быстрая анимация спидометра
        connect(animation, &QPropertyAnimation::valueChanged, this, [this]() {
            // Перерисовываем только спидометр во время анимации
            if (mode == "speedometer") {
                update();
            }
        });

        setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this, &QWidget::customContextMenuRequested, this, &DynamicSpeedometer::showContextMenu);
        setMouseTracking(true);

        cachedProcessedHistory.reserve(maxPoints);
        cachedProcessedBtcRatio.reserve(maxPoints);

        // --- Таймеры ---
        // 1. Таймер для ограничения частоты перерисовки (60 FPS)
        renderTimer = new QTimer(this);
        renderTimer->setSingleShot(false); // Повторяющийся
        connect(renderTimer, &QTimer::timeout, this, &DynamicSpeedometer::onRenderTimeout);
        renderTimer->start(16); // ~60 FPS

        // 2. Таймер для редкого пересчета данных графика (2 раза в секунду)
        cacheUpdateTimer = new QTimer(this);
        cacheUpdateTimer->setSingleShot(false);
        connect(cacheUpdateTimer, &QTimer::timeout, this, &DynamicSpeedometer::cacheChartData);
        cacheUpdateTimer->start(500); // Пересчитываем кэш каждые 500 мс

        // --- Удаление profilerTimer для снижения нагрузки ---
        // profilerTimer = new QTimer(this);
        // connect(profilerTimer, &QTimer::timeout, []() {
        //     g_profiler.printStats();
        // });
        // profilerTimer->start(10000);
    }

    void updateData(double price, double timestamp, double btcPrice = 0) {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::updateData_%1").arg(currency)); // Можно отключить для повышения производительности

        // 1. Обновляем данные
        history.push_back({timestamp, price});
        if (history.size() > static_cast<size_t>(cacheSize)) history.pop_front();

        if (currency == "BTC") {
            this->btcPrice = price;
            btcRatioHistory.push_back({timestamp, 1.0});
        } else if (btcPrice != 0) {
            double ratio = price / btcPrice;
            btcRatioHistory.push_back({timestamp, ratio});
        }
        if (btcRatioHistory.size() > static_cast<size_t>(cacheSize)) btcRatioHistory.pop_front();

        updateVolatility();
        updateBounds(price);

        // 2. Обновляем анимацию спидометра
        double scaled = 50;
        if (cachedMinVal.has_value() && cachedMaxVal.has_value() && cachedMaxVal.value() > cachedMinVal.value()) {
            scaled = (price - cachedMinVal.value()) / (cachedMaxVal.value() - cachedMinVal.value()) * 100;
            scaled = std::max(0.0, std::min(100.0, scaled));
        }
        animation->stop();
        animation->setStartValue(_value);
        animation->setEndValue(scaled);
        animation->start();

        // 3. Помечаем, что данные изменились и нужна перерисовка графика
        // Перерисовка будет выполнена по таймеру renderTimer
        dataNeedsRedraw = true;
        // НЕ вызываем update() напрямую здесь
    }

signals:
    void valueChanged(double newValue);
    void configUpdated(const QMap<QString, QVariant>& config);

protected:
    void paintEvent(QPaintEvent*) override {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::paintEvent_%1").arg(currency)); // Отключаем профайлер для paintEvent
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        if (mode == "speedometer") {
            drawSpeedometer(painter);
        } else if (mode == "line_chart") {
            drawLineChart(painter, cachedProcessedHistory, false);
        } else {
            drawLineChart(painter, cachedProcessedBtcRatio, true);
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::mousePressEvent_%1").arg(currency));
        if (event->button() == Qt::LeftButton) {
            if (mode == "speedometer") mode = "line_chart";
            else if (mode == "line_chart") mode = "btc_ratio";
            else mode = "speedometer";
            update(); // Немедленная перерисовка при смене режима
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::mouseMoveEvent_%1").arg(currency));
        if ((mode == "line_chart" || mode == "btc_ratio") && showTooltips) {
            double x = event->pos().x();
            int w = width(), margin = 30;
            const auto& values = (mode == "btc_ratio") ? cachedProcessedBtcRatio : cachedProcessedHistory;
            if (!values.empty()) {
                int idx = static_cast<int>((x - margin) * (values.size() - 1) / (w - 2 * margin));
                if (idx >= 0 && idx < static_cast<int>(values.size())) {
                    setToolTip(mode == "btc_ratio" ?
                                   QString("Ratio: %1 BTC").arg(values[idx], 0, 'f', 8) :
                                   QString("Price: $%1").arg(values[idx], 0, 'f', 2));
                }
            }
        }
    }

private slots:
    void showContextMenu(const QPoint& pos) {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::showContextMenu_%1").arg(currency));
        QMenu menu;
        QMenu* scaleMenu = menu.addMenu("Time Scale");
        for (const QString& scale : timeScales.keys()) {
            QAction* action = scaleMenu->addAction(scale);
            connect(action, &QAction::triggered, this, [this, scale]() { setTimeScale(scale); });
        }
        QAction* configAction = menu.addAction("Settings");
        connect(configAction, &QAction::triggered, this, &DynamicSpeedometer::openConfigDialog);
        menu.exec(mapToGlobal(pos));
    }

    void setTimeScale(const QString& scale) {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::setTimeScale_%1").arg(currency));
        if (timeScales.contains(scale)) {
            currentScale = scale;
            // Запрашиваем немедленный пересчет кэша
            cacheChartData();
            update(); // Перерисовываем немедленно
        }
    }

    void openConfigDialog() {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::openConfigDialog_%1").arg(currency));
        ChartConfig dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            auto config = dialog.getConfig();
            cacheSize = config["cache_size"].toInt();
            sampleMethod = config["sample_method"].toInt();
            showAxisLabels = config["show_axis_labels"].toBool();
            showTooltips = config["show_tooltips"].toBool();
            smoothLines = config["smooth_lines"].toBool();
            trendColors = config["trend_colors"].toBool();
            logScale = config["log_scale"].toBool();
            highlightLast = config["highlight_last"].toBool();
            showGrid = config["show_grid"].toBool();

            while (history.size() > static_cast<size_t>(cacheSize)) history.pop_front();
            while (btcRatioHistory.size() > static_cast<size_t>(cacheSize)) btcRatioHistory.pop_front();

            // Запрашиваем немедленный пересчет кэша
            cacheChartData();
            emit configUpdated(config);
            update(); // Перерисовываем немедленно
        }
    }

    // Слот для таймера рендеринга
    void onRenderTimeout() {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::onRenderTimeout_%1").arg(currency));
        // Перерисовываем только если это необходимо
        if (dataNeedsRedraw && mode != "speedometer") {
            dataNeedsRedraw = false; // Сбрасываем флаг
            update(); // Вызываем перерисовку
        }
        // Если mode == "speedometer", перерисовка уже управляется анимацией
    }

private:
    // Пересчет кэшированных данных
    void cacheChartData() {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::cacheChartData_%1").arg(currency));
        // Этот метод теперь вызывается редко по таймеру cacheUpdateTimer
        // или напрямую при изменении настроек/масштаба
        cachedProcessedHistory = processHistory(false);
        cachedProcessedBtcRatio = processHistory(true);
        // Устанавливаем флаг, что данные изменились
        dataNeedsRedraw = true;
    }

    std::vector<double> processHistory(bool useBtcRatio) {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::processHistory_%1_%2").arg(currency).arg(useBtcRatio ? "btc" : "normal"));
        double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        double timeWindow = timeScales[currentScale];
        double cutoff = now - timeWindow;
        const auto& source = useBtcRatio ? btcRatioHistory : history;

        std::vector<std::pair<double, double>> filtered;
        for (const auto& [t, p] : source) {
            if (t >= cutoff) filtered.emplace_back(t, p);
        }

        if (filtered.size() <= static_cast<size_t>(maxPoints)) {
            std::vector<double> result;
            result.reserve(filtered.size());
            for (const auto& [t, p] : filtered) result.push_back(p);
            return result;
        }

        std::vector<double> samples;
        size_t step = filtered.size() / maxPoints;
        if (step == 0) step = 1;
        for (size_t i = 0; i < filtered.size(); i += step) {
            auto start = filtered.begin() + i;
            auto end = filtered.begin() + std::min(i + step, filtered.size());
            std::vector<double> chunk;
            for (auto it = start; it != end; ++it) chunk.push_back(it->second);
            if (sampleMethod == 0) samples.push_back(chunk.back());
            else if (sampleMethod == 1) samples.push_back(std::accumulate(chunk.begin(), chunk.end(), 0.0) / chunk.size());
            else if (!chunk.empty()) samples.push_back(*std::max_element(chunk.begin(), chunk.end()));
        }
        return std::vector<double>(samples.begin(), samples.begin() + std::min(samples.size(), static_cast<size_t>(maxPoints)));
    }

    void updateVolatility() {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::updateVolatility_%1").arg(currency));
        if (history.size() < 2) {
            volatility = 0.0;
            return;
        }
        size_t window = std::min(static_cast<size_t>(volatilityWindow), history.size());
        std::vector<double> prices;
        prices.reserve(window);
        for (auto it = history.end() - window; it != history.end(); ++it) {
            prices.push_back(it->second);
        }
        std::vector<double> returns;
        returns.reserve(prices.size() - 1);
        for (size_t i = 1; i < prices.size(); ++i) {
            if (prices[i-1] != 0) {
                returns.push_back(std::abs((prices[i] - prices[i-1]) / prices[i-1]));
            } else {
                returns.push_back(0.0);
            }
        }
        if (!returns.empty()) {
            double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
            volatility = (sum / returns.size()) * 100;
        } else {
            volatility = 0.0;
        }
    }

    void updateBounds(double price) {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::updateBounds_%1").arg(currency));
        if (!cachedMinVal.has_value() || !cachedMaxVal.has_value()) {
            cachedMinVal = price * minInit;
            cachedMaxVal = price * maxInit;
            return;
        }
        cachedMinVal = cachedMinVal.value() * minFactor;
        cachedMaxVal = cachedMaxVal.value() * maxFactor;
        if (price < cachedMinVal.value()) cachedMinVal = price;
        if (price > cachedMaxVal.value()) cachedMaxVal = price;
        double epsilon = price * 0.0001;
        if (epsilon < 1e-10) epsilon = 1e-10;
        if (cachedMaxVal.value() - cachedMinVal.value() < epsilon) {
            cachedMaxVal = cachedMinVal.value() + epsilon;
        }
    }

    void drawSpeedometer(QPainter& painter) {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::drawSpeedometer_%1").arg(currency));
        int w = width(), h = height();
        int size = std::min(w, h) - 20;
        QRect rect((w - size) / 2, (h - size) / 2, size, size);
        painter.fillRect(0, 0, w, h, QColor(40, 44, 52));
        painter.setPen(QPen(QColor(100, 100, 100), 4));
        painter.drawArc(rect, 45 * 16, 270 * 16);

        if (cachedMinVal.has_value() && cachedMaxVal.has_value()) {
            struct Zone { double start, end; QColor color; };
            std::vector<Zone> zones = {{0, 70, QColor(76, 175, 80)}, {70, 90, QColor(255, 193, 7)}, {90, 100, QColor(244, 67, 54)}};
            for (const auto& zone : zones) {
                painter.setPen(QPen(zone.color, 8));
                double span = (zone.end - zone.start) * 270 / 100;
                double angle = 45 + (270 * zone.start / 100);
                painter.drawArc(rect, static_cast<int>(angle * 16), static_cast<int>(span * 16));
            }
        }

        double angle = 45 + (270 * _value / 100);
        painter.setPen(QPen(Qt::white, 2));
        painter.translate(w / 2, h / 2);
        painter.rotate(angle);
        painter.drawLine(0, 0, size / 2 - 15, 0);
        painter.setBrush(QColor(240, 67, 54, 190));
        painter.drawEllipse(QPoint(size / 2 - 15, 0), 18, 8);
        painter.resetTransform();

        painter.setFont(QFont("Arial", 8));
        painter.setPen(Qt::yellow);
        painter.drawText(QRect(1, h - 50, w - 10, 20), Qt::AlignLeft | Qt::AlignBottom, QString("Points: %1").arg(history.size()));
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 21, QFont::Bold));
        painter.drawText(QRect(0, h / 2 - 50, w, 20), Qt::AlignCenter, currency);

        if (cachedMinVal.has_value() && cachedMaxVal.has_value()) {
            double currentPrice = cachedMinVal.value() + (_value / 100) * (cachedMaxVal.value() - cachedMinVal.value());
            painter.setFont(QFont("Arial", 21, QFont::Bold));
            painter.drawText(QRect(0, h / 2 + 20, w, 20), Qt::AlignCenter, QString("%1").arg(currentPrice, 0, 'f', 3));
        }

        painter.setFont(QFont("Arial", 6));
        painter.drawText(QRect(1, h - 30, w - 10, 30), Qt::AlignLeft | Qt::AlignBottom,
                         cachedMinVal.has_value() ? QString("MAX: %1\nMIN: %2").arg(cachedMaxVal.value(), 0, 'f', 4).arg(cachedMinVal.value(), 0, 'f', 4) : "");
    }

    void drawLineChart(QPainter& painter, const std::vector<double>& values, bool useBtcRatio) {
        // PROFILE_SCOPE(QString("DynamicSpeedometer::drawLineChart_%1_%2").arg(currency).arg(useBtcRatio ? "btc" : "normal"));
        int w = width(), h = height(), margin = 30;
        painter.fillRect(0, 0, w, h, QColor(40, 44, 52));

        if (values.empty()) return;

        double minValChart = *std::min_element(values.begin(), values.end());
        double maxValChart = *std::max_element(values.begin(), values.end());
        if (maxValChart == minValChart) {
            maxValChart += useBtcRatio ? 0.00000001 : 0.0001;
        }

        if (showGrid) {
            painter.setPen(QPen(QColor(80, 80, 80, 50), 1, Qt::DotLine));
            for (int i = 1; i < 5; ++i) {
                double y = h - margin - (i * (h - 2 * margin) / 5);
                painter.drawLine(margin, y, w - margin, y);
            }
        }

        painter.setPen(QPen(QColor(100, 100, 100), 1));
        painter.drawLine(margin, h - margin, w - margin, h - margin);
        painter.drawLine(margin, margin, margin, h - margin);

        if (showAxisLabels) {
            painter.setPen(Qt::white);
            painter.setFont(QFont("Arial", 8));
            for (int i = 0; i < 5; ++i) {
                double y = h - margin - (i * (h - 2 * margin) / 4);
                double value = logScale ?
                                   minValChart + std::exp(i * std::log(maxValChart - minValChart + 1) / 4) - 1 :
                                   minValChart + (i * (maxValChart - minValChart) / 4);
                painter.drawText(margin - 25, y - 5, 20, 10, Qt::AlignRight,
                                 QString("%1").arg(value, 0, 'f', useBtcRatio ? 8 : 2));
            }
        }

        std::vector<QPointF> points;
        points.reserve(values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            double x = margin + i * (w - 2 * margin) / std::max(values.size() - 1, size_t(1));
            double range = maxValChart - minValChart;
            if (range <= 0) range = 1e-10;
            double normalizedValue = logScale ?
                                         (std::log(values[i] - minValChart + 1) / std::log(range + 1)) :
                                         ((values[i] - minValChart) / range);
            double y = h - margin - normalizedValue * (h - 2 * margin);
            points.emplace_back(x, y);
        }

        if (points.empty()) return;

        QPainterPath fillPath;
        fillPath.moveTo(points[0]);
        for (const auto& p : points) fillPath.lineTo(p);
        fillPath.lineTo(w - margin, h - margin);
        fillPath.lineTo(margin, h - margin);
        QLinearGradient gradient(0, margin, 0, h - margin);
        gradient.setColorAt(0, QColor(33, 150, 243, 80));
        gradient.setColorAt(1, QColor(33, 150, 243, 5));
        painter.setBrush(gradient);
        painter.setPen(Qt::NoPen);
        painter.drawPath(fillPath);

        QLinearGradient lineGradient(0, 0, w, 0);
        if (trendColors && values.size() > 1) {
            double trend = values.back() - values.front();
            if (trend > 0) {
                lineGradient.setColorAt(0, QColor(76, 175, 80));
                lineGradient.setColorAt(1, QColor(33, 150, 243));
            } else {
                lineGradient.setColorAt(0, QColor(244, 67, 54));
                lineGradient.setColorAt(1, QColor(33, 150, 243));
            }
        } else {
            lineGradient.setColorAt(0, QColor(33, 150, 243));
            lineGradient.setColorAt(1, QColor(76, 175, 80));
        }

        painter.setPen(QPen(lineGradient, 2));
        if (smoothLines && points.size() > 1) {
            QPainterPath path;
            path.moveTo(points[0]);
            for (size_t i = 1; i < points.size(); ++i) {
                double midX = (points[i-1].x() + points[i].x()) / 2;
                path.quadTo(points[i-1], QPointF(midX, points[i].y()));
            }
            painter.drawPath(path);
        } else {
            if (!points.empty()) {
                painter.drawPolyline(points.data(), static_cast<int>(points.size()));
            }
        }

        if (highlightLast && !points.empty()) {
            painter.setBrush(QColor(255, 255, 255, 100));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(points.back(), 5, 5);
        }

        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 16));
        QString title = useBtcRatio ? QString("%1/BTC | %2").arg(currency, currentScale) : QString("%1 | %2").arg(currency, currentScale);
        painter.drawText(10, 20, title);

        double currentVal = values.empty() ? 0 : values.back();
        painter.drawText(QRect(margin, h - 25, w - 2 * margin, 20), Qt::AlignCenter,
                         QString("%1").arg(currentVal, 0, 'f', useBtcRatio ? 8 : 2));
    }

    double getValue() const { return _value; }
    void setValue(double newValue) {
        if (qFuzzyCompare(_value, newValue)) return;
        _value = newValue;
        emit valueChanged(newValue);
        // Перерисовка спидометра управляется анимацией
    }

    QString currency;
    double _value;
    QString mode;
    QPropertyAnimation* animation;

    QTimer* renderTimer;       // Таймер для ограничения частоты перерисовки (16ms)
    QTimer* cacheUpdateTimer;  // Таймер для редкого обновления кэша (500ms)
    // QTimer* profilerTimer;     // Удален для снижения нагрузки

    double minInit, maxInit, minFactor, maxFactor;
    int volatilityWindow, maxPoints, sampleMethod, cacheSize;
    QMap<QString, int> timeScales;
    QString currentScale;
    bool showAxisLabels, showTooltips, smoothLines, trendColors, logScale, highlightLast, showGrid;

    std::deque<std::pair<double, double>> history, btcRatioHistory;
    double volatility;
    double btcPrice;
    std::optional<double> cachedMinVal, cachedMaxVal;
    std::vector<double> cachedProcessedHistory;
    std::vector<double> cachedProcessedBtcRatio;

    // --- Новое ---
    bool dataNeedsRedraw; // Флаг, указывающий, что данные изменились и нужна перерисовка
};

// --- CryptoDashboard ---
// (Код CryptoDashboard оставлен без изменений из File 1)
class CryptoDashboard : public QMainWindow {
    Q_OBJECT
public:
    CryptoDashboard() : btcPrice(0) {
        QWidget* central = new QWidget(this);
        setCentralWidget(central);
        QGridLayout* layout = new QGridLayout(central);
        layout->setSpacing(10);
        int row = 0, col = 0;
        for (const QString& currency : CURRENCIES) {
            DynamicSpeedometer* speedo = new DynamicSpeedometer(currency);
            speedometers[currency] = speedo;
            layout->addWidget(speedo, row, col);
            if (++col >= 4) { col = 0; ++row; }
        }
        setStyleSheet("background: #202429;");
        setWindowTitle("Crypto Dashboard");
        resize(1600, 900);

        dataWorker = new DataWorker();
        workerThread = new QThread(this);
        dataWorker->moveToThread(workerThread);
        connect(workerThread, &QThread::started, dataWorker, &DataWorker::start);
        connect(dataWorker, &DataWorker::dataUpdated, this, &CryptoDashboard::handleData);
        connect(dataWorker, &DataWorker::workerError, this, [](const QString& error) {
            qWarning() << "Worker error:" << error;
        });
        connect(workerThread, &QThread::finished, dataWorker, &QObject::deleteLater);
        workerThread->start();
    }
    ~CryptoDashboard() {
        dataWorker->stop();
        workerThread->quit();
        workerThread->wait();
    }

private slots:
    void handleData(const QString& currency, double price, double timestamp) {
        // PROFILE_SCOPE(QString("CryptoDashboard::handleData_%1").arg(currency));
        if (speedometers.contains(currency)) {
            if (currency == "BTC") btcPrice = price;
            QMetaObject::invokeMethod(this, [this, currency, price, timestamp]() {
                speedometers[currency]->updateData(price, timestamp, btcPrice);
            }, Qt::QueuedConnection);
        }
    }

private:
    QMap<QString, DynamicSpeedometer*> speedometers;
    DataWorker* dataWorker;
    QThread* workerThread;
    double btcPrice;
};

#include "main.moc" // Убедитесь, что имя файла соответствует

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    qApp->setStyleSheet("QWidget { background-color: #121212; color: white; font-family: Segoe UI, sans-serif; }");
    CryptoDashboard w;
    w.show();
    return app.exec();
}
