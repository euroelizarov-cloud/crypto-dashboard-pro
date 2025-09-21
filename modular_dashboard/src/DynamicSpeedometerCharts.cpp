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
    axisX = new QValueAxis(chart); axisY = new QValueAxis(chart);
    axisX->setVisible(false); axisX->setTickCount(2); axisY->setLabelFormat("%.2f");
    chart->addAxis(axisX, Qt::AlignBottom); chart->addAxis(axisY, Qt::AlignLeft);
    seriesNormal->attachAxis(axisX); seriesNormal->attachAxis(axisY);
    seriesRatio->attachAxis(axisX); seriesRatio->attachAxis(axisY); seriesRatio->setVisible(false);

    chartView = new QChartView(chart, this); chartView->setRenderHint(QPainter::Antialiasing); chartView->setVisible(false);
    chartView->setGeometry(rect());

    renderTimer = new QTimer(this); renderTimer->setInterval(16);
    connect(renderTimer, &QTimer::timeout, this, &DynamicSpeedometerCharts::onRenderTimeout); renderTimer->start();
    cacheUpdateTimer = new QTimer(this); cacheUpdateTimer->setInterval(300);
    connect(cacheUpdateTimer, &QTimer::timeout, this, &DynamicSpeedometerCharts::cacheChartData); cacheUpdateTimer->start();
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
    history.push_back({timestamp, price}); if (history.size()>static_cast<size_t>(cacheSize)) history.pop_front();
    if (currency=="BTC") { btcPrice=price; btcRatioHistory.push_back({timestamp, 1.0}); }
    else if (btc>0) { btcRatioHistory.push_back({timestamp, price/btc}); }
    if (btcRatioHistory.size()>static_cast<size_t>(cacheSize)) btcRatioHistory.pop_front();
    updateVolatility(); updateBounds(price);
    double scaled=50; if (cachedMinVal && cachedMaxVal && cachedMaxVal.value() > cachedMinVal.value()) { scaled = (price-cachedMinVal.value())/(cachedMaxVal.value()-cachedMinVal.value())*100; scaled = std::clamp(scaled, 0.0, 100.0); }
    animation->stop(); animation->setStartValue(_value); animation->setEndValue(scaled); animation->start();
    dataNeedsRedraw = true;
}

void DynamicSpeedometerCharts::setCurrencyName(const QString& name) { currency = name; if (modeView!="speedometer") updateChartSeries(); else update(); }

void DynamicSpeedometerCharts::paintEvent(QPaintEvent*) {
    if (modeView=="speedometer") {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); drawSpeedometer(p);
    }
}

void DynamicSpeedometerCharts::resizeEvent(QResizeEvent*) { if (chartView) chartView->setGeometry(rect()); }

void DynamicSpeedometerCharts::mousePressEvent(QMouseEvent* e) {
    if (e->button()==Qt::LeftButton) {
        if (modeView=="speedometer") setModeView("line_chart"); else if (modeView=="line_chart") setModeView("btc_ratio"); else setModeView("speedometer");
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
    QAction* stModern  = addStyle("ModernTicks");
    QAction* stCircle  = addStyle("Classic Pro");
    QAction* stGauge   = addStyle("Gauge");
    QAction* stRing    = addStyle("Modern Scale");
    auto checkByCurrent = [&](){
        stClassic->setChecked(style==SpeedometerStyle::Classic);
        stNeon->setChecked(style==SpeedometerStyle::NeonGlow);
        stMinimal->setChecked(style==SpeedometerStyle::Minimal);
        stModern->setChecked(style==SpeedometerStyle::ModernTicks);
        stCircle->setChecked(style==SpeedometerStyle::Circle);
        stGauge->setChecked(style==SpeedometerStyle::Gauge);
        stRing->setChecked(style==SpeedometerStyle::Ring);
    }; checkByCurrent();
    auto applyStyle = [&](SpeedometerStyle s, const QString& name){ setSpeedometerStyle(s); emit styleSelected(currency, name); };
    QObject::connect(stClassic, &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::Classic, "Classic"); });
    QObject::connect(stNeon,    &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::NeonGlow, "NeonGlow"); });
    QObject::connect(stMinimal, &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::Minimal, "Minimal"); });
    QObject::connect(stModern,  &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::ModernTicks, "ModernTicks"); });
    QObject::connect(stCircle,  &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::Circle, "Classic Pro"); });
    QObject::connect(stGauge,   &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::Gauge, "Gauge"); });
    QObject::connect(stRing,    &QAction::triggered, this, [&,this](){ applyStyle(SpeedometerStyle::Ring, "Modern Scale"); });
    menu.addSeparator();
    QAction* renameAct = menu.addAction("Rename ticker...");
    connect(renameAct, &QAction::triggered, this, [this](){ emit requestRename(currency); });
    menu.exec(e->globalPos());
}

