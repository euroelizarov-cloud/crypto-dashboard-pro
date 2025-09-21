// Refactor variant using Qt Charts for line charts rendering
#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QWebSocket>
#include <QGridLayout>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QMenu>
#include <QActionGroup>
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
#include <QMap>
#include <QLocale>
#include <QSettings>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QInputDialog>
#include <QContextMenuEvent>
#include <QRandomGenerator>

// Qt Charts
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

const QStringList CURRENCIES = {"BTC", "XRP", "BNB", "SOL", "DOGE", "XLM", "HBAR", "ETH", "APT", "TAO", "LAYER", "TON"};

// Simple profiler with periodic dump to file
class Profiler {
public:
	static void setEnabled(bool en) { enabled() = en; }
	static bool isEnabled() { return enabled(); }
	class Scope {
	public:
		Scope(const char* name) : _name(name) { if (Profiler::isEnabled()) _timer.start(); }
		~Scope() {
			if (!Profiler::isEnabled()) return; qint64 ns = _timer.nsecsElapsed();
			auto& tot = totals(); auto& cnt = counts(); tot[_name] += ns; cnt[_name] += 1;
			Profiler::maybeDump();
		}
	private:
		const char* _name; QElapsedTimer _timer;
	};
	static void maybeDump() {
		if (!isEnabled()) return; qint64 now = dumpTimer().elapsed();
		if (now - lastDumpMs() < 5000) return; // dump every 5s
		dumpToFile(); lastDumpMs() = now;
	}
	static void dumpToFile() {
		QString path = QCoreApplication::applicationDirPath() + "/profiler_stats.txt";
		QFile f(path); if (f.open(QIODevice::Append | QIODevice::Text)) {
			QTextStream out(&f); out << "==== PROFILER DUMP " << QDateTime::currentDateTime().toString(Qt::ISODate) << " ====" << '\n';
			for (auto it = totals().cbegin(); it != totals().cend(); ++it) {
				qint64 ns = it.value(); qint64 c = counts().value(it.key()); double avgMs = (c>0? (ns/1e6)/double(c) : 0.0);
				out << it.key() << ": calls=" << c << ", total_ms=" << QString::number(ns/1e6, 'f', 3) << ", avg_ms=" << QString::number(avgMs, 'f', 3) << '\n';
			}
			out << '\n';
		}
	}
private:
	static bool& enabled() { static bool e=false; return e; }
	static QMap<QString,qint64>& totals() { static QMap<QString,qint64> t; return t; }
	static QMap<QString,qint64>& counts() { static QMap<QString,qint64> c; return c; }
	static qint64& lastDumpMs() { static qint64 v=0; return v; }
	static QElapsedTimer& dumpTimer() { static QElapsedTimer t = [](){ QElapsedTimer z; z.start(); return z; }(); return t; }
};

#define PROFILE_SCOPE(name) Profiler::Scope CONCAT_SCOPE_##__LINE__(name)

enum class StreamMode { Trade, Ticker };

class PerformanceConfigDialog : public QDialog {
	Q_OBJECT
public:
	PerformanceConfigDialog(QWidget* parent,
							int animMsInit,
							int renderMsInit,
							int cacheMsInit,
							int volWindowInit,
							int maxPtsInit,
							int rawCacheInit) : QDialog(parent) {
		setWindowTitle("Performance Settings (Charts)");
		auto* layout = new QFormLayout(this);
		animMs = new QSpinBox(); animMs->setRange(50, 5000); animMs->setValue(animMsInit);
		renderMs = new QSpinBox(); renderMs->setRange(8, 1000); renderMs->setValue(renderMsInit);
		cacheMs = new QSpinBox(); cacheMs->setRange(50, 5000); cacheMs->setValue(cacheMsInit);
		volWindow = new QSpinBox(); volWindow->setRange(10, 20000); volWindow->setValue(volWindowInit);
		maxPts = new QSpinBox(); maxPts->setRange(100, 20000); maxPts->setValue(maxPtsInit);
		rawCache = new QSpinBox(); rawCache->setRange(1000, 500000); rawCache->setValue(rawCacheInit);
		layout->addRow("Animation (ms)", animMs);
		layout->addRow("Render interval (ms)", renderMs);
		layout->addRow("Cache update (ms)", cacheMs);
		layout->addRow("Volatility window", volWindow);
		layout->addRow("Max chart points", maxPts);
		layout->addRow("Raw cache size", rawCache);
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
		layout->addRow(buttons);
	}
	int animationMs() const { return animMs->value(); }
	int renderIntervalMs() const { return renderMs->value(); }
	int cacheIntervalMs() const { return cacheMs->value(); }
	int volatilityWindowSize() const { return volWindow->value(); }
	int maxPointsCount() const { return maxPts->value(); }
	int rawCacheSize() const { return rawCache->value(); }
private:
	QSpinBox *animMs, *renderMs, *cacheMs, *volWindow, *maxPts, *rawCache;
};

