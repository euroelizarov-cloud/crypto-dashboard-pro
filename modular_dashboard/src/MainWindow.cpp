#include "MainWindow.h"
#include "MarketOverviewWindow.h"
#include "MultiCompareWindow.h"
#include "HistoryStorage.h"
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
#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <algorithm>
#include <QDateTime>
#include <numeric>

static const QStringList DEFAULT_CURRENCIES = {"BTC", "XRP", "BNB", "SOL", "DOGE", "XLM", "HBAR", "ETH", "ONDO", "AAVE", "ZRO", "STRK"};
// Top 50 list used to auto-fill grid when more slots requested
static const QStringList TOP50 = {
    "BTC","ETH","BNB","SOL","XRP","ADA","DOGE","TON","TRX","AVAX",
    "SHIB","DOT","LINK","BCH","LTC","MATIC","NEAR","UNI","ETC","XLM",
    "ATOM","HBAR","APT","ARB","OP","IMX","INJ","FIL","AAVE","SUI",
    "LDO","RNDR","ICP","ALGO","VET","XTZ","FLOW","THETA","EGLD","MANA",
    "SAND","AXS","GRT","FTM","KAS","TAO","LAYER","APTOS","PEPE","SEI"
};

class PerformanceConfigDialog : public QDialog {
    Q_OBJECT
public:
    PerformanceConfigDialog(QWidget* parent,
                            int animMsInit,int renderMsInit,int cacheMsInit,int volWindowInit,int maxPtsInit,int rawCacheInit,
                            double pyInitSpanPctInit, double pyMinCompressInit, double pyMaxCompressInit, double pyMinWidthPctInit,
                            int scalingWindowSizeInit, double scalingPaddingPctInit)
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
        // Scaling controls
        scalingWindowSize = new QSpinBox(); scalingWindowSize->setRange(0, 20000); scalingWindowSize->setValue(scalingWindowSizeInit);
        scalingPaddingPct = new QDoubleSpinBox(); scalingPaddingPct->setRange(0.0, 0.5); scalingPaddingPct->setDecimals(4); scalingPaddingPct->setSingleStep(0.001); scalingPaddingPct->setValue(scalingPaddingPctInit);
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
        layout->addRow("Scaling window size (0=full)", scalingWindowSize);
        layout->addRow("Scaling padding (±pct)", scalingPaddingPct);
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
    int scalingWindowSizeVal() const { return scalingWindowSize->value(); }
    double scalingPaddingPctVal() const { return scalingPaddingPct->value(); }
private:
    QSpinBox *animMs, *renderMs, *cacheMs, *volWindow, *maxPts, *rawCache, *scalingWindowSize;
    QDoubleSpinBox *pyInitSpanPct, *pyMinCompress, *pyMaxCompress, *pyMinWidthPct, *scalingPaddingPct;
};

