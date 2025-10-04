#include "MarketOverviewWindow.h"
#include <QListWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <QSplitter>
#include <QLabel>

MarketOverviewWindow::MarketOverviewWindow(MarketAnalyzer* a, const QStringList& allSymbols, QWidget* parent)
    : QMainWindow(parent), analyzer(a) {
    setWindowTitle(tr("Обзор рынка")); resize(720, 420);
    auto* central = new QWidget(this); setCentralWidget(central);
    auto* layout = new QHBoxLayout(central);

    gauge = new MarketGaugeWidget(this);

    // Sidebar settings
    auto* panel = new QWidget(this); auto* pv = new QVBoxLayout(panel);
    pv->addWidget(new QLabel(tr("Интервал")));
    cmbWindow = new QComboBox(panel);
    cmbWindow->addItem(tr("5 минут"), 300);
    cmbWindow->addItem(tr("15 минут"), 900);
    cmbWindow->addItem(tr("1 час"), 3600);
    cmbWindow->addItem(tr("4 часа"), 14400);
    cmbWindow->setCurrentIndex(1);
    pv->addWidget(cmbWindow);

    pv->addWidget(new QLabel(tr("Взвешивание")));
    cmbWeight = new QComboBox(panel);
    cmbWeight->addItem(tr("Равное"), int(MarketAnalyzer::Weighting::Equal));
    cmbWeight->addItem(tr("Обратное волатильности"), int(MarketAnalyzer::Weighting::InverseVolatility));
    pv->addWidget(cmbWeight);

    chkIncludeBTC = new QCheckBox(tr("Учитывать BTC"), panel); chkIncludeBTC->setChecked(true); pv->addWidget(chkIncludeBTC);

    pv->addWidget(new QLabel(tr("Исключить активы")));
    lstSymbols = new QListWidget(panel); lstSymbols->setSelectionMode(QAbstractItemView::MultiSelection);
    pv->addWidget(lstSymbols, 1);

    btnApply = new QPushButton(tr("Применить"), panel); pv->addWidget(btnApply);
    pv->addStretch();

    layout->addWidget(gauge, 1);
    layout->addWidget(panel);

    setSymbols(allSymbols);

    // Connect analyzer updates to gauge
    connect(analyzer, &MarketAnalyzer::snapshotUpdated, this, [this](const MarketSnapshot& s){ gauge->setSnapshot(s); });

    connect(btnApply, &QPushButton::clicked, this, &MarketOverviewWindow::applySettings);

    applySettings();
}

void MarketOverviewWindow::setSymbols(const QStringList& allSymbols) {
    lstSymbols->clear();
    for (const auto& s : allSymbols) {
        if (s.startsWith("@")) continue; // skip pseudo in selection UI
        auto* item = new QListWidgetItem(s, lstSymbols);
        item->setSelected(false);
    }
}

void MarketOverviewWindow::applySettings() {
    MarketAnalyzer::Config cfg = analyzer->config();
    cfg.windowSeconds = cmbWindow->currentData().toInt();
    cfg.includeBTC = chkIncludeBTC->isChecked();
    cfg.weighting = static_cast<MarketAnalyzer::Weighting>(cmbWeight->currentData().toInt());
    cfg.excluded.clear();
    for (int i=0;i<lstSymbols->count();++i) {
        auto* it = lstSymbols->item(i);
        if (it->isSelected()) cfg.excluded.insert(it->text().toUpper());
    }
    analyzer->setConfig(cfg);
}