class DataWorker : public QObject {
	Q_OBJECT
public:
	explicit DataWorker(QObject* parent=nullptr) : QObject(parent), running(false), mode(StreamMode::Trade) {
		for (const QString& cur : CURRENCIES) msg_counter[cur] = 0;
		last_report_time = QDateTime::currentMSecsSinceEpoch() / 1000.0;
		// default subscribed currencies
		subscribedCurrencies = CURRENCIES;
	}
	void setMode(StreamMode m) { mode = m; }
public slots:
	void setCurrencies(const QStringList& list) {
		// Update desired subscription list and reconnect if running
		subscribedCurrencies = list;
		// Ensure counters exist for new currencies
		for (const QString& cur : subscribedCurrencies) {
			if (!msg_counter.contains(cur)) msg_counter[cur] = 0;
		}
		if (running) {
			qDebug() << "Re-subscribing to currencies:" << subscribedCurrencies;
			// Close and reconnect with new streams
			if (webSocket) {
				webSocket->close();
				webSocket->deleteLater();
				webSocket = nullptr;
			}
			startPingWatchdog(false);
			connectWebSocket();
		}
	}
public slots:
	void start() { if (running) return; running = true; connectWebSocket(); }
	void stop() {
		running = false;
		startPingWatchdog(false);
		if (webSocket && webSocket->state() != QAbstractSocket::UnconnectedState) webSocket->close();
	}
signals:
	void dataUpdated(const QString& currency, double price, double timestamp);
	void workerError(const QString& errorString);
private slots:
	void onConnected() {
		qDebug() << "WebSocket connected";
		reconnectAttempt = 0;
		lastPongMs = nowMs();
		lastMsgMs = nowMs();
		startPingWatchdog(true);
	}
	void onDisconnected() { qDebug() << "WebSocket disconnected"; startPingWatchdog(false); scheduleReconnect(); }
	void onError(QAbstractSocket::SocketError) { qDebug() << "WebSocket error:" << (webSocket? webSocket->errorString() : QString("unknown")); startPingWatchdog(false); scheduleReconnect(); }
	void processMessage(const QString& message) {
		if (!running) return;
		QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
		if (!doc.isObject()) return; QJsonObject root = doc.object(); if (!root.contains("data")) return; QJsonObject data = root["data"].toObject();
		QString symbol = data.value("s").toString(); if (symbol.isEmpty()) return; QString currency = symbol.left(symbol.length()-4).toUpper();
		double price = 0.0, timestamp = 0.0;
		if (mode == StreamMode::Trade) { price = data.value("p").toString().toDouble(); timestamp = data.value("T").toDouble()/1000.0; }
		else { price = data.value("c").toString().toDouble(); timestamp = data.value("E").toDouble()/1000.0; }
		if (price <= 0 || timestamp <= 0) return;
		if (msg_counter.contains(currency)) msg_counter[currency]++;
		double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
		if (now - last_report_time >= 10) {
			double interval = now - last_report_time; qDebug() << (mode == StreamMode::Trade ? "TRADE" : "TICKER") << "messages per second:";
			const auto& reportList = subscribedCurrencies.isEmpty() ? CURRENCIES : subscribedCurrencies;
			for (const QString& cur : reportList) { qDebug() << QString("  %1: %2 msg/s").arg(cur).arg(msg_counter[cur] / interval, 0, 'f', 2); msg_counter[cur] = 0; }
			last_report_time = now;
		}
		lastMsgMs = nowMs();
		emit dataUpdated(currency, price, timestamp);
	}
private:
	void connectWebSocket() {
		if (!running) return;
		if (webSocket) { webSocket->deleteLater(); webSocket = nullptr; }
		webSocket = new QWebSocket();
		connect(webSocket, &QWebSocket::connected, this, &DataWorker::onConnected);
		connect(webSocket, &QWebSocket::disconnected, this, &DataWorker::onDisconnected);
		connect(webSocket, &QWebSocket::errorOccurred, this, &DataWorker::onError);
		connect(webSocket, &QWebSocket::textMessageReceived, this, &DataWorker::processMessage);
		connect(webSocket, &QWebSocket::pong, this, [this](quint64 /*elapsed*/, const QByteArray& /*payload*/){ lastPongMs = nowMs(); });
		QStringList streams; const QString suffix = (mode == StreamMode::Trade) ? "@trade" : "@ticker";
		const auto& list = subscribedCurrencies.isEmpty() ? CURRENCIES : subscribedCurrencies;
		for (const QString& cur : list) streams << QString("%1usdt%2").arg(cur.toLower(), suffix);
		const QString url = QString("wss://stream.binance.com:9443/stream?streams=%1").arg(streams.join('/'));
		qDebug() << "Connecting to" << url; webSocket->open(QUrl(url));
	}
	void scheduleReconnect() {
		if (!running) return;
		// exponential backoff with cap and jitter
		int base = 1000; // 1s
		int delay = base * (1 << std::min(reconnectAttempt, 5)); // up to ~32s
		delay = std::min(delay, 30000);
		delay += int(QRandomGenerator::global()->bounded(500)); // jitter up to 0.5s
		reconnectAttempt++;
		qDebug() << "Scheduling reconnect in" << delay << "ms";
		QTimer::singleShot(delay, this, &DataWorker::connectWebSocket);
	}
	void startPingWatchdog(bool enable) {
		if (enable) {
			if (!pingTimer) {
				pingTimer = new QTimer(this);
				connect(pingTimer, &QTimer::timeout, this, [this]() {
					if (webSocket && webSocket->state() == QAbstractSocket::ConnectedState) {
						webSocket->ping("ka");
					}
				});
			}
			if (!watchdogTimer) {
				watchdogTimer = new QTimer(this);
				connect(watchdogTimer, &QTimer::timeout, this, [this]() {
					qint64 now = nowMs();
					bool pongStale = (now - lastPongMs) > pongTimeoutMs;
					bool idle = (now - lastMsgMs) > idleTimeoutMs;
					if (pongStale || idle) {
						qDebug() << "Watchdog trigger. pongStale=" << pongStale << " idle=" << idle;
						if (webSocket) {
							webSocket->close();
							webSocket->deleteLater();
							webSocket = nullptr;
						}
						startPingWatchdog(false);
						scheduleReconnect();
					}
				});
			}
			lastPongMs = nowMs();
			lastMsgMs = nowMs();
			pingTimer->start(pingIntervalMs);
			watchdogTimer->start(watchdogIntervalMs);
		} else {
			if (pingTimer) pingTimer->stop();
			if (watchdogTimer) watchdogTimer->stop();
		}
	}
	static qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
	QWebSocket* webSocket=nullptr; QMap<QString,int> msg_counter; double last_report_time; std::atomic<bool> running; StreamMode mode;
	QStringList subscribedCurrencies; int reconnectAttempt = 0;
	QTimer* pingTimer=nullptr; QTimer* watchdogTimer=nullptr; qint64 lastPongMs=0; qint64 lastMsgMs=0;
	const int pingIntervalMs = 10000; // 10s
	const int watchdogIntervalMs = 5000; // 5s checks
	const int pongTimeoutMs = 20000; // 20s without pong -> reconnect
	const int idleTimeoutMs = 30000; // 30s without msgs -> reconnect
};

