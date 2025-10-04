#include "MultiCompareWindow.h"
#include <QListWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QLabel>
#include <QDateTime>
#include <QMouseEvent>
#include <QToolTip>
#include <algorithm>
#include <cmath>

MultiCompareWindow::MultiCompareWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Сравнение графиков (норм.)")); resize(980, 600);
    auto* central = new QWidget(this); setCentralWidget(central);
    auto* layout = new QHBoxLayout(central);

    // Left panel chart
    chart = new QChart(); chart->legend()->setVisible(true); chart->setAnimationOptions(QChart::NoAnimation);
    view = new QChartView(chart); view->setRenderHint(QPainter::Antialiasing); view->setMinimumSize(640, 480);
    view->setMouseTracking(true);
    view->installEventFilter(this);
    layout->addWidget(view, 1);

    // Right panel controls
    auto* panel = new QWidget(this); auto* pv = new QVBoxLayout(panel);
    pv->addWidget(new QLabel(tr("Интервал")));
    cmbWindow = new QComboBox(panel);
    cmbWindow->addItem(tr("30 минут"), 30*60);
    cmbWindow->addItem(tr("1 час"),    60*60);
    cmbWindow->addItem(tr("2 часа"),   2*60*60);
    cmbWindow->addItem(tr("4 часа"),   4*60*60);
    cmbWindow->addItem(tr("12 часов"), 12*60*60);
    cmbWindow->addItem(tr("24 часа"),  24*60*60);
    cmbWindow->addItem(tr("48 часов"), 48*60*60);
    cmbWindow->setCurrentIndex(2);
    pv->addWidget(cmbWindow);

    pv->addWidget(new QLabel(tr("Нормализация")));
    cmbNorm = new QComboBox(panel);
    cmbNorm->addItem(tr("От старта, %"), int(NormMode::FromStartPct));
    cmbNorm->addItem(tr("Min-Max 0..1"), int(NormMode::MinMax01));
    cmbNorm->addItem(tr("Z-Score"), int(NormMode::ZScore));
    pv->addWidget(cmbNorm);

    pv->addWidget(new QLabel(tr("Шаг дискретизации")));
    cmbStep = new QComboBox(panel);
    cmbStep->addItem(tr("Auto"), -1);
    cmbStep->addItem("5s", 5);
    cmbStep->addItem("10s", 10);
    cmbStep->addItem("30s", 30);
    cmbStep->addItem("60s", 60);
    cmbStep->addItem("5m", 300);
    cmbStep->setCurrentIndex(0);
    pv->addWidget(cmbStep);

    chkSmooth = new QCheckBox(tr("Сглаживание (EMA)"), panel); chkSmooth->setChecked(true); pv->addWidget(chkSmooth);
    // Interpolation mode
    pv->addWidget(new QLabel(tr("Интерполяция")));
    cmbInterp = new QComboBox(panel);
    cmbInterp->addItem(tr("Держать последнюю"), 0); // hold-last (step)
    cmbInterp->addItem(tr("Линейная"), 1);          // linear
    pv->addWidget(cmbInterp);
    // Lag compensation
    chkLag = new QCheckBox(tr("Компенсация лага провайдера"), panel); chkLag->setChecked(false); pv->addWidget(chkLag);

    // Visual controls
    pv->addWidget(new QLabel(tr("Толщина линий")));
    spnLineWidth = new QDoubleSpinBox(panel); spnLineWidth->setRange(0.5, 6.0); spnLineWidth->setSingleStep(0.5); spnLineWidth->setValue(2.0); pv->addWidget(spnLineWidth);

    pv->addWidget(new QLabel(tr("Тема графика")));
    cmbTheme = new QComboBox(panel);
    cmbTheme->addItem(tr("Светлая"), 0);
    cmbTheme->addItem(tr("Тёмная"), 1);
    cmbTheme->addItem(tr("Контрастная"), 2);
    pv->addWidget(cmbTheme);
    chkAuto = new QCheckBox(tr("Авто-обновление"), panel); chkAuto->setChecked(true); pv->addWidget(chkAuto);
    spnAutoSec = new QSpinBox(panel); spnAutoSec->setRange(1, 60); spnAutoSec->setValue(5); pv->addWidget(spnAutoSec);

    lstSymbols = new QListWidget(panel); lstSymbols->setSelectionMode(QAbstractItemView::MultiSelection);
    pv->addWidget(lstSymbols, 1);

    auto* rowBtns = new QWidget(panel); auto* hb = new QHBoxLayout(rowBtns); hb->setContentsMargins(0,0,0,0);
    btnAll = new QPushButton(tr("Выбрать все"), rowBtns); btnNone = new QPushButton(tr("Снять все"), rowBtns);
    hb->addWidget(btnAll); hb->addWidget(btnNone); pv->addWidget(rowBtns);

    btnRefresh = new QPushButton(tr("Обновить"), panel); pv->addWidget(btnRefresh);
    pv->addStretch();
    layout->addWidget(panel);

    autoTimer = new QTimer(this);
    connect(autoTimer, &QTimer::timeout, this, &MultiCompareWindow::refreshChart);
    connect(chkAuto, &QCheckBox::toggled, this, &MultiCompareWindow::onAutoToggle);
    connect(spnAutoSec, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){ if (chkAuto->isChecked()) { autoTimer->start(v*1000); } });
    connect(btnRefresh, &QPushButton::clicked, this, &MultiCompareWindow::refreshChart);
    connect(btnAll, &QPushButton::clicked, this, [this](){ for (int i=0;i<lstSymbols->count();++i) lstSymbols->item(i)->setSelected(true); refreshChart(); });
    connect(btnNone, &QPushButton::clicked, this, [this](){ for (int i=0;i<lstSymbols->count();++i) lstSymbols->item(i)->setSelected(false); refreshChart(); });

    connect(cmbTheme, &QComboBox::currentIndexChanged, this, &MultiCompareWindow::onThemeChanged);
    connect(spnLineWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MultiCompareWindow::onLineWidthChanged);

    // Load persisted settings
    QSettings st("crypto-dashboard-pro", "modular_dashboard");
    st.beginGroup("Tools/Compare");
    cmbTheme->setCurrentIndex(st.value("Theme", 1).toInt());
    spnLineWidth->setValue(st.value("LineWidth", 2.0).toDouble());
    cmbWindow->setCurrentIndex(st.value("WindowIdx", cmbWindow->currentIndex()).toInt());
    cmbNorm->setCurrentIndex(st.value("NormIdx", cmbNorm->currentIndex()).toInt());
    cmbStep->setCurrentIndex(st.value("StepIdx", cmbStep->currentIndex()).toInt());
    chkSmooth->setChecked(st.value("Smooth", chkSmooth->isChecked()).toBool());
    chkAuto->setChecked(st.value("Auto", chkAuto->isChecked()).toBool());
    spnAutoSec->setValue(st.value("AutoSec", spnAutoSec->value()).toInt());
    cmbInterp->setCurrentIndex(st.value("InterpIdx", cmbInterp->currentIndex()).toInt());
    chkLag->setChecked(st.value("LagComp", chkLag->isChecked()).toBool());
    st.endGroup();

    onAutoToggle(true);
}

