#include "MainWindow.h"
#include <QGridLayout>
#include <QThread>
#include <QMenuBar>
#include <QActionGroup>
#include <QDialog>
#include <QFormLayout>
#include <QSpinBox>
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
                            int animMsInit,int renderMsInit,int cacheMsInit,int volWindowInit,int maxPtsInit,int rawCacheInit)
                            : QDialog(parent) {
        setWindowTitle("Performance Settings");
        auto* layout = new QFormLayout(this);
        animMs = new QSpinBox(); animMs->setRange(50,5000); animMs->setValue(animMsInit);
        renderMs = new QSpinBox(); renderMs->setRange(8,1000); renderMs->setValue(renderMsInit);
        cacheMs = new QSpinBox(); cacheMs->setRange(50,5000); cacheMs->setValue(cacheMsInit);
        volWindow = new QSpinBox(); volWindow->setRange(10,20000); volWindow->setValue(volWindowInit);
        maxPts = new QSpinBox(); maxPts->setRange(100,20000); maxPts->setValue(maxPtsInit);
        rawCache = new QSpinBox(); rawCache->setRange(1000,500000); rawCache->setValue(rawCacheInit);
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

    auto* settingsMenu = menuBar()->addMenu("Settings");
    auto* perfAct = settingsMenu->addAction("Performance..."); connect(perfAct, &QAction::triggered, this, &MainWindow::openPerformanceDialog);
    auto* themeAct = settingsMenu->addAction("Theme..."); connect(themeAct, &QAction::triggered, this, &MainWindow::openThemeDialog);
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
    auto applyScalingMode = [this](DynamicSpeedometerCharts::ScalingMode mode, double minVal = 0.0, double maxVal = 100.0){
        DynamicSpeedometerCharts::ScalingSettings s; s.mode = mode; s.fixedMin = minVal; s.fixedMax = maxVal;
        for (auto* w : widgets) w->applyScaling(s);
        QSettings st("alel12", "modular_dashboard");
        st.setValue("ui/scaling/mode", int(mode)); st.setValue("ui/scaling/min", minVal); st.setValue("ui/scaling/max", maxVal); st.sync();
    };
    connect(adaptiveScaling, &QAction::triggered, this, [applyScalingMode](){ applyScalingMode(DynamicSpeedometerCharts::ScalingMode::Adaptive); });
    connect(fixedScaling, &QAction::triggered, this, [applyScalingMode](){ applyScalingMode(DynamicSpeedometerCharts::ScalingMode::Fixed, 0.0, 100.0); });
    connect(manualScaling, &QAction::triggered, this, [applyScalingMode](){ applyScalingMode(DynamicSpeedometerCharts::ScalingMode::Manual); });

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
    dataWorker = new DataWorker(); dataWorker->setMode(streamMode); workerThread = new QThread(this); dataWorker->moveToThread(workerThread);
    connect(workerThread, &QThread::started, dataWorker, &DataWorker::start);
    connect(dataWorker, &DataWorker::dataUpdated, this, &MainWindow::handleData);
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

MainWindow::~MainWindow() { dataWorker->stop(); workerThread->quit(); workerThread->wait(); }

void MainWindow::switchMode(StreamMode m) {
    streamMode = m; QMetaObject::invokeMethod(dataWorker, [this,m](){ dataWorker->stop(); dataWorker->setMode(m); dataWorker->start(); }, Qt::QueuedConnection);
    setWindowTitle(QString("Modular Crypto Dashboard — %1").arg(m==StreamMode::Trade?"TRADE":"TICKER"));
    // Persist choice
    QSettings st("alel12", "modular_dashboard"); st.setValue("stream/mode", m==StreamMode::Ticker?"TICKER":"TRADE"); st.sync();
}

void MainWindow::openPerformanceDialog() {
    auto s = readPerfSettings();
    PerformanceConfigDialog dlg(this, s.animMs, s.renderMs, s.cacheMs, s.volWindow, s.maxPts, s.rawCache);
    if (dlg.exec()==QDialog::Accepted) {
        PerfSettings ns{dlg.animationMs(), dlg.renderIntervalMs(), dlg.cacheIntervalMs(), dlg.volatilityWindowSize(), dlg.maxPointsCount(), dlg.rawCacheSize()};
        writePerfSettings(ns);
        for (auto* w : widgets) w->applyPerformance(ns.animMs, ns.renderMs, ns.cacheMs, ns.volWindow, ns.maxPts, ns.rawCache);
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
    if (st.contains("ui/theme")) {
        auto theme = static_cast<ThemeManager::ColorTheme>(st.value("ui/theme").toInt());
        themeManager->setTheme(theme);
        onThemeChanged(theme);
    }
    
    // Apply saved thresholds
    bool thrEnabled = st.value("ui/thresholds/enabled", false).toBool();
    double warnVal = st.value("ui/thresholds/warn", 70.0).toDouble();
    double dangerVal = st.value("ui/thresholds/danger", 85.0).toDouble();
    applyThresholdsToAll(thrEnabled, warnVal, dangerVal);
    
    // Apply saved scaling mode
    if (st.contains("ui/scaling/mode")) {
        DynamicSpeedometerCharts::ScalingMode mode = static_cast<DynamicSpeedometerCharts::ScalingMode>(st.value("ui/scaling/mode").toInt());
        double minVal = st.value("ui/scaling/min", 0.0).toDouble();
        double maxVal = st.value("ui/scaling/max", 100.0).toDouble();
        DynamicSpeedometerCharts::ScalingSettings s; s.mode = mode; s.fixedMin = minVal; s.fixedMax = maxVal;
        for (auto* w : widgets) w->applyScaling(s);
    }
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

