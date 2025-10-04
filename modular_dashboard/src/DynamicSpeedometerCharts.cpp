#include "DynamicSpeedometerCharts.h"
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QDateTime>
#include <QLocale>
#include <algorithm>
#include <numeric>
#include <QSettings>
#include <cmath>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDebug>

DynamicSpeedometerCharts::DynamicSpeedometerCharts(const QString& cur, QWidget* parent)
    : QWidget(parent), currency(cur) {
    // Initialize default theme colors (Dark theme)
    themeColors = {
        QColor(40,44,52),      // background
        QColor(100,100,100),   // arcBase  
        QColor(255,255,255),   // needleNormal
        QColor(76,175,80),     // zoneGood
        QColor(255,193,7),     // zoneWarn
        QColor(244,67,54),     // zoneDanger
        QColor(255,255,255),   // text
        QColor(0,255,180)      // glow
    };
    
    timeScales = {{"1m",60},{"5m",300},{"15m",900},{"30m",1800},{"1h",3600},{"4h",14400},{"24h",86400}};
    setMinimumSize(100,100);
    animation = new QPropertyAnimation(this, "value", this); animation->setDuration(400);
    connect(animation, &QPropertyAnimation::valueChanged, this, [this](){ if (modeView=="speedometer") update(); });

    // Charts
    chart = new QChart(); chart->setBackgroundBrush(QColor(40,44,52)); chart->legend()->hide();
    seriesNormal = new QLineSeries(chart); seriesRatio = new QLineSeries(chart);
    chart->addSeries(seriesNormal); chart->addSeries(seriesRatio);
    // Indicator series
    rsiSeries = new QLineSeries(chart); rsiSeries->setName("RSI"); rsiSeries->setColor(QColor(180, 120, 255)); rsiSeries->setVisible(false); chart->addSeries(rsiSeries);
    macdSeries = new QLineSeries(chart); macdSeries->setName("MACD"); macdSeries->setColor(QColor(255, 90, 90)); macdSeries->setVisible(false); chart->addSeries(macdSeries);
    macdSignalSeries = new QLineSeries(chart); macdSignalSeries->setName("Signal"); macdSignalSeries->setColor(QColor(255, 190, 120)); macdSignalSeries->setVisible(false); chart->addSeries(macdSignalSeries);
    bbUpperSeries = new QLineSeries(chart); bbUpperSeries->setName("BB Upper"); bbUpperSeries->setColor(QColor(120, 200, 255)); bbUpperSeries->setVisible(false); chart->addSeries(bbUpperSeries);
    bbLowerSeries = new QLineSeries(chart); bbLowerSeries->setName("BB Lower"); bbLowerSeries->setColor(QColor(120, 200, 255)); bbLowerSeries->setVisible(false); chart->addSeries(bbLowerSeries);

    axisX = new QValueAxis(chart); axisY = new QValueAxis(chart);
    axisX->setVisible(false); axisX->setTickCount(2); axisY->setLabelFormat("%.2f");
    chart->addAxis(axisX, Qt::AlignBottom); chart->addAxis(axisY, Qt::AlignLeft);
    // Additional axes for indicators
    axisRSI = new QValueAxis(chart); axisRSI->setLabelFormat("%.0f"); axisRSI->setVisible(false); chart->addAxis(axisRSI, Qt::AlignRight);
    axisMACD = new QValueAxis(chart); axisMACD->setLabelFormat("%.2f"); axisMACD->setVisible(false); chart->addAxis(axisMACD, Qt::AlignRight);

    seriesNormal->attachAxis(axisX); seriesNormal->attachAxis(axisY);
    seriesRatio->attachAxis(axisX); seriesRatio->attachAxis(axisY); seriesRatio->setVisible(false);
    // Attach indicator series to axes
    rsiSeries->attachAxis(axisX); rsiSeries->attachAxis(axisRSI);
    macdSeries->attachAxis(axisX); macdSeries->attachAxis(axisMACD);
    macdSignalSeries->attachAxis(axisX); macdSignalSeries->attachAxis(axisMACD);
    bbUpperSeries->attachAxis(axisX); bbUpperSeries->attachAxis(axisY);
    bbLowerSeries->attachAxis(axisX); bbLowerSeries->attachAxis(axisY);

    chartView = new QChartView(chart, this); chartView->setRenderHint(QPainter::Antialiasing); chartView->setVisible(false);
    chartView->setGeometry(rect());

    renderTimer = new QTimer(this); renderTimer->setInterval(16);
    connect(renderTimer, &QTimer::timeout, this, &DynamicSpeedometerCharts::onRenderTimeout); renderTimer->start();
    cacheUpdateTimer = new QTimer(this); cacheUpdateTimer->setInterval(300);
    connect(cacheUpdateTimer, &QTimer::timeout, this, &DynamicSpeedometerCharts::cacheChartData); cacheUpdateTimer->start();

    // Load per-widget overlay settings
    {
        QSettings st("alel12", "modular_dashboard");
        showVolOverlay = st.value(QString("ui/overlays/vol/%1").arg(currency), false).toBool();
        showChangeOverlay = st.value(QString("ui/overlays/chg/%1").arg(currency), false).toBool();
    // Volume visualization mode
    QString vv = st.value(QString("ui/volume/vis/%1").arg(currency), "off").toString().toLower();
    if (vv=="bar") volVis = VolumeVis::Bar;
    else if (vv=="needle") volVis = VolumeVis::Needle;
    else if (vv=="sidebar") volVis = VolumeVis::Sidebar;
    else if (vv=="fuel") volVis = VolumeVis::Fuel;
    else volVis = VolumeVis::Off;
        showRSI = st.value(QString("ui/indicators/rsi/%1").arg(currency), false).toBool();
        showMACD = st.value(QString("ui/indicators/macd/%1").arg(currency), false).toBool();
        showBB = st.value(QString("ui/indicators/bb/%1").arg(currency), false).toBool();
        // Transitions (global)
        transitionsEnabled = st.value("ui/transitions/enabled", true).toBool();
        const QString t = st.value("ui/transitions/type", "flip").toString().toLower();
        if (t=="slide") transitionType = TransitionOverlay::Slide;
        else if (t=="crossfade") transitionType = TransitionOverlay::Crossfade;
        else if (t=="zoomblur") transitionType = TransitionOverlay::ZoomBlur;
        else if (t=="none") transitionType = TransitionOverlay::None;
        else transitionType = TransitionOverlay::Flip;
        // Anomalies per-widget
        showAnomalyBadge = st.value(QString("ui/anomaly/enabled/%1").arg(currency), false).toBool();
        QString am = st.value(QString("ui/anomaly/mode/%1").arg(currency), "off").toString().toLower();
    if (am=="rsi") anomalyMode = AnomalyMode::RSIOverboughtOversold;
        else if (am=="macd") anomalyMode = AnomalyMode::MACDCross;
        else if (am=="bb") anomalyMode = AnomalyMode::BollingerBreakout;
        else if (am=="zscore") anomalyMode = AnomalyMode::ZScore;
        else if (am=="vol") anomalyMode = AnomalyMode::VolSpike;
        else if (am=="comp") anomalyMode = AnomalyMode::Composite;
    else if (am=="rsi_div") anomalyMode = AnomalyMode::RSIDivergence;
    else if (am=="macd_hist") anomalyMode = AnomalyMode::MACDHistSurge;
    else if (am=="clustered_z") anomalyMode = AnomalyMode::ClusteredZ;
    else if (am=="vol_regime") anomalyMode = AnomalyMode::VolRegimeShift;
        else anomalyMode = AnomalyMode::Off;
        // Sidebar tuning
        sidebarWidthMode = st.value(QString("ui/volume/sidebar/width_mode/%1").arg(currency), 0).toInt();
        sidebarOutline = st.value(QString("ui/volume/sidebar/outline/%1").arg(currency), true).toBool();
        sidebarBrightnessPct = st.value(QString("ui/volume/sidebar/brightness/%1").arg(currency), 100).toInt();
        sidebarBrightnessPct = std::clamp(sidebarBrightnessPct, 50, 150);
        // Widget frame style
        QString fs = st.value(QString("ui/frame/style/%1").arg(currency), "none").toString().toLower();
        if (fs=="minimal") frameStyle = FrameStyle::Minimal;
        else if (fs=="dashed") frameStyle = FrameStyle::Dashed;
        else if (fs=="glow") frameStyle = FrameStyle::Glow;
        else frameStyle = FrameStyle::None;
    }
    // Sensitivity preferences
    loadSensitivityPrefs();
}

// Public helpers to be called from MainWindow for global control
void DynamicSpeedometerCharts::setFrameStyleByName(const QString& name) {
    FrameStyle fs = FrameStyle::None;
    const QString key = name.toLower();
    if (key=="minimal") fs = FrameStyle::Minimal;
    else if (key=="dashed") fs = FrameStyle::Dashed;
    else if (key=="glow") fs = FrameStyle::Glow;
    if (frameStyle != fs) { frameStyle = fs; }
    QSettings st("alel12","modular_dashboard");
    st.setValue(QString("ui/frame/style/%1").arg(currency), key); st.sync();
    if (modeView=="speedometer") update();
}

void DynamicSpeedometerCharts::setSidebarWidthMode(int mode) {
    mode = std::clamp(mode, 0, 3);
    if (sidebarWidthMode != mode) sidebarWidthMode = mode;
    QSettings st("alel12","modular_dashboard");
    st.setValue(QString("ui/volume/sidebar/width_mode/%1").arg(currency), sidebarWidthMode); st.sync();
    if (modeView=="speedometer") update();
}

void DynamicSpeedometerCharts::setSidebarOutline(bool enabled) {
    if (sidebarOutline != enabled) sidebarOutline = enabled;
    QSettings st("alel12","modular_dashboard");
    st.setValue(QString("ui/volume/sidebar/outline/%1").arg(currency), sidebarOutline); st.sync();
    if (modeView=="speedometer") update();
}

void DynamicSpeedometerCharts::setSidebarBrightnessPct(int pct) {
    pct = std::clamp(pct, 50, 150);
    if (sidebarBrightnessPct != pct) sidebarBrightnessPct = pct;
    QSettings st("alel12","modular_dashboard");
    st.setValue(QString("ui/volume/sidebar/brightness/%1").arg(currency), sidebarBrightnessPct); st.sync();
    if (modeView=="speedometer") update();
}

void DynamicSpeedometerCharts::setRSIEnabled(bool enabled) {
    if (showRSI != enabled) showRSI = enabled;
    QSettings st("alel12","modular_dashboard");
    st.setValue(QString("ui/indicators/rsi/%1").arg(currency), showRSI); st.sync();
    updateChartSeries();
}

void DynamicSpeedometerCharts::setMACDEnabled(bool enabled) {
    if (showMACD != enabled) showMACD = enabled;
    QSettings st("alel12","modular_dashboard");
    st.setValue(QString("ui/indicators/macd/%1").arg(currency), showMACD); st.sync();
    updateChartSeries();
}

void DynamicSpeedometerCharts::setBBEnabled(bool enabled) {
    if (showBB != enabled) showBB = enabled;
    QSettings st("alel12","modular_dashboard");
    st.setValue(QString("ui/indicators/bb/%1").arg(currency), showBB); st.sync();
    updateChartSeries();
}

void DynamicSpeedometerCharts::setAnomalyEnabled(bool enabled) {
    if (showAnomalyBadge != enabled) showAnomalyBadge = enabled;
    QSettings st("alel12","modular_dashboard");
    st.setValue(QString("ui/anomaly/enabled/%1").arg(currency), showAnomalyBadge); st.sync();
    if (modeView=="speedometer") update();
}

void DynamicSpeedometerCharts::setAnomalyModeByKey(const QString& key) {
    QString k = key.toLower();
    AnomalyMode m = AnomalyMode::Off;
    if (k=="rsi") m = AnomalyMode::RSIOverboughtOversold;
    else if (k=="macd") m = AnomalyMode::MACDCross;
    else if (k=="bb") m = AnomalyMode::BollingerBreakout;
    else if (k=="zscore") m = AnomalyMode::ZScore;
    else if (k=="vol") m = AnomalyMode::VolSpike;
    else if (k=="comp") m = AnomalyMode::Composite;
    else if (k=="rsi_div") m = AnomalyMode::RSIDivergence;
    else if (k=="macd_hist") m = AnomalyMode::MACDHistSurge;
    else if (k=="clustered_z") m = AnomalyMode::ClusteredZ;
    else if (k=="vol_regime") m = AnomalyMode::VolRegimeShift;
    if (anomalyMode != m) anomalyMode = m;
    QSettings st("alel12","modular_dashboard");
    st.setValue(QString("ui/anomaly/mode/%1").arg(currency), k); st.sync();
    if (modeView=="speedometer") update();
}

void DynamicSpeedometerCharts::setOverlayVolatility(bool enabled) {
    if (showVolOverlay != enabled) showVolOverlay = enabled;
    QSettings st("alel12","modular_dashboard");
    st.setValue(QString("ui/overlays/vol/%1").arg(currency), showVolOverlay); st.sync();
    if (modeView=="speedometer") update();
}

void DynamicSpeedometerCharts::setOverlayChange(bool enabled) {
    if (showChangeOverlay != enabled) showChangeOverlay = enabled;
    QSettings st("alel12","modular_dashboard");
    st.setValue(QString("ui/overlays/chg/%1").arg(currency), showChangeOverlay); st.sync();
    if (modeView=="speedometer") update();
}

void DynamicSpeedometerCharts::setPythonScalingParams(double initSpanPct, double minCompress, double maxCompress, double minWidthPct) {
    pyInitSpanPct = std::clamp(initSpanPct, 1e-6, 0.5); // 0.000001..0.5 (50%)
    pyMinCompress = std::max(1.0, minCompress);         // >= 1.0
    pyMaxCompress = std::min(1.0, std::max(0.0, maxCompress)); // <= 1.0
    pyMinWidthPct = std::clamp(minWidthPct, 1e-8, 1.0);
    // Recompute bounds using latest price
    if (!history.empty()) updateBounds(history.back().value);
}

void DynamicSpeedometerCharts::applyPerformance(int animMs, int renderMs, int cacheMs, int volWindowSize, int maxPts, int rawCacheSize) {
    animation->setDuration(animMs); renderTimer->setInterval(std::max(1, renderMs)); cacheUpdateTimer->setInterval(std::max(10, cacheMs));
    volatilityWindow = volWindowSize; maxPoints = maxPts; setRawCacheSize(rawCacheSize); cacheChartData(); update();
}

void DynamicSpeedometerCharts::setRawCacheSize(int sz) {
    cacheSize = std::max(100, sz);
    while (history.size() > static_cast<size_t>(cacheSize)) history.pop_front();
    while (btcRatioHistory.size() > static_cast<size_t>(cacheSize)) btcRatioHistory.pop_front();
}

void DynamicSpeedometerCharts::updateData(double price, double timestamp, double btc) {
    HistoryPoint hp; hp.ts = timestamp; hp.value = price; hp.source = currentSourceKind; hp.provider = property("providerName").toString(); hp.market = property("marketName").toString(); hp.seq = ++seqCounter;
    history.push_back(hp);
    if (history.size()>static_cast<size_t>(cacheSize)) history.pop_front();
    const double cutoff = timestamp - historyRetentionSec;
    while (!history.empty() && history.front().ts < cutoff) history.pop_front();
    // BTC ratio synthetic
    HistoryPoint br = hp; br.value = (currency=="BTC") ? 1.0 : (btc>0 ? price/btc : 0.0);
    if (currency=="BTC" || btc>0) btcRatioHistory.push_back(br);
    if (btcRatioHistory.size()>static_cast<size_t>(cacheSize)) btcRatioHistory.pop_front();
    while (!btcRatioHistory.empty() && btcRatioHistory.front().ts < cutoff) btcRatioHistory.pop_front();
    updateVolatility(); updateBounds(price);
    double scaled=50; if (cachedMinVal && cachedMaxVal && cachedMaxVal.value() > cachedMinVal.value()) {
        double t = (price-cachedMinVal.value())/(cachedMaxVal.value()-cachedMinVal.value());
        t = std::clamp(t, 0.0, 1.0);
        double mid = 0.5;
        double amplified = mid + (t - mid) * std::max(1.0, needleGain);
        amplified = std::clamp(amplified, 0.0, 1.0);
        scaled = amplified * 100.0;
    }
    animation->stop(); animation->setStartValue(_value); animation->setEndValue(scaled); animation->start();
    dataNeedsRedraw = true;
}