void MultiCompareWindow::setSources(const QMap<QString, DynamicSpeedometerCharts*>& wmap) {
    sources.clear(); lstSymbols->clear();
    for (auto it=wmap.begin(); it!=wmap.end(); ++it) {
        const QString sym = it.key(); if (sym.startsWith("@")) continue; // skip pseudo
        sources[sym.toUpper()] = it.value();
        lstSymbols->addItem(sym);
    }
    // By default select all
    for (int i=0;i<lstSymbols->count();++i) lstSymbols->item(i)->setSelected(true);
    refreshChart();
}

void MultiCompareWindow::onAutoToggle(bool on) {
    if (on) autoTimer->start(spnAutoSec->value()*1000); else autoTimer->stop();
}

void MultiCompareWindow::refreshChart() {
    if (!chart) return;
    chart->removeAllSeries();
    m_seriesToSymbol.clear();
    m_lastResampledRaw.clear();
    ensureAxes();

    // Collect selected
    QSet<QString> sel; for (int i=0;i<lstSymbols->count();++i) if (lstSymbols->item(i)->isSelected()) sel.insert(lstSymbols->item(i)->text().toUpper());
    if (sel.isEmpty()) return;

    int windowSec = cmbWindow->currentData().toInt();
    NormMode nm = static_cast<NormMode>(cmbNorm->currentData().toInt());
    int step = cmbStep->currentData().toInt();
    bool smooth = chkSmooth->isChecked();
    const double lineW = spnLineWidth->value();

    // Determine global time window across selected
    double tMax = 0.0; double nowRef = 0.0;
    QMap<QString, QVector<QPair<double,double>>> snaps;
    for (const auto& s : sel) {
        if (!sources.contains(s) || !sources[s]) continue;
        auto vec = sources[s]->historySnapshot();
        if (vec.isEmpty()) continue;
        snaps[s] = vec;
        tMax = std::max(tMax, vec.back().first);
    }
    if (tMax <= 0.0) return;
    nowRef = tMax; double tMin = nowRef - windowSec;

    // Optional lag offsets per source (provider-based coarse adjustment)
    QMap<QString, double> lagOffsetBySource; // seconds, + means shift to the right (delays)
    if (chkLag->isChecked()) {
        for (const auto& s : sel) {
            QPointer<DynamicSpeedometerCharts> w = sources.value(s);
            QString prov = (w ? w->property("providerName").toString() : QString());
            // Simple heuristic: Bybit +0.2s delay, Binance 0s; tweakable later
            double off = 0.0; if (prov.compare("Bybit", Qt::CaseInsensitive)==0) off = 0.2; else off = 0.0;
            lagOffsetBySource[s] = off;
        }
    }

    // Effective step: if 'Auto', choose based on window to target ~2.5k points max
    int effectiveStep = (step <= 0 ? pickStep(windowSec) : step);
    // Helper to resample per step seconds using selected interpolation
    auto resample = [&](const QString& key, const QVector<QPair<double,double>>& series) {
        QVector<QPointF> out; out.reserve(windowSec/effectiveStep + 4);
        // Apply lag offset if enabled
        double lag = lagOffsetBySource.value(key, 0.0);
        int i=0; double lastV = NAN; double lastT = NAN;
        const bool linear = (cmbInterp->currentData().toInt()==1);
        for (double t=tMin; t<=nowRef; t+=effectiveStep) {
            double tt = t - lag; // sample original series at shifted time
            while (i < series.size() && series[i].first <= tt) { lastT = series[i].first; lastV = series[i].second; ++i; }
            if (std::isnan(lastV)) continue; // no data yet
            double y = lastV;
            if (linear) {
                // interpolate to next known point if exists and within step*2
                if (i < series.size()) {
                    double t2 = series[i].first; double v2 = series[i].second;
                    if (t2 > lastT && std::isfinite(lastT)) {
                        double u = std::clamp((tt - lastT) / (t2 - lastT), 0.0, 1.0);
                        y = lastV + (v2 - lastV) * u;
                    }
                }
            }
            out.push_back(QPointF(t*1000.0, y)); // QDateTimeAxis expects ms
        }
        return out;
    };

    // Precompute min/max or baseline for normalization per series
    struct Stat { double base=0, minv=INFINITY, maxv=-INFINITY, mean=0, sd=0; };
    QMap<QString, Stat> stats;
    for (auto it=snaps.begin(); it!=snaps.end(); ++it) {
    auto pts = resample(it.key(), it.value()); if (pts.isEmpty()) continue;
        Stat st; st.base = pts.front().y();
        double sum=0.0, sum2=0.0; int n=0;
        for (const auto& p : pts) { st.minv = std::min(st.minv, p.y()); st.maxv = std::max(st.maxv, p.y()); sum+=p.y(); sum2+=p.y()*p.y(); ++n; }
        if (n>0) { st.mean = sum/n; double var = std::max(0.0, sum2/n - st.mean*st.mean); st.sd = std::sqrt(var); }
        stats[it.key()] = st;
    }

    // Series creation
    double globalMin=INFINITY, globalMax=-INFINITY;
    int colorIdx=0; auto nextColor = [&](){ static QVector<QColor> cols={QColor(220,90,90),QColor(90,200,120),QColor(90,150,220),QColor(220,180,90),QColor(180,100,220),QColor(120,200,200)}; return cols[(colorIdx++)%cols.size()]; };
    for (auto it=snaps.begin(); it!=snaps.end(); ++it) {
    const QString sym = it.key(); auto pts = resample(sym, it.value()); if (pts.isEmpty()) continue;
    // Remember raw resampled for hit testing (x=ms, y=price)
    m_lastResampledRaw[sym] = pts;
        QLineSeries* s = new QLineSeries(); s->setName(sym);
        // Color from theme palette
        auto palette = currentPalette(); s->setColor(palette[(colorIdx++)%palette.size()]);
        QPen pen = s->pen(); pen.setWidthF(lineW); pen.setCosmetic(true); s->setPen(pen);
        double prev = 0.0; bool havePrev=false; double alpha = 0.2;
        const Stat st = stats.value(sym);
        for (auto p : pts) {
            double y = p.y(); double v=0.0;
            switch (nm) {
                case NormMode::FromStartPct: v = (st.base>0? (y/st.base - 1.0)*100.0 : 0.0); break;
                case NormMode::MinMax01: v = (st.maxv>st.minv? (y-st.minv)/(st.maxv-st.minv) : 0.0); break;
                case NormMode::ZScore: v = (st.sd>1e-9? (y-st.mean)/st.sd : 0.0); break;
            }
            if (smooth) { v = havePrev? emaSmooth(prev, v, alpha) : v; prev = v; havePrev=true; }
            s->append(p.x(), v);
            globalMin = std::min(globalMin, v); globalMax = std::max(globalMax, v);
        }
        chart->addSeries(s);
        s->attachAxis(m_axisXTime);
        s->attachAxis(m_axisY);
        m_seriesToSymbol[s] = sym;
    }
    if (!std::isfinite(globalMin) || !std::isfinite(globalMax)) return;
    // Nice expand
    double pad = (globalMax-globalMin)*0.05 + 1e-6;
    m_axisXTime->setRange(QDateTime::fromSecsSinceEpoch((qint64)tMin), QDateTime::fromSecsSinceEpoch((qint64)nowRef));
    m_axisY->setRange(globalMin - pad, globalMax + pad);
    // Adaptive ticks and label format
    m_axisXTime->setFormat(pickTimeFormat(windowSec));
    m_axisXTime->setTickCount(pickXTicks(windowSec));

    // Persist settings after a refresh
    QSettings st("crypto-dashboard-pro", "modular_dashboard");
    st.beginGroup("Tools/Compare");
    st.setValue("Theme", cmbTheme->currentIndex());
    st.setValue("LineWidth", spnLineWidth->value());
    st.setValue("WindowIdx", cmbWindow->currentIndex());
    st.setValue("NormIdx", cmbNorm->currentIndex());
    st.setValue("StepIdx", cmbStep->currentIndex());
    st.setValue("Smooth", chkSmooth->isChecked());
    st.setValue("Auto", chkAuto->isChecked());
    st.setValue("AutoSec", spnAutoSec->value());
    st.endGroup();
    // Persist new options
    st.beginGroup("Tools/Compare");
    st.setValue("InterpIdx", cmbInterp->currentIndex());
    st.setValue("LagComp", chkLag->isChecked());
    st.endGroup();
}