namespace QtChartsWidgets {

class DynamicSpeedometerCharts : public QWidget {
	Q_OBJECT
	Q_PROPERTY(double value READ getValue WRITE setValue NOTIFY valueChanged)
public:
	explicit DynamicSpeedometerCharts(const QString& currency, QWidget* parent=nullptr)
		: QWidget(parent), currency(currency), _value(0), modeView("speedometer"),
		  minInit(0.9995), maxInit(1.0005), volatilityWindow(800), minFactor(1.00001), maxFactor(0.99999),
		  maxPoints(800), sampleMethod(0), cacheSize(20000), currentScale("5m"),
		  showAxisLabels(false), showTooltips(false), smoothLines(false), trendColors(false), logScale(false),
		  highlightLast(true), showGrid(true), volatility(0.0), btcPrice(0), dataNeedsRedraw(false)
	{
		timeScales = {{"1m",60},{"5m",300},{"15m",900},{"30m",1800},{"1h",3600},{"4h",14400},{"24h",86400}};
		setMinimumSize(100,100);
		animation = new QPropertyAnimation(this, "value", this); animation->setDuration(400);
		connect(animation, &QPropertyAnimation::valueChanged, this, [this](){ if (modeView=="speedometer") update(); });

		// Charts setup
	chart = new QChart(); chart->setBackgroundBrush(QColor(40,44,52)); chart->legend()->hide();
	seriesNormal = new QLineSeries(chart); seriesRatio = new QLineSeries(chart);
	chart->addSeries(seriesNormal); chart->addSeries(seriesRatio);
	axisX = new QValueAxis(chart); axisY = new QValueAxis(chart);
		axisX->setVisible(false); axisX->setTickCount(2);
		axisY->setLabelFormat("%.2f");
		chart->setTitleBrush(Qt::white);
		chart->addAxis(axisX, Qt::AlignBottom); chart->addAxis(axisY, Qt::AlignLeft);
		seriesNormal->attachAxis(axisX); seriesNormal->attachAxis(axisY);
		seriesRatio->attachAxis(axisX); seriesRatio->attachAxis(axisY);
		seriesRatio->setVisible(false);

	chartView = new QChartView(chart, this); chartView->setRenderHint(QPainter::Antialiasing); chartView->setVisible(false);
		chartView->setGeometry(rect());

		renderTimer = new QTimer(this); renderTimer->setSingleShot(false);
		connect(renderTimer, &QTimer::timeout, this, &DynamicSpeedometerCharts::onRenderTimeout); renderTimer->start(16);
		cacheUpdateTimer = new QTimer(this); cacheUpdateTimer->setSingleShot(false);
		connect(cacheUpdateTimer, &QTimer::timeout, this, &DynamicSpeedometerCharts::cacheChartData); cacheUpdateTimer->start(300);
	}

