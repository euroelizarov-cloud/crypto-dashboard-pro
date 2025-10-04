#pragma once
#include <QWidget>
#include "MarketAnalyzer.h"

class MarketGaugeWidget : public QWidget {
    Q_OBJECT
public:
    explicit MarketGaugeWidget(QWidget* parent=nullptr);
    void setSnapshot(const MarketSnapshot& s);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    MarketSnapshot snap;
};
