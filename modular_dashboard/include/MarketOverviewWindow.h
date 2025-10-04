#pragma once
#include <QMainWindow>
#include <QSet>
#include <QStringList>
#include "MarketAnalyzer.h"
#include "MarketGaugeWidget.h"

class QListWidget; class QComboBox; class QCheckBox; class QPushButton;

class MarketOverviewWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MarketOverviewWindow(MarketAnalyzer* analyzer, const QStringList& allSymbols, QWidget* parent=nullptr);
    void setSymbols(const QStringList& allSymbols);
private slots:
    void applySettings();
private:
    MarketAnalyzer* analyzer;
    MarketGaugeWidget* gauge;
    // Controls
    QComboBox* cmbWindow; QComboBox* cmbWeight; QCheckBox* chkIncludeBTC; QListWidget* lstSymbols; QPushButton* btnApply;
};