void MultiCompareWindow::onThemeChanged() {
    refreshChart();
}

void MultiCompareWindow::onLineWidthChanged(double) {
    refreshChart();
}

QVector<QColor> MultiCompareWindow::currentPalette() const {
    switch (cmbTheme ? cmbTheme->currentIndex() : 1) {
        case 0: // light
            return { QColor(52,152,219), QColor(231,76,60), QColor(46,204,113), QColor(155,89,182), QColor(241,196,15), QColor(149,165,166) };
        case 2: // high contrast
            return { QColor("#00FFFF"), QColor("#FF00FF"), QColor("#FFFF00"), QColor("#00FF00"), QColor("#FF0000"), QColor("#FFFFFF") };
        case 1: default: // dark
            return { QColor(90,200,250), QColor(255,99,132), QColor(75,192,192), QColor(153,102,255), QColor(255,206,86), QColor(201,203,207) };
    }
}

void MultiCompareWindow::applyThemeStyling(QAbstractAxis* axX, QValueAxis* axY) {
    const int theme = cmbTheme ? cmbTheme->currentIndex() : 1; // 0 light, 1 dark, 2 contrast
    QPalette vp = view->palette();
    QColor bg, grid, text;
    if (theme == 0) { // light
        bg = QColor("#FFFFFF"); text = QColor("#222222"); grid = QColor(0,0,0,40);
    } else if (theme == 2) { // contrast
        bg = QColor("#000000"); text = QColor("#FFFFFF"); grid = QColor(255,255,255,80);
    } else { // dark
        bg = QColor("#111418"); text = QColor("#D0D3D4"); grid = QColor(255,255,255,40);
    }
    chart->setBackgroundVisible(true);
    chart->setBackgroundBrush(bg);
    view->setStyleSheet(QString("QWidget { background: %1; } ").arg(bg.name()));
    // QAbstractAxis does not have grid color; handle Y grid via value axis
    axY->setGridLineColor(grid);
    axX->setLabelsColor(text); axY->setLabelsColor(text);
    axX->setTitleBrush(QBrush(text)); axY->setTitleBrush(QBrush(text));
    chart->legend()->setLabelColor(text);
}