void DynamicSpeedometerCharts::onRenderTimeout() { if (dataNeedsRedraw && (modeView != "speedometer")) { dataNeedsRedraw=false; updateChartSeries(); } }

void DynamicSpeedometerCharts::setTimeScale(const QString& scale) { if (timeScales.contains(scale)) { currentScale=scale; cacheChartData(); updateChartSeries(); } }

void DynamicSpeedometerCharts::setModeView(const QString& mv) {
    modeView = mv; bool charts = (modeView!="speedometer"); chartView->setVisible(charts); if (charts) updateChartSeries(); else update();
}

void DynamicSpeedometerCharts::cacheChartData() { cachedProcessedHistory = processHistory(false); cachedProcessedBtcRatio = processHistory(true); dataNeedsRedraw = true; }

std::vector<double> DynamicSpeedometerCharts::processHistory(bool useBtcRatio) {
    double now = QDateTime::currentMSecsSinceEpoch()/1000.0; double window = timeScales[currentScale]; double cutoff = now - window;
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

void DynamicSpeedometerCharts::updateVolatility() {
    if (history.size()<2) { volatility=0.0; return; }
    size_t window = std::min(static_cast<size_t>(volatilityWindow), history.size());
    std::vector<double> prices; prices.reserve(window); for (auto it = history.end()-window; it!=history.end(); ++it) prices.push_back(it->second);
    std::vector<double> rets; rets.reserve(prices.size()-1); for (size_t i=1;i<prices.size();++i) rets.push_back(prices[i-1] ? std::abs((prices[i]-prices[i-1])/prices[i-1]) : 0.0);
    volatility = rets.empty()? 0.0 : (std::accumulate(rets.begin(), rets.end(), 0.0)/rets.size())*100.0;
}

void DynamicSpeedometerCharts::updateBounds(double price) {
    if (scaling.mode == ScalingMode::Fixed) {
        cachedMinVal = scaling.fixedMin; cachedMaxVal = scaling.fixedMax; return;
    } else if (scaling.mode == ScalingMode::Manual && cachedMinVal && cachedMaxVal) {
        // Keep existing manual bounds, only update if price exceeds them
        if (price < cachedMinVal.value()) cachedMinVal = price;
        if (price > cachedMaxVal.value()) cachedMaxVal = price; return;
    }
    // Adaptive mode (default)
    if (!cachedMinVal || !cachedMaxVal) { cachedMinVal=price*minInit; cachedMaxVal=price*maxInit; return; }
    cachedMinVal = cachedMinVal.value()*minFactor + price*(1.0-minFactor);
    cachedMaxVal = cachedMaxVal.value()*maxFactor + price*(1.0-maxFactor);
    if (price < cachedMinVal.value()) cachedMinVal=price;
    if (price > cachedMaxVal.value()) cachedMaxVal=price;
    double range = cachedMaxVal.value() - cachedMinVal.value(); double epsilon = std::max(1e-10, range*1e-5);
    if (range < epsilon) { double center = (cachedMaxVal.value()+cachedMinVal.value())/2.0; cachedMinVal=center-epsilon/2.0; cachedMaxVal=center+epsilon/2.0; }
}

void DynamicSpeedometerCharts::drawSpeedometer(QPainter& painter) {
    int w = width(), h = height(); int size = std::min(w,h) - 20; QRect rect((w-size)/2,(h-size)/2,size,size);
    
    // Use theme background
    painter.fillRect(0,0,w,h,themeColors.background);

    auto drawCommonTexts = [&](QColor textColor = QColor()){
        // Use theme text color if no override
        if (!textColor.isValid()) textColor = themeColors.text;
        painter.setPen(textColor); painter.setFont(QFont("Arial",21, QFont::Bold)); painter.drawText(QRect(0,h/2-50,w,20), Qt::AlignCenter, currency);
        if (!history.empty()) { double currentPrice = history.back().second; painter.setFont(QFont("Arial",21, QFont::Bold)); painter.drawText(QRect(0,h/2+20,w,20), Qt::AlignCenter, QLocale().toString(currentPrice,'f',3)); }
        painter.setFont(QFont("Arial",8)); painter.setPen(Qt::yellow); painter.drawText(QRect(1,h-50,w-10,20), Qt::AlignLeft|Qt::AlignBottom, QString("Trades: %1").arg(history.size()));
        painter.setFont(QFont("Arial",6)); painter.setPen(textColor); painter.drawText(QRect(1,1,w-10,20), Qt::AlignLeft|Qt::AlignTop, QString("Volatility: %1%\n").arg(volatility,0,'f',4));
    };

    const double angle = 45 + (270 * _value / 100);

    switch (style) {
        case SpeedometerStyle::Classic: {
            painter.setPen(QPen(themeColors.arcBase, 4)); painter.drawArc(rect, 45*16, 270*16);
            int warn = thresholds.warn, danger = thresholds.danger; if (!thresholds.enabled) { warn=70; danger=90; }
            struct Zone{double s,e; QColor c;}; 
            std::vector<Zone> zones={{0,double(warn),themeColors.zoneGood},{double(warn),double(danger),themeColors.zoneWarn},{double(danger),100.0,themeColors.zoneDanger}};
            for (const auto& z:zones) { painter.setPen(QPen(z.c,8)); double span=(z.e-z.s)*270/100; double za=45+(270*z.s/100); painter.drawArc(rect, int(za*16), int(span*16)); }
            QColor needle = (!thresholds.enabled)? themeColors.needleNormal : (_value>=danger? themeColors.zoneDanger : (_value>=warn? themeColors.zoneWarn : themeColors.needleNormal));
            painter.setPen(QPen(needle,2)); painter.translate(w/2,h/2); painter.rotate(angle);
            painter.drawLine(0,0,size/2-15,0); painter.setBrush(themeColors.zoneDanger); painter.drawEllipse(QPoint(size/2-15,0), 18,8); painter.resetTransform();
            drawCommonTexts();
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
            painter.setPen(QPen(themeColors.arcBase, 2)); painter.drawArc(rect, 45*16, 270*16);
            painter.save(); painter.translate(w/2,h/2);
            for (int v=0; v<=100; v+=10) {
                painter.save(); double a = 45 + 270.0*v/100.0; painter.rotate(a);
                int len = (v%20==0) ? 16 : 8; int t = size/2 - 10; painter.setPen(QPen(themeColors.arcBase.lighter(), (v%20==0)?3:1)); painter.drawLine(t-len,0,t,0);
                painter.restore();
            }
            painter.restore();
            QColor needle = (!thresholds.enabled)? themeColors.needleNormal : (_value>=thresholds.danger? themeColors.zoneDanger : (_value>=thresholds.warn? themeColors.zoneWarn : themeColors.zoneGood));
            painter.setPen(QPen(needle, 4)); painter.translate(w/2,h/2); painter.rotate(angle); painter.drawLine(0,0,size/2-18,0); painter.resetTransform();
            drawCommonTexts();
            break;
        }
        case SpeedometerStyle::Circle: {
            // Classic Pro: Traditional speedometer with modern refinements
            // Main arc with subtle gradient
            painter.setPen(QPen(themeColors.arcBase, 4)); 
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
                painter.save(); painter.translate(w/2, h/2); 
                double a = 45 + 270.0*v/100.0; painter.rotate(a);
                int outer = size/2 - 8, inner = size/2 - 13;
                painter.drawLine(outer, 0, inner, 0); painter.restore();
                
                a = 45 + 270.0*(v+10)/100.0; painter.rotate(a-270.0*v/100.0);
                painter.drawLine(outer, 0, inner, 0); painter.restore();
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
            painter.setPen(QPen(needleColor, 3)); 
            painter.translate(w/2,h/2); painter.rotate(angle);
            painter.drawLine(0,0,size/2-20,0); 
            
            // Red needle tip
            painter.setBrush(themeColors.zoneDanger); painter.setPen(Qt::NoPen);
            painter.drawEllipse(QPoint(size/2-20,0), 12, 6); 
            
            // Center hub
            painter.setBrush(themeColors.text); 
            painter.drawEllipse(QPoint(0,0), 8, 8);
            painter.resetTransform();
            
            drawCommonTexts();
            break;
        }
        case SpeedometerStyle::Gauge: {
            // Clean gauge with single arc and value indicator
            painter.setPen(QPen(themeColors.arcBase, 3)); painter.drawArc(rect.adjusted(5,5,-5,-5), 45*16, 270*16);
            // Value indicator line at current position
            QColor indicatorColor = (!thresholds.enabled)? themeColors.needleNormal : (_value>=thresholds.danger? themeColors.zoneDanger : (_value>=thresholds.warn? themeColors.zoneWarn : themeColors.zoneGood));
            painter.setPen(QPen(indicatorColor, 6, Qt::SolidLine, Qt::RoundCap));
            double indicatorAngle = 45 + (270 * _value / 100);
            painter.save(); painter.translate(w/2, h/2); painter.rotate(indicatorAngle);
            int startR = size/2 - 25, endR = size/2 - 10;
            painter.drawLine(startR, 0, endR, 0); painter.restore();
            // Optional: small value marks
            painter.setPen(QPen(themeColors.arcBase.lighter(), 1));
            for (int v=0; v<=100; v+=25) {
                painter.save(); painter.translate(w/2, h/2); double a = 45 + 270.0*v/100.0; painter.rotate(a);
                int t = size/2 - 8; painter.drawLine(t-4,0,t,0); painter.restore();
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
                double currentPrice = history.back().second;
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
    }
}