MainWindow::MainWindow() {
    themeManager = new ThemeManager(this);
    QWidget* central = new QWidget(this); setCentralWidget(central); gridLayout = new QGridLayout(central); gridLayout->setSpacing(10);
    // Load saved currencies; fall back to defaults only if nothing saved
    currentCurrencies = readCurrenciesSettings();
    if (currentCurrencies.isEmpty()) currentCurrencies = DEFAULT_CURRENCIES;
    int row=0, col=0; for (const QString& c : currentCurrencies) {
    auto* w = new DynamicSpeedometerCharts(c); widgets[c]=w; gridLayout->addWidget(w,row,col);
        connect(w, &DynamicSpeedometerCharts::requestRename, this, &MainWindow::onRequestRename);
        connect(w, &DynamicSpeedometerCharts::requestChangeTicker, this, [this,c](const QString& oldName, const QString& newName){
            Q_UNUSED(oldName);
            QMetaObject::invokeMethod(this, [this,c,newName](){
                QString currentTicker = c;
                QString newTicker = newName.trimmed().toUpper();
                if (newTicker.isEmpty() || newTicker==currentTicker) return;
                if (!widgets.contains(currentTicker)) return;
                int idxOld = currentCurrencies.indexOf(currentTicker);
                if (idxOld < 0) return;
                if (widgets.contains(newTicker)) {
                    int idxNew = currentCurrencies.indexOf(newTicker);
                    if (idxNew >= 0) currentCurrencies.swapItemsAt(idxOld, idxNew);
                    auto* w = widgets[currentTicker]; auto* other = widgets[newTicker];
                    widgets[newTicker] = w; w->setCurrencyName(newTicker);
                    widgets[currentTicker] = other; other->setCurrencyName(currentTicker);
                } else {
                    auto* w = widgets[currentTicker]; widgets.remove(currentTicker);
                    widgets[newTicker] = w; w->setCurrencyName(newTicker);
                    currentCurrencies[idxOld] = newTicker;
                }
                {
                    QSettings st("alel12", "modular_dashboard");
                    const QString oldKey = QString("ui/stylePerWidget/%1").arg(currentTicker);
                    const QString newKey = QString("ui/stylePerWidget/%1").arg(newTicker);
                    QString val = st.value(oldKey).toString(); if (!val.isEmpty() && st.value(newKey).toString().isEmpty()) { st.setValue(newKey, val); st.remove(oldKey); st.sync(); }
                }
                saveCurrenciesSettings(currentCurrencies);
                widgets[newTicker]->setUnsupportedReason(""); widgets[currentTicker]->setUnsupportedReason("");
                QMetaObject::invokeMethod(dataWorker, [this](){ dataWorker->setCurrencies(realSymbolsFrom(currentCurrencies)); }, Qt::QueuedConnection);
                refreshCompareSubscriptions();
                reflowGrid();
            }, Qt::QueuedConnection);
        });
    connectRealWidgetSignals(c, w);
        // Persist per-widget style when chosen from widget context menu
        connect(w, &DynamicSpeedometerCharts::styleSelected, this, [this](const QString& cur, const QString& styleName){
            QSettings st("alel12", "modular_dashboard");
            st.setValue(QString("ui/stylePerWidget/%1").arg(cur), styleName);
            st.sync();
        });
        if (++col>=gridCols) { col=0; ++row; }
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
    // Grid submenu (1..7 x 1..7)
    QMenu* gridMenu = settingsMenu->addMenu("Grid");
    QMenu* presetsMenu = gridMenu->addMenu("Presets (NxM)");
    QMenu* colsMenu = gridMenu->addMenu("Columns");
    QMenu* rowsMenu = gridMenu->addMenu("Rows");
    QActionGroup* colsGroup = new QActionGroup(this); colsGroup->setExclusive(true);
    QActionGroup* rowsGroup = new QActionGroup(this); rowsGroup->setExclusive(true);
    auto makeNumAction = [&](QMenu* m, QActionGroup* g, int n){ QAction* a = m->addAction(QString::number(n)); a->setCheckable(true); g->addAction(a); return a; };
    QList<QAction*> colActs; QList<QAction*> rowActs;
    for (int n=1;n<=7;++n) { colActs << makeNumAction(colsMenu, colsGroup, n); rowActs << makeNumAction(rowsMenu, rowsGroup, n); }
    // Presets 1x1 .. 7x7
    for (int c=1;c<=7;++c) {
        for (int r=1;r<=7;++r) {
            QAction* a = presetsMenu->addAction(QString::number(c) + "×" + QString::number(r));
            connect(a, &QAction::triggered, this, [this,c,r](){
                QSettings st("alel12", "modular_dashboard");
                gridCols = c; gridRows = r;
                st.setValue("ui/grid/cols", gridCols); st.setValue("ui/grid/rows", gridRows); st.sync();
                reflowGrid();
            });
        }
    }
    // Load saved grid size
    {
        QSettings st("alel12", "modular_dashboard");
        gridCols = std::clamp(st.value("ui/grid/cols", 4).toInt(), 1, 7);
        gridRows = std::clamp(st.value("ui/grid/rows", 3).toInt(), 1, 7);
        colActs[gridCols-1]->setChecked(true);
        rowActs[gridRows-1]->setChecked(true);
    }
    auto applyGrid = [this](int cols, int rows){
        gridCols = std::clamp(cols, 1, 7);
        gridRows = std::clamp(rows, 1, 7);
        QSettings st("alel12", "modular_dashboard"); st.setValue("ui/grid/cols", gridCols); st.setValue("ui/grid/rows", gridRows); st.sync();
        reflowGrid();
    };
    for (int i=0;i<colActs.size();++i) connect(colActs[i], &QAction::triggered, this, [this,applyGrid,i](){ applyGrid(i+1, gridRows); });
    for (int i=0;i<rowActs.size();++i) connect(rowActs[i], &QAction::triggered, this, [this,applyGrid,i](){ applyGrid(gridCols, i+1); });
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
    QAction* stModern  = addStyle("KiloCode Modern Ticks");
    QAction* stCircle  = addStyle("Classic Pro");
    QAction* stGauge   = addStyle("Gauge");
    QAction* stRing    = addStyle("Modern Scale");
    auto applyStyleByName = [&](const QString& sName){
        DynamicSpeedometerCharts::SpeedometerStyle s = DynamicSpeedometerCharts::SpeedometerStyle::Classic;
        if (sName=="NeonGlow") s = DynamicSpeedometerCharts::SpeedometerStyle::NeonGlow;
        else if (sName=="Minimal") s = DynamicSpeedometerCharts::SpeedometerStyle::Minimal;
    else if (sName=="ModernTicks" || sName=="KiloCode Modern Ticks") s = DynamicSpeedometerCharts::SpeedometerStyle::ModernTicks;
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
    QAction* oldSchoolAdaptiveScaling = scalingMenu->addAction("Old School Adaptive"); oldSchoolAdaptiveScaling->setCheckable(true); scalingGroup->addAction(oldSchoolAdaptiveScaling);
    QAction* oldSchoolPythonScaling = scalingMenu->addAction("Old School Python-like"); oldSchoolPythonScaling->setCheckable(true); scalingGroup->addAction(oldSchoolPythonScaling);
    QAction* kiloCoderScaling = scalingMenu->addAction("KiloCoder Like"); kiloCoderScaling->setCheckable(true); scalingGroup->addAction(kiloCoderScaling);
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
    connect(oldSchoolAdaptiveScaling, &QAction::triggered, this, [applyScalingMode](){ applyScalingMode(DynamicSpeedometerCharts::ScalingMode::OldSchoolAdaptive); });
    connect(oldSchoolPythonScaling, &QAction::triggered, this, [applyScalingMode](){ applyScalingMode(DynamicSpeedometerCharts::ScalingMode::OldSchoolPythonLike); });
    connect(kiloCoderScaling, &QAction::triggered, this, [applyScalingMode](){ applyScalingMode(DynamicSpeedometerCharts::ScalingMode::KiloCoderLike); });
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
            case DynamicSpeedometerCharts::ScalingMode::OldSchoolAdaptive: oldSchoolAdaptiveScaling->setChecked(true); break;
            case DynamicSpeedometerCharts::ScalingMode::OldSchoolPythonLike: oldSchoolPythonScaling->setChecked(true); break;
            case DynamicSpeedometerCharts::ScalingMode::KiloCoderLike: kiloCoderScaling->setChecked(true); break;
            default: break; // Suppress compiler warning for unhandled enum values
        }
        applyScalingMode(mode, minVal, maxVal);
    }

    // Global per-widget controls
    QMenu* widgetsMenu = menuBar()->addMenu("Widgets");
    // Volume visualization
    QMenu* volGlobal = widgetsMenu->addMenu("Volume");
    QActionGroup* volGGroup = new QActionGroup(this); volGGroup->setExclusive(true);
    auto addVolG = [&](const QString& label, DynamicSpeedometerCharts::VolumeVis vv){ QAction* a = volGlobal->addAction(label); a->setCheckable(true); a->setActionGroup(volGGroup); a->setData(int(vv)); return a; };
    QAction* gVolOff = addVolG("Off", DynamicSpeedometerCharts::VolumeVis::Off);
    QAction* gVolBar = addVolG("Bar under arc", DynamicSpeedometerCharts::VolumeVis::Bar);
    QAction* gVolNeedle = addVolG("Second needle", DynamicSpeedometerCharts::VolumeVis::Needle);
    QAction* gVolSidebar = addVolG("Right sidebar bar", DynamicSpeedometerCharts::VolumeVis::Sidebar);
    QAction* gVolFuel = addVolG("Fuel gauge needle", DynamicSpeedometerCharts::VolumeVis::Fuel);
    // Sidebar options
    QMenu* gSidebar = volGlobal->addMenu("Sidebar options");
    QActionGroup* gSbWidth = new QActionGroup(this); gSbWidth->setExclusive(true);
    auto addGSBw = [&](const QString& label, int mode){ QAction* a = gSidebar->addAction(label); a->setCheckable(true); a->setActionGroup(gSbWidth); a->setData(mode); return a; };
    QAction* gSbAuto = addGSBw("Width: Auto", 0);
    QAction* gSbNar  = addGSBw("Width: Narrow", 1);
    QAction* gSbMed  = addGSBw("Width: Medium", 2);
    QAction* gSbWide = addGSBw("Width: Wide", 3);
    QAction* gSbOutline = gSidebar->addAction("Outline"); gSbOutline->setCheckable(true);
    QMenu* gSbBright = gSidebar->addMenu("Brightness");
    QActionGroup* gSbBrightGrp = new QActionGroup(this); gSbBrightGrp->setExclusive(true);
    auto addGSBb = [&](const QString& label, int pct){ QAction* a = gSbBright->addAction(label); a->setCheckable(true); a->setActionGroup(gSbBrightGrp); a->setData(pct); return a; };
    QAction* gSbB75  = addGSBb("Dim (75%)", 75);
    QAction* gSbB100 = addGSBb("Normal (100%)", 100);
    QAction* gSbB125 = addGSBb("Bright (125%)", 125);
    QAction* gSbB150 = addGSBb("Ultra (150%)", 150);
    // Widget frame
    QMenu* gFrame = widgetsMenu->addMenu("Widget Frame");
    QActionGroup* gFrameGrp = new QActionGroup(this); gFrameGrp->setExclusive(true);
    auto addGFrame = [&](const QString& label, const QString& key){ QAction* a = gFrame->addAction(label); a->setCheckable(true); a->setActionGroup(gFrameGrp); a->setData(key); return a; };
    QAction* gFrNone = addGFrame("None", "none");
    QAction* gFrMin  = addGFrame("Minimal", "minimal");
    QAction* gFrDash = addGFrame("Dashed", "dashed");
    QAction* gFrGlow = addGFrame("Glow", "glow");
    // Indicators
    QMenu* gInd = widgetsMenu->addMenu("Indicators");
    QAction* gIndRSI = gInd->addAction("RSI (14)"); gIndRSI->setCheckable(true);
    QAction* gIndMACD = gInd->addAction("MACD (12,26,9)"); gIndMACD->setCheckable(true);
    QAction* gIndBB = gInd->addAction("Bollinger Bands (20,2)"); gIndBB->setCheckable(true);
    // Anomalies
    QMenu* gAn = widgetsMenu->addMenu(QString::fromUtf8("Аномалии"));
    QAction* gAnEnable = gAn->addAction(QString::fromUtf8("Показывать значок аномалии")); gAnEnable->setCheckable(true);
    QActionGroup* gAnGrp = new QActionGroup(this); gAnGrp->setExclusive(true);
    auto addGAn = [&](const QString& label, const QString& key){ QAction* a = gAn->addAction(label); a->setCheckable(true); a->setActionGroup(gAnGrp); a->setData(key); return a; };
    QAction* gAnOff = addGAn(QString::fromUtf8("Выкл"), "off");
    QAction* gAnRsi = addGAn("RSI OB/OS", "rsi");
    QAction* gAnMacd = addGAn("MACD cross", "macd");
    QAction* gAnBB = addGAn("BB breakout", "bb");
    QAction* gAnZS = addGAn("Z-Score", "zscore");
    QAction* gAnVol = addGAn(QString::fromUtf8("Всплеск волатильности"), "vol");
    QAction* gAnComp = addGAn(QString::fromUtf8("Композитный"), "comp");
    QAction* gAnRsiDiv = addGAn(QString::fromUtf8("RSI дивергенция"), "rsi_div");
    QAction* gAnMacdHist = addGAn(QString::fromUtf8("MACD histogram surge"), "macd_hist");
    QAction* gAnClustZ = addGAn(QString::fromUtf8("Кластерный Z-Score"), "clustered_z");
    QAction* gAnVolReg = addGAn(QString::fromUtf8("Calm↔Volatile смена режима"), "vol_regime");
    // Overlays
    QAction* gOvVol = widgetsMenu->addAction("Show volatility overlay"); gOvVol->setCheckable(true);
    QAction* gOvChg = widgetsMenu->addAction("Show change overlay"); gOvChg->setCheckable(true);

    // Initialize checks from settings (best-effort, use first widget for some)
    {
        QSettings st("alel12","modular_dashboard");
        // Global defaults for menu checks
        QString volKey = st.value("ui/volume/globalVis","off").toString();
        auto checkVol = [&](const QString& k){ gVolOff->setChecked(k=="off"); gVolBar->setChecked(k=="bar"); gVolNeedle->setChecked(k=="needle"); gVolSidebar->setChecked(k=="sidebar"); gVolFuel->setChecked(k=="fuel"); };
        checkVol(volKey);
        int sbW = st.value("ui/volume/globalSidebarWidth", 0).toInt();
        if (sbW==0) gSbAuto->setChecked(true); else if (sbW==1) gSbNar->setChecked(true); else if (sbW==2) gSbMed->setChecked(true); else gSbWide->setChecked(true);
        bool sbOut = st.value("ui/volume/globalSidebarOutline", true).toBool(); gSbOutline->setChecked(sbOut);
        int sbB = st.value("ui/volume/globalSidebarBrightness", 100).toInt();
        if (sbB<=75) gSbB75->setChecked(true); else if (sbB<=100) gSbB100->setChecked(true); else if (sbB<=125) gSbB125->setChecked(true); else gSbB150->setChecked(true);
        QString fr = st.value("ui/frame/globalStyle","none").toString();
        gFrNone->setChecked(fr=="none"); gFrMin->setChecked(fr=="minimal"); gFrDash->setChecked(fr=="dashed"); gFrGlow->setChecked(fr=="glow");
        bool rsi = st.value("ui/indicators/globalRSI", false).toBool(); gIndRSI->setChecked(rsi);
        bool macd = st.value("ui/indicators/globalMACD", false).toBool(); gIndMACD->setChecked(macd);
        bool bb = st.value("ui/indicators/globalBB", false).toBool(); gIndBB->setChecked(bb);
        bool anEn = st.value("ui/anomaly/globalEnabled", false).toBool(); gAnEnable->setChecked(anEn);
        QString anKey = st.value("ui/anomaly/globalMode","off").toString();
        for (auto* a : gAnGrp->actions()) a->setChecked(a->data().toString()==anKey);
        bool ovVol = st.value("ui/overlays/globalVol", false).toBool(); gOvVol->setChecked(ovVol);
        bool ovChg = st.value("ui/overlays/globalChg", false).toBool(); gOvChg->setChecked(ovChg);
    }

    // Wiring: apply selections to all widgets and persist simple globals
    auto applyVolVisAll = [this](DynamicSpeedometerCharts::VolumeVis vv, const QString& key){
        QSettings st("alel12","modular_dashboard"); st.setValue("ui/volume/globalVis", key); st.sync();
        for (auto* w : widgets) { w->setVolumeVis(vv); }
    };
    connect(gVolOff, &QAction::triggered, this, [applyVolVisAll](){ applyVolVisAll(DynamicSpeedometerCharts::VolumeVis::Off, "off"); });
    connect(gVolBar, &QAction::triggered, this, [applyVolVisAll](){ applyVolVisAll(DynamicSpeedometerCharts::VolumeVis::Bar, "bar"); });
    connect(gVolNeedle, &QAction::triggered, this, [applyVolVisAll](){ applyVolVisAll(DynamicSpeedometerCharts::VolumeVis::Needle, "needle"); });
    connect(gVolSidebar, &QAction::triggered, this, [applyVolVisAll](){ applyVolVisAll(DynamicSpeedometerCharts::VolumeVis::Sidebar, "sidebar"); });
    connect(gVolFuel, &QAction::triggered, this, [applyVolVisAll](){ applyVolVisAll(DynamicSpeedometerCharts::VolumeVis::Fuel, "fuel"); });

    auto applySbWidthAll = [this](int mode){ QSettings st("alel12","modular_dashboard"); st.setValue("ui/volume/globalSidebarWidth", mode); st.sync(); for (auto* w : widgets) w->setSidebarWidthMode(mode); };
    connect(gSbAuto, &QAction::triggered, this, [applySbWidthAll](){ applySbWidthAll(0); });
    connect(gSbNar,  &QAction::triggered, this, [applySbWidthAll](){ applySbWidthAll(1); });
    connect(gSbMed,  &QAction::triggered, this, [applySbWidthAll](){ applySbWidthAll(2); });
    connect(gSbWide, &QAction::triggered, this, [applySbWidthAll](){ applySbWidthAll(3); });
    connect(gSbOutline, &QAction::toggled, this, [this](bool on){ QSettings st("alel12","modular_dashboard"); st.setValue("ui/volume/globalSidebarOutline", on); st.sync(); for (auto* w : widgets) w->setSidebarOutline(on); });
    auto applySbBrightAll = [this](int pct){ QSettings st("alel12","modular_dashboard"); st.setValue("ui/volume/globalSidebarBrightness", pct); st.sync(); for (auto* w : widgets) w->setSidebarBrightnessPct(pct); };
    connect(gSbB75,  &QAction::triggered, this, [applySbBrightAll](){ applySbBrightAll(75); });
    connect(gSbB100, &QAction::triggered, this, [applySbBrightAll](){ applySbBrightAll(100); });
    connect(gSbB125, &QAction::triggered, this, [applySbBrightAll](){ applySbBrightAll(125); });
    connect(gSbB150, &QAction::triggered, this, [applySbBrightAll](){ applySbBrightAll(150); });

    auto applyFrameAll = [this](const QString& key){ QSettings st("alel12","modular_dashboard"); st.setValue("ui/frame/globalStyle", key); st.sync(); for (auto* w : widgets) w->setFrameStyleByName(key); };
    connect(gFrNone, &QAction::triggered, this, [applyFrameAll](){ applyFrameAll("none"); });
    connect(gFrMin,  &QAction::triggered, this, [applyFrameAll](){ applyFrameAll("minimal"); });
    connect(gFrDash, &QAction::triggered, this, [applyFrameAll](){ applyFrameAll("dashed"); });
    connect(gFrGlow, &QAction::triggered, this, [applyFrameAll](){ applyFrameAll("glow"); });

    connect(gIndRSI, &QAction::toggled, this, [this](bool on){ QSettings st("alel12","modular_dashboard"); st.setValue("ui/indicators/globalRSI", on); st.sync(); for (auto* w : widgets) w->setRSIEnabled(on); });
    connect(gIndMACD, &QAction::toggled, this, [this](bool on){ QSettings st("alel12","modular_dashboard"); st.setValue("ui/indicators/globalMACD", on); st.sync(); for (auto* w : widgets) w->setMACDEnabled(on); });
    connect(gIndBB, &QAction::toggled, this, [this](bool on){ QSettings st("alel12","modular_dashboard"); st.setValue("ui/indicators/globalBB", on); st.sync(); for (auto* w : widgets) w->setBBEnabled(on); });

    connect(gAnEnable, &QAction::toggled, this, [this](bool on){ QSettings st("alel12","modular_dashboard"); st.setValue("ui/anomaly/globalEnabled", on); st.sync(); for (auto* w : widgets) w->setAnomalyEnabled(on); });
    auto applyAnModeAll = [this](const QString& key){ QSettings st("alel12","modular_dashboard"); st.setValue("ui/anomaly/globalMode", key); st.sync(); for (auto* w : widgets) w->setAnomalyModeByKey(key); };
    connect(gAnOff, &QAction::triggered, this, [applyAnModeAll](){ applyAnModeAll("off"); });
    connect(gAnRsi, &QAction::triggered, this, [applyAnModeAll](){ applyAnModeAll("rsi"); });
    connect(gAnMacd, &QAction::triggered, this, [applyAnModeAll](){ applyAnModeAll("macd"); });
    connect(gAnBB, &QAction::triggered, this, [applyAnModeAll](){ applyAnModeAll("bb"); });
    connect(gAnZS, &QAction::triggered, this, [applyAnModeAll](){ applyAnModeAll("zscore"); });
    connect(gAnVol, &QAction::triggered, this, [applyAnModeAll](){ applyAnModeAll("vol"); });
    connect(gAnComp, &QAction::triggered, this, [applyAnModeAll](){ applyAnModeAll("comp"); });
    connect(gAnRsiDiv, &QAction::triggered, this, [applyAnModeAll](){ applyAnModeAll("rsi_div"); });
    connect(gAnMacdHist, &QAction::triggered, this, [applyAnModeAll](){ applyAnModeAll("macd_hist"); });
    connect(gAnClustZ, &QAction::triggered, this, [applyAnModeAll](){ applyAnModeAll("clustered_z"); });
    connect(gAnVolReg, &QAction::triggered, this, [applyAnModeAll](){ applyAnModeAll("vol_regime"); });

    connect(gOvVol, &QAction::toggled, this, [this](bool on){ QSettings st("alel12","modular_dashboard"); st.setValue("ui/overlays/globalVol", on); st.sync(); for (auto* w : widgets) w->setOverlayVolatility(on); });
    connect(gOvChg, &QAction::toggled, this, [this](bool on){ QSettings st("alel12","modular_dashboard"); st.setValue("ui/overlays/globalChg", on); st.sync(); for (auto* w : widgets) w->setOverlayChange(on); });

    // Help menu
    auto* helpMenu = menuBar()->addMenu("Help");
    auto* aboutAct = helpMenu->addAction("About");
    connect(aboutAct, &QAction::triggered, this, &MainWindow::showAbout);

    // History menu: save/load/clear and backend choice + autosave
    auto* histMenu = menuBar()->addMenu(QString::fromUtf8("История"));
    QAction* actSaveHist = histMenu->addAction(QString::fromUtf8("Сохранить историю"));
    QAction* actLoadHist = histMenu->addAction(QString::fromUtf8("Загрузить историю"));
    QAction* actClearHist = histMenu->addAction(QString::fromUtf8("Очистить историю"));
    histMenu->addSeparator();
    QAction* actBackendJsonl = histMenu->addAction("Backend: JSONL"); actBackendJsonl->setCheckable(true);
    QAction* actBackendSql   = histMenu->addAction("Backend: SQLite"); actBackendSql->setCheckable(true);
    QActionGroup* backendGrp = new QActionGroup(this); backendGrp->setExclusive(true); backendGrp->addAction(actBackendJsonl); backendGrp->addAction(actBackendSql);
    histMenu->addSeparator();
    QAction* actAutoSave = histMenu->addAction(QString::fromUtf8("Автосохранение каждые N минут")); actAutoSave->setCheckable(true);

    // History state
    auto history = new HistoryStorage(this);
    QTimer* autoSaveTimer = new QTimer(this);
    // Load prefs
    {
        QSettings st("alel12", "modular_dashboard");
        const QString be = st.value("history/backend", "jsonl").toString();
        if (be=="sqlite") { history->setBackend(HistoryStorage::Backend::SQLite); actBackendSql->setChecked(true); }
        else { history->setBackend(HistoryStorage::Backend::Jsonl); actBackendJsonl->setChecked(true); }
        bool autoOn = st.value("history/auto", false).toBool(); actAutoSave->setChecked(autoOn);
        int mins = std::clamp(st.value("history/autoMins", 5).toInt(), 1, 120);
        if (autoOn) { autoSaveTimer->start(mins*60*1000); }
    }
    // Backend switch
    connect(actBackendJsonl, &QAction::triggered, this, [this,history](){ QSettings st("alel12","modular_dashboard"); st.setValue("history/backend","jsonl"); st.sync(); history->setBackend(HistoryStorage::Backend::Jsonl); QMessageBox::information(this, "History", "Backend: JSONL"); });
    connect(actBackendSql,   &QAction::triggered, this, [this,history](){ QSettings st("alel12","modular_dashboard"); st.setValue("history/backend","sqlite"); st.sync(); history->setBackend(HistoryStorage::Backend::SQLite); QMessageBox::information(this, "History", "Backend: SQLite"); });
    // Save
    auto doSave = [this,history](){
        QString path;
        const bool useSql = history->backend()==HistoryStorage::Backend::SQLite;
        if (useSql) path = QFileDialog::getSaveFileName(this, QString::fromUtf8("Сохранить SQLite"), QDir::homePath()+"/history.sqlite", "SQLite DB (*.sqlite *.db)");
        else path = QFileDialog::getSaveFileName(this, QString::fromUtf8("Сохранить JSONL"), QDir::homePath()+"/history.jsonl", "JSON Lines (*.jsonl)");
        if (path.isEmpty()) return;
        auto bundles = HistoryStorage::collect(widgets);
        QString err; bool ok = history->save(bundles, path, &err);
        if (ok) QMessageBox::information(this, QString::fromUtf8("Сохранение"), QString::fromUtf8("История сохранена: %1").arg(path));
        else QMessageBox::critical(this, QString::fromUtf8("Ошибка"), QString::fromUtf8("Не удалось сохранить: %1").arg(err));
    };
    connect(actSaveHist, &QAction::triggered, this, doSave);
    // Load (with validation)
    auto doLoad = [this,history](){
        QString path;
        const bool useSql = history->backend()==HistoryStorage::Backend::SQLite;
        if (useSql) path = QFileDialog::getOpenFileName(this, QString::fromUtf8("Открыть SQLite"), QDir::homePath(), "SQLite DB (*.sqlite *.db)");
        else path = QFileDialog::getOpenFileName(this, QString::fromUtf8("Открыть JSONL"), QDir::homePath(), "JSON Lines (*.jsonl)");
        if (path.isEmpty()) return;
        QVector<HistoryBundle> bundles; QString err; bool ok = history->load(&bundles, path, &err);
        if (!ok) { QMessageBox::critical(this, QString::fromUtf8("Ошибка"), QString::fromUtf8("Не удалось загрузить: %1").arg(err)); return; }
        // Validation: ensure symbols present, timestamps ascending, values finite
        for (const auto& b : bundles) {
            if (b.symbol.trimmed().isEmpty()) { QMessageBox::critical(this, "Ошибка", QString::fromUtf8("Пустой символ в данных")); return; }
            double prevT=-1e18; for (const auto& r : b.points) {
                if (!(std::isfinite(r.ts) && std::isfinite(r.value))) { QMessageBox::critical(this, "Ошибка", QString::fromUtf8("Невалидное число в %1").arg(b.symbol)); return; }
                if (r.ts < prevT) { QMessageBox::critical(this, "Ошибка", QString::fromUtf8("Несортированные временные метки в %1").arg(b.symbol)); return; }
                prevT = r.ts;
            }
        }
        // Apply: if widget exists for symbol, replace its history and badges
        int applied=0; for (const auto& b : bundles) {
            if (!widgets.contains(b.symbol)) continue;
            auto* w = widgets[b.symbol];
            QVector<DynamicSpeedometerCharts::HistoryPoint> pts; pts.reserve(b.points.size());
            for (const auto& r : b.points) { DynamicSpeedometerCharts::HistoryPoint hp; hp.ts=r.ts; hp.value=r.value; hp.provider=r.provider; hp.market=r.market; hp.source=r.source; hp.seq=r.seq; pts.push_back(hp); }
            w->setMarketBadge(b.provider, b.market);
            w->setSourceKind(b.source);
            w->replaceHistory(pts);
            ++applied;
        }
        QMessageBox::information(this, QString::fromUtf8("Загрузка"), QString::fromUtf8("Применено историй: %1").arg(applied));
    };
    connect(actLoadHist, &QAction::triggered, this, doLoad);
    // Clear
    auto doClear = [this,history](){
        QString path;
        const bool useSql = history->backend()==HistoryStorage::Backend::SQLite;
        if (useSql) path = QFileDialog::getOpenFileName(this, QString::fromUtf8("Очистить SQLite"), QDir::homePath(), "SQLite DB (*.sqlite *.db)");
        else path = QFileDialog::getSaveFileName(this, QString::fromUtf8("Очистить JSONL"), QDir::homePath()+"/history.jsonl", "JSON Lines (*.jsonl)");
        if (path.isEmpty()) return;
        QString err; bool ok = history->clear(path, &err);
        if (ok) QMessageBox::information(this, QString::fromUtf8("Очистка"), QString::fromUtf8("Хранилище очищено: %1").arg(path));
        else QMessageBox::critical(this, QString::fromUtf8("Ошибка"), QString::fromUtf8("Не удалось очистить: %1").arg(err));
    };
    connect(actClearHist, &QAction::triggered, this, doClear);
    // Autosave toggle
    connect(actAutoSave, &QAction::toggled, this, [this,autoSaveTimer,history](bool on){
        QSettings st("alel12","modular_dashboard"); st.setValue("history/auto", on); st.sync();
        if (on) {
            int mins = std::clamp(st.value("history/autoMins", 5).toInt(), 1, 120);
            autoSaveTimer->start(mins*60*1000);
        } else autoSaveTimer->stop();
        QMessageBox::information(this, "History", on? QString::fromUtf8("Автосохранение включено") : QString::fromUtf8("Автосохранение выключено"));
    });
    connect(autoSaveTimer, &QTimer::timeout, this, [this,history](){
        // Determine default autosave path in app data
        QSettings st("alel12","modular_dashboard"); QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(base);
        const bool useSql = history->backend()==HistoryStorage::Backend::SQLite;
        QString path = base + (useSql? "/autosave.sqlite" : "/autosave.jsonl");
        auto bundles = HistoryStorage::collect(widgets); QString err; if (!history->save(bundles, path, &err)) {
            // Silent log via message box only if first time? keep it simple:
            QMessageBox::warning(this, "History", QString::fromUtf8("Автосохранение не удалось: %1").arg(err));
        }
    });

    // Market Overview menu
    auto* toolsMenu = menuBar()->addMenu("Tools");
    QAction* openOverview = toolsMenu->addAction(QString::fromUtf8("Общий обзор рынка"));
    QAction* openCompare = toolsMenu->addAction(QString::fromUtf8("Сравнение графиков (норм.)"));
    marketAnalyzer = new MarketAnalyzer(this);
    connect(openOverview, &QAction::triggered, this, [this]() {
        if (!marketWindow) {
            // Build symbol list for UI
            QStringList syms = realSymbolsFrom(currentCurrencies);
            marketWindow = new MarketOverviewWindow(marketAnalyzer, syms, this);
            marketWindow->setAttribute(Qt::WA_DeleteOnClose, true);
            connect(marketWindow, &QObject::destroyed, this, [this](){ marketWindow=nullptr; });
        }
        marketWindow->show(); marketWindow->raise(); marketWindow->activateWindow();
    });
    connect(openCompare, &QAction::triggered, this, [this]() {
        if (!compareWindow) {
            compareWindow = new MultiCompareWindow(this);
            compareWindow->setAttribute(Qt::WA_DeleteOnClose, true);
            connect(compareWindow, &QObject::destroyed, this, [this](){ compareWindow=nullptr; });
        }
        compareWindow->setSources(widgets);
        compareWindow->show(); compareWindow->raise(); compareWindow->activateWindow();
    });

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
        // Propagate source kind to widgets for metadata tagging
        const QString srcKindInit = (streamMode==StreamMode::Trade? "TRADE" : "TICKER");
        for (auto* w : widgets) w->setSourceKind(srcKindInit);
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
        if (widgets.contains(cur)) widgets[cur]->setMarketBadge(prov, market);
    });
    connect(dataWorker, &DataWorker::volumeTick, this, [this](const QString& cur,double volBase,double volQuote,double volIncr,double ts){
        if (widgets.contains(cur)) widgets[cur]->updateVolume(volBase, volQuote, volIncr, ts);
    });
    connect(dataWorker, &DataWorker::unsupportedSymbol, this, [this](const QString& cur,const QString& reason){
        if (widgets.contains(cur)) widgets[cur]->setUnsupportedReason(reason);
    });
    connect(workerThread, &QThread::finished, dataWorker, &QObject::deleteLater);
    // push currencies to worker after moving to thread
    QMetaObject::invokeMethod(dataWorker, [this](){ dataWorker->setCurrencies(realSymbolsFrom(currentCurrencies)); }, Qt::QueuedConnection);
    workerThread->start();

    // Load all persistent settings
    loadSettingsAndApply();
    // Apply saved grid layout now that widgets are created
    reflowGrid();

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

    // Initialize compare workers infrastructure (idle until used)
    cmpBinance = new DataWorker(); cmpBinance->setProvider(DataProvider::Binance); cmpBinance->setMode(StreamMode::Ticker);
    cmpBybitLinear = new DataWorker(); cmpBybitLinear->setProvider(DataProvider::Bybit); cmpBybitLinear->setMode(StreamMode::Ticker); cmpBybitLinear->setBybitPreference(BybitPreference::LinearFirst); cmpBybitLinear->setAllowBybitFallback(false);
    cmpBybitSpot = new DataWorker(); cmpBybitSpot->setProvider(DataProvider::Bybit); cmpBybitSpot->setMode(StreamMode::Ticker); cmpBybitSpot->setBybitPreference(BybitPreference::SpotFirst); cmpBybitSpot->setAllowBybitFallback(false);
    cmpBinanceThread = new QThread(this); cmpBybitLinearThread = new QThread(this); cmpBybitSpotThread = new QThread(this);
    cmpBinance->moveToThread(cmpBinanceThread); cmpBybitLinear->moveToThread(cmpBybitLinearThread); cmpBybitSpot->moveToThread(cmpBybitSpotThread);
    connect(cmpBinanceThread, &QThread::started, cmpBinance, &DataWorker::start);
    connect(cmpBybitLinearThread, &QThread::started, cmpBybitLinear, &DataWorker::start);
    connect(cmpBybitSpotThread, &QThread::started, cmpBybitSpot, &DataWorker::start);
    connect(cmpBinance, &DataWorker::dataUpdated, this, [this](const QString& cur, double price, double){ binancePrice[cur]=price; recomputePseudoTickers(); });
    connect(cmpBybitLinear, &DataWorker::dataUpdated, this, [this](const QString& cur, double price, double){ bybitLinearPrice[cur]=price; recomputePseudoTickers(); });
    connect(cmpBybitSpot, &DataWorker::dataUpdated, this, [this](const QString& cur, double price, double){ bybitSpotPrice[cur]=price; recomputePseudoTickers(); });
    connect(cmpBinanceThread, &QThread::finished, cmpBinance, &QObject::deleteLater);
    connect(cmpBybitLinearThread, &QThread::finished, cmpBybitLinear, &QObject::deleteLater);
    connect(cmpBybitSpotThread, &QThread::finished, cmpBybitSpot, &QObject::deleteLater);
}