void MultiCompareWindow::ensureAxes() {
    if (!m_axisXTime) {
        m_axisXTime = new QDateTimeAxis();
        m_axisXTime->setFormat("HH:mm");
        chart->addAxis(m_axisXTime, Qt::AlignBottom);
    }
    if (!m_axisY) {
        m_axisY = new QValueAxis();
        m_axisY->setLabelFormat("%.2f");
        chart->addAxis(m_axisY, Qt::AlignLeft);
    }
    applyThemeStyling(m_axisXTime, m_axisY);
}

QString MultiCompareWindow::pickTimeFormat(int windowSec) const {
    if (windowSec <= 3600) return "HH:mm:ss";         // up to 1h show seconds
    if (windowSec <= 6*3600) return "HH:mm";          // up to 6h
    if (windowSec <= 48*3600) return "dd.MM HH:mm";   // up to 2 days
    return "dd.MM";                                    // larger
}

int MultiCompareWindow::pickXTicks(int windowSec) const {
    // Aim for ~6-8 ticks depending on range
    if (windowSec <= 1800) return 7;       // 30 min
    if (windowSec <= 3600) return 7;       // 1h
    if (windowSec <= 3*3600) return 7;     // 3h
    if (windowSec <= 6*3600) return 8;     // 6h
    if (windowSec <= 24*3600) return 9;    // 24h
    return 10;                             // larger
}