void DynamicSpeedometerCharts::setCurrencyName(const QString& name) { 
    currency = name; 
    // Reload overlay prefs for this currency
    QSettings st("alel12", "modular_dashboard");
    showVolOverlay = st.value(QString("ui/overlays/vol/%1").arg(currency), showVolOverlay).toBool();
    showChangeOverlay = st.value(QString("ui/overlays/chg/%1").arg(currency), showChangeOverlay).toBool();
    {
        QString vv = st.value(QString("ui/volume/vis/%1").arg(currency), "off").toString().toLower();
        if (vv=="bar") volVis = VolumeVis::Bar;
        else if (vv=="needle") volVis = VolumeVis::Needle;
        else if (vv=="sidebar") volVis = VolumeVis::Sidebar;
        else if (vv=="fuel") volVis = VolumeVis::Fuel;
        else volVis = VolumeVis::Off;
    }
    // Sidebar tuning reload
    sidebarWidthMode = st.value(QString("ui/volume/sidebar/width_mode/%1").arg(currency), sidebarWidthMode).toInt();
    sidebarOutline = st.value(QString("ui/volume/sidebar/outline/%1").arg(currency), sidebarOutline).toBool();
    sidebarBrightnessPct = st.value(QString("ui/volume/sidebar/brightness/%1").arg(currency), sidebarBrightnessPct).toInt();
    sidebarBrightnessPct = std::clamp(sidebarBrightnessPct, 50, 150);
    showRSI = st.value(QString("ui/indicators/rsi/%1").arg(currency), showRSI).toBool();
    showMACD = st.value(QString("ui/indicators/macd/%1").arg(currency), showMACD).toBool();
    showBB = st.value(QString("ui/indicators/bb/%1").arg(currency), showBB).toBool();
    showAnomalyBadge = st.value(QString("ui/anomaly/enabled/%1").arg(currency), showAnomalyBadge).toBool();
    {
        QString am = st.value(QString("ui/anomaly/mode/%1").arg(currency), "off").toString().toLower();
        if (am=="rsi") anomalyMode = AnomalyMode::RSIOverboughtOversold;
        else if (am=="macd") anomalyMode = AnomalyMode::MACDCross;
        else if (am=="bb") anomalyMode = AnomalyMode::BollingerBreakout;
        else if (am=="zscore") anomalyMode = AnomalyMode::ZScore;
        else if (am=="vol") anomalyMode = AnomalyMode::VolSpike;
        else if (am=="comp") anomalyMode = AnomalyMode::Composite;
        else anomalyMode = AnomalyMode::Off;
    }
    {
        QString fs = st.value(QString("ui/frame/style/%1").arg(currency), "none").toString().toLower();
        if (fs=="minimal") frameStyle = FrameStyle::Minimal;
        else if (fs=="dashed") frameStyle = FrameStyle::Dashed;
        else if (fs=="glow") frameStyle = FrameStyle::Glow;
        else frameStyle = FrameStyle::None;
    }
    // Reload per-widget sensitivity prefs for the new currency
    loadSensitivityPrefs();
    if (modeView!="speedometer") updateChartSeries(); else update(); 
}

void DynamicSpeedometerCharts::paintEvent(QPaintEvent*) {
    // Не блокируем полностью перерисовку, чтобы оверлей мог рисоваться поверх.
    // Но если мы в режиме спидометра и активен переход, избегаем перерисовки циферблата,
    // чтобы не мигало под оверлеем.
    if (modeView=="speedometer" && !transitionActive) {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); drawSpeedometer(p);
    }
}

void DynamicSpeedometerCharts::resizeEvent(QResizeEvent*) { if (chartView) chartView->setGeometry(rect()); }

void DynamicSpeedometerCharts::mousePressEvent(QMouseEvent* e) {
    if (e->button()==Qt::LeftButton) {
        if (modeView=="speedometer") animateViewSwitch("line_chart");
        else if (modeView=="line_chart") animateViewSwitch("btc_ratio");
        else animateViewSwitch("speedometer");
    }
}