#include "MainWindow.moc"

void MainWindow::closeEvent(QCloseEvent* e) {
    // Flush any pending UI state changes
    saveCurrenciesSettings(currentCurrencies);
    // Gracefully stop main worker (avoid Blocking to prevent deadlocks during app quit)
    if (dataWorker && workerThread && workerThread->isRunning()) {
        QMetaObject::invokeMethod(dataWorker, "stop", Qt::QueuedConnection);
    }
    // Stop compare workers
    if (cmpBinance && cmpBinanceThread && cmpBinanceThread->isRunning()) {
        QMetaObject::invokeMethod(cmpBinance, "stop", Qt::BlockingQueuedConnection);
    }
    if (cmpBybitLinear && cmpBybitLinearThread && cmpBybitLinearThread->isRunning()) {
        QMetaObject::invokeMethod(cmpBybitLinear, "stop", Qt::BlockingQueuedConnection);
    }
    if (cmpBybitSpot && cmpBybitSpotThread && cmpBybitSpotThread->isRunning()) {
        QMetaObject::invokeMethod(cmpBybitSpot, "stop", Qt::BlockingQueuedConnection);
    }
    // Quit threads
    auto quitAndWait = [](QThread* t){ if (!t) return; t->quit(); if (!t->wait(2500)) { t->quit(); t->wait(1000); } };
    quitAndWait(workerThread);
    quitAndWait(cmpBinanceThread);
    quitAndWait(cmpBybitLinearThread);
    quitAndWait(cmpBybitSpotThread);
    // Allow base class to proceed
    QMainWindow::closeEvent(e);
}