	void resizeEvent(QResizeEvent*) override { if (chartView) chartView->setGeometry(rect()); }

	void updateData(double price, double timestamp, double btcPrice = 0) {
		PROFILE_SCOPE("updateData");
		history.push_back({timestamp, price}); if (history.size() > static_cast<size_t>(cacheSize)) history.pop_front();
		if (currency=="BTC") { this->btcPrice = price; btcRatioHistory.push_back({timestamp, 1.0}); }
		else if (btcPrice>0) { btcRatioHistory.push_back({timestamp, price/btcPrice}); }
		if (btcRatioHistory.size() > static_cast<size_t>(cacheSize)) btcRatioHistory.pop_front();
		updateVolatility(); updateBounds(price);
		double scaled = 50; if (cachedMinVal && cachedMaxVal && cachedMaxVal.value() > cachedMinVal.value()) {
			scaled = (price - cachedMinVal.value()) / (cachedMaxVal.value() - cachedMinVal.value()) * 100; scaled = std::clamp(scaled, 0.0, 100.0);
		}
		animation->stop(); animation->setStartValue(_value); animation->setEndValue(scaled); animation->start();
		dataNeedsRedraw = true;
	}

	void applyPerformance(int animMs, int renderMs, int cacheMs, int volWindowSize, int maxPts, int rawCacheSize) {
		animation->setDuration(animMs); renderTimer->setInterval(std::max(1, renderMs)); cacheUpdateTimer->setInterval(std::max(10, cacheMs));
		volatilityWindow = volWindowSize; maxPoints = maxPts; setRawCacheSize(rawCacheSize); cacheChartData(); update();
	}
	void setRawCacheSize(int sz) {
		cacheSize = std::max(100, sz);
		// Trim existing data
		while (history.size() > static_cast<size_t>(cacheSize)) history.pop_front();
		while (btcRatioHistory.size() > static_cast<size_t>(cacheSize)) btcRatioHistory.pop_front();
	}

signals:
	void valueChanged(double newValue);
	void requestRename(const QString& currentTicker);

protected:
	void paintEvent(QPaintEvent*) override {
		if (modeView == "speedometer") {
			QPainter p(this); p.setRenderHint(QPainter::Antialiasing); drawSpeedometer(p);
		} else {
			// ChartView handles painting
		}
	}
	void contextMenuEvent(QContextMenuEvent* e) override {
		QMenu menu(this);
		QAction* renameAct = menu.addAction("Rename ticker...");
		connect(renameAct, &QAction::triggered, this, [this]() { emit requestRename(currency); });
		menu.exec(e->globalPos());
	}
	void mousePressEvent(QMouseEvent* e) override {
		if (e->button() == Qt::LeftButton) {
			if (modeView=="speedometer") setModeView("line_chart");
			else if (modeView=="line_chart") setModeView("btc_ratio");
			else setModeView("speedometer");
		}
	}

private slots:
	void onRenderTimeout() { if (dataNeedsRedraw && (modeView!="speedometer")) { dataNeedsRedraw=false; updateChartSeries(); } }
	void setTimeScale(const QString& scale) { if (timeScales.contains(scale)) { currentScale=scale; cacheChartData(); updateChartSeries(); } }

private:
	void setModeView(const QString& mv) {
		modeView = mv;
		bool charts = (modeView != "speedometer");
		chartView->setVisible(charts);
		if (charts) updateChartSeries(); else update();
	}
	void cacheChartData() {
		PROFILE_SCOPE("cacheChartData");
		cachedProcessedHistory = processHistory(false); cachedProcessedBtcRatio = processHistory(true); dataNeedsRedraw = true;
	}
	std::vector<double> processHistory(bool useBtcRatio) {
		PROFILE_SCOPE("processHistory");
		double now = QDateTime::currentMSecsSinceEpoch() / 1000.0; double timeWindow = timeScales[currentScale]; double cutoff = now - timeWindow;
		const auto& src = useBtcRatio ? btcRatioHistory : history; std::vector<std::pair<double,double>> filtered; filtered.reserve(src.size());
		for (const auto& pr : src) if (pr.first >= cutoff) filtered.push_back(pr);
		if (filtered.size() <= static_cast<size_t>(maxPoints)) { std::vector<double> out; out.reserve(filtered.size()); for (auto& kv:filtered) out.push_back(kv.second); return out; }
		std::vector<double> samples; samples.reserve(maxPoints); size_t step = std::max<size_t>(1, filtered.size()/maxPoints);
		for (size_t i=0;i<filtered.size();i+=step) {
			auto start = filtered.begin()+i; auto end = filtered.begin()+std::min(i+step, filtered.size());
			if (sampleMethod==0) samples.push_back((end-1)->second);
			else if (sampleMethod==1) { double sum=0.0; size_t c=0; for (auto it=start; it!=end; ++it) { sum+=it->second; ++c; } samples.push_back(c? sum/c : (end-1)->second); }
			else { double mx=(start!=end)? start->second : 0.0; for (auto it=start; it!=end; ++it) mx=std::max(mx, it->second); samples.push_back(mx); }
		}
		if (samples.size() > static_cast<size_t>(maxPoints)) samples.resize(maxPoints); return samples;
	}
	void updateVolatility() {
		PROFILE_SCOPE("updateVolatility");
		if (history.size()<2) { volatility=0.0; return; } size_t window = std::min(static_cast<size_t>(volatilityWindow), history.size());
		std::vector<double> prices; prices.reserve(window); for (auto it = history.end()-window; it!=history.end(); ++it) prices.push_back(it->second);
		std::vector<double> rets; rets.reserve(prices.size()-1); for (size_t i=1;i<prices.size();++i) rets.push_back(prices[i-1] ? std::abs((prices[i]-prices[i-1])/prices[i-1]) : 0.0);
		volatility = rets.empty()? 0.0 : (std::accumulate(rets.begin(), rets.end(), 0.0)/rets.size())*100.0;
	}
	void updateBounds(double price) {
		if (!cachedMinVal || !cachedMaxVal) { cachedMinVal=price*minInit; cachedMaxVal=price*maxInit; return; }
		cachedMinVal = cachedMinVal.value()*minFactor + price*(1.0-minFactor); cachedMaxVal = cachedMaxVal.value()*maxFactor + price*(1.0-maxFactor);
		if (price < cachedMinVal.value()) cachedMinVal=price; if (price > cachedMaxVal.value()) cachedMaxVal=price;
		double range = cachedMaxVal.value() - cachedMinVal.value(); double epsilon = std::max(1e-10, range*1e-5);
		if (range < epsilon) { double center = (cachedMaxVal.value()+cachedMinVal.value())/2.0; cachedMinVal=center-epsilon/2.0; cachedMaxVal=center+epsilon/2.0; }
	}
	void drawSpeedometer(QPainter& painter) {
		PROFILE_SCOPE("drawSpeedometer");
		int w = width(), h = height(); int size = std::min(w,h) - 20; QRect rect((w-size)/2,(h-size)/2,size,size);
		painter.fillRect(0,0,w,h,QColor(40,44,52)); painter.setPen(QPen(QColor(100,100,100), 4)); painter.drawArc(rect, 45*16, 270*16);
		if (cachedMinVal && cachedMaxVal) {
			struct Zone{double s,e; QColor c;}; std::vector<Zone> zones={{0,70,QColor(76,175,80)},{70,90,QColor(255,193,7)},{90,100,QColor(244,67,54)}};
			for (const auto& z:zones) { painter.setPen(QPen(z.c,8)); double span=(z.e-z.s)*270/100; double angle=45+(270*z.s/100); painter.drawArc(rect, int(angle*16), int(span*16)); }
		}
		double angle = 45 + (270 * _value / 100); painter.setPen(QPen(Qt::white,2)); painter.translate(w/2,h/2); painter.rotate(angle);
		painter.drawLine(0,0,size/2-15,0); painter.setBrush(QColor(240,67,54,190)); painter.drawEllipse(QPoint(size/2-15,0), 18,8); painter.resetTransform();
		painter.setFont(QFont("Arial",8)); painter.setPen(Qt::yellow); painter.drawText(QRect(1,h-50,w-10,20), Qt::AlignLeft|Qt::AlignBottom, QString("Trades: %1").arg(history.size()));
		painter.setPen(Qt::white); painter.setFont(QFont("Arial",21, QFont::Bold)); painter.drawText(QRect(0,h/2-50,w,20), Qt::AlignCenter, currency);
		if (!history.empty()) { double currentPrice = history.back().second; painter.setFont(QFont("Arial",21, QFont::Bold)); painter.drawText(QRect(0,h/2+20,w,20), Qt::AlignCenter, QLocale().toString(currentPrice,'f',3)); }
		painter.setFont(QFont("Arial",6)); painter.drawText(QRect(1,1,w-10,20), Qt::AlignLeft|Qt::AlignTop, QString("Volatility: %1%\n").arg(volatility,0,'f',4));
	}
	void updateChartSeries() {
		PROFILE_SCOPE("updateChartSeries");
		const bool useBtc = (modeView=="btc_ratio");
		auto& values = useBtc ? cachedProcessedBtcRatio : cachedProcessedHistory;
		seriesNormal->setVisible(!useBtc); seriesRatio->setVisible(useBtc);
	QLineSeries* s = useBtc? seriesRatio : seriesNormal;
		QVector<QPointF> pts; pts.reserve(int(values.size()));
		for (int i=0;i<int(values.size());++i) pts.append(QPointF(i, values[size_t(i)]));
		s->replace(pts);
		if (!values.empty()) {
			auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
			double minY = *minIt, maxY = *maxIt; if (qFuzzyCompare(minY, maxY)) maxY += useBtc ? 1e-8 : 1e-4;
			axisX->setRange(0, std::max<int>(1, int(values.size()-1)));
			axisY->setRange(minY, maxY);
			chart->setTitle(useBtc ? QString("%1/BTC | %2").arg(currency, currentScale) : QString("%1 | %2").arg(currency, currentScale));
		}
	}
	// Value property API
	double getValue() const { return _value; }
	void setValue(double v) { if (qFuzzyCompare(_value, v)) return; _value=v; emit valueChanged(v); }