void DynamicSpeedometerCharts::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    // Per-widget style submenu
    QMenu* styleMenu = menu.addMenu("Style");
    auto addStyle = [&](const QString& name){ QAction* a = styleMenu->addAction(name); a->setCheckable(true); return a; };
    QAction* stClassic = addStyle("Classic");
    QAction* stNeon    = addStyle("NeonGlow");
    QAction* stMinimal = addStyle("Minimal");
    QAction* stModern  = addStyle("KiloCode Modern Ticks");
    QAction* stCircle  = addStyle("Classic Pro");
    QAction* stGauge   = addStyle("Gauge");
    QAction* stRing    = addStyle("Modern Scale");
    QAction* stSegBar  = addStyle("Segment Bar");
    QAction* stDualArc = addStyle("Dual Arc");
    auto checkByCurrent = [&](){
        stClassic->setChecked(style==SpeedometerStyle::Classic);
        stNeon->setChecked(style==SpeedometerStyle::NeonGlow);
        stMinimal->setChecked(style==SpeedometerStyle::Minimal);
        stModern->setChecked(style==SpeedometerStyle::ModernTicks);
        stCircle->setChecked(style==SpeedometerStyle::Circle);
        stGauge->setChecked(style==SpeedometerStyle::Gauge);
        stRing->setChecked(style==SpeedometerStyle::Ring);
        stSegBar->setChecked(style==SpeedometerStyle::SegmentBar);
        stDualArc->setChecked(style==SpeedometerStyle::DualArc);
    }; checkByCurrent();
    auto applyStyle = [&](SpeedometerStyle s, const QString& name){ setSpeedometerStyle(s); emit styleSelected(currency, name); };
    QObject::connect(stClassic, &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::Classic, "Classic"); });
    QObject::connect(stNeon,    &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::NeonGlow, "NeonGlow"); });
    QObject::connect(stMinimal, &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::Minimal, "Minimal"); });
    QObject::connect(stModern,  &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::ModernTicks, "KiloCode Modern Ticks"); });
    QObject::connect(stCircle,  &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::Circle, "Classic Pro"); });
    QObject::connect(stGauge,   &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::Gauge, "Gauge"); });
    QObject::connect(stRing,    &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::Ring, "Modern Scale"); });
    QObject::connect(stSegBar,  &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::SegmentBar, "Segment Bar"); });
    QObject::connect(stDualArc, &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::DualArc, "Dual Arc"); });

    // Chart options submenu
    QMenu* chartMenu = menu.addMenu("Chart Options");
    QAction* actGrid = chartMenu->addAction("Show grid");
    actGrid->setCheckable(true); actGrid->setChecked(showGrid);
    QAction* actAxis = chartMenu->addAction("Show axis labels");
    actAxis->setCheckable(true); actAxis->setChecked(showAxisLabels);
    connect(actGrid, &QAction::toggled, this, [this](bool on){ setChartOptions(on, showAxisLabels); });
    connect(actAxis, &QAction::toggled, this, [this](bool on){ setChartOptions(showGrid, on); });

    // Volume visualization submenu
    QMenu* volMenu = menu.addMenu("Volume");
    QActionGroup* volGrp = new QActionGroup(volMenu); volGrp->setExclusive(true);
    auto addVol = [&](const QString& label, VolumeVis vv){ QAction* a = volMenu->addAction(label); a->setCheckable(true); a->setActionGroup(volGrp); a->setChecked(volVis==vv); a->setData(int(vv)); return a; };
    QAction* volOff = addVol("Off", VolumeVis::Off);
    QAction* volBar = addVol("Bar under arc", VolumeVis::Bar);
    QAction* volNeedle = addVol("Second needle", VolumeVis::Needle);
    QAction* volSidebar = addVol("Right sidebar bar", VolumeVis::Sidebar);
    QAction* volFuel = addVol("Fuel gauge needle", VolumeVis::Fuel);

    // Sidebar options submenu
    QMenu* sidebarMenu = volMenu->addMenu("Sidebar options");
    QActionGroup* sbWidthGrp = new QActionGroup(sidebarMenu); sbWidthGrp->setExclusive(true);
    auto addSBw = [&](const QString& label, int mode){ QAction* a = sidebarMenu->addAction(label); a->setCheckable(true); a->setActionGroup(sbWidthGrp); a->setChecked(sidebarWidthMode==mode); a->setData(mode); return a; };
    QAction* sbWAuto = addSBw("Width: Auto", 0);
    QAction* sbWNar  = addSBw("Width: Narrow", 1);
    QAction* sbWMed  = addSBw("Width: Medium", 2);
    QAction* sbWWide = addSBw("Width: Wide", 3);
    QAction* sbOutline = sidebarMenu->addAction("Outline"); sbOutline->setCheckable(true); sbOutline->setChecked(sidebarOutline);
    // Brightness options
    QMenu* sbBright = sidebarMenu->addMenu("Brightness");
    QActionGroup* sbBrightGrp = new QActionGroup(sbBright); sbBrightGrp->setExclusive(true);
    auto addSBb = [&](const QString& label, int pct){ QAction* a = sbBright->addAction(label); a->setCheckable(true); a->setActionGroup(sbBrightGrp); a->setChecked(sidebarBrightnessPct==pct); a->setData(pct); return a; };
    QAction* sbBDim    = addSBb("Dim (75%)", 75);
    QAction* sbBNormal = addSBb("Normal (100%)", 100);
    QAction* sbBBright = addSBb("Bright (125%)", 125);
    QAction* sbBUltra  = addSBb("Ultra (150%)", 150);

    // Computed widgets submenu
    QMenu* compMenu = menu.addMenu("Computed");
    auto addComp = [&](const QString& label, const QString& token){ QAction* a = compMenu->addAction(label); a->setToolTip(buildComputedTooltip(token)); QObject::connect(a, &QAction::triggered, this, [this,token](){ emit requestChangeTicker(currency, token); }); return a; };
    addComp("Average (@AVG)", "@AVG");
    addComp("Alt Average (@ALT_AVG)", "@ALT_AVG");
    addComp("Median (@MEDIAN)", "@MEDIAN");
    addComp("Spread (@SPREAD)", "@SPREAD");
    addComp("Top10 Avg (@TOP10_AVG)", "@TOP10_AVG");
    addComp("Volume Avg (@VOL_AVG)", "@VOL_AVG");
    addComp("Bitcoin Dominance (@BTC_DOM)", "@BTC_DOM");
    // DIFF presets for current currency if it looks like a symbol
    if (!currency.startsWith("@") && !currency.isEmpty()) {
        addComp(QString("Diff vs Binance: %1 Linear").arg(currency), QString("@DIFF:%1:Linear").arg(currency));
        addComp(QString("Diff vs Binance: %1 Spot").arg(currency),   QString("@DIFF:%1:Spot").arg(currency));
        addComp(QString("Z-Score: %1").arg(currency), QString("@Z_SCORE:%1").arg(currency));
    }
    menu.addSeparator();
    // Indicators submenu
    QMenu* indMenu = menu.addMenu("Indicators");
    QAction* indRSI = indMenu->addAction("RSI (14)"); indRSI->setCheckable(true); indRSI->setChecked(showRSI);
    QAction* indMACD = indMenu->addAction("MACD (12,26,9)"); indMACD->setCheckable(true); indMACD->setChecked(showMACD);
    QAction* indBB = indMenu->addAction("Bollinger Bands (20,2)"); indBB->setCheckable(true); indBB->setChecked(showBB);
    // Anomaly alerts submenu
    QMenu* anMenu = menu.addMenu(QString::fromUtf8("Аномалии"));
    QAction* anEnable = anMenu->addAction(QString::fromUtf8("Показывать значок аномалии")); anEnable->setCheckable(true); anEnable->setChecked(showAnomalyBadge);
    QActionGroup* anGrp = new QActionGroup(anMenu); anGrp->setExclusive(true);
    auto addAn = [&](const QString& label, AnomalyMode m){ QAction* a = anMenu->addAction(label); a->setCheckable(true); a->setActionGroup(anGrp); a->setChecked(anomalyMode==m); a->setData(int(m)); return a; };
    QAction* anOff = addAn(QString::fromUtf8("Выкл"), AnomalyMode::Off);
    QAction* anRsi = addAn("RSI OB/OS", AnomalyMode::RSIOverboughtOversold);
    QAction* anMacd = addAn("MACD cross", AnomalyMode::MACDCross);
    QAction* anBB = addAn("BB breakout", AnomalyMode::BollingerBreakout);
    QAction* anZS = addAn("Z-Score", AnomalyMode::ZScore);
    QAction* anVol = addAn(QString::fromUtf8("Всплеск волатильности"), AnomalyMode::VolSpike);
    QAction* anComp = addAn(QString::fromUtf8("Композитный"), AnomalyMode::Composite);
    anMenu->addSeparator();
    QAction* anRsiDiv = addAn(QString::fromUtf8("RSI дивергенция"), AnomalyMode::RSIDivergence);
    QAction* anMacdHist = addAn(QString::fromUtf8("MACD histogram surge"), AnomalyMode::MACDHistSurge);
    QAction* anClustZ = addAn(QString::fromUtf8("Кластерный Z-Score"), AnomalyMode::ClusteredZ);
    QAction* anVolReg = addAn(QString::fromUtf8("Calm↔Volatile смена режима"), AnomalyMode::VolRegimeShift);
    // Transitions submenu
    QMenu* transMenu = menu.addMenu("Transitions");
    QAction* transEnable = transMenu->addAction("Enable animations");
    transEnable->setCheckable(true); transEnable->setChecked(transitionsEnabled);
    QActionGroup* grp = new QActionGroup(transMenu); grp->setExclusive(true);
    auto addTrans = [&](const QString& label, TransitionOverlay::Type t, bool checked){
        QAction* a = transMenu->addAction(label); a->setCheckable(true); a->setActionGroup(grp); a->setChecked(checked); a->setData(int(t)); return a; };
    QAction* tNone = addTrans("None", TransitionOverlay::None, transitionType==TransitionOverlay::None);
    QAction* tFlip = addTrans("Flip", TransitionOverlay::Flip, transitionType==TransitionOverlay::Flip);
    QAction* tSlide = addTrans("Slide", TransitionOverlay::Slide, transitionType==TransitionOverlay::Slide);
    QAction* tCross = addTrans("Crossfade", TransitionOverlay::Crossfade, transitionType==TransitionOverlay::Crossfade);
    QAction* tZoom = addTrans("Zoom+Blur", TransitionOverlay::ZoomBlur, transitionType==TransitionOverlay::ZoomBlur);
    // Overlay toggles for metrics
    QAction* actVol = menu.addAction("Show volatility overlay"); actVol->setCheckable(true); actVol->setChecked(showVolOverlay);
    QAction* actChg = menu.addAction("Show change overlay"); actChg->setCheckable(true); actChg->setChecked(showChangeOverlay);
    // Widget frame styles (must be created before exec)
    QMenu* frameMenu = menu.addMenu("Widget Frame");
    QActionGroup* frameGrp = new QActionGroup(frameMenu); frameGrp->setExclusive(true);
    auto addFrame = [&](const QString& label, FrameStyle fs){ QAction* a = frameMenu->addAction(label); a->setCheckable(true); a->setActionGroup(frameGrp); a->setChecked(frameStyle==fs); a->setData(int(fs)); return a; };
    QAction* frNone = addFrame("None", FrameStyle::None);
    QAction* frMin  = addFrame("Minimal", FrameStyle::Minimal);
    QAction* frDash = addFrame("Dashed", FrameStyle::Dashed);
    QAction* frGlow = addFrame("Glow", FrameStyle::Glow);
    menu.addSeparator();
    // New sensitivity submenu (keep old menus intact)
    QMenu* sensMenu = menu.addMenu(QString::fromUtf8("Чувствительность"));
    // Needle gain presets
    QMenu* gainMenu = sensMenu->addMenu(QString::fromUtf8("Усиление стрелки"));
    QActionGroup* gainGrp = new QActionGroup(gainMenu); gainGrp->setExclusive(true);
    auto addGain = [&](const QString& label, double g){ QAction* a = gainMenu->addAction(label); a->setCheckable(true); a->setActionGroup(gainGrp); a->setChecked(std::abs(needleGain - g) < 1e-9); a->setData(g); return a; };
    QAction* g1 = addGain("1x", 1.0);
    QAction* g15 = addGain("1.5x", 1.5);
    QAction* g2 = addGain("2x", 2.0);
    QAction* g3 = addGain("3x", 3.0);
    QAction* g5 = addGain("5x", 5.0);
    // Auto-collapse controls
    QAction* acEnable = sensMenu->addAction(QString::fromUtf8("Автосхлопывание диапазона")); acEnable->setCheckable(true); acEnable->setChecked(autoCollapseEnabled);
    QMenu* acMenu = sensMenu->addMenu(QString::fromUtf8("Скорость схлопывания"));
    QActionGroup* acGrp = new QActionGroup(acMenu); acGrp->setExclusive(true);
    auto addAC = [&](const QString& label, double f){ QAction* a = acMenu->addAction(label); a->setCheckable(true); a->setActionGroup(acGrp); a->setChecked(std::abs(autoCollapseFactor - f) < 1e-9); a->setData(f); return a; };
    QAction* acSoft  = addAC(QString::fromUtf8("Мягко"), 0.999);
    QAction* acNorm  = addAC(QString::fromUtf8("Норма"), 0.995);
    QAction* acFast  = addAC(QString::fromUtf8("Быстро"), 0.990);
    // Spike expand
    QAction* spEnable = sensMenu->addAction(QString::fromUtf8("Расширять при всплеске")); spEnable->setCheckable(true); spEnable->setChecked(spikeExpandEnabled);
    QMenu* spMenu = sensMenu->addMenu(QString::fromUtf8("Порог всплеска"));
    QActionGroup* spGrp = new QActionGroup(spMenu); spGrp->setExclusive(true);
    auto addSP = [&](const QString& label, double thr){ QAction* a = spMenu->addAction(label); a->setCheckable(true); a->setActionGroup(spGrp); a->setChecked(std::abs(spikeExpandThreshold - thr) < 1e-12); a->setData(thr); return a; };
    QAction* sp04 = addSP("0.4%", 0.004);
    QAction* sp08 = addSP("0.8%", 0.008);
    QAction* sp15 = addSP("1.5%", 0.015);
    QAction* sp30 = addSP("3%", 0.03);
    // Min range option
    QMenu* mrMenu = sensMenu->addMenu(QString::fromUtf8("Мин. ширина окна"));
    QActionGroup* mrGrp = new QActionGroup(mrMenu); mrGrp->setExclusive(true);
    auto addMR = [&](const QString& label, double pct){ QAction* a = mrMenu->addAction(label); a->setCheckable(true); a->setActionGroup(mrGrp); a->setChecked(std::abs(minRangePctOfPrice - pct) < 1e-12); a->setData(pct); return a; };
    QAction* mr005 = addMR("0.05%", 0.0005);
    QAction* mr01  = addMR("0.1%", 0.001);
    QAction* mr02  = addMR("0.2%", 0.002);
    QAction* mr05  = addMR("0.5%", 0.005);
    QAction* mr10  = addMR("1%", 0.01);
    menu.addSeparator();
    QAction* renameAct = menu.addAction("Rename ticker...");
    connect(renameAct, &QAction::triggered, this, [this](){ emit requestRename(currency); });
    QAction* chosen = menu.exec(e->globalPos());
    if (chosen==actVol) { 
        showVolOverlay = !showVolOverlay; 
        QSettings st("alel12", "modular_dashboard"); st.setValue(QString("ui/overlays/vol/%1").arg(currency), showVolOverlay); st.sync();
        update(); 
    }
    else if (chosen==actChg) { 
        showChangeOverlay = !showChangeOverlay; 
        QSettings st("alel12", "modular_dashboard"); st.setValue(QString("ui/overlays/chg/%1").arg(currency), showChangeOverlay); st.sync();
        update(); 
    }
    else if (chosen==indRSI) {
        showRSI = !showRSI; QSettings st("alel12","modular_dashboard"); st.setValue(QString("ui/indicators/rsi/%1").arg(currency), showRSI); st.sync(); updateChartSeries();
    }
    else if (chosen==indMACD) {
        showMACD = !showMACD; QSettings st("alel12","modular_dashboard"); st.setValue(QString("ui/indicators/macd/%1").arg(currency), showMACD); st.sync(); updateChartSeries();
    }
    else if (chosen==indBB) {
        showBB = !showBB; QSettings st("alel12","modular_dashboard"); st.setValue(QString("ui/indicators/bb/%1").arg(currency), showBB); st.sync(); updateChartSeries();
    }
    else if (chosen==anEnable) {
        showAnomalyBadge = !showAnomalyBadge; QSettings st("alel12","modular_dashboard"); st.setValue(QString("ui/anomaly/enabled/%1").arg(currency), showAnomalyBadge); st.sync(); update();
    }
    else if (chosen && chosen->actionGroup()==anGrp) {
        anomalyMode = static_cast<AnomalyMode>(chosen->data().toInt());
        QSettings st("alel12","modular_dashboard");
        QString name = "off";
        switch (anomalyMode) {
            case AnomalyMode::RSIOverboughtOversold: name="rsi"; break;
            case AnomalyMode::MACDCross: name="macd"; break;
            case AnomalyMode::BollingerBreakout: name="bb"; break;
            case AnomalyMode::ZScore: name="zscore"; break;
            case AnomalyMode::VolSpike: name="vol"; break;
            case AnomalyMode::Composite: name="comp"; break;
            case AnomalyMode::RSIDivergence: name="rsi_div"; break;
            case AnomalyMode::MACDHistSurge: name="macd_hist"; break;
            case AnomalyMode::ClusteredZ: name="clustered_z"; break;
            case AnomalyMode::VolRegimeShift: name="vol_regime"; break;
            default: name="off"; break; }
        st.setValue(QString("ui/anomaly/mode/%1").arg(currency), name); st.sync(); update();
    }
    else if (chosen==transEnable) {
        transitionsEnabled = !transitionsEnabled; QSettings st("alel12","modular_dashboard"); st.setValue("ui/transitions/enabled", transitionsEnabled); st.sync();
    }
    else if (chosen==tNone || chosen==tFlip || chosen==tSlide || chosen==tCross || chosen==tZoom) {
        TransitionOverlay::Type old = transitionType;
        if (chosen==tNone) transitionType = TransitionOverlay::None;
        else if (chosen==tFlip) transitionType = TransitionOverlay::Flip;
        else if (chosen==tSlide) transitionType = TransitionOverlay::Slide;
        else if (chosen==tCross) transitionType = TransitionOverlay::Crossfade;
        else transitionType = TransitionOverlay::ZoomBlur;
        if (transitionType != old) {
            QSettings st("alel12","modular_dashboard");
            QString name = (transitionType==TransitionOverlay::None? "none" : transitionType==TransitionOverlay::Flip? "flip" : transitionType==TransitionOverlay::Slide? "slide" : transitionType==TransitionOverlay::Crossfade? "crossfade" : "zoomblur");
            st.setValue("ui/transitions/type", name); st.sync();
        }
    }
    else if (chosen && chosen->actionGroup()==volGrp) {
        VolumeVis prev = volVis;
        VolumeVis sel = static_cast<VolumeVis>(chosen->data().toInt());
        if (sel != prev) {
            volVis = sel;
            QSettings st("alel12","modular_dashboard");
            QString name;
            switch (sel) {
                case VolumeVis::Off: name = "off"; break;
                case VolumeVis::Bar: name = "bar"; break;
                case VolumeVis::Needle: name = "needle"; break;
                case VolumeVis::Sidebar: name = "sidebar"; break;
                case VolumeVis::Fuel: name = "fuel"; break;
            }
            st.setValue(QString("ui/volume/vis/%1").arg(currency), name);
            st.sync();
            update();
        }
    }
    else if (chosen && chosen->actionGroup()==sbWidthGrp) {
        int mode = chosen->data().toInt();
        if (mode != sidebarWidthMode) {
            sidebarWidthMode = mode;
            QSettings st("alel12","modular_dashboard");
            st.setValue(QString("ui/volume/sidebar/width_mode/%1").arg(currency), sidebarWidthMode); st.sync();
            update();
        }
    }
    else if (chosen==sbOutline) {
        sidebarOutline = !sidebarOutline;
        QSettings st("alel12","modular_dashboard");
        st.setValue(QString("ui/volume/sidebar/outline/%1").arg(currency), sidebarOutline); st.sync();
        update();
    }
    else if (chosen && chosen->actionGroup()==sbBrightGrp) {
        int pct = chosen->data().toInt();
        if (pct != sidebarBrightnessPct) {
            sidebarBrightnessPct = std::clamp(pct, 50, 150);
            QSettings st("alel12","modular_dashboard");
            st.setValue(QString("ui/volume/sidebar/brightness/%1").arg(currency), sidebarBrightnessPct); st.sync();
            update();
        }
    }
    else if (chosen && chosen->actionGroup()==frameGrp) {
        FrameStyle sel = static_cast<FrameStyle>(chosen->data().toInt());
        if (sel != frameStyle) {
            frameStyle = sel;
            QSettings st("alel12","modular_dashboard");
            QString name = (sel==FrameStyle::None? "none" : sel==FrameStyle::Minimal? "minimal" : sel==FrameStyle::Dashed? "dashed" : "glow");
            st.setValue(QString("ui/frame/style/%1").arg(currency), name); st.sync();
            update();
        }
    }
    // Sensitivity handlers
    if (chosen && chosen->actionGroup()==gainGrp) {
        needleGain = chosen->data().toDouble(); saveSensitivityPrefs(); if (modeView=="speedometer") update();
    } else if (chosen==acEnable) {
        autoCollapseEnabled = !autoCollapseEnabled; saveSensitivityPrefs();
    } else if (chosen && chosen->actionGroup()==acGrp) {
        autoCollapseFactor = chosen->data().toDouble(); saveSensitivityPrefs();
    } else if (chosen==spEnable) {
        spikeExpandEnabled = !spikeExpandEnabled; saveSensitivityPrefs();
    } else if (chosen && chosen->actionGroup()==spGrp) {
        spikeExpandThreshold = chosen->data().toDouble(); saveSensitivityPrefs();
    } else if (chosen && chosen->actionGroup()==mrGrp) {
        minRangePctOfPrice = chosen->data().toDouble(); saveSensitivityPrefs();
    }
}

void DynamicSpeedometerCharts::onRenderTimeout() { if (dataNeedsRedraw && (modeView != "speedometer")) { dataNeedsRedraw=false; updateChartSeries(); } }

void DynamicSpeedometerCharts::setTimeScale(const QString& scale) { if (timeScales.contains(scale)) { currentScale=scale; cacheChartData(); updateChartSeries(); } }

void DynamicSpeedometerCharts::setModeView(const QString& mv) {
    modeView = mv; bool charts = (modeView!="speedometer"); chartView->setVisible(charts); if (charts) updateChartSeries(); else update();
}

void DynamicSpeedometerCharts::setVolumeVisByKey(const QString& key) {
    QString k = key.toLower(); VolumeVis vv = VolumeVis::Off;
    if (k=="bar") vv = VolumeVis::Bar;
    else if (k=="needle") vv = VolumeVis::Needle;
    else if (k=="sidebar") vv = VolumeVis::Sidebar;
    else if (k=="fuel") vv = VolumeVis::Fuel;
    if (volVis != vv) volVis = vv;
    QSettings st("alel12","modular_dashboard"); st.setValue(QString("ui/volume/vis/%1").arg(currency), k); st.sync();
    if (modeView=="speedometer") update();
}

void DynamicSpeedometerCharts::animateViewSwitch(const QString& nextMode) {
    static bool logEnabled = qEnvironmentVariableIsSet("DASH_TRANSITION_LOG");
    if (logEnabled) qDebug() << "[TRANSITION] request" << currency << "from" << modeView << "to" << nextMode << "enabled=" << transitionsEnabled << "type=" << int(transitionType) << "active=" << transitionActive;
    if (!transitionsEnabled || transitionType==TransitionOverlay::None) {
        if (logEnabled) qDebug() << "[TRANSITION] disabled -> direct switch";
        setModeView(nextMode);
        return;
    }
    if (size().isEmpty()) { setModeView(nextMode); return; }
    if (transitionActive) {
        if (logEnabled) qDebug() << "[TRANSITION] already active -> ignore reentry";
        return;
    }

    // Единственный активный оверлей
    if (activeOverlay) {
        if (logEnabled) qDebug() << "[TRANSITION] existing overlay still alive -> force cleanup";
        activeOverlay->deleteLater();
        activeOverlay = nullptr;
    }

    // Снимок текущего состояния
    QPixmap from = this->grab();
    if (logEnabled) qDebug() << "[TRANSITION] captured FROM pixmap size=" << from.size();

    // Переключаем режим и даём макету/чартам отрисоваться
    setModeView(nextMode);
    if (logEnabled) qDebug() << "[TRANSITION] mode switched, scheduling TO snapshot";
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    this->repaint();

    // Делаем небольшой асинхронный снэпшот "to", чтобы точно успела отрисоваться цель
    transitionActive = true;
    if (logEnabled) qDebug() << "[TRANSITION] active=true timers paused";
    bool prevChartUpdates = chartView->updatesEnabled();
    chartView->setUpdatesEnabled(false);
    // Пауза внутренних таймеров/анимаций на время перехода
    bool renderWasActive = renderTimer && renderTimer->isActive();
    bool cacheWasActive  = cacheUpdateTimer && cacheUpdateTimer->isActive();
    if (renderWasActive) renderTimer->stop();
    if (cacheWasActive)  cacheUpdateTimer->stop();
    if (animation) animation->stop();

    QPointer<DynamicSpeedometerCharts> self(this);
    QTimer::singleShot(16, this, [this, self, from, prevChartUpdates, renderWasActive, cacheWasActive]() mutable {
        if (!self) return;
        // Последний шанс обработать отложенные события
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QPixmap to = this->grab();
        if (qEnvironmentVariableIsSet("DASH_TRANSITION_LOG")) qDebug() << "[TRANSITION] captured TO pixmap size=" << to.size() << "progress start";
        // Создаём и запускаем оверлей
        activeOverlay = new TransitionOverlay(this, from, to, transitionType, 380);
        connect(activeOverlay, &QObject::destroyed, this, [this, prevChartUpdates, renderWasActive, cacheWasActive]{
            transitionActive = false;
            chartView->setUpdatesEnabled(prevChartUpdates);
            if (renderWasActive && renderTimer) renderTimer->start();
            if (cacheWasActive && cacheUpdateTimer) cacheUpdateTimer->start();
            activeOverlay = nullptr;
            if (qEnvironmentVariableIsSet("DASH_TRANSITION_LOG")) qDebug() << "[TRANSITION] finished cleanup";
            update();
        });
        activeOverlay->start();
    });
}