MainWindow::~MainWindow() {
    if (dataWorker && workerThread && workerThread->isRunning()) {
        QMetaObject::invokeMethod(dataWorker, "stop", Qt::QueuedConnection);
    }
    if (workerThread) { workerThread->quit(); workerThread->wait(2500); }
    // Stop compare workers
    if (cmpBinance && cmpBinanceThread && cmpBinanceThread->isRunning()) QMetaObject::invokeMethod(cmpBinance, "stop", Qt::BlockingQueuedConnection);
    if (cmpBybitLinear && cmpBybitLinearThread && cmpBybitLinearThread->isRunning()) QMetaObject::invokeMethod(cmpBybitLinear, "stop", Qt::BlockingQueuedConnection);
    if (cmpBybitSpot && cmpBybitSpotThread && cmpBybitSpotThread->isRunning()) QMetaObject::invokeMethod(cmpBybitSpot, "stop", Qt::BlockingQueuedConnection);
    if (cmpBinanceThread) { cmpBinanceThread->quit(); cmpBinanceThread->wait(); }
    if (cmpBybitLinearThread) { cmpBybitLinearThread->quit(); cmpBybitLinearThread->wait(); }
    if (cmpBybitSpotThread) { cmpBybitSpotThread->quit(); cmpBybitSpotThread->wait(); }
}