int MultiCompareWindow::pickStep(int windowSec) const {
    // Keep roughly <= 2500 points. For 48h (172800s) => ~70s. Snap to friendly steps.
    int targetPts = 2500;
    int raw = std::max(1, windowSec / targetPts);
    // Snap to [1,2,5,10,15,30,60,120,300,600]
    const int steps[] = {1,2,5,10,15,30,60,120,300,600,900,1800};
    for (int s : steps) if (raw <= s) return s;
    return 3600; // worst-case fallback
}

bool MultiCompareWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton && chart && m_axisXTime) {
            // Map mouse position to chart value space
            QPointF scenePos = view->mapToScene(me->pos());
            QPointF chartPos = chart->mapFromScene(scenePos);
            QPointF valuePt = chart->mapToValue(chartPos, nullptr);
            const double xMs = valuePt.x();
            const double yNorm = valuePt.y();
            // Search nearest series by Euclidean distance in chart value space (x in ms, y is normalized chart value)
            QLineSeries* bestSeries = nullptr; int bestIdx = -1; double bestDist = 1e300;
            for (auto* s : chart->series()) {
                auto* ls = qobject_cast<QLineSeries*>(s);
                if (!ls) continue;
                const auto pts = ls->points(); if (pts.isEmpty()) continue;
                // binary search by x to get neighbors
                int lo=0, hi=pts.size()-1, mid=0;
                while (lo <= hi) { mid=(lo+hi)/2; double xm=pts[mid].x(); if (xm < xMs) lo=mid+1; else hi=mid-1; }
                auto consider = [&](int idx){ if (idx<0 || idx>=pts.size()) return; double dx = pts[idx].x() - xMs; double dy = pts[idx].y() - yNorm; double d2 = dx*dx + dy*dy; if (d2 < bestDist) { bestDist = d2; bestSeries = ls; bestIdx = idx; } };
                consider(std::max(0, hi)); consider(std::min(int(pts.size()-1), lo));
            }
            if (bestSeries && bestIdx >= 0) {
                const QString sym = m_seriesToSymbol.value(bestSeries);
                // Retrieve raw price for that time if available
                double showPrice = 0.0; double xSel = qobject_cast<QLineSeries*>(bestSeries)->points()[bestIdx].x();
                if (m_lastResampledRaw.contains(sym)) {
                    const auto& raw = m_lastResampledRaw[sym];
                    int lo=0, hi=raw.size()-1, mid=0;
                    while (lo <= hi) { mid=(lo+hi)/2; double xm=raw[mid].x(); if (xm < xSel) lo=mid+1; else hi=mid-1; }
                    auto pick = [&](int idx){ if (idx<0 || idx>=raw.size()) return; showPrice = raw[idx].y(); };
                    // Prefer exact neighbor closest in x
                    int idx1 = std::max(0, hi); int idx2 = std::min(int(raw.size()-1), lo);
                    double d1 = (idx1>=0 && idx1<raw.size())? std::abs(raw[idx1].x()-xSel) : 1e300;
                    double d2 = (idx2>=0 && idx2<raw.size())? std::abs(raw[idx2].x()-xSel) : 1e300;
                    pick(d1 <= d2 ? idx1 : idx2);
                }
                QDateTime dt = QDateTime::fromMSecsSinceEpoch((qint64)std::llround(xSel));
                QString text = QString("%1\n%2\n%3: %4")
                               .arg(tr("Символ: ") + (sym.isEmpty()? bestSeries->name() : sym))
                               .arg(tr("Время: ") + dt.toString("dd.MM.yyyy HH:mm:ss"))
                               .arg(tr("Значение"))
                               .arg(showPrice, 0, 'f', 6);
                QToolTip::showText(me->globalPosition().toPoint(), text, view);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