	// Public API to update the displayed currency label
public:
	void setCurrencyName(const QString& name) { currency = name; if (modeView != "speedometer") updateChartSeries(); else update(); }
private:

	// State
	QString currency; double _value; QString modeView; QPropertyAnimation* animation;
	QTimer* renderTimer; QTimer* cacheUpdateTimer; int volatilityWindow, maxPoints, sampleMethod, cacheSize; QMap<QString,int> timeScales; QString currentScale;
	bool showAxisLabels, showTooltips, smoothLines, trendColors, logScale, highlightLast, showGrid; double volatility; double btcPrice; std::optional<double> cachedMinVal, cachedMaxVal; bool dataNeedsRedraw;
	std::deque<std::pair<double,double>> history, btcRatioHistory; std::vector<double> cachedProcessedHistory, cachedProcessedBtcRatio;

	// Charts
	QChart* chart; QChartView* chartView; QLineSeries* seriesNormal; QLineSeries* seriesRatio; QValueAxis* axisX; QValueAxis* axisY;

	// Bounds smoothing
	double minInit, maxInit, minFactor, maxFactor;
};
}

class CryptoDashboardCharts : public QMainWindow {
	Q_OBJECT
public:
	CryptoDashboardCharts() : btcPrice(0), streamMode(StreamMode::Trade) {
		QWidget* central = new QWidget(this); setCentralWidget(central); auto* layout = new QGridLayout(central); layout->setSpacing(10);
		// Load currencies from settings or default list
		currentCurrencies = readCurrenciesSettings();
		int row=0, col=0; for (const QString& c : currentCurrencies) {
			auto* w = new QtChartsWidgets::DynamicSpeedometerCharts(c);
			widgets[c]=w;
			layout->addWidget(w,row,col);
			connect(w, &QtChartsWidgets::DynamicSpeedometerCharts::requestRename, this, &CryptoDashboardCharts::onRequestRename);
			if (++col>=4) { col=0; ++row; }
		}
		setStyleSheet("background: #202429;"); setWindowTitle("Crypto Dashboard (Charts)"); resize(1600,900);
		auto* modeMenu = menuBar()->addMenu("Mode"); auto* tradeAct = modeMenu->addAction("TRADE stream"); tradeAct->setCheckable(true); tradeAct->setChecked(true);
		auto* tickerAct = modeMenu->addAction("TICKER stream"); tickerAct->setCheckable(true); auto* group = new QActionGroup(this); group->addAction(tradeAct); group->addAction(tickerAct); group->setExclusive(true);
		connect(tradeAct, &QAction::triggered, this, [this](){ switchMode(StreamMode::Trade); }); connect(tickerAct, &QAction::triggered, this, [this](){ switchMode(StreamMode::Ticker); });
	// Settings and persistence
	auto* settingsMenu = menuBar()->addMenu("Settings");
	auto* perfAct = settingsMenu->addAction("Performance...");
	connect(perfAct, &QAction::triggered, this, &CryptoDashboardCharts::openPerformanceDialog);
	loadSettingsAndApply();
		dataWorker = new DataWorker(); dataWorker->setMode(streamMode); workerThread = new QThread(this); dataWorker->moveToThread(workerThread);
		// Push initial currencies to worker
		QMetaObject::invokeMethod(dataWorker, [this]() { dataWorker->setCurrencies(currentCurrencies); }, Qt::QueuedConnection);
		connect(workerThread, &QThread::started, dataWorker, &DataWorker::start); connect(dataWorker, &DataWorker::dataUpdated, this, &CryptoDashboardCharts::handleData);
		connect(workerThread, &QThread::finished, dataWorker, &QObject::deleteLater); workerThread->start();
	}
	~CryptoDashboardCharts(){ dataWorker->stop(); workerThread->quit(); workerThread->wait(); }
private slots:
	void switchMode(StreamMode m) {
		streamMode = m; QMetaObject::invokeMethod(dataWorker, [this,m](){ dataWorker->stop(); dataWorker->setMode(m); dataWorker->start(); }, Qt::QueuedConnection);
		setWindowTitle(QString("Crypto Dashboard (Charts) — %1").arg(m==StreamMode::Trade?"TRADE":"TICKER"));
	}
	void openPerformanceDialog() {
		auto s = readPerfSettings();
		PerformanceConfigDialog dlg(this, s.animMs, s.renderMs, s.cacheMs, s.volWindow, s.maxPts, s.rawCache);
		if (dlg.exec()==QDialog::Accepted) {
			// Save chosen values
			PerfSettings ns{dlg.animationMs(), dlg.renderIntervalMs(), dlg.cacheIntervalMs(), dlg.volatilityWindowSize(), dlg.maxPointsCount(), dlg.rawCacheSize()};
			writePerfSettings(ns);
			// Apply to all widgets
			for (auto* w : widgets) w->applyPerformance(ns.animMs, ns.renderMs, ns.cacheMs, ns.volWindow, ns.maxPts, ns.rawCache);
		}
	}
	void handleData(const QString& currency, double price, double timestamp) {
		if (widgets.contains(currency)) { if (currency=="BTC") btcPrice=price; QMetaObject::invokeMethod(this, [this,currency,price,timestamp](){ widgets[currency]->updateData(price, timestamp, btcPrice); }, Qt::QueuedConnection); }
	}
	void onRequestRename(const QString& currentTicker) {
		// Ask user for new ticker (USDT pair symbol, without USDT suffix)
		bool ok=false;
		QStringList suggestions = CURRENCIES;
		suggestions.removeAll(currentTicker);
		suggestions.prepend(currentTicker);
		QString newTicker = QInputDialog::getText(this, "Rename ticker", "Enter new ticker (e.g., BTC, ETH):", QLineEdit::Normal, currentTicker, &ok);
		if (!ok) return;
		newTicker = newTicker.trimmed().toUpper();
		if (newTicker.isEmpty()) return;
		if (newTicker == currentTicker) return;
		if (!widgets.contains(currentTicker)) return;
		auto* w = widgets[currentTicker];
		widgets.remove(currentTicker);
		if (widgets.contains(newTicker)) {
			// Swap: move existing widget's name to a temporary to avoid collision
			auto* other = widgets[newTicker];
			widgets[newTicker] = w;
			w->setCurrencyName(newTicker);
			// The 'other' widget must be reassigned to old name
			widgets[currentTicker] = other;
			other->setCurrencyName(currentTicker);
		} else {
			widgets[newTicker] = w;
			w->setCurrencyName(newTicker);
		}
		// Update currentCurrencies list and persist
		currentCurrencies = widgets.keys();
		saveCurrenciesSettings(currentCurrencies);
		// Re-subscribe worker to new list
		QMetaObject::invokeMethod(dataWorker, [this]() { dataWorker->setCurrencies(currentCurrencies); }, Qt::QueuedConnection);
	}
private:
	struct PerfSettings { int animMs; int renderMs; int cacheMs; int volWindow; int maxPts; int rawCache; };
	PerfSettings readPerfSettings() {
		QSettings st("alel12", "dashboard_charts");
		PerfSettings s;
		s.animMs = st.value("perf/animMs", 400).toInt();
		s.renderMs = st.value("perf/renderMs", 16).toInt();
		s.cacheMs = st.value("perf/cacheMs", 300).toInt();
		s.volWindow = st.value("perf/volWindow", 800).toInt();
		s.maxPts = st.value("perf/maxPts", 800).toInt();
		s.rawCache = st.value("perf/rawCache", 20000).toInt();
		return s;
	}
	void writePerfSettings(const PerfSettings& s) {
		QSettings st("alel12", "dashboard_charts");
		st.setValue("perf/animMs", s.animMs);
		st.setValue("perf/renderMs", s.renderMs);
		st.setValue("perf/cacheMs", s.cacheMs);
		st.setValue("perf/volWindow", s.volWindow);
		st.setValue("perf/maxPts", s.maxPts);
		st.setValue("perf/rawCache", s.rawCache);
		st.sync();
	}
	void loadSettingsAndApply() {
		auto s = readPerfSettings();
		for (auto* w : widgets) w->applyPerformance(s.animMs, s.renderMs, s.cacheMs, s.volWindow, s.maxPts, s.rawCache);
	}
	QStringList readCurrenciesSettings() {
		QSettings st("alel12", "dashboard_charts");
		QStringList list = st.value("currencies/list").toStringList();
		if (list.isEmpty()) list = CURRENCIES;
		return list;
	}
	void saveCurrenciesSettings(const QStringList& list) {
		QSettings st("alel12", "dashboard_charts");
		st.setValue("currencies/list", list);
		st.sync();
	}
	QMap<QString, QtChartsWidgets::DynamicSpeedometerCharts*> widgets; DataWorker* dataWorker; QThread* workerThread; double btcPrice; StreamMode streamMode; QStringList currentCurrencies;
};

#include "main_unified_charts.moc"

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	Profiler::setEnabled(true);
	qApp->setStyleSheet("QWidget { background-color: #121212; color: white; font-family: -apple-system, 'SF Pro Text', 'Helvetica Neue', Arial, sans-serif; }");
	CryptoDashboardCharts w; w.show(); return app.exec();
}