void MainWindow::switchMode(StreamMode m) {
    streamMode = m;
    QMetaObject::invokeMethod(dataWorker, [this,m](){ dataWorker->stop(); dataWorker->setMode(m); dataWorker->start(); }, Qt::QueuedConnection);
    setWindowTitle(QString("Modular Crypto Dashboard — %1").arg(m==StreamMode::Trade?"TRADE":"TICKER"));
    // Persist choice
    QSettings st("alel12", "modular_dashboard"); st.setValue("stream/mode", m==StreamMode::Ticker?"TICKER":"TRADE"); st.sync();
    // Update widgets' source kind metadata
    {
        const QString srcKind = (m==StreamMode::Trade? "TRADE" : "TICKER");
        for (auto* w : widgets) w->setSourceKind(srcKind);
    }
}

void MainWindow::openPerformanceDialog() {
    auto s = readPerfSettings();
    // Read python-like params from settings
    QSettings st("alel12", "modular_dashboard");
    double pyInitSpan = st.value("perf/pyInitSpanPct", 0.005).toDouble();
    double pyMinComp  = st.value("perf/pyMinCompress", 1.000001).toDouble();
    double pyMaxComp  = st.value("perf/pyMaxCompress", 0.999999).toDouble();
    double pyMinWidth = st.value("perf/pyMinWidthPct", 0.0001).toDouble();
    int scalingWindow = st.value("scaling/windowSize", 0).toInt();
    double scalingPadding = st.value("scaling/paddingPct", 0.01).toDouble();
    // Get current scaling settings from first widget
    auto currentScaling = widgets.isEmpty() ? DynamicSpeedometerCharts::ScalingSettings{} : widgets.first()->scaling();
    PerformanceConfigDialog dlg(this, s.animMs, s.renderMs, s.cacheMs, s.volWindow, s.maxPts, s.rawCache,
                                pyInitSpan, pyMinComp, pyMaxComp, pyMinWidth, currentScaling.windowSize, currentScaling.paddingPct);
    if (dlg.exec()==QDialog::Accepted) {
        PerfSettings ns{dlg.animationMs(), dlg.renderIntervalMs(), dlg.cacheIntervalMs(), dlg.volatilityWindowSize(), dlg.maxPointsCount(), dlg.rawCacheSize()};
        writePerfSettings(ns);
        // Save python-like params
        st.setValue("perf/pyInitSpanPct", dlg.pyInitSpanPctVal());
        st.setValue("perf/pyMinCompress", dlg.pyMinCompressVal());
        st.setValue("perf/pyMaxCompress", dlg.pyMaxCompressVal());
        st.setValue("perf/pyMinWidthPct", dlg.pyMinWidthPctVal());
        st.setValue("scaling/windowSize", dlg.scalingWindowSizeVal());
        st.setValue("scaling/paddingPct", dlg.scalingPaddingPctVal());
        st.sync();
        for (auto* w : widgets) w->applyPerformance(ns.animMs, ns.renderMs, ns.cacheMs, ns.volWindow, ns.maxPts, ns.rawCache);
        // Apply python-like params live
        for (auto* w : widgets) w->setPythonScalingParams(dlg.pyInitSpanPctVal(), dlg.pyMinCompressVal(), dlg.pyMaxCompressVal(), dlg.pyMinWidthPctVal());
        // Apply scaling params live
        for (auto* w : widgets) {
            auto scaling = w->scaling();
            scaling.windowSize = dlg.scalingWindowSizeVal();
            scaling.paddingPct = dlg.scalingPaddingPctVal();
            w->applyScaling(scaling);
        }
    }
}