void DynamicSpeedometerCharts::cacheChartData() {
    cachedProcessedHistory = processHistory(false);
    cachedProcessedBtcRatio = processHistory(true);
    dataNeedsRedraw = true;
    // Also evaluate anomalies for speedometer view
    if (showAnomalyBadge) {
        bool oldActive = anomalyActive; QString oldLabel = anomalyLabel;
        anomalyActive = false; anomalyLabel.clear();
        const auto& values = cachedProcessedHistory;
        if (!values.empty()) {
            switch (anomalyMode) {
                case AnomalyMode::RSIOverboughtOversold: {
                    auto rsi = computeRSI(values, 14);
                    if (!rsi.empty()) { double last=rsi.back(); if (last>=70.0) { anomalyActive=true; anomalyLabel="RSI↑"; } else if (last<=30.0) { anomalyActive=true; anomalyLabel="RSI↓"; } }
                    break; }
                case AnomalyMode::MACDCross: {
                    auto m = computeMACD(values);
                    if (m.macd.size()>1 && m.signal.size()>1) {
                        double pd = m.macd[m.macd.size()-2]-m.signal[m.signal.size()-2];
                        double ld = m.macd.back()-m.signal.back();
                        if (pd<=0 && ld>0) { anomalyActive=true; anomalyLabel="MACD↑"; }
                        else if (pd>=0 && ld<0) { anomalyActive=true; anomalyLabel="MACD↓"; }
                    }
                    break; }
                case AnomalyMode::BollingerBreakout: {
                    auto bb = computeBollinger(values, 20, 2.0);
                    if (!bb.upper.empty()) { double last=values.back(); if (last>bb.upper.back()) { anomalyActive=true; anomalyLabel="BB↑"; } else if (last<bb.lower.back()) { anomalyActive=true; anomalyLabel="BB↓"; } }
                    break; }
                case AnomalyMode::ZScore: {
                    double zs = computeZScore(values, std::min<int>(50,(int)values.size())); if (std::abs(zs)>=2.0) { anomalyActive=true; anomalyLabel = (zs>0? "Z↑" : "Z↓"); }
                    break; }
                case AnomalyMode::VolSpike: {
                    int N = std::min<int>(50, (int)values.size()-1);
                    if (N>10) {
                        std::vector<double> rets; rets.reserve(N);
                        for (int i=(int)values.size()-N; i<(int)values.size(); ++i) { if (i==0) continue; double prev=values[size_t(i-1)], cur=values[size_t(i)]; rets.push_back(prev? std::abs((cur-prev)/prev) : 0.0); }
                        if (rets.size()>5) { std::vector<double> tmp = rets; std::nth_element(tmp.begin(), tmp.begin()+tmp.size()/2, tmp.end()); double med = tmp[tmp.size()/2]; double last = rets.back(); if (last>med*2.5) { anomalyActive=true; anomalyLabel="VOL"; } }
                    }
                    break; }
                case AnomalyMode::Composite: {
                    // Smarter composite: reduce false positives via multi-signal confirmation + z-score/vol gating
                    QString lab; int votes = 0;
                    auto rsi=computeRSI(values,14);
                    bool rsiExtreme = (!rsi.empty() && (rsi.back()>=75.0 || rsi.back()<=25.0));
                    if (rsiExtreme) { ++votes; lab = lab.isEmpty()? "RSI" : lab+"+RSI"; }
                    auto m=computeMACD(values);
                    bool macdCross=false; if (m.macd.size()>1 && m.signal.size()>1) {
                        double pd=m.macd[m.macd.size()-2]-m.signal[m.signal.size()-2];
                        double ld=m.macd.back()-m.signal.back();
                        macdCross = ((pd<=0&&ld>0)||(pd>=0&&ld<0));
                    }
                    if (macdCross) { ++votes; lab = lab.isEmpty()? "MACD" : lab+"+MACD"; }
                    auto bb=computeBollinger(values,20,2.0);
                    bool bbBreak=false; if (!bb.upper.empty()) { double last=values.back(); bbBreak = (last>bb.upper.back()||last<bb.lower.back()); }
                    if (bbBreak) { ++votes; lab = lab.isEmpty()? "BB" : lab+"+BB"; }
                    // Gating by z-score and recent volatility
                    double zAbs = std::abs(computeZScore(values, std::min<int>(50,(int)values.size())));
                    bool volGate=false; {
                        int N = std::min<int>(40, (int)values.size()-1);
                        if (N>10) {
                            std::vector<double> rets; rets.reserve(N);
                            for (int i=(int)values.size()-N; i<(int)values.size(); ++i) { if (i==0) continue; double prev=values[size_t(i-1)], cur=values[size_t(i)]; rets.push_back(prev? std::abs((cur-prev)/prev) : 0.0); }
                            std::vector<double> tmp = rets; std::nth_element(tmp.begin(), tmp.begin()+tmp.size()/2, tmp.end()); double med = tmp[tmp.size()/2];
                            double last = rets.back(); volGate = (med>1e-9 && last/med >= 1.8);
                        }
                    }
                    bool trigger = (votes>=2) || (votes>=1 && zAbs>=2.0) || (votes>=1 && volGate && zAbs>=1.5);
                    if (trigger) { anomalyActive=true; anomalyLabel=lab; }
                    break; }
                case AnomalyMode::RSIDivergence: {
                    auto rsi = computeRSI(values, 14);
                    auto findPeaks = [](const std::vector<double>& v, bool maxPeaks){ std::vector<int> idx; for (int i=1;i+1<(int)v.size();++i){ if (maxPeaks){ if (v[i]>v[i-1] && v[i]>v[i+1]) idx.push_back(i);} else { if (v[i]<v[i-1] && v[i]<v[i+1]) idx.push_back(i);} } return idx; };
                    if (values.size()>=10 && rsi.size()==values.size()) {
                        auto maxP = findPeaks(values, true); auto maxR = findPeaks(rsi, true);
                        if (maxP.size()>=2 && maxR.size()>=2) {
                            int p2 = maxP.back(), p1 = maxP[maxP.size()-2]; int r2 = maxR.back(), r1 = maxR[maxR.size()-2];
                            if (values[p2] > values[p1] && rsi[r2] < rsi[r1]) { anomalyActive=true; anomalyLabel = "DIV-"; }
                        }
                        auto minP = findPeaks(values, false); auto minR = findPeaks(rsi, false);
                        if (minP.size()>=2 && minR.size()>=2) {
                            int p2 = minP.back(), p1 = minP[minP.size()-2]; int r2 = minR.back(), r1 = minR[minR.size()-2];
                            if (values[p2] < values[p1] && rsi[r2] > rsi[r1]) { anomalyActive=true; anomalyLabel = "DIV+"; }
                        }
                    }
                    break; }
                case AnomalyMode::MACDHistSurge: {
                    auto m = computeMACD(values);
                    if (m.macd.size()>5 && m.signal.size()>5) {
                        std::vector<double> hist(m.macd.size()); for (size_t i=0;i<hist.size();++i) hist[i]=m.macd[i]-m.signal[i];
                        int N = std::min<int>(50, (int)hist.size()); if (N>10) {
                            std::vector<double> window(hist.end()-N, hist.end()); for (auto& x:window) x=std::abs(x);
                            std::nth_element(window.begin(), window.begin()+window.size()/2, window.end()); double med = window[window.size()/2];
                            double last = std::abs(hist.back()); if (last > med*2.5) { anomalyActive=true; anomalyLabel="HIST"; }
                        }
                    }
                    break; }
                case AnomalyMode::ClusteredZ: {
                    auto rsi = computeRSI(values, 14);
                    auto m = computeMACD(values);
                    auto zVal = [&](const std::vector<double>& v){ return computeZScore(v, std::min<int>(50,(int)v.size())); };
                    double z1 = computeZScore(values, std::min<int>(50,(int)values.size()));
                    double z2 = rsi.empty()? 0.0 : zVal(rsi);
                    double z3 = 0.0; if (!m.macd.empty()) { z3 = zVal(m.macd); }
                    double z = (z1 + z2 + z3) / 3.0; if (std::abs(z) >= 2.2) { anomalyActive=true; anomalyLabel = (z>0? "CZ↑" : "CZ↓"); }
                    break; }
                case AnomalyMode::VolRegimeShift: {
                    int N = std::min<int>(120, (int)values.size()-1);
                    if (N>30) {
                        std::vector<double> rets; rets.reserve(N); for (int i=(int)values.size()-N;i<(int)values.size();++i){ if(i==0) continue; double prev=values[size_t(i-1)], cur=values[size_t(i)]; rets.push_back(prev? std::abs((cur-prev)/prev):0.0); }
                        int half = (int)rets.size()/2; if (half>5) {
                            auto avg = [](const std::vector<double>& a){ double s=0; for(double x:a) s+=x; return s/(double)a.size(); };
                            double avgOld = avg(std::vector<double>(rets.begin(), rets.begin()+half));
                            double avgNew = avg(std::vector<double>(rets.begin()+half, rets.end()));
                            if (avgOld>1e-12 && (avgNew/avgOld >= 1.8)) { anomalyActive=true; anomalyLabel = "VOL↑"; }
                            else if (avgNew<1e-12 && avgOld>1e-12) { anomalyActive=true; anomalyLabel = "VOL↓"; }
                        }
                    }
                    break; }
                default: break;
            }
        }
        if (modeView=="speedometer" && (oldActive!=anomalyActive || oldLabel!=anomalyLabel)) update();
    }
}

QVector<QPair<double,double>> DynamicSpeedometerCharts::historySnapshot() const {
    QVector<QPair<double,double>> out;
    out.reserve(int(history.size()));
    for (const auto& p : history) out.push_back({p.ts, p.value});
    return out;
}

