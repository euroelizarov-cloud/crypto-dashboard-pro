#include "MainWindow.h"
#include <QGridLayout>
#include <QThread>
#include <QMenuBar>
#include <QActionGroup>
#include <QDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QSettings>
#include <QApplication>
#include <algorithm>

static const QStringList DEFAULT_CURRENCIES = {"BTC", "XRP", "BNB", "SOL", "DOGE", "XLM", "HBAR", "ETH", "APT", "TAO", "LAYER", "TON"};

class PerformanceConfigDialog : public QDialog {
    Q_OBJECT
public:
    PerformanceConfigDialog(QWidget* parent,
                            int animMsInit,int renderMsInit,int cacheMsInit,int volWindowInit,int maxPtsInit,int rawCacheInit,
                            double pyInitSpanPctInit, double pyMinCompressInit, double pyMaxCompressInit, double pyMinWidthPctInit)
                            : QDialog(parent) {
        setWindowTitle("Performance Settings");
        auto* layout = new QFormLayout(this);
        animMs = new QSpinBox(); animMs->setRange(50,5000); animMs->setValue(animMsInit);
        renderMs = new QSpinBox(); renderMs->setRange(8,1000); renderMs->setValue(renderMsInit);
        cacheMs = new QSpinBox(); cacheMs->setRange(50,5000); cacheMs->setValue(cacheMsInit);
        volWindow = new QSpinBox(); volWindow->setRange(10,20000); volWindow->setValue(volWindowInit);
        maxPts = new QSpinBox(); maxPts->setRange(100,20000); maxPts->setValue(maxPtsInit);
        rawCache = new QSpinBox(); rawCache->setRange(1000,500000); rawCache->setValue(rawCacheInit);
        // Python-like scaling controls
        pyInitSpanPct = new QDoubleSpinBox(); pyInitSpanPct->setRange(0.000001, 0.5); pyInitSpanPct->setDecimals(6); pyInitSpanPct->setSingleStep(0.0005); pyInitSpanPct->setValue(pyInitSpanPctInit);
        pyMinCompress = new QDoubleSpinBox(); pyMinCompress->setRange(1.0, 1.01); pyMinCompress->setDecimals(6); pyMinCompress->setSingleStep(0.000001); pyMinCompress->setValue(pyMinCompressInit);
        pyMaxCompress = new QDoubleSpinBox(); pyMaxCompress->setRange(0.99, 1.0); pyMaxCompress->setDecimals(6); pyMaxCompress->setSingleStep(0.000001); pyMaxCompress->setValue(pyMaxCompressInit);
        pyMinWidthPct = new QDoubleSpinBox(); pyMinWidthPct->setRange(0.00000001, 0.01); pyMinWidthPct->setDecimals(8); pyMinWidthPct->setSingleStep(0.0000005); pyMinWidthPct->setValue(pyMinWidthPctInit);
        layout->addRow("Animation (ms)", animMs);
        layout->addRow("Render interval (ms)", renderMs);
        layout->addRow("Cache update (ms)", cacheMs);
        layout->addRow("Volatility window", volWindow);
        layout->addRow("Max chart points", maxPts);
        layout->addRow("Raw cache size", rawCache);
        layout->addRow("Python init span (±pct)", pyInitSpanPct);
        layout->addRow("Python min compress (×)", pyMinCompress);
        layout->addRow("Python max compress (×)", pyMaxCompress);
        layout->addRow("Python min width (pct)", pyMinWidthPct);
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
    double pyInitSpanPctVal() const { return pyInitSpanPct->value(); }
    double pyMinCompressVal() const { return pyMinCompress->value(); }
    double pyMaxCompressVal() const { return pyMaxCompress->value(); }
    double pyMinWidthPctVal() const { return pyMinWidthPct->value(); }
private:
    QSpinBox *animMs, *renderMs, *cacheMs, *volWindow, *maxPts, *rawCache;
    QDoubleSpinBox *pyInitSpanPct, *pyMinCompress, *pyMaxCompress, *pyMinWidthPct;
};

MainWindow::MainWindow() {
    themeManager = new ThemeManager(this);
    QWidget* central = new QWidget(this); setCentralWidget(central); auto* layout = new QGridLayout(central); layout->setSpacing(10);
    // Force the exact default currencies order as requested
    currentCurrencies = DEFAULT_CURRENCIES; saveCurrenciesSettings(currentCurrencies);
    int row=0, col=0; for (const QString& c : currentCurrencies) {
        auto* w = new DynamicSpeedometerCharts(c); widgets[c]=w; layout->addWidget(w,row,col);
        connect(w, &DynamicSpeedometerCharts::requestRename, this, &MainWindow::onRequestRename);
        // Persist per-widget style when chosen from widget context menu
        connect(w, &DynamicSpeedometerCharts::styleSelected, this, [this](const QString& cur, const QString& styleName){
            QSettings st("alel12", "modular_dashboard");
            st.setValue(QString("ui/stylePerWidget/%1").arg(cur), styleName);
            st.sync();
        });
        if (++col>=4) { col=0; ++row; }
    }
    setWindowTitle("Modular Crypto Dashboard"); resize(1600,900);

    // Menu
    auto* modeMenu = menuBar()->addMenu("Mode"); auto* tradeAct = modeMenu->addAction("TRADE stream"); tradeAct->setCheckable(true); tradeAct->setChecked(true);
    auto* tickerAct = modeMenu->addAction("TICKER stream"); tickerAct->setCheckable(true); auto* group = new QActionGroup(this); group->addAction(tradeAct); group->addAction(tickerAct); group->setExclusive(true);
    connect(tradeAct, &QAction::triggered, this, [this](){ switchMode(StreamMode::Trade); }); connect(tickerAct, &QAction::triggered, this, [this](){ switchMode(StreamMode::Ticker); });
    // Provider menu (Binance/Bybit)
    QMenu* providerMenu = menuBar()->addMenu("Provider");
    QActionGroup* providerGroup = new QActionGroup(this); providerGroup->setExclusive(true);
    QAction* provBinance = providerMenu->addAction("Binance"); provBinance->setCheckable(true);
    QAction* provBybit   = providerMenu->addAction("Bybit");   provBybit->setCheckable(true);
    providerGroup->addAction(provBinance); providerGroup->addAction(provBybit);
    // Load provider from settings
    {
        QSettings st("alel12", "modular_dashboard");
        QString p = st.value("stream/provider", "Binance").toString();
        if (p.compare("Bybit", Qt::CaseInsensitive)==0) {
            provBybit->setChecked(true);
        } else {
            provBinance->setChecked(true);
        }
    }
    connect(provBinance, &QAction::triggered, this, [this](){
        QSettings st("alel12", "modular_dashboard"); st.setValue("stream/provider", "Binance"); st.sync();
        if (dataWorker) {
            QMetaObject::invokeMethod(dataWorker, "stop", Qt::QueuedConnection);
            QMetaObject::invokeMethod(dataWorker, [this](){ dataWorker->setProvider(DataProvider::Binance); dataWorker->setMode(streamMode); dataWorker->start(); }, Qt::QueuedConnection);
        }
    });
    connect(provBybit, &QAction::triggered, this, [this](){
        QSettings st("alel12", "modular_dashboard"); st.setValue("stream/provider", "Bybit"); st.sync();
        if (dataWorker) {
            QMetaObject::invokeMethod(dataWorker, "stop", Qt::QueuedConnection);
            QMetaObject::invokeMethod(dataWorker, [this](){ dataWorker->setProvider(DataProvider::Bybit); dataWorker->setMode(streamMode); dataWorker->start(); }, Qt::QueuedConnection);
        }
    });

    auto* settingsMenu = menuBar()->addMenu("Settings");
    auto* perfAct = settingsMenu->addAction("Performance..."); connect(perfAct, &QAction::triggered, this, &MainWindow::openPerformanceDialog);
    // Theme submenu with live apply
    QMenu* themeMenu = settingsMenu->addMenu("Theme");
    QActionGroup* themeGroup = new QActionGroup(this); themeGroup->setExclusive(true);
    {
        QStringList names = themeManager->themeNames();
        QString currentName = themeManager->current().name;
        for (const auto& n : names) {
            QAction* a = themeMenu->addAction(n);
            a->setCheckable(true);
            if (n == currentName) a->setChecked(true);
            themeGroup->addAction(a);
            connect(a, &QAction::triggered, this, [this, n]() {
                applyTheme(n);
                QSettings st("alel12", "modular_dashboard");
                st.setValue("ui/themeName", n);
                st.sync();
            });
        }
    }
    // Speedometer style submenu (7 variants)
    auto* styleMenu = settingsMenu->addMenu("Speedometer Style");
    QActionGroup* styleGroup = new QActionGroup(this); styleGroup->setExclusive(true);
    auto addStyle = [&](const QString& name){ QAction* a = styleMenu->addAction(name); a->setCheckable(true); styleGroup->addAction(a); return a; };
    QAction* stClassic = addStyle("Classic");
    QAction* stNeon    = addStyle("NeonGlow");
    QAction* stMinimal = addStyle("Minimal");
    QAction* stModern  = addStyle("ModernTicks");
    QAction* stCircle  = addStyle("Classic Pro");
    QAction* stGauge   = addStyle("Gauge");
    QAction* stRing    = addStyle("Modern Scale");
    auto applyStyleByName = [&](const QString& sName){
        DynamicSpeedometerCharts::SpeedometerStyle s = DynamicSpeedometerCharts::SpeedometerStyle::Classic;
        if (sName=="NeonGlow") s = DynamicSpeedometerCharts::SpeedometerStyle::NeonGlow;
        else if (sName=="Minimal") s = DynamicSpeedometerCharts::SpeedometerStyle::Minimal;
        else if (sName=="ModernTicks") s = DynamicSpeedometerCharts::SpeedometerStyle::ModernTicks;
        else if (sName=="Circle" || sName=="Classic Pro") s = DynamicSpeedometerCharts::SpeedometerStyle::Circle;
        else if (sName=="Gauge") s = DynamicSpeedometerCharts::SpeedometerStyle::Gauge;
        else if (sName=="Ring" || sName=="Modern Scale") s = DynamicSpeedometerCharts::SpeedometerStyle::Ring;
        for (auto* w : widgets) w->setSpeedometerStyle(s);
        QSettings st("alel12", "modular_dashboard"); st.setValue("ui/speedometerStyle", sName); st.sync();
    };
    connect(stClassic, &QAction::triggered, this, [applyStyleByName](){ applyStyleByName("Classic"); });
    connect(stMinimal, &QAction::triggered, this, [applyStyleByName](){ applyStyleByName("Minimal"); });
    connect(stNeon,    &QAction::triggered, this, [applyStyleByName](){ applyStyleByName("NeonGlow"); });
    connect(stModern,  &QAction::triggered, this, [applyStyleByName](){ applyStyleByName("ModernTicks"); });
    connect(stCircle,  &QAction::triggered, this, [applyStyleByName](){ applyStyleByName("Classic Pro"); });
    connect(stGauge,   &QAction::triggered, this, [applyStyleByName](){ applyStyleByName("Gauge"); });
    connect(stRing,    &QAction::triggered, this, [applyStyleByName](){ applyStyleByName("Modern Scale"); });

    // Bybit Market Preference
    QMenu* bybitMenu = settingsMenu->addMenu("Bybit Market Preference");
    QActionGroup* bybitPrefGroup = new QActionGroup(this); bybitPrefGroup->setExclusive(true);
    QAction* actLinearFirst = bybitMenu->addAction("Linear → Spot"); actLinearFirst->setCheckable(true);
    QAction* actSpotFirst   = bybitMenu->addAction("Spot → Linear"); actSpotFirst->setCheckable(true);
    bybitPrefGroup->addAction(actLinearFirst); bybitPrefGroup->addAction(actSpotFirst);
    {
        QSettings st("alel12", "modular_dashboard");
        QString pref = st.value("bybit/preference", "LinearFirst").toString();
        bool linearFirst = (pref=="LinearFirst");
        actLinearFirst->setChecked(linearFirst);
        actSpotFirst->setChecked(!linearFirst);
    }
    auto applyBybitPref = [this](bool linearFirst){
        QSettings st("alel12", "modular_dashboard"); st.setValue("bybit/preference", linearFirst?"LinearFirst":"SpotFirst"); st.sync();
        if (dataWorker) {
            QMetaObject::invokeMethod(dataWorker, [this,linearFirst](){ dataWorker->setBybitPreference(linearFirst?BybitPreference::LinearFirst:BybitPreference::SpotFirst); dataWorker->stop(); dataWorker->start(); }, Qt::QueuedConnection);
        }
        // Update badges for all widgets
        for (auto* w : widgets) w->setMarketBadge("Bybit", linearFirst?"Linear":"Spot");
    };
    connect(actLinearFirst, &QAction::triggered, this, [applyBybitPref](){ applyBybitPref(true); });
    connect(actSpotFirst,   &QAction::triggered, this, [applyBybitPref](){ applyBybitPref(false); });

    // Market map (reference list): which ticker uses which market
    QAction* actMarketMap = settingsMenu->addAction("Show Market Map...");
    connect(actMarketMap, &QAction::triggered, this, [this](){
        QStringList lines; lines << "Ticker  | Provider • Market"; lines << "-----------------------------";
        for (auto it = widgets.begin(); it != widgets.end(); ++it) {
            auto* w = it.value();
            lines << QString("%1  | %2 • %3").arg(it.key(), -6).arg(w->property("providerName").toString(), w->property("marketName").toString());
        }
        QInputDialog::getMultiLineText(this, "Market Map", "Current market per ticker:", lines.join('\n'));
    });
    // Thresholds submenu
    QMenu* thrMenu = settingsMenu->addMenu("Thresholds");
    QAction* thrEnable = thrMenu->addAction("Enable thresholds"); thrEnable->setCheckable(true);
    QAction* actWarn = thrMenu->addAction("Set warn (0-100)");
    QAction* actDanger = thrMenu->addAction("Set danger (0-100)");
    auto applyThresholdsToAll = [this](bool enabled, int warn, int danger){
        DynamicSpeedometerCharts::Thresholds t; t.enabled = enabled; t.warn = warn; t.danger = danger;
        for (auto* w : widgets) w->applyThresholds(t);
    };
    // Load thresholds state
    {
        QSettings st("alel12", "modular_dashboard");
        bool en = st.value("ui/thresholds/enabled", false).toBool();
        int warn = st.value("ui/thresholds/warn", 70).toInt();
        int danger = st.value("ui/thresholds/danger", 90).toInt();
        warn = std::clamp(warn, 0, 100); danger = std::clamp(danger, 0, 100); if (warn > danger) std::swap(warn, danger);
        thrEnable->setChecked(en); applyThresholdsToAll(en, warn, danger);
    }
    connect(thrEnable, &QAction::toggled, this, [this,applyThresholdsToAll](bool checked){
        QSettings st("alel12", "modular_dashboard");
        int warn = st.value("ui/thresholds/warn", 70).toInt();
        int danger = st.value("ui/thresholds/danger", 90).toInt();
        warn = std::clamp(warn, 0, 100); danger = std::clamp(danger, 0, 100); if (warn > danger) std::swap(warn, danger);
        st.setValue("ui/thresholds/enabled", checked); st.sync();
        applyThresholdsToAll(checked, warn, danger);
    });
    connect(actWarn, &QAction::triggered, this, [this,thrEnable,applyThresholdsToAll](){
        QSettings st("alel12", "modular_dashboard");
        int curWarn = st.value("ui/thresholds/warn", 70).toInt();
        bool ok=false; int val = QInputDialog::getInt(this, "Warn threshold", "Warn (0-100):", curWarn, 0, 100, 1, &ok);
        if (!ok) return; int danger = st.value("ui/thresholds/danger", 90).toInt();
        val = std::clamp(val, 0, 100); danger = std::clamp(danger, 0, 100);
        if (val > danger) std::swap(val, danger);
        st.setValue("ui/thresholds/warn", val); st.setValue("ui/thresholds/danger", danger); st.sync();
        applyThresholdsToAll(thrEnable->isChecked(), val, danger);
    });
    connect(actDanger, &QAction::triggered, this, [this,thrEnable,applyThresholdsToAll](){
        QSettings st("alel12", "modular_dashboard");
        int curDanger = st.value("ui/thresholds/danger", 90).toInt();
        bool ok=false; int val = QInputDialog::getInt(this, "Danger threshold", "Danger (0-100):", curDanger, 0, 100, 1, &ok);
        if (!ok) return; int warn = st.value("ui/thresholds/warn", 70).toInt();
        val = std::clamp(val, 0, 100); warn = std::clamp(warn, 0, 100);
        if (warn > val) std::swap(warn, val);
        st.setValue("ui/thresholds/warn", warn); st.setValue("ui/thresholds/danger", val); st.sync();
        applyThresholdsToAll(thrEnable->isChecked(), warn, val);
    });
    // Auto-scaling submenu
    QMenu* scalingMenu = settingsMenu->addMenu("Auto-scaling");
    QActionGroup* scalingGroup = new QActionGroup(this); scalingGroup->setExclusive(true);
    QAction* adaptiveScaling = scalingMenu->addAction("Adaptive (default)"); adaptiveScaling->setCheckable(true); adaptiveScaling->setChecked(true); scalingGroup->addAction(adaptiveScaling);
    QAction* fixedScaling = scalingMenu->addAction("Fixed range (0-100)"); fixedScaling->setCheckable(true); scalingGroup->addAction(fixedScaling);
    QAction* manualScaling = scalingMenu->addAction("Manual bounds"); manualScaling->setCheckable(true); scalingGroup->addAction(manualScaling);
    QAction* pythonScaling = scalingMenu->addAction("Python-like (compressing window)"); pythonScaling->setCheckable(true); scalingGroup->addAction(pythonScaling);
    auto applyScalingMode = [this](DynamicSpeedometerCharts::ScalingMode mode, double minVal = 0.0, double maxVal = 100.0){
        DynamicSpeedometerCharts::ScalingSettings s; s.mode = mode; s.fixedMin = minVal; s.fixedMax = maxVal;
        for (auto* w : widgets) w->applyScaling(s);
        QSettings st("alel12", "modular_dashboard");
        st.setValue("ui/scaling/mode", int(mode)); st.setValue("ui/scaling/min", minVal); st.setValue("ui/scaling/max", maxVal); st.sync();
    };
    connect(adaptiveScaling, &QAction::triggered, this, [applyScalingMode](){ applyScalingMode(DynamicSpeedometerCharts::ScalingMode::Adaptive); });
    connect(fixedScaling, &QAction::triggered, this, [applyScalingMode](){ applyScalingMode(DynamicSpeedometerCharts::ScalingMode::Fixed, 0.0, 100.0); });
    connect(manualScaling, &QAction::triggered, this, [applyScalingMode](){ applyScalingMode(DynamicSpeedometerCharts::ScalingMode::Manual); });
    connect(pythonScaling, &QAction::triggered, this, [applyScalingMode](){ applyScalingMode(DynamicSpeedometerCharts::ScalingMode::PythonLike); });
    // Load saved scaling mode, reflect in menu and apply
    {
        QSettings st("alel12", "modular_dashboard");
        auto mode = static_cast<DynamicSpeedometerCharts::ScalingMode>(st.value("ui/scaling/mode", int(DynamicSpeedometerCharts::ScalingMode::Adaptive)).toInt());
        double minVal = st.value("ui/scaling/min", 0.0).toDouble();
        double maxVal = st.value("ui/scaling/max", 100.0).toDouble();
        switch (mode) {
            case DynamicSpeedometerCharts::ScalingMode::Adaptive: adaptiveScaling->setChecked(true); break;
            case DynamicSpeedometerCharts::ScalingMode::Fixed: fixedScaling->setChecked(true); break;
            case DynamicSpeedometerCharts::ScalingMode::Manual: manualScaling->setChecked(true); break;
            case DynamicSpeedometerCharts::ScalingMode::PythonLike: pythonScaling->setChecked(true); break;
        }
        applyScalingMode(mode, minVal, maxVal);
    }

    // Data worker thread (default to TICKER mode by settings)
    // Read default stream mode
    {
        QSettings st("alel12", "modular_dashboard");
        const QString mode = st.value("stream/mode", "TICKER").toString();
        streamMode = (mode=="TICKER") ? StreamMode::Ticker : StreamMode::Trade;
        // Reflect in menu and title
        tradeAct->setChecked(streamMode==StreamMode::Trade);
        tickerAct->setChecked(streamMode==StreamMode::Ticker);
        setWindowTitle(QString("Modular Crypto Dashboard — %1").arg(streamMode==StreamMode::Trade?"TRADE":"TICKER"));
    }
    dataWorker = new DataWorker(); dataWorker->setMode(streamMode);
    // Apply saved provider before starting
    {
        QSettings st("alel12", "modular_dashboard");
        QString p = st.value("stream/provider", "Binance").toString();
        auto provBybit = (p.compare("Bybit", Qt::CaseInsensitive)==0);
        dataWorker->setProvider(provBybit ? DataProvider::Bybit : DataProvider::Binance);
        // Apply saved Bybit preference
        QString pref = st.value("bybit/preference", "LinearFirst").toString();
        dataWorker->setBybitPreference(pref=="LinearFirst"? BybitPreference::LinearFirst : BybitPreference::SpotFirst);
        // seed badges
        const QString initMarket = provBybit ? (pref=="LinearFirst"?"Linear":"Spot") : "";
        for (auto* w : widgets) w->setMarketBadge(provBybit?"Bybit":"Binance", initMarket);
    }
    workerThread = new QThread(this); dataWorker->moveToThread(workerThread);
    connect(workerThread, &QThread::started, dataWorker, &DataWorker::start);
    connect(dataWorker, &DataWorker::dataUpdated, this, &MainWindow::handleData);
    connect(dataWorker, &DataWorker::dataTick, this, [this](const QString& cur,double price,double ts,const QString& prov,const QString& market){
        if (widgets.contains(cur)) {
            widgets[cur]->setMarketBadge(prov, market);
        }
    });
    connect(dataWorker, &DataWorker::unsupportedSymbol, this, [this](const QString& cur,const QString& reason){
        if (widgets.contains(cur)) widgets[cur]->setUnsupportedReason(reason);
    });
    connect(workerThread, &QThread::finished, dataWorker, &QObject::deleteLater);
    // push currencies to worker after moving to thread
    QMetaObject::invokeMethod(dataWorker, [this](){ dataWorker->setCurrencies(currentCurrencies); }, Qt::QueuedConnection);
    workerThread->start();

    // Load all persistent settings
    loadSettingsAndApply();

    // Initialize speedometer style from settings (global default)
    {
        QSettings st("alel12", "modular_dashboard"); QString sName = st.value("ui/speedometerStyle", "Classic").toString();
        // set initial check state for the 4 supported styles
        stClassic->setChecked(sName=="Classic");
        stMinimal->setChecked(sName=="Minimal");
        stNeon->setChecked(sName=="NeonGlow");
        stModern->setChecked(sName=="ModernTicks");
        applyStyleByName(sName);
        // Apply per-widget overrides if present
        for (auto it = widgets.begin(); it != widgets.end(); ++it) {
            const QString& cur = it.key(); auto* w = it.value();
            QString per = st.value(QString("ui/stylePerWidget/%1").arg(cur)).toString(); if (per.isEmpty()) continue;
            DynamicSpeedometerCharts::SpeedometerStyle s = DynamicSpeedometerCharts::SpeedometerStyle::Classic;
            if (per=="NeonGlow") s = DynamicSpeedometerCharts::SpeedometerStyle::NeonGlow;
            else if (per=="Minimal") s = DynamicSpeedometerCharts::SpeedometerStyle::Minimal;
            else if (per=="ModernTicks") s = DynamicSpeedometerCharts::SpeedometerStyle::ModernTicks;
            else if (per=="Circle" || per=="Classic Pro") s = DynamicSpeedometerCharts::SpeedometerStyle::Circle;
            else if (per=="Gauge") s = DynamicSpeedometerCharts::SpeedometerStyle::Gauge;
            else if (per=="Ring" || per=="Modern Scale") s = DynamicSpeedometerCharts::SpeedometerStyle::Ring;
            else s = DynamicSpeedometerCharts::SpeedometerStyle::Classic;
            w->setSpeedometerStyle(s);
        }
    }

    // Apply saved performance settings and default theme
    loadSettingsAndApply();
    applyTheme("Dark"); // This will now apply theme colors to speedometers
}

#include "MainWindow.moc"

MainWindow::~MainWindow() {
    if (dataWorker) {
        // Ensure the worker stops in its own thread before quitting
        QMetaObject::invokeMethod(dataWorker, "stop", Qt::BlockingQueuedConnection);
    }
    workerThread->quit();
    workerThread->wait();
}

void MainWindow::switchMode(StreamMode m) {
    streamMode = m;
    QMetaObject::invokeMethod(dataWorker, [this,m](){ dataWorker->stop(); dataWorker->setMode(m); dataWorker->start(); }, Qt::QueuedConnection);
    setWindowTitle(QString("Modular Crypto Dashboard — %1").arg(m==StreamMode::Trade?"TRADE":"TICKER"));
    // Persist choice
    QSettings st("alel12", "modular_dashboard"); st.setValue("stream/mode", m==StreamMode::Ticker?"TICKER":"TRADE"); st.sync();
}

void MainWindow::openPerformanceDialog() {
    auto s = readPerfSettings();
    // Read python-like params from settings
    QSettings st("alel12", "modular_dashboard");
    double pyInitSpan = st.value("perf/pyInitSpanPct", 0.005).toDouble();
    double pyMinComp  = st.value("perf/pyMinCompress", 1.000001).toDouble();
    double pyMaxComp  = st.value("perf/pyMaxCompress", 0.999999).toDouble();
    double pyMinWidth = st.value("perf/pyMinWidthPct", 0.0001).toDouble();
    PerformanceConfigDialog dlg(this, s.animMs, s.renderMs, s.cacheMs, s.volWindow, s.maxPts, s.rawCache,
                                pyInitSpan, pyMinComp, pyMaxComp, pyMinWidth);
    if (dlg.exec()==QDialog::Accepted) {
        PerfSettings ns{dlg.animationMs(), dlg.renderIntervalMs(), dlg.cacheIntervalMs(), dlg.volatilityWindowSize(), dlg.maxPointsCount(), dlg.rawCacheSize()};
        writePerfSettings(ns);
        // Save python-like params
        st.setValue("perf/pyInitSpanPct", dlg.pyInitSpanPctVal());
        st.setValue("perf/pyMinCompress", dlg.pyMinCompressVal());
        st.setValue("perf/pyMaxCompress", dlg.pyMaxCompressVal());
        st.setValue("perf/pyMinWidthPct", dlg.pyMinWidthPctVal());
        st.sync();
        for (auto* w : widgets) w->applyPerformance(ns.animMs, ns.renderMs, ns.cacheMs, ns.volWindow, ns.maxPts, ns.rawCache);
        // Apply python-like params live
        for (auto* w : widgets) w->setPythonScalingParams(dlg.pyInitSpanPctVal(), dlg.pyMinCompressVal(), dlg.pyMaxCompressVal(), dlg.pyMinWidthPctVal());
    }
}

void MainWindow::openThemeDialog() {
    bool ok=false; QStringList names = themeManager->themeNames(); QString current = themeManager->current().name;
    QString chosen = QInputDialog::getItem(this, "Choose Theme", "Theme:", names, names.indexOf(current), false, &ok);
    if (!ok) return; applyTheme(chosen);
}

void MainWindow::handleData(const QString& currency, double price, double timestamp) {
    if (widgets.contains(currency)) { if (currency=="BTC") btcPrice=price; QMetaObject::invokeMethod(this, [this,currency,price,timestamp](){ widgets[currency]->updateData(price, timestamp, btcPrice); }, Qt::QueuedConnection); }
}

void MainWindow::onRequestRename(const QString& currentTicker) {
    bool ok=false; QString newTicker = QInputDialog::getText(this, "Rename ticker", "Enter new ticker (e.g., BTC, ETH):", QLineEdit::Normal, currentTicker, &ok);
    if (!ok) return; newTicker = newTicker.trimmed().toUpper(); if (newTicker.isEmpty() || newTicker==currentTicker) return;
    if (!widgets.contains(currentTicker)) return;
    // Update mapping and preserve visual order in currentCurrencies
    int idxOld = currentCurrencies.indexOf(currentTicker);
    if (idxOld < 0) return;

    if (widgets.contains(newTicker)) {
        // Swap positions in list if target ticker exists
        int idxNew = currentCurrencies.indexOf(newTicker);
        if (idxNew >= 0) currentCurrencies.swapItemsAt(idxOld, idxNew);
        // Swap widgets' tickers
        auto* w = widgets[currentTicker];
        auto* other = widgets[newTicker];
        widgets[newTicker] = w; w->setCurrencyName(newTicker);
        widgets[currentTicker] = other; other->setCurrencyName(currentTicker);
    } else {
        // Replace ticker at the same position
        auto* w = widgets[currentTicker]; widgets.remove(currentTicker);
        widgets[newTicker] = w; w->setCurrencyName(newTicker);
        currentCurrencies[idxOld] = newTicker;
    }
    // Migrate per-widget style setting key if present
    {
        QSettings st("alel12", "modular_dashboard");
        const QString oldKey = QString("ui/stylePerWidget/%1").arg(currentTicker);
        const QString newKey = QString("ui/stylePerWidget/%1").arg(newTicker);
        QString val = st.value(oldKey).toString();
        if (!val.isEmpty() && st.value(newKey).toString().isEmpty()) {
            st.setValue(newKey, val);
            st.remove(oldKey);
            st.sync();
        }
    }
    saveCurrenciesSettings(currentCurrencies);
    // Clear unsupported banner on both involved widgets (fresh start after rename)
    widgets[newTicker]->setUnsupportedReason("");
    widgets[currentTicker]->setUnsupportedReason("");
    QMetaObject::invokeMethod(dataWorker, [this](){ dataWorker->setCurrencies(currentCurrencies); }, Qt::QueuedConnection);
}

MainWindow::PerfSettings MainWindow::readPerfSettings() {
    QSettings st("alel12", "modular_dashboard"); PerfSettings s;
    s.animMs = st.value("perf/animMs", 400).toInt(); s.renderMs = st.value("perf/renderMs", 16).toInt(); s.cacheMs = st.value("perf/cacheMs", 300).toInt();
    s.volWindow = st.value("perf/volWindow", 800).toInt(); s.maxPts = st.value("perf/maxPts", 800).toInt(); s.rawCache = st.value("perf/rawCache", 20000).toInt(); return s;
}

void MainWindow::writePerfSettings(const PerfSettings& s) {
    QSettings st("alel12", "modular_dashboard");
    st.setValue("perf/animMs", s.animMs); st.setValue("perf/renderMs", s.renderMs); st.setValue("perf/cacheMs", s.cacheMs);
    st.setValue("perf/volWindow", s.volWindow); st.setValue("perf/maxPts", s.maxPts); st.setValue("perf/rawCache", s.rawCache); st.sync();
}

void MainWindow::loadSettingsAndApply() {
    auto s = readPerfSettings(); for (auto* w : widgets) w->applyPerformance(s.animMs, s.renderMs, s.cacheMs, s.volWindow, s.maxPts, s.rawCache);
    
    // Initialize theme settings
    QSettings st("alel12", "modular_dashboard");
    // Prefer stored theme name if present
    QString themeName = st.value("ui/themeName").toString();
    if (!themeName.isEmpty()) {
        applyTheme(themeName);
    } else if (st.contains("ui/theme")) {
        auto theme = static_cast<ThemeManager::ColorTheme>(st.value("ui/theme").toInt());
        themeManager->setTheme(theme);
        onThemeChanged(theme);
    }
    
    // Apply saved thresholds
    bool thrEnabled = st.value("ui/thresholds/enabled", false).toBool();
    double warnVal = st.value("ui/thresholds/warn", 70.0).toDouble();
    double dangerVal = st.value("ui/thresholds/danger", 85.0).toDouble();
    applyThresholdsToAll(thrEnabled, warnVal, dangerVal);
    // Apply saved python-like params
    double pyInitSpan = st.value("perf/pyInitSpanPct", 0.005).toDouble();
    double pyMinComp  = st.value("perf/pyMinCompress", 1.000001).toDouble();
    double pyMaxComp  = st.value("perf/pyMaxCompress", 0.999999).toDouble();
    double pyMinWidth = st.value("perf/pyMinWidthPct", 0.0001).toDouble();
    for (auto* w : widgets) w->setPythonScalingParams(pyInitSpan, pyMinComp, pyMaxComp, pyMinWidth);
    
    // Apply saved scaling mode to widgets handled in constructor when actions exist
}

QStringList MainWindow::readCurrenciesSettings() {
    QSettings st("alel12", "modular_dashboard"); QStringList list = st.value("currencies/list").toStringList(); if (list.isEmpty()) list = DEFAULT_CURRENCIES; return list;
}

void MainWindow::saveCurrenciesSettings(const QStringList& list) { QSettings st("alel12", "modular_dashboard"); st.setValue("currencies/list", list); st.sync(); }

void MainWindow::applyTheme(const QString& name) {
    themeManager->setCurrent(name); const auto& t = themeManager->current();
    if (!t.appStyleSheet.isEmpty()) {
        if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
            app->setStyleSheet(t.appStyleSheet);
        }
    }
    // Apply theme colors to all speedometer widgets
    for (auto* widget : widgets) {
        widget->applyThemeColors(t.speedometer);
    }
}

void MainWindow::onThemeChanged(const ThemeManager::ColorTheme& theme) {
    // Apply theme colors to all widgets
    for (auto* w : widgets) {
        auto colors = themeManager->getSpeedometerColors(theme);
        w->setSpeedometerColors(colors.zoneGood, colors.arcBase, colors.text, colors.background);
    }
}

void MainWindow::applyThresholdsToAll(bool enabled, double warnValue, double dangerValue) {
    for (auto* w : widgets) {
        w->setThresholds(enabled, warnValue, dangerValue);
    }
}