void MainWindow::openThemeDialog() {
    bool ok=false; QStringList names = themeManager->themeNames(); QString current = themeManager->current().name;
    QString chosen = QInputDialog::getItem(this, "Choose Theme", "Theme:", names, names.indexOf(current), false, &ok);
    if (!ok) return; applyTheme(chosen);
}

void MainWindow::handleData(const QString& currency, double price, double timestamp) {
    if (widgets.contains(currency)) { if (currency=="BTC") btcPrice=price; QMetaObject::invokeMethod(this, [this,currency,price,timestamp](){ widgets[currency]->updateData(price, timestamp, btcPrice); }, Qt::QueuedConnection); }
    // Track normalized value when widget is a real symbol
    if (!isPseudo(currency) && widgets.contains(currency)) {
        // Ask widget for current normalized value via property 'value'
        bool ok=false; double v = widgets[currency]->property("value").toDouble(&ok); if (!ok) v = 0.0;
        normalizedBySymbol[currency] = std::clamp(v, 0.0, 100.0);
        recomputePseudoTickers();
        // Feed analyzer with latest normalized value and volatility if known
        if (marketAnalyzer) {
            double vol = volBySymbol.value(currency, 0.0);
            marketAnalyzer->updateSymbol(currency, timestamp, v, vol);
        }
    }
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
    QMetaObject::invokeMethod(dataWorker, [this](){ dataWorker->setCurrencies(realSymbolsFrom(currentCurrencies)); }, Qt::QueuedConnection);
    refreshCompareSubscriptions();
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
    
    // Apply saved scaling params
    int scalingWindow = st.value("scaling/windowSize", 0).toInt();
    double scalingPadding = st.value("scaling/paddingPct", 0.01).toDouble();
    for (auto* w : widgets) {
        auto scaling = w->scaling();
        scaling.windowSize = scalingWindow;
        scaling.paddingPct = scalingPadding;
        w->applyScaling(scaling);
    }
    
    // Apply saved scaling mode to widgets handled in constructor when actions exist
}

