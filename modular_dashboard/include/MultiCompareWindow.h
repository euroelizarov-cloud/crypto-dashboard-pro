#pragma once
#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QtCharts/QChartView>
#include <QtCharts/QAbstractAxis>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include "DynamicSpeedometerCharts.h"

QT_BEGIN_NAMESPACE
class QListWidget; class QComboBox; class QCheckBox; class QPushButton; class QSpinBox; class QTimer; class QDoubleSpinBox;
QT_END_NAMESPACE

class MultiCompareWindow : public QMainWindow {
    Q_OBJECT
public:
    enum class NormMode { FromStartPct, MinMax01, ZScore };
    explicit MultiCompareWindow(QWidget* parent=nullptr);
    void setSources(const QMap<QString, DynamicSpeedometerCharts*>& widgets);
private slots:
    void refreshChart();
    void onAutoToggle(bool on);
    void onThemeChanged();
    void onLineWidthChanged(double);
private:
    // UI
    QChart* chart=nullptr; QChartView* view=nullptr;
    QListWidget* lstSymbols=nullptr; QComboBox* cmbWindow=nullptr; QComboBox* cmbNorm=nullptr; QComboBox* cmbStep=nullptr; QCheckBox* chkSmooth=nullptr; QComboBox* cmbInterp=nullptr; QCheckBox* chkLag=nullptr; QCheckBox* chkAuto=nullptr; QSpinBox* spnAutoSec=nullptr; QDoubleSpinBox* spnLineWidth=nullptr; QComboBox* cmbTheme=nullptr; QPushButton* btnRefresh=nullptr; QPushButton* btnAll=nullptr; QPushButton* btnNone=nullptr;
    QTimer* autoTimer=nullptr;
    // Data
    QMap<QString, QPointer<DynamicSpeedometerCharts>> sources; // upper -> widget
    // Helpers
    static double emaSmooth(double prev, double cur, double alpha) { return prev*(1.0-alpha) + cur*alpha; }
    QVector<QColor> currentPalette() const;
    void applyThemeStyling(QAbstractAxis* axX, QValueAxis* axY);
    // Axes management
    QDateTimeAxis* m_axisXTime = nullptr;
    QValueAxis* m_axisY = nullptr;
    void ensureAxes();
    QString pickTimeFormat(int windowSec) const;
    int pickXTicks(int windowSec) const;
    int pickStep(int windowSec) const; // adaptive resample step for 'Auto'
    // Interaction helpers
    bool eventFilter(QObject* watched, QEvent* event) override;
    QMap<QLineSeries*, QString> m_seriesToSymbol;              // reverse mapping for hit test
    QMap<QString, QVector<QPointF>> m_lastResampledRaw;        // per-symbol raw resampled points (x=ms, y=price)
};