std::vector<double> DynamicSpeedometerCharts::processHistory(bool useBtcRatio) {
    double now = QDateTime::currentMSecsSinceEpoch()/1000.0; double window = timeScales[currentScale]; double cutoff = now - window;
    const auto& src = useBtcRatio ? btcRatioHistory : history; std::vector<std::pair<double,double>> filtered; filtered.reserve(src.size());
    for (const auto& pr : src) if (pr.ts >= cutoff) filtered.push_back({pr.ts, pr.value});
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

void DynamicSpeedometerCharts::updateVolatility() {
    if (history.size()<2) { if (!qFuzzyIsNull(volatility)) { volatility=0.0; emit volatilityChanged(volatility); } return; }
    size_t window = std::min(static_cast<size_t>(volatilityWindow), history.size());
    std::vector<double> prices; prices.reserve(window); for (auto it = history.end()-window; it!=history.end(); ++it) prices.push_back(it->value);
    std::vector<double> rets; rets.reserve(prices.size()-1); for (size_t i=1;i<prices.size();++i) rets.push_back(prices[i-1] ? std::abs((prices[i]-prices[i-1])/prices[i-1]) : 0.0);
    double newVol = rets.empty()? 0.0 : (std::accumulate(rets.begin(), rets.end(), 0.0)/rets.size())*100.0;
    if (!qFuzzyCompare(volatility, newVol)) { volatility = newVol; emit volatilityChanged(volatility); }
}

QString DynamicSpeedometerCharts::buildComputedTooltip(const QString& token) const {
    if (token=="@AVG") return "Average price of the current TOP list.";
    if (token=="@ALT_AVG") return "Average price of altcoins (ex-BTC/ETH) from TOP list.";
    if (token=="@MEDIAN") return "Median price across the current basket.";
    if (token=="@SPREAD") return "Spread = max - min within the basket window.";
    if (token=="@TOP10_AVG") return "Average of TOP-10 coins by market cap from the tracked set.";
    if (token=="@VOL_AVG") return "Average 24h volume across the basket.";
    if (token=="@BTC_DOM") return "Bitcoin dominance estimation within tracked basket.";
    if (token.startsWith("@DIFF:")) return "Difference between Binance and Bybit for the symbol; market fallback applies.";
    if (token.startsWith("@Z_SCORE:")) return "Z-score of the symbol vs its rolling mean/StdDev.";
    return token;
}

void DynamicSpeedometerCharts::updateBounds(double price) {
    if (scalingSettings.mode == ScalingMode::Fixed) {
        cachedMinVal = scalingSettings.fixedMin; cachedMaxVal = scalingSettings.fixedMax; return;
    } else if (scalingSettings.mode == ScalingMode::Manual && cachedMinVal && cachedMaxVal) {
        // Keep existing manual bounds, only update if price exceeds them
        if (price < cachedMinVal.value()) cachedMinVal = price;
        if (price > cachedMaxVal.value()) cachedMaxVal = price; return;
    }

    // Compute min/max from recent history to capture peaks and valleys
    int windowSize = (scalingSettings.windowSize == 0) ? (int)history.size() : std::min<int>(scalingSettings.windowSize, (int)history.size());
    if (windowSize < 2) {
        // Fallback to price-based if not enough history
        if (!cachedMinVal || !cachedMaxVal) { cachedMinVal = price * 0.99; cachedMaxVal = price * 1.01; }
        return;
    }
    double histMin = std::numeric_limits<double>::max();
    double histMax = std::numeric_limits<double>::lowest();
    for (int i = (int)history.size() - windowSize; i < (int)history.size(); ++i) {
        double val = history[size_t(i)].value;
        histMin = std::min(histMin, val);
        histMax = std::max(histMax, val);
    }
    double range = histMax - histMin;
    double padding = range * scalingSettings.paddingPct; // configurable padding
    if (range < 1e-10) padding = std::max(1e-10, histMax * 1e-4);

    if (scalingSettings.mode == ScalingMode::PythonLike) {
        // Set bounds to min/max of recent history with padding
        cachedMinVal = histMin - padding;
        cachedMaxVal = histMax + padding;
        // Optional auto-collapse around current price
        if (autoCollapseEnabled) {
            double span = cachedMaxVal.value() - cachedMinVal.value();
            double center = price;
            span = std::max(span * autoCollapseFactor, std::abs(price) * minRangePctOfPrice);
            cachedMinVal = center - span/2.0;
            cachedMaxVal = center + span/2.0;
        }
        // Ensure minimum width
        double minWidth = std::max(1e-10, std::abs(price) * pyMinWidthPct);
        if (cachedMaxVal.value() - cachedMinVal.value() < minWidth) {
            double center = (cachedMinVal.value() + cachedMaxVal.value()) / 2.0;
            cachedMinVal = center - minWidth / 2.0;
            cachedMaxVal = center + minWidth / 2.0;
        }
        return;
    } else if (scalingSettings.mode == ScalingMode::OldSchoolAdaptive) {
        // Old school adaptive: based only on current price, smooth update
        if (!cachedMinVal || !cachedMaxVal) {
            cachedMinVal = price * 0.99;
            cachedMaxVal = price * 1.01;
            return;
        }
        // Smooth update towards new price-based bounds
        cachedMinVal = cachedMinVal.value() * minFactor + (price * 0.99) * (1.0 - minFactor);
        cachedMaxVal = cachedMaxVal.value() * maxFactor + (price * 1.01) * (1.0 - maxFactor);
        // Ensure price is within bounds
        if (price < cachedMinVal.value()) cachedMinVal = price;
        if (price > cachedMaxVal.value()) cachedMaxVal = price;
        double currentRange = cachedMaxVal.value() - cachedMinVal.value();
        double epsilon = std::max(1e-10, currentRange * 1e-5);
        if (currentRange < epsilon) {
            double center = (cachedMaxVal.value() + cachedMinVal.value()) / 2.0;
            cachedMinVal = center - epsilon / 2.0;
            cachedMaxVal = center + epsilon / 2.0;
        }
        return;
    } else if (scalingSettings.mode == ScalingMode::OldSchoolPythonLike) {
        // Old school python-like: set bounds to current price ± padding
        double padding = price * scalingSettings.paddingPct;
        cachedMinVal = price - padding;
        cachedMaxVal = price + padding;
        if (autoCollapseEnabled) {
            double span = (cachedMaxVal.value() - cachedMinVal.value());
            double minSpan = std::max(1e-8, std::abs(price) * minRangePctOfPrice);
            span = std::max(span * autoCollapseFactor, minSpan);
            cachedMinVal = price - span/2.0;
            cachedMaxVal = price + span/2.0;
        }
        // Ensure minimum width
        double minWidth = std::max(1e-10, std::abs(price) * pyMinWidthPct);
        if (cachedMaxVal.value() - cachedMinVal.value() < minWidth) {
            double center = price;
            cachedMinVal = center - minWidth / 2.0;
            cachedMaxVal = center + minWidth / 2.0;
        }
        return;
    } else if (scalingSettings.mode == ScalingMode::KiloCoderLike) {
        // KiloCoder Like: hierarchical window analysis with historical context
        
        // Update global historical extremes
        if (!globalMin || price < globalMin.value()) globalMin = price;
        if (!globalMax || price > globalMax.value()) globalMax = price;
        
        // Calculate extremes for different windows
        int shortStart = std::max(0, (int)history.size() - shortWindowSize);
        int mediumStart = std::max(0, (int)history.size() - mediumWindowSize);
        int longStart = std::max(0, (int)history.size() - longWindowSize);
        
        // Find min/max for each window
        double shortHistMin = price, shortHistMax = price;
        double mediumHistMin = price, mediumHistMax = price;
        double longHistMin = price, longHistMax = price;
        
        for (int i = shortStart; i < (int)history.size(); i++) {
            double val = history[i].value;
            shortHistMin = std::min(shortHistMin, val);
            shortHistMax = std::max(shortHistMax, val);
        }
        
        for (int i = mediumStart; i < (int)history.size(); i++) {
            double val = history[i].value;
            mediumHistMin = std::min(mediumHistMin, val);
            mediumHistMax = std::max(mediumHistMax, val);
        }
        
        for (int i = longStart; i < (int)history.size(); i++) {
            double val = history[i].value;
            longHistMin = std::min(longHistMin, val);
            longHistMax = std::max(longHistMax, val);
        }
        
        // Update sliding extremes with exponential smoothing
        if (!shortMin) shortMin = shortHistMin; else shortMin = shortMin.value() * (1.0 - shortAlpha) + shortHistMin * shortAlpha;
        if (!shortMax) shortMax = shortHistMax; else shortMax = shortMax.value() * (1.0 - shortAlpha) + shortHistMax * shortAlpha;
        
        if (!mediumMin) mediumMin = mediumHistMin; else mediumMin = mediumMin.value() * (1.0 - mediumAlpha) + mediumHistMin * mediumAlpha;
        if (!mediumMax) mediumMax = mediumHistMax; else mediumMax = mediumMax.value() * (1.0 - mediumAlpha) + mediumHistMax * mediumAlpha;
        
        if (!longMin) longMin = longHistMin; else longMin = longMin.value() * (1.0 - longAlpha) + longHistMin * longAlpha;
        if (!longMax) longMax = longHistMax; else longMax = longMax.value() * (1.0 - longAlpha) + longHistMax * longAlpha;
        
        // Combine values with weights (50% short-term, 30% medium-term, 15% long-term, 5% historical)
        double effectiveMin = 0.5 * shortMin.value() + 0.3 * mediumMin.value() + 0.15 * longMin.value() + 0.05 * globalMin.value();
        double effectiveMax = 0.5 * shortMax.value() + 0.3 * mediumMax.value() + 0.15 * longMax.value() + 0.05 * globalMax.value();
        
        // Calculate volatility and adaptive padding
        double volatility = 0.0;
        if (history.size() > 1) {
            double prev = history[history.size()-2].value;
            volatility = std::abs(price - prev) / std::abs(prev);
        }
        double adaptivePadding = basePadding * price * (1.0 + volatility * 10.0); // amplify volatility effect
        
        // Set bounds
        cachedMinVal = effectiveMin - adaptivePadding;
        cachedMaxVal = effectiveMax + adaptivePadding;
        if (autoCollapseEnabled) {
            double span = cachedMaxVal.value() - cachedMinVal.value();
            double center = price;
            span = std::max(span * autoCollapseFactor, std::abs(price) * minRangePctOfPrice);
            cachedMinVal = center - span/2.0;
            cachedMaxVal = center + span/2.0;
        }
        
        // Ensure current price is within bounds
        if (price < cachedMinVal.value()) cachedMinVal = price * 0.95;
        if (price > cachedMaxVal.value()) cachedMaxVal = price * 1.05;
        
        return;
    }
    // Adaptive mode (default)
    if (!cachedMinVal || !cachedMaxVal) {
        cachedMinVal = histMin - padding;
        cachedMaxVal = histMax + padding;
        return;
    }
    // Smooth update towards new min/max
    cachedMinVal = cachedMinVal.value() * minFactor + (histMin - padding) * (1.0 - minFactor);
    cachedMaxVal = cachedMaxVal.value() * maxFactor + (histMax + padding) * (1.0 - maxFactor);
    // Auto-collapse step every tick
    if (autoCollapseEnabled) {
        double span = cachedMaxVal.value() - cachedMinVal.value();
        double center = price;
        span = std::max(span * autoCollapseFactor, std::abs(price) * minRangePctOfPrice);
        cachedMinVal = center - span/2.0;
        cachedMaxVal = center + span/2.0;
    }
    // Spike expand
    if (spikeExpandEnabled && lastPriceForCollapse > 0.0) {
        double ret = std::abs(price - lastPriceForCollapse) / std::max(1e-12, std::abs(lastPriceForCollapse));
        if (ret >= spikeExpandThreshold) {
            double center = price;
            double span = (cachedMaxVal.value() - cachedMinVal.value()) * autoExpandFactor;
            double minSpan = std::abs(price) * minRangePctOfPrice;
            span = std::max(span, minSpan);
            cachedMinVal = center - span/2.0;
            cachedMaxVal = center + span/2.0;
        }
    }
    lastPriceForCollapse = price;
    // Ensure price is within bounds
    if (price < cachedMinVal.value()) cachedMinVal = price;
    if (price > cachedMaxVal.value()) cachedMaxVal = price;
    double currentRange = cachedMaxVal.value() - cachedMinVal.value();
    double epsilon = std::max(1e-10, currentRange * 1e-5);
    if (currentRange < epsilon) {
        double center = (cachedMaxVal.value() + cachedMinVal.value()) / 2.0;
        cachedMinVal = center - epsilon / 2.0;
        cachedMaxVal = center + epsilon / 2.0;
    }
}

void DynamicSpeedometerCharts::drawSpeedometer(QPainter& painter) {
    int w = width(), h = height(); int size = std::min(w,h) - 20; QRect rect((w-size)/2,(h-size)/2,size,size);
    
    // Use theme background
    painter.fillRect(0,0,w,h,themeColors.background);

    auto drawCommonTexts = [&](QColor textColor = QColor()){
        // Use theme text color if no override
        if (!textColor.isValid()) textColor = themeColors.text;
        painter.setPen(textColor); painter.setFont(QFont("Arial",21, QFont::Bold)); painter.drawText(QRect(0,h/2-50,w,20), Qt::AlignCenter, currency);
    if (!history.empty()) { double currentPrice = history.back().value; painter.setFont(QFont("Arial",21, QFont::Bold)); painter.drawText(QRect(0,h/2+20,w,20), Qt::AlignCenter, QLocale().toString(currentPrice,'f',3)); }
        painter.setFont(QFont("Arial",8)); painter.setPen(Qt::yellow); painter.drawText(QRect(1,h-50,w-10,20), Qt::AlignLeft|Qt::AlignBottom, QString("Trades: %1").arg(history.size()));
        painter.setFont(QFont("Arial",6)); painter.setPen(textColor); painter.drawText(QRect(1,1,w-10,20), Qt::AlignLeft|Qt::AlignTop, QString("Volatility: %1%\n").arg(volatility,0,'f',4));
        // market/provider badge top-right (auto-sized narrower)
        if (!providerName.isEmpty()) {
            QString badge = marketName.isEmpty() ? providerName : providerName + " • " + marketName;
            QFont f("Arial", 8, QFont::DemiBold);
            painter.setFont(f);
            QFontMetrics fm(f);
            int textW = fm.horizontalAdvance(badge);
            int pad = 8;
            int bw = std::min(std::max(textW + pad*2, 60), w-8);
            int bx = std::max(4, w - bw - 4);
            QRect br(bx, 4, bw, 18);
            painter.setPen(Qt::NoPen); painter.setBrush(QColor(0,0,0,100)); painter.drawRoundedRect(br, 6, 6);
            painter.setPen(QColor(220,220,220));
            painter.drawText(br.adjusted(6,0,-6,0), Qt::AlignVCenter|Qt::AlignLeft, badge);
        }
        // unsupported banner bottom-right
        if (!unsupportedMsg.isEmpty()) {
            QRect wr(w-180, h-26, 176, 20);
            painter.setPen(Qt::NoPen); painter.setBrush(QColor(255, 140, 0, 160)); painter.drawRoundedRect(wr, 6, 6);
            painter.setPen(Qt::black); painter.setFont(QFont("Arial", 8, QFont::Bold));
            painter.drawText(wr.adjusted(6,0,-6,0), Qt::AlignVCenter|Qt::AlignLeft, unsupportedMsg);
        }
    };

    const double angle = 45 + (270 * _value / 100);

    switch (style) {
        case SpeedometerStyle::Classic: {
            int needleW = std::clamp(int(size/120), 2, 6);
            painter.setPen(QPen(themeColors.arcBase, std::clamp(int(size/80), 3, 6)));
            painter.drawArc(rect, 45*16, 270*16);
            int warn = thresholds.warn, danger = thresholds.danger; if (!thresholds.enabled) { warn=70; danger=90; }
            struct Zone{double s,e; QColor c;}; 
            std::vector<Zone> zones={{0,double(warn),themeColors.zoneGood},{double(warn),double(danger),themeColors.zoneWarn},{double(danger),100.0,themeColors.zoneDanger}};
            for (const auto& z:zones) { painter.setPen(QPen(z.c, std::clamp(int(size/60), 5, 10))); double span=(z.e-z.s)*270/100; double za=45+(270*z.s/100); painter.drawArc(rect, int(za*16), int(span*16)); }
            QColor needle = (!thresholds.enabled)? themeColors.needleNormal : (_value>=danger? themeColors.zoneDanger : (_value>=warn? themeColors.zoneWarn : themeColors.needleNormal));
            painter.setPen(QPen(needle, needleW)); painter.translate(w/2,h/2); painter.rotate(angle);
            int tipRx = std::clamp(int(size/10), 10, 26);
            int tipRy = std::clamp(int(tipRx*0.45), 5, 14);
            int endR = std::max(8, int(size/2 - (tipRx + 6)));
            painter.drawLine(0,0,endR,0);
            painter.setBrush(themeColors.zoneDanger);
            // White contour around needle tip
            {
                int tipOutline = std::clamp(int(size/180), 1, 2);
                painter.setPen(QPen(Qt::white, tipOutline));
                painter.drawEllipse(QPoint(endR,0), tipRx, tipRy);
            }
            painter.resetTransform();
            // Volume overlay under arc if enabled
            if (volVis == VolumeVis::Bar) {
                painter.save();
                double t = std::clamp(volNorm, 0.0, 1.0);
                QColor vc = themeColors.glow; vc.setAlpha(200);
                QRect vr = rect.adjusted(10,10,-10,-10);
                painter.setPen(QPen(themeColors.arcBase, std::clamp(int(size/200), 1, 2)));
                painter.drawArc(vr, 45*16, 270*16);
                painter.setPen(QPen(vc, std::clamp(int(size/50), 3, 6), Qt::SolidLine, Qt::FlatCap));
                painter.drawArc(vr, 45*16, int((270.0 * t) * 16));
                painter.restore();
            } else if (volVis == VolumeVis::Needle) {
                painter.save(); painter.translate(w/2, h/2);
                double a2 = 45 + 270.0 * std::clamp(volNorm, 0.0, 1.0);
                painter.rotate(a2);
                QColor vc = themeColors.glow; vc.setAlpha(220);
                painter.setPen(QPen(vc, 2));
                painter.drawLine(0, 0, size/2 - 28, 0);
                painter.restore();
            }
            drawCommonTexts();
            break;
        }
        case SpeedometerStyle::SegmentBar: {
            // SegmentBar: bold segmented progress along the arc with clear gaps
            painter.setPen(QPen(themeColors.arcBase, 2));
            QRect base = rect.adjusted(12,12,-12,-12);
            painter.drawArc(base, 45*16, 270*16);

            int warn = thresholds.warn, danger = thresholds.danger; if (!thresholds.enabled) { warn=70; danger=90; }
            QColor indicator = (_value>=danger? themeColors.zoneDanger : (_value>=warn? themeColors.zoneWarn : themeColors.zoneGood));
            painter.setPen(QPen(indicator, 8, Qt::SolidLine, Qt::RoundCap));

            // Draw discrete segments every 4% with 2% gap
            for (int v=0; v < int(_value); v+=4) {
                double segStart = 45 + (270.0 * v / 100.0);
                double segSpan  = 270.0 * 2.8 / 100.0; // segment length ~2.8%
                painter.drawArc(base, int(segStart*16), int(segSpan*16));
            }

            // Minimal central label
            painter.setPen(themeColors.text);
            painter.setFont(QFont("Arial", 18, QFont::DemiBold));
            QString valueText = history.empty() ? QString("0.000") : QLocale().toString(history.back().value, 'f', 3);
            painter.drawText(QRect(0, h/2-18, w, 24), Qt::AlignCenter, valueText);
            painter.setFont(QFont("Arial", 9));
            painter.setPen(themeColors.text.lighter());
            painter.drawText(QRect(0, h/2+6, w, 18), Qt::AlignCenter, currency);
            break;
        }
        case SpeedometerStyle::DualArc: {
            // DualArc: outer arc shows value; inner arc shows volatility level as a secondary metric
            QRect outer = rect.adjusted(6,6,-6,-6);
            QRect inner = rect.adjusted(22,22,-22,-22);

            // Base arcs
            painter.setPen(QPen(themeColors.arcBase, 2));
            painter.drawArc(outer, 45*16, 270*16);
            painter.setPen(QPen(themeColors.arcBase.lighter(130), 2));
            painter.drawArc(inner, 45*16, 270*16);

            // Outer progress (value)
            int warn = thresholds.warn, danger = thresholds.danger; if (!thresholds.enabled) { warn=70; danger=90; }
            QColor outerColor = (_value>=danger? themeColors.zoneDanger : (_value>=warn? themeColors.zoneWarn : themeColors.zoneGood));
            painter.setPen(QPen(outerColor, 6, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(outer, 45*16, int((270.0 * std::clamp(_value, 0.0, 100.0) / 100.0)*16));

            // Inner progress (volatility scaled 0..100)
            double volPct = std::min(100.0, std::max(0.0, volatility));
            QColor innerColor = themeColors.glow; innerColor.setAlpha(200);
            painter.setPen(QPen(innerColor, 4, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(inner, 45*16, int((270.0 * volPct / 100.0)*16));

            // Needle for precise value
            QColor needle = (!thresholds.enabled)? themeColors.needleNormal : (_value>=danger? themeColors.zoneDanger : (_value>=warn? themeColors.zoneWarn : themeColors.needleNormal));
            painter.setPen(QPen(needle, 3)); painter.translate(w/2,h/2); painter.rotate(angle);
            painter.drawLine(0,0,size/2-18,0); painter.resetTransform();

            // Labels
            painter.setPen(themeColors.text);
            painter.setFont(QFont("Arial", 10, QFont::DemiBold));
            painter.drawText(QRect(0, h/2+14, w, 16), Qt::AlignCenter, currency);
            painter.setFont(QFont("Arial", 16, QFont::Bold));
            QString valStr = history.empty() ? QString("0.000") : QLocale().toString(history.back().value, 'f', 3);
            painter.drawText(QRect(0, h/2-28, w, 24), Qt::AlignCenter, valStr);

            // Volatility tag in corner
            painter.setFont(QFont("Arial", 8)); painter.setPen(themeColors.text.lighter());
            painter.drawText(QRect(8, 8, w-16, 16), Qt::AlignLeft|Qt::AlignVCenter, QString("Vol: %1% ").arg(volatility,0,'f',2));

            break;
        }
        case SpeedometerStyle::NeonGlow: {
            // Darker background for neon effect, but still use theme tint
            QColor neonBg = themeColors.background.darker(150);
            painter.fillRect(0,0,w,h,neonBg);
            QRadialGradient rg(rect.center(), rect.width()/2.0);
            rg.setColorAt(0.0, QColor(themeColors.glow.red(), themeColors.glow.green(), themeColors.glow.blue(), 60));
            rg.setColorAt(0.6, QColor(themeColors.glow.red(), themeColors.glow.green(), themeColors.glow.blue(), 15));
            rg.setColorAt(1.0, QColor(0,0,0,0));
            painter.setBrush(rg); painter.setPen(Qt::NoPen); painter.drawEllipse(rect.adjusted(-10,-10,10,10));
            int warn = thresholds.warn, danger = thresholds.danger; if (!thresholds.enabled) { warn=70; danger=90; }
            auto glowPen = [&](double s,double e,QColor base,int w){ QPen p(QBrush(base), w); p.setCapStyle(Qt::RoundCap); painter.setPen(p); double span=(e-s)*270/100; double za=45+(270*s/100); painter.drawArc(rect, int(za*16), int(span*16)); };
            glowPen(0, warn, QColor(themeColors.zoneGood.red(), themeColors.zoneGood.green(), themeColors.zoneGood.blue(), 190), 4); 
            glowPen(warn, danger, QColor(themeColors.zoneWarn.red(), themeColors.zoneWarn.green(), themeColors.zoneWarn.blue(), 190), 4); 
            glowPen(danger, 100, QColor(themeColors.zoneDanger.red(), themeColors.zoneDanger.green(), themeColors.zoneDanger.blue(), 190), 4);
            QColor needle = (_value>=danger? themeColors.zoneDanger : (_value>=warn? themeColors.zoneWarn : themeColors.glow));
            painter.setPen(QPen(needle, 5, Qt::SolidLine, Qt::RoundCap)); painter.translate(w/2,h/2); painter.rotate(angle); painter.drawLine(0,0,size/2-18,0); painter.resetTransform();
            painter.setPen(themeColors.glow); drawCommonTexts(themeColors.text.lighter());
            break;
        }
        case SpeedometerStyle::Minimal: {
            painter.setPen(QPen(themeColors.arcBase, 2, Qt::DashLine)); painter.drawArc(rect.adjusted(10,10,-10,-10), 45*16, 270*16);
            QColor needle = (!thresholds.enabled)? themeColors.needleNormal : (_value>=thresholds.danger? themeColors.zoneDanger : (_value>=thresholds.warn? themeColors.zoneWarn : themeColors.needleNormal));
            painter.setPen(QPen(needle, 3)); painter.translate(w/2,h/2); painter.rotate(angle); painter.drawLine(0,0,size/2-10,0); painter.resetTransform();
            drawCommonTexts();
            break;
        }
        case SpeedometerStyle::ModernTicks: {
            // KiloCode Modern Ticks — multi-layer arcs, variable ticks, outside numerals, gradient needle, volumetric hub
            painter.save();
            // Background accent
            painter.fillRect(rect.adjusted(-6,-6,6,6), themeColors.background);
            // Outer and inner arcs
            QRect outer = rect.adjusted(6,6,-6,-6);
            QRect inner = rect.adjusted(18,18,-18,-18);
            // Base multi-level arcs with subtle gradients
            QConicalGradient g1(outer.center(), -45);
            g1.setColorAt(0.00, themeColors.arcBase.lighter(110));
            g1.setColorAt(0.25, themeColors.arcBase);
            g1.setColorAt(0.75, themeColors.arcBase.darker(105));
            g1.setColorAt(1.00, themeColors.arcBase.lighter(110));
            painter.setPen(QPen(QBrush(g1), 3, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(outer, 45*16, 270*16);
            painter.setPen(QPen(themeColors.arcBase.lighter(140), 2, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(inner, 45*16, 270*16);

            // Subtle auxiliary arc for structure
            QRect aux = rect.adjusted(28,28,-28,-28);
            painter.setPen(QPen(themeColors.arcBase.darker(115), 1, Qt::DotLine));
            painter.drawArc(aux, 45*16, 270*16);

            // Segmented scale with variable-length ticks: long (0/25/50/75/100), medium (x10), short (x5)
            painter.save(); painter.translate(w/2, h/2);
            for (int v=0; v<=100; v+=5) {
                painter.save(); double a = 45 + 270.0*v/100.0; painter.rotate(a);
                int tOuter = size/2 - 8;
                int len = 6; int thick = 1; QColor tc = themeColors.text;
                if (v%25==0) { len = 18; thick = 3; tc = themeColors.text; }
                else if (v%10==0) { len = 12; thick = 2; tc = themeColors.text.lighter(120); }
                else { len = 7; thick = 1; tc = themeColors.text.lighter(150); }
                painter.setPen(QPen(tc, thick, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(tOuter-len, 0, tOuter, 0);
                painter.restore();
            }

            // Numbers placed just outside the main arc with tiny drop shadow
            for (int v : {0,25,50,75,100}) {
                painter.save(); double a = 45 + 270.0*v/100.0; painter.rotate(a);
                QPoint pos(size/2 - 30, 0);
                painter.rotate(-a);
                QFont f("Arial", 9, QFont::DemiBold); painter.setFont(f);
                // shadow
                painter.setPen(QPen(QColor(0,0,0,120), 1)); painter.drawText(pos + QPoint(1,1), QString::number(v));
                painter.setPen(themeColors.text); painter.drawText(pos, QString::number(v));
                painter.restore();
            }
            painter.restore();

            // Gradient needle with glow and sharp tip
            painter.save(); painter.translate(w/2, h/2); painter.rotate(angle);
            QLinearGradient ng(QPointF(0,0), QPointF(size/2-16, 0));
            QColor needleColor = (!thresholds.enabled)? themeColors.glow : (_value>=thresholds.danger? themeColors.zoneDanger : (_value>=thresholds.warn? themeColors.zoneWarn : themeColors.glow));
            ng.setColorAt(0.0, needleColor.darker(120));
            ng.setColorAt(0.6, needleColor);
            ng.setColorAt(1.0, needleColor.lighter(130));
            painter.setPen(QPen(QBrush(ng), 4, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(0, 0, size/2-20, 0);
            // Sharp tip
            QPolygon tip; tip << QPoint(size/2-26, 0) << QPoint(size/2-14, -5) << QPoint(size/2-14, 5);
            painter.setBrush(needleColor); painter.setPen(Qt::NoPen); painter.drawPolygon(tip);
            // Central hub with volumetric effect
            QRadialGradient hub(0,0, 14); hub.setColorAt(0, themeColors.text); hub.setColorAt(1, QColor(0,0,0,120));
            painter.setBrush(hub); painter.setPen(QPen(QColor(255,255,255,80), 1)); painter.drawEllipse(QPoint(0,0), 9, 9);
            painter.restore();

            // Foreground value + currency; improved contrast
            drawCommonTexts(themeColors.text);
            // Volume overlay for ModernTicks
            if (volVis == VolumeVis::Bar || volVis == VolumeVis::Needle) {
                painter.save();
                if (volVis == VolumeVis::Bar) {
                    QRect vr = rect.adjusted(24,24,-24,-24);
                    QColor vc = themeColors.glow; vc.setAlpha(180);
                    painter.setPen(QPen(vc, std::clamp(int(size/60), 3, 6), Qt::SolidLine, Qt::FlatCap));
                    painter.drawArc(vr, 45*16, int((270.0 * std::clamp(volNorm,0.0,1.0))*16));
                } else {
                    painter.translate(w/2, h/2);
                    double a2 = 45 + 270.0 * std::clamp(volNorm, 0.0, 1.0);
                    painter.rotate(a2);
                    QColor vc = themeColors.glow; vc.setAlpha(220);
                    painter.setPen(QPen(vc, 2));
                    painter.drawLine(0, 0, size/2 - 24, 0);
                }
                painter.restore();
            }
            // Balance initial painter.save() at the start of this style block
            painter.restore();
            break;
        }
    case SpeedometerStyle::Circle: {
            // Classic Pro: Traditional speedometer with modern refinements
            // Main arc with subtle gradient
            int needleW = std::clamp(int(size/120), 2, 6);
            painter.setPen(QPen(themeColors.arcBase, std::clamp(int(size/80), 3, 6))); 
            painter.drawArc(rect.adjusted(8,8,-8,-8), 45*16, 270*16);
            
            // Draw major tick marks (every 20 units)
            painter.setPen(QPen(themeColors.text, 2));
            for (int v=0; v<=100; v+=20) {
                painter.save(); painter.translate(w/2, h/2); 
                double a = 45 + 270.0*v/100.0; painter.rotate(a);
                int outer = size/2 - 8, inner = size/2 - 18;
                painter.drawLine(outer, 0, inner, 0); painter.restore();
            }
            
            // Draw minor tick marks (every 10 units)
            painter.setPen(QPen(themeColors.text.lighter(), 1));
            for (int v=10; v<=90; v+=20) {
                // first minor tick
                painter.save(); painter.translate(w/2, h/2);
                double a1 = 45 + 270.0*v/100.0; painter.rotate(a1);
                { int outer = size/2 - 8, inner = size/2 - 13; painter.drawLine(outer, 0, inner, 0); }
                painter.restore();
                // second minor tick (v+10)
                painter.save(); painter.translate(w/2, h/2);
                double a2 = 45 + 270.0*(v+10)/100.0; painter.rotate(a2);
                { int outer = size/2 - 8, inner = size/2 - 13; painter.drawLine(outer, 0, inner, 0); }
                painter.restore();
            }
            
            // Colored zones with threshold support
            int warn = thresholds.warn, danger = thresholds.danger; 
            if (!thresholds.enabled) { warn=70; danger=90; }
            
            // Good zone (green)
            painter.setPen(QPen(themeColors.zoneGood, 8, Qt::SolidLine, Qt::RoundCap));
            double goodSpan = warn * 270.0 / 100.0;
            painter.drawArc(rect, 45*16, int(goodSpan*16));
            
            // Warning zone (yellow/orange)
            painter.setPen(QPen(themeColors.zoneWarn, 8, Qt::SolidLine, Qt::RoundCap));
            double warnStart = 45 + goodSpan;
            double warnSpan = (danger - warn) * 270.0 / 100.0;
            painter.drawArc(rect, int(warnStart*16), int(warnSpan*16));
            
            // Danger zone (red)
            painter.setPen(QPen(themeColors.zoneDanger, 8, Qt::SolidLine, Qt::RoundCap));
            double dangerStart = warnStart + warnSpan;
            double dangerSpan = (100 - danger) * 270.0 / 100.0;
            painter.drawArc(rect, int(dangerStart*16), int(dangerSpan*16));
            
            // Classic needle with red tip
            QColor needleColor = (!thresholds.enabled)? themeColors.needleNormal : 
                (_value>=danger? themeColors.zoneDanger : (_value>=warn? themeColors.zoneWarn : themeColors.needleNormal));
            painter.setPen(QPen(needleColor, needleW)); 
            painter.translate(w/2,h/2); painter.rotate(angle);
            int tipRx = std::clamp(int(size/12), 8, 20);
            int tipRy = std::clamp(int(tipRx*0.5), 5, 12);
            int endR = std::max(10, int(size/2 - (tipRx + 8)));
            painter.drawLine(0,0,endR,0); 
            // Red needle tip with white contour
            painter.setBrush(themeColors.zoneDanger);
            {
                int tipOutline = std::clamp(int(size/180), 1, 2);
                painter.setPen(QPen(Qt::white, tipOutline));
                painter.drawEllipse(QPoint(endR,0), tipRx, tipRy);
            }
            // Center hub scaled
            int hubR = std::clamp(int(size/40), 6, 12);
            painter.setBrush(themeColors.text); 
            painter.drawEllipse(QPoint(0,0), hubR, hubR);
            painter.resetTransform();
            // Volume overlay for Circle
            if (volVis == VolumeVis::Bar) {
                painter.save();
                QRect vr = rect.adjusted(12,12,-12,-12);
                QColor vc = themeColors.glow; vc.setAlpha(190);
                painter.setPen(QPen(vc, std::clamp(int(size/55), 3, 6), Qt::SolidLine, Qt::FlatCap));
                painter.drawArc(vr, 45*16, int((270.0 * std::clamp(volNorm,0.0,1.0))*16));
                painter.restore();
            } else if (volVis == VolumeVis::Needle) {
                painter.save(); painter.translate(w/2, h/2);
                double a2 = 45 + 270.0 * std::clamp(volNorm, 0.0, 1.0);
                painter.rotate(a2);
                QColor vc = themeColors.glow; vc.setAlpha(220);
                painter.setPen(QPen(vc, 2));
                painter.drawLine(0, 0, size/2 - 26, 0);
                painter.restore();
            }
            
            drawCommonTexts();
            break;
        }
        case SpeedometerStyle::Gauge: {
            // Minimalist Gauge: subdued base arc + thin multi-zone accents + refined indicator
            QColor base = themeColors.arcBase;
            painter.setPen(QPen(base, 2));
            QRect r2 = rect.adjusted(10,10,-10,-10);
            painter.drawArc(r2, 45*16, 270*16);

            int warn = thresholds.warn, danger = thresholds.danger; if (!thresholds.enabled) { warn=70; danger=90; }
            auto faint = [](QColor c){ c.setAlpha(140); return c; };
            // Subtle zones
            painter.setPen(QPen(faint(themeColors.zoneGood), 3)); painter.drawArc(r2, 45*16, int((warn*270.0/100.0)*16));
            painter.setPen(QPen(faint(themeColors.zoneWarn), 3)); painter.drawArc(r2, int((45 + warn*270.0/100.0)*16), int(((danger-warn)*270.0/100.0)*16));
            painter.setPen(QPen(faint(themeColors.zoneDanger), 3)); painter.drawArc(r2, int((45 + danger*270.0/100.0)*16), int(((100-danger)*270.0/100.0)*16));

            // Slim indicator with soft tip
            QColor indicatorColor = (!thresholds.enabled)? themeColors.text : (_value>=danger? themeColors.zoneDanger : (_value>=warn? themeColors.zoneWarn : themeColors.zoneGood));
            painter.setPen(QPen(indicatorColor, 4, Qt::SolidLine, Qt::RoundCap));
            double indicatorAngle = 45 + (270 * _value / 100);
            painter.save(); painter.translate(w/2, h/2); painter.rotate(indicatorAngle);
            int startR = size/2 - 28, endR = size/2 - 12;
            painter.drawLine(startR, 0, endR, 0);
            painter.setBrush(indicatorColor.lighter()); painter.setPen(Qt::NoPen);
            painter.drawEllipse(QPoint(endR, 0), 4, 4);
            painter.restore();

            // Sparse ticks
            painter.setPen(QPen(themeColors.text.lighter(), 1));
            for (int v=0; v<=100; v+=25) {
                painter.save(); painter.translate(w/2, h/2); double a = 45 + 270.0*v/100.0; painter.rotate(a);
                int t = size/2 - 8; painter.drawLine(t-3,0,t,0); painter.restore();
            }
            drawCommonTexts();
            break;
        }
        case SpeedometerStyle::Ring: {
            // Modern Scale: Contemporary linear-style scale design
            // Background arc - thin and subtle
            painter.setPen(QPen(themeColors.arcBase.lighter(), 2)); 
            painter.drawArc(rect.adjusted(15,15,-15,-15), 45*16, 270*16);
            
            // Modern scale marks - clean lines radiating outward
            painter.setPen(QPen(themeColors.text, 2));
            for (int v=0; v<=100; v+=10) {
                painter.save(); painter.translate(w/2, h/2); 
                double a = 45 + 270.0*v/100.0; painter.rotate(a);
                
                // Different lengths for major/minor marks
                int outer = size/2 - 15;
                int inner = (v % 20 == 0) ? size/2 - 30 : size/2 - 22; // Major vs minor
                int thickness = (v % 20 == 0) ? 3 : 1;
                
                painter.setPen(QPen(themeColors.text, thickness));
                painter.drawLine(outer, 0, inner, 0);
                
                // Scale numbers for major marks
                if (v % 20 == 0) {
                    painter.save();
                    painter.translate(inner - 15, 0);
                    painter.rotate(-a); // Counter-rotate text
                    painter.setFont(QFont("Arial", 8, QFont::Bold));
                    painter.drawText(-10, -5, 20, 10, Qt::AlignCenter, QString::number(v));
                    painter.restore();
                }
                painter.restore();
            }
            
            // Progress indicator - modern segmented arc
            int warn = thresholds.warn, danger = thresholds.danger; 
            if (!thresholds.enabled) { warn=70; danger=90; }
            
            QColor indicatorColor = (!thresholds.enabled)? themeColors.glow : 
                (_value>=danger? themeColors.zoneDanger : (_value>=warn? themeColors.zoneWarn : themeColors.zoneGood));
            
            // Draw progress as individual segments
            painter.setPen(QPen(indicatorColor, 6, Qt::SolidLine, Qt::RoundCap));
            int segments = int(_value / 5); // 5% per segment
            for (int i = 0; i < segments; i++) {
                double segStart = 45 + (270.0 * i * 5 / 100.0);
                double segSpan = 270.0 * 3 / 100.0; // 3% span with 2% gap
                painter.drawArc(rect.adjusted(5,5,-5,-5), int(segStart*16), int(segSpan*16));
            }
            
            // Modern center indicator - clean geometric shape
            painter.setPen(QPen(indicatorColor, 2));
            painter.setBrush(QBrush(indicatorColor.lighter()));
            
            // Draw pointing triangle at current value
            painter.save();
            painter.translate(w/2, h/2);
            painter.rotate(angle);
            
            QPolygon triangle;
            triangle << QPoint(size/2-35, 0) << QPoint(size/2-15, -8) << QPoint(size/2-15, 8);
            painter.drawPolygon(triangle);
            painter.restore();
            
            // Central value display with modern typography
            painter.setPen(themeColors.text);
            painter.setFont(QFont("Arial", 18, QFont::Light));
            QString valueText;
            if (!history.empty()) {
                double currentPrice = history.back().value;
                valueText = QLocale().toString(currentPrice, 'f', 3);
            } else {
                valueText = "0.000";
            }
            painter.drawText(QRect(0, h/2-15, w, 30), Qt::AlignCenter, valueText);
            
            // Currency label - smaller, positioned below
            painter.setFont(QFont("Arial", 9, QFont::Normal));
            painter.setPen(themeColors.text.lighter());
            painter.drawText(QRect(0, h/2+10, w, 20), Qt::AlignCenter, currency);
            break;
        }
    }

    // Extra volume visualizations independent of style
    if (volVis == VolumeVis::Sidebar || volVis == VolumeVis::Fuel) {
        painter.save();
        double t = std::clamp(volNorm, 0.0, 1.0);
        if (volVis == VolumeVis::Sidebar) {
            // Right-aligned vertical segmented bar from near top to bottom
            int marginR = 6;
            // Width by mode
            int autoW = std::clamp(int(std::min(width(), height())/24), 6, 14);
            int barW = autoW;
            if (sidebarWidthMode==1) barW = std::max(6, autoW-3);
            else if (sidebarWidthMode==2) barW = autoW; // medium
            else if (sidebarWidthMode==3) barW = autoW + 4; // wide
            int topY = providerName.isEmpty()? 6 : 26; // leave room for provider badge
            // Also leave room for chips if enabled
            if (showVolOverlay || showChangeOverlay) topY = std::max(topY, 8);
            int bottomY = height()-8;
            if (bottomY - topY < 24) { topY = 6; bottomY = height()-6; }
            QRect barRect(width()-barW-marginR, topY, barW, bottomY-topY);
            // Background
            QColor bg = themeColors.arcBase; bg.setAlpha(90);
            // Outline if enabled
            painter.setPen(Qt::NoPen); painter.setBrush(bg);
            painter.drawRoundedRect(barRect, 4, 4);
            if (sidebarOutline) {
                QColor oc = themeColors.text; oc.setAlpha(120);
                painter.setPen(QPen(oc, 1)); painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(barRect.adjusted(0,0,0,0), 4, 4);
            }

            // Segments
            int segH = std::clamp(int(barRect.height()/24), 4, 10);
            int gap = std::max(2, segH/3);
            int segCount = std::max(6, barRect.height() / (segH + gap));
            int filled = int(std::round(t * segCount));
            for (int i=0; i<segCount; ++i) {
                // bottom-up stacking
                int y1 = barRect.bottom() - (i+1)*(segH+gap) + gap;
                QRect sRect(barRect.left()+2, y1, barRect.width()-4, segH);
                // Gradient from good (bottom) to danger (top)
                double g = double(i) / std::max(1, segCount-1);
                auto lerp = [](const QColor& a, const QColor& b, double u){
                    auto L = [](int x){ return std::clamp(x,0,255); };
                    return QColor(L(int(a.red()  + (b.red()  - a.red())  * u)),
                                  L(int(a.green()+ (b.green()- a.green())* u)),
                                  L(int(a.blue() + (b.blue() - a.blue()) * u)));
                };
                // Apply brightness scaling
                auto scale = [&](QColor c){
                    double k = std::clamp(sidebarBrightnessPct/100.0, 0.5, 1.5);
                    int r = std::clamp(int(c.red()  * k),   0, 255);
                    int gC= std::clamp(int(c.green()* k),   0, 255);
                    int b = std::clamp(int(c.blue() * k),   0, 255);
                    return QColor(r,gC,b);
                };
                QColor col = scale(lerp(themeColors.zoneGood, themeColors.zoneDanger, g));
                if (i < filled) {
                    QColor c = col;
                    // blend with glow tint
                    c.setAlpha(220);
                    painter.setBrush(c);
                } else {
                    QColor c = col; c.setAlpha(60);
                    painter.setBrush(c);
                }
                painter.drawRoundedRect(sRect, 2, 2);
            }
        } else if (volVis == VolumeVis::Fuel) {
            // Small inner arc with a short needle indicating volume level
            int inset = std::clamp(int(std::min(width(), height())/6), 26, 48);
            QRect inner = rect.adjusted(inset, inset, -inset, -inset);
            // Base small arc
            painter.setPen(QPen(themeColors.arcBase.lighter(130), 2, Qt::SolidLine, Qt::RoundCap));
            painter.setBrush(Qt::NoBrush);
            painter.drawArc(inner, 45*16, 270*16);
            // Fuel needle
            painter.save(); painter.translate(width()/2, height()/2);
            double a2 = 45 + 270.0 * t;
            painter.rotate(a2);
            QColor vc = themeColors.glow; vc.setAlpha(230);
            QPen p(vc, 2, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(p);
            int len = std::max(10, inner.width()/2 - 6);
            painter.drawLine(0, 0, len, 0);
            // tiny hub
            painter.setPen(Qt::NoPen); painter.setBrush(vc);
            painter.drawEllipse(QPoint(0,0), 2, 2);
            painter.restore();
        }
        painter.restore();
    }

    // Anomaly badge in top-left
    if (showAnomalyBadge && anomalyActive) {
        painter.save();
        QPoint tl(8, 8);
        QPolygon tri;
        tri << QPoint(tl.x()+12, tl.y()) << QPoint(tl.x()+24, tl.y()+24) << QPoint(tl.x(), tl.y()+24);
        painter.setBrush(QColor(255, 170, 0)); painter.setPen(Qt::NoPen);
        painter.drawPolygon(tri);
        painter.setPen(Qt::black);
        painter.setFont(QFont("Arial", 10, QFont::Black));
        painter.drawText(QRect(tl.x()+6, tl.y()+6, 12, 14), Qt::AlignCenter, "!");
        if (!anomalyLabel.isEmpty()) {
            painter.setFont(QFont("Arial", 7, QFont::DemiBold));
            painter.setPen(Qt::white);
            painter.drawText(QRect(tl.x()-2, tl.y()+24, 40, 12), Qt::AlignCenter, anomalyLabel);
        }
        painter.restore();
    }
    // Overlays: small chips in top-right with metrics if enabled
    auto drawChip = [&](const QString& text, const QPoint& topLeft, const QColor& bg){
        painter.save();
        QFont f = painter.font(); f.setPointSizeF(std::max(9.0, f.pointSizeF())); painter.setFont(f);
        QFontMetrics fm(f); int pad = 6; QSize sz(fm.horizontalAdvance(text)+pad*2, fm.height()+pad);
        QRect r(topLeft, sz);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen); QColor c = bg; c.setAlpha(180); painter.setBrush(c);
        painter.drawRoundedRect(r, 8, 8);
        painter.setPen(Qt::white); painter.drawText(r.adjusted(pad,0,-pad,0), Qt::AlignVCenter|Qt::AlignLeft, text);
        painter.restore();
    };
    int y=8; int x = width()-160;
    if (showVolOverlay) {
        QString t = QString("Vol: %1%").arg(volatility, 0, 'f', 2);
        drawChip(t, QPoint(x,y), QColor(50,100,200)); y+=26;
    }
    if (showChangeOverlay) {
        double change=0.0; if (cachedMinVal && cachedMaxVal) {
            // interpret _value (0..100) as scaled percent; approximate change vs midpoint
            change = (_value - 50.0) / 50.0 * 100.0;
        }
        QString t = QString::fromUtf8("Δ: %1%").arg(change,0,'f',2);
        drawChip(t, QPoint(x,y), QColor(200,120,60)); y+=26;
    }

    // Draw widget frame (perimeter border) in minimalistic styles
    if (frameStyle != FrameStyle::None) {
        painter.save();
        QRect fr = this->rect().adjusted(1,1,-1,-1);
        switch (frameStyle) {
            case FrameStyle::Minimal: {
                QColor c = themeColors.arcBase.lighter(130); c.setAlpha(180);
                painter.setPen(QPen(c, 1)); painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(fr, 8, 8);
                break;
            }
            case FrameStyle::Dashed: {
                QColor c = themeColors.text; c.setAlpha(130);
                QPen p(c, 1, Qt::DashLine); p.setDashPattern({4,3});
                painter.setPen(p); painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(fr, 10, 10);
                break;
            }
            case FrameStyle::Glow: {
                QColor c = themeColors.glow; c.setAlpha(120);
                QPen p(c, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                painter.setPen(p); painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(fr, 12, 12);
                break;
            }
            default: break;
        }
        painter.restore();
    }
}

void DynamicSpeedometerCharts::updateChartSeries() {
    const bool useBtc = (modeView=="btc_ratio");
    auto& values = useBtc ? cachedProcessedBtcRatio : cachedProcessedHistory;
    seriesNormal->setVisible(!useBtc); seriesRatio->setVisible(useBtc);
    QLineSeries* s = useBtc? seriesRatio : seriesNormal;
    QVector<QPointF> pts; pts.reserve(int(values.size()));
    for (int i=0;i<int(values.size());++i) pts.append(QPointF(i, values[size_t(i)]));
    s->replace(pts);
    if (!values.empty()) {
        auto minmax = std::minmax_element(values.begin(), values.end());
        double minY = *minmax.first, maxY = *minmax.second; if (qFuzzyCompare(minY, maxY)) maxY += useBtc ? 1e-8 : 1e-4;
        axisX->setRange(0, std::max<int>(1, int(values.size()-1)));
        axisY->setRange(minY, maxY);
        chart->setTitle(useBtc ? QString("%1/BTC | %2").arg(currency, currentScale) : QString("%1 | %2").arg(currency, currentScale));
        // Apply chart options
        axisX->setGridLineVisible(showGrid);
        axisY->setGridLineVisible(showGrid);
        axisX->setVisible(showAxisLabels);
        axisY->setVisible(showAxisLabels);
        axisRSI->setGridLineVisible(showGrid);
        axisMACD->setGridLineVisible(showGrid);
        // Keep background aligned to theme
        chart->setBackgroundBrush(themeColors.background);
        QPen gridPen(themeColors.arcBase);
        gridPen.setStyle(Qt::DotLine);
        gridPen.setWidth(1);
        axisX->setGridLinePen(gridPen);
        axisY->setGridLinePen(gridPen);
        axisRSI->setGridLinePen(gridPen);
        axisMACD->setGridLinePen(gridPen);
        // Lines appearance
        s->setPointsVisible(false);
        s->setUseOpenGL(false);
    // Indicators update: hide axes by default
        axisRSI->setVisible(false);
        axisMACD->setVisible(false);
    // Default no anomaly until evaluated
    anomalyActive = false; anomalyLabel.clear();

        // RSI
        if (showRSI) {
            auto rsi = computeRSI(values, 14);
            QVector<QPointF> rpts; rpts.reserve(int(rsi.size()));
            for (int i=0;i<int(rsi.size());++i) rpts.append(QPointF(i, rsi[size_t(i)]));
            rsiSeries->replace(rpts);
            rsiSeries->setVisible(true);
            axisRSI->setRange(0, 100);
            axisRSI->setVisible(showAxisLabels);
        } else {
            rsiSeries->setVisible(false);
        }

        // MACD
        if (showMACD) {
            auto macd = computeMACD(values);
            QVector<QPointF> mp, sp; mp.reserve(int(macd.macd.size())); sp.reserve(int(macd.signal.size()));
            for (int i=0;i<int(macd.macd.size());++i) mp.append(QPointF(i, macd.macd[size_t(i)]));
            for (int i=0;i<int(macd.signal.size());++i) sp.append(QPointF(i, macd.signal[size_t(i)]));
            macdSeries->replace(mp); macdSignalSeries->replace(sp);
            macdSeries->setVisible(true); macdSignalSeries->setVisible(true);
            double mn = 0.0, mx = 0.0;
            if (!macd.macd.empty()) { auto mm = std::minmax_element(macd.macd.begin(), macd.macd.end()); mn = *mm.first; mx = *mm.second; }
            if (!macd.signal.empty()) { auto mm2 = std::minmax_element(macd.signal.begin(), macd.signal.end()); mn = std::min(mn, *mm2.first); mx = std::max(mx, *mm2.second); }
            if (qFuzzyCompare(mn, mx)) { mx += 1e-6; }
            axisMACD->setRange(mn, mx);
            axisMACD->setVisible(showAxisLabels);
        } else {
            macdSeries->setVisible(false); macdSignalSeries->setVisible(false);
        }

        // Bollinger Bands
        if (showBB) {
            auto bb = computeBollinger(values, 20, 2.0);
            QVector<QPointF> up, lo; up.reserve(int(bb.upper.size())); lo.reserve(int(bb.lower.size()));
            for (int i=0;i<int(bb.upper.size());++i) { up.append(QPointF(i, bb.upper[size_t(i)])); lo.append(QPointF(i, bb.lower[size_t(i)])); }
            bbUpperSeries->replace(up); bbLowerSeries->replace(lo);
            bbUpperSeries->setVisible(true); bbLowerSeries->setVisible(true);
        } else {
            bbUpperSeries->setVisible(false); bbLowerSeries->setVisible(false);
        }

        // Evaluate anomalies after indicators prepared
        if (showAnomalyBadge && !values.empty()) {
            switch (anomalyMode) {
                case AnomalyMode::RSIOverboughtOversold: {
                    auto rsi = computeRSI(values, 14);
                    if (!rsi.empty()) { double last=rsi.back(); if (last>=70.0) { anomalyActive=true; anomalyLabel="RSI↑"; } else if (last<=30.0) { anomalyActive=true; anomalyLabel="RSI↓"; } }
                    break; }
                case AnomalyMode::MACDCross: {
                    auto m = computeMACD(values);
                    if (m.macd.size()>1 && m.signal.size()>1) {
                        double pd = m.macd[m.macd.size()-2]-m.signal[m.signal.size()-2];
                        double ld = m.macd.back()-m.signal.back();
                        if (pd<=0 && ld>0) { anomalyActive=true; anomalyLabel="MACD↑"; }
                        else if (pd>=0 && ld<0) { anomalyActive=true; anomalyLabel="MACD↓"; }
                    }
                    break; }
                case AnomalyMode::BollingerBreakout: {
                    auto bb = computeBollinger(values, 20, 2.0);
                    if (!bb.upper.empty()) { double last=values.back(); if (last>bb.upper.back()) { anomalyActive=true; anomalyLabel="BB↑"; } else if (last<bb.lower.back()) { anomalyActive=true; anomalyLabel="BB↓"; } }
                    break; }
                case AnomalyMode::ZScore: {
                    double zs = computeZScore(values, std::min<int>(50,(int)values.size())); if (std::abs(zs)>=2.0) { anomalyActive=true; anomalyLabel = (zs>0? "Z↑" : "Z↓"); }
                    break; }
                case AnomalyMode::VolSpike: {
                    int N = std::min<int>(50, (int)values.size()-1);
                    if (N>10) {
                        std::vector<double> rets; rets.reserve(N);
                        for (int i=(int)values.size()-N; i<(int)values.size(); ++i) { if (i==0) continue; double prev=values[size_t(i-1)], cur=values[size_t(i)]; rets.push_back(prev? std::abs((cur-prev)/prev) : 0.0); }
                        if (rets.size()>5) { std::vector<double> tmp = rets; std::nth_element(tmp.begin(), tmp.begin()+tmp.size()/2, tmp.end()); double med = tmp[tmp.size()/2]; double last = rets.back(); if (last>med*2.5) { anomalyActive=true; anomalyLabel="VOL"; } }
                    }
                    break; }
                case AnomalyMode::Composite: {
                    QString lab; int votes = 0;
                    auto rsi=computeRSI(values,14);
                    bool rsiExtreme = (!rsi.empty() && (rsi.back()>=75.0 || rsi.back()<=25.0));
                    if (rsiExtreme) { ++votes; lab = lab.isEmpty()? "RSI" : lab+"+RSI"; }
                    auto m=computeMACD(values);
                    bool macdCross=false; if (m.macd.size()>1 && m.signal.size()>1) {
                        double pd=m.macd[m.macd.size()-2]-m.signal[m.signal.size()-2];
                        double ld=m.macd.back()-m.signal.back();
                        macdCross = ((pd<=0&&ld>0)||(pd>=0&&ld<0));
                    }
                    if (macdCross) { ++votes; lab = lab.isEmpty()? "MACD" : lab+"+MACD"; }
                    auto bb=computeBollinger(values,20,2.0);
                    bool bbBreak=false; if (!bb.upper.empty()) { double last=values.back(); bbBreak = (last>bb.upper.back()||last<bb.lower.back()); }
                    if (bbBreak) { ++votes; lab = lab.isEmpty()? "BB" : lab+"+BB"; }
                    double zAbs = std::abs(computeZScore(values, std::min<int>(50,(int)values.size())));
                    bool volGate=false; {
                        int N = std::min<int>(40, (int)values.size()-1);
                        if (N>10) {
                            std::vector<double> rets; rets.reserve(N);
                            for (int i=(int)values.size()-N; i<(int)values.size(); ++i) { if (i==0) continue; double prev=values[size_t(i-1)], cur=values[size_t(i)]; rets.push_back(prev? std::abs((cur-prev)/prev) : 0.0); }
                            std::vector<double> tmp = rets; std::nth_element(tmp.begin(), tmp.begin()+tmp.size()/2, tmp.end()); double med = tmp[tmp.size()/2];
                            double last = rets.back(); volGate = (med>1e-9 && last/med >= 1.8);
                        }
                    }
                    bool trigger = (votes>=2) || (votes>=1 && zAbs>=2.0) || (votes>=1 && volGate && zAbs>=1.5);
                    if (trigger) { anomalyActive=true; anomalyLabel=lab; }
                    break; }
                case AnomalyMode::RSIDivergence: {
                    auto rsi = computeRSI(values, 14);
                    // Простая дивергенция: последние 2 локальных экстремума цены и RSI противоположно направлены
                    auto findPeaks = [](const std::vector<double>& v, bool maxPeaks){ std::vector<int> idx; for (int i=1;i+1<(int)v.size();++i){ if (maxPeaks){ if (v[i]>v[i-1] && v[i]>v[i+1]) idx.push_back(i);} else { if (v[i]<v[i-1] && v[i]<v[i+1]) idx.push_back(i);} } return idx; };
                    if (values.size()>=10 && rsi.size()==values.size()) {
                        auto maxP = findPeaks(values, true); auto maxR = findPeaks(rsi, true);
                        if (maxP.size()>=2 && maxR.size()>=2) {
                            int p2 = maxP.back(), p1 = maxP[maxP.size()-2];
                            int r2 = maxR.back(), r1 = maxR[maxR.size()-2];
                            if (p1< p2 && r1< r2) {
                                // Медвежья дивергенция: цена делает выше максимум, RSI нет
                                if (values[p2] > values[p1] && rsi[r2] < rsi[r1]) { anomalyActive=true; anomalyLabel = "DIV-"; }
                            }
                        }
                        auto minP = findPeaks(values, false); auto minR = findPeaks(rsi, false);
                        if (minP.size()>=2 && minR.size()>=2) {
                            int p2 = minP.back(), p1 = minP[minP.size()-2];
                            int r2 = minR.back(), r1 = minR[minR.size()-2];
                            if (p1< p2 && r1< r2) {
                                // Бычья дивергенция: цена ниже минимум, RSI нет
                                if (values[p2] < values[p1] && rsi[r2] > rsi[r1]) { anomalyActive=true; anomalyLabel = "DIV+"; }
                            }
                        }
                    }
                    break; }
                case AnomalyMode::MACDHistSurge: {
                    auto m = computeMACD(values);
                    if (m.macd.size()>5 && m.signal.size()>5) {
                        // гистограмма = MACD - signal; surge: |последняя| > медианы×k
                        std::vector<double> hist(m.macd.size()); for (size_t i=0;i<hist.size();++i) hist[i]=m.macd[i]-m.signal[i];
                        int N = std::min<int>(50, (int)hist.size()); if (N>10) {
                            std::vector<double> window(hist.end()-N, hist.end()); for (auto& x:window) x=std::abs(x);
                            std::nth_element(window.begin(), window.begin()+window.size()/2, window.end()); double med = window[window.size()/2];
                            double last = std::abs(hist.back()); if (last > med*2.5) { anomalyActive=true; anomalyLabel="HIST"; }
                        }
                    }
                    break; }
                case AnomalyMode::ClusteredZ: {
                    // Комбинированный Z-Score: средний Z трех нормированных метрик: цена, RSI, MACD
                    auto rsi = computeRSI(values, 14);
                    auto m = computeMACD(values);
                    auto zVal = [&](const std::vector<double>& v){ return computeZScore(v, std::min<int>(50,(int)v.size())); };
                    double z1 = computeZScore(values, std::min<int>(50,(int)values.size()));
                    double z2 = rsi.empty()? 0.0 : zVal(rsi);
                    double z3 = 0.0; if (!m.macd.empty()) { z3 = zVal(m.macd); }
                    double z = (z1 + z2 + z3) / 3.0; if (std::abs(z) >= 2.2) { anomalyActive=true; anomalyLabel = (z>0? "CZ↑" : "CZ↓"); }
                    break; }
                case AnomalyMode::VolRegimeShift: {
                    // Изменение режима волатильности: скользящее среднее |ретёрнов| пересекло порог (или ratio short/long)
                    int N = std::min<int>(120, (int)values.size()-1);
                    if (N>30) {
                        std::vector<double> rets; rets.reserve(N); for (int i=(int)values.size()-N;i<(int)values.size();++i){ if(i==0) continue; double prev=values[size_t(i-1)], cur=values[size_t(i)]; rets.push_back(prev? std::abs((cur-prev)/prev):0.0); }
                        int half = (int)rets.size()/2; if (half>5) {
                            auto avg = [](const std::vector<double>& a){ double s=0; for(double x:a) s+=x; return s/(double)a.size(); };
                            double avgOld = avg(std::vector<double>(rets.begin(), rets.begin()+half));
                            double avgNew = avg(std::vector<double>(rets.begin()+half, rets.end()));
                            if (avgOld>1e-12 && (avgNew/avgOld >= 1.8)) { anomalyActive=true; anomalyLabel = "VOL↑"; }
                            else if (avgNew<1e-12 && avgOld>1e-12) { anomalyActive=true; anomalyLabel = "VOL↓"; }
                        }
                    }
                    break; }
                default: break;
            }
        }
    }
}

// ===== Indicator implementations =====
static inline double emaStep(double prev, double val, double alpha) { return prev + alpha*(val - prev); }

std::vector<double> DynamicSpeedometerCharts::computeRSI(const std::vector<double>& v, int period) {
    if (period <= 0 || (int)v.size() < period+1) return {};
    std::vector<double> rsi(v.size(), 50.0);
    double gain=0.0, loss=0.0;
    // Seed with first period
    for (int i=1; i<=period; ++i) {
        double diff = v[size_t(i)] - v[size_t(i-1)];
        if (diff >= 0) gain += diff; else loss -= diff;
    }
    gain /= period; loss /= period;
    for (size_t i=period+1; i<v.size(); ++i) {
        double diff = v[i] - v[i-1];
        double g = diff > 0 ? diff : 0.0;
        double l = diff < 0 ? -diff : 0.0;
        gain = (gain*(period-1) + g) / period;
        loss = (loss*(period-1) + l) / period;
        double rs = (loss == 0.0) ? 0.0 : (gain / loss);
        double val = (loss==0.0 && gain==0.0) ? 50.0 : (100.0 - (100.0/(1.0+rs)));
        rsi[i] = val;
    }
    return rsi;
}

DynamicSpeedometerCharts::MACDOut DynamicSpeedometerCharts::computeMACD(const std::vector<double>& v, int fast, int slow, int signal) {
    MACDOut out; if (fast<=0 || slow<=0 || signal<=0 || (int)v.size() < slow+signal+2) return out;
    std::vector<double> emaFast(v.size()), emaSlow(v.size());
    double aF = 2.0/(fast+1), aS = 2.0/(slow+1);
    emaFast[0] = v[0]; emaSlow[0] = v[0];
    for (size_t i=1;i<v.size();++i) { emaFast[i] = emaStep(emaFast[i-1], v[i], aF); emaSlow[i] = emaStep(emaSlow[i-1], v[i], aS); }
    out.macd.resize(v.size()); for (size_t i=0;i<v.size();++i) out.macd[i] = emaFast[i] - emaSlow[i];
    out.signal.resize(v.size()); double aSig = 2.0/(signal+1); out.signal[0] = out.macd[0];
    for (size_t i=1;i<out.macd.size();++i) out.signal[i] = emaStep(out.signal[i-1], out.macd[i], aSig);
    return out;
}

DynamicSpeedometerCharts::BBOut DynamicSpeedometerCharts::computeBollinger(const std::vector<double>& v, int period, double k) {
    BBOut out; if (period<=0 || (int)v.size() < period) return out; size_t n = v.size();
    out.upper.resize(n); out.lower.resize(n);
    std::vector<double> sum(n,0.0), sumsq(n,0.0);
    for (size_t i=0;i<n;++i) {
        sum[i] = v[i] + (i? sum[i-1] : 0.0);
        sumsq[i] = v[i]*v[i] + (i? sumsq[i-1] : 0.0);
        if (i+1 >= size_t(period)) {
            size_t j = i+1 - period;
            double s = sum[i] - (j? sum[j-1] : 0.0);
            double s2 = sumsq[i] - (j? sumsq[j-1] : 0.0);
            double m = s / period;
            double var = std::max(0.0, (s2/period - m*m));
            double sd = std::sqrt(var);
            out.upper[i] = m + k*sd; out.lower[i] = m - k*sd;
        } else {
            out.upper[i] = v[i]; out.lower[i] = v[i];
        }
    }
    return out;
}

// ===== Extra helpers =====
double DynamicSpeedometerCharts::computeZScore(const std::vector<double>& v, int window) {
    int n = std::min<int>(window, (int)v.size());
    if (n < 5) return 0.0;
    double sum = 0.0, sum2 = 0.0;
    for (int i=(int)v.size()-n; i<(int)v.size(); ++i) { sum += v[size_t(i)]; sum2 += v[size_t(i)]*v[size_t(i)]; }
    double mean = sum / n;
    double var = std::max(1e-12, sum2/n - mean*mean);
    double sd = std::sqrt(var);
    double last = v.back();
    return (last - mean) / sd;
}

void DynamicSpeedometerCharts::loadSensitivityPrefs() {
    QSettings st("alel12", "modular_dashboard");
    needleGain = st.value(QString("ui/sensitivity/needle_gain/%1").arg(currency), needleGain).toDouble();
    autoCollapseEnabled = st.value(QString("ui/sensitivity/collapse/enabled/%1").arg(currency), autoCollapseEnabled).toBool();
    autoCollapseFactor = st.value(QString("ui/sensitivity/collapse/factor/%1").arg(currency), autoCollapseFactor).toDouble();
    spikeExpandEnabled = st.value(QString("ui/sensitivity/spike/enabled/%1").arg(currency), spikeExpandEnabled).toBool();
    spikeExpandThreshold = st.value(QString("ui/sensitivity/spike/threshold/%1").arg(currency), spikeExpandThreshold).toDouble();
    minRangePctOfPrice = st.value(QString("ui/sensitivity/min_range_pct/%1").arg(currency), minRangePctOfPrice).toDouble();
    autoExpandFactor = st.value(QString("ui/sensitivity/spike/expand_factor/%1").arg(currency), autoExpandFactor).toDouble();
}

void DynamicSpeedometerCharts::saveSensitivityPrefs() const {
    QSettings st("alel12", "modular_dashboard");
    st.setValue(QString("ui/sensitivity/needle_gain/%1").arg(currency), needleGain);
    st.setValue(QString("ui/sensitivity/collapse/enabled/%1").arg(currency), autoCollapseEnabled);
    st.setValue(QString("ui/sensitivity/collapse/factor/%1").arg(currency), autoCollapseFactor);
    st.setValue(QString("ui/sensitivity/spike/enabled/%1").arg(currency), spikeExpandEnabled);
    st.setValue(QString("ui/sensitivity/spike/threshold/%1").arg(currency), spikeExpandThreshold);
    st.setValue(QString("ui/sensitivity/min_range_pct/%1").arg(currency), minRangePctOfPrice);
    st.setValue(QString("ui/sensitivity/spike/expand_factor/%1").arg(currency), autoExpandFactor);
    st.sync();
}