void MainWindow::reflowGrid() {
    if (!gridLayout) return;
    int desired = std::max(1, gridCols * gridRows);
    // Adjust currentCurrencies to desired size using TOP50 as source
    QStringList newList = currentCurrencies;
    // Extend if needed (do not auto-fill with pseudo)
    if (newList.size() < desired) {
        for (const auto& sym : TOP50) {
            if (newList.size() >= desired) break;
            if (!newList.contains(sym)) newList << sym;
        }
    }
    // Shrink if needed
    if (newList.size() > desired) {
        newList = newList.mid(0, desired);
    }

    // Create widgets for newly added symbols; remove widgets for dropped symbols
    // Build sets for diff
    QSet<QString> before = QSet<QString>(currentCurrencies.begin(), currentCurrencies.end());
    QSet<QString> after = QSet<QString>(newList.begin(), newList.end());
    QSet<QString> toAdd = after - before;
    QSet<QString> toRemove = before - after;

    // Remove widgets that are no longer needed
    for (const auto& c : toRemove) {
        if (!widgets.contains(c)) continue;
        QWidget* w = widgets[c];
        // Remove from layout
        for (int i=gridLayout->count()-1; i>=0; --i) {
            QLayoutItem* it = gridLayout->itemAt(i);
            if (it && it->widget()==w) { gridLayout->takeAt(i); break; }
        }
        widgets.remove(c);
        w->deleteLater();
    }

    // Helper to map name->style
    auto styleFromName = [](const QString& sName){
        using S = DynamicSpeedometerCharts::SpeedometerStyle;
        if (sName=="NeonGlow") return S::NeonGlow;
        if (sName=="Minimal")  return S::Minimal;
        if (sName=="ModernTicks") return S::ModernTicks;
        if (sName=="Circle" || sName=="Classic Pro") return S::Circle;
        if (sName=="Gauge") return S::Gauge;
        if (sName=="Ring" || sName=="Modern Scale") return S::Ring;
        return S::Classic;
    };

    // Create widgets for new symbols
    for (const auto& c : toAdd) {
        auto* w = new DynamicSpeedometerCharts(c);
        widgets[c] = w;
        connect(w, &DynamicSpeedometerCharts::requestRename, this, &MainWindow::onRequestRename);
        connect(w, &DynamicSpeedometerCharts::requestChangeTicker, this, [this,c](const QString& oldName, const QString& newName){ Q_UNUSED(oldName); QMetaObject::invokeMethod(this, [this,c,newName](){
            // Directly apply rename without dialog using the provided newName
            QString currentTicker = c;
            QString newTicker = newName.trimmed().toUpper();
            if (newTicker.isEmpty() || newTicker==currentTicker) return;
            if (!widgets.contains(currentTicker)) return;
            int idxOld = currentCurrencies.indexOf(currentTicker);
            if (idxOld < 0) return;
            if (widgets.contains(newTicker)) {
                int idxNew = currentCurrencies.indexOf(newTicker);
                if (idxNew >= 0) currentCurrencies.swapItemsAt(idxOld, idxNew);
                auto* w = widgets[currentTicker]; auto* other = widgets[newTicker];
                widgets[newTicker] = w; w->setCurrencyName(newTicker);
                widgets[currentTicker] = other; other->setCurrencyName(currentTicker);
            } else {
                auto* w = widgets[currentTicker]; widgets.remove(currentTicker);
                widgets[newTicker] = w; w->setCurrencyName(newTicker);
                currentCurrencies[idxOld] = newTicker;
            }
            // migrate per-widget style key
            {
                QSettings st("alel12", "modular_dashboard");
                const QString oldKey = QString("ui/stylePerWidget/%1").arg(currentTicker);
                const QString newKey = QString("ui/stylePerWidget/%1").arg(newTicker);
                QString val = st.value(oldKey).toString(); if (!val.isEmpty() && st.value(newKey).toString().isEmpty()) { st.setValue(newKey, val); st.remove(oldKey); st.sync(); }
            }
            saveCurrenciesSettings(currentCurrencies);
            widgets[newTicker]->setUnsupportedReason(""); widgets[currentTicker]->setUnsupportedReason("");
            QMetaObject::invokeMethod(dataWorker, [this](){ dataWorker->setCurrencies(realSymbolsFrom(currentCurrencies)); }, Qt::QueuedConnection);
            refreshCompareSubscriptions();
            reflowGrid();
        }, Qt::QueuedConnection); });
        connect(w, &DynamicSpeedometerCharts::styleSelected, this, [this](const QString& cur, const QString& styleName){
            QSettings st("alel12", "modular_dashboard");
            st.setValue(QString("ui/stylePerWidget/%1").arg(cur), styleName); st.sync();
        });
        connectRealWidgetSignals(c, w);
        // Apply per-widget or global style
        QSettings st("alel12", "modular_dashboard");
        QString per = st.value(QString("ui/stylePerWidget/%1").arg(c)).toString();
        QString globalName = st.value("ui/speedometerStyle", "Classic").toString();
        w->setSpeedometerStyle(styleFromName(per.isEmpty()? globalName : per));
        // Seed badge
        QString provider = st.value("stream/provider","Binance").toString();
        bool isBybit = provider.compare("Bybit", Qt::CaseInsensitive)==0;
        QString market = "";
        if (isBybit) {
            QString pref = st.value("bybit/preference","LinearFirst").toString();
            market = (pref=="LinearFirst")? "Linear" : "Spot";
        }
        w->setMarketBadge(isBybit?"Bybit":"Binance", market);
    }

    // Persist the new list and update state
    currentCurrencies = newList; saveCurrenciesSettings(currentCurrencies);
    if (dataWorker) QMetaObject::invokeMethod(dataWorker, [this](){ dataWorker->setCurrencies(realSymbolsFrom(currentCurrencies)); }, Qt::QueuedConnection);
    refreshCompareSubscriptions();

    // Rebuild layout grid with current list
    while (gridLayout->count() > 0) { QLayoutItem* it = gridLayout->takeAt(0); Q_UNUSED(it); }
    int row=0, col=0; for (const QString& c : currentCurrencies) {
        if (!widgets.contains(c)) continue;
        gridLayout->addWidget(widgets[c], row, col);
        if (++col >= gridCols) { col=0; ++row; }
    }

    // Re-apply theme and performance/scaling to ensure new widgets match current settings
    loadSettingsAndApply();
    // Update market overview symbol list if open
    if (marketWindow) {
        marketWindow->setSymbols(realSymbolsFrom(currentCurrencies));
    }
    if (compareWindow) {
        compareWindow->setSources(widgets);
    }
    // Update geometry
    centralWidget()->updateGeometry();
}

MainWindow::PseudoKind MainWindow::classifyPseudo(const QString& name, DiffSpec* outDiff) const {
    if (!name.startsWith("@")) return PseudoKind::None;
    const QString up = name.toUpper();
    if (up == "@AVG") return PseudoKind::Avg;
    if (up == "@ALT_AVG") return PseudoKind::AltAvg;
    if (up == "@MEDIAN") return PseudoKind::Median;
    if (up == "@SPREAD") return PseudoKind::Spread;
    if (up == "@TOP10_AVG") return PseudoKind::Top10Avg;
    if (up == "@VOL_AVG") return PseudoKind::VolAvg;
    if (up == "@BTC_DOM") return PseudoKind::BtcDom;
    if (up.startsWith("@Z_SCORE:")) return PseudoKind::ZScore;
    if (up.startsWith("@DIFF:")) {
        if (!outDiff) return PseudoKind::Diff;
        // Format: @DIFF:SYMBOL or @DIFF:SYMBOL:Linear|Spot
        const QString body = name.mid(6); // after @DIFF:
        QStringList parts = body.split(':', Qt::KeepEmptyParts);
        if (parts.isEmpty()) { outDiff->valid=false; return PseudoKind::Diff; }
        outDiff->symbol = parts[0].trimmed().toUpper(); outDiff->valid = !outDiff->symbol.isEmpty();
        if (parts.size()>=2) {
            QString m = parts[1].trimmed();
            outDiff->market = (m.compare("Spot", Qt::CaseInsensitive)==0) ? BybitMarket::Spot : BybitMarket::Linear;
        } else {
            outDiff->market = BybitMarket::Linear;
        }
        return PseudoKind::Diff;
    }
    return PseudoKind::None;
}

void MainWindow::connectRealWidgetSignals(const QString& symbol, DynamicSpeedometerCharts* w) {
    if (isPseudo(symbol)) return;
    connect(w, &DynamicSpeedometerCharts::valueChanged, this, [this, symbol](double v){ normalizedBySymbol[symbol] = std::clamp(v, 0.0, 100.0); recomputePseudoTickers(); });
    connect(w, &DynamicSpeedometerCharts::volatilityChanged, this, [this, symbol](double vol){ volBySymbol[symbol] = std::max(0.0, vol); });
}

QStringList MainWindow::realSymbolsFrom(const QStringList& list) const {
    QStringList out; out.reserve(list.size());
    for (const auto& s : list) if (!isPseudo(s)) out << s;
    return out;
}

void MainWindow::recomputePseudoTickers() {
    const auto now = QDateTime::currentMSecsSinceEpoch()/1000.0;
    // Pre-collect real normalized values
    QVector<double> vals; vals.reserve(normalizedBySymbol.size());
    for (auto it = normalizedBySymbol.begin(); it != normalizedBySymbol.end(); ++it) vals.push_back(it.value());
    std::sort(vals.begin(), vals.end());
    auto computeMedian = [&](){ if (vals.isEmpty()) return 0.0; int n=vals.size(); if (n%2==1) return double(vals[n/2]); else return 0.5*(vals[n/2-1]+vals[n/2]); };
    auto computeAvg = [&](){ if (vals.isEmpty()) return 0.0; double s=std::accumulate(vals.begin(), vals.end(), 0.0); return s/vals.size(); };
    auto computeAltAvg = [&](){ QVector<double> v2; v2.reserve(vals.size()); for (auto it = normalizedBySymbol.begin(); it!=normalizedBySymbol.end(); ++it) if (it.key()!="BTC") v2.push_back(it.value()); if (v2.isEmpty()) return 0.0; double s=std::accumulate(v2.begin(), v2.end(), 0.0); return s/v2.size(); };
    auto computeSpread = [&](){ if (vals.isEmpty()) return 0.0; return vals.last() - vals.first(); };
    auto computeTop10Avg = [&](){ QVector<double> v; v.reserve(10); for (const auto& sym : TOP50.mid(0,10)) if (normalizedBySymbol.contains(sym)) v.push_back(normalizedBySymbol.value(sym)); if (v.isEmpty()) return 0.0; double s=std::accumulate(v.begin(), v.end(), 0.0); return s/v.size(); };
    auto computeVolAvg = [&](){ if (volBySymbol.isEmpty()) return 0.0; double s=0.0; int n=0; for (auto it=volBySymbol.begin(); it!=volBySymbol.end(); ++it){ s+=it.value(); ++n; } return n? s/n : 0.0; };
    auto computeBtcDom = [&](){ // dominance proxy: BTC value vs average of basket
        double btc = normalizedBySymbol.value("BTC", 50.0); double avg = computeAvg(); if (avg<=0) return 100.0; double dom = btc/avg*100.0; return std::clamp(dom, 0.0, 200.0); };

    for (auto it = widgets.begin(); it != widgets.end(); ++it) {
        const QString& name = it.key(); auto* w = it.value(); DiffSpec ds; auto kind = classifyPseudo(name, &ds);
        if (kind == PseudoKind::None) continue;
        double agg = 0.0;
        switch (kind) {
            case PseudoKind::Avg: agg = computeAvg(); w->setMarketBadge("Computed", "AVG"); break;
            case PseudoKind::AltAvg: agg = computeAltAvg(); w->setMarketBadge("Computed", "ALT_AVG"); break;
            case PseudoKind::Median: agg = computeMedian(); w->setMarketBadge("Computed", "MEDIAN"); break;
            case PseudoKind::Spread: agg = computeSpread(); w->setMarketBadge("Computed", "SPREAD"); break;
            case PseudoKind::Top10Avg: agg = computeTop10Avg(); w->setMarketBadge("Computed", "TOP10_AVG"); break;
            case PseudoKind::VolAvg: {
                double vol = computeVolAvg(); // map vol% to 0..100 via soft clamp (assume 0..10% typical)
                double scale = 10.0; agg = std::clamp(vol/scale*100.0, 0.0, 100.0); w->setMarketBadge("Computed", "VOL_AVG"); break; }
            case PseudoKind::BtcDom: {
                double dom = computeBtcDom(); // dom 0..200 → map 0..100 with 100 as max meaningful
                agg = std::clamp(dom, 0.0, 100.0); w->setMarketBadge("Computed", "BTC_DOM"); break; }
            case PseudoKind::ZScore: {
                // Format: @Z_SCORE:SYMBOL
                QString sym = name.mid(QString("@Z_SCORE:").length()).trimmed().toUpper();
                double v = normalizedBySymbol.value(sym, 50.0);
                // Use global basket stats as proxy
                double mean = computeAvg(); double sd = 0.0; 
                if (vals.size()>1) {
                    double s2 = 0.0; for (double x: vals) s2 += (x-mean)*(x-mean); sd = std::sqrt(s2/vals.size());
                }
                double z = (sd>1e-9) ? ((v-mean)/sd) : 0.0; // z in ~[-3..3]
                // Map z-score to 0..100 with z=0 -> 50, 1 sigma -> 65, -1 -> 35
                agg = std::clamp(50.0 + z*15.0, 0.0, 100.0);
                w->setMarketBadge("Computed", QString("Z_SCORE • %1").arg(sym));
                break; }
            case PseudoKind::Diff: {
                if (!ds.valid) { agg = 0.0; w->setMarketBadge("Computed", "DIFF"); break; }
                double b = binancePrice.value(ds.symbol, 0.0);
                double y = (ds.market==BybitMarket::Linear? bybitLinearPrice.value(ds.symbol, 0.0) : bybitSpotPrice.value(ds.symbol, 0.0));
                double diffPct = 0.0; if (b>0 && y>0) diffPct = (y - b) / b * 100.0; // percent difference Bybit vs Binance
                // Map percent diff to 0..100 center at 50 (=0%) with +/- 10% window → 0..100
                double center=50.0; double scale=5.0; // 10% => 50 +/- 50 → scale=5 (since 10% * 5 = 50)
                agg = std::clamp(center + diffPct*scale, 0.0, 100.0);
                w->setMarketBadge("Computed", QString("DIFF %1 • %2").arg(ds.symbol, ds.market==BybitMarket::Linear?"Linear":"Spot"));
                break;
            }
            default: break;
        }
        w->applyScaling({DynamicSpeedometerCharts::ScalingMode::Fixed, 0.0, 100.0});
        w->updateData(agg, now, btcPrice);
    }
}

void MainWindow::refreshCompareSubscriptions() {
    // Build watch lists from currentCurrencies for all @DIFF items
    QSet<QString> needBinance, needLinear, needSpot;
    for (const auto& s : currentCurrencies) {
        DiffSpec ds; if (classifyPseudo(s, &ds) == PseudoKind::Diff && ds.valid) {
            needBinance.insert(ds.symbol);
            (ds.market==BybitMarket::Linear ? needLinear : needSpot).insert(ds.symbol);
        }
    }
    if (needBinance.isEmpty() && needLinear.isEmpty() && needSpot.isEmpty()) {
        if (cmpBinanceThread && cmpBinanceThread->isRunning()) { QMetaObject::invokeMethod(cmpBinance, "stop", Qt::QueuedConnection); cmpBinanceThread->quit(); }
        if (cmpBybitLinearThread && cmpBybitLinearThread->isRunning()) { QMetaObject::invokeMethod(cmpBybitLinear, "stop", Qt::QueuedConnection); cmpBybitLinearThread->quit(); }
        if (cmpBybitSpotThread && cmpBybitSpotThread->isRunning()) { QMetaObject::invokeMethod(cmpBybitSpot, "stop", Qt::QueuedConnection); cmpBybitSpotThread->quit(); }
        return;
    }
    // Start threads if needed
    if (!needBinance.isEmpty()) {
        if (!cmpBinanceThread->isRunning()) cmpBinanceThread->start();
        if (cmpSymsBinance != needBinance) { cmpSymsBinance = needBinance; QMetaObject::invokeMethod(cmpBinance, [this,needBinance](){ cmpBinance->setCurrencies(QStringList(needBinance.values())); }, Qt::QueuedConnection); }
    }
    if (!needLinear.isEmpty()) {
        if (!cmpBybitLinearThread->isRunning()) cmpBybitLinearThread->start();
        if (cmpSymsBybitLinear != needLinear) { cmpSymsBybitLinear = needLinear; QMetaObject::invokeMethod(cmpBybitLinear, [this,needLinear](){ cmpBybitLinear->setCurrencies(QStringList(needLinear.values())); }, Qt::QueuedConnection); }
    }
    if (!needSpot.isEmpty()) {
        if (!cmpBybitSpotThread->isRunning()) cmpBybitSpotThread->start();
        if (cmpSymsBybitSpot != needSpot) { cmpSymsBybitSpot = needSpot; QMetaObject::invokeMethod(cmpBybitSpot, [this,needSpot](){ cmpBybitSpot->setCurrencies(QStringList(needSpot.values())); }, Qt::QueuedConnection); }
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

void MainWindow::showAbout() {
    QMessageBox::about(this, "About", "Crypto Dashboard v1.1.2\nBuilt with Qt6");
}

